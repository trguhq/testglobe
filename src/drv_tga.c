/*
 * testglobe - TIGA 2.x, 640x480 indexed color, Microsoft C 7 /AH.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * All board access goes through the vendor's TIGA communication driver.
 * The embedded TI module performs fixed-point rotation and projection;
 * host clipping, input and scene ownership remain shared with other drivers.
 * DOS leaves the appended payload on disk. Extract it only when requested,
 * since the TIGA loader takes a filename rather than a host memory buffer.
 */
#include "testglob.h"
#ifdef DRV_TGA
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <conio.h>
#include <dos.h>
#ifdef _MSC_VER
#include <io.h>
#define tga_create(path) _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE)
#define tga_write _write
#define tga_close _close
#define TGA_PATH_SEPARATOR '\\'
#else
/* The host contract exercises real file I/O as well as recorded TIGA calls. */
#include <unistd.h>
#define tga_create(path) open(path, O_CREAT | O_EXCL | O_WRONLY, 0600)
#define tga_write write
#define tga_close close
#define TGA_PATH_SEPARATOR '/'
#endif
#include "drv.h"
#include "drv_ext.h"
#include "globe.h"
#include "glob_ext.h"
#include "tga_cpu.h"
#include "tga_cmd.h"

/* SDK order matters. Its far-data bindings must see small contiguous
 * packets, never a huge mesh pointer which may cross a segment boundary. */
#include <tiga.h>
#include <extend.h>
#include <typedefs.h>

#define TGA_WAIT_TICKS (10UL * (unsigned long)CLOCKS_PER_SEC)
#define TGA_TIMEOUT_MS 5000
#define TGA_PALET_CHUNK 16

static CONFIG tga_config;
static unsigned short tga_previous_video, tga_previous_mode;
static int tga_open, tga_video_set, tga_config_saved, tga_palette_saved;
static int tga_ready, tga_rendering, tga_io_failed, tga_cleaning;
static int tga_dirty = TRUE;
static int tga_soft3d = TRUE;
static int tga_cpu, tga_has_fpu, tga_mouse_present;
static short tga_module = -1;
static unsigned short tga_error_command;
static short tga_error_code;
static unsigned short tga_display_page, tga_draw_page;
static unsigned short tga_black, tga_white;
static unsigned int tga_font_width = 8, tga_font_height = 16;
static unsigned int tga_packet_vertices;
static PALET tga_saved_palette[256], tga_palette[256];
static char tga_program_path[260];
static char tga_temp_path[260];
static const char *tga_payload_error;
static unsigned long tga_temp_sequence;
static Drv_quad_stream tga_stream;
static Soft3d_project_cache tga_cache;
static union {
    long fixed[3][3];
    float floating[3][3];
} tga_matrix;
static union {
    Soft3d_fixed_point fixed[SOFT3D_MAX_VERTICES];
    Soft3d_float_point floating[SOFT3D_MAX_VERTICES];
} tga_points;
/* Wire fields are 32-bit longs on both Microsoft C and the TI compiler.
 * The packet is far, contiguous and far smaller than a 64 KiB segment. */
static long tga_packet[TGA_GSP_WORDS(TGA_GSP_MAX_VERTICES)];

/* Keep a resolved name rather than an open descriptor or a resident copy.
 * Microsoft C 7 supplies the DOS program name through argv[0]; _fullpath
 * also resolves drive-relative names before the working directory changes. */
static int tga_absolute_path(char *destination, const char *source)
{
    size_t length;
#ifndef _MSC_VER
    size_t used;
#endif
    if (source == NULL || *source == '\0') return FALSE;
    length = strlen(source);
    if (length >= sizeof(tga_program_path)) return FALSE;
#ifdef _MSC_VER
    return _fullpath(destination, source, sizeof(tga_program_path)) != NULL;
#else
    if (*source == '/')
    {
        strcpy(destination, source);
        return TRUE;
    }
    if (getcwd(destination, sizeof(tga_program_path)) == NULL) return FALSE;
    used = strlen(destination);
    if (used + 1U + length >= sizeof(tga_program_path)) return FALSE;
    if (used == 0U || destination[used - 1U] != '/') destination[used++] = '/';
    strcpy(destination + used, source);
    return TRUE;
#endif
}

static unsigned long tga_payload_u32(const unsigned char *bytes)
{
    return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
           ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
}

