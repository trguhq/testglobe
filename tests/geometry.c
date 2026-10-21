/* Exercise the actual FLAT/TRI globe implementation without a display server. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"

extern Vertex *globe_verts;
extern Color_RGB *globe_colors;
extern Coord *globe_coords;
extern int globe_longitude, globe_latitude;

static unsigned long flat_calls, texture_calls;
static unsigned char *expected_data;

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

void drv_draw_tri_flat_rgb(Vertex *a, Vertex *b, Vertex *c, Color_RGB *color)
{
    unsigned long face;
    unsigned char index;
    check_triangle(a, b, c);
    face = (unsigned long)(color - globe_colors);
    assert(face < globe_faces_num);
    index = expected_data[face];
    assert(color->r == earth_cmap[index][0]);
    assert(color->g == earth_cmap[index][1]);
    assert(color->b == earth_cmap[index][2]);
    ++flat_calls;
}

void drv_draw_tri_flat_uv(Vertex *a, Vertex *b, Vertex *c,
                          Coord *u, Coord *v, Coord *w)
{
    Coord *coords[3];
    int i;
    check_triangle(a, b, c);
    coords[0] = u; coords[1] = v; coords[2] = w;
    for (i = 0; i < 3; ++i) {
        assert(coords[i] >= globe_coords &&
               coords[i] < globe_coords + globe_verts_num);
        assert(coords[i]->u >= 0 && coords[i]->u <= 1);
        assert(coords[i]->v >= 0 && coords[i]->v <= 1);
    }
    ++texture_calls;
}

void drv_texture_enable(void) { drv_texture_enabled = TRUE; }
void drv_texture_disable(void) { drv_texture_enabled = FALSE; }

int main(void)
{
    int sizes[5];
    unsigned char *datasets[5];
    unsigned long expected_tris, row;
    int n;
    sizes[0] = 32; sizes[1] = 64; sizes[2] = 128;
    sizes[3] = 256; sizes[4] = 512;
    datasets[0] = earth_data_32; datasets[1] = earth_data_64;
    datasets[2] = earth_data_128; datasets[3] = earth_data_256;
    datasets[4] = earth_data_512;
    drv_ext_init();
    globe_init();
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
        drv_texture_enabled = FALSE;
        globe_draw_tris();
        assert(flat_calls == expected_tris && texture_calls == 0);
        globe_toggle_tex();
        globe_draw_tris();
        assert(flat_calls == expected_tris && texture_calls == expected_tris);
        globe_toggle_tex();
    }
    assert(globe_verts_num * sizeof(Vertex) > 65535UL);
    assert(globe_faces_num * sizeof(Color_RGB) > 65535UL);
    assert(globe_verts_num * sizeof(Coord) > 65535UL);
    globe_free();
    puts("PASS geometry: five resolutions, outward triangles, RGB mapping, texture UVs, large buffers");
    return 0;
}
