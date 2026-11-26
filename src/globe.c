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
#include <float.h>
#include "testglob.h"
#include "drv.h"
#include "globe.h"
#include "drv_ext.h"
#include "types.h"
#include "e_ext.h"

/* The small palette is shared by every Earth rendering path. */
#ifdef EARTH
#include "e_cmap.h"
#endif

Vertex TG_HUGE *globe_verts;
#ifdef DRV_EARTH_RESOURCES
/* Resource maps are loaded directly into their final owned index storage. */
Color_Index TG_HUGE *globe_colors;
#endif
#ifdef TRISTRIP
/* One latitude band; RGB and UV expansion never require a full second grid. */
static Vertex TG_HUGE *globe_strip_verts;
#ifdef DRV_COLORMAP
static unsigned char TG_HUGE *globe_strip_colors;
#else
static Color_RGB TG_HUGE *globe_strip_colors;
#endif
#ifdef TEXTURE
static Coord TG_HUGE *globe_strip_coords;
#endif
#endif
#ifdef DRV_QUAD_BATCH
static Vertex TG_HUGE *globe_quad_verts;
static unsigned int TG_HUGE *globe_quad_indices;
static unsigned char TG_HUGE *globe_quad_colors;
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
int globe_requested_longitude;
unsigned long globe_revision;
Drv_result globe_last_result = DRV_OK;
static int globe_valid;

static void globe_reset(void)
{
    globe_verts = NULL;
#ifdef DRV_EARTH_RESOURCES
    globe_colors = NULL;
#endif
#ifdef TRISTRIP
    globe_strip_verts = NULL;
    globe_strip_colors = NULL;
#ifdef TEXTURE
    globe_strip_coords = NULL;
#endif
#endif
#ifdef DRV_QUAD_BATCH
    globe_quad_verts = NULL;
    globe_quad_indices = NULL;
    globe_quad_colors = NULL;
#endif
    globe_radius = 0.0f;
    globe_verts_num = globe_faces_num = 0UL;
    globe_tris_num = globe_tris_num_vis = 0UL;
    globe_quads_num = globe_quads_num_vis = 0UL;
    globe_longitude = globe_latitude = 0;
    globe_valid = FALSE;
}

int globe_ready(void)
{
    return globe_valid;
}

void globe_free(void)
{
    if (globe_valid) ++globe_revision;
    drv_free_items(globe_verts);
#ifdef DRV_EARTH_RESOURCES
    drv_free_items(globe_colors);
#endif
#ifdef TRISTRIP
    drv_free_items(globe_strip_verts);
    drv_free_items(globe_strip_colors);
#ifdef TEXTURE
    drv_free_items(globe_strip_coords);
#endif
#endif
#ifdef DRV_QUAD_BATCH
    drv_free_items(globe_quad_verts);
    drv_free_items(globe_quad_indices);
    drv_free_items(globe_quad_colors);
#endif
    globe_reset();
}

void globe_init(void)
{
    globe_free();
    globe_requested_longitude = 0;
    globe_last_result = DRV_OK;
}

static int globe_supported(int longitude)
{
#ifdef DRV_EARTH_RESOURCES
    switch (longitude)
    {
#ifdef EARTH_32
        case 32: return TRUE;
#endif
#ifdef EARTH_64
        case 64: return TRUE;
#endif
#ifdef EARTH_128
        case 128: return TRUE;
#endif
#ifdef EARTH_256
        case 256: return TRUE;
#endif
#ifdef EARTH_512
        case 512: return TRUE;
#endif
        default: return FALSE;
    }
#else
    return earth_data_size(longitude) != 0UL;
#endif
}

static Drv_result globe_validate(int longitude, int latitude, float radius)
{
    if (!globe_supported(longitude) || latitude < 2 ||
        latitude > longitude / 2 || !(radius > 0.0f && radius <= FLT_MAX))
        return DRV_BAD_INPUT;
#ifdef DRV_MAX_RADIUS
    if (radius > DRV_MAX_RADIUS) return DRV_BAD_INPUT;
#endif
    return DRV_OK;
}

static Drv_result globe_alloc(void)
{
    globe_verts = (Vertex TG_HUGE *)drv_alloc_items(globe_verts_num, sizeof(Vertex));
    if (globe_verts == NULL) return DRV_NO_MEMORY;
#ifdef TRISTRIP
    globe_strip_verts = (Vertex TG_HUGE *)drv_alloc_items(
        2UL * (globe_longitude + 1UL), sizeof(Vertex));
#ifdef DRV_COLORMAP
    globe_strip_colors = (unsigned char TG_HUGE *)drv_alloc_items(
        2UL * globe_longitude, sizeof(unsigned char));
#else
    globe_strip_colors = (Color_RGB TG_HUGE *)drv_alloc_items(
        2UL * globe_longitude, sizeof(Color_RGB));
#endif
    if (globe_strip_verts == NULL || globe_strip_colors == NULL)
        return DRV_NO_MEMORY;
#ifdef TEXTURE
    globe_strip_coords = (Coord TG_HUGE *)drv_alloc_items(
        2UL * (globe_longitude + 1UL), sizeof(Coord));
    if (globe_strip_coords == NULL) return DRV_NO_MEMORY;
#endif
#endif
#ifdef DRV_QUAD_BATCH
    globe_quad_verts = (Vertex TG_HUGE *)drv_alloc_items(
        2UL * (DRV_QUAD_BATCH_MAX + 1UL), sizeof(Vertex));
    globe_quad_indices = (unsigned int TG_HUGE *)drv_alloc_items(
        4UL * DRV_QUAD_BATCH_MAX, sizeof(unsigned int));
    globe_quad_colors = (unsigned char TG_HUGE *)drv_alloc_items(
        DRV_QUAD_BATCH_MAX, sizeof(unsigned char));
    if (globe_quad_verts == NULL || globe_quad_indices == NULL ||
        globe_quad_colors == NULL) return DRV_NO_MEMORY;
#endif
    return DRV_OK;
}

