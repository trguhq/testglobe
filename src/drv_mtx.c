/*
 * testglobe - Matrox LibShell driver for 16-bit Microsoft C 7 /AH.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "testglob.h"
#ifdef DRV_MTX
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include <conio.h>
#include <dos.h>
#include "drv.h"
#include "globe.h"
#include "glob_ext.h"
#include "drv_ext.h"

/* /AH and the matching LSCL.LIB supply far code/data pointers. DEF.H must
 * precede prototypes. The C binding converts float V3COORDs to 16.16. */
#include "XGX1/HEADERS/DEF.H"
#ifndef PROTO
#define PROTO
#endif
#include "XGX1/HEADERS/LIBFN.H"
#include "XGX1/HEADERS/IOLIB.H"

/* Required communication object, as in the supplied LSINFO.C example. */
struct pg_com pg;
static int mtx_initialized;
static int mtx_palette_saved;
static int mtx_error_policy_saved;
static int mtx_cleanup_active;
static int mtx_io_failed;
static short mtx_previous_error_reporting;
static int mtx_dirty = TRUE;
static int mtx_mouse_present;
static int mtx_cpu, mtx_has_fpu;
static int mtx_native_failed;
static int mtx_mask_saved;
static union {
    long fixed[3][3];
    float floating[3][3];
} mtx_matrix;
static unsigned short mtx_previous_lut;
static unsigned short mtx_previous_mask;
static unsigned short mtx_background;
static unsigned short mtx_foreground;
/* Last foreground color sent; valid only while the board state is known. */
static unsigned short mtx_color;
static int mtx_color_valid;
/* Unconfirmed fill packets, queued only during a frame's traversal. */
static unsigned int mtx_pending_fills;
static int mtx_pending_native, mtx_queue_fills;
static struct Lutval mtx_saved_palette[256];
static struct Lutval mtx_palette[256];
/* Software mode's bounded packet state: transformed and projected points of
 * the current band, whose lower row the next band of a stream reuses. */
static Drv_quad_stream mtx_stream;
static union {
    Soft3d_fixed_point fixed[SOFT3D_MAX_VERTICES];
    Soft3d_float_point floating[SOFT3D_MAX_VERTICES];
} mtx_points;
static Soft3d_screen_point mtx_screen[SOFT3D_MAX_VERTICES];
/* The native path uses this eye vector only for visibility rejection.
 * Software mode performs the whole geometry stage before screen filling. */
static float mtx_eye_x;
static float mtx_eye_y;
static float mtx_eye_z = 1.0f;

/* LSERROR stalls the shell before a second unread error. A blocking
 * lslibsync() cannot service that register, so use a counted LSINQ reply
 * as a completion marker while polling the SDK's error and Read FIFO APIs.
 * Packets contain a small command, the per-frame model/view commands or up
 * to MTX_FILL_PACKETS color/polygon pairs, and a four-byte inquiry; LUT
 * writes and text are bounded so even a legacy 256-byte command FIFO cannot
 * fill before error servicing resumes. */
#define MTX_WAIT_TICKS (10UL * (unsigned long)CLOCKS_PER_SEC)
/* A native fill is a color (4 bytes) and a four-vertex V3ARWPLYGN (52):
 * four with the inquiry use 228 bytes. Screen fills (4 + 44) are smaller. */
#define MTX_FILL_PACKETS 4U
/* Stale startup data: a full-screen readback is 153,600 words. A shell
 * that stalled on a second error resumes once the first is read, so wait
 * for at least one BIOS tick of silence after discarding anything. */
#define MTX_STALE_LIMIT 262144UL
#define MTX_STALE_QUIET ((unsigned long)CLOCKS_PER_SEC / 10UL)

/* Only forward clock steps count toward a deadline. DOS clock() can step
 * backward (at midnight, for example); that must neither end a wait early
 * nor restart it. Callers poll the board first, so ready words never read
 * the clock. */
typedef struct Mtx_wait
{
    clock_t previous;
    unsigned long elapsed;
    int started;
} Mtx_wait;

static void mtx_wait_begin(Mtx_wait *wait)
{
    wait->elapsed = 0UL;
    wait->started = FALSE;
}

/* Return this sample's forward step. An unavailable clock ends the wait;
 * the total saturates at MTX_WAIT_TICKS instead of wrapping. */
