/* Execute the Classic Macintosh driver against Toolbox call records.
 * This is NOT an MPW SDK, ABI validation, or QuickDraw renderer. */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Classic.h>
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"
#include "qd_math.h"
#include "earth_resources.h"
#include "soft3d_cases.h"

#include "../src/drv_qd.c"
static int explicit_start;
static int qd_test_init_window(int x, int y, int width, int height)
{
    if (explicit_start == 1 || explicit_start == 2) width = 400;
    if (explicit_start == 1 || explicit_start == 3) height = 300;
    return drv_init_window(x, y, width, height);
}
#define drv_init_window qd_test_init_window
#define main testglobe_main
#include "../src/main.c"
#undef main
#undef drv_init_window

extern Vertex *globe_verts;
extern int globe_longitude, globe_latitude;

static GrafPort desktop_port;
static CGrafPort window_port;
static GrafPtr current_port;
static GDHandle current_device;
static WindowPtr window_live;
static PixMap screen_pixmap;
static PixMap *screen_pixmap_ptr = &screen_pixmap;
static GDevice screen_device;
static GDevice *screen_device_ptr = &screen_device;
static PixMap second_pixmap;
static PixMap *second_pixmap_ptr = &second_pixmap;
static GDevice second_device;
static GDevice *second_device_ptr = &second_device;
static int multiple_devices;
static GWorldPtr world_live;
static GDHandle world_device;
static GDevice offscreen_device;
static GDevice *offscreen_device_ptr = &offscreen_device;
static PixMap world_pixmap;
static PixMap *world_pixmap_ptr = &world_pixmap;
static int world_locked;
static short display_depth = 8;
static int has_color = TRUE;
static long qd_version = gestalt32BitQD;
static long cpu_type = gestalt68000, fpu_type = gestaltNoFPU;
static int fail_gworld;
static int fail_lock;
static int failure_profile;
static int fail_polygon;
static int fail_pattern;
static int fail_scratch;
static unsigned int scratch_calls;
static int fail_color_table;
static int fail_palette;
static int fail_resource;
static OSErr resource_error;
static Handle resource_lock_failure;
static Ptr resource_lock_bytes;
static unsigned int pattern_calls;
static unsigned int rgb_pattern_calls, color_fill_calls, solid_fill_calls;
static RGBColor last_pattern_rgb;
static unsigned int polygon_calls;
static short selected_font, selected_size = 9;
static unsigned int available_fonts = 7U;
static unsigned int font_queries;
static unsigned int loop_text_calls;
static short grow_width = 800, grow_height = 600;
static short find_part = inGoAway;
static unsigned long ticks;
static unsigned int window_calls, color_calls, world_calls, copy_calls;
static unsigned int palette_calls, text_calls, alert_calls;
static unsigned int ptr_count, handle_count, menu_count;
static unsigned int world_count, palette_count, window_count;
static void *memory_blocks[64];
static void *handle_blocks[64];
static Size handle_sizes[64];
static unsigned int handle_locks[64];
static PaletteHandle window_palette;
typedef struct { RGBColor foreground, background; } Test_port_colors;
static Test_port_colors desktop_colors, window_colors, world_colors;
static Point pen;
static int pen_hidden;
static unsigned int pixpat_count;
static unsigned int recorded_count;
static int next_event_stage, contract_started;
static int mouse_down;
static float loop_yaw;
static Point mouse_position;
static int run_contract(void);
static short screen_width = 1024, screen_height = 768;
static unsigned int expected_width = 640, expected_height = 480;
static int screen_mode;
static int streaming_polygons;
static char osd_text[1024];
/* System 7 services: version, Apple events, memory, cursor and updates. */
#define TEST_APP_A5 0x00a5a5a0L
#define TEST_FOREIGN_A5 0x00f0f0f0L
static long system_version = 0x0710;
static int apple_events = TRUE;
static int zone_ready, toolbox_started;
static unsigned int master_blocks;
static GrowZoneUPP grow_zone;
static long current_a5 = TEST_APP_A5;
static OSErr memory_error;
static Size max_block = 64L * 1024L * 1024L;
static int fail_reallocate;
/* Checked driver allocations under pressure ask the grow zone and fail;
 * the next unchecked Toolbox fill takes the reserve from it. */
static int memory_pressure, toolbox_pressure;
static unsigned int grow_zone_calls, grow_zone_gifts;
static Cursor watch_cursor;
static CursPtr watch_pointer = &watch_cursor;
static int cursor_watch;
static unsigned int watch_allocations, cursor_calls;
static int update_pending, in_update;
static unsigned int valid_calls, erase_calls, update_copies;
static unsigned int fore_calls, back_calls;
static Point window_origin;
static short drag_dx;
static Region gray_region;
static Region *gray_pointer = &gray_region;
static unsigned int char_width_calls, text_width_calls;
static unsigned int color_index_calls;
static int hidden_colors = TRUE;
static unsigned int about_calls, desk_calls, resource_menu_calls;
static unsigned char param_text[256];
static int expect_fatal;
static int quit_mode;
typedef struct { AEEventClass event_class; AEEventID id; AEEventHandlerUPP handler; long refcon; } Test_ae_handler;
static Test_ae_handler ae_handlers[8];
static unsigned int ae_handler_count;
static int ae_missed;
static OSErr ae_result;

typedef struct {
    int count;
    Point points[12];
    unsigned long color;
    unsigned int pattern_black;
} RecordedPolygon;
static RecordedPolygon recorded[5000];

static void color_api(void) { assert(has_color); ++color_calls; }
/* System 7 gives basic QuickDraw 1-bit GWorlds; Color QuickDraw needs 32-bit QD. */
static void gworld_api(void)
{
    if (has_color) { color_api(); assert(qd_version >= gestalt32BitQD); }
    else assert(system_version >= 0x0700);
}
/* Global coordinates stop at 32767, like the driver's own clamp. */
static void test_window_global(Rect *bounds)
{
    long right, bottom;
    right = (long)window_port.portRect.right + window_origin.h;
    bottom = (long)window_port.portRect.bottom + window_origin.v;
    bounds->left = (short)(window_port.portRect.left + window_origin.h);
    bounds->top = (short)(window_port.portRect.top + window_origin.v);
    bounds->right = (short)(right > 32767L ? 32767L : right);
    bounds->bottom = (short)(bottom > 32767L ? 32767L : bottom);
}
/* Mirror GetMaxDevice: the deepest device that intersects the rectangle. */
static GDHandle test_max_device(const Rect *r)
{
    GDHandle device, best;
    best = NULL;
    for (device = &screen_device_ptr; device != NULL; device = (**device).gdNextGD) {
        if ((**device).gdRect.left >= r->right || (**device).gdRect.right <= r->left ||
            (**device).gdRect.top >= r->bottom || (**device).gdRect.bottom <= r->top) continue;
        if (best == NULL || (**(**device).gdPMap).pixelSize > (**(**best).gdPMap).pixelSize)
            best = device;
    }
    return best;
}
static short test_window_depth(void)
{
    Rect bounds;
    GDHandle device;
    if (!has_color) return 1;
    test_window_global(&bounds);
    device = test_max_device(&bounds);
    assert(device != NULL);
    return (**(**device).gdPMap).pixelSize;
}
static int same_rgb(const RGBColor *a, const RGBColor *b)
{
    return a->red == b->red && a->green == b->green && a->blue == b->blue;
}
/* A simulated inverse table: index % 5 == 2 hides behind its neighbour. */
static long test_color_index(const RGBColor *rgb)
{
    CTabHandle table;
    long i;
    table = world_pixmap.pmTable;
    assert(table != NULL && (*table)->ctSize == 255);
    for (i = 0; i < 256; ++i)
        if (same_rgb(&(*table)->ctTable[i].rgb, rgb))
            return hidden_colors && i % 5 == 2 ? (i + 1) & 255 : i;
    assert(!"Earth GWorld probed a color outside its table");
    return 0;
}
static int test_exact(const RGBColor *rgb)
{
    return same_rgb(&(*world_pixmap.pmTable)->ctTable[test_color_index(rgb)].rgb, rgb);
}
/* A Toolbox allocation the driver cannot check takes the reserve. */
static void test_toolbox_allocation(void)
{
    long freed;
    if (!toolbox_pressure) return;
    toolbox_pressure = FALSE;
    assert(grow_zone != NULL && !qd_checked);
    current_a5 = TEST_FOREIGN_A5;
    freed = grow_zone(2048);
    assert(current_a5 == TEST_FOREIGN_A5);
    current_a5 = TEST_APP_A5;
    assert(freed == QD_RESERVE_BYTES && qd_reserve_spent && *qd_reserve == NULL);
    assert(grow_zone(2048) == 0); /* Nothing is left to give. */
    ++grow_zone_gifts;
}
static Test_port_colors *test_current_colors(void)
{
    if (current_port == &desktop_port) return &desktop_colors;
    if (current_port == (GrafPtr)&window_port) return &window_colors;
    assert(world_live != NULL && current_port == (GrafPtr)world_live);
    return &world_colors;
}
static void test_init_colors(Test_port_colors *colors)
{
    memset(colors, 0, sizeof(*colors));
    colors->background.red = colors->background.green = colors->background.blue = 65535U;
}
static void test_drawing_device(void)
{
    if (world_live != NULL && current_port == (GrafPtr)world_live)
        assert(current_device == world_device);
    else assert(current_device == qd_device);
}
static unsigned long rgb_value(const RGBColor *c)
{
    return ((unsigned long)(c->red >> 8) << 16) |
           ((unsigned long)(c->green >> 8) << 8) | (c->blue >> 8);
}
static unsigned int pattern_bits(const unsigned char *pattern)
{
    unsigned int total, i, j;
    total = 0;
    for (i = 0; i < 8; ++i)
        for (j = 0; j < 8; ++j)
            total += (pattern[i] >> j) & 1U;
    return total;
}
Ptr NewPtr(Size size)
{
    unsigned int i;
    assert(size > 0 && size <= 0x7fffffffL);
    if (cursor_watch) ++watch_allocations;
    if (memory_pressure && grow_zone != NULL) {
        ++grow_zone_calls;
        assert(qd_checked && grow_zone(size) == 0); /* Mesh storage is checked. */
        return NULL;
    }
    for (i = 0; i < 64; ++i) {
        if (memory_blocks[i] != NULL) continue;
        memory_blocks[i] = calloc((size_t)size, 1);
        assert(memory_blocks[i] != NULL);
        ++ptr_count;
        return (Ptr)memory_blocks[i];
    }
    assert(!"Macintosh pointer allocation leak");
    return NULL;
}
void DisposePtr(Ptr memory)
{
    unsigned int i;
    for (i = 0; i < 64; ++i) {
        if (memory_blocks[i] != memory) continue;
        assert(memory != NULL && ptr_count > 0);
        free(memory_blocks[i]); memory_blocks[i] = NULL; --ptr_count;
        return;
    }
    assert(!"Unknown Macintosh pointer");
}
Handle NewHandle(Size size)
{
    unsigned int i;
    Handle handle;
    assert(size > 0);
    if (size == (Size)QD_POLYGON_BYTES) {
        ++polygon_calls;
        if (fail_polygon) {
            if (fail_polygon == 2) fail_polygon = 0;
            return NULL;
        }
    }
    if (size == (Size)QD_PATTERN_SCRATCH || size == (Size)QD_FILL_SCRATCH) {
        ++scratch_calls;
        if (fail_scratch || (fail_pattern && size == (Size)QD_PATTERN_SCRATCH))
            return NULL;
    }
    if (fail_color_table && size == (Size)(sizeof(ColorTable) +
                                          255L * sizeof(ColorSpec))) return NULL;
    if (memory_pressure && grow_zone != NULL) {
        long freed;
        ++grow_zone_calls;
        current_a5 = TEST_FOREIGN_A5;
        freed = grow_zone(size);
        assert(current_a5 == TEST_FOREIGN_A5);
        current_a5 = TEST_APP_A5;
        /* The driver's own NewHandle calls recover from NULL themselves. */
        assert(qd_checked && freed == 0);
        return NULL;
    }
    handle = (Handle)malloc(sizeof(Ptr));
    assert(handle != NULL);
    *handle = (Ptr)calloc((size_t)size, 1);
    assert(*handle != NULL);
    for (i = 0; i < 64; ++i) {
        if (handle_blocks[i] != NULL) continue;
        handle_blocks[i] = handle; handle_sizes[i] = size;
        handle_locks[i] = 0; ++handle_count;
        return handle;
    }
    assert(!"Macintosh handle leak");
    return NULL;
}
void EmptyHandle(Handle handle)
{
    unsigned int i;
    /* A grow zone touching its globals must have switched to its own A5. */
    assert(current_a5 == TEST_APP_A5);
    for (i = 0; i < 64; ++i) {
        if (handle_blocks[i] != handle) continue;
        assert(handle_locks[i] == 0 && *handle != NULL);
        free(*handle); *handle = NULL; handle_sizes[i] = 0;
        return;
    }
    assert(!"Unknown emptied Macintosh handle");
}
void ReallocateHandle(Handle handle, Size size)
{
    unsigned int i;
    for (i = 0; i < 64; ++i) {
        if (handle_blocks[i] != handle) continue;
        assert(*handle == NULL && size > 0);
        if (fail_reallocate) {
            /* The Memory Manager first asks the grow zone, which declines. */
            assert(grow_zone(size) == 0);
            memory_error = memFullErr;
            return;
        }
        *handle = (Ptr)calloc((size_t)size, 1);
        assert(*handle != NULL);
        handle_sizes[i] = size; memory_error = noErr;
        return;
    }
    assert(!"Unknown reallocated Macintosh handle");
}
void DisposeHandle(Handle handle)
{
    unsigned int i;
    for (i = 0; i < 64; ++i) {
        if (handle_blocks[i] != handle) continue;
        assert(handle != NULL && handle_count > 0 && handle_locks[i] == 0);
        free(*handle); free(handle); handle_blocks[i] = NULL; --handle_count;
        return;
    }
    assert(!"Unknown Macintosh handle");
}
void HLock(Handle h)
{
    unsigned int i;
    for (i = 0; i < 64; ++i)
        if (handle_blocks[i] == h) {
            ++handle_locks[i];
            if (h == resource_lock_failure) {
                resource_lock_bytes = *h;
                *h = NULL;
            }
            return;
        }
    assert(!"Unknown locked Macintosh handle");
}
void HUnlock(Handle h)
{
    unsigned int i;
    for (i = 0; i < 64; ++i)
        if (handle_blocks[i] == h) {
            if (h == resource_lock_failure) {
                *h = resource_lock_bytes;
                resource_lock_failure = NULL;
                resource_lock_bytes = NULL;
            }
            assert(handle_locks[i] > 0); --handle_locks[i]; return;
        }
    assert(!"Unknown unlocked Macintosh handle");
}
Size GetHandleSize(Handle h)
{
    unsigned int i;
    for (i = 0; i < 64; ++i)
        if (handle_blocks[i] == h) return handle_sizes[i];
    assert(!"Unknown sized Macintosh handle");
    return 0;
}
Handle Get1Resource(OSType type, short id)
{
    unsigned long size;
    unsigned char *bytes;
    Handle handle;
    assert(type == 0x45415254UL);
    resource_error = noErr;
    if (fail_resource == 1 || fail_resource == 2) {
        resource_error = fail_resource == 1 ? memFullErr : -192;
        return NULL;
    }
    bytes = test_earth_resource(id, &size);
    handle = NewHandle((Size)(size + (fail_resource == 3 ? 1UL : 0UL)));
    if (handle == NULL) {
        resource_error = memFullErr;
        free(bytes);
        return NULL;
    }
    memcpy(*handle, bytes, (size_t)size);
    free(bytes);
    if (fail_resource == 4) resource_lock_failure = handle;
    return handle;
}
void ReleaseResource(Handle h) { DisposeHandle(h); }
OSErr ResError(void) { return resource_error; }
OSErr MemError(void) { return memory_error; }
void MoreMasters(void) { assert(zone_ready && !toolbox_started && handle_count == 0); ++master_blocks; }
void SetGrowZone(GrowZoneUPP proc) { assert(proc != NULL && qd_reserve != NULL); grow_zone = proc; }
Size MaxBlock(void) { return max_block; }
long SetCurrentA5(void) { long old = current_a5; current_a5 = TEST_APP_A5; return old; }
long SetA5(long a5) { long old = current_a5; current_a5 = a5; return old; }
unsigned long TickCount(void) { ticks = (ticks + 1UL) & 0xffffffffUL; return ticks; }
void InitGraf(GrafPtr *port)
{
    assert(zone_ready && master_blocks >= 2);
    toolbox_started = TRUE;
    test_init_colors(&desktop_colors);
    memset(&desktop_port, 0, sizeof(desktop_port));
    desktop_port.portRect.right = screen_width; desktop_port.portRect.bottom = screen_height;
    desktop_port.portBits.bounds = desktop_port.portRect;
    qd.screenBits = desktop_port.portBits;
    memset(qd.black.pat, 255, 8); memset(qd.white.pat, 0, 8);
    current_port = &desktop_port;
    current_device = has_color ? &screen_device_ptr : NULL;
    *port = current_port;
}
void InitFonts(void) {}
void InitWindows(void) {}
void InitMenus(void) {}
void TEInit(void) {}
void InitDialogs(void *resume) { assert(resume == NULL); }
void InitCursor(void) {}
void MaxApplZone(void) { assert(!zone_ready); zone_ready = TRUE; }
OSErr Gestalt(OSType selector, long *response)
{
    assert(response != NULL);
    if (selector == gestaltQuickdrawVersion) *response = qd_version;
    else if (selector == gestaltProcessorType) *response = cpu_type;
    else if (selector == gestaltFPUType) *response = fpu_type;
    else if (selector == gestaltSystemVersion) *response = system_version;
    else if (selector == gestaltAppleEventsAttr && apple_events)
        *response = 1L << gestaltAppleEventsPresent;
    else { *response = 0; return -5551; }
    return noErr;
}
GDHandle GetDeviceList(void) { color_api(); return &screen_device_ptr; }
GDHandle GetMainDevice(void) { color_api(); return &screen_device_ptr; }
GDHandle GetGDevice(void) { color_api(); return current_device; }
void SetGDevice(GDHandle device)
{
    color_api();
    assert(device == &screen_device_ptr || (multiple_devices && device == &second_device_ptr));
    current_device = device;
}
GDHandle GetNextDevice(GDHandle device) { color_api(); assert(device == &screen_device_ptr || device == &second_device_ptr); return (**device).gdNextGD; }
GDHandle GetMaxDevice(const Rect *r) { color_api(); assert(r != NULL); return test_max_device(r); }
Boolean TestDeviceAttribute(GDHandle device, short attribute)
{
    color_api(); assert(device == &screen_device_ptr || device == &second_device_ptr);
    assert(attribute == screenDevice || attribute == screenActive || attribute == mainScreen);
    return TRUE;
}
static WindowPtr create_window(const Rect *bounds, ConstStr255Param title,
                                Boolean visible, Boolean close_box)
{
    assert(window_live == NULL && bounds != NULL && title != NULL && title[0] != 0);
    assert(visible && close_box);
    memset(&window_port, 0, sizeof(window_port));
    test_init_colors(&window_colors);
    window_port.portRect.right = bounds->right - bounds->left;
    window_port.portRect.bottom = bounds->bottom - bounds->top;
    window_port.portBits.bounds = window_port.portRect;
    window_port.portPixMap = multiple_devices ? &second_pixmap_ptr : &screen_pixmap_ptr;
    window_origin.h = bounds->left; window_origin.v = bounds->top;
    window_live = (WindowPtr)&window_port;
    ++window_calls; ++window_count;
    return window_live;
}
WindowPtr NewWindow(Ptr storage, const Rect *bounds, ConstStr255Param title,
                     Boolean visible, short proc, WindowPtr behind,
                     Boolean close_box, long refcon)
{
    assert(!has_color && storage == NULL); (void)proc; (void)behind; (void)refcon;
    return create_window(bounds, title, visible, close_box);
}
WindowPtr NewCWindow(Ptr storage, const Rect *bounds, ConstStr255Param title,
                      Boolean visible, short proc, WindowPtr behind,
                      Boolean close_box, long refcon)
{
    color_api(); assert(storage == NULL); (void)proc; (void)behind; (void)refcon;
    return create_window(bounds, title, visible, close_box);
}
void DisposeWindow(WindowPtr window)
{
    assert(window_live != NULL && window == window_live && window_count == 1);
    window_live = NULL; --window_count;
    if (current_port == (GrafPtr)&window_port) current_port = &desktop_port;
}
void ShowWindow(WindowPtr w) { assert(w == window_live); }
void SelectWindow(WindowPtr w) { assert(w == window_live); update_pending = TRUE; }
WindowPtr FrontWindow(void) { return window_live; }
void SetPort(GrafPtr port) { assert(port != NULL); current_port = port; }
void GetPort(GrafPtr *port) { assert(port != NULL); *port = current_port; }
void SetGWorld(CGrafPtr port, GDHandle device)
{
    gworld_api(); assert(port != NULL);
    if (port == world_live) assert(world_locked);
    current_port = (GrafPtr)port;
    current_device = port == world_live && device == NULL ? world_device : device;
}
void GetGWorld(CGrafPtr *port, GDHandle *device)
{
    gworld_api(); assert(port != NULL && device != NULL);
    *port = (CGrafPtr)current_port; *device = current_device;
}
QDErr NewGWorld(GWorldPtr *world, short depth, const Rect *bounds,
                 CTabHandle table, GDHandle device, unsigned long flags)
{
    gworld_api(); ++world_calls;
    assert(world_live == NULL && world != NULL && bounds != NULL);
    assert(depth == test_window_depth());
    /* Only an 8-bit world takes the Earth table; others keep the standard one. */
    assert(table == (depth == 8 ? qd_colors : NULL));
    assert(has_color || (flags == 0 && device == NULL));
    assert(qd_checked); /* NewGWorld reports memFullErr; it must not take the reserve. */
    if (memory_pressure) {
        ++grow_zone_calls;
        assert(grow_zone(4096) == 0);
        *world = NULL; return memFullErr;
    }
    if (fail_gworld) { *world = NULL; return memFullErr; }
    world_live = (GWorldPtr)calloc(1, sizeof(CGrafPort));
    assert(world_live != NULL);
    test_init_colors(&world_colors);
    world_live->portRect = *bounds;
    world_live->portBits.bounds = *bounds;
    world_pixmap.bounds = *bounds; world_pixmap.pixelSize = depth;
    world_pixmap.pmTable = table;
    world_pixmap.rowBytes = (short)(((bounds->right - bounds->left) * depth + 7) / 8);
    world_pixmap.baseAddr = (Ptr)calloc((size_t)(world_pixmap.rowBytes & 0x3fff) *
                                      (bounds->bottom - bounds->top), 1);
    assert(world_pixmap.baseAddr != NULL);
    /* Basic QuickDraw worlds are GrafPorts, so the driver must ask
     * GetGWorldPixMap; only Color QuickDraw before 32-bit QD 1.2 lacks it. */
    world_live->portPixMap = has_color && qd_version < gestalt32BitQD12 ? &world_pixmap_ptr : NULL;
    *world = world_live;
    if (!has_color) world_device = NULL;
    else if (flags & noNewDevice) {
        assert(device != NULL);
        world_device = device;
    } else {
        /* Native QuickDraw ignores the supplied device and owns a new
         * offscreen GDevice when noNewDevice is clear. */
        memset(&offscreen_device, 0, sizeof(offscreen_device));
        offscreen_device.gdPMap = &world_pixmap_ptr;
        offscreen_device.gdRect = *bounds;
        world_device = &offscreen_device_ptr;
        assert(world_device != qd_device);
    }
    ++world_count;
    return noErr;
}
PixMapHandle GetGWorldPixMap(GWorldPtr world)
{
    gworld_api(); assert(world == world_live && (!has_color || qd_version >= gestalt32BitQD12));
    return &world_pixmap_ptr;
}
Boolean LockPixels(PixMapHandle pixels) { gworld_api(); assert(pixels == &world_pixmap_ptr && !world_locked); if (fail_lock) return FALSE; world_locked = TRUE; return TRUE; }
void UnlockPixels(PixMapHandle pixels) { gworld_api(); assert(pixels == &world_pixmap_ptr && world_locked); world_locked = FALSE; }
void DisposeGWorld(GWorldPtr world)
{
    gworld_api(); assert(world == world_live && !world_locked && world_count == 1);
    assert(current_port != (GrafPtr)world);
    free(world_pixmap.baseAddr); world_pixmap.baseAddr = NULL;
    free(world_live); world_live = NULL; --world_count;
}
void SetRect(Rect *r, short left, short top, short right, short bottom)
{ r->left = left; r->top = top; r->right = right; r->bottom = bottom; }
void OffsetRect(Rect *r, short dx, short dy)
{ r->left += dx; r->right += dx; r->top += dy; r->bottom += dy; }
void ClipRect(const Rect *r) { assert(r != NULL && current_port != NULL); }
RgnHandle NewRgn(void) { return (RgnHandle)NewHandle((Size)sizeof(Region)); }
void DisposeRgn(RgnHandle r) { DisposeHandle((Handle)r); }
void GetClip(RgnHandle r) { assert(r != NULL); }
void SetClip(RgnHandle r) { assert(r != NULL); }
void EraseRect(const Rect *r) { assert(r != NULL && current_port != NULL); ++erase_calls; }
void PaintRect(const Rect *r) { assert(r != NULL && current_port != NULL); }
/* Native OpenPoly/NewPixPat may invoke SysError on exhaustion. The
 * production renderer must use checked polygon storage and preflight the
 * optional pattern path, not depend on these calls returning NULL. */
