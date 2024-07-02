// testglobe - display 3d globe to test 3d hardware
// drv_ext.c - graphics driver externs
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

#ifndef drv_ext_h
#define drv_ext_h

#include "types.h"

extern unsigned int drv_win_x;
extern unsigned int drv_win_y;
extern unsigned int drv_win_width;
extern unsigned int drv_win_height;
extern int drv_colormapped;
extern unsigned int drv_bits_per_pixel;
extern unsigned int drv_bytes_per_pixel;
extern unsigned int drv_colormap_bits;
extern unsigned long int drv_colors;
extern unsigned long int drv_colormap_colors;
extern char drv_name[];
extern float drv_rot_x;
extern float drv_rot_y;
extern int drv_lmouse_pressed;
extern int drv_rmouse_pressed;

extern void drv_ext_init(void);
extern void drv_init(int *argc, char **argv);
extern int drv_init_window(int in_x, int in_y, int in_width, int in_height);
extern void drv_draw_tri_flat_rgb(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Color_RGB *color);
extern void drv_loop(void);

#endif /* drv_ext_h */
