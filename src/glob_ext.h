/*
 * testglobe - display 3d globe to test 3d hardware
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

#ifndef glob_ext_h
#define glob_ext_h

#include "testglob.h"
#include "drv.h"

extern void globe_init(void);
extern void globe_free(void);
extern int globe_init_verts(int, int, float);
#ifdef TRI
extern void globe_draw_tris(void);
#endif
#ifdef QUAD
extern void globe_draw_quads(void);
#endif
extern void globe_toggle_res(char);
#ifdef TEXTURE
extern void globe_toggle_tex(void);
#endif
#ifdef EARTH_32
extern unsigned char earth_data_32[];
#endif
#ifdef EARTH_64
extern unsigned char earth_data_64[];
#endif
#ifdef EARTH_128
extern unsigned char earth_data_128[];
#endif
#ifdef EARTH_256
extern unsigned char earth_data_256[];
#endif
#ifdef EARTH_512
extern unsigned char earth_data_512[];
#endif
extern unsigned char earth_cmap[][3];
extern unsigned long globe_verts_num;
extern unsigned long globe_faces_num;
extern unsigned long globe_tris_num;
extern unsigned long globe_tris_num_vis;
extern unsigned long globe_quads_num;
extern unsigned long globe_quads_num_vis;

#endif /* glob_ext_h */
