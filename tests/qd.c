/* Execute the Classic Macintosh driver against Toolbox call records.
 * This is NOT an MPW SDK, ABI validation, or QuickDraw renderer. */
#include <assert.h>
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

#include "../src/drv_qd.c"
#define main testglobe_main
#include "../src/main.c"
#undef main

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
static PixMap world_pixmap;
static PixMap *world_pixmap_ptr = &world_pixmap;
static int world_locked;
static short display_depth = 8;
static int has_color = TRUE;
static long qd_version = gestalt32BitQD;
static long cpu_type = gestalt68000, fpu_type = gestaltNoFPU;
static int fail_gworld;
static int fail_lock;
static short find_part = inGoAway;
static unsigned long ticks;
static unsigned int window_calls, color_calls, world_calls, copy_calls;
static unsigned int palette_calls, text_calls, alert_calls;
static unsigned int ptr_count, handle_count, poly_count, menu_count;
static unsigned int world_count, palette_count, window_count;
static void *memory_blocks[64];
static void *handle_blocks[64];
static PaletteHandle window_palette;
static RGBColor foreground, background;
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

typedef struct {
    int count;
    Point points[12];
    unsigned long color;
    unsigned int pattern_black;
} RecordedPolygon;
static RecordedPolygon recorded[5000];

