/* Shared driver utilities, without native windows or a graphics renderer. */
#include <assert.h>
#include <float.h>
#include <limits.h>
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

static unsigned int resolution_calls, texture_calls;
static unsigned char resolution_key;
static int helper_scene_ready = TRUE;

int globe_ready(void) { return helper_scene_ready; }

void error(char *message)
{
    assert(message != NULL);
    fprintf(stderr, "Unexpected shared-driver error: %s\n", message);
    abort();
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
Drv_result drv_load_earth_chunk(unsigned int resource, unsigned long length,
                           unsigned char TG_HUGE *destination)
{
    (void)resource; (void)length; (void)destination;
    assert(!"Utility tests must not load native Earth resources");
    return DRV_BAD_INPUT;
}
#endif

#ifdef GDI
static DWORD helper_ticks;
static void *helper_memory[64];
static unsigned int helper_locks[64];

HGLOBAL GlobalAlloc(UINT flags, DWORD bytes)
{
    unsigned int i;
    assert(flags == (GMEM_MOVEABLE | GMEM_ZEROINIT));
    for (i = 1; i < 64U; ++i)
        if (helper_memory[i] == NULL)
        {
            helper_memory[i] = calloc(1, (size_t)bytes);
            return helper_memory[i] == NULL ? 0 : (HGLOBAL)i;
        }
    return 0;
}

LPVOID GlobalLock(HGLOBAL handle)
{
    assert(handle > 0 && handle < 64 && helper_memory[handle] != NULL);
    ++helper_locks[handle];
    return helper_memory[handle];
}

BOOL GlobalUnlock(HGLOBAL handle)
{
    assert(handle > 0 && handle < 64 && helper_locks[handle] != 0);
    --helper_locks[handle];
    return FALSE;
}

HGLOBAL GlobalFree(HGLOBAL handle)
{
    assert(handle > 0 && handle < 64 && helper_memory[handle] != NULL);
    assert(helper_locks[handle] == 0);
    free(helper_memory[handle]);
    helper_memory[handle] = NULL;
    return 0;
}

DWORD GetTickCount(void)
{
    return helper_ticks;
}
#endif

static void test_services(void)
{
    unsigned char TG_HUGE *memory;
    unsigned char guarded[10];
    char status[128];
    assert(drv_alloc_items(0UL, 1U) == NULL);
    assert(drv_alloc_items(1UL, 0U) == NULL);
    assert(drv_alloc_items(ULONG_MAX, 2U) == NULL);
    memory = (unsigned char TG_HUGE *)drv_alloc_items(70000UL, 1U);
    assert(memory != NULL);
    memory[0] = 17;
    memory[65535UL] = 23;
    memory[69999UL] = 31;
    assert(memory[0] == 17 && memory[65535UL] == 23 && memory[69999UL] == 31);
    drv_free_items(memory);
    drv_free_items(NULL);
    memset(guarded, 0xa5, sizeof(guarded));
    strcpy((char *)guarded + 1, "abc");
    drv_append_status((char *)guarded + 1, 8U, "defghijkl");
    assert(strcmp((char *)guarded + 1, "abcdefg") == 0);
    assert(guarded[0] == 0xa5 && guarded[9] == 0xa5);
    drv_notice("testing resource recovery");
    helper_scene_ready = FALSE;
    drv_format_stats(status);
    assert(strstr(status, "No globe") != NULL);
    assert(strstr(status, "testing resource recovery") != NULL);
    helper_scene_ready = TRUE;
    drv_notice(NULL);
    assert(drv_get_notice()[0] == 0);
    assert(strcmp(drv_result_text(DRV_NO_MEMORY), drv_result_text(DRV_BAD_DATA)) != 0);
    puts("PASS shared services: bounded allocation, native handle release, status capacity and recoverable empty-scene notices");
}

static void near_float(float actual, float expected)
{
    float difference;
    difference = actual - expected;
    assert(difference > -0.0001f && difference < 0.0001f);
}

static float special_float(unsigned long pattern)
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
    assert(drv_fps_enabled);
    assert(drv_handle_key('f') && !drv_fps_enabled);
    assert(drv_handle_key('F') && drv_fps_enabled);
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
    drv_win_width = drv_win_height = 1U;
    drv_rotate_drag(2147483647L, -2147483647L);
    assert(drv_rot_x >= 0.0f && drv_rot_x < 360.0f);
    assert(drv_rot_y >= 0.0f && drv_rot_y < 360.0f);
    drv_rot_x = special_float(0x7fc00000UL);
    drv_rot_y = special_float(0x7f800000UL);
    drv_rotate_drag(0L, 0L);
    near_float(drv_rot_x, 0.0f); near_float(drv_rot_y, 0.0f);
    drv_rot_y = 358.0f;
    drv_rotate_elapsed(0.5);
    near_float(drv_rot_y, 3.0f);
    drv_rotate_elapsed(DBL_MAX);
    assert(drv_rot_y >= 0.0f && drv_rot_y < 360.0f);
    drv_rotate_elapsed((double)special_float(0x7fc00000UL));
    near_float(drv_rot_y, 0.0f);
    drv_rotate_elapsed((double)special_float(0x7f800000UL));
    near_float(drv_rot_y, 0.0f);
    drv_rot_y = special_float(0x7fc00000UL);
    drv_rotate_elapsed(0.5);
    near_float(drv_rot_y, 0.0f);
    drv_rot_y = special_float(0x7f800000UL);
    drv_rotate_elapsed(0.5);
    near_float(drv_rot_y, 0.0f);
    puts("PASS shared input: five resolution keys, case-insensitive toggles including FPS, unsupported controls, bounded multi-turn dragging and zero-sized windows");
}

