/* AmigaOS call records for the production Workbench driver. No chipset emulation. */
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Amiga.h>
#include "testglob.h"
#include "drv.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"
#include "earth_resources.h"
static unsigned long baseline_vertices, enhanced_vertices, fpu_vertices;
static int fail_cache_allocation;
static void *ami_test_malloc(size_t size);
static int record_baseline(long matrix[3][3], unsigned int count,
                           const Vertex *vertices, Soft3d_fixed_point *out,
                           unsigned int reuse);
static int record_enhanced(long matrix[3][3], unsigned int count,
                           const Vertex *vertices, Soft3d_fixed_point *out,
                           unsigned int reuse);
static int record_fpu(float matrix[3][3], unsigned int count,
                      const Vertex *vertices, Soft3d_float_point *out,
                      unsigned int reuse, int precision);
#define soft3d_fixed_transform_reuse record_baseline
#define soft3d_020_transform_reuse record_enhanced
#define soft3d_float_transform_reuse record_fpu
#define malloc ami_test_malloc
#include "../src/drv_ami.c"
#undef malloc
#undef soft3d_fixed_transform_reuse
#undef soft3d_020_transform_reuse
#undef soft3d_float_transform_reuse
static void *ami_test_malloc(size_t size)
{
    if (fail_cache_allocation && size == sizeof(Soft3d_project_cache)) return NULL;
    return malloc(size);
}
static int record_baseline(long matrix[3][3], unsigned int count,
                           const Vertex *vertices, Soft3d_fixed_point *out,
                           unsigned int reuse)
{
    assert(ami_cpu < 68020 && !ami_has_fpu); baseline_vertices += count - reuse;
    return soft3d_fixed_transform_reuse(matrix, count, vertices, out, reuse);
}
static int record_enhanced(long matrix[3][3], unsigned int count,
                           const Vertex *vertices, Soft3d_fixed_point *out,
                           unsigned int reuse)
{
    assert(ami_cpu >= 68020 && !ami_has_fpu); enhanced_vertices += count - reuse;
    return soft3d_020_transform_reuse(matrix, count, vertices, out, reuse);
}
static int record_fpu(float matrix[3][3], unsigned int count,
                      const Vertex *vertices, Soft3d_float_point *out,
                      unsigned int reuse, int precision)
{
    assert(ami_cpu >= 68020 && ami_has_fpu); fpu_vertices += count - reuse;
    return soft3d_float_transform_reuse(matrix, count, vertices, out, reuse, precision);
}

static struct ExecBase exec_base;
static struct Task task_record;
static unsigned char task_stack[32768];
struct ExecBase *SysBase = &exec_base;
static struct GfxBase graphics_base;
static struct IntuitionBase intuition_base;
static struct Screen desktop;
static struct ColorMap colormap;
static struct Window window_record;
static struct RastPort window_port;
static struct MsgPort window_messages;
static struct TextFont font;
static unsigned char desktop_palette[256][3];
static unsigned int library_count, window_count, raster_count;
static unsigned int raster_calls, raster_fail_at, area_calls, area_failures;
static unsigned int copy_calls, text_calls, refresh_count, reply_count, rect_calls;
static unsigned int resource_calls, resource_max, resource_first, resource_failure;
static unsigned int area_count;
static long area_x[16], area_y[16];
static unsigned long rendered_hash, rendered_count;
static int blitter_busy, event_outstanding, small_stack;
static unsigned long clock_micros;
static struct IntuiMessage messages[8];
static unsigned int message_count, message_next;
static struct IntuiMessage fullscreen_events[8];
static unsigned int fullscreen_event_count, fullscreen_event_next;
static char osd_text[4096];
static void *rasters[32];
static ULONG raster_sizes[32];
static unsigned int display_version = 34;
static unsigned int pixel_x = 1, pixel_y = 1;
static unsigned int pubscreen_locks, friend_count, friend_calls;
static struct BitMap friend_bitmap;
static unsigned int rtg_backend, rtg_depth, rtg_storage_depth;
static int friend_failure, pubscreen_failure;
static int rtg_unknown, rtg_both_libraries, rtg_libraries_native;
static struct Library cgx_library, p96_library;
/* Fullscreen records model two display surfaces and independent notification
 * delivery. Drawing calls must never touch a displayed or not-yet-safe bitmap. */
static struct Screen fullscreen_screen;
static struct ColorMap fullscreen_colormap;
static unsigned char fullscreen_palette[256][3];
static struct Window fullscreen_window;
static struct RastPort fullscreen_port;
static struct MsgPort fullscreen_messages, fullscreen_reply_ports[2];
static struct ScreenBuffer fullscreen_buffers[2];
static struct DBufInfo fullscreen_dbuf[2];
static struct BitMap fullscreen_alternate;
static UWORD fullscreen_plane_markers[2][8];
static struct BitMap *displayed_bitmap, *previous_bitmap;
static struct Message *pending_safe, *pending_display;
static unsigned int screen_count, fullscreen_window_count, screenbuffer_count;
static unsigned int reply_port_count, reply_port_calls, screen_calls, screenbuffer_calls, swap_calls, swap_successes;
static unsigned int palette_writes, activate_calls, screen_front_calls;
static unsigned int buffer_alive[2], reply_port_alive[2];
static unsigned int screen_failures, fullscreen_window_failures, buffer_fail_at, port_fail_at;
static unsigned int failed_swaps, hold_safe, hold_display, notification_waits;
static ULONG fullscreen_mode;
static int fullscreen_incompatible;
static int private_screen_visible;
static unsigned int pointer_base = 16;
static unsigned int fullscreen_loop_script, fullscreen_loop_step, hold_after_safe;

static unsigned int test_pointer_base(void)
{
    return !rtg_backend && fullscreen_screen.BitMap.Depth == 8U ? pointer_base : 16U;
}

static int test_screen_bitmap(const struct BitMap *bitmap)
{
    return screen_count && (bitmap == &fullscreen_screen.BitMap || bitmap == &fullscreen_alternate);
}

static void test_safe_draw(const struct RastPort *port)
{
    if (!test_screen_bitmap(port->BitMap)) return;
    if (private_screen_visible)
    {
        assert(port->BitMap != displayed_bitmap);
        assert(pending_safe == NULL || port->BitMap != previous_bitmap);
    }
    assert(port->Layer == NULL);
}
#if AMI_RTG
static int friend_incompatible;
static unsigned int cgx_calls, p96_calls, span_calls;
static int coverage_active;
static unsigned int coverage[64][64];
static ULONG coverage_rgb;
static int extreme_active;
static const Soft3d_screen_point *extreme_polygon;
static unsigned int extreme_pixels[32767];
static int test_inside(const Soft3d_screen_point *, unsigned int,
                       unsigned int, unsigned int);