static void color_api(void) { assert(has_color); ++color_calls; }
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
    handle = (Handle)malloc(sizeof(Ptr));
    assert(handle != NULL);
    *handle = (Ptr)calloc((size_t)size, 1);
    assert(*handle != NULL);
    for (i = 0; i < 64; ++i) {
        if (handle_blocks[i] != NULL) continue;
        handle_blocks[i] = handle; ++handle_count;
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
        assert(handle != NULL && handle_count > 0);
        free(*handle); free(handle); handle_blocks[i] = NULL; --handle_count;
        return;
    }
    assert(!"Unknown Macintosh handle");
}
void HLock(Handle h) { assert(h != NULL); }
void HUnlock(Handle h) { assert(h != NULL); }
OSErr MemError(void) { return noErr; }
unsigned long TickCount(void) { ticks = (ticks + 1UL) & 0xffffffffUL; return ticks; }
void InitGraf(GrafPtr *port)
{
    memset(&desktop_port, 0, sizeof(desktop_port));
    desktop_port.portRect.right = 1024; desktop_port.portRect.bottom = 768;
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
    (void)table; (void)flags;
    assert(device == (multiple_devices ? &second_device_ptr : &screen_device_ptr));
    if (fail_gworld) { *world = NULL; return memFullErr; }
    world_live = (GWorldPtr)calloc(1, sizeof(CGrafPort));
    assert(world_live != NULL);
    world_live->portRect = *bounds;
    world_live->portBits.bounds = *bounds;
    world_pixmap.bounds = *bounds; world_pixmap.pixelSize = display_depth;
    world_pixmap.rowBytes = (short)(((bounds->right - bounds->left) * display_depth + 7) / 8);
    world_pixmap.baseAddr = (Ptr)calloc((size_t)(world_pixmap.rowBytes & 0x3fff) *
                                      (bounds->bottom - bounds->top), 1);
    assert(world_pixmap.baseAddr != NULL);
    world_live->portPixMap = &world_pixmap_ptr;
    *world = world_live; world_device = device; ++world_count;
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
static void record_polygon(PolyHandle poly, unsigned int black)
{
    RecordedPolygon *entry;
    int i, count;
    assert(poly != NULL && current_port != NULL && recorded_count < 5000);
    count = pending_count;
    if (count > 3 && pending_points[count - 1].h == pending_points[0].h &&
        pending_points[count - 1].v == pending_points[0].v) --count;
    assert(count >= 3 && count <= 10);
    entry = recorded + recorded_count++;
    entry->count = count; entry->color = rgb_value(&foreground); entry->pattern_black = black;
    for (i = 0; i < count; ++i) {
        entry->points[i] = pending_points[i];
        assert(entry->points[i].h >= 0 && entry->points[i].h <= (int)drv_win_width);
        assert(entry->points[i].v >= 0 && entry->points[i].v <= (int)drv_win_height);
    }
}
void PaintPoly(PolyHandle p) { assert(has_color); record_polygon(p, 64U); }
void FillPoly(PolyHandle p, const Pattern *pattern) { assert(pattern != NULL); record_polygon(p, pattern_bits(pattern->pat)); }
void PenSize(short h, short v) { assert(h >= 0 && v >= 0); }
void PenNormal(void) {}
void HidePen(void) { assert(!pen_hidden); pen_hidden = TRUE; }
void ShowPen(void) { assert(pen_hidden); pen_hidden = FALSE; }
PixPatHandle NewPixPat(void) { color_api(); ++pixpat_count; return (PixPatHandle)NewHandle((Size)sizeof(PixPat)); }
void DisposePixPat(PixPatHandle p) { color_api(); assert(pixpat_count > 0); DisposeHandle((Handle)p); --pixpat_count; }
void MakeRGBPat(PixPatHandle p, const RGBColor *c) { color_api(); assert(p != NULL && c != NULL && current_device == qd_device); (*p)->color = *c; }
void FillCPoly(PolyHandle p, PixPatHandle pattern) { color_api(); assert(pattern != NULL); foreground = (*pattern)->color; record_polygon(p, 64U); }
void ForeColor(long color)
{
    assert(color == blackColor || color == whiteColor);
    foreground.red = foreground.green = foreground.blue = color == blackColor ? 0 : 65535U;
}
void BackColor(long color)
{
    assert(color == blackColor || color == whiteColor);
    background.red = background.green = background.blue = color == blackColor ? 0 : 65535U;
}
void RGBForeColor(const RGBColor *color) { color_api(); assert(color != NULL && current_device == qd_device); foreground = *color; }
void RGBBackColor(const RGBColor *color) { color_api(); assert(color != NULL && current_device == qd_device); background = *color; }
void TextFont(short font) { (void)font; }
void TextSize(short size) { assert(size > 0); }
void TextMode(short mode) { (void)mode; }
void DrawText(const void *text, short start, short count)
{
    assert(text != NULL && start >= 0 && count > 0); ++text_calls;
    assert(strstr((const char *)text + start, "texture") == NULL);
}
void DrawString(ConstStr255Param text) { assert(text != NULL && text[0] > 0); ++text_calls; }
void CopyBits(const BitMap *source, const BitMap *destination, const Rect *a,
                const Rect *b, short mode, RgnHandle mask)
{
    assert(has_color && source != NULL && destination != NULL && a != NULL && b != NULL);
    assert(mode == srcCopy || mode == (srcCopy | ditherCopy)); (void)mask;
    assert(source->bounds.right == a->right && source->bounds.bottom == a->bottom);
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
    if (!contract_started) { contract_started = TRUE; run_contract(); }
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
            event->what = keyDown; event->message = 'a'; loop_yaw = drv_rot_y;
            return TRUE;
        case 3:
            assert(!drv_auto_enabled && drv_rot_y == loop_yaw && sleep == 30UL);
            find_part = inContent; mouse_down = TRUE;
            mouse_position.h = 300; mouse_position.v = 200;
            event->what = mouseDown; event->where.h = 280; event->where.v = 180;
            return TRUE;
        case 4:
            assert(drv_lmouse_pressed && !drv_auto_enabled && sleep == 1UL);
            loop_yaw = drv_rot_y;
            mouse_position.h += 10; mouse_position.v += 5;
            return FALSE;
        case 5:
            assert(drv_lmouse_pressed && sleep == 1UL && drv_rot_y != loop_yaw);
            mouse_down = FALSE; event->what = mouseUp;
            return TRUE;
        case 6:
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
{ (void)b; (void)c; (void)d; assert(a != NULL); fprintf(stderr, "Unexpected Toolbox error: %.*s\n", (int)a[0], a + 1); }
short StopAlert(short id, void *filter) { assert(id == 128 && filter == NULL); ++alert_calls; return 1; }
void SysBeep(short duration) { (void)duration; }
void ExitToShell(void) { assert(!"Unexpected ExitToShell"); }

typedef struct { double x, y; } ReferencePoint;
typedef struct {
    ReferencePoint point[3];
    unsigned long color;
    double luminance;
    double area;
    int seen;
} ReferenceTriangle;
static ReferenceTriangle reference[5000];
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

static void add_reference(unsigned long a, unsigned long b, unsigned long c,
                           unsigned char color)
{
    ReferenceTriangle *triangle;
    ReferencePoint *p;
    assert(reference_count < 5000);
    triangle = reference + reference_count++;
    triangle->point[0] = project_reference(globe_verts + a);
    triangle->point[1] = project_reference(globe_verts + b);
    triangle->point[2] = project_reference(globe_verts + c);
    p = triangle->point;
    triangle->area = (p[1].x - p[0].x) * (p[2].y - p[0].y) -
                     (p[1].y - p[0].y) * (p[2].x - p[0].x);
    triangle->color = ((unsigned long)earth_cmap[color][0] << 16) |
                      ((unsigned long)earth_cmap[color][1] << 8) | earth_cmap[color][2];
    triangle->luminance = (0.299 * earth_cmap[color][0] +
                            0.587 * earth_cmap[color][1] +
                            0.114 * earth_cmap[color][2]) / 255.0;
    triangle->seen = FALSE;
}

static void build_reference(void)
{
    int latitude, longitude;
    unsigned long top, bottom, face;
    unsigned char *data;
    data = globe_longitude == 32 ? earth_data_32 : earth_data_64;
    reference_count = 0;
    for (latitude = 0; latitude < globe_latitude; ++latitude) {
        for (longitude = 0; longitude < globe_longitude; ++longitude) {
            top = (unsigned long)latitude * (globe_longitude + 1UL) + longitude;
            bottom = top + globe_longitude + 1UL;
            face = (unsigned long)latitude * globe_longitude + longitude;
            if (latitude != globe_latitude - 1)
                add_reference(top + 1UL, bottom, bottom + 1UL, data[face]);
            if (latitude != 0)
                add_reference(top, bottom, top + 1UL, data[face]);
        }
    }
    assert(reference_count == globe_tris_num);
}

static double reference_distance(const RecordedPolygon *actual,
                                  const ReferenceTriangle *expected)
{
    int shift, point, next, close;
    double distance, best, dx, dy;
    best = 1e30;
    if (actual->count != 3) return best;
    if (has_color && display_depth > 1) {
        if (actual->color != expected->color) return best;
    } else if (fabs((double)actual->pattern_black -
                     64.0 * (1.0 - expected->luminance)) > 2.0) return best;
    for (shift = 0; shift < 3; ++shift) {
        close = TRUE;
        distance = 0;
        for (point = 0; point < 3; ++point) {
            next = (point + shift) % 3;
            dx = actual->points[point].h - expected->point[next].x;
            dy = actual->points[point].v - expected->point[next].y;
            if (fabs(dx) > 3.0 || fabs(dy) > 3.0)
                close = FALSE;
            distance += dx * dx + dy * dy;
        }
        if (close && distance < best) best = distance;
    }
    return best;
}

static void check_projection(void)
{
    unsigned int polygon, triangle, found, matched;
    long area;
    double distance, best, dx, dy, longest, length;
    int point, next;
    matched = 0;
    for (polygon = 0; polygon < recorded_count; ++polygon) {
        assert(recorded[polygon].count == 3);
        area = (long)(recorded[polygon].points[1].h - recorded[polygon].points[0].h) *
               (recorded[polygon].points[2].v - recorded[polygon].points[0].v) -
               (long)(recorded[polygon].points[1].v - recorded[polygon].points[0].v) *
               (recorded[polygon].points[2].h - recorded[polygon].points[0].h);
        assert(area < 0);
        found = reference_count;
        best = 1e30;
        for (triangle = 0; triangle < reference_count; ++triangle) {
            distance = reference_distance(recorded + polygon, reference + triangle);
            if (distance < best) {
                best = distance;
                found = triangle;
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
    for (triangle = 0; triangle < reference_count; ++triangle) {
        longest = 0;
        for (point = 0; point < 3; ++point) {
            next = (point + 1) % 3;
            dx = reference[triangle].point[point].x - reference[triangle].point[next].x;
            dy = reference[triangle].point[point].y - reference[triangle].point[next].y;
            length = sqrt(dx * dx + dy * dy);
            if (length > longest) longest = length;
        }
        if (reference[triangle].area < -4.0 * longest && !reference[triangle].seen) {
            /* Adjacent subpixel facets can have identical rounded tuples;
             * coverage matters more than a greedy one-to-one assignment. */
            for (polygon = 0; polygon < recorded_count; ++polygon) {
                if (reference_distance(recorded + polygon, reference + triangle) < 1e30) {
                    reference[triangle].seen = TRUE;
                    break;
                }
            }
            assert(reference[triangle].seen);
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
        qd_fpu_prepare(&zero, &zero);
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
static void check_clipping(void)
{
    Point polygon[10];
    int count, i;
    camera_triangle(-9000.0, -9000.0, -11000.0);
    assert(qd_prepare_polygon(0, 1, 2, polygon) == 4);
    camera_triangle(-11000.0, -11000.0, -11000.0);
    assert(qd_prepare_polygon(0, 1, 2, polygon) == 0);
    camera_triangle(0.0, 0.0, 0.0);
    assert(qd_prepare_polygon(0, 1, 2, polygon) == 0);
    camera_triangle(-2.0, -2.0, -0.5);
    count = qd_prepare_polygon(0, 1, 2, polygon);
    assert(count >= 3 && count <= 10);
    for (i = 0; i < count; ++i) {
        assert(polygon[i].h >= 0 && polygon[i].h <= (int)drv_win_width);
        assert(polygon[i].v >= 0 && polygon[i].v <= (int)drv_win_height);
    }
    assert(labs(qd_mul_ratio(536000000L, 412000000L, 536000000L) - 412000000L) <= 1L);
}
static void event_key(unsigned char key, short modifiers)
{
    EventRecord event;
    memset(&event, 0, sizeof(event));
    event.what = keyDown; event.message = key; event.modifiers = modifiers;
    qd_event(&event);
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
    assert(drv_win_width == 640 && drv_win_height == 480);
    assert(qd_world != NULL || !has_color || fail_gworld || qd_version < gestalt32BitQD);
    drv_osd_enabled = FALSE;
    saved = current_port;
    for (resolution = 0; resolution < 2; ++resolution) {
        event_key((unsigned char)('1' + resolution), 0);
        assert(globe_longitude == (resolution == 0 ? 32 : 64));
        for (pose = 0; pose < 4; ++pose) {
            drv_rot_x = (float)pitch[pose]; drv_rot_y = (float)yaw[pose];
            build_reference(); recorded_count = 0;
            drv_render();
            assert(recorded_count == globe_tris_num_vis);
            assert(current_port == saved && !world_locked);
            assert(current_device == (has_color ? &screen_device_ptr : NULL));
            check_projection();
        }
        for (i = '3'; i <= '5'; ++i) event_key((unsigned char)i, 0);
        assert(globe_longitude == (resolution == 0 ? 32 : 64));
        event_key('t', 0); assert(!drv_texture_enabled);
    }
    check_clipping();
    puts("PASS QuickDraw rendering: two globe resolutions and eight poses, independent perspective/color/dither references, FPU/fixed paths, clipping and native fills");
    old_x = drv_rot_x; old_y = drv_rot_y;
    memset(&event, 0, sizeof(event)); event.what = mouseDown;
    event.where.h = 320; event.where.v = 240; find_part = inContent;
    qd_event(&event); assert(drv_lmouse_pressed);
    qd_mouse_move(384, 288);
    assert(drv_rot_x != old_x && drv_rot_y != old_y);
    memset(&event, 0, sizeof(event)); event.what = mouseUp;
    qd_event(&event); assert(!drv_lmouse_pressed);
    texts = text_calls; event_key('o', 0); assert(drv_osd_enabled);
    memset(&event, 0, sizeof(event)); event.what = updateEvt; event.message = (long)qd_window;
    recorded_count = 0; qd_event(&event); assert(text_calls == texts + 2);
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
    memset(&event, 0, sizeof(event)); event.what = mouseDown; find_part = inGrow;
    qd_event(&event); find_part = inGoAway;
    assert(drv_win_width == 800 && drv_win_height == 600);
    build_reference(); recorded_count = 0; drv_render(); check_projection();
    drv_resize(1, 1); camera_triangle(-2.0, -2.0, -0.5);
    {
        Point tiny[10]; assert(qd_prepare_polygon(0, 1, 2, tiny) >= 3);
    }
    SizeWindow(qd_window, 800, 600, TRUE); drv_resize(800, 600);
    ticks = 0xfffffff0UL;
    for (i = 0; i < 61; ++i) { drv_fps_start(); drv_fps_stop(); }
    assert(fabs(drv_fps_average - 60.0f) < 0.01);
    if (!has_color) assert(color_calls == 0 && world_calls == 0 && palette_calls == 0);
    else if (!fail_gworld && !fail_lock && qd_version >= gestalt32BitQD) assert(copy_calls > 0 && world_calls >= 3);
    if (fail_gworld || fail_lock || qd_version < gestalt32BitQD) assert(copy_calls == 0);
    puts("PASS QuickDraw events: actual common startup, keyboard/mouse, update/activate/suspend, resize, 1x1 viewport, depth handling, port restoration and TickCount rollover");
    return 0;
}
int main(int argc, char **argv)
{
    char *arguments[2];
    assert(argc == 2);
    has_color = strcmp(argv[1], "mono") != 0;
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
    screen_pixmap.bounds.right = 1024; screen_pixmap.bounds.bottom = 768;
    screen_device.gdPMap = &screen_pixmap_ptr;
    screen_device.gdRect = screen_pixmap.bounds;
    if (multiple_devices) {
        screen_pixmap.pixelSize = 4;
        second_pixmap.pixelSize = 32; second_pixmap.bounds = screen_pixmap.bounds;
        second_device.gdPMap = &second_pixmap_ptr; second_device.gdRect = screen_pixmap.bounds;
        second_device.gdRect.left = 1024; second_device.gdRect.right = 2048;
        screen_device.gdNextGD = &second_device_ptr;
    }
    arguments[0] = "testglobe"; arguments[1] = NULL;
    assert(testglobe_main(1, arguments) == 0);
    assert(contract_started && window_live == NULL);
    assert(ptr_count == 0 && handle_count == 0 && poly_count == 0 && menu_count == 0);
    assert(world_count == 0 && palette_count == 0 && window_count == 0 && !world_locked);
    assert(pixpat_count == 0 && !pen_hidden);
    assert(alert_calls == 0);
    assert(current_device == (has_color ? &screen_device_ptr : NULL));
    puts("PASS QuickDraw cleanup: polygon/heap/palette/menu/window/GWorld ownership and close path");
    return 0;
}
