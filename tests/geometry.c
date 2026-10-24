/* Exercise the actual FLAT/TRI globe implementation without a display server. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earth_reference.h"
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"

extern Vertex *globe_verts;
extern int globe_longitude, globe_latitude;

static unsigned long flat_calls, texture_calls;
static unsigned char *expected_data;
static Vertex *saved_verts;

static void invalid_radius_cleanup(void)
{
    assert(globe_verts == saved_verts);
    assert(globe_longitude == 32 && globe_latitude == 16);
    assert(globe_verts_num == 561UL && globe_faces_num == 512UL);
    assert(globe_verts[0].z == GLOBE_RADIUS);
    globe_free();
    puts("PASS invalid globe radius: nonfinite input fails before replacing the existing mesh");
}

static void check_triangle(Vertex *a, Vertex *b, Vertex *c)
{
    double ux, uy, uz, vx, vy, vz, nx, ny, nz, dot;
    assert(a >= globe_verts && a < globe_verts + globe_verts_num);
    assert(b >= globe_verts && b < globe_verts + globe_verts_num);
    assert(c >= globe_verts && c < globe_verts + globe_verts_num);
    ux = b->x - a->x; uy = b->y - a->y; uz = b->z - a->z;
    vx = c->x - a->x; vy = c->y - a->y; vz = c->z - a->z;
    nx = uy * vz - uz * vy;
    ny = uz * vx - ux * vz;
    nz = ux * vy - uy * vx;
    dot = nx * (a->x + b->x + c->x) +
          ny * (a->y + b->y + c->y) +
          nz * (a->z + b->z + c->z);
    assert(dot > 0.0); /* outward winding; no degenerate polar triangles */
}

void drv_draw_tri_flat_color(Vertex *a, Vertex *b, Vertex *c, unsigned char color)
{
    unsigned long row, column, index, vertex[3], r, cindex;
    int i;
    check_triangle(a, b, c);
    vertex[0] = (unsigned long)(a - globe_verts);
    vertex[1] = (unsigned long)(b - globe_verts);
    vertex[2] = (unsigned long)(c - globe_verts);
    row = column = 0xffffffffUL;
    for (i = 0; i < 3; ++i) {
        r = vertex[i] / (globe_longitude + 1UL);
        cindex = vertex[i] % (globe_longitude + 1UL);
        if (r < row) row = r;
        if (cindex < column) column = cindex;
    }
    index = row * globe_longitude + column;
    assert(index < globe_faces_num);
    assert(color == expected_data[index]);
    ++flat_calls;
}

void drv_draw_tri_flat_uv(Vertex *a, Vertex *b, Vertex *c,
                          Coord *u, Coord *v, Coord *w)
{
    Coord *coords[3];
    Vertex *vertices[3];
    unsigned long index;
    int i;
    check_triangle(a, b, c);
    coords[0] = u; coords[1] = v; coords[2] = w;
    vertices[0] = a; vertices[1] = b; vertices[2] = c;
    for (i = 0; i < 3; ++i) {
        index = (unsigned long)(vertices[i] - globe_verts);
        assert(coords[i]->u == (float)(index % (globe_longitude + 1UL)) / globe_longitude);
        assert(coords[i]->v == (float)(index / (globe_longitude + 1UL)) / globe_latitude);
        assert(coords[i]->u >= 0 && coords[i]->u <= 1);
        assert(coords[i]->v >= 0 && coords[i]->v <= 1);
    }
    ++texture_calls;
}

void drv_texture_enable(void) { drv_texture_enabled = drv_texture_active = TRUE; }
void drv_texture_disable(void) { drv_texture_enabled = drv_texture_active = FALSE; }
void drv_release_optional(void) {}

static float special_radius(unsigned long pattern)
{
    float value;
    unsigned char *bytes;
    const unsigned short endian = 1;
    unsigned int i, position;
    assert(sizeof(value) == 4U);
    bytes = (unsigned char *)&value;
    for (i = 0; i < 4U; ++i) {
        position = *(const unsigned char *)&endian ? i : 3U - i;
        bytes[position] = (unsigned char)(pattern >> (8U * i));
    }
    return value;
}

