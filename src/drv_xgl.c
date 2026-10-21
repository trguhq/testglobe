/*
 * testglobe - Sun XGL 3.x / X11 driver.
 * Copyright (C) 2024-2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * XGL performs the model/view transforms, clipping, back-face rejection,
 * z-buffering, strip filling, texture filtering, text and buffer switching.
 * Its installed device pipeline chooses hardware or XGL's software renderer.
 */
#include "testglob.h"
#ifdef DRV_XGL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <xgl/xgl.h>
#include "drv.h"
#include "drv_ext.h"
#include "globe.h"
#include "glob_ext.h"
#include "tex_ext.h"

static Display *xgl_display;
static Window xgl_window;
static Colormap xgl_colormap;
static Atom xgl_wm_delete;
static Atom xgl_wm_protocols;
static Xgl_sys_state xgl_system;
static Xgl_win_ras xgl_raster;
/* The raster descriptor may be queried again after window creation. */
static Xgl_X_window xgl_window_descriptor;
static Xgl_obj_desc xgl_raster_descriptor;
static Xgl_ctx xgl_context;
static Xgl_ctx xgl_overlay;
static Xgl_trans xgl_model;
static Xgl_trans xgl_view;
static Xgl_object xgl_mipmap;
static Xgl_object xgl_tmap;
static Xgl_boolean xgl_tmap_switch[1];
static int xgl_double_buffer;
static int xgl_failed;
static int xgl_dirty = TRUE;
static int xgl_old_x;
static int xgl_old_y;
static unsigned int xgl_strip_capacity;
static Xgl_pt_f3d *xgl_strip_points;
static Xgl_color_facet *xgl_strip_facets;
static float *xgl_strip_texture_points;

static void xgl_cleanup(void)
{
    /* Closing the System State releases all its objects and their resources. */
    if (xgl_system != NULL)
    {
        xgl_close(xgl_system);
        xgl_system = NULL;
    }
    if (xgl_display != NULL)
    {
        if (xgl_window != 0) XDestroyWindow(xgl_display, xgl_window);
        if (xgl_colormap != 0) XFreeColormap(xgl_display, xgl_colormap);
        XCloseDisplay(xgl_display);
        xgl_display = NULL;
    }
    free(xgl_strip_points);
    free(xgl_strip_facets);
    free(xgl_strip_texture_points);
    xgl_strip_points = NULL;
    xgl_strip_facets = NULL;
    xgl_strip_texture_points = NULL;
    xgl_strip_capacity = 0;
    globe_free();
    texture_free();
}

static Xgl_sgn32 xgl_error_notify(Xgl_sys_state state)
{
    Xgl_error_info info;
    memset(&info, 0, sizeof(info));
    xgl_object_get(state, XGL_SYS_ST_ERROR_INFO, &info);
    fprintf(stderr, "XGL error: %s: %s (%s)\n",
            info.id != NULL ? info.id : "unknown",
            info.msg != NULL ? info.msg : "operation failed",
            info.cur_op != NULL ? info.cur_op : "unknown operator");
    /* Do not call exit/close recursively from inside the XGL library. */
    xgl_failed = TRUE;
    return 1;
}

static void xgl_checkerrors(void)
{
    if (xgl_failed) error("Sun XGL operation failed; see the XGL diagnostic");
}

void drv_close(void)
{
    globe_free();
    texture_free();
    exit(0);
}

void drv_init(int *argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (atexit(xgl_cleanup) != 0) error("Cannot register XGL cleanup");
    xgl_display = XOpenDisplay(NULL);
    if (xgl_display == NULL) error("Cannot open X display for Sun XGL");
    xgl_system = xgl_open(XGL_SYS_ST_ERROR_DETECTION, TRUE, 0);
    if (xgl_system == NULL) error("Cannot initialize Sun XGL");
    xgl_object_set(xgl_system,
                   XGL_SYS_ST_ERROR_NOTIFICATION_FUNCTION, xgl_error_notify, 0);
    xgl_checkerrors();
}

