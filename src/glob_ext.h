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
#include "e_ext.h"

extern void globe_init(void);
extern void globe_free(void);
extern int globe_init_verts(int, int, float);
/* Returns true if the requested, restored, or reduced scene is usable. */
extern int globe_set_resolution(int, int, float);
extern int globe_ready(void);
extern Drv_result globe_last_result;
extern unsigned long globe_revision;
extern int globe_requested_longitude;
extern int globe_longitude;
extern int globe_latitude;
#ifdef TRI
extern void globe_draw_tris(void);
#endif
#if defined(TRISTRIP) && defined(DRV_TRI_CAPS)
/* Separate cap triangles retain their original colors and texture UVs. */
extern void globe_draw_pole_tris(void);
#endif
#ifdef TRISTRIP
extern void globe_draw_tristrips(void);
#endif
#ifdef QUAD
extern void globe_draw_quads(void);
#ifdef DRV_QUAD_BATCH
extern void globe_draw_quadbatches(void);
#endif
#endif
extern void globe_toggle_res(char);
#ifdef TEXTURE
extern void globe_toggle_tex(void);
#endif
extern unsigned long globe_verts_num;
extern unsigned long globe_faces_num;
extern unsigned long globe_tris_num;
extern unsigned long globe_tris_num_vis;
extern unsigned long globe_quads_num;
extern unsigned long globe_quads_num_vis;

#endif /* glob_ext_h */
