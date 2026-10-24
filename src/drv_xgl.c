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
#include "e_ext.h"

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
static Xgl_mem_ras xgl_texture_raster;
static Xgl_boolean xgl_tmap_switch[1];
static int xgl_double_buffer;
static int xgl_failed;
static int xgl_nonrecoverable;
static int xgl_texture_scope;
static int xgl_texture_error;
static int xgl_texture_failed;
static int xgl_texture_used;
static int xgl_texture_submitted;
static int xgl_texture_pending;
static const char *xgl_texture_reason;
static int xgl_dirty = TRUE;
static int xgl_old_x;
static int xgl_old_y;
static unsigned int xgl_strip_capacity;
static unsigned int xgl_strip_texture_capacity;
static Xgl_pt_f3d *xgl_strip_points;
static Xgl_color_facet *xgl_strip_facets;
static float *xgl_strip_texture_points;
/* A failed growth request must not be retried for every latitude band. */
static int xgl_strip_limited;
static Xgl_pt_f3d xgl_small_points[4];
static Xgl_color_facet xgl_small_facets[4];
static float xgl_small_texture_points[20];

static void xgl_cleanup(void)
{
    /* Closing the System State releases all its objects and their resources. */
    if (xgl_system != NULL)
    {
        /* A NONRECOVERABLE callback leaves native state invalid. Let
         * process teardown reclaim it without another XGL operator. */
        if (!xgl_nonrecoverable) xgl_close(xgl_system);
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
    xgl_strip_texture_capacity = 0;
    xgl_mipmap = xgl_tmap = xgl_texture_raster = NULL;
    xgl_texture_used = xgl_texture_failed = FALSE;
    drv_texture_loaded = drv_texture_enabled = drv_texture_active = FALSE;
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
    if (info.type == XGL_ERROR_NONRECOVERABLE) xgl_nonrecoverable = TRUE;
    if (xgl_texture_scope && info.type == XGL_ERROR_RECOVERABLE)
        xgl_texture_error = TRUE;
    else
        xgl_failed = TRUE;
    return 1;
}

static void xgl_checkerrors(void)
{
    if (xgl_failed) error("Sun XGL operation failed; see the XGL diagnostic");
}

void drv_close(void)
{
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
    xgl_checkerrors();
    if (xgl_system == NULL) error("Cannot initialize Sun XGL");
    xgl_object_set(xgl_system,
                   XGL_SYS_ST_ERROR_NOTIFICATION_FUNCTION, xgl_error_notify, 0);
    xgl_checkerrors();
}

static void xgl_projection(int width, int height)
{
    Xgl_matrix_d3d matrix;
    Xgl_bounds_d3d bounds;
    double near_plane = GLOBE_NEAR_DISTANCE;
    double far_plane = GLOBE_FAR_DISTANCE;
    double cotangent = 1.0 / tan(GLOBE_FIELD_OF_VIEW * PI / 360.0);
    memset(matrix, 0, sizeof(matrix));
    /* XGL uses row vectors. This is a 45-degree perspective projection,
     * with near mapped to VDC z=1 and far to z=0 (Y_UP_Z_TOWARD). */
    matrix[0][0] = cotangent * (double)height / (double)width;
    matrix[1][1] = cotangent;
    matrix[2][2] = near_plane / (far_plane - near_plane);
    matrix[3][2] = far_plane * near_plane / (far_plane - near_plane);
    matrix[2][3] = -1.0;
    xgl_transform_write_specific(xgl_view, matrix, 0);
    xgl_checkerrors();
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
    xgl_checkerrors();
}

static void xgl_resize(int width, int height)
{
    /* X11 can send zero geometry while a window is being withdrawn. */
    if (width < 1 || height < 1) return;
    drv_win_width = (unsigned int)width;
    drv_win_height = (unsigned int)height;
    if (xgl_raster != NULL)
    {
        xgl_window_raster_resize(xgl_raster);
        xgl_checkerrors();
        xgl_projection(width, height);
        xgl_checkerrors();
    }
    drv_reset_timing();
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
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 FocusChangeMask;
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
    xgl_checkerrors();
    if (xgl_raster == NULL) error("Cannot create Sun XGL raster");
    buffers = 1;
    xgl_object_get(xgl_raster, XGL_WIN_RAS_BUFFERS_ALLOCATED, &buffers);
    xgl_checkerrors();
    xgl_double_buffer = buffers > 1;
    if (xgl_double_buffer)
    {
        xgl_object_set(xgl_raster, XGL_WIN_RAS_BUF_DRAW, 1,
                       XGL_WIN_RAS_BUF_DISPLAY, 0, 0);
        xgl_checkerrors();
    }
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
    xgl_checkerrors();
    xgl_overlay = xgl_object_create(xgl_system, XGL_2D_CTX, NULL,
                                    XGL_CTX_DEVICE, xgl_raster,
                                    XGL_CTX_DEFERRAL_MODE, XGL_DEFER_ASAP,
                                    XGL_CTX_VDC_MAP, XGL_VDC_MAP_DEVICE,
                                    XGL_CTX_STEXT_CHAR_HEIGHT, 12.0,
                                    XGL_CTX_STEXT_COLOR, &white,
                                    XGL_CTX_NEW_FRAME_ACTION,
                                      xgl_double_buffer ?
                                      XGL_CTX_NEW_FRAME_SWITCH_BUFFER : 0, 0);
    xgl_checkerrors();
    xgl_model = xgl_object_create(xgl_system, XGL_TRANS, NULL,
                                  XGL_TRANS_DATA_TYPE, XGL_DATA_DBL, 0);
    xgl_checkerrors();
    xgl_view = xgl_object_create(xgl_system, XGL_TRANS, NULL,
                                 XGL_TRANS_DATA_TYPE, XGL_DATA_DBL, 0);
    xgl_checkerrors();
    if (xgl_context == NULL || xgl_overlay == NULL ||
        xgl_model == NULL || xgl_view == NULL)
        error("Cannot create Sun XGL contexts/transforms");
    xgl_object_set(xgl_context, XGL_CTX_LOCAL_MODEL_TRANS, xgl_model,
                   XGL_CTX_VIEW_TRANS, xgl_view, 0);
    xgl_checkerrors();
    xgl_resize((int)drv_win_width, (int)drv_win_height);
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
    xgl_checkerrors();
    xgl_transform_rotate(xgl_model, -PI / 2.0, XGL_AXIS_X,
                          XGL_TRANS_POSTCONCAT);
    xgl_checkerrors();
    xgl_transform_rotate(xgl_model, (double)drv_rot_y * PI / 180.0,
                          XGL_AXIS_Y, XGL_TRANS_POSTCONCAT);
    xgl_checkerrors();
    xgl_transform_rotate(xgl_model, (double)drv_rot_x * PI / 180.0,
                          XGL_AXIS_X, XGL_TRANS_POSTCONCAT);
    xgl_checkerrors();
    offset.x = 0.0f;
    offset.y = 0.0f;
    offset.z = -GLOBE_VIEW_DISTANCE;
    translation.pt_type = XGL_PT_F3D;
    translation.pt.f3d = &offset;
    xgl_transform_translate(xgl_model, &translation, XGL_TRANS_POSTCONCAT);
    xgl_checkerrors();
}

/* Errors are recoverable only while executing texture-specific operations.
 * The manual requires stopping after any NONRECOVERABLE library error. */
static void xgl_texture_begin(void)
{
    xgl_checkerrors();
    xgl_texture_scope = TRUE;
    xgl_texture_error = FALSE;
    xgl_texture_pending = FALSE;
    xgl_texture_reason = NULL;
}

static int xgl_texture_end(void)
{
    xgl_texture_scope = FALSE;
    xgl_checkerrors();
    return !xgl_texture_error;
}

static int xgl_texture_check(void)
{
    xgl_checkerrors();
    return !xgl_texture_error;
}

/* Destroy explicitly owned objects before removing their Context references.
 * XGL defers destruction while referenced; reducing TMAP_NUM then releases
 * the Context's maps and switches, so no stale handle is destroyed afterward. */
static void xgl_texture_release(void)
{
    if (xgl_mipmap != NULL) xgl_object_destroy(xgl_mipmap);
    xgl_checkerrors();
    xgl_mipmap = NULL;
    if (xgl_tmap != NULL) xgl_object_destroy(xgl_tmap);
    xgl_checkerrors();
    xgl_tmap = NULL;
    if (xgl_context != NULL)
        xgl_object_set(xgl_context, XGL_3D_CTX_SURF_FRONT_TMAP_NUM, 0, 0);
    xgl_checkerrors();
    xgl_tmap_switch[0] = FALSE;
    if (xgl_texture_raster != NULL) xgl_object_destroy(xgl_texture_raster);
    xgl_checkerrors();
    xgl_texture_raster = NULL;
    texture_free();
    drv_texture_loaded = FALSE;
}

static void xgl_texture_failure(const char *message)
{
    /* Cleanup itself must succeed before flat rendering is safe. */
    xgl_texture_scope = FALSE;
    xgl_checkerrors();
    xgl_texture_release();
    xgl_checkerrors();
    xgl_texture_failed = TRUE;
    xgl_texture_used = FALSE;
    xgl_texture_error = FALSE;
    drv_texture_active = FALSE;
    drv_notice(message);
    drv_reset_timing();
    fprintf(stderr, "Sun XGL texture: %s\n", message);
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
        xgl_checkerrors();
    }
}

