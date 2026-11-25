/* Actual driver + real X11; XGL calls are recorded, not rendered.
 * The manual-derived test declarations are deliberately not a Sun SDK/ABI.
 */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earth_reference.h"
static void *test_xgl_malloc(size_t size);
#define malloc test_xgl_malloc
#include "../src/drv_xgl.c"
#undef malloc

extern Vertex *globe_verts;
extern int globe_longitude, globe_latitude;

struct Test_xgl_object {
    int type, value[64];
    double font_height;
    Xgl_object device, model, view, texture;
    int pending_destroy;
    Xgl_error_notify error_notify;
    Xgl_texture_general_desc descriptor;
    Xgl_matrix_d3d matrix;
    void *image;
    struct Test_xgl_object *next;
};

static Xgl_object objects;
static unsigned int allocated_objects, close_calls, memory_destroys;
static unsigned int mipmap_creates, tmap_creates, mipmap_builds;
static unsigned int resize_calls, posts, clear_calls, buffer_switches, text_calls;
static char osd_text[1024];
static unsigned int extent_calls;
static unsigned long frame_bands, frame_facets;
static int single_buffer, tests_complete;
static const char *profile;
static const char *fault;
static Xgl_error_type injected_error_type;
static unsigned long frame_texture_facets;
static int frame_flush_ok;
static int native_stopped, fail_cleanup;
static unsigned int allocation_failures;
static int allocation_failure_type;
/* Device inquiry, raster setup order, ASTI completion and the model matrix. */
static unsigned int inquire_calls, model_writes;
static int backing_store_off, scene_unflushed, leo_device;

static int inject(const char *name)
{
    if (fault == NULL || strcmp(fault, name) != 0) return FALSE;
    fault = NULL;
    return TRUE;
}

static void native_error(int fatal)
{
    injected_error_type = fatal ? XGL_ERROR_NONRECOVERABLE : XGL_ERROR_RECOVERABLE;
    assert(xgl_system->error_notify != NULL);
    xgl_system->error_notify(xgl_system);
    if (fatal) native_stopped = TRUE;
}

static void *test_xgl_malloc(size_t size)
{
    if (inject("flatstripfail") ||
        (xgl_texture_scope && drv_texture_enabled && inject("stripallocfail")))
        return NULL;
    return malloc(size);
}

static Xgl_object allocate_object(int type)
{
    Xgl_object object;
    object = (Xgl_object)calloc(1, sizeof(*object));
    assert(!native_stopped);
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
        case XGL_WIN_RAS_BACKING_STORE:
            /* XGL grants backing store or double buffering, whichever is
             * requested first: refusing backing store must come first. */
            assert(va_arg(args, int) == FALSE);
            backing_store_off = TRUE;
            break;
        case XGL_WIN_RAS_BUFFERS_REQUESTED:
            assert(backing_store_off);
            object->value[attribute] = va_arg(args, int);
            assert(object->value[attribute] == 2);
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
            object->font_height = va_arg(args, double);
            assert(object->font_height == 10.0 || object->font_height == 12.0 ||
                   object->font_height == 16.0);
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
            object->texture = maps[0];
            break;
        case XGL_3D_CTX_SURF_FRONT_TMAP_NUM:
            object->value[attribute] = va_arg(args, int);
            if (object->value[attribute] == 0)
                object->value[XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES] = FALSE;
            if (object->value[attribute] == 0 && object->texture != NULL) {
                Xgl_object old = object->texture;
                object->texture = NULL;
                xgl_object_destroy(old);
            }
            break;
        case XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES:
            switches = va_arg(args, Xgl_boolean *);
            if (switches[0] && object == xgl_context && inject("bindfail")) {
                native_error(FALSE);
                break;
            }
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
            /* The Sun ZX textures in software: both filters point-sample. */
            assert(xgl_texture_software == leo_device);
            assert(descriptor->texture_info.mipmap.interp_info.filter1 == (leo_device ?
                   XGL_TEXTURE_INTERP_POINT : XGL_TEXTURE_INTERP_MIPMAP_TRILINEAR));
            assert(descriptor->texture_info.mipmap.interp_info.filter2 == (leo_device ?
                   XGL_TEXTURE_INTERP_POINT : XGL_TEXTURE_INTERP_BILINEAR));
            assert(descriptor->texture_info.mipmap.orientation_matrix[0][0] == 1.0f);
            assert(descriptor->texture_info.mipmap.orientation_matrix[1][1] == 1.0f);
            /* Native Xgl_matrix_f2d is three rows by two columns; the
             * last row holds UV translation, not a third column. */
            assert(sizeof(descriptor->texture_info.mipmap.orientation_matrix[0]) ==
                   2U * sizeof(float));
            assert(descriptor->texture_info.mipmap.orientation_matrix[0][1] == 0.0f);
            assert(descriptor->texture_info.mipmap.orientation_matrix[1][0] == 0.0f);
            assert(descriptor->texture_info.mipmap.orientation_matrix[2][0] == 0.0f);
            assert(descriptor->texture_info.mipmap.orientation_matrix[2][1] == 0.0f);
            assert(descriptor->comp_info.color_info.num_render_comp_desc == 1);
            assert(descriptor->comp_info.color_info.num_channels[0] == 3);
            assert(descriptor->comp_info.color_info.render_component_desc[0].comp == XGL_RENDER_COMP_DIFFUSE_COLOR);
            assert(descriptor->comp_info.color_info.render_component_desc[0].texture_op == XGL_TEXTURE_OP_REPLACE);
            if (inject("configurefail")) native_error(FALSE);
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
    /* TESTGLOBE_XGL_NOCHECK turns argument checking off; "0" or empty keeps it. */
    assert(object->value[XGL_SYS_ST_ERROR_DETECTION] ==
           (getenv("TESTGLOBE_XGL_NOCHECK") == NULL ||
            getenv("TESTGLOBE_XGL_NOCHECK")[0] == '\0' ||
            strcmp(getenv("TESTGLOBE_XGL_NOCHECK"), "0") == 0));
    return object;
}

