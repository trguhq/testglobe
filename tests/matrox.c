/* Real driver/SDK declarations, with recorded hardware and DOS calls only. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
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
#include "soft3d.h"

/* Count every globe patch before the real driver's visibility rejection. */
static void recorded_mtx_error(char *message);
static clock_t recorded_mtx_clock(void);
static int recorded_mtx_fprintf(FILE *stream, const char *format, ...);
static void check_backward_clock(void);
static int recorded_fixed_prepare(long matrix[3][3], int yaw, int pitch);
static int recorded_float_prepare(float matrix[3][3], int yaw, int pitch);
static int recorded_fixed_transform(long matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_fixed_point *out, unsigned int reuse);
static int recorded_386_transform(long matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_fixed_point *out, unsigned int reuse);
static int recorded_float_transform(float matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_float_point *out, unsigned int reuse,
    int precision);
static int recorded_fixed_cached(const Soft3d_fixed_point *points,
    unsigned int point_count, const unsigned int TG_HUGE *indices,
    unsigned int corners, unsigned int width, unsigned int height,
    Soft3d_project_cache TG_HUGE *cache, Soft3d_screen_point *out);
static int recorded_float_cached(const Soft3d_float_point *points,
    unsigned int point_count, const unsigned int TG_HUGE *indices,
    unsigned int corners, unsigned int width, unsigned int height, int precision,
    Soft3d_project_cache TG_HUGE *cache, Soft3d_screen_point *out);
static int recorded_fixed_vertex(const Vertex TG_HUGE *vertex, Soft3d_fixed_point *out);
#define drv_draw_quad_flat_color recorded_driver_quad
#define drv_draw_quadbatch_flat_color recorded_driver_quadbatch
#define soft3d_fixed_prepare_degrees recorded_fixed_prepare
#define soft3d_float_prepare_degrees recorded_float_prepare
#define soft3d_fixed_transform_reuse recorded_fixed_transform
#define soft3d_386_transform_reuse recorded_386_transform
#define soft3d_float_transform_reuse recorded_float_transform
#define soft3d_fixed_project_cached recorded_fixed_cached
#define soft3d_float_project_cached recorded_float_cached
#define soft3d_fixed_vertex recorded_fixed_vertex
#define error recorded_mtx_error
#define clock recorded_mtx_clock
#undef fprintf
#define fprintf recorded_mtx_fprintf
#include "../src/drv_mtx.c"
#undef fprintf
#undef clock
#undef error
#undef drv_draw_quad_flat_color
#undef drv_draw_quadbatch_flat_color
#undef soft3d_fixed_prepare_degrees
#undef soft3d_float_prepare_degrees
#undef soft3d_fixed_transform_reuse
#undef soft3d_386_transform_reuse
#undef soft3d_float_transform_reuse
#undef soft3d_fixed_project_cached
#undef soft3d_float_project_cached
#undef soft3d_fixed_vertex

#ifdef TEXTURE
#error The Matrox driver must not compile texture mapping support
#endif
#if defined(EARTH_128) || defined(EARTH_256) || defined(EARTH_512)
#error The segmented Matrox driver must only include EARTH_32 and EARTH_64
#endif

extern int globe_longitude, globe_latitude;
extern Vertex TG_HUGE *globe_verts;

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
static unsigned int world_window_calls, distance_calls, hither_calls, yon_calls;
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
static unsigned int clock_calls;
static clock_t clock_script[4];
static unsigned int clock_script_count, clock_script_next;
/* Polls before each reply word, stale startup data and an asynchronous
 * second error after the first is acknowledged (a stalled shell resuming). */
static unsigned int word_delay, pending_delay;
static unsigned long stale_words;
static unsigned int stale_error_gap, error_gap;
static int stale_endless, stale_toggle;
static char stderr_text[512];
static unsigned short original_mask = 0x7f, current_mask = 0x7f;
static unsigned int mask_words = 1;
static int break_exit;
/* Command bytes written since the last completed reply, when the board has
 * executed everything before it. Sizes follow the manual's formats: 16-bit
 * opcodes and integers, 32-bit FIXPT values, six-byte LUTVALs. */
static unsigned int fifo_bytes, fifo_peak;
static unsigned int fills_since_sync, fill_syncs, transform_syncs;
static int transform_since_sync;
/* The board's color is known after KRHWSFCOL until a clear, text or a
 * reported error; resending it while known is redundant. */
static int color_known;
static unsigned long color_commands;
static char board_message[128];
static jmp_buf board_error;
static int expected_inquiry_error;
static jmp_buf inquiry_error;
static unsigned short mouse_buttons;
static short mouse_dx, mouse_dy;
static unsigned int mouse_resets, mouse_polls, mouse_motion_reads;
static short last_yaw, last_pitch;
static const Vertex *submitted_vertices[4];
static unsigned short submitted_color;
static int selected_cpu = 286, selected_fpu;
static unsigned int fixed_transforms, optimized_transforms, floating_transforms;
/* Software packets: points transformed (excluding reused rows) and
 * projected, and each queued quad's expected fill, matched in order. */
static unsigned long transformed_points, projected_points;
/* Native frames without an x87: vertices decoded for the integer test. */
static unsigned long cull_decodes;
#define EXPECT_EITHER 0
#define EXPECT_FILL 1
#define EXPECT_NONE 2
static const Vertex *batch_vertices[DRV_QUAD_BATCH_MAX][4];
static unsigned char batch_colors[DRV_QUAD_BATCH_MAX];
static int batch_expect[DRV_QUAD_BATCH_MAX];
static unsigned int batch_count, batch_cursor;
static int batch_active, direct_quad;
static unsigned int screen_polygons, world_polygons;
static unsigned int fail_v3_rotation, fail_v3_polygon, fail_v3_projection;
static unsigned int fail_world_window, fail_screen_polygon, fail_software_prepare;
static unsigned short injected_v3_error = 1;
static char osd_text[2048];

static void fifo_command(unsigned int bytes)
{
    fifo_bytes += bytes;
    /* The driver's own FIFO argument: at most 228 bytes in flight. */
    assert(fifo_bytes <= 228U);
    if (fifo_bytes > fifo_peak) fifo_peak = fifo_bytes;
}

static void fifo_transform(unsigned int bytes)
{
    fifo_command(bytes);
    transform_since_sync = 1;
}

int drv_cpu_detect(void) { return selected_cpu; }
int drv_cpu_has_fpu(void) { return selected_fpu; }
static int recorded_fixed_prepare(long matrix[3][3], int yaw, int pitch)
{
    if (fail_software_prepare) { --fail_software_prepare; return 0; }
    return soft3d_fixed_prepare_degrees(matrix, yaw, pitch);
}
static int recorded_float_prepare(float matrix[3][3], int yaw, int pitch)
{
    if (fail_software_prepare) { --fail_software_prepare; return 0; }
    return soft3d_float_prepare_degrees(matrix, yaw, pitch);
}
/* Software mode transforms; native mode never transforms on the host. */
static int recorded_fixed_transform(long matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_fixed_point *out, unsigned int reuse)
{
    assert(!selected_fpu && selected_cpu < 386 && drv_soft3d_active);
    ++fixed_transforms;
    transformed_points += count - reuse;
    return soft3d_fixed_transform_reuse(matrix, count, vertices, out, reuse);
}
static int recorded_386_transform(long matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_fixed_point *out, unsigned int reuse)
{
    assert(!selected_fpu && selected_cpu >= 386 && drv_soft3d_active);
    ++optimized_transforms;
    transformed_points += count - reuse;
    return soft3d_386_transform_reuse(matrix, count, vertices, out, reuse);
}
static int recorded_float_transform(float matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_float_point *out, unsigned int reuse,
    int precision)
{
    assert(selected_fpu && drv_soft3d_active && precision == SOFT3D_FLOAT_SINGLE);
    ++floating_transforms;
    transformed_points += count - reuse;
    return soft3d_float_transform_reuse(matrix, count, vertices, out, reuse, precision);
}
/* Newly projected points: cache entries that become valid. */
static unsigned long cache_entries(const Soft3d_project_cache TG_HUGE *cache,
                                   unsigned int count)
{
    unsigned long valid = 0;
    unsigned int i;
    for (i = 0; i < count && i < SOFT3D_MAX_VERTICES; ++i) valid += cache->valid[i] != 0;
    return valid;
}
static int recorded_fixed_cached(const Soft3d_fixed_point *points,
    unsigned int point_count, const unsigned int TG_HUGE *indices,
    unsigned int corners, unsigned int width, unsigned int height,
    Soft3d_project_cache TG_HUGE *cache, Soft3d_screen_point *out)
{
    unsigned long before;
    int count;
    assert(!selected_fpu && drv_soft3d_active);
    assert(width == 640U && height == 480U && cache == &mtx_cache);
    before = cache_entries(cache, point_count);
    count = soft3d_fixed_project_cached(points, point_count, indices, corners,
                                        width, height, cache, out);
    projected_points += cache_entries(cache, point_count) - before;
    return count;
}
static int recorded_float_cached(const Soft3d_float_point *points,
    unsigned int point_count, const unsigned int TG_HUGE *indices,
    unsigned int corners, unsigned int width, unsigned int height, int precision,
    Soft3d_project_cache TG_HUGE *cache, Soft3d_screen_point *out)
{
    unsigned long before;
    int count;
    assert(selected_fpu && drv_soft3d_active && precision == SOFT3D_FLOAT_SINGLE);
    assert(width == 640U && height == 480U && cache == &mtx_cache);
    before = cache_entries(cache, point_count);
    count = soft3d_float_project_cached(points, point_count, indices, corners,
                                        width, height, precision, cache, out);
    projected_points += cache_entries(cache, point_count) - before;
    return count;
}
static int recorded_fixed_vertex(const Vertex TG_HUGE *vertex, Soft3d_fixed_point *out)
{
    assert(!selected_fpu && !drv_soft3d_active);
    ++cull_decodes;
    return soft3d_fixed_vertex(vertex, out);
}

