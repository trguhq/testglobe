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

/* Split latitude strips into even-sized facet packets. Each packet and its
 * PolyPolygon arrays fit within one Win16 segment at every Earth resolution. */
#define GDI_MAX_VERTICES 130
#define GDI_MAX_FACETS (GDI_MAX_VERTICES - 2)
#define GDI_MAX_POLY 10
#define GDI_TIMER 1
#define GDI_Q14 16384L
#define GDI_COORD_SCALE 16L
#define GDI_FOCAL_Q14 19777L /* cot(45 degrees / 2) / 2, in Q14 */

typedef struct Gdi_fixed_point { long x, y, z; } Gdi_fixed_point;
typedef struct Gdi_float_point { float x, y, z; } Gdi_float_point;
typedef struct Gdi_screen_point { long x, y; } Gdi_screen_point;

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
static unsigned char gdi_background;
static unsigned char gdi_foreground;
static int gdi_palette_foreground;
static int gdi_has_fpu;
static int gdi_ready;
static int gdi_dirty = TRUE;
static int gdi_last_x;
static int gdi_last_y;
static DWORD gdi_auto_tick;
static float gdi_float_matrix[3][3];
static long gdi_fixed_matrix[3][3];
static Gdi_float_point gdi_float_points[GDI_MAX_VERTICES];
static Gdi_fixed_point gdi_fixed_points[GDI_MAX_VERTICES];
static POINT gdi_polygons[GDI_MAX_FACETS][GDI_MAX_POLY];
static int gdi_polygon_counts[GDI_MAX_FACETS];
static int gdi_color_head[256];
static int gdi_color_next[GDI_MAX_FACETS];
static POINT gdi_batch_points[GDI_MAX_FACETS * GDI_MAX_POLY];
static int gdi_batch_counts[GDI_MAX_FACETS];
static const unsigned short gdi_sine_quadrant[91] = {
    0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406,
    3686, 3964, 4240, 4516, 4790, 5063, 5334, 5604, 5872, 6138, 6402, 6664, 6924,
    7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397, 9630, 9860, 10087,
    10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176,
    12365, 12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741, 13894,
    14044, 14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191,
    15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026,
    16083, 16135, 16182, 16225, 16262, 16294, 16322, 16344, 16362, 16374,
    16382, 16384
};

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

static void gdi_cleanup(void)
{
    unsigned int i;
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
    globe_free();
}

void gdi_error(char *message)
{
    MessageBox(gdi_window, message, "testglobe - Windows GDI error",
               MB_OK | MB_ICONEXCLAMATION);
    exit(1);
}