PolyHandle OpenPoly(void) { assert(!"Production must not record polygons"); return NULL; }
void ClosePoly(void) { assert(!"Production must not record polygons"); }
void KillPoly(PolyHandle p) { (void)p; assert(!"Reusable polygons use DisposeHandle"); }
void MoveTo(short h, short v) { pen.h = h; pen.v = v; }
void LineTo(short h, short v) { pen.h = h; pen.v = v; }
static void record_polygon(PolyHandle poly, unsigned int black, const RGBColor *color)
{
    RecordedPolygon *entry;
    int i, count;
    Point *points;
    Rect bounds;
    assert(poly != NULL && *poly != NULL && current_port != NULL && !pen_hidden);
    assert(GetHandleSize((Handle)poly) == (Size)QD_POLYGON_BYTES);
    assert((**poly).polySize >= (short)(offsetof(Polygon, polyPoints) + 4U * sizeof(Point)));
    assert((**poly).polySize <= (short)QD_POLYGON_BYTES);
    assert(((**poly).polySize - offsetof(Polygon, polyPoints)) % sizeof(Point) == 0);
    count = (int)(((**poly).polySize - offsetof(Polygon, polyPoints)) / sizeof(Point)) - 1;
    points = (Point *)((char *)*poly + offsetof(Polygon, polyPoints));
    assert(points[count].h == points[0].h && points[count].v == points[0].v);
    bounds.left = bounds.right = points[0].h;
    bounds.top = bounds.bottom = points[0].v;
    for (i = 0; i < count; ++i) {
        if (points[i].h < bounds.left) bounds.left = points[i].h;
        if (points[i].h > bounds.right) bounds.right = points[i].h;
        if (points[i].v < bounds.top) bounds.top = points[i].v;
        if (points[i].v > bounds.bottom) bounds.bottom = points[i].v;
    }
    assert(memcmp(&bounds, &(**poly).polyBBox, sizeof(bounds)) == 0);
    assert(count >= 3 && count <= 10);
    for (i = 0; i < count; ++i) {
        assert(points[i].h != points[(i + 1) % count].h ||
               points[i].v != points[(i + 1) % count].v);
    }
    if (streaming_polygons) {
        long area = 0;
        for (i = 0; i < count; ++i) {
            assert(points[i].h >= 0 && points[i].h <= (int)drv_win_width);
            assert(points[i].v >= 0 && points[i].v <= (int)drv_win_height);
            area += (long)points[i].h * points[(i + 1) % count].v -
                    (long)points[i].v * points[(i + 1) % count].h;
        }
        assert(area < 0);
        ++recorded_count;
        return;
    }
    assert(recorded_count < 5000);
    entry = recorded + recorded_count++;
    entry->count = count; entry->color = rgb_value(color); entry->pattern_black = black;
    for (i = 0; i < count; ++i) {
        entry->points[i] = points[i];
        assert(entry->points[i].h >= 0 && entry->points[i].h <= (int)drv_win_width);
        assert(entry->points[i].v >= 0 && entry->points[i].v <= (int)drv_win_height);
    }
}
void PaintPoly(PolyHandle p)
{
    assert(has_color); ++solid_fill_calls; test_toolbox_allocation();
    /* An Earth GWorld paints only colors its inverse table returns exactly. */
    if (qd_buffered && qd_world_earth && !qd_pattern_unavailable)
        assert(test_exact(&test_current_colors()->foreground));
    record_polygon(p, 64U, &test_current_colors()->foreground);
}
void FillPoly(PolyHandle p, const Pattern *pattern)
{
    assert(pattern != NULL); test_toolbox_allocation();
    /* Monochrome passes set black on white once, not per polygon. */
    assert(rgb_value(&test_current_colors()->foreground) == 0);
    assert(rgb_value(&test_current_colors()->background) == 0xffffffUL);
    record_polygon(p, pattern_bits(pattern->pat), &test_current_colors()->foreground);
}
void PenSize(short h, short v) { assert(h >= 0 && v >= 0); }
void PenNormal(void) {}
void HidePen(void) { assert(!pen_hidden); pen_hidden = TRUE; }
void ShowPen(void) { assert(pen_hidden); pen_hidden = FALSE; }
PixPatHandle NewPixPat(void)
{
    color_api();
    ++pattern_calls;
    assert(!fail_pattern);
    ++pixpat_count;
    return (PixPatHandle)NewHandle((Size)sizeof(PixPat));
}
void DisposePixPat(PixPatHandle p) { color_api(); assert(pixpat_count > 0); DisposeHandle((Handle)p); --pixpat_count; }
void MakeRGBPat(PixPatHandle p, const RGBColor *c)
{
    color_api(); assert(p != NULL && c != NULL); test_drawing_device(); ++rgb_pattern_calls;
    if (qd_buffered && qd_world_earth) assert(!test_exact(c)); /* Exact colors need no dither. */
    last_pattern_rgb = *c; (*p)->color = *c;
}
void FillCPoly(PolyHandle p, PixPatHandle pattern) { color_api(); assert(pattern != NULL); ++color_fill_calls; test_toolbox_allocation(); record_polygon(p, 64U, &(*pattern)->color); }
void ForeColor(long color)
{
    RGBColor *foreground;
    assert(color == blackColor || color == whiteColor);
    ++fore_calls;
    foreground = &test_current_colors()->foreground;
    foreground->red = foreground->green = foreground->blue = color == blackColor ? 0 : 65535U;
}
void BackColor(long color)
{
    RGBColor *background;
    assert(color == blackColor || color == whiteColor);
    ++back_calls;
    background = &test_current_colors()->background;
    background->red = background->green = background->blue = color == blackColor ? 0 : 65535U;
}
void RGBForeColor(const RGBColor *color) { color_api(); assert(color != NULL); test_drawing_device(); test_current_colors()->foreground = *color; }
void RGBBackColor(const RGBColor *color) { color_api(); assert(color != NULL); test_drawing_device(); test_current_colors()->background = *color; }
void GetForeColor(RGBColor *color) { color_api(); assert(color != NULL); *color = test_current_colors()->foreground; }
void GetBackColor(RGBColor *color) { color_api(); assert(color != NULL); *color = test_current_colors()->background; }
long Color2Index(const RGBColor *rgb)
{
    color_api(); assert(rgb != NULL);
    /* Probed only while drawing into the Earth GWorld, on its own device. */
    assert(qd_buffered && qd_world_earth && current_device == world_device);
    assert(world_device == &offscreen_device_ptr);
    ++color_index_calls;
    return test_color_index(rgb);
}
void Index2Color(long index, RGBColor *rgb)
{
    color_api(); assert(index >= 0 && index < 256 && rgb != NULL);
    assert(current_device == world_device && world_pixmap.pmTable != NULL);
    *rgb = (*world_pixmap.pmTable)->ctTable[index].rgb;
}
CursHandle GetCursor(short id) { assert(id == watchCursor); return &watch_pointer; }
void SetCursor(const Cursor *cursor)
{
    assert(cursor == &watch_cursor || cursor == &qd.arrow);
    assert(cursor_watch != (cursor == &watch_cursor));
    cursor_watch = cursor == &watch_cursor; ++cursor_calls;
}
void GetFNum(ConstStr255Param name, short *number)
{
    assert(name != NULL && name[0] == 6 && memcmp(name + 1, "Geneva", 6) == 0);
    assert(number != NULL);
    *number = 3;
}
Boolean RealFont(short font, short size)
{
    assert(font == 3 && (size == 9 || size == 12 || size == 18));
    ++font_queries;
    return (available_fonts & (size == 9 ? 1U : size == 12 ? 2U : 4U)) != 0;
}
void TextFont(short font) { assert(font == 0 || font == 3); selected_font = font; }
void TextSize(short size) { assert(size == 9 || size == 12 || size == 18); selected_size = size; }
void GetFontInfo(FontInfo *font)
{
    assert(font != NULL);
    font->ascent = selected_size == 18 ? 15 : selected_size == 12 ? 10 : 8;
    font->descent = selected_size == 18 ? 5 : selected_size == 12 ? 4 : 3;
    font->leading = selected_size == 18 ? 4 : selected_size == 12 ? 3 : 2;
    font->widMax = selected_size == 18 ? 11 : selected_size == 12 ? 8 : 6;
}
void TextMode(short mode) { (void)mode; }
static short test_text_width(short count)
{ FontInfo font; assert(count >= 0); GetFontInfo(&font); return (short)(font.widMax * count); }
short TextWidth(const void *text, short start, short count)
{ assert(text != NULL && start >= 0); ++text_width_calls; return test_text_width(count); }
short CharWidth(short character)
{ assert(character > 0 && character < 256); ++char_width_calls; return test_text_width(1); }
void DrawText(const void *text, short start, short count)
{
    FontInfo font;
    GetFontInfo(&font);
    assert(text != NULL && start >= 0 && count > 0); ++text_calls;
    assert(pen.h >= 0 && pen.v >= font.ascent && pen.v <= 32767);
    assert((long)pen.v + font.descent <= drv_win_height);
    assert(strstr((const char *)text + start, "texture") == NULL);
    assert(test_text_width(count) <= (int)drv_win_width - 16);
    test_toolbox_allocation();
    assert(strlen(osd_text) + (size_t)count + 2 < sizeof(osd_text));
    if (osd_text[0] != 0) strcat(osd_text, " ");
    strncat(osd_text, (const char *)text + start, (size_t)count);
}
void DrawString(ConstStr255Param text) { assert(text != NULL && text[0] > 0); ++text_calls; }
void CopyBits(const BitMap *source, const BitMap *destination, const Rect *a,
                const Rect *b, short mode, RgnHandle mask)
{
    assert(source != NULL && destination != NULL && a != NULL && b != NULL);
    /* portBits of both ports: a basic QuickDraw world has no PixMap, and
     * a dereferenced PixMap handle could move during the copy. */
    assert(world_live != NULL && source == &((GrafPtr)world_live)->portBits);
    assert(destination == &((GrafPtr)window_live)->portBits && world_locked);
    /* ditherCopy is a Color QuickDraw transfer mode. */
    assert(has_color ? (mode == srcCopy || mode == (srcCopy | ditherCopy)) : mode == srcCopy);
    (void)mask;
    assert(source->bounds.right == a->right && source->bounds.bottom == a->bottom);
    assert(current_port == (GrafPtr)&window_port);
    assert(rgb_value(&window_colors.foreground) == 0);
    assert(rgb_value(&window_colors.background) == 0xffffffUL);
    if (in_update) ++update_copies;
    ++copy_calls;
}
long GetCTSeed(void) { color_api(); return 1; }
PaletteHandle NewPalette(short count, CTabHandle table, short usage, short tolerance)
{
    PaletteHandle p;
    int i;
    color_api(); assert(count == 256 && table != NULL && (*table)->ctSize == 255);
    /* Only 8-bit devices take the Earth palette, and never their 2/4-bit
     * relatives' entries (the luminance order fills them with near-blacks). */
    assert(test_window_depth() == 8 && tolerance == 0);
    assert(usage == (pmTolerant | pmInhibitC2 | pmInhibitG2 | pmInhibitC4 | pmInhibitG4));
    for (i = 0; i < 256; ++i) {
        assert((*table)->ctTable[i].value == i);
        assert((*table)->ctTable[i].rgb.red == earth_cmap[i][0] * 257U);
        assert((*table)->ctTable[i].rgb.green == earth_cmap[i][1] * 257U);
        assert((*table)->ctTable[i].rgb.blue == earth_cmap[i][2] * 257U);
    }
    if (fail_palette) return NULL;
    p = (PaletteHandle)NewHandle((Size)sizeof(Palette));
    (*p)->entries = count; ++palette_count; ++palette_calls;
    return p;
}
void SetPalette(WindowPtr w, PaletteHandle p, Boolean updates)
{ color_api(); assert(w == window_live); (void)updates; window_palette = p; }
void NSetPalette(WindowPtr w, PaletteHandle p, short updates)
{ SetPalette(w, p, updates != 0); }
void ActivatePalette(WindowPtr w) { color_api(); assert(w == window_live); ++palette_calls; }
void DisposePalette(PaletteHandle p)
{ color_api(); assert(p != NULL && palette_count > 0); DisposeHandle((Handle)p); --palette_count; if (window_palette == p) window_palette = NULL; }
MenuHandle NewMenu(short id, ConstStr255Param title)
{
    MenuHandle m;
    assert(title != NULL && title[0] > 0);
    assert(id == QD_FILE_MENU || (id == QD_APPLE_MENU && title[0] == 1 && title[1] == 0x14));
    m = (MenuHandle)NewHandle((Size)sizeof(MenuInfo)); (*m)->menuID = id; ++menu_count;
    return m;
}
void AppendMenu(MenuHandle m, ConstStr255Param text) { assert(m != NULL && text != NULL && text[0] > 0); }
void AppendResMenu(MenuHandle m, ResType type)
{ assert(m != NULL && (*m)->menuID == QD_APPLE_MENU && type == 0x44525652UL); ++resource_menu_calls; }
void GetMenuItemText(MenuHandle m, short item, Str255 text)
{
    static const unsigned char name[] = "\012Calculator";
    assert(m != NULL && (*m)->menuID == QD_APPLE_MENU && item > 2 && text != NULL);
    memcpy(text, name, sizeof(name));
}
short OpenDeskAcc(ConstStr255Param name)
{
    assert(name != NULL && name[0] == 10 && memcmp(name + 1, "Calculator", 10) == 0);
    ++desk_calls;
    current_port = &desktop_port; /* A desk accessory leaves its own port. */
    return 1;
}
void InsertMenu(MenuHandle m, short before) { assert(m != NULL); (void)before; }
void DeleteMenu(short id) { (void)id; }
void DisposeMenu(MenuHandle m) { assert(m != NULL && menu_count > 0); DisposeHandle((Handle)m); --menu_count; }
void DrawMenuBar(void) {}
void ClearMenuBar(void) {}
void DrawGrowIcon(WindowPtr w) { assert(w == window_live); }
void ValidRect(const Rect *r)
{
    assert(r != NULL && current_port == (GrafPtr)&window_port && !in_update);
    assert(memcmp(r, &window_port.portRect, sizeof(*r)) == 0);
    update_pending = FALSE; ++valid_calls;
}
/* The desktop below the menu bar, across every active monitor. */
RgnHandle GetGrayRgn(void)
{
    gray_region.rgnBBox = qd.screenBits.bounds;
    if (multiple_devices) gray_region.rgnBBox.right = second_device.gdRect.right;
    gray_region.rgnBBox.top = 20;
    return &gray_pointer;
}
long MenuSelect(Point p) { (void)p; return 0; }
long MenuKey(short key) { return key == 'q' ? ((long)QD_FILE_MENU << 16) | 1L : 0; }
void HiliteMenu(short id) { (void)id; }
/* Everything released has come back, except a GWorld that cannot be made. */
static int test_restored(void)
{
    return qd_optional_released == (qd_world_available && fail_gworld) &&
           (qd_world != NULL) == (qd_world_available && !fail_gworld) &&
           !qd_palette_missing() && !qd_pattern_unavailable;
}
static void test_apple_event(EventRecord *event, AEEventID id)
{
    event->what = kHighLevelEvent;
    event->message = kCoreEventClass;
    event->where.v = (short)(id >> 16);
    event->where.h = (short)(id & 0xffffUL);
}
static unsigned int stage_erases, stage_valids, stage_updates, stage_watch;
static GDHandle stage_device;
Boolean WaitNextEvent(EventMask mask, EventRecord *event, unsigned long sleep, RgnHandle mouse)
{
    assert(mask == everyEvent && event != NULL); (void)mouse;
    if (!contract_started) {
        contract_started = TRUE;
        if (failure_profile) {
            unsigned int before_polygons;
            assert(qd_ready && (qd_world != NULL || !qd_world_available));
            SetPort(&desktop_port);
            if (has_color) SetGDevice(&screen_device_ptr);
            fail_polygon = failure_profile == 1;
            fail_pattern = failure_profile == 2;
            before_polygons = polygon_calls;
            drv_render();
            assert(qd_ready && qd_running);
            assert(alert_calls == 0 && !world_locked && !qd_rendering);
            if (failure_profile == 1) {
                assert(qd_frame_paused && polygon_calls == before_polygons + 2U);
                assert(qd_world == NULL && qd_optional_released);
                assert(current_port == &desktop_port);
                assert(current_device == (has_color ? &screen_device_ptr : NULL));
                osd_text[0] = 0;
                drv_render();
                assert(polygon_calls == before_polygons + 2U);
                assert(strstr(osd_text, "paused") != NULL);
                fail_polygon = FALSE;
                /* The user's retry takes the released buffers back too. */
                qd_keyboard('1');
                assert(!qd_frame_paused && !qd_optional_released);
                assert((qd_world != NULL) == qd_world_available);
                assert(!qd_palette_missing());
                recorded_count = 0; osd_text[0] = 0;
                drv_render();
                assert(recorded_count != 0 && !drv_frame_failed);
                puts("PASS QuickDraw polygon failure: two bounded attempts unwind ports, then pause; a key retries successfully and restores released buffers without an alert");
            } else {
                assert(qd_pattern_unavailable);
                fail_pattern = FALSE;
                puts("PASS QuickDraw pattern failure: nearest-color drawing continues without an alert");
            }
        }
        if (expect_fatal) {
            assert(qd_reserve != NULL && *qd_reserve != NULL);
            qd_busy(TRUE);
            qd_resize(40000, 100); /* QuickDraw coordinates stop at 32767. */
            assert(!"QuickDraw fatal error returned");
        }
        run_contract();
    }
    recorded_count = 0;
    memset(event, 0, sizeof(*event));
    /* The Event Manager reports a window's update region once no other
     * event is pending; the scripted stages wait behind it. */
    if (update_pending && window_live != NULL) {
        event->what = updateEvt; event->message = (unsigned long)window_live;
        return TRUE;
    }
    switch (next_event_stage++) {
        case 0:
            loop_yaw = drv_rot_y;
            event->what = keyDown; event->message = 'a';
            return TRUE;
        case 1:
            assert(drv_auto_enabled && fabs(drv_rot_y - loop_yaw) > 0.1);
            ticks = 0xffffffffUL; qd_auto_tick = ticks; loop_yaw = drv_rot_y;
            return FALSE;
        case 2:
            assert(fabs((drv_rot_y - loop_yaw) - (10.0 / 60.0)) < 0.001);
            ticks = 0xfffffffeUL; qd_auto_tick = 0; loop_yaw = drv_rot_y;
            return FALSE;
        case 3:
            assert(drv_rot_y >= 0.0f && drv_rot_y < 360.0f);
            assert(fabs(drv_rot_y - fmod((double)loop_yaw +
                       (double)DRV_ROTATE * (4294967295.0 / 60.0), 360.0)) < 0.001);
            drv_rot_y = (float)HUGE_VAL;
            ticks = qd_auto_tick = 0;
            return FALSE;
        case 4:
            assert(drv_rot_y == 0.0f);
            event->what = keyDown; event->message = 'a'; loop_yaw = drv_rot_y;
            return TRUE;
        case 5:
            assert(!drv_auto_enabled && drv_rot_y == loop_yaw && sleep == QD_IDLE_SLEEP);
            find_part = inContent; mouse_down = TRUE;
            mouse_position.h = 300; mouse_position.v = 200;
            event->what = mouseDown; event->where.h = 280; event->where.v = 180;
            return TRUE;
        case 6:
            assert(drv_lmouse_pressed && !drv_auto_enabled && sleep == 1UL);
            loop_yaw = drv_rot_y;
            mouse_position.h += 10; mouse_position.v += 5;
            return FALSE;
        case 7:
            assert(drv_lmouse_pressed && sleep == 1UL && drv_rot_y != loop_yaw);
            mouse_down = FALSE; event->what = mouseUp;
            return TRUE;
        case 8:
            assert(!drv_lmouse_pressed && sleep == QD_IDLE_SLEEP);
            assert(!qd_rendering && !world_locked);
            stage_watch = watch_allocations;
            event->what = keyDown; event->message = '1';
            return TRUE;
        case 9:
            assert(globe_longitude == 32 && !qd_rendering && !world_locked);
            /* The mesh rebuild ran under the watch cursor, then the arrow. */
            assert(watch_allocations > stage_watch && !cursor_watch);
            event->what = keyDown; event->message = 'o';
            return TRUE;
        case 10:
            /* The key's complete frame validated the window. */
            assert(!update_pending && !qd_dirty);
            loop_text_calls = text_calls;
            stage_erases = erase_calls; stage_updates = update_copies;
            osd_text[0] = 0;
            event->what = updateEvt; event->message = (unsigned long)qd_window;
            return TRUE;
        case 11:
            assert(!qd_rendering && !world_locked);
            /* An update with no scene change copies the held frame again;
             * only a window without one draws the scene. */
            if (qd_world_shown) {
                assert(erase_calls == stage_erases && text_calls == loop_text_calls);
                assert(update_copies == stage_updates + 1U);
            } else {
                assert(erase_calls == stage_erases + 1U && update_copies == stage_updates);
                if (drv_osd_enabled) assert(text_calls > loop_text_calls);
            }
            grow_width = 640; grow_height = 480;
            find_part = inGrow;
            stage_erases = erase_calls; stage_valids = valid_calls;
            event->what = mouseDown;
            return TRUE;
        case 12:
            assert(drv_win_width == 640U && drv_win_height == 480U);
            assert(!qd_rendering && !world_locked);
            /* One frame for the grow: it validated the region SizeWindow
             * invalidated, so no update event draws it a second time. */
            assert(erase_calls == stage_erases + 1U && valid_calls == stage_valids + 1U);
            puts("PASS QuickDraw synchronous events: resolution, OSD, update and resize dispatch between complete drawing passes");
            drv_auto_enabled = TRUE;
            stage_erases = erase_calls;
            event->what = osEvt; event->message = (unsigned long)suspendResumeMessage << 24;
            return TRUE;
        case 13:
            /* A background copy neither animates nor polls every tick. */
            assert(!qd_active && drv_auto_enabled && sleep == QD_BACKGROUND_SLEEP);
            assert(erase_calls == stage_erases);
            event->what = osEvt;
            event->message = ((unsigned long)suspendResumeMessage << 24) | resumeFlag;
            return TRUE;
        case 14:
            assert(qd_active && sleep == 1UL && erase_calls > stage_erases && !update_pending);
            drv_auto_enabled = FALSE;
            /* Drag onto the other monitor, or within the only one. */
            drag_dx = multiple_devices ? -1024 : 24;
            stage_device = qd_device;
            stage_erases = erase_calls; stage_valids = valid_calls; stage_updates = update_copies;
            find_part = inDrag; event->what = mouseDown;
            return TRUE;
        case 15:
            assert(!qd_rendering && !world_locked && !update_pending);
            if (multiple_devices) {
                /* The window now lies on the 4-bit display: no Earth
                 * palette, a 4-bit GWorld and one new frame. */
                assert(qd_device == &screen_device_ptr && qd_depth == 4);
                assert(qd_palette == NULL && qd_world != NULL && world_pixmap.pixelSize == 4);
                assert(erase_calls == stage_erases + 1U && valid_calls == stage_valids + 1U);
                assert(update_copies == stage_updates);
            } else {
                assert(qd_device == stage_device);
                if (qd_world_shown) {
                    /* Moved pixels stay valid; the exposure copies the frame. */
                    assert(erase_calls == stage_erases && update_copies == stage_updates + 1U);
                } else assert(erase_calls == stage_erases + 1U);
            }
            drag_dx = (short)-drag_dx;
            find_part = inDrag; event->what = mouseDown;
            return TRUE;
        case 16:
            assert(qd_device == stage_device && qd_depth == display_depth);
            assert((qd_palette != NULL) == (has_color && display_depth == 8));
            event->what = activateEvt; event->message = (unsigned long)qd_window;
            return TRUE;
        case 17:
            assert(!qd_active && sleep == QD_BACKGROUND_SLEEP);
            /* Coming forward exposes the window and activates it. */
            update_pending = TRUE;
            stage_erases = erase_calls; stage_valids = valid_calls;
            event->what = activateEvt; event->message = (unsigned long)qd_window;
            event->modifiers = activeFlag;
            return TRUE;
        case 18:
            assert(qd_active && !update_pending);
            assert(erase_calls == stage_erases + 1U && valid_calls == stage_valids + 1U);
            puts("PASS QuickDraw redraw policy: grow, drag and activation draw one frame and validate it; held-frame updates only copy; background copies sleep; drags reach and follow every monitor");
            test_apple_event(event, kAEOpenApplication);
            ae_result = 1;
            return TRUE;
        case 19:
            assert(ae_result == (apple_events ? noErr : 1));
            test_apple_event(event, kAEOpenDocuments);
            return TRUE;
        case 20:
            assert(ae_result == (apple_events ? errAEEventNotHandled : 1));
            ae_missed = TRUE;
            test_apple_event(event, kAEQuitApplication);
            return TRUE;
        case 21:
            /* An unread required parameter rejects the quit. */
            assert(ae_result == (apple_events ? errAEParamMissed : 1) && qd_running);
            ae_missed = FALSE;
            if (apple_events)
                puts("PASS QuickDraw Apple events: open application succeeds, documents are declined, a quit with unread parameters is rejected");
            else
                puts("PASS QuickDraw without Apple events: high-level events are ignored and the menus remain the way to quit");
            find_part = inGoAway;
            if (quit_mode == 1) { event->what = keyDown; event->message = 'q'; event->modifiers = cmdKey; }
            else if (quit_mode == 2) test_apple_event(event, kAEQuitApplication);
            else { event->what = mouseDown; event->where.h = 1; event->where.v = 1; }
            return TRUE;
        default: assert(!"QuickDraw driver did not close its window"); return FALSE;
    }
}
short FindWindow(Point p, WindowPtr *window) { (void)p; *window = window_live; return find_part; }
void DragWindow(WindowPtr w, Point p, const Rect *bounds)
{
    RgnHandle gray;
    assert(w == window_live && bounds != NULL); (void)p;
    gray = GetGrayRgn();
    assert(memcmp(bounds, &(**gray).rgnBBox, sizeof(*bounds)) == 0);
    window_origin.h = (short)(window_origin.h + drag_dx);
    update_pending = TRUE; /* Pixels moved off another window or screen. */
}
long GrowWindow(WindowPtr w, Point p, const Rect *bounds) { assert(w == window_live && bounds != NULL); (void)p; return ((long)grow_height << 16) | grow_width; }
void SizeWindow(WindowPtr w, short width, short height, Boolean update)
{
    assert(w == window_live && width > 0 && height > 0);
    if (update) update_pending = TRUE;
    window_port.portRect.right = width; window_port.portRect.bottom = height;
}
Boolean TrackGoAway(WindowPtr w, Point p) { assert(w == window_live); (void)p; return TRUE; }
void BeginUpdate(WindowPtr w) { assert(w == window_live && !in_update); in_update = TRUE; update_pending = FALSE; }
void EndUpdate(WindowPtr w) { assert(w == window_live && in_update); in_update = FALSE; }
void SystemClick(const EventRecord *event, WindowPtr w) { assert(event != NULL); (void)w; }
void SystemTask(void) {}
Boolean Button(void) { return mouse_down != 0; }
void GetMouse(Point *p) { assert(p != NULL); *p = mouse_position; }
void GlobalToLocal(Point *p)
{
    assert(p != NULL);
    if (current_port == (GrafPtr)&window_port) { p->h -= window_origin.h; p->v -= window_origin.v; }
}
void LocalToGlobal(Point *p)
{
    assert(p != NULL);
    if (current_port == (GrafPtr)&window_port) { p->h += window_origin.h; p->v += window_origin.v; }
}
void ParamText(ConstStr255Param a, ConstStr255Param b, ConstStr255Param c, ConstStr255Param d)
{
    assert(a != NULL && b != NULL && c != NULL && d != NULL && b[0] == 0);
    memcpy(param_text, a, (size_t)a[0] + 1U);
}
short NoteAlert(short id, void *filter)
{
    assert(id == QD_ABOUT_ALERT && filter == NULL && qd_ready && !qd_rendering);
    assert(param_text[0] > 9 && memcmp(param_text + 1, "testglobe ", 10) == 0);
    ++about_calls;
    return 1;
}
short StopAlert(short id, void *filter)
{
    unsigned int previous_texts, previous_handles;
    EventRecord event;
    assert(id == QD_ERROR_ALERT && filter == NULL); ++alert_calls;
    if (!expect_fatal)
        fprintf(stderr, "Unexpected Toolbox error: %.*s\n", (int)param_text[0], param_text + 1);
    assert(!qd_ready && !qd_running && !qd_active && !qd_rendering);
    assert(!world_locked && qd_locked_pixels == NULL);
    /* The alert gets the memory reserve, and the watch cursor is gone. */
    assert(qd_reserve == NULL && !cursor_watch);
    assert(!pen_hidden);
    previous_handles = handle_count;
    previous_texts = text_calls;
    memset(&event, 0, sizeof(event));
    event.what = updateEvt; event.message = (long)qd_window;
    qd_event(&event); drv_render(); drv_draw_osd();
    assert(text_calls == previous_texts && handle_count == previous_handles);
    if (expect_fatal)
        puts("PASS QuickDraw fatal alert: input and drawing stop, ports restore, the memory reserve is freed for the alert and the watch cursor is cleared");
    return 1;
}
OSErr AEInstallEventHandler(AEEventClass event_class, AEEventID id, AEEventHandlerUPP handler,
                            long refcon, Boolean system_handler)
{
    assert(apple_events && handler != NULL && !system_handler && event_class == kCoreEventClass);
    assert(ae_handler_count < 8U);
    ae_handlers[ae_handler_count].event_class = event_class;
    ae_handlers[ae_handler_count].id = id;
    ae_handlers[ae_handler_count].handler = handler;
    ae_handlers[ae_handler_count].refcon = refcon;
    ++ae_handler_count;
    return noErr;
}
/* A high-level event carries its class in message and its ID in where. */
OSErr AEProcessAppleEvent(const EventRecord *event)
{
    AEEventID id;
    AppleEvent apple_event, reply;
    unsigned int i;
    assert(apple_events && event != NULL && event->what == kHighLevelEvent);
    id = ((unsigned long)(unsigned short)event->where.v << 16) | (unsigned short)event->where.h;
    memset(&apple_event, 0, sizeof(apple_event)); memset(&reply, 0, sizeof(reply));
    ae_result = errAEEventNotHandled;
    for (i = 0; i < ae_handler_count; ++i)
        if (ae_handlers[i].event_class == event->message && ae_handlers[i].id == id)
            ae_result = ae_handlers[i].handler(&apple_event, &reply, ae_handlers[i].refcon);
    return ae_result;
}
OSErr AEGetAttributePtr(const AppleEvent *event, AEKeyword key, DescType type, DescType *actual,
                        void *data, Size maximum, Size *size)
{
    assert(event != NULL && key == keyMissedKeywordAttr && type == typeWildCard);
    assert(actual != NULL && data == NULL && maximum == 0 && size != NULL);
    if (!ae_missed) return errAEDescNotFound;
    *actual = 0x6B657930UL; *size = 0;
    return noErr;
}
void SysBeep(short duration) { (void)duration; }
void ExitToShell(void) { assert(!"Unexpected ExitToShell"); }

