/*
 * testglobe - display 3d globe to test 3d hardware
 *
 * Copyright (C) 2024 trguhq
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <math.h>
#ifdef SEG86
#include <malloc.h>
#endif
#include "testglob.h"
#include "drv.h"
#include "globe.h"
#include "drv_ext.h"
#include "types.h"
#ifdef GDI
#include <windows.h>
#endif
#ifdef SEG68
#include <Memory.h>
#endif

/* Only the selected driver's embedded Earth resolutions enter its image. */
#ifdef EARTH
#include "e_cmap.h"
#ifndef DRV_EARTH_RESOURCES
#ifdef EARTH_32
#include "e_32.h"
#endif
#ifdef EARTH_64
#include "e_64.h"
#endif
#ifdef EARTH_128
#include "e_128.h"
#endif
#ifdef EARTH_256
#include "e_256.h"
#endif
#ifdef EARTH_512
#include "e_512.h"
#endif
#endif
#endif

Vertex TG_HUGE *globe_verts;
Color TG_HUGE *globe_colors;
#ifdef TEXTURE
Coord TG_HUGE *globe_coords;
#endif
#ifdef TRISTRIP
/* One whole latitude band is contiguous for a single native strip call. */
static Vertex TG_HUGE *globe_strip_verts;
#ifdef DRV_COLORMAP
static unsigned char TG_HUGE *globe_strip_colors;
#else
static Color_RGB TG_HUGE *globe_strip_colors;
#endif
#ifdef TEXTURE
static Coord *globe_strip_coords;
#endif
#endif
float globe_radius;
unsigned long globe_verts_num;
unsigned long globe_faces_num;
unsigned long globe_tris_num;
unsigned long globe_tris_num_vis;
unsigned long globe_quads_num;
unsigned long globe_quads_num_vis;
int globe_longitude;
int globe_latitude;
static unsigned char TG_HUGE *globe_data;

#ifdef GDI
/* Win16's global heap supplies tiled selectors for objects over 64 KiB.
 * Keep the original handles; huge-pointer normalization can change selectors. */
#define GLOBE_ALLOCATION_SLOTS 7
static struct
{
    HGLOBAL handle;
    void TG_HUGE *memory;
} globe_allocations[GLOBE_ALLOCATION_SLOTS];
#endif

/* Allocations and arithmetic must not truncate at the 16-bit size_t boundary. */
static void TG_HUGE *globe_alloc_items(unsigned long count, unsigned int size)
{
#ifdef GDI
    int slot;
    HGLOBAL handle;
    void TG_HUGE *memory;
    if (size == 0 || count == 0 || count > 0xFFFFFFFFUL / size)
        return NULL;
    for (slot = 0; slot < GLOBE_ALLOCATION_SLOTS; ++slot)
    {
        if (globe_allocations[slot].handle != 0)
            continue;
        handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, count * size);
        if (handle == 0)
            return NULL;
        memory = (void TG_HUGE *)GlobalLock(handle);
        if (memory == NULL)
        {
            GlobalFree(handle);
            return NULL;
        }
        globe_allocations[slot].handle = handle;
        globe_allocations[slot].memory = memory;
        return memory;
    }
    return NULL;
#elif defined(SEG68)
    /* MPW's Size is signed 32-bit; never truncate through a small size_t. */
    if (size == 0 || count == 0 || count > 0x7FFFFFFFUL / size)
        return NULL;
    return (void *)NewPtr((Size)(count * size));
#else
#ifdef SEG86
    return _halloc((long)count, size);
#else
    return malloc((size_t)count * size);
#endif
#endif
}

static void globe_free_items(void TG_HUGE *memory)
{
#ifdef GDI
    int slot;
    for (slot = 0; slot < GLOBE_ALLOCATION_SLOTS; ++slot)
    {
        if (globe_allocations[slot].handle != 0 &&
            globe_allocations[slot].memory == memory)
        {
            GlobalUnlock(globe_allocations[slot].handle);
            GlobalFree(globe_allocations[slot].handle);
            globe_allocations[slot].handle = 0;
            globe_allocations[slot].memory = NULL;
            return;
        }
    }
#elif defined(SEG68)
    if (memory != NULL) DisposePtr((Ptr)memory);
#else
#ifdef SEG86
    _hfree(memory);
#else
    free(memory);
#endif
#endif
}