Xgl_inquire *xgl_inquire(Xgl_sys_state system, Xgl_obj_desc *descriptor)
{
    static char leo[] = "Sun:LEO", recorder[] = "Test:RECORDER";
    Xgl_X_window *window;
    Xgl_inquire *inquiry;
    assert(!native_stopped && system == xgl_system);
    assert(descriptor != NULL && descriptor->win_ras.type == XGL_WIN_X);
    window = (Xgl_X_window *)descriptor->win_ras.desc;
    assert(window->X_display == xgl_display &&
           window->X_window == (Xgl_usgn32)xgl_window);
    ++inquire_calls;
    /* The application owns and frees the returned structure (inq.c). */
    inquiry = (Xgl_inquire *)calloc(1, sizeof(*inquiry));
    assert(inquiry != NULL);
    inquiry->name = leo_device ? leo : recorder;
    inquiry->dga_flag = TRUE;
    inquiry->maximum_buffer = single_buffer ? 1 : 2;
    inquiry->double_buffer = single_buffer ? XGL_INQ_NOT_SUPPORTED : XGL_INQ_HARDWARE;
    inquiry->true_color = inquiry->hlhsr = inquiry->shading = XGL_INQ_HARDWARE;
    return inquiry;
}

Xgl_object xgl_object_create(Xgl_sys_state system, Xgl_obj_type type,
                             Xgl_obj_desc *descriptor, ...)
{
    Xgl_object object;
    Xgl_X_window *window;
    int first;
    va_list args;
    assert(!native_stopped);
    assert(system == xgl_system && system->type == -1);
    if (type == XGL_WIN_RAS) {
        XWindowAttributes state;
        assert(descriptor != NULL && descriptor->win_ras.type == XGL_WIN_X);
        /* DGA takes geometry and clipping now: the device has been inquired
         * and the window is already viewable. */
        assert(inquire_calls == 1U);
        assert(XGetWindowAttributes(xgl_display, xgl_window, &state));
        assert(state.map_state == IsViewable && state.backing_store == NotUseful);
        window = (Xgl_X_window *)descriptor->win_ras.desc;
        assert(window->X_display == xgl_display);
        assert(window->X_window == (Xgl_usgn32)xgl_window);
        assert(window->X_screen == DefaultScreen(xgl_display));
    } else assert(descriptor == NULL);
    if (type == XGL_MEM_RAS && fault != NULL && strcmp(fault, "stagefail") == 0)
        return NULL;
    if ((type == XGL_MEM_RAS && inject("rasterfail")) ||
        (type == XGL_MIPMAP_TEXTURE && inject("mipmapfail"))) {
        /* Fail the initial attempt and the same-size retry after eviction;
         * the bounded next size must then succeed. */
        allocation_failure_type = type;
        allocation_failures = 2;
    }
    if (type == allocation_failure_type && allocation_failures != 0) {
        --allocation_failures;
        return NULL;
    }
    if (type == XGL_TMAP && inject("mapfail")) {
        native_error(FALSE);
        return NULL;
    }
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
    assert(!native_stopped);
    assert(object != NULL);
    va_start(args, object);
    first = va_arg(args, int);
    attributes(object, first, args);
    va_end(args);
}

void xgl_object_get(Xgl_object object, Xgl_attribute attribute, void *value)
{
    assert(!native_stopped);
    assert(object != NULL && value != NULL);
    switch (attribute) {
    case XGL_WIN_RAS_BUFFERS_ALLOCATED:
        *(Xgl_usgn32 *)value = single_buffer ? 1U : 2U;
        break;
    case XGL_MEM_RAS_IMAGE_BUFFER_ADDR:
        if (inject("accessfail")) { *(Xgl_usgn32 **)value = NULL; break; }
        assert(object->type == XGL_MEM_RAS);
        assert(object->value[XGL_RAS_DEPTH] == 32);
        assert(object->value[XGL_DEV_COLOR_TYPE] == XGL_COLOR_RGB);
        assert(object->value[XGL_RAS_WIDTH] >= 32 && object->value[XGL_RAS_WIDTH] <= 512);
        assert(object->value[XGL_RAS_HEIGHT] == object->value[XGL_RAS_WIDTH] / 2);
        object->image = calloc((size_t)object->value[XGL_RAS_WIDTH] *
                                object->value[XGL_RAS_HEIGHT], sizeof(Xgl_usgn32));
        assert(object->image != NULL);
        *(Xgl_usgn32 **)value = (Xgl_usgn32 *)object->image;
        break;
    case XGL_SYS_ST_ERROR_INFO: {
        Xgl_error_info *info = (Xgl_error_info *)value;
        memset(info, 0, sizeof(*info));
        info->type = injected_error_type;
        info->category = 4;
        /* Solaris XGL can leave the ID empty; operands may be absent. */
        info->id = getenv("TEST_XGL_EMPTY_ID") != NULL ? "" : "test-123";
        info->msg = "injected texture failure";
        info->cur_op = "test operator";
        info->cur_obj = "test object";
        info->operand1 = "first operand";
        info->operand2 = getenv("TEST_XGL_EMPTY_ID") != NULL ? NULL : "second operand";
        break;
    }
    case XGL_3D_CTX_SURF_FRONT_TMAP_NUM:
        *(Xgl_usgn32 *)value = (Xgl_usgn32)object->value[attribute];
        break;
    case XGL_3D_CTX_SURF_FRONT_TMAP:
        *(Xgl_object *)value = object->texture;
        break;
    case XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES:
        *(Xgl_boolean *)value = object->value[attribute];
        if (inject("inactive")) *(Xgl_boolean *)value = FALSE;
        break;
    default:
        assert(!"Unexpected XGL attribute query");
    }
}

void xgl_object_destroy(Xgl_object object)
{
    Xgl_object *link, current, mipmap;
    assert(!native_stopped);
    assert(object != NULL && allocated_objects > 0);
    if (fail_cleanup) {
        fail_cleanup = FALSE;
        native_error(TRUE);
        return;
    }
    /* The manual permits destruction to wait until Context/map references
     * are removed. Mirror that lifetime to catch cleanup use-after-free. */
    for (current = objects; current != NULL; current = current->next)
        if (current->texture == object ||
            (current->type == XGL_TMAP &&
             current->descriptor.texture_info.mipmap.texture_map == object)) {
            object->pending_destroy = TRUE;
            return;
        }
    for (link = &objects; *link != object; link = &(*link)->next)
        assert(*link != NULL);
    *link = object->next;
    mipmap = object->type == XGL_TMAP ?
             object->descriptor.texture_info.mipmap.texture_map : NULL;
    if (object->type == XGL_MEM_RAS) ++memory_destroys;
    free(object->image);
    free(object);
    --allocated_objects;
    if (mipmap != NULL && mipmap->pending_destroy) xgl_object_destroy(mipmap);
}

