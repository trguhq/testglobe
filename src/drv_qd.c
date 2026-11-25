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
#ifdef QD_MULTIVERSAL
/* Retro68's generated interfaces collect the Toolbox declarations here. */
#include <Multiverse.h>
/* System 7 constants missing from the current open interface declarations.
 * Values match Apple's Universal Interfaces. */
#ifndef gestalt68020
#define gestalt68020 3
#endif
#ifndef gestalt68882
#define gestalt68882 2
#endif
#ifndef gestalt68040FPU
#define gestalt68040FPU 3
#endif
#ifndef gestalt32BitQD12
#define gestalt32BitQD12 0x0220
#endif
#ifndef gestaltSystemVersion
#define gestaltSystemVersion 0x73797376L
#endif
#ifndef gestaltAppleEventsAttr
#define gestaltAppleEventsAttr 0x65766E74L
#endif
#ifndef gestaltAppleEventsPresent
#define gestaltAppleEventsPresent 0
#endif
#ifndef screenDevice
#define screenDevice 13
#endif
#ifndef screenActive
#define screenActive 15
#endif
#ifndef ditherCopy
#define ditherCopy 64
#endif
#ifndef suspendResumeMessage
#define suspendResumeMessage 1
#endif
#ifndef resumeFlag
#define resumeFlag 1
#endif
#ifndef kHighLevelEvent
#define kHighLevelEvent 23
#endif
#ifndef watchCursor
#define watchCursor 4
#endif
#ifndef pmInhibitG2
#define pmInhibitG2 0x0100
#endif
#ifndef pmInhibitC2
#define pmInhibitC2 0x0200
#endif
#ifndef pmInhibitG4
#define pmInhibitG4 0x0400
#endif
#ifndef pmInhibitC4
#define pmInhibitC4 0x0800
#endif
#ifndef kCoreEventClass
#define kCoreEventClass 0x61657674L
#endif
#ifndef kAEOpenApplication
#define kAEOpenApplication 0x6F617070L
#endif
#ifndef kAEOpenDocuments
#define kAEOpenDocuments 0x6F646F63L
#endif
#ifndef kAEPrintDocuments
#define kAEPrintDocuments 0x70646F63L
#endif
#ifndef kAEQuitApplication
#define kAEQuitApplication 0x71756974L
#endif
#ifndef keyMissedKeywordAttr
#define keyMissedKeywordAttr 0x6D697373L
#endif
#ifndef typeWildCard
#define typeWildCard 0x2A2A2A2AL
#endif
#ifndef errAEDescNotFound
#define errAEDescNotFound (-1701)
#endif
#ifndef errAEEventNotHandled
#define errAEEventNotHandled (-1708)
#endif
#ifndef errAEParamMissed
#define errAEParamMissed (-1715)
#endif
/* The open interfaces use the Universal Interfaces 3 routine names. A 68K
 * routine descriptor is the procedure pointer itself. */
#define QD_GROW_ZONE_UPP(proc) NewGrowZoneUPP((GrowZoneProcPtr)(proc))
#define QD_AE_HANDLER_UPP(proc) NewAEEventHandlerUPP((AEEventHandlerProcPtr)(proc))
#else
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
#include <Desk.h>
#include <AppleEvents.h>
/* Universal Interfaces 2.2 spell the 68K routine descriptors ...Proc. */
#define QD_GROW_ZONE_UPP(proc) NewGrowZoneProc(proc)
#define QD_AE_HANDLER_UPP(proc) NewAEEventHandlerProc(proc)
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
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
#define QD_APPLE_MENU 128
#define QD_FILE_MENU 129
#define QD_ERROR_ALERT 128
#define QD_ABOUT_ALERT 129
/* These are conservative allocation probes, not promises about opaque
 * Toolbox storage. A ten-edge fill needs a small edge list; native color
 * patterns also allocate a PixMap, palette, handles and expanded pixels.
 * Leave more room for that optional path. Probe once per drawing pass. */
#define QD_FILL_SCRATCH 4096L
#define QD_PATTERN_SCRATCH 8192L
#define QD_POLYGON_BYTES (offsetof(Polygon, polyPoints) + \
                          (QD_MAX_POLY + 1U) * sizeof(Point))
#define QD_PALETTE_BYTES ((long)sizeof(ColorTable) + 255L * (long)sizeof(ColorSpec))
/* A few blocks of master pointers made before any handle exists stay at
 * the bottom of the heap instead of fragmenting it later. */
#define QD_MASTER_BLOCKS 4
/* Toolbox fills, alerts and menus raise SysError when their allocations
 * fail. The grow zone hands them this much, three times the pattern
 * scratch, then the driver takes it back between frames. */
#define QD_RESERVE_BYTES 24576L
/* A restored GWorld must still leave room for drawing and a later key. */
#define QD_RESTORE_HEADROOM 32768L
/* A GWorld's own GDevice, inverse table and handles beyond its pixels. */
#define QD_WORLD_OVERHEAD 8192L
#define QD_IDLE_SLEEP 30UL
/* A background copy has nothing to draw; events still wake it at once. */
#define QD_BACKGROUND_SLEEP 0x7fffffffUL
#define QD_HELP_LINES 32
/* Ordered luminance dithering has 65 distinct 8x8 coverage levels. */
#define QD_DITHER_LEVELS 65
/* 8-bit Earth components to 16-bit QuickDraw components without a multiply. */
#define QD_COMPONENT(c) ((unsigned short)(((unsigned short)(c) << 8) | (c)))
#if 2 * (DRV_QUAD_BATCH_MAX + 1) > QD_MAX_VERTICES
#error QuickDraw quad batches exceed the transformed vertex cache
#endif

QDGlobals qd;
static WindowPtr qd_window;
static GDHandle qd_device;
static GWorldPtr qd_world;
static CTabHandle qd_colors;
static PaletteHandle qd_palette;
static PixPatHandle qd_pattern;
static int qd_pattern_color = -1;
static int qd_pattern_unavailable;
static MenuHandle qd_apple_menu;
static MenuHandle qd_file_menu;
static int qd_color_available;
static int qd_world_available;
static long qd_version;
static int qd_apple_events;
static int qd_has_fpu;
static int qd_depth;
static int qd_ready;
static int qd_running;
static int qd_active;
static int qd_dirty = TRUE;
static int qd_rendering;
static int qd_buffered;
static int qd_frame_paused;
static int qd_frame_complete;
static int qd_polygon_failed;
static GrafPtr qd_render_port;
static GDHandle qd_render_device;
static PixMapHandle qd_locked_pixels;
static PolyHandle qd_polygon;
static int qd_native_ready;
static int qd_old_x, qd_old_y;
static unsigned long qd_auto_tick;
static Rect qd_screen;
static long qd_fixed_matrix[3][3];
static Soft3d_fixed_point qd_fixed_points[QD_MAX_VERTICES];
/* About 1.2 KiB of A5 data: never released, so never reacquired. */
static Soft3d_project_cache qd_project_cache;
static Drv_quad_stream qd_packet_stream;
static int qd_packet_active;
/* The GWorld holds the frame on screen; an update only copies it again. */
static int qd_world_shown;
/* An 8-bit GWorld built from the Earth table, and which Earth indices its
 * inverse table returns exactly (known, then exact, one bit per index). */
static int qd_world_earth;
static unsigned char qd_exact_known[32], qd_exact_colors[32];
/* Optional resources dropped by drv_release_optional, and the scene
 * revision of the last guarded attempt to take them back. */