/* Rows divide every embedded 16 KiB chunk exactly. Borrow only the bounded
 * row span, never assume successive chunks are contiguous on a huge heap. */
static const unsigned char TG_HUGE *globe_color_row(int row)
{
#ifdef DRV_EARTH_RESOURCES
    return (const unsigned char TG_HUGE *)globe_colors +
           (unsigned long)row * globe_longitude;
#else
    unsigned long source_row;
    unsigned int available;
    const unsigned char TG_HUGE *colors;
    source_row = ((2UL * row + 1UL) * (globe_longitude / 2UL)) /
                 (2UL * globe_latitude);
    colors = earth_data_span(globe_longitude, source_row * globe_longitude,
                             &available);
    if (colors == NULL || available < (unsigned int)globe_longitude)
    {
        drv_abort_frame("Earth data unavailable");
        return NULL;
    }
    return colors;
#endif
}

#ifndef DRV_COLORMAP
static void globe_color_rgb(unsigned char index, Color_RGB *color)
{
    color->r = earth_cmap[index][0];
    color->g = earth_cmap[index][1];
    color->b = earth_cmap[index][2];
}
#endif

int globe_init_verts(int longitude, int latitude, float radius)
{
    int i, j;
    float x, y, z, xy;
    float longitude_step, latitude_step;
    float longitude_angle, latitude_angle, cosine, sine;
    Vertex TG_HUGE *vertex_ptr;
    Vertex TG_HUGE *columns;
#ifdef DRV_EARTH_RESOURCES
    unsigned long row, source_row;
    unsigned char TG_HUGE *destination;
    const unsigned char TG_HUGE *source;
#endif
    if (latitude < 1) latitude = longitude / 2;
    globe_last_result = globe_validate(longitude, latitude, radius);
    if (globe_last_result != DRV_OK) return FALSE;
    if (globe_valid && globe_longitude == longitude &&
        globe_latitude == latitude && globe_radius == radius) return TRUE;
    globe_free();
    globe_radius = radius;
    globe_longitude = longitude;
    globe_latitude = latitude;
    globe_verts_num = (latitude + 1UL) * (longitude + 1UL);
    globe_faces_num = (unsigned long)longitude * latitude;
    globe_tris_num = (latitude - 1UL) * longitude * 2UL;
    globe_quads_num = globe_faces_num;
#ifdef DRV_EARTH_RESOURCES
    /* Load before allocating the large vertex grid. Custom shorter latitude
     * requests retain a source-sized buffer and compact sampled rows in place;
     * source_row >= row makes forward compaction safe without another map. */
    globe_colors = (Color_Index TG_HUGE *)drv_alloc_items(
        (unsigned long)longitude * (longitude / 2UL), sizeof(unsigned char));
    if (globe_colors == NULL)
    {
        globe_last_result = DRV_NO_MEMORY;
        goto failed;
    }
    globe_last_result = drv_load_earth_data(longitude,
                                    (unsigned char TG_HUGE *)globe_colors);
    if (globe_last_result != DRV_OK) goto failed;
    if (latitude != longitude / 2)
    {
        destination = (unsigned char TG_HUGE *)globe_colors;
        for (row = 0UL; row < (unsigned long)latitude; ++row)
        {
            source_row = ((2UL * row + 1UL) * (longitude / 2UL)) /
                         (2UL * latitude);
            source = (const unsigned char TG_HUGE *)globe_colors +
                     source_row * longitude;
            for (j = 0; j < longitude; ++j) *destination++ = *source++;
        }
    }
#endif
    globe_last_result = globe_alloc();
    if (globe_last_result != DRV_OK) goto failed;
    vertex_ptr = globe_verts;
    longitude_step = (float)(2.0 * PI / longitude);
    latitude_step = (float)(PI / latitude);
    /* Every row shares each column's longitude cosine and sine. Stage them
     * in the south-pole row, which is written last and read before it is
     * overwritten, instead of calling (often software) trigonometry for
     * every vertex: 263,682 calls become 1,026 at the 512-wide map. */
    columns = globe_verts + (unsigned long)latitude * (unsigned long)(longitude + 1);
    for (j = 0; j <= longitude; ++j)
    {
        longitude_angle = j * longitude_step;
        columns[j].x = (float)COS(longitude_angle);
        columns[j].y = (float)SIN(longitude_angle);
    }
    for (i = 0; i <= latitude; ++i)
    {
        latitude_angle = (float)(PI / 2.0) - i * latitude_step;
        xy = radius * (float)COS(latitude_angle);
        z = radius * (float)SIN(latitude_angle);
        if (i == 0 || i == latitude)
        {
            xy = 0.0f;
            z = (i == 0 ? radius : -radius);
        }
        for (j = 0; j <= longitude; ++j)
        {
            cosine = columns[j].x;
            sine = columns[j].y;
            x = xy * cosine;
            y = xy * sine;
            vertex_ptr->x = x;
            vertex_ptr->y = y;
            vertex_ptr->z = z;
#ifdef SEG86
            memset(vertex_ptr->reserved, 0, sizeof(vertex_ptr->reserved));
#endif
            ++vertex_ptr;
        }
    }
    globe_valid = TRUE;
    ++globe_revision;
#ifndef DRV_NO_CONSOLE
    /* Report only committed geometry: failed attempts and no-ops are silent. */
    printf("Globe resolution: %i x %i\n", globe_longitude, globe_latitude);
    printf("Vertices: %lu\n", globe_verts_num);
#if defined(TRI) || defined(TRISTRIP)
    printf("Triangles: %lu\n", globe_tris_num);
#endif
#ifdef QUAD
    printf("Quads: %lu\n", globe_quads_num);
#endif
#endif
    return TRUE;
failed:
    globe_free();
    return FALSE;
}

