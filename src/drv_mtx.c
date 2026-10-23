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
static unsigned short mtx_previous_lut;
static unsigned short mtx_background;
static unsigned short mtx_foreground;
static struct Lutval mtx_saved_palette[256];
static struct Lutval mtx_palette[256];
/* Only the visibility test uses this object-space orthographic eye vector.
 * LibShell transforms, clips and rasterizes every retained polygon. */
static float mtx_eye_x;
static float mtx_eye_y;
static float mtx_eye_z = 1.0f;

/* LSERROR stalls the shell before a second unread error. A blocking
 * lslibsync() cannot service that register, so use a counted LSINQ reply
 * as a completion marker while polling the SDK's error and Read FIFO APIs.
 * Packets contain a small command or color/polygon pair and a four-byte
 * inquiry; LUT writes and text are bounded so even a legacy 256-byte
 * command FIFO cannot fill before error servicing resumes. */
#define MTX_WAIT_TICKS (10UL * (unsigned long)CLOCKS_PER_SEC)

static int mtx_failure(const char *operation, unsigned short errors,
                        int communication)
{
    char message[128];
    const char *reason;
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
    if (mtx_cleanup_active) fprintf(stderr, "%s\n", message);
    else error(message);
    return FALSE;
}

static int mtx_read_word(unsigned short *word, unsigned short *errors,
                         const char *operation, clock_t start)
{
    clock_t now;
    for (;;)
    {
        /* One deadline covers the entire reply, including immediately
         * available words in a malformed, unexpectedly long reply. */
        now = clock();
        if (start == (clock_t)-1 || now == (clock_t)-1 ||
            (unsigned long)now - (unsigned long)start >= MTX_WAIT_TICKS)
            return mtx_failure(operation, *errors, TRUE);
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
    }
}

static int mtx_inquire(unsigned short code, unsigned short *value,
                        const char *operation, char *invalid_reply)
{
    unsigned short remaining, word, errors = 0;
    clock_t start = clock();
    int valid;
    lsinq(code);
    ioflush();
    if (!mtx_read_word(&remaining, &errors, operation, start)) return FALSE;
    valid = remaining == 1U;
    /* These two inquiries return exactly one value. Drain short malformed
     * replies for cleanup, but do not trust an unbounded corrupted count. */
    if (remaining > 16U)
    {
        mtx_io_failed = TRUE;
        if (mtx_cleanup_active) fprintf(stderr, "%s\n", invalid_reply);
        else error(invalid_reply);
        return FALSE;
    }
    while (remaining != 0U)
    {
        if (!mtx_read_word(&word, &errors, operation, start)) return FALSE;
        if (valid) *value = word;
        --remaining;
    }
    errors |= (unsigned short)iogeterror();
    if (errors != 0) return mtx_failure(operation, errors, FALSE);
    if (!valid)
    {
        if (mtx_cleanup_active) fprintf(stderr, "%s\n", invalid_reply);
        else error(invalid_reply);
        return FALSE;
    }
    return TRUE;
}

static int mtx_checkpoint(const char *operation)
{
    unsigned short reporting;
    if (mtx_io_failed) return FALSE;
    return mtx_inquire(77, &reporting, operation,
                       "Invalid Matrox error-policy inquiry reply length");
}

#define MTX_COMMAND(command, operation) \
    do { command; (void)mtx_checkpoint(operation); } while (0)

static int mtx_read_words(short *words, unsigned short count,
                           const char *operation)
{
    unsigned short word, errors = 0;
    clock_t start = clock();
    ioflush();
    while (count != 0U)
    {
        if (!mtx_read_word(&word, &errors, operation, start)) return FALSE;
        *words++ = (short)word;
        --count;
    }
    errors |= (unsigned short)iogeterror();
    if (errors != 0) return mtx_failure(operation, errors, FALSE);
    return TRUE;
}

static int mtx_upload_palette(struct Lutval *palette)
{
    unsigned short first;
    for (first = 0; first < 256U; first += 16U)
    {
        lslutmd(0, first, 16, palette + first);
        if (!mtx_checkpoint("palette upload")) return FALSE;
    }
    return TRUE;
}