static int qd_optional_released;
static int qd_restore_tried;
static unsigned long qd_restore_revision;
/* Memory reserve given to Toolbox allocations by the grow-zone procedure.
 * qd_checked marks the driver's own allocations, which handle failure. */
static Handle qd_reserve;
static int qd_reserve_spent;
static int qd_checked;
static int qd_busy_shown;
static Pattern qd_dither[QD_DITHER_LEVELS];
static unsigned char qd_dither_built[QD_DITHER_LEVELS];
/* Help line breaks for one font, size and width; drv_help never changes. */
static unsigned char qd_help_start[QD_HELP_LINES], qd_help_count[QD_HELP_LINES];
static unsigned int qd_help_lines;
static int qd_help_valid;
static short qd_help_font, qd_help_size, qd_help_width;
/* Event handlers run synchronously in drv_loop; drawing registers no
 * native event callbacks. Keep render guards without a second event queue. */
static short qd_font_family;
static short qd_font_id, qd_font_size = 9;

/* Prefer real Geneva bitmaps, retaining the existing system-font fallback
 * when they are absent. Selection changes only with window size. */
static void qd_select_font(void)
{
    short size;
    size = drv_win_width >= 960U && drv_win_height >= 640U ? 18 :
           (drv_win_width >= 480U && drv_win_height >= 320U ? 12 : 9);
    qd_font_id = 0;
    qd_font_size = 9;
    if (qd_font_family == 0) return;
    for (;;)
    {
        if (RealFont(qd_font_family, size))
        {
            qd_font_id = qd_font_family;
            qd_font_size = size;
            return;
        }
        if (size == 9) return;
        size = size == 18 ? 12 : 9;
    }
}

static void qd_pascal(Str255 result, const char *text)
{
    size_t length;
    length = strlen(text);
    if (length > 255) length = 255;
    result[0] = (unsigned char)length;
    memcpy(result + 1, text, length);
}

/* The driver's own allocations check for NULL and recover, so the grow
 * zone keeps the reserve for Toolbox calls that cannot. */
static Handle qd_new_handle(Size size)
{
    Handle handle;
    int checked;
    checked = qd_checked;
    qd_checked = TRUE;
    handle = NewHandle(size);
    qd_checked = checked;
    return handle;
}

/* Every stop path drops input and readiness before touching native state. */
static void qd_stop(void)
{
    qd_ready = FALSE;
    qd_running = FALSE;
    qd_active = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
}

/* Enter a drawing scope, remembering the caller's port and device so every
 * exit, including a fatal alert, can restore them. */
static void qd_begin_render(void)
{
    GetPort(&qd_render_port);
    qd_render_device = NULL;
    if (qd_color_available)
    {
        qd_render_device = GetGDevice();
        if (qd_device != NULL) SetGDevice(qd_device);
    }
    qd_rendering = TRUE;
}

/* Restore the caller's Toolbox state before a modal alert or disposal.
 * The reusable polygon belongs to the driver, not to a drawing scope. */
static void qd_finish_render(void)
{
    if (!qd_rendering) return;
    SetPort(qd_render_port);
    if (qd_locked_pixels != NULL) UnlockPixels(qd_locked_pixels);
    qd_locked_pixels = NULL;
    if (qd_color_available) SetGDevice(qd_render_device);
    qd_render_port = NULL;
    qd_render_device = NULL;
    qd_buffered = FALSE;
    qd_rendering = FALSE;
}

static void qd_dispose_buffer(void)
{
    if (qd_world != NULL) DisposeGWorld(qd_world);
    qd_world = NULL;
    qd_world_shown = FALSE;
    qd_world_earth = FALSE;
    /* MakeRGBPat chose its colors for the device being drawn. */
    qd_pattern_color = -1;
}

static void qd_dispose_pattern(void)
{
    if (qd_pattern != NULL) DisposePixPat(qd_pattern);
    qd_pattern = NULL;
    qd_pattern_color = -1;
}

static void qd_set_palette(WindowPtr window, PaletteHandle palette, int updates)
{
#ifdef QD_MULTIVERSAL
    /* The open interfaces declare SetPalette without a callable trap stub.
     * Its System 7 replacement has an inline trap and explicit update flags. */
    NSetPalette(window, palette, (short)(updates ? pmAllUpdates : pmNoUpdates));
#else
    SetPalette(window, palette, updates ? TRUE : FALSE);
#endif
}

static void qd_dispose_palette(void)
{
    if (qd_palette != NULL)
    {
        if (qd_window != NULL) qd_set_palette(qd_window, NULL, FALSE);
        DisposePalette(qd_palette);
    }
    qd_palette = NULL;
    if (qd_colors != NULL) DisposeHandle((Handle)qd_colors);
    qd_colors = NULL;
}

/* Only an 8-bit device takes the Earth palette; a missing one there means
 * a failed or released allocation. */
static int qd_palette_missing(void)
{
    return qd_color_available && qd_depth == 8 && qd_palette == NULL;
}

/* Drop the optional native resources; the window, menus and polygon stay.
 * Restore the caller's port before releasing its possible pixel storage. */
static void qd_release_resources(void)
{
    qd_finish_render();
    qd_native_ready = FALSE;
    soft3d_project_cache_reset(&qd_project_cache);
    qd_packet_active = FALSE;
    drv_quadstream_reset(&qd_packet_stream);
    qd_dispose_buffer();
    qd_dispose_pattern();
    qd_dispose_palette();
}

/* Called between frames when shared allocation recovery needs heap space.
 * Only qd_restore_optional takes the resources back, once memory allows. */
void drv_release_optional(void)
{
    qd_release_resources();
    qd_pattern_unavailable = qd_color_available && qd_depth > 1;
    qd_optional_released = TRUE;
    qd_restore_tried = FALSE;
    qd_dirty = TRUE;
    drv_notice("QuickDraw optional buffers released");
}

static void qd_dispose_menu(MenuHandle *menu, short id)
{
    if (*menu != NULL)
    {
        DeleteMenu(id);
        DisposeMenu(*menu);
    }
    *menu = NULL;
}

static void qd_cleanup(void)
{
    qd_stop();
    qd_release_resources();
    if (qd_polygon != NULL) DisposeHandle((Handle)qd_polygon);
    qd_polygon = NULL;
    if (qd_window != NULL) DisposeWindow(qd_window);
    qd_window = NULL;
    qd_dispose_menu(&qd_file_menu, QD_FILE_MENU);
    qd_dispose_menu(&qd_apple_menu, QD_APPLE_MENU);
    if (qd_reserve != NULL) DisposeHandle(qd_reserve);
    qd_reserve = NULL;
}

/* The watch covers mesh rebuilds, which take minutes at key 5 on a 68000. */
static void qd_busy(int busy)
{
    CursHandle watch;
    if (!busy)
    {
        if (qd_busy_shown) SetCursor(&qd.arrow);
        qd_busy_shown = FALSE;
        return;
    }
    watch = GetCursor(watchCursor);
    if (watch == NULL || *watch == NULL) return;
    /* SetCursor copies the image and cannot move the resource. */
    SetCursor(*watch);
    qd_busy_shown = TRUE;
}