static unsigned long tga_payload_crc(unsigned long crc,
                                      const unsigned char *bytes, unsigned int count)
{
    unsigned int i, bit;
    for (i = 0; i < count; ++i)
    {
        crc ^= bytes[i];
        for (bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1UL) != 0UL ? 0xedb88320UL : 0UL);
    }
    return crc;
}

static int tga_remove_temp(void)
{
    if (tga_temp_path[0] == '\0') return TRUE;
    if (remove(tga_temp_path) != 0 && errno != ENOENT) return FALSE;
    tga_temp_path[0] = '\0';
    return TRUE;
}

static int tga_create_temp_at(const char *directory)
{
    char candidate[260], absolute[260], name[13];
    size_t length;
    unsigned int attempt;
    int descriptor;
    if (directory == NULL || *directory == '\0') return -1;
    length = strlen(directory);
    if (length >= sizeof(candidate) ||
        1U + sizeof(name) > sizeof(candidate) - length) return -1;
    strcpy(candidate, directory);
    if (length != 0U && candidate[length - 1U] != '/' &&
        candidate[length - 1U] != '\\' && candidate[length - 1U] != ':')
        candidate[length++] = TGA_PATH_SEPARATOR;
    for (attempt = 0; attempt < 256U; ++attempt)
    {
        tga_temp_sequence = (tga_temp_sequence + 1UL) & 0x00ffffffUL;
        sprintf(name, "TG%06lX.RLM", tga_temp_sequence);
        strcpy(candidate + length, name);
        if (!tga_absolute_path(absolute, candidate)) return -1;
        descriptor = tga_create(absolute);
        if (descriptor >= 0)
        {
            strcpy(tga_temp_path, absolute);
            return descriptor;
        }
        if (errno != EEXIST) return -1;
    }
    return -1;
}

static int tga_extract_module(void)
{
    FILE *program;
    unsigned char footer[TGA_RLM_TRAILER_SIZE], header[28], buffer[512];
    unsigned long offset, length, expected_crc, crc, remaining;
    unsigned long pages, last_page, image_size, header_size;
    unsigned int chunk, written;
    long size;
    int descriptor = -1, amount, valid = FALSE;
    tga_payload_error = "soft3d on; embedded TI module missing or damaged";
    if (!tga_remove_temp())
    {
        tga_payload_error = "soft3d on; temporary TI module cleanup failed";
        return FALSE;
    }
    if (tga_program_path[0] == '\0' || (program = fopen(tga_program_path, "rb")) == NULL)
    {
        tga_payload_error = "soft3d on; cannot reopen executable for TI module";
        return FALSE;
    }
    if (fseek(program, 0L, SEEK_END) != 0 || (size = ftell(program)) < 0L ||
        (unsigned long)size > 0x7fffffffUL ||
        size < 28L + (long)TGA_RLM_TRAILER_SIZE ||
        fseek(program, size - (long)TGA_RLM_TRAILER_SIZE, SEEK_SET) != 0 ||
        fread(footer, 1, sizeof(footer), program) != sizeof(footer)) goto done;
    if (memcmp(footer, TGA_RLM_MAGIC, 8U) != 0 || tga_payload_u32(footer + 20) != 0UL)
        goto done;
    offset = tga_payload_u32(footer + 8);
    length = tga_payload_u32(footer + 12);
    expected_crc = tga_payload_u32(footer + 16);
    if (offset < sizeof(header) || offset > (unsigned long)size - TGA_RLM_TRAILER_SIZE ||
        length == 0UL || length > TGA_RLM_MAX_SIZE ||
        length != (unsigned long)size - TGA_RLM_TRAILER_SIZE - offset) goto done;
    if (fseek(program, 0L, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), program) != sizeof(header) ||
        header[0] != 'M' || header[1] != 'Z') goto done;
    pages = (unsigned long)header[4] | ((unsigned long)header[5] << 8);
    last_page = (unsigned long)header[2] | ((unsigned long)header[3] << 8);
    header_size = ((unsigned long)header[8] | ((unsigned long)header[9] << 8)) * 16UL;
    if (pages == 0UL || last_page > 511UL) goto done;
    image_size = (pages - 1UL) * 512UL + (last_page != 0UL ? last_page : 512UL);
    if (header_size < sizeof(header) || header_size > image_size || image_size > offset)
        goto done;
    if (fseek(program, (long)offset, SEEK_SET) != 0) goto done;
    descriptor = tga_create_temp_at(getenv("TEMP"));
    if (descriptor < 0) descriptor = tga_create_temp_at(getenv("TMP"));
    if (descriptor < 0) descriptor = tga_create_temp_at(".");
    if (descriptor < 0)
    {
        tga_payload_error = "soft3d on; no writable directory for temporary TI module";
        goto done;
    }
    crc = 0xffffffffUL;
    remaining = length;
    while (remaining != 0UL)
    {
        chunk = remaining > sizeof(buffer) ? (unsigned int)sizeof(buffer) : (unsigned int)remaining;
        if (fread(buffer, 1, chunk, program) != chunk) goto done;
        crc = tga_payload_crc(crc, buffer, chunk);
        written = 0;
        while (written < chunk)
        {
            amount = (int)tga_write(descriptor, buffer + written, chunk - written);
            if (amount < 0 && errno == EINTR) continue;
            if (amount <= 0 || (unsigned int)amount > chunk - written)
            {
                tga_payload_error = "soft3d on; cannot write temporary TI module";
                goto done;
            }
            written += (unsigned int)amount;
        }
        remaining -= chunk;
    }
    if ((crc ^ 0xffffffffUL) != expected_crc) goto done;
    valid = TRUE;