typedef struct { double x, y; } ReferencePoint;
typedef struct {
    ReferencePoint point[4];
    int count;
    unsigned long color;
    double luminance;
    double area;
    int seen;
} ReferenceQuad;
static ReferenceQuad reference[5000];
static unsigned int reference_count;

static ReferencePoint project_reference(const Vertex *vertex)
{
    ReferencePoint projected;
    double yaw, pitch, x, y, z, rotated_z, focal;
    yaw = (double)drv_rot_y * PI / 180.0;
    pitch = (double)drv_rot_x * PI / 180.0;
    /* Evaluate the documented OpenGL rotations directly in double precision,
     * independently of either production driver arithmetic path. */
    x = cos(yaw) * vertex->y + sin(yaw) * vertex->x;
    rotated_z = -sin(yaw) * vertex->y + cos(yaw) * vertex->x;
    y = cos(pitch) * vertex->z - sin(pitch) * rotated_z;
    z = sin(pitch) * vertex->z + cos(pitch) * rotated_z - 3000.0;
    focal = (double)drv_win_height / (2.0 * tan(PI / 8.0));
    projected.x = (double)drv_win_width / 2.0 + focal * x / -z;
    projected.y = (double)drv_win_height / 2.0 - focal * y / -z;
    return projected;
}