/* Recovery never owns two meshes. Remember the old recipe, release it, then
 * rebuild at most once per candidate; callbacks cannot enter this routine. */
int globe_set_resolution(int longitude, int latitude, float radius)
{
    int previous_longitude, previous_latitude, candidate;
    float previous_radius;
    Drv_result failure;
    char notice[96];

    if (latitude < 1) latitude = longitude / 2;
    globe_last_result = globe_validate(longitude, latitude, radius);
    if (globe_last_result != DRV_OK)
    {
        drv_notice("invalid globe dimensions");
        return FALSE;
    }
    globe_requested_longitude = longitude;
    previous_longitude = globe_valid ? globe_longitude : 0;
    previous_latitude = globe_latitude;
    previous_radius = globe_radius;
    if (globe_init_verts(longitude, latitude, radius))
    {
        drv_notice("");
        if (previous_longitude != longitude || previous_latitude != latitude ||
            previous_radius != radius) drv_reset_timing();
        return TRUE;
    }
    failure = globe_last_result;
    drv_release_optional();
    if (globe_init_verts(longitude, latitude, radius))
    {
        globe_last_result = failure;
        drv_notice("optional caches released");
        drv_reset_timing();
        return TRUE;
    }
    if (previous_longitude != 0 &&
        (previous_longitude != longitude || previous_latitude != latitude ||
         previous_radius != radius) &&
        globe_init_verts(previous_longitude, previous_latitude, previous_radius))
        goto recovered;
    for (candidate = longitude / 2; candidate >= 32; candidate /= 2)
    {
        if (!globe_supported(candidate) || candidate == previous_longitude)
            continue;
        if (globe_init_verts(candidate, -1, radius)) goto recovered;
    }
    globe_last_result = failure;
    sprintf(notice, "%.30s; select a resolution to retry",
             drv_result_text(failure));
    drv_notice(notice);
    drv_reset_timing();
    return FALSE;
recovered:
    globe_last_result = failure;
    sprintf(notice, "resolution %d: %.30s; using %d", longitude,
            drv_result_text(failure), globe_longitude);
    drv_notice(notice);
    drv_reset_timing();
    return TRUE;
}

#ifdef DRV_CULL
typedef struct Globe_cull_view
{
    double eye[3], horizontal_eye, phase, half_cosine, error;
} Globe_cull_view;

static int globe_cull_view_init(Globe_cull_view *view)
{
    if (!drv_cull_eye(view->eye)) return FALSE;
    view->horizontal_eye = sqrt(view->eye[0] * view->eye[0] +
                                  view->eye[1] * view->eye[1]);
    view->phase = atan2(view->eye[1], view->eye[0]);
    if (view->phase < 0.0) view->phase += 2.0 * PI;
    view->half_cosine = cos(PI / globe_longitude);
    view->error = 64.0 * FLT_EPSILON * (double)globe_radius *
                    ((double)GLOBE_VIEW_DISTANCE + globe_radius);
    return TRUE;
}

/* Two cyclic longitude intervals bound each band's front-facing region.
 * Their difference is a narrow silhouette neighborhood, checked against
 * the actual float vertices. No second mesh or per-face visibility array
 * is retained. A numerically tangent band can require checking the whole
 * band; ordinary bands skip their hidden and retained interiors in blocks. */
typedef struct Globe_cull_band
{
    const double *eye;
    int row, per_cell;
    int possible[2][2], definite[2][2];
    int possible_count, definite_count;
} Globe_cull_band;
#endif

#if defined(DRV_CULL) || defined(DRV_QUAD_TRIM)
static int globe_cull_ranges(double amplitude, double bias, double phase,
                              double margin, int ranges[2][2])
{
    double threshold, half, step;
    int first, last, count;
    if (amplitude <= 0.0)
    {
        if (bias < margin) return 0;
        ranges[0][0] = 0; ranges[0][1] = globe_longitude;
        return 1;
    }
    threshold = (margin - bias) / amplitude;
    if (threshold > 1.0) return 0;
    if (threshold <= -1.0)
    {
        ranges[0][0] = 0; ranges[0][1] = globe_longitude;
        return 1;
    }
    half = acos(threshold);
    step = 2.0 * PI / globe_longitude;
    first = (int)ceil((phase - half) / step - 0.5);
    last = (int)floor((phase + half) / step - 0.5) + 1;
    if (first >= last) return 0;
    if (last - first >= globe_longitude)
    {
        ranges[0][0] = 0; ranges[0][1] = globe_longitude;
        return 1;
    }
    count = 0;
    if (first < 0)
    {
        if (last > 0)
        { ranges[count][0] = 0; ranges[count++][1] = last; }
        ranges[count][0] = first + globe_longitude;
        ranges[count++][1] = globe_longitude;
    }
    else if (last > globe_longitude)
    {
        ranges[count][0] = 0;
        ranges[count++][1] = last - globe_longitude;
        if (first < globe_longitude)
        { ranges[count][0] = first; ranges[count++][1] = globe_longitude; }
    }
    else
    {
        ranges[count][0] = first;
        ranges[count++][1] = last;
    }
    return count;
}
#endif

