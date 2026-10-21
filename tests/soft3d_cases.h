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
    count = soft3d_fixed_project_polygon(camera, indices, 4U, 640, 480, points);
    assert(count == 4);
    assert(soft3d_finish_polygon(points, count, 640, 480) == 4);
    assert(points[0].x == 263 && points[0].y == 183);
    assert(points[1].x == 263 && points[1].y == 297);
    assert(points[2].x == 377 && points[2].y == 297);
    assert(points[3].x == 377 && points[3].y == 183);
    assert(soft3d_fixed_project(camera, 0U, 1U, 2U, 640, 480, triangle) == 3);
    assert(soft3d_fixed_project_polygon(camera, indices, 3U, 640, 480, points) == 3);
    for (i = 0; i < 3; ++i)
        assert(points[i].x == triangle[i].x && points[i].y == triangle[i].y);

    /* Both pole arrangements must become actual three-corner polygons. */
    camera[3] = camera[0];
    count = soft3d_fixed_project_polygon(camera, indices, 4U, 640, 480, points);
    assert(soft3d_finish_polygon(points, count, 640, 480) == 3);
    camera[3].x = 1600L; camera[3].y = 1600L;
    camera[2] = camera[1];
    count = soft3d_fixed_project_polygon(camera, indices, 4U, 640, 480, points);
    assert(soft3d_finish_polygon(points, count, 640, 480) == 3);

    /* Near-plane clipping preserves a quad instead of introducing a
     * triangulation diagonal. Entirely near/far-excluded quads disappear. */
    for (i = 0; i < 4; ++i)
    {
        camera[i].x = (i < 2 ? -8L : 8L);
        camera[i].y = (i == 0 || i == 3 ? 8L : -8L);
        camera[i].z = (i == 0 || i == 3 ? -8L : -32L);
    }
    count = soft3d_fixed_project_polygon(camera, indices, 4U, 640, 480, points);
    assert(count == 4 && soft3d_finish_polygon(points, count, 640, 480) == 4);
    for (i = 0; i < 4; ++i) camera[i].z = -15L;
    assert(soft3d_fixed_project_polygon(camera, indices, 4U, 640, 480, points) == 0);
    for (i = 0; i < 4; ++i) camera[i].z = -160016L;
    assert(soft3d_fixed_project_polygon(camera, indices, 4U, 640, 480, points) == 0);
    indices[3] = SOFT3D_MAX_VERTICES;
    assert(soft3d_fixed_project_polygon(camera, indices, 4U, 640, 480, points) == 0);
    assert(soft3d_fixed_project_polygon(camera, indices, 2U, 640, 480, points) == 0);
    assert(soft3d_fixed_project_polygon(camera, indices, 5U, 640, 480, points) == 0);
    assert(soft3d_fixed_project_polygon(camera, NULL, 4U, 640, 480, points) == 0);
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
}
#endif