/* Project all four corners independently and compact the exact coincident
 * poles. The reference never calls production transforms or clipping. */
static void add_reference_quad(const Vertex *a, const Vertex *b,
                                const Vertex *c, const Vertex *d,
                                unsigned char color)
{
    ReferenceQuad *quad;
    const Vertex *corners[4];
    ReferencePoint point;
    int i, next;
    assert(reference_count < 5000);
    quad = reference + reference_count++;
    corners[0] = a; corners[1] = b; corners[2] = c; corners[3] = d;
    quad->count = 0;
    for (i = 0; i < 4; ++i) {
        point = project_reference(corners[i]);
        if (quad->count != 0 && point.x == quad->point[quad->count - 1].x &&
            point.y == quad->point[quad->count - 1].y) continue;
        quad->point[quad->count++] = point;
    }
    if (quad->count > 1 && quad->point[0].x == quad->point[quad->count - 1].x &&
        quad->point[0].y == quad->point[quad->count - 1].y) --quad->count;
    quad->area = 0;
    for (i = 0; i < quad->count; ++i) {
        next = (i + 1) % quad->count;
        quad->area += quad->point[i].x * quad->point[next].y -
                      quad->point[i].y * quad->point[next].x;
    }
    quad->color = ((unsigned long)earth_cmap[color][0] << 16) |
                  ((unsigned long)earth_cmap[color][1] << 8) | earth_cmap[color][2];
    quad->luminance = (0.299 * earth_cmap[color][0] +
                       0.587 * earth_cmap[color][1] +
                       0.114 * earth_cmap[color][2]) / 255.0;
    quad->seen = FALSE;
}

static void build_reference(void)
{
    int latitude, longitude;
    unsigned long top, bottom, face;
    unsigned char *data;
    data = test_earth_data(globe_longitude);
    reference_count = 0;
    for (latitude = 0; latitude < globe_latitude; ++latitude) {
        for (longitude = 0; longitude < globe_longitude; ++longitude) {
            top = (unsigned long)latitude * (globe_longitude + 1UL) + longitude;
            bottom = top + globe_longitude + 1UL;
            face = (unsigned long)latitude * globe_longitude + longitude;
            add_reference_quad(globe_verts + top, globe_verts + bottom,
                                globe_verts + bottom + 1UL,
                                globe_verts + top + 1UL, data[face]);
        }
    }
    assert(reference_count == globe_quads_num);
}

static double reference_distance(const RecordedPolygon *actual,
                                  const ReferenceQuad *expected)
{
    int point, corner;
    double distance, total, best, dx, dy;
    if (actual->count < 3 || actual->count > 4 || expected->count < 3)
        return 1e30;
    if (has_color && display_depth > 1) {
        if (actual->color != expected->color) return 1e30;
    } else if (fabs((double)actual->pattern_black -
                     64.0 * (1.0 - expected->luminance)) > 2.0) return 1e30;
    /* Subpixel adjacent corners can collapse after integer projection.
     * Require every reference corner and every actual vertex to match;
     * no distinct corner more than three pixels away may disappear. */
    total = 0;
    for (point = 0; point < actual->count; ++point) {
        best = 1e30;
        for (corner = 0; corner < expected->count; ++corner) {
            dx = actual->points[point].h - expected->point[corner].x;
            dy = actual->points[point].v - expected->point[corner].y;
            distance = dx * dx + dy * dy;
            if (fabs(dx) <= 3.0 && fabs(dy) <= 3.0 && distance < best)
                best = distance;
        }
        if (best == 1e30) return best;
        total += best;
    }
    for (corner = 0; corner < expected->count; ++corner) {
        best = 1e30;
        for (point = 0; point < actual->count; ++point) {
            dx = actual->points[point].h - expected->point[corner].x;
            dy = actual->points[point].v - expected->point[corner].y;
            distance = dx * dx + dy * dy;
            if (fabs(dx) <= 3.0 && fabs(dy) <= 3.0 && distance < best)
                best = distance;
        }
        if (best == 1e30) return best;
        total += best;
    }
    return total;
}

static void check_projection(void)
{
    unsigned int polygon, quad, found, matched;
    long area;
    double distance, best, dx, dy, longest, length;
    int point, next;
    matched = 0;
    for (polygon = 0; polygon < recorded_count; ++polygon) {
        assert(recorded[polygon].count >= 3 && recorded[polygon].count <= 4);
        area = 0;
        for (point = 0; point < recorded[polygon].count; ++point) {
            next = (point + 1) % recorded[polygon].count;
            area += (long)recorded[polygon].points[point].h * recorded[polygon].points[next].v -
                    (long)recorded[polygon].points[point].v * recorded[polygon].points[next].h;
        }
        if (area >= 0) {
            fprintf(stderr, "Bad quad area %ld at pose %.2f/%.2f:", area, (double)drv_rot_x, (double)drv_rot_y);
            for (point = 0; point < recorded[polygon].count; ++point)
                fprintf(stderr, " (%d,%d)", recorded[polygon].points[point].h, recorded[polygon].points[point].v);
            fputc('\n', stderr);
        }
        assert(area < 0);
        found = reference_count;
        best = 1e30;
        for (quad = 0; quad < reference_count; ++quad) {
            distance = reference_distance(recorded + polygon, reference + quad);
            if (distance < best) {
                best = distance;
                found = quad;
            }
        }
        if (found == reference_count) {
            fprintf(stderr, "No reference for polygon %u/%u, color %lu, pose %.2f/%.2f, resolution %d\n",
                    polygon, recorded_count, recorded[polygon].color,
                    (double)drv_rot_x, (double)drv_rot_y, globe_longitude);
        }
        assert(found < reference_count);
        reference[found].seen = TRUE;
        ++matched;
    }
    /* At finite camera distance the visible cap is smaller than half the
     * surface; latitude sampling also changes its fraction of all facets. */
    assert(matched > reference_count / 6U);
    /* Silhouette facets can round to zero area on either arithmetic path.
     * Require every front-facing facet whose altitude exceeds four pixels,
     * independently of the longest edge's length. */
    for (quad = 0; quad < reference_count; ++quad) {
        longest = 0;
        for (point = 0; point < reference[quad].count; ++point) {
            next = (point + 1) % reference[quad].count;
            dx = reference[quad].point[point].x - reference[quad].point[next].x;
            dy = reference[quad].point[point].y - reference[quad].point[next].y;
            length = sqrt(dx * dx + dy * dy);
            if (length > longest) longest = length;
        }
        if (reference[quad].area < -4.0 * longest && !reference[quad].seen) {
            /* Adjacent subpixel facets can have identical rounded tuples;
             * coverage matters more than a greedy one-to-one assignment. */
            for (polygon = 0; polygon < recorded_count; ++polygon) {
                if (reference_distance(recorded + polygon, reference + quad) < 1e30) {
                    reference[quad].seen = TRUE;
                    break;
                }
            }
            assert(reference[quad].seen);
        }
    }
}


