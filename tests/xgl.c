/* Actual driver + real X11; XGL calls are recorded, not rendered.
 * The manual-derived test declarations are deliberately not a Sun SDK/ABI.
 */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/drv_xgl.c"

extern Vertex *globe_verts;
extern Coord *globe_coords;
extern int globe_longitude, globe_latitude;

struct Test_xgl_object {
    int type, value[64];
    Xgl_object device, model, view;
    Xgl_error_notify error_notify;
    Xgl_texture_general_desc descriptor;
    Xgl_matrix_d3d matrix;
    void *image;
    unsigned int rotations;
    struct Test_xgl_object *next;
};

static Xgl_object objects;
static unsigned int allocated_objects, close_calls, memory_destroys;
static unsigned int mipmap_creates, tmap_creates, mipmap_builds;
static unsigned int resize_calls, posts, clear_calls, buffer_switches, text_calls;
static unsigned long frame_bands, frame_facets;
static int single_buffer, tests_complete;

static Xgl_object allocate_object(int type)
{
    Xgl_object object;
    object = (Xgl_object)calloc(1, sizeof(*object));
    assert(object != NULL);
    object->type = type;
    object->next = objects;
    objects = object;
    ++allocated_objects;
    if (type == XGL_MIPMAP_TEXTURE) ++mipmap_creates;
    if (type == XGL_TMAP) ++tmap_creates;
    return object;
}

static void attributes(Xgl_object object, int attribute, va_list args)
{
    Xgl_bounds_d3d *bounds;
    Xgl_color *color;
    Xgl_boolean *switches;
    Xgl_object *maps;
    Xgl_texture_general_desc *descriptor;
    while (attribute != 0) {
        assert(attribute > 0 && attribute < 64);
        switch (attribute) {
        case XGL_SYS_ST_ERROR_NOTIFICATION_FUNCTION:
            object->error_notify = va_arg(args, Xgl_error_notify);
            assert(object->error_notify != NULL);
            break;
        case XGL_CTX_DEVICE:
            object->device = va_arg(args, Xgl_object);
            assert(object->device->type == XGL_WIN_RAS);
            break;
        case XGL_CTX_BACKGROUND_COLOR:
        case XGL_CTX_STEXT_COLOR:
            color = va_arg(args, Xgl_color *);
            if (attribute == XGL_CTX_BACKGROUND_COLOR)
                assert(color->rgb.r == 0 && color->rgb.g == 0 && color->rgb.b == 0);
            else
                assert(color->rgb.r == 1 && color->rgb.g == 1 && color->rgb.b == 1);
            break;
        case XGL_CTX_STEXT_CHAR_HEIGHT:
            assert(va_arg(args, double) == 12.0);
            break;
        case XGL_CTX_LOCAL_MODEL_TRANS:
            object->model = va_arg(args, Xgl_object);
            break;
        case XGL_CTX_VIEW_TRANS:
            object->view = va_arg(args, Xgl_object);
            break;
        case XGL_CTX_VDC_WINDOW:
        case XGL_CTX_VIEW_CLIP_BOUNDS:
            bounds = va_arg(args, Xgl_bounds_d3d *);
            assert(bounds->xmin == -1 && bounds->xmax == 1);
            assert(bounds->ymin == -1 && bounds->ymax == 1);
            assert(bounds->zmin == 0 && bounds->zmax == 1);
            break;
        case XGL_3D_CTX_SURF_FRONT_TMAP:
            maps = va_arg(args, Xgl_object *);
            assert(maps[0]->type == XGL_TMAP);
            break;
        case XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES:
            switches = va_arg(args, Xgl_boolean *);
            object->value[attribute] = switches[0];
            break;
        case XGL_TMAP_DESCRIPTOR:
            descriptor = va_arg(args, Xgl_texture_general_desc *);
            object->descriptor = *descriptor;
            assert(descriptor->texture_type == XGL_TEXTURE_TYPE_MIPMAP);
            assert(descriptor->texture_info.mipmap.texture_map->type == XGL_MIPMAP_TEXTURE);
            assert(descriptor->texture_info.mipmap.max_u_freq == 1.0f);
            assert(descriptor->texture_info.mipmap.max_v_freq == 1.0f);
            assert(descriptor->texture_info.mipmap.u_boundary == XGL_TEXTURE_BOUNDARY_WRAP);
            assert(descriptor->texture_info.mipmap.v_boundary == XGL_TEXTURE_BOUNDARY_WRAP);
            assert(descriptor->texture_info.mipmap.interp_info.filter1 == XGL_TEXTURE_INTERP_MIPMAP_TRILINEAR);
            assert(descriptor->texture_info.mipmap.interp_info.filter2 == XGL_TEXTURE_INTERP_BILINEAR);
            assert(descriptor->texture_info.mipmap.orientation_matrix[0][0] == 1.0f);
            assert(descriptor->texture_info.mipmap.orientation_matrix[1][1] == 1.0f);
            assert(descriptor->comp_info.color_info.num_render_comp_desc == 1);
            assert(descriptor->comp_info.color_info.num_channels[0] == 3);
            assert(descriptor->comp_info.color_info.render_component_desc[0].comp == XGL_RENDER_COMP_DIFFUSE_COLOR);
            assert(descriptor->comp_info.color_info.render_component_desc[0].texture_op == XGL_TEXTURE_OP_REPLACE);
            break;
        default:
            object->value[attribute] = va_arg(args, int);
            break;
        }
        attribute = va_arg(args, int);
    }
}

