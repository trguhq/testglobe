/* Execute the DOS TIGA driver against recorded API calls and the real
 * downloadable transform kernel. This does not emulate a TIGA board or
 * Microsoft's 16-bit calling convention. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "testglob.h"
#include "drv.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"
#include "soft3d.h"
#include "tga_cmd.h"

static void recorded_error(char *message);
static clock_t recorded_clock(void);
static time_t recorded_time(time_t *now);
static unsigned int recorded_quadstream_reuse(const Drv_quad_stream *stream,
    unsigned int vertex_count, const Vertex TG_HUGE *vertices,
    Drv_quad_context *context);
static int recorded_open(const char *path, int flags, ...);
static ssize_t recorded_write(int descriptor, const void *buffer, size_t count);
static int recorded_close(int descriptor);
static int recorded_remove(const char *path);
static int recorded_fixed_transform(long matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_fixed_point *out, unsigned int reuse);
static int recorded_386_transform(long matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_fixed_point *out, unsigned int reuse);
static int recorded_float_transform(float matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_float_point *out,
    unsigned int reuse, int precision);
#define error recorded_error
#define clock recorded_clock
#define time recorded_time
#define drv_quadstream_reuse recorded_quadstream_reuse
#define open recorded_open
#define write recorded_write
#define close recorded_close
#define remove recorded_remove
#define soft3d_fixed_transform_reuse recorded_fixed_transform
#define soft3d_386_transform_reuse recorded_386_transform
#define soft3d_float_transform_reuse recorded_float_transform
#include "../src/drv_tga.c"
#undef soft3d_fixed_transform_reuse
#undef soft3d_386_transform_reuse
#undef soft3d_float_transform_reuse
#undef drv_quadstream_reuse
#undef time
#undef clock
#undef open
#undef write
#undef close
#undef remove
#undef error

extern void tga_gsp_transform(unsigned long words, long *packet);
extern int globe_longitude, globe_latitude;
extern Vertex TG_HUGE *globe_verts;

typedef struct {
    unsigned char color;
    short count;
    short xy[20];
} RecordedPolygon;
static RecordedPolygon polygons[4096], software_polygons[4096];
static unsigned int polygon_count, software_count;
static unsigned int convex_polygons, general_polygons;
static const char *profile = "default";
static MODEINFO modes[5];
static CONFIG hardware;
static PALET original_palette[256], default_palette[256], hardware_palette[256];
static unsigned int allocations, palette_reads, palette_writes;
static unsigned int cd_opens, cd_closes, mode_queries, mode_sets;
static unsigned int primitive_loads, module_loads, module_calls, module_unloads;
static unsigned int module_flushes;
static int fail_flush;
static unsigned int syncs, page_flips, page_polls, pending_flip;
static short display_page, drawing_page, pending_display;
static unsigned int current_color;
/* Fill-color sets consumed by polygons; repeats are redundant commands. */
static unsigned int frame_fill_sets, fill_repeats;
static int color_set_since_clear, color_pending, color_pending_repeat;
/* Transform traffic expected from the driver's batches in TI mode. */
static unsigned long ti_vertices, ti_fresh, ti_expected_words, ti_full_words;
static unsigned long packet_words, packet_vertices;
static long last_packet_count;
static int hardware_open, graphics_active;
static int module_loaded, primitive_loaded;
static int selected_cpu = 286, selected_fpu;
static unsigned int fixed_transforms, optimized_transforms, floating_transforms;
static int bad_module, module_missing;
static unsigned short mouse_buttons;
static short mouse_dx, mouse_dy;
static unsigned int mouse_resets, mouse_polls;
static char osd[2048];
static int check_text_bounds, previous_text_bottom;
static char failure[160];
static jmp_buf failure_jump;
static int expected_failure;
static int failure_rendering;
static unsigned int failure_flips;
static clock_t fake_clock;
static int advance_clock;
static void (*sdk_error_handler)();
static int temp_descriptor = -1;
static unsigned int temp_creates, temp_writes, temp_removes;
static char last_temp[1024], collision_path[1024];
static int collision_created, cleanup_failed;
static unsigned int console_polls, break_after_polls;
static int loop_exit_expected;
static unsigned int loop_flips;
static float loop_rotation_y;

#define FIXTURE_MODULE_BYTES 1549U

static int is_profile(const char *name) { return strcmp(profile, name) == 0; }

/* Configuration and initialization errors that restore the display first. */
static int restoring_failure(void)
{
    return is_profile("nocd") || is_profile("videoerror") ||
           is_profile("oldversion") || is_profile("nomode") ||
           is_profile("modeerror") || is_profile("primitivesfail") ||
           is_profile("tinybuffer") || is_profile("badreturn");
}

/* syncerror needs a page without flips, which is where frames synchronize. */
static int single_page(void) { return is_profile("single") || is_profile("syncerror"); }

static int unavailable_payload(void)
{
    return is_profile("nooverlay") || is_profile("badcrc") ||
           is_profile("truncated") || is_profile("oversized") ||
           is_profile("badoffset") || is_profile("reserved") ||
           is_profile("badmagic") || is_profile("badimage") ||
           is_profile("missingexe") ||
           is_profile("diskfull") || is_profile("closefail") ||
           is_profile("notemp");
}

static int recorded_open(const char *path, int flags, ...)
{
    va_list arguments;
    int mode, descriptor;
    FILE *sentinel;
    assert(path != NULL && strlen(path) < sizeof(last_temp));
    assert((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL));
    va_start(arguments, flags);
    mode = va_arg(arguments, int);
    va_end(arguments);
    if (is_profile("collision") && !collision_created)
    {
        sentinel = fopen(path, "wb");
        assert(sentinel != NULL);
        assert(fwrite("preserve", 1, 8, sentinel) == 8);
        assert(fclose(sentinel) == 0);
        strcpy(collision_path, path);
        collision_created = 1;
    }
    descriptor = open(path, flags, mode);
    if (descriptor >= 0)
    {
        assert(temp_descriptor == -1);
        temp_descriptor = descriptor;
        strcpy(last_temp, path);
        ++temp_creates;
    }
    return descriptor;
}

static ssize_t recorded_write(int descriptor, const void *buffer, size_t count)
{
    assert(descriptor == temp_descriptor);
    ++temp_writes;
    if (is_profile("diskfull") && temp_writes > 1U)
    {
        errno = ENOSPC;
        return -1;
    }
    if ((is_profile("shortwrite") || is_profile("diskfull")) && count > 17U)
        count = 17U;
    return write(descriptor, buffer, count);
}