static void camera_triangle(double z0, double z1, double z2)
{
    Vertex source[3];
    float zero = 0.0f;
    source[0].x = (float)(z0 + 3000.0); source[0].y = -100.0f; source[0].z = -100.0f;
    source[1].x = (float)(z1 + 3000.0); source[1].y = 100.0f; source[1].z = -100.0f;
    source[2].x = (float)(z2 + 3000.0); source[2].y = 0.0f; source[2].z = 100.0f;
    if (qd_has_fpu) {
        assert(qd_fpu_prepare(&zero, &zero));
        assert(qd_fpu_transform_reuse(3U, source, 0U));
    } else {
        qd_fixed_points[0].x = -1600L; qd_fixed_points[0].y = -1600L;
        qd_fixed_points[0].z = (long)(z0 * 16.0);
        qd_fixed_points[1].x = 1600L; qd_fixed_points[1].y = -1600L;
        qd_fixed_points[1].z = (long)(z1 * 16.0);
        qd_fixed_points[2].x = 0; qd_fixed_points[2].y = 1600L;
        qd_fixed_points[2].z = (long)(z2 * 16.0);
    }
}
static void camera_quad(double z0, double z1, double z2, double z3)
{
    Vertex source[4];
    double depths[4];
    int i;
    float zero = 0.0f;
    depths[0] = z0; depths[1] = z1; depths[2] = z2; depths[3] = z3;
    for (i = 0; i < 4; ++i) {
        source[i].x = (float)(depths[i] + 3000.0);
        source[i].y = i == 0 || i == 3 ? -1000.0f : 1000.0f;
        source[i].z = i < 2 ? -1000.0f : 1000.0f;
    }
    if (qd_has_fpu) {
        assert(qd_fpu_prepare(&zero, &zero));
        assert(qd_fpu_transform_reuse(4U, source, 0U));
    } else {
        for (i = 0; i < 4; ++i) {
            qd_fixed_points[i].x = (long)(source[i].y * 16.0f);
            qd_fixed_points[i].y = (long)(source[i].z * 16.0f);
            qd_fixed_points[i].z = (long)(depths[i] * 16.0);
        }
    }
}
static void check_clipping(void)
{
    Point polygon[10];
    int count, i;
    unsigned int indices[4];
    indices[0] = 0; indices[1] = 1; indices[2] = 2; indices[3] = 3;
    camera_triangle(-9000.0, -9000.0, -11000.0);
    assert(qd_prepare_polygon(indices, 3U, 3U, polygon) == 4);
    camera_triangle(-11000.0, -11000.0, -11000.0);
    assert(qd_prepare_polygon(indices, 3U, 3U, polygon) == 0);
    camera_triangle(0.0, 0.0, 0.0);
    assert(qd_prepare_polygon(indices, 3U, 3U, polygon) == 0);
    camera_triangle(-2.0, -2.0, -0.5);
    count = qd_prepare_polygon(indices, 3U, 3U, polygon);
    assert(count >= 3 && count <= 10);
    for (i = 0; i < count; ++i) {
        assert(polygon[i].h >= 0 && polygon[i].h <= (int)drv_win_width);
        assert(polygon[i].v >= 0 && polygon[i].v <= (int)drv_win_height);
    }
    camera_quad(-9000.0, -9000.0, -11000.0, -9000.0);
    assert(qd_prepare_polygon(indices, 4U, 4U, polygon) == 5);
    camera_quad(-11000.0, -11000.0, -11000.0, -11000.0);
    assert(qd_prepare_polygon(indices, 4U, 4U, polygon) == 0);
    camera_quad(0.0, 0.0, 0.0, 0.0);
    assert(qd_prepare_polygon(indices, 4U, 4U, polygon) == 0);
    camera_quad(-2.0, -2.0, -0.5, -2.0);
    count = qd_prepare_polygon(indices, 4U, 4U, polygon);
    assert(count >= 3 && count <= 10);
    for (i = 0; i < count; ++i) {
        assert(polygon[i].h >= 0 && polygon[i].h <= (int)drv_win_width);
        assert(polygon[i].v >= 0 && polygon[i].v <= (int)drv_win_height);
    }
    test_soft3d_shared_pipeline();
}

static void check_fpu_angles(void)
{
    Vertex vertices[3];
    Qd_screen_point projected[QD_FPU_MAX_POLY];
    float zero, last, bad[7];
    unsigned int i, corners[3];
    if (!qd_has_fpu) return;
    corners[0] = 0U; corners[1] = 1U; corners[2] = 2U;
    zero = 0.0f; last = 359.999f;
    for (i = 0; i < 3U; ++i) vertices[i].x = 0;
    vertices[0].y = -100; vertices[0].z = -100;
    vertices[1].y = 100; vertices[1].z = -100;
    vertices[2].y = 0; vertices[2].z = 100;
    bad[0] = -1.0f; bad[1] = 360.0f;
    bad[2] = FLT_MAX; bad[3] = -FLT_MAX;
    /* These host-only tests explicitly exercise IEEE nonfinite inputs. */
    bad[4] = (float)strtod("inf", NULL);
    bad[5] = -bad[4]; bad[6] = (float)strtod("nan", NULL);
    assert(bad[4] > FLT_MAX && bad[6] != bad[6]);
    /* The driver's own entry points: transform with no reuse, then project
     * without a cache. A failed prepare leaves nothing to project. */
    for (i = 0; i < 7U; ++i) {
        assert(qd_fpu_prepare(&zero, &zero));
        assert(qd_fpu_transform_reuse(3U, vertices, 0U));
        assert(qd_fpu_project_cached(corners, 3U, 640U, 480U, NULL, projected) == 3);
        assert(!qd_fpu_prepare(bad + i, &zero));
        assert(!qd_fpu_transform_reuse(3U, vertices, 0U));
        assert(qd_fpu_project_cached(corners, 3U, 640U, 480U, NULL, projected) == 0);
        assert(!qd_fpu_prepare(&zero, bad + i));
    }
    assert(!qd_fpu_prepare(NULL, &zero));
    assert(!qd_fpu_prepare(&zero, NULL));
    assert(qd_fpu_prepare(&last, &last));
    assert(qd_fpu_transform_reuse(3U, vertices, 0U));
    assert(qd_fpu_project_cached(corners, 3U, 640U, 480U, NULL, projected) == 3);
    puts("PASS QuickDraw FPU angles: normalized endpoints accepted, huge/nonfinite/null inputs rejected without loops or reuse of a stale transform");
}

/* This checks the application's native requests, not QuickDraw's palette
 * conversion or physical dither output. Native patterns must receive the
 * original RGB rather than an application-selected palette approximation. */
static void check_color_submission(void)
{
    PolyHandle polygon;
    Point triangle[3];
    GrafPtr saved_port;
    GDHandle saved_device;
    Test_port_colors saved_colors;
    RGBColor *submitted;
    unsigned int color, fills, patterns, luminance;
    unsigned char saved_earth[3];
    ColorTable changed_table, *changed_table_ptr;
    CTabHandle old_table;
    GetPort(&saved_port);
    saved_device = has_color ? GetGDevice() : NULL;
    SetPort((GrafPtr)qd_window);
    if (has_color) SetGDevice(qd_device);
    saved_colors = window_colors;
    triangle[0].h = triangle[0].v = 10;
    triangle[1].h = 50; triangle[1].v = 10;
    triangle[2].h = 10; triangle[2].v = 50;
    polygon = qd_make_polygon(triangle, 3); assert(polygon != NULL);
    assert(qd_prepare_native());
    qd_pattern_color = -1;
    for (color = 0; color < 256U; ++color)
    {
        recorded_count = 0;
        fills = color_fill_calls;
        patterns = solid_fill_calls;
        qd_flat_color(polygon, (unsigned char)color);
        assert(recorded_count == 1U);
        if (qd_depth == 1)
        {
            luminance = (77U * (unsigned int)earth_cmap[color][0] +
                         150U * (unsigned int)earth_cmap[color][1] +
                         29U * (unsigned int)earth_cmap[color][2] + 128U) / 256U;
            assert(recorded[0].pattern_black == 64U - (luminance + 2U) / 4U);
            assert(color_fill_calls == fills && solid_fill_calls == patterns);
            continue;
        }
        if (qd_depth <= 8 && !qd_pattern_unavailable)
        {
            assert(color_fill_calls == fills + 1U && solid_fill_calls == patterns);
            submitted = &last_pattern_rgb;
        }
        else
        {
            assert(color_fill_calls == fills && solid_fill_calls == patterns + 1U);
            submitted = &window_colors.foreground;
        }
        assert(submitted->red == earth_cmap[color][0] * 257U);
        assert(submitted->green == earth_cmap[color][1] * 257U);
        assert(submitted->blue == earth_cmap[color][2] * 257U);
    }
    if (qd_depth > 1 && qd_depth <= 8 && !qd_pattern_unavailable)
    {
        /* A changed device palette still reaches FillCPoly with the same
         * RGB pattern. The native Toolbox, not this recorder, checks the
         * table seed and reselects its device colors on that call. */
        old_table = (**(**qd_device).gdPMap).pmTable;
        memset(&changed_table, 0, sizeof(changed_table));
        changed_table_ptr = &changed_table;
        (**(**qd_device).gdPMap).pmTable = &changed_table_ptr;
        patterns = rgb_pattern_calls; fills = color_fill_calls;
        qd_flat_color(polygon, 255);
        ++changed_table.ctSeed;
        qd_flat_color(polygon, 255);
        assert(rgb_pattern_calls == patterns && color_fill_calls == fills + 2U);
        (**(**qd_device).gdPMap).pmTable = old_table;
    }
    if (qd_depth == 1)
    {
        memcpy(saved_earth, earth_cmap[0], sizeof(saved_earth));
        for (color = 0; color < 256U; ++color)
        {
            memset(earth_cmap[0], (int)color, 3);
            recorded_count = 0;
            qd_flat_color(polygon, 0);
            assert(recorded[0].pattern_black == 64U - (color + 2U) / 4U);
        }
        memcpy(earth_cmap[0], saved_earth, sizeof(saved_earth));
    }
    window_colors = saved_colors;
    SetPort(saved_port);
    if (has_color) SetGDevice(saved_device);
    recorded_count = 0;
    puts("PASS QuickDraw color submission: all Earth RGB values reach native pattern/solid APIs unchanged; cached patterns reach changed palettes; monochrome retains 64-step luminance");
}

static void check_reusable_polygon(void)
{
    Point points[QD_MAX_POLY];
    Point *stored;
    PolyHandle handle;
    unsigned int allocations;
    int i;
    for (i = 0; i < QD_MAX_POLY; ++i)
    {
        points[i].h = (short)(10 + i * 3);
        points[i].v = (short)(40 - i * 2);
    }
    handle = qd_make_polygon(points, QD_MAX_POLY);
    assert(handle != NULL && GetHandleSize((Handle)handle) == (Size)QD_POLYGON_BYTES);
    assert((**handle).polySize == (short)QD_POLYGON_BYTES);
    assert((**handle).polyBBox.left == 10 && (**handle).polyBBox.right == 37);
    assert((**handle).polyBBox.top == 22 && (**handle).polyBBox.bottom == 40);
    stored = (Point *)((char *)*handle + offsetof(Polygon, polyPoints));
    assert(memcmp(stored, points, sizeof(points)) == 0);
    assert(memcmp(stored + QD_MAX_POLY, points, sizeof(Point)) == 0);
    allocations = polygon_calls;
    for (i = 0; i < 100; ++i) assert(qd_make_polygon(points, 3) == handle);
    assert(polygon_calls == allocations && !pen_hidden);
    assert((**handle).polySize == (short)(offsetof(Polygon, polyPoints) + 4U * sizeof(Point)));
    assert(qd_make_polygon(NULL, 3) == NULL);
    assert(qd_make_polygon(points, 2) == NULL);
    assert(qd_make_polygon(points, QD_MAX_POLY + 1) == NULL);
    assert(polygon_calls == allocations);
    puts("PASS QuickDraw reusable polygon: checked bounded native record, exact bounds and closure, no per-facet allocation or pen recording, invalid counts rejected");
}

static void check_buffer_recovery(void)
{
    Test_port_colors saved_colors;
    GrafPtr saved_port;
    GDHandle saved_device;
    unsigned int previous_copies;
    int saved_osd;
    if (!qd_world_available || fail_gworld || fail_lock) return;
    saved_port = current_port; saved_device = current_device;
    saved_osd = drv_osd_enabled;
    fail_gworld = TRUE;
    qd_resize((int)drv_win_width, (int)drv_win_height);
    assert(qd_world == NULL);
    drv_osd_enabled = TRUE;
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    saved_colors = window_colors;
    if (qd_depth > 1) {
        assert(rgb_value(&saved_colors.foreground) == 0xffffffUL);
        assert(rgb_value(&saved_colors.background) == 0);
    }
    fail_gworld = FALSE;
    qd_resize((int)drv_win_width, (int)drv_win_height);
    assert(qd_world != NULL && (has_color ? world_device != qd_device : world_device == NULL));
    previous_copies = copy_calls;
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    assert(copy_calls == previous_copies + 1U);
    assert(memcmp(&window_colors, &saved_colors, sizeof(saved_colors)) == 0);
    assert(current_port == saved_port && current_device == saved_device && !world_locked);
    drv_osd_enabled = saved_osd;
    puts("PASS QuickDraw GWorld recovery: direct rendering followed by buffering copies with black/white destination colors, restores port colors and uses its own offscreen device");
}

static void event_key(unsigned char key, short modifiers)
{
    EventRecord event;
    memset(&event, 0, sizeof(event));
    event.what = keyDown; event.message = key; event.modifiers = modifiers;
    qd_event(&event);
}

/* Shared indexed quads use intentionally reversed packet vertex storage.
 * This catches drivers that assume implicit strip order instead of honoring
 * all four indices, colors and bounded packet seams. */