#ifdef DRV_CULL
static void globe_cull_band_init(Globe_cull_band *band, int row,
                                  const Globe_cull_view *view)
{
    const Vertex TG_HUGE *top, *bottom;
    double horizontal, vertical, plane, amplitude, bias;
    band->eye = view->eye;
    band->row = row;
    band->per_cell = row == 0 || row == globe_latitude - 1 ? 1 : 2;
    top = globe_verts + (unsigned long)row * (globe_longitude + 1UL);
    bottom = top + globe_longitude + 1;
    /* A latitude cell lies in n.xy = horizontal*(cos(mid),sin(mid)),
     * n.z = vertical, n.p = plane. The finite eye handles perspective. */
    horizontal = ((double)top->z - bottom->z) / view->half_cosine;
    vertical = (double)bottom->x - top->x;
    plane = (double)bottom->x * top->z - (double)top->x * bottom->z;
    amplitude = horizontal * view->horizontal_eye;
    bias = vertical * view->eye[2] - plane;
    /* Allow for float trigonometry, vertex rounding and differences between
     * the two triangles' planes. This only expands the boundary checks. */
    band->possible_count = globe_cull_ranges(amplitude, bias, view->phase, -view->error,
                                             band->possible);
    band->definite_count = globe_cull_ranges(amplitude, bias, view->phase, view->error,
                                             band->definite);
    if (globe_radius < FLT_MIN / FLT_EPSILON)
    {
        /* Subnormal vertices have no useful relative rounding bound. */
        band->possible_count = 1;
        band->possible[0][0] = 0;
        band->possible[0][1] = globe_longitude;
        band->definite_count = 0;
    }
}

static int globe_cull_boundary(const Globe_cull_band *band, int facet)
{
    const Vertex TG_HUGE *top, *bottom;
    int cell;
    cell = facet / band->per_cell;
    top = globe_verts + (unsigned long)band->row * (globe_longitude + 1UL) + cell;
    bottom = top + globe_longitude + 1;
    if (band->row == 0 || (band->per_cell == 2 && (facet & 1)))
        return drv_cull_triangle(top + 1, bottom, bottom + 1, band->eye);
    return drv_cull_triangle(top, bottom, top + 1, band->eye);
}

static int globe_cull_step(const Globe_cull_band *band, int facet, int *next)
{
    int i, j, first, last;
    for (i = 0; i < band->possible_count; ++i)
    {
        first = band->possible[i][0] * band->per_cell;
        last = band->possible[i][1] * band->per_cell;
        if (facet < first) { *next = first; return FALSE; }
        if (facet >= last) continue;
        for (j = 0; j < band->definite_count; ++j)
        {
            if (facet >= band->definite[j][0] * band->per_cell &&
                facet < band->definite[j][1] * band->per_cell)
            {
                *next = band->definite[j][1] * band->per_cell;
                if (*next > last) *next = last;
                return TRUE;
            }
        }
        *next = facet + 1;
        return globe_cull_boundary(band, facet);
    }
    *next = globe_longitude * band->per_cell;
    return FALSE;
}

static int globe_cull_next(const Globe_cull_band *band, int cursor,
                            int *first, int *last)
{
    int next, visible, started;
    started = FALSE;
    while (cursor < globe_longitude * band->per_cell)
    {
        visible = globe_cull_step(band, cursor, &next);
        if (!visible && started) break;
        if (visible && !started) { *first = cursor; started = TRUE; }
        cursor = next;
    }
    *last = cursor;
    return started;
}
#endif

#if defined(TRI) || defined(DRV_TRI_CAPS)
#ifdef DRV_CULL
static void globe_draw_tri_facet(int row, int facet,
                                  const unsigned char TG_HUGE *colors)
{
    Vertex TG_HUGE *a, TG_HUGE *b, TG_HUGE *c;
    unsigned long top, bottom;
    int cell, second;
#ifndef DRV_COLORMAP
    Color_RGB color;
#endif
#ifdef TEXTURE
    Coord ca, cb, cc;
#endif
    second = row == 0 || (row != globe_latitude - 1 && (facet & 1));
    cell = row == 0 || row == globe_latitude - 1 ? facet : facet / 2;
    top = (unsigned long)row * (globe_longitude + 1UL) + cell;
    bottom = top + globe_longitude + 1UL;
    a = globe_verts + top + (second ? 1 : 0);
    b = globe_verts + bottom;
    c = globe_verts + (second ? bottom : top) + 1;
#ifdef TEXTURE
    if (drv_texture_active)
    {
        ca.u = (float)(cell + (second ? 1 : 0)) / globe_longitude;
        ca.v = (float)row / globe_latitude;
        cb.u = (float)cell / globe_longitude;
        cb.v = (float)(row + 1) / globe_latitude;
        cc.u = (float)(cell + 1) / globe_longitude;
        cc.v = (float)(row + (second ? 1 : 0)) / globe_latitude;
        drv_draw_tri_flat_uv(a, b, c, &ca, &cb, &cc);
        return;
    }
#endif
#ifdef DRV_COLORMAP
    drv_draw_tri_flat_color(a, b, c, colors[cell]);
#else
    globe_color_rgb(colors[cell], &color);
    drv_draw_tri_flat_rgb(a, b, c, &color);
#endif
}
#endif