static int recorded_close(int descriptor)
{
    int result;
    assert(descriptor == temp_descriptor);
    result = close(descriptor);
    temp_descriptor = -1;
    if (is_profile("closefail"))
    {
        errno = EIO;
        return -1;
    }
    return result;
}

static int recorded_remove(const char *path)
{
    assert(path != NULL && *path != '\0');
    ++temp_removes;
    if (is_profile("removefail") && !cleanup_failed)
    {
        cleanup_failed = 1;
        errno = EACCES;
        return -1;
    }
    return remove(path);
}

static void check_temp_deleted(void)
{
    FILE *stream;
    if (*last_temp == '\0') return;
    stream = fopen(last_temp, "rb");
    if (is_profile("removefail") && temp_removes == 1U)
    {
        assert(stream != NULL);
        assert(fclose(stream) == 0);
    }
    else
    {
        assert(stream == NULL);
        assert(errno == ENOENT);
    }
    assert(temp_descriptor == -1);
}

static void recorded_error(char *message)
{
    assert(message != NULL && strlen(message) < sizeof(failure));
    strcpy(failure, message);
    /* The message must reach a restored screen: TIGA closed, the previous
     * video mode back, and any saved palette already rewritten. */
    if (restoring_failure())
    {
        assert(!tga_open && !hardware_open && !graphics_active);
        assert(cd_closes == (unsigned int)!is_profile("nocd"));
        assert(memcmp(hardware_palette, original_palette, sizeof(hardware_palette)) == 0);
    }
    failure_rendering = tga_rendering;
    failure_flips = page_flips;
    if (expected_failure) longjmp(failure_jump, 1);
    fprintf(stderr, "Unexpected TIGA failure: %s\n", message);
    abort();
}

static clock_t recorded_clock(void)
{
    if (advance_clock) fake_clock += CLOCKS_PER_SEC;
    return advance_clock ? fake_clock : clock();
}

/* The temporary-name seed must come from the date, not process time. */
static time_t recorded_time(time_t *now)
{
    time_t fixed = (time_t)0x5abcdefL;
    if (now != NULL) *now = fixed;
    return fixed;
}

void *_halloc(long count, size_t size)
{
    void *memory;
    assert(count > 0 && size > 0 && (unsigned long)count <= (size_t)-1 / size);
    memory = calloc((size_t)count, size);
    assert(memory != NULL);
    ++allocations;
    return memory;
}

void _hfree(void *memory)
{
    assert(memory != NULL && allocations > 0);
    --allocations;
    free(memory);
}

int drv_cpu_detect(void) { return selected_cpu; }
int drv_cpu_has_fpu(void) { return selected_fpu; }

static int recorded_fixed_transform(long matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_fixed_point *out, unsigned int reuse)
{
    ++fixed_transforms;
    return soft3d_fixed_transform_reuse(matrix, count, vertices, out, reuse);
}
static int recorded_386_transform(long matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_fixed_point *out, unsigned int reuse)
{
    ++optimized_transforms;
    return soft3d_386_transform_reuse(matrix, count, vertices, out, reuse);
}
static int recorded_float_transform(float matrix[3][3], unsigned int count,
    const Vertex TG_HUGE *vertices, Soft3d_float_point *out,
    unsigned int reuse, int precision)
{
    ++floating_transforms;
    return soft3d_float_transform_reuse(matrix, count, vertices, out, reuse, precision);
}

static void dummy_interrupt(void) {}
void (*_dos_getvect(unsigned int number))(void)
{
    assert(number == 0x33);
    return is_profile("nomouse") ? NULL : dummy_interrupt;
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
    if (function == 0)
    {
        ++mouse_resets;
        output->x.ax = 0xffff;
        output->x.bx = 2;
    }
    else if (function == 3)
    {
        ++mouse_polls;
        output->x.bx = mouse_buttons;
    }
    else if (function == 0x0b)
    {
        output->x.cx = (unsigned short)mouse_dx;
        output->x.dx = (unsigned short)mouse_dy;
        mouse_dx = mouse_dy = 0;
    }
    else assert(function == 2);
    return 0;
}

/* DOS raises INT 23h for Ctrl-C/Break during a console status call. */
int kbhit(void)
{
    ++console_polls;
    if (break_after_polls != 0U && console_polls == break_after_polls)
    {
        assert(test_dos_vectors[0x23] != NULL);
        ((void (*)(void))test_dos_vectors[0x23])();
    }
    return 0;
}
int getch(void) { assert(!"Unexpected blocking keyboard read"); return 0; }

long tiga_set(short state)
{
    if (state == CD_OPEN)
    {
        /* Break and critical-error routines precede TIGA owning the screen. */
        assert(test_dos_vectors[0x23] != NULL && test_dos_vectors[0x24] != NULL);
        ++cd_opens;
        if (is_profile("nocd")) return CD_NOT_INSTALLED;
        assert(!hardware_open);
        hardware_open = 1;
        return 0L;
    }
    assert(state == CD_CLOSE && hardware_open);
    ++cd_closes;
    hardware_open = 0;
    return 0L;
}

unsigned short get_videomode(void) { assert(hardware_open); return VGA; }

short set_videomode(unsigned short mode, unsigned short flags)
{
    assert(hardware_open);
    if (mode == (unsigned short)TIGA)
    {
        assert(flags & INIT);
        if (is_profile("videoerror")) return 0;
        graphics_active = 1;
    }
    else
    {
        assert(mode == VGA || mode == PREVIOUS);
        graphics_active = 0;
    }
    return 1;
}

void get_config(CONFIG *config)
{
    assert(hardware_open && graphics_active && config != NULL);
    hardware.mode = modes[hardware.current_mode];
    *config = hardware;
    if (is_profile("badreturn") && hardware.current_mode != 3U)
        config->mode.disp_psize = 16;
}

unsigned short get_modeinfo(short index, void *mode)
{
    assert(hardware_open && graphics_active && mode != NULL);
    assert(index >= 0 && index < 5);
    ++mode_queries;
    *(MODEINFO *)mode = modes[index];
    return 1;
}

