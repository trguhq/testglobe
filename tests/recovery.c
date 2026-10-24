/* Deterministic heap/data failures exercise the real scene coordinator. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"
#ifdef DRV_EARTH_RESOURCES
#include "earth_reference.h"
#endif

extern Vertex TG_HUGE *globe_verts;
#ifdef DRV_EARTH_RESOURCES
extern Color_Index TG_HUGE *globe_colors;
#endif
int drv_frame_failed;
int drv_texture_enabled;
int drv_texture_active;
static void *blocks[16];
static unsigned long lengths[16], used, peak, budget = 0xffffffffUL;
static unsigned long allocations, fail_at, releases, resets, release_budget;
static unsigned long callbacks;
static int bad_data;
static char last_notice[128];

void TG_HUGE *drv_alloc_items(unsigned long count, unsigned int size)
{
    unsigned int slot;
    unsigned long bytes;
    ++allocations;
    bytes = count * size;
    if ((fail_at != 0UL && allocations == fail_at) || bytes > budget - used)
        return NULL;
    for (slot = 0; slot < 16U; ++slot)
    {
        if (blocks[slot] != NULL) continue;
        blocks[slot] = malloc((size_t)bytes);
        assert(blocks[slot] != NULL);
        lengths[slot] = bytes;
        used += bytes;
        if (used > peak) peak = used;
        return blocks[slot];
    }
    assert(!"allocation slots exhausted");
    return NULL;
}
void drv_free_items(void TG_HUGE *memory)
{
    unsigned int slot;
    if (memory == NULL) return;
    for (slot = 0; slot < 16U; ++slot)
    {
        if (blocks[slot] != memory) continue;
        used -= lengths[slot];
        free(memory);
        blocks[slot] = NULL;
        return;
    }
    assert(!"unknown or double free");
}
void drv_release_optional(void)
{
    ++releases;
    if (release_budget != 0UL) budget = release_budget;
}
void drv_notice(const char *notice)
{
    assert(strlen(notice) < sizeof(last_notice));
    strcpy(last_notice, notice);
}
void drv_reset_timing(void) { ++resets; }
void drv_abort_frame(const char *notice)
{
    drv_frame_failed = TRUE;
    drv_notice(notice);
}
#ifdef TEXTURE
void drv_texture_enable(void) { drv_texture_enabled = TRUE; }
void drv_texture_disable(void) { drv_texture_enabled = FALSE; }
void drv_draw_tri_flat_uv(Vertex TG_HUGE *a, Vertex TG_HUGE *b,
                          Vertex TG_HUGE *c, Coord TG_HUGE *u,
                          Coord TG_HUGE *v, Coord TG_HUGE *w)
{
    (void)a; (void)b; (void)c; (void)u; (void)v; (void)w;
    ++callbacks;
    drv_abort_frame("injected submission failure");
}
void drv_draw_tristrip_flat_uv(unsigned int count,
                               const Vertex TG_HUGE *vertices,
                               const Coord TG_HUGE *coords)
{
    (void)count; (void)vertices; (void)coords;
    ++callbacks;
    drv_abort_frame("injected submission failure");
}
void drv_draw_tri_flat_color(Vertex TG_HUGE *a, Vertex TG_HUGE *b,
                             Vertex TG_HUGE *c, unsigned char color)
{
    (void)a; (void)b; (void)c; (void)color;
    ++callbacks;
    drv_abort_frame("injected submission failure");
}
void drv_draw_tristrip_flat_color(unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  const unsigned char TG_HUGE *colors)
{
    (void)count; (void)vertices; (void)colors;
    ++callbacks;
    drv_abort_frame("injected submission failure");
}
#endif
#ifdef DRV_EARTH_RESOURCES
Drv_result drv_load_earth_data(int longitude, unsigned char TG_HUGE *destination)
{
    unsigned long bytes;
    bytes = (unsigned long)longitude * (longitude / 2UL);
    /* Data is loaded before allocating any vertex/scratch storage. */
    assert(used == bytes);
    if (longitude == bad_data) return DRV_BAD_DATA;
    memcpy(destination, test_earth_data(longitude), (size_t)bytes);
    return DRV_OK;
}
void drv_draw_quad_flat_color(Vertex TG_HUGE *a, Vertex TG_HUGE *b,
                              Vertex TG_HUGE *c, Vertex TG_HUGE *d,
                              unsigned char color)
{
    (void)a; (void)b; (void)c; (void)d; (void)color;
    ++callbacks;
    drv_abort_frame("injected submission failure");
}
void drv_draw_quadbatch_flat_color(unsigned int count,
                                   const Vertex TG_HUGE *vertices,
                                   unsigned int quads,
                                   const unsigned int TG_HUGE *indices,
                                   const unsigned char TG_HUGE *colors)
{
    (void)count; (void)vertices; (void)quads; (void)indices; (void)colors;
    ++callbacks;
    drv_abort_frame("injected submission failure");
}
#endif

