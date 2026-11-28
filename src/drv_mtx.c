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

#ifdef MTX_LSCL
/* The vendor binding: /AH and the matching LSCL.LIB supply far code/data
 * pointers. DEF.H must precede prototypes. */
#include "XGX1/HEADERS/DEF.H"
#ifndef PROTO
#define PROTO
#endif
#include "XGX1/HEADERS/LIBFN.H"
#include "XGX1/HEADERS/IOLIB.H"

/* Required communication object, as in the supplied LSINFO.C example. */
struct pg_com pg;
#else
/* The same calls, implemented directly on the board by mtx_ls.c. */
#include "mtx_ls.h"
#endif
/* Boards without the LIB Shell: the IBM PGC command set (mtx_pgc.c). */
#include "mtx_pgc.h"
static int mtx_initialized;
/* A PGC-compatible board instead of a LIB Shell board, and the display
 * (graphics 0 or emulator 1) it showed at startup. */
static int mtx_pgc;
static unsigned char mtx_pgc_display = 1U;
static int mtx_palette_saved;
static int mtx_error_policy_saved;
static int mtx_cleanup_active;
static int mtx_io_failed;
static short mtx_previous_error_reporting;
static int mtx_dirty = TRUE;
static int mtx_mouse_present;
static int mtx_native_failed;
static int mtx_mask_saved;
static Soft3d_matrix mtx_matrix;
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
static Soft3d_points mtx_points;
static Soft3d_project_cache mtx_cache;
/* The native path uses the eye's object-space position only for visibility
 * rejection; software mode performs the whole geometry stage before screen
 * filling. Without an x87, native frames also keep the eye in Q4 integers
 * and decide most facings without emulated floats. */
static float mtx_eye_x;
static float mtx_eye_y;
static float mtx_eye_z = (float)GLOBE_VIEW_DISTANCE;
static long mtx_eye_fixed[3];
static int mtx_integer_cull;
/* Q4 object coordinates: the transform kernels' 16 units per world unit. */
#define MTX_Q4 16L
/* Q14 matrix entries, as in the shared fixed-point kernels. */
#define MTX_Q14 16384L

/* LSERROR stalls the shell before a second unread error. A blocking
 * lslibsync() cannot service that register, so use a counted LSINQ reply
 * as a completion marker while polling the ERROR register and read FIFO.
 * Packets contain a small command, the per-frame model/view commands or up
 * to MTX_FILL_PACKETS color/polygon pairs, and a four-byte inquiry; LUT
 * writes and text are bounded too. The PG Series 1 command FIFO holds 4096
 * words, so a stalled shell cannot fill it before error servicing resumes;
 * the bound also suits a 256-byte FIFO. */
#define MTX_WAIT_TICKS (10UL * (unsigned long)CLOCKS_PER_SEC)
/* A native fill is a color (4 bytes) and a four-vertex V3ARWPLYGN (52);
 * screen fills (4 + 44) are smaller. A group of fills leaves 32 bytes for
 * its 4-byte inquiry and margin: four fills in a 256-byte FIFO (228 bytes
 * in flight). MTXFIFO, in bytes, can assume a larger command FIFO, up to
 * the PG Series 1 boards' 4096 words (8192 bytes): 145 fills a group. */
#define MTX_FILL_BYTES 56UL
#define MTX_FIFO_DEFAULT 256UL
#define MTX_FIFO_LARGEST 8192UL
static unsigned int mtx_fill_packets = 4U;
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

/* LIB Shell ERROR register bits, or PGC error codes as bits (code n is
 * bit n; see mtx_pgc.h). */