static void inject_v3_error(unsigned int *remaining)
{
    if (*remaining == 0U) return;
    --*remaining;
    assert(error_count == error_read);
    error_read = 0; error_count = 1;
    board_errors[0] = injected_v3_error;
}

static int recorded_mtx_fprintf(FILE *stream, const char *format, ...)
{
    char text[1024];
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = vsprintf(text, format, arguments);
    va_end(arguments);
    assert(result >= 0 && (size_t)result < sizeof(text));
    if (stream == stderr)
    {
        assert(strlen(stderr_text) + strlen(text) < sizeof(stderr_text));
        strcat(stderr_text, text);
    }
    return fputs(text, stream) == EOF ? -1 : result;
}

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
    ++clock_calls;
    if (clock_script_next < clock_script_count) return clock_script[clock_script_next++];
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

/* DOS 5 reports 5.0; drv_dos_break_init() installs INT 23h and INT 24h. */
unsigned char _osmajor = 5, _osminor = 0;
static void (*test_dos_vectors[256])();

void _dos_setvect(unsigned int number, void (*handler)())
{
    assert((number == 0x23 || number == 0x24) && handler != NULL);
    test_dos_vectors[number] = handler;
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

int kbhit(void)
{
    static unsigned int break_polls;
    /* After Ctrl-Break a hung event loop fails instead of spinning. */
    if (break_exit) assert(++break_polls < 1000U);
    return 0;
}
int getch(void) { assert(!"Unexpected blocking keyboard read"); return 0; }

static int board_absent;
void lsinit(short handle, short address)
{
    assert(handle == 0 && address == 1);
    ++init_calls;
}
#ifndef MTX_LSCL
int mtx_ls_present(void) { return init_calls != 0 && !board_absent; }
unsigned int mtx_ls_segment(void) { return 0xC600U; }
#endif
static unsigned int flush_calls;
/* Both bindings wait for queued commands and then read and discard every
 * reply word, so a flush must never separate a command from its reply.
 * Stale words behind unread errors are not readable yet and survive. */
void ioflush(void)
{
    assert(inquiry == 0 && inquiry_remaining == 0);
    if (error_read >= error_count && !clock_timeout) stale_words = 0UL;
    ++flush_calls;
}
void lslibsync(void) { assert(!"Blocking synchronization must not hide board errors"); }
void lsemulen(short restore)
{
    assert(restore == 1);
    fifo_command(4);
    ++close_calls;
}
void lserror(short enabled)
{
    assert(enabled == 0 || enabled == 1);
    fifo_command(4);
    error_reporting = enabled;
}
short iogeterror(void)
{
    ++error_polls;
    if (stale_endless)
    {
        stale_toggle = !stale_toggle;
        return stale_toggle ? 2 : 0;
    }
    if (error_read < error_count)
    {
        if (error_gap != 0U) { --error_gap; return 0; }
        if (error_read == 0U) error_gap = stale_error_gap;
        if (board_errors[error_read] != 0U) color_known = 0;
        return (short)board_errors[error_read++];
    }
    return 0;
}
short rdfifocnt(void)
{
    if (error_read < error_count || clock_timeout)
    {
        ++delayed_reads;
        return 0;
    }
    if (inquiry == 0 && stale_words == 0UL) return 0;
    if (pending_delay != 0U)
    {
        --pending_delay;
        ++delayed_reads;
        return 0;
    }
    return 1;
}
void lsinqsys(void)
{
    assert(inquiry == 0 && stale_words == 0UL);
    fifo_command(2);
    inquiry = 1;
    inquiry_remaining = 10;
    fixed_reply_offset = 0;
    pending_delay = word_delay;
}
void lsinq(unsigned short code)
{
    assert((code == 77 || code == 78 || code == 88) && inquiry == 0 && stale_words == 0UL);
    fifo_command(4);
    inquiry = code == 78 ? 2U : code == 88 ? 5U : 4U;
    if (code == 77) ++sync_calls;
    if (fills_since_sync != 0U) ++fill_syncs;
    if (transform_since_sync) ++transform_syncs;
    fills_since_sync = 0; transform_since_sync = 0;
    inquiry_header = 1;
    inquiry_remaining = code == 78 ? inquiry_words : code == 88 ? mask_words : 1U;
    pending_delay = word_delay;
}
short iogetw(void)
{
    unsigned int type, offset;
    static short system_info[10] = {640,480,256,0x0104,640,480,80,0x0108,0,0};
    assert(error_read >= error_count && !clock_timeout && pending_delay == 0U);
    if (stale_words != 0UL)
    {
        /* Words left by a previous program precede every new reply. */
        --stale_words;
        return 0x5a5a;
    }
    assert(inquiry != 0);
    ++inquiry_reads;
    pending_delay = word_delay;
    type = inquiry;
    if (inquiry_header)
    {
        inquiry_header = 0;
        if (inquiry_remaining == 0) inquiry = fifo_bytes = 0;
        return (short)(type == 2 ? inquiry_words : type == 5 ? mask_words : 1U);
    }
    assert(inquiry_remaining != 0);
    --inquiry_remaining;
    if (inquiry_remaining == 0) inquiry = fifo_bytes = 0;
    if (type == 1) return system_info[fixed_reply_offset++];
    if (type == 3)
    {
        offset = fixed_reply_offset++;
        return (short)original_palette[offset / 3U][offset % 3U];
    }
    if (type == 4) return error_reporting;
    if (type == 5) return (short)current_mask;
    return 3;
}
void lslutrd(unsigned short logical, unsigned short first, unsigned short count)
{
    assert(logical == 0 && first == 0 && count == 256 && inquiry == 0);
    fifo_command(8);
    inquiry = 3;
    inquiry_remaining = 768;
    fixed_reply_offset = 0;
    pending_delay = word_delay;
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
    fifo_command(8U + 6U * count);
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
    fifo_command(6);
    current_lut = logical;
}
void lslutpm(unsigned short physical, unsigned short mask)
{
    assert(physical == 0 && mask <= 255);
    fifo_command(6);
    current_mask = mask;
}
void lssbm(short bitmap) { assert(bitmap == -1); fifo_command(4); }
void lssclpl(short clip) { assert(clip == -1); fifo_command(4); }
void scsorg(unsigned short x, unsigned short y) { assert(x == 0 && y == 0); fifo_command(6); }
void krgcdi(void) { fifo_command(2); }
void krhwsbop(short operation) { assert(operation == 0); fifo_command(4); }
void krhwsmsk(unsigned short mask) { assert(mask == 255); fifo_command(4); }
void krhwstdef(unsigned short mode) { assert(mode == 0); fifo_command(4); }
void lssafapat(short pattern) { assert(pattern == -1); fifo_command(4); }
void lssfont(short font) { assert(font == -1 || font == -4); fifo_command(4); }
void sctxssf(double x, double y) { assert(x > 0 && y > 0); fifo_command(10); }
void sctxsdmode(short mode) { assert(mode == 0); fifo_command(4); }
void sctxsalign(short horizontal, short vertical)
{
    (void)horizontal; (void)vertical;
    fifo_command(6);
}
void sctxbstring(short x, short y, char *text)
{
    char expected[80];
    assert(x >= 0 && x < 640 && y >= 0 && y < 480);
    /* SCTXBSTRING sends SCTXSTRING: opcode, x, y, count, 16-bit chars. */
    fifo_command(8U + 2U * (unsigned int)strlen(text));
    color_known = 0;
    assert(strstr(text, "texture") == NULL);
    assert(strstr(text, "Texture") == NULL);
    assert(strstr(text, "T =") == NULL);
    assert(strlen(osd_text) + strlen(text) + 2U < sizeof(osd_text));
    strcat(osd_text, text); strcat(osd_text, "\n");
    sprintf(expected, "%lu quads (%lu visible)",
            globe_quads_num, globe_quads_num_vis);
    if (strstr(text, expected) == text) {
        /* A diagnostic can put FPS on the following wrapped OSD line. */
        if (!drv_fps_enabled) assert(strstr(text, "frames per second") == NULL);
        assert(strstr(text, "triangles") == NULL);
    }
    ++osd_calls;
}
void v2gpsvwp(short xmin, short xmax, short ymin, short ymax)
{
    assert(xmin == 0 && xmax == 639 && ymin == 0 && ymax == 479);
    fifo_command(10);
    ++viewport_calls;
}
/* The shared 45-degree field on the view plane through the reference point,
 * with the viewport's 639:479 aspect. */
static double window_width, window_height;
void v2gpswdw(double xmin, double xmax, double ymin, double ymax)
{
    double half = (double)GLOBE_VIEW_DISTANCE * tan(22.5 * PI / 180.0);
    assert(fabs(ymin + half) < 1e-9 && fabs(ymax - half) < 1e-9);
    assert(fabs(xmax + xmin) < 1e-9 && fabs(xmax - half * 639.0 / 479.0) < 1e-9);
    window_width = xmax - xmin; window_height = ymax - ymin;
    fifo_transform(18);
    ++world_window_calls;
    inject_v3_error(&fail_world_window);
}
/* The PGC projection (IBM PGC and PG-640A manuals) scales view x by
 * (window diagonal / 2) / tan(angle / 2) * (viewport width / window width)
 * over the depth. That must give the shared camera's focal length,
 * 240 / tan(22.5 degrees), within the whole-degree angle's rounding. */
void v3gpsproj(short projection)
{
    double diagonal, scale_x, scale_y, focal;
    assert(projection == 69);
    assert(window_width > 0.0 && window_height > 0.0);
    diagonal = sqrt(window_width * window_width + window_height * window_height);
    scale_x = diagonal / 2.0 / tan(projection * PI / 360.0) * 639.0 / window_width;
    scale_y = diagonal / 2.0 / tan(projection * PI / 360.0) * 479.0 / window_height;
    focal = 240.0 / tan(22.5 * PI / 180.0);
    assert(fabs(scale_x - scale_y) < 1e-9);
    assert(fabs(scale_x / focal - 1.0) < 0.005);
    fifo_transform(4);
    ++projection_calls; inject_v3_error(&fail_v3_projection);
}
void v3gpsdist(double distance) { assert(distance == GLOBE_VIEW_DISTANCE); fifo_transform(6); ++distance_calls; }
void v3gpshclip(short enabled) { assert(enabled == 0); fifo_transform(4); ++hither_calls; }
void v3gpsyclip(short enabled) { assert(enabled == 0); fifo_transform(4); ++yon_calls; }
void v3mdid(void) { fifo_transform(2); ++model_calls; }
void v3mdorg(double x, double y, double z) { assert(x == 0 && y == 0 && z == 0); fifo_transform(14); }
void v3mdrotz(short angle) { fifo_transform(4); last_yaw = angle; inject_v3_error(&fail_v3_rotation); }
void v3vwid(void) { fifo_transform(2); ++view_calls; }
void v3vwrpt(double x, double y, double z) { assert(x == 0 && y == 0 && z == 0); fifo_transform(14); }
void v3vwrotx(short angle) { fifo_transform(4); last_pitch = angle; }
void krhwcclr(unsigned short color)
{
    unsigned int i, brightness;
    assert(color < 256);
    fifo_command(4);
    color_known = 0;
    batch_active = 0; /* a frame (or retry) starts; drop any aborted packet */
    brightness = earth_cmap[color][0] + earth_cmap[color][1] + earth_cmap[color][2];
    for (i = 0; i < 256; ++i)
        assert(brightness <= (unsigned int)earth_cmap[i][0] + earth_cmap[i][1] +
                             earth_cmap[i][2]);
    submitted_quads = filled_polygons = filled_triangles = 0;
    expected_visible = expected_caps = silhouette_faces = silhouette_caps = 0;
    osd_text[0] = '\0';
}
void krhwsfcol(unsigned short color)
{
    assert(color < 256);
    assert(!color_known || color != last_color);
    fifo_command(4);
    color_known = 1;
    ++color_commands;
    last_color = color;
}

void v3arwplygn(unsigned short count, struct V3coord *vertices)
{
    unsigned int i, j;
    double radius;
    int found;
    assert(count == 3 || count == 4);
    assert(last_color == submitted_color);
    fifo_command(4U + 12U * count);
    ++fills_since_sync;
    assert(fills_since_sync <= 4U);
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
    ++world_polygons;
    if (count == 3) ++filled_triangles;
    inject_v3_error(&fail_v3_polygon);
}

/* Independent LibShell camera oracle: Rz(model yaw), then the inverse
 * viewer rotation Rx(-pitch), with the eye GLOBE_VIEW_DISTANCE from the
 * reference point and a 45-degree field across the 480-pixel height. */
static void project_native(const Vertex *point, double *x, double *y)
{
    double yaw, pitch, model_y, view_x, view_y, depth, focal;
    yaw = (double)mtx_angle(drv_rot_y) * PI / 180.0;
    pitch = (double)mtx_angle(90.0f + drv_rot_x) * PI / 180.0;
    view_x = cos(yaw) * point->x - sin(yaw) * point->y;
    model_y = sin(yaw) * point->x + cos(yaw) * point->y;
    view_y = cos(pitch) * model_y + sin(pitch) * point->z;
    depth = (double)GLOBE_VIEW_DISTANCE -
            (-sin(pitch) * model_y + cos(pitch) * point->z);
    focal = 240.0 / tan(22.5 * PI / 180.0);
    *x = 320.0 + view_x * focal / depth;
    *y = 240.0 - view_y * focal / depth;
}

/* Truncating division (below one pixel), the integer focal length (579
 * for 579.41: under 0.25 pixel at the screen edge) and Q14/Q4 transform
 * error (about 0.1 pixel) keep host points within 1.5 pixels of the
 * unquantized independent camera. */
static int near_corner(const struct Phcoord *point, const Vertex *const corners[4])
{
    unsigned int j;
    double x, y;
    for (j = 0; j < 4; ++j)
    {
        project_native(corners[j], &x, &y);
        if (fabs(point->x - x) <= 1.5 && fabs(point->y - y) <= 1.5)
            return 1;
    }
    return 0;
}

/* Screen rounding can collapse a thin front-facing quad. Require
 * sufficiently large facets and reject definite back faces using an
 * independent projected-area error bound, rather than forcing software
 * pixel counts to equal unrounded native geometry. */
static int software_expectation(const Vertex *const corners[4])
{
    double px[4], py[4], projected_area = 0.0, perimeter = 0.0;
    int index, next;
    for (index = 0; index < 4; ++index)
        project_native(corners[index], px + index, py + index);
    for (index = 0; index < 4; ++index)
    {
        next = (index + 1) % 4;
        projected_area += px[index] * py[next] - px[next] * py[index];
        perimeter += fabs(px[next] - px[index]) + fabs(py[next] - py[index]);
    }
    if (projected_area < -4.0 * perimeter - 8.0) return EXPECT_FILL;
    if (projected_area > 4.0 * perimeter + 8.0) return EXPECT_NONE;
    return EXPECT_EITHER;
}

/* Packet fills arrive in quad order. Skipped quads must not be definite
 * front faces; the filled one must not be a definite back face. */
static void match_batch_fill(const struct Phcoord *polygon, unsigned int count)
{
    unsigned int i, j;
    int match;
    for (; batch_cursor < batch_count; ++batch_cursor)
    {
        match = batch_colors[batch_cursor] == last_color;
        for (i = 0; i < count && match; ++i)
            match = near_corner(polygon + i, batch_vertices[batch_cursor]);
        if (match) break;
        assert(batch_expect[batch_cursor] != EXPECT_FILL);
    }
    assert(batch_cursor < batch_count);
    assert(batch_expect[batch_cursor] != EXPECT_NONE);
    for (j = 0; j < 4; ++j) submitted_vertices[j] = batch_vertices[batch_cursor][j];
    submitted_color = batch_colors[batch_cursor];
    ++batch_cursor;
}

void scarwplygn(unsigned short count, struct Phcoord *vertices)
{
    unsigned int i, j;
    long area;
    assert(drv_soft3d_active && count >= 3U && count <= SOFT3D_MAX_POLY);
    assert(batch_active);
    match_batch_fill(vertices, count);
    assert(last_color == submitted_color);
    fifo_command(4U + 4U * count);
    ++fills_since_sync;
    assert(fills_since_sync <= 4U);
    area = 0;
    for (i = 0; i < count; ++i)
    {
        assert(vertices[i].x >= 0 && vertices[i].x <= 639);
        assert(vertices[i].y >= 0 && vertices[i].y <= 479);
        assert(near_corner(vertices + i, submitted_vertices));
        j = (i + 1U) % count;
        area += (long)vertices[i].x * vertices[j].y -
                (long)vertices[j].x * vertices[i].y;
        assert(vertices[i].x != vertices[j].x || vertices[i].y != vertices[j].y);
    }
    assert(area < 0L);
    ++screen_polygons; ++filled_polygons;
    if (count == 3) ++filled_triangles;
    inject_v3_error(&fail_screen_polygon);
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *a, Vertex TG_HUGE *b,
                            Vertex TG_HUGE *c, Vertex TG_HUGE *d,
                            unsigned char color)
{
    double ux, uy, uz, vx, vy, vz, nx, ny, nz;
    double angle, model_y, view_x, view_y, view_z, facing, tolerance;
    double point_x, point_y, point_z;
    unsigned long before_fill;
    int cap;
    Vertex *third;
    /* Software frames stream bounded packets instead of world quads. */
    assert(!drv_soft3d_active);
    assert(submitted_quads < globe_faces_num);
    {
        unsigned char expected;
        assert(earth_read_data(globe_longitude, submitted_quads, 1U, &expected));
        assert(color == expected);
    }
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
    /* Independently transform the normal and a vertex using the commands
     * actually sent: model Rz(yaw), followed by inverse viewer Rx(-pitch).
     * The facet faces the eye, GLOBE_VIEW_DISTANCE along view-space Z, when
     * the normal points toward it from the vertex. This deliberately
     * avoids the driver's object-space formula. */
    angle = (double)last_yaw * PI / 180.0;
    view_x = cos(angle) * nx - sin(angle) * ny;
    model_y = sin(angle) * nx + cos(angle) * ny;
    point_x = cos(angle) * a->x - sin(angle) * a->y;
    point_y = sin(angle) * a->x + cos(angle) * a->y;
    angle = (double)last_pitch * PI / 180.0;
    view_y = cos(angle) * model_y + sin(angle) * nz;
    view_z = -sin(angle) * model_y + cos(angle) * nz;
    point_z = -sin(angle) * point_y + cos(angle) * a->z;
    point_y = cos(angle) * point_y + sin(angle) * a->z;
    facing = -view_x * point_x - view_y * point_y +
             view_z * ((double)GLOBE_VIEW_DISTANCE - point_z);
    tolerance = sqrt(nx * nx + ny * ny + nz * nz) * GLOBE_VIEW_DISTANCE * 0.000001;
    submitted_vertices[0] = a; submitted_vertices[1] = b;
    submitted_vertices[2] = c; submitted_vertices[3] = d;
    submitted_color = color;
    ++submitted_quads;
    before_fill = filled_polygons;
    recorded_driver_quad(a, b, c, d, color);
    assert(filled_polygons - before_fill <= 1);
    if (facing > tolerance) {
        ++expected_visible;
        if (cap) ++expected_caps;
        assert(filled_polygons - before_fill == 1);
    } else if (facing < -tolerance) {
        assert(filled_polygons == before_fill);
    } else {
        ++silhouette_faces;
        if (cap) ++silhouette_caps;
    }
}

static int same_vertex(const Vertex *a, const Vertex *b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

/* Called by the shared packet traversal and by drv_submit_quad_as_batch. */
void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                   const Vertex TG_HUGE *vertices,
                                   unsigned int quad_count,
                                   const unsigned int TG_HUGE *indices,
                                   const unsigned char TG_HUGE *colors)
{
    unsigned long quad, row, before;
    unsigned int i, j;
    const Vertex *mesh[4];
    unsigned char expected;
    double nx, ny, nz;
    assert(quad_count >= 1U && quad_count <= DRV_QUAD_BATCH_MAX);
    assert(vertex_count <= 2U * (DRV_QUAD_BATCH_MAX + 1U));
    /* Native frames draw world quads; only direct calls send packets. */
    assert(drv_soft3d_active || direct_quad);
    for (i = 0; i < quad_count; ++i)
    {
        for (j = 0; j < 4U; ++j) batch_vertices[i][j] = vertices + indices[4U * i + j];
        batch_colors[i] = colors[i];
        batch_expect[i] = EXPECT_EITHER;
        if (direct_quad) continue;
        /* Longitudes 32 and 64 fit one packet: quads stay row-major. */
        quad = submitted_quads++;
        assert(quad < globe_faces_num);
        assert(earth_read_data(globe_longitude, quad, 1U, &expected));
        assert(colors[i] == expected && colors[i] == expected_data[quad]);
        row = quad / (unsigned long)globe_longitude;
        mesh[0] = globe_verts + row * (globe_longitude + 1UL) +
                  quad % (unsigned long)globe_longitude;
        mesh[1] = mesh[0] + globe_longitude + 1;
        mesh[2] = mesh[1] + 1;
        mesh[3] = mesh[0] + 1;
        for (j = 0; j < 4U; ++j) assert(same_vertex(batch_vertices[i][j], mesh[j]));
        /* Outward winding, with d at the collapsed south pole. */
        j = same_vertex(mesh[1], mesh[2]) ? 3U : 2U;
        nx = (mesh[1]->y - mesh[0]->y) * (mesh[j]->z - mesh[0]->z) -
             (mesh[1]->z - mesh[0]->z) * (mesh[j]->y - mesh[0]->y);
        ny = (mesh[1]->z - mesh[0]->z) * (mesh[j]->x - mesh[0]->x) -
             (mesh[1]->x - mesh[0]->x) * (mesh[j]->z - mesh[0]->z);
        nz = (mesh[1]->x - mesh[0]->x) * (mesh[j]->y - mesh[0]->y) -
             (mesh[1]->y - mesh[0]->y) * (mesh[j]->x - mesh[0]->x);
        assert(nx * mesh[0]->x + ny * mesh[0]->y + nz * mesh[0]->z > 0.0);
        batch_expect[i] = software_expectation(batch_vertices[i]);
    }
    if (!drv_soft3d_active)
    {
        /* A direct native packet sends each quad's world vertices. */
        for (j = 0; j < 4U; ++j) submitted_vertices[j] = batch_vertices[0][j];
        submitted_color = colors[0];
    }
    batch_count = quad_count; batch_cursor = 0; batch_active = 1;
    before = filled_polygons;
    recorded_driver_quadbatch(vertex_count, vertices, quad_count, indices, colors);
    batch_active = 0;
    assert(filled_polygons - before <= quad_count);
    if (!drv_soft3d_active) return;
    for (; batch_cursor < batch_count; ++batch_cursor)
        assert(batch_expect[batch_cursor] != EXPECT_FILL);
}

static void direct_driver_quad(Vertex *a, Vertex *b, Vertex *c, Vertex *d,
                               unsigned char color)
{
    direct_quad = 1;
    recorded_driver_quad(a, b, c, d, color);
    direct_quad = 0;
}

static void render_and_check(unsigned long faces, unsigned char *data)
{
    unsigned int before_sync, before_model, before_view;
    unsigned int before_fill_syncs, before_transform_syncs;
    unsigned long before_colors, before_transformed, before_projected, before_cull, points;
    double elevation;
    int software;
    submitted_quads = filled_polygons = filled_triangles = 0;
    expected_visible = expected_caps = silhouette_faces = silhouette_caps = 0;
    expected_data = data;
    before_sync = sync_calls; before_model = model_calls; before_view = view_calls;
    before_fill_syncs = fill_syncs; before_transform_syncs = transform_syncs;
    before_colors = color_commands;
    before_transformed = transformed_points; before_projected = projected_points;
    before_cull = cull_decodes;
    software = drv_soft3d_active;
    /* Sine of the eye's elevation above the equator, from the frame's pose. */
    elevation = cos((double)mtx_angle(90.0f + drv_rot_x) * PI / 180.0);
    drv_render();
    /* Native frames without an x87 run the integer facing test per quad. */
    if (!software && !drv_soft3d_active)
        assert((cull_decodes > before_cull) == !selected_fpu);
    if (software && drv_soft3d_active)
    {
        /* Row reuse: each mesh vertex is transformed and projected once. */
        points = (globe_longitude + 1UL) * (globe_latitude + 1UL);
        assert(transformed_points - before_transformed == points);
        assert(projected_points - before_projected == points);
    }
    /* One checkpoint per four fills, one for the model/view commands and
     * a color command only when the next fill's palette index changes. */
    if (software == drv_soft3d_active)
    {
        assert(fill_syncs - before_fill_syncs == (filled_polygons + 3UL) / 4UL);
        assert(transform_syncs - before_transform_syncs == (software ? 0U : 1U));
        assert(color_commands - before_colors < filled_polygons);
    }
    assert(submitted_quads == faces);
    assert(filled_polygons > 0 && filled_polygons < faces);
    /* Duplicate pole vertices are compacted into triangles. From three radii
     * away a pole is in view when the eye is more than asin(1/3) above or
     * below the equator; the margin keeps a whole cap triangle in view. */
    if (fabs(elevation) > 0.4) assert(filled_triangles > 0);
    if (!drv_soft3d_active)
    {
        assert(filled_polygons >= expected_visible &&
               filled_polygons <= expected_visible + silhouette_faces);
        assert(filled_triangles >= expected_caps &&
               filled_triangles <= expected_caps + silhouette_caps);
    }
    assert(globe_quads_num_vis == filled_polygons);
    assert(sync_calls > before_sync);
    if (software)
        assert(model_calls == before_model && view_calls == before_view);
    else if (!drv_soft3d_active)
    {
        assert(model_calls > before_model && view_calls > before_view);
        assert(last_yaw >= 0 && last_yaw < 360);
        assert(last_pitch >= 0 && last_pitch < 360);
    }
}

static void check_noncardinal_poses(unsigned long faces, unsigned char *data)
{
    drv_rot_y = 35.75f; drv_rot_x = 13.5f;
    render_and_check(faces, data);
    assert(drv_soft3d_active || (last_yaw == 35 && last_pitch == 103));
    assert(silhouette_faces == 0);
    drv_rot_y = 211.8f; drv_rot_x = -40.5f;
    render_and_check(faces, data);
    assert(drv_soft3d_active || (last_yaw == 211 && last_pitch == 49));
    assert(silhouette_faces == 0);
    drv_rot_y = 73.2f; drv_rot_x = 278.8f;
    render_and_check(faces, data);
    assert(drv_soft3d_active || (last_yaw == 73 && last_pitch == 8));
    assert(silhouette_faces == 0);
}

static void cleanup_checks(void)
{
    assert(inquiry == 0 && inquiry_header == 0 && inquiry_remaining == 0);
    assert(close_calls == 1);
    assert(palette_uploads == 2);
    assert(memcmp(supplied_palette, original_palette, sizeof(original_palette)) == 0);
    assert(current_lut == 3 && current_mask == original_mask);
    assert(error_reporting == initial_error_reporting);
    assert(allocated_blocks == 0);
    assert(fifo_bytes == 4U && fifo_peak <= 228U);
    if (break_exit)
        puts("PASS Matrox Ctrl-Break: INT 23h sets a flag and the event loop exits through board, palette and mask cleanup");
    else
        puts("PASS Matrox cleanup: Escape, board emulation, palette selection/content, plane mask, huge allocations");
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
            (void)mtx_checkpoint("command test", FALSE);
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
        (void)mtx_checkpoint("multiple errors", FALSE);
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
        (void)mtx_checkpoint("timeout test", FALSE);
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

    /* Ready words are read before the clock and need none. Reply lengths
     * are bounded by count, so an always-ready reply still terminates. */
    polls = clock_calls;
    assert(mtx_inquire_lut() == 3 && clock_calls == polls && inquiry == 0);
    /* One deadline spans the reply: each word waits one poll while the
     * clock advances half the limit. Restarting it per word never expires. */
    expected_board_error = 1;
    clock_step = MTX_WAIT_TICKS / 2UL;
    inquiry_words = 2; word_delay = 1;
    polls = inquiry_reads;
    if (setjmp(board_error) == 0)
    {
        (void)mtx_inquire_lut();
        assert(!"Waits for successive words must share the reply deadline");
    }
    expected_board_error = 0;
    clock_step = 0; word_delay = 0;
    assert(strstr(board_message, "communication timeout") != NULL && mtx_io_failed);
    assert(inquiry_reads - polls == 2U && inquiry_remaining == 1U);
    mtx_io_failed = FALSE;
    inquiry = inquiry_header = inquiry_remaining = 0;
    pending_delay = 0;
    inquiry_words = 1;
    puts("PASS Matrox reply bounds: ready words read no clock; one deadline covers every wait of a reply");
    check_backward_clock();
}

static void check_backward_clock(void)
{
    unsigned int calls;
    /* DOS clock() can step backward at midnight. That step adds nothing:
     * nearly the whole limit afterward still completes the reply. */
    clock_script[0] = (clock_t)(3UL * MTX_WAIT_TICKS);
    clock_script[1] = 0;
    clock_script[2] = (clock_t)(MTX_WAIT_TICKS - 1UL);
    clock_script[3] = (clock_t)(MTX_WAIT_TICKS - 1UL);
    clock_script_count = 4; clock_script_next = 0;
    word_delay = 2;
    calls = clock_calls;
    assert(mtx_inquire_lut() == 3 && !mtx_io_failed && inquiry == 0);
    assert(clock_calls - calls == 4U && clock_script_next == 4U);
    /* Nor does a backward step restart the deadline: half the limit before
     * it and half after still expires within one reply. */
    clock_script[0] = 100;
    clock_script[1] = (clock_t)(100UL + MTX_WAIT_TICKS / 2UL);
    clock_script[2] = 50;
    clock_script[3] = (clock_t)(50UL + MTX_WAIT_TICKS / 2UL);
    clock_script_next = 0;
    word_delay = 4;
    expected_board_error = 1;
    if (setjmp(board_error) == 0)
    {
        (void)mtx_inquire_lut();
        assert(!"A backward clock step must not restart the reply deadline");
    }
    expected_board_error = 0;
    assert(strstr(board_message, "communication timeout") != NULL && mtx_io_failed);
    assert(clock_script_next == 4U);
    clock_script_count = clock_script_next = 0;
    word_delay = pending_delay = 0;
    mtx_io_failed = FALSE;
    inquiry = inquiry_header = inquiry_remaining = 0;
    puts("PASS Matrox backward clock: backward steps add no time and do not restart the reply deadline");
}

static void check_stale_limits(void)
{
    unsigned int calls;
    /* Bounded discard: a board that never stops replying is abandoned. */
    stale_words = MTX_STALE_LIMIT + 1UL;
    expected_board_error = 1;
    if (setjmp(board_error) == 0)
    {
        (void)mtx_discard_stale();
        assert(!"An endless stale reply must be bounded");
    }
    assert(strstr(board_message, "stale replies") != NULL && mtx_io_failed);
    assert(stale_words == 1UL);
    stale_words = 0; mtx_io_failed = FALSE;
    /* Deadline: errors keep arriving, each before the quiet interval ends. */
    stale_endless = 1; clock_step = MTX_STALE_QUIET / 2UL;
    if (setjmp(board_error) == 0)
    {
        (void)mtx_discard_stale();
        assert(!"Endless stale errors must meet the deadline");
    }
    expected_board_error = 0;
    assert(strstr(board_message, "stale replies") != NULL && mtx_io_failed);
    stale_endless = stale_toggle = 0; clock_step = 0; mtx_io_failed = FALSE;
    calls = clock_calls;
    assert(mtx_discard_stale() && inquiry == 0 && clock_calls == calls);
    puts("PASS Matrox stale replies: discard is bounded by count and deadline; an idle board is not delayed");
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

static void check_direct_quad(void)
{
    unsigned long top, before_fill, before_triangles, before_quads, before_caps;
    unsigned char color;
    Vertex *a, *b, *c;
    /* Default view looks toward the positive-Y equatorial hemisphere. A
     * real outward facet there, given as a quad with a repeated corner,
     * exercises direct calls, NULL rejection and the original color byte. */
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
    direct_driver_quad(NULL, b, c, a, color);
    direct_driver_quad(a, NULL, c, a, color);
    direct_driver_quad(a, b, NULL, a, color);
    direct_driver_quad(a, b, c, NULL, color);
    assert(filled_polygons == before_fill &&
           globe_tris_num_vis == before_triangles &&
           globe_quads_num_vis == before_quads);
    before_caps = filled_triangles;
    direct_driver_quad(a, b, c, c, color);
    assert(filled_polygons == before_fill + 1UL);
    assert(filled_triangles == before_caps + 1UL);
    assert(globe_tris_num_vis == before_triangles);
    assert(globe_quads_num_vis == before_quads + 1UL);
    assert(last_color == color);
    {
        /* A direct two-quad packet: the facet and its reverse winding. */
        static const unsigned int indices[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
        unsigned char colors[2];
        Vertex packet[4];
        packet[0] = *a; packet[1] = *b; packet[2] = b[1]; packet[3] = *c;
        colors[0] = colors[1] = color;
        before_fill = filled_polygons;
        before_quads = globe_quads_num_vis;
        direct_quad = 1;
        drv_submit_quadbatch(4U, packet, 2U, indices, colors, FALSE);
        direct_quad = 0;
        assert(filled_polygons == before_fill + 1UL);
        assert(globe_quads_num_vis == before_quads + 1UL && last_color == color);
    }
    puts("PASS direct quad drawing: NULL corners rejected, repeated corner compacted, original color byte, direct packets");
}

/* Row reuse and rejected inputs match the full kernels, so the x/y kernels
 * can replace them without changing a packet's outcome. */
/* The integer facing bound assumes the Q14 last row is within 2/16384 of
 * the exact eye direction, so the Q4 integer eye is within 7 of the exact
 * eye position, for every integer-degree pose. */
static void check_integer_eye(void)
{
    long matrix[3][3];
    double exact[3], row, eye, worst_row = 0.0, worst_eye = 0.0;
    int yaw, pitch, i;
    for (yaw = 0; yaw < 360; ++yaw)
        for (pitch = 0; pitch < 360; ++pitch)
        {
            assert(soft3d_fixed_prepare_degrees(matrix, yaw, pitch));
            exact[0] = -sin(yaw * PI / 180.0) * sin(pitch * PI / 180.0);
            exact[1] = -cos(yaw * PI / 180.0) * sin(pitch * PI / 180.0);
            exact[2] = cos(pitch * PI / 180.0);
            for (i = 0; i < 3; ++i)
            {
                row = fabs((double)matrix[2][i] / 16384.0 - exact[i]) * 16384.0;
                eye = fabs((double)(matrix[2][i] * (GLOBE_VIEW_DISTANCE * 16L) / 16384L) -
                           exact[i] * GLOBE_VIEW_DISTANCE * 16.0);
                if (row > worst_row) worst_row = row;
                if (eye > worst_eye) worst_eye = eye;
            }
        }
    assert(worst_row <= 2.0 && worst_eye <= 7.0);
    printf("PASS Matrox integer eye: Q14 last row within %.2f/16384 of the exact eye "
           "direction and the Q4 eye within %.2f of its position at every integer-degree "
           "pose\n", worst_row, worst_eye);
}

/* Without an x87, native culling decides most facings with the integer
 * object-space test and defers the rest to the float test. Every decision
 * must equal the float test, so the submitted polygons are unchanged. */
static void check_integer_culling(void)
{
    static const int yaws[] = { 0, 35, 73, 90, 135, 180, 211, 270, 359 };
    static const int pitches[] = { 0, 8, 49, 90, 103, 180, 225, 270, 300 };
    const Vertex TG_HUGE *source[4], *kept[4];
    struct V3coord vertices[4];
    unsigned long decided = 0, total = 0, row, column, top;
    unsigned int y, x;
    float rot_x = drv_rot_x, rot_y = drv_rot_y;
    int facing;
    if (selected_fpu) return;
    assert(!drv_soft3d_active);
    for (y = 0; y < sizeof(yaws) / sizeof(yaws[0]); ++y)
        for (x = 0; x < sizeof(pitches) / sizeof(pitches[0]); ++x)
        {
            drv_rot_y = (float)yaws[y];
            drv_rot_x = (float)pitches[x] - 90.0f;
            assert(mtx_set_transform() && mtx_integer_cull);
            for (row = 0; row < (unsigned long)globe_latitude; ++row)
                for (column = 0; column < (unsigned long)globe_longitude; ++column)
                {
                    top = row * (globe_longitude + 1UL) + column;
                    source[0] = globe_verts + top;
                    source[1] = source[0] + globe_longitude + 1;
                    source[2] = source[1] + 1;
                    source[3] = source[0] + 1;
                    if (mtx_compact(source, vertices, kept) < 3) continue;
                    facing = mtx_integer_facing(kept);
                    ++total;
                    if (facing == 0) continue;
                    ++decided;
                    assert((facing > 0) == mtx_float_visible(vertices));
                }
        }
    /* Only facings near the silhouette or at thin pole caps need floats. */
    assert(decided * 10UL >= total * 9UL);
    printf("PASS Matrox integer culling: %lu of %lu facings at %d longitudes decided "
           "without floating point, each equal to the float test\n",
           decided, total, globe_longitude);
    drv_rot_x = rot_x; drv_rot_y = rot_y;
}

/* The host kernels for LibShell's camera: baseline and 386 transforms are
 * identical, and with the float kernels project within about a pixel of
 * an independent double-precision perspective camera. */
static void check_perspective_math(void)
{
    static const unsigned int corners[4] = { 0, 1, 2, 3 };
    long fixed_matrix[3][3];
    float floating_matrix[3][3];
    Vertex vertices[4], probe;
    Soft3d_fixed_point fixed[4], optimized[4], decoded;
    Soft3d_float_point floating[4];
    Soft3d_project_cache cache;
    Soft3d_screen_point a[SOFT3D_MAX_POLY], b[SOFT3D_MAX_POLY];
    int yaw, pitch, i;
    double cy, sy, cp, sp, view_x, model_y, view_y, depth, focal, x, y;
    for (i = 0; i < 4; ++i)
    {
        vertices[i].x = (float)(i * 300 - 500);
        vertices[i].y = (float)(i * 200 - 250);
        vertices[i].z = (float)(i * 110 - 100);
    }
    focal = 240.0 / tan(22.5 * PI / 180.0);
    for (yaw = -360; yaw < 720; yaw += 13)
        for (pitch = -360; pitch < 720; pitch += 17)
        {
            assert(soft3d_fixed_prepare_degrees(fixed_matrix, yaw, pitch));
            assert(soft3d_float_prepare_degrees(floating_matrix, yaw, pitch));
            assert(soft3d_fixed_transform_reuse(fixed_matrix, 4, vertices, fixed, 0));
            assert(soft3d_386_transform_reuse(fixed_matrix, 4, vertices, optimized, 0));
            assert(memcmp(fixed, optimized, sizeof(fixed)) == 0);
            assert(soft3d_float_transform_reuse(floating_matrix, 4, vertices, floating,
                                               0, SOFT3D_FLOAT_SINGLE));
            assert(soft3d_project_cache_begin(&cache, 4, 640, 480, 0));
            assert(soft3d_fixed_project_cached(fixed, 4, corners, 4, 640, 480, &cache, a) == 4);
            assert(soft3d_project_cache_begin(&cache, 4, 640, 480, 0));
            assert(soft3d_float_project_cached(floating, 4, corners, 4, 640, 480,
                                               SOFT3D_FLOAT_SINGLE, &cache, b) == 4);
            cy = cos(yaw * PI / 180.0); sy = sin(yaw * PI / 180.0);
            cp = cos(pitch * PI / 180.0); sp = sin(pitch * PI / 180.0);
            for (i = 0; i < 4; ++i)
            {
                view_x = cy * vertices[i].x - sy * vertices[i].y;
                model_y = sy * vertices[i].x + cy * vertices[i].y;
                view_y = cp * model_y + sp * vertices[i].z;
                depth = (double)GLOBE_VIEW_DISTANCE - (-sp * model_y + cp * vertices[i].z);
                x = 320.0 + view_x * focal / depth;
                y = 240.0 - view_y * focal / depth;
                assert(fabs(a[i].x - x) <= 1.5 && fabs(a[i].y - y) <= 1.5);
                assert(fabs(b[i].x - x) <= 1.5 && fabs(b[i].y - y) <= 1.5);
            }
        }
    /* Object-space Q4 decoding for integer culling: truncation toward zero,
     * the kernels' radius domain and non-finite rejection. */
    probe.x = 12.34f; probe.y = -12.34f; probe.z = 1000.0f;
    assert(soft3d_fixed_vertex(&probe, &decoded));
    assert(decoded.x == 197 && decoded.y == -197 && decoded.z == 16000);
    probe.z = -1000.0625f;
    assert(!soft3d_fixed_vertex(&probe, &decoded));
    probe.z = 0.0f;
    memset(&probe.y, 0xff, sizeof(probe.y));
    assert(!soft3d_fixed_vertex(&probe, &decoded));
    assert(!soft3d_fixed_vertex(NULL, &decoded));
    assert(!soft3d_fixed_vertex(vertices, NULL));
    assert(!soft3d_fixed_prepare_degrees(NULL, 0, 0));
    assert(!soft3d_float_prepare_degrees(NULL, 0, 0));
    check_integer_eye();
    puts("PASS DOS perspective math: LibShell's camera against an independent oracle, baseline/386 equality, float agreement, Q4 decoding and invalid-input rejection");
}

static void check_software_controls(void)
{
    unsigned int native_before, screen_before;
    assert(!drv_soft3d_requested && !drv_soft3d_active);
    drv_rot_x = 0.0f; drv_rot_y = 180.0f;
    native_before = world_polygons;
    fail_software_prepare = 1U;
    mtx_keyboard('S');
    assert(drv_soft3d_requested && !drv_soft3d_active && !fail_software_prepare);
    render_and_check(512, test_earth_data(32));
    assert(strstr(osd_text, "soft3d fail (LibShell)") != NULL);
    assert(world_polygons > native_before);
    native_before = world_polygons;
    mtx_keyboard('S');
    assert(drv_soft3d_requested && drv_soft3d_active);
    {
        unsigned long points = transformed_points;
        unsigned int syncs = sync_calls;
        render_and_check(512, test_earth_data(32));
        printf("PASS Matrox software packets: a key-1 frame transforms and projects %lu "
               "points (not 2048 quad corners) with %u checkpoints for %lu fills\n",
               transformed_points - points, sync_calls - syncs, filled_polygons);
    }
    assert(world_polygons == native_before && screen_polygons > 0);
    assert(strstr(osd_text, "soft3d on") != NULL && strstr(osd_text, "S = soft3d") != NULL);
    check_direct_quad();
    if (selected_fpu) assert(floating_transforms && !fixed_transforms && !optimized_transforms);
    else if (selected_cpu >= 386) assert(optimized_transforms && !fixed_transforms && !floating_transforms);
    else assert(fixed_transforms && !optimized_transforms && !floating_transforms);
    check_noncardinal_poses(512, test_earth_data(32));
    mtx_keyboard('2');
    render_and_check(2048, test_earth_data(64));
    check_noncardinal_poses(2048, test_earth_data(64));
    screen_before = screen_polygons;
    mtx_keyboard('s');
    assert(!drv_soft3d_requested && !drv_soft3d_active);
    render_and_check(2048, test_earth_data(64));
    assert(screen_polygons == screen_before && world_polygons > native_before);
    assert(strstr(osd_text, "soft3d off (LibShell)") != NULL);
    mtx_keyboard('1');
    puts("PASS Matrox software selection: S/s, CPU/x87 dispatch, LibShell's perspective camera, bounded screen polygons, palette bytes, both LODs and native restoration");
}

static void check_software_clipping(void)
{
    static const unsigned int corners[4] = { 0, 1, 2, 3 };
    Vertex vertices[4];
    Soft3d_fixed_point fixed[4];
    Soft3d_screen_point projected[SOFT3D_MAX_POLY], swap;
    unsigned int i, before;
    /* The ordinary radius-1000 globe stays inside the view. Exercise
     * generated clip intersections independently with eye-space points
     * that the perspective projector places beyond every screen edge. */
    fixed[0].x = fixed[1].x = -48000;
    fixed[2].x = fixed[3].x = 48000;
    fixed[0].y = fixed[3].y = 48000;
    fixed[1].y = fixed[2].y = -48000;
    for (i = 0; i < 4U; ++i) fixed[i].z = -32000;
    assert(soft3d_fixed_project_polygon(fixed, 4, corners, 4, 640, 480, projected) == 4);
    assert(soft3d_finish_polygon(projected, 4, 639, 479) == 4);
    for (i = 0; i < 4U; ++i)
    {
        assert(projected[i].x == 0 || projected[i].x == 639);
        assert(projected[i].y == 0 || projected[i].y == 479);
    }
    swap = projected[0]; projected[0] = projected[3]; projected[3] = swap;
    swap = projected[1]; projected[1] = projected[2]; projected[2] = swap;
    assert(!soft3d_finish_polygon(projected, 4, 639, 479));
    for (i = 0; i < 4U; ++i) fixed[i].x = 32000L + (long)i * 16L;
    assert(soft3d_fixed_project_polygon(fixed, 4, corners, 4, 640, 480, projected) == 4);
    assert(!soft3d_finish_polygon(projected, 4, 639, 479));
    assert(!drv_soft3d_active);
    mtx_keyboard('S');
    drv_rot_x = -90.0f; drv_rot_y = 0.0f;
    assert(mtx_set_transform());
    vertices[0].x = vertices[1].x = -500.0f;
    vertices[2].x = vertices[3].x = 500.0f;
    vertices[0].y = vertices[3].y = 500.0f;
    vertices[1].y = vertices[2].y = -500.0f;
    for (i = 0; i < 4U; ++i)
    {
        vertices[i].z = 0.0f;
        submitted_vertices[i] = vertices + i;
    }
    submitted_color = 129;
    before = screen_polygons;
    direct_driver_quad(vertices, vertices + 1, vertices + 2, vertices + 3, 129);
    assert(screen_polygons == before + 1U && last_color == 129);
    direct_driver_quad(vertices + 3, vertices + 2, vertices + 1, vertices, 129);
    assert(screen_polygons == before + 1U);
    mtx_keyboard('S');
    drv_rot_x = 0.0f; drv_rot_y = 180.0f;
    puts("PASS Matrox screen clipping: all four boundaries, reversed winding and wholly offscreen rejection retain indexed polygon color");
}

static void retry_native(void)
{
    unsigned int world, projection, distance, hither, yon;
    assert(drv_soft3d_active && !drv_soft3d_requested);
    world = world_window_calls; projection = projection_calls;
    distance = distance_calls; hither = hither_calls; yon = yon_calls;
    mtx_keyboard('S');
    assert(!drv_soft3d_requested && !drv_soft3d_active);
    assert(world_window_calls == world + 1U && projection_calls == projection + 1U);
    assert(distance_calls == distance + 1U && hither_calls == hither + 1U && yon_calls == yon + 1U);
    render_and_check(512, test_earth_data(32));
}

static void check_transform_recovery(void)
{
    unsigned int i, stage;
    drv_rot_x = 13.5f; drv_rot_y = 35.75f;
    for (stage = 0; stage < 2U; ++stage)
        for (i = 0; i < 4U; ++i)
        {
            assert(!drv_soft3d_active && !drv_soft3d_requested);
            injected_v3_error = (unsigned short)(1U << i);
            if (stage == 0U) fail_v3_rotation = 1U;
            else fail_v3_polygon = 1U;
            render_and_check(512, test_earth_data(32));
            assert(drv_soft3d_active && !drv_soft3d_requested);
            assert(!drv_frame_failed && !mtx_io_failed);
            assert(inquiry == 0 && error_read == error_count);
            assert(strstr(osd_text, "soft3d on") != NULL);
            retry_native();
        }
    /* Failed S requests preserve host mode, and S retries the entire native
     * projection configuration before the next model/view frame. */
    mtx_keyboard('S');
    injected_v3_error = 2U; fail_v3_projection = 1U;
    mtx_keyboard('S');
    assert(drv_soft3d_active && !drv_soft3d_requested);
    render_and_check(512, test_earth_data(32));
    retry_native();

    injected_v3_error = 17U; fail_v3_rotation = 1U;
    expected_board_error = 1;
    if (setjmp(board_error) == 0)
    {
        drv_render();
        assert(!"Mixed V3 and command-list faults must remain fatal");
    }
    expected_board_error = 0;
    assert(!drv_soft3d_active && strstr(board_message, "0x11") != NULL);
    assert(inquiry == 0 && error_read == error_count);
    /* Only the fixture resumes after fatal error(); the application exits. */
    mtx_keyboard('S');
    injected_v3_error = 1U; fail_screen_polygon = 1U;
    expected_board_error = 1;
    if (setjmp(board_error) == 0)
    {
        drv_render();
        assert(!"2D fill faults must remain fatal in software mode");
    }
    expected_board_error = 0;
    assert(strstr(board_message, "screen polygon fill") != NULL);
    assert(drv_soft3d_active && inquiry == 0 && error_read == error_count);
    mtx_keyboard('S');
    render_and_check(512, test_earth_data(32));
    puts("PASS Matrox transform recovery: four V3 error classes, setup and polygon failure, complete host retry, S restoration, fatal mixed faults and fatal 2D errors");
}

int main(int process_argc, char **process_argv)
{
    unsigned int i;
    int argc;
    int startup_failure = 0, stale_profile = 0, break_profile = 0;
    char *argv[2];
    float x, y;
    if (process_argc > 1)
    {
        const char *profile;
        assert(process_argc == 2);
        profile = process_argv[1];
        if (strcmp(profile, "reporting-off") == 0) error_reporting = initial_error_reporting = 0;
        else if (strcmp(profile, "startup-v3") == 0) startup_failure = 1;
        else if (strcmp(profile, "startup-world") == 0) startup_failure = 2;
        else if (strcmp(profile, "stale") == 0) stale_profile = 1;
        else if (strcmp(profile, "ctrl-break") == 0) break_profile = 1;
        else if (strcmp(profile, "8086") == 0) selected_cpu = 86;
        else if (strcmp(profile, "286") == 0) selected_cpu = 286;
        else if (strcmp(profile, "386") == 0) selected_cpu = 386;
        else if (strcmp(profile, "486") == 0) selected_cpu = 486;
        else if (strcmp(profile, "8087") == 0) { selected_cpu = 86; selected_fpu = 1; }
        else if (strcmp(profile, "287") == 0) { selected_cpu = 286; selected_fpu = 1; }
        else if (strcmp(profile, "387") == 0) { selected_cpu = 386; selected_fpu = 1; }
        else if (strcmp(profile, "fpu") == 0) { selected_cpu = 486; selected_fpu = 1; }
        else if (strcmp(profile, "absent") == 0) board_absent = 1;
        else assert(!"Unknown Matrox profile");
    }
    for (i = 0; i < 256; ++i) {
        original_palette[i][0] = (unsigned short)((i + 11) & 255);
        original_palette[i][1] = (unsigned short)((i + 23) & 255);
        original_palette[i][2] = (unsigned short)((i + 47) & 255);
    }
    argc = 1; argv[0] = "testglob.exe"; argv[1] = NULL;
#ifndef MTX_LSCL
    if (board_absent)
    {
        /* No board answers the presence test: stop before any command. */
        drv_ext_init();
        expected_board_error = 1;
        if (setjmp(board_error) == 0)
        {
            drv_init(&argc, argv);
            assert(!"A missing LIB Shell board must stop initialization");
        }
        expected_board_error = 0;
        assert(strstr(board_message, "segment C600h") != NULL);
        assert(strstr(board_message, "PG1281") != NULL);
        assert(init_calls == 1 && !mtx_initialized && fifo_bytes == 0U);
        puts("PASS Matrox board check: a board that does not answer stops initialization before any command");
        return 0;
    }
#endif
#if defined(MTX_LSCL) && !defined(MTX_8086_BINDING)
    if (selected_cpu < 286)
    {
        /* The supplied binding uses 286 instructions; reject before lsinit
         * or any interrupt vector changes. */
        drv_ext_init();
        expected_board_error = 1;
        if (setjmp(board_error) == 0)
        {
            drv_init(&argc, argv);
            assert(!"The 286 LibShell binding must reject an 8086");
        }
        expected_board_error = 0;
        assert(strstr(board_message, "80286") != NULL);
        assert(init_calls == 0 && !mtx_initialized && test_dos_vectors[0x23] == NULL);
        puts("PASS Matrox CPU requirement: the supplied 286 binding rejects 8086-class CPUs before lsinit");
        return 0;
    }
#endif
    /* Registered first, so this checks the production atexit cleanup last. */
    assert(atexit(cleanup_checks) == 0);
    /* Main owns the scene; native cleanup remains registered afterward. */
    assert(atexit(globe_free) == 0);
    drv_ext_init();
    assert(!drv_soft3d_requested && !drv_soft3d_active);
    assert(GLOBE_DEFAULT_LONGITUDE == 32);
    assert(drv_colormapped && !drv_texture_avail && !drv_texture_enabled);
    assert(strstr(drv_help, "texture") == NULL && strstr(drv_help, "1-2") != NULL);
    check_invalid_inquiry(0);
    check_invalid_inquiry(2);
    check_invalid_inquiry(65535U);
    inquiry_words = 1;
    if (stale_profile)
    {
        /* A previous program left two errors, the second reported only
         * after the first is read, and unread reply words behind them. */
        board_errors[0] = 2; board_errors[1] = 8;
        error_read = 0; error_count = 2; stale_error_gap = 3;
        stale_words = 5;
    }
    drv_init(&argc, argv);
    assert(init_calls == 1 && mouse_resets > 0);
    assert(test_dos_vectors[0x23] != NULL && test_dos_vectors[0x24] != NULL);
    assert(mtx_previous_lut == 3 && inquiry == 0);
    assert(mtx_mask_saved && mtx_previous_mask == original_mask);
    assert(error_reporting == 1 && mtx_previous_error_reporting == initial_error_reporting);
    if (stale_profile)
    {
        assert(error_read == error_count && stale_words == 0UL && error_gap == 0U);
        assert(strstr(stderr_text, "discarded 5 stale reply words, error bits 0x0A") != NULL);
        stale_error_gap = 0;
        puts("PASS Matrox stale startup: latched errors, a delayed second error and old reply words are discarded with a warning");
    }
    else assert(stderr_text[0] == '\0');
    puts("PASS Matrox inquiry: short malformed replies drained, oversized counts rejected, palette FIFO remains synchronized");
    {
        /* LSINQ 88 is optional: a shell without it leaves the mask alone. */
        unsigned short mask = 1234;
        int present = TRUE;
        mask_words = 0;
        assert(mtx_inquire(88, &mask, "plane-mask inquiry", "invalid", FALSE, &present));
        assert(!present && mask == 1234 && inquiry == 0);
        mask_words = 1;
        assert(mtx_inquire(88, &mask, "plane-mask inquiry", "invalid", FALSE, &present));
        assert(present && mask == original_mask);
        puts("PASS Matrox plane mask: LSINQ 88 saves the original mask; an empty reply means the shell keeps it");
    }
    check_stale_limits();
    if (startup_failure == 1) fail_v3_projection = 1U;
    if (startup_failure == 2) fail_world_window = 1U;
    assert(drv_init_window(0, 0, 0, 0));
    assert(drv_win_width == 640 && drv_win_height == 480);
    assert(viewport_calls > 0 && world_window_calls > 0);
    if (startup_failure != 2) assert(projection_calls > 0);
    assert(palette_uploads == 1 && current_lut == 0 && current_mask == 255);
    for (i = 0; i < 256; ++i) {
        assert(supplied_palette[i][0] == earth_cmap[i][0]);
        assert(supplied_palette[i][1] == earth_cmap[i][1]);
        assert(supplied_palette[i][2] == earth_cmap[i][2]);
    }
    check_board_errors();
    globe_init();
    assert(globe_init_verts(GLOBE_DEFAULT_LONGITUDE, -1, GLOBE_RADIUS));
    if (startup_failure)
    {
        assert(drv_soft3d_active && !drv_soft3d_requested);
        render_and_check(512, test_earth_data(32));
        retry_native();
        puts("PASS Matrox startup fallback: optional native projection failure retains graphics, host drawing and complete S-key retry");
    }
    check_perspective_math();
    render_and_check(512, test_earth_data(32));
    check_direct_quad();
    check_noncardinal_poses(512, test_earth_data(32));
    check_integer_culling();
    assert(osd_calls > 0);
    {
        unsigned int syncs = sync_calls;
        unsigned long colors = color_commands;
        render_and_check(512, test_earth_data(32));
        printf("PASS Matrox packets: a key-1 frame uses %u checkpoints and %lu color commands "
               "for %lu fills; at most %u bytes in flight\n", sync_calls - syncs,
               color_commands - colors, filled_polygons, fifo_peak);
    }
    mtx_keyboard('2');
    assert(globe_longitude == 64 && globe_faces_num == 2048UL);
    render_and_check(2048, test_earth_data(64));
    check_noncardinal_poses(2048, test_earth_data(64));
    check_integer_culling();
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
    check_software_controls();
    check_software_clipping();
    check_transform_recovery();

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
    {
        unsigned int before_osd;
        globe_free();
        drv_osd_enabled = FALSE;
        before_osd = osd_calls;
        drv_notice("no globe; choose a resolution");
        drv_render();
        assert(!globe_ready() && osd_calls >= before_osd + 2U);
        drv_release_optional();
        mtx_keyboard('1');
        assert(globe_ready());
        assert(mtx_angle(FLT_MAX) >= 0 && mtx_angle(FLT_MAX) < 360);
        assert(mtx_angle((float)HUGE_VAL) == 0);
    }
    puts("PASS Matrox recovery: empty scene draws status with OSD disabled and resolution input restores the globe");
    if (break_profile)
    {
        /* Ctrl-C/Break runs the stored INT 23h routine; the next loop pass
         * must leave through exit() and the registered board cleanup. */
        test_dos_vectors[0x23]();
        assert(drv_dos_break_pending());
        break_exit = 1;
        drv_loop();
        assert(!"Ctrl-Break must leave the event loop");
        return 1;
    }
    mtx_keyboard(27);
    assert(!"Escape must terminate the application");
    return 1;
}
