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

#ifndef e_ext_h
#define e_ext_h

#include "testglob.h"
#include "drv.h"
#include "types.h"

/* One declaration source for both embedded and resource-backed Earth data. */
#ifdef EARTH
extern unsigned char earth_cmap[][3];
#ifndef DRV_EARTH_RESOURCES
/* Generated from the binary maps in data/. Each object is at most 16 KiB. */
extern unsigned long earth_data_size(int longitude);
/* Copy a bounded range without exposing chunk or segment boundaries.
 * Return zero for an unsupported level, invalid range, or NULL destination
 * with a nonzero count. A zero-byte read may use NULL at any valid offset. */
extern int earth_read_data(int longitude, unsigned long offset,
                            unsigned int count,
                            unsigned char TG_HUGE *destination);
#endif
#endif

#endif /* e_ext_h */