done:
    if (descriptor >= 0 && tga_close(descriptor) != 0) valid = FALSE;
    if (fclose(program) != 0) valid = FALSE;
    if (!valid) (void)tga_remove_temp();
    return valid;
}

/* The SDK declares a void callback, but documents a short far return value:
 * zero aborts the failed request, nonzero retries it. Do not retry forever,
 * recurse into TIGA, or exit from inside its communication callback. */
static short far tga_user_error(unsigned short command, short code)
{
    tga_error_command = command;
    tga_error_code = code;
    tga_io_failed = TRUE;
    return 0;
}

static int tga_check(void)
{
    char message[100];
    if (!tga_io_failed) return TRUE;
    if (!tga_cleaning)
    {
        sprintf(message, "TIGA communication failed (command %04X, error %d)",
                (unsigned int)tga_error_command, (int)tga_error_code);
        error(message);
    }
    return FALSE;
}

static int tga_wait_draw(void)
{
    if (tga_io_failed) return FALSE;
    synchronize();
    return tga_check();
}

static int tga_wait_page(void)
{
    clock_t start, now;
    int busy;
    if (tga_io_failed) return FALSE;
    start = clock();
    do
    {
        busy = page_busy();
        if (!tga_check()) return FALSE;
        if (!busy) return TRUE;
        now = clock();
        if (start == (clock_t)-1 || now == (clock_t)-1 ||
            (unsigned long)now - (unsigned long)start >= TGA_WAIT_TICKS)
        {
            tga_io_failed = TRUE;
            if (!tga_cleaning) error("TIGA display-page timeout");
            return FALSE;
        }
    } while (busy);
    return TRUE;
}

/* flush_module is the documented host API: it frees both code and export
 * symbols. Keep ownership when the module is locked or release otherwise
 * fails, so recovery can retry without installing a second copy. */
static int tga_release_module(void)
{
    short released;
    if (tga_module < 0) return TRUE;
    if (!tga_open || tga_io_failed || !tga_wait_draw()) return FALSE;
    released = flush_module(tga_module);
    if (!tga_check() || released != 1) return FALSE;
    tga_module = -1;
    return TRUE;
}

static void tga_cleanup(void)
{
    int i;
    (void)tga_remove_temp();
    if (!tga_open) return;
    tga_cleaning = TRUE;
    tga_ready = FALSE;
    if (!tga_io_failed && tga_video_set)
    {
        (void)tga_wait_draw();
        if (!tga_io_failed) (void)tga_wait_page();
        if (!tga_io_failed) (void)tga_release_module();
        if (!tga_io_failed && tga_config_saved)
        {
            if (set_config((short)tga_previous_mode, TRUE) & 1)
                if (tga_palette_saved)
                    for (i = 0; i < 256 && !tga_io_failed; i += TGA_PALET_CHUNK)
                        set_palet(TGA_PALET_CHUNK, (long)i, tga_saved_palette + i);
        }
        if (!tga_io_failed)
            (void)set_videomode(tga_previous_video,
                               tga_previous_video == (unsigned short)TIGA ? NO_INIT : INIT);
    }
    /* CD_CLOSE is host-side teardown. It is also required after an aborted
     * request; no further GM commands are attempted after an I/O failure. */
    (void)tiga_set(CD_CLOSE);
    tga_open = tga_video_set = FALSE;
    tga_module = -1;
    tga_cleaning = FALSE;
}

void drv_close(void)
{
    exit(0);
}