Xgl_sys_state xgl_open(Xgl_attribute first, ...)
{
    Xgl_object object;
    va_list args;
    assert(sizeof(Xgl_usgn32) == 4);
    object = allocate_object(-1);
    va_start(args, first);
    attributes(object, first, args);
    va_end(args);
    assert(object->value[XGL_SYS_ST_ERROR_DETECTION]);
    return object;
}

Xgl_object xgl_object_create(Xgl_sys_state system, Xgl_obj_type type,
                             Xgl_obj_desc *descriptor, ...)
{
    Xgl_object object;
    Xgl_X_window *window;
    int first;
    va_list args;
    assert(system == xgl_system && system->type == -1);
    if (type == XGL_WIN_RAS) {
        assert(descriptor != NULL && descriptor->win_ras.type == XGL_WIN_X);
        window = (Xgl_X_window *)descriptor->win_ras.desc;
        assert(window->X_display == xgl_display);
        assert(window->X_window == (Xgl_usgn32)xgl_window);
        assert(window->X_screen == DefaultScreen(xgl_display));
    } else assert(descriptor == NULL);
    object = allocate_object(type);
    va_start(args, descriptor);
    first = va_arg(args, int);
    attributes(object, first, args);
    va_end(args);
    return object;
}

void xgl_object_set(Xgl_object object, ...)
{
    int first;
    va_list args;
    assert(object != NULL);
    va_start(args, object);
    first = va_arg(args, int);
    attributes(object, first, args);
    va_end(args);
}

void xgl_object_get(Xgl_object object, Xgl_attribute attribute, void *value)
{
    assert(object != NULL && value != NULL);
    switch (attribute) {
    case XGL_WIN_RAS_BUFFERS_ALLOCATED:
        *(Xgl_usgn32 *)value = single_buffer ? 1U : 2U;
        break;
    case XGL_MEM_RAS_IMAGE_BUFFER_ADDR:
        assert(object->type == XGL_MEM_RAS);
        assert(object->value[XGL_RAS_DEPTH] == 32);
        assert(object->value[XGL_DEV_COLOR_TYPE] == XGL_COLOR_RGB);
        assert(object->value[XGL_RAS_WIDTH] == 512);
        assert(object->value[XGL_RAS_HEIGHT] == 256);
        object->image = calloc(512U * 256U, sizeof(Xgl_usgn32));
        assert(object->image != NULL);
        *(Xgl_usgn32 **)value = (Xgl_usgn32 *)object->image;
        break;
    case XGL_SYS_ST_ERROR_INFO:
        memset(value, 0, sizeof(Xgl_error_info));
        break;
    default:
        assert(!"Unexpected XGL attribute query");
    }
}

void xgl_object_destroy(Xgl_object object)
{
    Xgl_object *link;
    assert(object != NULL && allocated_objects > 0);
    for (link = &objects; *link != object; link = &(*link)->next)
        assert(*link != NULL);
    *link = object->next;
    if (object->type == XGL_MEM_RAS) {
        assert(mipmap_builds == 1);
        ++memory_destroys;
    }
    free(object->image);
    free(object);
    --allocated_objects;
}