void qd_error(char *message)
{
    Str255 text;
    static const unsigned char empty[] = "";
    /* A modal alert must never expose a partially rebuilt mesh or port. */
    qd_stop();
    qd_finish_render();
    qd_busy(FALSE);
    /* The alert's window and items need heap: give it the reserve now,
     * leaving the grow zone nothing to hand out. */
    if (qd_reserve != NULL) DisposeHandle(qd_reserve);
    qd_reserve = NULL;
    qd_pascal(text, message);
    ParamText(text, empty, empty, empty);
    StopAlert(QD_ERROR_ALERT, NULL);
    exit(1);
}

void drv_close(void)
{
    if (qd_rendering) return;
    qd_stop();
}

/* The Memory Manager calls this when the application heap is full. A
 * Toolbox allocation that cannot fail cleanly gets the reserve; checked
 * driver allocations do not, so their own recovery still runs. MPW globals
 * are A5-relative, so switch to this application's A5 world first. The
 * QuickDraw segment is loaded by drv_init and never unloaded, so this call
 * cannot itself need LoadSeg. Only an emptied reserve is reallocated. */
static pascal long qd_grow_zone(Size needed)
{
    long saved_a5, freed;
    saved_a5 = SetCurrentA5();
    freed = 0L;
    if (!qd_checked && qd_reserve != NULL && *qd_reserve != NULL)
    {
        EmptyHandle(qd_reserve);
        qd_reserve_spent = TRUE;
        freed = QD_RESERVE_BYTES;
    }
    (void)SetA5(saved_a5);
    (void)needed;
    return freed;
}

/* Between frames, take back a reserve that a Toolbox allocation used. Drop
 * optional memory first; if the reserve still does not fit, pause so no
 * unprotected fill can reach the system's out-of-memory error. */
static void qd_keep_reserve(void)
{
    int checked;
    if (!qd_reserve_spent || qd_reserve == NULL) return;
    if (!qd_optional_released) drv_release_optional();
    checked = qd_checked;
    qd_checked = TRUE;
    ReallocateHandle(qd_reserve, (Size)QD_RESERVE_BYTES);
    qd_checked = checked;
    if (MemError() == noErr && *qd_reserve != NULL)
    {
        qd_reserve_spent = FALSE;
        return;
    }
    if (!qd_frame_paused) drv_notice("QuickDraw memory reserve unavailable");
    qd_frame_paused = TRUE;
    drv_auto_enabled = FALSE;
}

/* Required events carry nothing this application reads. The Apple Event
 * Manager rules still reject any required parameter left unread. */
static OSErr qd_ae_parameters(AppleEvent *event)
{
    DescType type;
    Size size;
    OSErr result;
    result = AEGetAttributePtr(event, keyMissedKeywordAttr, typeWildCard,
                               &type, NULL, 0, &size);
    if (result == errAEDescNotFound) return noErr;
    return result == noErr ? errAEParamMissed : result;
}

static pascal OSErr qd_ae_open(AppleEvent *event, AppleEvent *reply, long refcon)
{
    (void)reply; (void)refcon;
    return qd_ae_parameters(event);
}

/* testglobe has no document types to open or print. */
static pascal OSErr qd_ae_documents(AppleEvent *event, AppleEvent *reply, long refcon)
{
    (void)event; (void)reply; (void)refcon;
    return errAEEventNotHandled;
}

/* Shut Down and scripts quit through the same normal exit as File > Quit. */
static pascal OSErr qd_ae_quit(AppleEvent *event, AppleEvent *reply, long refcon)
{
    OSErr result;
    (void)reply; (void)refcon;
    result = qd_ae_parameters(event);
    if (result == noErr) drv_close();
    return result;
}

/* Without the Apple Event Manager, high-level events never arrive and the
 * File menu remains the only way to quit, as on earlier systems. */
static void qd_install_apple_events(void)
{
    long attributes;
    qd_apple_events = FALSE;
    attributes = 0;
    if (Gestalt(gestaltAppleEventsAttr, &attributes) != noErr ||
        (attributes & (1L << gestaltAppleEventsPresent)) == 0) return;
    if (AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                              QD_AE_HANDLER_UPP(qd_ae_open), 0L, FALSE) != noErr ||
        AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
                              QD_AE_HANDLER_UPP(qd_ae_documents), 0L, FALSE) != noErr ||
        AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
                              QD_AE_HANDLER_UPP(qd_ae_documents), 0L, FALSE) != noErr ||
        AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                              QD_AE_HANDLER_UPP(qd_ae_quit), 0L, FALSE) != noErr)
        qd_error("Cannot install Apple event handlers");
    qd_apple_events = TRUE;
}

static void qd_make_menus(void)
{
    static const unsigned char apple_title[] = "\001\024";
    static const unsigned char about_item[] = "\020About testglobe\311";
    static const unsigned char separator[] = "\002(-";
    static const unsigned char file_title[] = "\004File";
    static const unsigned char quit_item[] = "\006Quit/Q";
    qd_apple_menu = NewMenu(QD_APPLE_MENU, apple_title);
    qd_file_menu = NewMenu(QD_FILE_MENU, file_title);
    if (qd_apple_menu == NULL || qd_file_menu == NULL)
        qd_error("Cannot create QuickDraw menus");
    AppendMenu(qd_apple_menu, about_item);
    AppendMenu(qd_apple_menu, separator);
    /* Desk accessories on System 6; Apple Menu Items on System 7. */
    AppendResMenu(qd_apple_menu, (ResType)0x44525652UL);
    AppendMenu(qd_file_menu, quit_item);
    ClearMenuBar();
    InsertMenu(qd_apple_menu, 0);
    InsertMenu(qd_file_menu, 0);
    DrawMenuBar();
}

void drv_init(int *argc, char **argv)
{
    long quickdraw_version, processor, fpu, system;
    int i;
    static const unsigned char font_name[] = "\006Geneva";
    (void)argc; (void)argv;
    qd_ready = FALSE;
    qd_pattern_unavailable = FALSE;
    qd_frame_paused = FALSE;
    qd_polygon_failed = FALSE;
    qd_optional_released = FALSE;
    MaxApplZone();
    for (i = 0; i < QD_MASTER_BLOCKS; ++i) MoreMasters();
    InitGraf(&qd.thePort);
    InitFonts();
    qd_font_family = 0;
    GetFNum(font_name, &qd_font_family);
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    if (atexit(qd_cleanup) != 0) qd_error("Cannot register QuickDraw cleanup");
    qd_reserve_spent = FALSE;
    qd_reserve = NewHandle((Size)QD_RESERVE_BYTES);
    if (qd_reserve == NULL) qd_error("Cannot reserve QuickDraw memory");
    SetGrowZone(QD_GROW_ZONE_UPP(qd_grow_zone));
    quickdraw_version = processor = fpu = system = 0;
    qd_color_available = Gestalt(gestaltQuickdrawVersion, &quickdraw_version) == noErr &&
                         quickdraw_version >= gestalt8BitQD;
    qd_version = quickdraw_version;
    /* Color QuickDraw needs 32-bit QuickDraw for GWorlds; System 7 also
     * gives basic QuickDraw 1-bit GWorlds (Plus, SE and Classic). */
    qd_world_available = qd_color_available ? quickdraw_version >= gestalt32BitQD :
                         Gestalt(gestaltSystemVersion, &system) == noErr &&
                         (system & 0xffffL) >= 0x0700L;
    qd_has_fpu = Gestalt(gestaltProcessorType, &processor) == noErr &&
                 processor >= gestalt68020 &&
                 Gestalt(gestaltFPUType, &fpu) == noErr &&
                 (fpu == gestalt68881 || fpu == gestalt68882 ||
                  fpu == gestalt68040FPU);
    qd_make_menus();
    qd_install_apple_events();
    soft3d_project_cache_reset(&qd_project_cache);
    qd_running = TRUE;
    qd_active = TRUE;
}