#endif

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    assert(!event_outstanding);
    if (strcmp(name, "cybergraphics.library") == 0 ||
        strcmp(name, "Picasso96API.library") == 0)
    {
        int cgx;
        cgx = strcmp(name, "cybergraphics.library") == 0;
        assert(display_version >= 39 && version <= 41);
        if (rtg_unknown || (!rtg_libraries_native && !rtg_both_libraries &&
            rtg_backend != (unsigned int)(cgx ? 1 : 2))) return NULL;
        ++library_count;
        return cgx ? &cgx_library : &p96_library;
    }
    assert(version <= display_version);
    ++library_count;
    if (strcmp(name, "graphics.library") == 0) return (struct Library *)&graphics_base;
    assert(strcmp(name, "intuition.library") == 0);
    return (struct Library *)&intuition_base;
}
struct Task *FindTask(CONST_STRPTR name)
{ assert(name == NULL); task_record.tc_SPLower = task_stack; task_record.tc_SPUpper = task_stack + (small_stack ? 4096 : sizeof(task_stack)); return &task_record; }
void CloseLibrary(struct Library *library)
{ assert(library != NULL && library_count != 0); --library_count; }
APTR AllocMem(ULONG size, ULONG flags)
{ assert((flags & MEMF_CHIP) == 0); return flags & MEMF_CLEAR ? calloc(1, size) : malloc(size); }
void FreeMem(APTR memory, ULONG size) { (void)size; free(memory); }
struct Message *GetMsg(struct MsgPort *port)
{
    struct Message *message;
    unsigned int i;
    assert(!event_outstanding);
    for (i = 0; i < 2U; ++i)
        if (port == &fullscreen_reply_ports[i])
        {
            assert(reply_port_alive[i]);
            if (pending_safe != NULL && pending_safe->mn_ReplyPort == port && !hold_safe)
            {
                message = pending_safe; pending_safe = NULL;
                if (hold_after_safe) { hold_safe = hold_display = 1; hold_after_safe = 0; }
                return message;
            }
            if (pending_display != NULL && pending_display->mn_ReplyPort == port && !hold_display)
            { message = pending_display; pending_display = NULL; return message; }
            return NULL;
        }
    if (port == &fullscreen_messages)
    {
        assert(fullscreen_window_count);
        if (fullscreen_event_next == fullscreen_event_count) return NULL;
        event_outstanding = 1;
        return (struct Message *)&fullscreen_events[fullscreen_event_next++];
    }
    assert(port == &window_messages);
    if (message_next == message_count) return NULL;
    event_outstanding = 1;
    return (struct Message *)&messages[message_next++];
}
void ReplyMsg(struct Message *message)
{ assert(message != NULL && event_outstanding); event_outstanding = 0; ++reply_count; }
ULONG Wait(ULONG mask)
{
    assert(!event_outstanding);
    if (fullscreen_loop_script)
    {
        if (fullscreen_loop_step < 4U)
            assert((mask & ((1UL << 4) | (1UL << 5) | (1UL << 6) | (1UL << 7))) ==
                   ((1UL << 4) | (1UL << 5) | (1UL << 6) | (1UL << 7)));
        switch (fullscreen_loop_step++)
        {
            case 0:
                assert(pending_safe != NULL && pending_display != NULL);
                hold_display = 0;
                return 1UL << 7;
            case 1:
                assert(pending_safe != NULL && pending_display == NULL);
                hold_safe = 0; hold_after_safe = 1;
                fullscreen_events[0].Class = VANILLAKEY; fullscreen_events[0].Code = 'D';
                fullscreen_events[1].Class = VANILLAKEY; fullscreen_events[1].Code = 'O';
                fullscreen_events[2].Class = VANILLAKEY; fullscreen_events[2].Code = '2';
                fullscreen_event_count = 3;
                return (1UL << 6) | (1UL << 5);
            case 2:
                assert(pending_safe != NULL && pending_display != NULL);
                assert(globe_longitude == 64 && !drv_osd_enabled && ami_dither_enabled);
                /* Returning to Workbench through the OS leaves the original
                 * window alive. Its F must still be delivered while fullscreen. */
                messages[1].Class = VANILLAKEY; messages[1].Code = 'F'; message_count = 2;
                return 1UL << 4;
            case 3:
                assert(screen_count == 1 && pending_safe != NULL && pending_display != NULL);
                hold_safe = hold_display = 0;
                return (1UL << 6) | (1UL << 7);
            case 4:
                assert(screen_count == 0 && ami_window == &window_record);
                assert(mask == (1UL << window_messages.mp_SigBit));
                messages[2].Class = CLOSEWINDOW; message_count = 3;
                return mask;
            default: assert(!"Unexpected fullscreen event-loop wait");
        }
    }
    if (pending_safe != NULL || pending_display != NULL)
    {
        ULONG notification_mask;
        notification_mask = 0UL;
        if (pending_safe != NULL) notification_mask |= 1UL << pending_safe->mn_ReplyPort->mp_SigBit;
        if (pending_display != NULL) notification_mask |= 1UL << pending_display->mn_ReplyPort->mp_SigBit;
        assert(mask & notification_mask);
        hold_safe = hold_display = 0;
        ++notification_waits;
        return mask & notification_mask;
    }
    assert(mask & ((1UL << window_messages.mp_SigBit) | (1UL << fullscreen_messages.mp_SigBit)));
    if (ami_window == &fullscreen_window)
    {
        assert(fullscreen_event_count < 8U);
        fullscreen_events[fullscreen_event_count++].Class = CLOSEWINDOW;
    }
    else
    {
        assert(message_count < 8U);
        messages[message_count++].Class = CLOSEWINDOW;
    }
    return mask;
}
struct MsgPort *CreateMsgPort(void)
{
    unsigned int i;
    ++reply_port_calls;
    if (port_fail_at && reply_port_calls == port_fail_at) return NULL;
    for (i = 0; i < 2U && reply_port_alive[i]; ++i) ;
    assert(i < 2U);
    reply_port_alive[i] = 1; ++reply_port_count;
    fullscreen_reply_ports[i].mp_SigBit = (UBYTE)(6U + i);
    return &fullscreen_reply_ports[i];
}
void DeleteMsgPort(struct MsgPort *port)
{
    unsigned int i;
    for (i = 0; i < 2U && port != &fullscreen_reply_ports[i]; ++i) ;
    assert(i < 2U && reply_port_alive[i]);
    assert(pending_safe == NULL || pending_safe->mn_ReplyPort != port);
    assert(pending_display == NULL || pending_display->mn_ReplyPort != port);
    reply_port_alive[i] = 0; --reply_port_count;
}
void CurrentTime(ULONG *seconds, ULONG *micros)
{ clock_micros += 16667UL; *seconds = clock_micros / 1000000UL; *micros = clock_micros % 1000000UL; }
LONG GetScreenData(APTR out, ULONG size, ULONG kind, struct Screen *screen)
{
    (void)screen; assert(size == sizeof(desktop) && kind == WBENCHSCREEN);
    memcpy(out, &desktop, sizeof(desktop)); return 1;
}
struct Screen *LockPubScreen(CONST_STRPTR name)
{
    assert(display_version >= 36);
    assert(name == NULL || strcmp(name, "Workbench") == 0);
    if (pubscreen_failure) return NULL;
    ++pubscreen_locks; return &desktop;
}
void UnlockPubScreen(CONST_STRPTR name, struct Screen *screen)
{
    assert(name == NULL || strcmp(name, "Workbench") == 0);
    assert(screen == &desktop && pubscreen_locks != 0); --pubscreen_locks;
}
struct Screen *OpenScreenTagList(const struct NewScreen *settings, const struct TagItem *tags)
{
    ULONG width, height, depth, type;
    unsigned int plane;
    int behind, quiet, exclusive, draggable;
    assert(settings == NULL && display_version >= 39 && !event_outstanding && screen_count == 0);
    ++screen_calls;
    if (screen_failures) { --screen_failures; return NULL; }
    width = height = depth = type = 0;
    behind = quiet = exclusive = 0; draggable = 1;
    for (; tags->ti_Tag != TAG_DONE; ++tags)
        switch (tags->ti_Tag)
        {
            case SA_Width: width = tags->ti_Data; break;
            case SA_Height: height = tags->ti_Data; break;
            case SA_Depth: depth = tags->ti_Data; break;
            case SA_Type: type = tags->ti_Data; break;
            case SA_DisplayID: fullscreen_mode = tags->ti_Data; break;
            case SA_Behind: behind = tags->ti_Data != 0; break;
            case SA_Quiet: quiet = tags->ti_Data != 0; break;
            case SA_Exclusive: exclusive = tags->ti_Data != 0; break;
            case SA_Draggable: draggable = tags->ti_Data != 0; break;
            case SA_ShowTitle: assert(tags->ti_Data == 0); break;
            case SA_Title: assert(tags->ti_Data != 0); break;
            default: assert(!"Unexpected screen tag");
        }
    assert(type == CUSTOMSCREEN && behind && quiet && exclusive && !draggable);
    if (rtg_backend)
    {
        assert(fullscreen_mode == 0x50000UL && width == (ULONG)desktop.Width &&
               height == (ULONG)desktop.Height && depth == rtg_depth);
    }
    else
    {
        assert(width == 320UL && height == ((graphics_base.DisplayFlags & PAL) ? 256UL : 200UL));
        assert(fullscreen_mode == ((graphics_base.DisplayFlags & PAL) ? PAL_MONITOR_ID : NTSC_MONITOR_ID));
        assert(depth == 5UL || ((graphics_base.ChipRevBits0 & GFXF_AA_ALICE) && depth == 8UL));
    }
    memset(&fullscreen_screen, 0, sizeof(fullscreen_screen));
    fullscreen_screen.Width = (WORD)width; fullscreen_screen.Height = (WORD)height;
    InitBitMap(&fullscreen_screen.BitMap, depth, width, height);
    for (plane = 0; plane < 8U; ++plane)
        fullscreen_screen.BitMap.Planes[plane] = (PLANEPTR)&fullscreen_plane_markers[0][plane];
    if (rtg_backend)
    { fullscreen_screen.BitMap.Depth = 1; fullscreen_screen.BitMap.BytesPerRow = 65535U; }
    InitRastPort(&fullscreen_screen.RastPort);
    fullscreen_screen.RastPort.BitMap = &fullscreen_screen.BitMap;
    fullscreen_colormap.Count = depth <= 8UL ? (UWORD)(1UL << depth) : 256U;
    fullscreen_screen.ViewPort.ColorMap = &fullscreen_colormap;
    memset(fullscreen_palette, 0, sizeof(fullscreen_palette));
    /* The system's pointer pens must survive the private palette install. */
    fullscreen_palette[test_pointer_base() + 1U][0] = 17;
    fullscreen_palette[test_pointer_base() + 2U][1] = 18;
    fullscreen_palette[test_pointer_base() + 3U][2] = 19;
    displayed_bitmap = &fullscreen_screen.BitMap;
    private_screen_visible = 0;
    ++screen_count;
    return &fullscreen_screen;
}
BOOL CloseScreen(struct Screen *screen)
{
    assert(screen == &fullscreen_screen && screen_count == 1);
    assert(!fullscreen_window_count && !screenbuffer_count && !pending_safe && !pending_display && !blitter_busy);
    --screen_count; displayed_bitmap = previous_bitmap = NULL;
    return TRUE;
}
void ScreenToFront(struct Screen *screen)
{
    assert((screen == &fullscreen_screen && screen_count) || screen == &desktop);
    private_screen_visible = screen == &fullscreen_screen;
    ++screen_front_calls;
}
void ActivateWindow(struct Window *window)
{ assert((window == &window_record && window_count) || (window == &fullscreen_window && fullscreen_window_count)); ++activate_calls; }
struct ScreenBuffer *AllocScreenBuffer(struct Screen *screen, struct BitMap *bitmap, ULONG flags)
{
    unsigned int i, plane;
    assert(screen == &fullscreen_screen && screen_count && bitmap == NULL && display_version >= 39);
    ++screenbuffer_calls;
    if (buffer_fail_at && screenbuffer_calls == buffer_fail_at) return NULL;
    i = flags & SB_SCREEN_BITMAP ? 0U : 1U;
    assert(flags == (i == 0U ? SB_SCREEN_BITMAP : 0UL) && !buffer_alive[i]);
    memset(&fullscreen_dbuf[i], 0, sizeof(fullscreen_dbuf[i]));
    if (i == 1U)
    {
        InitBitMap(&fullscreen_alternate, fullscreen_screen.BitMap.Depth,
                   (ULONG)fullscreen_screen.Width, (ULONG)fullscreen_screen.Height);
        for (plane = 0; plane < 8U; ++plane)
            fullscreen_alternate.Planes[plane] = (PLANEPTR)&fullscreen_plane_markers[1][plane];
        if (rtg_backend)
        { fullscreen_alternate.Depth = 1; fullscreen_alternate.BytesPerRow = 65535U; }
        else if (fullscreen_incompatible) fullscreen_alternate.BytesPerRow += 2U;
    }
    fullscreen_buffers[i].sb_BitMap = i == 0U ? &fullscreen_screen.BitMap : &fullscreen_alternate;
    fullscreen_buffers[i].sb_DBufInfo = &fullscreen_dbuf[i];
    buffer_alive[i] = 1; ++screenbuffer_count;
    return &fullscreen_buffers[i];
}
void FreeScreenBuffer(struct Screen *screen, struct ScreenBuffer *buffer)
{
    unsigned int i;
    if (buffer == NULL) return;
    assert(screen == &fullscreen_screen && screen_count && !blitter_busy);
    i = buffer == &fullscreen_buffers[0] ? 0U : 1U;
    assert(buffer == &fullscreen_buffers[i] && buffer_alive[i]);
    assert(pending_safe != &fullscreen_dbuf[i].dbi_SafeMessage && pending_display != &fullscreen_dbuf[i].dbi_DispMessage);
    buffer_alive[i] = 0; --screenbuffer_count;
}
ULONG ChangeScreenBuffer(struct Screen *screen, struct ScreenBuffer *buffer)
{
    unsigned int i;
    assert(screen == &fullscreen_screen && screen_count && display_version >= 39);
    i = buffer == &fullscreen_buffers[0] ? 0U : 1U;
    assert(buffer == &fullscreen_buffers[i] && buffer_alive[i] && !blitter_busy);
    assert(pending_safe == NULL && pending_display == NULL);
    assert(buffer->sb_BitMap != displayed_bitmap);
    ++swap_calls;
    if (failed_swaps) { --failed_swaps; return FALSE; }
    previous_bitmap = displayed_bitmap; displayed_bitmap = buffer->sb_BitMap;
    pending_safe = &buffer->sb_DBufInfo->dbi_SafeMessage;
    pending_display = &buffer->sb_DBufInfo->dbi_DispMessage;
    assert(pending_safe->mn_ReplyPort != NULL && pending_display->mn_ReplyPort != NULL);
    ++swap_successes;
    return TRUE;
}
void SetRGB32(struct ViewPort *view, ULONG pen, ULONG red, ULONG green, ULONG blue)
{
    unsigned int bits;
    assert(view == &fullscreen_screen.ViewPort && screen_count && pen < fullscreen_colormap.Count);
    assert(pen <= test_pointer_base() || pen > test_pointer_base() + 3U);
    bits = rtg_backend || (graphics_base.ChipRevBits0 & GFXF_AA_ALICE) ? 8U : 4U;
    fullscreen_palette[pen][0] = (UBYTE)(bits == 8U ? red >> 24 : (red >> 28) * 17UL);
    fullscreen_palette[pen][1] = (UBYTE)(bits == 8U ? green >> 24 : (green >> 28) * 17UL);
    fullscreen_palette[pen][2] = (UBYTE)(bits == 8U ? blue >> 24 : (blue >> 28) * 17UL);
    ++palette_writes;
}
ULONG VideoControl(struct ColorMap *colors, struct TagItem *tags)
{
    assert(colors == &fullscreen_colormap && screen_count && display_version >= 39);
    assert(tags[0].ti_Tag == VTAG_SPEVEN_BASE_GET && tags[1].ti_Tag == TAG_DONE);
    tags[0].ti_Data = pointer_base;
    return 0;
}
struct Window *OpenWindow(const struct NewWindow *settings)
{
    if (settings->Type == CUSTOMSCREEN)
    {
        assert(settings->Screen == &fullscreen_screen && screen_count && !fullscreen_window_count);
        assert((settings->Flags & (BORDERLESS | GIMMEZEROZERO | SIMPLE_REFRESH)) ==
               (BORDERLESS | GIMMEZEROZERO | SIMPLE_REFRESH));
        assert(!(settings->Flags & (WINDOWCLOSE | WINDOWDRAG | WINDOWSIZING)));
        assert(settings->Width == fullscreen_screen.Width && settings->Height == fullscreen_screen.Height);
        if (fullscreen_window_failures) { --fullscreen_window_failures; return NULL; }
        memset(&fullscreen_window, 0, sizeof(fullscreen_window));
        InitRastPort(&fullscreen_port); fullscreen_port.BitMap = &fullscreen_screen.BitMap;
        fullscreen_window.Width = fullscreen_window.GZZWidth = settings->Width;
        fullscreen_window.Height = fullscreen_window.GZZHeight = settings->Height;
        fullscreen_window.WScreen = &fullscreen_screen; fullscreen_window.RPort = &fullscreen_port;
        fullscreen_window.UserPort = &fullscreen_messages; fullscreen_messages.mp_SigBit = 5;
        ++fullscreen_window_count;
        return &fullscreen_window;
    }
    assert((settings->Type == WBENCHSCREEN && settings->Screen == NULL) ||
           (settings->Type == PUBLICSCREEN && settings->Screen == &desktop && pubscreen_locks));
    assert((settings->Flags & (WINDOWSIZING | GIMMEZEROZERO | WINDOWCLOSE)) ==
           (WINDOWSIZING | GIMMEZEROZERO | WINDOWCLOSE));
    assert(settings->IDCMPFlags & NEWSIZE);
    memset(&window_record, 0, sizeof(window_record));
    window_record.LeftEdge = settings->LeftEdge; window_record.TopEdge = settings->TopEdge;
    window_record.Width = settings->Width; window_record.Height = settings->Height;
    window_record.GZZWidth = settings->Width - 16; window_record.GZZHeight = settings->Height - 24;
    window_record.WScreen = &desktop; window_record.RPort = &window_port;
    window_record.UserPort = &window_messages;
    window_record.BorderLeft = window_record.BorderRight = 8;
    window_record.BorderTop = 16; window_record.BorderBottom = 8;
    ++window_count;
    return &window_record;
}
void CloseWindow(struct Window *window)
{
    assert(!event_outstanding);
    if (window == &fullscreen_window) { assert(fullscreen_window_count); --fullscreen_window_count; return; }
    assert(window == &window_record && window_count == 1); --window_count;
}
void BeginRefresh(struct Window *window)
{ assert((window == &window_record || window == &fullscreen_window) && !refresh_count); ++refresh_count; }
void EndRefresh(struct Window *window, LONG complete)
{ assert((window == &window_record || window == &fullscreen_window) && complete && refresh_count == 1); --refresh_count; }
void InitBitMap(struct BitMap *bitmap, LONG depth, ULONG width, ULONG height)
{
    memset(bitmap, 0, sizeof(*bitmap)); bitmap->Depth = (UBYTE)depth;
    bitmap->BytesPerRow = (UWORD)(((width + 15UL) / 16UL) * 2UL); bitmap->Rows = (UWORD)height;
}
struct BitMap *AllocBitMap(ULONG width, ULONG height, ULONG depth, ULONG flags,
                          const struct BitMap *friend_map)
{
    assert(display_version >= 39 && rtg_backend != 0);
    assert(width == drv_win_width && height == drv_win_height);
    assert(depth == rtg_depth && friend_map == &desktop.BitMap);
    assert((flags & BMF_CLEAR) && !(flags & BMF_STANDARD));
    assert(friend_count == 0); ++friend_calls;
    if (friend_failure) return NULL;
    memset(&friend_bitmap, 0xa5, sizeof(friend_bitmap));
    ++friend_count; return &friend_bitmap;
}
void FreeBitMap(struct BitMap *bitmap)
{
    assert(display_version >= 39 && bitmap == &friend_bitmap && friend_count == 1);
    assert(!blitter_busy); --friend_count;
}
ULONG GetBitMapAttr(const struct BitMap *bitmap, ULONG attribute)
{
    assert(display_version >= 39);
    assert(bitmap == &desktop.BitMap || (bitmap == &friend_bitmap && friend_count) || test_screen_bitmap(bitmap));
    if (attribute == BMA_FLAGS)
        return rtg_backend != 0 || rtg_unknown ? 0UL : BMF_STANDARD;
    if (attribute == BMA_WIDTH) return test_screen_bitmap(bitmap) ? (ULONG)fullscreen_screen.Width : (ULONG)desktop.Width;
    if (attribute == BMA_HEIGHT) return test_screen_bitmap(bitmap) ? (ULONG)fullscreen_screen.Height : (ULONG)desktop.Height;
    assert(attribute == BMA_DEPTH);
    return rtg_backend != 0 || rtg_unknown ? rtg_storage_depth : bitmap->Depth;
}
PLANEPTR AllocRaster(ULONG width, ULONG height)
{
    unsigned int i;
    ULONG size;
    assert(!event_outstanding); ++raster_calls;
    if (raster_fail_at != 0 && raster_calls == raster_fail_at) return NULL;
    size = RASSIZE(width, height);
    for (i = 0; i < 32; ++i) if (rasters[i] == NULL) break;
    assert(i < 32 && size > 0);
    rasters[i] = calloc(1, size); assert(rasters[i] != NULL);
    raster_sizes[i] = size; ++raster_count;
    return (PLANEPTR)rasters[i];
}
void FreeRaster(PLANEPTR raster, ULONG width, ULONG height)
{
    unsigned int i;
    assert(!blitter_busy);
    for (i = 0; i < 32; ++i) if (rasters[i] == raster) break;
    assert(i < 32 && raster_sizes[i] == RASSIZE(width, height));
    free(rasters[i]); rasters[i] = NULL; --raster_count;
}
void InitRastPort(struct RastPort *port)
{ memset(port, 0, sizeof(*port)); SetFont(port, &font); }
void SetFont(struct RastPort *port, struct TextFont *value)
{
    assert(value != NULL); port->Font = value; port->TxHeight = value->tf_YSize;
    port->TxWidth = value->tf_XSize; port->TxBaseline = value->tf_Baseline;
}
void InitArea(struct AreaInfo *area, APTR memory, LONG maximum)
{
    assert(((size_t)memory & 1U) == 0 && maximum >= SOFT3D_MAX_POLY + 1);
    memset(area, 0, sizeof(*area)); area->VctrTbl = memory; area->MaxCount = (WORD)maximum;
    area_count = 0;
}
struct TmpRas *InitTmpRas(struct TmpRas *tmp, PLANEPTR raster, LONG size)
{
    unsigned int i;
    for (i = 0; i < 32; ++i) if (rasters[i] == raster) break;
    assert(i < 32 && (ULONG)size <= raster_sizes[i]);
    tmp->RasPtr = raster; tmp->Size = size; return tmp;
}
void SetAPen(struct RastPort *port, ULONG pen)
{ assert(pen < (test_screen_bitmap(port->BitMap) ? fullscreen_colormap.Count : colormap.Count)); port->FgPen = (UBYTE)pen; }
void SetBPen(struct RastPort *port, ULONG pen)
{ assert(pen < (test_screen_bitmap(port->BitMap) ? fullscreen_colormap.Count : colormap.Count)); port->BgPen = (UBYTE)pen; }
void SetDrMd(struct RastPort *port, ULONG mode) { port->DrawMode = (UBYTE)mode; }
void RectFill(struct RastPort *port, LONG x0, LONG y0, LONG x1, LONG y1)
{
    assert(port != NULL && x0 == 0 && y0 == 0);
    assert(x1 == (test_screen_bitmap(port->BitMap) ? (LONG)fullscreen_screen.Width : (LONG)drv_win_width) - 1);
    assert(y1 == (test_screen_bitmap(port->BitMap) ? (LONG)fullscreen_screen.Height : (LONG)drv_win_height) - 1);
    test_safe_draw(port); ++rect_calls; blitter_busy = 1;
}
void Move(struct RastPort *port, LONG x, LONG y)
{ port->cp_x = (WORD)x; port->cp_y = (WORD)y; }
void Text(struct RastPort *port, CONST_STRPTR text, ULONG length)
{
    size_t used;
    assert(port->cp_x >= 0 && port->cp_y >= 0 && port->cp_y < (WORD)drv_win_height);
    assert((ULONG)port->cp_x + length * 8UL <= drv_win_width);
    test_safe_draw(port);
    blitter_busy = 1;
    ++text_calls; used = strlen(osd_text);
    if (used + length + 2U < sizeof(osd_text)) { memcpy(osd_text + used, text, length); osd_text[used + length] = '\n'; osd_text[used + length + 1] = 0; }
}
WORD TextLength(struct RastPort *port, CONST_STRPTR text, ULONG length)
{ (void)port; (void)text; return (WORD)(8UL * length); }
static LONG record_corner(struct RastPort *port, LONG x, LONG y)
{
    test_safe_draw(port);
    assert(port->TmpRas != NULL && port->TmpRas->RasPtr != NULL && port->AreaInfo != NULL);
    assert(x >= 0 && y >= 0 && x < (LONG)drv_win_width && y < (LONG)drv_win_height);
    assert(area_count < SOFT3D_MAX_POLY && area_count < (unsigned int)port->AreaInfo->MaxCount);
    area_x[area_count] = x; area_y[area_count] = y; ++area_count;
    if (area_failures) { --area_failures; return -1; }
    return 0;
}
LONG AreaMove(struct RastPort *port, LONG x, LONG y) { assert(area_count == 0); return record_corner(port, x, y); }
LONG AreaDraw(struct RastPort *port, LONG x, LONG y) { return record_corner(port, x, y); }
LONG AreaEnd(struct RastPort *port)
{
    unsigned int i;
    assert(area_count >= 3 && !(port->Flags & AREAOUTLINE));
    if (!ami_dither_enabled)
        assert(port->DrawMode == JAM1 && port->AreaPtrn == NULL);
    for (i = 0; i < area_count; ++i) rendered_hash = rendered_hash * 33UL + (ULONG)(area_x[i] * 32768L + area_y[i]);
    rendered_hash = rendered_hash * 33UL + port->FgPen;
    ++rendered_count; ++area_calls; blitter_busy = 1; return 0;
}
void WaitBlit(void) { blitter_busy = 0; }
void BltBitMapRastPort(const struct BitMap *bitmap, LONG sx, LONG sy,
                      struct RastPort *port, LONG dx, LONG dy, LONG width, LONG height, ULONG minterm)
{
    assert(bitmap != NULL && port == &window_port);
    if (rtg_backend) assert(bitmap == &friend_bitmap && friend_count == 1);
    else assert(bitmap->Depth == desktop.BitMap.Depth);
    assert(sx == 0 && sy == 0 && dx == 0 && dy == 0 && minterm == 0xc0UL);
    assert(width == (LONG)drv_win_width && height == (LONG)drv_win_height);
    ++copy_calls; blitter_busy = 1;
}
ULONG GetRGB4(struct ColorMap *colors, LONG pen)
{
    unsigned char (*palette)[3];
    assert((colors == &colormap || colors == &fullscreen_colormap) && pen >= 0 && (ULONG)pen < colors->Count);
    palette = colors == &colormap ? desktop_palette : fullscreen_palette;
    return ((ULONG)(palette[pen][0] >> 4) << 8) |
           ((ULONG)(palette[pen][1] >> 4) << 4) | (palette[pen][2] >> 4);
}
void GetRGB32(struct ColorMap *colors, ULONG first, ULONG count, ULONG *out)
{
    unsigned int i;
    unsigned char (*palette)[3];
    assert(display_version >= 39 && (colors == &colormap || colors == &fullscreen_colormap) && count == 1 && first < colors->Count);
    palette = colors == &colormap ? desktop_palette : fullscreen_palette;
    for (i = 0; i < 3; ++i) out[i] = (ULONG)palette[first][i] * 0x01010101UL;
}
ULONG GetVPModeID(struct ViewPort *view)
{ assert(display_version >= 36 && (view == &desktop.ViewPort || view == &fullscreen_screen.ViewPort)); return view == &fullscreen_screen.ViewPort ? fullscreen_mode : rtg_backend ? 0x50000UL : 0; }
ULONG GetDisplayInfoData(APTR handle, UBYTE *buffer, ULONG length, ULONG tag, ULONG id)
{
    struct DisplayInfo *info;
    assert(display_version >= 36 && handle == NULL && length == sizeof(*info) && tag == DTAG_DISP && (id == 0 || id == fullscreen_mode));
    info = (struct DisplayInfo *)buffer; info->Resolution.x = (WORD)pixel_x; info->Resolution.y = (WORD)pixel_y;
    return sizeof(*info);
}
#if AMI_RTG
ULONG ami_cgx_attr(struct Library *base, struct BitMap *bitmap, ULONG attribute)
{
    assert(base == &cgx_library && display_version >= 39);
    assert(bitmap == &desktop.BitMap || (bitmap == &friend_bitmap && friend_count) || test_screen_bitmap(bitmap));
    if (attribute == AMI_CGX_IS)
        return rtg_backend == 1 &&
               !(bitmap == &friend_bitmap && friend_incompatible) &&
               !(bitmap == &fullscreen_alternate && fullscreen_incompatible);
    assert(rtg_backend == 1);
    if (attribute == AMI_CGX_BYTES) return (rtg_storage_depth + 7U) / 8U;
    assert(attribute == AMI_CGX_DEPTH); return rtg_depth;
}
ULONG ami_p96_attr(struct Library *base, struct BitMap *bitmap, ULONG attribute)
{
    assert(base == &p96_library && display_version >= 39);
    assert(bitmap == &desktop.BitMap || (bitmap == &friend_bitmap && friend_count) || test_screen_bitmap(bitmap));
    if (attribute == AMI_P96_IS)
        return rtg_backend == 2 && !(bitmap == &friend_bitmap && friend_incompatible) &&
               !(bitmap == &fullscreen_alternate && fullscreen_incompatible);
    assert(rtg_backend == 2);
    if (attribute == AMI_P96_DEPTH) return rtg_storage_depth;
    assert(attribute == AMI_P96_BITS); return rtg_depth;
}
static void record_rgb_fill(struct RastPort *port, unsigned int x0, unsigned int y0,
                            unsigned int x1, unsigned int y1, ULONG argb)
{
    unsigned int x, y;
    assert(rtg_depth > 8 && port != NULL);
    assert(port->BitMap == &desktop.BitMap ||
           (port->BitMap == &friend_bitmap && friend_count) || test_screen_bitmap(port->BitMap));
    test_safe_draw(port);
    blitter_busy = 1;
    assert(x0 <= x1 && y0 <= y1 && x1 < drv_win_width && y1 < drv_win_height);
    assert((argb & 0xff000000UL) == 0);
    if (coverage_active)
    {
        assert(x1 < 64 && y1 < 64);
        assert(argb == coverage_rgb);
        for (y = y0; y <= y1; ++y)
            for (x = x0; x <= x1; ++x) ++coverage[y][x];
    }
    else if (x0 == 0 && y0 == 0 && x1 + 1U == drv_win_width && y1 + 1U == drv_win_height)
    { assert(argb == 0); ++rect_calls; }
    else
    {
        assert(y0 == y1);
        if (extreme_active)
        {
            for (x = x0; x <= x1; ++x) assert(test_inside(extreme_polygon, 3, x, y0));
            extreme_pixels[y0] += x1 - x0 + 1U;
        }
        rendered_hash = rendered_hash * 33UL + x0 * 32768UL + x1;
        rendered_hash = rendered_hash * 33UL + y0;
        rendered_hash = rendered_hash * 33UL + argb;
        ++span_calls;
    }
}
ULONG ami_cgx_fill(struct Library *base, struct RastPort *port, UWORD x, UWORD y,
                   UWORD width, UWORD height, ULONG argb)
{
    assert(base == &cgx_library && width && height); ++cgx_calls;
    record_rgb_fill(port, x, y, x + width - 1U, y + height - 1U, argb);
    /* The real API documents incorrect counts through v41, and clipped window
     * draws can legitimately write zero. Neither implies a rendering failure. */
    return 0;
}
void ami_p96_fill(struct Library *base, struct RastPort *port, UWORD x0, UWORD y0,
                  UWORD x1, UWORD y1, ULONG argb)
{
    assert(base == &p96_library); ++p96_calls;
    record_rgb_fill(port, x0, y0, x1, y1, argb);
}
#endif
BOOL AutoRequest(struct Window *window, struct IntuiText *body, struct IntuiText *positive,
                 struct IntuiText *negative, ULONG good, ULONG bad, WORD width, WORD height)
{
    (void)window; (void)positive; (void)negative; (void)good; (void)bad; (void)width; (void)height;
    if (small_stack)
    { assert(strstr((char *)body->IText, "32768") != NULL); return 1; }
    fprintf(stderr, "Unexpected Amiga error: %s\n", body->IText); assert(0); return 0;
}
Drv_result ami_data_init(int argc, char **argv) { (void)argc; (void)argv; return DRV_OK; }
void ami_data_close(void) {}
Drv_result drv_load_earth_chunk(unsigned int id, unsigned long length, unsigned char *destination)
{
    unsigned char *bytes;
    unsigned long size;
    ++resource_calls; if (resource_first == 0) resource_first = id;
    if (length > resource_max) resource_max = (unsigned int)length;
    if (resource_failure == id) return DRV_BAD_DATA;
    bytes = test_earth_resource((short)id, &size); assert(size == length);
    memcpy(destination, bytes, (size_t)length); free(bytes); return DRV_OK;
}

