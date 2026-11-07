/*
 * testglobe - TIGA 2.x / Microsoft C 7 huge-model DOS driver.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef drv_tga_h
#define drv_tga_h

#ifndef SEG86
#error TIGA requires the SEG86 memory model
#endif
#ifndef DRV
#define DRV
#endif
#include "soft3d.h"
#define DRV_MAX_RADIUS SOFT3D_MAX_RADIUS
#define DRV_NAME "TI TIGA"
#define DRV_HELP "1-2 = facets, A = rotate, F = FPS, O = OSD, Esc = exit, S = soft3d"
#define DRV_WIN_X 0
#define DRV_WIN_Y 0
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_COLORMAPPED TRUE
#define DRV_COLORMAP
#define DRV_OSD_DEFAULT TRUE
#define DRV_VISIBLE_COUNTS
#define DRV_SOFT3D
#define DRV_SOFT3D_DEFAULT TRUE
#define DRV_DEPTH 8
#define DRV_COLORMAPDEPTH 8
#define Color Color_Index
#define DRV_FPS_HISTORY 60
#define DRV_FPS_DEFAULT TRUE
#define DRV_ROTATE 10.0f
#define EARTH
#define EARTH_32
#define EARTH_64
#define GLOBE_DEFAULT_LONGITUDE 32

/* No texture capability: the palette byte is the polygon's flat color. */
#define QUAD
#define DRV_QUAD_BATCH
#define DRV_QUAD_BATCH_MAX 64

#endif /* drv_tga_h */
