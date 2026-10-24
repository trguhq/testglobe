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
#include <stdlib.h>
#include <string.h>
#ifdef DRV_EARTH_RESOURCES
#include "earthres.h"
#endif
#ifdef GDI
#include <windows.h>
#elif defined(DRV_QD)
#include <Events.h>
#include <Memory.h>
#else
#ifdef DRV_MTX
#include <time.h>
#include <malloc.h>
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
int drv_texture_active;                 /* effective material for this pass */
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
static double drv_frame_start, drv_previous_present, drv_animation_tick;
static int drv_present_valid, drv_animation_valid;
static float drv_fps_history_seconds;
static long drv_fps_history_num;        /* frames in this FPS sample */
int drv_auto_enabled;                   /* automatically rotate */
int drv_auto_available;
int drv_frame_failed;
static char drv_message[64];

#ifdef GDI
/* Preserve original handles: normalized huge pointers may change selectors. */
#define DRV_ALLOCATION_SLOTS 8
static struct { HGLOBAL handle; void TG_HUGE *memory; }
    drv_allocations[DRV_ALLOCATION_SLOTS];
#endif

void TG_HUGE *drv_alloc_items(unsigned long count, unsigned int size)
{
#ifdef GDI
    int slot;
    HGLOBAL handle;
    void TG_HUGE *memory;
    if (!count || !size || count > 0xffffffffUL / size) return NULL;
    for (slot = 0; slot < DRV_ALLOCATION_SLOTS; ++slot)
    {
        if (drv_allocations[slot].handle != 0) continue;
        handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, count * size);
        if (handle == 0) return NULL;
        memory = (void TG_HUGE *)GlobalLock(handle);
        if (memory == NULL) { GlobalFree(handle); return NULL; }
        drv_allocations[slot].handle = handle;
        drv_allocations[slot].memory = memory;
        return memory;
    }
    return NULL;
#elif defined(DRV_QD)
    if (!count || !size || count > 0x7fffffffUL / size) return NULL;
    return (void *)NewPtr((Size)(count * size));
#elif defined(DRV_MTX)
    if (!count || !size || count > 0x7fffffffUL / size) return NULL;
    return _halloc((long)count, size);
#else
    if (!count || !size || count > (size_t)-1 / size) return NULL;
    return malloc((size_t)count * size);
#endif
}

void drv_free_items(void TG_HUGE *memory)
{
#ifdef GDI
    int slot;
    if (memory == NULL) return;
    for (slot = 0; slot < DRV_ALLOCATION_SLOTS; ++slot)
        if (drv_allocations[slot].handle != 0 &&
            drv_allocations[slot].memory == memory)
        {
            GlobalUnlock(drv_allocations[slot].handle);
            GlobalFree(drv_allocations[slot].handle);
            drv_allocations[slot].handle = 0;
            drv_allocations[slot].memory = NULL;
            return;
        }
#elif defined(DRV_QD)
    if (memory != NULL) DisposePtr((Ptr)memory);
#elif defined(DRV_MTX)
    if (memory != NULL) _hfree(memory);
#else
    free(memory);
#endif
}

const char *drv_result_text(Drv_result result)
{
    switch (result)
    {
        case DRV_OK: return "ready";
        case DRV_NO_MEMORY: return "insufficient memory";
        case DRV_BAD_DATA: return "invalid Earth data";
        case DRV_BAD_INPUT: return "invalid request";
        case DRV_UNAVAILABLE: return "unavailable";
        default: return "native rendering failure";
    }
}

void drv_append_status(char *buffer, unsigned int capacity, const char *message)
{
    unsigned int used;
    if (buffer == NULL || capacity == 0 || message == NULL) return;
    used = 0;
    while (used < capacity && buffer[used] != 0) ++used;
    if (used == capacity) { buffer[capacity - 1U] = 0; return; }
    while (used + 1U < capacity && *message != 0) buffer[used++] = *message++;
    buffer[used] = 0;
}

void drv_notice(const char *message)
{
    drv_message[0] = 0;
    if (message != NULL) drv_append_status(drv_message, sizeof(drv_message), message);
}

const char *drv_get_notice(void) { return drv_message; }

void drv_abort_frame(const char *message)
{
    drv_frame_failed = TRUE;
    drv_notice(message);
}

