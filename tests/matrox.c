/* Real driver/SDK declarations, with recorded hardware and DOS calls only. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "earth_reference.h"
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"

/* Count every globe patch before the real driver's visibility rejection. */
static void recorded_mtx_error(char *message);
static clock_t recorded_mtx_clock(void);
#define drv_draw_quad_flat_color recorded_driver_quad
#define error recorded_mtx_error
#define clock recorded_mtx_clock
#include "../src/drv_mtx.c"
#undef clock
#undef error
#undef drv_draw_quad_flat_color

#ifdef TEXTURE
#error The Matrox driver must not compile texture mapping support
#endif
#if defined(EARTH_128) || defined(EARTH_256) || defined(EARTH_512)
#error The segmented Matrox driver must only include EARTH_32 and EARTH_64
#endif

extern int globe_longitude, globe_latitude;
extern Vertex TG_HUGE *globe_verts;
extern Color TG_HUGE *globe_colors;

static unsigned long allocated_blocks;
static unsigned long submitted_quads, filled_polygons, filled_triangles;
static unsigned long expected_visible, expected_caps;
static unsigned long silhouette_faces, silhouette_caps;
static unsigned char *expected_data;
static unsigned short last_color;
static unsigned short supplied_palette[256][3];
static unsigned short original_palette[256][3];
static unsigned short current_lut;
static unsigned int palette_uploads, init_calls, close_calls, sync_calls;
static unsigned int model_calls, view_calls, viewport_calls, projection_calls;
static unsigned int osd_calls, inquiry;
static unsigned int inquiry_header, inquiry_remaining, inquiry_words = 1;
static unsigned int inquiry_reads;
static unsigned int fixed_reply_offset;
static short error_reporting = 1;
static short initial_error_reporting = 1;
static unsigned short board_errors[8];
static unsigned int error_read, error_count, error_polls, delayed_reads;
static int expected_board_error, clock_timeout;
static unsigned long clock_step;
static char board_message[128];
static jmp_buf board_error;
static int expected_inquiry_error;
static jmp_buf inquiry_error;
static unsigned short mouse_buttons;
static short mouse_dx, mouse_dy;
static unsigned int mouse_resets, mouse_polls, mouse_motion_reads;
static short last_yaw, last_pitch;
static Vertex *submitted_vertices[4];
static unsigned short submitted_color;

static void recorded_mtx_error(char *message)
{
    if (expected_board_error)
    {
        assert(strlen(message) < sizeof(board_message));
        strcpy(board_message, message);
        longjmp(board_error, 1);
    }
    if (expected_inquiry_error)
    {
        assert(strcmp(message, "Invalid Matrox LUT inquiry reply length") == 0);
        longjmp(inquiry_error, 1);
    }
    error(message);
}

static clock_t recorded_mtx_clock(void)
{
    static unsigned long ticks;
    if (!clock_timeout && clock_step == 0) return clock();
    ticks += clock_timeout ? MTX_WAIT_TICKS : clock_step;
    return (clock_t)ticks;
}

void *_halloc(long count, size_t size)
{
    void *pointer;
    assert(count > 0 && size > 0);
    assert((unsigned long)count <= (size_t)-1 / size);
    pointer = calloc((size_t)count, size);
    assert(pointer != NULL);
    ++allocated_blocks;
    return pointer;
}

void _hfree(void *pointer)
{
    assert(pointer != NULL && allocated_blocks > 0);
    --allocated_blocks;
    free(pointer);
}

static void dummy_interrupt(void) {}
void (*_dos_getvect(unsigned int number))(void)
{
    assert(number == 0x33);
    return dummy_interrupt;
}

int int86(int number, union REGS *input, union REGS *output)
{
    unsigned short function;
    assert(number == 0x33);
    function = input->x.ax;
    memset(output, 0, sizeof(*output));
    switch (function) {
    case 0:
        ++mouse_resets;
        output->x.ax = 0xffff;
        output->x.bx = 2;
        break;
    case 3:
        ++mouse_polls;
        output->x.bx = mouse_buttons;
        break;
    case 0x0b:
        ++mouse_motion_reads;
        output->x.cx = (unsigned short)mouse_dx;
        output->x.dx = (unsigned short)mouse_dy;
        mouse_dx = mouse_dy = 0;
        break;
    case 2: /* hiding a software mouse cursor is harmless */
        break;
    default:
        assert(!"Unexpected mouse interface function");
    }
    return 0;
}