static unsigned long mtx_wait_step(Mtx_wait *wait)
{
    clock_t now = clock();
    unsigned long step = 0UL;
    if (now == (clock_t)-1) step = MTX_WAIT_TICKS;
    else
    {
        if (wait->started && now > wait->previous)
            step = (unsigned long)now - (unsigned long)wait->previous;
        wait->previous = now;
        wait->started = TRUE;
    }
    if (step >= MTX_WAIT_TICKS - wait->elapsed) wait->elapsed = MTX_WAIT_TICKS;
    else wait->elapsed += step;
    return step;
}

static int mtx_failure(const char *operation, unsigned short errors,
                        int communication, int recoverable)
{
    char message[128];
    const char *reason;
    /* A failed packet may not have set its color. */
    mtx_color_valid = FALSE;
    if (communication) mtx_io_failed = TRUE;
    if (errors & 1U) reason = "undefined opcode";
    else if (errors & 2U) reason = "unsupported parameter";
    else if (errors & 4U) reason = "board memory exhausted";
    else if (errors & 8U) reason = "arithmetic overflow";
    else if (errors & 16U) reason = "command-list nesting";
    else if (errors & 32U) reason = "command-list memory exhausted";
    else reason = communication ? "communication timeout" : "unknown error";
    sprintf(message, "Matrox %s: %s (0x%02X)", operation, reason,
            (unsigned int)errors);
    /* A fully drained, valid inquiry establishes that communication still
     * works. Only optional native geometry setup/polygon packets are
     * recoverable, permitting a bounded 2D retry; the latter include their
     * color command. A persistent color/fill error remains fatal at the
     * software packet's checkpoint. Unknown bits, command-list faults, and
     * transport faults stay fatal. */
    if (!communication && !mtx_cleanup_active && recoverable &&
        errors != 0U && (errors & ~15U) == 0U)
    {
        mtx_native_failed = TRUE;
        drv_soft3d_active = TRUE;
        drv_abort_frame(message);
        return FALSE;
    }
    if (mtx_cleanup_active) fprintf(stderr, "%s\n", message);
    else error(message);
    return FALSE;
}

/* One wait covers the entire reply. Callers bound the word count, so words
 * that are already ready need no deadline of their own. */
static int mtx_read_word(unsigned short *word, unsigned short *errors,
                         const char *operation, Mtx_wait *wait)
{
    for (;;)
    {
        /* IOGETERROR reads/acknowledges the eight-bit error register; zero
         * has no error bits set. Keep draining while firmware is busy. */
        *errors |= (unsigned short)iogeterror();
        /* SDK integration assumption: a positive RDFIFOCNT must mean that
         * IOGETW can read one complete word without waiting. The supplied
         * headers expose only word reads, but do not document count units
         * or atomic availability; verify this on the actual board/binding. */
        if (rdfifocnt() > 0)
        {
            *word = (unsigned short)iogetw();
            return TRUE;
        }
        (void)mtx_wait_step(wait);
        if (wait->elapsed >= MTX_WAIT_TICKS)
            return mtx_failure(operation, *errors, TRUE, FALSE);
    }
}

/* Each inquiry used here returns one value. With present == NULL that value
 * is required; otherwise an empty reply (an index this shell does not know)
 * clears *present instead of failing. */
static int mtx_inquire(unsigned short code, unsigned short *value,
                        const char *operation, char *invalid_reply,
                        int recoverable, int *present)
{
    unsigned short remaining, word, errors = 0;
    Mtx_wait wait;
    int valid;
    mtx_wait_begin(&wait);
    lsinq(code);
    ioflush();
    if (!mtx_read_word(&remaining, &errors, operation, &wait)) return FALSE;
    valid = remaining == 1U || (remaining == 0U && present != NULL);
    if (present != NULL) *present = remaining == 1U;
    /* Drain short malformed replies for cleanup, but do not trust an
     * unbounded corrupted count. */
    if (remaining > 16U)
    {
        mtx_io_failed = TRUE;
        if (mtx_cleanup_active) fprintf(stderr, "%s\n", invalid_reply);
        else error(invalid_reply);
        return FALSE;
    }
    while (remaining != 0U)
    {
        if (!mtx_read_word(&word, &errors, operation, &wait)) return FALSE;
        if (valid) *value = word;
        --remaining;
    }
    errors |= (unsigned short)iogeterror();
    if (!valid)
    {
        if (mtx_cleanup_active) fprintf(stderr, "%s\n", invalid_reply);
        else error(invalid_reply);
        return FALSE;
    }
    if (errors != 0) return mtx_failure(operation, errors, FALSE, recoverable);
    return TRUE;
}