void xgl_close(Xgl_sys_state system)
{
    assert(system == xgl_system);
    ++close_calls;
    while (objects != NULL) xgl_object_destroy(objects);
}

void xgl_window_raster_resize(Xgl_win_ras raster)
{
    assert(raster == xgl_raster);
    ++resize_calls;
}

void xgl_transform_write_specific(Xgl_trans transform, void *matrix,
                                   Xgl_trans_member membership)
{
    double (*m)[4];
    double cotangent, near_z, far_z;
    assert(transform == xgl_view && membership == 0);
    assert(transform->value[XGL_TRANS_DATA_TYPE] == XGL_DATA_DBL);
    memcpy(transform->matrix, matrix, sizeof(Xgl_matrix_d3d));
    m = transform->matrix;
    cotangent = 1.0 / tan(PI / 8.0);
    assert(fabs(m[0][0] - cotangent * drv_win_height / drv_win_width) < 0.000001);
    assert(fabs(m[1][1] - cotangent) < 0.000001);
    assert(m[2][3] == -1.0 && m[3][3] == 0.0);
    near_z = (-m[2][2] + m[3][2]);
    far_z = (-10000.0 * m[2][2] + m[3][2]) / 10000.0;
    assert(fabs(near_z - 1.0) < 0.000001 && fabs(far_z) < 0.000001);
}

void xgl_transform_rotate(Xgl_trans transform, double angle, Xgl_axis axis,
                           Xgl_trans_update update)
{
    double expected;
    int expected_axis;
    assert(transform == xgl_model);
    if (update == XGL_TRANS_REPLACE) transform->rotations = 0;
    else assert(update == XGL_TRANS_POSTCONCAT);
    assert(transform->rotations < 4);
    expected = -PI / 2.0;
    expected_axis = XGL_AXIS_X;
    if (transform->rotations == 0) expected_axis = XGL_AXIS_Z;
    if (transform->rotations == 2) {
        expected_axis = XGL_AXIS_Y;
        expected = (double)drv_rot_y * PI / 180.0;
    }
    if (transform->rotations == 3) expected = (double)drv_rot_x * PI / 180.0;
    assert(axis == expected_axis && fabs(angle - expected) < 0.000001);
    ++transform->rotations;
}

void xgl_transform_translate(Xgl_trans transform, Xgl_pt *point,
                              Xgl_trans_update update)
{
    assert(transform == xgl_model && transform->rotations == 4);
    assert(update == XGL_TRANS_POSTCONCAT && point->pt_type == XGL_PT_F3D);
    assert(point->pt.f3d->x == 0 && point->pt.f3d->y == 0 && point->pt.f3d->z == -3000);
}

void xgl_mipmap_texture_build(Xgl_object mipmap, Xgl_mem_ras memory,
                              int u_boundary, int v_boundary)
{
    unsigned int x, y;
    Xgl_usgn32 *pixels, expected;
    unsigned char index;
    assert(mipmap->type == XGL_MIPMAP_TEXTURE && memory->type == XGL_MEM_RAS);
    assert(mipmap->value[XGL_MIPMAP_TEXTURE_LEVELS] == 8);
    assert(u_boundary == XGL_TEXTURE_BOUNDARY_WRAP && v_boundary == XGL_TEXTURE_BOUNDARY_WRAP);
    pixels = (Xgl_usgn32 *)memory->image;
    assert(pixels != NULL);
    for (y = 0; y < 256; ++y)
        for (x = 0; x < 512; ++x) {
            index = earth_data_512[(unsigned long)y * 512UL + x];
            expected = (Xgl_usgn32)earth_cmap[index][0] |
                       ((Xgl_usgn32)earth_cmap[index][1] << 8) |
                       ((Xgl_usgn32)earth_cmap[index][2] << 16);
            assert(pixels[(unsigned long)(255U - y) * 512UL + x] == expected);
        }
    ++mipmap_builds;
}

static unsigned char *earth_data(void)
{
    switch (globe_longitude) {
    case 32: return earth_data_32;
    case 64: return earth_data_64;
    case 128: return earth_data_128;
    case 256: return earth_data_256;
    case 512: return earth_data_512;
    default: assert(!"Unsupported test resolution"); return NULL;
    }
}

