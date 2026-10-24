/*
 * testglobe - native 16-bit Windows 3.1 GDI driver.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GDI only fills 2D polygons. This driver supplies object/view transforms,
 * perspective, near/far and window clipping, and back-face removal. The
 * application draws a convex globe: retained front facets do not overlap,
 * so grouping them by palette entry needs neither a z-buffer nor sorting.
 * This is not a general renderer for intersecting or transparent objects.
 */
#include "testglob.h"
#ifdef DRV_GDI
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "drv.h"
#include "drv_ext.h"
#include "globe.h"
#include "glob_ext.h"
#include "earthres.h"
#include "soft3d.h"

/* Indexed quad packets share their boundary vertices. Each packet and its
 * PolyPolygon arrays fit within one Win16 segment at every Earth resolution. */
#define GDI_MAX_VERTICES SOFT3D_MAX_VERTICES
#define GDI_MAX_FACETS DRV_QUAD_BATCH_MAX
#define GDI_MAX_POLY SOFT3D_MAX_POLY
#define GDI_TIMER 1
#if 2 * (DRV_QUAD_BATCH_MAX + 1) > GDI_MAX_VERTICES
#error GDI quad batches exceed the transformed vertex cache
#endif

typedef struct Gdi_float_point { float x, y, z; } Gdi_float_point;

static HINSTANCE gdi_instance;
static int gdi_show;
static HWND gdi_window;
static FARPROC gdi_callback;
static int gdi_registered;
static char gdi_class_name[40];
static HPALETTE gdi_palette;
static HBRUSH gdi_brushes[256];
static HDC gdi_buffer;
static HBITMAP gdi_bitmap;
static HBITMAP gdi_previous_bitmap;
static HPALETTE gdi_previous_palette;
/* A paint DC is borrowed only for one render; gdi_buffer remains owned. */
static HDC gdi_target;
static HBRUSH gdi_selected_brush;
static int gdi_rendering;
static int gdi_release_pending;
static int gdi_batch_enabled = TRUE;
static int gdi_restart_frame;
static int gdi_frame_failed;
static int gdi_draw_paused;
static int gdi_brush_reduced;
static unsigned char gdi_background;
static unsigned char gdi_foreground;
static int gdi_palette_foreground;
static int gdi_has_fpu;
static int gdi_ready;
static int gdi_failed;
static int gdi_timer_active;
static int gdi_dirty = TRUE;
static int gdi_last_x;
static int gdi_last_y;
static DWORD gdi_auto_tick;
static unsigned long gdi_scene_revision;
static float gdi_float_matrix[3][3];
static long gdi_fixed_matrix[3][3];
static Gdi_float_point gdi_float_points[GDI_MAX_VERTICES];
static Soft3d_fixed_point gdi_fixed_points[GDI_MAX_VERTICES];
static POINT gdi_polygons[GDI_MAX_FACETS][GDI_MAX_POLY];
static int gdi_polygon_counts[GDI_MAX_FACETS];
static int gdi_color_head[256];
static int gdi_color_next[GDI_MAX_FACETS];
static POINT gdi_batch_points[GDI_MAX_FACETS * GDI_MAX_POLY];
static int gdi_batch_counts[GDI_MAX_FACETS];
static void gdi_present(HDC dc);

static HDC gdi_draw_dc(void)
{
    return gdi_target != 0 ? gdi_target : gdi_buffer;
}

static void gdi_release_brushes(void)
{
    unsigned int i;
    for (i = 0; i < 256U; ++i)
    {
        if (gdi_brushes[i] == 0 || gdi_brushes[i] == gdi_selected_brush)
            continue;
        DeleteObject(gdi_brushes[i]);
        gdi_brushes[i] = 0;
    }
}

extern int testglobe_main(int argc, char **argv);
LONG FAR PASCAL gdi_window_proc(HWND window, UINT message,
                                 WPARAM wparam, LPARAM lparam);

static void gdi_delete_buffer(void)
{
    if (gdi_buffer != 0)
    {
        if (gdi_previous_bitmap != 0)
            SelectObject(gdi_buffer, gdi_previous_bitmap);
        if (gdi_previous_palette != 0)
            SelectPalette(gdi_buffer, gdi_previous_palette, TRUE);
        DeleteDC(gdi_buffer);
    }
    if (gdi_bitmap != 0) DeleteObject(gdi_bitmap);
    gdi_buffer = 0;
    gdi_bitmap = 0;
    gdi_previous_bitmap = 0;
    gdi_previous_palette = 0;
}

void drv_release_optional(void)
{
    if (gdi_rendering)
    {
        gdi_release_pending = TRUE;
        return;
    }
    gdi_delete_buffer();
    gdi_release_brushes();
    gdi_dirty = TRUE;
    drv_notice("direct drawing");
}

