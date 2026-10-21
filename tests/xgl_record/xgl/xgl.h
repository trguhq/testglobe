/* Test-only contract declarations transcribed from the supplied XGL manuals.
 * This is NOT Sun's SDK header, an ABI replacement, or an XGL implementation.
 * Only the fields/operators used by drv_xgl.c are represented. Symbol values
 * are local to this recorder; native builds must use Sun's installed headers.
 */
#ifndef TEST_XGL_CONTRACT_H
#define TEST_XGL_CONTRACT_H

typedef unsigned int Xgl_usgn32;
typedef int Xgl_sgn32;
typedef int Xgl_boolean;
typedef int Xgl_attribute;
typedef int Xgl_obj_type;
typedef int Xgl_axis;
typedef int Xgl_trans_update;
typedef unsigned int Xgl_trans_member;
typedef struct Test_xgl_object *Xgl_object;
typedef Xgl_object Xgl_sys_state;
typedef Xgl_object Xgl_ctx;
typedef Xgl_object Xgl_win_ras;
typedef Xgl_object Xgl_mem_ras;
typedef Xgl_object Xgl_trans;
typedef Xgl_sgn32 (*Xgl_error_notify)(Xgl_sys_state);
typedef double Xgl_matrix_d3d[4][4];

typedef struct { float x, y; } Xgl_pt_f2d;
typedef struct { float x, y, z; } Xgl_pt_f3d;
typedef struct { float x, y, z, data[1]; } Xgl_pt_data_f3d;
typedef struct { double xmin, xmax, ymin, ymax, zmin, zmax; } Xgl_bounds_d3d;
typedef union { Xgl_usgn32 index; struct { float r, g, b; } rgb; } Xgl_color;
typedef struct { Xgl_color color; } Xgl_color_facet;
typedef struct {
    int pt_type;
    union { Xgl_pt_f3d *f3d; Xgl_pt_f2d *f2d; } pt;
} Xgl_pt;
typedef struct {
    int pt_type;
    void *bbox;
    Xgl_usgn32 num_pts, num_data_values;
    union { Xgl_pt_f3d *f3d; Xgl_pt_data_f3d *data_f3d; } pts;
} Xgl_pt_list;
typedef struct {
    int facet_type;
    Xgl_usgn32 num_facets;
    union { Xgl_color_facet *color_facets; } facets;
} Xgl_facet_list;
typedef struct { void *X_display; int X_screen; Xgl_usgn32 X_window; } Xgl_X_window;
typedef union { struct { int type; void *desc; } win_ras; } Xgl_obj_desc;
typedef int Xgl_error_type;
#define XGL_ERROR_RECOVERABLE 1
#define XGL_ERROR_NONRECOVERABLE 2
typedef struct { Xgl_error_type type; char *id, *msg, *cur_op; } Xgl_error_info;

typedef struct {
    int comp, texture_op;
    union { float unused[16]; } op;
} Xgl_render_component_desc;
typedef struct {
    Xgl_usgn32 num_render_comp_desc;
    Xgl_render_component_desc render_component_desc[2];
    Xgl_usgn32 num_channels[2], channel_number[2];
} Test_xgl_color_comp;
typedef struct {
    Xgl_object texture_map;
    float max_u_freq, max_v_freq;
    int u_boundary, v_boundary;
    struct { int filter1, filter2; } interp_info;
    float orientation_matrix[2][3];
} Test_xgl_mipmap_desc;
typedef struct {
    int texture_type;
    union { Test_xgl_color_comp color_info; } comp_info;
    union { Test_xgl_mipmap_desc mipmap; Xgl_usgn32 unused[64]; } texture_info;
} Xgl_texture_general_desc;