void drv_close(void)
{
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

void drv_load_earth_data(int longitude, unsigned char TG_HUGE *destination)
{
    HRSRC resource;
    HGLOBAL loaded;
    const unsigned char FAR *source;
    unsigned long remaining, length;
    unsigned int base, chunk, i;
    switch (longitude)
    {
        case 32: base = TG_EARTH_32_ID; break;
        case 64: base = TG_EARTH_64_ID; break;
        case 128: base = TG_EARTH_128_ID; break;
        case 256: base = TG_EARTH_256_ID; break;
        case 512: base = TG_EARTH_512_ID; break;
        default: gdi_error("Unsupported Earth resource resolution"); return;
    }
    if (destination == NULL) gdi_error("Missing Earth resource destination");
    remaining = (unsigned long)longitude * (unsigned long)(longitude / 2);
    chunk = 0;
    while (remaining != 0)
    {
        length = remaining < TG_EARTH_CHUNK_SIZE ? remaining : TG_EARTH_CHUNK_SIZE;
        resource = FindResource(gdi_instance, MAKEINTRESOURCE(base + chunk), RT_RCDATA);
        if (resource == 0) gdi_error("Cannot find Earth resource");
        if (SizeofResource(gdi_instance, resource) != length)
            gdi_error("Earth resource has an invalid size");
        loaded = LoadResource(gdi_instance, resource);
        if (loaded == 0) gdi_error("Cannot load Earth resource");
        source = (const unsigned char FAR *)LockResource(loaded);
        if (source == NULL)
        {
            FreeResource(loaded);
            gdi_error("Cannot lock Earth resource");
        }
        /* Each resource is at most 16 KiB. Only the destination traverses
         * the full huge allocation; no far source pointer crosses a segment. */
        for (i = 0; (unsigned long)i < length; ++i)
            *destination++ = source[i];
        UnlockResource(loaded);
        FreeResource(loaded);
        remaining -= length;
        ++chunk;
    }
}

static HBRUSH gdi_brush(unsigned int color)
{
    if (gdi_brushes[color] == 0)
    {
        gdi_brushes[color] = CreateSolidBrush(PALETTEINDEX(color));
        if (gdi_brushes[color] == 0) gdi_error("Cannot allocate GDI brush");
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
    if (dc == 0) gdi_error("Cannot access Windows GDI window");
    gdi_buffer = CreateCompatibleDC(dc);
    gdi_bitmap = CreateCompatibleBitmap(dc, (int)drv_win_width,
                                        (int)drv_win_height);
    ReleaseDC(gdi_window, dc);
    if (gdi_buffer == 0 || gdi_bitmap == 0)
        gdi_error("Cannot allocate Windows GDI back buffer");
    gdi_previous_bitmap = (HBITMAP)SelectObject(gdi_buffer, gdi_bitmap);
    if (gdi_previous_bitmap == 0)
        gdi_error("Cannot select Windows GDI back buffer");
    gdi_previous_palette = SelectPalette(gdi_buffer, gdi_palette, TRUE);
    /* Realize only on a window DC. Windows 3.1 has a documented foreground
     * palette bug when RealizePalette is applied to a compatible memory DC. */
    gdi_dirty = TRUE;
}

void drv_resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if ((unsigned long)width > 32767UL || (unsigned long)height > 32767UL)
        gdi_error("Windows GDI client dimensions exceed signed 16-bit coordinates");
    drv_win_width = (unsigned int)width;
    drv_win_height = (unsigned int)height;
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
    bounds.right = (int)drv_win_width;
    bounds.bottom = (int)drv_win_height;
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    gdi_window = CreateWindow(gdi_class_name, TESTGLOBE_NAME,
                              WS_OVERLAPPEDWINDOW, (int)drv_win_x, (int)drv_win_y,
                              bounds.right - bounds.left, bounds.bottom - bounds.top,
                              0, 0, gdi_instance, NULL);
    if (gdi_window == 0) gdi_error("Cannot create Windows GDI window");
    gdi_realize_palette(gdi_palette_foreground);
    gdi_make_buffer();
    if (SetTimer(gdi_window, GDI_TIMER, 30, NULL) == 0)
        gdi_error("Cannot create Windows animation timer");
    ShowWindow(gdi_window, gdi_show);
    UpdateWindow(gdi_window);
    return 1;
}

/* Extract an IEEE single-precision value without floating instructions.
 * All driver inputs are bounded globe coordinates or rotation angles. */
static long gdi_float_fixed(const float TG_HUGE *value, int fraction_bits)
{
    const unsigned char TG_HUGE *bytes;
    unsigned long bits, magnitude;
    int exponent, shift;
    bytes = (const unsigned char TG_HUGE *)value;
    bits = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
           ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
    exponent = (int)((bits >> 23) & 255UL);
    if (exponent < 127 - fraction_bits) return 0;
    if (exponent > 157 - fraction_bits)
        gdi_error("Windows GDI coordinate is out of range");
    magnitude = (bits & 0x7fffffUL) | 0x800000UL;
    shift = exponent - 150 + fraction_bits;
    if (shift < 0) magnitude >>= -shift;
    else magnitude <<= shift;
    return (bits & 0x80000000UL) != 0 ? -(long)magnitude : (long)magnitude;
}

static long gdi_sine(long angle)
{
    long position;
    long first, second;
    int negative;
    unsigned int degree, fraction;
    angle %= 360L * 256L;
    if (angle < 0) angle += 360L * 256L;
    negative = angle >= 180L * 256L;
    if (negative) angle -= 180L * 256L;
    position = angle > 90L * 256L ? 180L * 256L - angle : angle;
    degree = (unsigned int)(position / 256L);
    fraction = (unsigned int)(position % 256L);
    first = gdi_sine_quadrant[degree];
    second = degree == 90 ? first : gdi_sine_quadrant[degree + 1];
    first = (first * (256U - fraction) + second * fraction) / 256L;
    return negative ? -first : first;
}

static void gdi_set_transform(void)
{
    long ax, ay, sx, cx, sy, cy;
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
    else
    {
        ax = gdi_float_fixed(&drv_rot_x, 8);
        ay = gdi_float_fixed(&drv_rot_y, 8);
        sx = gdi_sine(ax); cx = gdi_sine(ax + 90L * 256L);
        sy = gdi_sine(ay); cy = gdi_sine(ay + 90L * 256L);
        gdi_fixed_matrix[0][0] = sy;
        gdi_fixed_matrix[0][1] = cy;
        gdi_fixed_matrix[0][2] = 0;
        gdi_fixed_matrix[1][0] = -sx * cy / GDI_Q14;
        gdi_fixed_matrix[1][1] = sx * sy / GDI_Q14;
        gdi_fixed_matrix[1][2] = cx;
        gdi_fixed_matrix[2][0] = cx * cy / GDI_Q14;
        gdi_fixed_matrix[2][1] = -cx * sy / GDI_Q14;
        gdi_fixed_matrix[2][2] = sx;
    }
}

/* a * numerator / denominator, with |numerator| <= |denominator|.
 * Screen clipping can otherwise overflow a 32-bit long intermediate.
 * Bounds here are below 2^29, so remainder*2+numerator also fits. */
static long gdi_mul_ratio(long a, long numerator, long denominator)
{
    unsigned long value, num, den, remainder, result, bit;
    int negative;
    if (denominator == 0) return 0;
    negative = (a < 0) != ((numerator < 0) != (denominator < 0));
    value = (unsigned long)(a < 0 ? -a : a);
    num = (unsigned long)(numerator < 0 ? -numerator : numerator);
    den = (unsigned long)(denominator < 0 ? -denominator : denominator);
    result = remainder = 0;
    for (bit = 0x40000000UL; bit != 0; bit >>= 1)
    {
        result <<= 1;
        remainder <<= 1;
        if ((value & bit) != 0) remainder += num;
        if (remainder >= den)
        {
            result += remainder / den;
            remainder %= den;
        }
    }
    return negative ? -(long)result : (long)result;
}

static int gdi_clip_fixed_z(const Gdi_fixed_point *in, int count,
                            Gdi_fixed_point *out, long plane, int greater)
{
    int i, total, a_inside, b_inside;
    Gdi_fixed_point a, b, point;
    total = 0;
    a = in[count - 1];
    a_inside = greater ? a.z >= plane : a.z <= plane;
    for (i = 0; i < count; ++i)
    {
        b = in[i];
        b_inside = greater ? b.z >= plane : b.z <= plane;
        if (a_inside != b_inside)
        {
            point.x = a.x + gdi_mul_ratio(b.x - a.x, plane - a.z, b.z - a.z);
            point.y = a.y + gdi_mul_ratio(b.y - a.y, plane - a.z, b.z - a.z);
            point.z = plane;
            out[total++] = point;
        }
        if (b_inside) out[total++] = b;
        a = b;
        a_inside = b_inside;
    }
    return total;
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
            out[total++] = point;
        }
        if (b_inside) out[total++] = b;
        a = b;
        a_inside = b_inside;
    }
    return total;
}

