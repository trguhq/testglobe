/*
 * testglobe - shared integer software transform and clipping pipeline.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef soft3d_h
#define soft3d_h

#include "types.h"

#define SOFT3D_MAX_VERTICES 130
#define SOFT3D_MAX_POLY 10

typedef struct Soft3d_fixed_point
{
    long x, y, z;
} Soft3d_fixed_point;

typedef struct Soft3d_screen_point
{
    long x, y;
} Soft3d_screen_point;

/* Float inputs are decoded through their IEEE single-precision bytes;
 * these functions execute no floating-point arithmetic. The matrix is Q14
 * and transformed coordinates are Q4. Errors return zero for the driver
 * to report through its native interface. */
extern int soft3d_fixed_prepare(long matrix[3][3],
                                const float TG_HUGE *rotation_x,
                                const float TG_HUGE *rotation_y);
/* The matrix comes from soft3d_fixed_prepare; input radius is at most 1000.
 * C89 does not permit adding const through a pointer to a 2D array, so the
 * matrix parameter is unqualified even though this function only reads it. */
extern int soft3d_fixed_transform(long matrix[3][3], unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  Soft3d_fixed_point *out);
/* Select a triangle from the point_count initialized packet elements,
 * clip near/far planes, and project into downward-Y screen coordinates.
 * point_count must be between 1 and SOFT3D_MAX_VERTICES. The caller supplies
 * SOFT3D_MAX_POLY output points and then finishes viewport clipping below. */
extern int soft3d_fixed_project(const Soft3d_fixed_point *points,
                                unsigned int point_count,
                                unsigned int first, unsigned int second,
                                unsigned int third, unsigned int width,
                                unsigned int height, Soft3d_screen_point *out);
/* The indexed polygon form accepts three or four ordered packet corners.
 * Quads are clipped and projected as polygons, without an internal diagonal. */
extern int soft3d_fixed_project_polygon(const Soft3d_fixed_point *points,
                                        unsigned int point_count,
                                        const unsigned int TG_HUGE *indices,
                                        unsigned int corners, unsigned int width,
                                        unsigned int height,
                                        Soft3d_screen_point *out);
/* Clip a convex projected polygon to its viewport and reject back faces.
 * The array has SOFT3D_MAX_POLY elements; retained points replace its input.
 * Consecutive duplicate points (including collapsed poles) are compacted.
 * This also accepts polygons projected by the driver's native FPU path. */
extern int soft3d_finish_polygon(Soft3d_screen_point *points, int count,
                                 unsigned int width, unsigned int height);

#endif /* soft3d_h */
