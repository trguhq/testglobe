/*
 * testglobe - display 3d globe to test 3d hardware
 * drv.c - graphics driver
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

#include "testglob.h"
#include "drv.h"
#include "drv_ext.h"
#include "globe.h"
#include "glob_ext.h"
#include <stdio.h>
#include <math.h>
#include <float.h>
#ifdef DRV_EARTH_RESOURCES
#include "earthres.h"
#endif
#ifdef GDI
#include <windows.h>
#elif defined(SEG68)
#include <Events.h>
#else
#ifdef SEG86
#include <time.h>
#else /* FLAT */
#include <sys/time.h>
#endif
#endif

unsigned int drv_win_x, drv_win_y;              /* window geometry */
unsigned int drv_win_width, drv_win_height;     /* window geometry */
int drv_colormapped;                    /* bool is colormap window */
int drv_texture_avail;                  /* is texturing available in the driver */
int drv_texture_loaded;                 /* has texture been loaded */
int drv_texture_enabled;                /* is texturing enabled */
int drv_osd_enabled;                    /* is OSD enabled */
unsigned int drv_bits_per_pixel;        /* bits depth */
unsigned int drv_colormap_bits;         /* bits per color */
unsigned long int drv_colors;           /* number of colors */
unsigned long int drv_colormap_colors;  /* number of colors in colormap */
unsigned int drv_bytes_per_pixel;       /* bytes per color: 1, 2 or 3 */
char drv_name[64];                      /* name of driver */
float drv_rot_x;                        /* rotation of globe on x axis */
float drv_rot_y;                        /* rotation of globe on y axis */
int drv_lmouse_pressed;                 /* mouse pressed */
int drv_rmouse_pressed;                 /* */
char drv_help[256];                     /* OSD help string to print */
int drv_fps_enabled;                    /* fps counter enabled */
float drv_fps_average;                  /* average fps */
#if defined(GDI) || defined(SEG68)
static unsigned long drv_frame_start, drv_frame_stop;
#else
#ifdef SEG86
static clock_t drv_frame_start, drv_frame_stop;
#else /* FLAT */
static struct timeval drv_frame_start, drv_frame_stop;
#endif
#endif
static float drv_frame_seconds;
static float drv_fps_history_seconds;
static long drv_fps_history_num;        /* frames in this FPS sample */
int drv_auto_enabled;                   /* automatically rotate */

void drv_format_stats(char *buffer)
{
    char status[192];
    size_t length;
    unsigned long total;
    const char *primitive;
#ifdef DRV_VISIBLE_COUNTS
    unsigned long visible;
#endif
    if (buffer == NULL) return;
#ifdef QUAD
    total = globe_quads_num;
    primitive = "quads";
#ifdef DRV_VISIBLE_COUNTS
    visible = globe_quads_num_vis;
#endif
#else /* TRI and TRISTRIP both count the triangles submitted for drawing. */
    total = globe_tris_num;
    primitive = "triangles";
#ifdef DRV_VISIBLE_COUNTS
    visible = globe_tris_num_vis;
#endif
#endif
#ifdef DRV_VISIBLE_COUNTS
    sprintf(status, "%lu %s (%lu visible)", total, primitive, visible);
#else
    /* Native pipelines cull internally without returning a facet count. */
    sprintf(status, "%lu %s", total, primitive);
#endif
    if (drv_fps_enabled)
        sprintf(status + strlen(status), ", %.2f frames per second",
                 (double)drv_fps_average);
#if DEBUG
    sprintf(status + strlen(status), ", %u B/px, R=%d", drv_bytes_per_pixel,
             drv_rmouse_pressed != FALSE);
#endif
    /* The public contract is 128 bytes, including diagnostic builds. The
     * intermediate fits even native float maxima and 64-bit host counts. */
    length = strlen(status);
    if (length > 127U) length = 127U;
    memcpy(buffer, status, length);
    buffer[length] = 0;
}

#if defined(TRISTRIP) && defined(DRV_COLORMAP)
void drv_split_tristrip_batches(unsigned int count,
                            const Vertex TG_HUGE *vertices,
                            const unsigned char TG_HUGE *facet_colors,
                            unsigned int max_facets,
                            Drv_strip_batch draw_batch)
{
    unsigned int first, facets;
    if (count < 3 || vertices == NULL || facet_colors == NULL || draw_batch == NULL)
        return;
    /* Every nonfinal batch starts at the strip's original even winding.
     * Facet i uses vertices i..i+2, so adjoining batches share two vertices. */
    max_facets &= ~1U;
    if (max_facets < 2U)
    {
        error("Strip batch must hold at least two facets");
        return;
    }
    first = 0;
    while (first < count - 2U)
    {
        facets = count - 2U - first;
        if (facets > max_facets) facets = max_facets;
        draw_batch(facets + 2U, vertices + first, facet_colors + first);
        first += facets;
    }
}
#endif