static int gdi_clip_screen(const Gdi_screen_point *in, int count,
                           Gdi_screen_point *out, int axis, long edge, int greater)
{
    int i, total, a_inside, b_inside;
    long av, bv;
    Gdi_screen_point a, b, point;
    total = 0;
    a = in[count - 1];
    av = axis ? a.y : a.x;
    a_inside = greater ? av >= edge : av <= edge;
    for (i = 0; i < count; ++i)
    {
        b = in[i];
        bv = axis ? b.y : b.x;
        b_inside = greater ? bv >= edge : bv <= edge;
        if (a_inside != b_inside)
        {
            point.x = a.x + gdi_mul_ratio(b.x - a.x, edge - av, bv - av);
            point.y = a.y + gdi_mul_ratio(b.y - a.y, edge - av, bv - av);
            if (axis) point.y = edge;
            else point.x = edge;
            out[total++] = point;
        }
        if (b_inside) out[total++] = b;
        a = b;
        av = bv;
        a_inside = b_inside;
    }
    return total;
}

static int gdi_prepare_polygon(unsigned int first, unsigned int second,
                               unsigned int third, POINT *destination)
{
    Gdi_fixed_point fixed_a[GDI_MAX_POLY], fixed_b[GDI_MAX_POLY];
    Gdi_float_point float_a[GDI_MAX_POLY], float_b[GDI_MAX_POLY];
    Gdi_screen_point screen_a[GDI_MAX_POLY], screen_b[GDI_MAX_POLY];
    long focal, area;
    float float_focal;
    int i, count;
    if (gdi_has_fpu)
    {
        float_a[0] = gdi_float_points[first];
        float_a[1] = gdi_float_points[second];
        float_a[2] = gdi_float_points[third];
        count = gdi_clip_float_z(float_a, 3, float_b, -1.0f, FALSE);
        if (count < 3) return 0;
        count = gdi_clip_float_z(float_b, count, float_a, -10000.0f, TRUE);
        if (count < 3) return 0;
        float_focal = (float)drv_win_height * 1.2071067811865475f;
        for (i = 0; i < count; ++i)
        {
            screen_a[i].x = (long)((float)drv_win_width / 2.0f +
                                    float_a[i].x * float_focal / -float_a[i].z);
            screen_a[i].y = (long)((float)drv_win_height / 2.0f -
                                    float_a[i].y * float_focal / -float_a[i].z);
        }
    }
    else
    {
        fixed_a[0] = gdi_fixed_points[first];
        fixed_a[1] = gdi_fixed_points[second];
        fixed_a[2] = gdi_fixed_points[third];
        count = gdi_clip_fixed_z(fixed_a, 3, fixed_b, -GDI_COORD_SCALE, FALSE);
        if (count < 3) return 0;
        count = gdi_clip_fixed_z(fixed_b, count, fixed_a,
                                 -10000L * GDI_COORD_SCALE, TRUE);
        if (count < 3) return 0;
        focal = (long)drv_win_height * GDI_FOCAL_Q14 / GDI_Q14;
        for (i = 0; i < count; ++i)
        {
            screen_a[i].x = (long)drv_win_width / 2L +
                            fixed_a[i].x * focal / -fixed_a[i].z;
            screen_a[i].y = (long)drv_win_height / 2L -
                            fixed_a[i].y * focal / -fixed_a[i].z;
        }
    }
    count = gdi_clip_screen(screen_a, count, screen_b, 0, 0, TRUE);
    if (count < 3) return 0;
    count = gdi_clip_screen(screen_b, count, screen_a, 0,
                            (long)drv_win_width, FALSE);
    if (count < 3) return 0;
    count = gdi_clip_screen(screen_a, count, screen_b, 1, 0, TRUE);
    if (count < 3) return 0;
    count = gdi_clip_screen(screen_b, count, screen_a, 1,
                            (long)drv_win_height, FALSE);
    if (count < 3) return 0;
    /* OpenGL's CCW front faces are clockwise in Windows' downward Y. */
    area = 0;
    for (i = 1; i < count - 1 && area == 0; ++i)
        area = (screen_a[i].x - screen_a[0].x) *
               (screen_a[i + 1].y - screen_a[0].y) -
               (screen_a[i].y - screen_a[0].y) *
               (screen_a[i + 1].x - screen_a[0].x);
    if (area >= 0) return 0;
    for (i = 0; i < count; ++i)
    {
        destination[i].x = (int)screen_a[i].x;
        destination[i].y = (int)screen_a[i].y;
    }
    return count;
}