/* recoverable marks optional native 3D packets; see mtx_failure(). */
static int mtx_checkpoint(const char *operation, int recoverable)
{
    unsigned short reporting;
    if (mtx_io_failed) return FALSE;
    return mtx_inquire(77, &reporting, operation,
                       "Invalid Matrox error-policy inquiry reply length",
                       recoverable, NULL);
}

#define MTX_COMMAND(command, operation) \
    do { command; (void)mtx_checkpoint(operation, FALSE); } while (0)

/* Used only in helpers which return a success flag. Never allow a failed
 * V3 operation to continue submitting the rest of that transformation. */
#define MTX_TRANSFORM(command, operation) \
    do { command; if (!mtx_checkpoint(operation, TRUE)) return FALSE; } while (0)

/* Confirm queued fills with one inquiry; only confirmed fills are counted
 * as visible. Native packets remain recoverable, as single ones were: a
 * frame retried with host transforms needs no finer error attribution. */
static int mtx_flush_fills(void)
{
    unsigned int fills = mtx_pending_fills;
    if (fills == 0U) return TRUE;
    mtx_pending_fills = 0U;
    if (!mtx_checkpoint(mtx_pending_native ? "polygon fill" : "screen polygon fill",
                        mtx_pending_native)) return FALSE;
    globe_quads_num_vis += fills;
    return TRUE;
}

/* Never mix native and screen packets in one group. */
static int mtx_fill_room(int native)
{
    if (mtx_pending_fills != 0U && mtx_pending_native != native)
        return mtx_flush_fills();
    return TRUE;
}

/* Direct calls outside a traversal still confirm their own packet. */
static void mtx_fill_sent(int native)
{
    mtx_pending_native = native;
    ++mtx_pending_fills;
    if (!mtx_queue_fills || mtx_pending_fills >= MTX_FILL_PACKETS)
        (void)mtx_flush_fills();
}

/* The foreground color persists between fills. Return whether a command
 * was needed: it is resent after a clear, the OSD or any failure. */
static int mtx_set_color(unsigned short color)
{
    if (mtx_color_valid && mtx_color == color) return FALSE;
    krhwsfcol(color);
    mtx_color = color;
    mtx_color_valid = TRUE;
    return TRUE;
}

static int mtx_read_words(short *words, unsigned short count,
                           const char *operation)
{
    unsigned short word, errors = 0;
    Mtx_wait wait;
    mtx_wait_begin(&wait);
    ioflush();
    while (count != 0U)
    {
        if (!mtx_read_word(&word, &errors, operation, &wait)) return FALSE;
        *words++ = (short)word;
        --count;
    }
    errors |= (unsigned short)iogeterror();
    if (errors != 0) return mtx_failure(operation, errors, FALSE, FALSE);
    return TRUE;
}

/* A previous program can leave a latched error (the register holds it until
 * it is read) and unread replies. Discard both before the first inquiry, so
 * neither is mistaken for this program's own reply or failure. */
static int mtx_discard_stale(void)
{
    unsigned short error_bits, errors = 0;
    unsigned long discarded = 0UL, words = 0UL, quiet = 0UL;
    Mtx_wait wait;
    mtx_wait_begin(&wait);
    for (;;)
    {
        error_bits = (unsigned short)iogeterror();
        if (error_bits != 0U || rdfifocnt() > 0)
        {
            if (discarded == MTX_STALE_LIMIT) break;
            if (error_bits != 0U) errors |= error_bits;
            else
            {
                (void)iogetw();
                ++words;
            }
            ++discarded;
            quiet = 0UL;
            continue;
        }
        if (discarded == 0UL) return TRUE;
        quiet += mtx_wait_step(&wait);
        if (wait.elapsed >= MTX_WAIT_TICKS) break;
        if (quiet >= MTX_STALE_QUIET)
        {
            fprintf(stderr, "Matrox: discarded %lu stale reply words, "
                    "error bits 0x%02X\n", words, (unsigned int)errors);
            return TRUE;
        }
    }
    mtx_io_failed = TRUE;
    error("Matrox board kept returning stale replies; reset the board");
    return FALSE;
}

static int mtx_upload_palette(struct Lutval *palette)
{
    unsigned short first;
    for (first = 0; first < 256U; first += 16U)
    {
        lslutmd(0, first, 16, palette + first);
        if (!mtx_checkpoint("palette upload", FALSE)) return FALSE;
    }
    return TRUE;
}