static void xgl_projection(int width, int height)
{
    Xgl_matrix_d3d matrix;
    Xgl_bounds_d3d bounds;
    double near_plane = 1.0;
    double far_plane = 10000.0;
    double cotangent = 1.0 / tan(PI / 8.0);
    memset(matrix, 0, sizeof(matrix));
    /* XGL uses row vectors. This is a 45-degree perspective projection,
     * with near mapped to VDC z=1 and far to z=0 (Y_UP_Z_TOWARD). */
    matrix[0][0] = cotangent * (double)height / (double)width;
    matrix[1][1] = cotangent;
    matrix[2][2] = near_plane / (far_plane - near_plane);
    matrix[3][2] = far_plane * near_plane / (far_plane - near_plane);
    matrix[2][3] = -1.0;
    xgl_transform_write_specific(xgl_view, matrix, 0);
    bounds.xmin = -1.0;
    bounds.xmax = 1.0;
    bounds.ymin = -1.0;
    bounds.ymax = 1.0;
    bounds.zmin = 0.0;
    bounds.zmax = 1.0;
    xgl_object_set(xgl_context,
                   XGL_CTX_VDC_MAP, XGL_VDC_MAP_ALL,
                   XGL_CTX_VDC_ORIENTATION, XGL_Y_UP_Z_TOWARD,
                   XGL_CTX_VDC_WINDOW, &bounds,
                   XGL_CTX_VIEW_CLIP_BOUNDS, &bounds, 0);
}

void drv_resize(int width, int height)
{
    /* X11 can send zero geometry while a window is being withdrawn. */
    if (width < 1 || height < 1) return;
    drv_win_width = (unsigned int)width;
    drv_win_height = (unsigned int)height;
    if (xgl_raster != NULL)
    {
        xgl_window_raster_resize(xgl_raster);
        xgl_projection(width, height);
        xgl_checkerrors();
    }
    xgl_dirty = TRUE;
}

