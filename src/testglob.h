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

#ifndef testglob_h
#define testglob_h

#include <string.h>
#include "err_ext.h"

#ifndef DEBUG
#define DEBUG 0
#endif
#define TESTGLOBE_NAME "testglobe"
#define TESTGLOBE_VERSION_MAJOR 0
#define TESTGLOBE_VERSION_MINOR 2

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* Fallbacks for legacy build settings; the Makefile selects the driver. */
#if !defined(DRV_OGL) && !defined(DRV_IGL) && !defined(DRV_XGL) && \
    !defined(DRV_MTX) && !defined(DRV_GDI) && !defined(DRV_QD) && \
    !defined(DRV_PGC) && !defined(DRV_PHG)
#if defined(__APPLE__) && defined(__MACH__)
#define DRV_OGL
#elif defined(applec) || defined(__SC__) || defined(__APPLE__)
#define DRV_QD
#elif defined(sparc) || defined(__sparc)
#define DRV_XGL
#endif
#endif

/* Infer memory architecture only after the driver has been selected. */
#if !defined(FLAT) && !defined(SEG86) && !defined(SEG68)
#if defined(DRV_MTX) || defined(DRV_GDI)
#define SEG86
#elif defined(DRV_QD)
#define SEG68
#else
#define FLAT
#endif
#endif
#if (defined(FLAT) + defined(SEG86) + defined(SEG68)) > 1
#error Choose exactly one memory architecture: FLAT, SEG86, or SEG68
#endif
/* The selected driver header chooses TRI, QUAD, or TRISTRIP. */
#if defined(DRV_OGL) || defined(DRV_IGL) || defined(DRV_XGL) || \
    defined(DRV_MTX) || defined(DRV_GDI) || defined(DRV_QD)
#ifndef DRV
#define DRV
#endif
#endif

/* Historical Microsoft and MPW runtimes predate C99 sinf/cosf. */
#if defined(SEG86) || defined(SEG68)
#ifndef NO_SINF
#define NO_SINF
#endif
#endif
#ifdef NO_SINF
#define SIN sin
#define COS cos
#else
#define SIN sinf
#define COS cosf
#endif

#endif /* testglob_h */
