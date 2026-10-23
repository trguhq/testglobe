/* Production IRIS GL indexed/textured strip geometry without an SGI runtime. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "earth_reference.h"
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"

#if !defined(TRISTRIP) || defined(TRI) || defined(QUAD)
#error IRIS GL must select only TRISTRIP in its driver header
#endif
#ifndef TEXTURE
#error IRIS GL must include interactive texture mapping
#endif
#if !defined(DRV_COLORMAP) || !defined(DRV_IGL) || !defined(FLAT)
#error IRIS GL must use FLAT, indexed Earth colors and its own driver
#endif

extern Vertex *globe_verts;
extern Color *globe_colors;
extern Coord *globe_coords;
extern int globe_longitude, globe_latitude;

static unsigned long strip_calls, rendered_triangles, full_bands, cap_strips;
static unsigned long color_calls, uv_calls;
static unsigned char *expected_data;

static void same_vertex(const Vertex *a, const Vertex *b)
{
    assert(a->x == b->x && a->y == b->y && a->z == b->z);
}

static double outward_area(const Vertex *a, const Vertex *b, const Vertex *c)
{
    double ux, uy, uz, vx, vy, vz, nx, ny, nz;
    ux = b->x - a->x; uy = b->y - a->y; uz = b->z - a->z;
    vx = c->x - a->x; vy = c->y - a->y; vz = c->z - a->z;
    nx = uy * vz - uz * vy;
    ny = uz * vx - ux * vz;
    nz = ux * vy - uy * vx;
    return nx * (a->x + b->x + c->x) +
           ny * (a->y + b->y + c->y) +
           nz * (a->z + b->z + c->z);
}

static void check_strip(unsigned int count, const Vertex *vertices,
                          const unsigned char *colors, const Coord *coords)
{
    unsigned int i, cell;
    unsigned long band, index, top, bottom, face, cap[3];
    unsigned char color;
    int polar;
    const Vertex *a, *b, *c;

    polar = FALSE;
    cell = 0;
    if (strip_calls < (unsigned long)globe_longitude) {
        band = 0;
        cell = (unsigned int)strip_calls;
        polar = TRUE;
    } else if (strip_calls < (unsigned long)globe_longitude + globe_latitude - 2UL) {
        band = strip_calls - globe_longitude + 1UL;
    } else {
        band = (unsigned long)globe_latitude - 1UL;
        cell = (unsigned int)(strip_calls - globe_longitude - globe_latitude + 2UL);
        polar = TRUE;
    }
    assert(cell < (unsigned int)globe_longitude);
    top = band * (globe_longitude + 1UL) + cell;
    bottom = top + globe_longitude + 1UL;
    if (polar) {
        assert(count == 3U);
        cap[0] = top + (band == 0 ? 1UL : 0UL);
        cap[1] = bottom;
        cap[2] = band == 0 ? bottom + 1UL : top + 1UL;
        ++cap_strips;
    } else {
        assert(count == 2U * ((unsigned int)globe_longitude + 1U));
        ++full_bands;
    }
    assert(count <= 1026U);
    assert(vertices != NULL);
    assert((coords != NULL) == (drv_texture_enabled != FALSE));
    assert((colors != NULL) == (drv_texture_enabled == FALSE));
    for (i = 0; i < count; ++i) {
        index = polar ? cap[i] :
                (band + i % 2U) * (globe_longitude + 1UL) + i / 2U;
        assert(index < globe_verts_num);
        same_vertex(vertices + i, globe_verts + index);
        if (coords != NULL) {
            assert(coords[i].u == globe_coords[index].u);
            assert(coords[i].v == globe_coords[index].v);
            assert(coords[i].u >= 0.0f && coords[i].u <= 1.0f);
            assert(coords[i].v >= 0.0f && coords[i].v <= 1.0f);
        }
    }
    if (coords != NULL && !polar) {
        assert(coords[0].u == 0.0f && coords[1].u == 0.0f);
        assert(coords[count - 2U].u == 1.0f && coords[count - 1U].u == 1.0f);
    }
    for (i = 0; i < count - 2U; ++i) {
        if (!polar) cell = i / 2U;
        top = band * (globe_longitude + 1UL) + cell;
        bottom = top + globe_longitude + 1UL;
        /* Reconstruct strip winding independently, then compare the exact
         * triangle tuple with the established individual-TRI traversal. */
        if (polar) {
            a = vertices; b = vertices + 1; c = vertices + 2;
            same_vertex(a, globe_verts + cap[0]);
            same_vertex(b, globe_verts + cap[1]);
            same_vertex(c, globe_verts + cap[2]);
        } else if (i % 2U == 0U) {
            a = vertices + i; b = vertices + i + 1U; c = vertices + i + 2U;
            same_vertex(a, globe_verts + top);
            same_vertex(b, globe_verts + bottom);
            same_vertex(c, globe_verts + top + 1UL);
        } else {
            a = vertices + i + 1U; b = vertices + i; c = vertices + i + 2U;
            same_vertex(a, globe_verts + top + 1UL);
            same_vertex(b, globe_verts + bottom);
            same_vertex(c, globe_verts + bottom + 1UL);
        }
        face = band * globe_longitude + cell;
        color = expected_data[face];
        if (colors != NULL) assert(colors[i] == color);
        assert(globe_colors[face].color == color);
        assert(outward_area(a, b, c) > 0.0);
        ++rendered_triangles;
    }
    ++strip_calls;
}