static void test_stats(void)
{
    char status[128];
    unsigned char guarded[130];
    drv_fps_enabled = TRUE;
    drv_fps_average = 12.5f;
    globe_tris_num = 1024UL;
    globe_tris_num_vis = 512UL;
    globe_quads_num = 512UL;
    globe_quads_num_vis = 256UL;
    drv_bytes_per_pixel = 2U;
    drv_rmouse_pressed = -7;
    drv_format_stats(status);
    assert(strstr(status, "12.50 frames per second") != NULL);
#if DEBUG
    assert(strstr(status, ", 2 B/px, R=1") != NULL);
#else
    assert(strstr(status, "B/px") == NULL && strstr(status, "R=") == NULL);
#endif
    assert(drv_handle_key('f') && !drv_fps_enabled);
    assert(drv_fps_average == 0.0f);
    drv_rmouse_pressed = FALSE;
    drv_format_stats(status);
    assert(strstr(status, "frames per second") == NULL);
    assert(strstr(status, "triangles") != NULL || strstr(status, "quads") != NULL);
#if DEBUG
    assert(strstr(status, ", 2 B/px, R=0") != NULL);
#endif
#ifdef GDI
    helper_ticks = 1000;
    drv_fps_start();
    helper_ticks = 1500;
    drv_fps_stop();
    drv_rot_y = 0.0f;
    drv_globe_rotate();
    near_float(drv_rot_y, 5.0f);
    assert(drv_fps_average == 0.0f);
#endif
    assert(drv_handle_key('F') && drv_fps_enabled);
    assert(drv_fps_average == 0.0f);
    memset(guarded, 0xa5, sizeof(guarded));
    globe_tris_num = globe_tris_num_vis = ULONG_MAX;
    globe_quads_num = globe_quads_num_vis = ULONG_MAX;
    drv_fps_average = FLT_MAX;
    drv_format_stats((char *)guarded + 1);
    assert(guarded[0] == 0xa5 && guarded[129] == 0xa5);
    assert(strlen((char *)guarded + 1) < 128U);
    drv_fps_average = 0.0f;
    drv_rmouse_pressed = FALSE;
    puts("PASS shared FPS and metadata: configurable OSD, reset on toggle, debug-only storage/button diagnostics and animation timing preserved");
}

#ifdef GDI
static void test_presentation_timing(void)
{
    unsigned int i;
    drv_fps_enabled = TRUE;
    drv_reset_timing();
    drv_fps_start();
    helper_ticks += 32;
    drv_fps_stop();
    for (i = 1; i < DRV_FPS_HISTORY; ++i)
    {
        helper_ticks += 24; /* Time waiting for the next native event counts. */
        drv_fps_start();
        helper_ticks += 8;
        drv_fps_stop();
    }
    near_float(drv_fps_average, 31.25f);
    drv_fps_start();
    drv_abort_frame("retry frame");
    drv_fps_stop();
    assert(drv_fps_average == 0.0f);
    drv_frame_failed = FALSE;
    drv_notice(NULL);
    puts("PASS presentation timing: idle intervals count, failed frames reset the sample");
}
#endif

#ifdef TRISTRIP
static Vertex strip_vertices[1026];
static unsigned char strip_colors[1024];
static unsigned int next_facet, split_calls, facet_limit;
static int abort_split;

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
    if (abort_split) drv_abort_frame("stop split submission");
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
    drv_split_tristrip_batches(3U, strip_vertices, strip_colors, 1U, record_split);
    drv_split_tristrip_batches(3U, strip_vertices, strip_colors, 0U, record_split);
    assert(split_calls == 0U && drv_frame_failed);
    drv_frame_failed = FALSE;
    next_facet = split_calls = 0U;
    facet_limit = 128U;
    abort_split = TRUE;
    drv_split_tristrip_batches(1026U, strip_vertices, strip_colors, 128U, record_split);
    assert(drv_frame_failed && split_calls == 1U && next_facet == 128U);
    drv_split_tristrip_batches(1026U, strip_vertices, strip_colors, 128U, record_split);
    assert(split_calls == 1U);
    abort_split = FALSE;
    drv_frame_failed = FALSE;
    drv_notice(NULL);
    puts("PASS retained strip splitter: 1,026 vertices, even packet seams, odd limits and tails, exact winding/colors, bounded calls and argument guards");
}

#ifdef TEXTURE
static Coord strip_coords[1026];
static unsigned int uv_triangles;
static int abort_uv;

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
    if (abort_uv) drv_abort_frame("stop UV submission");
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
    abort_uv = TRUE;
    drv_expand_tristrip_uv(1026U, strip_vertices, strip_coords, record_uv_triangle);
    assert(drv_frame_failed && uv_triangles == 1U);
    drv_expand_tristrip_uv(1026U, strip_vertices, strip_coords, record_uv_triangle);
    assert(uv_triangles == 1U);
    abort_uv = FALSE;
    drv_frame_failed = FALSE;
    drv_notice(NULL);
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
    test_services();
    test_stats();
#ifdef GDI
    test_presentation_timing();
#endif
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