static void check_quad_batches(void)
{
    static Vertex vertices[1026], packet[QD_MAX_VERTICES];
    static unsigned char colors[512];
    unsigned int indices[DRV_QUAD_BATCH_MAX * 4U];
    unsigned int i, j, first, count, vertex_count, local, tests;
    unsigned int old_width, old_height, sizes[5];
    Vertex cap[4];
    int old_failure;
    GrafPtr saved_port;
    GDHandle saved_device;
    sizes[0] = 1; sizes[1] = 2; sizes[2] = 63;
    sizes[3] = 64; sizes[4] = 512;
    old_width = drv_win_width; old_height = drv_win_height;
    old_failure = fail_gworld; fail_gworld = TRUE;
    SizeWindow(qd_window, 32767, 32767, TRUE); qd_resize(32767, 32767);
    drv_rot_x = drv_rot_y = 0;
    qd_set_transform();
    for (i = 0; i < 1026; ++i) {
        vertices[i].x = 0;
        vertices[i].y = (float)((int)(i / 2U) * 3 - 768);
        vertices[i].z = (i & 1U) ? -10.0f : 10.0f;
    }
    for (i = 0; i < 512; ++i) colors[i] = (unsigned char)((i * 37U) & 255U);
    GetPort(&saved_port);
    saved_device = has_color ? GetGDevice() : NULL;
    SetPort((GrafPtr)qd_window);
    if (has_color) SetGDevice(qd_device);
    for (tests = 0; tests < 5; ++tests) {
        reference_count = recorded_count = 0;
        globe_quads_num_vis = 0;
        for (i = 0; i < sizes[tests]; ++i)
            add_reference_quad(vertices + 2U * i, vertices + 2U * i + 1U,
                                vertices + 2U * i + 3U, vertices + 2U * i + 2U,
                                colors[i]);
        for (first = 0; first < sizes[tests]; first += count) {
            count = sizes[tests] - first;
            if (count > DRV_QUAD_BATCH_MAX) count = DRV_QUAD_BATCH_MAX;
            vertex_count = 2U * (count + 1U);
            for (i = 0; i < vertex_count; ++i)
                packet[vertex_count - 1U - i] = vertices[first * 2U + i];
            for (i = 0; i < count; ++i) {
                local = 2U * i;
                indices[4U * i] = vertex_count - 1U - local;
                indices[4U * i + 1U] = vertex_count - 2U - local;
                indices[4U * i + 2U] = vertex_count - 4U - local;
                indices[4U * i + 3U] = vertex_count - 3U - local;
            }
            drv_draw_quadbatch_flat_color(vertex_count, packet, count,
                                           indices, colors + first);
        }
        assert(recorded_count == sizes[tests]);
        assert(recorded_count == globe_quads_num_vis);
        for (i = 0; i < recorded_count; ++i) assert(recorded[i].count == 4);
        check_projection();
    }
    /* The individual callback retains the same quad representation. */
    reference_count = recorded_count = 0;
    add_reference_quad(vertices, vertices + 1, vertices + 3, vertices + 2, colors[0]);
    drv_draw_quad_flat_color(vertices, vertices + 1, vertices + 3, vertices + 2,
                              colors[0]);
    assert(recorded_count == 1U && recorded[0].count == 4);
    check_projection();
    /* Either pole's coincident pair becomes one native triangle while
     * still contributing exactly one visible quad to the statistics. */
    cap[0] = vertices[0]; cap[1] = vertices[1]; cap[2] = vertices[3];
    cap[3] = cap[0];
    recorded_count = 0;
    drv_draw_quad_flat_color(cap, cap + 1, cap + 2, cap + 3, colors[0]);
    assert(recorded_count == 1U && recorded[0].count == 3);
    cap[2] = cap[1]; cap[3] = vertices[2];
    recorded_count = 0;
    drv_draw_quad_flat_color(cap, cap + 1, cap + 2, cap + 3, colors[0]);
    assert(recorded_count == 1U && recorded[0].count == 3);
    j = recorded_count;
    drv_draw_quadbatch_flat_color(0U, NULL, 0U, NULL, NULL);
    assert(recorded_count == j);
    SetPort(saved_port);
    if (has_color) SetGDevice(saved_device);
    fail_gworld = old_failure;
    SizeWindow(qd_window, (short)old_width, (short)old_height, TRUE);
    qd_resize((int)old_width, (int)old_height);
    puts("PASS QuickDraw quad batches: shared indexed vertices, arbitrary order, 64-quad bounds, exact colors and full seams; individual quad callback retained");
}

/* Compare exact native polygon multisets: packet ordering can change, but
 * sharing transformed/projected rows must not change a rounded coordinate,
 * palette index, pole triangle, or clipped silhouette. */
static int compare_recorded(const void *left, const void *right)
{
    const RecordedPolygon *a = (const RecordedPolygon *)left;
    const RecordedPolygon *b = (const RecordedPolygon *)right;
    int i;
    if (a->color != b->color) return a->color < b->color ? -1 : 1;
    if (a->pattern_black != b->pattern_black)
        return a->pattern_black < b->pattern_black ? -1 : 1;
    if (a->count != b->count) return a->count < b->count ? -1 : 1;
    for (i = 0; i < a->count; ++i) {
        if (a->points[i].h != b->points[i].h)
            return a->points[i].h < b->points[i].h ? -1 : 1;
        if (a->points[i].v != b->points[i].v)
            return a->points[i].v < b->points[i].v ? -1 : 1;
    }
    return 0;
}

static void check_stream_equivalence(void)
{
    static RecordedPolygon individual[512];
    static const unsigned int widths[4] = { 640U, 320U, 97U, 800U };
    static const unsigned int heights[4] = { 480U, 240U, 480U, 120U };
    static const float pitch[4] = { 0.0f, 19.0f, 89.75f, 247.0f };
    static const float yaw[4] = { 180.0f, 37.0f, 179.5f, 121.0f };
    unsigned int size, pose, expected, i, old_width, old_height;
    int old_longitude;
    float old_x, old_y;
    GrafPtr saved_port;
    GDHandle saved_device;
    int old_fail;
    old_width = drv_win_width; old_height = drv_win_height;
    old_longitude = globe_longitude;
    old_x = drv_rot_x; old_y = drv_rot_y;
    GetPort(&saved_port);
    saved_device = has_color ? GetGDevice() : NULL;
    old_fail = fail_gworld; fail_gworld = TRUE;
    assert(globe_set_resolution(32, -1, GLOBE_RADIUS));
    for (size = 0; size < 4U; ++size) {
        SizeWindow(qd_window, (short)widths[size], (short)heights[size], TRUE);
        qd_resize((int)widths[size], (int)heights[size]);
        SetPort((GrafPtr)qd_window);
        if (has_color) SetGDevice(qd_device);
        for (pose = 0; pose < 4U; ++pose) {
            drv_rot_x = pitch[pose]; drv_rot_y = yaw[pose];
            drv_frame_failed = FALSE;
            qd_set_transform();
            recorded_count = 0;
            globe_draw_quads();
            assert(!drv_frame_failed && recorded_count <= 512U);
            expected = recorded_count;
            qsort(recorded, expected, sizeof(recorded[0]), compare_recorded);
            memcpy(individual, recorded, expected * sizeof(recorded[0]));
            /* The previous individual calls must not establish row reuse. */
            recorded_count = 0;
            globe_draw_quadbatches();
            assert(!drv_frame_failed && recorded_count == expected);
            qsort(recorded, recorded_count, sizeof(recorded[0]), compare_recorded);
            for (i = 0; i < expected; ++i)
                assert(compare_recorded(individual + i, recorded + i) == 0);
        }
    }
    drv_rot_x = old_x; drv_rot_y = old_y;
    assert(globe_set_resolution(old_longitude, -1, GLOBE_RADIUS));
    fail_gworld = old_fail;
    SizeWindow(qd_window, (short)old_width, (short)old_height, TRUE);
    qd_resize((int)old_width, (int)old_height);
    SetPort(saved_port);
    if (has_color) SetGDevice(saved_device);
    puts("PASS QuickDraw optimized stream: exact individual/batched polygon tuples and colors across poses, poles, resize scales and narrow clipped viewports");
}

static void check_osd_boundaries(void)
{
    unsigned int old_width, old_height, old_texts;
    int old_failure;
    Int next;
    FontInfo font;
    Int advance;
    Int (*wide_text)(const char *, Int, short, const FontInfo *, Int, int);
    old_width = drv_win_width; old_height = drv_win_height;
    old_failure = fail_gworld; fail_gworld = TRUE;
    SizeWindow(qd_window, 32767, 32767, TRUE);
    qd_resize(32767, 32767);
    SetPort((GrafPtr)qd_window);
    if (has_color) SetGDevice(qd_device);
    osd_text[0] = 0;
    old_texts = text_calls;
    wide_text = qd_osd_text;
    TextFont(qd_font_id); TextSize(qd_font_size); GetFontInfo(&font);
    advance = (Int)font.ascent + font.descent + font.leading;
    next = wide_text("boundary", 32767L - font.descent, 32751, &font, advance, TRUE);
    assert(next > 32767L && text_calls == old_texts + 1U);
    next = wide_text("outside", next, 32751, &font, advance, TRUE);
    assert(next > 32767L && text_calls == old_texts + 1U);
    osd_text[0] = 0;
    drv_draw_osd();
    assert(strstr(osd_text, "GWorld off") != NULL);
    assert(strstr(osd_text, "1-5") != NULL);
    fail_gworld = old_failure;
    SizeWindow(qd_window, (short)old_width, (short)old_height, TRUE);
    qd_resize((int)old_width, (int)old_height);
    SetPort(&desktop_port);
    if (has_color) SetGDevice(&screen_device_ptr);
    puts("PASS QuickDraw OSD bounds: 32-bit baselines cross 32767 without wrapping and native text coordinates remain in range");
}

static void check_osd_fonts(void)
{
    static const unsigned int widths[3] = { 320U, 640U, 1024U };
    static const unsigned int heights[3] = { 240U, 480U, 768U };
    static const short sizes[3] = { 9, 12, 18 };
    unsigned int old_width, old_height, i, queries;
    int old_failure;
    GrafPtr port;
    GDHandle device;
    old_width = drv_win_width; old_height = drv_win_height;
    old_failure = fail_gworld; fail_gworld = TRUE;
    GetPort(&port); device = has_color ? GetGDevice() : NULL;
    for (i = 0; i < 3U; ++i) {
        SizeWindow(qd_window, (short)widths[i], (short)heights[i], TRUE);
        qd_resize((int)widths[i], (int)heights[i]);
        SetPort((GrafPtr)qd_window);
        if (has_color) SetGDevice(qd_device);
        assert(qd_font_id == 3 && qd_font_size == sizes[i]);
        queries = font_queries;
        osd_text[0] = 0;
        drv_draw_osd();
        assert(selected_font == 3 && selected_size == sizes[i]);
        assert(strstr(osd_text, "GWorld off") != NULL && strstr(osd_text, "1-5") != NULL);
        assert(font_queries == queries); /* No font-resource search per frame. */
    }
    available_fonts = 1U;
    qd_resize(1024, 768);
    assert(qd_font_id == 3 && qd_font_size == 9);
    available_fonts = 0U;
    qd_resize(640, 480);
    assert(qd_font_id == 0 && qd_font_size == 9);
    osd_text[0] = 0;
    drv_draw_osd();
    assert(selected_font == 0 && selected_size == 9);
    assert(strstr(osd_text, "1-5") != NULL);
    available_fonts = 7U;
    fail_gworld = old_failure;
    SizeWindow(qd_window, (short)old_width, (short)old_height, TRUE);
    qd_resize((int)old_width, (int)old_height);
    SetPort(port);
    if (has_color) SetGDevice(device);
    puts("PASS QuickDraw OSD fonts: resize selects real 9/12/18-point bitmaps, unavailable sizes fall back, native ascent/descent/leading bound every line");
}

static void check_readiness(void)
{
    unsigned int previous_texts, previous_polygons;
    int previous_longitude;
    float previous_x, previous_y;
    assert(qd_ready);
    previous_texts = text_calls;
    previous_polygons = recorded_count;
    previous_longitude = globe_longitude;
    previous_x = drv_rot_x; previous_y = drv_rot_y;
    qd_ready = FALSE;
    drv_lmouse_pressed = TRUE;
    drv_render(); drv_draw_osd();
    drv_draw_quad_flat_color(globe_verts, globe_verts + 1, globe_verts + 2,
                              globe_verts + 3, 0);
    qd_mouse_move(123, 234);
    event_key('1', 0);
    assert(globe_longitude == previous_longitude);
    assert(text_calls == previous_texts && recorded_count == previous_polygons);
    assert(drv_rot_x == previous_x && drv_rot_y == previous_y);
    drv_lmouse_pressed = FALSE;
    qd_ready = TRUE;
    puts("PASS QuickDraw readiness: disabled lifecycle state blocks render, OSD, mesh keys and drag updates");
}

static void check_display_refresh(void)
{
    short original_depth, first_depth, other_depth, changed_depth;
    EventRecord event;
    unsigned int previous_worlds;
    if (!has_color) return;
    original_depth = display_depth;
    first_depth = screen_pixmap.pixelSize;
    other_depth = second_pixmap.pixelSize;
    changed_depth = qd_version < gestalt32BitQD ?
                     (original_depth == 4 ? 8 : 4) :
                     (original_depth <= 8 ? 16 : 8);
    memset(&event, 0, sizeof(event));
    event.what = osEvt;
    event.message = (long)suspendResumeMessage << 24;
    qd_event(&event);
    assert(!qd_active);
    display_depth = changed_depth;
    screen_pixmap.pixelSize = changed_depth;
    if (multiple_devices) second_pixmap.pixelSize = changed_depth;
    previous_worlds = world_calls;
    event.message |= resumeFlag;
    qd_event(&event);
    assert(qd_active && qd_ready && qd_dirty);
    assert(qd_depth == changed_depth && drv_bits_per_pixel == (unsigned int)changed_depth);
    /* The device follows the window, which stays on its monitor. */
    assert(qd_device == (multiple_devices ? &second_device_ptr : &screen_device_ptr));
    assert((qd_palette != NULL) == (changed_depth == 8));
    if (qd_version >= gestalt32BitQD) assert(world_calls > previous_worlds);
    recorded_count = 0;
    drv_render();
    assert(recorded_count > 0 && !world_locked);

    display_depth = original_depth;
    screen_pixmap.pixelSize = first_depth;
    second_pixmap.pixelSize = other_depth;
    event.what = activateEvt;
    event.message = (long)qd_window;
    event.modifiers = activeFlag;
    qd_event(&event);
    assert(qd_depth == original_depth && drv_bits_per_pixel == (unsigned int)original_depth);
    assert(qd_device == (multiple_devices ? &second_device_ptr : &screen_device_ptr));
    assert((qd_palette != NULL) == (original_depth == 8));
    puts("PASS QuickDraw display refresh: resume and activation rebuild palette/GWorld policy for changed device depth and restore the selected display");
}