/* A modal dialog can dispatch paint and timer messages. Quiesce the driver
 * before reporting an error or destroying any of its native resources. */
static void gdi_quiesce(void)
{
    gdi_ready = FALSE;
    gdi_dirty = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
    ReleaseCapture();
    if (gdi_timer_active && gdi_window != 0)
        KillTimer(gdi_window, GDI_TIMER);
    gdi_timer_active = FALSE;
}

static void gdi_cleanup(void)
{
    unsigned int i;
    gdi_quiesce();
    if (gdi_window != 0) DestroyWindow(gdi_window);
    gdi_delete_buffer();
    for (i = 0; i < 256; ++i)
    {
        if (gdi_brushes[i] != 0) DeleteObject(gdi_brushes[i]);
        gdi_brushes[i] = 0;
    }
    if (gdi_palette != 0) DeleteObject(gdi_palette);
    gdi_palette = 0;
    if (gdi_registered)
    {
        UnregisterClass(gdi_class_name, gdi_instance);
        gdi_registered = FALSE;
    }
    if (gdi_callback != NULL) FreeProcInstance(gdi_callback);
    gdi_callback = NULL;
    gdi_ready = FALSE;
}

void gdi_error(char *message)
{
    if (gdi_failed) exit(1);
    gdi_failed = TRUE;
    drv_auto_enabled = FALSE;
    gdi_quiesce();
    MessageBox(gdi_window, message, "testglobe - Windows GDI error",
               MB_OK | MB_ICONEXCLAMATION);
    exit(1);
}

void drv_close(void)
{
    gdi_quiesce();
    if (gdi_window != 0)
        DestroyWindow(gdi_window);
    else
        PostQuitMessage(0);
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show)
{
    int result;
    char *arguments[2];
    (void)command_line;
    /* Microsoft C's huge Windows model has multiple data segments; Win16
     * cannot safely create a second instance of that executable. */
    if (previous != 0)
    {
        MessageBox(0, "testglobe is already running", "Windows GDI",
                   MB_OK | MB_ICONEXCLAMATION);
        return 1;
    }
    gdi_instance = instance;
    gdi_show = show;
    arguments[0] = "testglobe";
    arguments[1] = NULL;
    result = testglobe_main(1, arguments);
    gdi_cleanup();
    return result;
}

void drv_init(int *argc, char **argv)
{
    DWORD flags;
    (void)argc;
    (void)argv;
    flags = GetWinFlags();
    gdi_has_fpu = (flags & WF_80x87) != 0 &&
                  (flags & (WF_CPU386 | WF_CPU486)) != 0;
    if (atexit(gdi_cleanup) != 0)
        gdi_error("Cannot register Windows GDI cleanup");
}

Drv_result drv_load_earth_chunk(unsigned int id, unsigned long length,
                           unsigned char TG_HUGE *destination)
{
    HRSRC resource;
    HGLOBAL loaded;
    const unsigned char FAR *source;
    unsigned int i;
    if (destination == NULL || length == 0 || length > TG_EARTH_CHUNK_SIZE)
        return DRV_BAD_INPUT;
    resource = FindResource(gdi_instance, MAKEINTRESOURCE(id), RT_RCDATA);
    if (resource == 0) return DRV_BAD_DATA;
    if (SizeofResource(gdi_instance, resource) != length)
        return DRV_BAD_DATA;
    loaded = LoadResource(gdi_instance, resource);
    if (loaded == 0) return DRV_NO_MEMORY;
    source = (const unsigned char FAR *)LockResource(loaded);
    if (source == NULL)
    {
        FreeResource(loaded);
        return DRV_NO_MEMORY;
    }
    /* Only the huge destination crosses selectors; each far source fits
     * within one resource segment. The shared loader advances chunks. */
    for (i = 0; (unsigned long)i < length; ++i)
        *destination++ = source[i];
    UnlockResource(loaded);
    FreeResource(loaded);
    return DRV_OK;
}

static HBRUSH gdi_brush(unsigned int color)
{
    unsigned long luminance;
    if (!gdi_brush_reduced && gdi_brushes[color] == 0)
    {
        gdi_brushes[color] = CreateSolidBrush(PALETTEINDEX(color));
        if (gdi_brushes[color] == 0)
        {
            gdi_release_brushes();
            gdi_brushes[color] = CreateSolidBrush(PALETTEINDEX(color));
            if (gdi_brushes[color] == 0)
            {
                gdi_brush_reduced = TRUE;
                drv_notice("reduced colors");
            }
        }
    }
    if (gdi_brush_reduced)
    {
        luminance = (unsigned long)earth_cmap[color][0] * 77UL +
                    (unsigned long)earth_cmap[color][1] * 150UL +
                    (unsigned long)earth_cmap[color][2] * 29UL;
        /* Stock brushes are borrowed and never enter the owned cache. */
        return (HBRUSH)GetStockObject(luminance < 32768UL ? BLACK_BRUSH : WHITE_BRUSH);
    }
    return gdi_brushes[color];
}