static void reset_faults(void)
{
    allocations = fail_at = releases = release_budget = 0UL;
    budget = 0xffffffffUL;
    drv_frame_failed = FALSE;
    bad_data = 0;
}

/* The runner checks exact stdout: two committed meshes, no messages for
 * no-op/invalid/failed candidates, and no console diagnostics on GUI builds. */
static void diagnostic_sequence(void)
{
    unsigned long revision;
    Vertex TG_HUGE *vertices;
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    budget = used;
    vertices = globe_verts;
    revision = globe_revision;
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    assert(!globe_set_resolution(33, -1, GLOBE_RADIUS));
    assert(globe_verts == vertices && globe_revision == revision);
    assert(globe_set_resolution(512, -1, GLOBE_RADIUS));
    assert(globe_longitude == 32 && globe_last_result == DRV_NO_MEMORY);
    budget = 0UL;
    assert(!globe_set_resolution(512, -1, GLOBE_RADIUS));
    assert(!globe_ready() && used == 0UL);
    puts("PASS scene diagnostics: committed and recovered meshes only");
}

int main(int argc, char **argv)
{
    unsigned long retained, revision, count, width32, width64, width512;
    Vertex TG_HUGE *vertices;
#ifdef DRV_EARTH_RESOURCES
    unsigned int row, column, source_row;
#endif
    globe_init();
    if (argc > 1)
    {
        assert(argc == 2 && strcmp(argv[1], "diagnostics") == 0);
        diagnostic_sequence();
        return 0;
    }
    assert(!globe_ready() && used == 0UL);
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    width32 = used;
    count = allocations;
#ifdef DRV_EARTH_RESOURCES
    assert(count == 5UL);
    assert(width32 == 512UL + 561UL * sizeof(Vertex) +
           130UL * sizeof(Vertex) + 256UL * sizeof(unsigned int) + 64UL);
#else
    assert(count == 4UL);
    assert(width32 == 561UL * sizeof(Vertex) +
           66UL * sizeof(Vertex) + 64UL + 66UL * sizeof(Coord));
#endif
    vertices = globe_verts;
    revision = globe_revision;
    retained = allocations;
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    assert(allocations == retained && globe_verts == vertices);
    assert(globe_revision == revision && releases == 0UL);
    assert(!globe_set_resolution(33, -1, GLOBE_RADIUS));
    assert(globe_last_result == DRV_BAD_INPUT && globe_verts == vertices);
    assert(globe_revision == revision && globe_ready());
    assert(!globe_init_verts(32, 1, GLOBE_RADIUS));
    assert(!globe_init_verts(32, 17, GLOBE_RADIUS));
    assert(!globe_init_verts(32, 16, 0.0f));
    assert(!globe_init_verts(32, 16, -1.0f));
#ifdef DRV_MAX_RADIUS
    /* The initial 1000-unit sphere exercises the inclusive software bound. */
    assert(GLOBE_RADIUS == DRV_MAX_RADIUS);
    assert(!globe_init_verts(32, 16, DRV_MAX_RADIUS + 1.0f));
    assert(!globe_set_resolution(64, -1, DRV_MAX_RADIUS + 1.0f));
    assert(globe_last_result == DRV_BAD_INPUT && releases == 0UL);
#endif
    assert(globe_verts == vertices && globe_revision == revision);
#ifndef DRV_MAX_RADIUS
    /* Native float drivers retain their previous positive-finite domain. */
    assert(globe_set_resolution(32, -1, 1500.0f));
    assert(globe_verts[0].z == 1500.0f);
    assert(globe_set_resolution(32, -1, 2000.0f));
    assert(globe_verts[0].z == 2000.0f);
#endif

    assert(globe_set_resolution(64, -1, GLOBE_RADIUS));
    width64 = used;
    assert(globe_set_resolution(512, -1, GLOBE_RADIUS));
    width512 = used;
    globe_free();
    assert(used == 0UL && !globe_ready());
    for (retained = 1UL; retained <= count; ++retained)
    {
        reset_faults();
        fail_at = retained;
        assert(!globe_init_verts(32, -1, GLOBE_RADIUS));
        assert(globe_last_result == DRV_NO_MEMORY && !globe_ready());
        assert(used == 0UL && globe_verts_num == 0UL);
    }
    reset_faults();
    assert(globe_set_resolution(64, -1, GLOBE_RADIUS));
    budget = width64;
    peak = used;
    revision = globe_revision;
    assert(globe_set_resolution(512, -1, GLOBE_RADIUS));
    assert(globe_longitude == 64 && globe_requested_longitude == 512);
    assert(globe_last_result == DRV_NO_MEMORY && releases == 1UL);
    assert(used == width64 && peak <= width64 && globe_revision > revision);
    assert(strstr(last_notice, "using 64") != NULL);
    globe_free();
    reset_faults();
    budget = width32;
    assert(globe_set_resolution(512, -1, GLOBE_RADIUS));
    assert(globe_longitude == 32 && used == width32 && releases == 1UL);
    globe_free();
    reset_faults();
    budget = width32;
    release_budget = width512;
    assert(globe_set_resolution(512, -1, GLOBE_RADIUS));
    assert(globe_longitude == 512 && used == width512 && releases == 1UL);
    assert(globe_last_result == DRV_NO_MEMORY);
    globe_free();
    reset_faults();
    budget = 0UL;
    assert(!globe_set_resolution(512, -1, GLOBE_RADIUS));
    assert(!globe_ready() && used == 0UL && releases == 1UL);
    assert(globe_last_result == DRV_NO_MEMORY);
    callbacks = 0UL;
#ifdef TRISTRIP
    globe_draw_pole_tris();
    globe_draw_tristrips();
#else
    globe_draw_quads();
    globe_draw_quadbatches();
#endif
    assert(callbacks == 0UL);
    reset_faults();
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
#ifdef TRISTRIP
    globe_draw_pole_tris();
    globe_draw_tristrips();
#else
    globe_draw_quads();
    globe_draw_quadbatches();
#endif
    assert(callbacks == 1UL && globe_ready());
    globe_free();
#ifdef DRV_EARTH_RESOURCES
    reset_faults();
    bad_data = 64;
    assert(!globe_init_verts(64, -1, GLOBE_RADIUS));
    assert(globe_last_result == DRV_BAD_DATA && used == 0UL);
    assert(allocations == 1UL);
    assert(globe_set_resolution(64, -1, GLOBE_RADIUS));
    assert(globe_longitude == 32 && globe_last_result == DRV_BAD_DATA);
    reset_faults();
    assert(globe_init_verts(32, 3, GLOBE_RADIUS));
    for (row = 0; row < 3U; ++row)
    {
        source_row = (2U * row + 1U) * 16U / 6U;
        for (column = 0; column < 32U; ++column)
            assert(globe_colors[row * 32U + column].color ==
                   test_earth_data(32)[source_row * 32U + column]);
    }
#endif
    globe_free();
    revision = globe_revision;
    globe_init();
    assert(used == 0UL && globe_revision == revision && resets > 0UL);
    puts("PASS scene recovery: bounded retries, old/lower/empty fallback, exact owned storage, no-op revisions, partial cleanup and aborted traversal");
    return 0;
}
