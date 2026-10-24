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

typedef struct Qd_fpu_point
{
    float x;
    float y;
    float z;
} Qd_fpu_point;

static float qd_fpu_matrix[3][3];
static Qd_fpu_point qd_fpu_points[QD_FPU_MAX_VERTICES];
static unsigned int qd_fpu_count;
static int qd_fpu_ready;

/* The application's rotation angles are normalized to [0,360). Reduce
 * into [-pi/2,pi/2] so this degree-13 sine polynomial has absolute error
 * below 7e-10 before the final single-precision matrix store. Keeping the
 * arithmetic here avoids dependencies on a SANE math-library calling ABI. */
static double qd_fpu_sine(double angle)
{
    double radians, square;
    /* prepare() bounds this input to [0,450), including cosine's quarter
     * turn. A single subtraction also avoids non-progress on huge values. */
    if (angle > 180.0) angle -= 360.0;
    if (angle > 90.0) angle = 180.0 - angle;
    if (angle < -90.0) angle = -180.0 - angle;
    radians = angle * 0.017453292519943295769;
    square = radians * radians;
    return radians * (1.0 + square * (-1.0 / 6.0 +
           square * (1.0 / 120.0 + square * (-1.0 / 5040.0 +
           square * (1.0 / 362880.0 + square * (-1.0 / 39916800.0 +
           square * (1.0 / 6227020800.0)))))));
}

int qd_fpu_prepare(const float *rotation_x, const float *rotation_y)
{
    double sx, cx, sy, cy;
    qd_fpu_count = 0;
    qd_fpu_ready = FALSE;
    if (rotation_x == NULL || rotation_y == NULL) return FALSE;
    if (!(*rotation_x >= 0.0f && *rotation_x < 360.0f &&
          *rotation_y >= 0.0f && *rotation_y < 360.0f)) return FALSE;
    sx = qd_fpu_sine((double)*rotation_x);
    cx = qd_fpu_sine((double)*rotation_x + 90.0);
    sy = qd_fpu_sine((double)*rotation_y);
    cy = qd_fpu_sine((double)*rotation_y + 90.0);
    /* OpenGL's Rx(x) Ry(y) Rx(-90) Rz(-90), then camera translation. */
    qd_fpu_matrix[0][0] = (float)sy;
    qd_fpu_matrix[0][1] = (float)cy;
    qd_fpu_matrix[0][2] = 0.0f;
    qd_fpu_matrix[1][0] = (float)(-sx * cy);
    qd_fpu_matrix[1][1] = (float)(sx * sy);
    qd_fpu_matrix[1][2] = (float)cx;
    qd_fpu_matrix[2][0] = (float)(cx * cy);
    qd_fpu_matrix[2][1] = (float)(-cx * sy);
    qd_fpu_matrix[2][2] = (float)sx;
    qd_fpu_ready = TRUE;
    return TRUE;
}

void qd_fpu_transform(unsigned int count, const Vertex *vertices)
{
    unsigned int i;
    float x, y, z;
    qd_fpu_count = 0;
    if (!qd_fpu_ready || vertices == NULL || count > QD_FPU_MAX_VERTICES) return;
    for (i = 0; i < count; ++i)
    {
        x = vertices[i].x;
        y = vertices[i].y;
        z = vertices[i].z;
        qd_fpu_points[i].x = qd_fpu_matrix[0][0] * x +
                              qd_fpu_matrix[0][1] * y +
                              qd_fpu_matrix[0][2] * z;
        qd_fpu_points[i].y = qd_fpu_matrix[1][0] * x +
                              qd_fpu_matrix[1][1] * y +
                              qd_fpu_matrix[1][2] * z;
        qd_fpu_points[i].z = qd_fpu_matrix[2][0] * x +
                              qd_fpu_matrix[2][1] * y +
                              qd_fpu_matrix[2][2] * z - GLOBE_VIEW_DISTANCE;
    }
    qd_fpu_count = count;
}

static int qd_fpu_clip_z(const Qd_fpu_point *in, int count,
                        Qd_fpu_point *out, float plane, int greater)
{
    int i, total, a_inside, b_inside;
    float fraction;
    Qd_fpu_point a, b, point;
    total = 0;
    a = in[count - 1];
    a_inside = greater ? a.z >= plane : a.z <= plane;
    for (i = 0; i < count; ++i)
    {
        b = in[i];
        b_inside = greater ? b.z >= plane : b.z <= plane;
        if (a_inside != b_inside)
        {
            fraction = (plane - a.z) / (b.z - a.z);
            point.x = a.x + (b.x - a.x) * fraction;
            point.y = a.y + (b.y - a.y) * fraction;
            point.z = plane;
            out[total++] = point;
        }
        if (b_inside) out[total++] = b;
        a = b;
        a_inside = b_inside;
    }
    return total;
}

int qd_fpu_project_polygon(const unsigned int *indices, unsigned int count,
                            unsigned int width, unsigned int height,
                            Qd_screen_point *out)
{
    Qd_fpu_point a[QD_FPU_MAX_POLY], b[QD_FPU_MAX_POLY];
    double focal, x, y;
    unsigned int index;
    int i, clipped_count;
    if (indices == NULL || (count != 3U && count != 4U) ||
        out == NULL || width == 0 || height == 0 ||
        width > 32767U || height > 32767U)
        return 0;
    for (index = 0; index < count; ++index)
    {
        if (indices[index] >= qd_fpu_count) return 0;
        a[index] = qd_fpu_points[indices[index]];
    }
    clipped_count = qd_fpu_clip_z(a, (int)count, b, -GLOBE_NEAR_DISTANCE, FALSE);
    if (clipped_count < 3) return 0;
    clipped_count = qd_fpu_clip_z(b, clipped_count, a, -GLOBE_FAR_DISTANCE, TRUE);
    if (clipped_count < 3) return 0;
    /* Shared vertical field of view, downward-Y QuickDraw coordinates. */
    focal = (double)height * GLOBE_FOCAL_SCALE;
    for (i = 0; i < clipped_count; ++i)
    {
        x = (double)width / 2.0 + (double)a[i].x * focal / -(double)a[i].z;
        y = (double)height / 2.0 - (double)a[i].y * focal / -(double)a[i].z;
        /* The supported radius-1000 globe is well inside these limits.
         * Bound conversion and the following driver's integer clipping
         * deltas without relying on the host's potentially wider long. */
        if (!(x >= -134217727.0 && x <= 134217727.0 &&
              y >= -134217727.0 && y <= 134217727.0))
            return 0;
        out[i].x = (long)x;
        out[i].y = (long)y;
    }
    return clipped_count;
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