static int xgl_texture_ready(void)
{
    Xgl_usgn32 count;
    Xgl_object map;
    Xgl_boolean enabled;
    count = 0;
    map = NULL;
    enabled = FALSE;
    if (xgl_tmap == NULL || xgl_mipmap == NULL) return FALSE;
    xgl_object_get(xgl_context, XGL_3D_CTX_SURF_FRONT_TMAP_NUM, &count);
    if (!xgl_texture_check() || count != 1U) return FALSE;
    /* Array getters copy into caller storage; query the length first. */
    xgl_object_get(xgl_context, XGL_3D_CTX_SURF_FRONT_TMAP, &map);
    if (!xgl_texture_check()) return FALSE;
    xgl_object_get(xgl_context, XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES, &enabled);
    return xgl_texture_check() && map == xgl_tmap && enabled;
}

static Drv_result xgl_texture_load_size(unsigned int size)
{
    Xgl_usgn32 *pixels;
    Xgl_texture_general_desc descriptor;
    Xgl_render_component_desc *composition;
    unsigned int x, y, levels, smaller;
    unsigned char row[512], index;
    Drv_result result;
    const char *message;
    result = DRV_NATIVE_FAILED;
    if (size > sizeof(row) || size < 32U ||
        earth_data_size((int)size) != (unsigned long)size * (size / 2U))
        return DRV_BAD_DATA;
    message = "Cannot allocate XGL texture raster";
    xgl_texture_raster = xgl_object_create(xgl_system, XGL_MEM_RAS, NULL,
                                XGL_DEV_COLOR_TYPE, XGL_COLOR_RGB,
                                XGL_RAS_DEPTH, 32,
                                XGL_RAS_WIDTH, size,
                                XGL_RAS_HEIGHT, size / 2, 0);
    if (!xgl_texture_check()) goto failed;
    if (xgl_texture_raster == NULL) { result = DRV_NO_MEMORY; goto failed; }
    message = "Cannot access XGL texture raster";
    pixels = NULL;
    xgl_object_get(xgl_texture_raster, XGL_MEM_RAS_IMAGE_BUFFER_ADDR, &pixels);
    if (!xgl_texture_check() || pixels == NULL) goto failed;
    for (y = 0; y < size / 2; ++y)
    {
        if (!earth_read_data((int)size, (unsigned long)y * size, size, row))
        {
            result = DRV_BAD_DATA;
            message = "Cannot read XGL texture Earth data";
            goto failed;
        }
        for (x = 0; x < size; ++x)
        {
            /* Raster top-left rows become the globe's bottom-left UV rows. */
            index = row[x];
            pixels[(unsigned long)(size / 2 - 1 - y) * size + x] =
                (Xgl_usgn32)earth_cmap[index][0] |
                ((Xgl_usgn32)earth_cmap[index][1] << 8) |
                ((Xgl_usgn32)earth_cmap[index][2] << 16);
        }
    }
    message = "Cannot create XGL mipmap";
    xgl_mipmap = xgl_object_create(xgl_system, XGL_MIPMAP_TEXTURE, NULL, 0);
    if (!xgl_texture_check()) goto failed;
    if (xgl_mipmap == NULL) { result = DRV_NO_MEMORY; goto failed; }
    levels = 0;
    smaller = size / 2;
    while (smaller > 1) { ++levels; smaller /= 2; }
    message = "Cannot build XGL mipmap";
    xgl_object_set(xgl_mipmap, XGL_MIPMAP_TEXTURE_LEVELS, levels, 0);
    if (!xgl_texture_check()) goto failed;
    xgl_mipmap_texture_build(xgl_mipmap, xgl_texture_raster,
                             XGL_TEXTURE_BOUNDARY_WRAP,
                             XGL_TEXTURE_BOUNDARY_WRAP);
    if (!xgl_texture_check()) goto failed;
    xgl_object_destroy(xgl_texture_raster);
    xgl_checkerrors();
    xgl_texture_raster = NULL;
    if (!xgl_texture_check()) goto failed;
    message = "Cannot create XGL texture map";
    xgl_tmap = xgl_object_create(xgl_system, XGL_TMAP, NULL, 0);
    if (!xgl_texture_check() || xgl_tmap == NULL) goto failed;
    message = "Cannot configure XGL texture map";
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
    if (!xgl_texture_check()) goto failed;
    xgl_tmap_switch[0] = FALSE;
    xgl_object_set(xgl_context, XGL_3D_CTX_SURF_FRONT_TMAP_NUM, 1,
                   XGL_3D_CTX_SURF_FRONT_TMAP, &xgl_tmap,
                   XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES,
                   xgl_tmap_switch, 0);
    if (!xgl_texture_check()) goto failed;
    drv_texture_loaded = TRUE;
    texture_size = size;
    return DRV_OK;
failed:
    /* Only a NULL allocation without a library error permits a smaller
     * retry. Recoverable native errors still use the established flat path;
     * NONRECOVERABLE errors have already stopped in xgl_checkerrors(). */
    xgl_texture_scope = FALSE;
    xgl_texture_release();
    xgl_texture_reason = message;
    return result;
}