int kbhit(void) { return 0; }
int getch(void) { assert(!"Unexpected blocking keyboard read"); return 0; }

void lsinit(short handle, short address)
{
    assert(handle == 0 && address == 1);
    ++init_calls;
}
void ioflush(void) {}
void lslibsync(void) { assert(!"Blocking synchronization must not hide board errors"); }
void lsemulen(short restore)
{
    assert(restore == 1);
    ++close_calls;
}
void lserror(short enabled) { assert(enabled == 0 || enabled == 1); error_reporting = enabled; }
short iogeterror(void)
{
    ++error_polls;
    if (error_read < error_count) return (short)board_errors[error_read++];
    return 0;
}
short rdfifocnt(void)
{
    if (error_read < error_count || clock_timeout)
    {
        ++delayed_reads;
        return 0;
    }
    return inquiry != 0;
}
void lsinqsys(void)
{
    assert(inquiry == 0);
    inquiry = 1;
    inquiry_remaining = 10;
    fixed_reply_offset = 0;
}
void lsinq(unsigned short code)
{
    assert((code == 77 || code == 78) && inquiry == 0);
    inquiry = code == 78 ? 2U : 4U;
    if (code == 77) ++sync_calls;
    inquiry_header = 1;
    inquiry_remaining = code == 78 ? inquiry_words : 1U;
}
short iogetw(void)
{
    unsigned int type, offset;
    static short system_info[10] = {640,480,256,0x0104,640,480,80,0x0108,0,0};
    assert(inquiry != 0 && rdfifocnt() > 0);
    ++inquiry_reads;
    type = inquiry;
    if (inquiry_header)
    {
        inquiry_header = 0;
        if (inquiry_remaining == 0) inquiry = 0;
        return (short)(type == 2 ? inquiry_words : 1U);
    }
    assert(inquiry_remaining != 0);
    --inquiry_remaining;
    if (inquiry_remaining == 0) inquiry = 0;
    if (type == 1) return system_info[fixed_reply_offset++];
    if (type == 3)
    {
        offset = fixed_reply_offset++;
        return (short)original_palette[offset / 3U][offset % 3U];
    }
    if (type == 4) return error_reporting;
    return 3;
}
void lslutrd(unsigned short logical, unsigned short first, unsigned short count)
{
    assert(logical == 0 && first == 0 && count == 256 && inquiry == 0);
    inquiry = 3;
    inquiry_remaining = 768;
    fixed_reply_offset = 0;
}
void iogetbuf(short *buffer, unsigned short size)
{
    (void)buffer; (void)size;
    assert(!"Blocking buffer reads must not hide board errors");
}
void lslutmd(unsigned short logical, unsigned short first,
             unsigned short count, struct Lutval *colors)
{
    unsigned int i;
    assert(logical == 0 && count == 16 && first + count <= 256);
    assert(inquiry == 0 && error_read == error_count);
    for (i = 0; i < count; ++i) {
        supplied_palette[first + i][0] = colors[i].red;
        supplied_palette[first + i][1] = colors[i].green;
        supplied_palette[first + i][2] = colors[i].blue;
    }
    if (first + count == 256) ++palette_uploads;
}
void lslutsl(unsigned short logical, unsigned short physical)
{
    assert(physical == 0);
    current_lut = logical;
}
void lslutpm(unsigned short physical, unsigned short mask)
{
    assert(physical == 0 && mask == 255);
}
void lssbm(short bitmap) { assert(bitmap == -1); }
void lssclpl(short clip) { assert(clip == -1); }
void scsorg(unsigned short x, unsigned short y) { assert(x == 0 && y == 0); }
void krgcdi(void) {}
void krhwsbop(short operation) { assert(operation == 0); }
void krhwsmsk(unsigned short mask) { assert(mask == 255); }
void krhwstdef(unsigned short mode) { assert(mode == 0); }
void lssafapat(short pattern) { assert(pattern == -1); }
void lssfont(short font) { assert(font == -1 || font == -4); }
void sctxssf(double x, double y) { assert(x > 0 && y > 0); }
void sctxsdmode(short mode) { assert(mode == 0); }
void sctxsalign(short horizontal, short vertical)
{
    (void)horizontal; (void)vertical;
}
void sctxbstring(short x, short y, char *text)
{
    char expected[80];
    assert(x >= 0 && x < 640 && y >= 0 && y < 480);
    assert(strstr(text, "texture") == NULL);
    assert(strstr(text, "Texture") == NULL);
    assert(strstr(text, "T =") == NULL);
    sprintf(expected, "%lu quads (%lu visible)",
            globe_quads_num, globe_quads_num_vis);
    if (strstr(text, expected) == text) {
        assert((strstr(text, "frames per second") != NULL) ==
               (drv_fps_enabled != 0));
        assert(strstr(text, "triangles") == NULL);
    }
    ++osd_calls;
}
void v2gpsvwp(short xmin, short xmax, short ymin, short ymax)
{
    assert(xmin == 0 && xmax == 639 && ymin == 0 && ymax == 479);
    ++viewport_calls;
}
void v2gpswdw(double xmin, double xmax, double ymin, double ymax)
{
    assert(xmin < -GLOBE_RADIUS && xmax > GLOBE_RADIUS);
    assert(ymin < -GLOBE_RADIUS && ymax > GLOBE_RADIUS);
    assert(fabs((xmax - xmin) / (ymax - ymin) - 4.0 / 3.0) < 0.01);
}
void v3gpsproj(short projection) { assert(projection == 0); ++projection_calls; }
void v3gpsdist(double distance) { assert(distance > GLOBE_RADIUS); }
void v3gpshclip(short enabled) { assert(enabled == 0); }
void v3gpsyclip(short enabled) { assert(enabled == 0); }
void v3mdid(void) { ++model_calls; }
void v3mdorg(double x, double y, double z) { assert(x == 0 && y == 0 && z == 0); }
void v3mdrotz(short angle) { last_yaw = angle; }
void v3vwid(void) { ++view_calls; }
void v3vwrpt(double x, double y, double z) { assert(x == 0 && y == 0 && z == 0); }
void v3vwrotx(short angle) { last_pitch = angle; }
void krhwcclr(unsigned short color)
{
    unsigned int i, brightness;
    assert(color < 256);
    brightness = earth_cmap[color][0] + earth_cmap[color][1] + earth_cmap[color][2];
    for (i = 0; i < 256; ++i)
        assert(brightness <= (unsigned int)earth_cmap[i][0] + earth_cmap[i][1] +
                             earth_cmap[i][2]);
}
void krhwsfcol(unsigned short color) { assert(color < 256); last_color = color; }