static void test_render(void)
{
    rendered_hash = rendered_count = 0; osd_text[0] = 0;
    baseline_vertices = enhanced_vertices = fpu_vertices = 0;
    drv_render();
    assert(!drv_frame_failed && !ami_rendering && ami_draw_port == NULL);
    if (rtg_depth > 8) rendered_count = globe_quads_num_vis;
    else assert(rendered_count == globe_quads_num_vis);
}

/* Evaluate actual 4x4 pixel averages independently of the driver's projection
 * of a target onto a pen-pair segment. Keep the denominator rather than
 * rounding the mean to bytes: all 17 possible coverage ratios are observable. */
static unsigned long test_palette_error(unsigned int color, unsigned int a,
                                        unsigned int b, unsigned int mix)
{
    unsigned int channel;
    long difference;
    unsigned long error;
    error = 0;
    for (channel = 0; channel < 3U; ++channel)
    {
        difference = 16L * earth_cmap[color][channel] -
            (16L - mix) * desktop_palette[a][channel] -
            (long)mix * desktop_palette[b][channel];
        error += (unsigned long)(difference * difference);
    }
    return error;
}

static unsigned long test_palette_optimum(unsigned int color)
{
    unsigned int a, b, mix;
    unsigned long error, best;
    best = ULONG_MAX;
    for (a = 0; a < colormap.Count; ++a)
        for (b = a; b < colormap.Count; ++b)
            for (mix = 0; mix <= 16U; ++mix)
            {
                error = test_palette_error(color, a, b, mix);
                if (error < best) best = error;
            }
    return best;
}