void globe_init(void)
{
    globe_verts = NULL;
    globe_colors = NULL;
#ifdef TEXTURE
    globe_coords = NULL;
#endif
#ifdef TRISTRIP
    globe_strip_verts = NULL;
    globe_strip_colors = NULL;
#ifdef TEXTURE
    globe_strip_coords = NULL;
#endif
#endif
    globe_radius = 0;
    globe_verts_num = 0UL;
    globe_faces_num = 0UL;
    globe_tris_num = 0UL;
    globe_tris_num_vis = 0UL;
    globe_quads_num = 0UL;
    globe_quads_num_vis = 0UL;
    globe_longitude = 0;
    globe_latitude = 0;
    globe_data = NULL;
}

void globe_free(void)
{
#ifdef DRV_EARTH_RESOURCES
    if (globe_data != NULL)
        globe_free_items(globe_data);
#endif
    if (globe_verts != NULL)
        globe_free_items(globe_verts);
    if (globe_colors != NULL)
        globe_free_items(globe_colors);
#ifdef TEXTURE
    if (globe_coords != NULL)
        globe_free_items(globe_coords);
#endif
#ifdef TRISTRIP
    if (globe_strip_verts != NULL)
        globe_free_items(globe_strip_verts);
    if (globe_strip_colors != NULL)
        globe_free_items(globe_strip_colors);
#ifdef TEXTURE
    if (globe_strip_coords != NULL)
        globe_free_items(globe_strip_coords);
#endif
#endif
    globe_init();
}

static void globe_alloc(void)
{
    globe_verts = (Vertex TG_HUGE *)globe_alloc_items(globe_verts_num, sizeof(Vertex));
    if (globe_verts == NULL)
        error("Couldn't allocate memory for vertices!");
    /* One color per lat/long patch, shared by its two triangles or one quad. */
    globe_colors = (Color TG_HUGE *)globe_alloc_items(globe_faces_num, sizeof(Color));
    if (globe_colors == NULL)
        error("Couldn't allocate memory for colors!");
#ifdef TEXTURE
    globe_coords = (Coord TG_HUGE *)globe_alloc_items(globe_verts_num, sizeof(Coord));
    if (globe_coords == NULL)
        error("Couldn't allocate memory for texture coordinates!");
#endif
#ifdef TRISTRIP
    globe_strip_verts = (Vertex TG_HUGE *)globe_alloc_items(
        2UL * (globe_longitude + 1UL), sizeof(Vertex));
#ifdef DRV_COLORMAP
    globe_strip_colors = (unsigned char TG_HUGE *)globe_alloc_items(
        2UL * globe_longitude, sizeof(unsigned char));
#else
    globe_strip_colors = (Color_RGB TG_HUGE *)globe_alloc_items(
        2UL * globe_longitude, sizeof(Color_RGB));
#endif
    if (globe_strip_verts == NULL || globe_strip_colors == NULL)
        error("Couldn't allocate memory for triangle strips!");
#ifdef TEXTURE
    globe_strip_coords = (Coord *)globe_alloc_items(2UL * (globe_longitude + 1UL),
                                                   sizeof(Coord));
    if (globe_strip_coords == NULL)
        error("Couldn't allocate memory for strip texture coordinates!");
#endif
#endif
}

static unsigned char TG_HUGE *globe_select_data(int longitude)
{
#ifdef DRV_EARTH_RESOURCES
    /* Validate the selected level here; load its bytes into the heap below. */
#define GLOBE_DATA_POINTER(name) NULL
#else
#define GLOBE_DATA_POINTER(name) name
#endif
    switch (longitude)
    {
#ifdef EARTH_32
        case 32: return GLOBE_DATA_POINTER(earth_data_32);
#endif
#ifdef EARTH_64
        case 64: return GLOBE_DATA_POINTER(earth_data_64);
#endif
#ifdef EARTH_128
        case 128: return GLOBE_DATA_POINTER(earth_data_128);
#endif
#ifdef EARTH_256
        case 256: return GLOBE_DATA_POINTER(earth_data_256);
#endif
#ifdef EARTH_512
        case 512: return GLOBE_DATA_POINTER(earth_data_512);
#endif
        default: error("Unsupported globe resolution");
    }
    return NULL;
#undef GLOBE_DATA_POINTER
}