static void TG_ATEXIT_CALL mtx_cleanup(void)
{
    if (!mtx_initialized)
        return;
    mtx_cleanup_active = TRUE;
    /* An exit during a traversal confirms its fills before other packets. */
    if (!mtx_io_failed) (void)mtx_flush_fills();
    if (!mtx_io_failed && mtx_palette_saved)
    {
        (void)mtx_upload_palette(mtx_saved_palette);
    }
    if (!mtx_io_failed && mtx_palette_saved)
    {
        lslutsl(mtx_previous_lut, 0);
        (void)mtx_checkpoint("restore palette selection", FALSE);
    }
    if (!mtx_io_failed && mtx_mask_saved)
    {
        lslutpm(0, mtx_previous_mask);
        (void)mtx_checkpoint("restore plane mask", FALSE);
    }
    if (!mtx_io_failed)
    {
        lserror(mtx_error_policy_saved ? mtx_previous_error_reporting : 0);
        (void)mtx_checkpoint("restore error reporting", FALSE);
    }
    if (!mtx_io_failed)
    {
        /* Emulation is the final command; it may stop servicing inquiries. */
        lsemulen(1);
        ioflush();
    }
    mtx_initialized = FALSE;
    mtx_cleanup_active = FALSE;
}

void drv_close(void)
{
    /* atexit also restores the board after shared allocation failures. */
    exit(0);
}

/* This immediate-mode driver owns no optional host rendering cache. */
void drv_release_optional(void)
{
}

static unsigned short mtx_inquire_lut(void)
{
    unsigned short value = 0;
    (void)mtx_inquire(78, &value, "LUT inquiry",
                      "Invalid Matrox LUT inquiry reply length", FALSE, NULL);
    return value;
}

void drv_init(int *argc, char **argv)
{
    short system_info[10];
    unsigned short reporting;
    (void)argc;
    (void)argv;
    mtx_cpu = drv_cpu_detect();
#ifndef MTX_8086_BINDING
    /* The supplied LSCL.LIB, linked by both compilers, uses ENTER/LEAVE even
     * in its 2D binding; an 8086 misexecutes them inside lsinit. The host
     * probe remains 8086-safe. Define MTX_8086_BINDING only when linking a
     * binding built for 8086 instructions. */
    if (mtx_cpu < 286) error("This LibShell library requires an 80286 or later CPU");
#endif
    mtx_has_fpu = drv_cpu_has_fpu();
    mtx_native_failed = FALSE;
    /* Before the board changes state: Ctrl-C/Break then only sets a flag,
     * and the event loop exits through the cleanup registered below. */
    drv_dos_break_init();
    lsinit(0, 1);
    ioflush();
    mtx_initialized = TRUE;
    if (atexit(mtx_cleanup) != 0)
    {
        mtx_cleanup();
        error("Cannot register Matrox cleanup");
    }
    if (!mtx_discard_stale()) return;
    if (!mtx_inquire(77, &reporting, "error-policy inquiry",
                     "Invalid Matrox error-policy inquiry reply length",
                     FALSE, NULL)) return;
    mtx_previous_error_reporting = reporting != 0;
    mtx_error_policy_saved = TRUE;
    MTX_COMMAND(lserror(1), "enable error reporting");
    lsinqsys();
    if (!mtx_read_words(system_info, 10, "system inquiry")) return;
    if ((unsigned short)system_info[0] < DRV_WIN_WIDTH ||
        (unsigned short)system_info[1] < DRV_WIN_HEIGHT ||
        (unsigned short)system_info[2] < 256)
        error("Matrox LibShell needs a 640x480, 256-color display");
    /* Preserve the table replaced, the displayed LUT label and its plane
     * mask (LSINQ 88). Without index 88, or with a reply outside LSLUTPM's
     * 0..255 range, cleanup leaves the mask alone instead of guessing. */
    mtx_previous_lut = mtx_inquire_lut();
    if (!mtx_inquire(88, &mtx_previous_mask, "plane-mask inquiry",
                     "Invalid Matrox plane-mask inquiry reply length",
                     FALSE, &mtx_mask_saved)) return;
    if (mtx_previous_mask > 255U) mtx_mask_saved = FALSE;
    lslutrd(0, 0, 256);
    if (!mtx_read_words((short *)mtx_saved_palette, 256 * 3,
                        "palette inquiry")) return;
    mtx_palette_saved = TRUE;
    mtx_mouse_present = drv_dos_mouse_init();
}

/* Reissue all native projection state after failed initialization or S retry.
 * Screen-coordinate fills do not depend on these optional settings. */
