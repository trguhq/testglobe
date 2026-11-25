/*
 * testglobe - native 16-bit Windows 3.0/3.1 GDI driver.
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
#include "drv.h"
#include "drv_ext.h"
#include "globe.h"
#include "glob_ext.h"
#include "earthres.h"
#include "soft3d.h"

/* Some Win16 headers omit the documented GetNearestColor failure value. */
#ifndef CLR_INVALID
#define CLR_INVALID ((COLORREF)0xffffffffUL)
#endif

/* Indexed quad packets share their boundary vertices. Each packet and its
 * PolyPolygon arrays fit within one Win16 segment at every Earth resolution. */
#define GDI_MAX_VERTICES SOFT3D_MAX_VERTICES
#define GDI_MAX_FACETS DRV_QUAD_BATCH_MAX
#define GDI_MAX_POLY SOFT3D_MAX_POLY
#define GDI_MASK_ROP 0x00e20746UL /* DSPDxax: mask ? selected brush : destination */
#define GDI_TIMER 1
/* Up to 256 brushes plus these pens bound our share of the 64 KiB GDI heap;
 * solid batches beyond the cached indices borrow a temporary pen. */
#define GDI_PEN_CACHE 64
/* Areas drawn over the uniform background: globe facets, status, help. */
#define GDI_GLOBE_RECT 0
#define GDI_STATUS_RECT 1
#define GDI_HELP_RECT 2
#define GDI_RECTS 3
#if 2 * (DRV_QUAD_BATCH_MAX + 1) > GDI_MAX_VERTICES
#error GDI quad batches exceed the transformed vertex cache
#endif
#if GDI_MAX_FACETS > 254
#error GDI color lists store facet numbers in bytes
#endif

typedef Soft3d_float_point Gdi_float_point;

static HINSTANCE gdi_instance;
static int gdi_show;
static HWND gdi_window;
static FARPROC gdi_callback;
static int gdi_registered;
static char gdi_class_name[40];
static HPALETTE gdi_palette;
static HBRUSH gdi_brushes[256];
static unsigned char gdi_brush_patterned[256];
static HPEN gdi_pens[256];
static unsigned int gdi_pen_count;
static unsigned char gdi_realized_colors[256][3];
static Drv_palette_match gdi_color_matches[256];
static int gdi_palette_valid;
static int gdi_palette_pending;
static int gdi_pattern_unavailable;
static HDC gdi_buffer;
static HBITMAP gdi_bitmap;
static HBITMAP gdi_previous_bitmap;
static HPALETTE gdi_previous_palette;
/* A diagnostic-only frame is valid too, but a failed clear never is. */
static int gdi_buffer_valid;
static HFONT gdi_font;
static int gdi_font_size;
static HDC gdi_mask;
static HBITMAP gdi_mask_bitmap;
static HBITMAP gdi_mask_previous;
static int gdi_mask_unavailable;
/* A paint DC is borrowed only for one render; gdi_buffer remains owned. */
static HDC gdi_target;
/* Objects a packet selected into gdi_selection_dc, and the DC's own ones. */
static HDC gdi_selection_dc;
static HBRUSH gdi_selected_brush;
static HPEN gdi_selected_pen;
static HPEN gdi_transient_pen;
static HBRUSH gdi_saved_brush;
static HPEN gdi_saved_pen;
/* The DC whose text/background colors a frame's mask blits replaced. */
static HDC gdi_mask_colors_dc;
static COLORREF gdi_saved_text_color;
static COLORREF gdi_saved_background_color;
static int gdi_rendering;
static int gdi_release_pending;
/* Optional resources released by drv_release_optional() are retried at
 * most once per scene revision, never inside a render. */
static int gdi_reacquire_pending;
static unsigned long gdi_reacquire_revision;
static int gdi_batch_enabled = TRUE;
static int gdi_restart_frame;
static int gdi_frame_failed;
static int gdi_draw_paused;
static int gdi_brush_reduced;
static unsigned char gdi_background;
static unsigned char gdi_foreground;
static int gdi_palette_foreground;
static int gdi_active;
static int gdi_has_fpu;
static int gdi_ready;
static int gdi_failed;
static int gdi_timer_active;
static int gdi_idle_frame;
static int gdi_dirty = TRUE;
/* Some change since the last paint needs the whole client presented. */
static int gdi_requested_full = TRUE;
/* What the presented frame drew over the background, and the frame being
 * drawn. Timer and drag frames invalidate only these areas. */
static RECT gdi_shown[GDI_RECTS];
static RECT gdi_drawn[GDI_RECTS];
static int gdi_shown_valid;
static int gdi_last_x;
static int gdi_last_y;
static DWORD gdi_auto_tick;
static unsigned long gdi_scene_revision;
static float gdi_float_matrix[3][3];
static long gdi_fixed_matrix[3][3];
static Gdi_float_point gdi_float_points[GDI_MAX_VERTICES];
static Soft3d_fixed_point gdi_fixed_points[GDI_MAX_VERTICES];
static Soft3d_project_cache TG_HUGE *gdi_project_cache;
static Drv_quad_stream gdi_stream;
static int gdi_packet_active;
static POINT gdi_polygons[GDI_MAX_FACETS][GDI_MAX_POLY];
static int gdi_polygon_counts[GDI_MAX_FACETS];
/* Per-packet lists hold facet number + 1, so zero (the static state) is
 * empty; only the touched colors are visited and cleared again. */
static unsigned char gdi_color_head[256];
static unsigned char gdi_color_next[GDI_MAX_FACETS];
static unsigned char gdi_touched[GDI_MAX_FACETS];
/* Win16 PolyPolygon requires an explicit closing point for every contour. */
static POINT gdi_batch_points[GDI_MAX_FACETS * (GDI_MAX_POLY + 1)];
static int gdi_batch_counts[GDI_MAX_FACETS];
static void gdi_restore_mask_colors(void);

static HDC gdi_draw_dc(void)
{
    return gdi_target != 0 ? gdi_target : gdi_buffer;
}

/* Rectangles are half-open; an empty one covers no pixels. */
static int gdi_rect_empty(const RECT *rect)
{
    return rect->left >= rect->right || rect->top >= rect->bottom;
}

static void gdi_rect_clear(RECT *rect)
{
    rect->left = rect->top = rect->right = rect->bottom = 0;
}

