/*
 * testglobe - display 3d globe to test 3d hardware
 * texture.h - generic texture support
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

#ifndef texture_h
#define texture_h

#include "testglob.h"
#include "types.h"

/* Earth maps and the software fallback share this fixed image contract. */
#define TEXTURE_MAX_SIZE 512U
#define TEXTURE_PALETTE_SIZE 256U
#define TEXTURE_PALETTE_BYTES (TEXTURE_PALETTE_SIZE * 3U)

/* Keep mipmaps, but normally release inactive frame/depth/transfer buffers.
 * Define to TRUE when faster texture toggles matter more than memory use. */
#ifndef TEXTURE_CACHE_RASTER
#define TEXTURE_CACHE_RASTER FALSE
#endif

#endif /* texture_h */
