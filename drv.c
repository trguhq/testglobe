// testglobe - display 3d globe to test 3d hardware
// drv.c - graphics driver
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

#include "testglob.h"
#include "globe.h"

#ifdef DRV_OGL
#include "drv_ogl.h"
#endif
#ifdef DRV_PGC
#include "drv_pgc.h"
#endif
#ifdef DRV_XGL
#include "drv_xgl.h"
#endif

unsigned int drv_win_x, drv_win_y;              // window geometry
unsigned int drv_win_width, drv_win_height;     // window geometry
int drv_colormapped;                    // bool is colormap window
int drv_texture_avail;                  // is texturing available in the driver
int drv_texture_loaded;                 // has texture been loaded
int drv_texture_enabled;                // is texturing enabled
int drv_osd_enabled;                    // is OSD enabled
unsigned int drv_bits_per_pixel;        // bits depth
unsigned int drv_colormap_bits;         // bits per color
unsigned long int drv_colors;           // number of colors
unsigned long int drv_colormap_colors;  // number of colors in colormap
unsigned int drv_bytes_per_pixel;       // bytes per color: 1, 2 or 3
char drv_name[64];                      // name of driver
float drv_rot_x;                            // rotation of globe on x axis
float drv_rot_y;                            // rotation of globe on y axis
int drv_lmouse_pressed;                 // mouse pressed
int drv_rmouse_pressed;                 //
char drv_help[256];                     // OSD help string to print


// call first
void drv_ext_init(void)
{
#ifndef DRV
    error("No driver loaded");          // won't compile anyway
#endif
    drv_win_x = DRV_WIN_X;
    drv_win_y = DRV_WIN_Y;
    drv_win_width = DRV_WIN_WIDTH;
    drv_win_height = DRV_WIN_HEIGHT;
    drv_colormapped = DRV_COLORMAPPED;
    drv_texture_avail = DRV_TEXTURE;
    drv_texture_enabled = DRV_TEXTURE_DEFAULT;
    drv_osd_enabled = DRV_OSD_DEFAULT;
    drv_bits_per_pixel = DRV_DEPTH;
    drv_colormap_bits = DRV_COLORMAPDEPTH;
    drv_colors = (drv_bits_per_pixel == 32 ? 0xFFFFFFFF : 1<<drv_bits_per_pixel);
    drv_colormap_colors = (drv_colormap_bits == 32 ? 0xFFFFFFFF : 1<<drv_colormap_bits);
    switch (drv_bits_per_pixel)
    {
        case 8:
            drv_bytes_per_pixel = 1;
            break;
        case 16:
            drv_bytes_per_pixel = 2;
            break;
        case 24:
            drv_bytes_per_pixel = 3;
            break;
        default:    // this should really be an error as it's not handled
            drv_bytes_per_pixel = 4;
            break;
    }
    strcpy(drv_name, DRV_NAME);
    strcpy(drv_help, DRV_HELP);
    drv_rot_x = GLOBE_DEFAULT_ROTATION_X;
    drv_rot_y = GLOBE_DEFAULT_ROTATION_Y;
    drv_lmouse_pressed = FALSE;
    drv_rmouse_pressed = FALSE;
}