static UINT gdi_realize_palette(int foreground)
{
    HDC dc;
    HPALETTE previous;
    UINT changed;
    if (gdi_window == 0 || gdi_palette == 0) return 0;
    dc = GetDC(gdi_window);
    if (dc == 0) return 0;
    previous = SelectPalette(dc, gdi_palette, !foreground);
    changed = RealizePalette(dc);
    SelectPalette(dc, previous, TRUE);
    ReleaseDC(gdi_window, dc);
    return changed;
}

static void gdi_make_buffer(void)
{
    HDC dc;
    gdi_delete_buffer();
    if (gdi_window == 0 || drv_win_width == 0 || drv_win_height == 0) return;
    dc = GetDC(gdi_window);
    if (dc == 0)
    {
        drv_notice("direct drawing");
        return;
    }
    gdi_buffer = CreateCompatibleDC(dc);
    gdi_bitmap = CreateCompatibleBitmap(dc, (int)drv_win_width,
                                        (int)drv_win_height);
    ReleaseDC(gdi_window, dc);
    if (gdi_buffer == 0 || gdi_bitmap == 0)
    {
        gdi_delete_buffer();
        drv_notice("direct drawing");
        return;
    }
    gdi_previous_bitmap = (HBITMAP)SelectObject(gdi_buffer, gdi_bitmap);
    if (gdi_previous_bitmap == 0)
    {
        gdi_delete_buffer();
        drv_notice("direct drawing");
        return;
    }
    gdi_previous_palette = SelectPalette(gdi_buffer, gdi_palette, TRUE);
    /* Realize only on a window DC. Windows 3.1 has a documented foreground
     * palette bug when RealizePalette is applied to a compatible memory DC. */
    gdi_dirty = TRUE;
}

static void gdi_resize(int width, int height)
{
    if (gdi_failed) return;
    if (width <= 0 || height <= 0) return;
    if ((unsigned long)width > 32767UL || (unsigned long)height > 32767UL)
        gdi_error("Windows GDI client dimensions exceed signed 16-bit coordinates");
    drv_win_width = (unsigned int)width;
    drv_win_height = (unsigned int)height;
    gdi_auto_tick = GetTickCount();
    drv_reset_timing();
    if (gdi_palette != 0) gdi_make_buffer();
    gdi_dirty = TRUE;
}

