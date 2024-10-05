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

#ifndef globe_h
#define globe_h

#include <stdio.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

#define GLOBE_RADIUS 1000

// default rotation
#define GLOBE_DEFAULT_ROTATION_X 0
#define GLOBE_DEFAULT_ROTATION_Y 180

// this can be 32, 64, 128, 256 and 512, provided the above are defined for each
#define GLOBE_DEFAULT_LONGITUDE 512

#endif /* globe_h */
