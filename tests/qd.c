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
static int fail_color_table;
static int fail_palette;
static int fail_resource;
static OSErr resource_error;
static Handle resource_lock_failure;
static Ptr resource_lock_bytes;
static unsigned int pattern_calls;
static unsigned int polygon_calls;
static int reentry_requested;
static unsigned int reentry_calls, reentry_width;
static short find_part = inGoAway;
static unsigned long ticks;
static unsigned int window_calls, color_calls, world_calls, copy_calls;
static unsigned int palette_calls, text_calls, alert_calls;
static unsigned int ptr_count, handle_count, poly_count, menu_count;
static unsigned int world_count, palette_count, window_count;
static void *memory_blocks[64];
static void *handle_blocks[64];
static Size handle_sizes[64];
static unsigned int handle_locks[64];
static PaletteHandle window_palette;
typedef struct { RGBColor foreground, background; } Test_port_colors;
static Test_port_colors desktop_colors, window_colors, world_colors;
static Point pen;
static PolyHandle recording_poly;
static Point pending_points[16];
static int pending_count;
static int pen_hidden;
static unsigned int pixpat_count;
static unsigned int recorded_count;
static int next_event_stage, contract_started, quit_using_key;
static int mouse_down;
static float loop_yaw;
static Point mouse_position;
static int run_contract(void);
static short screen_width = 1024, screen_height = 768;
static unsigned int expected_width = 640, expected_height = 480;
static int screen_mode;
static int streaming_polygons;
static char osd_text[1024];

typedef struct {
    int count;
    Point points[12];
    unsigned long color;
    unsigned int pattern_black;
} RecordedPolygon;
static RecordedPolygon recorded[5000];