short set_config(short mode, short flags)
{
    assert(hardware_open && mode >= 0 && mode < 5);
    (void)flags;
    /* Guide p. 4-100: a reload flushes extensions, so select modes before
     * downloading any; only exit cleanup may follow them. */
    assert(tga_cleaning || (!primitive_loaded && !module_loaded));
    /* Changing configurations can reload the GM and discard modules. */
    primitive_loaded = module_loaded = 0;
    ++mode_sets;
    if (is_profile("modeerror") && (mode == 1 || mode == 2)) return 0;
    hardware.current_mode = (unsigned short)mode;
    hardware.mode = modes[mode];
    /* The selected mode starts with its default palette (p. 4-101). */
    memcpy(hardware_palette, default_palette, sizeof(hardware_palette));
    return 3;
}

short install_primitives(void)
{
    assert(hardware_open && graphics_active);
    assert(hardware.mode.disp_hres == 640 && hardware.mode.disp_psize == 8);
    ++primitive_loads;
    if (is_profile("primitivesfail")) return -6;
    primitive_loaded = 1;
    return 0x3c00;
}

short install_rlm(char *name)
{
    FILE *module;
    unsigned int i;
    size_t length;
    short (*callback)(unsigned short, short);
    assert(primitive_loaded && name != NULL && *name != '\0');
    assert(*last_temp != '\0');
    length = strlen(last_temp);
    assert(strlen(name) == length + 3U &&
           memcmp(name, last_temp, length) == 0 &&
           strcmp(name + length, " %f") == 0);
    /* The loader flag must never become part of the extracted filename. */
    assert(strcmp(tga_temp_path, last_temp) == 0);
    module = fopen(last_temp, "rb");
    assert(module != NULL);
    for (i = 0; i < FIXTURE_MODULE_BYTES; ++i)
        assert(fgetc(module) == (int)((i * 73U + 19U) & 255U));
    assert(fgetc(module) == EOF && !ferror(module));
    assert(fclose(module) == 0);
    ++module_loads;
    if (is_profile("moduleioerror"))
    {
        assert(sdk_error_handler != NULL);
        callback = (short (*)(unsigned short, short))sdk_error_handler;
        assert(callback(0x1234, 2) == 0);
        return -6;
    }
    if (module_missing) return -6;
    module_loaded = 1;
    return 0x0200;
}

long del_module(short module)
{
    (void)module;
    assert(!"Applications must use flush_module, not internal del_module");
    return -1;
}

short flush_module(short module)
{
    short (*callback)(unsigned short, short);
    assert(hardware_open && module == 0x0200 && module_loaded);
    ++module_flushes;
    if (is_profile("flushioerror"))
    {
        assert(sdk_error_handler != NULL);
        callback = (short (*)(unsigned short, short))sdk_error_handler;
        assert(callback(0x1234, 2) == 0);
        return 0;
    }
    if (fail_flush > 0)
    {
        --fail_flush;
        return 0; /* A locked module retains its code and identifier. */
    }
    module_loaded = 0;
    ++module_unloads;
    return 1;
}

short uninstall_module(short module) { return (short)del_module(module); }
void install_usererror(void (*handler)()) { sdk_error_handler = handler; }
/* Guide p. 4-123: milliseconds. Slow boards need at least a second. */
void set_timeout(short milliseconds) { assert(milliseconds >= 1000); }
void synchronize(void)
{
    assert(hardware_open);
    ++syncs;
    if (is_profile("syncerror"))
    {
        short (*callback)(unsigned short, short);
        assert(sdk_error_handler != NULL);
        callback = (short (*)(unsigned short, short))sdk_error_handler;
        assert(callback(0x1234, -2) == 0);
    }
}
short tiga_busy(void) { assert(hardware_open); return 0; }

void get_palet(short count, PALET *entries)
{
    assert(count > 0 && count <= 256 && entries != NULL);
    memcpy(entries, hardware_palette, (size_t)count * sizeof(PALET));
    ++palette_reads;
}

short get_palet_entry(long index, unsigned char *r, unsigned char *g,
                       unsigned char *b, unsigned char *intensity)
{
    assert(index >= 0 && index < 256 && r != NULL && g != NULL && b != NULL && intensity != NULL);
    if (is_profile("palettefail") && index == 200) return 0;
    *r = hardware_palette[index].r;
    *g = hardware_palette[index].g;
    *b = hardware_palette[index].b;
    *intensity = hardware_palette[index].i;
    ++palette_reads;
    return 1;
}

void set_palet(long count, long first, PALET *entries)
{
    assert(count > 0 && first >= 0 && count <= 256 - first && entries != NULL);
    /* Palette wrappers may internally segment transfers; the fixture checks
     * table bounds, while packet-level ABI checks use the native SDK. */
    memcpy(hardware_palette + first, entries, (size_t)count * sizeof(PALET));
    ++palette_writes;
}

void set_fcolor(long color)
{
    assert(color >= 0 && color < 256);
    color_pending_repeat = color_set_since_clear && (unsigned int)color == current_color;
    color_pending = 1;
    current_color = (unsigned int)color;
    color_set_since_clear = 1;
}
void set_bcolor(long color) { assert(color >= 0 && color < 256); }
void set_pmask(unsigned long mask) { assert(mask == 255UL); }
void set_ppop(short operation) { assert(operation == 0); }
void set_clip_rect(short width, short height, short x, short y)
{ assert(width == 640 && height == 480 && x == 0 && y == 0); }
void set_windowing(short mode) { assert(mode >= 0); }
void set_transp(short transparency) { assert(transparency == 0); }
void transp_off(void) {}
void set_curs_state(short state) { assert(state == 0); }

static void assert_drawable_page(void)
{
    assert(hardware_open && graphics_active && primitive_loaded);
    assert(!pending_flip);
    if (hardware.mode.num_pages > 1) assert(drawing_page != display_page);
}

void clear_page(long color)
{
    assert_drawable_page();
    assert(color >= 0 && color < 256);
    polygon_count = 0;
    frame_fill_sets = 0;
    color_set_since_clear = color_pending = color_pending_repeat = 0;
}
void clear_screen(long color) { clear_page(color); }

void page_flip(short display, short draw)
{
    assert(hardware_open && !pending_flip);
    assert(display >= 0 && draw >= 0);
    assert(display < hardware.mode.num_pages && draw < hardware.mode.num_pages);
    ++page_flips;
    if (is_profile("flipioerror") && page_flips == 2U)
    {
        short (*callback)(unsigned short, short);
        assert(sdk_error_handler != NULL);
        callback = (short (*)(unsigned short, short))sdk_error_handler;
        assert(callback(0x1234, -2) == 0);
    }
    pending_display = display;
    drawing_page = draw;
    pending_flip = 2;
}

