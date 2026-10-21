/*
 * testglobe - Matrox LibShell driver for 16-bit Microsoft C 7 /AH.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef drv_mtx_h
#define drv_mtx_h
#if !defined(SEG86)
#error Matrox LibShell requires SEG86
#endif
#ifndef DRV
#define DRV
#endif
#define DRV_NAME "Matrox LibShell"
#define DRV_HELP "1-2 = facets, A = auto rotate, O = OSD, Esc = exit"
#define DRV_WIN_X 0
#define DRV_WIN_Y 0
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_COLORMAPPED TRUE
#define DRV_COLORMAP
#define DRV_TEXTURE FALSE
#define DRV_TEXTURE_DEFAULT FALSE
#define DRV_OSD_DEFAULT TRUE
#define DRV_VISIBLE_COUNTS
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

/* No TEXTURE: the palette supplies polygon colors, not texture mapping. */
/* render individual quads */
#define QUAD
/* The indexed triangle leaf is also available to auxiliary callers. */
#define DRV_TRI_COLOR

#endif /* drv_mtx_h */