Drv_result drv_load_earth_chunk(unsigned int id, unsigned long length,
                           unsigned char TG_HUGE *destination)
{
    Handle resource;
    if (destination == NULL || length == 0 || length > TG_EARTH_CHUNK_SIZE)
        return DRV_BAD_INPUT;
    resource = Get1Resource((ResType)0x45415254UL, (short)id);
    if (resource == NULL)
        return ResError() == memFullErr ? DRV_NO_MEMORY : DRV_BAD_DATA;
    if (GetHandleSize(resource) != (Size)length)
    {
        ReleaseResource(resource);
        return DRV_BAD_DATA;
    }
    HLock(resource);
    if (*resource == NULL)
    {
        HUnlock(resource);
        ReleaseResource(resource);
        return DRV_NO_MEMORY;
    }
    /* The shared loader limits every locked resource to 16 KiB. */
    memcpy(destination, *resource, (size_t)length);
    HUnlock(resource);
    ReleaseResource(resource);
    return DRV_OK;
}

/* The window's content in global coordinates, for choosing its monitor.
 * Clamp the far corner: QuickDraw coordinates stop at 32767. */
static void qd_window_bounds(Rect *bounds)
{
    GrafPtr port;
    Point origin;
    long right, bottom;
    GetPort(&port);
    SetPort((GrafPtr)qd_window);
    *bounds = qd_window->portRect;
    origin.h = bounds->left;
    origin.v = bounds->top;
    LocalToGlobal(&origin);
    SetPort(port);
    right = (long)bounds->right + origin.h - bounds->left;
    bottom = (long)bounds->bottom + origin.v - bounds->top;
    bounds->left = origin.h;
    bounds->top = origin.v;
    bounds->right = (short)(right > 32767L ? 32767L : right);
    bounds->bottom = (short)(bottom > 32767L ? 32767L : bottom);
}