void v3arwplygn(unsigned short count, struct V3coord *vertices)
{
    unsigned int i, j;
    double radius;
    int found;
    assert(count == 3 || count == 4);
    assert(last_color == submitted_color);
    for (i = 0; i < count; ++i) {
        found = 0;
        for (j = 0; j < 4; ++j)
            if (vertices[i].x == submitted_vertices[j]->x &&
                vertices[i].y == submitted_vertices[j]->y &&
                vertices[i].z == submitted_vertices[j]->z) found = 1;
        assert(found); /* SDK receives original world vertices, not host projections. */
        radius = vertices[i].x * vertices[i].x + vertices[i].y * vertices[i].y +
                 vertices[i].z * vertices[i].z;
        assert(fabs(radius - (double)GLOBE_RADIUS * GLOBE_RADIUS) < 2.0);
        for (j = 0; j < i; ++j)
            assert(vertices[i].x != vertices[j].x || vertices[i].y != vertices[j].y ||
                   vertices[i].z != vertices[j].z);
    }
    ++filled_polygons;
    if (count == 3) ++filled_triangles;
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *a, Vertex TG_HUGE *b,
                            Vertex TG_HUGE *c, Vertex TG_HUGE *d,
                            unsigned char color)
{
    double ux, uy, uz, vx, vy, vz, nx, ny, nz;
    double angle, model_y, view_z, tolerance;
    unsigned long before_fill;
    int cap;
    Vertex *third;
    assert(submitted_quads < globe_faces_num);
    assert(color == globe_colors[submitted_quads].color);
    assert(color == expected_data[submitted_quads]);
    assert(a >= globe_verts && a < globe_verts + globe_verts_num);
    assert(b >= globe_verts && b < globe_verts + globe_verts_num);
    assert(c >= globe_verts && c < globe_verts + globe_verts_num);
    assert(d >= globe_verts && d < globe_verts + globe_verts_num);
    /* At the south pole b/c coincide; use d for a nondegenerate normal. */
    third = (b->x == c->x && b->y == c->y && b->z == c->z) ? d : c;
    ux = b->x - a->x; uy = b->y - a->y; uz = b->z - a->z;
    vx = third->x - a->x; vy = third->y - a->y; vz = third->z - a->z;
    nx = uy * vz - uz * vy; ny = uz * vx - ux * vz; nz = ux * vy - uy * vx;
    assert(nx * (a->x + b->x + third->x) +
           ny * (a->y + b->y + third->y) +
           nz * (a->z + b->z + third->z) > 0.0);
    cap = (third == d || (a->x == d->x && a->y == d->y && a->z == d->z));
    /* Independently transform the normal using the commands actually sent:
     * model Rz(yaw), followed by inverse viewer Rx(-pitch). The resulting
     * positive eye-space Z hemisphere must be the one the SDK receives.
     * This deliberately avoids the driver's object-space eye-dot formula. */
    angle = (double)last_yaw * PI / 180.0;
    model_y = sin(angle) * nx + cos(angle) * ny;
    angle = (double)last_pitch * PI / 180.0;
    view_z = -sin(angle) * model_y + cos(angle) * nz;
    tolerance = sqrt(nx * nx + ny * ny + nz * nz) * 0.000001;
    submitted_vertices[0] = a; submitted_vertices[1] = b;
    submitted_vertices[2] = c; submitted_vertices[3] = d;
    submitted_color = color;
    ++submitted_quads;
    before_fill = filled_polygons;
    recorded_driver_quad(a, b, c, d, color);
    assert(filled_polygons - before_fill <= 1);
    if (view_z > tolerance) {
        ++expected_visible;
        if (cap) ++expected_caps;
        assert(filled_polygons - before_fill == 1);
    } else if (view_z < -tolerance) {
        assert(filled_polygons == before_fill);
    } else {
        ++silhouette_faces;
        if (cap) ++silhouette_caps;
    }
}