static void globe_draw_tri_rows(int first, int last)
{
    const unsigned char TG_HUGE *colors;
    Vertex TG_HUGE *v1, *v2, *v1_1, *v2_1;
#ifndef DRV_COLORMAP
    Color_RGB color;
#endif
#ifdef TEXTURE
    Coord c1, c2, c1_1, c2_1;
#endif
    int i, j;
    unsigned long row;
#ifdef DRV_CULL
    Globe_cull_view view;
    Globe_cull_band band;
    int cursor, retained_first, retained_last, facet;
#endif

    if (!globe_valid || drv_frame_failed || first >= last) return;
#ifdef DRV_CULL
    if (drv_cull_enabled && !globe_cull_view_init(&view))
    { drv_abort_frame("invalid culling view"); return; }
#endif
    for (i = first; i < last; ++i)
    {
        colors = globe_color_row(i);
        if (colors == NULL) return;
#ifdef DRV_CULL
        if (drv_cull_enabled)
        {
            globe_cull_band_init(&band, i, &view);
            cursor = 0;
            while (globe_cull_next(&band, cursor, &retained_first, &retained_last))
            {
                for (facet = retained_first; facet < retained_last; ++facet)
                {
                    globe_draw_tri_facet(i, facet, colors);
                    if (drv_frame_failed) return;
                }
                cursor = retained_last;
            }
            continue;
        }
#endif
        row = (unsigned long)i * (globe_longitude + 1UL);
        v1 = globe_verts + row;
        v1_1 = v1 + 1;
        v2 = v1 + globe_longitude + 1;
        v2_1 = v2 + 1;
#ifdef TEXTURE
        c1.v = c1_1.v = (float)i / globe_latitude;
        c2.v = c2_1.v = (float)(i + 1) / globe_latitude;
#endif
        for (j = 0; j < globe_longitude; ++j)
        {
#ifdef TEXTURE
            if (drv_texture_active)
            {
                c1.u = c2.u = (float)j / globe_longitude;
                c1_1.u = c2_1.u = (float)(j + 1) / globe_longitude;
                if (i != 0) drv_draw_tri_flat_uv(v1, v2, v1_1, &c1, &c2, &c1_1);
                if (drv_frame_failed) return;
                if (i != globe_latitude - 1)
                    drv_draw_tri_flat_uv(v1_1, v2, v2_1, &c1_1, &c2, &c2_1);
            }
            else
#endif
            {
#ifndef DRV_COLORMAP
                globe_color_rgb(colors[j], &color);
#endif
                if (i != 0)
#ifdef DRV_COLORMAP
                    drv_draw_tri_flat_color(v1, v2, v1_1, colors[j]);
#else
                    drv_draw_tri_flat_rgb(v1, v2, v1_1, &color);
#endif
                if (drv_frame_failed) return;
                if (i != globe_latitude - 1)
#ifdef DRV_COLORMAP
                    drv_draw_tri_flat_color(v1_1, v2, v2_1, colors[j]);
#else
                    drv_draw_tri_flat_rgb(v1_1, v2, v2_1, &color);
#endif
            }
            if (drv_frame_failed) return;
            ++v1; ++v1_1; ++v2; ++v2_1;
        }
    }
}
#ifdef TRI
void globe_draw_tris(void)
{
    globe_draw_tri_rows(0, globe_latitude);
}
#endif
#if defined(TRISTRIP) && defined(DRV_TRI_CAPS)
void globe_draw_pole_tris(void)
{
    if (globe_latitude < 2) return;
    globe_draw_tri_rows(0, 1);
    globe_draw_tri_rows(globe_latitude - 1, globe_latitude);
}
#endif
#endif

#ifdef TRISTRIP
static void globe_submit_tristrip(unsigned int count)
{
#ifdef TEXTURE
    if (drv_texture_active)
    {
        drv_draw_tristrip_flat_uv(count, globe_strip_verts, globe_strip_coords);
        return;
    }
#endif
#ifdef DRV_COLORMAP
    drv_draw_tristrip_flat_color(count, globe_strip_verts, globe_strip_colors);
#else
    drv_draw_tristrip_flat_rgb(count, globe_strip_verts, globe_strip_colors);
#endif
}

#if defined(DRV_CULL) || !defined(DRV_TRI_CAPS)
static void globe_strip_vertex(unsigned int destination, unsigned long source)
{
    globe_strip_verts[destination] = globe_verts[source];
#ifdef TEXTURE
    if (drv_texture_active)
    {
        globe_strip_coords[destination].u =
            (float)(source % (globe_longitude + 1UL)) / globe_longitude;
        globe_strip_coords[destination].v =
            (float)(source / (globe_longitude + 1UL)) / globe_latitude;
    }
#endif
}

static void globe_strip_color(unsigned int destination, unsigned char color)
{
#ifdef DRV_COLORMAP
    globe_strip_colors[destination] = color;
#else
    globe_color_rgb(color, globe_strip_colors + destination);
#endif
}
#endif

#ifndef DRV_TRI_CAPS
/* Both traversal paths retain the original pole triangle's winding and UVs. */
static void globe_submit_pole_strip(int row, unsigned long top,
                                     unsigned long bottom, int column,
                                     unsigned char color)
{
    globe_strip_vertex(0U, top + column + (row == 0 ? 1UL : 0UL));
    globe_strip_vertex(1U, bottom + column);
    globe_strip_vertex(2U, (row == 0 ? bottom : top) + column + 1UL);
    globe_strip_color(0U, color);
    globe_submit_tristrip(3U);
}
#endif