int drv_init_window(int in_x, int in_y, int in_width, int in_height)
{
    struct Gdi_palette_data { WORD version, count; PALETTEENTRY entries[256]; } data;
    WNDCLASS window_class;
    RECT bounds;
    HDC dc;
    unsigned int i;
    unsigned int brightness, darkest, lightest;
    int screen_width, screen_height;
    long outer_width, outer_height;
    if (gdi_failed) return 0;
    gdi_batch_enabled = TRUE;
    gdi_restart_frame = gdi_frame_failed = gdi_draw_paused = FALSE;
    gdi_brush_reduced = FALSE;
    if (in_x > 0) drv_win_x = (unsigned int)in_x;
    if (in_y > 0) drv_win_y = (unsigned int)in_y;
    dc = GetDC(0);
    if (dc == 0) gdi_error("Cannot access Windows display");
    screen_width = GetDeviceCaps(dc, HORZRES);
    screen_height = GetDeviceCaps(dc, VERTRES);
    if (screen_width > 0 && screen_width <= 640 &&
        screen_height > 0 && screen_height <= 480)
    {
        if (in_width <= 0) drv_win_width = 320;
        if (in_height <= 0) drv_win_height = 240;
    }
    if (in_width > 0) drv_win_width = (unsigned int)in_width;
    if (in_height > 0) drv_win_height = (unsigned int)in_height;
    i = (unsigned int)(GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES));
    if (i != 8 || (GetDeviceCaps(dc, RASTERCAPS) & RC_PALETTE) == 0 ||
        GetDeviceCaps(dc, SIZEPALETTE) < 256)
    {
        ReleaseDC(0, dc);
        gdi_error("Windows GDI requires an 8-bit, 256-color display driver");
    }
    ReleaseDC(0, dc);
    darkest = 766;
    lightest = 0;
    data.version = 0x0300;
    data.count = 256;
    for (i = 0; i < 256; ++i)
    {
        data.entries[i].peRed = earth_cmap[i][0];
        data.entries[i].peGreen = earth_cmap[i][1];
        data.entries[i].peBlue = earth_cmap[i][2];
        data.entries[i].peFlags = PC_NOCOLLAPSE;
        brightness = earth_cmap[i][0] + earth_cmap[i][1] + earth_cmap[i][2];
        if (brightness < darkest) { darkest = brightness; gdi_background = (unsigned char)i; }
        if (brightness > lightest) { lightest = brightness; gdi_foreground = (unsigned char)i; }
    }
    gdi_palette = CreatePalette((LOGPALETTE FAR *)&data);
    if (gdi_palette == 0) gdi_error("Cannot create Earth palette");
    memset(&window_class, 0, sizeof(window_class));
    gdi_callback = MakeProcInstance((FARPROC)gdi_window_proc, gdi_instance);
    if (gdi_callback == NULL) gdi_error("Cannot create Windows callback thunk");
    /* A distinct class name keeps the callback thunk's instance explicit. */
    sprintf(gdi_class_name, "testglobeGDI%u", (unsigned int)gdi_instance);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = (WNDPROC)gdi_callback;
    window_class.hInstance = gdi_instance;
    window_class.hIcon = LoadIcon(0, IDI_APPLICATION);
    window_class.hCursor = LoadCursor(0, IDC_ARROW);
    window_class.lpszClassName = gdi_class_name;
    if (!RegisterClass(&window_class)) gdi_error("Cannot register GDI window class");
    gdi_registered = TRUE;
    bounds.left = bounds.top = 0;
    bounds.right = bounds.bottom = 0;
    /* Query border offsets around a zero-sized client rectangle first.
     * Adjusting a near-32767 client RECT directly can overflow Win16's
     * signed fields before the resulting outer dimensions are checked. */
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    outer_width = (long)drv_win_width + (long)bounds.right - (long)bounds.left;
    outer_height = (long)drv_win_height + (long)bounds.bottom - (long)bounds.top;
    if (drv_win_width == 0 || drv_win_height == 0 ||
        drv_win_x > 32767U || drv_win_y > 32767U ||
        outer_width <= 0 || outer_height <= 0 ||
        outer_width > 32767L || outer_height > 32767L)
        gdi_error("Windows GDI window dimensions exceed signed 16-bit coordinates");
    gdi_window = CreateWindow(gdi_class_name, TESTGLOBE_NAME,
                              WS_OVERLAPPEDWINDOW, (int)drv_win_x, (int)drv_win_y,
                              (int)outer_width, (int)outer_height,
                              0, 0, gdi_instance, NULL);
    if (gdi_window == 0) gdi_error("Cannot create Windows GDI window");
    gdi_realize_palette(gdi_palette_foreground);
    gdi_make_buffer();
    gdi_timer_active = SetTimer(gdi_window, GDI_TIMER, 30, NULL) != 0;
    drv_auto_available = gdi_timer_active;
    if (!gdi_timer_active)
    {
        drv_auto_enabled = FALSE;
        drv_notice("animation unavailable");
    }
    ShowWindow(gdi_window, gdi_show);
    UpdateWindow(gdi_window);
    return 1;
}

static void gdi_set_transform(void)
{
    float fsx, fcx, fsy, fcy;
    /* R = Rx(user x) Ry(user y) Rx(-90) Rz(-90), followed by z=-3000. */
    if (gdi_has_fpu)
    {
        fsx = (float)sin((double)drv_rot_x * PI / 180.0);
        fcx = (float)cos((double)drv_rot_x * PI / 180.0);
        fsy = (float)sin((double)drv_rot_y * PI / 180.0);
        fcy = (float)cos((double)drv_rot_y * PI / 180.0);
        gdi_float_matrix[0][0] = fsy;
        gdi_float_matrix[0][1] = fcy;
        gdi_float_matrix[0][2] = 0;
        gdi_float_matrix[1][0] = -fsx * fcy;
        gdi_float_matrix[1][1] = fsx * fsy;
        gdi_float_matrix[1][2] = fcx;
        gdi_float_matrix[2][0] = fcx * fcy;
        gdi_float_matrix[2][1] = -fcx * fsy;
        gdi_float_matrix[2][2] = fsx;
    }
    else if (!soft3d_fixed_prepare(gdi_fixed_matrix, &drv_rot_x, &drv_rot_y))
    {
        gdi_frame_failed = TRUE;
        drv_abort_frame("coordinate is out of range");
    }
}

