// testglobe - display 3d globe to test 3d hardware
// drv_ogl.h - OpenGL driver
//
// Copyright (C) 2024 trguhq
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#ifndef drv_ogl_h
#define drv_ogl_h

#ifndef DRV
#define DRV
#endif

#define DRV_NAME "OpenGL"
#define DRV_HELP "1-5 = facets, O = OSD, T = texturing"
#define GL_SILENCE_DEPRECATION

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



#endif /* drv_ogl_h */
