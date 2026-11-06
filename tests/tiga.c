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
#include <unistd.h>
#include "testglob.h"
#include "drv.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"
#include "soft3d.h"
#include "tga_cpu.h"
#include "tga_cmd.h"

static void recorded_error(char *message);
static clock_t recorded_clock(void);
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
static PALET original_palette[256], hardware_palette[256];
static unsigned int allocations, palette_reads, palette_writes;
static unsigned int cd_opens, cd_closes, mode_queries, mode_sets;
static unsigned int primitive_loads, module_loads, module_calls, module_unloads;
static unsigned int module_flushes;
static int fail_flush;
static unsigned int syncs, page_flips, page_polls, pending_flip;
static short display_page, drawing_page, pending_display;
static unsigned int current_color;
static int hardware_open, graphics_active;
static int module_loaded, primitive_loaded;
static int selected_cpu = 286, selected_fpu;
static unsigned int fixed_transforms, optimized_transforms, floating_transforms;
static int bad_module, module_missing;
static unsigned short mouse_buttons;
static short mouse_dx, mouse_dy;
static unsigned int mouse_resets, mouse_polls;
static char osd[2048];
static char failure[160];
static jmp_buf failure_jump;
static int expected_failure;
static clock_t fake_clock;
static int advance_clock;
static void (*sdk_error_handler)();
static int temp_descriptor = -1;
static unsigned int temp_creates, temp_writes, temp_removes;
static char last_temp[1024], collision_path[1024];
static int collision_created, cleanup_failed;

#define FIXTURE_MODULE_BYTES 1549U

static int is_profile(const char *name) { return strcmp(profile, name) == 0; }

static int unavailable_payload(void)
{
    return is_profile("nooverlay") || is_profile("badcrc") ||
           is_profile("truncated") || is_profile("oversized") ||
           is_profile("badoffset") || is_profile("reserved") ||
           is_profile("badmagic") || is_profile("missingexe") ||
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
    if (expected_failure) longjmp(failure_jump, 1);
    fprintf(stderr, "Unexpected TIGA failure: %s\n", message);
    abort();
}

