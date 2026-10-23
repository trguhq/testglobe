/* Production OpenGL strips and separate pole triangles without a GL runtime. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "earth_reference.h"
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"

#if !defined(TRISTRIP) || !defined(DRV_TRI_CAPS) || defined(TRI) || defined(QUAD)
#error OpenGL must select TRISTRIP with separate TRI pole caps
#endif
#if !defined(TEXTURE) || !defined(DRV_COLORMAP) || !defined(DRV_OGL) || !defined(FLAT)
#error OpenGL must retain FLAT, indexed Earth colors and interactive textures
#endif

extern Vertex *globe_verts;
extern Color *globe_colors;
extern Coord *globe_coords;
extern int globe_longitude, globe_latitude;

static unsigned long cap_calls, strip_calls, rendered_triangles;
static unsigned long cap_color_calls, cap_uv_calls, strip_color_calls, strip_uv_calls;
static unsigned long submitted_vertices;
static unsigned int largest_strip;
static int draw_phase;
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

static void check_cap(Vertex *a, Vertex *b, Vertex *c,
                      Coord *ua, Coord *ub, Coord *uc, unsigned char color)
{
    unsigned long band, cell, top, bottom, expected[3], face;
    Vertex *vertices[3];
    Coord *coords[3];
    int i;

    assert(draw_phase == 1);
    assert(cap_calls < 2UL * globe_longitude);
    band = cap_calls < (unsigned long)globe_longitude ?
           0UL : (unsigned long)globe_latitude - 1UL;
    cell = cap_calls % globe_longitude;
    top = band * (globe_longitude + 1UL) + cell;
    bottom = top + globe_longitude + 1UL;
    /* These are the retained TRI traversal's exact cap tuples.  In
     * particular each coincident pole keeps its original longitude UV. */
    expected[0] = top + (band == 0UL ? 1UL : 0UL);
    expected[1] = bottom;
    expected[2] = band == 0UL ? bottom + 1UL : top + 1UL;
    vertices[0] = a; vertices[1] = b; vertices[2] = c;
    coords[0] = ua; coords[1] = ub; coords[2] = uc;
    for (i = 0; i < 3; ++i) {
        assert(vertices[i] == globe_verts + expected[i]);
        if (drv_texture_enabled) {
            assert(coords[i] == globe_coords + expected[i]);
            assert(coords[i]->u >= 0.0f && coords[i]->u <= 1.0f);
            assert(coords[i]->v >= 0.0f && coords[i]->v <= 1.0f);
        } else {
            assert(coords[i] == NULL);
        }
    }
    face = band * globe_longitude + cell;
    assert(globe_colors[face].color == expected_data[face]);
    if (!drv_texture_enabled) assert(color == expected_data[face]);
    assert(outward_area(a, b, c) > 0.0);
    ++cap_calls;
    ++rendered_triangles;
    submitted_vertices += 3UL;
}