void drv_release_optional(void)
{
    /* Geometry caches are bounded static storage. The module is optional
     * board memory; release it only between completed submissions. */
    if (tga_rendering) return;
    if (tga_module >= 0 && tga_open && !tga_io_failed)
    {
        tga_soft3d = TRUE;
        if (tga_release_module())
            drv_notice("TIGA soft3d on; optional TI module released");
        else
            drv_notice("TIGA soft3d on; TI module release failed");
    }
    soft3d_project_cache_reset(&tga_cache);
    drv_quadstream_reset(&tga_stream);
    tga_dirty = TRUE;
}

static int tga_mode_supported(const MODEINFO *mode)
{
    return mode->disp_hres == DRV_WIN_WIDTH &&
           mode->disp_vres == DRV_WIN_HEIGHT && mode->disp_psize == 8U &&
           mode->pixel_mask == 255UL && mode->color_class == 2U &&
           mode->palet_size == 256UL && mode->num_pages >= 1U;
}

void drv_init(int *argc, char **argv)
{
    long result;
    (void)argc;
    tga_cpu = tga_cpu_detect();
    tga_has_fpu = tga_cpu_has_fpu();
    if (argv == NULL || !tga_absolute_path(tga_program_path, argv[0])) tga_program_path[0] = '\0';
    tga_temp_sequence = (unsigned long)clock() & 0x00ffffffUL;
    if (atexit(tga_cleanup) != 0) error("Cannot register TIGA cleanup");
    result = tiga_set(CD_OPEN);
    if (result < 0L) error("Cannot open TIGA 2.x; install the board's TIGACD driver");
    tga_open = TRUE;
    tga_previous_video = get_videomode();
    if (!set_videomode(TIGA, INIT | CLR_SCREEN))
        error("Cannot initialize the TIGA graphics manager");
    tga_video_set = TRUE;
    install_usererror((void (far *)())tga_user_error);
    set_timeout(TGA_TIMEOUT_MS);
    get_config(&tga_config);
    if (!tga_check()) return;
    if ((tga_config.version_number >> 8) != 2U)
        error("This driver requires TIGA 2.x");
    tga_previous_mode = tga_config.current_mode;
    tga_config_saved = TRUE;
    /* Save only the indexed palette we can faithfully restore. Individual
     * reads avoid assuming a command buffer large enough for 256 entries. */
    if (tga_config.mode.color_class == 2U && tga_config.mode.palet_size == 256UL)
    {
        unsigned int i;
        for (i = 0; i < 256U; ++i)
        {
            if (!get_palet_entry((long)i, &tga_saved_palette[i].r,
                &tga_saved_palette[i].g, &tga_saved_palette[i].b,
                &tga_saved_palette[i].i))
                error("Cannot read TIGA palette");
            if (!tga_check()) return;
        }
        tga_palette_saved = TRUE;
    }
    tga_mouse_present = drv_dos_mouse_init();
}