static void render_and_check(unsigned long faces, unsigned char *data)
{
    unsigned int before_sync, before_model, before_view;
    submitted_quads = filled_polygons = filled_triangles = 0;
    expected_visible = expected_caps = silhouette_faces = silhouette_caps = 0;
    expected_data = data;
    before_sync = sync_calls; before_model = model_calls; before_view = view_calls;
    drv_render();
    assert(submitted_quads == faces);
    assert(filled_polygons > 0 && filled_polygons < faces);
    assert(filled_triangles > 0); /* duplicate pole vertices compacted */
    assert(filled_polygons >= expected_visible &&
           filled_polygons <= expected_visible + silhouette_faces);
    assert(filled_triangles >= expected_caps &&
           filled_triangles <= expected_caps + silhouette_caps);
    assert(globe_quads_num_vis == filled_polygons);
    assert(sync_calls > before_sync && model_calls > before_model && view_calls > before_view);
    assert(last_yaw >= 0 && last_yaw < 360);
    assert(last_pitch >= 0 && last_pitch < 360);
}

static void check_noncardinal_poses(unsigned long faces, unsigned char *data)
{
    drv_rot_y = 35.75f; drv_rot_x = 13.5f;
    render_and_check(faces, data);
    assert(last_yaw == 35 && last_pitch == 103);
    assert(silhouette_faces == 0);
    drv_rot_y = 211.8f; drv_rot_x = -40.5f;
    render_and_check(faces, data);
    assert(last_yaw == 211 && last_pitch == 49);
    assert(silhouette_faces == 0);
    drv_rot_y = 73.2f; drv_rot_x = 278.8f;
    render_and_check(faces, data);
    assert(last_yaw == 73 && last_pitch == 8);
    assert(silhouette_faces == 0);
}