int drv_init_window(int in_x, int in_y, int in_width, int in_height)
{
    int screen;
    XVisualInfo visual;
    XSetWindowAttributes attributes;
    Xgl_usgn32 buffers;
    Xgl_color black;
    Xgl_color white;
    unsigned long event_mask;
    screen = DefaultScreen(xgl_display);
    if (!XMatchVisualInfo(xgl_display, screen, 24, TrueColor, &visual))
        error("Sun XGL needs a 24-bit TrueColor X visual");
    if (in_x > 0) drv_win_x = (unsigned int)in_x;
    if (in_y > 0) drv_win_y = (unsigned int)in_y;
    if (in_width > 0) drv_win_width = (unsigned int)in_width;
    if (in_height > 0) drv_win_height = (unsigned int)in_height;
    xgl_colormap = XCreateColormap(xgl_display,
                                   RootWindow(xgl_display, screen),
                                   visual.visual, AllocNone);
    memset(&attributes, 0, sizeof(attributes));
    event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    attributes.colormap = xgl_colormap;
    attributes.border_pixel = 0;
    attributes.background_pixel = 0;
    attributes.event_mask = (long)event_mask;
    xgl_window = XCreateWindow(xgl_display, RootWindow(xgl_display, screen),
                               (int)drv_win_x, (int)drv_win_y,
                               drv_win_width, drv_win_height, 0,
                               visual.depth, InputOutput, visual.visual,
                               CWColormap | CWBorderPixel | CWBackPixel |
                               CWEventMask, &attributes);
    if (xgl_window == 0) error("Cannot create Sun XGL window");
    XStoreName(xgl_display, xgl_window, TESTGLOBE_NAME);
    xgl_wm_delete = XInternAtom(xgl_display, "WM_DELETE_WINDOW", False);
    xgl_wm_protocols = XInternAtom(xgl_display, "WM_PROTOCOLS", False);
    XSetWMProtocols(xgl_display, xgl_window, &xgl_wm_delete, 1);
    XMapWindow(xgl_display, xgl_window);
    XSync(xgl_display, False);

    memset(&xgl_window_descriptor, 0, sizeof(xgl_window_descriptor));
    memset(&xgl_raster_descriptor, 0, sizeof(xgl_raster_descriptor));
    xgl_window_descriptor.X_display = (void *)xgl_display;
    xgl_window_descriptor.X_screen = screen;
    xgl_window_descriptor.X_window = (Xgl_usgn32)xgl_window;
    xgl_raster_descriptor.win_ras.type = XGL_WIN_X;
    xgl_raster_descriptor.win_ras.desc = &xgl_window_descriptor;
    xgl_raster = xgl_object_create(xgl_system, XGL_WIN_RAS, &xgl_raster_descriptor,
                                   XGL_DEV_COLOR_TYPE, XGL_COLOR_RGB,
                                   XGL_WIN_RAS_BUFFERS_REQUESTED, 2, 0);
    if (xgl_raster == NULL) error("Cannot create Sun XGL raster");
    buffers = 1;
    xgl_object_get(xgl_raster, XGL_WIN_RAS_BUFFERS_ALLOCATED, &buffers);
    xgl_double_buffer = buffers > 1;
    if (xgl_double_buffer)
        xgl_object_set(xgl_raster, XGL_WIN_RAS_BUF_DRAW, 1,
                       XGL_WIN_RAS_BUF_DISPLAY, 0, 0);
    memset(&black, 0, sizeof(black));
    memset(&white, 0, sizeof(white));
    white.rgb.r = white.rgb.g = white.rgb.b = 1.0f;
    xgl_context = xgl_object_create(xgl_system, XGL_3D_CTX, NULL,
                                    XGL_CTX_DEVICE, xgl_raster,
                                    XGL_CTX_DEFERRAL_MODE, XGL_DEFER_ASAP,
                                    XGL_CTX_GEOM_DATA_IS_VOLATILE, TRUE,
                                    XGL_CTX_CLIP_PLANES,
                                      XGL_CLIP_XMIN | XGL_CLIP_XMAX |
                                      XGL_CLIP_YMIN | XGL_CLIP_YMAX |
                                      XGL_CLIP_ZMIN | XGL_CLIP_ZMAX,
                                    XGL_CTX_BACKGROUND_COLOR, &black,
                                    XGL_3D_CTX_HLHSR_MODE, XGL_HLHSR_Z_BUFFER,
                                    XGL_CTX_NEW_FRAME_ACTION,
                                      XGL_CTX_NEW_FRAME_CLEAR |
                                      XGL_CTX_NEW_FRAME_HLHSR_ACTION,
                                    XGL_CTX_SURF_FRONT_COLOR_SELECTOR,
                                      XGL_SURF_COLOR_FACET,
                                    XGL_CTX_SURF_FRONT_FILL_STYLE,
                                      XGL_SURF_FILL_SOLID,
                                    XGL_3D_CTX_SURF_FRONT_ILLUMINATION,
                                      XGL_ILLUM_NONE,
                                    XGL_3D_CTX_SURF_GEOM_NORMAL,
                                      XGL_GEOM_NORMAL_FIRST_POINTS,
                                    XGL_3D_CTX_SURF_FACE_CULL, XGL_CULL_BACK,
                                    XGL_CTX_SURF_EDGE_FLAG, FALSE, 0);
    xgl_overlay = xgl_object_create(xgl_system, XGL_2D_CTX, NULL,
                                    XGL_CTX_DEVICE, xgl_raster,
                                    XGL_CTX_DEFERRAL_MODE, XGL_DEFER_ASAP,
                                    XGL_CTX_VDC_MAP, XGL_VDC_MAP_DEVICE,
                                    XGL_CTX_STEXT_CHAR_HEIGHT, 12.0,
                                    XGL_CTX_STEXT_COLOR, &white,
                                    XGL_CTX_NEW_FRAME_ACTION,
                                      xgl_double_buffer ?
                                      XGL_CTX_NEW_FRAME_SWITCH_BUFFER : 0, 0);
    xgl_model = xgl_object_create(xgl_system, XGL_TRANS, NULL,
                                  XGL_TRANS_DATA_TYPE, XGL_DATA_DBL, 0);
    xgl_view = xgl_object_create(xgl_system, XGL_TRANS, NULL,
                                 XGL_TRANS_DATA_TYPE, XGL_DATA_DBL, 0);
    if (xgl_context == NULL || xgl_overlay == NULL ||
        xgl_model == NULL || xgl_view == NULL)
        error("Cannot create Sun XGL contexts/transforms");
    xgl_object_set(xgl_context, XGL_CTX_LOCAL_MODEL_TRANS, xgl_model,
                   XGL_CTX_VIEW_TRANS, xgl_view, 0);
    drv_resize((int)drv_win_width, (int)drv_win_height);
    xgl_checkerrors();
    return 1;
}

