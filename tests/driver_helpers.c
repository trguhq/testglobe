/* Shared driver utilities, without native windows or a graphics renderer. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DRV_GDI
#include <windows.h>
#endif
#include "testglob.h"
#include "drv.h"
#include "drv_ext.h"
#include "glob_ext.h"

unsigned long globe_tris_num, globe_tris_num_vis;
unsigned long globe_quads_num, globe_quads_num_vis;

static unsigned int resolution_calls, texture_calls, error_calls;
static unsigned char resolution_key;
static int expected_error;

void error(char *message)
{
    assert(message != NULL);
    if (!expected_error)
    {
        fprintf(stderr, "Unexpected shared-driver error: %s\n", message);
        abort();
    }
    ++error_calls;
}

void globe_toggle_res(char key)
{
    resolution_key = (unsigned char)key;
    ++resolution_calls;
}

#ifdef TEXTURE
void globe_toggle_tex(void)
{
    drv_texture_enabled = !drv_texture_enabled;
    ++texture_calls;
}
#endif

#ifdef DRV_EARTH_RESOURCES
void drv_load_earth_chunk(unsigned int resource, unsigned long length,
                           unsigned char TG_HUGE *destination)
{
    (void)resource; (void)length; (void)destination;
    assert(!"Utility tests must not load native Earth resources");
}
#endif

#ifdef GDI
DWORD GetTickCount(void)
{
    return 0;
}
#endif

static void near_float(float actual, float expected)
{
    float difference;
    difference = actual - expected;
    assert(difference > -0.0001f && difference < 0.0001f);
}

static void test_input(void)
{
    unsigned char key;
    unsigned int previous;
    drv_ext_init();
    for (key = '1'; key <= '5'; ++key)
    {
        previous = resolution_calls;
        assert(drv_handle_key(key));
        assert(resolution_calls == previous + 1U && resolution_key == key);
    }
    previous = resolution_calls;
    assert(!drv_handle_key('0') && !drv_handle_key('6'));
    assert(!drv_handle_key('z') && !drv_handle_key(27));
    assert(resolution_calls == previous);
    assert(!drv_auto_enabled && drv_osd_enabled);
    assert(drv_handle_key('a') && drv_auto_enabled);
    assert(drv_handle_key('A') && !drv_auto_enabled);
    assert(drv_handle_key('o') && !drv_osd_enabled);
    assert(drv_handle_key('O') && drv_osd_enabled);
#ifdef TEXTURE
    assert(!drv_texture_enabled);
    assert(drv_handle_key('t') && drv_texture_enabled);
    assert(drv_handle_key('T') && !drv_texture_enabled);
    assert(texture_calls == 2U);
#else
    assert(!drv_handle_key('t') && !drv_handle_key('T'));
    assert(texture_calls == 0U);
#endif

    drv_win_width = 640; drv_win_height = 480;
    drv_rot_x = 10.0f; drv_rot_y = 350.0f;
    drv_rotate_drag(128L, -96L);
    near_float(drv_rot_x, 350.0f); near_float(drv_rot_y, 10.0f);
    drv_rotate_drag(7040L, -5280L);
    near_float(drv_rot_x, 330.0f); near_float(drv_rot_y, 30.0f);
    drv_rotate_drag(0L, 0L);
    near_float(drv_rot_x, 330.0f); near_float(drv_rot_y, 30.0f);
    drv_win_width = 0;
    drv_rotate_drag(500L, 500L);
    near_float(drv_rot_x, 330.0f); near_float(drv_rot_y, 30.0f);
    drv_win_width = 640; drv_win_height = 0;
    drv_rotate_drag(-500L, -500L);
    near_float(drv_rot_x, 330.0f); near_float(drv_rot_y, 30.0f);
    puts("PASS shared input: five resolution keys, case-insensitive toggles, unsupported controls, normalized dragging and zero-sized windows");
}

#ifdef TRISTRIP
static Vertex strip_vertices[1026];
static unsigned char strip_colors[1024];
static unsigned int next_facet, split_calls, facet_limit;

static void prepare_strip(void)
{
    unsigned int i;
    for (i = 0; i < 1026U; ++i)
    {
        strip_vertices[i].x = (float)(i / 2U);
        strip_vertices[i].y = (i & 1U) ? -1.0f : 1.0f;
        strip_vertices[i].z = 0.0f;
        if (i < 1024U) strip_colors[i] = (unsigned char)(i * 37U + 11U);
    }
}

static void front_triangle(const Vertex *a, const Vertex *b, const Vertex *c)
{
    double area;
    area = ((double)b->x - a->x) * ((double)c->y - a->y) -
           ((double)b->y - a->y) * ((double)c->x - a->x);
    assert(area > 0.0);
}

static void record_split(unsigned int count, const Vertex TG_HUGE *vertices,
                          const unsigned char TG_HUGE *colors)
{
    unsigned int i, first, second;
    assert(count >= 3U && count - 2U <= facet_limit);
    assert((next_facet & 1U) == 0U);
    assert(vertices == strip_vertices + next_facet);
    assert(colors == strip_colors + next_facet);
    for (i = 0; i < count - 2U; ++i)
    {
        assert(colors[i] == (unsigned char)((next_facet + i) * 37U + 11U));
        first = (i & 1U) ? i + 1U : i;
        second = (i & 1U) ? i : i + 1U;
        front_triangle(vertices + first, vertices + second, vertices + i + 2U);
    }
    next_facet += count - 2U;
    ++split_calls;
}

static void split_case(unsigned int count, unsigned int requested_limit,
                        unsigned int expected_limit, unsigned int expected_calls)
{
    next_facet = split_calls = 0U;
    facet_limit = expected_limit;
    drv_split_tristrip_batches(count, strip_vertices, strip_colors,
                               requested_limit, record_split);
    assert(next_facet == count - 2U && split_calls == expected_calls);
}

static void test_splitter(void)
{
    unsigned int previous_errors;
    prepare_strip();
    split_case(1026U, 128U, 128U, 8U);
    split_case(1026U, 127U, 126U, 9U);
    split_case(1025U, 128U, 128U, 8U);
    split_case(3U, 2U, 2U, 1U);
    next_facet = split_calls = 0U;
    drv_split_tristrip_batches(2U, strip_vertices, strip_colors, 128U, record_split);
    drv_split_tristrip_batches(0U, NULL, NULL, 128U, record_split);
    drv_split_tristrip_batches(3U, NULL, strip_colors, 128U, record_split);
    drv_split_tristrip_batches(3U, strip_vertices, NULL, 128U, record_split);
    drv_split_tristrip_batches(3U, strip_vertices, strip_colors, 128U, NULL);
    assert(next_facet == 0U && split_calls == 0U);
    previous_errors = error_calls;
    expected_error = TRUE;
    drv_split_tristrip_batches(3U, strip_vertices, strip_colors, 1U, record_split);
    drv_split_tristrip_batches(3U, strip_vertices, strip_colors, 0U, record_split);
    expected_error = FALSE;
    assert(error_calls == previous_errors + 2U && split_calls == 0U);
    puts("PASS retained strip splitter: 1,026 vertices, even packet seams, odd limits and tails, exact winding/colors, bounded calls and argument guards");
}

#ifdef TEXTURE
static Coord strip_coords[1026];
static unsigned int uv_triangles;

static void record_uv_triangle(const Vertex TG_HUGE *a,
                                const Vertex TG_HUGE *b,
                                const Vertex TG_HUGE *c,
                                const Coord TG_HUGE *ua,
                                const Coord TG_HUGE *ub,
                                const Coord TG_HUGE *uc)
{
    unsigned int first, second;
    first = (uv_triangles & 1U) ? uv_triangles + 1U : uv_triangles;
    second = (uv_triangles & 1U) ? uv_triangles : uv_triangles + 1U;
    assert(a == strip_vertices + first && b == strip_vertices + second);
    assert(c == strip_vertices + uv_triangles + 2U);
    assert(ua == strip_coords + first && ub == strip_coords + second);
    assert(uc == strip_coords + uv_triangles + 2U);
    front_triangle(a, b, c);
    ++uv_triangles;
}

static void test_uv_expansion(void)
{
    unsigned int i;
    for (i = 0; i < 1026U; ++i)
    {
        strip_coords[i].u = (float)i / 1025.0f;
        strip_coords[i].v = (float)(i & 1U);
    }
    uv_triangles = 0;
    drv_expand_tristrip_uv(1026U, strip_vertices, strip_coords, record_uv_triangle);
    assert(uv_triangles == 1024U);
    uv_triangles = 0;
    drv_expand_tristrip_uv(3U, strip_vertices, strip_coords, record_uv_triangle);
    assert(uv_triangles == 1U);
    uv_triangles = 0;
    drv_expand_tristrip_uv(4U, strip_vertices, strip_coords, record_uv_triangle);
    assert(uv_triangles == 2U);
    uv_triangles = 0;
    drv_expand_tristrip_uv(2U, strip_vertices, strip_coords, record_uv_triangle);
    drv_expand_tristrip_uv(0U, NULL, NULL, record_uv_triangle);
    drv_expand_tristrip_uv(3U, NULL, strip_coords, record_uv_triangle);
    drv_expand_tristrip_uv(3U, strip_vertices, NULL, record_uv_triangle);
    drv_expand_tristrip_uv(3U, strip_vertices, strip_coords, NULL);
    assert(uv_triangles == 0U);
    puts("PASS shared textured strip expansion: exact alternating triangle and UV tuples, outward winding, complete seam coverage and argument guards");
}
#endif
#endif

#ifdef DRV_QUAD_BATCH
static Vertex expected_corners[4];
static unsigned int quad_calls;

void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                   const Vertex TG_HUGE *vertices,
                                   unsigned int quad_count,
                                   const unsigned int TG_HUGE *indices,
                                   const unsigned char TG_HUGE *colors)
{
    unsigned int i;
    assert(vertex_count == 4U && quad_count == 1U);
    assert(vertices != NULL && indices != NULL && colors != NULL);
    assert(colors[0] == 197U);
    for (i = 0; i < 4U; ++i)
    {
        assert(indices[i] < vertex_count);
        assert(vertices[indices[i]].x == expected_corners[i].x);
        assert(vertices[indices[i]].y == expected_corners[i].y);
        assert(vertices[indices[i]].z == expected_corners[i].z);
    }
    ++quad_calls;
}

static void test_quad_helpers(void)
{
    unsigned int i, indices[256];
    Vertex source[7];
    for (i = 0; i < 64U; ++i)
    {
        indices[4U * i] = 2U * i;
        indices[4U * i + 1U] = 2U * i + 1U;
        indices[4U * i + 2U] = 2U * i + 3U;
        indices[4U * i + 3U] = 2U * i + 2U;
    }
    assert(drv_validate_quadbatch(130U, 64U, indices));
    assert(drv_validate_quadbatch(4U, 1U, indices));
    assert(!drv_validate_quadbatch(0U, 1U, indices));
    assert(!drv_validate_quadbatch(4U, 0U, indices));
    assert(!drv_validate_quadbatch(131U, 64U, indices));
    assert(!drv_validate_quadbatch(130U, 65U, indices));
    assert(!drv_validate_quadbatch(130U, 64U, NULL));
    indices[255] = 130U;
    assert(!drv_validate_quadbatch(130U, 64U, indices));
    indices[255] = 128U;
    assert(drv_validate_quadbatch(130U, 64U, indices));

    memset(source, 0, sizeof(source));
    for (i = 0; i < 7U; ++i)
    {
        source[i].x = (float)(11U + i);
        source[i].y = (float)(17U + i * i);
        source[i].z = (float)(3U * i);
    }
    for (i = 0; i < 4U; ++i) expected_corners[i] = source[2U * i];
    drv_submit_quad_as_batch(source, source + 2, source + 4, source + 6, 197U);
    assert(quad_calls == 1U);
    drv_submit_quad_as_batch(NULL, source + 2, source + 4, source + 6, 197U);
    drv_submit_quad_as_batch(source, NULL, source + 4, source + 6, 197U);
    drv_submit_quad_as_batch(source, source + 2, NULL, source + 6, 197U);
    drv_submit_quad_as_batch(source, source + 2, source + 4, NULL, 197U);
    assert(quad_calls == 1U);
    puts("PASS shared quad adapters: 64-quad bounds, complete index validation and exact individual-quad corner/color submission");
}
#endif

int main(void)
{
    test_input();
#ifdef TRISTRIP
    test_splitter();
#ifdef TEXTURE
    test_uv_expansion();
#endif
#endif
#ifdef DRV_QUAD_BATCH
    test_quad_helpers();
#endif
    return 0;
}