static void gdi_draw_strip_packet(unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  const unsigned char TG_HUGE *facet_colors)
{
    unsigned int i, color;
    int row, facet, total, points, j;
    long source[3];
    float fsource[3];
    HBRUSH previous;
    for (i = 0; i < count; ++i)
    {
        if (gdi_has_fpu)
        {
            fsource[0] = vertices[i].x;
            fsource[1] = vertices[i].y;
            fsource[2] = vertices[i].z;
            for (row = 0; row < 3; ++row)
            {
                float transformed = gdi_float_matrix[row][0] * fsource[0] +
                                    gdi_float_matrix[row][1] * fsource[1] +
                                    gdi_float_matrix[row][2] * fsource[2];
                if (row == 0) gdi_float_points[i].x = transformed;
                if (row == 1) gdi_float_points[i].y = transformed;
                if (row == 2) gdi_float_points[i].z = transformed - 3000.0f;
            }
        }
        else
        {
            source[0] = gdi_float_fixed(&vertices[i].x, 4);
            source[1] = gdi_float_fixed(&vertices[i].y, 4);
            source[2] = gdi_float_fixed(&vertices[i].z, 4);
            /* The application's radius is 1000. With Q4 input coordinates
             * each matrix sum is < 3*16000*16384 = 786432000 (32-bit safe). */
            if (source[0] < -16000L || source[0] > 16000L ||
                source[1] < -16000L || source[1] > 16000L ||
                source[2] < -16000L || source[2] > 16000L)
                gdi_error("GDI fixed-point path supports globe radius up to 1000");
            for (row = 0; row < 3; ++row)
            {
                long transformed = (gdi_fixed_matrix[row][0] * source[0] +
                                    gdi_fixed_matrix[row][1] * source[1] +
                                    gdi_fixed_matrix[row][2] * source[2]) / GDI_Q14;
                if (row == 0) gdi_fixed_points[i].x = transformed;
                if (row == 1) gdi_fixed_points[i].y = transformed;
                if (row == 2) gdi_fixed_points[i].z = transformed - 3000L * GDI_COORD_SCALE;
            }
        }
    }
    for (color = 0; color < 256; ++color) gdi_color_head[color] = -1;
    for (i = 0; i < count - 2; ++i)
    {
        if ((i & 1) != 0)
            gdi_polygon_counts[i] = gdi_prepare_polygon(i + 1, i, i + 2,
                                                        gdi_polygons[i]);
        else
            gdi_polygon_counts[i] = gdi_prepare_polygon(i, i + 1, i + 2,
                                                        gdi_polygons[i]);
        if (gdi_polygon_counts[i] == 0) continue;
        color = facet_colors[i];
        gdi_color_next[i] = gdi_color_head[color];
        gdi_color_head[color] = (int)i;
        ++globe_tris_num_vis;
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
        previous = (HBRUSH)SelectObject(gdi_buffer, gdi_brush(color));
        if (!PolyPolygon(gdi_buffer, gdi_batch_points, gdi_batch_counts, total))
            gdi_error("GDI PolyPolygon failed");
        SelectObject(gdi_buffer, previous);
    }
}