#if defined(TRISTRIP) && defined(TEXTURE)
void drv_expand_tristrip_uv(unsigned int count,
                            const Vertex TG_HUGE *vertices,
                            const Coord TG_HUGE *coords,
                            Drv_uv_triangle draw_triangle)
{
    unsigned int i, first, second;
    if (count < 3U || vertices == NULL || coords == NULL || draw_triangle == NULL)
        return;
    for (i = 0; i < count - 2U; ++i)
    {
        first = (i & 1U) ? i + 1U : i;
        second = (i & 1U) ? i : i + 1U;
        draw_triangle(vertices + first, vertices + second, vertices + i + 2U,
                       coords + first, coords + second, coords + i + 2U);
    }
}
#endif

#ifdef DRV_QUAD_BATCH
int drv_validate_quadbatch(unsigned int vertex_count, unsigned int quad_count,
                            const unsigned int TG_HUGE *indices)
{
    unsigned int i;
    if (vertex_count == 0 || vertex_count > 2U * (DRV_QUAD_BATCH_MAX + 1U) ||
        quad_count == 0 || quad_count > DRV_QUAD_BATCH_MAX || indices == NULL)
        return FALSE;
    for (i = 0; i < quad_count * 4U; ++i)
        if (indices[i] >= vertex_count) return FALSE;
    return TRUE;
}

void drv_submit_quad_as_batch(Vertex TG_HUGE *first, Vertex TG_HUGE *second,
                               Vertex TG_HUGE *third, Vertex TG_HUGE *fourth,
                               unsigned char color)
{
    Vertex vertices[4];
    static const unsigned int indices[4] = { 0, 1, 2, 3 };
    if (first == NULL || second == NULL || third == NULL || fourth == NULL) return;
    vertices[0] = *first; vertices[1] = *second;
    vertices[2] = *third; vertices[3] = *fourth;
    drv_draw_quadbatch_flat_color(4U, vertices, 1U, indices, &color);
}
#endif

#ifdef DRV_EARTH_RESOURCES
void drv_load_earth_data(int longitude, unsigned char TG_HUGE *destination)
{
    unsigned int resource;
    unsigned long remaining, length;
    switch (longitude)
    {
        case 32: resource = TG_EARTH_32_ID; break;
        case 64: resource = TG_EARTH_64_ID; break;
        case 128: resource = TG_EARTH_128_ID; break;
        case 256: resource = TG_EARTH_256_ID; break;
        case 512: resource = TG_EARTH_512_ID; break;
        default: error("Unsupported Earth resource resolution"); return;
    }
    if (destination == NULL)
    {
        error("Missing Earth resource destination");
        return;
    }
    remaining = (unsigned long)longitude * ((unsigned long)longitude / 2UL);
    while (remaining != 0)
    {
        length = remaining < TG_EARTH_CHUNK_SIZE ? remaining : TG_EARTH_CHUNK_SIZE;
        drv_load_earth_chunk(resource, length, destination);
        destination += length;
        remaining -= length;
        ++resource;
    }
}
#endif

/* call first */
void drv_ext_init(void)
{
#ifndef DRV
    error("No driver loaded");          /* won't compile anyway */
#endif
    drv_win_x = DRV_WIN_X;
    drv_win_y = DRV_WIN_Y;
    drv_win_width = DRV_WIN_WIDTH;
    drv_win_height = DRV_WIN_HEIGHT;
    drv_colormapped = DRV_COLORMAPPED;
#ifdef DRV_TEXTURE
    drv_texture_avail = DRV_TEXTURE;
#else
    drv_texture_avail = FALSE;
#endif
#ifdef DRV_TEXTURE_DEFAULT
    drv_texture_enabled = DRV_TEXTURE_DEFAULT;
#else
    drv_texture_enabled = FALSE;
#endif
    drv_osd_enabled = DRV_OSD_DEFAULT;
    drv_bits_per_pixel = DRV_DEPTH;
    drv_colormap_bits = DRV_COLORMAPDEPTH;
    /* An unsigned long is 32 bits even when the DOS compiler has 16-bit ints. */
    drv_colors = (drv_bits_per_pixel == 32 ? 0xFFFFFFFFUL : 1UL << drv_bits_per_pixel);
    drv_colormap_colors = (drv_colormap_bits == 32 ? 0xFFFFFFFFUL : 1UL << drv_colormap_bits);
    drv_bytes_per_pixel = (drv_bits_per_pixel + 7U) / 8U;
    strcpy(drv_name, DRV_NAME);
    strcpy(drv_help, DRV_HELP);
    drv_rot_x = GLOBE_DEFAULT_ROTATION_X;
    drv_rot_y = GLOBE_DEFAULT_ROTATION_Y;
    drv_lmouse_pressed = FALSE;
    drv_rmouse_pressed = FALSE;
    drv_fps_enabled = DRV_FPS_DEFAULT;
    drv_fps_average = 0;
    drv_fps_history_num = 0;
    drv_fps_history_seconds = 0;
    drv_frame_seconds = 0;
    drv_auto_enabled = FALSE;
    
}