void xgl_close(Xgl_sys_state system)
{
    Xgl_object object;
    assert(!native_stopped);
    assert(system == xgl_system);
    ++close_calls;
    for (object = objects; object != NULL; object = object->next) {
        object->texture = NULL;
        if (object->type == XGL_TMAP)
            object->descriptor.texture_info.mipmap.texture_map = NULL;
    }
    while (objects != NULL) xgl_object_destroy(objects);
}

void xgl_window_raster_resize(Xgl_win_ras raster)
{
    assert(!native_stopped);
    assert(raster == xgl_raster);
    ++resize_calls;
}

static void test_matrix_multiply(double result[4][4], double left[4][4],
                                 double right[4][4])
{
    double product[4][4];
    int i, j, k;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j) {
            product[i][j] = 0.0;
            for (k = 0; k < 4; ++k) product[i][j] += left[i][k] * right[k][j];
        }
    memcpy(result, product, sizeof(product));
}

/* The Programmer's Guide row-vector forms; postconcatenation multiplies on
 * the right. An independent oracle for the driver's composed matrix. */
static void test_postconcat_rotation(double matrix[4][4], double angle, char axis)
{
    double rotation[4][4], c, s;
    int i;
    memset(rotation, 0, sizeof(rotation));
    for (i = 0; i < 4; ++i) rotation[i][i] = 1.0;
    c = cos(angle); s = sin(angle);
    if (axis == 'z') { rotation[0][0] = c; rotation[0][1] = s; rotation[1][0] = -s; rotation[1][1] = c; }
    if (axis == 'y') { rotation[0][0] = c; rotation[0][2] = -s; rotation[2][0] = s; rotation[2][2] = c; }
    if (axis == 'x') { rotation[1][1] = c; rotation[1][2] = s; rotation[2][1] = -s; rotation[2][2] = c; }
    test_matrix_multiply(matrix, matrix, rotation);
}

static void check_model_matrix(double m[4][4])
{
    double expected[4][4], translation[4][4], eye[4], x, y, dot;
    int i, j;
    memset(expected, 0, sizeof(expected));
    memset(translation, 0, sizeof(translation));
    for (i = 0; i < 4; ++i) expected[i][i] = translation[i][i] = 1.0;
    test_postconcat_rotation(expected, -PI / 2.0, 'z');
    test_postconcat_rotation(expected, -PI / 2.0, 'x');
    test_postconcat_rotation(expected, (double)drv_rot_y * PI / 180.0, 'y');
    test_postconcat_rotation(expected, (double)drv_rot_x * PI / 180.0, 'x');
    translation[3][2] = -3000.0;
    test_matrix_multiply(expected, expected, translation);
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j) assert(fabs(m[i][j] - expected[i][j]) < 1e-9);
    /* LENGTH_PRESERV promises an orthonormal rotation with a translation. */
    for (i = 0; i < 3; ++i) {
        assert(m[i][3] == 0.0);
        for (j = 0; j < 3; ++j) {
            dot = m[i][0] * m[j][0] + m[i][1] * m[j][1] + m[i][2] * m[j][2];
            assert(fabs(dot - (i == j ? 1.0 : 0.0)) < 1e-12);
        }
    }
    assert(m[3][3] == 1.0);
    /* The culling oracle's object-space eye is the view-space origin. */
    x = (double)drv_rot_x * PI / 180.0;
    y = (double)drv_rot_y * PI / 180.0;
    eye[0] = 3000.0 * cos(x) * cos(y);
    eye[1] = -3000.0 * cos(x) * sin(y);
    eye[2] = 3000.0 * sin(x);
    eye[3] = 1.0;
    for (j = 0; j < 3; ++j)
        assert(fabs(eye[0] * m[0][j] + eye[1] * m[1][j] + eye[2] * m[2][j] +
                    eye[3] * m[3][j]) < 1e-6);
}

void xgl_transform_write_specific(Xgl_trans transform, void *matrix,
                                   Xgl_trans_member membership)
{
    double (*m)[4];
    double cotangent, near_z, far_z;
    assert(!native_stopped);
    assert(transform == xgl_view || transform == xgl_model);
    assert(transform->value[XGL_TRANS_DATA_TYPE] == XGL_DATA_DBL);
    if (transform == xgl_model) {
        if (inject("fatalmodel")) { native_error(TRUE); return; }
        /* One write per pass replaces the former five transform operators. */
        assert(membership == XGL_TRANS_MEMBER_LENGTH_PRESERV);
        memcpy(transform->matrix, matrix, sizeof(Xgl_matrix_d3d));
        check_model_matrix(transform->matrix);
        ++model_writes;
        return;
    }
    assert(membership == 0);
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

void xgl_mipmap_texture_build(Xgl_object mipmap, Xgl_mem_ras memory,
                              int u_boundary, int v_boundary)
{
    unsigned int x, y, width, height, levels, smaller;
    Xgl_usgn32 *pixels, expected;
    unsigned char index;
    assert(!native_stopped);
    assert(mipmap->type == XGL_MIPMAP_TEXTURE && memory->type == XGL_MEM_RAS);
    width = (unsigned int)memory->value[XGL_RAS_WIDTH];
    height = (unsigned int)memory->value[XGL_RAS_HEIGHT];
    smaller = height; levels = 0;
    while (smaller > 1U) { smaller /= 2U; ++levels; }
    assert(mipmap->value[XGL_MIPMAP_TEXTURE_LEVELS] == (int)levels);
    assert(u_boundary == XGL_TEXTURE_BOUNDARY_WRAP && v_boundary == XGL_TEXTURE_BOUNDARY_WRAP);
    pixels = (Xgl_usgn32 *)memory->image;
    assert(pixels != NULL);
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) {
            index = test_earth_data((int)width)[(unsigned long)y * width + x];
            expected = (Xgl_usgn32)earth_cmap[index][0] |
                       ((Xgl_usgn32)earth_cmap[index][1] << 8) |
                       ((Xgl_usgn32)earth_cmap[index][2] << 16);
            assert(pixels[(unsigned long)(height - 1U - y) * width + x] == expected);
        }
    ++mipmap_builds;
    if (inject("buildfail")) native_error(FALSE);
    if (inject("fataltexture")) native_error(TRUE);
}