static void gdi_rect_union(RECT *total, const RECT *part)
{
    if (gdi_rect_empty(part)) return;
    if (gdi_rect_empty(total)) { *total = *part; return; }
    if (part->left < total->left) total->left = part->left;
    if (part->top < total->top) total->top = part->top;
    if (part->right > total->right) total->right = part->right;
    if (part->bottom > total->bottom) total->bottom = part->bottom;
}

/* Geometry, colors, modes and input redraw and present the whole client. */
static void gdi_mark_full(void)
{
    gdi_dirty = TRUE;
    gdi_requested_full = TRUE;
}

static void gdi_invalidate_full(void)
{
    gdi_mark_full();
    if (gdi_window != 0) InvalidateRect(gdi_window, NULL, FALSE);
}

/* The buffer already holds a complete frame; only the copy is pending. */
static void gdi_present_full(void)
{
    gdi_requested_full = TRUE;
    if (gdi_window != 0) InvalidateRect(gdi_window, NULL, FALSE);
}

/* Every packet borrows the target DC's brush and pen and restores them once,
 * not after each color batch. A temporary pen is deleted once deselected. */
static void gdi_restore_objects(void)
{
    if (gdi_selection_dc != 0)
    {
        if (gdi_selected_brush != 0)
            SelectObject(gdi_selection_dc, gdi_saved_brush);
        if (gdi_selected_pen != 0)
            SelectObject(gdi_selection_dc, gdi_saved_pen);
    }
    gdi_selection_dc = 0;
    gdi_selected_brush = 0;
    gdi_selected_pen = 0;
    if (gdi_transient_pen != 0) DeleteObject(gdi_transient_pen);
    gdi_transient_pen = 0;
}

static int gdi_select_brush(HDC dc, HBRUSH brush)
{
    HBRUSH previous;
    if (gdi_selection_dc != 0 && gdi_selection_dc != dc) gdi_restore_objects();
    if (brush == gdi_selected_brush) return TRUE;
    previous = (HBRUSH)SelectObject(dc, brush);
    if (previous == 0) return FALSE;
    if (gdi_selected_brush == 0) gdi_saved_brush = previous;
    gdi_selection_dc = dc;
    gdi_selected_brush = brush;
    return TRUE;
}

static int gdi_select_pen(HDC dc, HPEN pen, int transient)
{
    HPEN previous;
    if (gdi_selection_dc != 0 && gdi_selection_dc != dc) gdi_restore_objects();
    if (pen == gdi_selected_pen) return TRUE;
    previous = (HPEN)SelectObject(dc, pen);
    if (previous == 0)
    {
        if (transient) DeleteObject(pen);
        return FALSE;
    }
    if (gdi_selected_pen == 0) gdi_saved_pen = previous;
    if (gdi_transient_pen != 0) DeleteObject(gdi_transient_pen);
    gdi_transient_pen = transient ? pen : 0;
    gdi_selection_dc = dc;
    gdi_selected_pen = pen;
    return TRUE;
}

/* Brushes and their same-index pens are released together. Objects still
 * selected by the current packet stay cached until a later release. */
static void gdi_release_brushes(void)
{
    unsigned int i;
    for (i = 0; i < 256U; ++i)
    {
        if (gdi_brushes[i] != 0 && gdi_brushes[i] != gdi_selected_brush)
        {
            DeleteObject(gdi_brushes[i]);
            gdi_brushes[i] = 0;
            gdi_brush_patterned[i] = FALSE;
        }
        if (gdi_pens[i] != 0 && gdi_pens[i] != gdi_selected_pen)
        {
            DeleteObject(gdi_pens[i]);
            gdi_pens[i] = 0;
            --gdi_pen_count;
        }
    }
}

/* Query colors after the Earth palette has been selected and realized on a
 * window DC. Palette entries which Windows cannot allocate may map to other
 * system colors; those actual colors, not the requested RGBs, form mixtures.
 * Do not retain partial query results or stale pattern brushes on failure. */
static int gdi_refresh_colors(HDC dc)
{
    unsigned char colors[256][3];
    COLORREF rgb;
    unsigned int i;
    int changed;
    gdi_palette_pending = FALSE;
    for (i = 0; i < 256U; ++i)
    {
        rgb = GetNearestColor(dc, PALETTEINDEX(i));
        if (rgb == CLR_INVALID)
        {
            changed = gdi_palette_valid;
            gdi_palette_valid = FALSE;
            gdi_release_brushes();
            return changed;
        }
        colors[i][0] = (unsigned char)(rgb & 255UL);
        colors[i][1] = (unsigned char)((rgb >> 8) & 255UL);
        colors[i][2] = (unsigned char)((rgb >> 16) & 255UL);
    }
    changed = !gdi_palette_valid ||
              memcmp(colors, gdi_realized_colors, sizeof(colors)) != 0;
    if (changed)
    {
        gdi_release_brushes();
        memcpy(gdi_realized_colors, colors, sizeof(colors));
        /* One de-duplicated pass: a background palette often realizes
         * only a few dozen distinct colors among its 256 entries. */
        (void)drv_match_palette_colors(&earth_cmap[0][0], 256U,
                          &gdi_realized_colors[0][0], 256U, gdi_color_matches);
        gdi_pattern_unavailable = FALSE;
        gdi_palette_valid = TRUE;
    }
    return changed;
}

extern int testglobe_main(int argc, char **argv);
LONG FAR PASCAL gdi_window_proc(HWND window, UINT message,
                                 WPARAM wparam, LPARAM lparam);