static void globe_calc_colors(void)
{
    unsigned long index;
    unsigned char value;
    Color TG_HUGE *color_ptr;
    unsigned char TG_HUGE *data_ptr;

    color_ptr = globe_colors;
    data_ptr = globe_data;
    for (index = 0UL; index < globe_faces_num; ++index)
    {
        value = *data_ptr++;
#ifdef DRV_COLORMAP
        /* Preserve the original byte: the driver installs earth_cmap in its LUT. */
        color_ptr->color = value;
#else
        color_ptr->r = earth_cmap[value][0];
        color_ptr->g = earth_cmap[value][1];
        color_ptr->b = earth_cmap[value][2];
#endif
        ++color_ptr;
    }
}

int globe_init_verts(int longitude, int latitude, float radius)
{
    int i, j;
    float x, y, z, xy;
    float longitude_step, latitude_step;
    float longitude_angle, latitude_angle;
    Vertex TG_HUGE *vertex_ptr;
#ifdef TEXTURE
    Coord TG_HUGE *coord_ptr;
#endif
    unsigned char TG_HUGE *data;

    data = globe_select_data(longitude);
    if (latitude < 1)
        latitude = longitude / 2;
    if (latitude < 2 || latitude > longitude / 2 || radius <= 0.0f)
        error("Invalid globe dimensions");
    globe_free();
    globe_data = data;
#ifdef DRV_EARTH_RESOURCES
    globe_data = (unsigned char TG_HUGE *)globe_alloc_items(
        (unsigned long)longitude * (longitude / 2UL), sizeof(unsigned char));
    if (globe_data == NULL)
        error("Couldn't allocate memory for Earth data!");
    drv_load_earth_data(longitude, globe_data);
#endif
    globe_radius = radius;
    globe_longitude = longitude;
    globe_latitude = latitude;
    globe_verts_num = (globe_latitude + 1UL) * (globe_longitude + 1UL);
    globe_faces_num = (unsigned long)globe_longitude * globe_latitude;
    globe_tris_num = (globe_latitude - 1UL) * globe_longitude * 2UL;
    globe_tris_num_vis = globe_tris_num / 2UL;
    globe_quads_num = globe_faces_num;
    globe_quads_num_vis = globe_quads_num / 2UL;

#if !defined(GDI) && !defined(SEG68)
    printf("Globe resolution: %i x %i\n", globe_longitude, globe_latitude);
    printf("Vertices: %lu\n", globe_verts_num);
#if defined(TRI) || defined(TRISTRIP)
    printf("Triangles: %lu\n", globe_tris_num);
#endif
#ifdef QUAD
    printf("Quads: %lu\n", globe_quads_num);
#endif
#endif
    globe_alloc();
    vertex_ptr = globe_verts;
#ifdef TEXTURE
    coord_ptr = globe_coords;
#endif
    longitude_step = (float)(2.0 * PI / globe_longitude);
    latitude_step = (float)(PI / globe_latitude);
    for (i = 0; i <= globe_latitude; ++i)
    {
        latitude_angle = (float)(PI / 2.0) - i * latitude_step;
        xy = globe_radius * (float)COS(latitude_angle);
        z = globe_radius * (float)SIN(latitude_angle);
        /* Exact coincident pole vertices let polygon drivers reduce caps to triangles. */
        if (i == 0 || i == globe_latitude)
        {
            xy = 0.0f;
            z = (i == 0 ? globe_radius : -globe_radius);
        }
        for (j = 0; j <= globe_longitude; ++j)
        {
            longitude_angle = j * longitude_step;
            x = xy * (float)COS(longitude_angle);
            y = xy * (float)SIN(longitude_angle);
            vertex_ptr->x = x;
            vertex_ptr->y = y;
            vertex_ptr->z = z;
#ifdef SEG86
            memset(vertex_ptr->reserved, 0, sizeof(vertex_ptr->reserved));
#endif
            ++vertex_ptr;
#ifdef TEXTURE
            coord_ptr->u = (float)j / globe_longitude;
            coord_ptr->v = (float)i / globe_latitude;
            ++coord_ptr;
#endif
        }
    }
    globe_calc_colors();
    return 1;
}