#ifdef DRV_CULL
static void globe_draw_culled_strip_row(int row,
                                         const unsigned char TG_HUGE *colors,
                                         const Globe_cull_view *view)
{
    Globe_cull_band band;
    unsigned long top, bottom, source;
    unsigned int i, raw, count;
    int cursor, first, last;
#ifndef DRV_TRI_CAPS
    int j;
#endif
    globe_cull_band_init(&band, row, view);
    top = (unsigned long)row * (globe_longitude + 1UL);
    bottom = top + globe_longitude + 1UL;
    cursor = 0;
    while (globe_cull_next(&band, cursor, &first, &last))
    {
        cursor = last;
        if (band.per_cell == 1)
        {
#ifndef DRV_TRI_CAPS
            for (j = first; j < last; ++j)
            {
                globe_submit_pole_strip(row, top, bottom, j, colors[j]);
                if (drv_frame_failed) return;
            }
#endif
            continue;
        }
        if (first & 1)
        {
            /* Restarting on an odd triangle reverses a strip's winding.
             * Emit that one triangle in its original order, then resume
             * the remainder at an even facet with no degenerate padding. */
            globe_strip_vertex(0U, top + (unsigned int)(first + 1) / 2U);
            globe_strip_vertex(1U, bottom + (unsigned int)first / 2U);
            globe_strip_vertex(2U, bottom + (unsigned int)(first + 2) / 2U);
            globe_strip_color(0U, colors[first / 2]);
            globe_submit_tristrip(3U);
            if (drv_frame_failed) return;
            ++first;
        }
        if (first >= last) continue;
        count = (unsigned int)(last - first + 2);
        for (i = 0; i < count; ++i)
        {
            raw = (unsigned int)first + i;
            source = (raw & 1U ? bottom : top) + raw / 2U;
            globe_strip_vertex(i, source);
            if (i < count - 2U) globe_strip_color(i, colors[raw / 2U]);
        }
        globe_submit_tristrip(count);
        if (drv_frame_failed) return;
    }
}
#endif

void globe_draw_tristrips(void)
{
    int i, j;
    unsigned int count;
    unsigned long top, bottom;
    const unsigned char TG_HUGE *colors;
#ifdef DRV_CULL
    Globe_cull_view view;
#endif
    if (!globe_valid || drv_frame_failed) return;
#ifdef DRV_CULL
    if (drv_cull_enabled && !globe_cull_view_init(&view))
    { drv_abort_frame("invalid culling view"); return; }
#endif
    count = 2U * ((unsigned int)globe_longitude + 1U);
    for (i = 0; i < globe_latitude; ++i)
    {
        colors = globe_color_row(i);
        if (colors == NULL) return;
#ifdef DRV_CULL
        if (drv_cull_enabled)
        {
#ifdef DRV_TRI_CAPS
            if (i == 0 || i == globe_latitude - 1) continue;
#endif
            globe_draw_culled_strip_row(i, colors, &view);
            if (drv_frame_failed) return;
            continue;
        }
#endif
        top = (unsigned long)i * (globe_longitude + 1UL);
        bottom = top + globe_longitude + 1UL;
        if (i == 0 || i == globe_latitude - 1)
        {
#ifndef DRV_TRI_CAPS
            for (j = 0; j < globe_longitude; ++j)
            {
                globe_submit_pole_strip(i, top, bottom, j, colors[j]);
                if (drv_frame_failed) return;
            }
#endif
            continue;
        }
        for (j = 0; j <= globe_longitude; ++j)
        {
            globe_strip_verts[2 * j] = globe_verts[top + j];
            globe_strip_verts[2 * j + 1] = globe_verts[bottom + j];
#ifdef TEXTURE
            if (drv_texture_active)
            {
                globe_strip_coords[2 * j].u = (float)j / globe_longitude;
                globe_strip_coords[2 * j + 1].u = (float)j / globe_longitude;
                globe_strip_coords[2 * j].v = (float)i / globe_latitude;
                globe_strip_coords[2 * j + 1].v = (float)(i + 1) / globe_latitude;
            }
#endif
            if (j < globe_longitude)
            {
#ifdef DRV_COLORMAP
                globe_strip_colors[2 * j] = colors[j];
#else
                globe_color_rgb(colors[j], globe_strip_colors + 2 * j);
#endif
                globe_strip_colors[2 * j + 1] = globe_strip_colors[2 * j];
            }
        }
        globe_submit_tristrip(count);
        if (drv_frame_failed) return;
    }
}
#endif

#ifdef QUAD
void globe_draw_quads(void)
{
    const unsigned char TG_HUGE *colors;
    Vertex TG_HUGE *v1, *v2, *v1_1, *v2_1;
#ifndef DRV_COLORMAP
    Color_RGB color;
#endif
    int i, j;
    if (!globe_valid || drv_frame_failed) return;
    for (i = 0; i < globe_latitude; ++i)
    {
        colors = globe_color_row(i);
        if (colors == NULL) return;
        v1 = globe_verts + (unsigned long)i * (globe_longitude + 1UL);
        v1_1 = v1 + 1;
        v2 = v1 + globe_longitude + 1;
        v2_1 = v2 + 1;
        for (j = 0; j < globe_longitude; ++j)
        {
#ifdef DRV_COLORMAP
            drv_draw_quad_flat_color(v1, v2, v2_1, v1_1, colors[j]);
#else
            globe_color_rgb(colors[j], &color);
            drv_draw_quad_flat_rgb(v1, v2, v2_1, v1_1, &color);
#endif
            if (drv_frame_failed) return;
            ++v1; ++v1_1; ++v2; ++v2_1;
        }
    }
}

#ifdef DRV_QUAD_BATCH
#ifdef DRV_QUAD_TRIM
/* Back-face trimming for quad batches. Each latitude band's cells are
 * congruent planar trapezoids (triangles at the poles) whose plane value
 * v = n.eye - n.p varies with longitude as amplitude*cos(mid - phase) +
 * bias; -v/|n| is the eye's distance behind a cell's plane.
 *
 * Integer rounding reverses a back face only while its projection is
 * thinner than about a pixel. Across 3,600 random poses, windows and pixel
 * aspects at every resolution, no cell that the fixed pipeline drew from
 * behind was more than 1.2 pixels thick. Projection scales areas by
 * fx*fy*d/Z^3 and lengths by about f/Z, so a cell d behind its plane is at
 * least fmin*d*A/(Zmax^2*Lmax) pixels thick, with area A, longest edge Lmax
 * and depth at most Zmax = D + R. Only cells thicker than GLOBE_TRIM_PIXELS
 * plus the driver's transform error are omitted, and never in a frame whose
 * globe could reach the viewport edge, where clipping adds rounded vertices. */