void drv_format_stats(char *buffer)
{
    char status[256];
    size_t length;
    unsigned long total;
    const char *primitive;
#ifdef DRV_VISIBLE_COUNTS
    unsigned long visible;
#endif
    if (buffer == NULL) return;
    if (!globe_ready())
    {
        strcpy(buffer, "No globe: press a resolution key to retry; Esc to exit");
        if (drv_message[0] != 0)
        {
            drv_append_status(buffer, 128U, "; ");
            drv_append_status(buffer, 128U, drv_message);
        }
        return;
    }
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
    if (drv_message[0] != 0)
    {
        drv_append_status(status, sizeof(status), "; ");
        drv_append_status(status, sizeof(status), drv_message);
    }
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
        drv_abort_frame("Strip batch must hold at least two facets");
        return;
    }
    first = 0;
    while (first < count - 2U && !drv_frame_failed)
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
    for (i = 0; i < count - 2U && !drv_frame_failed; ++i)
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
Drv_result drv_load_earth_data(int longitude, unsigned char TG_HUGE *destination)
{
    unsigned int resource;
    unsigned long remaining, length;
    Drv_result result;
    switch (longitude)
    {
        case 32: resource = TG_EARTH_32_ID; break;
        case 64: resource = TG_EARTH_64_ID; break;
        case 128: resource = TG_EARTH_128_ID; break;
        case 256: resource = TG_EARTH_256_ID; break;
        case 512: resource = TG_EARTH_512_ID; break;
        default: return DRV_BAD_INPUT;
    }
    if (destination == NULL)
    {
        return DRV_BAD_INPUT;
    }
    remaining = (unsigned long)longitude * ((unsigned long)longitude / 2UL);
    while (remaining != 0)
    {
        length = remaining < TG_EARTH_CHUNK_SIZE ? remaining : TG_EARTH_CHUNK_SIZE;
        result = drv_load_earth_chunk(resource, length, destination);
        if (result != DRV_OK) return result;
        destination += length;
        remaining -= length;
        ++resource;
    }
    return DRV_OK;
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
    drv_auto_enabled = FALSE;
    drv_auto_available = TRUE;
    drv_texture_active = FALSE;
    drv_frame_failed = FALSE;
    drv_notice(NULL);
    drv_reset_timing();
    
}

/* Native clocks are hidden behind one service. Wrapping 32-bit tick clocks
 * accumulate unsigned deltas; the POSIX fallback never moves backwards. */
double drv_time_now(void)
{
#if defined(GDI) || defined(DRV_QD) || defined(DRV_MTX)
    static unsigned long previous;
    static int initialized;
    static double elapsed;
    unsigned long tick;
    double frequency;
#ifdef GDI
    tick = GetTickCount(); frequency = 1000.0;
#elif defined(DRV_QD)
    tick = TickCount(); frequency = 60.0;
#else
    clock_t sample;
    sample = clock();
    if (sample == (clock_t)-1) return elapsed;
    tick = (unsigned long)sample; frequency = (double)CLOCKS_PER_SEC;
#endif
    tick &= 0xffffffffUL;
    if (initialized)
        elapsed += (double)((tick - previous) & 0xffffffffUL) / frequency;
    else initialized = TRUE;
    previous = tick;
    return elapsed;
#else
    struct timeval sample;
    static double previous;
    double current;
    if (gettimeofday(&sample, NULL) != 0) return previous;
    current = (double)sample.tv_sec + (double)sample.tv_usec / 1000000.0;
    if (current < previous) return previous;
    previous = current;
    return current;
#endif
}

void drv_reset_timing(void)
{
    drv_present_valid = FALSE;
    drv_animation_valid = FALSE;
    drv_fps_average = 0;
    drv_fps_history_seconds = 0;
    drv_fps_history_num = 0;
}

void drv_fps_start(void)
{
    drv_frame_failed = FALSE;
    drv_frame_start = drv_time_now();
    if (!drv_animation_valid)
    {
        drv_animation_tick = drv_frame_start;
        drv_animation_valid = TRUE;
    }
}

/* Count completed presentations over wall time, including event-loop waits.
 * This is observed presentation throughput, not a GPU-completion benchmark.
 * Each driver calls this after its native presentation operation returns. */
void drv_fps_stop(void)
{
    double now, seconds;
    now = drv_time_now();
    if (drv_frame_failed || !globe_ready())
    {
        drv_reset_timing();
        return;
    }
    seconds = now - (drv_present_valid ? drv_previous_present : drv_frame_start);
    drv_previous_present = now;
    drv_present_valid = TRUE;
    if (!drv_fps_enabled)
    {
        drv_fps_average = 0;
        drv_fps_history_num = 0;
        drv_fps_history_seconds = 0;
        return;
    }
    if (seconds < 0.0) seconds = 0.0;
    if (seconds >= 1.0)
    {
        drv_fps_average = (float)(1.0 / seconds);
        drv_fps_history_num = 0;
        drv_fps_history_seconds = 0;
        return;
    }
    drv_fps_history_seconds += (float)seconds;
    ++drv_fps_history_num;
    if (drv_fps_history_num >= DRV_FPS_HISTORY)
    {
        if (drv_fps_history_seconds > 0)
            drv_fps_average = (float)drv_fps_history_num / drv_fps_history_seconds;
        drv_fps_history_num = 0;
        drv_fps_history_seconds = 0;
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
            if (!drv_auto_available)
            {
                drv_notice("Automatic rotation unavailable; use mouse");
                return TRUE;
            }
            drv_auto_enabled = !drv_auto_enabled;
            drv_reset_timing();
            return TRUE;
        case 'o': case 'O':
            drv_osd_enabled = !drv_osd_enabled;
            return TRUE;
        case 'f': case 'F':
            drv_fps_enabled = !drv_fps_enabled;
            drv_reset_timing();
            return TRUE;
#ifdef TEXTURE
        case 't': case 'T':
            globe_toggle_tex();
            drv_reset_timing();
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
    double now, seconds;
    now = drv_time_now();
    if (!drv_animation_valid)
    {
        drv_animation_tick = now;
        drv_animation_valid = TRUE;
        return;
    }
    seconds = now - drv_animation_tick;
    drv_animation_tick = now;
    if (seconds < 0.0) seconds = 0.0;
    drv_rotate_elapsed(seconds);
}
