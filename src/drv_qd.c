/*
 * testglobe - System 7 QuickDraw driver for the MPW 68000 code model.
 * Copyright (C) 2024-2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * QuickDraw fills the projected polygons; this driver handles the 3D
 * pipeline. The mesh is a convex globe, so its front facets do not overlap
 * and need no general-purpose z-buffer. Optional 68881 code is isolated in
 * qd_fpu.c and is never called on the baseline 68000.
 */
#include "testglob.h"
#ifdef DRV_QD
#include <Types.h>
#include <QuickDraw.h>
#include <Windows.h>
#include <QDOffscreen.h>
#include <Gestalt.h>
#include <Events.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <Palettes.h>
#include <stdlib.h>
#include <stdio.h>
#include "drv.h"
#include "drv_ext.h"
#include "globe.h"
#include "glob_ext.h"
#include "qd_math.h"
#if defined(__SC__) || defined(applec)
#pragma segment QuickDraw
#endif

#define QD_MAX_VERTICES 130
#define QD_MAX_POLY 10
#define QD_Q14 16384L
#define QD_COORD_SCALE 16L
#define QD_FOCAL_Q14 19777L
#define QD_FILE_MENU 128

typedef struct Qd_fixed_point { long x, y, z; } Qd_fixed_point;
QDGlobals qd;
static WindowPtr qd_window;
static GDHandle qd_device;
static GWorldPtr qd_world;
static CTabHandle qd_colors;
static PaletteHandle qd_palette;
static PixPatHandle qd_pattern;
static int qd_pattern_color = -1;
static MenuHandle qd_file_menu;
static int qd_color_available;
static int qd_world_available;
static int qd_has_fpu;
static int qd_depth;
static int qd_ready;
static int qd_running;
static int qd_active;
static int qd_dirty = TRUE;
static int qd_old_x, qd_old_y;
static unsigned long qd_auto_tick;
static Rect qd_screen;
static long qd_fixed_matrix[3][3];
static Qd_fixed_point qd_fixed_points[QD_MAX_VERTICES];
static const unsigned short qd_sine_quadrant[91] = {
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


static void qd_pascal(Str255 result, const char *text)
{
    size_t length;
    length = strlen(text);
    if (length > 255) length = 255;
    result[0] = (unsigned char)length;
    memcpy(result + 1, text, length);
}

static void qd_dispose_buffer(void)
{
    if (qd_world != NULL) DisposeGWorld(qd_world);
    qd_world = NULL;
    qd_pattern_color = -1;
}

static void qd_cleanup(void)
{
    qd_dispose_buffer();
    if (qd_pattern != NULL) DisposePixPat(qd_pattern);
    qd_pattern = NULL;
    if (qd_palette != NULL)
    {
        if (qd_window != NULL) SetPalette(qd_window, NULL, FALSE);
        DisposePalette(qd_palette);
    }
    qd_palette = NULL;
    if (qd_colors != NULL) DisposeHandle((Handle)qd_colors);
    qd_colors = NULL;
    if (qd_window != NULL) DisposeWindow(qd_window);
    qd_window = NULL;
    if (qd_file_menu != NULL)
    {
        DeleteMenu(QD_FILE_MENU);
        DisposeMenu(qd_file_menu);
    }
    qd_file_menu = NULL;
    qd_ready = FALSE;
    globe_free();
}

void qd_error(char *message)
{
    Str255 text;
    static const unsigned char empty[] = "";
    qd_pascal(text, message);
    ParamText(text, empty, empty, empty);
    StopAlert(128, NULL);
    exit(1);
}

void drv_close(void)
{
    qd_running = FALSE;
}

void drv_init(int *argc, char **argv)
{
    long quickdraw_version, processor, fpu;
    static const unsigned char file_title[] = "\004File";
    static const unsigned char quit_item[] = "\006Quit/Q";
    (void)argc; (void)argv;
    MaxApplZone();
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    if (atexit(qd_cleanup) != 0) qd_error("Cannot register QuickDraw cleanup");
    quickdraw_version = processor = fpu = 0;
    qd_color_available = Gestalt(gestaltQuickdrawVersion, &quickdraw_version) == noErr &&
                         quickdraw_version >= gestalt8BitQD;
    qd_world_available = qd_color_available && quickdraw_version >= gestalt32BitQD;
    qd_has_fpu = Gestalt(gestaltProcessorType, &processor) == noErr &&
                 processor >= gestalt68020 &&
                 Gestalt(gestaltFPUType, &fpu) == noErr &&
                 (fpu == gestalt68881 || fpu == gestalt68882 ||
                  fpu == gestalt68040FPU);
    qd_file_menu = NewMenu(QD_FILE_MENU, file_title);
    if (qd_file_menu == NULL) qd_error("Cannot create QuickDraw File menu");
    ClearMenuBar();
    AppendMenu(qd_file_menu, quit_item);
    InsertMenu(qd_file_menu, 0);
    DrawMenuBar();
    qd_running = TRUE;
    qd_active = TRUE;
}

static void qd_display_info(void)
{
    GDHandle device;
    int depth;
    qd_depth = 1;
    qd_device = NULL;
    qd_screen = qd.screenBits.bounds;
    if (qd_color_available)
    {
        for (device = GetDeviceList(); device != NULL; device = GetNextDevice(device))
        {
            if (!TestDeviceAttribute(device, screenDevice) ||
                !TestDeviceAttribute(device, screenActive)) continue;
            depth = (**(**device).gdPMap).pixelSize;
            if (qd_device == NULL || depth > qd_depth)
            {
                qd_device = device;
                qd_depth = depth;
                qd_screen = (**device).gdRect;
            }
        }
    }
    drv_bits_per_pixel = (unsigned int)qd_depth;
    drv_bytes_per_pixel = (unsigned int)((qd_depth + 7) / 8);
    /* Color QuickDraw stores 5:5:5 RGB in 16 bits and 24 RGB bits in 32. */
    drv_colors = qd_depth == 32 ? 16777216UL :
                 (qd_depth == 16 ? 32768UL : 1UL << qd_depth);
    drv_colormapped = qd_depth <= 8;
    drv_colormap_bits = qd_depth <= 8 ? (unsigned int)qd_depth : 24;
    drv_colormap_colors = 1UL << drv_colormap_bits;
}

static void qd_make_palette(void)
{
    int i;
    qd_colors = (CTabHandle)NewHandle((Size)(sizeof(ColorTable) + 255L * sizeof(ColorSpec)));
    if (qd_colors == NULL) qd_error("Cannot allocate Earth color table");
    (**qd_colors).ctSeed = GetCTSeed();
    (**qd_colors).ctFlags = 0;
    (**qd_colors).ctSize = 255;
    for (i = 0; i < 256; ++i)
    {
        (**qd_colors).ctTable[i].value = (short)i;
        (**qd_colors).ctTable[i].rgb.red = (unsigned short)(earth_cmap[i][0] * 257U);
        (**qd_colors).ctTable[i].rgb.green = (unsigned short)(earth_cmap[i][1] * 257U);
        (**qd_colors).ctTable[i].rgb.blue = (unsigned short)(earth_cmap[i][2] * 257U);
    }
    qd_palette = NewPalette(256, qd_colors, pmTolerant, 0);
    if (qd_palette == NULL) qd_error("Cannot allocate Earth palette");
    SetPalette(qd_window, qd_palette, TRUE);
    ActivatePalette(qd_window);
}

static void qd_make_buffer(void)
{
    Rect bounds;
    qd_dispose_buffer();
    if (!qd_world_available) return;
    SetRect(&bounds, 0, 0, (short)drv_win_width, (short)drv_win_height);
    /* This is ordinary offscreen pixel storage, not a special RAM feature.
     * NewGWorld lets the installed QuickDraw implementation choose storage. */
    if (NewGWorld(&qd_world, (short)qd_depth, &bounds,
                  qd_depth == 8 ? qd_colors : NULL, qd_device, 0) != noErr)
        qd_world = NULL; /* Low-memory systems still draw in the window. */
}

void drv_resize(int width, int height)
{
    if (width < 1 || height < 1) return;
    if (width > 32767 || height > 32767) qd_error("QuickDraw window is too large");
    drv_win_width = (unsigned int)width;
    drv_win_height = (unsigned int)height;
    if (qd_window != NULL) qd_make_buffer();
    qd_dirty = TRUE;
}

int drv_init_window(int in_x, int in_y, int in_width, int in_height)
{
    Rect bounds;
    Str255 title;
    int available_width, available_height;
    qd_display_info();
    available_width = qd_screen.right - qd_screen.left - 40;
    available_height = qd_screen.bottom - qd_screen.top - 80;
    if (in_width > 0) drv_win_width = (unsigned int)in_width;
    if (in_height > 0) drv_win_height = (unsigned int)in_height;
    if ((int)drv_win_width > available_width) drv_win_width = (unsigned int)available_width;
    if ((int)drv_win_height > available_height) drv_win_height = (unsigned int)available_height;
    if (drv_win_width < 64 || drv_win_height < 64) qd_error("Screen is too small for testglobe");
    drv_win_x = (unsigned int)(in_x > 0 ? in_x : qd_screen.left + 20);
    drv_win_y = (unsigned int)(in_y > 0 ? in_y : qd_screen.top + 40);
    SetRect(&bounds, (short)drv_win_x, (short)drv_win_y,
             (short)(drv_win_x + drv_win_width), (short)(drv_win_y + drv_win_height));
    qd_pascal(title, TESTGLOBE_NAME);
    if (qd_color_available)
        qd_window = NewCWindow(NULL, &bounds, title, TRUE,
                               documentProc, (WindowPtr)-1L, TRUE, 0L);
    else
        qd_window = NewWindow(NULL, &bounds, title, TRUE,
                              documentProc, (WindowPtr)-1L, TRUE, 0L);
    if (qd_window == NULL) qd_error("Cannot create QuickDraw window");
    SetPort((GrafPtr)qd_window);
    if (qd_color_available && qd_depth <= 8) qd_make_palette();
    qd_make_buffer();
    return 1;
}

/* IEEE single decoding uses integer operations. The byte-order probe is
 * constant on native big-endian 68000 and little-endian host tests alike. */
static long qd_float_fixed(const float *value, int fraction_bits)
{
    const unsigned char *bytes;
    const unsigned short endian = 1;
    unsigned long bits, magnitude;
    int exponent, shift;
    bytes = (const unsigned char *)value;
    if (*(const unsigned char *)&endian)
        bits = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
               ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
    else
        bits = (unsigned long)bytes[3] | ((unsigned long)bytes[2] << 8) |
               ((unsigned long)bytes[1] << 16) | ((unsigned long)bytes[0] << 24);
    exponent = (int)((bits >> 23) & 255UL);
    if (exponent < 127 - fraction_bits) return 0;
    if (exponent > 157 - fraction_bits) qd_error("QuickDraw coordinate is out of range");
    magnitude = (bits & 0x7fffffUL) | 0x800000UL;
    shift = exponent - 150 + fraction_bits;
    if (shift < 0) magnitude >>= -shift;
    else magnitude <<= shift;
    return (bits & 0x80000000UL) != 0 ? -(long)magnitude : (long)magnitude;
}
static long qd_sine(long angle)
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
    first = qd_sine_quadrant[degree];
    second = degree == 90 ? first : qd_sine_quadrant[degree + 1];
    first = (first * (256U - fraction) + second * fraction) / 256L;
    return negative ? -first : first;
}