static const char *mtx_error_reason(unsigned short errors, int communication)
{
    if (mtx_pgc)
    {
        if (errors & (1U << 1)) return "parameter out of range";
        if (errors & (1U << 3)) return "board memory exhausted";
        if (errors & (1U << 4)) return "arithmetic overflow";
        if (errors & (1U << 6)) return "opcode not recognized";
        if (errors & (1U << 10)) return "area fill error";
        if (errors & ((1U << 7) | (1U << 8))) return "command-list nesting";
        if (errors & (1U << 9)) return "string or command list too long";
    }
    else
    {
        if (errors & 1U) return "undefined opcode";
        if (errors & 2U) return "unsupported parameter";
        if (errors & 4U) return "board memory exhausted";
        if (errors & 8U) return "arithmetic overflow";
        if (errors & 16U) return "command-list nesting";
        if (errors & 32U) return "command-list memory exhausted";
    }
    return communication ? "communication timeout" : "unknown error";
}

/* Errors from native 3D geometry that host transforms can avoid: LIB Shell
 * opcode, parameter, memory and overflow bits; PGC range, overflow and
 * area-fill codes. */
static int mtx_error_recoverable(unsigned short errors)
{
    if (errors == 0U) return FALSE;
    if (mtx_pgc) return (errors & ~((1U << 1) | (1U << 4) | (1U << 10))) == 0U;
    return (errors & ~15U) == 0U;
}

