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

#ifndef e_ext_h
#define e_ext_h

#ifdef EARTH
extern unsigned char earth_cmap[][];
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
#endif

#endif /* e_ext_h */