static void check_strip(unsigned int count, const Vertex *vertices,
                         const unsigned char *colors, const Coord *coords)
{
    unsigned int i, cell;
    unsigned long band, index, top, bottom, face;
    const Vertex *a, *b, *c;

    assert(draw_phase == 2);
    band = strip_calls + 1UL;
    assert(band < (unsigned long)globe_latitude - 1UL);
    assert(count == 2U * ((unsigned int)globe_longitude + 1U));
    assert(count <= 1026U);
    assert(vertices != NULL);
    assert((coords != NULL) == (drv_texture_enabled != FALSE));
    assert((colors != NULL) == (drv_texture_enabled == FALSE));
    for (i = 0; i < count; ++i) {
        index = (band + i % 2U) * (globe_longitude + 1UL) + i / 2U;
        assert(index < globe_verts_num);
        same_vertex(vertices + i, globe_verts + index);
        if (coords != NULL) {
            assert(coords[i].u == globe_coords[index].u);
            assert(coords[i].v == globe_coords[index].v);
            assert(coords[i].u >= 0.0f && coords[i].u <= 1.0f);
            assert(coords[i].v > 0.0f && coords[i].v < 1.0f);
        }
    }
    if (coords != NULL) {
        assert(coords[0].u == 0.0f && coords[1].u == 0.0f);
        assert(coords[count - 2U].u == 1.0f && coords[count - 1U].u == 1.0f);
    }
    for (i = 0; i < count - 2U; ++i) {
        cell = i / 2U;
        top = band * (globe_longitude + 1UL) + cell;
        bottom = top + globe_longitude + 1UL;
        /* Apply triangle-strip parity independently, then compare each
         * ordered tuple against the established individual TRI traversal. */
        if (i % 2U == 0U) {
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
        assert(globe_colors[face].color == expected_data[face]);
        if (colors != NULL) assert(colors[i] == expected_data[face]);
        assert(outward_area(a, b, c) > 0.0);
        ++rendered_triangles;
    }
    if (count > largest_strip) largest_strip = count;
    submitted_vertices += count;
    ++strip_calls;
}

void drv_draw_tri_flat_color(Vertex *a, Vertex *b, Vertex *c, unsigned char color)
{
    assert(!drv_texture_enabled);
    ++cap_color_calls;
    check_cap(a, b, c, NULL, NULL, NULL, color);
}

void drv_draw_tri_flat_uv(Vertex *a, Vertex *b, Vertex *c,
                          Coord *ua, Coord *ub, Coord *uc)
{
    assert(drv_texture_enabled);
    ++cap_uv_calls;
    check_cap(a, b, c, ua, ub, uc, 0);
}

void drv_draw_tristrip_flat_color(unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  const unsigned char TG_HUGE *colors)
{
    assert(!drv_texture_enabled);
    ++strip_color_calls;
    check_strip(count, vertices, colors, NULL);
}

void drv_draw_tristrip_flat_uv(unsigned int count,
                               const Vertex TG_HUGE *vertices,
                               const Coord TG_HUGE *coords)
{
    assert(drv_texture_enabled);
    ++strip_uv_calls;
    check_strip(count, vertices, NULL, coords);
}

void drv_texture_enable(void) { drv_texture_enabled = TRUE; }
void drv_texture_disable(void) { drv_texture_enabled = FALSE; }

static void check_drawing(void)
{
    unsigned long expected_caps, expected_bands, expected_submissions;
    char stats[128], counts[80];
    cap_calls = strip_calls = rendered_triangles = submitted_vertices = 0UL;
    cap_color_calls = cap_uv_calls = strip_color_calls = strip_uv_calls = 0UL;
    largest_strip = 0U;
    expected_caps = 2UL * globe_longitude;
    expected_bands = (unsigned long)globe_latitude - 2UL;
    expected_submissions = expected_bands * 2UL * (globe_longitude + 1UL) +
                           expected_caps * 3UL;
    draw_phase = 1;
    globe_draw_pole_tris();
    assert(cap_calls == expected_caps && strip_calls == 0UL);
    assert(rendered_triangles == expected_caps);
    draw_phase = 2;
    globe_draw_tristrips();
    draw_phase = 0;
    assert(cap_calls == expected_caps && strip_calls == expected_bands);
    assert(rendered_triangles == globe_tris_num);
    assert(submitted_vertices == expected_submissions);
    assert(largest_strip == 2U * ((unsigned int)globe_longitude + 1U));
    assert(cap_color_calls == (drv_texture_enabled ? 0UL : expected_caps));
    assert(cap_uv_calls == (drv_texture_enabled ? expected_caps : 0UL));
    assert(strip_color_calls == (drv_texture_enabled ? 0UL : expected_bands));
    assert(strip_uv_calls == (drv_texture_enabled ? expected_bands : 0UL));
    if (globe_longitude == 512) {
        assert(largest_strip == 1026U);
        assert(strip_calls == 254UL && cap_calls == 1024UL);
        assert(rendered_triangles == 261120UL);
        assert(submitted_vertices == 263676UL);
    }
    drv_fps_average = 12.5f;
    globe_tris_num_vis = 12345UL;
    drv_format_stats(stats);
    sprintf(counts, "%lu triangles,", rendered_triangles);
    assert(strstr(stats, counts) == stats);
    assert(strstr(stats, "12.50 frames per second") != NULL);
    assert(strstr(stats, "quads") == NULL);
    assert(strstr(stats, "visible") == NULL);
}

static void check_counts(int size)
{
    unsigned long row;
    assert(globe_longitude == size && globe_latitude == size / 2);
    assert(globe_verts_num == (unsigned long)(size + 1) * (size / 2 + 1UL));
    assert(globe_faces_num == (unsigned long)size * (size / 2));
    assert(globe_tris_num == 2UL * size * (size / 2 - 1));
    row = (unsigned long)globe_latitude * (globe_longitude + 1UL);
    assert(globe_verts[0].x == 0.0f && globe_verts[0].y == 0.0f);
    assert(globe_verts[row].x == 0.0f && globe_verts[row].y == 0.0f);
    assert(globe_verts[0].z == GLOBE_RADIUS);
    assert(globe_verts[row].z == -GLOBE_RADIUS);
}

int main(void)
{
    int i, sizes[5];
    unsigned char *datasets[5];
    sizes[0] = 32; sizes[1] = 64; sizes[2] = 128;
    sizes[3] = 256; sizes[4] = 512;
    datasets[0] = test_earth_data(32); datasets[1] = test_earth_data(64);
    datasets[2] = test_earth_data(128); datasets[3] = test_earth_data(256);
    datasets[4] = test_earth_data(512);
    drv_ext_init();
    globe_init();
    assert(GLOBE_DEFAULT_LONGITUDE == 512);
    assert(drv_texture_avail && !drv_texture_enabled);
    expected_data = test_earth_data(512);
    assert(globe_init_verts(GLOBE_DEFAULT_LONGITUDE, -1, GLOBE_RADIUS));
    check_counts(512);
    check_drawing();
    for (i = 0; i < 5; ++i) {
        expected_data = datasets[i];
        globe_toggle_res((char)('1' + i));
        check_counts(sizes[i]);
        assert(!drv_texture_enabled);
        check_drawing();
        globe_toggle_tex();
        assert(drv_texture_enabled);
        check_drawing();
        globe_toggle_tex();
        assert(!drv_texture_enabled);
    }
    assert(globe_verts_num * sizeof(Vertex) > 65535UL);
    assert(sizeof(Color) == 1);
    assert(globe_faces_num * sizeof(Color) > 65535UL);
    assert(globe_verts_num * sizeof(Coord) > 65535UL);
    globe_free();
    puts("PASS OpenGL strips: default 512, all five resolution keys, exact palette bytes, TRI-equivalent outward winding and seam UVs");
    puts("PASS OpenGL cap dispatch: original TRI tuples and UVs, separate pole triangles, full 1026-vertex interior bands and reduced vertex count");
    return 0;
}
