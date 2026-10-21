/*
 * testglobe - 16-bit Windows 3.1 GDI / Microsoft C 7 huge-model driver.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef drv_gdi_h
#define drv_gdi_h

#ifndef SEG86
#error Windows GDI requires the SEG86 memory model
#endif
#ifndef DRV
#define DRV
#endif
#define GDI

#define DRV_NAME "Windows GDI"
#define DRV_HELP "1-5 = facets, A = auto rotate, O = OSD, Esc = exit"
#define DRV_WIN_X 20
#define DRV_WIN_Y 20
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_COLORMAPPED TRUE
#define DRV_COLORMAP
#define DRV_EARTH_RESOURCES
#define DRV_OSD_DEFAULT TRUE
#define DRV_VISIBLE_COUNTS
#define DRV_DEPTH 8
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

/* One indexed quad per Earth patch; native batches group by palette byte. */
#define QUAD
#define DRV_QUAD_BATCH
#define DRV_QUAD_BATCH_MAX 64

extern void gdi_error(char *message);

#endif /* drv_gdi_h */