void drv_draw_tristrip_flat_color(unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  const unsigned char TG_HUGE *colors)
{
    assert(!drv_texture_enabled);
    ++color_calls;
    check_strip(count, vertices, colors, NULL);
}

void drv_draw_tristrip_flat_uv(unsigned int count,
                               const Vertex TG_HUGE *vertices,
                               const Coord TG_HUGE *coords)
{
    assert(drv_texture_enabled);
    ++uv_calls;
    check_strip(count, vertices, NULL, coords);
}

void drv_texture_enable(void) { drv_texture_enabled = TRUE; }
void drv_texture_disable(void) { drv_texture_enabled = FALSE; }

static void check_drawing(void)
{
    char stats[128], counts[80];
    strip_calls = rendered_triangles = full_bands = cap_strips = 0UL;
    color_calls = uv_calls = 0UL;
    globe_draw_tristrips();
    assert(strip_calls == (unsigned long)globe_latitude - 2UL + 2UL * globe_longitude);
    assert(full_bands == (unsigned long)globe_latitude - 2UL);
    assert(cap_strips == 2UL * globe_longitude);
    assert(rendered_triangles == globe_tris_num);
    assert(color_calls == (drv_texture_enabled ? 0UL : strip_calls));
    assert(uv_calls == (drv_texture_enabled ? strip_calls : 0UL));
    drv_fps_average = 12.5f;
    drv_format_stats(stats);
    sprintf(counts, "%lu triangles,", rendered_triangles);
    assert(strstr(stats, counts) == stats);
    assert(strstr(stats, "12.50 frames per second") != NULL);
    assert(strstr(stats, "quads") == NULL);
    assert(strstr(stats, "visible") == NULL);
}

int main(void)
{
    int i;
    int sizes[5];
    unsigned char *datasets[5];
    sizes[0] = 32; sizes[1] = 64; sizes[2] = 128;
    sizes[3] = 256; sizes[4] = 512;
    datasets[0] = test_earth_data(32); datasets[1] = test_earth_data(64);
    datasets[2] = test_earth_data(128); datasets[3] = test_earth_data(256);
    datasets[4] = test_earth_data(512);
    drv_ext_init();
    globe_init();
    assert(GLOBE_DEFAULT_LONGITUDE == 128);
    expected_data = test_earth_data(128);
    assert(globe_init_verts(GLOBE_DEFAULT_LONGITUDE, -1, GLOBE_RADIUS));
    assert(globe_longitude == 128);
    check_drawing();
    assert(drv_texture_avail && !drv_texture_enabled);
    for (i = 0; i < 5; ++i) {
        expected_data = datasets[i];
        globe_toggle_res((char)('1' + i));
        assert(globe_longitude == sizes[i]);
        assert(globe_latitude == sizes[i] / 2);
        assert(!drv_texture_enabled);
        check_drawing();
        globe_toggle_tex();
        assert(drv_texture_enabled);
        check_drawing();
        globe_toggle_tex();
        assert(!drv_texture_enabled);
    }
    globe_free();
    puts("PASS IRIS GL indexed/textured strips: default 128, five resolution keys, complete latitude bands, exact palette bytes, TRI-equivalent winding and polar caps");
    puts("PASS IRIS GL strip dispatch: distinct color/UV entry points, original UV coordinates, full 1026-vertex bands and seam coverage");
    return 0;
}