static void cleanup_checks(void)
{
    assert(inquiry == 0 && inquiry_header == 0 && inquiry_remaining == 0);
    assert(close_calls == 1);
    assert(palette_uploads == 2);
    assert(memcmp(supplied_palette, original_palette, sizeof(original_palette)) == 0);
    assert(current_lut == 3);
    assert(error_reporting == initial_error_reporting);
    assert(allocated_blocks == 0);
    puts("PASS Matrox cleanup: Escape, board emulation, palette selection/content, huge allocations");
}

static void check_board_errors(void)
{
    static unsigned short codes[4] = { 1, 2, 4, 8 };
    static const char *reasons[4] = {
        "undefined opcode", "unsupported parameter",
        "board memory exhausted", "arithmetic overflow"
    };
    unsigned int i, polls, syncs, uploads, closes;
    assert(error_reporting == 1);
    for (i = 0; i < 4U; ++i)
    {
        board_errors[0] = codes[i]; error_read = 0; error_count = 1;
        expected_board_error = 1;
        if (setjmp(board_error) == 0)
        {
            (void)mtx_checkpoint("command test");
            assert(!"Board errors must be reported");
        }
        expected_board_error = 0;
        assert(strstr(board_message, reasons[i]) != NULL);
        assert(inquiry == 0 && error_read == error_count && !mtx_io_failed);
    }
    /* Model firmware that cannot produce its completion reply until every
     * pending error has been acknowledged. A blocking sync would hang. */
    board_errors[0] = 1; board_errors[1] = 2;
    board_errors[2] = 4; board_errors[3] = 8;
    error_read = 0; error_count = 4;
    polls = error_polls;
    expected_board_error = 1;
    if (setjmp(board_error) == 0)
    {
        (void)mtx_checkpoint("multiple errors");
        assert(!"Multiple board errors must be drained and reported");
    }
    expected_board_error = 0;
    assert(strstr(board_message, "0x0F") != NULL);
    assert(error_polls - polls >= 4U && delayed_reads >= 3U && inquiry == 0);
    error_read = error_count = 0;
    puts("PASS Matrox errors: opcode, parameter, memory and overflow diagnostics; multiple errors drained while awaiting completion");

    expected_board_error = clock_timeout = 1;
    if (setjmp(board_error) == 0)
    {
        (void)mtx_checkpoint("timeout test");
        assert(!"Missing completion reply must time out");
    }
    expected_board_error = clock_timeout = 0;
    assert(strstr(board_message, "communication timeout") != NULL && mtx_io_failed);
    syncs = sync_calls; uploads = palette_uploads; closes = close_calls;
    mtx_cleanup();
    assert(!mtx_initialized && sync_calls == syncs &&
           palette_uploads == uploads && close_calls == closes);
    /* Re-establish the fixture after testing the fatal communication path;
     * the real application exits and never resumes a timed-out connection. */
    mtx_initialized = TRUE; mtx_io_failed = FALSE;
    inquiry = inquiry_header = inquiry_remaining = 0;
    puts("PASS Matrox timeout: bounded FIFO wait and cleanup avoids further commands after lost communication");

    /* A deadline applies to the entire reply even when each individual
     * word is ready immediately; restarting it for each word is unsafe. */
    expected_board_error = 1;
    clock_step = MTX_WAIT_TICKS / 2UL;
    inquiry_words = 2;
    polls = inquiry_reads;
    if (setjmp(board_error) == 0)
    {
        (void)mtx_inquire_lut();
        assert(!"Continuous ready data must still respect the reply deadline");
    }
    expected_board_error = 0;
    clock_step = 0;
    assert(strstr(board_message, "communication timeout") != NULL && mtx_io_failed);
    assert(inquiry_reads - polls == 1U && inquiry_remaining == 2U);
    mtx_io_failed = FALSE;
    inquiry = inquiry_header = inquiry_remaining = 0;
    inquiry_words = 1;
    puts("PASS Matrox reply bounds: total deadline includes immediately available words");
}