short page_busy(void)
{
    assert(hardware_open);
    ++page_polls;
    if (pending_flip > 0)
    {
        if (!is_profile("pagehang")) --pending_flip;
        if (!pending_flip) display_page = pending_display;
    }
    return pending_flip != 0;
}

short text_out(short x, short y, char *text)
{
    size_t used = strlen(osd), length;
    assert(x >= 0 && x < 640 && y >= 0 && y < 480 && text != NULL);
    length = strlen(text);
    color_pending = color_pending_repeat = 0;
    if (check_text_bounds)
    {
        assert(y >= previous_text_bottom);
        assert(y + (int)tga_font_height <= 472);
        assert(length * tga_font_width <= 624U);
        previous_text_bottom = y + (int)tga_font_height;
    }
    assert(length + used + 2U < sizeof(osd));
    memcpy(osd + used, text, length);
    osd[used + length] = '\n'; osd[used + length + 1] = '\0';
    return (short)length;
}
void init_text(void) {}
short get_fontinfo(short id, FONTINFO *font)
{
    assert(id == 0 && font != NULL);
    font->maxwide = 8;
    font->charhigh = 16;
    return 1;
}
short select_font(short id) { assert(id == 0); return 1; }

static void record_polygon(short count, void *points, int convex)
{
    short *xy = (short *)points;
    long twice_area = 0;
    unsigned int i, next;
    RecordedPolygon *polygon;
    assert_drawable_page();
    assert(count >= 3 && count <= 10 && points != NULL);
    assert(10UL + (unsigned long)count * 4UL <= hardware.comm_buff_size);
    assert(polygon_count < sizeof(polygons) / sizeof(polygons[0]));
    /* Each frame sets its first fill color; later sets change the color. */
    assert(color_set_since_clear);
    if (color_pending)
    {
        ++frame_fill_sets;
        if (color_pending_repeat) ++fill_repeats;
        color_pending = color_pending_repeat = 0;
    }
    polygon = polygons + polygon_count++;
    memset(polygon, 0, sizeof(*polygon));
    polygon->color = (unsigned char)current_color;
    polygon->count = count;
    for (i = 0; i < (unsigned int)count; ++i)
    {
        assert(xy[i*2] >= 0 && xy[i*2] < 640);
        assert(xy[i*2+1] >= 0 && xy[i*2+1] < 480);
        next = (i + 1U) % (unsigned int)count;
        twice_area += (long)xy[i*2] * xy[next*2+1] - (long)xy[next*2] * xy[i*2+1];
    }
    /* TIGA fill_convex rejects counterclockwise screen-space polygons. */
    assert(twice_area > 0);
    if (convex)
    {
        unsigned int after;
        long turn;
        for (i = 0; i < (unsigned int)count; ++i)
        {
            next = (i + 1U) % (unsigned int)count;
            after = (i + 2U) % (unsigned int)count;
            turn = ((long)xy[next*2] - xy[i*2]) * ((long)xy[after*2+1] - xy[next*2+1]) -
                   ((long)xy[next*2+1] - xy[i*2+1]) * ((long)xy[after*2] - xy[next*2]);
            assert(turn >= 0);
        }
        ++convex_polygons;
    }
    else ++general_polygons;
    memcpy(polygon->xy, xy, (size_t)count * 2U * sizeof(short));
}
void fill_convex(short count, void *points) { record_polygon(count, points, 1); }
void fill_polygon(short count, void *points) { record_polygon(count, points, 0); }

long cp_alt(short command, short packets, ...)
{
    va_list args;
    long *packet;
    unsigned long words;
    int tag, bytes;
    assert(module_loaded && command == USER_CP(0x0200) && packets == 2);
    va_start(args, packets);
    tag = va_arg(args, int); bytes = va_arg(args, int);
    assert(tag == 0 && bytes == 4);
    words = (unsigned long)va_arg(args, long);
    tag = va_arg(args, int); bytes = va_arg(args, int);
    assert(tag == 129 && bytes == (int)(words * 4UL));
    packet = va_arg(args, long *);
    va_end(args);
    assert(words * 4UL + 32UL <= hardware.comm_buff_size);
    /* Only the status is reset between packets; the module must set it. */
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_PENDING);
    ++module_calls;
    packet_words += words;
    packet_vertices += (unsigned long)packet[TGA_GSP_COUNT];
    last_packet_count = packet[TGA_GSP_COUNT];
    tga_gsp_transform(words, packet);
    if (bad_module) packet[TGA_GSP_STATUS] = TGA_GSP_BAD_PACKET;
    if (is_profile("wrongabi")) packet[TGA_GSP_MAGIC] ^= 1L;
    if (is_profile("badscreen") && packet[TGA_GSP_COUNT] != 0)
        packet[TGA_GSP_HEADER + TGA_GSP_SCREEN_X] = 32768L;
    return 0;
}

static void initialize_hardware(void)
{
    unsigned int i;
    memset(&hardware, 0, sizeof(hardware));
    hardware.version_number = is_profile("oldversion") ? 0x0101 : 0x0200;
    hardware.comm_buff_size = is_profile("smallbuffer") ? 256UL :
                              (is_profile("tinybuffer") ? 64UL : 4096UL);
    hardware.num_modes = 5;
    hardware.current_mode = 3;
    for (i = 0; i < 5; ++i)
    {
        modes[i].disp_hres = 640; modes[i].disp_vres = 480;
        modes[i].disp_psize = 8; modes[i].pixel_mask = 255UL;
        modes[i].palet_size = 256UL; modes[i].palet_gun_depth = 6;
        modes[i].color_class = 2;
        modes[i].num_pages = 1;
        modes[i].x_aspect = modes[i].y_aspect = 1;
    }
    modes[0].disp_psize = 16;
    modes[0].color_class = 5;
    modes[2].num_pages = single_page() ? 1 : 2;
    modes[3].disp_hres = 1024; modes[3].disp_vres = 768;
    modes[4].palet_size = 64; modes[4].pixel_mask = 63;
    if (is_profile("nomode"))
        modes[1].disp_hres = modes[2].disp_hres = 800;
    hardware.mode = modes[3];
    for (i = 0; i < 256; ++i)
    {
        original_palette[i].r = (unsigned char)(i + 17U);
        original_palette[i].g = (unsigned char)(i + 41U);
        original_palette[i].b = (unsigned char)(i + 91U);
        original_palette[i].i = (unsigned char)i;
        default_palette[i].r = (unsigned char)(i * 3U);
        default_palette[i].g = (unsigned char)(i * 5U);
        default_palette[i].b = (unsigned char)(i * 7U + 1U);
        default_palette[i].i = 0;
    }
    memcpy(hardware_palette, original_palette, sizeof(hardware_palette));
    module_missing = is_profile("nomodule");
    bad_module = is_profile("badmodule");
    if (is_profile("386") || is_profile("387")) selected_cpu = 386;
    if (is_profile("486") || is_profile("fpu")) selected_cpu = 486;
    if (is_profile("8086") || is_profile("8087")) selected_cpu = 86;
    selected_fpu = is_profile("fpu") || is_profile("8087") ||
                   is_profile("287") || is_profile("387");
}