static void qd_set_transform(void)
{
    long ax, ay, sx, cx, sy, cy;
    if (qd_has_fpu)
    {
        qd_fpu_prepare(&drv_rot_x, &drv_rot_y);
        return;
    }
    ax = qd_float_fixed(&drv_rot_x, 8);
    ay = qd_float_fixed(&drv_rot_y, 8);
    sx = qd_sine(ax); cx = qd_sine(ax + 90L * 256L);
    sy = qd_sine(ay); cy = qd_sine(ay + 90L * 256L);
    qd_fixed_matrix[0][0] = sy;
    qd_fixed_matrix[0][1] = cy;
    qd_fixed_matrix[0][2] = 0;
    qd_fixed_matrix[1][0] = -sx * cy / QD_Q14;
    qd_fixed_matrix[1][1] = sx * sy / QD_Q14;
    qd_fixed_matrix[1][2] = cx;
    qd_fixed_matrix[2][0] = cx * cy / QD_Q14;
    qd_fixed_matrix[2][1] = -cx * sy / QD_Q14;
    qd_fixed_matrix[2][2] = sx;
}
static long qd_mul_ratio(long a, long numerator, long denominator)
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

static int qd_clip_fixed_z(const Qd_fixed_point *in, int count,
                            Qd_fixed_point *out, long plane, int greater)
{
    int i, total, a_inside, b_inside;
    Qd_fixed_point a, b, point;
    total = 0;
    a = in[count - 1];
    a_inside = greater ? a.z >= plane : a.z <= plane;
    for (i = 0; i < count; ++i)
    {
        b = in[i];
        b_inside = greater ? b.z >= plane : b.z <= plane;
        if (a_inside != b_inside)
        {
            point.x = a.x + qd_mul_ratio(b.x - a.x, plane - a.z, b.z - a.z);
            point.y = a.y + qd_mul_ratio(b.y - a.y, plane - a.z, b.z - a.z);
            point.z = plane;
            out[total++] = point;
        }
        if (b_inside) out[total++] = b;
        a = b;
        a_inside = b_inside;
    }
    return total;
}

