// testglobe - display 3d globe to test 3d hardware
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

#ifndef glob_ext_h
#define glob_ext_h

extern void globe_init(void);
extern int globe_init_verts(int, int, float);
extern void globe_draw_tris(void);
extern void globe_toggle_res(char);
extern void globe_toggle_tex(void);
extern unsigned char earth_data_32[];
extern unsigned char earth_data_64[];
extern unsigned char earth_data_128[];
extern unsigned char earth_data_256[];
extern unsigned char earth_data_512[];
extern unsigned char earth_cmap[][3];
extern int globe_tris_num;

#endif /* glob_ext_h */