/* Words for one batch's vertices, split as the driver splits packets. */
static unsigned long expected_packet_words(unsigned int vertices)
{
    unsigned long words = 0UL;
    unsigned int n;
    while (vertices != 0U)
    {
        n = vertices < tga_packet_vertices ? vertices : tga_packet_vertices;
        words += TGA_GSP_WORDS(n);
        vertices -= n;
    }
    return words;
}

static unsigned int recorded_quadstream_reuse(const Drv_quad_stream *stream,
    unsigned int vertex_count, const Vertex TG_HUGE *vertices,
    Drv_quad_context *context)
{
    unsigned int reuse = drv_quadstream_reuse(stream, vertex_count, vertices, context);
    if (!drv_soft3d_active && tga_module >= 0 && tga_packet_vertices != 0U)
    {
        ti_vertices += vertex_count;
        ti_fresh += vertex_count - reuse;
        ti_expected_words += expected_packet_words(vertex_count - reuse);
        ti_full_words += expected_packet_words(vertex_count);
    }
    return reuse;
}

static void reset_traffic(void)
{
    ti_vertices = ti_fresh = ti_expected_words = ti_full_words = 0UL;
    packet_words = packet_vertices = 0UL;
}

/* A TI frame sends each reused row once, not again as the next band's top. */
static void check_traffic(void)
{
    assert(packet_vertices == ti_fresh && packet_words == ti_expected_words);
    assert(ti_fresh < ti_vertices && packet_words < ti_full_words);
}

static void render_frame(void)
{
    unsigned int flips = page_flips, synced = syncs;
    polygon_count = 0;
    osd[0] = '\0';
    drv_render();
    assert(!drv_frame_failed);
    assert(polygon_count > 0 && polygon_count == globe_quads_num_vis);
    assert(polygon_count < globe_faces_num);
    assert(strstr(osd, "texture") == NULL);
    /* Commands run in queue order, so a flip needs no synchronize first.
     * A single page synchronizes instead, so FPS counts finished drawing. */
    if (hardware.mode.num_pages > 1) assert(page_flips == flips + 1U && syncs == synced);
    else assert(page_flips == flips && syncs == synced + 1U);
    /* Neighboring facets share colors; unchanged colors are not resent. */
    assert(fill_repeats == 0U && frame_fill_sets > 0U && frame_fill_sets < polygon_count);
}

/* tga_coordinate_valid stands in for the module on reused rows, so both
 * must accept exactly the same IEEE bit patterns. */
static void check_coordinate_filter(void)
{
    static const float edges[] = {
        0.0f, 0.0625f, 0.0624f, 1.0f, 999.9375f, 1000.0f, 1000.0625f, 1024.0f,
        4096.0f, 1.0e20f
    };
    static long packet[TGA_GSP_WORDS(1)];
    unsigned long bits, seed = 12345UL;
    unsigned char bytes[4];
    unsigned int i, accepted = 0U, rejected = 0U;
    float value;
    for (i = 0; i < 256U * 24U + 2U * 10U + 20000U; ++i)
    {
        if (i < 256U * 24U)
            /* Every exponent, both signs, mantissa edges and middles. */
            bits = ((unsigned long)(i / 24U) << 23) |
                   ((i % 2U) != 0U ? 0x80000000UL : 0UL) |
                   ((i % 24U) / 2U == 0U ? 0UL : 0x7fffffUL >> ((i % 24U) / 2U - 1U));
        else if (i < 256U * 24U + 20U)
        {
            value = edges[(i - 256U * 24U) / 2U];
            if ((i & 1U) != 0U) value = -value;
            memcpy(bytes, &value, 4U);
            bits = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
                   ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
        }
        else
        {
            seed = (seed * 1103515245UL + 12345UL) & 0xffffffffUL;
            bits = seed;
        }
        bytes[0] = (unsigned char)(bits & 255UL);
        bytes[1] = (unsigned char)((bits >> 8) & 255UL);
        bytes[2] = (unsigned char)((bits >> 16) & 255UL);
        bytes[3] = (unsigned char)((bits >> 24) & 255UL);
        memcpy(&value, bytes, 4U);
        memset(packet, 0, sizeof(packet));
        packet[TGA_GSP_MAGIC] = TGA_GSP_ABI;
        packet[TGA_GSP_COUNT] = 1;
        packet[TGA_GSP_WIDTH] = 640;
        packet[TGA_GSP_HEIGHT] = 480;
        packet[TGA_GSP_FOCAL] = GLOBE_FOCAL_Q14;
        packet[TGA_GSP_VIEW] = GLOBE_VIEW_DISTANCE;
        packet[TGA_GSP_NEAR] = GLOBE_NEAR_DISTANCE;
        packet[TGA_GSP_FAR] = GLOBE_FAR_DISTANCE;
        packet[TGA_GSP_MATRIX] = packet[TGA_GSP_MATRIX + 4U] =
            packet[TGA_GSP_MATRIX + 8U] = 16384L;
        packet[TGA_GSP_HEADER + TGA_GSP_X] = (long)bits;
        tga_gsp_transform(TGA_GSP_WORDS(1), packet);
        assert(packet[TGA_GSP_STATUS] == TGA_GSP_OK ||
               packet[TGA_GSP_STATUS] == TGA_GSP_BAD_RANGE);
        assert(tga_coordinate_valid(&value) == (packet[TGA_GSP_STATUS] == TGA_GSP_OK));
        if (packet[TGA_GSP_STATUS] == TGA_GSP_OK) ++accepted;
        else ++rejected;
    }
    assert(accepted > 1000U && rejected > 1000U);
}