/* frame start */
void drv_fps_start(void)
{
#ifdef GDI
    drv_frame_start = GetTickCount();
#elif defined(SEG68)
    drv_frame_start = TickCount();
#else
#ifdef SEG86
    /* Microsoft C's DOS clock() reports elapsed time without POSIX headers. */
    drv_frame_start = clock();
#else /* FLAT */
    gettimeofday(&drv_frame_start, NULL);
#endif
#endif
}

/* frame stop */
void drv_fps_stop(void)
{
#ifdef GDI
    drv_frame_stop = GetTickCount();
    /* GetTickCount is a wrapping 32-bit counter, including on test hosts. */
    drv_frame_seconds = (float)((drv_frame_stop - drv_frame_start) &
                                0xFFFFFFFFUL) / 1000.0f;
#elif defined(SEG68)
    drv_frame_stop = TickCount();
    drv_frame_seconds = (float)((drv_frame_stop - drv_frame_start) &
                                0xFFFFFFFFUL) / 60.0f;
#else
#ifdef SEG86
    drv_frame_stop = clock();
    if (drv_frame_start == (clock_t)-1 || drv_frame_stop == (clock_t)-1)
    {
        drv_frame_seconds = 0;
    }
    else
    {
        /* Unsigned subtraction also handles a 32-bit DOS clock rollover. */
        drv_frame_seconds = (float)((unsigned long)drv_frame_stop -
                                    (unsigned long)drv_frame_start) /
                            (float)CLOCKS_PER_SEC;
    }
#else /* FLAT */
    gettimeofday(&drv_frame_stop, NULL);
    drv_frame_seconds = (float)(drv_frame_stop.tv_sec - drv_frame_start.tv_sec) +
                        (float)(drv_frame_stop.tv_usec - drv_frame_start.tv_usec) /
                        1000000.0f;
    if (drv_frame_seconds < 0)
        drv_frame_seconds = 0;
#endif
#endif

    if (!drv_fps_enabled)
    {
        drv_fps_average = 0;
        drv_fps_history_num = 0;
        drv_fps_history_seconds = 0;
        return;
    }
    if (drv_frame_seconds >= 1.0f)
    {
        drv_fps_average = 1.0f / drv_frame_seconds;
        drv_fps_history_num = 0;
        drv_fps_history_seconds = 0;
    } else
    {
        drv_fps_history_seconds += drv_frame_seconds;
        drv_fps_history_num++;

        if (drv_fps_history_num >= DRV_FPS_HISTORY)
        {
            /* A coarse DOS timer can return zero for an entire short sample. */
            if (drv_fps_history_seconds > 0)
                drv_fps_average = (float)drv_fps_history_num /
                                  drv_fps_history_seconds;
            drv_fps_history_num = 0;
            drv_fps_history_seconds = 0;
        }
    }
}

/* Apply only application actions; event loops retain native exit, redraw,
 * timer and geometry-cache work after a recognized key. */
int drv_handle_key(unsigned char key)
{
    switch (key)
    {
#ifdef EARTH_32
        case '1':
#endif
#ifdef EARTH_64
        case '2':
#endif
#ifdef EARTH_128
        case '3':
#endif
#ifdef EARTH_256
        case '4':
#endif
#ifdef EARTH_512
        case '5':
#endif
            globe_toggle_res((char)key);
            return TRUE;
        case 'a': case 'A':
            drv_auto_enabled = !drv_auto_enabled;
            return TRUE;
        case 'o': case 'O':
            drv_osd_enabled = !drv_osd_enabled;
            return TRUE;
        case 'f': case 'F':
            drv_fps_enabled = !drv_fps_enabled;
            drv_fps_average = 0;
            drv_fps_history_num = 0;
            drv_fps_history_seconds = 0;
            return TRUE;
#ifdef TEXTURE
        case 't': case 'T':
            globe_toggle_tex();
            return TRUE;
#endif
        default: return FALSE;
    }
}

static float drv_wrap_angle(double angle)
{
    double wrapped;
    float result;
    /* C89 comparison rejects NaN as well as either infinity. fmod bounds
     * work even when a floating-point subtraction of 360 would not change
     * the input value at all. Native emulation libraries supply fmod too. */
    if (!(angle >= -DBL_MAX && angle <= DBL_MAX)) return 0.0f;
    wrapped = fmod(angle, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    result = (float)wrapped;
    return result >= 360.0f ? 0.0f : result;
}

/* Drivers supply deltas in the application's downward-Y mouse convention. */
void drv_rotate_drag(long dx, long dy)
{
    if (drv_win_width == 0 || drv_win_height == 0) return;
    drv_rot_y = drv_wrap_angle((double)drv_rot_y + (double)dx * 100.0 / drv_win_width);
    drv_rot_x = drv_wrap_angle((double)drv_rot_x + (double)dy * 100.0 / drv_win_height);
}

/* Native event timers include their wait time in this interval. */
void drv_rotate_elapsed(double seconds)
{
    drv_rot_y = drv_wrap_angle((double)drv_rot_y +
                                (double)DRV_ROTATE * seconds);
}

/* rotate globe at a fixed speed */
void drv_globe_rotate(void)
{
    drv_rotate_elapsed((double)drv_frame_seconds);
}