static unsigned char *earth_data(void)
{
    switch (globe_longitude) {
    case 32: return test_earth_data(32);
    case 64: return test_earth_data(64);
    case 128: return test_earth_data(128);
    case 256: return test_earth_data(256);
    case 512: return test_earth_data(512);
    default: assert(!"Unsupported test resolution"); return NULL;
    }
}

static void check_culled_strip(Xgl_facet_list *facets, Xgl_pt_list *points)
{
    double p[3][3], uv[2], edge[2][3], normal[3], center[3], eye[3];
    double x, y, u, v, difference;
    const float *packed;
    unsigned int i, j, k, slot, latitude, longitude, color;
    assert(points->num_pts >= 3U && points->num_pts <= 1026U);
    assert(facets->facet_type == XGL_FACET_COLOR);
    assert(facets->num_facets == points->num_pts - 2U);
    assert(points->pt_type == (drv_texture_active ? XGL_PT_DATA_F3D : XGL_PT_F3D));
    if(drv_texture_active)assert(points->num_data_values == 2U);
    x = (double)drv_rot_x * PI / 180.0;
    y = (double)drv_rot_y * PI / 180.0;
    eye[0] = 3000.0 * cos(x) * cos(y);
    eye[1] = -3000.0 * cos(x) * sin(y);
    eye[2] = 3000.0 * sin(x);
    for(i=0;i<facets->num_facets;++i){
        for(j=0;j<3U;++j){
            slot=i+(j<2U?(j^(i&1U)):2U);
            if(drv_texture_active){
                packed=(const float *)points->pts.data_f3d+5UL*slot;
                for(k=0;k<3U;++k)p[j][k]=packed[k];
                uv[0]=packed[3];uv[1]=packed[4];
                assert(uv[0]>=0.0&&uv[0]<=1.0&&uv[1]>=0.0&&uv[1]<=1.0);
                x=sqrt(p[j][0]*p[j][0]+p[j][1]*p[j][1]);
                v=atan2(x,p[j][2])/PI;
                assert(fabs(v-uv[1])<1e-5);
                if(x>0.001){
                    u=atan2(p[j][1],p[j][0])/(2.0*PI);if(u<0.0)u+=1.0;
                    difference=fabs(u-uv[0]);if(difference>0.5)difference=1.0-difference;
                    assert(difference<1e-5);
                }
            }else{
                p[j][0]=points->pts.f3d[slot].x;
                p[j][1]=points->pts.f3d[slot].y;
                p[j][2]=points->pts.f3d[slot].z;
            }
        }
        for(k=0;k<3U;++k){
            edge[0][k]=p[1][k]-p[0][k];edge[1][k]=p[2][k]-p[0][k];
            center[k]=(p[0][k]+p[1][k]+p[2][k])/3.0;
        }
        normal[0]=edge[0][1]*edge[1][2]-edge[0][2]*edge[1][1];
        normal[1]=edge[0][2]*edge[1][0]-edge[0][0]*edge[1][2];
        normal[2]=edge[0][0]*edge[1][1]-edge[0][1]*edge[1][0];
        assert(normal[0]*center[0]+normal[1]*center[1]+normal[2]*center[2]>0.0);
        assert(normal[0]*(eye[0]-p[0][0])+normal[1]*(eye[1]-p[0][1])+normal[2]*(eye[2]-p[0][2])>0.0);
        if(drv_texture_active){
            assert(facets->facets.color_facets[i].color.rgb.r==1.0f&&
                   facets->facets.color_facets[i].color.rgb.g==1.0f&&
                   facets->facets.color_facets[i].color.rgb.b==1.0f);
        }else{
            u=atan2(center[1],center[0]);if(u<0.0)u+=2.0*PI;
            v=atan2(sqrt(center[0]*center[0]+center[1]*center[1]),center[2]);
            longitude=(unsigned int)(u*globe_longitude/(2.0*PI));
            latitude=(unsigned int)(v*globe_latitude/PI);
            assert(longitude<(unsigned int)globe_longitude&&latitude<(unsigned int)globe_latitude);
            color=earth_data()[(unsigned long)latitude*globe_longitude+longitude];
            assert(fabs(facets->facets.color_facets[i].color.rgb.r-(float)earth_cmap[color][0]/255.0f)<1e-6);
            assert(fabs(facets->facets.color_facets[i].color.rgb.g-(float)earth_cmap[color][1]/255.0f)<1e-6);
            assert(fabs(facets->facets.color_facets[i].color.rgb.b-(float)earth_cmap[color][2]/255.0f)<1e-6);
        }
    }
}