static int reference_convex(const short *xy, unsigned int count)
{
    unsigned int i, next, after;
    long turn;
    for (i = 0; i < count; ++i)
    {
        next = (i + 1U) % count;
        after = (i + 2U) % count;
        turn = ((long)xy[next*2] - xy[i*2]) * ((long)xy[after*2+1] - xy[next*2+1]) -
               ((long)xy[next*2+1] - xy[i*2+1]) * ((long)xy[after*2] - xy[next*2]);
        if (turn < 0) return 0;
    }
    return 1;
}

/* The incremental early-exit test must classify every polygon the driver
 * can submit (positive area, 3 to 10 corners) as the full test does. */
static void check_convexity(void)
{
    short xy[20];
    unsigned long seed = 99UL;
    unsigned int trial, count, i, next, convex = 0U, concave = 0U;
    long twice_area;
    for (trial = 0; trial < 40000U; ++trial)
    {
        count = 3U + trial % 8U;
        twice_area = 0;
        for (i = 0; i < 2U * count; ++i)
        {
            seed = (seed * 1103515245UL + 12345UL) & 0xffffffffUL;
            xy[i] = (short)((seed >> 16) % (trial < 20000U ? 9UL : 640UL));
        }
        for (i = 0; i < count; ++i)
        {
            next = (i + 1U) % count;
            twice_area += (long)xy[i*2] * xy[next*2+1] - (long)xy[next*2] * xy[i*2+1];
        }
        if (twice_area <= 0) continue;
        assert(tga_convex(xy, count) == reference_convex(xy, count));
        if (reference_convex(xy, count)) ++convex;
        else ++concave;
    }
    assert(convex > 1000U && concave > 1000U);
}

/* Two crafted bands: the second reuses the first band's lower row. */
static unsigned int draw_bands(Vertex *first, Vertex *second, int ti,
                               RecordedPolygon *out)
{
    static const unsigned int quads[12] = { 0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6 };
    static const unsigned char colors[3] = { 5, 5, 9 };
    polygon_count = 0;
    drv_frame_failed = FALSE;
    drv_notice(NULL);
    drv_soft3d_active = !ti;
    drv_quadstream_reset(&tga_stream);
    soft3d_project_cache_reset(&tga_cache);
    assert(soft3d_fixed_prepare(tga_matrix.fixed, &drv_rot_x, &drv_rot_y));
    drv_submit_quadbatch(8U, first, 3U, quads, colors, FALSE);
    drv_submit_quadbatch(8U, second, 3U, quads, colors, TRUE);
    if (out != NULL) memcpy(out, polygons, polygon_count * sizeof(polygons[0]));
    return polygon_count;
}

static void check_reused_rows(void)
{
    static Vertex first[8], second[8];
    RecordedPolygon ti[8], host[8];
    unsigned int row, column, count, j, calls;
    unsigned long width = (unsigned long)globe_longitude + 1UL;
    /* Find visible facets near the equator in the current pose. */
    for (column = 0, row = (unsigned int)globe_latitude / 2U - 1U;
         column + 3U <= (unsigned int)globe_longitude; ++column)
    {
        for (j = 0; j < 4U; ++j)
        {
            first[2U * j] = globe_verts[row * width + column + j];
            first[2U * j + 1U] = second[2U * j] = globe_verts[(row + 1U) * width + column + j];
            second[2U * j + 1U] = globe_verts[(row + 2U) * width + column + j];
        }
        count = draw_bands(first, second, 0, NULL);
        assert(!drv_frame_failed);
        if (count == 6U) break;
    }
    assert(column + 3U <= (unsigned int)globe_longitude);
    count = draw_bands(first, second, 0, host);
    calls = module_calls;
    reset_traffic();
    assert(draw_bands(first, second, 1, ti) == count && !drv_frame_failed);
    assert(memcmp(ti, host, count * sizeof(ti[0])) == 0);
    /* Eight vertices, then only the second band's four new ones. */
    assert(module_calls == calls + 2U && packet_vertices == 12U && last_packet_count == 4L);
    check_traffic();
    /* A malformed reused corner is never sent to the module. The host
     * rejects it as the software transform does, and TI falls back. */
    second[2].x = 4096.0f;
    calls = module_calls;
    (void)draw_bands(first, second, 1, NULL);
    assert(drv_frame_failed && drv_soft3d_active && module_calls == calls + 1U);
    assert(strstr(drv_get_notice(), "TI transform failed") != NULL);
    (void)draw_bands(first, second, 0, NULL);
    assert(drv_frame_failed && strstr(drv_get_notice(), "out of range") != NULL);
    drv_frame_failed = FALSE;
    drv_notice(NULL);
    drv_soft3d_active = TRUE;
    tga_keyboard('S');
    assert(!drv_soft3d_active && tga_module == 0x0200);
    puts("PASS TIGA reused rows: only new rows reach the module, reused corners are validated on the host, and polygons match the host fixed-point path");
}

/* State every completed profile must leave once cleanup has run. */
static void finish_checks(void)
{
    FILE *sentinel;
    char contents[8];
    check_temp_deleted();
    assert(temp_descriptor == -1);
    if (collision_created)
    {
        sentinel = fopen(collision_path, "rb");
        assert(sentinel != NULL);
        assert(fread(contents, 1, sizeof(contents), sentinel) == sizeof(contents));
        assert(memcmp(contents, "preserve", sizeof(contents)) == 0);
        assert(fclose(sentinel) == 0);
        assert(remove(collision_path) == 0);
    }
    assert(cd_opens == 1 && cd_closes == 1 && !hardware_open && !graphics_active);
    /* Without a complete saved copy, exit keeps the mode's default palette. */
    assert(memcmp(hardware_palette, is_profile("palettefail") ? default_palette :
                  original_palette, sizeof(hardware_palette)) == 0);
    assert(fill_repeats == 0U);
    globe_free();
    assert(allocations == 0);
    printf("PASS TIGA %s: queried 640x480 indexed mode, palette, bounded clockwise polygons, software/TI transforms, presentation, controls and cleanup\n", profile);
}