int drv_init_window(int x, int y, int width, int height)
{
    MODEINFO mode;
    FONTINFO font;
    int selected = -1, best_pages = 0, best_dac = -1, found;
    unsigned int i, brightness, darkest = 766U, lightest = 0U;
    unsigned long capacity;
    (void)x; (void)y; (void)width; (void)height;
    for (i = 0; i < tga_config.num_modes && i <= 32767U; ++i)
    {
        memset(&mode, 0, sizeof(mode));
        found = get_modeinfo((short)i, &mode) != 0;
        if (!tga_check()) return FALSE;
        if (!found || !tga_mode_supported(&mode)) continue;
        if (selected < 0 || (mode.num_pages >= 2U && best_pages < 2) ||
            ((mode.num_pages >= 2U) == (best_pages >= 2) &&
             (int)mode.palet_gun_depth > best_dac))
        {
            selected = (int)i;
            best_pages = mode.num_pages >= 2U ? 2 : 1;
            best_dac = (int)mode.palet_gun_depth;
        }
    }
    if (selected < 0) error("TIGA needs a 640x480 programmable 256-color mode");
    if (!(set_config((short)selected, TRUE) & 1)) error("Cannot select TIGA display mode");
    get_config(&tga_config);
    if (!tga_check()) return FALSE;
    if (!tga_mode_supported(&tga_config.mode)) error("TIGA returned an incompatible display mode");
    /* A clipped polygon has at most ten vertices. Palette writes are also
     * deliberately short; never trust a vendor's default FIFO capacity. */
    if (tga_config.comm_buff_size < 128UL) error("TIGA command buffer is too small");
    if (install_primitives() < 0) error("Cannot load TIGA extprims.rlm");
    if (!tga_check()) return FALSE;
    capacity = tga_config.comm_buff_size;
    tga_packet_vertices = 0U;
    if (capacity > TGA_GSP_PACKET_OVERHEAD + TGA_GSP_HEADER * 4UL)
    {
        capacity = (capacity - TGA_GSP_PACKET_OVERHEAD - TGA_GSP_HEADER * 4UL) /
                   (TGA_GSP_VERTEX_WORDS * 4UL);
        if (capacity > TGA_GSP_MAX_VERTICES) capacity = TGA_GSP_MAX_VERTICES;
        tga_packet_vertices = (unsigned int)capacity;
    }
    drv_win_x = drv_win_y = 0;
    drv_win_width = DRV_WIN_WIDTH;
    drv_win_height = DRV_WIN_HEIGHT;
    for (i = 0; i < 256U; ++i)
    {
        tga_palette[i].r = earth_cmap[i][0];
        tga_palette[i].g = earth_cmap[i][1];
        tga_palette[i].b = earth_cmap[i][2];
        tga_palette[i].i = 0;
        brightness = (unsigned int)tga_palette[i].r + tga_palette[i].g + tga_palette[i].b;
        if (brightness < darkest) { darkest = brightness; tga_black = (unsigned short)i; }
        if (brightness > lightest) { lightest = brightness; tga_white = (unsigned short)i; }
    }
    for (i = 0; i < 256U; i += TGA_PALET_CHUNK)
    {
        set_palet(TGA_PALET_CHUNK, (long)i, tga_palette + i);
        if (!tga_check()) return FALSE;
    }
    set_clip_rect(DRV_WIN_WIDTH, DRV_WIN_HEIGHT, 0, 0);
    if (!tga_check()) return FALSE;
    set_fcolor((long)tga_white);
    if (!tga_check()) return FALSE;
    set_bcolor((long)tga_black);
    if (!tga_check()) return FALSE;
    (void)select_font(0);
    if (!tga_check()) return FALSE;
    memset(&font, 0, sizeof(font));
    (void)get_fontinfo(0, &font);
    if (!tga_check()) return FALSE;
    if (font.maxwide > 0 && font.maxwide <= 32) tga_font_width = (unsigned int)font.maxwide;
    if (font.charhigh > 0 && font.charhigh <= 64) tga_font_height = (unsigned int)font.charhigh;
    tga_display_page = 0;
    tga_draw_page = tga_config.mode.num_pages >= 2U ? 1 : 0;
    page_flip(tga_display_page, tga_draw_page);
    if (!tga_wait_page()) return FALSE;
    tga_ready = TRUE;
    tga_soft3d = TRUE;
    tga_dirty = TRUE;
    if (tga_draw_page == 0) drv_notice("TIGA single page; presentation may flicker");
    return TRUE;
}

static void tga_module_failed(const char *message)
{
    tga_soft3d = TRUE;
    drv_quadstream_reset(&tga_stream);
    soft3d_project_cache_reset(&tga_cache);
    drv_abort_frame(message);
    tga_dirty = TRUE;
}