static void check_invalid_inquiry(unsigned int words)
{
    unsigned int reads;
    reads = inquiry_reads;
    inquiry_words = words;
    expected_inquiry_error = 1;
    if (setjmp(inquiry_error) == 0)
    {
        (void)mtx_inquire_lut();
        assert(!"Invalid inquiry reply must be rejected");
    }
    expected_inquiry_error = 0;
    if (words > 16U)
    {
        assert(inquiry_reads - reads == 1U && mtx_io_failed);
        /* A real connection is abandoned; reset only this recorder. */
        mtx_io_failed = FALSE;
        inquiry = inquiry_header = inquiry_remaining = 0;
    }
    else
    {
        assert(inquiry_reads - reads == words + 1U);
        assert(inquiry == 0 && inquiry_header == 0 && inquiry_remaining == 0);
    }
}

static void check_indexed_triangle(void)
{
    unsigned long top, before_fill, before_triangles, before_quads;
    unsigned char color;
    Vertex *a, *b, *c;
    /* Default view looks toward the positive-Y equatorial hemisphere. Use
     * a real outward triangle there to exercise the separate byte-color API. */
    top = ((unsigned long)globe_latitude / 2UL - 1UL) *
          (globe_longitude + 1UL) + (unsigned long)globe_longitude / 4UL;
    a = globe_verts + top;
    b = a + globe_longitude + 1;
    c = a + 1;
    color = test_earth_data(32)[((unsigned long)globe_latitude / 2UL - 1UL) *
                         globe_longitude + (unsigned long)globe_longitude / 4UL];
    submitted_vertices[0] = a; submitted_vertices[1] = b;
    submitted_vertices[2] = c; submitted_vertices[3] = c;
    submitted_color = color;
    before_fill = filled_polygons;
    before_triangles = globe_tris_num_vis;
    before_quads = globe_quads_num_vis;
    drv_draw_tri_flat_color(NULL, b, c, color);
    drv_draw_tri_flat_color(a, NULL, c, color);
    drv_draw_tri_flat_color(a, b, NULL, color);
    recorded_driver_quad(NULL, b, c, a, color);
    recorded_driver_quad(a, NULL, c, a, color);
    recorded_driver_quad(a, b, NULL, a, color);
    recorded_driver_quad(a, b, c, NULL, color);
    assert(filled_polygons == before_fill &&
           globe_tris_num_vis == before_triangles &&
           globe_quads_num_vis == before_quads);
    drv_draw_tri_flat_color(a, b, c, color);
    assert(filled_polygons == before_fill + 1UL);
    assert(globe_tris_num_vis == before_triangles + 1UL);
    assert(globe_quads_num_vis == before_quads);
    assert(last_color == color);
    puts("PASS indexed drawing: distinct triangle and quad APIs receive original color bytes");
}

