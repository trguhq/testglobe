/* Shared public-pipeline cases, independent of either native driver. */
#ifndef TEST_SOFT3D_CASES_H
#define TEST_SOFT3D_CASES_H
#include <assert.h>
#include <stddef.h>
#include "soft3d.h"

static void test_soft3d_quad_projection(void)
{
    Soft3d_fixed_point camera[4];
    Soft3d_screen_point points[SOFT3D_MAX_POLY], triangle[SOFT3D_MAX_POLY];
    unsigned int indices[4];
    int i, count;
    /* Q4 camera coordinates describe a square facing the viewer. */
    for (i = 0; i < 4; ++i)
    {
        indices[i] = (unsigned int)i;
        camera[i].x = (i < 2 ? -1600L : 1600L);
        camera[i].y = (i == 0 || i == 3 ? 1600L : -1600L);
        camera[i].z = -16000L;
    }
    count = soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points);
    assert(count == 4);
    assert(soft3d_finish_polygon(points, count, 640, 480) == 4);
    assert(points[0].x == 263 && points[0].y == 183);
    assert(points[1].x == 263 && points[1].y == 297);
    assert(points[2].x == 377 && points[2].y == 297);
    assert(points[3].x == 377 && points[3].y == 183);
    assert(soft3d_fixed_project(camera, 4U, 0U, 1U, 2U, 640, 480, triangle) == 3);
    assert(soft3d_fixed_project_polygon(camera, 4U, indices, 3U, 640, 480, points) == 3);
    for (i = 0; i < 3; ++i)
        assert(points[i].x == triangle[i].x && points[i].y == triangle[i].y);

    /* Both pole arrangements must become actual three-corner polygons. */
    camera[3] = camera[0];
    count = soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points);
    assert(soft3d_finish_polygon(points, count, 640, 480) == 3);
    camera[3].x = 1600L; camera[3].y = 1600L;
    camera[2] = camera[1];
    count = soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points);
    assert(soft3d_finish_polygon(points, count, 640, 480) == 3);

    /* Near-plane clipping preserves a quad instead of introducing a
     * triangulation diagonal. Entirely near/far-excluded quads disappear. */
    for (i = 0; i < 4; ++i)
    {
        camera[i].x = (i < 2 ? -8L : 8L);
        camera[i].y = (i == 0 || i == 3 ? 8L : -8L);
        camera[i].z = (i == 0 || i == 3 ? -8L : -32L);
    }
    count = soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points);
    assert(count == 4 && soft3d_finish_polygon(points, count, 640, 480) == 4);
    for (i = 0; i < 4; ++i) camera[i].z = -15L;
    assert(soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points) == 0);
    for (i = 0; i < 4; ++i) camera[i].z = -160016L;
    assert(soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points) == 0);
    indices[3] = SOFT3D_MAX_VERTICES;
    assert(soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points) == 0);
    assert(soft3d_fixed_project_polygon(camera, 4U, indices, 2U, 640, 480, points) == 0);
    assert(soft3d_fixed_project_polygon(camera, 4U, indices, 5U, 640, 480, points) == 0);
    assert(soft3d_fixed_project_polygon(camera, 4U, NULL, 4U, 640, 480, points) == 0);
}

static void test_soft3d_packet_bounds(void)
{
    Soft3d_fixed_point camera[4], maximum[SOFT3D_MAX_VERTICES];
    Soft3d_screen_point points[SOFT3D_MAX_POLY];
    unsigned int indices[4];
    int i;
    for (i = 0; i < 4; ++i)
    {
        indices[i] = (unsigned int)i;
        camera[i].x = (i < 2 ? -1600L : 1600L);
        camera[i].y = (i == 0 || i == 3 ? 1600L : -1600L);
        camera[i].z = -16000L;
    }
    for (i = 0; i < SOFT3D_MAX_POLY; ++i)
    {
        points[i].x = 123456L;
        points[i].y = -654321L;
    }
    /* This index fits the global capacity but is one past this packet.
     * Reject it before touching the point or changing any output element. */
    indices[3] = 4U;
    assert(soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points) == 0);
    assert(soft3d_fixed_project(camera, 4U, 0U, 1U, 4U, 640, 480, points) == 0);
    indices[3] = 3U;
    assert(soft3d_fixed_project_polygon(camera, 3U, indices, 4U, 640, 480, points) == 0);
    assert(soft3d_fixed_project_polygon(camera, 0U, indices, 4U, 640, 480, points) == 0);
    assert(soft3d_fixed_project_polygon(camera, SOFT3D_MAX_VERTICES + 1U,
                                        indices, 4U, 640, 480, points) == 0);
    assert(soft3d_fixed_project(camera, 0U, 0U, 1U, 2U, 640, 480, points) == 0);
    assert(soft3d_fixed_project(NULL, 4U, 0U, 1U, 2U, 640, 480, points) == 0);
    assert(soft3d_fixed_project(camera, 4U, 0U, 1U, 2U, 640, 480, NULL) == 0);
    for (i = 0; i < SOFT3D_MAX_POLY; ++i)
        assert(points[i].x == 123456L && points[i].y == -654321L);
    /* Both the last point in a short packet and the last supported packet
     * element remain valid; capacity checks must not reject their indices. */
    assert(soft3d_fixed_project_polygon(camera, 4U, indices, 4U, 640, 480, points) == 4);
    for (i = 0; i < SOFT3D_MAX_VERTICES; ++i) maximum[i] = camera[0];
    maximum[1] = camera[1];
    maximum[2] = camera[2];
    maximum[SOFT3D_MAX_VERTICES - 1U] = camera[3];
    indices[3] = SOFT3D_MAX_VERTICES - 1U;
    assert(soft3d_fixed_project_polygon(maximum, SOFT3D_MAX_VERTICES,
                                        indices, 4U, 640, 480, points) == 4);
}