static unsigned long test_palette_previous(unsigned int color)
{
    unsigned int a, b, pen, mix;
    unsigned long error, best;
    a = b = 0;
    best = ULONG_MAX;
    for (pen = 0; pen < colormap.Count; ++pen)
    {
        error = test_palette_error(color, pen, pen, 0U);
        if (error < best) { best = error; a = pen; }
    }
    best = ULONG_MAX;
    for (pen = 0; pen < colormap.Count; ++pen)
    {
        if (pen == a) continue;
        error = test_palette_error(color, pen, pen, 0U);
        if (error < best) { best = error; b = pen; }
    }
    best = ULONG_MAX;
    for (mix = 0; mix <= 16U; ++mix)
    {
        error = test_palette_error(color, a, b, mix);
        if (error < best) best = error;
    }
    return best;
}

static void test_palette_mapping(int exhaustive)
{
    unsigned char before[sizeof(desktop_palette)];
    unsigned int color;
    unsigned long actual;
    memcpy(before, desktop_palette, sizeof(before));
    ami_palette_valid = FALSE;
    ami_map_palette();
    assert(ami_color_count == colormap.Count);
    assert(memcmp(before, desktop_palette, sizeof(before)) == 0);
    for (color = 0; color < 256U; ++color)
    {
        assert(ami_pen_a[color] < colormap.Count && ami_pen_b[color] < colormap.Count);
        assert(ami_mix[color] <= 16U);
        actual = test_palette_error(color, ami_pen_a[color], ami_pen_b[color], ami_mix[color]);
        if (exhaustive) assert(actual == test_palette_optimum(color));
        else assert(actual <= test_palette_previous(color));
    }
}