#define GLOBE_TRIM_PIXELS 4.0
#define GLOBE_TRIM_BANDS 256

typedef struct Globe_trim_view
{
    Drv_quad_trim model;
    double horizontal_eye, phase, error;
} Globe_trim_view;

/* Margins on v per band, kept for one mesh revision and screen model.
 * Negative: the band can never be trimmed. */
static double globe_trim_margin[GLOBE_TRIM_BANDS];
static double globe_trim_half_cosine;
static Drv_quad_trim globe_trim_model;
static unsigned long globe_trim_revision;
static int globe_trim_valid;

static double globe_trim_length(const Vertex TG_HUGE *a, const Vertex TG_HUGE *b)
{
    double x, y, z;
    x = (double)b->x - a->x; y = (double)b->y - a->y; z = (double)b->z - a->z;
    return sqrt(x * x + y * y + z * z);
}

/* FALSE when no band of this mesh can be trimmed on this screen, so frames
 * then skip the per-frame trigonometry entirely. */
static int globe_trim_margins(const Drv_quad_trim *model)
{
    const Vertex TG_HUGE *top, *bottom;
    double radius, near_z, far_z, k, e_x, e_y, tangent, thickness, focal;
    double ax, ay, az, bx, by, bz, cx, cy, cz, area, longest, edge, normal, margin;
    int row, side, trimmable;
    radius = (double)globe_radius + model->world_error + model->world_scale * globe_radius;
    near_z = (double)GLOBE_VIEW_DISTANCE - radius;
    far_z = (double)GLOBE_VIEW_DISTANCE + radius;
    if (!(near_z > 0.0) || globe_latitude > GLOBE_TRIM_BANDS || globe_longitude < 3 ||
        !(model->focal_x > 0.0 && model->focal_y > 0.0)) return FALSE;
    /* Bound how far a transform error can move a projected corner. */
    k = 1.0 / near_z + radius / (near_z * near_z);
    e_x = model->pixel_x + model->focal_x * k * (radius - globe_radius);
    e_y = model->pixel_y + model->focal_y * k * (radius - globe_radius);
    tangent = radius / sqrt((double)GLOBE_VIEW_DISTANCE * GLOBE_VIEW_DISTANCE - radius * radius);
    if (model->center_x - model->focal_x * tangent - e_x < 0.0 ||
        model->center_x + model->focal_x * tangent + e_x > model->limit_x ||
        model->center_y - model->focal_y * tangent - e_y < 0.0 ||
        model->center_y + model->focal_y * tangent + e_y > model->limit_y) return FALSE;
    thickness = GLOBE_TRIM_PIXELS + (e_x - model->pixel_x) + (e_y - model->pixel_y);
    focal = model->focal_x < model->focal_y ? model->focal_x : model->focal_y;
    globe_trim_half_cosine = cos(PI / globe_longitude);
    trimmable = FALSE;
    for (row = 0; row < globe_latitude; ++row)
    {
        top = globe_verts + (unsigned long)row * (globe_longitude + 1UL);
        bottom = top + globe_longitude + 1;
        /* Half the diagonals' cross product is the planar cell's area. */
        ax = (double)bottom[1].x - top->x; ay = (double)bottom[1].y - top->y;
        az = (double)bottom[1].z - top->z;
        bx = (double)top[1].x - bottom->x; by = (double)top[1].y - bottom->y;
        bz = (double)top[1].z - bottom->z;
        cx = ay * bz - az * by; cy = az * bx - ax * bz; cz = ax * by - ay * bx;
        area = 0.5 * sqrt(cx * cx + cy * cy + cz * cz);
        longest = 0.0;
        for (side = 0; side < 4; ++side)
        {
            edge = side == 0 ? globe_trim_length(top, top + 1) :
                   side == 1 ? globe_trim_length(top + 1, bottom + 1) :
                   side == 2 ? globe_trim_length(bottom + 1, bottom) :
                   globe_trim_length(bottom, top);
            if (edge > longest) longest = edge;
        }
        normal = sqrt(((double)top->z - bottom->z) * ((double)top->z - bottom->z) /
                      (globe_trim_half_cosine * globe_trim_half_cosine) +
                      ((double)bottom->x - top->x) * ((double)bottom->x - top->x));
        margin = area > 0.0 ?
                 normal * thickness * far_z * far_z * longest / (focal * area) : -1.0;
        /* No cell lies more than D + R behind its plane. */
        if (!(margin >= 0.0 && margin < normal * far_z)) margin = -1.0;
        else trimmable = TRUE;
        globe_trim_margin[row] = margin;
    }
    return trimmable;
}

static int globe_trim_begin(Globe_trim_view *view)
{
    const Drv_quad_trim *model;
    const double *eye;
    model = &view->model;
    if (!drv_quad_trim_view(&view->model)) return FALSE;
    /* Margins depend only on the mesh and the screen model, not the pose. */
    if (!globe_trim_valid || globe_trim_revision != globe_revision ||
        globe_trim_model.focal_x != model->focal_x ||
        globe_trim_model.focal_y != model->focal_y ||
        globe_trim_model.center_x != model->center_x ||
        globe_trim_model.center_y != model->center_y ||
        globe_trim_model.limit_x != model->limit_x ||
        globe_trim_model.limit_y != model->limit_y ||
        globe_trim_model.pixel_x != model->pixel_x ||
        globe_trim_model.pixel_y != model->pixel_y ||
        globe_trim_model.world_error != model->world_error ||
        globe_trim_model.world_scale != model->world_scale)
    {
        globe_trim_model = *model;
        globe_trim_revision = globe_revision;
        globe_trim_valid = globe_trim_margins(model) ? 1 : -1;
    }
    /* With no trimmable band, a frame costs no trigonometry at all. */
    if (globe_trim_valid < 0 || globe_radius < FLT_MIN / FLT_EPSILON) return FALSE;
    eye = model->eye;
    if (!(eye[0] >= -DBL_MAX && eye[0] <= DBL_MAX && eye[1] >= -DBL_MAX &&
          eye[1] <= DBL_MAX && eye[2] >= -DBL_MAX && eye[2] <= DBL_MAX)) return FALSE;
    view->horizontal_eye = sqrt(eye[0] * eye[0] + eye[1] * eye[1]);
    view->phase = atan2(eye[1], eye[0]);
    if (view->phase < 0.0) view->phase += 2.0 * PI;
    /* As for band culling: float vertices and trigonometry bound v. */
    view->error = 64.0 * FLT_EPSILON * (double)globe_radius *
                  ((double)GLOBE_VIEW_DISTANCE + globe_radius);
    return TRUE;
}