static int gdi_clip_float_z(const Gdi_float_point *in, int count,
                            Gdi_float_point *out, float plane, int greater)
{
    int i, total, a_inside, b_inside;
    float fraction;
    Gdi_float_point a, b, point;
    total = 0;
    a = in[count - 1];
    a_inside = greater ? a.z >= plane : a.z <= plane;
    for (i = 0; i < count; ++i)
    {
        b = in[i];
        b_inside = greater ? b.z >= plane : b.z <= plane;
        if (a_inside != b_inside)
        {
            fraction = (plane - a.z) / (b.z - a.z);
            point.x = a.x + (b.x - a.x) * fraction;
            point.y = a.y + (b.y - a.y) * fraction;
            point.z = plane;
            if (total >= GDI_MAX_POLY) return 0;
            out[total++] = point;
        }
        if (b_inside)
        {
            if (total >= GDI_MAX_POLY) return 0;
            out[total++] = b;
        }
        a = b;
        a_inside = b_inside;
    }
    return total;
}

static int gdi_prepare_polygon(unsigned int point_count,
                               const unsigned int *indices,
                               unsigned int index_count, POINT *destination)
{
    Gdi_float_point float_a[GDI_MAX_POLY], float_b[GDI_MAX_POLY];
    Soft3d_screen_point screen[GDI_MAX_POLY];
    float focal, projected_x, projected_y;
    int i, count;
    if (indices == NULL || destination == NULL ||
        point_count == 0U || point_count > GDI_MAX_VERTICES ||
        (index_count != 3U && index_count != 4U)) return 0;
    for (i = 0; i < (int)index_count; ++i)
        if (indices[i] >= point_count) return 0;
    if (gdi_has_fpu)
    {
        for (i = 0; i < (int)index_count; ++i)
            float_a[i] = gdi_float_points[indices[i]];
        count = gdi_clip_float_z(float_a, (int)index_count, float_b,
                                 -(float)GLOBE_NEAR_DISTANCE, FALSE);
        if (count < 3) return 0;
        count = gdi_clip_float_z(float_b, count, float_a,
                                 -(float)GLOBE_FAR_DISTANCE, TRUE);
        if (count < 3) return 0;
        focal = (float)drv_win_height * (float)GLOBE_FOCAL_SCALE;
        for (i = 0; i < count; ++i)
        {
            projected_x = (float)drv_win_width / 2.0f +
                           float_a[i].x * focal / -float_a[i].z;
            projected_y = (float)drv_win_height / 2.0f -
                           float_a[i].y * focal / -float_a[i].z;
            /* Bound conversion and the shared integer clipper's deltas,
             * including nonfinite results from invalid external vertices. */
            if (!(projected_x >= -134217727.0 && projected_x <= 134217727.0 &&
                  projected_y >= -134217727.0 && projected_y <= 134217727.0))
                return 0;
            screen[i].x = (long)projected_x;
            screen[i].y = (long)projected_y;
        }
    }
    else
        count = soft3d_fixed_project_polygon(gdi_fixed_points, point_count,
                                             indices, index_count,
                                             drv_win_width, drv_win_height, screen);
    count = soft3d_finish_polygon(screen, count, drv_win_width, drv_win_height);
    for (i = 0; i < count; ++i)
    {
        destination[i].x = (int)screen[i].x;
        destination[i].y = (int)screen[i].y;
    }
    return count;
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *first, Vertex TG_HUGE *second,
                               Vertex TG_HUGE *third, Vertex TG_HUGE *fourth,
                               unsigned char color)
{
    drv_submit_quad_as_batch(first, second, third, fourth, color);
}