void xgl_triangle_strip(Xgl_ctx context, Xgl_facet_list *facets, Xgl_pt_list *points)
{
    unsigned int i;
    unsigned long index, face, band, cap[3], interior;
    unsigned int cell, first;
    int polar;
    unsigned char value;
    const Vertex *vertex;
    const float *packed;
    Xgl_pt_f3d *plain;
    assert(!native_stopped);
    assert(context == xgl_context);
    /* ASTI may hold this strip until the Context is flushed. */
    scene_unflushed = TRUE;
    if (drv_texture_active && inject("drawfail")) { native_error(FALSE); return; }
    assert(context->value[XGL_CTX_SURF_FRONT_COLOR_SELECTOR] == XGL_SURF_COLOR_FACET);
    assert(context->value[XGL_CTX_SURF_FRONT_FILL_STYLE] == XGL_SURF_FILL_SOLID);
    assert(context->value[XGL_3D_CTX_SURF_FRONT_ILLUMINATION] == XGL_ILLUM_NONE);
    assert(context->value[XGL_3D_CTX_SURF_FACE_CULL] == XGL_CULL_BACK);
    assert(context->value[XGL_3D_CTX_HLHSR_MODE] == XGL_HLHSR_Z_BUFFER);
    assert(context->model == xgl_model && context->view == xgl_view);
    assert(context->value[XGL_CTX_DEFERRAL_MODE] == XGL_DEFER_ASTI);
    assert(context->value[XGL_CTX_GEOM_DATA_IS_VOLATILE]);
    assert(context->value[XGL_CTX_CLIP_PLANES] == (XGL_CLIP_XMIN | XGL_CLIP_XMAX |
           XGL_CLIP_YMIN | XGL_CLIP_YMAX | XGL_CLIP_ZMIN | XGL_CLIP_ZMAX));
    assert(frame_facets < globe_tris_num);
    if(drv_cull_enabled){check_culled_strip(facets,points);goto submitted;}
    polar = FALSE;
    cell = first = 0;
    interior = (globe_latitude - 2UL) * 2UL * globe_longitude;
    if (frame_facets < (unsigned long)globe_longitude) {
        band = 0;
        cell = (unsigned int)frame_facets;
        polar = TRUE;
    } else if (frame_facets < (unsigned long)globe_longitude + interior) {
        index = frame_facets - globe_longitude;
        band = index / (2UL * globe_longitude) + 1UL;
        first = (unsigned int)(index % (2UL * globe_longitude));
        assert((first & 1U) == 0U);
    } else {
        band = (unsigned long)globe_latitude - 1UL;
        cell = (unsigned int)(frame_facets - globe_longitude - interior);
        polar = TRUE;
    }
    if (polar) {
        assert(points->num_pts == 3U);
        index = band * (globe_longitude + 1UL) + cell;
        cap[0] = index + (band == 0 ? 1UL : 0UL);
        cap[1] = index + globe_longitude + 1UL;
        cap[2] = band == 0 ? cap[1] + 1UL : index + 1UL;
    } else {
        assert(points->num_pts <= 2U * ((unsigned int)globe_longitude + 1U) - first);
        assert((points->num_pts & 1U) == 0U);
    }
    assert(facets->facet_type == XGL_FACET_COLOR);
    assert(facets->num_facets == points->num_pts - 2U);
    assert(points->pt_type == (drv_texture_active ? XGL_PT_DATA_F3D : XGL_PT_F3D));
    if (drv_texture_active) {
        assert(points->num_data_values == 2);
        assert(context->value[XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES]);
    } else assert(!context->value[XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES]);
    for (i = 0; i < points->num_pts; ++i) {
        index = polar ? cap[i] : (band + (first + i) % 2U) *
                                 (globe_longitude + 1UL) + (first + i) / 2U;
        vertex = globe_verts + index;
        if (drv_texture_active) {
            packed = (float *)points->pts.data_f3d + (unsigned long)i * 5UL;
            assert(packed[0] == vertex->x && packed[1] == vertex->y && packed[2] == vertex->z);
            assert(packed[3] == (float)(index % (globe_longitude + 1UL)) / globe_longitude);
            assert(packed[4] == (float)(index / (globe_longitude + 1UL)) / globe_latitude);
        } else {
            plain = points->pts.f3d + i;
            assert(plain->x == vertex->x && plain->y == vertex->y && plain->z == vertex->z);
        }
    }
    for (i = 0; i < facets->num_facets; ++i) {
        face = band * globe_longitude + (polar ? cell : (first + i) / 2U);
        value = earth_data()[face];
        if (drv_texture_active) {
            /* REPLACE takes its RGB from the texture, not a facet color. */
            assert(facets->facets.color_facets[i].color.rgb.r == 1.0f);
            assert(facets->facets.color_facets[i].color.rgb.g == 1.0f);
            assert(facets->facets.color_facets[i].color.rgb.b == 1.0f);
        } else {
            assert(fabs(facets->facets.color_facets[i].color.rgb.r - (float)earth_cmap[value][0] / 255.0f) < 0.000001);
            assert(fabs(facets->facets.color_facets[i].color.rgb.g - (float)earth_cmap[value][1] / 255.0f) < 0.000001);
            assert(fabs(facets->facets.color_facets[i].color.rgb.b - (float)earth_cmap[value][2] / 255.0f) < 0.000001);
        }
    }
submitted:
    ++frame_bands;
    frame_facets += facets->num_facets;
    if (points->pt_type == XGL_PT_DATA_F3D) frame_texture_facets += facets->num_facets;
    if (inject("abortscene")) drv_abort_frame("Injected scene traversal failure");
}

void xgl_stroke_text_extent(Xgl_ctx context, void *text,
                             Xgl_bounds_f2d *bounds, Xgl_pt_f2d *concatenate)
{
    const char *character;
    float width, scale;
    assert(!native_stopped);
    assert(context == xgl_overlay && text != NULL && bounds != NULL &&
           concatenate != NULL);
    /* Proportional metrics and a negative bearing catch fixed-width guesses
     * and placement that ignores the returned bounding box. */
    width = 0.0f;
    for (character = (const char *)text; *character != '\0'; ++character)
        width += *character == 'W' ? 9.0f :
                  (*character == 'i' || *character == ' ' ? 3.0f : 6.0f);
    scale = (float)(context->font_height / 12.0);
    bounds->xmin = -scale;
    bounds->xmax = (width - 1.0f) * scale;
    bounds->ymin = -2.0f * scale;
    bounds->ymax = 10.0f * scale;
    concatenate->x = width * scale;
    concatenate->y = 0.0f;
    ++extent_calls;
}

static void check_osd(void)
{
    char expected[80];
    if (osd_text[0] == '\0') return;
    sprintf(expected, "%lu triangles", globe_tris_num);
    if (strstr(osd_text, expected) == osd_text) {
        assert((strstr(osd_text, "frames per second") != NULL) ==
               (drv_fps_enabled != 0));
        assert(strstr(osd_text, "quads") == NULL);
        assert(strstr(osd_text, "visible") == NULL);
        assert(strstr(osd_text, drv_cull_enabled ? ", cull on" : ", cull off") != NULL);
        assert(strstr(osd_text, !drv_texture_enabled ? ", texture off" :
                      (frame_texture_facets != 0 && frame_flush_ok ?
                       ", texture on" : ", texture fail")) != NULL);
        /* Only the Sun ZX is known to texture in XGL software. */
        if (drv_texture_enabled && frame_texture_facets != 0 && frame_flush_ok)
            assert((strstr(osd_text, ", texture on (software") != NULL) == leo_device);
    } else if (!globe_ready()) {
        assert(strstr(osd_text, "No globe:") == osd_text);
        assert(strstr(osd_text, "retry") != NULL);
    }
    assert(strstr(osd_text, "T = texture mapping") != NULL);
}

void xgl_stroke_text(Xgl_ctx context, void *text, void *position,
                      Xgl_pt_f3d direction[])
{
    Xgl_pt_f2d *point, concatenate;
    Xgl_bounds_f2d bounds;
    assert(!native_stopped);
    assert(context == xgl_overlay && direction == NULL);
    assert(!scene_unflushed);
    assert(context->value[XGL_CTX_DEFERRAL_MODE] == XGL_DEFER_ASAP);
    if (inject("fatalosd")) { native_error(TRUE); return; }
    point = (Xgl_pt_f2d *)position;
    xgl_stroke_text_extent(context, text, &bounds, &concatenate);
    assert(point->x + bounds.xmin >= 0 &&
           point->x + bounds.xmax < drv_win_width);
    assert(point->y - bounds.ymax >= 0 &&
           point->y - bounds.ymin < drv_win_height);
    assert(strlen(osd_text) + strlen((char *)text) + 2U < sizeof(osd_text));
    if (osd_text[0] != '\0') strcat(osd_text, " ");
    strcat(osd_text, (char *)text);
    ++text_calls;
}