int main(int argc, char **argv)
{
    int sizes[5];
    unsigned char *datasets[5];
    unsigned long expected_tris, row;
    int n;
    unsigned long vertex;
    char stats[128], counts[80];
    unsigned char coarse_colors[96];
    unsigned int latitude, column, source_row;
    sizes[0] = 32; sizes[1] = 64; sizes[2] = 128;
    sizes[3] = 256; sizes[4] = 512;
    datasets[0] = test_earth_data(32); datasets[1] = test_earth_data(64);
    datasets[2] = test_earth_data(128); datasets[3] = test_earth_data(256);
    datasets[4] = test_earth_data(512);
    drv_ext_init();
    globe_init();
    if (argc > 1) {
        assert(strcmp(argv[1], "nan") == 0 || strcmp(argv[1], "infinity") == 0);
        assert(globe_init_verts(32, -1, GLOBE_RADIUS));
        saved_verts = globe_verts;
        assert(!globe_init_verts(32, -1, special_radius(strcmp(argv[1], "nan") == 0 ?
                                               0x7fc00000UL : 0x7f800000UL)));
        assert(globe_last_result == DRV_BAD_INPUT);
        invalid_radius_cleanup();
        return 0;
    }
    assert(GLOBE_DEFAULT_LONGITUDE == 512);
    for (n = 0; n < 5; ++n) {
        expected_data = datasets[n];
        assert(globe_init_verts(sizes[n], -1, GLOBE_RADIUS));
        assert(globe_longitude == sizes[n]);
        assert(globe_latitude == sizes[n] / 2);
        assert(globe_verts_num == (unsigned long)(sizes[n] + 1) *
                                   (unsigned long)(sizes[n] / 2 + 1));
        assert(globe_faces_num == (unsigned long)sizes[n] * (sizes[n] / 2));
        expected_tris = 2UL * sizes[n] * (sizes[n] / 2 - 1);
        assert(globe_tris_num == expected_tris);
        row = (unsigned long)globe_latitude * (globe_longitude + 1UL);
        assert(globe_verts[0].x == 0 && globe_verts[0].y == 0);
        assert(globe_verts[row].x == 0 && globe_verts[row].y == 0);
        assert(globe_verts[0].z == GLOBE_RADIUS);
        assert(globe_verts[row].z == -GLOBE_RADIUS);
        flat_calls = texture_calls = 0;
        drv_texture_enabled = drv_texture_active = FALSE;
        globe_draw_tris();
        assert(flat_calls == expected_tris && texture_calls == 0);
        globe_toggle_tex();
        drv_fps_average = 12.5f;
        /* Native culling supplies no visibility counter to report. */
        globe_tris_num_vis = 12345UL;
        drv_format_stats(stats);
        sprintf(counts, "%lu triangles,", expected_tris);
        assert(strstr(stats, counts) == stats);
        assert(strstr(stats, "12.50 frames per second") != NULL);
        assert(strstr(stats, "quads") == NULL);
        assert(strstr(stats, "visible") == NULL);
        globe_draw_tris();
        assert(flat_calls == expected_tris && texture_calls == expected_tris);
        globe_toggle_tex();
    }
    assert(globe_verts_num * sizeof(Vertex) > 65535UL);
    assert(sizeof(Color) == 1);
    assert(globe_faces_num * sizeof(Color) > 65535UL);
    assert(globe_verts_num * sizeof(Coord) > 65535UL);
    /* Three latitude bands still represent the full source map. Center
     * sampling must reach southern rows rather than cropping at row two. */
    for (latitude = 0; latitude < 3U; ++latitude) {
        source_row = (2U * latitude + 1U) * 16U / 6U;
        for (column = 0; column < 32U; ++column)
            coarse_colors[latitude * 32U + column] =
                test_earth_data(32)[source_row * 32U + column];
    }
    expected_data = coarse_colors;
    assert(globe_init_verts(32, 3, GLOBE_RADIUS));
    assert(globe_latitude == 3 && globe_faces_num == 96UL);
    assert(globe_tris_num == 128UL);
    flat_calls = texture_calls = 0;
    globe_draw_tris();
    assert(flat_calls == 128UL && texture_calls == 0UL);
    globe_toggle_tex();
    globe_draw_tris();
    assert(texture_calls == 128UL);
    globe_toggle_tex();
    /* Unbounded native-float geometry must not inherit a software-driver
     * radius limit. This exercises construction only, not camera visibility. */
    assert(globe_init_verts(32, -1, 1500.0f));
    assert(globe_verts[0].z == 1500.0f);
    assert(globe_init_verts(32, -1, 2000.0f));
    assert(globe_verts[0].z == 2000.0f);
    for (vertex = 0UL; vertex < globe_verts_num; ++vertex) {
        assert(globe_verts[vertex].x >= -2000.0f && globe_verts[vertex].x <= 2000.0f);
        assert(globe_verts[vertex].y >= -2000.0f && globe_verts[vertex].y <= 2000.0f);
        assert(globe_verts[vertex].z >= -2000.0f && globe_verts[vertex].z <= 2000.0f);
    }
    saved_verts = globe_verts;
    assert(!globe_init_verts(32, -1, 0.0f));
    assert(!globe_init_verts(32, -1, -1.0f));
    assert(globe_last_result == DRV_BAD_INPUT && globe_verts == saved_verts);
    globe_free();
    puts("PASS native float radii: larger finite geometry accepted, nonpositive requests preserve existing mesh");
    puts("PASS geometry: five resolutions, outward triangles, original palette indices, texture UVs, large buffers");
    puts("PASS custom globe latitudes: full-map center sampling, outward pole triangles and unchanged UV range");
    return 0;
}
