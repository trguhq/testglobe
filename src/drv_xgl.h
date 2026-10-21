/*
 * testglobe - Sun XGL 3.x driver for 32-bit SPARC / Solaris 2.6.
 * Copyright (C) 2024-2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef drv_xgl_h
#define drv_xgl_h

#ifndef FLAT
#error Sun XGL requires the FLAT memory model
#endif
#ifndef DRV
#define DRV
#endif

#define DRV_NAME "Sun XGL"
#define DRV_HELP "1-5 = facets, A = auto rotate, O = OSD, T = texture mapping"
#define DRV_WIN_X 100
#define DRV_WIN_Y 100
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_COLORMAPPED FALSE
#define DRV_TEXTURE TRUE
#define DRV_TEXTURE_DEFAULT FALSE
#define DRV_OSD_DEFAULT TRUE
#define DRV_DEPTH 24
#define DRV_COLORMAPDEPTH 24
#define Color Color_RGB
#define DRV_FPS_HISTORY 60
#define DRV_FPS_DEFAULT TRUE
#define DRV_ROTATE 10.0f
#define GLOBE_DEFAULT_LONGITUDE 512

#define EARTH
#define EARTH_32
#define EARTH_64
#define EARTH_128
#define EARTH_256
#define EARTH_512

#define TEXTURE
#define TEXTURE_DEFAULT_SIZE 512
#define TEXTURE_RGB
/* render tristrips */
#define TRISTRIP

#endif /* drv_xgl_h */