static void qd_display_info(void)
{
    GDHandle device;
    Rect bounds;
    int depth;
    qd_depth = 1;
    qd_device = NULL;
    qd_screen = qd.screenBits.bounds;
    if (qd_color_available)
    {
        /* Follow the window across monitors once it exists; before then,
         * open it on the deepest active screen. */
        if (qd_window != NULL)
        {
            qd_window_bounds(&bounds);
            qd_device = GetMaxDevice(&bounds);
            if (qd_device != NULL)
            {
                qd_depth = (**(**qd_device).gdPMap).pixelSize;
                qd_screen = (**qd_device).gdRect;
            }
        }
        if (qd_device == NULL)
        {
            for (device = GetDeviceList(); device != NULL;
                 device = GetNextDevice(device))
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

/* Only 8-bit devices take the Earth palette. Its luminance-sorted entries
 * would otherwise fill a 2- or 4-bit CLUT with near-blacks, recoloring the
 * desktop, so those depths keep the standard table and dither to it. */
static void qd_make_palette(void)
{
    int i;
    qd_colors = (CTabHandle)qd_new_handle((Size)QD_PALETTE_BYTES);
    if (qd_colors == NULL)
    {
        drv_notice("QuickDraw uses the display palette");
        return;
    }
    (**qd_colors).ctSeed = GetCTSeed();
    (**qd_colors).ctFlags = 0;
    (**qd_colors).ctSize = 255;
    for (i = 0; i < 256; ++i)
    {
        (**qd_colors).ctTable[i].value = (short)i;
        (**qd_colors).ctTable[i].rgb.red = QD_COMPONENT(earth_cmap[i][0]);
        (**qd_colors).ctTable[i].rgb.green = QD_COMPONENT(earth_cmap[i][1]);
        (**qd_colors).ctTable[i].rgb.blue = QD_COMPONENT(earth_cmap[i][2]);
    }
    qd_palette = NewPalette(256, qd_colors, pmTolerant | pmInhibitC2 |
                            pmInhibitG2 | pmInhibitC4 | pmInhibitG4, 0);
    if (qd_palette == NULL)
    {
        qd_dispose_palette();
        drv_notice("QuickDraw uses the display palette");
        return;
    }
    qd_set_palette(qd_window, qd_palette, TRUE);
    ActivatePalette(qd_window);
}

static void qd_make_buffer(void)
{
    Rect bounds;
    QDErr result;
    int checked;
    qd_dispose_buffer();
    if (!qd_world_available) return;
    SetRect(&bounds, 0, 0, (short)drv_win_width, (short)drv_win_height);
    /* This is ordinary offscreen pixel storage, not a special RAM feature.
     * Without noNewDevice, NewGWorld creates its own offscreen GDevice.
     * Basic QuickDraw makes a 1-bit GrafPort world with no PixMap. */
    checked = qd_checked;
    qd_checked = TRUE;
    result = NewGWorld(&qd_world, (short)qd_depth, &bounds,
                       qd_depth == 8 ? qd_colors : NULL, NULL, 0);
    qd_checked = checked;
    if (result != noErr)
    {
        qd_world = NULL; /* Low-memory systems still draw in the window. */
        return;
    }
    qd_world_earth = qd_depth == 8 && qd_colors != NULL;
    memset(qd_exact_known, 0, sizeof(qd_exact_known));
    memset(qd_exact_colors, 0, sizeof(qd_exact_colors));
}

/* GetGWorldPixMap is the only way to a basic QuickDraw world's pixels but
 * arrived with 32-Bit QuickDraw 1.2; earlier Color QuickDraw worlds are
 * CGrafPorts whose portPixMap is valid. */
static PixMapHandle qd_world_pixels(void)
{
    if (qd_color_available && qd_version < gestalt32BitQD12)
        return qd_world->portPixMap;
    return GetGWorldPixMap(qd_world);
}

/* Pixel storage plus the GWorld's own device and tables; -1 if it cannot
 * be represented, which no partition could satisfy anyway. */
static long qd_world_bytes(void)
{
    unsigned long row;
    if (!qd_world_available) return 0L;
    row = (((unsigned long)drv_win_width * (unsigned long)qd_depth + 31UL) / 32UL) * 4UL;
    if (drv_win_height != 0U &&
        row > (0x7fffffffUL - (unsigned long)QD_WORLD_OVERHEAD) / drv_win_height)
        return -1L;
    return (long)(row * drv_win_height) + QD_WORLD_OVERHEAD;
}

static int qd_block_fits(long bytes)
{
    long largest;
    if (bytes < 0L) return FALSE;
    largest = (long)MaxBlock();
    return largest >= QD_RESTORE_HEADROOM && largest - QD_RESTORE_HEADROOM >= bytes;
}

/* After a release, take optional resources back at most once per scene
 * revision (a new release, size or device allows one more attempt), and
 * only when the largest free block covers them and leaves headroom. The
 * small palette and pattern come back first; the GWorld needs its own. */
static void qd_restore_optional(void)
{
    long display_bytes;
    /* The reserve comes back first, without using up this attempt. */
    if (!qd_optional_released || qd_window == NULL || qd_rendering ||
        qd_reserve_spent || !globe_ready()) return;
    if (qd_restore_tried && qd_restore_revision == globe_revision) return;
    qd_restore_tried = TRUE;
    qd_restore_revision = globe_revision;
    display_bytes = (qd_palette_missing() ? QD_PALETTE_BYTES : 0L) +
                    (qd_color_available && qd_depth > 1 && qd_depth <= 8 ?
                     QD_PATTERN_SCRATCH : 0L);
    if (!qd_block_fits(display_bytes)) return;
    if (qd_palette_missing()) qd_make_palette();
    /* The pattern is made on first use, still behind the scratch probe. */
    qd_pattern_unavailable = FALSE;
    qd_native_ready = FALSE;
    qd_dirty = TRUE;
    if (qd_world_available && qd_world == NULL)
    {
        if (!qd_block_fits(qd_world_bytes())) return;
        qd_make_buffer();
        if (qd_world == NULL) return;
    }
    qd_optional_released = FALSE;
}

/* Build the device's palette and back buffer; after a release only the
 * guarded restore may take that memory back. */
static void qd_make_display(void)
{
    if (qd_optional_released)
    {
        qd_restore_tried = FALSE; /* A new device or size is a new need. */
        qd_restore_optional();
        return;
    }
    if (qd_color_available && qd_depth == 8) qd_make_palette();
    qd_make_buffer();
}

/* Recheck devices after background display-mode changes. Keep native
 * palette and GWorld policy consistent with the newly sampled depth. */
static void qd_refresh_display(void)
{
    GDHandle old_device;
    Rect old_screen;
    int old_depth;
    if (!qd_ready || qd_window == NULL) return;
    if (qd_rendering) return;
    old_device = qd_device;
    old_depth = qd_depth;
    old_screen = qd_screen;
    qd_display_info();
    if (old_device != qd_device || old_depth != qd_depth ||
        old_screen.left != qd_screen.left || old_screen.top != qd_screen.top ||
        old_screen.right != qd_screen.right || old_screen.bottom != qd_screen.bottom)
    {
        qd_ready = FALSE;
        qd_release_resources();
        qd_ready = TRUE;
        qd_make_display();
        qd_dirty = TRUE;
    }
    if (qd_palette != NULL) ActivatePalette(qd_window);
    qd_auto_tick = TickCount();
    drv_reset_timing();
}

static void qd_resize(int width, int height)
{
    if (width < 1 || height < 1) return;
    if (qd_rendering) return;
    if (width > 32767 || height > 32767) qd_error("QuickDraw window is too large");
    drv_win_width = (unsigned int)width;
    drv_win_height = (unsigned int)height;
    qd_select_font();
    if (qd_window != NULL)
    {
        if (qd_optional_released) qd_make_display();
        else qd_make_buffer();
    }
    qd_frame_paused = FALSE;
    qd_auto_tick = TickCount();
    drv_reset_timing();
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
    qd_select_font();
    qd_make_display();
    qd_ready = TRUE;
    return 1;
}

static void qd_set_transform(void)
{
    qd_native_ready = FALSE;
    qd_packet_active = FALSE;
    drv_quadstream_reset(&qd_packet_stream);
    soft3d_project_cache_reset(&qd_project_cache);
    if (qd_has_fpu)
    {
        if (!qd_fpu_prepare(&drv_rot_x, &drv_rot_y))
            qd_error("QuickDraw rotation is out of range");
    }
    else if (!soft3d_fixed_prepare(qd_fixed_matrix, &drv_rot_x, &drv_rot_y))
        qd_error("QuickDraw coordinate is out of range");
}

static int qd_prepare_polygon(const unsigned int *indices, unsigned int count,
                               unsigned int vertex_count,
                               Point *destination)
{
    Qd_screen_point screen[QD_MAX_POLY];
    Soft3d_project_cache *cache;
    int i, projected_count;
    cache = qd_packet_active ? &qd_project_cache : NULL;
    if (qd_has_fpu)
        projected_count = qd_fpu_project_cached(indices, count,
                               drv_win_width, drv_win_height, cache, screen);
    else
        projected_count = soft3d_fixed_project_cached(qd_fixed_points, vertex_count,
                                     indices, count,
                                     drv_win_width, drv_win_height, cache, screen);
    projected_count = soft3d_finish_polygon(screen, projected_count,
                                            drv_win_width, drv_win_height);
    for (i = 0; i < projected_count; ++i)
    {
        destination[i].h = (short)screen[i].x;
        destination[i].v = (short)screen[i].y;
    }
    return projected_count;
}

/* OpenPoly grows its handle through fatal QuickDraw allocation wrappers on
 * classic systems. Build the documented record ourselves using checked
 * Memory Manager storage, then reuse it without recording or hiding the pen. */
static PolyHandle qd_make_polygon(const Point *points, int count)
{
    Polygon *record;
    Point *destination;
    Rect bounds;
    int i;
    if (points == NULL || count < 3 || count > QD_MAX_POLY) return NULL;
    if (qd_polygon == NULL)
        qd_polygon = (PolyHandle)qd_new_handle((Size)QD_POLYGON_BYTES);
    if (qd_polygon == NULL) return NULL;
    /* Nothing below calls the Toolbox, so the record cannot move. */
    record = *qd_polygon;
    if (record == NULL) return NULL;
    destination = (Point *)((char *)record + offsetof(Polygon, polyPoints));
    bounds.left = bounds.right = points[0].h;
    bounds.top = bounds.bottom = points[0].v;
    for (i = 0; i < count; ++i)
    {
        destination[i] = points[i];
        if (points[i].h < bounds.left) bounds.left = points[i].h;
        if (points[i].h > bounds.right) bounds.right = points[i].h;
        if (points[i].v < bounds.top) bounds.top = points[i].v;
        if (points[i].v > bounds.bottom) bounds.bottom = points[i].v;
    }
    destination[count] = points[0];
    record->polySize = (short)(offsetof(Polygon, polyPoints) +
                               (count + 1U) * sizeof(Point));
    record->polyBBox = bounds;
    return qd_polygon;
}

/* A probe measures free space, so it must not get the reserve. */
static int qd_probe_scratch(Size size)
{
    Handle probe;
    probe = qd_new_handle(size);
    if (probe == NULL) return FALSE;
    DisposeHandle(probe);
    return TRUE;
}

/* Some Toolbox versions raise SysError on pattern allocation and can
 * silently skip a fill if their edge allocation fails. Avoid entering
 * these paths under known heap pressure. Successful preflight cannot
 * guarantee every internal or patched Toolbox allocation will succeed. */
static int qd_prepare_native(void)
{
    if (qd_native_ready) return TRUE;
    if (qd_depth > 1 && qd_depth <= 8 && !qd_pattern_unavailable)
    {
        if (qd_probe_scratch((Size)QD_PATTERN_SCRATCH))
        {
            qd_native_ready = TRUE;
            return TRUE;
        }
        qd_dispose_pattern();
        qd_pattern_unavailable = TRUE;
        drv_notice("QuickDraw uses nearest display colors");
    }
    if (!qd_probe_scratch((Size)QD_FILL_SCRATCH))
    {
        qd_polygon_failed = TRUE;
        drv_abort_frame("QuickDraw drawing scratch unavailable");
        return FALSE;
    }
    qd_native_ready = TRUE;
    return TRUE;
}

/* Bit (x, y) of level k is black when the Bayer threshold is at least k:
 * the same bits as testing luminance < 4 * threshold + 2 per pixel, where
 * k = (luminance + 2) / 4. Build each level once, on first use. */
static const Pattern *qd_dither_pattern(unsigned int level)
{
    static const unsigned char bayer[8][8] = {
        {0,48,12,60,3,51,15,63}, {32,16,44,28,35,19,47,31},
        {8,56,4,52,11,59,7,55}, {40,24,36,20,43,27,39,23},
        {2,50,14,62,1,49,13,61}, {34,18,46,30,33,17,45,29},
        {10,58,6,54,9,57,5,53}, {42,26,38,22,41,25,37,21}
    };
    unsigned char *rows;
    int x, y;
    if (!qd_dither_built[level])
    {
        rows = (unsigned char *)&qd_dither[level];
        for (y = 0; y < 8; ++y)
        {
            rows[y] = 0;
            for (x = 0; x < 8; ++x)
                if (bayer[y][x] >= level) rows[y] |= (unsigned char)(0x80U >> x);
        }
        qd_dither_built[level] = TRUE;
    }
    return &qd_dither[level];
}

/* An Earth-table GWorld holds every Earth color, but Color2Index looks
 * colors up through an inverse table whose cells hide close neighbours
 * (the 255 distinct colors share only 125 four-bit cells). Probe each index
 * once per GWorld: an exact round trip lets a solid fill draw that pixel,
 * skipping MakeRGBPat's dither build and pattern re-expansion. */
static int qd_exact_color(unsigned char index, const RGBColor *rgb)
{
    RGBColor actual;
    unsigned int byte;
    unsigned char bit;
    if (!qd_buffered || !qd_world_earth) return FALSE;
    byte = index >> 3;
    bit = (unsigned char)(1U << (index & 7U));
    if (!(qd_exact_known[byte] & bit))
    {
        qd_exact_known[byte] |= bit;
        Index2Color(Color2Index(rgb), &actual);
        if (actual.red == rgb->red && actual.green == rgb->green &&
            actual.blue == rgb->blue) qd_exact_colors[byte] |= bit;
    }
    return (qd_exact_colors[byte] & bit) != 0;
}

/* Monochrome passes set black on white once; fills only pick a pattern. */
static void qd_flat_color(PolyHandle polygon, unsigned char index)
{
    RGBColor rgb;
    unsigned int luminance;
    if (qd_depth == 1)
    {
        luminance = (unsigned int)(((unsigned long)earth_cmap[index][0] * 77UL +
                                    (unsigned long)earth_cmap[index][1] * 150UL +
                                    (unsigned long)earth_cmap[index][2] * 29UL + 128UL) / 256UL);
        FillPoly(polygon, qd_dither_pattern((luminance + 2U) / 4U));
        return;
    }
    rgb.red = QD_COMPONENT(earth_cmap[index][0]);
    rgb.green = QD_COMPONENT(earth_cmap[index][1]);
    rgb.blue = QD_COMPONENT(earth_cmap[index][2]);
    if (qd_depth <= 8 && !qd_pattern_unavailable && !qd_exact_color(index, &rgb))
    {
        if (qd_pattern == NULL)
        {
            qd_pattern = NewPixPat();
            if (qd_pattern == NULL)
            {
                qd_pattern_unavailable = TRUE;
                drv_notice("QuickDraw uses nearest display colors");
            }
        }
        if (qd_pattern != NULL && qd_pattern_color != index)
        {
            MakeRGBPat(qd_pattern, &rgb);
            qd_pattern_color = index;
        }
        /* Keep the original RGB for Color QuickDraw's device-aware
         * dither, which can combine four palette colors. QuickDraw
         * reconverts cached patterns when the device table/depth changes;
         * preselecting an Amiga-style two-pen pair would restrict it. */
        if (qd_pattern != NULL)
        {
            FillCPoly(polygon, qd_pattern);
            return;
        }
    }
    RGBForeColor(&rgb);
    PaintPoly(polygon);
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *xyz1, Vertex TG_HUGE *xyz2,
                               Vertex TG_HUGE *xyz3, Vertex TG_HUGE *xyz4,
                               unsigned char color)
{
    if (!qd_ready) return;
    drv_submit_quad_as_batch(xyz1, xyz2, xyz3, xyz4, color);
}

/* One indexed packet shares transformed vertices between adjacent quads.
 * Classic QuickDraw still fills one polygon per retained quad. */
void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                    const Vertex TG_HUGE *vertices,
                                    unsigned int quad_count,
                                    const unsigned int TG_HUGE *indices,
                                    const unsigned char TG_HUGE *colors)
{
    Point projected[QD_MAX_POLY];
    unsigned int i, j, corners[4], reuse_count;
    int total, transformed;
    Drv_quad_context context;
    PolyHandle polygon;
    if (!qd_ready || drv_frame_failed || vertex_count == 0 || quad_count == 0 || vertices == NULL ||
        indices == NULL || colors == NULL) return;
    if (!drv_validate_quadbatch(vertex_count, quad_count, indices))
        qd_error("QuickDraw quad batch or vertex index is out of range");
    if (!qd_prepare_native()) return;
    reuse_count = drv_quadstream_reuse(&qd_packet_stream, vertex_count,
                                        vertices, &context);
    if (qd_has_fpu)
        transformed = qd_fpu_transform_reuse(vertex_count, vertices, reuse_count);
    else
        transformed = soft3d_fixed_transform_reuse(qd_fixed_matrix,
                             vertex_count, vertices, qd_fixed_points, reuse_count);
    if (!transformed)
    {
        drv_quadstream_reset(&qd_packet_stream);
        qd_error("QuickDraw coordinate is out of range");
    }
    (void)soft3d_project_cache_begin(&qd_project_cache, vertex_count,
                         drv_win_width, drv_win_height, reuse_count);
    drv_quadstream_commit(&qd_packet_stream, &context);
    for (i = 0; i < quad_count; ++i)
    {
        for (j = 0; j < 4U; ++j) corners[j] = indices[i * 4U + j];
        qd_packet_active = TRUE;
        total = qd_prepare_polygon(corners, 4U, vertex_count, projected);
        qd_packet_active = FALSE;
        if (total == 0) continue;
        polygon = qd_make_polygon(projected, total);
        if (polygon == NULL)
        {
            qd_polygon_failed = TRUE;
            drv_abort_frame("QuickDraw polygon allocation failed");
            return;
        }
        qd_flat_color(polygon, colors[i]);
        ++globe_quads_num_vis;
    }
}

/* Characters of text that fit in width pixels, breaking after the last
 * space. Adding each character's width keeps the scan linear instead of
 * remeasuring a growing prefix with TextWidth. */
static short qd_line_length(const char *text, short width)
{
    short count, last_space;
    long used;
    count = last_space = 0;
    used = 0L;
    while (text[count] != 0)
    {
        used += CharWidth((short)(unsigned char)text[count]);
        if (used > width) break;
        if (text[count] == ' ') last_space = count;
        ++count;
    }
    if (text[count] != 0 && last_space > 0) count = last_space;
    return count;
}

static void qd_draw_line(const char *text, short count, Int baseline,
                         const FontInfo *font)
{
    if (baseline >= (Int)font->ascent &&
        baseline + font->descent <= (Int)drv_win_height &&
        baseline <= 32767L)
    {
        MoveTo(8, (short)baseline);
        DrawText((Ptr)text, 0, count);
    }
}

/* Native font metrics keep both the statistics and help readable on a
 * 320-pixel window. A counting pass positions wrapped help at the bottom. */
static Int qd_osd_text(const char *text, Int baseline, short width,
                       const FontInfo *font, Int advance, int draw)
{
    short count;
    while (*text != 0)
    {
        count = qd_line_length(text, width);
        if (count == 0) break;
        if (draw) qd_draw_line(text, count, baseline, font);
        baseline += advance;
        text += count;
        while (*text == ' ') ++text;
    }
    return baseline;
}

/* drv_help is fixed after start-up: break it once per font, size and width
 * instead of measuring it twice every frame. Unusually many lines leave
 * the cache invalid and fall back to measuring. */
static void qd_wrap_help(short width)
{
    const char *text;
    short count;
    if (qd_help_valid && qd_help_font == qd_font_id &&
        qd_help_size == qd_font_size && qd_help_width == width) return;
    qd_help_valid = FALSE;
    qd_help_lines = 0;
    text = drv_help;
    while (*text != 0)
    {
        count = qd_line_length(text, width);
        if (count == 0) break;
        if (qd_help_lines == QD_HELP_LINES) return;
        qd_help_start[qd_help_lines] = (unsigned char)(text - drv_help);
        qd_help_count[qd_help_lines] = (unsigned char)count;
        ++qd_help_lines;
        text += count;
        while (*text == ' ') ++text;
    }
    qd_help_font = qd_font_id;
    qd_help_size = qd_font_size;
    qd_help_width = width;
    qd_help_valid = TRUE;
}

void drv_draw_osd(void)
{
    char status[224];
    RGBColor white;
    short width;
    unsigned int line;
    Int advance, next_line, help_baseline, help_height;
    FontInfo font;
    if (!qd_ready || drv_win_width <= 16 || drv_win_height < 16) return;
    if (qd_depth > 1)
    {
        white.red = white.green = white.blue = 65535U;
        RGBForeColor(&white);
    }
    else ForeColor(blackColor);
    TextFont(qd_font_id);
    TextSize(qd_font_size);
    GetFontInfo(&font);
    advance = (Int)font.ascent + font.descent + font.leading;
    if (font.ascent <= 0 || font.descent < 0 ||
        font.leading < 0 || advance <= 0) return;
    TextMode(srcCopy);
    drv_format_stats(status);
    sprintf(status + strlen(status), ", %s, GWorld %s",
             qd_has_fpu ? "68881 math" : "integer math",
             qd_world != NULL ? "on" : "off");
    if (qd_depth > 1 && qd_depth <= 8 &&
        (qd_palette_missing() || qd_pattern_unavailable))
        strcat(status, ", color reduced");
    if (qd_frame_paused)
        drv_append_status(status, sizeof(status), ", paused; use a control to retry");
    width = (short)(drv_win_width - 16U);
    next_line = qd_osd_text(status, 4L + font.ascent, width,
                            &font, advance, TRUE);
    qd_wrap_help(width);
    help_height = qd_help_valid ? (Int)qd_help_lines * advance :
                  qd_osd_text(drv_help, 0L, width, &font, advance, FALSE);
    help_baseline = (Int)drv_win_height - 4L - font.descent -
                     help_height + advance;
    if (help_baseline < next_line) return;
    if (!qd_help_valid)
    {
        qd_osd_text(drv_help, help_baseline, width, &font, advance, TRUE);
        return;
    }
    for (line = 0; line < qd_help_lines; ++line)
        qd_draw_line(drv_help + qd_help_start[line], (short)qd_help_count[line],
                     help_baseline + (Int)line * advance, &font);
}

/* Copy the locked GWorld to the window. Both bitmaps are portBits: a basic
 * QuickDraw GWorld has no PixMap, and a dereferenced, unlocked PixMap
 * handle could move while CopyBits allocates. */
static void qd_present(void)
{
    Rect bounds;
    RGBColor saved_foreground, saved_background, black, white;
    if (qd_world == NULL) return;
    SetPort((GrafPtr)qd_window);
    SetRect(&bounds, 0, 0, (short)drv_win_width, (short)drv_win_height);
    if (!qd_color_available)
    {
        /* srcCopy maps through these; basic QuickDraw has no RGB colors. */
        ForeColor(blackColor);
        BackColor(whiteColor);
        CopyBits(&((GrafPtr)qd_world)->portBits, &qd_window->portBits,
                  &bounds, &bounds, srcCopy, NULL);
        return;
    }
    /* srcCopy colorizes through the destination port's colors. Direct
     * rendering during a GWorld allocation failure can change those colors. */
    GetForeColor(&saved_foreground);
    GetBackColor(&saved_background);
    black.red = black.green = black.blue = 0;
    white.red = white.green = white.blue = 65535U;
    RGBForeColor(&black);
    RGBBackColor(&white);
    /* Color QuickDraw's copy mode performs final device-depth conversion.
     * Dithering stays on: the device table follows other applications, so
     * it is never known to match the GWorld's own table. */
    CopyBits(&((GrafPtr)qd_world)->portBits, &qd_window->portBits,
              &bounds, &bounds, srcCopy | ditherCopy, NULL);
    RGBForeColor(&saved_foreground);
    RGBBackColor(&saved_background);
}

static void qd_render_pass(void)
{
    CGrafPtr old_world;
    GDHandle old_device;
    PixMapHandle pixels;
    Rect bounds;
    RGBColor background;
    if (!qd_ready || qd_rendering || qd_window == NULL) return;
    drv_fps_start();
    qd_begin_render();
    old_world = NULL;
    old_device = NULL;
    if (qd_world != NULL)
    {
        pixels = qd_world_pixels();
        if (LockPixels(pixels))
        {
            qd_locked_pixels = pixels;
            GetGWorld(&old_world, &old_device);
            SetGWorld(qd_world, NULL);
            qd_buffered = TRUE;
            qd_world_shown = FALSE; /* Until this pass presents a frame. */
        }
        else qd_dispose_buffer();
    }
    if (!qd_buffered) SetPort((GrafPtr)qd_window);
    SetRect(&bounds, 0, 0, (short)drv_win_width, (short)drv_win_height);
    if (qd_depth > 1)
    {
        background.red = background.green = background.blue = 0;
        RGBBackColor(&background);
    }
    else
    {
        /* Every monochrome fill draws black on white: set it once per pass. */
        ForeColor(blackColor);
        BackColor(whiteColor);
    }
    EraseRect(&bounds);
    PenNormal();
    globe_quads_num_vis = 0;
    if (globe_ready() && !qd_frame_paused)
    {
        qd_set_transform();
        globe_draw_quadbatches();
    }
    if (drv_frame_failed)
    {
        /* Do not present an incomplete offscreen frame, or release its
         * pixels while their port is selected. Recovery runs after unwind. */
        qd_finish_render();
        drv_reset_timing();
        return;
    }
    if (qd_frame_paused)
        drv_frame_failed = TRUE; /* Retain the actual failure notice. */
    if (drv_status_required()) drv_draw_osd();
    if (qd_buffered)
    {
        SetGWorld(old_world, old_device);
        qd_present();
        qd_world_shown = TRUE;
    }
    SetPort((GrafPtr)qd_window);
    DrawGrowIcon(qd_window);
    /* Only a whole new frame may validate the window's update region. */
    qd_frame_complete = !drv_frame_failed;
    qd_finish_render();
    qd_dirty = FALSE;
    drv_fps_stop();
}

void drv_render(void)
{
    int attempt;
    qd_frame_complete = FALSE;
    if (!qd_ready || qd_rendering || qd_window == NULL) return;
    qd_keep_reserve();
    for (attempt = 0; attempt < 2; ++attempt)
    {
        qd_polygon_failed = FALSE;
        qd_render_pass();
        if (!drv_frame_failed || qd_frame_paused) return;
        if (!qd_polygon_failed) break;
        if (attempt == 0) drv_release_optional();
    }
    /* Both complete-frame attempts failed. Keep a usable event loop and
     * paint a status view; only a subsequent user action retries geometry. */
    qd_frame_paused = TRUE;
    drv_auto_enabled = FALSE;
    qd_polygon_failed = FALSE;
    qd_render_pass();
}

/* Copy the frame the GWorld already holds for an update event that brings
 * no scene change. Returns FALSE when a new frame must be drawn instead. */
static int qd_present_held(void)
{
    PixMapHandle pixels;
    if (qd_dirty || !qd_world_shown || qd_world == NULL || qd_rendering)
        return FALSE;
    qd_begin_render();
    pixels = qd_world_pixels();
    if (LockPixels(pixels))
    {
        qd_locked_pixels = pixels;
        qd_present();
        DrawGrowIcon(qd_window);
    }
    else qd_world_shown = FALSE;
    qd_finish_render();
    return qd_world_shown;
}

/* Update events repaint pixels; they need a new scene only when it changed
 * or nothing holds the last frame. That frame is drawn whole, not clipped
 * to the update region, so no part of the window keeps an older one. */
static void qd_update(void)
{
    int held;
    held = FALSE;
    if (!qd_dirty && qd_world_shown)
    {
        BeginUpdate(qd_window);
        held = qd_present_held();
        EndUpdate(qd_window);
    }
    if (held) return;
    drv_render();
    BeginUpdate(qd_window);
    EndUpdate(qd_window);
}

/* A whole new frame covers the update region a grow, drag or activation
 * left behind; without this its update event would draw the scene again. */
static void qd_validate(void)
{
    GrafPtr port;
    if (!qd_frame_complete || qd_window == NULL) return;
    qd_frame_complete = FALSE;
    GetPort(&port);
    SetPort((GrafPtr)qd_window);
    ValidRect(&qd_window->portRect);
    SetPort(port);
}

static void qd_keyboard(unsigned char key)
{
    int rebuild, handled;
    if (!qd_ready || qd_rendering) return;
    if (key == 27) { drv_close(); return; }
    rebuild = key >= '1' && key <= '5';
    if (rebuild) qd_busy(TRUE);
    /* Mesh storage and Earth resources are checked allocations. */
    qd_checked = rebuild;
    handled = drv_handle_key(key);
    qd_checked = FALSE;
    qd_busy(FALSE);
    if (!handled) return;
    qd_frame_paused = FALSE;
    /* A successful rebuild or a retry may bring released buffers back. */
    qd_restore_optional();
    /* Mesh recovery may have taken time; it is not animation elapsed time. */
    qd_auto_tick = TickCount();
    qd_dirty = TRUE;
}

static void qd_mouse_move(int x, int y)
{
    if (!qd_ready || qd_rendering) return;
    if (drv_lmouse_pressed)
    {
        drv_rotate_drag((long)x - qd_old_x, (long)y - qd_old_y);
        qd_dirty = TRUE;
    }
    qd_old_x = x; qd_old_y = y;
}

/* The About box reuses the error alert's items without its sound. */
static void qd_about(void)
{
    char about[128];
    Str255 text;
    static const unsigned char empty[] = "";
    sprintf(about, "%s %d.%d draws an Earth globe with QuickDraw.",
            TESTGLOBE_NAME, TESTGLOBE_VERSION_MAJOR, TESTGLOBE_VERSION_MINOR);
    qd_pascal(text, about);
    ParamText(text, empty, empty, empty);
    (void)NoteAlert(QD_ABOUT_ALERT, NULL);
    /* Time spent in the modal alert is not animation time. */
    qd_auto_tick = TickCount();
    drv_reset_timing();
}

static void qd_menu(long selection)
{
    short menu, item;
    Str255 name;
    GrafPtr port;
    menu = (short)(selection >> 16);
    item = (short)selection;
    if (menu == QD_FILE_MENU && item == 1) drv_close();
    else if (menu == QD_APPLE_MENU && item == 1) qd_about();
    else if (menu == QD_APPLE_MENU && item > 2)
    {
        /* A desk accessory may leave its own port selected. */
        GetPort(&port);
        GetMenuItemText(qd_apple_menu, item, name);
        (void)OpenDeskAcc(name);
        SetPort(port);
    }
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
    if (!qd_ready || !qd_running || qd_rendering || event == NULL) return;
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
                        /* The gray region spans every monitor. Moved pixels
                         * stay valid; only a new device needs a new frame. */
                        limit = (**GetGrayRgn()).rgnBBox;
                        DragWindow(window, event->where, &limit);
                        qd_refresh_display();
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
                            qd_resize((short)dimensions, (short)(dimensions >> 16));
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
            if ((WindowPtr)event->message == qd_window) qd_update();
            break;
        case activateEvt:
            if ((WindowPtr)event->message == qd_window)
            {
                qd_active = (event->modifiers & activeFlag) != 0;
                qd_auto_tick = TickCount();
                if (!qd_active) drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
                if (qd_active) qd_refresh_display();
                qd_dirty = TRUE;
            }
            break;
        case osEvt:
            if (((unsigned long)event->message >> 24) == suspendResumeMessage)
            {
                qd_active = (event->message & resumeFlag) != 0;
                qd_auto_tick = TickCount();
                if (qd_active)
                {
                    qd_refresh_display();
                    qd_dirty = TRUE;
                }
                else drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
            }
            break;
        case kHighLevelEvent:
            if (qd_apple_events) (void)AEProcessAppleEvent(event);
            break;
        default: break;
    }
}

/* Only the frontmost copy animates or tracks a drag. A background copy
 * asking for one tick would force sixty process switches a second. */
static unsigned long qd_sleep(void)
{
    if (!qd_active) return QD_BACKGROUND_SLEEP;
    return (drv_animation_allowed() && !qd_frame_paused) || drv_lmouse_pressed ?
           1UL : QD_IDLE_SLEEP;
}

void drv_loop(void)
{
    EventRecord event;
    Point point;
    unsigned long tick, elapsed;
    if (!qd_ready || qd_window == NULL) return;
    qd_auto_tick = TickCount();
    while (qd_running)
    {
        if (WaitNextEvent(everyEvent, &event, qd_sleep(), NULL))
            qd_event(&event);
        if (!qd_running) break;
        if (qd_active && drv_lmouse_pressed)
        {
            SetPort((GrafPtr)qd_window);
            GetMouse(&point);
            qd_mouse_move(point.h, point.v);
            if (!Button()) drv_lmouse_pressed = FALSE;
        }
        if (qd_active && drv_animation_allowed() && !qd_frame_paused)
        {
            tick = TickCount();
            elapsed = (tick - qd_auto_tick) & 0xffffffffUL;
            qd_auto_tick = tick;
            drv_rotate_elapsed((double)elapsed / 60.0);
            qd_dirty = TRUE;
        }
        else qd_auto_tick = TickCount();
        if (qd_active && qd_dirty)
        {
            drv_render();
            qd_validate();
        }
    }
    qd_cleanup();
}
#endif /* DRV_QD */