static int qd_clip_screen(const Qd_screen_point *in, int count,
                           Qd_screen_point *out, int axis, long edge, int greater)
{
    int i, total, a_inside, b_inside;
    long av, bv;
    Qd_screen_point a, b, point;
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
            point.x = a.x + qd_mul_ratio(b.x - a.x, edge - av, bv - av);
            point.y = a.y + qd_mul_ratio(b.y - a.y, edge - av, bv - av);
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


static int qd_prepare_polygon(unsigned int first, unsigned int second,
                               unsigned int third, Point *destination)
{
    Qd_fixed_point fixed_a[QD_MAX_POLY], fixed_b[QD_MAX_POLY];
    Qd_screen_point screen_a[QD_MAX_POLY], screen_b[QD_MAX_POLY];
    long focal, area;
    int i, count;
    if (qd_has_fpu)
    {
        count = qd_fpu_project(first, second, third,
                               drv_win_width, drv_win_height, screen_a);
        if (count < 3) return 0;
    }
    else
    {
        fixed_a[0] = qd_fixed_points[first];
        fixed_a[1] = qd_fixed_points[second];
        fixed_a[2] = qd_fixed_points[third];
        count = qd_clip_fixed_z(fixed_a, 3, fixed_b, -QD_COORD_SCALE, FALSE);
        if (count < 3) return 0;
        count = qd_clip_fixed_z(fixed_b, count, fixed_a,
                                 -10000L * QD_COORD_SCALE, TRUE);
        if (count < 3) return 0;
        focal = (long)drv_win_height * QD_FOCAL_Q14 / QD_Q14;
        for (i = 0; i < count; ++i)
        {
            screen_a[i].x = (long)drv_win_width / 2L + fixed_a[i].x * focal / -fixed_a[i].z;
            screen_a[i].y = (long)drv_win_height / 2L - fixed_a[i].y * focal / -fixed_a[i].z;
        }
    }
    count = qd_clip_screen(screen_a, count, screen_b, 0, 0, TRUE);
    if (count < 3) return 0;
    count = qd_clip_screen(screen_b, count, screen_a, 0, (long)drv_win_width, FALSE);
    if (count < 3) return 0;
    count = qd_clip_screen(screen_a, count, screen_b, 1, 0, TRUE);
    if (count < 3) return 0;
    count = qd_clip_screen(screen_b, count, screen_a, 1, (long)drv_win_height, FALSE);
    if (count < 3) return 0;
    area = 0;
    for (i = 1; i < count - 1 && area == 0; ++i)
        area = (screen_a[i].x - screen_a[0].x) * (screen_a[i + 1].y - screen_a[0].y) -
               (screen_a[i].y - screen_a[0].y) * (screen_a[i + 1].x - screen_a[0].x);
    if (area >= 0) return 0; /* QuickDraw Y points downward. */
    for (i = 0; i < count; ++i)
    {
        destination[i].h = (short)screen_a[i].x;
        destination[i].v = (short)screen_a[i].y;
    }
    return count;
}

static void qd_flat_color(PolyHandle polygon, unsigned char index)
{
    RGBColor rgb;
    Pattern pattern;
    unsigned char *rows;
    unsigned int luminance;
    int x, y;
    static const unsigned char bayer[8][8] = {
        {0,48,12,60,3,51,15,63}, {32,16,44,28,35,19,47,31},
        {8,56,4,52,11,59,7,55}, {40,24,36,20,43,27,39,23},
        {2,50,14,62,1,49,13,61}, {34,18,46,30,33,17,45,29},
        {10,58,6,54,9,57,5,53}, {42,26,38,22,41,25,37,21}
    };
    if (qd_depth > 1)
    {
        rgb.red = (unsigned short)(earth_cmap[index][0] * 257U);
        rgb.green = (unsigned short)(earth_cmap[index][1] * 257U);
        rgb.blue = (unsigned short)(earth_cmap[index][2] * 257U);
        if (qd_depth <= 8)
        {
            if (qd_pattern == NULL)
            {
                qd_pattern = NewPixPat();
                if (qd_pattern == NULL) qd_error("Cannot allocate QuickDraw color pattern");
            }
            if (qd_pattern_color != index)
            {
                MakeRGBPat(qd_pattern, &rgb);
                qd_pattern_color = index;
            }
            /* Color QuickDraw dithers this RGB pattern at low pixel depths. */
            FillCPoly(polygon, qd_pattern);
        }
        else
        {
            RGBForeColor(&rgb);
            PaintPoly(polygon);
        }
    }
    else
    {
        luminance = (unsigned int)(((unsigned long)earth_cmap[index][0] * 77UL +
                                    (unsigned long)earth_cmap[index][1] * 150UL +
                                    (unsigned long)earth_cmap[index][2] * 29UL + 128UL) / 256UL);
        rows = (unsigned char *)&pattern;
        for (y = 0; y < 8; ++y)
        {
            rows[y] = 0;
            for (x = 0; x < 8; ++x)
                if (luminance < (unsigned int)bayer[y][x] * 4U + 2U)
                    rows[y] |= (unsigned char)(0x80U >> x);
        }
        ForeColor(blackColor);
        BackColor(whiteColor);
        FillPoly(polygon, &pattern);
    }
}

void drv_draw_tristrip_flat_color(unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  const unsigned char TG_HUGE *facet_colors)
{
    Point projected[QD_MAX_POLY];
    unsigned int i;
    int row, j, total;
    long source[3], transformed;
    PolyHandle polygon;
    if (count < 3 || vertices == NULL || facet_colors == NULL) return;
    if (count > QD_MAX_VERTICES) qd_error("QuickDraw strip exceeds Earth-64 size");
    if (qd_has_fpu) qd_fpu_transform(count, vertices);
    else
    {
        for (i = 0; i < count; ++i)
        {
            source[0] = qd_float_fixed(&vertices[i].x, 4);
            source[1] = qd_float_fixed(&vertices[i].y, 4);
            source[2] = qd_float_fixed(&vertices[i].z, 4);
            if (source[0] < -16000L || source[0] > 16000L ||
                source[1] < -16000L || source[1] > 16000L ||
                source[2] < -16000L || source[2] > 16000L)
                qd_error("QuickDraw fixed path supports radius up to 1000");
            /* Q4 coordinates * Q14 matrix: each sum is below 786432000. */
            for (row = 0; row < 3; ++row)
            {
                transformed = (qd_fixed_matrix[row][0] * source[0] +
                               qd_fixed_matrix[row][1] * source[1] +
                               qd_fixed_matrix[row][2] * source[2]) / QD_Q14;
                if (row == 0) qd_fixed_points[i].x = transformed;
                if (row == 1) qd_fixed_points[i].y = transformed;
                if (row == 2) qd_fixed_points[i].z = transformed - 48000L;
            }
        }
    }
    for (i = 0; i < count - 2; ++i)
    {
        total = (i & 1) ? qd_prepare_polygon(i + 1, i, i + 2, projected) :
                          qd_prepare_polygon(i, i + 1, i + 2, projected);
        if (total == 0) continue;
        polygon = OpenPoly();
        if (polygon == NULL) qd_error("Cannot allocate QuickDraw polygon");
        MoveTo(projected[0].h, projected[0].v);
        for (j = 1; j < total; ++j) LineTo(projected[j].h, projected[j].v);
        LineTo(projected[0].h, projected[0].v);
        ClosePoly();
        qd_flat_color(polygon, facet_colors[i]);
        KillPoly(polygon);
        ++globe_tris_num_vis;
    }
}

void drv_draw_osd(void)
{
    char status[160];
    RGBColor white;
    if (qd_depth > 1)
    {
        white.red = white.green = white.blue = 65535U;
        RGBForeColor(&white);
    }
    else ForeColor(blackColor);
    TextFont(0);
    TextSize(9);
    TextMode(srcCopy);
    sprintf(status, "%lu triangles (%lu visible), %s, %.1f frames per second",
             globe_tris_num, globe_tris_num_vis,
             qd_has_fpu ? "68881 math" : "integer math", (double)drv_fps_average);
    MoveTo(8, 14);
    DrawText(status, 0, (short)strlen(status));
    MoveTo(8, (short)drv_win_height - 8);
    DrawText(drv_help, 0, (short)strlen(drv_help));
}

static void qd_present(void)
{
    Rect bounds;
    if (qd_world == NULL) return;
    SetPort((GrafPtr)qd_window);
    SetRect(&bounds, 0, 0, (short)drv_win_width, (short)drv_win_height);
    /* Color QuickDraw's copy mode performs final device-depth conversion. */
    CopyBits((const BitMap *)*qd_world->portPixMap, &qd_window->portBits,
              &bounds, &bounds, srcCopy | ditherCopy, NULL);
}

void drv_render(void)
{
    GrafPtr saved;
    CGrafPtr old_world;
    GDHandle old_device;
    GDHandle saved_device;
    PixMapHandle pixels;
    Rect bounds;
    RGBColor background;
    int buffered;
    if (qd_window == NULL || globe_verts_num == 0) return;
    drv_fps_start();
    GetPort(&saved);
    saved_device = NULL;
    if (qd_color_available)
    {
        saved_device = GetGDevice();
        if (qd_device != NULL) SetGDevice(qd_device);
    }
    buffered = FALSE;
    pixels = NULL;
    if (qd_world != NULL)
    {
        pixels = qd_world->portPixMap;
        if (LockPixels(pixels))
        {
            GetGWorld(&old_world, &old_device);
            SetGWorld(qd_world, NULL);
            buffered = TRUE;
        }
        else qd_dispose_buffer();
    }
    if (!buffered) SetPort((GrafPtr)qd_window);
    SetRect(&bounds, 0, 0, (short)drv_win_width, (short)drv_win_height);
    if (qd_depth > 1)
    {
        background.red = background.green = background.blue = 0;
        RGBBackColor(&background);
    }
    else BackColor(whiteColor);
    EraseRect(&bounds);
    PenNormal();
    qd_set_transform();
    globe_tris_num_vis = 0;
    globe_draw_tripstrip();
    if (drv_osd_enabled) drv_draw_osd();
    if (buffered)
    {
        SetGWorld(old_world, old_device);
        qd_present();
        UnlockPixels(pixels);
    }
    SetPort((GrafPtr)qd_window);
    DrawGrowIcon(qd_window);
    SetPort(saved);
    if (qd_color_available) SetGDevice(saved_device);
    qd_dirty = FALSE;
    drv_fps_stop();
}

static void qd_keyboard(unsigned char key)
{
    switch (key)
    {
        case 27: drv_close(); return;
        case '1': case '2': globe_toggle_res((char)key); break;
        case 'a': case 'A':
            drv_auto_enabled = !drv_auto_enabled;
            qd_auto_tick = TickCount();
            break;
        case 'o': case 'O': drv_osd_enabled = !drv_osd_enabled; break;
        default: return;
    }
    qd_dirty = TRUE;
}

static void qd_mouse_move(int x, int y)
{
    if (drv_lmouse_pressed)
    {
        drv_rot_y += (float)((long)x - qd_old_x) * 100.0f / drv_win_width;
        drv_rot_x += (float)((long)y - qd_old_y) * 100.0f / drv_win_height;
        while (drv_rot_x < 0) drv_rot_x += 360.0f;
        while (drv_rot_x >= 360.0f) drv_rot_x -= 360.0f;
        while (drv_rot_y < 0) drv_rot_y += 360.0f;
        while (drv_rot_y >= 360.0f) drv_rot_y -= 360.0f;
        qd_dirty = TRUE;
    }
    qd_old_x = x; qd_old_y = y;
}

static void qd_menu(long selection)
{
    if ((short)(selection >> 16) == QD_FILE_MENU && (short)selection == 1)
        drv_close();
    HiliteMenu(0);
}

static void qd_event(EventRecord *event)
{
    WindowPtr window;
    Point point;
    Rect limit;
    long dimensions;
    short part;
    unsigned char key;
    switch (event->what)
    {
        case mouseDown:
            part = FindWindow(event->where, &window);
            switch (part)
            {
                case inMenuBar: qd_menu(MenuSelect(event->where)); break;
                case inSysWindow: SystemClick(event, window); break;
                case inContent:
                    if (window != qd_window) break;
                    if (FrontWindow() != qd_window) SelectWindow(qd_window);
                    else
                    {
                        SetPort((GrafPtr)qd_window);
                        point = event->where;
                        GlobalToLocal(&point);
                        qd_old_x = point.h; qd_old_y = point.v;
                        drv_lmouse_pressed = TRUE;
                    }
                    break;
                case inDrag:
                    if (window == qd_window)
                    {
                        limit = qd_screen;
                        DragWindow(window, event->where, &limit);
                        qd_dirty = TRUE;
                    }
                    break;
                case inGrow:
                    if (window == qd_window)
                    {
                        SetRect(&limit, 64, 64,
                                 qd_screen.right - qd_screen.left,
                                 qd_screen.bottom - qd_screen.top - 40);
                        dimensions = GrowWindow(window, event->where, &limit);
                        if (dimensions != 0)
                        {
                            SizeWindow(window, (short)dimensions,
                                        (short)(dimensions >> 16), TRUE);
                            drv_resize((short)dimensions, (short)(dimensions >> 16));
                        }
                    }
                    break;
                case inGoAway:
                    if (window == qd_window && TrackGoAway(window, event->where)) drv_close();
                    break;
                default: break;
            }
            break;
        case mouseUp: drv_lmouse_pressed = FALSE; break;
        case keyDown: case autoKey:
            key = (unsigned char)(event->message & charCodeMask);
            if (event->modifiers & cmdKey) qd_menu(MenuKey((char)key));
            else qd_keyboard(key);
            break;
        case updateEvt:
            if ((WindowPtr)event->message == qd_window)
            {
                BeginUpdate(qd_window);
                drv_render();
                EndUpdate(qd_window);
            }
            break;
        case activateEvt:
            if ((WindowPtr)event->message == qd_window)
            {
                qd_active = (event->modifiers & activeFlag) != 0;
                qd_auto_tick = TickCount();
                if (!qd_active) drv_lmouse_pressed = FALSE;
                if (qd_active && qd_palette != NULL) ActivatePalette(qd_window);
                qd_dirty = TRUE;
            }
            break;
        case osEvt:
            if (((unsigned long)event->message >> 24) == suspendResumeMessage)
            {
                qd_active = (event->message & resumeFlag) != 0;
                qd_auto_tick = TickCount();
                if (qd_active) qd_dirty = TRUE;
                else drv_lmouse_pressed = FALSE;
            }
            break;
        default: break;
    }
}

void drv_loop(void)
{
    EventRecord event;
    Point point;
    unsigned long tick, elapsed;
    qd_ready = TRUE;
    qd_auto_tick = TickCount();
    while (qd_running)
    {
        if (WaitNextEvent(everyEvent, &event,
                           (drv_auto_enabled || drv_lmouse_pressed) ? 1L : 30L, NULL))
            qd_event(&event);
        if (!qd_running) break;
        if (qd_active && drv_lmouse_pressed)
        {
            SetPort((GrafPtr)qd_window);
            GetMouse(&point);
            qd_mouse_move(point.h, point.v);
            if (!Button()) drv_lmouse_pressed = FALSE;
        }
        if (qd_active && drv_auto_enabled)
        {
            tick = TickCount();
            elapsed = (tick - qd_auto_tick) & 0xffffffffUL;
            qd_auto_tick = tick;
            drv_rot_y += (float)elapsed * (DRV_ROTATE / 60.0f);
            while (drv_rot_y >= 360.0f) drv_rot_y -= 360.0f;
            qd_dirty = TRUE;
        }
        else qd_auto_tick = TickCount();
        if (qd_active && qd_dirty) drv_render();
    }
    qd_cleanup();
}
#endif /* DRV_QD */