void xgl_triangle_strip(Xgl_ctx context, Xgl_facet_list *facets, Xgl_pt_list *points)
{
    unsigned int i;
    unsigned long index, face, band, cap[3];
    unsigned int cell;
    int polar;
    unsigned char value;
    const Vertex *vertex;
    const float *packed;
    Xgl_pt_f3d *plain;
    assert(context == xgl_context);
    assert(context->value[XGL_CTX_SURF_FRONT_COLOR_SELECTOR] == XGL_SURF_COLOR_FACET);
    assert(context->value[XGL_CTX_SURF_FRONT_FILL_STYLE] == XGL_SURF_FILL_SOLID);
    assert(context->value[XGL_3D_CTX_SURF_FRONT_ILLUMINATION] == XGL_ILLUM_NONE);
    assert(context->value[XGL_3D_CTX_SURF_FACE_CULL] == XGL_CULL_BACK);
    assert(context->value[XGL_3D_CTX_HLHSR_MODE] == XGL_HLHSR_Z_BUFFER);
    assert(context->model == xgl_model && context->view == xgl_view);
    assert(context->value[XGL_CTX_DEFERRAL_MODE] == XGL_DEFER_ASAP);
    assert(context->value[XGL_CTX_GEOM_DATA_IS_VOLATILE]);
    assert(context->value[XGL_CTX_CLIP_PLANES] == (XGL_CLIP_XMIN | XGL_CLIP_XMAX |
           XGL_CLIP_YMIN | XGL_CLIP_YMAX | XGL_CLIP_ZMIN | XGL_CLIP_ZMAX));
    assert(frame_bands < (unsigned long)globe_latitude - 2UL + 2UL * globe_longitude);
    polar = FALSE;
    cell = 0;
    if (frame_bands < (unsigned long)globe_longitude) {
        band = 0;
        cell = (unsigned int)frame_bands;
        polar = TRUE;
    } else if (frame_bands < (unsigned long)globe_longitude + globe_latitude - 2UL) {
        band = frame_bands - globe_longitude + 1UL;
    } else {
        band = (unsigned long)globe_latitude - 1UL;
        cell = (unsigned int)(frame_bands - globe_longitude - globe_latitude + 2UL);
        polar = TRUE;
    }
    if (polar) {
        assert(points->num_pts == 3U);
        index = band * (globe_longitude + 1UL) + cell;
        cap[0] = index + (band == 0 ? 1UL : 0UL);
        cap[1] = index + globe_longitude + 1UL;
        cap[2] = band == 0 ? cap[1] + 1UL : index + 1UL;
    } else assert(points->num_pts == 2U * ((unsigned int)globe_longitude + 1U));
    assert(facets->facet_type == XGL_FACET_COLOR);
    assert(facets->num_facets == points->num_pts - 2U);
    assert(points->pt_type == (drv_texture_enabled ? XGL_PT_DATA_F3D : XGL_PT_F3D));
    if (drv_texture_enabled) {
        assert(points->num_data_values == 2);
        assert(context->value[XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES]);
    } else assert(!context->value[XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES]);
    for (i = 0; i < points->num_pts; ++i) {
        index = polar ? cap[i] : (band + i % 2U) * (globe_longitude + 1UL) + i / 2U;
        vertex = globe_verts + index;
        if (drv_texture_enabled) {
            packed = (float *)points->pts.data_f3d + (unsigned long)i * 5UL;
            assert(packed[0] == vertex->x && packed[1] == vertex->y && packed[2] == vertex->z);
            assert(packed[3] == globe_coords[index].u);
            assert(packed[4] == globe_coords[index].v);
        } else {
            plain = points->pts.f3d + i;
            assert(plain->x == vertex->x && plain->y == vertex->y && plain->z == vertex->z);
        }
    }
    for (i = 0; i < facets->num_facets; ++i) {
        face = band * globe_longitude + (polar ? cell : i / 2U);
        value = earth_data()[face];
        assert(fabs(facets->facets.color_facets[i].color.rgb.r - (float)earth_cmap[value][0] / 255.0f) < 0.000001);
        assert(fabs(facets->facets.color_facets[i].color.rgb.g - (float)earth_cmap[value][1] / 255.0f) < 0.000001);
        assert(fabs(facets->facets.color_facets[i].color.rgb.b - (float)earth_cmap[value][2] / 255.0f) < 0.000001);
    }
    ++frame_bands;
    frame_facets += facets->num_facets;
}