/* Attribute codes are deliberately not the proprietary SDK's binary values. */
enum {
    XGL_SYS_ST_ERROR_DETECTION = 1,
    XGL_SYS_ST_ERROR_NOTIFICATION_FUNCTION,
    XGL_SYS_ST_ERROR_INFO,
    XGL_DEV_COLOR_TYPE,
    XGL_WIN_RAS_BUFFERS_REQUESTED,
    XGL_WIN_RAS_BUFFERS_ALLOCATED,
    XGL_WIN_RAS_BUF_DRAW,
    XGL_WIN_RAS_BUF_DISPLAY,
    XGL_CTX_DEVICE,
    XGL_CTX_BACKGROUND_COLOR,
    XGL_3D_CTX_HLHSR_MODE,
    XGL_CTX_NEW_FRAME_ACTION,
    XGL_CTX_SURF_FRONT_COLOR_SELECTOR,
    XGL_CTX_SURF_FRONT_FILL_STYLE,
    XGL_3D_CTX_SURF_FRONT_ILLUMINATION,
    XGL_3D_CTX_SURF_GEOM_NORMAL,
    XGL_3D_CTX_SURF_FACE_CULL,
    XGL_CTX_SURF_EDGE_FLAG,
    XGL_CTX_VDC_MAP,
    XGL_CTX_STEXT_CHAR_HEIGHT,
    XGL_CTX_STEXT_COLOR,
    XGL_TRANS_DATA_TYPE,
    XGL_CTX_LOCAL_MODEL_TRANS,
    XGL_CTX_VIEW_TRANS,
    XGL_CTX_VDC_ORIENTATION,
    XGL_CTX_VDC_WINDOW,
    XGL_CTX_VIEW_CLIP_BOUNDS,
    XGL_3D_CTX_SURF_FRONT_TMAP_SWITCHES,
    XGL_RAS_DEPTH,
    XGL_RAS_WIDTH,
    XGL_RAS_HEIGHT,
    XGL_MEM_RAS_IMAGE_BUFFER_ADDR,
    XGL_MIPMAP_TEXTURE_LEVELS,
    XGL_TMAP_DESCRIPTOR,
    XGL_3D_CTX_SURF_FRONT_TMAP_NUM,
    XGL_3D_CTX_SURF_FRONT_TMAP,
    XGL_CTX_DEFERRAL_MODE,
    XGL_3D_CTX_SURF_TMAP_PERSP_CORRECTION,
    XGL_CTX_CLIP_PLANES,
    XGL_CTX_GEOM_DATA_IS_VOLATILE
};
enum { XGL_WIN_RAS = 100, XGL_3D_CTX, XGL_2D_CTX, XGL_TRANS,
       XGL_MEM_RAS, XGL_MIPMAP_TEXTURE, XGL_TMAP };
enum { XGL_WIN_X = 200, XGL_COLOR_RGB, XGL_HLHSR_Z_BUFFER,
       XGL_SURF_COLOR_FACET, XGL_SURF_FILL_SOLID, XGL_ILLUM_NONE,
       XGL_GEOM_NORMAL_FIRST_POINTS, XGL_CULL_BACK, XGL_VDC_MAP_DEVICE,
       XGL_DATA_DBL, XGL_VDC_MAP_ALL, XGL_Y_UP_Z_TOWARD,
       XGL_PT_F3D, XGL_PT_DATA_F3D, XGL_FACET_COLOR,
       XGL_TEXTURE_BOUNDARY_WRAP, XGL_TEXTURE_TYPE_MIPMAP,
       XGL_TEXTURE_INTERP_MIPMAP_TRILINEAR, XGL_TEXTURE_INTERP_BILINEAR,
       XGL_RENDER_COMP_DIFFUSE_COLOR, XGL_TEXTURE_OP_REPLACE,
       XGL_DEFER_ASAP, XGL_TEXTURE_PERSP_PIXEL };
enum { XGL_AXIS_X = 0, XGL_AXIS_Y, XGL_AXIS_Z };
enum { XGL_TRANS_REPLACE = 0, XGL_TRANS_POSTCONCAT };
enum { XGL_FLUSH_SYNCHRONIZE = 1 };
#define XGL_CLIP_XMIN 1
#define XGL_CLIP_XMAX 2
#define XGL_CLIP_YMIN 4
#define XGL_CLIP_YMAX 8
#define XGL_CLIP_ZMIN 16
#define XGL_CLIP_ZMAX 32
#define XGL_CTX_NEW_FRAME_CLEAR 1
#define XGL_CTX_NEW_FRAME_HLHSR_ACTION 2
#define XGL_CTX_NEW_FRAME_SWITCH_BUFFER 4

Xgl_sys_state xgl_open(Xgl_attribute attr, ...);
void xgl_close(Xgl_sys_state system);
Xgl_object xgl_object_create(Xgl_sys_state system, Xgl_obj_type type,
                             Xgl_obj_desc *descriptor, ...);
void xgl_object_set(Xgl_object object, ...);
void xgl_object_get(Xgl_object object, Xgl_attribute attr, void *value);
void xgl_object_destroy(Xgl_object object);
void xgl_window_raster_resize(Xgl_win_ras raster);
void xgl_transform_write_specific(Xgl_trans transform, void *matrix,
                                   Xgl_trans_member membership);
void xgl_transform_rotate(Xgl_trans transform, double angle, Xgl_axis axis,
                           Xgl_trans_update update);
void xgl_transform_translate(Xgl_trans transform, Xgl_pt *point,
                              Xgl_trans_update update);
void xgl_mipmap_texture_build(Xgl_object mipmap, Xgl_mem_ras memory,
                              int u_boundary, int v_boundary);
void xgl_triangle_strip(Xgl_ctx context, Xgl_facet_list *facets,
                         Xgl_pt_list *points);
void xgl_stroke_text(Xgl_ctx context, void *text, void *position,
                      Xgl_pt_f3d direction[]);
void xgl_context_new_frame(Xgl_ctx context);
void xgl_context_post(Xgl_ctx context, Xgl_boolean wait);
void xgl_context_flush(Xgl_ctx context, int action);

#endif
