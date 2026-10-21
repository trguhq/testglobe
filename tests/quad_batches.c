/* Indexed quad batches without Windows or Macintosh graphics hardware. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DRV_GDI
#include <windows.h>
#else
#include <Memory.h>
#include <Events.h>
#endif
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"
#include "earth_resources.h"

#if !defined(QUAD) || !defined(DRV_QUAD_BATCH) || defined(TRI) || defined(TRISTRIP)
#error GDI and QuickDraw must select QUAD with optional batching
#endif
#if defined(TEXTURE) || defined(DRV_TEXTURE) || defined(FLAT) || defined(WIN32)
#error Segmented polygon drivers must not include texture support or FLAT/WIN32
#endif
#if !defined(DRV_COLORMAP) || !defined(DRV_EARTH_RESOURCES)
#error GDI and QuickDraw must select resource-backed indexed geometry
#endif
#if defined(DRV_GDI) && (!defined(SEG86) || !defined(GDI) || defined(SEG68))
#error GDI must retain its Windows-specific segmented memory path
#endif
#if defined(DRV_QD) && (!defined(SEG68) || defined(SEG86) || defined(GDI))
#error QuickDraw must retain its classic Macintosh segmented memory path
#endif
#if !defined(EARTH_32) || !defined(EARTH_64) || !defined(EARTH_128) || !defined(EARTH_256) || !defined(EARTH_512)
#error GDI and QuickDraw must include all five Earth resolutions
#endif

extern Vertex TG_HUGE *globe_verts;
extern Color TG_HUGE *globe_colors;
extern int globe_longitude, globe_latitude;

static unsigned long batch_calls, rendered_quads, cap_quads, triangles;
static unsigned long submitted_vertices, shared_edges, allocations;
static unsigned long largest_allocation;
static unsigned int largest_batch, largest_vertices;
static unsigned char *expected_data;
static void *memory_blocks[16];

#ifdef DRV_GDI
HGLOBAL GlobalAlloc(UINT flags, DWORD bytes)
{
    HGLOBAL handle;
    assert(flags == (GMEM_MOVEABLE | GMEM_ZEROINIT));
    assert(bytes > 0);
    if (bytes > largest_allocation) largest_allocation = bytes;
    for (handle = 1; handle < 16; ++handle) {
        if (memory_blocks[handle] != NULL) continue;
        memory_blocks[handle] = calloc((size_t)bytes, 1);
        assert(memory_blocks[handle] != NULL);
        ++allocations;
        return handle;
    }
    assert(!"Global handle leak");
    return 0;
}

LPVOID GlobalLock(HGLOBAL handle)
{
    assert(handle > 0 && handle < 16 && memory_blocks[handle] != NULL);
    return memory_blocks[handle];
}

BOOL GlobalUnlock(HGLOBAL handle)
{
    assert(handle > 0 && handle < 16 && memory_blocks[handle] != NULL);
    return 0;
}

HGLOBAL GlobalFree(HGLOBAL handle)
{
    assert(handle > 0 && handle < 16 && memory_blocks[handle] != NULL);
    --allocations;
    free(memory_blocks[handle]);
    memory_blocks[handle] = NULL;
    return 0;
}

DWORD GetTickCount(void)
{
    static DWORD ticks;
    ticks += 16;
    return ticks;
}

void gdi_error(char *message)
{
    fprintf(stderr, "Unexpected GDI error: %s\n", message);
    abort();
}
#else
Ptr NewPtr(Size bytes)
{
    unsigned int slot;
    assert(bytes > 0 && bytes <= 0x7fffffffL);
    if ((unsigned long)bytes > largest_allocation) largest_allocation = (unsigned long)bytes;
    for (slot = 0; slot < 16; ++slot) {
        if (memory_blocks[slot] != NULL) continue;
        memory_blocks[slot] = malloc((size_t)bytes);
        assert(memory_blocks[slot] != NULL);
        ++allocations;
        return (Ptr)memory_blocks[slot];
    }
    assert(!"QuickDraw allocation leak");
    return NULL;
}

void DisposePtr(Ptr memory)
{
    unsigned int slot;
    for (slot = 0; slot < 16; ++slot) {
        if (memory_blocks[slot] != memory) continue;
        assert(memory != NULL && allocations != 0UL);
        --allocations;
        free(memory_blocks[slot]);
        memory_blocks[slot] = NULL;
        return;
    }
    assert(!"Unknown Macintosh heap block");
}

unsigned long TickCount(void)
{
    static unsigned long ticks;
    ticks = (ticks + 1UL) & 0xffffffffUL;
    return ticks;
}

void qd_error(char *message)
{
    fprintf(stderr, "Unexpected QuickDraw error: %s\n", message);
    abort();
}
#endif

void drv_load_earth_chunk(unsigned int resource, unsigned long size,
                           unsigned char TG_HUGE *destination)
{
    unsigned long count;
    unsigned char *chunk;
    chunk = test_earth_resource((short)resource, &count);
    assert(count == size && count > 0 && count <= TG_EARTH_CHUNK_SIZE);
    memcpy(destination, chunk, (size_t)count);
    free(chunk);
}

static void same_vertex(const Vertex *a, const Vertex *b)
{
    assert(a->x == b->x && a->y == b->y && a->z == b->z);
}

static double outward_area(const Vertex *a, const Vertex *b, const Vertex *c)
{
    double ux, uy, uz, vx, vy, vz;
    ux = b->x - a->x; uy = b->y - a->y; uz = b->z - a->z;
    vx = c->x - a->x; vy = c->y - a->y; vz = c->z - a->z;
    return (uy * vz - uz * vy) * (a->x + b->x + c->x) +
           (uz * vx - ux * vz) * (a->y + b->y + c->y) +
           (ux * vy - uy * vx) * (a->z + b->z + c->z);
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *a, Vertex TG_HUGE *b,
                              Vertex TG_HUGE *c, Vertex TG_HUGE *d,
                              unsigned char color)
{
    (void)a; (void)b; (void)c; (void)d; (void)color;
    assert(!"Batch-capable globe traversal must use the batch callback");
}

void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                   const Vertex TG_HUGE *vertices,
                                   unsigned int quad_count,
                                   const unsigned int TG_HUGE *indices,
                                   const unsigned char TG_HUGE *colors)
{
    unsigned int i, j, first_cell, remaining, expected_count;
    unsigned long band, index, face, top, bottom;
    unsigned int corners[4];
    const Vertex *a, *b, *c, *d;

    band = rendered_quads / globe_longitude;
    first_cell = (unsigned int)(rendered_quads % globe_longitude);
    remaining = (unsigned int)globe_longitude - first_cell;
    expected_count = remaining < DRV_QUAD_BATCH_MAX ? remaining : DRV_QUAD_BATCH_MAX;
    assert(band < (unsigned long)globe_latitude);
    assert(quad_count == expected_count && quad_count > 0U && quad_count <= 64U);
    assert(vertex_count == 2U * (quad_count + 1U) && vertex_count <= 130U);
    assert(vertices != NULL && indices != NULL && colors != NULL);
    for (i = 0; i < vertex_count; ++i) {
        index = (band + i % 2U) * (globe_longitude + 1UL) + first_cell + i / 2U;
        assert(index < globe_verts_num);
        same_vertex(vertices + i, globe_verts + index);
    }
    for (i = 0; i < quad_count; ++i) {
        top = band * (globe_longitude + 1UL) + first_cell + i;
        bottom = top + globe_longitude + 1UL;
        corners[0] = 2U * i;
        corners[1] = 2U * i + 1U;
        corners[2] = 2U * i + 3U;
        corners[3] = 2U * i + 2U;
        for (j = 0; j < 4U; ++j) {
            assert(indices[4U * i + j] == corners[j]);
            assert(indices[4U * i + j] < vertex_count);
        }
        a = vertices + corners[0]; b = vertices + corners[1];
        c = vertices + corners[2]; d = vertices + corners[3];
        same_vertex(a, globe_verts + top);
        same_vertex(b, globe_verts + bottom);
        same_vertex(c, globe_verts + bottom + 1UL);
        same_vertex(d, globe_verts + top + 1UL);
        face = band * globe_longitude + first_cell + i;
        assert(globe_colors[face].color == expected_data[face]);
        assert(colors[i] == expected_data[face]);
        if (band == 0UL) {
            same_vertex(a, d);
            assert(outward_area(d, b, c) > 0.0);
            ++cap_quads;
            ++triangles;
        } else if (band == (unsigned long)globe_latitude - 1UL) {
            same_vertex(b, c);
            assert(outward_area(a, b, d) > 0.0);
            ++cap_quads;
            ++triangles;
        } else {
            assert(outward_area(a, b, d) > 0.0);
            assert(outward_area(d, b, c) > 0.0);
            triangles += 2UL;
        }
        if (i != 0U) {
            assert(indices[4U * (i - 1U) + 2U] == indices[4U * i + 1U]);
            assert(indices[4U * (i - 1U) + 3U] == indices[4U * i]);
            ++shared_edges;
        }
    }
    if (quad_count > largest_batch) largest_batch = quad_count;
    if (vertex_count > largest_vertices) largest_vertices = vertex_count;
    submitted_vertices += vertex_count;
    rendered_quads += quad_count;
    ++batch_calls;
}

int main(void)
{
    int i, resolution;
    unsigned long batches_per_band;
    char stats[128], expected_stats[80];
    drv_ext_init();
    globe_init();
    assert(GLOBE_DEFAULT_LONGITUDE == 32);
    assert(DRV_QUAD_BATCH_MAX == 64);
    assert(!drv_texture_avail && !drv_texture_enabled);
    /* Empty traversal must not form pointers into unallocated mesh data. */
    globe_draw_quads();
    assert(batch_calls == 0UL);
    for (i = 0; i < 5; ++i) {
        resolution = 32 << i;
        expected_data = test_earth_data(resolution);
        globe_toggle_res((char)('1' + i));
        assert(globe_longitude == resolution);
        assert(globe_latitude == resolution / 2);
        assert(sizeof(Color) == 1U);
        batch_calls = rendered_quads = cap_quads = triangles = 0UL;
        submitted_vertices = shared_edges = 0UL;
        largest_batch = largest_vertices = 0U;
        globe_draw_quads();
        batches_per_band = ((unsigned long)resolution + DRV_QUAD_BATCH_MAX - 1UL) /
                           DRV_QUAD_BATCH_MAX;
        assert(batch_calls == batches_per_band * globe_latitude);
        assert(rendered_quads == globe_quads_num);
        assert(rendered_quads == (unsigned long)resolution * (resolution / 2UL));
        assert(cap_quads == 2UL * globe_longitude);
        assert(triangles == globe_tris_num);
        assert(submitted_vertices == 2UL * rendered_quads + 2UL * batch_calls);
        assert(shared_edges == rendered_quads - batch_calls);
        if (resolution == 512) {
            assert(batch_calls == 2048UL && rendered_quads == 131072UL);
            assert(largest_batch == 64U && largest_vertices == 130U);
            assert(submitted_vertices == 266240UL);
        }
        drv_fps_average = 12.5f;
        globe_quads_num_vis = 17UL;
        globe_tris_num_vis = 99UL;
        drv_format_stats(stats);
        sprintf(expected_stats, "%lu quads (17 visible), 12.50 frames per second",
                rendered_quads);
        assert(strcmp(stats, expected_stats) == 0);
        globe_toggle_res('6');
        assert(globe_longitude == resolution);
    }
    globe_free();
    assert(allocations == 0UL);
    assert(largest_allocation > 65535UL);
#ifdef DRV_GDI
    puts("PASS GDI quad batches: five resolutions/default32, exact indexed corners/colors, shared vertices, bounded packets, seams, pole triangles, quad OSD and huge allocations");
#else
    puts("PASS QuickDraw quad batches: five resolutions/default32, exact indexed corners/colors, shared vertices, bounded packets, seams, pole triangles, quad OSD and huge allocations");
#endif
    return 0;
}