void xgl_stroke_text(Xgl_ctx context, void *text, void *position,
                      Xgl_pt_f3d direction[])
{
    Xgl_pt_f2d *point;
    char expected[80];
    assert(context == xgl_overlay && direction == NULL);
    point = (Xgl_pt_f2d *)position;
    assert(point->x >= 0 && point->x < drv_win_width);
    assert(point->y >= 0 && point->y < drv_win_height);
    assert(strstr((char *)text, "texture mapping") != NULL);
    if (strstr((char *)text, "frames per second") != NULL) {
        sprintf(expected, "%lu triangles (%lu visible)",
                globe_tris_num, globe_tris_num_vis);
        assert(strstr((char *)text, expected) == (char *)text);
        assert(strstr((char *)text, "quads") == NULL);
    }
    ++text_calls;
}

void xgl_context_new_frame(Xgl_ctx context)
{
    int action;
    action = context->value[XGL_CTX_NEW_FRAME_ACTION];
    if (context == xgl_context) {
        assert(action == (XGL_CTX_NEW_FRAME_CLEAR | XGL_CTX_NEW_FRAME_HLHSR_ACTION));
        ++clear_calls;
    } else {
        assert(context == xgl_overlay && !single_buffer);
        assert(action == XGL_CTX_NEW_FRAME_SWITCH_BUFFER);
        ++buffer_switches;
    }
}

void xgl_context_post(Xgl_ctx context, Xgl_boolean wait)
{
    assert(context == xgl_context || context == xgl_overlay);
    assert(wait);
    ++posts;
}

void xgl_context_flush(Xgl_ctx context, int action)
{
    assert(action == XGL_FLUSH_SYNCHRONIZE);
    xgl_context_post(context, TRUE);
}

static void check_render(void)
{
    unsigned int before_text, before_clear, before_switches;
    frame_bands = frame_facets = 0;
    before_text = text_calls; before_clear = clear_calls; before_switches = buffer_switches;
    drv_render();
    assert(frame_bands == (unsigned long)globe_latitude - 2UL + 2UL * globe_longitude);
    assert(frame_facets == globe_tris_num);
    assert(text_calls == before_text + (drv_osd_enabled ? 2U : 0U));
    assert(clear_calls == before_clear + 1U);
    assert(buffer_switches == before_switches + (single_buffer ? 0U : 1U));
}

static void drain_events(void)
{
    XEvent event;
    XSync(xgl_display, False);
    while (XPending(xgl_display)) {
        XNextEvent(xgl_display, &event);
        xgl_event(&event);
    }
}

static void send_key(KeySym symbol)
{
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xkey.type = KeyPress;
    event.xkey.display = xgl_display;
    event.xkey.window = xgl_window;
    event.xkey.root = DefaultRootWindow(xgl_display);
    event.xkey.same_screen = True;
    event.xkey.keycode = XKeysymToKeycode(xgl_display, symbol);
    assert(event.xkey.keycode != 0);
    assert(XSendEvent(xgl_display, xgl_window, False, KeyPressMask, &event));
    drain_events();
}

static void send_mouse(int type, int x, int y)
{
    XEvent event;
    long mask;
    memset(&event, 0, sizeof(event));
    event.type = type;
    if (type == MotionNotify) {
        event.xmotion.display = xgl_display;
        event.xmotion.window = xgl_window;
        event.xmotion.x = x; event.xmotion.y = y;
        mask = PointerMotionMask;
    } else {
        event.xbutton.display = xgl_display;
        event.xbutton.window = xgl_window;
        event.xbutton.button = Button1;
        event.xbutton.x = x; event.xbutton.y = y;
        mask = type == ButtonPress ? ButtonPressMask : ButtonReleaseMask;
    }
    assert(XSendEvent(xgl_display, xgl_window, False, mask, &event));
    drain_events();
}

static void cleanup_checks(void)
{
    assert(tests_complete);
    assert(close_calls == 1 && allocated_objects == 0);
    assert(xgl_system == NULL && xgl_display == NULL);
    assert(xgl_strip_points == NULL && xgl_strip_facets == NULL && xgl_strip_texture_points == NULL);
    assert(globe_verts == NULL && texture_data == NULL);
    puts("PASS XGL recorded cleanup: native objects, temporary raster, strip packets, geometry and X11 display released");
}

