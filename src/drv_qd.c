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
#include <Resources.h>
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
#include "earthres.h"
#if defined(__SC__) || defined(applec)
#pragma segment QuickDraw
#endif

#define QD_MAX_VERTICES SOFT3D_MAX_VERTICES
#define QD_MAX_POLY SOFT3D_MAX_POLY
#define QD_FILE_MENU 128

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
static Soft3d_fixed_point qd_fixed_points[QD_MAX_VERTICES];

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

void drv_load_earth_chunk(unsigned int id, unsigned long length,
                           unsigned char TG_HUGE *destination)
{
    Handle resource;
    if (destination == NULL || length == 0 || length > TG_EARTH_CHUNK_SIZE)
        qd_error("Invalid QuickDraw Earth resource chunk");
    resource = Get1Resource((ResType)0x45415254UL, (short)id);
    if (resource == NULL) qd_error("Cannot load QuickDraw Earth resource");
    if (GetHandleSize(resource) != (Size)length)
    {
        ReleaseResource(resource);
        qd_error("QuickDraw Earth resource has the wrong size");
    }
    HLock(resource);
    if (*resource == NULL)
    {
        HUnlock(resource);
        ReleaseResource(resource);
        qd_error("Cannot lock QuickDraw Earth resource");
    }
    /* The shared loader limits every locked resource to 16 KiB. */
    memcpy(destination, *resource, (size_t)length);
    HUnlock(resource);
    ReleaseResource(resource);
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
    drv_colormap_bits = qd_depth <= 8 ? (unsigned int)qd_depth :
                         (qd_depth == 16 ? 15U : 24U);
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
    int screen_width, screen_height, small_screen;
    int available_width, available_height;
    qd_display_info();
    screen_width = qd_screen.right - qd_screen.left;
    screen_height = qd_screen.bottom - qd_screen.top;
    small_screen = screen_width <= 640 && screen_height <= 480;
    available_width = screen_width - 40;
    available_height = screen_height - 80;
    drv_win_width = (unsigned int)(in_width > 0 ? in_width :
                                    (small_screen ? 320 : DRV_WIN_WIDTH));
    drv_win_height = (unsigned int)(in_height > 0 ? in_height :
                                     (small_screen ? 240 : DRV_WIN_HEIGHT));
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

static void qd_set_transform(void)
{
    if (qd_has_fpu)
        qd_fpu_prepare(&drv_rot_x, &drv_rot_y);
    else if (!soft3d_fixed_prepare(qd_fixed_matrix, &drv_rot_x, &drv_rot_y))
        qd_error("QuickDraw coordinate is out of range");
}

static int qd_prepare_polygon(unsigned int first, unsigned int second,
                               unsigned int third, Point *destination)
{
    Qd_screen_point screen[QD_MAX_POLY];
    int i, count;
    if (qd_has_fpu)
        count = qd_fpu_project(first, second, third,
                               drv_win_width, drv_win_height, screen);
    else
        count = soft3d_fixed_project(qd_fixed_points, first, second, third,
                                     drv_win_width, drv_win_height, screen);
    count = soft3d_finish_polygon(screen, count, drv_win_width, drv_win_height);
    for (i = 0; i < count; ++i)
    {
        destination[i].h = (short)screen[i].x;
        destination[i].v = (short)screen[i].y;
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

static void qd_draw_strip_batch(unsigned int count,
                                const Vertex TG_HUGE *vertices,
                                const unsigned char TG_HUGE *facet_colors)
{
    Point projected[QD_MAX_POLY];
    unsigned int i;
    int j, total;
    PolyHandle polygon;
    if (count < 3 || vertices == NULL || facet_colors == NULL) return;
    if (count > QD_MAX_VERTICES) qd_error("QuickDraw strip batch is too large");
    if (qd_has_fpu) qd_fpu_transform(count, vertices);
    else if (!soft3d_fixed_transform(qd_fixed_matrix, count, vertices,
                                      qd_fixed_points))
        qd_error("QuickDraw fixed path supports radius up to 1000");
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

void drv_draw_tristrip_flat_color(unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  const unsigned char TG_HUGE *facet_colors)
{
    drv_draw_strip_batches(count, vertices, facet_colors,
                            QD_MAX_VERTICES - 2U, qd_draw_strip_batch);
}

/* Native font metrics keep both the statistics and help readable on a
 * 320-pixel window. A counting pass positions wrapped help at the bottom. */
static int qd_osd_text(const char *text, int baseline, short width, int draw)
{
    short count, last_space;
    while (*text != 0)
    {
        count = last_space = 0;
        while (text[count] != 0 && TextWidth(text, 0, (short)(count + 1)) <= width)
        {
            if (text[count] == ' ') last_space = count;
            ++count;
        }
        if (text[count] != 0 && last_space > 0) count = last_space;
        if (count == 0) count = 1;
        if (draw && baseline > 0 && baseline <= (int)drv_win_height)
        {
            MoveTo(8, (short)baseline);
            DrawText(text, 0, count);
        }
        baseline += 12;
        text += count;
        while (*text == ' ') ++text;
    }
    return baseline;
}

void drv_draw_osd(void)
{
    char status[160];
    RGBColor white;
    short width;
    int next_line, help_baseline;
    if (drv_win_width <= 16 || drv_win_height < 16) return;
    if (qd_depth > 1)
    {
        white.red = white.green = white.blue = 65535U;
        RGBForeColor(&white);
    }
    else ForeColor(blackColor);
    TextFont(0);
    TextSize(9);
    TextMode(srcCopy);
    drv_format_stats(status);
    sprintf(status + strlen(status), ", %s, GWorld %s",
             qd_has_fpu ? "68881 math" : "integer math",
             qd_world != NULL ? "on" : "off");
    width = (short)(drv_win_width - 16U);
    next_line = qd_osd_text(status, 14, width, TRUE);
    help_baseline = (int)drv_win_height + 4 - qd_osd_text(drv_help, 0, width, FALSE);
    if (help_baseline >= next_line) qd_osd_text(drv_help, help_baseline, width, TRUE);
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
        case '1': case '2': case '3': case '4': case '5':
            globe_toggle_res((char)key);
            break;
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
