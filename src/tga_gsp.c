/*
 * testglobe - downloadable TIGA 2.0 TMS34010/34020 transform module.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile with TI's TMS340 compiler, not the Microsoft DOS compiler. This
 * freestanding C89 module uses only bounded integer arithmetic and requires
 * neither a TMS34082 nor a TI floating-point runtime. Keep rounding in step
 * with soft3d.c; tests compare this actual entry point with that host pipeline.
 */
#include "tga_cmd.h"

#define TGA_GSP_Q14 16384L
#define TGA_GSP_SCALE 16L
#define TGA_GSP_RADIUS 1000L

/* C89 permits a negative quotient to round down. TI's compiler uses an
 * arithmetic shift for signed division by a power of two, so spell out
 * truncation toward zero to match the Microsoft/Unix host pipelines.
 * All callers prove value != LONG_MIN and divisor > 0.
 */
static long tga_gsp_divide(long value, long divisor)
{
    return value < 0L ? -((-value) / divisor) : value / divisor;
}

/* Decode the host's IEEE coordinate without using the TMS compiler's native
 * floating representation (which need not be IEEE unless explicitly chosen).
 */
static int tga_gsp_coordinate(long input, long *out)
{
    unsigned long bits, magnitude;
    int exponent, shift;
    bits = (unsigned long)input & 0xffffffffUL;
    exponent = (int)((bits >> 23) & 255UL);
    if (exponent < 123)
    {
        *out = 0L;
        return 1;
    }
    if (exponent > 153) return 0;
    magnitude = (bits & 0x7fffffUL) | 0x800000UL;
    shift = exponent - 146;
    if (shift < 0) magnitude >>= -shift;
    else magnitude <<= shift;
    if (magnitude > (unsigned long)(TGA_GSP_RADIUS * TGA_GSP_SCALE))
        return 0;
    *out = (bits & 0x80000000UL) != 0UL ? -(long)magnitude : (long)magnitude;
    return 1;
}

void tga_gsp_transform(unsigned long words, long *packet)
{
    unsigned int count, i, row, start;
    long matrix[9], source[3], camera[3];
    long width, height, focal, view, near_z, far_z, value;
    if (packet == 0 || words < 2UL) return;
    packet[TGA_GSP_STATUS] = TGA_GSP_BAD_PACKET;
    if (words < TGA_GSP_HEADER || words > TGA_GSP_MAX_WORDS ||
        packet[TGA_GSP_MAGIC] != TGA_GSP_ABI ||
        packet[TGA_GSP_COUNT] < 0L ||
        packet[TGA_GSP_COUNT] > (long)TGA_GSP_MAX_VERTICES)
        return;
    count = (unsigned int)packet[TGA_GSP_COUNT];
    if (words != (unsigned long)TGA_GSP_WORDS(count)) return;
    width = packet[TGA_GSP_WIDTH];
    height = packet[TGA_GSP_HEIGHT];
    /* These bounds retain soft3d.c's signed-32-bit product proof. The focal,
     * view and depth values describe testglobe's current scene convention;
     * rejecting incompatible callers is safer than overflowing silently.
     */
    if (width < 1L || width > 32767L || height < 1L || height > 32767L ||
        packet[TGA_GSP_FOCAL] != 19777L ||
        packet[TGA_GSP_VIEW] != 3000L ||
        packet[TGA_GSP_NEAR] != 1L || packet[TGA_GSP_FAR] != 10000L)
        return;
    for (i = 0U; i < 9U; ++i)
    {
        matrix[i] = packet[TGA_GSP_MATRIX + i];
        if (matrix[i] < -TGA_GSP_Q14 || matrix[i] > TGA_GSP_Q14) return;
    }
    focal = height * packet[TGA_GSP_FOCAL] / TGA_GSP_Q14;
    view = packet[TGA_GSP_VIEW] * TGA_GSP_SCALE;
    near_z = -packet[TGA_GSP_NEAR] * TGA_GSP_SCALE;
    far_z = -packet[TGA_GSP_FAR] * TGA_GSP_SCALE;
    packet[TGA_GSP_STATUS] = TGA_GSP_BAD_RANGE;
    for (i = 0U; i < count; ++i)
    {
        start = TGA_GSP_HEADER + i * TGA_GSP_VERTEX_WORDS;
        if (!tga_gsp_coordinate(packet[start + TGA_GSP_X], &source[0]) ||
            !tga_gsp_coordinate(packet[start + TGA_GSP_Y], &source[1]) ||
            !tga_gsp_coordinate(packet[start + TGA_GSP_Z], &source[2]))
            return;
        /* Each product is at most 262144000; each sum at most 786432000.
         * Division truncates toward zero, matching the shared host pipeline.
         */
        for (row = 0U; row < 3U; ++row)
        {
            value = matrix[3U * row] * source[0] +
                    matrix[3U * row + 1U] * source[1] +
                    matrix[3U * row + 2U] * source[2];
            camera[row] = tga_gsp_divide(value, TGA_GSP_Q14);
        }
        camera[2] -= view;
        packet[start + TGA_GSP_X] = camera[0];
        packet[start + TGA_GSP_Y] = camera[1];
        packet[start + TGA_GSP_Z] = camera[2];
        packet[start + TGA_GSP_SCREEN_X] = 0L;
        packet[start + TGA_GSP_SCREEN_Y] = 0L;
        packet[start + TGA_GSP_PROJECTED] = 0L;
        if (camera[2] <= near_z && camera[2] >= far_z)
        {
            /* At the largest accepted viewport, abs(x/y)*focal is less
             * than 1.9 billion. The depth guard excludes division by zero.
             */
            packet[start + TGA_GSP_SCREEN_X] = width / 2L +
                          tga_gsp_divide(camera[0] * focal, -camera[2]);
            packet[start + TGA_GSP_SCREEN_Y] = height / 2L -
                          tga_gsp_divide(camera[1] * focal, -camera[2]);
            packet[start + TGA_GSP_PROJECTED] = 1L;
        }
    }
    packet[TGA_GSP_STATUS] = TGA_GSP_OK;
}