#ifdef TRI
void globe_draw_tris(void)
{
    Color TG_HUGE *color_ptr;
    Vertex TG_HUGE *v1;
    Vertex TG_HUGE *v2;
    Vertex TG_HUGE *v1_1;
    Vertex TG_HUGE *v2_1;
#ifdef TEXTURE
    Coord TG_HUGE *c1;
    Coord TG_HUGE *c2;
    Coord TG_HUGE *c1_1;
    Coord TG_HUGE *c2_1;
#endif
    int i, j;
    unsigned long row;

    color_ptr = globe_colors;
    for (i = 0; i < globe_latitude; ++i)
    {
        row = (unsigned long)i * (globe_longitude + 1UL);
        v1 = globe_verts + row;
        v1_1 = v1 + 1;
        v2 = v1 + globe_longitude + 1;
        v2_1 = v2 + 1;
#ifdef TEXTURE
        c1 = globe_coords + row;
        c1_1 = c1 + 1;
        c2 = c1 + globe_longitude + 1;
        c2_1 = c2 + 1;
#endif
        for (j = 0; j < globe_longitude; ++j)
        {
#ifdef TEXTURE
            if (drv_texture_enabled)
            {
                if (i != 0)
                    drv_draw_tri_flat_uv(v1, v2, v1_1, c1, c2, c1_1);
                if (i != globe_latitude - 1)
                    drv_draw_tri_flat_uv(v1_1, v2, v2_1, c1_1, c2, c2_1);
            }
            else
#endif
            {
                if (i != 0)
#ifdef DRV_COLORMAP
                    drv_draw_tri_flat_color(v1, v2, v1_1, color_ptr->color);
#else
                    drv_draw_tri_flat_rgb(v1, v2, v1_1, color_ptr);
#endif
                if (i != globe_latitude - 1)
#ifdef DRV_COLORMAP
                    drv_draw_tri_flat_color(v1_1, v2, v2_1, color_ptr->color);
#else
                    drv_draw_tri_flat_rgb(v1_1, v2, v2_1, color_ptr);
#endif
            }
            ++color_ptr;
            ++v1;
            ++v1_1;
            ++v2;
            ++v2_1;
#ifdef TEXTURE
            ++c1;
            ++c1_1;
            ++c2;
            ++c2_1;
#endif
        }
    }
}
#endif

#ifdef QUAD
void globe_draw_quads(void)
{
    Color TG_HUGE *color_ptr;
    Vertex TG_HUGE *v1;
    Vertex TG_HUGE *v2;
    Vertex TG_HUGE *v1_1;
    Vertex TG_HUGE *v2_1;
    int i, j;

    color_ptr = globe_colors;
    for (i = 0; i < globe_latitude; ++i)
    {
        v1 = globe_verts + (unsigned long)i * (globe_longitude + 1UL);
        v1_1 = v1 + 1;
        v2 = v1 + globe_longitude + 1;
        v2_1 = v2 + 1;
        for (j = 0; j < globe_longitude; ++j)
        {
#ifdef DRV_COLORMAP
            drv_draw_quad_flat_color(v1, v2, v2_1, v1_1, color_ptr->color);
#else
            drv_draw_quad_flat_rgb(v1, v2, v2_1, v1_1, color_ptr);
#endif
            ++color_ptr;
            ++v1;
            ++v1_1;
            ++v2;
            ++v2_1;
        }
    }
}
#endif