void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                   const Vertex TG_HUGE *vertices,
                                   unsigned int quad_count,
                                   const unsigned int TG_HUGE *indices,
                                   const unsigned char TG_HUGE *colors)
{
    unsigned int i, color, corner, quad_indices[4];
    int row, facet, total, points, j;
    float source[3];
    HBRUSH previous, brush;
    HDC dc;
    dc = gdi_draw_dc();
    if (gdi_failed || dc == 0 || gdi_restart_frame || gdi_frame_failed ||
        vertex_count == 0 || quad_count == 0 ||
        vertices == NULL || indices == NULL || colors == NULL) return;
    if (!drv_validate_quadbatch(vertex_count, quad_count, indices))
    {
        gdi_frame_failed = TRUE;
        drv_abort_frame("quad batch or index is out of range");
        return;
    }
    if (gdi_has_fpu)
    {
        for (i = 0; i < vertex_count; ++i)
        {
            source[0] = vertices[i].x;
            source[1] = vertices[i].y;
            source[2] = vertices[i].z;
            for (row = 0; row < 3; ++row)
            {
                float transformed = gdi_float_matrix[row][0] * source[0] +
                                    gdi_float_matrix[row][1] * source[1] +
                                    gdi_float_matrix[row][2] * source[2];
                if (row == 0) gdi_float_points[i].x = transformed;
                if (row == 1) gdi_float_points[i].y = transformed;
                if (row == 2) gdi_float_points[i].z = transformed - (float)GLOBE_VIEW_DISTANCE;
            }
        }
    }
    else if (!soft3d_fixed_transform(gdi_fixed_matrix, vertex_count, vertices,
                                      gdi_fixed_points))
    {
        gdi_frame_failed = TRUE;
        drv_abort_frame("fixed-point coordinate is out of range");
        return;
    }
    for (color = 0; color < 256; ++color) gdi_color_head[color] = -1;
    for (i = 0; i < quad_count; ++i)
    {
        for (corner = 0; corner < 4U; ++corner)
            quad_indices[corner] = indices[4U * i + corner];
        gdi_polygon_counts[i] = gdi_prepare_polygon(vertex_count, quad_indices, 4U,
                                                   gdi_polygons[i]);
        if (gdi_polygon_counts[i] == 0) continue;
        color = colors[i];
        gdi_color_next[i] = gdi_color_head[color];
        gdi_color_head[color] = (int)i;
        ++globe_quads_num_vis;
    }
    for (color = 0; color < 256; ++color)
    {
        total = points = 0;
        for (facet = gdi_color_head[color]; facet >= 0; facet = gdi_color_next[facet])
        {
            gdi_batch_counts[total++] = gdi_polygon_counts[facet];
            for (j = 0; j < gdi_polygon_counts[facet]; ++j)
                gdi_batch_points[points++] = gdi_polygons[facet][j];
        }
        if (total == 0) continue;
        brush = gdi_brush(color);
        previous = (HBRUSH)SelectObject(dc, brush);
        if (previous == 0)
        {
            gdi_frame_failed = TRUE;
            drv_abort_frame("cannot select drawing brush");
            return;
        }
        gdi_selected_brush = brush;
        if (gdi_batch_enabled)
        {
            if (!PolyPolygon(dc, gdi_batch_points, gdi_batch_counts, total))
            {
                gdi_batch_enabled = FALSE;
                gdi_restart_frame = TRUE;
                drv_abort_frame("individual polygons");
            }
        }
        else
        {
            points = 0;
            for (j = 0; j < total; ++j)
            {
                /* The public quad leaf delegates back to this batch entry.
                 * Retry only the native call, without recursive submission. */
                if (!Polygon(dc, gdi_batch_points + points, gdi_batch_counts[j]))
                {
                    gdi_frame_failed = TRUE;
                    drv_abort_frame("individual polygon failed");
                    break;
                }
                points += gdi_batch_counts[j];
            }
        }
        SelectObject(dc, previous);
        gdi_selected_brush = 0;
        if (gdi_restart_frame || gdi_frame_failed) return;
    }
}

void drv_draw_osd(void)
{
    char status[256];
    RECT status_bounds, help_bounds;
    int help_height;
    UINT flags;
    HDC dc;
    dc = gdi_draw_dc();
    if (gdi_failed || dc == 0 || drv_win_width <= 16 || drv_win_height <= 16) return;
    drv_format_stats(status);
    drv_append_status(status, sizeof(status),
                       gdi_has_fpu ? ", 80387 math" : ", fixed-point math");
    if (gdi_buffer == 0)
        drv_append_status(status, sizeof(status), ", direct drawing");
    if (!gdi_batch_enabled)
        drv_append_status(status, sizeof(status), ", individual polygons");
    if (gdi_brush_reduced)
        drv_append_status(status, sizeof(status), ", reduced colors");
    if (!drv_auto_available)
        drv_append_status(status, sizeof(status), ", manual rotation");
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, PALETTEINDEX(gdi_foreground));
    flags = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX;
    help_bounds.left = 8;
    help_bounds.right = (int)drv_win_width - 8;
    help_bounds.top = help_bounds.bottom = 0;
    DrawText(dc, drv_help, -1, &help_bounds, flags | DT_CALCRECT);
    help_height = help_bounds.bottom - help_bounds.top;
    help_bounds.right = (int)drv_win_width - 8;
    help_bounds.bottom = (int)drv_win_height - 8;
    help_bounds.top = help_bounds.bottom - help_height;
    if (help_bounds.top < 8) help_bounds.top = 8;
    status_bounds.left = 8;
    status_bounds.right = (int)drv_win_width - 8;
    status_bounds.top = 8;
    status_bounds.bottom = help_bounds.top - 4;
    if (status_bounds.bottom > status_bounds.top)
        DrawText(dc, status, -1, &status_bounds, flags);
    if (help_bounds.bottom > help_bounds.top)
        DrawText(dc, drv_help, -1, &help_bounds, flags);
}