static void xgl_model_transform(void)
{
    Xgl_pt translation;
    Xgl_pt_f3d offset;
    /* Transpose of OpenGL's T * Rx * Ry * Rx(-90) * Rz(-90).
     * Coordinates remain in object space until XGL processes each strip. */
    xgl_transform_rotate(xgl_model, -PI / 2.0, XGL_AXIS_Z,
                          XGL_TRANS_REPLACE);
    xgl_transform_rotate(xgl_model, -PI / 2.0, XGL_AXIS_X,
                          XGL_TRANS_POSTCONCAT);
    xgl_transform_rotate(xgl_model, (double)drv_rot_y * PI / 180.0,
                          XGL_AXIS_Y, XGL_TRANS_POSTCONCAT);
    xgl_transform_rotate(xgl_model, (double)drv_rot_x * PI / 180.0,
                          XGL_AXIS_X, XGL_TRANS_POSTCONCAT);
    offset.x = 0.0f;
    offset.y = 0.0f;
    offset.z = -3000.0f;
    translation.pt_type = XGL_PT_F3D;
    translation.pt.f3d = &offset;
    xgl_transform_translate(xgl_model, &translation, XGL_TRANS_POSTCONCAT);
}

static void xgl_texture_switch(int enabled)
{
    Xgl_boolean value = enabled ? TRUE : FALSE;
    if (xgl_tmap != NULL && xgl_tmap_switch[0] != value)
    {
        xgl_tmap_switch[0] = value;
        xgl_object_set(xgl_context,
                       XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES,
                       xgl_tmap_switch, 0);
    }
}

