/*
 * testglobe - pointer-only boundary to the optional 68881 code segment.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef qd_math_h
#define qd_math_h

#include "types.h"

#define QD_FPU_MAX_VERTICES 130
#define QD_FPU_MAX_POLY 10

typedef struct Qd_screen_point
{
    long x;
    long y;
} Qd_screen_point;

/* Compile qd_fpu.c separately for the 68881. The baseline 68000 driver
 * must check both CPU and FPU Gestalt results before calling any of these
 * routines. Only pointers and integers cross the SANE/68881 ABI boundary. */
extern void qd_fpu_prepare(const float *rotation_x, const float *rotation_y);
extern void qd_fpu_transform(unsigned int count, const Vertex *vertices);
/* Returns a near/far-clipped polygon in downward-Y screen coordinates.
 * The caller then clips to its window and rejects back-facing polygons. */
extern int qd_fpu_project(unsigned int first, unsigned int second,
                           unsigned int third, unsigned int width,
                           unsigned int height, Qd_screen_point *out);

#endif /* qd_math_h */