void drv_draw_tristrip_flat_color(unsigned int count,
                                 const Vertex TG_HUGE *vertices,
                                 const unsigned char TG_HUGE *facet_colors)
{
    unsigned int first, facets;
    if (count < 3 || vertices == NULL || facet_colors == NULL || gdi_buffer == 0)
        return;
    /* A strip's facet i uses vertices i..i+2. Retain those two shared
     * vertices at each boundary and advance by an even number of facets
     * so every packet begins with the original winding. */
    first = 0;
    while (first < count - 2U)
    {
        facets = count - 2U - first;
        if (facets > GDI_MAX_FACETS) facets = GDI_MAX_FACETS;
        gdi_draw_strip_packet(facets + 2U, vertices + first,
                               facet_colors + first);
        first += facets;
    }
}

void drv_draw_osd(void)
{
    char status[160];
    RECT status_bounds, help_bounds;
    int help_height;
    UINT flags;
    if (drv_win_width <= 16 || drv_win_height <= 16) return;
    drv_format_stats(status);
    strcat(status, gdi_has_fpu ? ", 80387 math" : ", fixed-point math");
    SetBkMode(gdi_buffer, TRANSPARENT);
    SetTextColor(gdi_buffer, PALETTEINDEX(gdi_foreground));
    flags = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX;
    help_bounds.left = 8;
    help_bounds.right = (int)drv_win_width - 8;
    help_bounds.top = help_bounds.bottom = 0;
    DrawText(gdi_buffer, drv_help, -1, &help_bounds, flags | DT_CALCRECT);
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
        DrawText(gdi_buffer, status, -1, &status_bounds, flags);
    if (help_bounds.bottom > help_bounds.top)
        DrawText(gdi_buffer, drv_help, -1, &help_bounds, flags);
}