static int mtx_failure(const char *operation, unsigned short errors,
                        int communication, int recoverable)
{
    char message[128];
    const char *reason;
    /* A failed packet may not have set its color. */
    mtx_color_valid = FALSE;
    if (communication) mtx_io_failed = TRUE;
    reason = mtx_error_reason(errors, communication);
    if (mtx_pgc)
    {
        /* The manuals number PGC errors; name the lowest one reported. */
        unsigned int code = 0U;
        while (code < 15U && (errors & (1U << code)) == 0U) ++code;
        if (errors == 0U) sprintf(message, "PGC %s: %s", operation, reason);
        else sprintf(message, "PGC %s: %s (error %u%s)", operation, reason, code,
                     code == 15U ? " or later" : "");
    }
    else sprintf(message, "Matrox %s: %s (0x%02X)", operation, reason,
                 (unsigned int)errors);
    /* A fully drained, valid inquiry establishes that communication still
     * works. Only optional native geometry setup/polygon packets are
     * recoverable, permitting a bounded 2D retry; the latter include their
     * color command. A persistent color/fill error remains fatal at the
     * software packet's checkpoint. Unknown bits, command-list faults, and
     * transport faults stay fatal. */
    if (!communication && !mtx_cleanup_active && recoverable &&
        mtx_error_recoverable(errors))
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
         * has no error bits set. Keep draining while firmware is busy. The
         * direct binding reads it without waiting; the vendor's first waits
         * for the command FIFO to drain (see docs/matrox.md). */
        *errors |= (unsigned short)iogeterror();
        /* RDFIFOCNT counts whole words, so IOGETW then reads one without
         * waiting. */
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

/* A PGC answers FLAGRD only after running every earlier command, whose
 * errors are reported by then. */
static int mtx_pgc_sync(const char *operation, int recoverable)
{
    unsigned char value;
    unsigned short errors;
    if (mtx_io_failed) return FALSE;
    if (!pgc_flagrd(4U, &value, 1U))
        return mtx_failure(operation, (unsigned short)pgc_take_errors(), TRUE, FALSE);
    errors = (unsigned short)pgc_take_errors();
    if (errors != 0U) return mtx_failure(operation, errors, FALSE, recoverable);
    return TRUE;
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
    if (!mtx_queue_fills || mtx_pending_fills >= mtx_fill_packets)
        (void)mtx_flush_fills();
}

/* The foreground color persists between fills. Return whether a command
 * was needed: it is resent after a clear, the OSD or any failure. */
static int mtx_set_color(unsigned short color)
{
    if (mtx_color_valid && mtx_color == color) return FALSE;
    if (mtx_pgc) pgc_color((unsigned char)color);
    else krhwsfcol(color);
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
    if (mtx_pgc)
    {
        /* The LUT saved with LUTSAV, the display shown at startup, error
         * reporting and ASCII mode. */
        if (!mtx_io_failed && !pgc_failed())
        {
            if (mtx_palette_saved) pgc_lutint(255U);
            pgc_displa(mtx_pgc_display);
        }
        pgc_close();
        mtx_initialized = FALSE;
        mtx_cleanup_active = FALSE;
        return;
    }
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

static void mtx_fifo_option(void)
{
    const char *setting = getenv("MTXFIFO");
    char *end;
    unsigned long bytes = MTX_FIFO_DEFAULT;
    if (setting != NULL && *setting != '\0')
    {
        bytes = strtoul(setting, &end, 10);
        if (*end != '\0' || bytes < MTX_FIFO_DEFAULT || bytes > MTX_FIFO_LARGEST)
            error("MTXFIFO must be a command FIFO size from 256 to 8192 bytes");
    }
    mtx_fill_packets = (unsigned int)((bytes - 32UL) / MTX_FILL_BYTES);
}

#ifndef MTX_LSCL
/* pgc_open() has selected hex mode and enabled error reports. LUTSAV keeps
 * the board's LUT for cleanup, which also restores the display it showed. */
static void mtx_pgc_init(void)
{
    unsigned char display;
    mtx_initialized = TRUE;
    if (atexit(mtx_cleanup) != 0)
    {
        mtx_cleanup();
        error("Cannot register Matrox cleanup");
    }
    if (!pgc_flagrd(5U, &display, 1U))
    {
        (void)mtx_failure("display inquiry", 0U, TRUE, FALSE);
        return;
    }
    mtx_pgc_display = display;
    pgc_resetf();
    pgc_lutsav();
    mtx_palette_saved = TRUE;
    pgc_displa(0U);
    if (!mtx_pgc_sync("startup", FALSE)) return;
    mtx_mouse_present = drv_dos_mouse_init();
}
#endif

void drv_init(int *argc, char **argv)
{
    short system_info[10];
    unsigned short reporting;
#ifndef MTX_LSCL
    char message[128];
#endif
    (void)argc;
    (void)argv;
    drv_soft3d_select();
    mtx_fifo_option();
#if defined(MTX_LSCL) && !defined(MTX_8086_BINDING)
    /* The supplied LSCL.LIB, linked by both compilers, uses ENTER/LEAVE even
     * in its 2D binding; an 8086 misexecutes them inside lsinit. The host
     * probe remains 8086-safe. Define MTX_8086_BINDING only when linking a
     * binding built for 8086 instructions. The direct binding needs none. */
    if (drv_soft3d_cpu < 286) error("This LibShell library requires an 80286 or later CPU");
#endif
    mtx_native_failed = FALSE;
    /* Before the board changes state: Ctrl-C/Break then only sets a flag,
     * and the event loop exits through the cleanup registered below. */
    drv_dos_break_init();
    lsinit(0, 1);
#ifndef MTX_LSCL
    if (!mtx_ls_present())
    {
        /* Without a LIB Shell board, a PGC-compatible board at C6000h or
         * C6400h, such as an IBM PGC or a Matrox PG-640A. */
        if (!pgc_open())
        {
            sprintf(message, "No LIB Shell board at segment %04Xh and no PGC at"
                    " C6000h or C6400h; download LIB SHELL or set PG1281",
                    mtx_ls_segment());
            error(message);
        }
        mtx_pgc = TRUE;
        mtx_pgc_init();
        return;
    }
#endif
    /* Discard what a previous program left: queued commands run, unread
     * replies are dropped. */
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
 * Screen-coordinate fills do not depend on these optional settings.
 *   LibShell's perspective follows the PGC's, which the IBM PGC and Matrox
 * PG-640A manuals define: the viewing angle spans the window's diagonal,
 *   x_screen = x_view / (distance - z_view) * (diagonal / 2)
 *              / tan(angle / 2) * (viewport width / window width),
 * and likewise for y. With the window's aspect equal to the viewport's,
 * the scale is half the viewport's diagonal over tan(angle / 2), and the
 * window's size cancels. Matching the shared camera's focal length,
 * (height / 2) / tan(field / 2), gives 69.15 degrees for the 45-degree
 * field, sent as 69 (LibShell's ANGLE is whole degrees): an image 0.27%
 * larger than host transforms draw. The window is the shared field on the
 * view plane through the reference point. */
static double mtx_view_window(double *half_width, double *half_height)
{
    double field;
    field = tan((double)GLOBE_FIELD_OF_VIEW * PI / 360.0);
    *half_height = (double)GLOBE_VIEW_DISTANCE * field;
    *half_width = *half_height * (DRV_WIN_WIDTH - 1) / (DRV_WIN_HEIGHT - 1);
    return 2.0 * atan(sqrt((double)(DRV_WIN_WIDTH - 1) * (DRV_WIN_WIDTH - 1) +
                           (double)(DRV_WIN_HEIGHT - 1) * (DRV_WIN_HEIGHT - 1)) *
                      field / DRV_WIN_HEIGHT) * 180.0 / PI;
}

static int mtx_native_projection(void)
{
    double half_width, half_height, angle;
    angle = mtx_view_window(&half_width, &half_height);
    if (mtx_pgc)
    {
        /* The 3D window is set each frame: the OSD and software fills use
         * a pixel window. Errors surface at the next synchronization. */
        pgc_projct((int)floor(angle + 0.5));
        pgc_distan((double)GLOBE_VIEW_DISTANCE);
        pgc_cliph(0U);
        pgc_clipy(0U);
        return TRUE;
    }
    MTX_TRANSFORM(v2gpswdw(-half_width, half_width, -half_height, half_height),
                  "world window");
    MTX_TRANSFORM(v3gpsproj((short)floor(angle + 0.5)), "projection");
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
    if (mtx_pgc)
    {
        /* LUTX keeps all eight bits on a PG-640A; LUT keeps four. */
        for (i = 0; i < 256; ++i)
            pgc_lut((unsigned char)i, (unsigned char)mtx_palette[i].red,
                    (unsigned char)mtx_palette[i].green,
                    (unsigned char)mtx_palette[i].blue);
        /* The viewport counts pixels up from the screen's lower left.
         * Text is left-justified with its top at the current point; in
         * the OSD's pixel window a size of 8 gives 8 x 12 cells. */
        pgc_vwport(0, DRV_WIN_WIDTH - 1, 0, DRV_WIN_HEIGHT - 1);
        pgc_prmfil(1U);
        pgc_tjust(1U, 3U);
        pgc_tsize(8.0);
        if (!mtx_pgc_sync("display setup", FALSE)) return 0;
        /* As with LibShell, a rejected projection leaves host transforms. */
        if (!drv_soft3d_active && mtx_native_projection())
            (void)mtx_pgc_sync("projection", TRUE);
        mtx_color_valid = FALSE;
        mtx_dirty = TRUE;
        return 1;
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
    if (drv_soft3d_kernel == DRV_KERNEL_FLOAT)
        return soft3d_float_prepare_degrees(mtx_matrix.floating, yaw, pitch);
    return soft3d_fixed_prepare_degrees(mtx_matrix.fixed, yaw, pitch);
}

/* The eye's Q4 object-space position for integer culling: the matrix's
 * last row, the direction to the eye, scaled to the viewing distance.
 * 16384 * 48000 fits a 32-bit long. */
static int mtx_prepare_integer_eye(short yaw, short pitch)
{
    long matrix[3][3];
    int i;
    if (!soft3d_fixed_prepare_degrees(matrix, yaw, pitch)) return FALSE;
    for (i = 0; i < 3; ++i)
        mtx_eye_fixed[i] = matrix[2][i] * (GLOBE_VIEW_DISTANCE * MTX_Q4) / MTX_Q14;
    return TRUE;
}

static int mtx_set_transform(void)
{
    short yaw = mtx_angle(drv_rot_y);
    short pitch = mtx_angle(90.0f + drv_rot_x);
    double yaw_radians, pitch_radians, sin_pitch, distance;
    double half_width, half_height;
    mtx_integer_cull = FALSE;
    if (drv_soft3d_active)
    {
        if (mtx_pgc)
            pgc_window(0.0, DRV_WIN_WIDTH - 1.0, 0.0, DRV_WIN_HEIGHT - 1.0);
        return mtx_prepare_software(yaw, pitch);
    }
    yaw_radians = (double)yaw * PI / 180.0;
    pitch_radians = (double)pitch * PI / 180.0;
    sin_pitch = sin(pitch_radians);
    /* A single rotation in each stage avoids undocumented cumulative
     * matrix ordering. Globe longitude runs around its Z axis. The view
     * command moves the viewer about X around the reference point. The
     * manual calls the origin a section of the modelling transformation
     * and does not say that v3mdid/v3vwid preserve it or the reference
     * point, so both are resent each frame. One recoverable checkpoint
     * (44 bytes) covers all six: any fault retries the whole frame. A PGC
     * reports them with the frame's fills, also recoverably. */
    if (mtx_pgc)
    {
        (void)mtx_view_window(&half_width, &half_height);
        pgc_window(-half_width, half_width, -half_height, half_height);
        pgc_mdiden();
        pgc_mdorg(0.0, 0.0, 0.0);
        pgc_mdrotz(yaw);
        pgc_vwiden();
        pgc_vwrpt(0.0, 0.0, 0.0);
        pgc_vwrotx(pitch);
    }
    else
    {
        v3mdid();
        v3mdorg(0.0, 0.0, 0.0);
        v3mdrotz(yaw);
        v3vwid();
        v3vwrpt(0.0, 0.0, 0.0);
        v3vwrotx(pitch);
        if (!mtx_checkpoint("model/view transform", TRUE)) return FALSE;
    }
    /* The eye sits at the viewing distance from the reference point (the
     * origin): inverse model yaw applied to the viewer's direction, using
     * exactly the integer-degree angles sent to LibShell. The float eye
     * stays even without an x87: undecided integer facings use it, so the
     * submitted polygons are those of the float test alone. */
    distance = (double)GLOBE_VIEW_DISTANCE;
    mtx_eye_x = (float)(-sin(yaw_radians) * sin_pitch * distance);
    mtx_eye_y = (float)(-cos(yaw_radians) * sin_pitch * distance);
    mtx_eye_z = (float)(cos(pitch_radians) * distance);
    if (drv_soft3d_kernel != DRV_KERNEL_FLOAT)
        mtx_integer_cull = mtx_prepare_integer_eye(yaw, pitch);
    return TRUE;
}

/* Tiny contiguous far-data packet for the SDK: never pass huge mesh
 * pointers directly to legacy bindings. Compact repeated pole vertices
 * into three-vertex caps to avoid degenerate quad fills. kept[] names the
 * source of each retained vertex. */
static unsigned short mtx_compact(const Vertex TG_HUGE *source[4],
                                  struct V3coord vertices[4],
                                  const Vertex TG_HUGE *kept[4])
{
    unsigned short count = 0;
    unsigned short i;
    for (i = 0; i < 4U; ++i)
    {
        if (count != 0 && source[i]->x == vertices[count - 1].x &&
            source[i]->y == vertices[count - 1].y &&
            source[i]->z == vertices[count - 1].z)
            continue;
        vertices[count].x = source[i]->x;
        vertices[count].y = source[i]->y;
        vertices[count].z = source[i]->z;
        kept[count] = source[i];
        ++count;
    }
    if (count > 1 && vertices[count - 1].x == vertices[0].x &&
        vertices[count - 1].y == vertices[0].y &&
        vertices[count - 1].z == vertices[0].z)
        --count;
    return count;
}

/* Base LIB Shell fills do not remove hidden surfaces. Rejecting back faces
 * is sufficient for a convex globe: the retained facets overlap only at
 * edges. LibShell performs all vertex transforms, projection, clipping and
 * polygon filling. A facet faces the eye when its normal points toward the
 * eye's position, seen from one of its vertices. */
static int mtx_float_visible(const struct V3coord *vertices)
{
    float ax, ay, az, bx, by, bz;
    float nx, ny, nz, wx, wy, wz;
    ax = vertices[1].x - vertices[0].x;
    ay = vertices[1].y - vertices[0].y;
    az = vertices[1].z - vertices[0].z;
    bx = vertices[2].x - vertices[0].x;
    by = vertices[2].y - vertices[0].y;
    bz = vertices[2].z - vertices[0].z;
    nx = ay * bz - az * by;
    ny = az * bx - ax * bz;
    nz = ax * by - ay * bx;
    wx = mtx_eye_x - vertices[0].x;
    wy = mtx_eye_y - vertices[0].y;
    wz = mtx_eye_z - vertices[0].z;
    return !(nx * wx + ny * wy + nz * wz <= 0.0f);
}

/* The same facing test without floating point, in object space: n.(e - p0)
 * with Q4 vertex coordinates p and the Q4 integer eye e. Return 1 or -1
 * only when that sign must equal the float test's; 0 asks the caller to
 * run the float test.
 *   In Q4 units, each p is within 1 of its float vertex (truncation) and e
 * within 7 of the exact eye (Q14 last row within 2/16384 of the exact
 * direction, times 48000, plus truncation), so the edges u = p1 - p0 and
 * v = p2 - p0 are within 2 of the exact edges and w = e - p0 within 8.
 * Each component of n = u x v (Q8) is then within twice the sum of the four
 * edge components it uses, plus 8, so n.w (Q12) is within
 * (4(|u|_1 + |v|_1) + 24)(|w|_inf + 8) + 8|n|_1 of the exact product. T is that product in world units cubed,
 * from n / 256 and w / 16, which adds below |w|_1/16 + |n|_1/256 + 3. The
 * float test's own rounding, about 12 operations of 2^-24 each on terms
 * bounded by |u|_1 |v|_1 |w|_1 in world units, stays below a quarter of
 * (|u|_1 + 6)(|v|_1 + 6)(|w|_1 + 24) / 2^30. When |T| exceeds the sum, the
 * exact facing, and so the float test, has T's sign. The bounds' factors
 * are rounded up, and every product fits a 32-bit long. */
static int mtx_integer_facing(const Vertex TG_HUGE *kept[4])
{
    Soft3d_fixed_point p[3];
    long u[3], v[3], n[3], w[3], u1, v1, n1, w1, wmax, t, bound;
    unsigned int i;
    for (i = 0; i < 3U; ++i)
        if (!soft3d_fixed_vertex(kept[i], &p[i])) return 0;
    u[0] = p[1].x - p[0].x; u[1] = p[1].y - p[0].y; u[2] = p[1].z - p[0].z;
    v[0] = p[2].x - p[0].x; v[1] = p[2].y - p[0].y; v[2] = p[2].z - p[0].z;
    w[0] = mtx_eye_fixed[0] - p[0].x;
    w[1] = mtx_eye_fixed[1] - p[0].y;
    w[2] = mtx_eye_fixed[2] - p[0].z;
    u1 = v1 = w1 = wmax = 0;
    for (i = 0; i < 3U; ++i)
    {
        /* 256-unit edges keep n within 2^25 and T below 2^31. */
        if (labs(u[i]) > 4096L || labs(v[i]) > 4096L) return 0;
        u1 += labs(u[i]);
        v1 += labs(v[i]);
        w1 += labs(w[i]);
        if (labs(w[i]) > wmax) wmax = labs(w[i]);
    }
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    n1 = labs(n[0]) + labs(n[1]) + labs(n[2]);
    t = 0;
    for (i = 0; i < 3U; ++i) t += (n[i] / 256L) * (w[i] / 16L);
    bound = w1 / 16L + 1L + n1 / 256L + 1L + 3L +
            ((4L * (u1 + v1) + 24L) / 64L + 1L) * ((wmax + 8L) / 64L + 1L) +
            n1 / 512L + 1L +
            ((u1 + 6L) / 1024L + 1L) * ((v1 + 6L) / 1024L + 1L) *
            ((w1 + 24L) / 1024L + 1L);
    if (t > bound) return 1;
    if (t < -bound) return -1;
    return 0;
}

/* Native LibShell 3D: world vertices of one quad, in traversal order. */
static void mtx_draw_native(const Vertex TG_HUGE *source[4],
                            unsigned char color)
{
    struct V3coord vertices[4];
    const Vertex TG_HUGE *kept[4];
    unsigned short count;
    int facing = 0;
    count = mtx_compact(source, vertices, kept);
    if (count < 3) return;
    if (mtx_integer_cull) facing = mtx_integer_facing(kept);
    if (facing < 0 || (facing == 0 && !mtx_float_visible(vertices))) return;
    if (mtx_pgc)
    {
        /* FIFO flow control replaces grouped inquiries; the frame's final
         * synchronization reports any error. */
        (void)mtx_set_color((unsigned short)color);
        pgc_poly3(count, kept);
        ++globe_quads_num_vis;
        return;
    }
    /* Color (4) and four 16.16 vertices (52); see MTX_FILL_PACKETS. */
    if (!mtx_fill_room(TRUE)) return;
    (void)mtx_set_color((unsigned short)color);
    v3arwplygn(count, vertices);
    mtx_fill_sent(TRUE);
}

/* Software mode: the host transforms, projects, clips and culls with the
 * shared perspective pipeline; LibShell fills screen polygons. */
static void mtx_draw_screen(const Soft3d_screen_point *points, int count,
                            unsigned char color)
{
    struct Phcoord polygon[SOFT3D_MAX_POLY];
    Soft3d_screen_point lower_left[SOFT3D_MAX_POLY];
    int i;
    if (mtx_pgc)
    {
        /* PGC pixel windows count up from the bottom of the screen. */
        for (i = 0; i < count; ++i)
        {
            lower_left[i].x = points[i].x;
            lower_left[i].y = DRV_WIN_HEIGHT - 1L - points[i].y;
        }
        (void)mtx_set_color((unsigned short)color);
        pgc_poly((unsigned int)count, lower_left);
        ++globe_quads_num_vis;
        return;
    }
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
    soft3d_project_cache_reset(&mtx_cache);
    drv_abort_frame(message);
}

/* Each packet vertex is transformed once; with a stream's row reuse, the
 * previous band's lower row is neither transformed nor projected again.
 * The cache projects each remaining vertex the first time a quad uses it. */
static int mtx_software_points(unsigned int vertex_count,
                               const Vertex TG_HUGE *vertices,
                               unsigned int reuse)
{
    int transformed;
    switch (drv_soft3d_kernel)
    {
    case DRV_KERNEL_FLOAT:
        transformed = soft3d_float_transform_reuse(mtx_matrix.floating,
            vertex_count, vertices, mtx_points.floating, reuse, SOFT3D_FLOAT_SINGLE);
        break;
    case DRV_KERNEL_386:
        transformed = soft3d_386_transform_reuse(mtx_matrix.fixed,
            vertex_count, vertices, mtx_points.fixed, reuse);
        break;
    default:
        transformed = soft3d_fixed_transform_reuse(mtx_matrix.fixed,
            vertex_count, vertices, mtx_points.fixed, reuse);
        break;
    }
    if (!transformed)
    {
        mtx_software_failed("Matrox software coordinate is out of range");
        return FALSE;
    }
    (void)soft3d_project_cache_begin(&mtx_cache, vertex_count,
                                     DRV_WIN_WIDTH, DRV_WIN_HEIGHT, reuse);
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
    unsigned int corners[4];
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
        for (j = 0; j < 4U; ++j) corners[j] = indices[4U * i + j];
        if (drv_soft3d_kernel == DRV_KERNEL_FLOAT)
            count = soft3d_float_project_cached(mtx_points.floating, vertex_count,
                corners, 4U, DRV_WIN_WIDTH, DRV_WIN_HEIGHT, SOFT3D_FLOAT_SINGLE,
                &mtx_cache, screen);
        else
            count = soft3d_fixed_project_cached(mtx_points.fixed, vertex_count,
                corners, 4U, DRV_WIN_WIDTH, DRV_WIN_HEIGHT, &mtx_cache, screen);
        /* LibShell screen fills take pixels 0..639 and 0..479. */
        count = soft3d_finish_polygon(screen, count, DRV_WIN_WIDTH - 1U,
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
        if (draw && mtx_pgc)
        {
            pgc_move(8.0, (double)(DRV_WIN_HEIGHT - 1 - (y + 16 * (int)lines)));
            pgc_text(line);
        }
        else if (draw) MTX_COMMAND(sctxbstring(8, (short)(y + 16U * lines), line), "OSD text");
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
    if (mtx_pgc) pgc_window(0.0, DRV_WIN_WIDTH - 1.0, 0.0, DRV_WIN_HEIGHT - 1.0);
    if (mtx_set_color(mtx_foreground) && !mtx_pgc)
        (void)mtx_checkpoint("text color", FALSE);
    lines = mtx_text_lines(drv_help, 0, FALSE);
    (void)mtx_text_lines(drv_help, (short)(DRV_WIN_HEIGHT - 4U - 16U * lines), TRUE);
    drv_format_stats(status);
    drv_append_soft3d_status(status, sizeof(status), mtx_pgc ? "PGC" : "LibShell");
    (void)mtx_text_lines(status, 8, TRUE);
    if (mtx_pgc) (void)mtx_pgc_sync("OSD text", FALSE);
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
        soft3d_project_cache_reset(&mtx_cache);
        if (mtx_pgc) pgc_clears((unsigned char)mtx_background);
        else MTX_COMMAND(krhwcclr(mtx_background), "clear screen");
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
                /* LibShell transforms world quads itself; copying them into
                 * packets would only add host work. */
                if (drv_soft3d_active) globe_draw_quadbatches();
                else globe_draw_quads();
                mtx_queue_fills = FALSE;
                if (mtx_pgc)
                    (void)mtx_pgc_sync(drv_soft3d_active ? "screen polygon fill" :
                                       "polygon fill", !drv_soft3d_active);
                else (void)mtx_flush_fills();
            }
            else if (!drv_frame_failed) drv_abort_frame("Matrox software rotation failed");
        }
        if (drv_frame_failed)
        {
            drv_reset_timing();
            if (mtx_native_failed && attempt == 0) continue;
            if (mtx_pgc) pgc_clears((unsigned char)mtx_background);
            else MTX_COMMAND(krhwcclr(mtx_background), "clear failed frame");
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
            else drv_notice(mtx_pgc ? "Matrox software setup failed; using PGC transforms" :
                            "Matrox software setup failed; using LibShell");
        }
        else if (mtx_native_projection())
        {
            drv_soft3d_active = FALSE;
            drv_notice(NULL);
        }
        drv_quadstream_reset(&mtx_stream);
        soft3d_project_cache_reset(&mtx_cache);
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
