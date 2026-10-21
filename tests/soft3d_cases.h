/* Shared public-pipeline cases, independent of either native driver. */
#ifndef TEST_SOFT3D_CASES_H
#define TEST_SOFT3D_CASES_H
#include <assert.h>
#include "soft3d.h"

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
}
#endif