static clock_t recorded_clock(void)
{
    if (advance_clock) fake_clock += CLOCKS_PER_SEC;
    return advance_clock ? fake_clock : clock();
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

int tga_cpu_detect(void) { return selected_cpu; }
int tga_cpu_has_fpu(void) { return selected_fpu; }

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

int kbhit(void) { return 0; }
int getch(void) { assert(!"Unexpected blocking keyboard read"); return 0; }

long tiga_set(short state)
{
    if (state == CD_OPEN)
    {
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
    /* Changing configurations can reload the GM and discard modules. */
    primitive_loaded = module_loaded = 0;
    ++mode_sets;
    if (is_profile("modeerror") && (mode == 1 || mode == 2)) return 0;
    hardware.current_mode = (unsigned short)mode;
    hardware.mode = modes[mode];
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
void set_timeout(short seconds) { assert(seconds > 0); }
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

void set_fcolor(long color) { assert(color >= 0 && color < 256); current_color = (unsigned int)color; }
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
}
void clear_screen(long color) { clear_page(color); }

void page_flip(short display, short draw)
{
    assert(hardware_open && !pending_flip);
    assert(display >= 0 && draw >= 0);
    assert(display < hardware.mode.num_pages && draw < hardware.mode.num_pages);
    ++page_flips;
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
    ++module_calls;
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
    hardware.comm_buff_size = is_profile("smallbuffer") ? 256UL : 4096UL;
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
    modes[2].num_pages = is_profile("single") ? 1 : 2;
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
    }
    memcpy(hardware_palette, original_palette, sizeof(hardware_palette));
    module_missing = is_profile("nomodule");
    bad_module = is_profile("badmodule");
    if (is_profile("386")) selected_cpu = 386;
    if (is_profile("486") || is_profile("fpu")) selected_cpu = 486;
    if (is_profile("8086")) selected_cpu = 86;
    selected_fpu = is_profile("fpu");
}

static void render_frame(void)
{
    polygon_count = 0;
    osd[0] = '\0';
    drv_render();
    assert(!drv_frame_failed);
    assert(polygon_count > 0 && polygon_count == globe_quads_num_vis);
    assert(polygon_count < globe_faces_num);
    assert(strstr(osd, "texture") == NULL);
}

int main(int argc, char **argv)
{
    unsigned int i, before;
    float rotation_x, rotation_y;
    char *application_argv[2];
    int application_argc = 1;
    FILE *sentinel;
    char contents[8];
    assert(argc == 3);
    profile = argv[1];
    application_argv[0] = argv[2];
    application_argv[1] = NULL;
    initialize_hardware();
    drv_ext_init();
    globe_init();
    assert(drv_colormapped && !drv_texture_enabled && !drv_texture_avail);
    assert(strstr(drv_help, "1-2") != NULL && strstr(drv_help, "soft3d") != NULL);
    expected_failure = is_profile("nocd") || is_profile("videoerror") ||
                       is_profile("oldversion") || is_profile("nomode") ||
                       is_profile("modeerror") || is_profile("primitivesfail") ||
                       is_profile("pagehang") || is_profile("syncerror") ||
                       is_profile("moduleioerror") || is_profile("flushioerror");
    advance_clock = is_profile("pagehang");
    if (setjmp(failure_jump) != 0)
    {
        assert(expected_failure && failure[0]);
        if (is_profile("flushioerror"))
            assert(tga_module == 0x0200 && module_loaded && module_flushes == 1U);
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
    if (is_profile("movedcwd"))
    {
        assert(getenv("TGA_TEST_OTHER_CWD") != NULL);
        assert(chdir(getenv("TGA_TEST_OTHER_CWD")) == 0);
    }
    assert(drv_init_window(0, 0, 0, 0));
    assert(!expected_failure || is_profile("syncerror") ||
           is_profile("moduleioerror") || is_profile("flushioerror"));
    assert(drv_win_width == 640 && drv_win_height == 480);
    assert(mode_queries >= 3 && hardware.current_mode == (is_profile("single") ? 1U : 2U));
    assert(primitive_loads == 1 && palette_reads > 0 && palette_writes > 0);
    for (i = 0; i < 256; ++i)
    {
        assert(hardware_palette[i].r == earth_cmap[i][0]);
        assert(hardware_palette[i].g == earth_cmap[i][1]);
        assert(hardware_palette[i].b == earth_cmap[i][2]);
    }
    assert(tga_soft3d);
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    render_frame();
    assert(strstr(osd, "soft3d on") != NULL && module_calls == 0);
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
    check_temp_deleted();
    render_frame();
    if (unavailable_payload() || module_missing || bad_module ||
        is_profile("wrongabi") || is_profile("badscreen"))
    {
        assert(tga_soft3d);
        if (unavailable_payload()) assert(module_loads == 0 && module_calls == 0);
        else
        {
            assert(module_loads > 0);
            assert(module_missing ? module_calls == 0 : module_calls > 0);
        }
        assert(strstr(osd, "soft3d on") != NULL);
        assert(polygon_count == software_count);
        assert(memcmp(polygons, software_polygons, polygon_count * sizeof(polygons[0])) == 0);
    }
    else
    {
        assert(!tga_soft3d && module_calls > 0);
        assert(strstr(osd, "soft3d off") != NULL);
        if (!selected_fpu)
        {
            assert(polygon_count == software_count);
            assert(memcmp(polygons, software_polygons, polygon_count * sizeof(polygons[0])) == 0);
        }
        tga_keyboard('s');
        assert(tga_soft3d);
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
        render_frame();
        if (!selected_fpu)
        {
            assert(polygon_count == software_count);
            assert(memcmp(polygons, software_polygons, polygon_count * sizeof(polygons[0])) == 0);
        }
        tga_keyboard('s');
    }
    if (is_profile("default"))
    {
        assert(tga_soft3d && tga_module == 0x0200 && module_loaded);
        before = module_loads;
        fail_flush = 1;
        drv_release_optional();
        assert(tga_soft3d && tga_module == 0x0200 && module_loaded);
        assert(module_flushes == 1U && module_unloads == 0U);
        assert(strstr(drv_get_notice(), "release failed") != NULL);
        render_frame();
        assert(strstr(osd, "soft3d on") != NULL);
        tga_keyboard('s');
        assert(!tga_soft3d && module_loads == before);
        render_frame();
        drv_release_optional();
        assert(tga_soft3d && tga_module == -1 && !module_loaded);
        assert(module_flushes == 2U && module_unloads == 1U);
        assert(strstr(drv_get_notice(), "module released") != NULL);
        tga_keyboard('s');
        check_temp_deleted();
        assert(!tga_soft3d && module_loads == before + 1U && module_loaded);
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
    tga_cleanup();
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
    assert(memcmp(hardware_palette, original_palette, sizeof(hardware_palette)) == 0);
    globe_free();
    assert(allocations == 0);
    printf("PASS TIGA %s: queried 640x480 indexed mode, palette, bounded clockwise polygons, software/TI transforms, presentation, controls and cleanup\n", profile);
    return 0;
}