/* Registered before drv_init, so it runs after the driver's atexit cleanup. */
static void loop_exit(void)
{
    if (!loop_exit_expected) return;
    loop_exit_expected = 0;
    assert(drv_dos_break_pending() && console_polls == break_after_polls);
    assert(!tga_open && tga_module == -1);
    if (is_profile("persistent"))
    {
        /* A persistent failure neither redraws nor rotates in the loop. */
        assert(page_flips == loop_flips && drv_rot_y == loop_rotation_y);
        puts("PASS TIGA persistent failure: cleared page shows the notice, rotation and redraw stop until input");
    }
    else assert(page_flips == loop_flips + 3U && drv_rot_y != loop_rotation_y);
    puts("PASS TIGA Ctrl-C/Break: INT 23h ends the event loop through drv_close and the display cleanup");
    finish_checks();
}

/* Animate until the stored INT 23h routine runs inside the fourth kbhit. */
static void run_loop_until_break(void)
{
    mouse_buttons = 0;
    mouse_dx = mouse_dy = 0;
    drv_auto_enabled = TRUE;
    loop_flips = page_flips;
    loop_rotation_y = drv_rot_y;
    break_after_polls = console_polls + 4U;
    loop_exit_expected = 1;
    drv_loop();
    assert(!"drv_loop must exit through drv_close after Ctrl-C/Break");
}