#ifdef TRISTRIP
void globe_draw_tripstrip(void)
{
    int i, j, k;
    unsigned int count;
    unsigned long top, bottom, face;
    unsigned long cap[3];
#ifndef DRV_COLORMAP
    const Coord *coords;
#endif

    /* XGL's point count is 32-bit with no smaller documented strip limit.
     * A complete latitude band needs at most 1026 vertices (EARTH_512).
     * Alternating top/bottom rows gives exactly the TRI path's winding;
     * the two facets for each longitude patch use the same flat color.
     * At the poles use three-vertex strips: XGL computes facet normals and
     * does not document safe handling of zero-area strip facets. */
    count = 2U * ((unsigned int)globe_longitude + 1U);
    for (i = 0; i < globe_latitude; ++i)
    {
        top = (unsigned long)i * (globe_longitude + 1UL);
        bottom = top + globe_longitude + 1UL;
        face = (unsigned long)i * globe_longitude;
#ifndef DRV_COLORMAP
        coords = NULL;
#endif
#ifdef TEXTURE
        if (drv_texture_enabled)
            coords = globe_strip_coords;
#endif
        if (i == 0 || i == globe_latitude - 1)
        {
            for (j = 0; j < globe_longitude; ++j)
            {
                /* Preserve the individual TRI path's exact pole UVs,
                 * rather than merging the cap into a differently mapped fan. */
                if (i == 0)
                {
                    cap[0] = top + j + 1;
                    cap[1] = bottom + j;
                    cap[2] = bottom + j + 1;
                }
                else
                {
                    cap[0] = top + j;
                    cap[1] = bottom + j;
                    cap[2] = top + j + 1;
                }
                for (k = 0; k < 3; ++k)
                {
                    globe_strip_verts[k] = globe_verts[cap[k]];
#ifdef TEXTURE
                    if (drv_texture_enabled)
                        globe_strip_coords[k] = globe_coords[cap[k]];
#endif
                }
#ifdef DRV_COLORMAP
                globe_strip_colors[0] = globe_colors[face + j].color;
                drv_draw_tristrip_flat_color(3, globe_strip_verts,
                                             globe_strip_colors);
#else
                globe_strip_colors[0] = globe_colors[face + j];
                drv_draw_tristrip_flat_rgb(3, globe_strip_verts,
                                           globe_strip_colors, coords);
#endif
            }
            continue;
        }
        for (j = 0; j <= globe_longitude; ++j)
        {
            globe_strip_verts[2 * j] = globe_verts[top + j];
            globe_strip_verts[2 * j + 1] = globe_verts[bottom + j];
#ifdef TEXTURE
            if (drv_texture_enabled)
            {
                globe_strip_coords[2 * j] = globe_coords[top + j];
                globe_strip_coords[2 * j + 1] = globe_coords[bottom + j];
            }
#endif
            if (j < globe_longitude)
            {
#ifdef DRV_COLORMAP
                globe_strip_colors[2 * j] = globe_colors[face + j].color;
                globe_strip_colors[2 * j + 1] = globe_colors[face + j].color;
#else
                globe_strip_colors[2 * j] = globe_colors[face + j];
                globe_strip_colors[2 * j + 1] = globe_colors[face + j];
#endif
            }
        }
#ifdef DRV_COLORMAP
        drv_draw_tristrip_flat_color(count, globe_strip_verts,
                                     globe_strip_colors);
#else
        drv_draw_tristrip_flat_rgb(count, globe_strip_verts, globe_strip_colors,
                                   coords);
#endif
    }
}
#endif

void globe_toggle_res(char key)
{
    int new_longitude;

    switch (key)
    {
#ifdef EARTH_32
        case '1': new_longitude = 32; break;
#endif
#ifdef EARTH_64
        case '2': new_longitude = 64; break;
#endif
#ifdef EARTH_128
        case '3': new_longitude = 128; break;
#endif
#ifdef EARTH_256
        case '4': new_longitude = 256; break;
#endif
#ifdef EARTH_512
        case '5': new_longitude = 512; break;
#endif
        default: return;
    }
    globe_init_verts(new_longitude, -1, GLOBE_RADIUS);
}

#ifdef TEXTURE
void globe_toggle_tex(void)
{
    if (drv_texture_enabled)
        drv_texture_disable();
    else
        drv_texture_enable();
}
#endif