static void mtx_cleanup(void)
{
    if (!mtx_initialized)
        return;
    mtx_cleanup_active = TRUE;
    if (!mtx_io_failed && mtx_palette_saved)
    {
        (void)mtx_upload_palette(mtx_saved_palette);
    }
    if (!mtx_io_failed && mtx_palette_saved)
    {
        lslutsl(mtx_previous_lut, 0);
        (void)mtx_checkpoint("restore palette selection");
    }
    if (!mtx_io_failed)
    {
        lserror(mtx_error_policy_saved ? mtx_previous_error_reporting : 0);
        (void)mtx_checkpoint("restore error reporting");
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
    globe_free();
    /* atexit also restores the board after shared allocation failures. */
    exit(0);
}

static void mtx_init_mouse(void)
{
    union REGS regs;
    mtx_mouse_present = FALSE;
    /* Some DOS installations leave INT33h null; others point to IRET. */
    if (_dos_getvect(0x33) == NULL)
        return;
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0;
    int86(0x33, &regs, &regs);
    if (regs.x.ax == 0xffff)
    {
        mtx_mouse_present = TRUE;
        regs.x.ax = 0x0b;
        int86(0x33, &regs, &regs); /* Discard motion accumulated at startup. */
    }
}

static unsigned short mtx_inquire_lut(void)
{
    unsigned short value = 0;
    (void)mtx_inquire(78, &value, "LUT inquiry",
                      "Invalid Matrox LUT inquiry reply length");
    return value;
}

void drv_init(int *argc, char **argv)
{
    short system_info[10];
    unsigned short reporting;
    (void)argc;
    (void)argv;
    lsinit(0, 1);
    ioflush();
    mtx_initialized = TRUE;
    if (atexit(mtx_cleanup) != 0)
    {
        mtx_cleanup();
        error("Cannot register Matrox cleanup");
    }
    if (!mtx_inquire(77, &reporting, "error-policy inquiry",
                     "Invalid Matrox error-policy inquiry reply length")) return;
    mtx_previous_error_reporting = reporting != 0;
    mtx_error_policy_saved = TRUE;
    MTX_COMMAND(lserror(1), "enable error reporting");
    lsinqsys();
    if (!mtx_read_words(system_info, 10, "system inquiry")) return;
    if ((unsigned short)system_info[0] < DRV_WIN_WIDTH ||
        (unsigned short)system_info[1] < DRV_WIN_HEIGHT ||
        (unsigned short)system_info[2] < 256)
        error("Matrox LibShell needs a 640x480, 256-color display");
    /* Preserve the table replaced and the currently displayed LUT label. */
    mtx_previous_lut = mtx_inquire_lut();
    lslutrd(0, 0, 256);
    if (!mtx_read_words((short *)mtx_saved_palette, 256 * 3,
                        "palette inquiry")) return;
    mtx_palette_saved = TRUE;
    mtx_init_mouse();
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
    /* 4:3 aspect, 400-pixel globe diameter, room for the status display. */
    MTX_COMMAND(v2gpswdw(-1600.0, 1600.0, -1200.0, 1200.0), "world window");
    MTX_COMMAND(v3gpsproj(0), "projection");                /* Documented orthographic projection. */
    MTX_COMMAND(v3gpsdist(3000.0), "view distance");
    MTX_COMMAND(v3gpshclip(0), "hither clipping");
    MTX_COMMAND(v3gpsyclip(0), "yon clipping");
    MTX_COMMAND(krhwsbop(0), "drawing operation");                 /* REPLACE, not SET-all-ones. */
    MTX_COMMAND(krhwsmsk(0xff), "plane mask");
    MTX_COMMAND(krhwstdef(0), "transparency");
    MTX_COMMAND(lssafapat(-1), "solid fill");               /* Solid fill, no board-specific shading. */
    MTX_COMMAND(krgcdi(), "graphics cursor");
    MTX_COMMAND(lssfont(-4), "font selection");                 /* Documented 640x480, 8x16 raster font. */
    MTX_COMMAND(sctxsalign(0, 0), "text alignment");
    MTX_COMMAND(sctxsdmode(0), "text mode");               /* Text must not enqueue read-FIFO replies. */
    MTX_COMMAND(sctxssf(1.0, 1.0), "text scale");
    mtx_dirty = TRUE;
    return 1;
}

static short mtx_angle(float angle)
{
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return (short)angle;
}

static void mtx_set_transform(void)
{
    short yaw = mtx_angle(drv_rot_y);
    short pitch = mtx_angle(90.0f + drv_rot_x);
    double yaw_radians = (double)yaw * PI / 180.0;
    double pitch_radians = (double)pitch * PI / 180.0;
    double sin_pitch = sin(pitch_radians);
    /* A single rotation in each stage avoids undocumented cumulative
     * matrix ordering. Globe longitude runs around its Z axis. The view
     * command moves the viewer about X around the reference point. */
    MTX_COMMAND(v3mdid(), "model identity");
    MTX_COMMAND(v3mdorg(0.0, 0.0, 0.0), "model origin");
    MTX_COMMAND(v3mdrotz(yaw), "model rotation");
    MTX_COMMAND(v3vwid(), "view identity");
    MTX_COMMAND(v3vwrpt(0.0, 0.0, 0.0), "view reference");
    MTX_COMMAND(v3vwrotx(pitch), "view rotation");
    /* Inverse model yaw applied to the viewer's world-space direction.
     * Use exactly the integer-degree angles sent to LibShell. */
    mtx_eye_x = (float)(-sin(yaw_radians) * sin_pitch);
    mtx_eye_y = (float)(-cos(yaw_radians) * sin_pitch);
    mtx_eye_z = (float)cos(pitch_radians);
}

static void mtx_draw_flat_color(unsigned short npoints,
                                Vertex TG_HUGE *source[],
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
    for (i = 0; i < npoints; ++i)
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
    /* Color (4), four 16.16 vertices (52), inquiry (4): 60 bytes total. */
    krhwsfcol((unsigned short)color);
    v3arwplygn(count, vertices);
    (void)mtx_checkpoint("polygon fill");
    if (npoints == 3)
        ++globe_tris_num_vis;
    else
        ++globe_quads_num_vis;
}

void drv_draw_tri_flat_color(Vertex TG_HUGE *xyz1,
                             Vertex TG_HUGE *xyz2,
                             Vertex TG_HUGE *xyz3,
                             unsigned char color)
{
    Vertex TG_HUGE *source[3];
    if (xyz1 == NULL || xyz2 == NULL || xyz3 == NULL) return;
    source[0] = xyz1;
    source[1] = xyz2;
    source[2] = xyz3;
    mtx_draw_flat_color(3, source, color);
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *xyz1,
                              Vertex TG_HUGE *xyz2,
                              Vertex TG_HUGE *xyz3,
                              Vertex TG_HUGE *xyz4,
                              unsigned char color)
{
    Vertex TG_HUGE *source[4];
    if (xyz1 == NULL || xyz2 == NULL || xyz3 == NULL || xyz4 == NULL) return;
    source[0] = xyz1;
    source[1] = xyz2;
    source[2] = xyz3;
    source[3] = xyz4;
    mtx_draw_flat_color(4, source, color);
}

static void mtx_draw_text(short x, short y, char *text)
{
    char bounded[120];
    /* SCTXBSTRING expands bytes into 16-bit SCTXSTRING characters. With
     * its eight-byte header and the inquiry, 119 characters use 250 bytes.
     * Text beyond this limit is already outside the 640-pixel display. */
    strncpy(bounded, text, sizeof(bounded) - 1);
    bounded[sizeof(bounded) - 1] = '\0';
    MTX_COMMAND(sctxbstring(x, y, bounded), "OSD text");
}

void drv_draw_osd(void)
{
    char status[128];
    MTX_COMMAND(krhwsfcol(mtx_foreground), "text color");
    mtx_draw_text(8, DRV_WIN_HEIGHT - 20, drv_help);
    drv_format_stats(status);
    mtx_draw_text(8, 8, status);
}

void drv_render(void)
{
    drv_fps_start();
    MTX_COMMAND(krhwcclr(mtx_background), "clear screen");
    mtx_set_transform();
    globe_quads_num_vis = 0;
    globe_draw_quads();
    if (drv_osd_enabled) drv_draw_osd();
    /* Every completion marker includes actual board execution in FPS. */
    drv_fps_stop();
    if (drv_auto_enabled) drv_globe_rotate();
}

static void mtx_keyboard(unsigned char key)
{
    if (key == 27) { drv_close(); return; }
    if (drv_handle_key(key)) mtx_dirty = TRUE;
}

static void mtx_poll_mouse(void)
{
    union REGS regs;
    short dx, dy;
    int right_pressed;
    if (!mtx_mouse_present) return;
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 3;
    int86(0x33, &regs, &regs);
    drv_lmouse_pressed = (regs.x.bx & 1) != 0;
    right_pressed = (regs.x.bx & 2) != 0;
    if (drv_rmouse_pressed != right_pressed) mtx_dirty = TRUE;
    drv_rmouse_pressed = right_pressed;
    /* Relative mickeys avoid VGA/CGA coordinate ranges and clipping when
     * the mouse controls an independent Matrox display. */
    regs.x.ax = 0x0b;
    int86(0x33, &regs, &regs);
    dx = (short)regs.x.cx;
    dy = (short)regs.x.dx;
    if (drv_lmouse_pressed && (dx != 0 || dy != 0))
    {
        drv_rotate_drag((long)dx, (long)dy);
        mtx_dirty = TRUE;
    }
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
        mtx_poll_mouse();
        if (mtx_dirty || drv_auto_enabled)
        {
            drv_render();
            mtx_dirty = FALSE;
        }
    }
}
#endif /* DRV_MTX */