int main(int argc, char **argv)
{
    unsigned int i, before;
    float rotation_x, rotation_y;
    char *application_argv[2];
    int application_argc = 1;
    assert(argc == 3);
    assert(atexit(loop_exit) == 0);
    profile = argv[1];
    application_argv[0] = argv[2];
    application_argv[1] = NULL;
    initialize_hardware();
    drv_ext_init();
    assert(drv_soft3d_requested && drv_soft3d_active);
    globe_init();
    assert(drv_colormapped && !drv_texture_enabled && !drv_texture_avail);
    assert(strstr(drv_help, "1-2") != NULL && strstr(drv_help, "soft3d") != NULL);
    expected_failure = restoring_failure() ||
                       is_profile("pagehang") || is_profile("syncerror") ||
                       is_profile("moduleioerror") || is_profile("flushioerror") ||
                       is_profile("flipioerror");
    advance_clock = is_profile("pagehang");
    if (setjmp(failure_jump) != 0)
    {
        assert(expected_failure && failure[0]);
        if (is_profile("flushioerror"))
            assert(tga_module == 0x0200 && module_loaded && module_flushes == 1U);
        /* A failed flip is reported by the frame that issued it. */
        if (is_profile("flipioerror")) assert(failure_rendering && failure_flips == 2U);
        tga_cleanup();
        if (is_profile("flushioerror"))
            assert(module_flushes == 1U && module_unloads == 0U);
        check_temp_deleted();
        assert(cd_closes == (unsigned int)!is_profile("nocd"));
        assert(!hardware_open);
        globe_free();
        assert(allocations == 0);
        printf("PASS TIGA initialization failure: %s restores available state\n", profile);
        return 0;
    }
    drv_init(&application_argc, application_argv);
    /* A board without a readable palette skips saving rather than failing. */
    assert(tga_palette_saved == !is_profile("palettefail"));
    assert(palette_reads == (is_profile("palettefail") ? 200U : 256U));
    if (is_profile("movedcwd"))
    {
        assert(getenv("TGA_TEST_OTHER_CWD") != NULL);
        assert(chdir(getenv("TGA_TEST_OTHER_CWD")) == 0);
    }
    assert(drv_init_window(0, 0, 0, 0));
    assert(!expected_failure || is_profile("syncerror") ||
           is_profile("moduleioerror") || is_profile("flushioerror") ||
           is_profile("flipioerror"));
    assert(drv_win_width == 640 && drv_win_height == 480);
    assert(mode_queries >= 3 && hardware.current_mode == (single_page() ? 1U : 2U));
    assert(primitive_loads == 1 && palette_reads > 0 && palette_writes > 0);
    for (i = 0; i < 256; ++i)
    {
        assert(hardware_palette[i].r == earth_cmap[i][0]);
        assert(hardware_palette[i].g == earth_cmap[i][1]);
        assert(hardware_palette[i].b == earth_cmap[i][2]);
    }
    assert(drv_soft3d_active);
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    render_frame();
    if (is_profile("default"))
    {
        check_coordinate_filter();
        check_convexity();
        puts("PASS TIGA host checks: reused-row coordinate filter matches the module bit for bit; incremental convexity matches the full test");
    }
    assert(strstr(osd, "soft3d on") != NULL && module_calls == 0);
    if (is_profile("default"))
    {
        unsigned int width, height;
        char *separator;
        check_text_bounds = TRUE;
        for (width = 8U; width <= 32U; width += 8U)
            for (height = 8U; height <= 64U; height += 8U)
            {
                tga_font_width = width;
                tga_font_height = height;
                previous_text_bottom = 0;
                osd[0] = '\0';
                drv_draw_osd();
                assert(strstr(osd, "S = soft3d") != NULL);
                for (separator = osd; *separator != '\0'; ++separator)
                    if (*separator == '\n') *separator = ' ';
                assert(strstr(osd, "soft3d on") != NULL);
            }
        check_text_bounds = FALSE;
        tga_font_width = 8U;
        tga_font_height = 16U;
        render_frame();
        /* Without the OSD, only frame start invalidates the color cache:
         * a stale entry naming the first fill must still be resent. */
        drv_osd_enabled = FALSE;
        render_frame();
        tga_fill_color = polygons[0].color;
        current_color = (polygons[0].color + 1U) & 255U;
        render_frame();
        drv_osd_enabled = TRUE;
        puts("PASS TIGA fill colors: each frame sets its first color, repeated colors are not resent and a stale cache entry is not trusted");
        puts("PASS TIGA OSD: measured system-font wrapping retains S help without overlapping status or exceeding screen bounds");
    }
    assert(temp_creates == 0 && module_loads == 0);
    if (selected_fpu)
        assert(floating_transforms > 0 && !fixed_transforms && !optimized_transforms);
    else if (selected_cpu >= 386)
        assert(optimized_transforms > 0 && !fixed_transforms && !floating_transforms);
    else
        assert(fixed_transforms > 0 && !optimized_transforms && !floating_transforms);
    software_count = polygon_count;
    memcpy(software_polygons, polygons, polygon_count * sizeof(polygons[0]));
    tga_keyboard('S');
    assert(!drv_soft3d_requested);
    check_temp_deleted();
    /* The temporary name is seeded from the date, not from clock(). */
    if (is_profile("default")) assert(strstr(last_temp, "TGABCDF0.RLM") != NULL);
    reset_traffic();
    render_frame();
    if (unavailable_payload() || module_missing || bad_module ||
        is_profile("wrongabi") || is_profile("badscreen"))
    {
        assert(drv_soft3d_active);
        if (unavailable_payload()) assert(module_loads == 0 && module_calls == 0);
        else
        {
            assert(module_loads > 0);
            assert(module_missing ? module_calls == 0 : module_calls > 0);
        }
        assert(strstr(osd, "soft3d on") != NULL);
        assert(polygon_count == software_count);
        assert(memcmp(polygons, software_polygons, polygon_count * sizeof(polygons[0])) == 0);
        /* A failed hardware request leaves software active; another S must
         * request hardware again, instead of toggling the stale request. */
        tga_keyboard('S');
        assert(!drv_soft3d_requested);
        check_temp_deleted();
        render_frame();
        assert(drv_soft3d_active);
        assert(polygon_count == software_count);
        assert(memcmp(polygons, software_polygons, polygon_count * sizeof(polygons[0])) == 0);
    }
    else
    {
        assert(!drv_soft3d_active && module_calls > 0);
        assert(strstr(osd, "soft3d off") != NULL);
        check_traffic();
        if (is_profile("default"))
            printf("PASS TIGA key 1 TI traffic: %lu of %lu vertices, %lu of %lu packet words\n",
                   packet_vertices, ti_vertices, packet_words, ti_full_words);
        if (!selected_fpu)
        {
            assert(polygon_count == software_count);
            assert(memcmp(polygons, software_polygons, polygon_count * sizeof(polygons[0])) == 0);
        }
        if (is_profile("default")) check_reused_rows();
        tga_keyboard('s');
        assert(drv_soft3d_requested && drv_soft3d_active);
        before = module_calls;
        render_frame();
        assert(module_calls == before);
    }
    tga_keyboard('2');
    assert(globe_longitude == 64 && globe_faces_num == 2048UL);
    drv_rot_x = 17.0f; drv_rot_y = 39.0f;
    render_frame();
    software_count = polygon_count;
    memcpy(software_polygons, polygons, polygon_count * sizeof(polygons[0]));
    if (!unavailable_payload() && !module_missing && !bad_module &&
        !is_profile("wrongabi") && !is_profile("badscreen"))
    {
        before = temp_creates;
        tga_keyboard('s');
        assert(temp_creates == before);
        check_temp_deleted();
        reset_traffic();
        render_frame();
        check_traffic();
        if (is_profile("default"))
            printf("PASS TIGA key 2 TI traffic: %lu of %lu vertices, %lu of %lu packet words\n",
                   packet_vertices, ti_vertices, packet_words, ti_full_words);
        if (!selected_fpu)
        {
            assert(polygon_count == software_count);
            assert(memcmp(polygons, software_polygons, polygon_count * sizeof(polygons[0])) == 0);
        }
        tga_keyboard('s');
    }
    if (is_profile("default"))
    {
        assert(drv_soft3d_active && tga_module == 0x0200 && module_loaded);
        before = module_loads;
        fail_flush = 1;
        drv_release_optional();
        assert(drv_soft3d_active && tga_module == 0x0200 && module_loaded);
        assert(module_flushes == 1U && module_unloads == 0U);
        assert(strstr(drv_get_notice(), "release failed") != NULL);
        render_frame();
        assert(strstr(osd, "soft3d on") != NULL);
        tga_keyboard('s');
        assert(!drv_soft3d_active && module_loads == before);
        render_frame();
        drv_release_optional();
        assert(drv_soft3d_active && tga_module == -1 && !module_loaded);
        assert(module_flushes == 2U && module_unloads == 1U);
        assert(strstr(drv_get_notice(), "module released") != NULL);
        tga_keyboard('s');
        check_temp_deleted();
        assert(!drv_soft3d_active && module_loads == before + 1U && module_loaded);
        render_frame();
        tga_keyboard('s');
        puts("PASS TIGA module release: documented flush result, retained failed-release ownership, host fallback, reuse and reload without export symbols");
    }
    if (is_profile("flushioerror"))
    {
        drv_release_optional();
        assert(!"Module release communication failure must stop native calls");
    }
    for (i = '3'; i <= '5'; ++i) tga_keyboard((unsigned char)i);
    tga_keyboard('t');
    assert(globe_longitude == 64 && !drv_texture_enabled);
    rotation_x = drv_rot_x; rotation_y = drv_rot_y;
    mouse_buttons = 0; mouse_dx = 12; mouse_dy = -6;
    tga_poll_mouse();
    assert(drv_rot_x == rotation_x && drv_rot_y == rotation_y);
    mouse_buttons = 1; mouse_dx = 12; mouse_dy = -6;
    tga_poll_mouse();
    if (!is_profile("nomouse")) assert(drv_rot_x != rotation_x || drv_rot_y != rotation_y);
    assert(page_polls > 0 || is_profile("single"));
    if (is_profile("persistent"))
    {
        /* Corrupt one mesh vertex: both the module and the host reject it. */
        tga_keyboard('s');
        assert(!drv_soft3d_active);
        render_frame();
        globe_verts[(unsigned long)(globe_latitude / 2) * (globe_longitude + 1UL) + 3UL].x = 4096.0f;
        drv_auto_enabled = TRUE;
        rotation_y = drv_rot_y;
        before = page_flips;
        i = module_calls;
        polygon_count = 0;
        osd[0] = '\0';
        drv_render();
        assert(drv_frame_failed && drv_soft3d_active && module_calls > i);
        assert(strstr(drv_get_notice(), "out of range") != NULL);
        for (i = 0; osd[i] != '\0'; ++i) if (osd[i] == '\n') osd[i] = ' ';
        assert(strstr(osd, "out of range") != NULL);
        assert(polygon_count == 0U && globe_quads_num_vis == 0UL);
        assert(page_flips == before + 1U && drv_rot_y == rotation_y);
        assert(!drv_animation_allowed());
    }
    if (is_profile("ctrlbreak") || is_profile("persistent")) run_loop_until_break();
    tga_cleanup();
    finish_checks();
    return 0;
}