int main(int argc, char **argv)
{
    int sizes[5], i;
    unsigned int old_width, old_height;
    float old_x, old_y;
    XWindowAttributes geometry;
    XEvent close;
    assert(atexit(cleanup_checks) == 0);
    single_buffer = argc > 1 && strcmp(argv[1], "single") == 0;
    drv_ext_init();
    assert(!drv_texture_enabled && drv_texture_avail);
    drv_init(&argc, argv);
    assert(drv_init_window(0, 0, 0, 0));
    assert(XGetWindowAttributes(xgl_display, xgl_window, &geometry));
    assert(geometry.width == 640 && geometry.height == 480);
    assert(geometry.depth == 24 && geometry.visual->class == TrueColor);
    assert(xgl_double_buffer == !single_buffer);
    globe_init();
    assert(globe_init_verts(GLOBE_DEFAULT_LONGITUDE, -1, GLOBE_RADIUS));
    texture_init();
    drain_events();
    check_render();
    sizes[0] = 32; sizes[1] = 64; sizes[2] = 128; sizes[3] = 256; sizes[4] = 512;
    for (i = 0; i < 5; ++i) {
        send_key((KeySym)(XK_1 + i));
        assert(globe_longitude == sizes[i]);
        check_render();
    }
    send_key(XK_t);
    assert(drv_texture_enabled && drv_texture_loaded);
    assert(mipmap_creates == 1 && tmap_creates == 1 && mipmap_builds == 1 && memory_destroys == 1);
    assert(texture_data == NULL);
    check_render();
    send_key(XK_t); assert(!drv_texture_enabled); check_render();
    send_key(XK_t); assert(drv_texture_enabled); check_render();
    assert(mipmap_builds == 1);
    send_key(XK_o); assert(!drv_osd_enabled); check_render();
    send_key(XK_o); assert(drv_osd_enabled); check_render();
    puts("PASS XGL recorded rendering: native strip/facet packets, transforms, depth/cull, RGB texture rows, mipmap descriptor and OSD toggle");

    old_x = drv_rot_x; old_y = drv_rot_y;
    send_mouse(ButtonPress, 100, 100);
    assert(drv_lmouse_pressed);
    send_mouse(MotionNotify, 190, 145);
    assert(drv_rot_x != old_x && drv_rot_y != old_y);
    send_mouse(ButtonRelease, 190, 145);
    assert(!drv_lmouse_pressed);
    old_x = drv_rot_x; old_y = drv_rot_y;
    send_mouse(MotionNotify, 210, 160);
    assert(drv_rot_x == old_x && drv_rot_y == old_y);
    send_key(XK_a); assert(drv_auto_enabled); check_render();
    send_key(XK_a); assert(!drv_auto_enabled);
    old_width = drv_win_width; old_height = drv_win_height;
    drv_resize(0, 0);
    assert(drv_win_width == old_width && drv_win_height == old_height);
    XResizeWindow(xgl_display, xgl_window, 800, 600);
    drain_events();
    assert(drv_win_width == 800 && drv_win_height == 600 && resize_calls > 1);
    check_render();
    puts("PASS XGL X11 input: real window/events, all resolution keys, mouse drag/release, autorotation, resize and projection");
    memset(&close, 0, sizeof(close));
    close.xclient.type = ClientMessage;
    close.xclient.display = xgl_display;
    close.xclient.window = xgl_window;
    close.xclient.format = 32;
    close.xclient.data.l[0] = (long)xgl_wm_delete;
    /* Other client messages must not be mistaken for WM_DELETE_WINDOW. */
    close.xclient.message_type = XInternAtom(xgl_display, "TEST_NOT_WM_PROTOCOLS", False);
    assert(XSendEvent(xgl_display, xgl_window, False, NoEventMask, &close));
    drain_events();
    tests_complete = TRUE;
    if (single_buffer) send_key(XK_Escape);
    else {
        close.xclient.message_type = XInternAtom(xgl_display, "WM_PROTOCOLS", False);
        assert(XSendEvent(xgl_display, xgl_window, False, NoEventMask, &close));
        drain_events();
    }
    assert(!"Escape/WM close must terminate the application");
    return 1;
}
