/* PiStorm3D renderer call contract. In particular GL_FLAT on this backend
 * supplies one color for an entire Begin/End, unlike desktop provoking colors.
 * Geometry is compared independently; no GPU or native ABI is emulated. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earth_reference.h"
#include "../src/drv_ogl.c"

#define TEST_PI 3.14159265358979323846
#define TEST_VERTICES 1026
static unsigned int batch_count, vertex_total, facet_total, max_batch;
static GLenum batch_mode, pending_error;
static Vertex batch_vertices[256];
static Coord batch_uv[256], current_uv;
static unsigned char current_rgb[3];
static unsigned int batch_vertices_count;
static int texture_enabled, context_ready, texture_exists, list_exists;
static GLuint texture_binding;
static unsigned int list_compiling, list_count, list_calls, list_deletes;
static unsigned int texture_uploads, texture_deletes, presentations, text_calls, redisplays;
static unsigned int window_creates, window_closes, frame_clears;
static int matrix_mode, stack_depth[2], size_width = 640, size_height = 480;
static int resize_result = 1, fail_lists, fail_texture;
static int geometry_kind, geometry_textured;
static unsigned int expected_facet;
static const unsigned char *expected_colors;
static char osd[1024];
static double clock_value;
static unsigned char *earth;

static int same_vertex(const Vertex *a, const Vertex *b)
{ return a->x == b->x && a->y == b->y && a->z == b->z; }

static void record_triangle(unsigned int a, unsigned int b, unsigned int c)
{
    const Vertex *va, *vb, *vc;
    unsigned int ids[3], i, color, latitude, longitude;
    double ux, uy, uz, vx, vy, vz, cx, cy, cz, area, phi, theta;
    va = batch_vertices + a; vb = batch_vertices + b; vc = batch_vertices + c;
    if (same_vertex(va, vb) || same_vertex(va, vc) || same_vertex(vb, vc)) return;
    if (geometry_kind == 1)
    {
        ids[0] = expected_facet + (expected_facet & 1U);
        ids[1] = expected_facet + ((expected_facet & 1U) ? 0U : 1U);
        ids[2] = expected_facet + 2U;
        assert(va->x == (float)ids[0] && vb->x == (float)ids[1] && vc->x == (float)ids[2]);
        if (geometry_textured)
        {
            unsigned int indexes[3];
            indexes[0] = a; indexes[1] = b; indexes[2] = c;
            for (i = 0; i < 3; ++i)
                assert(batch_uv[indexes[i]].u == (float)ids[i] / TEST_VERTICES &&
                       batch_uv[indexes[i]].v == (float)(ids[i] & 1U));
        }
        else
        {
            color = expected_colors[expected_facet];
            assert(memcmp(current_rgb, earth_cmap[color], 3) == 0);
        }
        ++expected_facet;
    }
    else if (geometry_kind == 2)
    {
        ux = vb->x - va->x; uy = vb->y - va->y; uz = vb->z - va->z;
        vx = vc->x - va->x; vy = vc->y - va->y; vz = vc->z - va->z;
        cx = (va->x + vb->x + vc->x) / 3.0;
        cy = (va->y + vb->y + vc->y) / 3.0;
        cz = (va->z + vb->z + vc->z) / 3.0;
        area = (uy * vz - uz * vy) * cx + (uz * vx - ux * vz) * cy + (ux * vy - uy * vx) * cz;
        assert(area > 0.0);
        if (!geometry_textured)
        {
            phi = atan2(cy, cx); if (phi < 0.0) phi += 2.0 * TEST_PI;
            theta = atan2(sqrt(cx * cx + cy * cy), cz);
            longitude = (unsigned int)(phi * globe_longitude / (2.0 * TEST_PI));
            latitude = (unsigned int)(theta * globe_latitude / TEST_PI);
            assert(longitude < (unsigned int)globe_longitude && latitude < (unsigned int)globe_latitude);
            color = earth[(unsigned long)latitude * globe_longitude + longitude];
            assert(memcmp(current_rgb, earth_cmap[color], 3) == 0);
        }
        else
        {
            unsigned int indexes[3];
            indexes[0] = a; indexes[1] = b; indexes[2] = c;
            for (i = 0; i < 3; ++i)
                assert(batch_uv[indexes[i]].u >= 0.0f && batch_uv[indexes[i]].u <= 1.0f &&
                       batch_uv[indexes[i]].v >= 0.0f && batch_uv[indexes[i]].v <= 1.0f);
        }
    }
    ++facet_total;
}

void glBegin(GLenum mode)
{
    assert(batch_mode == 0 && (mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP));
    batch_mode = mode; batch_vertices_count = 0; ++batch_count;
}
void glEnd(void)
{
    unsigned int i;
    assert(batch_mode && batch_vertices_count <= 256);
    if (batch_vertices_count > max_batch) max_batch = batch_vertices_count;
    if (batch_mode == GL_TRIANGLES)
    {
        assert(batch_vertices_count % 3U == 0);
        for (i = 0; i < batch_vertices_count; i += 3U) record_triangle(i, i + 1U, i + 2U);
    }
    else for (i = 0; i + 2U < batch_vertices_count; ++i)
        record_triangle(i + (i & 1U), i + ((i & 1U) ? 0U : 1U), i + 2U);
    batch_mode = 0;
}
void glVertex3fv(const GLfloat *v)
{
    assert(batch_mode && batch_vertices_count < 256);
    memcpy(batch_vertices + batch_vertices_count, v, sizeof(Vertex));
    batch_uv[batch_vertices_count] = current_uv;
    ++batch_vertices_count; ++vertex_total;
}
void glTexCoord2fv(const GLfloat *uv) { memcpy(&current_uv, uv, sizeof(current_uv)); }
void glColor3ub(GLubyte r, GLubyte g, GLubyte b)
{ current_rgb[0] = r; current_rgb[1] = g; current_rgb[2] = b; }
void glColor3f(GLfloat r, GLfloat g, GLfloat b)
{ glColor3ub((GLubyte)(r * 255), (GLubyte)(g * 255), (GLubyte)(b * 255)); }
void glIndexi(GLint color) { (void)color; assert(!"PiStorm3D cannot draw indexed GL colors"); }
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{ assert(a == 1.0f); glColor3f(r, g, b); }
GLenum glGetError(void) { GLenum value = pending_error; pending_error = 0; return value; }
void glGetIntegerv(GLenum query, GLint *value)
{
    switch (query) {
    case GL_MAX_TEXTURE_SIZE: *value = 512; break;
    case GL_TEXTURE_BINDING_2D: assert(!"MiniGL does not implement the binding query"); break;
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: *value = 8; break;
    case GL_DEPTH_BITS: *value = 24; break;
    case GL_DOUBLEBUFFER: case GL_RGBA_MODE: *value = 1; break;
    default: assert(!"Unexpected MiniGL query");
    }
}
GLboolean glIsEnabled(GLenum cap)
{ assert(cap == GL_TEXTURE_2D); return (GLboolean)texture_enabled; }
void glEnable(GLenum cap)
{ if (cap == GL_TEXTURE_2D) texture_enabled = 1; else assert(cap == GL_DEPTH_TEST || cap == GL_CULL_FACE); }
void glDisable(GLenum cap)
{ if (cap == GL_TEXTURE_2D) texture_enabled = 0; else assert(cap == GL_DEPTH_TEST || cap == GL_CULL_FACE); }
void glClear(GLbitfield mask)
{ assert(context_ready && !batch_mode && mask & GL_COLOR_BUFFER_BIT); ++frame_clears; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{ assert(r == 0 && g == 0 && b == 0 && a == 1); }
void glFrontFace(GLenum mode) { assert(mode == GL_CCW); }
void glCullFace(GLenum mode) { assert(mode == GL_BACK); }
void glShadeModel(GLenum mode) { assert(mode == GL_FLAT); }
void glMatrixMode(GLenum mode) { assert(mode == GL_PROJECTION || mode == GL_MODELVIEW); matrix_mode = mode; }
void glLoadIdentity(void) {}
void glPushMatrix(void) { ++stack_depth[matrix_mode == GL_PROJECTION]; }
void glPopMatrix(void) { assert(stack_depth[matrix_mode == GL_PROJECTION] > 0); --stack_depth[matrix_mode == GL_PROJECTION]; }
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) { (void)x; (void)y; (void)z; }
void glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) { (void)a; (void)x; (void)y; (void)z; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{ assert(x == 0 && y == 0 && w > 0 && h > 0); }
void gluPerspective(GLdouble f, GLdouble a, GLdouble n, GLdouble z)
{ assert(f > 0 && a > 0 && n > 0 && z > n); }
GLuint glGenLists(GLsizei n)
{ assert(n == 1); ++list_count; return fail_lists ? 0U : 1U; }
void glNewList(GLuint list, GLenum mode)
{ assert(list == 1 && mode == GL_COMPILE && !list_compiling); list_compiling = 1; }
void glEndList(void)
{ assert(list_compiling && !batch_mode); list_compiling = 0; list_exists = 1; }
GLboolean glIsList(GLuint list) { return (GLboolean)(list == 1 && list_exists); }
void glCallList(GLuint list) { assert(list == 1 && list_exists); ++list_calls; }
void glDeleteLists(GLuint list, GLsizei n)
{ assert(context_ready && list == 1 && n == 1); list_exists = 0; ++list_deletes; }
void glGenTextures(GLsizei n, GLuint *value)
{ assert(n == 1); texture_exists = 1; *value = 9; }
void glDeleteTextures(GLsizei n, const GLuint *value)
{ assert(context_ready && n == 1 && *value == 9); texture_exists = 0; ++texture_deletes; }
void glBindTexture(GLenum target, GLuint value)
{ assert(target == GL_TEXTURE_2D && (value == 0 || value == 9)); texture_binding = value; }
GLboolean glIsTexture(GLuint value) { return (GLboolean)(value == 9 && texture_exists); }
void glTexParameteri(GLenum target, GLenum name, GLint value)
{ assert(target == GL_TEXTURE_2D); assert((name == GL_TEXTURE_MIN_FILTER && value == GL_LINEAR_MIPMAP_LINEAR) || (name == GL_TEXTURE_MAG_FILTER && value == GL_LINEAR)); }
void glTexEnvi(GLenum target, GLenum name, GLint value)
{ assert(target == GL_TEXTURE_ENV && name == GL_TEXTURE_ENV_MODE && value == GL_REPLACE); }
GLint gluBuild2DMipmaps(GLenum target, GLint components, GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid *pixels)
{
    assert(target == GL_TEXTURE_2D && components == GL_RGB && w == 2 * h && w >= 32 && w <= 512);
    assert(format == GL_RGB && type == GL_UNSIGNED_BYTE && pixels != NULL);
    ++texture_uploads; if (fail_texture) pending_error = GL_OUT_OF_MEMORY; return 0;
}
void glFlush(void) {}

void ogl_ami_init(int *argc, char **argv) { assert(argc && argv); }
int ogl_ami_create_window(int x, int y, int w, int h)
{ assert(x >= 0 && y >= 0 && w > 0 && h > 0); size_width = w; size_height = h; context_ready = 1; ++window_creates; return 1; }
void ogl_ami_cleanup(void) { context_ready = 0; ++window_closes; }
int ogl_ami_ready(void) { return context_ready; }
void ogl_ami_present(void) { assert(context_ready && !batch_mode); ++presentations; osd[0] = 0; }
void ogl_ami_post_redisplay(void) { ++redisplays; }
void ogl_ami_get_size(int *w, int *h) { *w = size_width; *h = size_height; }
int ogl_ami_resize_context(int w, int h)
{
    int answer = resize_result;
    assert(w > 0 && h > 0); size_width = w; size_height = h;
    if (answer != 1) { list_exists = texture_exists = 0; texture_binding = 0; texture_enabled = 0; }
    context_ready = answer != 0; resize_result = 1; return answer;
}
void ogl_ami_draw_string(const char *s, int x, int y)
{ assert(presentations && x >= 0 && y >= 0); assert(strlen(osd) + strlen(s) + 2 < sizeof(osd)); strcat(osd, s); strcat(osd, "\n"); ++text_calls; }
double ogl_ami_time_now(void) { clock_value += 0.016; return clock_value; }
void ogl_ami_error(char *message) { fprintf(stderr, "Unexpected error: %s\n", message); abort(); }
void ogl_ami_loop(void (*draw)(void), void (*resize)(int,int), void (*key)(unsigned char,int,int), void (*mouse)(int,int,int,int), void (*motion)(int,int))
{ assert(draw && resize && key && mouse && motion); }

static void reset_geometry(int kind, int textured)
{ geometry_kind = kind; geometry_textured = textured; batch_count = vertex_total = facet_total = max_batch = expected_facet = 0; drv_frame_failed = FALSE; }

static void check_strips(void)
{
    Vertex vertices[TEST_VERTICES];
    Coord coords[TEST_VERTICES];
    unsigned char colors[TEST_VERTICES - 2];
    unsigned int i, profile;
    for (i = 0; i < TEST_VERTICES; ++i)
    { vertices[i].x = (float)i; vertices[i].y = (float)(i & 1U); vertices[i].z = 1.0f; coords[i].u = (float)i / TEST_VERTICES; coords[i].v = vertices[i].y; }
    for (profile = 0; profile < 4; ++profile)
    {
        for (i = 0; i < TEST_VERTICES - 2; ++i)
            colors[i] = (unsigned char)(profile == 0 ? 13 : profile == 1 ? i & 1U : profile == 2 ? i / 3U : i / 251U);
        expected_colors = colors; reset_geometry(1, 0);
        drv_draw_tristrip_flat_color(TEST_VERTICES, vertices, colors);
        assert(expected_facet == TEST_VERTICES - 2 && facet_total == TEST_VERTICES - 2 && max_batch <= 256);
        assert(batch_count > 1);
    }
    reset_geometry(1, 1);
    drv_draw_tristrip_flat_uv(TEST_VERTICES, vertices, coords);
    assert(expected_facet == TEST_VERTICES - 2 && facet_total == TEST_VERTICES - 2 && max_batch <= 256);
    puts("PASS Amiga OpenGL strips: bounded MiniGL packets, constant-color runs, odd starts, shared-edge winding and UVs at 512-map width");
}

static void check_globes(void)
{
    unsigned int level, textured;
    for (level = 32; level <= 512; level *= 2U)
    {
        assert(globe_set_resolution((int)level, -1, GLOBE_RADIUS));
        earth = test_earth_data(level);
        for (textured = 0; textured <= 1; ++textured)
        {
            reset_geometry(2, textured); drv_texture_active = (int)textured;
            ogl_draw_globe();
            assert(facet_total == globe_tris_num && max_batch <= 256);
        }
    }
    drv_texture_active = FALSE; geometry_kind = 0;
    puts("PASS Amiga OpenGL geometry: all five Earth levels preserve every triangle, color, pole winding and texture coordinates");
}

int main(int argc, char **argv)
{
    unsigned int before;
    drv_ext_init(); globe_init(); texture_init();
    drv_init(&argc, argv); assert(drv_init_window(0, 0, 0, 0));
    assert(window_creates == 1 && !ogl_indexed && drv_bits_per_pixel >= 24);
    check_strips(); check_globes();
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    drv_auto_enabled = FALSE;
    drv_render(); assert(strstr(osd, "dlist fail") && strstr(osd, "texture off"));
    assert(list_count == 0 && list_calls == 0);
    ogl_keyboard('d', 0, 0); drv_render(); assert(strstr(osd, "dlist off"));
    ogl_keyboard('d', 0, 0); drv_render(); assert(strstr(osd, "dlist fail") && list_count == 0);
    ogl_keyboard('t', 0, 0); drv_render();
    assert(strstr(osd, "texture on") && strstr(osd, "dlist on") && texture_uploads == 1);
    before = list_count; drv_render(); assert(list_count == before && list_calls >= 2);
    ogl_keyboard('d', 0, 0); fail_lists = 1; ogl_keyboard('d', 0, 0);
    drv_render(); assert(strstr(osd, "dlist fail"));
    fail_lists = 0; ogl_keyboard('d', 0, 0); ogl_keyboard('d', 0, 0);
    /* Context recreation must invalidate old native names before reloading. */
    resize_result = 2; before = texture_uploads; ogl_resize(320, 240); drv_render();
    assert(texture_uploads == before + 1 && strstr(osd, "texture on"));
    resize_result = 0; ogl_resize(300, 220); before = frame_clears; drv_render();
    assert(frame_clears == before);
    resize_result = 2; ogl_resize(320, 240); drv_render(); assert(strstr(osd, "texture on"));
    ogl_keyboard('t', 0, 0); drv_release_optional(); fail_texture = 1; ogl_keyboard('t', 0, 0); drv_render();
    assert(strstr(osd, "texture fail"));
    fail_texture = 0; ogl_keyboard('t', 0, 0); ogl_keyboard('t', 0, 0); drv_render();
    assert(strstr(osd, "texture on"));
    ogl_cleanup(); assert(!context_ready && window_closes == 1);
    texture_free(); globe_free();
    puts("PASS Amiga OpenGL state: native display lists/textures, failure statuses, resize context recovery, post-present OSD and cleanup");
    return 0;
}