void xgl_context_new_frame(Xgl_ctx context)
{
    int action;
    assert(!native_stopped);
    action = context->value[XGL_CTX_NEW_FRAME_ACTION];
    /* Clearing or switching with held ASTI strips would show stale geometry. */
    assert(!scene_unflushed);
    if (context == xgl_context) {
        assert(action == (XGL_CTX_NEW_FRAME_CLEAR | XGL_CTX_NEW_FRAME_HLHSR_ACTION));
        ++clear_calls;
        frame_bands = frame_facets = frame_texture_facets = 0;
        frame_flush_ok = FALSE;
        osd_text[0] = '\0';
    } else {
        assert(context == xgl_overlay && !single_buffer);
        assert(action == XGL_CTX_NEW_FRAME_SWITCH_BUFFER);
        ++buffer_switches;
    }
}

void xgl_context_post(Xgl_ctx context, Xgl_boolean wait)
{
    assert(!native_stopped);
    assert(context == xgl_context || context == xgl_overlay);
    assert(wait);
    ++posts;
}

void xgl_context_flush(Xgl_ctx context, int action)
{
    assert(!native_stopped);
    assert(action == XGL_FLUSH_SYNCHRONIZE);
    if (context == xgl_context && xgl_texture_scope && inject("flushfail")) {
        /* A recoverable error still completes the attempted flush. */
        scene_unflushed = FALSE;
        native_error(FALSE);
        return;
    }
    if (context == xgl_overlay && inject("fataloverlay")) native_error(FALSE);
    xgl_context_post(context, TRUE);
    if (context == xgl_context) { frame_flush_ok = TRUE; scene_unflushed = FALSE; }
}

static void check_render(void)
{
    unsigned int before_text, before_clear, before_switches, before_models;
    frame_bands = frame_facets = 0;
    before_text = text_calls; before_clear = clear_calls; before_switches = buffer_switches;
    before_models = model_writes;
    drv_render();
    /* One model write per pass (a texture failure redraws with a second). */
    assert(model_writes >= before_models + 1U && model_writes <= before_models + 2U);
    if (drv_cull_enabled)
        assert(frame_facets > 0 && frame_facets < globe_tris_num);
    else
    {
        assert(frame_bands >= (unsigned long)globe_latitude - 2UL + 2UL * globe_longitude);
        assert(frame_facets == globe_tris_num);
    }
    assert(drv_osd_enabled ? text_calls >= before_text + 2U : text_calls == before_text);
    check_osd();
    assert(clear_calls >= before_clear + 1U && clear_calls <= before_clear + 2U);
    assert(xgl_texture_used == (frame_texture_facets != 0 && frame_flush_ok));
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

static void send_mouse(int type, unsigned int button, int x, int y)
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
        event.xbutton.button = button;
        event.xbutton.x = x; event.xbutton.y = y;
        mask = type == ButtonPress ? ButtonPressMask : ButtonReleaseMask;
    }
    assert(XSendEvent(xgl_display, xgl_window, False, mask, &event));
    drain_events();
}

static void check_texture_failure(void)
{
    unsigned int before_objects, before_mipmaps, before_maps;
    int runtime;
    before_objects = allocated_objects;
    runtime = strcmp(profile, "inactive") == 0 || strcmp(profile, "unloaded") == 0 ||
              strcmp(profile, "drawfail") == 0 || strcmp(profile, "flushfail") == 0 ||
              strcmp(profile, "stripallocfail") == 0 || strcmp(profile, "fatalcleanup") == 0;
    if (strcmp(profile, "fataloverlay") == 0 || strcmp(profile, "fatalosd") == 0 ||
        strcmp(profile, "fatalmodel") == 0) {
        fault = profile;
        check_render();
        assert(!"Nontexture library errors must remain fatal");
    }
    if (runtime) {
        send_key(XK_t);
        assert(drv_texture_loaded && drv_texture_enabled);
        if (strcmp(profile, "unloaded") == 0) drv_texture_loaded = FALSE;
        else if (strcmp(profile, "fatalcleanup") == 0) {
            fault = "inactive";
            fail_cleanup = TRUE;
        } else fault = profile;
    } else {
        fault = profile;
        send_key(XK_t);
    }
    check_render();
    if (strcmp(profile, "rasterfail") == 0 || strcmp(profile, "mipmapfail") == 0 ||
        strcmp(profile, "stripallocfail") == 0) {
        assert(drv_texture_enabled && drv_texture_loaded && xgl_texture_used &&
               !xgl_texture_failed && !drv_texture_active);
        if (strcmp(profile, "stripallocfail") == 0) {
            assert(xgl_strip_limited && frame_bands >
                    (unsigned long)globe_latitude - 2UL + 2UL * globe_longitude);
        } else assert(texture_size == 256U);
        puts("PASS XGL bounded resource fallback: smaller textures/packets preserve complete geometry and active texturing");
        tests_complete = TRUE;
        drv_close();
    }
    fault = NULL;
    assert(drv_texture_enabled && xgl_texture_failed && !xgl_texture_used);
    assert(!drv_texture_loaded && xgl_tmap == NULL && xgl_mipmap == NULL &&
           xgl_texture_raster == NULL && texture_data == NULL);
    assert(!xgl_texture_scope && !xgl_failed && allocated_objects == before_objects);
    before_mipmaps = mipmap_creates; before_maps = tmap_creates;
    check_render();
    assert(mipmap_creates == before_mipmaps && tmap_creates == before_maps);
    send_key(XK_t); assert(!drv_texture_enabled); check_render();
    send_key(XK_t); assert(drv_texture_enabled && drv_texture_loaded); check_render();
    assert(xgl_texture_used && !xgl_texture_failed);
    send_key(XK_t); check_render();
    puts("PASS XGL recoverable texture failure: scoped native/resource errors release partial objects, preserve flat geometry and show texture fail until off/on retry");
    tests_complete = TRUE;
    drv_close();
}

