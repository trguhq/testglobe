/*
 * testglobe - optional 68881 transform/projection code segment.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile ONLY this object with MPW C's -mc68020 -mc68881 options. The
 * baseline 68000 driver checks the processor and FPU before entering it.
 * All floating operations, including trigonometry, remain in this object:
 * no floating arguments or returns cross to SANE-compiled application code.
 */
#include "testglob.h"
#if defined(SEG68) && defined(DRV_QD)
#if defined(applec) || defined(__SC__)
#pragma segment QDFPU
#endif
#include "qd_math.h"
#include "globe.h"

/* The shared floating object is built with the same guarded FPU options.
 * These wrappers keep the Macintosh calling boundary and triangle entry. */
static float qd_fpu_matrix[3][3];
static Soft3d_float_point qd_fpu_points[QD_FPU_MAX_VERTICES];
static unsigned int qd_fpu_count;
static int qd_fpu_ready;

int qd_fpu_prepare(const float *rotation_x, const float *rotation_y)
{
    qd_fpu_count = 0;
    qd_fpu_ready = soft3d_float_prepare(qd_fpu_matrix, rotation_x, rotation_y);
    return qd_fpu_ready;
}

int qd_fpu_transform_reuse(unsigned int count, const Vertex *vertices,
                            unsigned int reuse_count)
{
    unsigned int previous_count;
    previous_count = qd_fpu_count;
    qd_fpu_count = 0;
    if (!qd_fpu_ready || vertices == NULL || count > QD_FPU_MAX_VERTICES)
        return FALSE;
    if (reuse_count != 0U && previous_count != count) reuse_count = 0U;
    if (!soft3d_float_transform_reuse(qd_fpu_matrix, count, vertices,
                                      qd_fpu_points, reuse_count,
                                      SOFT3D_FLOAT_DOUBLE)) return FALSE;
    qd_fpu_count = count;
    return TRUE;
}

void qd_fpu_transform(unsigned int count, const Vertex *vertices)
{
    (void)qd_fpu_transform_reuse(count, vertices, 0U);
}

int qd_fpu_project_cached(const unsigned int *indices, unsigned int count,
                           unsigned int width, unsigned int height,
                           Soft3d_project_cache *cache, Qd_screen_point *out)
{
    return soft3d_float_project_cached(qd_fpu_points, qd_fpu_count, indices,
                                       count, width, height,
                                       SOFT3D_FLOAT_DOUBLE, cache, out);
}

int qd_fpu_project_polygon(const unsigned int *indices, unsigned int count,
                            unsigned int width, unsigned int height,
                            Qd_screen_point *out)
{
    return qd_fpu_project_cached(indices, count, width, height, NULL, out);
}

int qd_fpu_project(unsigned int first, unsigned int second,
                     unsigned int third, unsigned int width,
                     unsigned int height, Qd_screen_point *out)
{
    unsigned int indices[3];
    indices[0] = first; indices[1] = second; indices[2] = third;
    return qd_fpu_project_polygon(indices, 3U, width, height, out);
}
#endif /* SEG68 && DRV_QD */
