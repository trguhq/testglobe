/*
 * testglobe - display 3d globe to test 3d hardware
 * drv_qd.h - Classic Mac QuickDraw driver
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

#ifndef drv_qd_h
#define drv_qd_h

#ifndef DRV
#define DRV
#endif

#define DRV_NAME "QD"
#define DRV_HELP "1-5 = facets, A = auto-rotate, B = benchmark, O = OSD, T = texture mapping"

#define DRV_WIN_X 100
#define DRV_WIN_Y 100
#define DRV_WIN_WIDTH 640
#define DRV_WIN_HEIGHT 480
#define DRV_COLORMAPPED TRUE
#define DRV_TEXTURE FALSE
#define DRV_TEXTURE_DEFAULT FALSE
#define DRV_OSD_DEFAULT TRUE
#define DRV_DEPTH 8
#define DRV_COLORMAPDEPTH 24

#define EARTH
#define EARTH_32
#define EARTH_64
#define EARTH_128
#define EARTH_256
#define EARTH_512

/* compile in texture support */
#undefine TEXTURE
/* default texture size, this can be 32, 64, 128, 256 or 512 */
#undefine TEXTURE_DEFAULT_SIZE 512
/* currently means 24-bit RGB */
#undefine TEXTURE_RGB

#endif /* drv_qd_h */