static void color_api(void) { assert(has_color); ++color_calls; }
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
    if (fail_color_table && size == (Size)(sizeof(ColorTable) +
                                          255L * sizeof(ColorSpec))) return NULL;
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
    memcpy(*handle, bytes, (size_t)size);
    free(bytes);
    if (fail_resource == 4) resource_lock_failure = handle;
    return handle;
}
void ReleaseResource(Handle h) { DisposeHandle(h); }
OSErr ResError(void) { return resource_error; }
OSErr MemError(void) { return noErr; }
unsigned long TickCount(void) { ticks = (ticks + 1UL) & 0xffffffffUL; return ticks; }
void InitGraf(GrafPtr *port)
{
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
void MaxApplZone(void) {}
OSErr Gestalt(OSType selector, long *response)
{
    assert(response != NULL);
    if (selector == gestaltQuickdrawVersion) *response = qd_version;
    else if (selector == gestaltProcessorType) *response = cpu_type;
    else if (selector == gestaltFPUType) *response = fpu_type;
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
GDHandle GetMaxDevice(const Rect *r) { color_api(); assert(r != NULL); return &screen_device_ptr; }
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
void SelectWindow(WindowPtr w) { assert(w == window_live); }
WindowPtr FrontWindow(void) { return window_live; }
void SetPort(GrafPtr port) { assert(port != NULL); current_port = port; }
void GetPort(GrafPtr *port) { assert(port != NULL); *port = current_port; }
void SetGWorld(CGrafPtr port, GDHandle device)
{
    color_api(); assert(qd_version >= gestalt32BitQD && port != NULL);
    if (port == world_live) assert(world_locked);
    current_port = (GrafPtr)port;
    current_device = port == world_live && device == NULL ? world_device : device;
}
void GetGWorld(CGrafPtr *port, GDHandle *device)
{
    color_api(); assert(qd_version >= gestalt32BitQD && port != NULL && device != NULL);
    *port = (CGrafPtr)current_port; *device = current_device;
}
QDErr NewGWorld(GWorldPtr *world, short depth, const Rect *bounds,
                 CTabHandle table, GDHandle device, unsigned long flags)
{
    color_api(); ++world_calls;
    assert(qd_version >= gestalt32BitQD && world_live == NULL);
    assert(world != NULL && bounds != NULL && (depth == 0 || depth == display_depth));
    (void)table;
    if (fail_gworld) { *world = NULL; return memFullErr; }
    world_live = (GWorldPtr)calloc(1, sizeof(CGrafPort));
    assert(world_live != NULL);
    test_init_colors(&world_colors);
    world_live->portRect = *bounds;
    world_live->portBits.bounds = *bounds;
    world_pixmap.bounds = *bounds; world_pixmap.pixelSize = display_depth;
    world_pixmap.rowBytes = (short)(((bounds->right - bounds->left) * display_depth + 7) / 8);
    world_pixmap.baseAddr = (Ptr)calloc((size_t)(world_pixmap.rowBytes & 0x3fff) *
                                      (bounds->bottom - bounds->top), 1);
    assert(world_pixmap.baseAddr != NULL);
    world_live->portPixMap = &world_pixmap_ptr;
    *world = world_live;
    if (flags & noNewDevice) {
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
PixMapHandle GetGWorldPixMap(GWorldPtr world) { color_api(); assert(world == world_live); return &world_pixmap_ptr; }
Boolean LockPixels(PixMapHandle pixels) { color_api(); assert(qd_version >= gestalt32BitQD && pixels == &world_pixmap_ptr && !world_locked); if (fail_lock) return FALSE; world_locked = TRUE; return TRUE; }
void UnlockPixels(PixMapHandle pixels) { color_api(); assert(pixels == &world_pixmap_ptr && world_locked); world_locked = FALSE; }
void DisposeGWorld(GWorldPtr world)
{
    color_api(); assert(world == world_live && !world_locked && world_count == 1);
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
void EraseRect(const Rect *r) { assert(r != NULL && current_port != NULL); }
void PaintRect(const Rect *r) { assert(r != NULL && current_port != NULL); }
PolyHandle OpenPoly(void)
{
    assert(recording_poly == NULL && !pen_hidden);
    ++polygon_calls;
    if (fail_polygon) {
        if (fail_polygon == 2) fail_polygon = 0;
        return NULL;
    }
    pen_hidden = TRUE;
    recording_poly = (PolyHandle)NewHandle((Size)(sizeof(Polygon) + 12 * sizeof(Point)));
    pending_count = 0; ++poly_count;
    return recording_poly;
}
void ClosePoly(void) { assert(recording_poly != NULL && pending_count >= 3 && pen_hidden); recording_poly = NULL; pen_hidden = FALSE; }
void KillPoly(PolyHandle p) { assert(p != NULL && poly_count > 0); DisposeHandle((Handle)p); --poly_count; }
void MoveTo(short h, short v)
{
    pen.h = h; pen.v = v;
    if (recording_poly != NULL) {
        assert(pending_count < 12); pending_points[pending_count++] = pen;
    }
}
void LineTo(short h, short v)
{
    pen.h = h; pen.v = v;
    if (recording_poly != NULL) {
        assert(pending_count < 12); pending_points[pending_count++] = pen;
    }
}
static void record_polygon(PolyHandle poly, unsigned int black, const RGBColor *color)
{
    RecordedPolygon *entry;
    EventRecord event;
    Vertex *borrowed;
    unsigned long revision;
    unsigned int width;
    int longitude, osd;
    int i, count;
    assert(poly != NULL && current_port != NULL);
    if (reentry_requested) {
        reentry_requested = FALSE; ++reentry_calls;
        assert(qd_rendering);
        borrowed = globe_verts; revision = globe_revision;
        longitude = globe_longitude; width = drv_win_width;
        osd = drv_osd_enabled;
        qd_keyboard('2');
        qd_keyboard('o');
        memset(&event, 0, sizeof(event));
        event.what = keyDown; event.message = 'o';
        qd_event(&event);
        qd_resize((int)reentry_width, (int)drv_win_height);
        qd_refresh_display();
        event.what = updateEvt; event.message = (long)qd_window;
        qd_event(&event);
        assert(globe_verts == borrowed && globe_revision == revision);
        assert(globe_longitude == longitude && drv_win_width == width);
        assert(drv_osd_enabled == osd && qd_rendering);
    }
    count = pending_count;
    if (count > 3 && pending_points[count - 1].h == pending_points[0].h &&
        pending_points[count - 1].v == pending_points[0].v) --count;
    assert(count >= 3 && count <= 10);
    for (i = 0; i < count; ++i) {
        assert(pending_points[i].h != pending_points[(i + 1) % count].h ||
               pending_points[i].v != pending_points[(i + 1) % count].v);
    }
    if (streaming_polygons) {
        long area = 0;
        for (i = 0; i < count; ++i) {
            assert(pending_points[i].h >= 0 && pending_points[i].h <= (int)drv_win_width);
            assert(pending_points[i].v >= 0 && pending_points[i].v <= (int)drv_win_height);
            area += (long)pending_points[i].h * pending_points[(i + 1) % count].v -
                    (long)pending_points[i].v * pending_points[(i + 1) % count].h;
        }
        assert(area < 0);
        ++recorded_count;
        return;
    }
    assert(recorded_count < 5000);
    entry = recorded + recorded_count++;
    entry->count = count; entry->color = rgb_value(color); entry->pattern_black = black;
    for (i = 0; i < count; ++i) {
        entry->points[i] = pending_points[i];
        assert(entry->points[i].h >= 0 && entry->points[i].h <= (int)drv_win_width);
        assert(entry->points[i].v >= 0 && entry->points[i].v <= (int)drv_win_height);
    }
}
void PaintPoly(PolyHandle p) { assert(has_color); record_polygon(p, 64U, &test_current_colors()->foreground); }
void FillPoly(PolyHandle p, const Pattern *pattern) { assert(pattern != NULL); record_polygon(p, pattern_bits(pattern->pat), &test_current_colors()->foreground); }
void PenSize(short h, short v) { assert(h >= 0 && v >= 0); }
void PenNormal(void) {}
void HidePen(void) { assert(!pen_hidden); pen_hidden = TRUE; }
void ShowPen(void) { assert(pen_hidden); pen_hidden = FALSE; }
PixPatHandle NewPixPat(void)
{
    color_api();
    ++pattern_calls;
    if (fail_pattern) return NULL;
    ++pixpat_count;
    return (PixPatHandle)NewHandle((Size)sizeof(PixPat));
}
void DisposePixPat(PixPatHandle p) { color_api(); assert(pixpat_count > 0); DisposeHandle((Handle)p); --pixpat_count; }
void MakeRGBPat(PixPatHandle p, const RGBColor *c) { color_api(); assert(p != NULL && c != NULL); test_drawing_device(); (*p)->color = *c; }
void FillCPoly(PolyHandle p, PixPatHandle pattern) { color_api(); assert(pattern != NULL); record_polygon(p, 64U, &(*pattern)->color); }
void ForeColor(long color)
{
    RGBColor *foreground;
    assert(color == blackColor || color == whiteColor);
    foreground = &test_current_colors()->foreground;
    foreground->red = foreground->green = foreground->blue = color == blackColor ? 0 : 65535U;
}
void BackColor(long color)
{
    RGBColor *background;
    assert(color == blackColor || color == whiteColor);
    background = &test_current_colors()->background;
    background->red = background->green = background->blue = color == blackColor ? 0 : 65535U;
}
void RGBForeColor(const RGBColor *color) { color_api(); assert(color != NULL); test_drawing_device(); test_current_colors()->foreground = *color; }
void RGBBackColor(const RGBColor *color) { color_api(); assert(color != NULL); test_drawing_device(); test_current_colors()->background = *color; }
void GetForeColor(RGBColor *color) { color_api(); assert(color != NULL); *color = test_current_colors()->foreground; }
void GetBackColor(RGBColor *color) { color_api(); assert(color != NULL); *color = test_current_colors()->background; }
void TextFont(short font) { (void)font; }
void TextSize(short size) { assert(size > 0); }
void TextMode(short mode) { (void)mode; }
short TextWidth(const void *text, short start, short count)
{ assert(text != NULL && start >= 0 && count >= 0); return (short)(6 * count); }
void DrawText(const void *text, short start, short count)
{
    assert(text != NULL && start >= 0 && count > 0); ++text_calls;
    assert(pen.h >= 0 && pen.v > 0 && pen.v <= 32767);
    assert(strstr((const char *)text + start, "texture") == NULL);
    assert(TextWidth(text, start, count) <= (int)drv_win_width - 16);
    assert(strlen(osd_text) + (size_t)count + 2 < sizeof(osd_text));
    if (osd_text[0] != 0) strcat(osd_text, " ");
    strncat(osd_text, (const char *)text + start, (size_t)count);
}
void DrawString(ConstStr255Param text) { assert(text != NULL && text[0] > 0); ++text_calls; }
void CopyBits(const BitMap *source, const BitMap *destination, const Rect *a,
                const Rect *b, short mode, RgnHandle mask)
{
    assert(has_color && source != NULL && destination != NULL && a != NULL && b != NULL);
    assert(mode == srcCopy || mode == (srcCopy | ditherCopy)); (void)mask;
    assert(source->bounds.right == a->right && source->bounds.bottom == a->bottom);
    assert(current_port == (GrafPtr)&window_port);
    assert(rgb_value(&window_colors.foreground) == 0);
    assert(rgb_value(&window_colors.background) == 0xffffffUL);
    ++copy_calls;
}
long GetCTSeed(void) { color_api(); return 1; }
PaletteHandle NewPalette(short count, CTabHandle table, short usage, short tolerance)
{
    PaletteHandle p;
    int i;
    color_api(); assert(count == 256 && table != NULL && (*table)->ctSize == 255);
    for (i = 0; i < 256; ++i) {
        assert((*table)->ctTable[i].value == i);
        assert((*table)->ctTable[i].rgb.red == earth_cmap[i][0] * 257U);
        assert((*table)->ctTable[i].rgb.green == earth_cmap[i][1] * 257U);
        assert((*table)->ctTable[i].rgb.blue == earth_cmap[i][2] * 257U);
    }
    (void)usage; (void)tolerance;
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
    m = (MenuHandle)NewHandle((Size)sizeof(MenuInfo)); (*m)->menuID = id; ++menu_count;
    return m;
}
void AppendMenu(MenuHandle m, ConstStr255Param text) { assert(m != NULL && text != NULL); }
void InsertMenu(MenuHandle m, short before) { assert(m != NULL); (void)before; }
void DeleteMenu(short id) { (void)id; }
void DisposeMenu(MenuHandle m) { assert(m != NULL && menu_count > 0); DisposeHandle((Handle)m); --menu_count; }
void DrawMenuBar(void) {}
void ClearMenuBar(void) {}
void DrawGrowIcon(WindowPtr w) { assert(w == window_live); }
long MenuSelect(Point p) { (void)p; return 0; }
long MenuKey(short key) { return key == 'q' ? (128L << 16) | 1L : 0; }
void HiliteMenu(short id) { (void)id; }
Boolean WaitNextEvent(EventMask mask, EventRecord *event, unsigned long sleep, RgnHandle mouse)
{
    assert(mask == everyEvent && event != NULL); (void)sleep; (void)mouse;
    if (!contract_started) {
        contract_started = TRUE;
        if (failure_profile) {
            unsigned int before_polygons;
            assert(qd_ready && qd_world != NULL);
            SetPort(&desktop_port);
            SetGDevice(&screen_device_ptr);
            fail_polygon = failure_profile == 1;
            fail_pattern = failure_profile == 2;
            before_polygons = polygon_calls;
            drv_render();
            assert(qd_ready && qd_running);
            assert(alert_calls == 0 && !world_locked && !qd_rendering);
            if (failure_profile == 1) {
                assert(qd_frame_paused && polygon_calls == before_polygons + 2U);
                assert(qd_world == NULL && qd_pending_polygon == NULL);
                assert(current_port == &desktop_port && current_device == &screen_device_ptr);
                osd_text[0] = 0;
                drv_render();
                assert(polygon_calls == before_polygons + 2U);
                assert(strstr(osd_text, "paused") != NULL);
                fail_polygon = FALSE;
                qd_keyboard('1');
                assert(!qd_frame_paused);
                recorded_count = 0; osd_text[0] = 0;
                drv_render();
                assert(recorded_count != 0 && !drv_frame_failed);
                qd_make_palette();
                qd_make_buffer();
                puts("PASS QuickDraw polygon failure: two bounded attempts unwind ports, then pause; a key retries successfully without an alert");
            } else {
                assert(qd_pattern_unavailable);
                fail_pattern = FALSE;
                puts("PASS QuickDraw pattern failure: nearest-color drawing continues without an alert");
            }
        }
        run_contract();
    }
    recorded_count = 0;
    memset(event, 0, sizeof(*event));
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
            assert(!drv_auto_enabled && drv_rot_y == loop_yaw && sleep == 30UL);
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
            assert(!drv_lmouse_pressed && sleep == 30UL);
            find_part = inGoAway;
            if (quit_using_key) { event->what = keyDown; event->message = 'q'; event->modifiers = cmdKey; }
            else { event->what = mouseDown; event->where.h = 1; event->where.v = 1; }
            return TRUE;
        default: assert(!"QuickDraw driver did not close its window"); return FALSE;
    }
}
short FindWindow(Point p, WindowPtr *window) { (void)p; *window = window_live; return find_part; }
void DragWindow(WindowPtr w, Point p, const Rect *bounds) { assert(w == window_live && bounds != NULL); (void)p; }
long GrowWindow(WindowPtr w, Point p, const Rect *bounds) { assert(w == window_live && bounds != NULL); (void)p; return (600L << 16) | 800L; }
void SizeWindow(WindowPtr w, short width, short height, Boolean update)
{ assert(w == window_live && width > 0 && height > 0); (void)update; window_port.portRect.right = width; window_port.portRect.bottom = height; }
Boolean TrackGoAway(WindowPtr w, Point p) { assert(w == window_live); (void)p; return TRUE; }
void BeginUpdate(WindowPtr w) { assert(w == window_live); }
void EndUpdate(WindowPtr w) { assert(w == window_live); }
void SystemClick(const EventRecord *event, WindowPtr w) { assert(event != NULL); (void)w; }
void SystemTask(void) {}
Boolean Button(void) { return mouse_down != 0; }
void GetMouse(Point *p) { assert(p != NULL); *p = mouse_position; }
void GlobalToLocal(Point *p) { assert(p != NULL); }
void LocalToGlobal(Point *p) { assert(p != NULL); }
void ParamText(ConstStr255Param a, ConstStr255Param b, ConstStr255Param c, ConstStr255Param d)
{
    (void)b; (void)c; (void)d; assert(a != NULL);
    fprintf(stderr, "Unexpected Toolbox error: %.*s\n", (int)a[0], a + 1);
}
short StopAlert(short id, void *filter)
{
    unsigned int previous_texts;
    EventRecord event;
    assert(id == 128 && filter == NULL); ++alert_calls;
    assert(!qd_ready && !qd_running && !qd_active && !qd_rendering);
    assert(!world_locked && qd_locked_pixels == NULL && qd_pending_polygon == NULL);
    assert(poly_count == 0 && recording_poly == NULL && !pen_hidden);
    previous_texts = text_calls;
    memset(&event, 0, sizeof(event));
    event.what = updateEvt; event.message = (long)qd_window;
    qd_event(&event); drv_render(); drv_draw_osd();
    assert(text_calls == previous_texts);
    return 1;
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
        qd_fpu_transform(3, source);
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
        qd_fpu_transform(4U, source);
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
    unsigned int i;
    if (!qd_has_fpu) return;
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
    for (i = 0; i < 7U; ++i) {
        assert(qd_fpu_prepare(&zero, &zero));
        qd_fpu_transform(3U, vertices);
        assert(qd_fpu_project(0U, 1U, 2U, 640U, 480U, projected) == 3);
        assert(!qd_fpu_prepare(bad + i, &zero));
        qd_fpu_transform(3U, vertices);
        assert(qd_fpu_project(0U, 1U, 2U, 640U, 480U, projected) == 0);
        assert(!qd_fpu_prepare(&zero, bad + i));
    }
    assert(!qd_fpu_prepare(NULL, &zero));
    assert(!qd_fpu_prepare(&zero, NULL));
    assert(qd_fpu_prepare(&last, &last));
    qd_fpu_transform(3U, vertices);
    assert(qd_fpu_project(0U, 1U, 2U, 640U, 480U, projected) == 3);
    puts("PASS QuickDraw FPU angles: normalized endpoints accepted, huge/nonfinite/null inputs rejected without loops or reuse of a stale transform");
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
    assert(qd_world != NULL && world_device != qd_device);
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

static void check_osd_boundaries(void)
{
    unsigned int old_width, old_height, old_texts;
    int old_failure;
    Int next;
    Int (*wide_text)(const char *, Int, short, int);
    old_width = drv_win_width; old_height = drv_win_height;
    old_failure = fail_gworld; fail_gworld = TRUE;
    SizeWindow(qd_window, 32767, 32767, TRUE);
    qd_resize(32767, 32767);
    SetPort((GrafPtr)qd_window);
    if (has_color) SetGDevice(qd_device);
    osd_text[0] = 0;
    old_texts = text_calls;
    wide_text = qd_osd_text;
    next = wide_text("boundary", 32767L, 32751, TRUE);
    assert(next == 32779L && text_calls == old_texts + 1U);
    next = wide_text("outside", next, 32751, TRUE);
    assert(next == 32791L && text_calls == old_texts + 1U);
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
    assert(qd_device == &screen_device_ptr);
    assert((qd_palette != NULL) == (changed_depth <= 8));
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
    assert((qd_palette != NULL) == (original_depth <= 8));
    puts("PASS QuickDraw display refresh: resume and activation rebuild palette/GWorld policy for changed device depth and restore the selected display");
}

static void check_render_reentry(void)
{
    unsigned int old_width, old_height;
    int old_osd;
    GrafPtr port;
    GDHandle device;
    old_width = drv_win_width; old_height = drv_win_height;
    old_osd = drv_osd_enabled;
    GetPort(&port); device = has_color ? GetGDevice() : NULL;
    event_key('1', 0);
    drv_osd_enabled = FALSE;
    reentry_width = old_width - 16U;
    reentry_requested = TRUE;
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    assert(!reentry_requested && reentry_calls == 1U);
    assert(globe_longitude == 32 && drv_win_width == old_width);
    assert(qd_pending_count == 4U && qd_pending_resize && qd_pending_refresh);
    assert(current_port == port && current_device == device && !world_locked);
    SizeWindow(qd_window, (short)reentry_width, (short)old_height, TRUE);
    recorded_count = 0; osd_text[0] = 0;
    qd_apply_pending();
    assert(globe_longitude == 64 && drv_win_width == reentry_width);
    assert(!drv_osd_enabled && qd_pending_count == 0U);
    assert(!qd_pending_resize && !qd_pending_refresh && !qd_rendering);
    assert(current_port == port && current_device == device && !world_locked);
    SizeWindow(qd_window, (short)old_width, (short)old_height, TRUE);
    drv_fps_average = 99.0f;
    qd_resize((int)old_width, (int)old_height);
    assert(drv_fps_average == 0.0f);
    drv_osd_enabled = old_osd;
    puts("PASS QuickDraw reentry: native fill callbacks defer resolution, toggles, resize and updates until mesh borrows and native ports are released; resize resets timing");
}

static void check_recoverable_resources(void)
{
    unsigned char bytes[512];
    unsigned int handles, texts, calls;
    int old_osd, i;
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
    if (has_color && qd_depth <= 8) {
        qd_dispose_buffer();
        qd_dispose_palette();
        handles = handle_count;
        fail_color_table = TRUE;
        qd_make_palette();
        fail_color_table = FALSE;
        assert(qd_colors == NULL && qd_palette == NULL && qd_palette_reduced);
        assert(handle_count == handles);
        fail_palette = TRUE;
        qd_make_palette();
        fail_palette = FALSE;
        assert(qd_colors == NULL && qd_palette == NULL && qd_palette_reduced);
        assert(handle_count == handles);
        qd_make_buffer();
        recorded_count = 0; osd_text[0] = 0;
        drv_render();
        assert(recorded_count != 0 && alert_calls == 0);
        if (qd_depth > 1) {
            if (qd_pattern != NULL) DisposePixPat(qd_pattern);
            qd_pattern = NULL;
            qd_pattern_unavailable = FALSE;
            calls = pattern_calls;
            fail_pattern = TRUE;
            recorded_count = 0; osd_text[0] = 0;
            drv_render();
            assert(recorded_count != 0 && qd_pattern_unavailable);
            assert(pattern_calls == calls + 1U && qd_pattern == NULL);
            recorded_count = 0; osd_text[0] = 0;
            drv_render();
            assert(recorded_count != 0 && pattern_calls == calls + 1U);
            fail_pattern = FALSE;
        }
    }
    fail_polygon = 2;
    calls = polygon_calls;
    recorded_count = 0; osd_text[0] = 0;
    drv_render();
    assert(recorded_count != 0 && !qd_frame_paused && !drv_frame_failed);
    assert(polygon_calls > calls + 1U && !world_locked && !qd_rendering);
    assert(current_port == saved_port && current_device == saved_device);
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
    drv_osd_enabled = old_osd;
    if (has_color && qd_depth <= 8) qd_make_palette();
    qd_make_buffer();
    assert(current_port == saved_port && current_device == saved_device);
    puts("PASS QuickDraw recovery: resource errors release handles; palette/pattern failures and optional-buffer release keep drawing; empty scenes show status and accept a resolution retry");
}

static int run_contract(void)
{
    int resolution, pose, i;
    float old_x, old_y;
    unsigned int texts, palettes;
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
    assert(qd_world != NULL || !has_color || fail_gworld || qd_version < gestalt32BitQD);
    check_fpu_angles();
    drv_osd_enabled = FALSE;
    saved = current_port;
    for (resolution = 0; resolution < 2; ++resolution) {
        event_key((unsigned char)('1' + resolution), 0);
        assert(globe_longitude == (resolution == 0 ? 32 : 64));
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
        event_key((unsigned char)('1' + resolution), 0);
        assert(globe_longitude == (32 << resolution));
        assert(globe_verts_num * sizeof(Vertex) > 65535UL);
        recorded_count = 0;
        drv_render();
        assert(recorded_count == globe_quads_num_vis);
        assert(recorded_count > 0 && recorded_count <= globe_quads_num);
    }
    streaming_polygons = FALSE;
    event_key('6', 0); assert(globe_longitude == 512);
    check_quad_batches();
    check_osd_boundaries();
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
    if (has_color && display_depth <= 8) assert(palette_calls > palettes);
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
    if (!has_color) assert(color_calls == 0 && world_calls == 0 && palette_calls == 0);
    else if (!fail_gworld && !fail_lock && qd_version >= gestalt32BitQD) assert(copy_calls > 0 && world_calls >= 3);
    if (fail_gworld || fail_lock || qd_version < gestalt32BitQD) assert(copy_calls == 0);
    check_render_reentry();
    check_recoverable_resources();
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
    has_color = strcmp(argv[1], "mono") != 0;
    if (strcmp(argv[1], "polyfail") == 0) failure_profile = 1;
    if (strcmp(argv[1], "patternfail") == 0) failure_profile = 2;
    display_depth = has_color ? (short)atoi(argv[1]) : 1;
    if (display_depth == 0) display_depth = 8;
    multiple_devices = strcmp(argv[1], "multi") == 0;
    if (multiple_devices) display_depth = 32;
    qd_version = has_color ? gestalt32BitQD : gestaltOriginalQD;
    fail_gworld = strcmp(argv[1], "nogworld") == 0;
    fail_lock = strcmp(argv[1], "lockfail") == 0;
    if (strcmp(argv[1], "oldcolor") == 0) qd_version = gestalt8BitQD;
    if (strcmp(argv[1], "fpu") == 0) { cpu_type = gestalt68030; fpu_type = gestalt68882; }
    if (strcmp(argv[1], "falsefpu") == 0) fpu_type = gestalt68881;
    quit_using_key = strcmp(argv[1], "fpu") == 0;
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
    assert(ptr_count == 0 && handle_count == 0 && poly_count == 0 && menu_count == 0);
    assert(world_count == 0 && palette_count == 0 && window_count == 0 && !world_locked);
    assert(pixpat_count == 0 && !pen_hidden);
    assert(alert_calls == 0);
    old_texts = text_calls;
    assert(!qd_ready && !qd_running);
    drv_render(); drv_draw_osd(); event_key('1', 0);
    assert(text_calls == old_texts && globe_verts_num == 0);
    assert(current_device == (has_color ? &screen_device_ptr : NULL));
    puts("PASS QuickDraw cleanup: polygon/heap/palette/menu/window/GWorld ownership and close path");
    return 0;
}
