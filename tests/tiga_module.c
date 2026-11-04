/* The actual downloadable module executes here as ordinary C. Compare its
 * camera/pixel results with the independent shared host transform path,
 * including malformed packet bounds. Native TMS340 ABI is tested separately. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "testglob.h"
#include "globe.h"
#include "soft3d.h"
#include "tga_cmd.h"

static long packet[TGA_GSP_MAX_WORDS + 2U];

static long ieee_word(float value)
{
    unsigned char bytes[4];
    const unsigned short endian = 1;
    unsigned long bits;
    assert(sizeof(float) == 4);
    memcpy(bytes, &value, 4U);
    if (*(const unsigned char *)&endian)
        bits = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
               ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
    else
        bits = (unsigned long)bytes[3] | ((unsigned long)bytes[2] << 8) |
               ((unsigned long)bytes[1] << 16) | ((unsigned long)bytes[0] << 24);
    return (long)bits;
}

static void prepare(unsigned int count, long matrix[3][3],
                    unsigned int width, unsigned int height)
{
    unsigned int i;
    memset(packet, 0, sizeof(packet));
    packet[TGA_GSP_MAGIC] = TGA_GSP_ABI;
    packet[TGA_GSP_COUNT] = count;
    packet[TGA_GSP_WIDTH] = width;
    packet[TGA_GSP_HEIGHT] = height;
    packet[TGA_GSP_FOCAL] = GLOBE_FOCAL_Q14;
    packet[TGA_GSP_VIEW] = GLOBE_VIEW_DISTANCE;
    packet[TGA_GSP_NEAR] = GLOBE_NEAR_DISTANCE;
    packet[TGA_GSP_FAR] = GLOBE_FAR_DISTANCE;
    for (i = 0; i < 9; ++i) packet[TGA_GSP_MATRIX + i] = matrix[i / 3U][i % 3U];
    packet[TGA_GSP_WORDS(count)] = 0x12345678L;
    packet[TGA_GSP_WORDS(count) + 1U] = 0x76543210L;
}

static void compare(unsigned int count, float x_angle, float y_angle,
                    unsigned int width, unsigned int height)
{
    static Vertex vertices[SOFT3D_MAX_VERTICES];
    static Soft3d_fixed_point camera[SOFT3D_MAX_VERTICES];
    static Soft3d_project_cache cache;
    Soft3d_screen_point projected[SOFT3D_MAX_POLY];
    long matrix[3][3];
    unsigned int i, start, corners[3];
    assert(soft3d_fixed_prepare(matrix, &x_angle, &y_angle));
    prepare(count, matrix, width, height);
    for (i = 0; i < count; ++i)
    {
        vertices[i].x = (float)((long)(i * 71U % 2001U) - 1000L);
        vertices[i].y = (float)((long)(i * 167U % 2001U) - 1000L) + 0.03125f;
        vertices[i].z = (float)((long)(i * 251U % 2001U) - 1000L);
        start = TGA_GSP_HEADER + i * TGA_GSP_VERTEX_WORDS;
        packet[start] = ieee_word(vertices[i].x);
        packet[start + 1U] = ieee_word(vertices[i].y);
        packet[start + 2U] = ieee_word(vertices[i].z);
    }
    assert(soft3d_fixed_transform(matrix, count, vertices, camera));
    tga_gsp_transform(TGA_GSP_WORDS(count), packet);
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_OK);
    assert(packet[TGA_GSP_WORDS(count)] == 0x12345678L);
    assert(packet[TGA_GSP_WORDS(count) + 1U] == 0x76543210L);
    soft3d_project_cache_reset(&cache);
    assert(soft3d_project_cache_begin(&cache, count, width, height, 0));
    for (i = 0; i < count; ++i)
    {
        start = TGA_GSP_HEADER + i * TGA_GSP_VERTEX_WORDS;
        assert(packet[start] == camera[i].x);
        assert(packet[start + 1U] == camera[i].y);
        assert(packet[start + 2U] == camera[i].z);
        assert(packet[start + TGA_GSP_PROJECTED] == 1L);
        corners[0] = corners[1] = corners[2] = i;
        assert(soft3d_fixed_project_cached(camera, count, corners, 3, width, height,
                                           &cache, projected) == 3);
        assert(packet[start + TGA_GSP_SCREEN_X] == projected[0].x);
        assert(packet[start + TGA_GSP_SCREEN_Y] == projected[0].y);
    }
}

int main(void)
{
    long matrix[3][3];
    float zero = 0.0f;
    unsigned int i;
    static const float angles[][2] = {
        {0.0f, 0.0f}, {17.25f, -39.5f}, {90.0f, 180.0f},
        {-179.75f, 359.75f}, {7200.0f, -7200.0f}
    };
    assert(soft3d_fixed_prepare(matrix, &zero, &zero));
    for (i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i)
    {
        compare(1, angles[i][0], angles[i][1], 640, 480);
        compare(130, angles[i][0], angles[i][1], 640, 480);
        compare(130, angles[i][0], angles[i][1], 32767, 32767);
        compare(130, angles[i][0], angles[i][1], 1, 1);
    }
    prepare(0, matrix, 640, 480);
    tga_gsp_transform(TGA_GSP_WORDS(0), packet);
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_OK);
    tga_gsp_transform(100, NULL);
    prepare(1, matrix, 640, 480);
    tga_gsp_transform(1, packet);
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_PENDING);
    tga_gsp_transform(2, packet);
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_BAD_PACKET);
    prepare(1, matrix, 640, 480);
    packet[TGA_GSP_COUNT] = 131;
    tga_gsp_transform(TGA_GSP_WORDS(1), packet);
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_BAD_PACKET);
    prepare(1, matrix, 640, 480);
    packet[TGA_GSP_MAGIC] ^= 1;
    tga_gsp_transform(TGA_GSP_WORDS(1), packet);
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_BAD_PACKET);
    prepare(1, matrix, 640, 480);
    packet[TGA_GSP_MATRIX] = 16385L;
    tga_gsp_transform(TGA_GSP_WORDS(1), packet);
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_BAD_PACKET);
    prepare(1, matrix, 640, 480);
    packet[TGA_GSP_WIDTH] = 32768L;
    tga_gsp_transform(TGA_GSP_WORDS(1), packet);
    assert(packet[TGA_GSP_STATUS] == TGA_GSP_BAD_PACKET);
    for (i = 0; i < 3; ++i)
    {
        prepare(1, matrix, 640, 480);
        packet[TGA_GSP_HEADER] = i == 0 ? 0x7f800000L :
                                   (i == 1 ? 0x7fc00001L : ieee_word(1001.0f));
        tga_gsp_transform(TGA_GSP_WORDS(1), packet);
        assert(packet[TGA_GSP_STATUS] == TGA_GSP_BAD_RANGE);
        assert(packet[TGA_GSP_WORDS(1)] == 0x12345678L);
    }
    puts("PASS TIGA module: camera coordinates and projection match host fixed-point arithmetic; bounded packets, handshake, malformed fields and IEEE range rejection");
    return 0;
}
