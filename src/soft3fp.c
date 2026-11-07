/*
 * testglobe - shared optional floating-point software geometry.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile this object with the native FPU options on classic Macintosh and
 * Amiga builds. Their baseline code enters only after checking the CPU/FPU;
 * pointers and integers are the entire public calling interface. The fixed
 * pipeline in soft3d.c never depends on this object.
 */
#include "testglob.h"
#include "soft3d.h"
#include "globe.h"
#include <float.h>

#if defined(SEG68) && (defined(applec) || defined(__SC__))
#pragma segment QDFPU
#endif

/* Basic arithmetic avoids 68881 transcendental instructions which later
 * 68040/68060 FPUs do not implement directly. Inputs are bounded degrees. */
static double soft3d_float_sine(double angle)
{
    double radians, square;
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

static int soft3d_float_valid(const Soft3d_float_point *point)
{
    return point->x >= -FLT_MAX && point->x <= FLT_MAX &&
           point->y >= -FLT_MAX && point->y <= FLT_MAX &&
           point->z >= -FLT_MAX && point->z <= FLT_MAX;
}

int soft3d_float_prepare(float matrix[3][3],
                         const float TG_HUGE *rotation_x,
                         const float TG_HUGE *rotation_y)
{
    double sx, cx, sy, cy;
    if (matrix == NULL || rotation_x == NULL || rotation_y == NULL)
        return FALSE;
    if (!(*rotation_x >= 0.0f && *rotation_x < 360.0f &&
          *rotation_y >= 0.0f && *rotation_y < 360.0f)) return FALSE;
    sx = soft3d_float_sine((double)*rotation_x);
    cx = soft3d_float_sine((double)*rotation_x + 90.0);
    sy = soft3d_float_sine((double)*rotation_y);
    cy = soft3d_float_sine((double)*rotation_y + 90.0);
    matrix[0][0] = (float)sy;
    matrix[0][1] = (float)cy;
    matrix[0][2] = 0.0f;
    matrix[1][0] = (float)(-sx * cy);
    matrix[1][1] = (float)(sx * sy);
    matrix[1][2] = (float)cx;
    matrix[2][0] = (float)(cx * cy);
    matrix[2][1] = (float)(-cx * sy);
    matrix[2][2] = (float)sx;
    return TRUE;
}

#ifdef DRV_MTX
int soft3d_float_prepare_ortho(float matrix[3][3],
                               int yaw_degrees, int pitch_degrees)
{
    double yaw, pitch, sy, cy, sp, cp;
    if (matrix == NULL) return FALSE;
    yaw = (double)(yaw_degrees % 360);
    pitch = (double)(pitch_degrees % 360);
    if (yaw < 0.0) yaw += 360.0;
    if (pitch < 0.0) pitch += 360.0;
    sy = soft3d_float_sine(yaw); cy = soft3d_float_sine(yaw + 90.0);
    sp = soft3d_float_sine(pitch); cp = soft3d_float_sine(pitch + 90.0);
    matrix[0][0] = (float)cy;
    matrix[0][1] = (float)-sy;
    matrix[0][2] = 0.0f;
    matrix[1][0] = (float)(cp * sy);
    matrix[1][1] = (float)(cp * cy);
    matrix[1][2] = (float)sp;
    matrix[2][0] = (float)(-sp * sy);
    matrix[2][1] = (float)(-sp * cy);
    matrix[2][2] = (float)cp;
    return TRUE;
}
#endif

int soft3d_float_transform_reuse(float matrix[3][3], unsigned int count,
                                const Vertex TG_HUGE *vertices,
                                Soft3d_float_point *out,
                                unsigned int reuse_count, int precision)
{
    unsigned int i;
    float x, y, z, translated;
    if (matrix == NULL || vertices == NULL || out == NULL || count == 0U ||
        count > SOFT3D_MAX_VERTICES ||
        (precision != SOFT3D_FLOAT_SINGLE && precision != SOFT3D_FLOAT_DOUBLE) ||
        (reuse_count != 0U && (count % 2U != 0U || reuse_count != count / 2U)))
        return FALSE;
    /* Reuse skips arithmetic, not input validation. Invalid vertices must
     * not become hidden by a cache hit in a malformed packet. */
    for (i = 0; i < count; ++i)
        if (!(vertices[i].x >= -FLT_MAX && vertices[i].x <= FLT_MAX &&
              vertices[i].y >= -FLT_MAX && vertices[i].y <= FLT_MAX &&
              vertices[i].z >= -FLT_MAX && vertices[i].z <= FLT_MAX))
            return FALSE;
    /* The stream explicitly identifies an unchanged adjoining row. Copy
     * upward: old odd entries are never overwritten before they are read. */
    for (i = 0; i < reuse_count; ++i) out[2U * i] = out[2U * i + 1U];
    for (i = 0; i < count; ++i)
    {
        if (reuse_count != 0U && i % 2U == 0U) continue;
        x = vertices[i].x;
        y = vertices[i].y;
        z = vertices[i].z;
        out[i].x = matrix[0][0] * x + matrix[0][1] * y + matrix[0][2] * z;
        out[i].y = matrix[1][0] * x + matrix[1][1] * y + matrix[1][2] * z;
        if (precision == SOFT3D_FLOAT_SINGLE)
        {
            /* Win16 historically stores the rotation sum as a float before
             * subtracting the camera translation. Keep that rounding boundary
             * even when x87 registers evaluate expressions more precisely. */
            translated = matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z;
            out[i].z = translated - (float)GLOBE_VIEW_DISTANCE;
        }
        else
            out[i].z = matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z -
                       GLOBE_VIEW_DISTANCE;
        if (!soft3d_float_valid(out + i)) return FALSE;
    }
    return TRUE;
}

static int soft3d_float_clip_z(const Soft3d_float_point *in, int count,
                              Soft3d_float_point *out, float plane, int greater)
{
    int i, total, a_inside, b_inside;
    float fraction;
    Soft3d_float_point a, b, point;
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
            if (total >= SOFT3D_MAX_POLY) return 0;
            out[total++] = point;
        }
        if (b_inside)
        {
            if (total >= SOFT3D_MAX_POLY) return 0;
            out[total++] = b;
        }
        a = b;
        a_inside = b_inside;
    }
    return total;
}