static void gdi_render_to(HDC dc)
{
    RECT bounds;
    HPEN previous;
    HPALETTE previous_palette;
    int pass;
    if (gdi_failed || !gdi_ready || gdi_rendering || dc == 0) return;
    gdi_rendering = TRUE;
    gdi_target = dc;
    previous_palette = SelectPalette(dc, gdi_palette, !gdi_palette_foreground);
    if (dc != gdi_buffer) RealizePalette(dc);
    drv_fps_start();
    bounds.left = bounds.top = 0;
    bounds.right = (int)drv_win_width;
    bounds.bottom = (int)drv_win_height;
    gdi_frame_failed = FALSE;
    for (pass = 0; pass < 2; ++pass)
    {
        drv_frame_failed = FALSE;
        gdi_restart_frame = FALSE;
        FillRect(dc, &bounds, gdi_brush(gdi_background));
        previous = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        SetPolyFillMode(dc, WINDING);
        globe_quads_num_vis = 0;
        if (globe_ready() && !gdi_draw_paused)
        {
            gdi_set_transform();
            if (!gdi_frame_failed) globe_draw_quadbatches();
        }
        SelectObject(dc, previous);
        if (drv_frame_failed && !gdi_restart_frame) gdi_frame_failed = TRUE;
        if (!gdi_restart_frame) break;
    }
    if (gdi_frame_failed)
    {
        /* Keep a usable native window and the valid mesh; retry only after
         * another input action, never from a partially submitted primitive. */
        gdi_draw_paused = TRUE;
        drv_auto_enabled = FALSE;
        globe_quads_num_vis = 0;
        drv_notice("drawing unavailable; retry a resolution");
        FillRect(dc, &bounds, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    if (gdi_draw_paused)
        drv_abort_frame("drawing unavailable; retry a resolution");
    if (drv_osd_enabled || !globe_ready() || gdi_draw_paused) drv_draw_osd();
    if (previous_palette != 0) SelectPalette(dc, previous_palette, TRUE);
    gdi_target = 0;
    gdi_rendering = FALSE;
    gdi_dirty = FALSE;
    if (gdi_release_pending)
    {
        gdi_release_pending = FALSE;
        drv_release_optional();
    }
}

void drv_render(void)
{
    HDC dc;
    if (gdi_window != 0 && !gdi_rendering)
    {
        dc = GetDC(gdi_window);
        if (dc != 0)
        {
            if (gdi_buffer != 0)
            {
                gdi_render_to(gdi_buffer);
                if (gdi_buffer != 0) gdi_present(dc);
                else gdi_render_to(dc);
            }
            else gdi_render_to(dc);
            drv_fps_stop();
            ReleaseDC(gdi_window, dc);
        }
    }
}

static void gdi_present(HDC dc)
{
    HPALETTE previous;
    int copied;
    if (gdi_failed || gdi_buffer == 0) return;
    previous = SelectPalette(dc, gdi_palette, !gdi_palette_foreground);
    RealizePalette(dc);
    copied = BitBlt(dc, 0, 0, (int)drv_win_width, (int)drv_win_height,
                    gdi_buffer, 0, 0, SRCCOPY);
    SelectPalette(dc, previous, TRUE);
    if (!copied)
    {
        drv_release_optional();
        gdi_render_to(dc);
    }
}

static void gdi_keyboard(unsigned char key)
{
    if (gdi_failed) return;
    if (key == 27) { drv_close(); return; }
    if (!drv_handle_key(key)) return;
    if (gdi_scene_revision != globe_revision)
    {
        gdi_scene_revision = globe_revision;
        gdi_auto_tick = GetTickCount();
    }
    gdi_draw_paused = FALSE;
    if (key == 'a' || key == 'A') gdi_auto_tick = GetTickCount();
    gdi_dirty = TRUE;
    InvalidateRect(gdi_window, NULL, FALSE);
}

static void gdi_mouse_move(int x, int y)
{
    if (drv_lmouse_pressed)
    {
        drv_rotate_drag((long)x - gdi_last_x, (long)y - gdi_last_y);
        gdi_dirty = TRUE;
        InvalidateRect(gdi_window, NULL, FALSE);
    }
    gdi_last_x = x;
    gdi_last_y = y;
}

static void gdi_mouse_metadata_changed(void)
{
#if DEBUG
    if (gdi_ready && !gdi_failed && gdi_window != 0)
    {
        gdi_dirty = TRUE;
        InvalidateRect(gdi_window, NULL, FALSE);
    }
#endif
}

LONG FAR PASCAL gdi_window_proc(HWND window, UINT message,
                                 WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT paint;
    HDC dc;
    DWORD tick, elapsed;
    if (gdi_rendering && !gdi_failed)
    {
        /* Native callbacks can dispatch messages while a packet borrows the
         * mesh or a paint DC. Requeue mutations until those borrows end. */
        switch (message)
        {
            case WM_CHAR: case WM_SIZE: case WM_CLOSE: case WM_TIMER:
            case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP:
                if (!PostMessage(window, message, wparam, lparam))
                    drv_notice("window busy; retry the control");
                return 0;
            default: break;
        }
    }
    if (gdi_rendering && !gdi_failed && message == WM_PAINT)
    {
        dc = BeginPaint(window, &paint);
        (void)dc;
        EndPaint(window, &paint);
        return 0;
    }
    if (gdi_failed && message != WM_DESTROY)
    {
        if (message == WM_PAINT)
        {
            dc = BeginPaint(window, &paint);
            (void)dc;
            EndPaint(window, &paint);
        }
        return 0;
    }
    switch (message)
    {
        case WM_PAINT:
            dc = BeginPaint(window, &paint);
            if (gdi_ready)
            {
                if (gdi_buffer != 0)
                {
                    if (gdi_dirty) gdi_render_to(gdi_buffer);
                    if (gdi_buffer != 0) gdi_present(dc);
                    else gdi_render_to(dc);
                }
                else gdi_render_to(dc);
                drv_fps_stop();
            }
            EndPaint(window, &paint);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
                gdi_resize((int)(unsigned short)LOWORD(lparam),
                            (int)(unsigned short)HIWORD(lparam));
            return 0;
        case WM_CHAR: gdi_keyboard((unsigned char)wparam); return 0;
        case WM_LBUTTONDOWN:
            drv_lmouse_pressed = TRUE;
            gdi_last_x = (int)(short)LOWORD(lparam);
            gdi_last_y = (int)(short)HIWORD(lparam);
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP:
            drv_lmouse_pressed = FALSE;
            if (!drv_rmouse_pressed) ReleaseCapture();
            return 0;
        case WM_RBUTTONDOWN:
            drv_rmouse_pressed = TRUE;
            SetCapture(window);
            gdi_mouse_metadata_changed();
            return 0;
        case WM_RBUTTONUP:
            drv_rmouse_pressed = FALSE;
            if (!drv_lmouse_pressed) ReleaseCapture();
            gdi_mouse_metadata_changed();
            return 0;
        case WM_MOUSEMOVE:
            gdi_mouse_move((int)(short)LOWORD(lparam), (int)(short)HIWORD(lparam));
            return 0;
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
            drv_lmouse_pressed = FALSE;
            drv_rmouse_pressed = FALSE;
            ReleaseCapture();
            gdi_mouse_metadata_changed();
            return 0;
        case WM_ACTIVATE:
            gdi_palette_foreground = wparam != WA_INACTIVE;
            if (gdi_palette_foreground && gdi_realize_palette(TRUE) != 0)
            {
                gdi_dirty = TRUE;
                InvalidateRect(window, NULL, FALSE);
            }
            return 0;
        case WM_QUERYNEWPALETTE:
            gdi_palette_foreground = TRUE;
            if (gdi_realize_palette(TRUE) != 0)
            {
                gdi_dirty = TRUE;
                InvalidateRect(window, NULL, FALSE);
                return TRUE;
            }
            return FALSE;
        case WM_PALETTECHANGED:
            if ((HWND)wparam != window)
            {
                if (gdi_realize_palette(FALSE) != 0)
                {
                    gdi_dirty = TRUE;
                    InvalidateRect(window, NULL, FALSE);
                }
            }
            return 0;
        case WM_TIMER:
            if (wparam == GDI_TIMER && drv_auto_enabled && gdi_ready &&
                globe_ready() && !gdi_draw_paused)
            {
                tick = GetTickCount();
                elapsed = (tick - gdi_auto_tick) & 0xffffffffUL;
                gdi_auto_tick = tick;
                /* Include timer wait time, not only the duration of GDI calls. */
                drv_rotate_elapsed((double)elapsed / 1000.0);
                gdi_dirty = TRUE;
                InvalidateRect(window, NULL, FALSE);
            }
            return 0;
        case WM_CLOSE: drv_close(); return 0;
        case WM_DESTROY:
            gdi_quiesce();
            gdi_window = 0;
            PostQuitMessage(0);
            return 0;
        default: return DefWindowProc(window, message, wparam, lparam);
    }
}

void drv_loop(void)
{
    MSG message;
    int result;
    if (gdi_failed || gdi_window == 0) return;
    gdi_ready = TRUE;
    gdi_dirty = TRUE;
    InvalidateRect(gdi_window, NULL, FALSE);
    while ((result = GetMessage(&message, 0, 0, 0)) > 0)
    {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    if (result < 0) gdi_error("Windows message loop failed");
}
#endif /* DRV_GDI */
