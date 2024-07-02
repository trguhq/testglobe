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

#ifndef types_h
#define types_h

typedef struct Vertex
{
    float x;
    float y;
    float z;
} Vertex;

typedef struct Color_RGB
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
} Color_RGB;

typedef struct Color_Index
{
    unsigned char color;
} Color_Index;

#endif /* types_h */