static unsigned long tga_float_bits(const float TG_HUGE *value)
{
    const unsigned char TG_HUGE *p = (const unsigned char TG_HUGE *)value;
    /* The host binding is explicitly little-endian 16-bit x86 DOS. */
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int tga_transform_ti(unsigned int count, const Vertex TG_HUGE *vertices)
{
    unsigned int first, n, i, j, base, at;
    unsigned long words;
    if (tga_module < 0 || tga_packet_vertices == 0U) return FALSE;
    (void)soft3d_project_cache_begin(&tga_cache, count, drv_win_width, drv_win_height, 0U);
    for (first = 0; first < count; first += n)
    {
        n = count - first;
        if (n > tga_packet_vertices) n = tga_packet_vertices;
        words = TGA_GSP_WORDS(n);
        memset(tga_packet, 0, (size_t)words * sizeof(long));
        tga_packet[TGA_GSP_MAGIC] = TGA_GSP_ABI;
        tga_packet[TGA_GSP_COUNT] = (long)n;
        tga_packet[TGA_GSP_WIDTH] = (long)drv_win_width;
        tga_packet[TGA_GSP_HEIGHT] = (long)drv_win_height;
        tga_packet[TGA_GSP_FOCAL] = GLOBE_FOCAL_Q14;
        tga_packet[TGA_GSP_VIEW] = GLOBE_VIEW_DISTANCE;
        tga_packet[TGA_GSP_NEAR] = GLOBE_NEAR_DISTANCE;
        tga_packet[TGA_GSP_FAR] = GLOBE_FAR_DISTANCE;
        for (i = 0; i < 3U; ++i)
            for (j = 0; j < 3U; ++j) tga_packet[TGA_GSP_MATRIX + 3U * i + j] = tga_matrix.fixed[i][j];
        for (i = 0; i < n; ++i)
        {
            base = TGA_GSP_HEADER + TGA_GSP_VERTEX_WORDS * i;
            tga_packet[base + TGA_GSP_X] = (long)tga_float_bits(&vertices[first + i].x);
            tga_packet[base + TGA_GSP_Y] = (long)tga_float_bits(&vertices[first + i].y);
            tga_packet[base + TGA_GSP_Z] = (long)tga_float_bits(&vertices[first + i].z);
        }
        (void)cp_alt(USER_CP(tga_module | TGA_GSP_TRANSFORM), 2,
                     _DWORD(words), _ALTDWORD_PTR(words, tga_packet));
        if (!tga_check()) return FALSE;
        if (tga_packet[TGA_GSP_MAGIC] != TGA_GSP_ABI || tga_packet[TGA_GSP_STATUS] != TGA_GSP_OK ||
            tga_packet[TGA_GSP_COUNT] != (long)n) return FALSE;
        for (i = 0; i < n; ++i)
        {
            base = TGA_GSP_HEADER + TGA_GSP_VERTEX_WORDS * i;
            at = first + i;
            if (tga_packet[base + TGA_GSP_X] < -48000L || tga_packet[base + TGA_GSP_X] > 48000L ||
                tga_packet[base + TGA_GSP_Y] < -48000L || tga_packet[base + TGA_GSP_Y] > 48000L ||
                tga_packet[base + TGA_GSP_Z] < -96000L || tga_packet[base + TGA_GSP_Z] > 0L ||
                (tga_packet[base + TGA_GSP_PROJECTED] != 0L && tga_packet[base + TGA_GSP_PROJECTED] != 1L)) return FALSE;
            tga_points.fixed[at].x = tga_packet[base + TGA_GSP_X];
            tga_points.fixed[at].y = tga_packet[base + TGA_GSP_Y];
            tga_points.fixed[at].z = tga_packet[base + TGA_GSP_Z];
            if (tga_packet[base + TGA_GSP_PROJECTED])
            {
                if (tga_packet[base + TGA_GSP_SCREEN_X] < -32768L || tga_packet[base + TGA_GSP_SCREEN_X] > 32767L ||
                    tga_packet[base + TGA_GSP_SCREEN_Y] < -32768L || tga_packet[base + TGA_GSP_SCREEN_Y] > 32767L) return FALSE;
                tga_cache.screen[at].x = tga_packet[base + TGA_GSP_SCREEN_X];
                tga_cache.screen[at].y = tga_packet[base + TGA_GSP_SCREEN_Y];
                tga_cache.valid[at] = 1;
            }
        }
    }
    return TRUE;
}

void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                   const Vertex TG_HUGE *vertices,
                                   unsigned int quad_count,
                                   const unsigned int TG_HUGE *indices,
                                   const unsigned char TG_HUGE *colors)
{
    Drv_quad_context context;
    Soft3d_screen_point screen[SOFT3D_MAX_POLY];
    short xy[SOFT3D_MAX_POLY * 2];
    unsigned int i, j, corners[4], reuse = 0U;
    int transformed, count, convex;
    unsigned int next, after;
    long turn;
    if (!tga_ready || drv_frame_failed || tga_io_failed) return;
    if (vertices == NULL || colors == NULL ||
        !drv_validate_quadbatch(vertex_count, quad_count, indices))
    {
        drv_abort_frame("TIGA quad batch is out of range");
        return;
    }
    reuse = drv_quadstream_reuse(&tga_stream, vertex_count, vertices, &context);
    if (!tga_soft3d)
    {
        if (!tga_transform_ti(vertex_count, vertices))
        {
            tga_module_failed("TI transform failed; soft3d on");
            return;
        }
    }
    else
    {
        if (tga_has_fpu)
            transformed = soft3d_float_transform_reuse(tga_matrix.floating,
                vertex_count, vertices, tga_points.floating, reuse, SOFT3D_FLOAT_SINGLE);
        else if (tga_cpu >= 386)
            transformed = soft3d_386_transform_reuse(tga_matrix.fixed,
                vertex_count, vertices, tga_points.fixed, reuse);
        else
            transformed = soft3d_fixed_transform_reuse(tga_matrix.fixed,
                vertex_count, vertices, tga_points.fixed, reuse);
        if (!transformed)
        {
            drv_abort_frame("TIGA coordinate is out of range");
            return;
        }
        (void)soft3d_project_cache_begin(&tga_cache, vertex_count,
                                       drv_win_width, drv_win_height, reuse);
    }
    drv_quadstream_commit(&tga_stream, &context);
    for (i = 0; i < quad_count; ++i)
    {
        for (j = 0; j < 4U; ++j) corners[j] = indices[4U * i + j];
        if (tga_soft3d && tga_has_fpu)
            count = soft3d_float_project_cached(tga_points.floating, vertex_count,
                corners, 4U, drv_win_width, drv_win_height, SOFT3D_FLOAT_SINGLE, &tga_cache, screen);
        else
            count = soft3d_fixed_project_cached(tga_points.fixed, vertex_count,
                corners, 4U, drv_win_width, drv_win_height, &tga_cache, screen);
        count = soft3d_finish_polygon(screen, count, drv_win_width, drv_win_height);
        if (count < 3) continue;
        /* Shared software culling retains negative screen-space area.
         * TIGA's convex fill explicitly requires the opposite winding. */
        for (j = 0; j < (unsigned int)count; ++j)
        {
            xy[2U * j] = (short)screen[count - 1 - (int)j].x;
            xy[2U * j + 1U] = (short)screen[count - 1 - (int)j].y;
        }
        /* Pixel rounding at a silhouette can make a nominal globe quad
         * concave. Do not feed that case to the convex-only accelerator. */
        convex = TRUE;
        for (j = 0; j < (unsigned int)count; ++j)
        {
            next = (j + 1U) % (unsigned int)count;
            after = (j + 2U) % (unsigned int)count;
            turn = ((long)xy[2U * next] - xy[2U * j]) *
                   ((long)xy[2U * after + 1U] - xy[2U * next + 1U]) -
                   ((long)xy[2U * next + 1U] - xy[2U * j + 1U]) *
                   ((long)xy[2U * after] - xy[2U * next]);
            if (turn < 0L) convex = FALSE;
        }
        set_fcolor((long)colors[i]);
        if (!tga_check()) return;
        if (convex) fill_convex((short)count, xy);
        else fill_polygon((short)count, xy);
        if (!tga_check()) return;
        ++globe_quads_num_vis;
    }
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *first, Vertex TG_HUGE *second,
                              Vertex TG_HUGE *third, Vertex TG_HUGE *fourth,
                              unsigned char color)
{
    drv_submit_quad_as_batch(first, second, third, fourth, color);
}

