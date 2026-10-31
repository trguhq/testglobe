/*
 * testglobe - shared integer software transform and clipping pipeline.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef soft3d_h
#define soft3d_h

#include "types.h"

/* Q4 input and Q14 rotation intermediates are proven for this radius. */
#define SOFT3D_MAX_RADIUS 1000L
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

/* Optional bounded cache for original packet vertices. Newly generated
 * clipping intersections use the uncached polygon path. A valid byte means
 * screen[] contains this packet's projection at the recorded dimensions. */
typedef struct Soft3d_project_cache
{
    Soft3d_screen_point screen[SOFT3D_MAX_VERTICES];
    unsigned char valid[SOFT3D_MAX_VERTICES];
    unsigned int count, width, height;
} Soft3d_project_cache;

typedef struct Soft3d_float_point
{
    float x, y, z;
} Soft3d_float_point;

#define SOFT3D_FLOAT_SINGLE 0
#define SOFT3D_FLOAT_DOUBLE 1

extern void soft3d_project_cache_reset(Soft3d_project_cache TG_HUGE *cache);
/* reuse_count is zero, or count/2 when an explicitly identified band's
 * old odd-indexed bottom row becomes this band's even-indexed top row.
 * The owner must reset at each frame, pose change, or interrupted stream. */
extern int soft3d_project_cache_begin(Soft3d_project_cache TG_HUGE *cache,
                                      unsigned int count,
                                      unsigned int width, unsigned int height,
                                      unsigned int reuse_count);

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
extern int soft3d_fixed_transform_reuse(long matrix[3][3], unsigned int count,
                                        const Vertex TG_HUGE *vertices,
                                        Soft3d_fixed_point *out,
                                        unsigned int reuse_count);
#if defined(DRV_AMI) || defined(SOFT3D_TRANSFORM_020)
/* Compiled separately for 68020 instructions; call only after CPU detection. */
extern int soft3d_020_transform_reuse(long matrix[3][3], unsigned int count,
                                      const Vertex TG_HUGE *vertices,
                                      Soft3d_fixed_point *out,
                                      unsigned int reuse_count);
#endif
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
extern int soft3d_fixed_project_cached(const Soft3d_fixed_point *points,
                                       unsigned int point_count,
                                       const unsigned int TG_HUGE *indices,
                                       unsigned int corners, unsigned int width,
                                       unsigned int height,
                                       Soft3d_project_cache TG_HUGE *cache,
                                       Soft3d_screen_point *out);

/* The floating implementation is a separate object. On 68K targets only
 * that object may contain FPU instructions; all boundaries use pointers or
 * integer arguments so baseline and native-FPU calling ABIs cannot mix. */
extern int soft3d_float_prepare(float matrix[3][3],
                                const float TG_HUGE *rotation_x,
                                const float TG_HUGE *rotation_y);
extern int soft3d_float_transform_reuse(float matrix[3][3], unsigned int count,
                                        const Vertex TG_HUGE *vertices,
                                        Soft3d_float_point *out,
                                        unsigned int reuse_count, int precision);
extern int soft3d_float_project_cached(const Soft3d_float_point *points,
                                       unsigned int point_count,
                                       const unsigned int TG_HUGE *indices,
                                       unsigned int corners, unsigned int width,
                                       unsigned int height, int precision,
                                       Soft3d_project_cache TG_HUGE *cache,
                                       Soft3d_screen_point *out);
/* Clip a convex projected polygon to its viewport and reject back faces.
 * The array has SOFT3D_MAX_POLY elements; retained points replace its input.
 * Consecutive duplicate points (including collapsed poles) are compacted.
 * This also accepts polygons projected by the driver's native FPU path. */
extern int soft3d_finish_polygon(Soft3d_screen_point *points, int count,
                                 unsigned int width, unsigned int height);

#endif /* soft3d_h */