static int mtx_native_projection(void)
{
    /* 4:3 aspect, 400-pixel globe diameter, room for the status display. */
    MTX_TRANSFORM(v2gpswdw(-1600.0, 1600.0, -1200.0, 1200.0), "world window");
    MTX_TRANSFORM(v3gpsproj(0), "projection");
    MTX_TRANSFORM(v3gpsdist((double)GLOBE_VIEW_DISTANCE), "view distance");
    MTX_TRANSFORM(v3gpshclip(0), "hither clipping");
    MTX_TRANSFORM(v3gpsyclip(0), "yon clipping");
    return TRUE;
}

int drv_init_window(int in_x, int in_y, int in_width, int in_height)
{
    int i;
    unsigned short brightness;
    unsigned short darkest = 766;
    unsigned short lightest = 0;
    /* PG-641 display dimensions are fixed, not a desktop window. */
    (void)in_x;
    (void)in_y;
    (void)in_width;
    (void)in_height;
    drv_win_x = 0;
    drv_win_y = 0;
    drv_win_width = DRV_WIN_WIDTH;
    drv_win_height = DRV_WIN_HEIGHT;
    for (i = 0; i < 256; ++i)
    {
        /* Eight-bit DACs use bits0..7; four-bit DACs use bits4..7.
         * Both accept the original earth_cmap component values 0..255. */
        mtx_palette[i].red = earth_cmap[i][0];
        mtx_palette[i].green = earth_cmap[i][1];
        mtx_palette[i].blue = earth_cmap[i][2];
        brightness = mtx_palette[i].red + mtx_palette[i].green +
                     mtx_palette[i].blue;
        if (brightness < darkest)
        {
            darkest = brightness;
            mtx_background = (unsigned short)i;
        }
        if (brightness > lightest)
        {
            lightest = brightness;
            mtx_foreground = (unsigned short)i;
        }
    }
    if (!mtx_upload_palette(mtx_palette)) return 0;
    MTX_COMMAND(lslutsl(0, 0), "palette selection");
    MTX_COMMAND(lslutpm(0, 0xff), "palette mask");
    MTX_COMMAND(lssbm(-1), "screen bitmap");
    MTX_COMMAND(lssclpl(-1), "clipping selection");
    MTX_COMMAND(scsorg(0, 0), "screen origin");
    MTX_COMMAND(v2gpsvwp(0, DRV_WIN_WIDTH - 1, 0, DRV_WIN_HEIGHT - 1), "viewport");
    if (!drv_soft3d_active) (void)mtx_native_projection();
    MTX_COMMAND(krhwsbop(0), "drawing operation");                 /* REPLACE, not SET-all-ones. */
    MTX_COMMAND(krhwsmsk(0xff), "plane mask");
    MTX_COMMAND(krhwstdef(0), "transparency");
    MTX_COMMAND(lssafapat(-1), "solid fill");               /* Solid fill, no board-specific shading. */
    MTX_COMMAND(krgcdi(), "graphics cursor");
    MTX_COMMAND(lssfont(-4), "font selection");                 /* Documented 640x480, 8x16 raster font. */
    MTX_COMMAND(sctxsalign(0, 0), "text alignment");
    MTX_COMMAND(sctxsdmode(0), "text mode");               /* Text must not enqueue read-FIFO replies. */
    MTX_COMMAND(sctxssf(1.0, 1.0), "text scale");
    mtx_color_valid = FALSE;
    mtx_dirty = TRUE;
    return 1;
}