static unsigned int tga_text_columns(void)
{
    unsigned long limit = tga_config.comm_buff_size > 32UL ? tga_config.comm_buff_size - 32UL : 1UL;
    unsigned int columns = (drv_win_width - 16U) / tga_font_width;
    if (columns > 119U) columns = 119U;
    if ((unsigned long)columns > limit) columns = (unsigned int)limit;
    return columns != 0U ? columns : 1U;
}

static void tga_draw_text(short y, const char *text)
{
    char line[120];
    unsigned int n, take, columns = tga_text_columns();
    while (*text != '\0' && y < (short)(drv_win_height - tga_font_height))
    {
        n = 0;
        while (n < columns && text[n] != '\0') ++n;
        take = n;
        if (text[n] != '\0')
        {
            while (take > 0U && text[take] != ' ') --take;
            if (take == 0U) take = n;
        }
        memcpy(line, text, take);
        line[take] = '\0';
        (void)text_out(8, y, line);
        if (!tga_check()) return;
        text += take;
        while (*text == ' ') ++text;
        y += (short)tga_font_height;
    }
}

void drv_draw_osd(void)
{
    char status[256], detail[100];
    unsigned int lines, columns;
    set_fcolor((long)tga_white);
    if (!tga_check()) return;
    drv_format_stats(status);
    drv_append_status(status, sizeof(status), tga_soft3d ? ", soft3d on" : ", soft3d off (TI)");
    sprintf(detail, ", %s%s, %s", tga_cpu >= 486 ? "486+" :
            (tga_cpu >= 386 ? "386" : (tga_cpu >= 286 ? "286" : "8086")),
            tga_has_fpu ? " + x87" : " integer",
            tga_config.mode.num_pages >= 2U ? "double buffer" : "single page");
    drv_append_status(status, sizeof(status), detail);
    tga_draw_text(8, status);
    columns = tga_text_columns();
    /* Help may wrap at spaces before the column limit; reserve one extra
     * line so the S option remains visible even with a wide system font. */
    lines = (unsigned int)strlen(drv_help) / columns + 2U;
    if (lines * tga_font_height > drv_win_height / 2U)
        lines = drv_win_height / (2U * tga_font_height);
    tga_draw_text((short)(drv_win_height - 8U - lines * tga_font_height), drv_help);
}

