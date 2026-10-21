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

static void mtx_cleanup(void)
{
    if (!mtx_initialized)
        return;
    if (mtx_palette_saved)
    {
        lslutmd(0, 0, 256, mtx_saved_palette);
        lslutsl(mtx_previous_lut, 0);
    }
    ioflush();
    lslibsync();
    lsemulen(1);
    ioflush();
    mtx_initialized = FALSE;
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
    unsigned short remaining;
    lsinq(78);
    ioflush();
    /* LSINQ replies start with the number of following data words, unlike
     * the fixed-size LSINQSYS and LSLUTRD replies. Read both header and LUT. */
    remaining = (unsigned short)iogetw();
    if (remaining != 1U)
    {
        /* Leave the Read FIFO synchronized before atexit restores the board. */
        while (remaining != 0U)
        {
            (void)iogetw();
            --remaining;
        }
        error("Invalid Matrox LUT inquiry reply length");
        return 0;
    }
    return (unsigned short)iogetw();
}

void drv_init(int *argc, char **argv)
{
    short system_info[10];
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
    lsinqsys();
    ioflush();
    iogetbuf(system_info, 10);
    if ((unsigned short)system_info[0] < DRV_WIN_WIDTH ||
        (unsigned short)system_info[1] < DRV_WIN_HEIGHT ||
        (unsigned short)system_info[2] < 256)
        error("Matrox LibShell needs a 640x480, 256-color display");
    /* Preserve the table replaced and the currently displayed LUT label. */
    mtx_previous_lut = mtx_inquire_lut();
    lslutrd(0, 0, 256);
    ioflush();
    iogetbuf((short *)mtx_saved_palette, 256 * 3);
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
    lslutmd(0, 0, 256, mtx_palette);
    lslutsl(0, 0);
    lslutpm(0, 0xff);
    lssbm(-1);
    lssclpl(-1);
    scsorg(0, 0);
    v2gpsvwp(0, DRV_WIN_WIDTH - 1, 0, DRV_WIN_HEIGHT - 1);
    /* 4:3 aspect, 400-pixel globe diameter, room for the status display. */
    v2gpswdw(-1600.0, 1600.0, -1200.0, 1200.0);
    v3gpsproj(0);                /* Documented orthographic projection. */
    v3gpsdist(3000.0);
    v3gpshclip(0);
    v3gpsyclip(0);
    krhwsbop(0);                 /* REPLACE, not SET-all-ones. */
    krhwsmsk(0xff);
    krhwstdef(0);
    lssafapat(-1);               /* Solid fill, no board-specific shading. */
    krgcdi();
    lssfont(-4);                 /* Documented 640x480, 8x16 raster font. */
    sctxsalign(0, 0);
    sctxsdmode(0);               /* Text must not enqueue read-FIFO replies. */
    sctxssf(1.0, 1.0);
    ioflush();
    lslibsync();
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
    v3mdid();
    v3mdorg(0.0, 0.0, 0.0);
    v3mdrotz(yaw);
    v3vwid();
    v3vwrpt(0.0, 0.0, 0.0);
    v3vwrotx(pitch);
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
    krhwsfcol((unsigned short)color);
    v3arwplygn(count, vertices);
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
    source[0] = xyz1;
    source[1] = xyz2;
    source[2] = xyz3;
    source[3] = xyz4;
    mtx_draw_flat_color(4, source, color);
}

void drv_draw_osd(void)
{
    char status[128];
    krhwsfcol(mtx_foreground);
    sctxbstring(8, DRV_WIN_HEIGHT - 20, drv_help);
    drv_format_stats(status);
    sctxbstring(8, 8, status);
}

void drv_render(void)
{
    drv_fps_start();
    krhwcclr(mtx_background);
    mtx_set_transform();
    globe_quads_num_vis = 0;
    globe_draw_quads();
    if (drv_osd_enabled) drv_draw_osd();
    ioflush();
    lslibsync();                 /* FPS includes actual board execution. */
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
    if (!mtx_mouse_present) return;
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 3;
    int86(0x33, &regs, &regs);
    drv_lmouse_pressed = (regs.x.bx & 1) != 0;
    drv_rmouse_pressed = (regs.x.bx & 2) != 0;
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
