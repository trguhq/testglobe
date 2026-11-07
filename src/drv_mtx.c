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
static int mtx_transform_scope, mtx_native_failed;
static union {
    long fixed[3][3];
    float floating[3][3];
} mtx_matrix;
static unsigned short mtx_previous_lut;
static unsigned short mtx_background;
static unsigned short mtx_foreground;
static struct Lutval mtx_saved_palette[256];
static struct Lutval mtx_palette[256];
/* The native path uses this eye vector only for visibility rejection.
 * Software mode performs the whole geometry stage before screen filling. */
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
    /* A fully drained, valid inquiry establishes that communication still
     * works. Only optional native geometry setup/polygon packets permit a
     * bounded 2D retry; the latter include their color command. A persistent
     * color/fill error remains fatal at the software packet's checkpoint.
     * Unknown bits, command-list faults, and transport faults stay fatal. */
    if (!communication && !mtx_cleanup_active && mtx_transform_scope &&
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
    if (!valid)
    {
        if (mtx_cleanup_active) fprintf(stderr, "%s\n", invalid_reply);
        else error(invalid_reply);
        return FALSE;
    }
    if (errors != 0) return mtx_failure(operation, errors, FALSE);
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

/* Used only in helpers which return a success flag. Never allow a failed
 * V3 operation to continue submitting the rest of that transformation. */
#define MTX_TRANSFORM(command, operation) \
    do { int mtx_ok; mtx_transform_scope = TRUE; command; \
         mtx_ok = mtx_checkpoint(operation); mtx_transform_scope = FALSE; \
         if (!mtx_ok) return FALSE; } while (0)

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
                      "Invalid Matrox LUT inquiry reply length");
    return value;
}

void drv_init(int *argc, char **argv)
{
    short system_info[10];
    unsigned short reporting;
    (void)argc;
    (void)argv;
    mtx_cpu = drv_cpu_detect();
    mtx_has_fpu = drv_cpu_has_fpu();
    mtx_transform_scope = mtx_native_failed = FALSE;
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
     * command moves the viewer about X around the reference point. */
    MTX_TRANSFORM(v3mdid(), "model identity");
    MTX_TRANSFORM(v3mdorg(0.0, 0.0, 0.0), "model origin");
    MTX_TRANSFORM(v3mdrotz(yaw), "model rotation");
    MTX_TRANSFORM(v3vwid(), "view identity");
    MTX_TRANSFORM(v3vwrpt(0.0, 0.0, 0.0), "view reference");
    MTX_TRANSFORM(v3vwrotx(pitch), "view rotation");
    /* Inverse model yaw applied to the viewer's world-space direction.
     * Use exactly the integer-degree angles sent to LibShell. */
    mtx_eye_x = (float)(-sin(yaw_radians) * sin_pitch);
    mtx_eye_y = (float)(-cos(yaw_radians) * sin_pitch);
    mtx_eye_z = (float)cos(pitch_radians);
    return TRUE;
}

/* Only a triangle/quad packet is retained on the host. Unlike the perspective
 * drivers, Matrox maps its fixed orthographic world window to the screen. */
static void mtx_draw_software_color(unsigned short npoints,
                                    Vertex TG_HUGE *source[],
                                    unsigned char color)
{
    Vertex vertices[4];
    union {
        Soft3d_fixed_point fixed[4];
        Soft3d_float_point floating[4];
    } points;
    Soft3d_screen_point screen[SOFT3D_MAX_POLY];
    struct Phcoord polygon[SOFT3D_MAX_POLY];
    unsigned int i;
    int transformed, count;
    for (i = 0; i < npoints; ++i) vertices[i] = *source[i];
    if (mtx_has_fpu)
        transformed = soft3d_float_transform_reuse(mtx_matrix.floating,
            npoints, vertices, points.floating, 0U, SOFT3D_FLOAT_SINGLE);
    else if (mtx_cpu >= 386)
        transformed = soft3d_386_transform_reuse(mtx_matrix.fixed,
            npoints, vertices, points.fixed, 0U);
    else
        transformed = soft3d_fixed_transform_reuse(mtx_matrix.fixed,
            npoints, vertices, points.fixed, 0U);
    if (!transformed)
    {
        drv_abort_frame("Matrox software coordinate is out of range");
        return;
    }
    if (mtx_has_fpu)
        count = soft3d_float_project_ortho(points.floating, npoints,
                    DRV_WIN_WIDTH, DRV_WIN_HEIGHT, 3200U, 2400U, screen);
    else
        count = soft3d_fixed_project_ortho(points.fixed, npoints,
                    DRV_WIN_WIDTH, DRV_WIN_HEIGHT, 3200U, 2400U, screen);
    if (count == 0)
    {
        drv_abort_frame("Matrox software projection failed");
        return;
    }
    count = soft3d_finish_polygon(screen, count, DRV_WIN_WIDTH - 1U,
                                   DRV_WIN_HEIGHT - 1U);
    if (count < 3) return;
    for (i = 0; i < (unsigned int)count; ++i)
    {
        polygon[i].x = (short)screen[i].x;
        polygon[i].y = (short)screen[i].y;
    }
    krhwsfcol((unsigned short)color);
    scarwplygn((unsigned short)count, polygon);
    if (!mtx_checkpoint("screen polygon fill")) return;
    if (npoints == 3) ++globe_tris_num_vis;
    else ++globe_quads_num_vis;
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
    int completed;
    if (drv_frame_failed || mtx_io_failed) return;
    if (drv_soft3d_active)
    {
        mtx_draw_software_color(npoints, source, color);
        return;
    }
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
    mtx_transform_scope = TRUE;
    v3arwplygn(count, vertices);
    completed = mtx_checkpoint("polygon fill");
    mtx_transform_scope = FALSE;
    if (!completed) return;
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

/* The selected font is 8x16. Wrap both status and help to the actual screen
 * width, in addition to respecting the smaller command FIFO's text limit. */
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
        if (draw) mtx_draw_text(8, (short)(y + 16U * lines), line);
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
    MTX_COMMAND(krhwsfcol(mtx_foreground), "text color");
    lines = mtx_text_lines(drv_help, 0, FALSE);
    (void)mtx_text_lines(drv_help, (short)(DRV_WIN_HEIGHT - 4U - 16U * lines), TRUE);
    drv_format_stats(status);
    drv_append_soft3d_status(status, sizeof(status), "LibShell", mtx_cpu, mtx_has_fpu);
    (void)mtx_text_lines(status, 8, TRUE);
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
        MTX_COMMAND(krhwcclr(mtx_background), "clear screen");
        globe_quads_num_vis = 0;
        if (globe_ready())
        {
            prepared = mtx_set_transform();
            if (prepared) globe_draw_quads();
            else if (!drv_frame_failed) drv_abort_frame("Matrox software rotation failed");
        }
        if (drv_frame_failed)
        {
            drv_reset_timing();
            if (mtx_native_failed && attempt == 0) continue;
            MTX_COMMAND(krhwcclr(mtx_background), "clear failed frame");
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
        mtx_poll_mouse();
        if (mtx_dirty || drv_animation_allowed())
        {
            drv_render();
            mtx_dirty = FALSE;
        }
    }
}
#endif /* DRV_MTX */