#ifdef DRV_AMI
/* VBCC's 68881 conversion of double to long uses FINTRZ, which later 68K
 * FPUs need software support to execute. Decode the stored IEEE value
 * instead. The projection limit below bounds its exponent to 26, so two
 * unsigned 32-bit words suffice without wide integers or unaligned reads.
 * Keep other targets' established conversions: MPW also supports extended
 * double representations which are not the Amiga ABI. */
#if FLT_RADIX != 2 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
#error Amiga floating geometry requires IEEE binary64 double
#endif
typedef char Soft3d_amiga_float_size[(sizeof(float) == 4) ? 1 : -1];
typedef char Soft3d_amiga_double_size[(sizeof(double) == 8) ? 1 : -1];

static int soft3d_float_truncate(const double *value, long *result)
{
    const unsigned char *bytes;
    const unsigned short endian = 1;
    unsigned long high, low, magnitude;
    int exponent, shift;
    if (value == NULL || result == NULL) return FALSE;
    bytes = (const unsigned char *)value;
    if (*(const unsigned char *)&endian)
    {
        high = (unsigned long)bytes[4] | ((unsigned long)bytes[5] << 8) |
               ((unsigned long)bytes[6] << 16) | ((unsigned long)bytes[7] << 24);
        low = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
              ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
    }
    else
    {
        high = (unsigned long)bytes[3] | ((unsigned long)bytes[2] << 8) |
               ((unsigned long)bytes[1] << 16) | ((unsigned long)bytes[0] << 24);
        low = (unsigned long)bytes[7] | ((unsigned long)bytes[6] << 8) |
              ((unsigned long)bytes[5] << 16) | ((unsigned long)bytes[4] << 24);
    }
    exponent = (int)((high >> 20) & 2047UL) - 1023;
    if (exponent < 0)
    {
        *result = 0L;
        return TRUE;
    }
    if (exponent > 26) return FALSE;
    magnitude = (high & 0x000fffffUL) | 0x00100000UL;
    if (exponent <= 20) magnitude >>= 20 - exponent;
    else
    {
        shift = exponent - 20;
        magnitude = (magnitude << shift) | (low >> (32 - shift));
    }
    *result = (high & 0x80000000UL) ? -(long)magnitude : (long)magnitude;
    return TRUE;
}
#endif

static int soft3d_float_project_point(const Soft3d_float_point *point,
                                     unsigned int width, unsigned int height,
                                     int precision, Soft3d_screen_point *out)
{
    float focal_single, x_single, y_single;
    double focal, x, y;
    /* Preserve each native driver's established rounding: Win16's float
     * arithmetic differs from the QuickDraw FPU path's double projection. */
    if (precision == SOFT3D_FLOAT_SINGLE)
    {
        focal_single = (float)height * (float)GLOBE_FOCAL_SCALE;
        x_single = (float)width / 2.0f + point->x * focal_single / -point->z;
        y_single = (float)height / 2.0f - point->y * focal_single / -point->z;
        x = x_single;
        y = y_single;
    }
    else
    {
        focal = (double)height * GLOBE_FOCAL_SCALE;
        x = (double)width / 2.0 + (double)point->x * focal / -(double)point->z;
        y = (double)height / 2.0 - (double)point->y * focal / -(double)point->z;
    }
    if (!(x >= -134217727.0 && x <= 134217727.0 &&
          y >= -134217727.0 && y <= 134217727.0)) return FALSE;
#ifdef DRV_AMI
    if (!soft3d_float_truncate(&x, &out->x) ||
        !soft3d_float_truncate(&y, &out->y)) return FALSE;
#else
    out->x = (long)x;
    out->y = (long)y;
#endif
    return TRUE;
}