static int xgl_texture_load(void)
{
    unsigned int size;
    int released;
    Drv_result result;
    if (drv_texture_loaded) return TRUE;
    size = TEXTURE_DEFAULT_SIZE;
    released = FALSE;
    for (;;)
    {
        xgl_texture_begin();
        result = xgl_texture_load_size(size);
        if (result == DRV_OK)
        {
            if (size != TEXTURE_DEFAULT_SIZE)
                drv_notice("XGL uses a smaller texture");
            return TRUE;
        }
        if (result == DRV_BAD_DATA || result == DRV_BAD_INPUT)
            error("Invalid XGL texture Earth data");
        if (result != DRV_NO_MEMORY) break;
        if (!released)
        {
            drv_release_optional();
            released = TRUE;
            continue;
        }
        if (size <= 32U) break;
        size /= 2U;
    }
    xgl_texture_failure(xgl_texture_reason != NULL ? xgl_texture_reason :
                         "Cannot allocate XGL texture");
    return FALSE;
}

void drv_texture_enable(void)
{
    drv_texture_enabled = TRUE;
    xgl_texture_failed = xgl_texture_used = FALSE;
    xgl_dirty = TRUE;
    if (!globe_ready()) { xgl_texture_pending = TRUE; return; }
    xgl_texture_pending = FALSE;
    xgl_texture_begin();
    if (!drv_texture_loaded && !xgl_texture_load()) return;
    xgl_texture_switch(TRUE);
    if (!xgl_texture_ready() || !xgl_texture_end())
    {
        xgl_texture_failure("XGL texture is not active");
        return;
    }
}