void drv_texture_load(void)
{
    Xgl_mem_ras memory;
    Xgl_usgn32 *pixels;
    Xgl_texture_general_desc descriptor;
    Xgl_render_component_desc *composition;
    unsigned int x, y;
    unsigned int levels;
    unsigned int smaller;
    unsigned char *source;
    if (drv_texture_loaded) return;
    texture_generate(TEXTURE_DEFAULT_SIZE);
    memory = xgl_object_create(xgl_system, XGL_MEM_RAS, NULL,
                                XGL_DEV_COLOR_TYPE, XGL_COLOR_RGB,
                                XGL_RAS_DEPTH, 32,
                                XGL_RAS_WIDTH, texture_size,
                                XGL_RAS_HEIGHT, texture_size / 2, 0);
    if (memory == NULL) error("Cannot allocate XGL texture raster");
    xgl_checkerrors();
    pixels = NULL;
    xgl_object_get(memory, XGL_MEM_RAS_IMAGE_BUFFER_ADDR, &pixels);
    if (pixels == NULL) error("Cannot access XGL texture raster");
    xgl_checkerrors();
    for (y = 0; y < texture_size / 2; ++y)
    {
        /* Memory rasters start at top left; UV v=0 is bottom left. Put the
         * first Earth row at v=0, matching OpenGL and common globe UVs. */
        source = texture_data + (unsigned long)y * texture_size * 3UL;
        for (x = 0; x < texture_size; ++x)
        {
            pixels[(unsigned long)(texture_size / 2 - 1 - y) * texture_size + x] =
                (Xgl_usgn32)source[0] |
                ((Xgl_usgn32)source[1] << 8) |
                ((Xgl_usgn32)source[2] << 16);
            source += 3;
        }
    }
    texture_free();
    xgl_mipmap = xgl_object_create(xgl_system, XGL_MIPMAP_TEXTURE, NULL, 0);
    if (xgl_mipmap == NULL) error("Cannot create XGL mipmap");
    /* XGL defines the level count as log2 of the smaller dimension.
     * For the 512x256 Earth image this is eight levels, as in guide p.457. */
    levels = 0;
    smaller = texture_size / 2;
    while (smaller > 1) { ++levels; smaller /= 2; }
    xgl_object_set(xgl_mipmap, XGL_MIPMAP_TEXTURE_LEVELS, levels, 0);
    xgl_mipmap_texture_build(xgl_mipmap, memory,
                             XGL_TEXTURE_BOUNDARY_WRAP,
                             XGL_TEXTURE_BOUNDARY_WRAP);
    xgl_checkerrors();
    xgl_object_destroy(memory);
    xgl_tmap = xgl_object_create(xgl_system, XGL_TMAP, NULL, 0);
    if (xgl_tmap == NULL) error("Cannot create XGL texture map");
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.texture_type = XGL_TEXTURE_TYPE_MIPMAP;
    descriptor.texture_info.mipmap.texture_map = xgl_mipmap;
    descriptor.texture_info.mipmap.max_u_freq = 1.0f;
    descriptor.texture_info.mipmap.max_v_freq = 1.0f;
    descriptor.texture_info.mipmap.u_boundary = XGL_TEXTURE_BOUNDARY_WRAP;
    descriptor.texture_info.mipmap.v_boundary = XGL_TEXTURE_BOUNDARY_WRAP;
    descriptor.texture_info.mipmap.interp_info.filter1 =
        XGL_TEXTURE_INTERP_MIPMAP_TRILINEAR;
    descriptor.texture_info.mipmap.interp_info.filter2 =
        XGL_TEXTURE_INTERP_BILINEAR;
    descriptor.texture_info.mipmap.orientation_matrix[0][0] = 1.0f;
    descriptor.texture_info.mipmap.orientation_matrix[1][1] = 1.0f;
    descriptor.comp_info.color_info.num_render_comp_desc = 1;
    descriptor.comp_info.color_info.num_channels[0] = 3;
    composition = &descriptor.comp_info.color_info.render_component_desc[0];
    composition->comp = XGL_RENDER_COMP_DIFFUSE_COLOR;
    composition->texture_op = XGL_TEXTURE_OP_REPLACE;
    xgl_object_set(xgl_tmap, XGL_TMAP_DESCRIPTOR, &descriptor, 0);
    xgl_tmap_switch[0] = FALSE;
    xgl_object_set(xgl_context, XGL_3D_CTX_SURF_FRONT_TMAP_NUM, 1,
                   XGL_3D_CTX_SURF_FRONT_TMAP, &xgl_tmap,
                   XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES,
                   xgl_tmap_switch, 0);
    xgl_checkerrors();
    drv_texture_loaded = TRUE;
}

void drv_texture_enable(void)
{
    if (!drv_texture_loaded) drv_texture_load();
    xgl_texture_switch(TRUE);
    xgl_checkerrors();
    drv_texture_enabled = TRUE;
    xgl_dirty = TRUE;
}

void drv_texture_disable(void)
{
    xgl_texture_switch(FALSE);
    xgl_checkerrors();
    drv_texture_enabled = FALSE;
    xgl_dirty = TRUE;
}

static size_t xgl_packet_bytes(unsigned int count, size_t stride)
{
    size_t bytes = (size_t)count * stride;
    /* Check each actual packet layout on both 32-bit Solaris and hosts
     * whose size_t is wider than the XGL strip count. */
    if (count != 0 && bytes / (size_t)count != stride)
        error("XGL triangle strip is too large");
    return bytes;
}