int main(int process_argc, char **process_argv)
{
    unsigned int i;
    int argc;
    char *argv[2];
    float x, y;
    if (process_argc > 1)
    {
        assert(process_argc == 2 && strcmp(process_argv[1], "reporting-off") == 0);
        error_reporting = initial_error_reporting = 0;
    }
    for (i = 0; i < 256; ++i) {
        original_palette[i][0] = (unsigned short)((i + 11) & 255);
        original_palette[i][1] = (unsigned short)((i + 23) & 255);
        original_palette[i][2] = (unsigned short)((i + 47) & 255);
    }
    argc = 1; argv[0] = "testglob.exe"; argv[1] = NULL;
    /* Registered first, so this checks the production atexit cleanup last. */
    assert(atexit(cleanup_checks) == 0);
    drv_ext_init();
    assert(GLOBE_DEFAULT_LONGITUDE == 32);
    assert(drv_colormapped && !drv_texture_avail && !drv_texture_enabled);
    assert(strstr(drv_help, "texture") == NULL && strstr(drv_help, "1-2") != NULL);
    check_invalid_inquiry(0);
    check_invalid_inquiry(2);
    check_invalid_inquiry(65535U);
    inquiry_words = 1;
    drv_init(&argc, argv);
    assert(init_calls == 1 && mouse_resets > 0);
    assert(mtx_previous_lut == 3 && inquiry == 0);
    assert(error_reporting == 1 && mtx_previous_error_reporting == initial_error_reporting);
    puts("PASS Matrox inquiry: short malformed replies drained, oversized counts rejected, palette FIFO remains synchronized");
    assert(drv_init_window(0, 0, 0, 0));
    assert(drv_win_width == 640 && drv_win_height == 480);
    assert(viewport_calls > 0 && projection_calls > 0);
    assert(palette_uploads == 1 && current_lut == 0);
    for (i = 0; i < 256; ++i) {
        assert(supplied_palette[i][0] == earth_cmap[i][0]);
        assert(supplied_palette[i][1] == earth_cmap[i][1]);
        assert(supplied_palette[i][2] == earth_cmap[i][2]);
    }
    check_board_errors();
    globe_init();
    assert(globe_init_verts(GLOBE_DEFAULT_LONGITUDE, -1, GLOBE_RADIUS));
    render_and_check(512, test_earth_data(32));
    check_indexed_triangle();
    check_noncardinal_poses(512, test_earth_data(32));
    assert(osd_calls > 0);
    mtx_keyboard('2');
    assert(globe_longitude == 64 && globe_faces_num == 2048UL);
    render_and_check(2048, test_earth_data(64));
    check_noncardinal_poses(2048, test_earth_data(64));
    for (i = '3'; i <= '5'; ++i) mtx_keyboard((unsigned char)i);
    mtx_keyboard('t');
    assert(globe_longitude == 64 && !drv_texture_enabled);
    mtx_keyboard('1');
    assert(globe_longitude == 32 && globe_faces_num == 512UL);
    render_and_check(512, test_earth_data(32));
    mtx_keyboard('a'); assert(drv_auto_enabled);
    mtx_keyboard('a'); assert(!drv_auto_enabled);
    mtx_keyboard('o'); assert(!drv_osd_enabled);
    mtx_keyboard('o'); assert(drv_osd_enabled);
    mtx_keyboard('f'); assert(!drv_fps_enabled); drv_draw_osd();
    mtx_keyboard('F'); assert(drv_fps_enabled); drv_draw_osd();
    puts("PASS Matrox rendering: indexed palette, 32/64 quads, world vertices, transforms, pole compaction, OSD");
    puts("PASS Matrox visibility: independent transformed normals and visible/cap counts across rotated poses");

    x = drv_rot_x; y = drv_rot_y;
    mouse_buttons = 0; mouse_dx = 70; mouse_dy = -20;
    mtx_poll_mouse();
    assert(drv_rot_x == x && drv_rot_y == y);
    mouse_buttons = 1; mouse_dx = mouse_dy = 0;
    mtx_poll_mouse();
    assert(drv_lmouse_pressed);
    mouse_dx = 18; mouse_dy = -9;
    mtx_poll_mouse();
    assert(drv_rot_x != x && drv_rot_y != y);
    x = drv_rot_x; y = drv_rot_y;
    mouse_buttons = 0; mouse_dx = 50; mouse_dy = 50;
    mtx_poll_mouse();
    assert(!drv_lmouse_pressed && drv_rot_x == x && drv_rot_y == y);
    mtx_dirty = FALSE;
    mouse_buttons = 2; mouse_dx = mouse_dy = 0;
    mtx_poll_mouse();
    assert(drv_rmouse_pressed && mtx_dirty && drv_rot_x == x && drv_rot_y == y);
    mtx_dirty = FALSE;
    mtx_poll_mouse();
    assert(drv_rmouse_pressed && !mtx_dirty);
    mouse_buttons = 0;
    mtx_poll_mouse();
    assert(!drv_rmouse_pressed && mtx_dirty && drv_rot_x == x && drv_rot_y == y);
    assert(mouse_polls >= 4 && mouse_motion_reads >= 4);
    puts("PASS DOS input: INT 33h polling, motion drain, left drag/release, right-button redraw, key restrictions and toggles");
    mtx_keyboard(27);
    assert(!"Escape must terminate the application");
    return 1;
}