static short mtx_angle(float angle)
{
    double wrapped;
    if (!(angle >= -FLT_MAX && angle <= FLT_MAX)) return 0;
    wrapped = fmod((double)angle, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return (short)wrapped;
}

static int mtx_prepare_software(short yaw, short pitch)
{
    if (mtx_has_fpu)
        return soft3d_float_prepare_ortho(mtx_matrix.floating, yaw, pitch);
    return soft3d_fixed_prepare_ortho(mtx_matrix.fixed, yaw, pitch);
}

static int mtx_set_transform(void)
{
    short yaw = mtx_angle(drv_rot_y);
    short pitch = mtx_angle(90.0f + drv_rot_x);
    double yaw_radians, pitch_radians, sin_pitch;
    if (drv_soft3d_active) return mtx_prepare_software(yaw, pitch);
    yaw_radians = (double)yaw * PI / 180.0;
    pitch_radians = (double)pitch * PI / 180.0;
    sin_pitch = sin(pitch_radians);
    /* A single rotation in each stage avoids undocumented cumulative
     * matrix ordering. Globe longitude runs around its Z axis. The view
     * command moves the viewer about X around the reference point. The
     * manual calls the origin a section of the modelling transformation
     * and does not say that v3mdid/v3vwid preserve it or the reference
     * point, so both are resent each frame. One recoverable checkpoint
     * (44 bytes) covers all six: any fault retries the whole frame. */
    v3mdid();
    v3mdorg(0.0, 0.0, 0.0);
    v3mdrotz(yaw);
    v3vwid();
    v3vwrpt(0.0, 0.0, 0.0);
    v3vwrotx(pitch);
    if (!mtx_checkpoint("model/view transform", TRUE)) return FALSE;
    /* Inverse model yaw applied to the viewer's world-space direction.
     * Use exactly the integer-degree angles sent to LibShell. */
    mtx_eye_x = (float)(-sin(yaw_radians) * sin_pitch);
    mtx_eye_y = (float)(-cos(yaw_radians) * sin_pitch);
    mtx_eye_z = (float)cos(pitch_radians);
    return TRUE;
}

/* Native LibShell 3D: world vertices of one quad, in traversal order. */
static void mtx_draw_native(const Vertex TG_HUGE *source[4],
                            unsigned char color)
{
    struct V3coord vertices[4];
    unsigned short count = 0;
    unsigned short i;
    float ax, ay, az, bx, by, bz;
    float nx, ny, nz;
    /* Tiny contiguous far-data packet for the SDK: never pass huge mesh
     * pointers directly to legacy bindings. Compact repeated pole
     * vertices into three-vertex caps to avoid degenerate quad fills. */
    for (i = 0; i < 4U; ++i)
    {
        if (count != 0 && source[i]->x == vertices[count - 1].x &&
            source[i]->y == vertices[count - 1].y &&
            source[i]->z == vertices[count - 1].z)
            continue;
        vertices[count].x = source[i]->x;
        vertices[count].y = source[i]->y;
        vertices[count].z = source[i]->z;
        ++count;
    }
    if (count > 1 && vertices[count - 1].x == vertices[0].x &&
        vertices[count - 1].y == vertices[0].y &&
        vertices[count - 1].z == vertices[0].z)
        --count;
    if (count < 3) return;
    /* Base LIB Shell fills do not remove hidden surfaces. Rejecting
     * back faces is sufficient for a convex globe: the retained facets
     * overlap only at edges. LibShell performs all vertex transforms,
     * projection, clipping and polygon filling. */
    ax = vertices[1].x - vertices[0].x;
    ay = vertices[1].y - vertices[0].y;
    az = vertices[1].z - vertices[0].z;
    bx = vertices[2].x - vertices[0].x;
    by = vertices[2].y - vertices[0].y;
    bz = vertices[2].z - vertices[0].z;
    nx = ay * bz - az * by;
    ny = az * bx - ax * bz;
    nz = ax * by - ay * bx;
    if (nx * mtx_eye_x + ny * mtx_eye_y + nz * mtx_eye_z <= 0.0f)
        return;
    /* Color (4) and four 16.16 vertices (52); see MTX_FILL_PACKETS. */
    if (!mtx_fill_room(TRUE)) return;
    (void)mtx_set_color((unsigned short)color);
    v3arwplygn(count, vertices);
    mtx_fill_sent(TRUE);
}

/* Software mode: the host transforms, projects, clips and culls; LibShell
 * fills screen polygons. Unlike the perspective drivers, Matrox maps its
 * fixed orthographic world window to the screen. */
static void mtx_draw_screen(const Soft3d_screen_point *points, int count,
                            unsigned char color)
{
    struct Phcoord polygon[SOFT3D_MAX_POLY];
    int i;
    for (i = 0; i < count; ++i)
    {
        polygon[i].x = (short)points[i].x;
        polygon[i].y = (short)points[i].y;
    }
    if (!mtx_fill_room(FALSE)) return;
    (void)mtx_set_color((unsigned short)color);
    scarwplygn((unsigned short)count, polygon);
    mtx_fill_sent(FALSE);
}

static void mtx_software_failed(const char *message)
{
    drv_quadstream_reset(&mtx_stream);
    drv_abort_frame(message);
}

static int mtx_project(unsigned int first, unsigned int count)
{
    int projected;
    if (mtx_has_fpu)
        projected = soft3d_float_project_ortho(mtx_points.floating + first, count,
            DRV_WIN_WIDTH, DRV_WIN_HEIGHT, 3200U, 2400U, mtx_screen + first);
    else
        projected = soft3d_fixed_project_ortho(mtx_points.fixed + first, count,
            DRV_WIN_WIDTH, DRV_WIN_HEIGHT, 3200U, 2400U, mtx_screen + first);
    if (projected != 0) return TRUE;
    mtx_software_failed("Matrox software projection failed");
    return FALSE;
}

/* Each packet vertex is transformed (x/y only) and projected once; with a
 * stream's row reuse, the previous band's lower row is not recomputed.
 * Points are identical to transforming and projecting each quad alone. */
static int mtx_software_points(unsigned int vertex_count,
                               const Vertex TG_HUGE *vertices,
                               unsigned int reuse)
{
    unsigned int i;
    int transformed;
    if (mtx_has_fpu)
        transformed = soft3d_float_transform_xy_reuse(mtx_matrix.floating,
            vertex_count, vertices, mtx_points.floating, reuse);
    else if (mtx_cpu >= 386)
        transformed = soft3d_386_transform_xy_reuse(mtx_matrix.fixed,
            vertex_count, vertices, mtx_points.fixed, reuse);
    else
        transformed = soft3d_fixed_transform_xy_reuse(mtx_matrix.fixed,
            vertex_count, vertices, mtx_points.fixed, reuse);
    if (!transformed)
    {
        mtx_software_failed("Matrox software coordinate is out of range");
        return FALSE;
    }
    if (reuse == 0U) return mtx_project(0U, vertex_count);
    /* As in the transform, old odd entries become the new even entries. */
    for (i = 0; i < reuse; ++i) mtx_screen[2U * i] = mtx_screen[2U * i + 1U];
    for (i = 1U; i < vertex_count; i += 2U)
        if (!mtx_project(i, 1U)) return FALSE;
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
    const Vertex TG_HUGE *source[4];
    unsigned int i, j, reuse;
    int count;
    if (drv_frame_failed || mtx_io_failed) return;
    if (vertices == NULL || colors == NULL ||
        !drv_validate_quadbatch(vertex_count, quad_count, indices))
    {
        mtx_software_failed("Matrox quad batch is out of range");
        return;
    }
    if (!drv_soft3d_active)
    {
        /* LibShell transforms world vertices itself: nothing to reuse. */
        for (i = 0; i < quad_count && !drv_frame_failed && !mtx_io_failed; ++i)
        {
            for (j = 0; j < 4U; ++j) source[j] = vertices + indices[4U * i + j];
            mtx_draw_native(source, colors[i]);
        }
        return;
    }
    reuse = drv_quadstream_reuse(&mtx_stream, vertex_count, vertices, &context);
    if (!mtx_software_points(vertex_count, vertices, reuse)) return;
    drv_quadstream_commit(&mtx_stream, &context);
    for (i = 0; i < quad_count; ++i)
    {
        for (j = 0; j < 4U; ++j) screen[j] = mtx_screen[indices[4U * i + j]];
        count = soft3d_finish_polygon(screen, 4, DRV_WIN_WIDTH - 1U,
                                       DRV_WIN_HEIGHT - 1U);
        if (count < 3) continue;
        mtx_draw_screen(screen, count, colors[i]);
        if (drv_frame_failed || mtx_io_failed) return;
    }
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *xyz1,
                              Vertex TG_HUGE *xyz2,
                              Vertex TG_HUGE *xyz3,
                              Vertex TG_HUGE *xyz4,
                              unsigned char color)
{
    const Vertex TG_HUGE *source[4];
    if (xyz1 == NULL || xyz2 == NULL || xyz3 == NULL || xyz4 == NULL ||
        drv_frame_failed || mtx_io_failed) return;
    /* A lone software quad is a one-quad packet without reuse. */
    if (drv_soft3d_active)
    {
        drv_submit_quad_as_batch(xyz1, xyz2, xyz3, xyz4, color);
        return;
    }
    source[0] = xyz1;
    source[1] = xyz2;
    source[2] = xyz3;
    source[3] = xyz4;
    mtx_draw_native(source, color);
}

/* The selected font is 8x16. Wrap both status and help to the actual screen
 * width. SCTXBSTRING expands bytes into 16-bit characters: with its
 * eight-byte header and the inquiry, a 78-character line uses 168 bytes,
 * well inside the smallest command FIFO. */
static unsigned int mtx_text_lines(const char *text, short y, int draw)
{
    char line[79];
    unsigned int count, split, lines = 0;
    while (*text != '\0')
    {
        count = (unsigned int)strlen(text);
        if (count > 78U)
        {
            count = 78U;
            split = count;
            while (split != 0U && text[split] != ' ') --split;
            if (split != 0U) count = split;
        }
        memcpy(line, text, count);
        line[count] = '\0';
        if (draw) MTX_COMMAND(sctxbstring(8, (short)(y + 16U * lines), line), "OSD text");
        ++lines;
        text += count;
        while (*text == ' ') ++text;
    }
    return lines;
}

void drv_draw_osd(void)
{
    char status[256];
    unsigned int lines;
    if (mtx_set_color(mtx_foreground)) (void)mtx_checkpoint("text color", FALSE);
    lines = mtx_text_lines(drv_help, 0, FALSE);
    (void)mtx_text_lines(drv_help, (short)(DRV_WIN_HEIGHT - 4U - 16U * lines), TRUE);
    drv_format_stats(status);
    drv_append_soft3d_status(status, sizeof(status), "LibShell", mtx_cpu, mtx_has_fpu);
    (void)mtx_text_lines(status, 8, TRUE);
    /* The manual does not say text leaves the foreground color unchanged. */
    mtx_color_valid = FALSE;
}

void drv_render(void)
{
    int attempt, prepared;
    if (mtx_io_failed) return;
    /* The board's immediate surface can contain a partial native pass.
     * Clear it before the single host retry, retaining palette and input. */
    for (attempt = 0; attempt < 2; ++attempt)
    {
        drv_fps_start();
        mtx_native_failed = FALSE;
        drv_quadstream_reset(&mtx_stream);
        MTX_COMMAND(krhwcclr(mtx_background), "clear screen");
        mtx_color_valid = FALSE;
        globe_quads_num_vis = 0;
        if (globe_ready())
        {
            prepared = mtx_set_transform();
            if (prepared)
            {
                /* Fills are confirmed in groups. Confirm the last group
                 * before any other command, so its faults stay recoverable. */
                mtx_queue_fills = TRUE;
                /* Native LibShell needs world vertices, not packet copies. */
                if (drv_soft3d_active) globe_draw_quadbatches();
                else globe_draw_quads();
                mtx_queue_fills = FALSE;
                (void)mtx_flush_fills();
            }
            else if (!drv_frame_failed) drv_abort_frame("Matrox software rotation failed");
        }
        if (drv_frame_failed)
        {
            drv_reset_timing();
            if (mtx_native_failed && attempt == 0) continue;
            MTX_COMMAND(krhwcclr(mtx_background), "clear failed frame");
            mtx_color_valid = FALSE;
            globe_quads_num_vis = 0;
        }
        if (drv_status_required()) drv_draw_osd();
        /* Completion markers include actual board execution in FPS. */
        if (!drv_frame_failed) drv_fps_stop();
        if (drv_animation_allowed()) drv_globe_rotate();
        return;
    }
}

static void mtx_keyboard(unsigned char key)
{
    if (key == 27) { drv_close(); return; }
    if (key == 's' || key == 'S')
    {
        if (drv_soft3d_toggle())
        {
            /* Commit the selected path only after its frame setup succeeds.
             * A rejected optional setup leaves the working native mode and
             * requested-vs-active OSD state intact for another S-key retry. */
            if (mtx_prepare_software(mtx_angle(drv_rot_y), mtx_angle(90.0f + drv_rot_x)))
            {
                drv_soft3d_active = TRUE;
                drv_notice(NULL);
            }
            else drv_notice("Matrox software setup failed; using LibShell");
        }
        else if (mtx_native_projection())
        {
            drv_soft3d_active = FALSE;
            drv_notice(NULL);
        }
        drv_quadstream_reset(&mtx_stream);
        mtx_dirty = TRUE;
    }
    else if (drv_handle_key(key)) mtx_dirty = TRUE;
}

static void mtx_poll_mouse(void)
{
    if (drv_dos_mouse_poll(mtx_mouse_present)) mtx_dirty = TRUE;
}

void drv_loop(void)
{
    int key;
    for (;;)
    {
        while (kbhit())
        {
            key = getch();
            if (key == 0 || key == 0xe0)
            {
                (void)getch();
                continue;
            }
            mtx_keyboard((unsigned char)key);
        }
        /* Ctrl-C/Break only set a flag; leave through exit's cleanup, which
         * restores the palette and board emulation, instead of from DOS. */
        if (drv_dos_break_pending()) drv_close();
        mtx_poll_mouse();
        if (mtx_dirty || drv_animation_allowed())
        {
            drv_render();
            mtx_dirty = FALSE;
        }
    }
}
#endif /* DRV_MTX */