static void test_palette_quality(void)
{
    static const unsigned int counts[] = {1, 2, 3, 4, 7, 8, 16, 17, 32, 63, 64, 128, 255, 256};
    static const unsigned int difficult_counts[] = {4, 32, 64, 256};
    static const unsigned char difficult[4][3] = {
        {120, 100, 100}, {100, 120, 100}, {255, 0, 0}, {0, 255, 0}
    };
    static const unsigned char thresholds[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}
    };
    unsigned char saved_palette[sizeof(desktop_palette)], saved_earth[3];
    unsigned int saved_count, index, pen, mix, x, y, background;
    struct RastPort saved_port;
    unsigned long actual;
    /* Run once on the AGA fixture so GetRGB32 supplies unquantized RGB bytes.
     * All temporary colors and mappings are restored before frame checks. */
    assert(display_version == 39 && ami_depth == 8U && ami_draw_port == NULL);
    saved_count = colormap.Count;
    memcpy(saved_palette, desktop_palette, sizeof(saved_palette));
    memcpy(saved_earth, earth_cmap[0], sizeof(saved_earth));
    saved_port = window_port;
    for (index = 0; index < sizeof(counts) / sizeof(counts[0]); ++index)
    {
        colormap.Count = (UWORD)counts[index];
        for (pen = 0; pen < colormap.Count; ++pen)
        {
            desktop_palette[pen][0] = (UBYTE)((pen * 73U) & 255U);
            desktop_palette[pen][1] = (UBYTE)((pen * 151U) & 255U);
            desktop_palette[pen][2] = (UBYTE)((pen * 199U) & 255U);
        }
        if (colormap.Count > 1U) memset(desktop_palette[colormap.Count - 1U], 255, 3);
        if (colormap.Count >= 4U) memcpy(desktop_palette[1], desktop_palette[0], 3);
        test_palette_mapping(colormap.Count <= 16U);
    }
    /* Neither individually nearest pen belongs to the best red/green pair.
     * Repeat at the larger desktop sizes to check their bounded search. */
    earth_cmap[0][0] = earth_cmap[0][1] = 127; earth_cmap[0][2] = 0;
    for (index = 0; index < sizeof(difficult_counts) / sizeof(difficult_counts[0]); ++index)
    {
        colormap.Count = (UWORD)difficult_counts[index];
        for (pen = 0; pen < colormap.Count; ++pen)
            memcpy(desktop_palette[pen], difficult[pen < 4U ? pen : 0], 3);
        test_palette_mapping(index == 0);
        actual = test_palette_error(0, ami_pen_a[0], ami_pen_b[0], ami_mix[0]);
        assert(actual == 128UL && actual < test_palette_previous(0));
        assert(ami_mix[0] == 8U);
        assert((ami_pen_a[0] == 2U && ami_pen_b[0] == 3U) ||
               (ami_pen_a[0] == 3U && ami_pen_b[0] == 2U));
    }
    /* An exact entry, including duplicates, must stay solid without noise. */
    colormap.Count = 16;
    memcpy(desktop_palette[1], earth_cmap[0], 3);
    memcpy(desktop_palette[5], earth_cmap[0], 3);
    test_palette_mapping(TRUE);
    assert(ami_mix[0] == 0U && test_palette_error(0, ami_pen_a[0], ami_pen_a[0], 0U) == 0UL);
    colormap.Count = 2;
    memset(desktop_palette[0], 0, 3); memset(desktop_palette[1], 255, 3);
    for (index = 0; index < 2U; ++index)
    {
        memset(earth_cmap[0], index ? 255 : 0, 3);
        test_palette_mapping(TRUE);
        assert(ami_mix[0] == 0U && ami_pen_a[0] == index);
    }
    /* Test the native draw state and actual bit coverage, not just the
     * lookup's mixture number. Clear pattern bits select the background pen. */
    ami_draw_port = &window_port;
    ami_pen_a[0] = 0; ami_pen_b[0] = 1;
    for (mix = 0; mix <= 16U; ++mix)
    {
        ami_mix[0] = (unsigned char)mix;
        ami_set_color(0);
        if (mix == 0U || mix == 16U)
        {
            assert(window_port.DrawMode == JAM1 && window_port.AreaPtrn == NULL);
            assert(window_port.FgPen == (mix == 0U ? 0U : 1U));
        }
        else
        {
            assert(window_port.DrawMode == JAM2 && window_port.AreaPtSz == 2);
            assert(window_port.FgPen == 0U && window_port.BgPen == 1U);
            assert(window_port.AreaPtrn != NULL);
            background = 0;
            for (y = 0; y < 4U; ++y)
                for (x = 0; x < 16U; ++x)
                {
                    assert(!!(window_port.AreaPtrn[y] & (UWORD)(0x8000U >> x)) ==
                           (thresholds[y][x % 4U] >= mix));
                    if (!(window_port.AreaPtrn[y] & (UWORD)(0x8000U >> x))) ++background;
                }
            assert(background == 4U * mix);
        }
    }
    ami_draw_port = NULL; window_port = saved_port;
    memcpy(earth_cmap[0], saved_earth, sizeof(saved_earth));
    memcpy(desktop_palette, saved_palette, sizeof(saved_palette));
    colormap.Count = (UWORD)saved_count;
    ami_palette_valid = FALSE;
    ami_map_palette();
    puts("PASS Amiga palette matching: exhaustive small-palette colors, bounded-search quality, exact/duplicate pens and Bayer coverage");
}

static void test_osd_contains(const char *expected)
{
    char joined[sizeof(osd_text)];
    unsigned int i, length;
    length = 0;
    for (i = 0; osd_text[i] != 0; ++i)
        if (osd_text[i] != '\n') joined[length++] = osd_text[i];
    joined[length] = 0;
    assert(strstr(joined, expected) != NULL);
}

static void test_solid_palette(void)
{
    struct RastPort saved_port;
    unsigned int color, pen, nearest, channel;
    long difference;
    unsigned long error, best;
    assert(!ami_dither_enabled && ami_draw_port == NULL && ami_depth <= 8U);
    saved_port = window_port;
    ami_draw_port = &window_port;
    for (color = 0; color < 256U; ++color)
    {
        nearest = 0; best = ULONG_MAX;
        for (pen = 0; pen < ami_color_count; ++pen)
        {
            error = 0;
            /* The effective desktop palette includes RGB12 quantization on
             * older graphics.library versions; compare the colors read. */
            for (channel = 0; channel < 3U; ++channel)
            {
                difference = (long)earth_cmap[color][channel] - ami_palette[pen][channel];
                error += (unsigned long)(difference * difference);
            }
            if (error < best) { best = error; nearest = pen; }
        }
        assert(ami_pen_solid[color] == nearest);
        ami_set_color((unsigned char)color);
        assert(window_port.FgPen == nearest);
        assert(window_port.DrawMode == JAM1 && window_port.AreaPtrn == NULL);
    }
    window_port = saved_port;
    ami_draw_port = NULL;
}

static void test_dither_toggle(void)
{
    unsigned char saved_palette[sizeof(desktop_palette)];
    unsigned int resources, rasters;
    unsigned long before_hash, before_count, revision;
    Soft3d_project_cache *cache;
    assert(ami_dither_enabled && drv_osd_enabled);
    assert(strstr(drv_help, "D dither") != NULL);
    test_osd_contains(ami_depth <= 8U ? "dither on" : "dither n/a");
    resources = resource_calls; rasters = raster_calls;
    revision = globe_revision; cache = ami_project_cache;
    before_hash = rendered_hash; before_count = rendered_count;
    ami_dirty = FALSE; drv_fps_average = 99.0f;
    ami_event(VANILLAKEY, 'd', 0, 0);
    assert(!ami_dither_enabled && ami_dirty && drv_fps_average == 0.0f);
    assert(resource_calls == resources && raster_calls == rasters);
    assert(globe_revision == revision && ami_project_cache == cache);
    if (ami_depth <= 8U) test_solid_palette();
    test_render();
    assert(rendered_count == before_count);
    test_osd_contains(ami_depth <= 8U ? "dither off" : "dither n/a");
    if (ami_depth > 8U) assert(rendered_hash == before_hash);
    /* A refresh may change Workbench colors while solid matching is active.
     * Preserve the option while rebuilding the cache from the new palette. */
    if (ami_depth <= 8U)
    {
        memcpy(saved_palette, desktop_palette, sizeof(saved_palette));
        memset(desktop_palette[0], 255, 3);
        memset(desktop_palette[colormap.Count - 1U], 0, 3);
        ami_event(REFRESHWINDOW, 0, 0, 0);
        assert(!ami_dither_enabled);
        test_solid_palette();
        memcpy(desktop_palette, saved_palette, sizeof(saved_palette));
        ami_event(ACTIVEWINDOW, 0, 0, 0);
        assert(!ami_dither_enabled);
        test_solid_palette();
    }
    ami_event(NEWSIZE, 0, 0, 0);
    assert(!ami_dither_enabled);
    ami_keyboard('2'); assert(!ami_dither_enabled && globe_longitude == 64);
    ami_keyboard('1'); assert(!ami_dither_enabled && globe_longitude == 32);
    ami_event(VANILLAKEY, 'D', 0, 0);
    assert(ami_dither_enabled && ami_dirty);
    test_render();
    assert(rendered_hash == before_hash && rendered_count == before_count);
    test_osd_contains(ami_depth <= 8U ? "dither on" : "dither n/a");
}

static void test_fpu_integer_conversion(void)
{
    static const double targets[] = {
        -134217700.0, -65536.9, -1.9, -1.0, -0.001, 0.0,
        0.001, 0.999, 1.00001, 65536.9, 134217700.0
    };
    Soft3d_float_point points[3];
    Soft3d_screen_point projected[SOFT3D_MAX_POLY];
    unsigned int indices[3], i, j;
    double focal, expected;
    focal = 10.0 * GLOBE_FOCAL_SCALE;
    for (j = 0; j < 3; ++j) indices[j] = j;
    for (i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
        for (j = 0; j < 3; ++j)
        {
            points[j].x = (float)((targets[i] - 5.0) * 3000.0 / focal);
            points[j].y = 0.0f; points[j].z = -3000.0f;
        }
        expected = 5.0 + (double)points[0].x * focal / 3000.0;
        assert(expected >= -134217727.0 && expected <= 134217727.0);
        assert(soft3d_float_project_cached(points, 3U, indices, 3U, 10U, 10U,
                   SOFT3D_FLOAT_DOUBLE, NULL, projected) == 3);
        for (j = 0; j < 3; ++j) assert(projected[j].x == (long)expected && projected[j].y == 5);
    }
}

static void test_fatal_cleanup(void)
{
    assert(small_stack && library_count == 0 && window_count == 0 && raster_count == 0);
    puts("PASS Amiga stack guard: insufficient startup stack reports a native error and closes libraries before exit");
}

#if AMI_RTG
/* Independent pixel-centre crossing oracle. The production code converts edge
 * intersections to integer spans; this tests occupancy one sample at a time. */
static int test_inside(const Soft3d_screen_point *polygon, unsigned int count,
                       unsigned int x, unsigned int y)
{
    unsigned int i, j;
    double ax, ay, bx, by, sample_x, sample_y, cross;
    int inside;
    /* These integer-valued products remain exact in binary64 even at the
     * native 32767 coordinate limit, independently of host long width. */
    inside = 0; sample_x = (double)x * 2.0 + 1.0; sample_y = (double)y * 2.0 + 1.0;
    for (i = 0, j = count - 1U; i < count; j = i++)
    {
        ax = (double)polygon[j].x * 2.0; ay = (double)polygon[j].y * 2.0;
        bx = (double)polygon[i].x * 2.0; by = (double)polygon[i].y * 2.0;
        if ((ay > sample_y) == (by > sample_y)) continue;
        cross = (bx - ax) * (sample_y - ay) - (sample_x - ax) * (by - ay);
        if ((by > ay && cross > 0) || (by < ay && cross < 0)) inside = !inside;
    }
    return inside;
}

