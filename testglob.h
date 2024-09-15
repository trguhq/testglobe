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

#include <string.h>

#include "err_ext.h"

#define DEBUG

#define TESTGLOBE_NAME "testglobe"

#define TESTGLOBE_VERSION_MAJOR 0
#define TESTGLOBE_VERSION_MINOR 1

#ifndef testglob_h
#define testglob_h

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define EARTH
#define EARTH_32
#define EARTH_64
#define EARTH_128
#define EARTH_256
#define EARTH_512

// compile in texture support
#define TEXTURE
// default texture size, this can be 32, 64, 128, 256 or 512
#define TEXTURE_DEFAULT_SIZE 512
#define TEXTURE_RGB

#define GLOBE_RADIUS 1000

// this can be 32, 64, 128, 256 and 512, provided the above are defined for each
#define DEFAULT_GLOBE_LONGITUDE 512

// set a graphics driver if not already set
#ifndef DRV
#ifdef __APPLE__
#ifdef __MACH__
#define DRV_OGL
#endif
#endif
#ifdef __BORLANDC__
#define DRV_PGC
#endif
#ifdef sparc
#define DRV_XGL
#endif
#endif

// system specific defines
#ifdef NO_SINF
#define SIN sin
#define COS cos
#else
#define SIN sinf
#define COS cosf
#endif

#endif /* testglob_h */