void drv_texture_disable(void)
{
    drv_texture_enabled = FALSE;
    drv_texture_active = FALSE;
    xgl_texture_pending = FALSE;
    xgl_texture_failed = xgl_texture_used = FALSE;
    xgl_dirty = TRUE;
    xgl_texture_begin();
    xgl_texture_switch(FALSE);
    if (!xgl_texture_end())
        xgl_texture_failure("Cannot disable XGL texture");
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

static unsigned int xgl_reserve_strip(unsigned int count, int textured)
{
    Xgl_pt_f3d *points;
    Xgl_color_facet *facets;
    float *texture_points;
    unsigned int capacity;
    if (!xgl_strip_limited && count > xgl_strip_capacity)
    {
        points = (Xgl_pt_f3d *)malloc(xgl_packet_bytes(count, sizeof(*points)));
        facets = (Xgl_color_facet *)malloc(xgl_packet_bytes(count, sizeof(*facets)));
        if (points == NULL || facets == NULL)
        {
            free(points); free(facets);
            xgl_strip_limited = TRUE;
            drv_notice("XGL uses smaller strip packets");
            drv_reset_timing();
        }
        else
        {
            free(xgl_strip_points);
            free(xgl_strip_facets);
            xgl_strip_points = points;
            xgl_strip_facets = facets;
            xgl_strip_capacity = count;
        }
    }
    if (!xgl_strip_limited && textured && count > xgl_strip_texture_capacity)
    {
        texture_points = (float *)malloc(xgl_packet_bytes(count, 5 * sizeof(*texture_points)));
        if (texture_points == NULL)
        {
            xgl_strip_limited = TRUE;
            drv_notice("XGL uses smaller strip packets");
            drv_reset_timing();
        }
        else
        {
            free(xgl_strip_texture_points);
            xgl_strip_texture_points = texture_points;
            xgl_strip_texture_capacity = count;
        }
    }
    capacity = xgl_strip_capacity;
    if (textured && xgl_strip_texture_capacity < capacity)
        capacity = xgl_strip_texture_capacity;
    return capacity >= 4U ? capacity : 4U;
}

/* A bounded packet has complete native ownership during this one call.
 * XGL_CTX_GEOM_DATA_IS_VOLATILE makes reuse safe after submission. */
static void xgl_submit_tristrip(unsigned int count, const Vertex *vertices,
                               const Color_RGB *facet_colors, const Coord *coords,
                               Xgl_pt_f3d *plain, Xgl_color_facet *colors,
                               float *texture_points)
{
    Xgl_pt_list points;
    Xgl_facet_list facets;
    unsigned int i;
    float *destination;
    int textured;
    textured = coords != NULL;
    xgl_texture_switch(textured);
    xgl_checkerrors();
    if (textured && !xgl_texture_check()) return;
    for (i = 0; i < count - 2; ++i)
    {
        colors[i].color.rgb.r = facet_colors == NULL ?
                                           1.0f : (float)facet_colors[i].r / 255.0f;
        colors[i].color.rgb.g = facet_colors == NULL ?
                                           1.0f : (float)facet_colors[i].g / 255.0f;
        colors[i].color.rgb.b = facet_colors == NULL ?
                                           1.0f : (float)facet_colors[i].b / 255.0f;
    }
    memset(&points, 0, sizeof(points));
    points.num_pts = (Xgl_usgn32)count;
    if (textured)
    {
        /* DATA_F3D is variable-sized: XYZ followed by two contiguous floats,
         * not an array of fixed sizeof(Xgl_pt_data_f3d) elements. */
        destination = texture_points;
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
        points.pts.data_f3d = (Xgl_pt_data_f3d *)texture_points;
    }
    else
    {
        for (i = 0; i < count; ++i)
        {
            plain[i].x = vertices[i].x;
            plain[i].y = vertices[i].y;
            plain[i].z = vertices[i].z;
        }
        points.pt_type = XGL_PT_F3D;
        points.pts.f3d = plain;
    }
    memset(&facets, 0, sizeof(facets));
    facets.facet_type = XGL_FACET_COLOR;
    facets.num_facets = (Xgl_usgn32)(count - 2);
    facets.facets.color_facets = colors;
    xgl_triangle_strip(xgl_context, &facets, &points);
    xgl_checkerrors();
    if (textured && xgl_texture_check()) xgl_texture_submitted = TRUE;
}

static void xgl_draw_tristrip(unsigned int count, const Vertex *vertices,
                               const Color_RGB *facet_colors, const Coord *coords)
{
    unsigned int capacity, first, facets, maximum;
    Xgl_pt_f3d *plain;
    Xgl_color_facet *colors;
    float *texture_points;
    int textured;
    if (count < 3 || vertices == NULL ||
        (facet_colors == NULL && coords == NULL)) return;
    textured = coords != NULL;
    if (textured && xgl_texture_error) return;
    capacity = xgl_reserve_strip(count, textured);
    if (xgl_strip_capacity < 4U ||
        (textured && xgl_strip_texture_capacity < 4U))
    {
        plain = xgl_small_points;
        colors = xgl_small_facets;
        texture_points = xgl_small_texture_points;
    }
    else
    {
        plain = xgl_strip_points;
        colors = xgl_strip_facets;
        texture_points = xgl_strip_texture_points;
    }
    /* Nonfinal pieces contain an even number of facets, retaining the
     * strip's original winding and its final two vertices in the next piece. */
    maximum = (capacity - 2U) & ~1U;
    first = 0;
    while (first < count - 2U)
    {
        facets = count - 2U - first;
        if (facets > maximum) facets = maximum;
        xgl_submit_tristrip(facets + 2U, vertices + first,
                             facet_colors == NULL ? NULL : facet_colors + first,
                             coords == NULL ? NULL : coords + first,
                             plain, colors, texture_points);
        if (drv_frame_failed || (textured && xgl_texture_error)) return;
        first += facets;
    }
}

void drv_release_optional(void)
{
    /* Recovery is called between frames. Never discard a packet still in
     * the caller's active pass, or issue XGL calls after invalid native state. */
    if (drv_texture_active || xgl_failed || xgl_nonrecoverable) return;
    free(xgl_strip_points); free(xgl_strip_facets);
    free(xgl_strip_texture_points);
    xgl_strip_points = NULL; xgl_strip_facets = NULL;
    xgl_strip_texture_points = NULL;
    xgl_strip_capacity = xgl_strip_texture_capacity = 0;
    xgl_strip_limited = TRUE;
    if (!drv_texture_enabled) xgl_texture_release();
    drv_reset_timing();
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
    char texture_status[40];
    drv_format_stats(status);
    if (xgl_texture_used && texture_size != TEXTURE_DEFAULT_SIZE)
        sprintf(texture_status, ", texture on (%u)", texture_size);
    else
        strcpy(texture_status, !drv_texture_enabled ? ", texture off" :
                    (xgl_texture_used ? ", texture on" : ", texture fail"));
    drv_append_status(status, sizeof(status), texture_status);
    position.x = 10.0f;
    position.y = 18.0f;
    xgl_stroke_text(xgl_overlay, status, &position, NULL);
    xgl_checkerrors();
    position.y = (float)drv_win_height - 10.0f;
    xgl_stroke_text(xgl_overlay, drv_help, &position, NULL);
    xgl_checkerrors();
}

static int xgl_animating(void)
{
    return drv_auto_enabled && globe_ready() && !drv_frame_failed;
}

void drv_render(void)
{
    int requested, ready, scene, native_ok;
    const char *reason;
    drv_fps_start();
    requested = drv_texture_enabled;
    scene = globe_ready();
    if (!scene) drv_frame_failed = TRUE;
    drv_texture_active = FALSE;
    xgl_texture_used = xgl_texture_submitted = FALSE;
    xgl_context_new_frame(xgl_context);
    xgl_checkerrors();
    xgl_model_transform();
    if (scene && requested && xgl_texture_pending)
        drv_texture_enable();
    if (scene && requested && !xgl_texture_failed && !drv_texture_loaded)
        xgl_texture_failure("XGL texture data is not loaded");
    ready = scene && requested && drv_texture_loaded && !xgl_texture_failed;
    if (ready)
    {
        xgl_texture_begin();
        ready = xgl_texture_ready();
        if (ready)
        {
            drv_texture_active = TRUE;
            globe_draw_tristrips();
            drv_texture_active = FALSE;
            /* Complete deferred geometry before choosing this frame's status. */
            xgl_context_flush(xgl_context, XGL_FLUSH_SYNCHRONIZE);
            xgl_checkerrors();
            ready = xgl_texture_check() && xgl_texture_submitted;
        }
        reason = xgl_texture_reason;
        native_ok = xgl_texture_end();
        if (!native_ok || (!ready && !drv_frame_failed))
        {
            xgl_texture_failure(reason != NULL ? reason : "Cannot use XGL texture");
            ready = FALSE;
            xgl_context_new_frame(xgl_context);
            xgl_checkerrors();
            xgl_model_transform();
        }
        else if (!drv_frame_failed) xgl_texture_used = TRUE;
    }
    if (scene && !ready && !drv_frame_failed)
    {
        drv_texture_active = FALSE;
        xgl_texture_switch(FALSE);
        globe_draw_tristrips();
        xgl_context_flush(xgl_context, XGL_FLUSH_SYNCHRONIZE);
        xgl_checkerrors();
    }
    if (scene && drv_frame_failed)
    {
        /* A failed traversal is not a texture failure. Complete/unwind the
         * pass, then erase partial geometry while retaining usable resources. */
        drv_texture_active = FALSE;
        xgl_texture_used = FALSE;
        xgl_context_new_frame(xgl_context);
        xgl_checkerrors();
    }
    if (drv_osd_enabled || !scene || drv_frame_failed) drv_draw_osd();
    xgl_context_flush(xgl_overlay, XGL_FLUSH_SYNCHRONIZE);
    xgl_checkerrors();
    if (xgl_double_buffer)
    {
        /* Overlay Context's frame action switches only: it must not clear
         * the newly displayed image or the next draw buffer's z-buffer. */
        xgl_context_new_frame(xgl_overlay);
        xgl_checkerrors();
        xgl_context_flush(xgl_overlay, XGL_FLUSH_SYNCHRONIZE);
        xgl_checkerrors();
    }
    xgl_checkerrors();
    drv_fps_stop();
    if (xgl_animating()) drv_globe_rotate();
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
            xgl_resize(event->xconfigure.width, event->xconfigure.height);
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
            if (event->xbutton.button == Button3)
            {
                drv_rmouse_pressed = TRUE;
                xgl_dirty = TRUE;
            }
            break;
        case ButtonRelease:
            if (event->xbutton.button == Button1) drv_lmouse_pressed = FALSE;
            if (event->xbutton.button == Button3)
            {
                drv_rmouse_pressed = FALSE;
                xgl_dirty = TRUE;
            }
            break;
        case MotionNotify:
            xgl_mouse_move(event->xmotion.x, event->xmotion.y);
            break;
        case FocusOut:
            drv_lmouse_pressed = FALSE;
            drv_rmouse_pressed = FALSE;
            xgl_dirty = TRUE;
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
        if (!xgl_animating() && !xgl_dirty)
        {
            XNextEvent(xgl_display, &event);
            xgl_event(&event);
        }
        while (XPending(xgl_display))
        {
            XNextEvent(xgl_display, &event);
            xgl_event(&event);
        }
        if (xgl_dirty || xgl_animating())
        {
            drv_render();
            xgl_dirty = FALSE;
        }
    }
}
#endif /* DRV_XGL */