static void test_soft3d_cached_packets(void)
{
    Soft3d_fixed_point camera[6], transformed[6], expected[6];
    Soft3d_screen_point cached[SOFT3D_MAX_POLY], raw[SOFT3D_MAX_POLY];
    Soft3d_project_cache cache;
    Vertex vertices[6];
    unsigned int first[4], second[4];
    long matrix[3][3];
    float rotation_x = 0.0f, rotation_y = 180.0f;
    unsigned int i, j;
    int count, raw_count;
    first[0] = 0U; first[1] = 1U; first[2] = 3U; first[3] = 2U;
    second[0] = 2U; second[1] = 3U; second[2] = 5U; second[3] = 4U;
    for (i = 0; i < 6U; ++i)
    {
        camera[i].x = ((long)(i / 2U) - 1L) * 1600L;
        camera[i].y = (i & 1U) == 0U ? 1600L : -1600L;
        camera[i].z = -16000L;
    }
    soft3d_project_cache_reset(&cache);
    assert(soft3d_project_cache_begin(&cache, 6U, 640U, 480U, 0U));
    count = soft3d_fixed_project_cached(camera, 6U, first, 4U,
                                          640U, 480U, &cache, cached);
    raw_count = soft3d_fixed_project_polygon(camera, 6U, first, 4U,
                                              640U, 480U, raw);
    assert(count == raw_count && count == 4);
    for (i = 0; i < 4U; ++i)
        assert(cached[i].x == raw[i].x && cached[i].y == raw[i].y);
    for (i = 0; i < 4U; ++i) assert(cache.valid[i] == 1U);
    assert(cache.valid[4] == 0U && cache.valid[5] == 0U);
    assert(soft3d_fixed_project_cached(camera, 6U, second, 4U,
                                        640U, 480U, &cache, cached) == 4);
    for (i = 0; i < 6U; ++i) assert(cache.valid[i] == 1U);

    /* Removing the optional cache or calling with a different viewport
     * must produce the uncached result, without consuming stale pixels. */
    count = soft3d_fixed_project_cached(camera, 6U, first, 4U,
                                         320U, 240U, &cache, cached);
    raw_count = soft3d_fixed_project_polygon(camera, 6U, first, 4U,
                                              320U, 240U, raw);
    assert(count == raw_count && count == 4);
    for (i = 0; i < 4U; ++i)
        assert(cached[i].x == raw[i].x && cached[i].y == raw[i].y);
    assert(cache.width == 640U && cache.height == 480U);
    assert(soft3d_fixed_project_cached(camera, 6U, first, 4U,
                                        320U, 240U, NULL, cached) == raw_count);
    for (i = 0; i < 4U; ++i)
        assert(cached[i].x == raw[i].x && cached[i].y == raw[i].y);

    /* Explicit band continuity retains only the previous bottom row;
     * changing viewport dimensions invalidates every retained projection. */
    for (i = 0; i < 3U; ++i) raw[i] = cache.screen[2U * i + 1U];
    assert(soft3d_project_cache_begin(&cache, 6U, 640U, 480U, 3U));
    for (i = 0; i < 3U; ++i)
    {
        assert(cache.valid[2U * i] == 1U && cache.valid[2U * i + 1U] == 0U);
        assert(cache.screen[2U * i].x == raw[i].x &&
               cache.screen[2U * i].y == raw[i].y);
    }
    assert(soft3d_project_cache_begin(&cache, 6U, 320U, 240U, 3U));
    for (i = 0; i < 6U; ++i) assert(cache.valid[i] == 0U);
    assert(!soft3d_project_cache_begin(&cache, 6U, 320U, 240U, 2U));
    assert(cache.count == 0U);
    assert(!soft3d_project_cache_begin(&cache, SOFT3D_MAX_VERTICES + 1U,
                                       320U, 240U, 0U));
    assert(!soft3d_project_cache_begin(NULL, 6U, 320U, 240U, 0U));

    /* A packet can end before every vertex has needed a projection. Only
     * initialized bottom-row entries may survive into the following band. */
    assert(soft3d_project_cache_begin(&cache, 6U, 640U, 480U, 0U));
    assert(soft3d_fixed_project_cached(camera, 6U, first, 4U,
                                        640U, 480U, &cache, cached) == 4);
    assert(soft3d_project_cache_begin(&cache, 6U, 640U, 480U, 3U));
    assert(cache.valid[0] == 1U && cache.valid[2] == 1U && cache.valid[4] == 0U);
    for (i = 1U; i < 6U; i += 2U) assert(cache.valid[i] == 0U);

    /* Reused camera rows match a complete transform exactly. Row validity
     * is a stream-owner contract, but every new input is still validated. */
    assert(soft3d_fixed_prepare(matrix, &rotation_x, &rotation_y));
    for (i = 0; i < 6U; ++i)
    {
        vertices[i].x = (float)((int)(i / 2U) - 1) * 100.0f;
        vertices[i].y = (i & 1U) == 0U ? 100.0f : 0.0f;
        vertices[i].z = 500.0f;
    }
    assert(soft3d_fixed_transform(matrix, 6U, vertices, transformed));
    for (i = 0; i < 3U; ++i)
    {
        vertices[2U * i] = vertices[2U * i + 1U];
        vertices[2U * i + 1U].y = -100.0f;
    }
    assert(soft3d_fixed_transform(matrix, 6U, vertices, expected));
    assert(soft3d_fixed_transform_reuse(matrix, 6U, vertices, transformed, 3U));
    for (i = 0; i < 6U; ++i)
        assert(transformed[i].x == expected[i].x &&
               transformed[i].y == expected[i].y &&
               transformed[i].z == expected[i].z);
    assert(!soft3d_fixed_transform_reuse(matrix, 6U, vertices, transformed, 2U));
    vertices[0].x = 1001.0f;
    assert(!soft3d_fixed_transform_reuse(matrix, 6U, vertices, transformed, 3U));

    /* A late bad vertex leaves a partial camera packet. The owner restarts
     * without row reuse; cache invalidation must also discard earlier pixels. */
    vertices[0].x = -100.0f;
    assert(soft3d_fixed_transform(matrix, 6U, vertices, transformed));
    assert(soft3d_project_cache_begin(&cache, 6U, 640U, 480U, 0U));
    assert(soft3d_fixed_project_cached(transformed, 6U, first, 4U,
                                        640U, 480U, &cache, cached) == 4);
    for (i = 0; i < 3U; ++i)
    {
        vertices[2U * i] = vertices[2U * i + 1U];
        vertices[2U * i + 1U].y = -200.0f;
    }
    vertices[5].x = 1001.0f;
    assert(!soft3d_fixed_transform_reuse(matrix, 6U, vertices, transformed, 3U));
    vertices[5].x = 100.0f;
    assert(soft3d_fixed_transform(matrix, 6U, vertices, expected));
    assert(soft3d_fixed_transform_reuse(matrix, 6U, vertices, transformed, 0U));
    for (i = 0; i < 6U; ++i)
        assert(transformed[i].x == expected[i].x &&
               transformed[i].y == expected[i].y && transformed[i].z == expected[i].z);
    soft3d_project_cache_reset(&cache);
    assert(soft3d_project_cache_begin(&cache, 6U, 640U, 480U, 0U));
    for (i = 0; i < 6U; ++i) assert(cache.valid[i] == 0U);
    count = soft3d_fixed_project_cached(transformed, 6U, first, 4U,
                                         640U, 480U, &cache, cached);
    raw_count = soft3d_fixed_project_polygon(expected, 6U, first, 4U,
                                              640U, 480U, raw);
    assert(count == raw_count && count == 4);
    for (i = 0; i < 4U; ++i)
        assert(cached[i].x == raw[i].x && cached[i].y == raw[i].y);

    /* A depth-plane intersection bypasses original-vertex projections.
     * The fallback preserves the complete clipped polygon and its order. */
    for (i = 0; i < 4U; ++i)
    {
        first[i] = i;
        camera[i].x = i < 2U ? -8L : 8L;
        camera[i].y = i == 0U || i == 3U ? 8L : -8L;
        camera[i].z = i == 0U || i == 3U ? -8L : -32L;
    }
    assert(soft3d_project_cache_begin(&cache, 4U, 640U, 480U, 0U));
    count = soft3d_fixed_project_cached(camera, 4U, first, 4U,
                                         640U, 480U, &cache, cached);
    raw_count = soft3d_fixed_project_polygon(camera, 4U, first, 4U,
                                             640U, 480U, raw);
    assert(count == raw_count && count == 4);
    for (i = 0; i < (unsigned int)count; ++i)
        assert(cached[i].x == raw[i].x && cached[i].y == raw[i].y);
    assert(soft3d_fixed_project_cached(camera, 4U, first, 4U,
                                        640U, 480U, NULL, cached) == raw_count);
    for (i = 0; i < (unsigned int)count; ++i)
        assert(cached[i].x == raw[i].x && cached[i].y == raw[i].y);
    for (i = 0; i < 4U; ++i) assert(cache.valid[i] == 0U);
    for (i = 0; i < SOFT3D_MAX_POLY; ++i)
        cached[i].x = cached[i].y = 123456L;
    first[3] = 4U;
    assert(soft3d_fixed_project_cached(camera, 4U, first, 4U,
                                       640U, 480U, &cache, cached) == 0);
    for (j = 0; j < SOFT3D_MAX_POLY; ++j)
        assert(cached[j].x == 123456L && cached[j].y == 123456L);
}