static void xgl_reserve_strip(unsigned int count)
{
    Xgl_pt_f3d *points;
    Xgl_color_facet *facets;
    float *textured;
    if (count <= xgl_strip_capacity) return;
    points = (Xgl_pt_f3d *)malloc(xgl_packet_bytes(count, sizeof(*points)));
    facets = (Xgl_color_facet *)malloc(xgl_packet_bytes(count, sizeof(*facets)));
    textured = (float *)malloc(xgl_packet_bytes(count, 5 * sizeof(*textured)));
    if (points == NULL || facets == NULL || textured == NULL)
    {
        free(points); free(facets); free(textured);
        error("Cannot allocate XGL triangle strip packet");
    }
    free(xgl_strip_points);
    free(xgl_strip_facets);
    free(xgl_strip_texture_points);
    xgl_strip_points = points;
    xgl_strip_facets = facets;
    xgl_strip_texture_points = textured;
    xgl_strip_capacity = count;
}

static void xgl_draw_tristrip(unsigned int count, const Vertex *vertices,
                               const Color_RGB *facet_colors,
                               const Coord *coords)
{
    Xgl_pt_list points;
    Xgl_facet_list facets;
    unsigned int i;
    float *destination;
    int textured;
    if (count < 3 || vertices == NULL ||
        (facet_colors == NULL && coords == NULL)) return;
    xgl_reserve_strip(count);
    textured = coords != NULL;
    xgl_texture_switch(textured);
    for (i = 0; i < count - 2; ++i)
    {
        xgl_strip_facets[i].color.rgb.r = facet_colors == NULL ?
                                           1.0f : (float)facet_colors[i].r / 255.0f;
        xgl_strip_facets[i].color.rgb.g = facet_colors == NULL ?
                                           1.0f : (float)facet_colors[i].g / 255.0f;
        xgl_strip_facets[i].color.rgb.b = facet_colors == NULL ?
                                           1.0f : (float)facet_colors[i].b / 255.0f;
    }
    memset(&points, 0, sizeof(points));
    points.num_pts = (Xgl_usgn32)count;
    if (textured)
    {
        /* DATA_F3D is variable-sized: XYZ followed by two contiguous floats,
         * not an array of fixed sizeof(Xgl_pt_data_f3d) elements. */
        destination = xgl_strip_texture_points;
        for (i = 0; i < count; ++i)
        {
            *destination++ = vertices[i].x;
            *destination++ = vertices[i].y;
            *destination++ = vertices[i].z;
            *destination++ = coords[i].u;
            *destination++ = coords[i].v;
        }
        points.pt_type = XGL_PT_DATA_F3D;
        points.num_data_values = 2;
        points.pts.data_f3d = (Xgl_pt_data_f3d *)xgl_strip_texture_points;
    }
    else
    {
        for (i = 0; i < count; ++i)
        {
            xgl_strip_points[i].x = vertices[i].x;
            xgl_strip_points[i].y = vertices[i].y;
            xgl_strip_points[i].z = vertices[i].z;
        }
        points.pt_type = XGL_PT_F3D;
        points.pts.f3d = xgl_strip_points;
    }
    memset(&facets, 0, sizeof(facets));
    facets.facet_type = XGL_FACET_COLOR;
    facets.num_facets = (Xgl_usgn32)(count - 2);
    facets.facets.color_facets = xgl_strip_facets;
    xgl_triangle_strip(xgl_context, &facets, &points);
}

void drv_draw_tristrip_flat_rgb(unsigned int count, const Vertex *vertices,
                                const Color_RGB *facet_colors)
{
    if (facet_colors == NULL) return;
    xgl_draw_tristrip(count, vertices, facet_colors, NULL);
}

void drv_draw_tristrip_flat_uv(unsigned int count,
                               const Vertex TG_HUGE *vertices,
                               const Coord TG_HUGE *coords)
{
    if (coords == NULL) return;
    /* REPLACE texture mapping supplies polygon colors independently of
     * the white facet metadata used by the native XGL strip packet. */
    xgl_draw_tristrip(count, vertices, NULL, coords);
}