static void test_span_coverage(void)
{
    static const Soft3d_screen_point shapes[][8] = {
        {{5,7}, {40,7}, {40,39}, {5,39}},
        {{5,7}, {40,7}, {40,39}},
        {{5,7}, {40,39}, {5,39}},
        {{0,0}, {63,31}, {0,63}},
        {{31,0}, {32,63}, {30,63}},
        {{0,11}, {11,0}, {50,0}, {63,13}, {63,51}, {51,63}, {13,63}, {0,50}},
        {{5,39}, {40,39}, {40,7}, {5,7}},
        {{0,0}, {63,63}, {62,62}},
        /* Rounded silhouette quads may become concave. The upper notch must
         * stay empty instead of joining the outermost crossings. */
        {{5,5}, {30,30}, {55,5}, {30,55}}
    };
    static const unsigned int lengths[] = {4, 3, 3, 3, 3, 8, 4, 3, 4};
    Soft3d_screen_point polygon[8];
    unsigned int saved_width, saved_height, shape, x, y, color, expected;
    saved_width = drv_win_width; saved_height = drv_win_height;
    drv_win_width = drv_win_height = 64;
    ami_draw_port = &window_port; coverage_active = 1;
    for (shape = 0; shape < sizeof(lengths) / sizeof(lengths[0]); ++shape)
    {
        memset(coverage, 0, sizeof(coverage));
        memcpy(polygon, shapes[shape], sizeof(polygon));
        coverage_rgb = ((ULONG)earth_cmap[17][0] << 16) |
                       ((ULONG)earth_cmap[17][1] << 8) | earth_cmap[17][2];
        assert(ami_fill_polygon(polygon, lengths[shape], 17));
        for (y = 0; y < 64; ++y) for (x = 0; x < 64; ++x)
            assert(coverage[y][x] == (unsigned int)test_inside(polygon, lengths[shape], x, y));
    }
    /* Complementary triangles own the shared diagonal exactly once. */
    memset(coverage, 0, sizeof(coverage));
    for (shape = 1; shape <= 2; ++shape)
    {
        memcpy(polygon, shapes[shape], sizeof(polygon));
        assert(ami_fill_polygon(polygon, 3, 17));
    }
    for (y = 0; y < 64; ++y) for (x = 0; x < 64; ++x)
        assert(coverage[y][x] == (unsigned int)(x >= 5 && x < 40 && y >= 7 && y < 39));
    /* Every globe index is expanded through the canonical RGB colormap. */
    memcpy(polygon, shapes[1], sizeof(polygon));
    for (color = 0; color < 256; ++color)
    {
        coverage_rgb = ((ULONG)earth_cmap[color][0] << 16) |
                       ((ULONG)earth_cmap[color][1] << 8) | earth_cmap[color][2];
        assert(ami_fill_polygon(polygon, 3, (unsigned char)color));
    }
    coverage_active = 0;
    shape = span_calls;
    polygon[0].x = -1;
    assert(!ami_fill_polygon(polygon, 3, 17));
    assert(!ami_fill_polygon(NULL, 3, 17));
    assert(!ami_fill_polygon(polygon, 2, 17));
    assert(!ami_fill_polygon(polygon, AMI_MAX_POLY + 1, 17));
    assert(span_calls == shape);
    /* Native signed-word endpoints at the largest supported dimensions. */
    drv_win_width = drv_win_height = 32767;
    polygon[0].x = polygon[0].y = 0;
    polygon[1].x = 32766; polygon[1].y = 32765;
    polygon[2].x = 32765; polygon[2].y = 32766;
    extreme_active = 1; extreme_polygon = polygon;
    memset(extreme_pixels, 0, sizeof(extreme_pixels));
    assert(ami_fill_polygon(polygon, 3, 17));
    for (y = 0; y < 32767; ++y)
    {
        expected = 0;
        for (x = y > 2 ? y - 2 : 0; x < 32767 && x <= y + 2; ++x)
            expected += test_inside(polygon, 3, x, y);
        assert(extreme_pixels[y] == expected);
    }
    extreme_active = 0; extreme_polygon = NULL;
    drv_win_width = saved_width; drv_win_height = saved_height;
    ami_draw_port = NULL;
}

static int test_rtg(int argc, char **argv, const char *mode)
{
    unsigned long reference_hash, reference_count;
    unsigned int i, copies, spans, raster_attempts;
    unsigned char saved_palette[sizeof(desktop_palette)];
    drv_ext_init(); globe_init(); drv_init(&argc, argv);
    if (rtg_unknown || rtg_depth == 12 || pubscreen_failure)
    {
        assert(!drv_init_window(0, 0, 0, 0)); ami_cleanup();
        assert(library_count == 0 && window_count == 0 && raster_count == 0);
        assert(friend_count == 0 && pubscreen_locks == 0);
        assert(area_calls == 0 && span_calls == 0 && rect_calls == 0);
        printf("PASS Amiga %s: unsupported RTG screen is rejected before drawing and releases resources\n", mode);
        return 0;
    }
    assert(drv_init_window(0, 0, 0, 0));
    assert(ami_rtg == (int)rtg_backend && ami_depth == rtg_depth);
    assert(pubscreen_locks == 0 && ami_aspect_num == ami_aspect_den);
    assert(!ami_big_blits && !ami_unsafe_bitmap);
    assert(drv_bits_per_pixel == rtg_storage_depth);
    assert(drv_colormapped == (rtg_depth <= 8U));
    assert(drv_colormap_bits == (rtg_depth > 24U ? 24U : rtg_depth));
    if (rtg_depth > 8) assert(ami_scratch == NULL && raster_calls == 0);
    else assert(ami_scratch != NULL && raster_count == 1);
    assert((friend_failure || friend_incompatible) ? friend_count == 0 : friend_count == 1);
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    assert(!drv_texture_avail && !drv_texture_enabled);
    memcpy(saved_palette, desktop_palette, sizeof(saved_palette));
    test_render();
    assert(rendered_count > 20 && text_calls > 0 && strstr(osd_text, "quads"));
    assert(strstr(osd_text, "RTG"));
    if (rtg_depth > 8)
    {
        assert(span_calls > 20 && area_calls == 0);
        assert(rtg_backend == 1 ? cgx_calls > 0 && p96_calls == 0 : p96_calls > 0 && cgx_calls == 0);
        test_span_coverage();
        test_render();
    }
    else assert(area_calls > 20 && span_calls == 0);
    test_dither_toggle();
    reference_hash = rendered_hash; reference_count = rendered_count;
    copies = copy_calls; drv_release_optional();
    assert(friend_count == 0 && ami_project_cache == NULL);
    test_render(); assert(copy_calls == copies);
    assert(rendered_hash == reference_hash && rendered_count == reference_count);
    /* Native OCS dimensions/stride never constrain an identified RTG screen. */
    window_record.GZZWidth = 1200; window_record.GZZHeight = 1100;
    ami_event(NEWSIZE, 0, 0, 0);
    assert(ami_ready && drv_win_width == 1200 && drv_win_height == 1100);
    test_render(); assert(rendered_count > 20);
    window_record.GZZWidth = 320; window_record.GZZHeight = 200;
    ami_event(NEWSIZE, 0, 0, 0); test_render();
    spans = span_calls; copies = copy_calls;
    ami_event(REFRESHWINDOW, 0, 0, 0);
    assert(span_calls == spans && copy_calls == copies && refresh_count == 0);
    ami_event(ACTIVEWINDOW, 0, 0, 0); test_render();
    assert(memcmp(saved_palette, desktop_palette, sizeof(saved_palette)) == 0);
    raster_attempts = raster_calls;
    ami_event(MOUSEBUTTONS, SELECTDOWN, 10, 10); ami_event(MOUSEMOVE, 0, 30, 20);
    ami_event(MOUSEBUTTONS, SELECTUP, 30, 20);
    ami_keyboard('1'); test_render();
    assert(!ami_paused);
    if (rtg_depth > 8) assert(raster_calls == raster_attempts && ami_scratch == NULL);
    /* Out-of-range coordinates suppress fills before any native call. */
    copies = copy_calls; spans = span_calls;
    window_record.GZZWidth = -32768;
    ami_event(NEWSIZE, 0, 0, 0); drv_render();
    assert(!ami_ready && span_calls == spans && copy_calls == copies);
    window_record.GZZWidth = 320; ami_event(NEWSIZE, 0, 0, 0); test_render();
    for (i = 0; i < 2; ++i) ami_cleanup();
    assert(library_count == 0 && window_count == 0 && raster_count == 0);
    assert(friend_count == 0 && pubscreen_locks == 0 && ami_project_cache == NULL);
    globe_free();
    printf("PASS Amiga %s: opaque RTG bitmaps, RGB/palette rendering, exact span coverage, direct fallback, resize and cleanup\n", mode);
    return 0;
}
#endif

static void test_fullscreen_windowed(void)
{
    assert(ami_window == &window_record && window_count == 1);
    assert(!screen_count && !fullscreen_window_count && !screenbuffer_count && !reply_port_count);
    assert(!pending_safe && !pending_display);
    assert(drv_win_width == (unsigned int)window_record.GZZWidth &&
           drv_win_height == (unsigned int)window_record.GZZHeight);
}

