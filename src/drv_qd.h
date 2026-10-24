/*
 * testglobe - System 7 QuickDraw / MPW 68000 driver.
 * Copyright (C) 2024-2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef drv_qd_h
#define drv_qd_h
#ifndef SEG68
#error Classic QuickDraw requires the SEG68 memory model
#endif
#ifndef DRV
#define DRV
#endif
#include "soft3d.h"
/* Both FPU and integer paths must accept the same scene domain. */
#define DRV_MAX_RADIUS SOFT3D_MAX_RADIUS
#define DRV_NO_CONSOLE

#define DRV_NAME "QuickDraw"
#define DRV_HELP "1-5 = facets, A = auto rotate, O = OSD, F = FPS, Esc/Cmd-Q = quit"
#define DRV_WIN_X 20
#define DRV_WIN_Y 40
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_COLORMAPPED TRUE
#define DRV_COLORMAP
#define DRV_OSD_DEFAULT TRUE
#define DRV_VISIBLE_COUNTS
#define DRV_DEPTH 1
#define DRV_COLORMAPDEPTH 8
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
#define DRV_EARTH_RESOURCES
/* Bounded indexed packets retain shared vertices; QuickDraw fills each quad. */
#define QUAD
#define DRV_QUAD_BATCH
#define DRV_QUAD_BATCH_MAX 64
extern void qd_error(char *message);
#endif /* drv_qd_h */
