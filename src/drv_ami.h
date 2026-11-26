/*
 * testglobe - Amiga Workbench / VBCC 68000 driver.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef drv_ami_h
#define drv_ami_h

#ifndef FLAT
#error Amiga requires the FLAT memory model
#endif
#ifndef DRV
#define DRV
#endif
#include "soft3d.h"

/* The same executable optionally detects RTG; 0 builds native chipset only. */
#ifndef AMI_RTG
#define AMI_RTG 1
#endif
#if AMI_RTG != 0 && AMI_RTG != 1
#error AMI_RTG must be 0 or 1
#endif

#define DRV_NAME "Amiga"
#define DRV_HELP "1-5 detail, A rotate, O OSD, P FPS, Esc exit, D dither, F fullscreen"
#define DRV_WIN_X 20
#define DRV_WIN_Y 20
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_NO_CONSOLE
#define DRV_MAX_RADIUS SOFT3D_MAX_RADIUS
#define DRV_COLORMAPPED TRUE
#define DRV_COLORMAP
#define DRV_PALETTE_DITHER
#define DRV_EARTH_RESOURCES
#define DRV_OSD_DEFAULT TRUE
#define AMI_DITHER_DEFAULT TRUE
#define DRV_VISIBLE_COUNTS
#define DRV_DEPTH 5
#define DRV_COLORMAPDEPTH 5
#define Color Color_Index
#define DRV_FPS_HISTORY 60
#define DRV_FPS_DEFAULT TRUE
#define DRV_ROTATE 10.0f
#define GLOBE_DEFAULT_LONGITUDE 32
#define EARTH
#define EARTH_32
#define EARTH_64
#define EARTH_128
#define EARTH_256
#define EARTH_512
#define QUAD
#define DRV_QUAD_BATCH
#define DRV_QUAD_BATCH_MAX 64
/* Optional back-face trimming of quad batches (DRV_QUAD_TRIM, globe.c).
 * Off by default: the margins that keep the drawn polygons identical let it
 * skip under 10% of key-1 quads and almost none above, while each trimmed
 * band costs soft-float trigonometry. Build with -DAMI_QUAD_TRIM=1 to try. */
#ifndef AMI_QUAD_TRIM
#define AMI_QUAD_TRIM 0
#endif
#if AMI_QUAD_TRIM != 0 && AMI_QUAD_TRIM != 1
#error AMI_QUAD_TRIM must be 0 or 1
#endif
#if AMI_QUAD_TRIM
/* Quad batches may omit longitude runs facing away from the camera by more
 * than this driver's integer rounding could reverse. The driver describes
 * its projection: screen = center + focal * (X, Y) / -Z, Y down, clip limits
 * 0..limit, each coordinate rounded by at most pixel_x/y after camera-space
 * vertex errors of world_error + world_scale * radius. */
#define DRV_QUAD_TRIM
typedef struct Drv_quad_trim
{
    double eye[3]; /* camera in mesh coordinates */
    double focal_x, focal_y, center_x, center_y, limit_x, limit_y;
    double pixel_x, pixel_y, world_error, world_scale;
} Drv_quad_trim;
/* Called while a frame draws its quad batches; FALSE draws every quad. */
extern int drv_quad_trim_view(Drv_quad_trim *view);
#endif
/* The kick13 C runtime preserves the stack supplied by Shell/Workbench. */
#define AMI_STACK_BYTES 32768UL
/* Startup code, main and drv_init frames already above the stack probe. */
#define AMI_STACK_STARTUP 1024UL

extern void ami_error(char *message);
extern double ami_time_now(void);
extern Drv_result ami_data_init(int argc, char **argv);
extern void ami_data_close(void);

#endif /* drv_ami_h */
