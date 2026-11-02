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
#define DRV_HELP "1-5 detail, A rotate, O OSD, F FPS, Esc exit, D dither"
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
/* The kick13 C runtime preserves the stack supplied by Shell/Workbench. */
#define AMI_STACK_BYTES 32768UL

extern void ami_error(char *message);
extern double ami_time_now(void);
extern Drv_result ami_data_init(int argc, char **argv);
extern void ami_data_close(void);

#endif /* drv_ami_h */