static void gdi_delete_buffer(void)
{
    if (gdi_buffer != 0)
    {
        if (gdi_selection_dc == gdi_buffer) gdi_restore_objects();
        if (gdi_mask_colors_dc == gdi_buffer) gdi_restore_mask_colors();
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
    gdi_buffer_valid = FALSE;
    gdi_shown_valid = FALSE;
}

/* Select native bitmap text by client size, retaining the DC's font when
 * Windows substitutes an unsuitable face. One font is cached; a failed
 * request is retried only when the size tier changes, not every frame. */
static HFONT gdi_select_font(HDC dc)
{
    HFONT previous;
    TEXTMETRIC metrics;
    int height;
    height = drv_win_width < 480U || drv_win_height < 320U ? 10 :
             drv_win_width >= 960U && drv_win_height >= 640U ? 16 : 12;
    if (height != gdi_font_size)
    {
        if (gdi_font != 0) DeleteObject(gdi_font);
        gdi_font_size = height;
        gdi_font = CreateFont(height, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, "Small Fonts");
    }
    if (gdi_font == 0) return 0;
    previous = (HFONT)SelectObject(dc, gdi_font);
    if (previous == 0) return 0;
    if (!GetTextMetrics(dc, &metrics) || metrics.tmHeight < 6 ||
        metrics.tmHeight > height + 2 || metrics.tmAveCharWidth <= 0)
    {
        SelectObject(dc, previous);
        DeleteObject(gdi_font);
        gdi_font = 0;
        return 0;
    }
    return previous;
}

static void gdi_delete_mask(void)
{
    if (gdi_mask != 0)
    {
        if (gdi_mask_previous != 0)
            SelectObject(gdi_mask, gdi_mask_previous);
        DeleteDC(gdi_mask);
    }
    if (gdi_mask_bitmap != 0) DeleteObject(gdi_mask_bitmap);
    gdi_mask = 0;
    gdi_mask_bitmap = gdi_mask_previous = 0;
}

static void gdi_use_solid_colors(void)
{
    /* A failing raster operation may have partly painted the target.
     * Deselect cached objects first so every pattern brush is released. */
    gdi_restore_objects();
    gdi_delete_mask();
    gdi_mask_unavailable = TRUE;
    gdi_release_brushes();
    drv_notice("nearest palette colors");
}

/* The mask includes the one-pixel outline. Windows 3.1 NULL_PEN polygon
 * fills can omit boundary pixels (Microsoft Q98619). Applying the actual
 * brush through this mask gives the outline the same dither as the interior.
 * One reusable bit per viewport pixel costs 9,600 bytes at 320x240. */
static int gdi_make_mask(HDC dc)
{
    if (gdi_mask != 0) return TRUE;
    if (gdi_mask_unavailable) return FALSE;
    gdi_mask = CreateCompatibleDC(dc);
    gdi_mask_bitmap = CreateBitmap((int)drv_win_width, (int)drv_win_height,
                                  1, 1, NULL);
    if (gdi_mask != 0 && gdi_mask_bitmap != 0)
    {
        gdi_mask_previous = (HBITMAP)SelectObject(gdi_mask, gdi_mask_bitmap);
        if (gdi_mask_previous != 0 &&
            SelectObject(gdi_mask, GetStockObject(WHITE_BRUSH)) != 0 &&
            SelectObject(gdi_mask, GetStockObject(WHITE_PEN)) != 0 &&
            SetPolyFillMode(gdi_mask, WINDING) != 0) return TRUE;
    }
    gdi_use_solid_colors();
    return FALSE;
}

void drv_release_optional(void)
{
    if (gdi_rendering)
    {
        gdi_release_pending = TRUE;
        return;
    }
    gdi_delete_buffer();
    gdi_use_solid_colors(); /* Also releases the brushes and pens. */
    if (gdi_font != 0) DeleteObject(gdi_font);
    gdi_font = 0; /* Keep the attempted size: low-memory frames use native defaults. */
    drv_free_items(gdi_project_cache);
    gdi_project_cache = NULL;
    gdi_packet_active = FALSE;
    drv_quadstream_reset(&gdi_stream);
    gdi_reacquire_pending = TRUE;
    gdi_reacquire_revision = globe_revision;
    /* Direct drawing can paint only the current update region. */
    gdi_invalidate_full();
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

static void TG_ATEXIT_CALL gdi_cleanup(void)
{
    unsigned int i;
    gdi_quiesce();
    gdi_reacquire_pending = FALSE;
    drv_free_items(gdi_project_cache);
    gdi_project_cache = NULL;
    gdi_packet_active = FALSE;
    drv_quadstream_reset(&gdi_stream);
    if (gdi_window != 0) DestroyWindow(gdi_window);
    gdi_restore_objects();
    gdi_restore_mask_colors();
    gdi_delete_buffer();
    gdi_delete_mask();
    if (gdi_font != 0) DeleteObject(gdi_font);
    gdi_font = 0;
    gdi_font_size = 0;
    for (i = 0; i < 256; ++i)
    {
        if (gdi_brushes[i] != 0) DeleteObject(gdi_brushes[i]);
        if (gdi_pens[i] != 0) DeleteObject(gdi_pens[i]);
        gdi_brushes[i] = 0;
        gdi_brush_patterned[i] = FALSE;
        gdi_pens[i] = 0;
    }
    gdi_pen_count = 0;
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
    /* Keep the single-instance policy required by Microsoft C's huge
     * Windows model, which can have multiple data segments. */
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
    if (gdi_project_cache == NULL)
        gdi_project_cache = (Soft3d_project_cache TG_HUGE *)drv_alloc_items(
            1UL, sizeof(Soft3d_project_cache));
    soft3d_project_cache_reset(gdi_project_cache);
    /* The first scene revision retries a cache that does not fit now. */
    if (gdi_project_cache == NULL)
    {
        gdi_reacquire_pending = TRUE;
        gdi_reacquire_revision = globe_revision;
    }
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

static HBRUSH gdi_create_brush(unsigned int color, int *patterned)
{
    struct Gdi_pattern_dib
    {
        BITMAPINFOHEADER header;
        WORD colors[2];
        BYTE pixels[32];
    } FAR *dib;
    HGLOBAL memory;
    HBRUSH brush;
    const unsigned short TG_HUGE *pattern;
    unsigned int y, solid;
    Drv_palette_match *match;
    *patterned = FALSE;
    solid = color;
    if (gdi_palette_valid)
    {
        match = &gdi_color_matches[color];
        solid = match->solid;
        if (match->mix != 0U && !gdi_pattern_unavailable &&
            !gdi_mask_unavailable)
        {
            memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                                  (DWORD)sizeof(*dib));
            if (memory != 0)
            {
                dib = (struct Gdi_pattern_dib FAR *)GlobalLock(memory);
                if (dib != NULL)
                {
                    dib->header.biSize = sizeof(BITMAPINFOHEADER);
                    dib->header.biWidth = dib->header.biHeight = 8;
                    dib->header.biPlanes = dib->header.biBitCount = 1;
                    dib->header.biCompression = BI_RGB;
                    dib->header.biSizeImage = sizeof(dib->pixels);
                    dib->header.biClrUsed = 2;
                    dib->colors[0] = match->first;
                    dib->colors[1] = match->second;
                    pattern = drv_dither_pattern(match->mix);
                    /* Bottom-up DIB rows are DWORD-aligned. A clear Bayer
                     * bit selects the second pen, so invert its high byte. */
                    for (y = 0; y < 8U; ++y)
                        dib->pixels[(7U - y) * 4U] =
                            (BYTE)((pattern[y & 3U] >> 8) ^ 255U);
                    GlobalUnlock(memory);
                    brush = CreateDIBPatternBrush(memory, DIB_PAL_COLORS);
                    GlobalFree(memory);
                    if (brush != 0)
                    {
                        *patterned = TRUE;
                        return brush;
                    }
                }
                else GlobalFree(memory);
            }
            gdi_pattern_unavailable = TRUE;
            drv_notice("nearest palette colors");
        }
    }
    return CreateSolidBrush(PALETTEINDEX(solid));
}

static HBRUSH gdi_brush(unsigned int color)
{
    unsigned long luminance;
    int patterned;
    if (!gdi_brush_reduced && gdi_brushes[color] == 0)
    {
        gdi_brushes[color] = gdi_create_brush(color, &patterned);
        if (gdi_brushes[color] == 0)
        {
            gdi_release_brushes();
            gdi_brushes[color] = gdi_create_brush(color, &patterned);
            if (gdi_brushes[color] == 0)
            {
                gdi_brush_reduced = TRUE;
                drv_notice("reduced colors");
            }
        }
        gdi_brush_patterned[color] =
            (unsigned char)(gdi_brushes[color] != 0 && patterned);
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

static unsigned int gdi_solid_index(unsigned int color)
{
    return gdi_palette_valid ? gdi_color_matches[color].solid : color;
}

/* Solid fills outline with a pen of the brush's own palette index. */
static HPEN gdi_pen(unsigned int solid, int *transient)
{
    HPEN pen;
    *transient = FALSE;
    if (gdi_pens[solid] != 0) return gdi_pens[solid];
    pen = CreatePen(PS_SOLID, 0, PALETTEINDEX(solid));
    if (pen == 0)
    {
        gdi_release_brushes();
        pen = CreatePen(PS_SOLID, 0, PALETTEINDEX(solid));
        if (pen == 0) return 0;
    }
    if (gdi_pen_count < GDI_PEN_CACHE)
    {
        gdi_pens[solid] = pen;
        ++gdi_pen_count;
    }
    else *transient = TRUE;
    return pen;
}

/* Only an inexactly realized color needs a two-color pattern, and only a
 * pattern needs the mask: the ROP3 blit is the slowest raster operation. */
static int gdi_needs_pattern(unsigned int color)
{
    if (gdi_brush_reduced) return FALSE;
    if (gdi_brushes[color] != 0) return gdi_brush_patterned[color];
    return gdi_palette_valid && gdi_color_matches[color].mix != 0U &&
           !gdi_pattern_unavailable && !gdi_mask_unavailable;
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
    if (gdi_refresh_colors(dc)) changed = TRUE;
    SelectPalette(dc, previous, TRUE);
    ReleaseDC(gdi_window, dc);
    return changed;
}

/* Returns FALSE only when the buffer could not be allocated; callers then
 * draw directly. Without a window or size there is nothing to build yet. */
static int gdi_make_buffer(void)
{
    HDC dc;
    gdi_delete_buffer();
    gdi_delete_mask();
    gdi_mask_unavailable = FALSE;
    gdi_release_brushes();
    gdi_pattern_unavailable = FALSE;
    gdi_mark_full();
    if (gdi_window == 0 || drv_win_width == 0 || drv_win_height == 0) return TRUE;
    dc = GetDC(gdi_window);
    if (dc == 0) return FALSE;
    gdi_buffer = CreateCompatibleDC(dc);
    gdi_bitmap = CreateCompatibleBitmap(dc, (int)drv_win_width,
                                        (int)drv_win_height);
    ReleaseDC(gdi_window, dc);
    if (gdi_buffer == 0 || gdi_bitmap == 0)
    {
        gdi_delete_buffer();
        return FALSE;
    }
    gdi_previous_bitmap = (HBITMAP)SelectObject(gdi_buffer, gdi_bitmap);
    if (gdi_previous_bitmap == 0)
    {
        gdi_delete_buffer();
        return FALSE;
    }
    gdi_previous_palette = SelectPalette(gdi_buffer, gdi_palette, TRUE);
    /* Realize only on a window DC. Windows 3.1 has a documented foreground
     * palette bug when RealizePalette is applied to a compatible memory DC. */
    return TRUE;
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
    gdi_shown_valid = FALSE;
    if (gdi_palette != 0 && !gdi_make_buffer()) drv_notice("direct drawing");
    gdi_mark_full();
}

/* After drv_release_optional(), retry the buffer, projection cache, mask and
 * brush modes once per scene revision (a resolution change, then the next
 * key or paint) and never inside a render; a failed attempt waits for the
 * next revision instead of allocating on every paint. */
static void gdi_reacquire_optional(void)
{
    if (!gdi_reacquire_pending || gdi_rendering || gdi_failed ||
        gdi_window == 0 || gdi_reacquire_revision == globe_revision) return;
    gdi_reacquire_revision = globe_revision;
    if (gdi_project_cache == NULL)
    {
        gdi_project_cache = (Soft3d_project_cache TG_HUGE *)drv_alloc_items(
            1UL, sizeof(Soft3d_project_cache));
        soft3d_project_cache_reset(gdi_project_cache);
    }
    gdi_brush_reduced = FALSE;
    gdi_pattern_unavailable = FALSE;
    gdi_mask_unavailable = FALSE;
    if (gdi_font == 0) gdi_font_size = 0;
    /* Brushes made for solid-only modes must not outlive them. The OSD
     * already reports a missing buffer; keep the scene's own notice. */
    if (gdi_buffer == 0) (void)gdi_make_buffer();
    else gdi_release_brushes();
    gdi_reacquire_pending = gdi_buffer == 0 || gdi_project_cache == NULL;
    gdi_invalidate_full();
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
    gdi_palette_valid = FALSE;
    gdi_palette_pending = TRUE;
    gdi_pattern_unavailable = FALSE;
    gdi_active = gdi_palette_foreground = gdi_idle_frame = FALSE;
    gdi_shown_valid = FALSE;
    gdi_requested_full = TRUE;
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
    /* No palette match here: activation by ShowWindow realizes and matches
     * the foreground palette, and an inactive start matches on first paint. */
    if (!gdi_make_buffer()) drv_notice("direct drawing");
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
    int prepared;
    gdi_packet_active = FALSE;
    drv_quadstream_reset(&gdi_stream);
    soft3d_project_cache_reset(gdi_project_cache);
    /* R = Rx(user x) Ry(user y) Rx(-90) Rz(-90), followed by z=-3000. */
    if (gdi_has_fpu)
        prepared = soft3d_float_prepare(gdi_float_matrix, &drv_rot_x, &drv_rot_y);
    else
        prepared = soft3d_fixed_prepare(gdi_fixed_matrix, &drv_rot_x, &drv_rot_y);
    if (!prepared)
    {
        gdi_frame_failed = TRUE;
        drv_abort_frame("coordinate is out of range");
    }
}

/* Raw callers, including clipping fixtures, never borrow packet caches. */
static int gdi_prepare_polygon(unsigned int point_count,
                               const unsigned int *indices,
                               unsigned int index_count, POINT *destination)
{
    Soft3d_screen_point screen[GDI_MAX_POLY];
    Soft3d_project_cache TG_HUGE *cache;
    int i, count;
    if (indices == NULL || destination == NULL ||
        point_count == 0U || point_count > GDI_MAX_VERTICES ||
        (index_count != 3U && index_count != 4U)) return 0;
    for (i = 0; i < (int)index_count; ++i)
        if (indices[i] >= point_count) return 0;
    cache = gdi_packet_active ? gdi_project_cache : NULL;
    if (gdi_has_fpu)
        count = soft3d_float_project_cached(gdi_float_points, point_count,
                         indices, index_count, drv_win_width, drv_win_height,
                         SOFT3D_FLOAT_SINGLE, cache, screen);
    else
        count = soft3d_fixed_project_cached(gdi_fixed_points, point_count,
                         indices, index_count, drv_win_width, drv_win_height,
                         cache, screen);
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

/* Both native calls consume the same explicitly closed packed contours. */
static int gdi_fill_polygons(HDC dc, int total)
{
    int j, points;
    if (gdi_batch_enabled)
    {
        if (PolyPolygon(dc, gdi_batch_points, gdi_batch_counts, total))
            return TRUE;
        gdi_batch_enabled = FALSE;
        gdi_restart_frame = TRUE;
        drv_abort_frame("individual polygons");
    }
    else
    {
        points = 0;
        for (j = 0; j < total; ++j)
        {
            if (!Polygon(dc, gdi_batch_points + points, gdi_batch_counts[j]))
            {
                gdi_frame_failed = TRUE;
                drv_abort_frame("individual polygon failed");
                return FALSE;
            }
            points += gdi_batch_counts[j];
        }
        return TRUE;
    }
    return FALSE;
}

/* The mask blit expands black mask bits to zero and white bits to all
 * ones. The first masked batch of a frame sets these colors; the frame
 * restores the DC's own colors once, before the OSD chooses its own. */
static int gdi_set_mask_colors(HDC dc)
{
    COLORREF text_color, background_color;
    if (gdi_mask_colors_dc == dc) return TRUE;
    gdi_restore_mask_colors();
    text_color = SetTextColor(dc, RGB(0, 0, 0));
    if (text_color == CLR_INVALID) return FALSE;
    background_color = SetBkColor(dc, RGB(255, 255, 255));
    if (background_color == CLR_INVALID)
    {
        SetTextColor(dc, text_color);
        return FALSE;
    }
    gdi_saved_text_color = text_color;
    gdi_saved_background_color = background_color;
    gdi_mask_colors_dc = dc;
    return TRUE;
}

static void gdi_restore_mask_colors(void)
{
    if (gdi_mask_colors_dc == 0) return;
    SetBkColor(gdi_mask_colors_dc, gdi_saved_background_color);
    SetTextColor(gdi_mask_colors_dc, gdi_saved_text_color);
    gdi_mask_colors_dc = 0;
}

static void gdi_selection_failed(void)
{
    gdi_frame_failed = TRUE;
    drv_abort_frame("cannot select drawing brush or pen");
}

static void gdi_fill_batch(HDC dc, unsigned int color, int total, int points)
{
    RECT bounds;
    HBRUSH brush;
    HPEN pen;
    int j, masked, transient, mask_failed;
    bounds.left = bounds.right = gdi_batch_points[0].x;
    bounds.top = bounds.bottom = gdi_batch_points[0].y;
    for (j = 1; j < points; ++j)
    {
        if (gdi_batch_points[j].x < bounds.left) bounds.left = gdi_batch_points[j].x;
        if (gdi_batch_points[j].x > bounds.right) bounds.right = gdi_batch_points[j].x;
        if (gdi_batch_points[j].y < bounds.top) bounds.top = gdi_batch_points[j].y;
        if (gdi_batch_points[j].y > bounds.bottom) bounds.bottom = gdi_batch_points[j].y;
    }
    /* Include the outline without exceeding signed Win16 bounds. */
    if (bounds.right < (int)drv_win_width) ++bounds.right;
    if (bounds.bottom < (int)drv_win_height) ++bounds.bottom;
    gdi_rect_union(&gdi_drawn[GDI_GLOBE_RECT], &bounds);
    masked = gdi_needs_pattern(color) && gdi_make_mask(dc);
    brush = gdi_brush(color);
    if (gdi_brush_reduced || !gdi_brush_patterned[color]) masked = FALSE;
    if (!gdi_select_brush(dc, brush))
    {
        gdi_selection_failed();
        return;
    }
    if (!masked)
    {
        pen = 0;
        transient = FALSE;
        if (!gdi_brush_reduced)
        {
            pen = gdi_pen(gdi_solid_index(color), &transient);
            if (pen == 0)
            {
                gdi_brush_reduced = TRUE;
                drv_notice("reduced colors");
                brush = gdi_brush(color);
                if (!gdi_select_brush(dc, brush))
                {
                    gdi_selection_failed();
                    return;
                }
            }
        }
        if (pen == 0)
            pen = (HPEN)GetStockObject(brush == GetStockObject(BLACK_BRUSH) ?
                                      BLACK_PEN : WHITE_PEN);
        if (!gdi_select_pen(dc, pen, transient))
        {
            gdi_selection_failed();
            return;
        }
        (void)gdi_fill_polygons(dc, total);
        return;
    }
    mask_failed = FALSE;
    if (!PatBlt(gdi_mask, bounds.left, bounds.top, bounds.right - bounds.left,
                bounds.bottom - bounds.top, BLACKNESS))
        mask_failed = TRUE;
    else if (gdi_fill_polygons(gdi_mask, total))
    {
        /* The brush stays anchored in destination coordinates. */
        if (!gdi_set_mask_colors(dc) ||
            !BitBlt(dc, bounds.left, bounds.top, bounds.right - bounds.left,
                    bounds.bottom - bounds.top, gdi_mask, bounds.left,
                    bounds.top, GDI_MASK_ROP))
            mask_failed = TRUE;
    }
    else if (!gdi_restart_frame)
    {
        /* A device may reject drawing to a monochrome bitmap while
         * still supporting the direct solid-color operation. */
        gdi_frame_failed = FALSE;
        mask_failed = TRUE;
    }
    if (mask_failed)
    {
        /* Retry the complete frame with solid brushes and matching outlines. */
        gdi_use_solid_colors();
        gdi_restart_frame = TRUE;
        drv_abort_frame("nearest palette colors");
    }
}

void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                   const Vertex TG_HUGE *vertices,
                                   unsigned int quad_count,
                                   const unsigned int TG_HUGE *indices,
                                   const unsigned char TG_HUGE *colors)
{
    unsigned int i, color, corner, quad_indices[4], reuse_count, touched, t, facet;
    int total, points, j, transformed, stopped;
    Drv_quad_context context;
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
    reuse_count = drv_quadstream_reuse(&gdi_stream, vertex_count,
                                      vertices, &context);
    if (gdi_has_fpu)
        transformed = soft3d_float_transform_reuse(gdi_float_matrix,
                         vertex_count, vertices, gdi_float_points, reuse_count,
                         SOFT3D_FLOAT_SINGLE);
    else
        transformed = soft3d_fixed_transform_reuse(gdi_fixed_matrix,
                         vertex_count, vertices, gdi_fixed_points, reuse_count);
    if (!transformed)
    {
        drv_quadstream_reset(&gdi_stream);
        gdi_frame_failed = TRUE;
        drv_abort_frame("coordinate is out of range");
        return;
    }
    if (gdi_project_cache != NULL)
        (void)soft3d_project_cache_begin(gdi_project_cache, vertex_count,
                         drv_win_width, drv_win_height, reuse_count);
    drv_quadstream_commit(&gdi_stream, &context);
    gdi_packet_active = TRUE;
    touched = 0;
    for (i = 0; i < quad_count; ++i)
    {
        for (corner = 0; corner < 4U; ++corner)
            quad_indices[corner] = indices[4U * i + corner];
        gdi_polygon_counts[i] = gdi_prepare_polygon(vertex_count, quad_indices, 4U,
                                                   gdi_polygons[i]);
        if (gdi_polygon_counts[i] == 0) continue;
        color = colors[i];
        if (gdi_color_head[color] == 0U)
        {
            /* Keep batches in ascending palette order without scanning
             * all 256 heads: a packet touches at most 64 colors. */
            for (t = touched; t > 0U && gdi_touched[t - 1U] > color; --t)
                gdi_touched[t] = gdi_touched[t - 1U];
            gdi_touched[t] = (unsigned char)color;
            ++touched;
        }
        gdi_color_next[i] = gdi_color_head[color];
        gdi_color_head[color] = (unsigned char)(i + 1U);
        ++globe_quads_num_vis;
    }
    gdi_packet_active = FALSE;
    stopped = FALSE;
    for (t = 0; t < touched; ++t)
    {
        color = gdi_touched[t];
        total = points = 0;
        for (facet = gdi_color_head[color]; facet != 0U && !stopped;
             facet = gdi_color_next[facet - 1U])
        {
            i = facet - 1U;
            gdi_batch_counts[total++] = gdi_polygon_counts[i] + 1;
            for (j = 0; j < gdi_polygon_counts[i]; ++j)
                gdi_batch_points[points++] = gdi_polygons[i][j];
            gdi_batch_points[points++] = gdi_polygons[i][0];
        }
        gdi_color_head[color] = 0;
        if (stopped) continue;
        gdi_fill_batch(dc, color, total, points);
        if (gdi_restart_frame || gdi_frame_failed) stopped = TRUE;
    }
    gdi_restore_objects();
}

void drv_draw_osd(void)
{
    char status[256];
    RECT status_bounds, help_bounds;
    int help_height, status_height;
    UINT flags;
    HDC dc;
    HFONT previous_font;
    dc = gdi_draw_dc();
    gdi_rect_clear(&gdi_drawn[GDI_STATUS_RECT]);
    gdi_rect_clear(&gdi_drawn[GDI_HELP_RECT]);
    if (gdi_failed || dc == 0 || drv_win_width <= 16 || drv_win_height <= 16) return;
    previous_font = gdi_select_font(dc);
    drv_format_stats(status);
    drv_append_status(status, sizeof(status),
                       gdi_has_fpu ? ", 80387 math" : ", fixed-point math");
    if (gdi_buffer == 0)
        drv_append_status(status, sizeof(status), ", direct drawing");
    if (!gdi_batch_enabled)
        drv_append_status(status, sizeof(status), ", individual polygons");
    if (gdi_brush_reduced)
        drv_append_status(status, sizeof(status), ", reduced colors");
    else if (gdi_mask_unavailable)
        drv_append_status(status, sizeof(status), ", nearest palette colors");
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
    {
        status_height = DrawText(dc, status, -1, &status_bounds, flags);
        /* Wrapped lines are clipped to the rectangle; track only those. */
        if (status_height > 0 &&
            status_height < status_bounds.bottom - status_bounds.top)
            status_bounds.bottom = status_bounds.top + status_height;
        gdi_drawn[GDI_STATUS_RECT] = status_bounds;
    }
    if (help_bounds.bottom > help_bounds.top)
    {
        DrawText(dc, drv_help, -1, &help_bounds, flags);
        gdi_drawn[GDI_HELP_RECT] = help_bounds;
    }
    if (previous_font != 0) SelectObject(dc, previous_font);
}

/* Draw one complete frame into dc, which already has the Earth palette. */
static void gdi_render_to(HDC dc)
{
    RECT bounds;
    int pass, cleared, i;
    if (gdi_failed || !gdi_ready || gdi_rendering || dc == 0) return;
    gdi_rendering = TRUE;
    gdi_target = dc;
    drv_fps_start();
    bounds.left = bounds.top = 0;
    bounds.right = (int)drv_win_width;
    bounds.bottom = (int)drv_win_height;
    gdi_frame_failed = FALSE;
    cleared = FALSE;
    if (dc == gdi_buffer) gdi_buffer_valid = FALSE;
    /* At most one retry each for mask failure and batched-call failure. */
    for (pass = 0; pass < 3; ++pass)
    {
        drv_frame_failed = FALSE;
        gdi_restart_frame = FALSE;
        globe_quads_num_vis = 0;
        for (i = 0; i < GDI_RECTS; ++i) gdi_rect_clear(&gdi_drawn[i]);
        cleared = FillRect(dc, &bounds, gdi_brush(gdi_background)) != 0;
        if (!cleared)
        {
            gdi_frame_failed = TRUE;
            break;
        }
        SetPolyFillMode(dc, WINDING);
        if (globe_ready() && !gdi_draw_paused)
        {
            gdi_set_transform();
            if (!gdi_frame_failed) globe_draw_quadbatches();
        }
        if (drv_frame_failed && !gdi_restart_frame) gdi_frame_failed = TRUE;
        if (!gdi_restart_frame) break;
    }
    gdi_restore_mask_colors();
    if (gdi_frame_failed)
    {
        /* Keep a usable native window and the valid mesh; retry only after
         * another input action, never from a partially submitted primitive. */
        gdi_draw_paused = TRUE;
        drv_auto_enabled = FALSE;
        globe_quads_num_vis = 0;
        drv_notice("drawing unavailable; retry a resolution");
        cleared = FillRect(dc, &bounds, (HBRUSH)GetStockObject(BLACK_BRUSH)) != 0;
    }
    if (gdi_draw_paused)
        drv_abort_frame("drawing unavailable; retry a resolution");
    if (cleared && (drv_osd_enabled || !globe_ready() || gdi_draw_paused))
        drv_draw_osd();
    if (dc == gdi_buffer) gdi_buffer_valid = cleared;
    gdi_target = 0;
    gdi_rendering = FALSE;
    gdi_dirty = FALSE;
    if (gdi_release_pending)
    {
        gdi_release_pending = FALSE;
        drv_release_optional();
    }
}

/* Copy the rendered frame; a paint DC clips it to the update region. */
static int gdi_present(HDC dc)
{
    if (gdi_failed || gdi_buffer == 0 || !gdi_buffer_valid) return TRUE;
    return BitBlt(dc, 0, 0, (int)drv_win_width, (int)drv_win_height,
                  gdi_buffer, 0, 0, SRCCOPY);
}

/* Modes which choose the background brush or pattern use. */
static int gdi_color_mode(void)
{
    return (gdi_brush_reduced ? 1 : 0) | (gdi_mask_unavailable ? 2 : 0) |
           (gdi_pattern_unavailable ? 4 : 0) | (gdi_palette_valid ? 8 : 0);
}

/* Present the part of a drawn area outside the area already updated. */
static void gdi_invalidate_outside(const RECT *drawn, const RECT *covered)
{
    RECT part;
    if (gdi_rect_empty(drawn)) return;
    if (gdi_rect_empty(covered))
    {
        InvalidateRect(gdi_window, drawn, FALSE);
        return;
    }
    part = *drawn;
    if (drawn->top < covered->top)
    {
        if (part.bottom > covered->top) part.bottom = covered->top;
        InvalidateRect(gdi_window, &part, FALSE);
    }
    part = *drawn;
    if (drawn->bottom > covered->bottom)
    {
        if (part.top < covered->bottom) part.top = covered->bottom;
        InvalidateRect(gdi_window, &part, FALSE);
    }
    part = *drawn;
    if (part.top < covered->top) part.top = covered->top;
    if (part.bottom > covered->bottom) part.bottom = covered->bottom;
    if (part.top >= part.bottom) return;
    if (drawn->left < covered->left)
    {
        if (part.right > covered->left) part.right = covered->left;
        InvalidateRect(gdi_window, &part, FALSE);
        part.right = drawn->right;
    }
    if (drawn->right > covered->right)
    {
        if (part.left < covered->right) part.left = covered->right;
        InvalidateRect(gdi_window, &part, FALSE);
    }
}

/* A timer or drag frame was invalidated with the previous frame's areas;
 * outside them both frames show the same background. Present what this
 * frame drew beyond them. Failed frames and mode changes present it all. */
static void gdi_track_frame(int covered_full, int mode)
{
    int i, full;
    full = gdi_frame_failed || gdi_draw_paused || mode != gdi_color_mode();
    if (!covered_full && !gdi_requested_full)
    {
        if (full || !gdi_shown_valid) gdi_present_full();
        else
            for (i = 0; i < GDI_RECTS; ++i)
                gdi_invalidate_outside(&gdi_drawn[i], &gdi_shown[i]);
    }
    memcpy(gdi_shown, gdi_drawn, sizeof(gdi_shown));
    gdi_shown_valid = !full;
}

/* One paint or render request: realize the palette once on the window DC,
 * draw when the buffer is stale (always without one), present, and draw
 * directly if the copy fails. covered_full: the whole client is updated. */
static void gdi_paint(HDC dc, int force, int covered_full)
{
    HPALETTE previous;
    int mode, rendered;
    if (gdi_failed || !gdi_ready || gdi_rendering || dc == 0) return;
    if (covered_full) gdi_requested_full = FALSE;
    previous = SelectPalette(dc, gdi_palette, !gdi_palette_foreground);
    if (RealizePalette(dc) != 0) gdi_palette_pending = TRUE;
    /* Another application can change the palette after the buffer was
     * drawn; old pixels cannot acquire new two-pen mixtures by realization. */
    if (gdi_palette_pending && gdi_refresh_colors(dc))
    {
        if (covered_full) gdi_dirty = TRUE;
        else gdi_invalidate_full();
    }
    mode = gdi_color_mode();
    rendered = FALSE;
    if (gdi_buffer != 0 && (force || gdi_dirty))
    {
        gdi_render_to(gdi_buffer);
        rendered = TRUE;
    }
    if (gdi_buffer != 0 && !gdi_present(dc)) drv_release_optional();
    if (gdi_buffer == 0)
    {
        gdi_render_to(dc);
        rendered = TRUE;
    }
    if (rendered)
    {
        drv_fps_stop();
        gdi_track_frame(covered_full, mode);
    }
    if (previous != 0) SelectPalette(dc, previous, TRUE);
}

void drv_render(void)
{
    HDC dc;
    if (gdi_window == 0 || gdi_rendering) return;
    gdi_reacquire_optional();
    dc = GetDC(gdi_window);
    if (dc == 0) return;
    gdi_paint(dc, TRUE, TRUE);
    ReleaseDC(gdi_window, dc);
}

/* Timer and drag frames update only the previous frame's drawn areas; the
 * paint adds this frame's areas beyond them. Without a buffer, or without
 * a known previous frame, the whole client is drawn. */
static void gdi_invalidate_animation(void)
{
    int i, any;
    if (gdi_window == 0) return;
    if (gdi_buffer == 0 || !gdi_shown_valid || gdi_requested_full)
    {
        gdi_invalidate_full();
        return;
    }
    any = FALSE;
    for (i = 0; i < GDI_RECTS; ++i)
    {
        if (gdi_rect_empty(&gdi_shown[i])) continue;
        InvalidateRect(gdi_window, &gdi_shown[i], FALSE);
        any = TRUE;
    }
    if (any) gdi_dirty = TRUE;
    else gdi_invalidate_full();
}

static int gdi_animating(void)
{
    return drv_auto_enabled && gdi_ready && !gdi_failed && gdi_window != 0 &&
           globe_ready() && !gdi_draw_paused;
}

/* Include the wait for a timer or idle slot, not only the GDI calls. */
static void gdi_animate(void)
{
    DWORD tick, elapsed;
    tick = GetTickCount();
    elapsed = (tick - gdi_auto_tick) & 0xffffffffUL;
    gdi_auto_tick = tick;
    drv_rotate_elapsed((double)elapsed / 1000.0);
    gdi_invalidate_animation();
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
    gdi_reacquire_optional();
    gdi_invalidate_full();
}

static void gdi_mouse_move(int x, int y)
{
    if (drv_lmouse_pressed)
    {
        drv_rotate_drag((long)x - gdi_last_x, (long)y - gdi_last_y);
        gdi_invalidate_animation();
    }
    gdi_last_x = x;
    gdi_last_y = y;
}

static void gdi_mouse_metadata_changed(void)
{
#if DEBUG
    if (gdi_ready && !gdi_failed && gdi_window != 0) gdi_invalidate_full();
#endif
}

LONG FAR PASCAL gdi_window_proc(HWND window, UINT message,
                                 WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT paint;
    HDC dc;
    int width, height, covered_full;
    if (gdi_rendering && !gdi_failed)
    {
        /* Native callbacks can dispatch messages while a packet borrows the
         * mesh or a paint DC. Requeue mutations until those borrows end. */
        switch (message)
        {
            case WM_CHAR: case WM_SIZE: case WM_MOVE: case WM_CLOSE: case WM_TIMER:
            case WM_ACTIVATE: case WM_QUERYNEWPALETTE: case WM_PALETTECHANGED:
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
            gdi_reacquire_optional();
            /* BeginPaint validates everything requested so far. */
            covered_full = gdi_requested_full;
            gdi_requested_full = FALSE;
            dc = BeginPaint(window, &paint);
            if (gdi_ready) gdi_paint(dc, FALSE, covered_full);
            EndPaint(window, &paint);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (wparam == SIZE_MINIMIZED) return 0;
            width = (int)(unsigned short)LOWORD(lparam);
            height = (int)(unsigned short)HIWORD(lparam);
            if (gdi_buffer != 0 && width == (int)drv_win_width &&
                height == (int)drv_win_height)
            {
                /* Restoring an icon keeps the client size: retain the
                 * frame, mask and brushes, and restart elapsed time. */
                gdi_auto_tick = GetTickCount();
                drv_reset_timing();
                if (!gdi_buffer_valid) gdi_mark_full();
            }
            else gdi_resize(width, height);
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
            gdi_active = wparam != WA_INACTIVE;
            gdi_palette_pending = TRUE;
            gdi_palette_foreground = gdi_active;
            if (gdi_palette_foreground && gdi_realize_palette(TRUE) != 0)
                gdi_invalidate_full();
            return 0;
        case WM_QUERYNEWPALETTE:
            gdi_palette_pending = TRUE;
            gdi_palette_foreground = TRUE;
            if (gdi_realize_palette(TRUE) != 0)
            {
                gdi_invalidate_full();
                return TRUE;
            }
            return FALSE;
        case WM_PALETTECHANGED:
            if ((HWND)wparam != window)
            {
                gdi_palette_pending = TRUE;
                /* Forcing a background mapping while active would keep
                 * degraded colors until the next activation. */
                if (gdi_realize_palette(gdi_palette_foreground) != 0)
                    gdi_invalidate_full();
            }
            return 0;
        case WM_MOVE:
            /* Native pattern brushes cache an origin for their current DC. */
            gdi_release_brushes();
            gdi_invalidate_full();
            return 0;
        case WM_TIMER:
            if (wparam != GDI_TIMER) return 0;
            if (gdi_idle_frame)
            {
                /* The idle loop draws; the timer covers modal loops. */
                gdi_idle_frame = FALSE;
                return 0;
            }
            if (!IsIconic(window) && gdi_animating()) gdi_animate();
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

/* One message, or one frame while active auto-rotation is idle: a
 * PeekMessage that finds nothing has already yielded to other tasks.
 * Otherwise WaitMessage sleeps until input, a paint or the fallback timer,
 * which also keeps rotation going in modal loops. FALSE after WM_QUIT. */
static int gdi_pump(void)
{
    MSG message;
    if (PeekMessage(&message, 0, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT) return FALSE;
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    else if (gdi_animating() && gdi_active && !IsIconic(gdi_window))
    {
        gdi_animate();
        gdi_idle_frame = TRUE;
        UpdateWindow(gdi_window);
    }
    else WaitMessage();
    return TRUE;
}

void drv_loop(void)
{
    if (gdi_failed || gdi_window == 0) return;
    gdi_ready = TRUE;
    gdi_invalidate_full();
    while (gdi_pump()) continue;
}
#endif /* DRV_GDI */
