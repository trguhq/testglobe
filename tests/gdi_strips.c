/* Indexed strip geometry without Windows or graphics hardware. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"
#include "earth_resources.h"

#if !defined(TRISTRIP) || defined(TRI) || defined(QUAD)
#error GDI must select exactly TRISTRIP in its driver header
#endif
#if defined(TEXTURE) || defined(DRV_TEXTURE) || defined(WIN32)
#error Win16 GDI must not include texture support or WIN32
#endif
#if !defined(SEG86) || !defined(DRV_COLORMAP) || !defined(GDI)
#error GDI must select segmented indexed-color rendering
#endif
#if !defined(EARTH_32) || !defined(EARTH_64) || !defined(EARTH_128) || !defined(EARTH_256) || !defined(EARTH_512)
#error GDI must include all five Earth resolutions
#endif

extern Vertex TG_HUGE *globe_verts;
extern Color TG_HUGE *globe_colors;
extern int globe_longitude, globe_latitude;

static unsigned long strip_calls, rendered_triangles, full_bands, cap_strips;
static unsigned long allocations;
static unsigned long largest_allocation;
static unsigned char *expected_data;
static void *memory_blocks[16];

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

void drv_load_earth_data(int longitude, unsigned char TG_HUGE *destination)
{
    unsigned long offset, count, total;
    unsigned char *chunk;
    short resource;
    total = (unsigned long)longitude * longitude / 2UL;
    resource = test_earth_resource_id(longitude);
    for (offset = 0; offset < total; offset += count)
    {
        chunk = test_earth_resource(resource++, &count);
        assert(count > 0 && count <= total - offset);
        memcpy(destination + offset, chunk, (size_t)count);
        free(chunk);
    }
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

void drv_draw_tristrip_flat_color(unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  const unsigned char TG_HUGE *colors)
{
    unsigned int i, cell;
    unsigned long band, index, top, bottom, face, cap[3];
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
    assert(vertices != NULL && colors != NULL);
    for (i = 0; i < count; ++i) {
        index = polar ? cap[i] :
                (band + i % 2U) * (globe_longitude + 1UL) + i / 2U;
        assert(index < globe_verts_num);
        same_vertex(vertices + i, globe_verts + index);
    }
    for (i = 0; i < count - 2U; ++i) {
        if (!polar) cell = i / 2U;
        top = band * (globe_longitude + 1UL) + cell;
        bottom = top + globe_longitude + 1UL;
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
        assert(colors[i] == expected_data[face]);
        assert(outward_area(a, b, c) > 0.0);
        ++rendered_triangles;
    }
    ++strip_calls;
}

int main(void)
{
    int i, resolution;
    drv_ext_init();
    globe_init();
    assert(GLOBE_DEFAULT_LONGITUDE == 32);
    assert(!drv_texture_avail && !drv_texture_enabled);
    for (i = 0; i < 5; ++i) {
        resolution = 32 << i;
        expected_data = test_earth_data(resolution);
        globe_toggle_res((char)('1' + i));
        assert(globe_longitude == resolution);
        assert(globe_latitude == resolution / 2);
        assert(sizeof(Color) == 1U);
        strip_calls = rendered_triangles = full_bands = cap_strips = 0UL;
        globe_draw_tripstrip();
        assert(strip_calls == (unsigned long)globe_latitude - 2UL + 2UL * globe_longitude);
        assert(full_bands == (unsigned long)globe_latitude - 2UL);
        assert(cap_strips == 2UL * globe_longitude);
        assert(rendered_triangles == globe_tris_num);
        globe_toggle_res('6');
        assert(globe_longitude == resolution);
    }
    globe_free();
    assert(allocations == 0UL);
    assert(largest_allocation > 65535UL);
    puts("PASS GDI strips: five resolutions, exact indexed colors, TRI-equivalent winding, complete latitude bands, nondegenerate caps and large allocations");
    return 0;
}