void drv_render(void)
{
    RECT bounds;
    HPEN previous;
    if (gdi_buffer == 0 || globe_verts_num == 0) return;
    drv_fps_start();
    bounds.left = bounds.top = 0;
    bounds.right = (int)drv_win_width;
    bounds.bottom = (int)drv_win_height;
    FillRect(gdi_buffer, &bounds, gdi_brush(gdi_background));
    previous = (HPEN)SelectObject(gdi_buffer, GetStockObject(NULL_PEN));
    SetPolyFillMode(gdi_buffer, WINDING);
    gdi_set_transform();
    globe_tris_num_vis = 0;
    globe_draw_tripstrip();
    SelectObject(gdi_buffer, previous);
    if (drv_osd_enabled) drv_draw_osd();
    drv_fps_stop();
    gdi_dirty = FALSE;
}

static void gdi_present(HDC dc)
{
    HPALETTE previous;
    if (gdi_buffer == 0) return;
    previous = SelectPalette(dc, gdi_palette, !gdi_palette_foreground);
    RealizePalette(dc);
    BitBlt(dc, 0, 0, (int)drv_win_width, (int)drv_win_height,
            gdi_buffer, 0, 0, SRCCOPY);
    SelectPalette(dc, previous, TRUE);
}

static void gdi_keyboard(unsigned char key)
{
    switch (key)
    {
        case 27: drv_close(); return;
        case '1': case '2': case '3': case '4': case '5':
            globe_toggle_res((char)key);
            break;
        case 'a': case 'A':
            drv_auto_enabled = !drv_auto_enabled;
            gdi_auto_tick = GetTickCount();
            break;
        case 'o': case 'O': drv_osd_enabled = !drv_osd_enabled; break;
        default: return;
    }
    gdi_dirty = TRUE;
    InvalidateRect(gdi_window, NULL, FALSE);
}

static void gdi_mouse_move(int x, int y)
{
    if (drv_lmouse_pressed)
    {
        drv_rot_y += (float)((long)x - gdi_last_x) * 100.0f / drv_win_width;
        drv_rot_x += (float)((long)y - gdi_last_y) * 100.0f / drv_win_height;
        while (drv_rot_x < 0) drv_rot_x += 360.0f;
        while (drv_rot_x >= 360.0f) drv_rot_x -= 360.0f;
        while (drv_rot_y < 0) drv_rot_y += 360.0f;
        while (drv_rot_y >= 360.0f) drv_rot_y -= 360.0f;
        gdi_dirty = TRUE;
        InvalidateRect(gdi_window, NULL, FALSE);
    }
    gdi_last_x = x;
    gdi_last_y = y;
}

LONG FAR PASCAL gdi_window_proc(HWND window, UINT message,
                                 WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT paint;
    HDC dc;
    DWORD tick, elapsed;
    switch (message)
    {
        case WM_PAINT:
            dc = BeginPaint(window, &paint);
            if (gdi_ready && gdi_dirty) drv_render();
            if (gdi_ready) gdi_present(dc);
            EndPaint(window, &paint);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
                drv_resize((int)(unsigned short)LOWORD(lparam),
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
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            gdi_mouse_move((int)(short)LOWORD(lparam), (int)(short)HIWORD(lparam));
            return 0;
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
            drv_lmouse_pressed = FALSE;
            ReleaseCapture();
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
            if (wparam == GDI_TIMER && drv_auto_enabled && gdi_ready)
            {
                tick = GetTickCount();
                elapsed = (tick - gdi_auto_tick) & 0xffffffffUL;
                gdi_auto_tick = tick;
                /* Include timer wait time, not only the duration of GDI calls. */
                drv_rot_y += (float)elapsed * (DRV_ROTATE / 1000.0f);
                while (drv_rot_y >= 360.0f) drv_rot_y -= 360.0f;
                gdi_dirty = TRUE;
                InvalidateRect(window, NULL, FALSE);
            }
            return 0;
        case WM_CLOSE: drv_close(); return 0;
        case WM_DESTROY:
            KillTimer(window, GDI_TIMER);
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