void drv_render(void)
{
    int prepared, retry;
    unsigned short previous;
    if (!tga_ready || tga_rendering || tga_io_failed) return;
    tga_rendering = TRUE;
    /* A bad optional module abandons the hidden partial frame and retries
     * once with host geometry. No partial frame is flipped into view. */
    for (retry = 0; retry < 2; ++retry)
    {
        drv_fps_start();
        tga_dirty = FALSE;
        clear_page((long)tga_black);
        if (!tga_check()) break;
        globe_quads_num_vis = 0;
        drv_quadstream_reset(&tga_stream);
        soft3d_project_cache_reset(&tga_cache);
        if (globe_ready())
        {
            if (tga_soft3d && tga_has_fpu)
                prepared = soft3d_float_prepare(tga_matrix.floating, &drv_rot_x, &drv_rot_y);
            else
                prepared = soft3d_fixed_prepare(tga_matrix.fixed, &drv_rot_x, &drv_rot_y);
            if (prepared) globe_draw_quadbatches();
            else drv_abort_frame("TIGA rotation is out of range");
        }
        if (drv_frame_failed)
        {
            drv_reset_timing();
            if (tga_dirty && retry == 0) continue;
            break;
        }
        if (drv_osd_enabled || !globe_ready()) drv_draw_osd();
        if (!tga_wait_draw()) break;
        if (tga_config.mode.num_pages >= 2U)
        {
            previous = tga_display_page;
            page_flip(tga_draw_page, previous);
            if (!tga_wait_page()) break;
            tga_display_page = tga_draw_page;
            tga_draw_page = previous;
        }
        drv_fps_stop();
        if (drv_auto_enabled && globe_ready()) drv_globe_rotate();
        break;
    }
    tga_rendering = FALSE;
}

static void tga_keyboard(unsigned char key)
{
    char loader_command[sizeof(tga_temp_path) + 3U];
    if (key == 27) { drv_close(); return; }
    if (key == 's' || key == 'S')
    {
        (void)tga_remove_temp();
        if (!tga_soft3d) tga_soft3d = TRUE;
        else if (tga_packet_vertices == 0U)
            drv_notice("soft3d on; TIGA command buffer cannot hold TI transforms");
        else
        {
            if (tga_module < 0)
            {
                if (tga_extract_module())
                {
                    /* No subsequently installed module imports our symbols.
                     * The documented flag avoids retaining them on the TI.
                     * Keep the unadorned path intact for file cleanup. */
                    strcpy(loader_command, tga_temp_path);
                    strcat(loader_command, " %f");
                    tga_module = install_rlm(loader_command);
                    /* Installation is synchronous. The board retains its
                     * loaded code, so the host file is no longer needed. */
                    (void)tga_remove_temp();
                    if (!tga_check()) return;
                    if (tga_module < 0)
                        drv_notice("soft3d on; embedded TI module could not load");
                }
                else drv_notice(tga_payload_error);
            }
            if (tga_module >= 0)
            {
                tga_soft3d = FALSE;
                drv_notice(tga_temp_path[0] == '\0' ? NULL : "TI temporary-file cleanup pending");
            }
        }
        soft3d_project_cache_reset(&tga_cache);
        drv_quadstream_reset(&tga_stream);
        drv_reset_timing();
        tga_dirty = TRUE;
    }
    else if (drv_handle_key(key)) tga_dirty = TRUE;
}

static void tga_poll_mouse(void)
{
    if (drv_dos_mouse_poll(tga_mouse_present)) tga_dirty = TRUE;
}

void drv_loop(void)
{
    int key;
    for (;;)
    {
        while (kbhit())
        {
            key = getch();
            if (key == 0 || key == 0xe0) { (void)getch(); continue; }
            tga_keyboard((unsigned char)key);
        }
        tga_poll_mouse();
        if (tga_dirty || (drv_auto_enabled && globe_ready())) drv_render();
    }
}
#endif /* DRV_TGA */
