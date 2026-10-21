/*
 * testglobe - display 3d globe to test 3d hardware
 * drv_mtx.h - Matrox LibShell driver
 *
 * Copyright (C) 2026 trguhq
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

#ifndef drv_mtx_h
#define drv_mtx_h

#ifndef DRV
#define DRV
#endif

#include "XGX1/HEADERS/LIBFN.H"
#include "XGX1/HEADERS/IOLIB.H"

#define DRV_NAME "Matrox LibShell"
#define DRV_HELP "1-5 = facets, A = auto rotate, O = OSD, T = texture mapping"

#define DRV_WIN_X 100
#define DRV_WIN_Y 100
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_COLORMAPPED TRUE
#define DRV_TEXTURE TRUE
#define DRV_TEXTURE_DEFAULT FALSE
#define DRV_OSD_DEFAULT TRUE
#define DRV_DEPTH 8
#define DRV_COLORMAPDEPTH 16
#define Color Color_
#define DRV_FPS_HISTORY 60
#define DRV_FPS_DEFAULT TRUE
#define DRV_ROTATE 10.0f

#define EARTH
#define EARTH_32
#define EARTH_64

#endif /* drv_mtx_h */