static void cleanup_checks(void)
{
    assert(tests_complete || strncmp(profile, "fatal", 5) == 0);
    if (xgl_nonrecoverable) {
        Xgl_object object;
        assert(native_stopped && close_calls == 0);
        /* Model process reclamation without issuing any native operator. */
        while (objects != NULL) {
            object = objects; objects = object->next;
            free(object->image); free(object); --allocated_objects;
        }
        puts("PASS XGL nonrecoverable error: no native operator or close is called after invalid native state");
    } else assert(close_calls == 1);
    assert(allocated_objects == 0);
    assert(xgl_system == NULL && xgl_display == NULL);
    assert(xgl_strip_points == NULL && xgl_strip_facets == NULL && xgl_strip_texture_points == NULL);
    /* This harness supplies main, so perform the application's shared
     * shutdown after the native driver's registered cleanup has run. */
    texture_free();
    globe_free();
    assert(globe_verts == NULL && texture_data == NULL);
    puts("PASS XGL recorded cleanup: native objects, temporary raster, strip packets, geometry and X11 display released");
}

int main(int argc, char **argv)
{
    int sizes[5], i;
    unsigned int old_width, old_height, before_text, before_mipmaps, first_extents;
    char first_osd[sizeof(osd_text)];
    float old_x, old_y;
    XWindowAttributes geometry;
    XEvent close;
    assert(atexit(cleanup_checks) == 0);
    profile = argc > 1 ? argv[1] : "double";
    single_buffer = strcmp(profile, "single") == 0;
    leo_device = strcmp(profile, "leo") == 0;
    drv_ext_init();
    assert(!drv_texture_enabled && drv_texture_avail);
    drv_init(&argc, argv);
    assert(drv_init_window(0, 0, 0, 0));
    assert(XGetWindowAttributes(xgl_display, xgl_window, &geometry));
    assert(geometry.width == 640 && geometry.height == 480);
    assert(geometry.depth == 24 && geometry.visual->class == TrueColor);
    assert(geometry.backing_store == NotUseful);
    assert(xgl_double_buffer == !single_buffer);
    assert(inquire_calls == 1U && xgl_texture_software == leo_device);
    assert(strcmp(xgl_device_name, leo_device ? "Sun:LEO" : "Test:RECORDER") == 0);
    globe_init();
    assert(globe_init_verts(GLOBE_DEFAULT_LONGITUDE, -1, GLOBE_RADIUS));
    assert(GLOBE_DEFAULT_LONGITUDE == 128 && globe_longitude == 128);
    texture_init();
    drain_events();
    /* XGL starts with CPU band culling on: the ZX is bound by submitted
     * triangles. The complete-band checks below then run with it off. */
    assert(DRV_CULL_DEFAULT && drv_cull_enabled);
    check_render();
    assert(strstr(osd_text, ", cull on") != NULL);
    send_key(XK_c);
    assert(!drv_cull_enabled);
    if (strcmp(profile, "flatstripfail") == 0) fault = profile;
    check_render();
    sizes[0] = 32; sizes[1] = 64; sizes[2] = 128; sizes[3] = 256; sizes[4] = 512;
    for (i = 0; i < 5; ++i) {
        send_key((KeySym)(XK_1 + i));
        assert(globe_longitude == sizes[i]);
        check_render();
    }
    if (strcmp(profile, "double") != 0 && strcmp(profile, "single") != 0 &&
        strcmp(profile, "flatstripfail") != 0 && !leo_device)
        check_texture_failure();
    send_key(XK_t);
    assert(drv_texture_enabled && drv_texture_loaded);
    assert(mipmap_creates == 1 && tmap_creates == 1 && mipmap_builds == 1 && memory_destroys == 1);
    assert(texture_data == NULL);
    assert((strcmp(drv_get_notice(), XGL_SOFTWARE_NOTICE) == 0) == leo_device);
    check_render();
    if (leo_device) assert(strstr(osd_text, XGL_SOFTWARE_NOTICE) != NULL &&
                           strstr(osd_text, ", texture on (software)") != NULL);
    send_key(XK_t); assert(!drv_texture_enabled); check_render();
    assert(strstr(osd_text, XGL_SOFTWARE_NOTICE) == NULL);
    send_key(XK_t); assert(drv_texture_enabled); check_render();
    /* An accepted globe enclosing the camera has no front-facing facets.
     * Empty successful submission is not a failed native texture resource. */
    before_mipmaps = mipmap_builds;
    assert(globe_init_verts(32, 16, 3500.0f));
    send_key(XK_c);
    assert(drv_cull_enabled);
    drv_render();
    assert(!drv_frame_failed && frame_facets == 0 && frame_texture_facets == 0);
    assert(drv_texture_loaded && !xgl_texture_failed && !xgl_texture_used);
    assert(mipmap_builds == before_mipmaps && xgl_mipmap != NULL && xgl_tmap != NULL);
    assert(strstr(osd_text, ", texture fail") != NULL);
    send_key(XK_c);
    assert(globe_init_verts(512, -1, GLOBE_RADIUS));
    check_render();
    assert(xgl_texture_used && mipmap_builds == before_mipmaps);
    puts("PASS XGL empty culling: no submissions preserves native texture resources and later visible frames reuse them");
    assert(mipmap_builds == 1);
    send_key(XK_o); assert(!drv_osd_enabled); check_render();
    send_key(XK_o); assert(drv_osd_enabled); check_render();
    send_key(XK_f); assert(!drv_fps_enabled); check_render();
    send_key(XK_F); assert(drv_fps_enabled); check_render();
    assert(!drv_cull_enabled);
    send_key(XK_C); assert(drv_cull_enabled); check_render();
    send_key(XK_t); assert(!drv_texture_enabled); check_render();
    send_key(XK_c); assert(!drv_cull_enabled); check_render();
    send_key(XK_t); assert(drv_texture_enabled); check_render();
    puts("PASS XGL CPU culling: C/c filters native flat and textured strip submissions and restores complete bands while API backface culling remains active");
    puts("PASS XGL recorded rendering: native strip/facet packets, transforms, depth/cull, RGB texture rows, mipmap descriptor and OSD toggle");

    /* A usable window and native texture outlive an empty scene. Recovery
     * must show controls even with normal OSD disabled, without mistaking
     * absent polygons for a failed texture submission. */
    before_mipmaps = mipmap_builds;
    send_key(XK_a);
    assert(drv_auto_enabled && xgl_animating());
    globe_free();
    assert(!globe_ready());
    drv_osd_enabled = FALSE;
    before_text = text_calls;
    drv_render();
    assert(frame_facets == 0 && text_calls >= before_text + 2U);
    assert(drv_texture_enabled && drv_texture_loaded && !xgl_texture_failed);
    assert(!drv_texture_active && mipmap_builds == before_mipmaps);
    assert(drv_auto_enabled && !xgl_animating());
    drv_osd_enabled = TRUE;
    assert(globe_init_verts(512, -1, GLOBE_RADIUS));
    drv_release_optional();
    assert(drv_texture_loaded && xgl_strip_limited && xgl_strip_points == NULL);
    check_render();
    assert(xgl_animating());
    send_key(XK_a);
    send_key(XK_t);
    drv_release_optional();
    assert(!drv_texture_loaded && xgl_mipmap == NULL && xgl_tmap == NULL);
    check_render();
    send_key(XK_t);
    check_render();
    assert(mipmap_builds == before_mipmaps + 1U);
    before_text = text_calls;
    drv_osd_enabled = FALSE;
    send_key(XK_a);
    fault = "abortscene";
    drv_render();
    assert(drv_frame_failed && frame_facets == 0 && text_calls >= before_text + 2U);
    assert(!xgl_texture_used && !xgl_texture_failed && drv_texture_loaded);
    assert(!drv_texture_active);
    assert(drv_auto_enabled && !xgl_animating());
    drv_osd_enabled = TRUE;
    check_render();
    assert(xgl_animating());
    send_key(XK_a);
    puts("PASS XGL empty scene and optional eviction: controls remain available, valid textures survive empty frames, and bounded packets recover after pressure");

    old_x = drv_rot_x; old_y = drv_rot_y;
    send_mouse(ButtonPress, Button1, 100, 100);
    assert(drv_lmouse_pressed);
    send_mouse(MotionNotify, Button1, 190, 145);
    assert(drv_rot_x != old_x && drv_rot_y != old_y);
    send_mouse(ButtonRelease, Button1, 190, 145);
    assert(!drv_lmouse_pressed);
    old_x = drv_rot_x; old_y = drv_rot_y;
    send_mouse(MotionNotify, Button1, 210, 160);
    assert(drv_rot_x == old_x && drv_rot_y == old_y);
    send_mouse(ButtonPress, Button3, 210, 160);
    assert(drv_rmouse_pressed);
    send_mouse(ButtonRelease, Button3, 210, 160);
    assert(!drv_rmouse_pressed);
    send_mouse(ButtonPress, Button1, 210, 160);
    send_mouse(ButtonPress, Button3, 210, 160);
    memset(&close, 0, sizeof(close));
    close.xfocus.type = FocusOut;
    close.xfocus.display = xgl_display;
    close.xfocus.window = xgl_window;
    close.xfocus.mode = NotifyNormal;
    close.xfocus.detail = NotifyNonlinear;
    assert(XSendEvent(xgl_display, xgl_window, False, FocusChangeMask, &close));
    drain_events();
    assert(!drv_lmouse_pressed && !drv_rmouse_pressed);
    send_key(XK_a); assert(drv_auto_enabled); check_render();
    send_key(XK_a); assert(!drv_auto_enabled);
    old_width = drv_win_width; old_height = drv_win_height;
    xgl_resize(0, 0);
    assert(drv_win_width == old_width && drv_win_height == old_height);
    XResizeWindow(xgl_display, xgl_window, 800, 600);
    drain_events();
    assert(drv_win_width == 800 && drv_win_height == 600 && resize_calls > 1);
    assert(xgl_overlay->font_height == 12.0);
    check_render();
    xgl_resize(320, 240);
    assert(xgl_overlay->font_height == 10.0);
    check_render();
    assert(strstr(osd_text, "T = texture mapping") != NULL);
    before_text = text_calls;
    extent_calls = 0;
    osd_text[0] = '\0';
    xgl_resize(100, 240);
    drv_draw_osd();
    assert(text_calls > before_text && extent_calls < 250U);
    /* A narrow unbroken word still advances, and impossibly small windows
     * submit no out-of-bounds text or endless line-wrapping queries. */
    osd_text[0] = '\0';
    assert(xgl_osd_text("WWWWWWWWWWWWWWWW", 18.0f, 18.0f, 16.0f, TRUE) == 8U);
    before_text = text_calls;
    xgl_resize(21, 240);
    drv_draw_osd();
    assert(text_calls == before_text);
    xgl_resize(320, 20);
    drv_draw_osd();
    assert(text_calls == before_text);
    xgl_resize(1, 1);
    drv_draw_osd();
    assert(text_calls == before_text);
    xgl_resize(800, 600);
    check_render();
    xgl_resize(1024, 768);
    assert(xgl_overlay->font_height == 16.0);
    check_render();
    xgl_resize(1024, 240);
    assert(xgl_overlay->font_height == 10.0);
    check_render();
    xgl_resize(800, 600);
    /* The fixed help wrap and line spacing are measured once per width/font:
     * a repeat draw queries fewer extents, and a fresh layout matches. */
    xgl_resize(700, 500);
    xgl_help_valid = xgl_line_valid = FALSE;
    osd_text[0] = '\0'; extent_calls = 0; drv_draw_osd();
    first_extents = extent_calls;
    assert(strstr(osd_text, "T = texture mapping") != NULL);
    strcpy(first_osd, osd_text);
    osd_text[0] = '\0'; extent_calls = 0; drv_draw_osd();
    assert(strcmp(first_osd, osd_text) == 0 && extent_calls < first_extents);
    xgl_help_valid = xgl_line_valid = FALSE;
    osd_text[0] = '\0'; extent_calls = 0; drv_draw_osd();
    assert(strcmp(first_osd, osd_text) == 0 && extent_calls == first_extents);
    xgl_resize(400, 500);
    osd_text[0] = '\0'; extent_calls = 0; drv_draw_osd();
    assert(extent_calls > 0U && xgl_help_width == 380.0f);
    assert(strstr(osd_text, "Esc = exit") != NULL);
    xgl_resize(800, 600);
    puts("PASS XGL OSD: small/normal/large native fonts, measured proportional wrapping, bearings, narrow windows, long words, tiny-window guards and cached help layout");
    puts("PASS XGL X11 input: real window/events, all resolution keys, FPS toggle, mouse drag/release and right-button metadata, autorotation, resize and projection");
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