void drv_draw_osd(void)
{
    Xgl_pt_f2d position;
    char status[160];
    drv_format_stats(status);
    strcat(status, drv_texture_enabled ? ", texture mapping on" :
                                        ", texture mapping off");
    position.x = 10.0f;
    position.y = 18.0f;
    xgl_stroke_text(xgl_overlay, status, &position, NULL);
    position.y = (float)drv_win_height - 10.0f;
    xgl_stroke_text(xgl_overlay, drv_help, &position, NULL);
}

void drv_render(void)
{
    drv_fps_start();
    xgl_context_new_frame(xgl_context);
    xgl_model_transform();
    globe_draw_tristrips();
    if (drv_osd_enabled) drv_draw_osd();
    xgl_context_flush(xgl_context, XGL_FLUSH_SYNCHRONIZE);
    xgl_context_flush(xgl_overlay, XGL_FLUSH_SYNCHRONIZE);
    if (xgl_double_buffer)
    {
        /* Overlay Context's frame action switches only: it must not clear
         * the newly displayed image or the next draw buffer's z-buffer. */
        xgl_context_new_frame(xgl_overlay);
        xgl_context_flush(xgl_overlay, XGL_FLUSH_SYNCHRONIZE);
    }
    xgl_checkerrors();
    drv_fps_stop();
    if (drv_auto_enabled) drv_globe_rotate();
}

static void xgl_keyboard(unsigned char key)
{
    if (key == 27)
    {
        drv_close();
        return;
    }
    if (drv_handle_key(key)) xgl_dirty = TRUE;
}

static void xgl_mouse_move(int x, int y)
{
    if (drv_lmouse_pressed)
    {
        drv_rotate_drag((long)x - xgl_old_x, (long)y - xgl_old_y);
        xgl_dirty = TRUE;
    }
    xgl_old_x = x;
    xgl_old_y = y;
}

static void xgl_event(XEvent *event)
{
    char text[8];
    KeySym symbol;
    int length;
    switch (event->type)
    {
        case Expose:
            if (event->xexpose.count == 0) xgl_dirty = TRUE;
            break;
        case ConfigureNotify:
            drv_resize(event->xconfigure.width, event->xconfigure.height);
            break;
        case KeyPress:
            length = XLookupString(&event->xkey, text, sizeof(text), &symbol, NULL);
            if (symbol == XK_Escape) drv_close();
            if (length > 0) xgl_keyboard((unsigned char)text[0]);
            break;
        case ButtonPress:
            if (event->xbutton.button == Button1)
            {
                drv_lmouse_pressed = TRUE;
                xgl_old_x = event->xbutton.x;
                xgl_old_y = event->xbutton.y;
            }
            break;
        case ButtonRelease:
            if (event->xbutton.button == Button1) drv_lmouse_pressed = FALSE;
            break;
        case MotionNotify:
            xgl_mouse_move(event->xmotion.x, event->xmotion.y);
            break;
        case ClientMessage:
            if (event->xclient.message_type == xgl_wm_protocols &&
                event->xclient.format == 32 &&
                (Atom)event->xclient.data.l[0] == xgl_wm_delete) drv_close();
            break;
        case DestroyNotify:
            xgl_window = 0;
            drv_close();
            break;
        default: break;
    }
}

void drv_loop(void)
{
    XEvent event;
    for (;;)
    {
        if (!drv_auto_enabled && !xgl_dirty)
        {
            XNextEvent(xgl_display, &event);
            xgl_event(&event);
        }
        while (XPending(xgl_display))
        {
            XNextEvent(xgl_display, &event);
            xgl_event(&event);
        }
        if (xgl_dirty || drv_auto_enabled)
        {
            drv_render();
            xgl_dirty = FALSE;
        }
    }
}
#endif /* DRV_XGL */
