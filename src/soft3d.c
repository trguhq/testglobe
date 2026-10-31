/*
 * testglobe - shared integer software transform and clipping pipeline.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keep this object on the baseline CPU and floating-point compiler settings.
 * The integer implementation works on both Win16 and the original 68000;
 * optional native FPU code remains behind each driver's own boundary.
 */
#include "testglob.h"
#include "soft3d.h"
#include "globe.h"

#if defined(SEG68) && (defined(applec) || defined(__SC__))
#pragma segment Soft3D
#endif

#define SOFT3D_Q14 16384L
#define SOFT3D_COORD_SCALE 16L
#define SOFT3D_FOCAL_Q14 GLOBE_FOCAL_Q14

#ifndef SOFT3D_TRANSFORM_020
static const unsigned short soft3d_sine_quadrant[91] = {
    0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406,
    3686, 3964, 4240, 4516, 4790, 5063, 5334, 5604, 5872, 6138, 6402, 6664, 6924,
    7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397, 9630, 9860, 10087,
    10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176,
    12365, 12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741, 13894,
    14044, 14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191,
    15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026,
    16083, 16135, 16182, 16225, 16262, 16294, 16322, 16344, 16362, 16374,
    16382, 16384
};
#endif

/* The byte-order probe folds to a constant on both native platforms. */
static int soft3d_float_fixed(const float TG_HUGE *value, int fraction_bits,
                              long *result)
{
    const unsigned char TG_HUGE *bytes;
    const unsigned short endian = 1;
    unsigned long bits, magnitude;
    int exponent, shift;
    if (value == NULL || result == NULL) return 0;
    bytes = (const unsigned char TG_HUGE *)value;
    if (*(const unsigned char *)&endian)
        bits = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
               ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
    else
        bits = (unsigned long)bytes[3] | ((unsigned long)bytes[2] << 8) |
               ((unsigned long)bytes[1] << 16) | ((unsigned long)bytes[0] << 24);
    exponent = (int)((bits >> 23) & 255UL);
    if (exponent < 127 - fraction_bits)
    {
        *result = 0;
        return 1;
    }
    if (exponent > 157 - fraction_bits) return 0;
    magnitude = (bits & 0x7fffffUL) | 0x800000UL;
    shift = exponent - 150 + fraction_bits;
    if (shift < 0) magnitude >>= -shift;
    else magnitude <<= shift;
    *result = (bits & 0x80000000UL) != 0 ? -(long)magnitude : (long)magnitude;
    return 1;
}

#ifndef SOFT3D_TRANSFORM_020
static long soft3d_sine(long angle)
{
    long position, first, second;
    int negative;
    unsigned int degree, fraction;
    angle %= 360L * 256L;
    if (angle < 0) angle += 360L * 256L;
    negative = angle >= 180L * 256L;
    if (negative) angle -= 180L * 256L;
    position = angle > 90L * 256L ? 180L * 256L - angle : angle;
    degree = (unsigned int)(position / 256L);
    fraction = (unsigned int)(position % 256L);
    first = soft3d_sine_quadrant[degree];
    second = degree == 90 ? first : soft3d_sine_quadrant[degree + 1];
    first = (first * (256U - fraction) + second * fraction) / 256L;
    return negative ? -first : first;
}

int soft3d_fixed_prepare(long matrix[3][3],
                          const float TG_HUGE *rotation_x,
                          const float TG_HUGE *rotation_y)
{
    long ax, ay, sx, cx, sy, cy;
    if (matrix == NULL || !soft3d_float_fixed(rotation_x, 8, &ax) ||
        !soft3d_float_fixed(rotation_y, 8, &ay))
        return 0;
    /* Reduce before the cosine quarter-turn so even the largest accepted
     * IEEE inputs cannot overflow a native 32-bit long in that addition. */
    ax %= 360L * 256L;
    ay %= 360L * 256L;
    sx = soft3d_sine(ax); cx = soft3d_sine(ax + 90L * 256L);
    sy = soft3d_sine(ay); cy = soft3d_sine(ay + 90L * 256L);
    /* Rx(user x) Ry(user y) Rx(-90) Rz(-90), followed by z=-3000. */
    matrix[0][0] = sy;
    matrix[0][1] = cy;
    matrix[0][2] = 0;
    matrix[1][0] = -sx * cy / SOFT3D_Q14;
    matrix[1][1] = sx * sy / SOFT3D_Q14;
    matrix[1][2] = cx;
    matrix[2][0] = cx * cy / SOFT3D_Q14;
    matrix[2][1] = -cx * sy / SOFT3D_Q14;
    matrix[2][2] = sx;
    return 1;
}
#endif

