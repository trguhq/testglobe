/*
 * testglobe - display 3d globe to test 3d hardware
 * drv_igl.h - IRIS GL driver
 *
 * Copyright (C) 2024 trguhq
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef drv_igl_h
#define drv_igl_h

#if !defined(FLAT)
#error IRIS GL requires the FLAT build option
#endif

#ifndef DRV
#define DRV
#endif

#define DRV_NAME "IRIS GL"
#define DRV_HELP "1-5 = facets, A = auto rotate, O = OSD, T = texture mapping, Esc = exit"

#define DRV_WIN_X 100
#define DRV_WIN_Y 100
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_COLORMAPPED FALSE
#define DRV_COLORMAP
#define DRV_TEXTURE TRUE
#define DRV_TEXTURE_DEFAULT FALSE
#define DRV_OSD_DEFAULT TRUE
#define DRV_DEPTH 24
#define DRV_COLORMAPDEPTH 24
#define Color Color_Index
#define DRV_FPS_HISTORY 60
#define DRV_FPS_DEFAULT TRUE
#define DRV_ROTATE 10.0f
#define GLOBE_DEFAULT_LONGITUDE 128

#define EARTH
#define EARTH_32
#define EARTH_64
#define EARTH_128
#define EARTH_256
#define EARTH_512
#define TEXTURE
#define TEXTURE_DEFAULT_SIZE 512
#define TEXTURE_RGB
#define TRISTRIP

#endif /* drv_igl_h */