static void check_recoverable_resources(void)
{
    unsigned char bytes[512];
    unsigned int handles, texts, calls;
    int old_osd, old_auto, i;
    Drv_result expected;
    GrafPtr saved_port;
    GDHandle saved_device;
    handles = handle_count;
    assert(drv_load_earth_chunk(TG_EARTH_32_ID, 512UL, NULL) == DRV_BAD_INPUT);
    assert(drv_load_earth_chunk(TG_EARTH_32_ID, 0UL, bytes) == DRV_BAD_INPUT);
    for (i = 1; i <= 4; ++i) {
        fail_resource = i;
        expected = i == 1 || i == 4 ? DRV_NO_MEMORY : DRV_BAD_DATA;
        assert(drv_load_earth_chunk(TG_EARTH_32_ID, 512UL, bytes) == expected);
        assert(handle_count == handles && resource_lock_failure == NULL);
    }
    fail_resource = 0;
    assert(drv_load_earth_chunk(TG_EARTH_32_ID, 512UL, bytes) == DRV_OK);
    assert(handle_count == handles);
    GetPort(&saved_port);
    saved_device = has_color ? GetGDevice() : NULL;
    if (has_color && qd_depth == 8) {
        qd_dispose_buffer();
        qd_dispose_palette();
        handles = handle_count;
        fail_color_table = TRUE;
        qd_make_palette();
        fail_color_table = FALSE;
        assert(qd_colors == NULL && qd_palette == NULL && qd_palette_missing());
        assert(handle_count == handles);
        fail_palette = TRUE;
        qd_make_palette();
        fail_palette = FALSE;
        assert(qd_colors == NULL && qd_palette == NULL && qd_palette_missing());
        assert(handle_count == handles);
    }
    if (has_color && qd_depth <= 8) {
        qd_make_buffer();
        recorded_count = 0; osd_text[0] = 0;
        calls = color_fill_calls;
        drv_render();
        assert(recorded_count != 0 && alert_calls == 0);
        if (qd_depth > 1 && !qd_pattern_unavailable)
            assert(qd_pattern != NULL && color_fill_calls > calls);
        if (qd_depth > 1) {
            if (qd_pattern != NULL) DisposePixPat(qd_pattern);
            qd_pattern = NULL;
            qd_pattern_unavailable = FALSE;
            calls = pattern_calls;
            fail_pattern = TRUE;
            recorded_count = 0; osd_text[0] = 0;
            drv_render();
            assert(recorded_count != 0 && qd_pattern_unavailable);
            assert(pattern_calls == calls && qd_pattern == NULL);
            recorded_count = 0; osd_text[0] = 0;
            drv_render();
            assert(recorded_count != 0 && pattern_calls == calls);
            fail_pattern = FALSE;
        }
    }
    DisposeHandle((Handle)qd_polygon); qd_polygon = NULL;
    fail_polygon = 2;
    calls = polygon_calls;
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    assert(recorded_count != 0 && !qd_frame_paused && !drv_frame_failed);
    assert(polygon_calls == calls + 2U && !world_locked && !qd_rendering);
    assert(current_port == saved_port && current_device == saved_device);
    old_osd = drv_osd_enabled;
    old_auto = drv_auto_enabled;
    drv_osd_enabled = FALSE;
    drv_auto_enabled = TRUE;
    fail_scratch = TRUE;
    calls = scratch_calls;
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    assert(qd_frame_paused && drv_frame_failed && !drv_auto_enabled);
    assert(recorded_count == 0 && scratch_calls == calls + 2U);
    assert(strstr(osd_text, "scratch unavailable") != NULL);
    assert(strstr(osd_text, "paused") != NULL);
    assert(current_port == saved_port && current_device == saved_device);
    assert(!world_locked && !qd_rendering && alert_calls == 0);
    drv_render();
    assert(scratch_calls == calls + 2U);
    fail_scratch = FALSE;
    qd_keyboard('1');
    recorded_count = 0; osd_text[0] = 0;
    calls = scratch_calls;
    drv_render();
    assert(!qd_frame_paused && !drv_frame_failed && recorded_count != 0);
    assert(scratch_calls == calls + 1U); /* Once per pass, not per polygon. */
    drv_osd_enabled = old_osd;
    drv_auto_enabled = old_auto;
    puts("PASS QuickDraw native scratch: bounded preflight avoids known low-memory fills, forces diagnostic OSD and pauses after two attempts; input retries and one probe serves each pass");
    drv_release_optional();
    assert(qd_world == NULL && qd_pattern == NULL && qd_palette == NULL);
    assert(qd_colors == NULL && !world_locked && !qd_rendering);
    assert(current_port == saved_port && current_device == saved_device);
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    assert(recorded_count != 0 && qd_ready && qd_running && alert_calls == 0);
    old_osd = drv_osd_enabled;
    drv_osd_enabled = FALSE;
    globe_free();
    assert(!globe_ready());
    drv_notice("No globe; select a resolution to retry");
    texts = text_calls;
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    assert(recorded_count == 0 && text_calls > texts);
    assert(strstr(osd_text, "No globe") != NULL);
    event_key('2', 0);
    assert(globe_ready() && globe_longitude == 64);
    /* The successful rebuild took the released resources back. */
    assert(test_restored());
    assert(!qd_palette_missing() && !qd_pattern_unavailable);
    drv_osd_enabled = old_osd;
    assert(current_port == saved_port && current_device == saved_device);
    puts("PASS QuickDraw recovery: resource errors release handles; palette/pattern failures and optional-buffer release keep drawing; empty scenes show status and a resolution retry restores the buffers");
}

static void select_window_port(GrafPtr *port, GDHandle *device)
{
    GetPort(port);
    *device = has_color ? GetGDevice() : NULL;
    SetPort((GrafPtr)qd_window);
    if (has_color) SetGDevice(qd_device);
}
static void restore_port(GrafPtr port, GDHandle device)
{
    SetPort(port);
    if (has_color) SetGDevice(device);
}

/* Apple menu, desk accessories and the Gestalt-guarded Apple events. */
static void check_menus(void)
{
    GrafPtr port;
    GDHandle device;
    unsigned int abouts, desks;
    EventRecord event;
    assert(qd_apple_menu != NULL && (*qd_apple_menu)->menuID == QD_APPLE_MENU);
    assert(qd_file_menu != NULL && (*qd_file_menu)->menuID == QD_FILE_MENU);
    assert(resource_menu_calls == 1U && menu_count == 2U);
    assert(qd_apple_events == apple_events);
    assert(ae_handler_count == (apple_events ? 4U : 0U));
    abouts = about_calls; desks = desk_calls;
    qd_menu((long)QD_APPLE_MENU << 16 | 1L);
    assert(about_calls == abouts + 1U && qd_running && qd_ready);
    qd_menu((long)QD_APPLE_MENU << 16 | 2L); /* The separator does nothing. */
    assert(about_calls == abouts + 1U && desk_calls == desks);
    select_window_port(&port, &device);
    qd_menu((long)QD_APPLE_MENU << 16 | 3L);
    assert(desk_calls == desks + 1U && current_port == (GrafPtr)&window_port);
    restore_port(port, device);
    if (apple_events) {
        memset(&event, 0, sizeof(event));
        test_apple_event(&event, kAEPrintDocuments);
        qd_event(&event);
        assert(ae_result == errAEEventNotHandled && qd_running);
        test_apple_event(&event, 0x7a7a7a7aUL);
        qd_event(&event);
        assert(ae_result == errAEEventNotHandled && qd_running);
    }
    puts("PASS QuickDraw menus: Apple menu About alert and desk accessories restore the port; Apple event handlers are installed only when Gestalt reports them");
}

/* Only an 8-bit device takes the Earth palette; others keep their table. */
static void check_palette_policy(void)
{
    GrafPtr port;
    GDHandle device;
    if (!has_color) {
        assert(qd_palette == NULL && qd_colors == NULL && window_palette == NULL);
        return;
    }
    assert((qd_palette != NULL) == (qd_depth == 8) && (qd_colors != NULL) == (qd_depth == 8));
    assert(window_palette == qd_palette);
    if (qd_world != NULL) assert(world_pixmap.pmTable == (qd_depth == 8 ? qd_colors : NULL));
    if (qd_depth > 1 && qd_depth < 8) {
        /* The standard 2/4-bit table is the policy, not a reduction. */
        select_window_port(&port, &device);
        osd_text[0] = 0;
        drv_draw_osd();
        assert(strstr(osd_text, "GWorld ") != NULL && strstr(osd_text, "color reduced") == NULL);
        restore_port(port, device);
    }
    puts("PASS QuickDraw palette policy: only 8-bit devices take the Earth palette, tolerant with 2/4-bit entries inhibited; other depths keep the standard table");
}

/* The cached levels must reproduce the per-pixel luminance threshold bits
 * exactly, and a monochrome pass must not reset its colors per polygon. */
static void check_dither_patterns(void)
{
    static const unsigned char bayer[8][8] = {
        {0,48,12,60,3,51,15,63}, {32,16,44,28,35,19,47,31},
        {8,56,4,52,11,59,7,55}, {40,24,36,20,43,27,39,23},
        {2,50,14,62,1,49,13,61}, {34,18,46,30,33,17,45,29},
        {10,58,6,54,9,57,5,53}, {42,26,38,22,41,25,37,21}
    };
    unsigned char expected[8];
    unsigned int luminance, x, y, level, built, fores, backs;
    int saved_osd;
    for (built = level = 0; level < QD_DITHER_LEVELS; ++level) built += qd_dither_built[level] != 0;
    /* Levels are built on first use: color depths never build one. */
    if (qd_depth > 1) assert(built == 0U);
    for (luminance = 0; luminance < 256U; ++luminance) {
        for (y = 0; y < 8U; ++y) {
            expected[y] = 0;
            for (x = 0; x < 8U; ++x)
                if (luminance < (unsigned int)bayer[y][x] * 4U + 2U)
                    expected[y] |= (unsigned char)(0x80U >> x);
        }
        assert(memcmp(qd_dither_pattern((luminance + 2U) / 4U)->pat, expected, 8) == 0);
    }
    for (built = level = 0; level < QD_DITHER_LEVELS; ++level) built += qd_dither_built[level] != 0;
    assert(built == QD_DITHER_LEVELS);
    if (qd_depth == 1) {
        saved_osd = drv_osd_enabled;
        drv_osd_enabled = FALSE;
        fores = fore_calls; backs = back_calls; recorded_count = 0;
        drv_render();
        /* One pair for the pass, one for presenting a basic QuickDraw world. */
        assert(recorded_count > 64U && fore_calls - fores <= 2U && back_calls - backs <= 2U);
        drv_osd_enabled = saved_osd;
    }
    recorded_count = 0;
    puts("PASS QuickDraw monochrome dither: 65 lazily cached levels match the per-pixel luminance thresholds bit for bit; colors are set once per pass");
}

/* An Earth-table GWorld paints exact colors solid and dithers only the
 * indices its inverse table hides, probing each index once per world. */
static void check_exact_colors(void)
{
    unsigned int probes, solid, dithered, patterns;
    int saved_osd;
    /* Without a lockable Earth world, or with nearest-color fills already
     * solid, nothing is probed. */
    if (!has_color || qd_depth != 8 || qd_world == NULL || fail_lock || qd_pattern_unavailable) {
        assert(color_index_calls == 0);
        return;
    }
    assert(qd_world_earth);
    saved_osd = drv_osd_enabled;
    drv_osd_enabled = FALSE;
    qd_make_buffer();
    assert(qd_world_earth);
    probes = color_index_calls; solid = solid_fill_calls; dithered = color_fill_calls;
    recorded_count = 0;
    drv_render();
    assert(recorded_count > 0 && color_index_calls > probes && color_index_calls - probes <= 256U);
    assert(solid_fill_calls > solid && color_fill_calls > dithered);
    probes = color_index_calls;
    recorded_count = 0;
    drv_render();
    assert(recorded_count > 0 && color_index_calls == probes);
    /* With no hidden colors every fill is solid and no pattern is built. */
    hidden_colors = FALSE;
    qd_make_buffer();
    dithered = color_fill_calls; patterns = rgb_pattern_calls;
    recorded_count = 0;
    drv_render();
    assert(recorded_count > 0 && color_fill_calls == dithered && rgb_pattern_calls == patterns);
    hidden_colors = TRUE;
    qd_make_buffer();
    drv_osd_enabled = saved_osd;
    recorded_count = 0;
    puts("PASS QuickDraw exact Earth colors: an Earth-table GWorld probes each index once, paints exact inverse-table colors solid and dithers only hidden ones");
}

/* Line breaking adds character widths, and help breaks are cached. */
static void check_osd_measure(void)
{
    static const char text[] = "QuickDraw measures each character once while it wraps this "
                               "longer status line into several rows for the narrow window";
    FontInfo font;
    Int advance;
    unsigned int calls, lines, help_calls, line;
    short width;
    char direct[1024];
    GrafPtr port;
    GDHandle device;
    select_window_port(&port, &device);
    TextFont(qd_font_id); TextSize(qd_font_size); GetFontInfo(&font);
    advance = (Int)font.ascent + font.descent + font.leading;
    width = 100;
    calls = char_width_calls;
    lines = (unsigned int)(qd_osd_text(text, 0L, width, &font, advance, FALSE) / advance);
    assert(lines > 3U);
    /* Linear: a character is measured again only when its word moves to
     * the next line, and one more ends each line. Remeasuring prefixes
     * would cost about strlen * line length / 2 instead. */
    assert(char_width_calls - calls <= 2U * (unsigned int)strlen(text) + lines);
    assert(text_width_calls == 0U);
    qd_help_valid = FALSE;
    calls = char_width_calls;
    qd_wrap_help(width);
    help_calls = char_width_calls - calls;
    assert(qd_help_valid && qd_help_lines > 1U && help_calls > 0U);
    calls = char_width_calls;
    qd_wrap_help(width);
    assert(char_width_calls == calls);
    /* The cached breaks draw exactly what measuring would. */
    osd_text[0] = 0;
    qd_osd_text(drv_help, 4L + font.ascent, width, &font, advance, TRUE);
    strcpy(direct, osd_text);
    osd_text[0] = 0;
    for (line = 0; line < qd_help_lines; ++line)
        qd_draw_line(drv_help + qd_help_start[line], (short)qd_help_count[line],
                     4L + font.ascent + (Int)line * advance, &font);
    assert(strcmp(direct, osd_text) == 0);
    calls = char_width_calls;
    qd_wrap_help((short)(width + 8));
    assert(char_width_calls > calls && qd_help_width == width + 8);
    /* A second identical frame measures only the status line. */
    qd_help_valid = FALSE;
    osd_text[0] = 0; calls = char_width_calls;
    drv_draw_osd();
    lines = char_width_calls - calls;
    qd_help_valid = FALSE;
    calls = char_width_calls;
    qd_wrap_help((short)(drv_win_width - 16U));
    help_calls = char_width_calls - calls;
    osd_text[0] = 0; calls = char_width_calls;
    drv_draw_osd();
    assert(lines - (char_width_calls - calls) == help_calls);
    restore_port(port, device);
    puts("PASS QuickDraw OSD measurement: per-character widths keep wrapping linear with no TextWidth calls; help line breaks are cached per font, size and width");
}

/* Released optional resources come back once per scene revision, when the
 * largest block leaves headroom, palette and pattern before the GWorld. */
static void check_optional_restore(void)
{
    unsigned int copies;
    int saved_osd;
    Size enough;
    saved_osd = drv_osd_enabled;
    enough = max_block;
    event_key('1', 0); event_key('2', 0);
    assert(globe_longitude == 64 && !qd_optional_released);
    drv_release_optional();
    assert(qd_optional_released && qd_world == NULL && qd_pattern == NULL && qd_palette == NULL);
    /* Too little free memory: the attempt after the rebuild declines. */
    max_block = QD_RESTORE_HEADROOM - 1L;
    event_key('1', 0);
    assert(globe_longitude == 32 && qd_optional_released && qd_world == NULL);
    assert(qd_palette_missing() == (has_color && qd_depth == 8));
    /* Memory alone does not bring a second attempt in the same revision. */
    max_block = enough;
    event_key('o', 0); event_key('o', 0);
    assert(qd_optional_released && qd_world == NULL);
    if (qd_world_available) {
        /* Room for the palette and pattern scratch, not for the GWorld. */
        max_block = QD_RESTORE_HEADROOM + QD_PALETTE_BYTES + QD_PATTERN_SCRATCH;
        event_key('2', 0);
        assert(qd_optional_released && qd_world == NULL);
        assert(!qd_palette_missing() && !qd_pattern_unavailable);
        max_block = enough;
        event_key('1', 0);
    } else event_key('2', 0);
    assert(test_restored());
    assert(!qd_palette_missing() && !qd_pattern_unavailable);
    copies = copy_calls; recorded_count = 0;
    drv_render();
    assert(recorded_count != 0 && copy_calls == copies + (qd_world != NULL ? 1U : 0U));
    /* A spent reserve comes back first, without using up the attempt. */
    drv_release_optional();
    assert(grow_zone(1024) == QD_RESERVE_BYTES && qd_reserve_spent);
    event_key('2', 0);
    assert(qd_optional_released && !qd_restore_tried);
    recorded_count = 0;
    drv_render();
    assert(!qd_reserve_spent && *qd_reserve != NULL && recorded_count != 0);
    event_key('o', 0); event_key('o', 0);
    assert(test_restored());
    drv_osd_enabled = saved_osd;
    recorded_count = 0;
    puts("PASS QuickDraw optional recovery: released palette, pattern and GWorld return once per scene revision when the largest block leaves headroom; the reserve returns first");
}