#ifdef DRV_MTX
int soft3d_float_project_ortho(const Soft3d_float_point *points,
                               unsigned int count,
                               unsigned int width, unsigned int height,
                               unsigned int world_width,
                               unsigned int world_height,
                               Soft3d_screen_point *out)
{
    unsigned int i;
    double x, y;
    if (points == NULL || out == NULL || count == 0U ||
        count > SOFT3D_MAX_VERTICES || width == 0U || height == 0U ||
        width > 32767U || height > 32767U ||
        world_width == 0U || world_height == 0U ||
        world_width > 32767U || world_height > 32767U) return 0;
    for (i = 0; i < count; ++i)
        if (!soft3d_float_valid(points + i) ||
            !(points[i].x >= -3.0f * SOFT3D_MAX_RADIUS &&
              points[i].x <= 3.0f * SOFT3D_MAX_RADIUS &&
              points[i].y >= -3.0f * SOFT3D_MAX_RADIUS &&
              points[i].y <= 3.0f * SOFT3D_MAX_RADIUS)) return 0;
    for (i = 0; i < count; ++i)
    {
        /* The validated bounds keep both results within +/-100 million,
         * even with a one-unit world window and maximum viewport extent.
         * Floor matches the fixed projector on either side of the window;
         * no x87 generation-specific instructions are required by /FPi. */
        x = (0.5 + (double)points[i].x / world_width) * (width - 1U);
        y = (0.5 - (double)points[i].y / world_height) * (height - 1U);
        out[i].x = (long)x;
        out[i].y = (long)y;
        if ((double)out[i].x > x) --out[i].x;
        if ((double)out[i].y > y) --out[i].y;
    }
    return (int)count;
}
#endif

int soft3d_float_project_cached(const Soft3d_float_point *points,
                                unsigned int point_count,
                                const unsigned int TG_HUGE *indices,
                                unsigned int corners, unsigned int width,
                                unsigned int height, int precision,
                                Soft3d_project_cache TG_HUGE *cache,
                                Soft3d_screen_point *out)
{
    Soft3d_float_point a[SOFT3D_MAX_POLY], b[SOFT3D_MAX_POLY];
    unsigned int i, index, bits, outside, common;
    int count;
    if (points == NULL || indices == NULL || out == NULL || point_count == 0U ||
        point_count > SOFT3D_MAX_VERTICES || (corners != 3U && corners != 4U) ||
        width == 0U || height == 0U || width > 32767U || height > 32767U ||
        (precision != SOFT3D_FLOAT_SINGLE && precision != SOFT3D_FLOAT_DOUBLE))
        return 0;
    outside = 0U;
    common = 3U;
    for (i = 0; i < corners; ++i)
    {
        index = indices[i];
        if (index >= point_count || !soft3d_float_valid(points + index)) return 0;
        bits = 0U;
        if (points[index].z > -(float)GLOBE_NEAR_DISTANCE) bits |= 1U;
        if (points[index].z < -(float)GLOBE_FAR_DISTANCE) bits |= 2U;
        outside |= bits;
        common &= bits;
    }
    if (common != 0U) return 0;
    if (outside == 0U)
    {
        if (cache != NULL && (cache->count != point_count ||
            cache->width != width || cache->height != height)) cache = NULL;
        for (i = 0; i < corners; ++i)
        {
            index = indices[i];
            if (cache != NULL && cache->valid[index]) out[i] = cache->screen[index];
            else
            {
                if (!soft3d_float_project_point(points + index, width, height,
                                                precision, out + i)) return 0;
                if (cache != NULL)
                {
                    cache->screen[index] = out[i];
                    cache->valid[index] = 1U;
                }
            }
        }
        return (int)corners;
    }
    /* Clipping creates new intersection vertices. Keep those local and
     * retain the original path's interpolation and projection rounding. */
    for (i = 0; i < corners; ++i) a[i] = points[indices[i]];
    count = soft3d_float_clip_z(a, (int)corners, b,
                               -(float)GLOBE_NEAR_DISTANCE, FALSE);
    if (count < 3) return 0;
    count = soft3d_float_clip_z(b, count, a, -(float)GLOBE_FAR_DISTANCE, TRUE);
    if (count < 3) return 0;
    for (i = 0; i < (unsigned int)count; ++i)
        if (!soft3d_float_project_point(a + i, width, height, precision, out + i))
            return 0;
    return count;
}