static void test_soft3d_shared_pipeline(void)
{
    Soft3d_screen_point points[SOFT3D_MAX_POLY], saved;
    long large_matrix[3][3], normalized_matrix[3][3];
    float large_x = 8388607.0f, large_y = -8388607.0f;
    float normalized_x = 247.0f, normalized_y = -247.0f;
    unsigned int corners;
    int count, i, row, column;
    /* Q8 decoding puts this accepted rotation just below LONG_MAX on
     * native systems. Adding a quarter-turn before reducing it would
     * overflow; modulo 360 gives the same exact rotation at 247 degrees. */
    assert(soft3d_fixed_prepare(large_matrix, &large_x, &large_y));
    assert(soft3d_fixed_prepare(normalized_matrix, &normalized_x, &normalized_y));
    for (row = 0; row < 3; ++row)
        for (column = 0; column < 3; ++column)
            assert(large_matrix[row][column] == normalized_matrix[row][column]);
    /* This front-facing triangle encloses the entire viewport. Its edges
     * require interpolation products well beyond a signed 32-bit long. */
    points[0].x = -134000000L; points[0].y = -134000000L;
    points[1].x = -134000000L; points[1].y = 134000000L;
    points[2].x = 134000000L; points[2].y = 0L;
    count = soft3d_finish_polygon(points, 3, 640, 480);
    assert(count == 4);
    corners = 0;
    for (i = 0; i < count; ++i)
    {
        assert(points[i].x == 0 || points[i].x == 640);
        assert(points[i].y == 0 || points[i].y == 480);
        corners |= 1U << ((points[i].x != 0 ? 1 : 0) +
                          (points[i].y != 0 ? 2 : 0));
    }
    assert(corners == 15U);
    saved = points[1]; points[1] = points[3]; points[3] = saved;
    assert(soft3d_finish_polygon(points, count, 640, 480) == 0);
    /* Rounded silhouettes can have a front-facing first fan triangle and
     * a back-facing total area. Check the full polygon, then its reverse. */
    points[0].x = 117; points[0].y = 270;
    points[1].x = 117; points[1].y = 252;
    points[2].x = 116; points[2].y = 216;
    points[3].x = 117; points[3].y = 233;
    assert(soft3d_finish_polygon(points, 4, 640, 480) == 0);
    saved = points[1]; points[1] = points[3]; points[3] = saved;
    assert(soft3d_finish_polygon(points, 4, 640, 480) == 4);
    /* A full-size front quad exceeds a 16-bit int's range; its exact area
     * remains within the supported signed 32-bit coordinate calculation. */
    points[0].x = 0; points[0].y = 0;
    points[1].x = 0; points[1].y = 32767;
    points[2].x = 32767; points[2].y = 32767;
    points[3].x = 32767; points[3].y = 0;
    assert(soft3d_finish_polygon(points, 4, 32767, 32767) == 4);
    test_soft3d_quad_projection();
    test_soft3d_packet_bounds();
    test_soft3d_cached_packets();
}
#endif