/* Master pointers come first; checked allocations never take the reserve;
 * an A5-safe grow zone gives it to Toolbox fills; frames take it back. */
static void check_grow_zone(void)
{
    unsigned int calls, gifts;
    int saved_osd;
    assert(master_blocks == QD_MASTER_BLOCKS && grow_zone != NULL);
    assert(qd_reserve != NULL && *qd_reserve != NULL);
    assert(GetHandleSize(qd_reserve) == QD_RESERVE_BYTES && !qd_reserve_spent);
    saved_osd = drv_osd_enabled;
    drv_osd_enabled = FALSE;
    memory_pressure = TRUE;
    calls = grow_zone_calls;
    assert(!qd_probe_scratch((Size)QD_FILL_SCRATCH)); /* A probe cannot be faked. */
    qd_make_buffer();
    assert(qd_world == NULL);
    event_key('3', 0);
    assert(!globe_ready() && !cursor_watch);
    assert(grow_zone_calls > calls && *qd_reserve != NULL && !qd_reserve_spent);
    memory_pressure = FALSE;
    event_key('2', 0);
    assert(globe_ready() && globe_longitude == 64 && test_restored());
    gifts = grow_zone_gifts;
    toolbox_pressure = TRUE;
    recorded_count = 0;
    drv_render();
    assert(grow_zone_gifts == gifts + 1U && qd_reserve_spent && recorded_count != 0);
    recorded_count = 0;
    drv_render();
    assert(!qd_reserve_spent && GetHandleSize(qd_reserve) == QD_RESERVE_BYTES);
    assert(qd_optional_released && qd_world == NULL && recorded_count != 0 && !qd_frame_paused);
    /* If the reserve cannot come back, drawing pauses until a retry. */
    toolbox_pressure = TRUE;
    drv_render();
    assert(qd_reserve_spent);
    fail_reallocate = TRUE;
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    assert(qd_frame_paused && qd_reserve_spent && recorded_count == 0);
    assert(strstr(osd_text, "reserve unavailable") != NULL && strstr(osd_text, "paused") != NULL);
    fail_reallocate = FALSE;
    event_key('1', 0);
    assert(!qd_frame_paused && qd_optional_released);
    recorded_count = 0;
    drv_render();
    assert(!qd_frame_paused && !qd_reserve_spent && recorded_count != 0);
    event_key('o', 0); event_key('o', 0);
    assert(test_restored());
    event_key('2', 0);
    drv_osd_enabled = saved_osd;
    recorded_count = 0;
    puts("PASS QuickDraw memory reserve: master pointers precede the Toolbox; checked allocations never take the reserve; an A5-safe grow zone gives it to Toolbox fills, then frames release optional memory and reacquire it or pause");
}

static int run_contract(void)
{
    int resolution, pose, i;
    float old_x, old_y;
    unsigned int texts, palettes, watch, cursors;
    EventRecord event;
    GrafPtr saved;
    static const int pitch[4] = { 0, 19, 247, 71 };
    static const int yaw[4] = { 180, 37, 121, 302 };
    assert(globe_longitude == 32 && globe_latitude == 16);
    assert(!drv_texture_avail && !drv_texture_enabled);
    assert(qd_color_available == has_color && qd_depth == display_depth);
    assert(qd_has_fpu == (cpu_type >= gestalt68020 && fpu_type != gestaltNoFPU));
    assert(window_calls == 1 && drv_bits_per_pixel == (unsigned int)display_depth);
    assert(drv_colormap_bits == (unsigned int)(display_depth <= 8 ? display_depth :
                                                (display_depth == 16 ? 15 : 24)));
    assert(drv_colormap_colors == (1UL << drv_colormap_bits));
    assert(drv_colors == drv_colormap_colors);
    assert(drv_win_width == expected_width && drv_win_height == expected_height);
    assert(strstr(drv_help, "1-5") != NULL);
    /* Color QuickDraw needs 32-bit QD for a GWorld; basic QuickDraw System 7. */
    assert(qd_world_available == (has_color ? qd_version >= gestalt32BitQD :
                                  system_version >= 0x0700));
    assert(qd_world != NULL || !qd_world_available || fail_gworld);
    check_menus();
    check_palette_policy();
    check_fpu_angles();
    check_reusable_polygon();
    check_color_submission();
    check_dither_patterns();
    check_exact_colors();
    check_osd_measure();
    drv_osd_enabled = FALSE;
    saved = current_port;
    for (resolution = 0; resolution < 2; ++resolution) {
        watch = watch_allocations;
        event_key((unsigned char)('1' + resolution), 0);
        assert(globe_longitude == (resolution == 0 ? 32 : 64));
        /* Rebuilt meshes allocate under the watch, then the arrow returns. */
        assert(!cursor_watch && (resolution == 0 || watch_allocations > watch));
        for (pose = 0; pose < (screen_mode ? 1 : 4); ++pose) {
            drv_rot_x = (float)pitch[pose]; drv_rot_y = (float)yaw[pose];
            build_reference(); recorded_count = 0;
            drv_render();
            assert(recorded_count == globe_quads_num_vis);
            assert(current_port == saved && !world_locked);
            assert(current_device == (has_color ? &screen_device_ptr : NULL));
            check_projection();
        }
        event_key('t', 0); assert(!drv_texture_enabled);
    }
    streaming_polygons = TRUE;
    for (resolution = 2; resolution < 5; ++resolution) {
        watch = watch_allocations;
        event_key((unsigned char)('1' + resolution), 0);
        assert(globe_longitude == (32 << resolution));
        assert(!cursor_watch && watch_allocations > watch);
        assert(globe_verts_num * sizeof(Vertex) > 65535UL);
        recorded_count = 0;
        drv_render();
        assert(recorded_count == globe_quads_num_vis);
        assert(recorded_count > 0 && recorded_count <= globe_quads_num);
    }
    streaming_polygons = FALSE;
    cursors = cursor_calls;
    event_key('6', 0); assert(globe_longitude == 512);
    event_key('o', 0); event_key('o', 0);
    assert(cursor_calls == cursors); /* Only resolution keys show the watch. */
    check_quad_batches();
    check_stream_equivalence();
    check_osd_boundaries();
    check_osd_fonts();
    check_readiness();
    event_key('2', 0);
    drv_rot_x = 19.0f; drv_rot_y = 37.0f;
    check_clipping();
    check_buffer_recovery();
    puts("PASS QuickDraw rendering: five quad resolutions, independent perspective/color/dither references, compact pole triangles, FPU/fixed paths, clipping and native fills");
    old_x = drv_rot_x; old_y = drv_rot_y;
    memset(&event, 0, sizeof(event)); event.what = mouseDown;
    event.where.h = 320; event.where.v = 240; find_part = inContent;
    qd_event(&event); assert(drv_lmouse_pressed);
    qd_mouse_move(384, 288);
    assert(drv_rot_x != old_x && drv_rot_y != old_y);
    memset(&event, 0, sizeof(event)); event.what = mouseUp;
    qd_event(&event); assert(!drv_lmouse_pressed);
    texts = text_calls; osd_text[0] = 0;
    event_key('o', 0); assert(drv_osd_enabled);
    memset(&event, 0, sizeof(event)); event.what = updateEvt; event.message = (long)qd_window;
    recorded_count = 0; qd_event(&event); assert(text_calls >= texts + 2);
    assert(strstr(osd_text, "quads") != NULL && strstr(osd_text, "triangles") == NULL);
    assert(strstr(osd_text, "frames per second") != NULL);
    assert(strstr(osd_text, "GWorld ") != NULL);
    assert(strstr(osd_text, "frames per second") < strstr(osd_text, "GWorld "));
    assert(strstr(osd_text, qd_world != NULL ? "GWorld on" : "GWorld off") != NULL);
    event_key('o', 0); assert(!drv_osd_enabled);
    event_key('f', 0); assert(!drv_fps_enabled);
    event_key('o', 0); assert(drv_osd_enabled);
    osd_text[0] = 0;
    recorded_count = 0;
    drv_render();
    assert(strstr(osd_text, "frames per second") == NULL);
    assert(strstr(osd_text, "GWorld ") != NULL);
    event_key('F', 0); assert(drv_fps_enabled);
    event_key('o', 0); assert(!drv_osd_enabled);
    event_key('a', 0); assert(drv_auto_enabled);
    event_key('a', 0); assert(!drv_auto_enabled);
    palettes = palette_calls;
    event.what = activateEvt; event.message = (long)qd_window; event.modifiers = 0;
    qd_event(&event); assert(!qd_active);
    event.modifiers = activeFlag; qd_event(&event); assert(qd_active);
    if (has_color && display_depth == 8) assert(palette_calls > palettes);
    event.what = osEvt; event.message = (long)suspendResumeMessage << 24;
    drv_lmouse_pressed = TRUE;
    qd_event(&event); assert(!qd_active && !drv_lmouse_pressed);
    event.message |= resumeFlag; qd_event(&event); assert(qd_active);
    check_display_refresh();
    memset(&event, 0, sizeof(event)); event.what = mouseDown; find_part = inGrow;
    qd_event(&event); find_part = inGoAway;
    assert(drv_win_width == 800 && drv_win_height == 600);
    build_reference(); recorded_count = 0; drv_render(); check_projection();
    qd_resize(1, 1); camera_triangle(-2.0, -2.0, -0.5);
    {
        Point tiny[10];
        unsigned int indices[3];
        indices[0] = 0; indices[1] = 1; indices[2] = 2;
        assert(qd_prepare_polygon(indices, 3U, 3U, tiny) >= 3);
    }
    SizeWindow(qd_window, 800, 600, TRUE); qd_resize(800, 600);
    ticks = 0xfffffff0UL;
    drv_reset_timing();
    for (i = 0; i < 2 * DRV_FPS_HISTORY; ++i) { drv_fps_start(); drv_fps_stop(); }
    /* Each fake start/stop consumes a tick: completed frames are two ticks
     * apart, including the wrap, rather than one render-call tick apart. */
    assert(fabs(drv_fps_average - 30.0f) < 0.01);
    /* Basic QuickDraw uses System 7 GWorld calls, never Color QuickDraw. */
    if (!has_color) assert(color_calls == 0 && palette_calls == 0);
    if (qd_world_available && !fail_gworld && !fail_lock) assert(copy_calls > 0 && world_calls >= 3);
    if (!qd_world_available || fail_gworld || fail_lock) assert(copy_calls == 0);
    if (!qd_world_available) assert(world_calls == 0);
    check_optional_restore();
    check_grow_zone();
    check_recoverable_resources();
    assert(text_width_calls == 0); /* Wrapping adds character widths. */
    puts("PASS QuickDraw events: adaptive default/explicit window size, 1-5 keys, wrapped OSD and GWorld state, mouse, resize, port restoration and TickCount rollover");
    return 0;
}
int main(int argc, char **argv)
{
    char *arguments[2];
    unsigned int old_texts;
    assert(argc == 2);
    screen_mode = strstr(argv[1], "small") != NULL ||
                  strstr(argv[1], "large") != NULL ||
                  strstr(argv[1], "explicit") != NULL ||
                  strstr(argv[1], "tall") != NULL;
    if (strcmp(argv[1], "small640") == 0 || strstr(argv[1], "explicit") != NULL) {
        screen_width = 640; screen_height = 480;
        expected_width = 320; expected_height = 240;
    }
    if (strcmp(argv[1], "small512") == 0) {
        screen_width = 512; screen_height = 342;
        expected_width = 320; expected_height = 240;
    }
    if (strcmp(argv[1], "large800") == 0) {
        screen_width = 800; screen_height = 600;
    }
    if (strcmp(argv[1], "tall640") == 0) {
        screen_width = 640; screen_height = 600;
        expected_width = 600;
    }
    if (strcmp(argv[1], "explicit") == 0) explicit_start = 1;
    if (strcmp(argv[1], "explicitwidth") == 0) explicit_start = 2;
    if (strcmp(argv[1], "explicitheight") == 0) explicit_start = 3;
    if (explicit_start == 1 || explicit_start == 2) expected_width = 400;
    if (explicit_start == 1 || explicit_start == 3) expected_height = 300;
    /* mono: System 7 basic QuickDraw (1-bit GWorld); mono6: System 6, no
     * GWorld or Apple events; monofail: the 1-bit GWorld cannot be made. */
    has_color = strncmp(argv[1], "mono", 4) != 0;
    if (strcmp(argv[1], "mono6") == 0 || strcmp(argv[1], "oldcolor") == 0) {
        system_version = 0x0608;
        apple_events = FALSE;
    }
    expect_fatal = strcmp(argv[1], "fatal") == 0;
    if (strcmp(argv[1], "polyfail") == 0) failure_profile = 1;
    if (strcmp(argv[1], "patternfail") == 0) failure_profile = 2;
    display_depth = has_color ? (short)atoi(argv[1]) : 1;
    if (display_depth == 0) display_depth = 8;
    multiple_devices = strcmp(argv[1], "multi") == 0;
    if (multiple_devices) display_depth = 32;
    /* System 7.1 includes 32-Bit QuickDraw 1.3 on Color QuickDraw Macs. */
    qd_version = has_color ? gestalt32BitQD13 : gestaltOriginalQD;
    fail_gworld = strcmp(argv[1], "nogworld") == 0 || strcmp(argv[1], "monofail") == 0;
    fail_lock = strcmp(argv[1], "lockfail") == 0;
    if (strcmp(argv[1], "oldcolor") == 0) qd_version = gestalt8BitQD;
    if (strcmp(argv[1], "qd32") == 0) {
        /* System 6 with the 32-Bit QuickDraw 1.0 INIT: GWorlds, but no
         * GetGWorldPixMap and no Apple events. */
        qd_version = gestalt32BitQD;
        system_version = 0x0607;
        apple_events = FALSE;
    }
    if (strcmp(argv[1], "fpu") == 0) { cpu_type = gestalt68030; fpu_type = gestalt68882; }
    if (strcmp(argv[1], "falsefpu") == 0) fpu_type = gestalt68881;
    /* Close box by default; Command-Q and the quit Apple event in turn. */
    quit_mode = strcmp(argv[1], "fpu") == 0 ? 1 : (strcmp(argv[1], "16") == 0 ? 2 : 0);
    screen_pixmap.pixelSize = display_depth;
    screen_pixmap.bounds.right = screen_width; screen_pixmap.bounds.bottom = screen_height;
    screen_device.gdPMap = &screen_pixmap_ptr;
    screen_device.gdRect = screen_pixmap.bounds;
    if (multiple_devices) {
        screen_pixmap.pixelSize = 4;
        second_pixmap.pixelSize = 32; second_pixmap.bounds = screen_pixmap.bounds;
        second_device.gdPMap = &second_pixmap_ptr; second_device.gdRect = screen_pixmap.bounds;
        second_device.gdRect.left = 1024; second_device.gdRect.right = 2048;
        screen_device.gdNextGD = &second_device_ptr;
    }
    /* Before initialization and after close, late updates must be harmless. */
    assert(!qd_ready);
    drv_render(); drv_draw_osd(); event_key('a', 0);
    assert(!drv_auto_enabled && text_calls == 0);
    arguments[0] = "testglobe"; arguments[1] = NULL;
    assert(testglobe_main(1, arguments) == 0);
    /* The process-level owner releases scene storage after native cleanup. */
    globe_free();
    assert(contract_started && window_live == NULL);
    assert(ptr_count == 0 && handle_count == 0 && menu_count == 0);
    assert(world_count == 0 && palette_count == 0 && window_count == 0 && !world_locked);
    assert(pixpat_count == 0 && !pen_hidden && !cursor_watch);
    assert(alert_calls == 0 && !in_update);
    old_texts = text_calls;
    assert(!qd_ready && !qd_running);
    drv_render(); drv_draw_osd(); event_key('1', 0);
    assert(text_calls == old_texts && globe_verts_num == 0);
    assert(current_device == (has_color ? &screen_device_ptr : NULL));
    puts("PASS QuickDraw cleanup: polygon/heap/palette/menu/window/GWorld ownership and close path");
    return 0;
}