#ifdef SOFT3D_TRANSFORM_020
int soft3d_020_transform_reuse(long matrix[3][3], unsigned int count,
#else
int soft3d_fixed_transform_reuse(long matrix[3][3], unsigned int count,
#endif
                                 const Vertex TG_HUGE *vertices,
                                 Soft3d_fixed_point *out,
                                 unsigned int reuse_count)
{
    unsigned int i;
    int row;
    long source[3], transformed;
    if (matrix == NULL || vertices == NULL || out == NULL ||
        count > SOFT3D_MAX_VERTICES ||
        (reuse_count != 0U && (count == 0U || (count & 1U) != 0U ||
                              reuse_count != count / 2U)))
        return 0;
    for (i = 0; i < count; ++i)
    {
        if (!soft3d_float_fixed(&vertices[i].x, 4, &source[0]) ||
            !soft3d_float_fixed(&vertices[i].y, 4, &source[1]) ||
            !soft3d_float_fixed(&vertices[i].z, 4, &source[2]))
            return 0;
        /* Q4 coordinates * Q14 matrix: every sum is below 786432000. */
        if (source[0] < -SOFT3D_MAX_RADIUS * SOFT3D_COORD_SCALE ||
            source[0] > SOFT3D_MAX_RADIUS * SOFT3D_COORD_SCALE ||
            source[1] < -SOFT3D_MAX_RADIUS * SOFT3D_COORD_SCALE ||
            source[1] > SOFT3D_MAX_RADIUS * SOFT3D_COORD_SCALE ||
            source[2] < -SOFT3D_MAX_RADIUS * SOFT3D_COORD_SCALE ||
            source[2] > SOFT3D_MAX_RADIUS * SOFT3D_COORD_SCALE)
            return 0;
        /* Validate every incoming coordinate even on the reused row. Only
         * arithmetic is skipped; malformed vertices cannot hide in a cache. */
        if (reuse_count != 0U && (i & 1U) == 0U)
        {
            out[i] = out[i + 1U];
            continue;
        }
        for (row = 0; row < 3; ++row)
        {
            transformed = (matrix[row][0] * source[0] +
                           matrix[row][1] * source[1] +
                           matrix[row][2] * source[2]) / SOFT3D_Q14;
            if (row == 0) out[i].x = transformed;
            if (row == 1) out[i].y = transformed;
            if (row == 2) out[i].z = transformed - GLOBE_VIEW_DISTANCE * SOFT3D_COORD_SCALE;
        }
    }
    return 1;
}

#ifndef SOFT3D_TRANSFORM_020
int soft3d_fixed_transform(long matrix[3][3], unsigned int count,
                            const Vertex TG_HUGE *vertices,
                            Soft3d_fixed_point *out)
{
    return soft3d_fixed_transform_reuse(matrix, count, vertices, out, 0U);
}

void soft3d_project_cache_reset(Soft3d_project_cache TG_HUGE *cache)
{
    if (cache != NULL)
        cache->count = cache->width = cache->height = 0U;
}

int soft3d_project_cache_begin(Soft3d_project_cache TG_HUGE *cache,
                               unsigned int count,
                               unsigned int width, unsigned int height,
                               unsigned int reuse_count)
{
    unsigned int i;
    int reuse;
    if (cache == NULL) return 0;
    if (count == 0U || count > SOFT3D_MAX_VERTICES ||
        width == 0U || height == 0U || width > 32767U || height > 32767U ||
        (reuse_count != 0U && ((count & 1U) != 0U || reuse_count != count / 2U)))
    {
        soft3d_project_cache_reset(cache);
        return 0;
    }
    reuse = reuse_count != 0U && cache->count == count &&
            cache->width == width && cache->height == height;
    for (i = 0; i < count; ++i)
    {
        if (reuse && (i & 1U) == 0U)
        {
            cache->valid[i] = cache->valid[i + 1U];
            if (cache->valid[i]) cache->screen[i] = cache->screen[i + 1U];
        }
        else cache->valid[i] = 0;
    }
    cache->count = count;
    cache->width = width;
    cache->height = height;
    return 1;
}

/* a*numerator/denominator, |numerator| <= |denominator|. Intersections
 * stay below 2^29, so remainder*2+numerator also fits a native 32-bit long. */
static long soft3d_mul_ratio(long a, long numerator, long denominator)
{
    unsigned long value, num, den, remainder, result, bit;
    int negative;
    if (denominator == 0) return 0;
    negative = (a < 0) != ((numerator < 0) != (denominator < 0));
    value = (unsigned long)(a < 0 ? -a : a);
    num = (unsigned long)(numerator < 0 ? -numerator : numerator);
    den = (unsigned long)(denominator < 0 ? -denominator : denominator);
    result = remainder = 0;
    for (bit = 0x40000000UL; bit != 0; bit >>= 1)
    {
        result <<= 1;
        remainder <<= 1;
        if ((value & bit) != 0) remainder += num;
        if (remainder >= den)
        {
            result += remainder / den;
            remainder %= den;
        }
    }
    return negative ? -(long)result : (long)result;
}

static int soft3d_clip_fixed_z(const Soft3d_fixed_point *in, int count,
                               Soft3d_fixed_point *out, long plane, int greater)
{
    int i, total, a_inside, b_inside;
    Soft3d_fixed_point a, b, point;
    total = 0;
    a = in[count - 1];
    a_inside = greater ? a.z >= plane : a.z <= plane;
    for (i = 0; i < count; ++i)
    {
        b = in[i];
        b_inside = greater ? b.z >= plane : b.z <= plane;
        if (a_inside != b_inside)
        {
            point.x = a.x + soft3d_mul_ratio(b.x - a.x, plane - a.z, b.z - a.z);
            point.y = a.y + soft3d_mul_ratio(b.y - a.y, plane - a.z, b.z - a.z);
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

int soft3d_fixed_project(const Soft3d_fixed_point *points,
                          unsigned int point_count,
                          unsigned int first, unsigned int second,
                          unsigned int third, unsigned int width,
                          unsigned int height, Soft3d_screen_point *out)
{
    unsigned int indices[3];
    indices[0] = first;
    indices[1] = second;
    indices[2] = third;
    return soft3d_fixed_project_polygon(points, point_count, indices, 3U,
                                         width, height, out);
}

int soft3d_fixed_project_polygon(const Soft3d_fixed_point *points,
                                  unsigned int point_count,
                                  const unsigned int TG_HUGE *indices,
                                  unsigned int corners, unsigned int width,
                                  unsigned int height, Soft3d_screen_point *out)
{
    Soft3d_fixed_point a[SOFT3D_MAX_POLY], b[SOFT3D_MAX_POLY];
    long focal;
    unsigned int outside, common, flags;
    int i, count;
    if (points == NULL || indices == NULL || out == NULL ||
        point_count == 0U || point_count > SOFT3D_MAX_VERTICES ||
        corners < 3U || corners > 4U ||
        width == 0 || height == 0 || width > 32767U || height > 32767U)
        return 0;
    /* Validate all indices before copying, then classify depth once. Most
     * globe packets are entirely inside both planes. Outside polygons can
     * be rejected without entering either interpolation pass. */
    for (i = 0; i < (int)corners; ++i)
        if (indices[i] >= point_count) return 0;
    outside = 0U;
    common = 3U;
    for (i = 0; i < (int)corners; ++i)
    {
        a[i] = points[indices[i]];
        flags = 0U;
        if (a[i].z > -GLOBE_NEAR_DISTANCE * SOFT3D_COORD_SCALE) flags |= 1U;
        if (a[i].z < -GLOBE_FAR_DISTANCE * SOFT3D_COORD_SCALE) flags |= 2U;
        outside |= flags;
        common &= flags;
    }
    if (common != 0U) return 0;
    count = (int)corners;
    if (outside != 0U)
    {
        count = soft3d_clip_fixed_z(a, count, b,
                                   -GLOBE_NEAR_DISTANCE * SOFT3D_COORD_SCALE, FALSE);
        if (count < 3) return 0;
        count = soft3d_clip_fixed_z(b, count, a,
                                   -GLOBE_FAR_DISTANCE * SOFT3D_COORD_SCALE, TRUE);
        if (count < 3) return 0;
    }
    focal = (long)height * SOFT3D_FOCAL_Q14 / SOFT3D_Q14;
    for (i = 0; i < count; ++i)
    {
        out[i].x = (long)width / 2L + a[i].x * focal / -a[i].z;
        out[i].y = (long)height / 2L - a[i].y * focal / -a[i].z;
    }
    return count;
}

int soft3d_fixed_project_cached(const Soft3d_fixed_point *points,
                                 unsigned int point_count,
                                 const unsigned int TG_HUGE *indices,
                                 unsigned int corners, unsigned int width,
                                 unsigned int height,
                                 Soft3d_project_cache TG_HUGE *cache,
                                 Soft3d_screen_point *out)
{
    unsigned int i, index;
    long focal;
    if (points == NULL || indices == NULL || out == NULL ||
        point_count == 0U || point_count > SOFT3D_MAX_VERTICES ||
        corners < 3U || corners > 4U || width == 0U || height == 0U ||
        width > 32767U || height > 32767U) return 0;
    for (i = 0; i < corners; ++i)
        if (indices[i] >= point_count) return 0;
    if (cache == NULL || cache->count != point_count ||
        cache->width != width || cache->height != height)
        return soft3d_fixed_project_polygon(points, point_count, indices,
                                             corners, width, height, out);
    /* Clipping generates new intersection vertices. Preserve the original
     * polygon path, ordering, and integer rounding for every crossing. */
    for (i = 0; i < corners; ++i)
    {
        index = indices[i];
        if (points[index].z > -GLOBE_NEAR_DISTANCE * SOFT3D_COORD_SCALE ||
            points[index].z < -GLOBE_FAR_DISTANCE * SOFT3D_COORD_SCALE)
            return soft3d_fixed_project_polygon(points, point_count, indices,
                                                 corners, width, height, out);
    }
    focal = (long)height * SOFT3D_FOCAL_Q14 / SOFT3D_Q14;
    for (i = 0; i < corners; ++i)
    {
        index = indices[i];
        if (!cache->valid[index])
        {
            cache->screen[index].x = (long)width / 2L +
                                    points[index].x * focal / -points[index].z;
            cache->screen[index].y = (long)height / 2L -
                                    points[index].y * focal / -points[index].z;
            cache->valid[index] = 1;
        }
        out[i] = cache->screen[index];
    }
    return (int)corners;
}

static int soft3d_clip_screen(const Soft3d_screen_point *in, int count,
                              Soft3d_screen_point *out, int axis,
                              long edge, int greater)
{
    int i, total, a_inside, b_inside;
    long av, bv;
    Soft3d_screen_point a, b, point;
    total = 0;
    a = in[count - 1];
    av = axis ? a.y : a.x;
    a_inside = greater ? av >= edge : av <= edge;
    for (i = 0; i < count; ++i)
    {
        b = in[i];
        bv = axis ? b.y : b.x;
        b_inside = greater ? bv >= edge : bv <= edge;
        if (a_inside != b_inside)
        {
            point.x = a.x + soft3d_mul_ratio(b.x - a.x, edge - av, bv - av);
            point.y = a.y + soft3d_mul_ratio(b.y - a.y, edge - av, bv - av);
            if (axis) point.y = edge;
            else point.x = edge;
            if (total >= SOFT3D_MAX_POLY) return 0;
            out[total++] = point;
        }
        if (b_inside)
        {
            if (total >= SOFT3D_MAX_POLY) return 0;
            out[total++] = b;
        }
        a = b;
        av = bv;
        a_inside = b_inside;
    }
    return total;
}

static int soft3d_finish_inside(Soft3d_screen_point *points, int count)
{
    long area, area_high;
    unsigned long area_low, previous;
    int i, total;
    /* Pole patches and clipped edges can have coincident corners. Submit
     * their actual polygon, retaining triangles at either collapsed pole. */
    total = 0;
    for (i = 0; i < count; ++i)
    {
        if (total != 0 && points[i].x == points[total - 1].x &&
            points[i].y == points[total - 1].y)
            continue;
        points[total++] = points[i];
    }
    if (total > 1 && points[total - 1].x == points[0].x &&
        points[total - 1].y == points[0].y)
        --total;
    if (total < 3) return 0;
    count = total;
    /* Rounded silhouette quads can contain fan triangles of either sign.
     * Cull by the complete polygon's signed area. Each cross product fits
     * a signed 32-bit long inside this viewport; accumulate its sign in two
     * words so even mixed-sign partial sums cannot overflow on 68000/8086.
     * CCW front faces become clockwise in downward-Y window coordinates. */
    area_high = 0;
    area_low = 0;
    for (i = 1; i < count - 1; ++i)
    {
        area = (points[i].x - points[0].x) *
               (points[i + 1].y - points[0].y) -
               (points[i].y - points[0].y) *
               (points[i + 1].x - points[0].x);
        previous = area_low;
        area_low = (area_low + ((unsigned long)area & 0xFFFFFFFFUL)) &
                   0xFFFFFFFFUL;
        if (area < 0) --area_high;
        if (area_low < previous) ++area_high;
    }
    return area_high < 0 ? count : 0;
}

int soft3d_finish_polygon(Soft3d_screen_point *points, int count,
                           unsigned int width, unsigned int height)
{
    Soft3d_screen_point clipped[SOFT3D_MAX_POLY];
    unsigned int outside, common, flags;
    int i;
    if (points == NULL || count < 3 || count > SOFT3D_MAX_POLY ||
        width == 0U || height == 0U || width > 32767U || height > 32767U)
        return 0;
    outside = 0U;
    common = 15U;
    for (i = 0; i < count; ++i)
    {
        /* Match the floating drivers' conversion bounds. These also bound
         * every subtraction used by the integer intersection routine. */
        if (points[i].x < -134217727L || points[i].x > 134217727L ||
            points[i].y < -134217727L || points[i].y > 134217727L)
            return 0;
        flags = 0U;
        if (points[i].x < 0L) flags |= 1U;
        if (points[i].x > (long)width) flags |= 2U;
        if (points[i].y < 0L) flags |= 4U;
        if (points[i].y > (long)height) flags |= 8U;
        outside |= flags;
        common &= flags;
    }
    if (common != 0U) return 0;
    /* Reject wholly-inside back faces before the general clipping path.
     * The complete rounded polygon area is exact here, including pole
     * duplicates and silhouette quads whose fan triangles have mixed signs.
     * Crossing polygons retain clipping before culling: rounding generated
     * intersections can otherwise change their visibility. */
    if (outside == 0U) return soft3d_finish_inside(points, count);
    count = soft3d_clip_screen(points, count, clipped, 0, 0, TRUE);
    if (count < 3) return 0;
    count = soft3d_clip_screen(clipped, count, points, 0, (long)width, FALSE);
    if (count < 3) return 0;
    count = soft3d_clip_screen(points, count, clipped, 1, 0, TRUE);
    if (count < 3) return 0;
    count = soft3d_clip_screen(clipped, count, points, 1, (long)height, FALSE);
    if (count < 3) return 0;
    return soft3d_finish_inside(points, count);
}
#endif /* !SOFT3D_TRANSFORM_020 */