static int test_fullscreen(int argc, char **argv, const char *mode)
{
    unsigned int calls, copies, draws, swaps, i, saved_width, saved_height;
    int old_fps;
    WORD left, top;
    unsigned char saved_palette[sizeof(desktop_palette)];
    drv_ext_init(); globe_init(); drv_init(&argc, argv);
    assert(drv_init_window(0, 0, 0, 0));
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    drv_auto_enabled = FALSE;
    drv_rot_x = 12.0f; drv_rot_y = 34.0f;
    ami_keyboard('d'); assert(!ami_dither_enabled);
    old_fps = drv_fps_enabled;
    ami_keyboard('p'); assert(drv_fps_enabled != old_fps);
    ami_keyboard('P'); assert(drv_fps_enabled == old_fps);
    saved_width = drv_win_width; saved_height = drv_win_height;
    left = window_record.LeftEdge; top = window_record.TopEdge;
    memcpy(saved_palette, desktop_palette, sizeof(saved_palette));
    if (strcmp(mode, "fs-loop") == 0)
    {
        fullscreen_loop_script = 1; hold_safe = hold_display = 1;
        messages[0].Class = VANILLAKEY; messages[0].Code = 'f'; message_count = 1;
        drv_loop();
        assert(fullscreen_loop_step == 5U && reply_count == 6U && swap_successes >= 2U);
        assert(!screen_count && !screenbuffer_count && !reply_port_count && !fullscreen_window_count);
        assert(!window_count && !library_count && !raster_count && !friend_count);
        assert(!pending_safe && !pending_display && !blitter_busy);
        assert(memcmp(saved_palette, desktop_palette, sizeof(saved_palette)) == 0);
        globe_free();
        puts("PASS Amiga fullscreen event loop: delayed display/safe notifications, combined wait masks, scene changes and retained-window F");
        return 0;
    }
    if (display_version < 39)
    {
        ami_keyboard('f'); ami_keyboard('F');
        test_fullscreen_windowed();
        assert(screen_calls == 0 && reply_port_calls == 0 && drv_fps_enabled == old_fps);
        test_render();
        ami_cleanup(); globe_free();
        assert(!library_count && !raster_count && !window_count);
        puts("PASS Amiga fullscreen legacy: F/f retains windowed mode without newer OS calls; P/p toggles FPS");
        return 0;
    }
    /* Each failed acquisition unwinds its partial graph and preserves the
     * original Workbench window, including its visible contents and palette. */
    if (strcmp(mode, "fs-ocs") == 0)
    {
        for (i = 0; i < 7U; ++i)
        {
            if (i == 0U) screen_failures = 1;
            if (i == 1U) fullscreen_window_failures = 1;
            if (i == 2U || i == 3U) buffer_fail_at = screenbuffer_calls + i - 1U;
            if (i == 4U || i == 5U) port_fail_at = reply_port_calls + i - 3U;
            if (i == 6U) raster_fail_at = raster_calls + 1U;
            ami_keyboard('f');
            test_fullscreen_windowed();
            screen_failures = fullscreen_window_failures = buffer_fail_at = port_fail_at = raster_fail_at = 0;
            assert(memcmp(saved_palette, desktop_palette, sizeof(saved_palette)) == 0);
        }
    }
    fullscreen_incompatible = 1;
    ami_keyboard('f');
    test_fullscreen_windowed();
    fullscreen_incompatible = 0;
    /* AGA retries a smaller private palette when 8-bit display storage is
     * unavailable; a later toggle may again choose the full palette. */
    if (strcmp(mode, "fs-aga") == 0)
    {
        buffer_fail_at = screenbuffer_calls + 2U;
        ami_keyboard('F');
        assert(ami_window == &fullscreen_window && ami_depth == 5U);
        buffer_fail_at = 0;
        ami_keyboard('f'); test_fullscreen_windowed();
    }
    copies = copy_calls;
    ami_keyboard('f');
    assert(ami_window == &fullscreen_window && screen_count == 1 && screenbuffer_count == 2);
    assert(fullscreen_window_count == 1 && reply_port_count == 2 && window_count == 1);
    assert(drv_fps_enabled == old_fps && !ami_dither_enabled && globe_longitude == 32);
    assert(drv_rot_x == 12.0f && drv_rot_y == 34.0f && !drv_auto_enabled);
    assert(memcmp(saved_palette, desktop_palette, sizeof(saved_palette)) == 0);
    if (ami_depth <= 8U)
    {
        assert(palette_writes > 0 && fullscreen_palette[test_pointer_base() + 1U][0] == 17 &&
               fullscreen_palette[test_pointer_base() + 2U][1] == 18 &&
               fullscreen_palette[test_pointer_base() + 3U][2] == 19);
        assert(fullscreen_palette[0][0] == 0 && fullscreen_palette[1][0] == 255);
    }
    test_render();
    assert(copy_calls == copies && swap_successes > 0 && rendered_count > 20);
    test_osd_contains("fullscreen, double buffered");
    test_osd_contains("F fullscreen"); test_osd_contains("P FPS");
    ami_fs_poll();
    /* Safe and displayed notifications are deliberately separated. Releasing
     * only the old buffer permits rendering but cannot permit another swap. */
    hold_safe = hold_display = 1;
    drv_render();
    assert(pending_safe != NULL && pending_display != NULL);
    swaps = swap_successes; draws = rect_calls; calls = area_calls;
    drv_render();
    assert(swap_successes == swaps && rect_calls == draws && area_calls == calls);
    hold_safe = 0;
    drv_render();
    assert(pending_safe == NULL && pending_display != NULL && swap_successes == swaps);
    assert(rect_calls > draws);
    draws = rect_calls;
    drv_render(); assert(rect_calls == draws && swap_successes == swaps);
    hold_safe = 1; hold_display = 0;
    drv_render(); assert(swap_successes == swaps + 1U && pending_safe != NULL);
    /* Optional cache reclamation must not destroy either presentation surface. */
    drv_release_optional();
    assert(screenbuffer_count == 2 && ami_project_cache == NULL && screen_count == 1);
    draws = rect_calls; ami_keyboard('F');
    assert(screen_count == 1 && pending_safe != NULL);
    drv_render(); assert(rect_calls == draws && screen_count == 1);
    hold_safe = 0; drv_render(); test_fullscreen_windowed();
    assert(drv_win_width == saved_width && drv_win_height == saved_height &&
           window_record.LeftEdge == left && window_record.TopEdge == top);
    assert(drv_rot_x == 12.0f && drv_rot_y == 34.0f && !ami_dither_enabled && drv_fps_enabled == old_fps);
    assert(memcmp(saved_palette, desktop_palette, sizeof(saved_palette)) == 0);
    /* A rejected swap is retried on events without rerendering its completed
     * bitmap. Persistent rejection restores Workbench instead of direct draw. */
    ami_keyboard('f'); failed_swaps = 40;
    drv_render(); calls = swap_calls; draws = rect_calls; swaps = swap_successes;
    drv_render(); assert(swap_calls == calls && rect_calls == draws);
    for (i = 0; i < 35U && screen_count; ++i)
    { ami_event(INTUITICKS, 0, 0, 0); drv_render(); }
    failed_swaps = 0;
    test_fullscreen_windowed(); assert(swap_successes == swaps);
    /* Rendering failure keeps the old frame intact through fallback. */
    ami_keyboard('f'); test_render(); ami_fs_poll();
    swaps = swap_successes;
    area_failures = rtg_depth > 8U ? 0U : 3U;
    if (area_failures)
    {
        drv_render(); assert(swap_successes == swaps);
        area_failures = 0; drv_render(); test_fullscreen_windowed();
        ami_keyboard('f');
    }
    /* Close while both notifications are delayed: cleanup must wait and drain
     * them before freeing either bitmap, its ports, or the custom screen. */
    ami_fs_poll(); hold_safe = hold_display = 1;
    drv_render(); assert(pending_safe != NULL && pending_display != NULL);
    calls = notification_waits;
    ami_cleanup();
    assert(notification_waits > calls);
    assert(!screen_count && !screenbuffer_count && !reply_port_count && !fullscreen_window_count);
    assert(!window_count && !library_count && !raster_count && !friend_count);
    assert(!pending_safe && !pending_display && !blitter_busy);
    assert(memcmp(saved_palette, desktop_palette, sizeof(saved_palette)) == 0);
    ami_cleanup(); globe_free();
    printf("PASS Amiga %s: safe fullscreen swaps, independent notifications, palette ownership, failure rollback, input and cleanup\n", mode);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned int i, saved_palette_size, saved_count, level;
    unsigned char saved_palette[sizeof(desktop_palette)];
    unsigned long reference_hash, reference_count;
    float old_rotation;
    double time_before;
    const char *mode;
    mode = argc > 1 ? argv[1] : "ocs";
    test_fpu_integer_conversion();
    fail_cache_allocation = strcmp(mode, "cachefail") == 0;
    if (strcmp(mode, "stack") == 0) { small_stack = 1; assert(atexit(test_fatal_cleanup) == 0); }
    font.tf_YSize = 8; font.tf_XSize = 8; font.tf_Baseline = 6;
    desktop.Width = 640; desktop.Height = 256;
    desktop.BitMap.Depth = 5;
    if (strcmp(mode, "ocswide") == 0 || strcmp(mode, "denisewide") == 0 ||
        strcmp(mode, "ecswide") == 0) desktop.Width = 1280;
    desktop.BitMap.BytesPerRow = (UWORD)(((desktop.Width + 15U) / 16U) * 2U);
    desktop.BitMap.Rows = (UWORD)desktop.Height;
    if (strcmp(mode, "mono") == 0) desktop.BitMap.Depth = 1;
    if (strcmp(mode, "two") == 0) desktop.BitMap.Depth = 2;
    if (strcmp(mode, "three") == 0) desktop.BitMap.Depth = 3;
    if (strcmp(mode, "four") == 0) desktop.BitMap.Depth = 4;
    if (strcmp(mode, "ecs") == 0) { display_version = 36; graphics_base.ChipRevBits0 = GFXF_HR_AGNUS | GFXF_HR_DENISE; }
    if (strcmp(mode, "aga") == 0 || strcmp(mode, "aga64") == 0 || strcmp(mode, "aga128") == 0 ||
        strcmp(mode, "fpu") == 0 || strcmp(mode, "020") == 0 || strcmp(mode, "040") == 0)
    { display_version = 39; desktop.BitMap.Depth = 8; graphics_base.ChipRevBits0 = GFXF_AA_ALICE; exec_base.AttnFlags = AFF_68020; }
    if (strcmp(mode, "aga64") == 0) desktop.BitMap.Depth = 6;
    if (strcmp(mode, "aga128") == 0) desktop.BitMap.Depth = 7;
    if (strcmp(mode, "denisewide") == 0 || strcmp(mode, "ecswide") == 0)
    { display_version = 39; graphics_base.ChipRevBits0 = strcmp(mode, "ecswide") == 0 ? GFXF_HR_AGNUS : GFXF_HR_DENISE; }
    if (strcmp(mode, "fpu") == 0) exec_base.AttnFlags |= AFF_68881;
    if (strcmp(mode, "040") == 0) exec_base.AttnFlags |= AFF_68040 | AFF_FPU40;
    if (strcmp(mode, "falsefpu") == 0) exec_base.AttnFlags = AFF_68881;
    if (strcmp(mode, "aspect") == 0) { display_version = 39; pixel_x = 1; pixel_y = 2; }
    if (strcmp(mode, "backfail") == 0) raster_fail_at = 3;
    if (strcmp(mode, "scratchfail") == 0) raster_fail_at = 1;
    if (strcmp(mode, "nativeunknown") == 0)
    {
        display_version = 39; rtg_unknown = 1; rtg_storage_depth = 32;
        desktop.BitMap.Depth = 1; desktop.BitMap.BytesPerRow = 65535U;
    }
    if (strncmp(mode, "fs-", 3) == 0)
    {
        display_version = strcmp(mode, "fs-old") == 0 ? 34U : 39U;
        if (strcmp(mode, "fs-aga") == 0 || strcmp(mode, "fs-ntsc") == 0 || strcmp(mode, "fs-pointer") == 0)
        { desktop.BitMap.Depth = 8; graphics_base.ChipRevBits0 = GFXF_AA_ALICE; }
        if (strcmp(mode, "fs-pointer") == 0) pointer_base = 240;
#if AMI_RTG
        if (strncmp(mode + 3, "cgx", 3) == 0 || strncmp(mode + 3, "p96", 3) == 0)
        {
            rtg_backend = strncmp(mode + 3, "cgx", 3) == 0 ? 1U : 2U;
            rtg_depth = (unsigned int)atoi(mode + 6);
            rtg_storage_depth = rtg_depth == 15U ? 16U : rtg_depth;
            if (rtg_backend == 2U && rtg_depth == 32U) rtg_depth = 24U;
            desktop.Width = 400; desktop.Height = 300;
            desktop.BitMap.Depth = 1; desktop.BitMap.BytesPerRow = 65535U;
        }
#endif
    }
#if AMI_RTG
    if (strncmp(mode, "cgx", 3) == 0 || strncmp(mode, "p96", 3) == 0 ||
        strncmp(mode, "rtg", 3) == 0)
    {
        display_version = 39;
        rtg_backend = strncmp(mode, "p96", 3) == 0 ? 2U : 1U;
        rtg_depth = (unsigned int)atoi(mode + 3);
        if (rtg_depth == 0) rtg_depth = 32;
        rtg_storage_depth = rtg_depth == 15 ? 16U : rtg_depth;
        /* P96 reports 24 useful RGB bits in a 32-bit ARGB bitmap. */
        if (rtg_backend == 2 && rtg_storage_depth == 32) rtg_depth = 24;
        rtg_unknown = strcmp(mode, "rtgunknown") == 0;
        rtg_both_libraries = strcmp(mode, "rtgboth") == 0 || strcmp(mode, "p96both") == 0;
        friend_failure = strcmp(mode, "rtgbackfail") == 0;
        friend_incompatible = strcmp(mode, "rtgbadfriend") == 0;
        pubscreen_failure = strcmp(mode, "rtglockfail") == 0;
        if (strcmp(mode, "rtgbaddepth") == 0) rtg_depth = rtg_storage_depth = 12;
        if (strcmp(mode, "rtgnative") == 0)
        { rtg_libraries_native = 1; rtg_backend = rtg_depth = rtg_storage_depth = 0; }
        else
        {
            desktop.Width = 1600; desktop.Height = 1200;
            /* RTG bitmaps are opaque. These fields deliberately disagree with
             * actual depth/stride and exceed OCS limits on an OCS motherboard. */
            desktop.BitMap.Depth = 1; desktop.BitMap.BytesPerRow = 65535U;
        }
    }
#endif
    graphics_base.LibNode.lib_Version = intuition_base.LibNode.lib_Version = (UWORD)display_version;
    colormap.Count = rtg_backend ? 256U : (UWORD)(1U << desktop.BitMap.Depth);
    desktop.ViewPort.ColorMap = &colormap;
    desktop.ViewPort.Modes = HIRES; graphics_base.DisplayFlags = PAL;
    if (strcmp(mode, "fs-ntsc") == 0) graphics_base.DisplayFlags = 0;
    if (strcmp(mode, "ham") == 0) desktop.ViewPort.Modes |= HAM;
    for (i = 0; i < colormap.Count; ++i)
    {
        desktop_palette[i][0] = (UBYTE)((i * 73U) & 255U);
        desktop_palette[i][1] = (UBYTE)((i * 151U) & 255U);
        desktop_palette[i][2] = (UBYTE)((i * 199U) & 255U);
    }
    memset(desktop_palette[0], 0, 3); memset(desktop_palette[colormap.Count - 1], 255, 3);
    memcpy(saved_palette, desktop_palette, sizeof(desktop_palette)); saved_palette_size = sizeof(desktop_palette);
    InitRastPort(&window_port); window_port.BitMap = &desktop.BitMap; window_messages.mp_SigBit = 4;
    desktop.RastPort.BitMap = &desktop.BitMap;
    if (strncmp(mode, "fs-", 3) == 0) return test_fullscreen(argc, argv, mode);
#if AMI_RTG
    if (rtg_backend) return test_rtg(argc, argv, mode);
#endif
    drv_ext_init(); globe_init(); drv_init(&argc, argv);
    if (strcmp(mode, "ham") == 0 || strcmp(mode, "ocswide") == 0 || strcmp(mode, "denisewide") == 0 || strcmp(mode, "nativeunknown") == 0)
    {
        assert(!drv_init_window(0, 0, 0, 0)); ami_cleanup();
        assert(library_count == 0 && window_count == 0 && raster_count == 0);
        assert(area_calls == 0 && copy_calls == 0 && rect_calls == 0);
        puts("PASS Amiga unsupported desktop: special palette modes and unsafe OCS stride are rejected before drawing");
        return 0;
    }
    if (strcmp(mode, "oversize") == 0)
        assert(drv_init_window(INT_MAX, INT_MAX, INT_MAX, INT_MAX));
    else assert(drv_init_window(0, 0, 0, 0));
    if (strcmp(mode, "ocs") == 0)
    {
        clock_micros = 100000000UL;
        time_before = ami_time_now();
        clock_micros = 90000000UL;
        assert(ami_time_now() == time_before);
        clock_micros = 91000000UL;
        assert(fabs(ami_time_now() - time_before - 1.0) < 0.000001);
        ami_auto_time = ami_time_now();
        drv_reset_timing();
    }
    assert(window_count == 1 && drv_win_width < (unsigned int)desktop.Width && drv_win_height < 240);
    if (fail_cache_allocation) assert(ami_project_cache == NULL);
    assert(ami_cpu == (strcmp(mode, "040") == 0 ? 68040 : (exec_base.AttnFlags & AFF_68020 ? 68020 : 68000)));
    assert(ami_has_fpu == (strcmp(mode, "fpu") == 0 || strcmp(mode, "040") == 0));
    if (strcmp(mode, "aga") == 0) test_palette_quality();
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS) == TRUE);
    assert(resource_calls == 1 && resource_first == TG_EARTH_32_ID && resource_max == 512);
    assert(!drv_texture_avail && !drv_texture_enabled);
    if (strcmp(mode, "scratchfail") == 0)
    {
        assert(ami_paused && ami_scratch == NULL); test_render(); assert(rendered_count == 0);
        raster_fail_at = 0; ami_keyboard('1'); assert(!ami_paused && ami_scratch != NULL);
    }
    if (strcmp(mode, "backfail") == 0) assert(ami_back_planes == 0 && raster_count == 1);
    test_render(); assert(rendered_count > 20 && text_calls > 0 && strstr(osd_text, "quads") != NULL);
    /* FPS appears once in the help; the statistic uses the shared long label. */
    assert(strstr(osd_text, "FPS") != NULL);
    assert(strstr(strstr(osd_text, "FPS") + 3, "FPS") == NULL);
    assert(baseline_vertices + enhanced_vertices + fpu_vertices == 561);
    test_dither_toggle();
    reference_hash = rendered_hash; reference_count = rendered_count;
    drv_release_optional(); assert(ami_project_cache == NULL && ami_back_planes == 0);
    test_render(); assert(rendered_hash == reference_hash && rendered_count == reference_count);
    ami_project_cache = malloc(sizeof(*ami_project_cache)); assert(ami_project_cache != NULL);
    /* Each resize replaces obsolete rasters and invalidates coordinates. */
    window_record.GZZWidth = 160; window_record.GZZHeight = 300;
    ami_event(NEWSIZE, 0, 0, 0); assert(drv_win_width == 160 && drv_win_height == 300);
    test_render(); assert(rendered_count > 0 && rendered_hash != reference_hash);
    window_record.GZZWidth = 320; window_record.GZZHeight = 200;
    ami_event(NEWSIZE, 0, 0, 0); test_render(); assert(rendered_count > 0);
    saved_count = area_calls;
    ami_dirty = FALSE; drv_rot_y += 15.0f;
    ami_event(REFRESHWINDOW, 0, 0, 0);
    assert(refresh_count == 0 && ami_dirty && area_calls == saved_count);
    test_render(); assert(!ami_dirty && area_calls > saved_count);
    if (strcmp(mode, "ocs") == 0)
    {
        saved_count = area_calls; desktop.BitMap.Depth = 9;
        ami_event(NEWSIZE, 0, 0, 0); assert(!ami_ready);
        ami_event(ACTIVEWINDOW, 0, 0, 0); ami_event(REFRESHWINDOW, 0, 0, 0);
        drv_render(); assert(area_calls == saved_count);
        desktop.BitMap.Depth = 5; ami_event(NEWSIZE, 0, 0, 0); assert(ami_ready);
        test_render(); assert(rendered_count > 0);
    }
    if (strcmp(mode, "ocs") == 0)
    {
        for (i = 0; i < 2; ++i)
        {
            saved_count = area_calls + copy_calls + rect_calls;
            window_record.GZZWidth = i == 0 ? 993 : 320;
            window_record.GZZHeight = i == 0 ? 200 : 1025;
            ami_event(NEWSIZE, 0, 0, 0); assert(!ami_ready);
            ami_event(ACTIVEWINDOW, 0, 0, 0); ami_event(REFRESHWINDOW, 0, 0, 0);
            drv_render(); assert(area_calls + copy_calls + rect_calls == saved_count);
            window_record.GZZWidth = 320; window_record.GZZHeight = 200;
            ami_event(NEWSIZE, 0, 0, 0); assert(ami_ready);
            test_render(); assert(rendered_count > 0);
        }
    }
    old_rotation = drv_rot_y;
    ami_event(MOUSEBUTTONS, SELECTDOWN, 10, 10); ami_event(MOUSEMOVE, 0, 50, 30);
    ami_event(MOUSEBUTTONS, SELECTUP, 50, 30); assert(drv_rot_y != old_rotation && !drv_lmouse_pressed);
    ami_event(INACTIVEWINDOW, 0, 0, 0); assert(!ami_active);
    ami_event(ACTIVEWINDOW, 0, 0, 0); assert(ami_active);
    ami_keyboard('t'); assert(!drv_texture_enabled);
    ami_keyboard('o'); assert(!drv_osd_enabled); ami_keyboard('o'); assert(drv_osd_enabled);
    ami_keyboard('p'); assert(!drv_fps_enabled); ami_keyboard('P'); assert(drv_fps_enabled);
    if (strcmp(mode, "ocs") == 0)
    {
        for (level = 32, i = 1; level <= 512; level *= 2, ++i)
        { ami_keyboard((unsigned char)('0' + i)); assert(globe_ready()); assert(globe_longitude == (int)level); }
        assert(resource_max == 16384); ami_keyboard('1');
        saved_count = area_calls; area_failures = 1; test_render(); assert(area_calls > saved_count && !ami_paused);
        area_failures = 3; drv_render(); assert(ami_paused && !drv_auto_enabled);
        area_failures = 0; ami_keyboard('1'); test_render(); assert(!ami_paused && rendered_count > 0);
    }
    /* Workbench owns palette changes; refresh/activation must invalidate the
     * driver's mapping while leaving every native pen untouched. */
    memset(desktop_palette[0], 255, 3);
    memset(desktop_palette[colormap.Count - 1], 0, 3);
    ami_event(REFRESHWINDOW, 0, 0, 0);
    assert(ami_black == colormap.Count - 1U && ami_white == 0U);
    assert(ami_palette[0][0] == 255 && ami_palette[colormap.Count - 1U][0] == 0);
    assert(desktop_palette[0][0] == 255 && desktop_palette[colormap.Count - 1U][0] == 0);
    memcpy(desktop_palette, saved_palette, saved_palette_size);
    ami_event(ACTIVEWINDOW, 0, 0, 0);
    assert(ami_black == 0U && ami_white == colormap.Count - 1U);
    assert(memcmp(saved_palette, desktop_palette, saved_palette_size) == 0);
    messages[0].Class = VANILLAKEY; messages[0].Code = 'p';
    messages[1].Class = MOUSEBUTTONS; messages[1].Code = SELECTDOWN;
    messages[2].Class = MOUSEMOVE; messages[2].MouseX = 20; messages[2].MouseY = 10;
    messages[3].Class = MOUSEBUTTONS; messages[3].Code = SELECTUP;
    messages[4].Class = CLOSEWINDOW; message_count = 5;
    drv_loop();
    assert(reply_count == 5 && library_count == 0 && window_count == 0 && raster_count == 0);
    assert(!ami_running && !ami_ready && ami_project_cache == NULL);
    globe_free();
    printf("PASS Amiga %s: palette-preserving Workbench window, bounded native fills, selected resources, CPU dispatch, cache fallback, resize/input and cleanup\n", mode);
    return 0;
}