/* The hull of the band's kept cells within columns first..first+count-1. */
static void globe_trim_row(const Globe_trim_view *view, int row, unsigned int first,
                           unsigned int count, unsigned int *start, unsigned int *end)
{
    const Vertex TG_HUGE *top, *bottom;
    double horizontal, vertical, plane;
    int ranges[2][2], total, i;
    unsigned int low, high;
    *start = first;
    *end = first + count;
    if (globe_trim_margin[row] < 0.0) return;
    top = globe_verts + (unsigned long)row * (globe_longitude + 1UL);
    bottom = top + globe_longitude + 1;
    horizontal = ((double)top->z - bottom->z) / globe_trim_half_cosine;
    vertical = (double)bottom->x - top->x;
    plane = (double)bottom->x * top->z - (double)top->x * bottom->z;
    total = globe_cull_ranges(horizontal * view->horizontal_eye,
                              vertical * view->model.eye[2] - plane, view->phase,
                              -(globe_trim_margin[row] + view->error), ranges);
    *end = first;
    for (i = 0; i < total; ++i)
    {
        low = (unsigned int)ranges[i][0] > first ? (unsigned int)ranges[i][0] : first;
        high = (unsigned int)ranges[i][1] < first + count ? (unsigned int)ranges[i][1] : first + count;
        if (low >= high) continue;
        if (*end == first) *start = low;
        else if (low < *start) *start = low;
        if (high > *end) *end = high;
    }
    if (*end == first) *start = first;
}
#endif

void globe_draw_quadbatches(void)
{
    unsigned long top, bottom;
    unsigned int first, count, j, index, start, span;
    const unsigned char TG_HUGE *colors;
    int i, reuse;
#ifdef DRV_QUAD_TRIM
    Globe_trim_view trim;
    unsigned int end, previous_start, previous_end;
    int trimming;
    trimming = globe_valid && !drv_frame_failed && globe_trim_begin(&trim);
#endif
    if (!globe_valid || drv_frame_failed) return;
    /* A narrow longitude tile streams down the globe before advancing
     * sideways. Its previous lower row becomes the next packet's upper
     * row, keeping transform/projection reuse bounded by packet capacity
     * rather than by the full width of a high-resolution globe. */
    for (first = 0; first < (unsigned int)globe_longitude; first += count)
    {
        count = (unsigned int)globe_longitude - first;
        if (count > DRV_QUAD_BATCH_MAX) count = DRV_QUAD_BATCH_MAX;
#ifdef DRV_QUAD_TRIM
        previous_start = previous_end = first;
#endif
        for (i = 0; i < globe_latitude; ++i)
        {
            colors = globe_color_row(i);
            if (colors == NULL) return;
            top = (unsigned long)i * (globe_longitude + 1UL);
            bottom = top + globe_longitude + 1UL;
            start = first;
            span = count;
            reuse = i != 0;
#ifdef DRV_QUAD_TRIM
            if (trimming)
            {
                globe_trim_row(&trim, i, first, count, &start, &end);
                if (start >= end)
                {
                    previous_start = previous_end = first;
                    continue;
                }
                /* Keep the previous row's wider run while its extra quads
                 * cost less than transforming a new upper row; the row
                 * reuse needs identical columns in consecutive packets. */
                if (previous_end > previous_start && start >= previous_start &&
                    end <= previous_end &&
                    4U * ((previous_end - previous_start) - (end - start)) <
                    5U * (end - start + 1U))
                {
                    start = previous_start;
                    end = previous_end;
                }
                reuse = i != 0 && start == previous_start && end == previous_end;
                previous_start = start;
                previous_end = end;
                span = end - start;
            }
#endif
            /* Every packet keeps the original order of its row's quads. */
            for (j = 0; j <= span; ++j)
            {
                index = 2U * j;
                globe_quad_verts[index] = globe_verts[top + start + j];
                globe_quad_verts[index + 1U] = globe_verts[bottom + start + j];
                if (j == span) continue;
                globe_quad_indices[4U * j] = index;
                globe_quad_indices[4U * j + 1U] = index + 1U;
                globe_quad_indices[4U * j + 2U] = index + 3U;
                globe_quad_indices[4U * j + 3U] = index + 2U;
                globe_quad_colors[j] = colors[start + j];
            }
            drv_submit_quadbatch(2U * (span + 1U), globe_quad_verts,
                                   span, globe_quad_indices,
                                   globe_quad_colors, reuse);
            if (drv_frame_failed) return;
        }
    }
}
#endif
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
    globe_set_resolution(new_longitude, -1, GLOBE_RADIUS);
}

#ifdef TEXTURE
void globe_toggle_tex(void)
{
    if (drv_texture_enabled) drv_texture_disable();
    else drv_texture_enable();
}
#endif
