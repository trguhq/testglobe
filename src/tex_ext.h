// testglobe - display 3d globe to test 3d hardware
// text_ext.h - texture externs
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

#ifndef tex_ext_h
#define tex_ext_h

extern unsigned char* texture_data;
extern unsigned int texture_size;
extern unsigned int texture_data_size;

extern void texture_generate(int size);
extern void texture_init(void);
extern void texture_free(void);

#endif /* tex_ext_h */
