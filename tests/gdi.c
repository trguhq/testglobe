/* Execute the real Win16 driver against recorded Windows calls.
 * This host contract is not a Windows SDK, ABI check, or renderer. */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "testglob.h"
#include "drv.h"
#include "types.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"
#include "earth_resources.h"
#include "soft3d_cases.h"

#include "../src/drv_gdi.c"

extern Vertex TG_HUGE *globe_verts;
extern int globe_longitude, globe_latitude;

enum { OBJ_NONE, OBJ_DC, OBJ_BITMAP, OBJ_BRUSH, OBJ_PEN, OBJ_PALETTE,
       OBJ_MEMORY, OBJ_WINDOW, OBJ_FONT, OBJ_RESOURCE };
typedef struct {
    int kind;
    void *memory;
    DWORD size;
    COLORREF color;
    HGDIOBJ brush, pen, bitmap, font;
    HPALETTE palette;
    int memory_dc;
    unsigned int locks;
    BOOL palette_background;
} RecordedObject;
static RecordedObject objects[1024];
static unsigned int resource_count, allocated_count;
static unsigned long maximum_allocation, loaded_chunks;
static DWORD windows_flags, ticks;
static UINT display_depth = 8;
static int client_width = 640, client_height = 480;
static int screen_width = 800, screen_height = 600;
static unsigned int created_windows;
static HWND main_window, capture_window;
static WNDPROC window_callback;
static unsigned int palette_uploads, palette_realizations, text_calls;
static unsigned int bitblts, polygon_calls, polypolygon_calls, timer_calls;
static unsigned int invalidations;
static unsigned int destroyed_windows, proc_instances, quit_messages;
static int quit_code;
static unsigned long submitted_polygons, submitted_vertices;
static unsigned int recorded_count;
static int stream_polygons;
static char last_status[200];
typedef struct { unsigned char color; int count; POINT points[12]; } RecordedPolygon;
static RecordedPolygon recorded[5000];
static int run_contract(void);
static int contract_started;
static int close_with_escape;
static BOOL last_realize_background;
static const char *profile;
static unsigned int fail_allocation, allocation_attempts;
static int fail_polygon;
static unsigned int fatal_dialogs;

static HANDLE object_new(int kind)
{
    HANDLE handle;
    for (handle = 1; handle < 900; ++handle) {
        if (objects[handle].kind != OBJ_NONE) continue;
        memset(objects + handle, 0, sizeof(objects[handle]));
        objects[handle].kind = kind;
        ++resource_count;
        return handle;
    }
    assert(!"GDI object leak");
    return 0;
}

static void object_delete(HANDLE handle)
{
    assert(handle > 0 && handle < 900);
    assert(objects[handle].kind != OBJ_NONE && resource_count > 0);
    assert(objects[handle].locks == 0);
    free(objects[handle].memory);
    memset(objects + handle, 0, sizeof(objects[handle]));
    --resource_count;
}

DWORD GetWinFlags(void) { return windows_flags; }
DWORD GetTickCount(void) { ticks = (ticks + 16UL) & 0xffffffffUL; return ticks; }
HGLOBAL GlobalAlloc(UINT flags, DWORD bytes)
{
    HGLOBAL handle;
    assert(bytes > 0 && (flags & GMEM_MOVEABLE));
    if (fail_allocation != 0 && ++allocation_attempts == fail_allocation)
        return 0;
    handle = object_new(OBJ_MEMORY);
    objects[handle].memory = calloc((size_t)bytes, 1);
    assert(objects[handle].memory != NULL);
    objects[handle].size = bytes;
    if (bytes > maximum_allocation) maximum_allocation = bytes;
    ++allocated_count;
    return handle;
}
LPVOID GlobalLock(HGLOBAL handle)
{
    assert(objects[handle].kind == OBJ_MEMORY);
    ++objects[handle].locks;
    return objects[handle].memory;
}
BOOL GlobalUnlock(HGLOBAL handle)
{
    assert(objects[handle].kind == OBJ_MEMORY ||
           objects[handle].kind == OBJ_RESOURCE);
    assert(objects[handle].locks > 0);
    --objects[handle].locks;
    return objects[handle].locks != 0;
}
HGLOBAL GlobalFree(HGLOBAL handle)
{
    assert(objects[handle].kind == OBJ_MEMORY && allocated_count > 0);
    --allocated_count;
    object_delete(handle);
    return 0;
}
DWORD GlobalSize(HGLOBAL handle)
{
    assert(objects[handle].kind == OBJ_MEMORY);
    return objects[handle].size;
}
HRSRC FindResource(HINSTANCE instance, LPCSTR name, LPCSTR type)
{
    unsigned char *data;
    unsigned long size;
    short id;
    assert(instance == 1 && type == RT_RCDATA);
    id = (short)(unsigned long)name;
    data = test_earth_resource(id, &size);
    assert(data != NULL && size > 0 && size <= TG_EARTH_CHUNK_SIZE);
    free(data);
    return (HRSRC)id;
}
DWORD SizeofResource(HINSTANCE instance, HRSRC resource)
{
    unsigned char *data;
    unsigned long size;
    assert(instance == 1);
    data = test_earth_resource((short)resource, &size);
    free(data);
    return size;
}
HGLOBAL LoadResource(HINSTANCE instance, HRSRC resource)
{
    HGLOBAL handle;
    unsigned long size;
    unsigned char *data;
    assert(instance == 1);
    data = test_earth_resource((short)resource, &size);
    assert(data != NULL && size <= TG_EARTH_CHUNK_SIZE);
    handle = object_new(OBJ_RESOURCE);
    objects[handle].memory = data;
    objects[handle].size = size;
    ++loaded_chunks;
    return handle;
}
LPSTR LockResource(HGLOBAL handle)
{
    assert(objects[handle].kind == OBJ_RESOURCE);
    ++objects[handle].locks;
    return (LPSTR)objects[handle].memory;
}
BOOL FreeResource(HGLOBAL handle)
{
    assert(objects[handle].kind == OBJ_RESOURCE);
    object_delete(handle);
    return FALSE;
}

FARPROC MakeProcInstance(FARPROC proc, HINSTANCE instance)
{
    assert(proc != NULL && instance == 1);
    ++proc_instances;
    return proc;
}
void FreeProcInstance(FARPROC proc)
{
    assert(proc != NULL && proc_instances > 0);
    --proc_instances;
}
ATOM RegisterClass(const WNDCLASS *window_class)
{
    assert(window_class->hInstance == 1 && window_class->lpfnWndProc != NULL);
    window_callback = window_class->lpfnWndProc;
    return 1;
}
BOOL UnregisterClass(LPCSTR name, HINSTANCE instance)
{
    assert(name != NULL && instance == 1);
    return 1;
}
HWND CreateWindow(LPCSTR class_name, LPCSTR title, DWORD style,
                  int x, int y, int width, int height, HWND parent,
                  HANDLE menu, HINSTANCE instance, LPVOID parameter)
{
    (void)style; (void)x; (void)y; (void)parameter;
    assert(class_name != NULL && title != NULL && parent == 0 && menu == 0);
    assert(instance == 1 && width > 0 && height > 0);
    assert(width <= 32767 && height <= 32767);
    client_width = width - 8; client_height = height - 28;
    main_window = object_new(OBJ_WINDOW);
    ++created_windows;
    return main_window;
}
BOOL DestroyWindow(HWND window)
{
    assert(window == main_window && objects[window].kind == OBJ_WINDOW);
    ++destroyed_windows;
    if (window_callback != NULL) window_callback(window, WM_DESTROY, 0, 0);
    object_delete(window);
    main_window = 0;
    return 1;
}
BOOL ShowWindow(HWND window, int show)
{
    assert(window == main_window);
    (void)show;
    return 1;
}
void UpdateWindow(HWND window) { assert(window == main_window); }
LONG DefWindowProc(HWND window, UINT message, WPARAM word, LPARAM parameter)
{
    (void)message; (void)word; (void)parameter;
    assert(window == main_window);
    return 0;
}
BOOL GetMessage(MSG *message, HWND window, UINT first, UINT last)
{
    (void)window; (void)first; (void)last;
    memset(message, 0, sizeof(*message));
    if (!contract_started) {
        contract_started = TRUE;
        assert(run_contract() == 0);
        message->hwnd = main_window;
        message->message = close_with_escape ? WM_CHAR : WM_CLOSE;
        message->wParam = close_with_escape ? 27 : 0;
        return 1;
    }
    message->message = WM_QUIT;
    message->wParam = (WPARAM)quit_code;
    return 0;
}
BOOL PeekMessage(MSG *message, HWND window, UINT first, UINT last, UINT flags)
{
    (void)message; (void)window; (void)first; (void)last; (void)flags;
    return 0;
}
BOOL TranslateMessage(const MSG *message) { assert(message != NULL); return 1; }
LONG DispatchMessage(const MSG *message)
{
    assert(window_callback != NULL);
    return window_callback(message->hwnd, message->message, message->wParam, message->lParam);
}
void PostQuitMessage(int code) { ++quit_messages; quit_code = code; }
void InvalidateRect(HWND window, const RECT *rectangle, BOOL erase)
{
    assert(window == main_window);
    ++invalidations;
    (void)rectangle; (void)erase;
}
HDC GetDC(HWND window)
{
    HDC handle;
    assert(window == main_window || window == 0);
    handle = object_new(OBJ_DC);
    objects[handle].brush = GetStockObject(BLACK_BRUSH);
    objects[handle].pen = GetStockObject(NULL_PEN);
    objects[handle].bitmap = 903;
    objects[handle].palette = 904;
    return handle;
}
int ReleaseDC(HWND window, HDC dc)
{
    assert(window == main_window || window == 0);
    assert(objects[dc].kind == OBJ_DC);
    object_delete(dc);
    return 1;
}
HDC BeginPaint(HWND window, PAINTSTRUCT *paint)
{
    memset(paint, 0, sizeof(*paint));
    paint->hdc = GetDC(window);
    return paint->hdc;
}
void EndPaint(HWND window, const PAINTSTRUCT *paint)
{
    ReleaseDC(window, paint->hdc);
}
void GetClientRect(HWND window, RECT *rectangle)
{
    assert(window == main_window);
    rectangle->left = rectangle->top = 0;
    rectangle->right = (short)client_width;
    rectangle->bottom = (short)client_height;
}
void AdjustWindowRect(RECT *rectangle, DWORD style, BOOL menu)
{
    assert(rectangle != NULL && style != 0 && !menu);
    /* Native RECT fields are signed16. The production code must query
     * border offsets at zero, then add client dimensions in wider math. */
    assert(rectangle->left == 0 && rectangle->top == 0 &&
           rectangle->right == 0 && rectangle->bottom == 0);
    rectangle->left = -4; rectangle->top = -24;
    rectangle->right = 4; rectangle->bottom = 4;
}
HCURSOR LoadCursor(HINSTANCE instance, LPCSTR name)
{
    (void)instance; (void)name;
    return 910;
}
HICON LoadIcon(HINSTANCE instance, LPCSTR name)
{
    (void)instance; (void)name;
    return 911;
}
HDC CreateCompatibleDC(HDC source)
{
    HDC result;
    assert(objects[source].kind == OBJ_DC || source == 0);
    result = GetDC(main_window);
    objects[result].memory_dc = TRUE;
    return result;
}
BOOL DeleteDC(HDC dc)
{
    assert(objects[dc].kind == OBJ_DC);
    object_delete(dc);
    return 1;
}
HBITMAP CreateCompatibleBitmap(HDC dc, int width, int height)
{
    assert(objects[dc].kind == OBJ_DC && width > 0 && height > 0);
    return object_new(OBJ_BITMAP);
}
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object)
{
    HGDIOBJ previous;
    assert(objects[dc].kind == OBJ_DC && object > 0);
    switch (objects[object].kind) {
    case OBJ_BITMAP: previous = objects[dc].bitmap; objects[dc].bitmap = object; break;
    case OBJ_BRUSH: previous = objects[dc].brush; objects[dc].brush = object; break;
    case OBJ_PEN: previous = objects[dc].pen; objects[dc].pen = object; break;
    case OBJ_FONT: previous = objects[dc].font; objects[dc].font = object; break;
    default: assert(!"Unexpected GDI object selection"); previous = 0;
    }
    return previous;
}
BOOL DeleteObject(HGDIOBJ object)
{
    HANDLE i;
    assert(object > 0 && object < 900);
    for (i = 1; i < 900; ++i) {
        if (objects[i].kind != OBJ_DC) continue;
        assert(objects[i].bitmap != object && objects[i].brush != object);
        assert(objects[i].pen != object && objects[i].palette != object);
    }
    object_delete(object);
    return 1;
}
HGDIOBJ GetStockObject(int index)
{
    HANDLE handle;
    if (index == NULL_PEN) { handle = 900; objects[handle].kind = OBJ_PEN; }
    else if (index == BLACK_BRUSH) { handle = 901; objects[handle].kind = OBJ_BRUSH; }
    else if (index == WHITE_BRUSH) { handle = 902; objects[handle].kind = OBJ_BRUSH; }
    else { assert(index == SYSTEM_FONT); handle = 905; objects[handle].kind = OBJ_FONT; }
    objects[903].kind = OBJ_BITMAP;
    objects[904].kind = OBJ_PALETTE;
    return handle;
}
int GetDeviceCaps(HDC dc, int capability)
{
    assert(objects[dc].kind == OBJ_DC);
    switch (capability) {
    case HORZRES: return screen_width;
    case VERTRES: return screen_height;
    case RASTERCAPS: return RC_PALETTE;
    case BITSPIXEL: return display_depth;
    case PLANES: return 1;
    case NUMCOLORS: case SIZEPALETTE: return 256;
    default: assert(!"Unexpected device capability"); return 0;
    }
}
HPALETTE CreatePalette(const LOGPALETTE *palette)
{
    unsigned int i;
    assert(palette->palVersion == 0x300 && palette->palNumEntries == 256);
    for (i = 0; i < 256; ++i) {
        assert(palette->palPalEntry[i].peRed == earth_cmap[i][0]);
        assert(palette->palPalEntry[i].peGreen == earth_cmap[i][1]);
        assert(palette->palPalEntry[i].peBlue == earth_cmap[i][2]);
    }
    ++palette_uploads;
    return object_new(OBJ_PALETTE);
}
HPALETTE SelectPalette(HDC dc, HPALETTE palette, BOOL background)
{
    HPALETTE previous;
    (void)background;
    assert(objects[dc].kind == OBJ_DC && objects[palette].kind == OBJ_PALETTE);
    previous = objects[dc].palette;
    objects[dc].palette = palette;
    objects[dc].palette_background = background;
    return previous;
}
UINT RealizePalette(HDC dc)
{
    assert(objects[dc].kind == OBJ_DC);
    assert(!objects[dc].memory_dc);
    last_realize_background = objects[dc].palette_background;
    ++palette_realizations;
    return 256;
}
BOOL UnrealizeObject(HGDIOBJ object) { assert(object > 0); return 1; }
HBRUSH CreateSolidBrush(COLORREF color)
{
    HBRUSH brush;
    assert((color & 0xFFFFFF00UL) == 0x01000000UL);
    brush = object_new(OBJ_BRUSH);
    objects[brush].color = color;
    return brush;
}

static void record_polygon(HDC dc, const POINT *points, int count)
{
    RecordedPolygon *polygon;
    int i;
    assert(objects[dc].kind == OBJ_DC);
    assert(objects[objects[dc].brush].kind == OBJ_BRUSH);
    assert(objects[dc].pen == GetStockObject(NULL_PEN));
    assert(count >= 3 && count <= 12);
    polygon = NULL;
    if (!stream_polygons) {
        assert(recorded_count < 5000);
        polygon = recorded + recorded_count++;
        polygon->count = count;
        polygon->color = (unsigned char)objects[objects[dc].brush].color;
    }
    for (i = 0; i < count; ++i) {
        assert(points[i].x >= 0 && points[i].x <= (int)drv_win_width);
        assert(points[i].y >= 0 && points[i].y <= (int)drv_win_height);
        if (polygon != NULL) polygon->points[i] = points[i];
    }
    {
        long area;
        area = 0;
        for (i = 1; i < count - 1; ++i)
            area += (long)(points[i].x - points[0].x) *
                     (points[i + 1].y - points[0].y) -
                     (long)(points[i].y - points[0].y) *
                     (points[i + 1].x - points[0].x);
        if (area >= 0) {
            fprintf(stderr, "Non-front GDI polygon: count %d, area %ld, pose %.2f/%.2f, resolution %d\n",
                    count, area, (double)drv_rot_x, (double)drv_rot_y, globe_longitude);
            for (i = 0; i < count; ++i)
                fprintf(stderr, " %d,%d", points[i].x, points[i].y);
            fputc('\n', stderr);
        }
        assert(area < 0);
    }
    ++submitted_polygons;
    submitted_vertices += count;
}
BOOL Polygon(HDC dc, const POINT *points, int count)
{
    ++polygon_calls;
    record_polygon(dc, points, count);
    return 1;
}
BOOL PolyPolygon(HDC dc, const POINT *points, int *counts, int polygons)
{
    int i;
    unsigned long total;
    assert(polygons > 0 && polygons <= DRV_QUAD_BATCH_MAX);
    ++polypolygon_calls;
    if (fail_polygon) return FALSE;
    total = 0;
    for (i = 0; i < polygons; ++i) {
        record_polygon(dc, points + total, counts[i]);
        total += counts[i];
    }
    assert(total * 4UL < 65536UL && (unsigned long)polygons * 2UL < 65536UL);
    return 1;
}
BOOL PatBlt(HDC dc, int x, int y, int width, int height, DWORD operation)
{
    assert(objects[dc].kind == OBJ_DC && width > 0 && height > 0);
    (void)x; (void)y; (void)operation;
    return 1;
}
BOOL BitBlt(HDC dc, int x, int y, int width, int height, HDC source,
             int source_x, int source_y, DWORD operation)
{
    assert(objects[dc].kind == OBJ_DC && objects[source].kind == OBJ_DC);
    assert(width > 0 && height > 0 && operation == SRCCOPY);
    (void)x; (void)y; (void)source_x; (void)source_y;
    ++bitblts;
    return 1;
}
int FillRect(HDC dc, const RECT *rectangle, HBRUSH brush)
{
    assert(objects[dc].kind == OBJ_DC && rectangle != NULL && brush > 0);
    return 1;
}
int SetPolyFillMode(HDC dc, int mode)
{
    assert(objects[dc].kind == OBJ_DC && (mode == WINDING || mode == ALTERNATE));
    return WINDING;
}
int SetBkMode(HDC dc, int mode) { assert(objects[dc].kind == OBJ_DC); return mode; }
COLORREF SetTextColor(HDC dc, COLORREF color) { assert(objects[dc].kind == OBJ_DC); return color; }
COLORREF SetBkColor(HDC dc, COLORREF color) { assert(objects[dc].kind == OBJ_DC); return color; }
static void check_text(LPCSTR text, int length)
{
    assert(text != NULL && length > 0 && strstr(text, "texture") == NULL);
    if (strstr(text, "visible") != NULL) {
        assert(length < (int)sizeof(last_status));
        memcpy(last_status, text, (size_t)length);
        last_status[length] = 0;
        assert(strstr(last_status, "quads") != NULL);
        assert(strstr(last_status, "triangles") == NULL);
        assert((strstr(last_status, "frames per second") != NULL) ==
               (drv_fps_enabled != FALSE));
        assert(strstr(last_status, "math") != NULL);
    } else {
        assert(strstr(text, "1-5") != NULL);
    }
}
BOOL TextOut(HDC dc, int x, int y, LPCSTR text, int length)
{
    assert(objects[dc].kind == OBJ_DC);
    check_text(text, length);
    (void)x; (void)y;
    ++text_calls;
    return 1;
}
int DrawText(HDC dc, LPCSTR text, int length, RECT *rectangle, UINT flags)
{
    int columns, lines;
    assert(objects[dc].kind == OBJ_DC && rectangle != NULL);
    assert(flags & DT_WORDBREAK);
    assert(flags & DT_NOPREFIX);
    if (length < 0) length = (int)strlen(text);
    columns = (rectangle->right - rectangle->left) / 6;
    if (columns < 1) columns = 1;
    lines = (length + columns - 1) / columns;
    if (flags & DT_CALCRECT) {
        rectangle->bottom = (short)(rectangle->top + lines * 12);
    } else {
        assert(rectangle->left >= 0 && rectangle->right <= (int)drv_win_width);
        assert(rectangle->top >= 0 && rectangle->bottom <= (int)drv_win_height);
        assert(rectangle->right > rectangle->left && rectangle->bottom > rectangle->top);
        check_text(text, length);
        ++text_calls;
    }
    return lines * 12;
}

HWND SetCapture(HWND window)
{
    HWND previous;
    assert(window == main_window);
    previous = capture_window;
    capture_window = window;
    return previous;
}
void ReleaseCapture(void) { capture_window = 0; }
UINT SetTimer(HWND window, UINT id, UINT milliseconds, TIMERPROC callback)
{
    assert(window == main_window && milliseconds > 0 && callback == NULL);
    ++timer_calls;
    return id != 0 ? id : 1;
}
BOOL KillTimer(HWND window, UINT id)
{
    assert(window == main_window && id != 0 && timer_calls > 0);
    --timer_calls;
    return 1;
}
int MessageBox(HWND window, LPCSTR text, LPCSTR title, UINT flags)
{
    unsigned int polygons, allocated, texts;
    unsigned long vertices;
    float rotation;
    (void)title; (void)flags;
    if (profile != NULL && strncmp(profile, "fatal-", 6) == 0) {
        assert(gdi_failed && !gdi_ready && !gdi_dirty && !gdi_timer_active);
        assert(!drv_auto_enabled && !drv_lmouse_pressed && !drv_rmouse_pressed);
        assert(timer_calls == 0 && capture_window == 0);
        assert(fatal_dialogs++ == 0);
        if (strcmp(profile, "fatal-poly") == 0)
            assert(strstr(text, "PolyPolygon") != NULL);
        else if (strcmp(profile, "fatal-oom") == 0) {
            assert(strstr(text, "vertices") != NULL);
            assert(globe_verts == NULL && globe_verts_num == 131841UL);
        } else {
            assert(strcmp(profile, "fatal-size") == 0);
            assert(strstr(text, "dimensions") != NULL && window == 0);
        }
        polygons = polypolygon_calls;
        allocated = allocated_count;
        texts = text_calls;
        vertices = globe_verts_num;
        rotation = drv_rot_y;
        if (window != 0) {
            /* A modal Win16 dialog pumps queued paint/timer/input messages.
             * None may touch the failed or partly replaced scene. */
            window_callback(window, WM_TIMER, GDI_TIMER, 0);
            window_callback(window, WM_PAINT, 0, 0);
            window_callback(window, WM_CHAR, '1', 0);
            window_callback(window, WM_SIZE, 0, (LPARAM)(800UL | (600UL << 16)));
            window_callback(window, WM_LBUTTONDOWN, 0, 0);
            window_callback(window, WM_RBUTTONDOWN, 0, 0);
            drv_render();
        }
        assert(polypolygon_calls == polygons && allocated_count == allocated);
        assert(text_calls == texts && globe_verts_num == vertices);
        assert(drv_rot_y == rotation && capture_window == 0);
        return 1;
    }
    fprintf(stderr, "Unexpected GDI error dialog: %s\n", text);
    assert(!"Unexpected error dialog");
    return 1;
}

/* Exercise the production startup sequence, not a test replacement. */
#include "../src/main.c"

typedef struct { double x, y; } ReferencePoint;
typedef struct {
    ReferencePoint point[4];
    int count;
    unsigned char color;
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

static void add_reference(unsigned long a, unsigned long b, unsigned long c,
                           unsigned long d, unsigned char color)
{
    ReferenceQuad *quad;
    ReferencePoint projected[4];
    int i, previous;
    assert(reference_count < 5000);
    quad = reference + reference_count++;
    projected[0] = project_reference(globe_verts + a);
    projected[1] = project_reference(globe_verts + b);
    projected[2] = project_reference(globe_verts + c);
    projected[3] = project_reference(globe_verts + d);
    quad->count = 0;
    for (i = 0; i < 4; ++i) {
        previous = quad->count - 1;
        if (previous >= 0 && projected[i].x == quad->point[previous].x &&
            projected[i].y == quad->point[previous].y) continue;
        quad->point[quad->count++] = projected[i];
    }
    if (quad->count > 1 && quad->point[quad->count - 1].x == quad->point[0].x &&
        quad->point[quad->count - 1].y == quad->point[0].y) --quad->count;
    assert(quad->count == 3 || quad->count == 4);
    quad->area = 0;
    for (i = 1; i < quad->count - 1; ++i)
        quad->area += (quad->point[i].x - quad->point[0].x) *
                      (quad->point[i + 1].y - quad->point[0].y) -
                      (quad->point[i].y - quad->point[0].y) *
                      (quad->point[i + 1].x - quad->point[0].x);
    quad->color = color;
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
            add_reference(top, bottom, bottom + 1UL, top + 1UL, data[face]);
        }
    }
    assert(reference_count == globe_quads_num);
}

static double reference_distance(const RecordedPolygon *actual,
                                  const ReferenceQuad *expected)
{
    int shift, point, next, close, skipped, skip;
    double distance, best, dx, dy;
    best = 1e30;
    if (actual->color != expected->color || actual->count > expected->count ||
        actual->count < expected->count - 1) return best;
    /* Screen rounding may merge an adjacent pair on a silhouette quad.
     * Permit that one missing corner only when it is within the same
     * three-pixel projection tolerance as its retained neighbor. */
    for (skip = -1; skip < expected->count; ++skip) {
        if ((skip < 0) != (actual->count == expected->count)) continue;
        if (skip >= 0) {
            next = (skip + 1) % expected->count;
            dx = expected->point[skip].x - expected->point[next].x;
            dy = expected->point[skip].y - expected->point[next].y;
            if (fabs(dx) > 3.0 || fabs(dy) > 3.0) continue;
        }
        for (shift = 0; shift < expected->count; ++shift) {
            close = TRUE; distance = 0; skipped = 0;
            for (point = 0; point < actual->count; ++point) {
                next = (point + shift + skipped) % expected->count;
                if (next == skip) {
                    skipped = 1;
                    next = (next + 1) % expected->count;
                }
                dx = actual->points[point].x - expected->point[next].x;
                dy = actual->points[point].y - expected->point[next].y;
                if (fabs(dx) > 3.0 || fabs(dy) > 3.0) close = FALSE;
                distance += dx * dx + dy * dy;
            }
            if (close && distance < best) best = distance;
        }
    }
    return best;
}

static void check_projection(void)
{
    unsigned int polygon, quad, found, matched;
    long area;
    double distance, best, dx, dy, longest, length;
    int point, next;
    matched = 0;
    for (polygon = 0; polygon < recorded_count; ++polygon) {
        assert(recorded[polygon].count == 3 || recorded[polygon].count == 4);
        area = 0;
        for (point = 1; point < recorded[polygon].count - 1; ++point)
            area += (long)(recorded[polygon].points[point].x - recorded[polygon].points[0].x) *
                     (recorded[polygon].points[point + 1].y - recorded[polygon].points[0].y) -
                     (long)(recorded[polygon].points[point].y - recorded[polygon].points[0].y) *
                     (recorded[polygon].points[point + 1].x - recorded[polygon].points[0].x);
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
            fprintf(stderr, "No reference for polygon %u/%u, color %u, pose %.2f/%.2f, resolution %d\n",
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

/* Shared vertices are deliberately permuted so the driver must honor all
 * four indices. Repeated colors require native PolyPolygon grouping, while
 * 1/17/64-quad packets cover partial and maximum-size native batches. */
static void check_quad_batches(void)
{
    Vertex vertices[130], *saved_vertices;
    unsigned int indices[64 * 4], sizes[3], test, i, quad, calls;
    unsigned char colors[64];
    HPEN previous;
    for (i = 0; i < 130U; ++i) {
        unsigned int slot = (i * 37U) % 130U;
        vertices[slot].x = (float)((int)(i / 2U) * 2 - 64);
        vertices[slot].y = 0.0f;
        vertices[slot].z = (i & 1U) ? -100.0f : 100.0f;
    }
    for (i = 0; i < 64U; ++i) {
        indices[4U * i] = (2U * i * 37U) % 130U;
        indices[4U * i + 1U] = ((2U * i + 1U) * 37U) % 130U;
        indices[4U * i + 2U] = ((2U * i + 3U) * 37U) % 130U;
        indices[4U * i + 3U] = ((2U * i + 2U) * 37U) % 130U;
        colors[i] = (unsigned char)((i % 4U) * 37U);
    }
    sizes[0] = 1U; sizes[1] = 17U; sizes[2] = 64U;
    gdi_resize(32767, 32767);
    drv_rot_x = 0.0f; drv_rot_y = 90.0f;
    gdi_set_transform();
    saved_vertices = globe_verts;
    previous = (HPEN)SelectObject(gdi_buffer, GetStockObject(NULL_PEN));
    for (test = 0; test < 3U; ++test) {
        reference_count = 0;
        globe_verts = vertices;
        for (quad = 0; quad < sizes[test]; ++quad)
            add_reference(indices[4U * quad], indices[4U * quad + 1U],
                           indices[4U * quad + 2U], indices[4U * quad + 3U],
                           colors[quad]);
        globe_verts = saved_vertices;
        recorded_count = 0;
        globe_quads_num_vis = 0;
        calls = polypolygon_calls;
        drv_draw_quadbatch_flat_color(130U, vertices, sizes[test], indices, colors);
        assert(recorded_count == sizes[test] && globe_quads_num_vis == sizes[test]);
        assert(polypolygon_calls - calls == (sizes[test] < 4U ? sizes[test] : 4U));
        check_projection();
        for (quad = 0; quad < reference_count; ++quad) assert(reference[quad].seen);
    }
    /* The individual QUAD entry point retains identical palette and geometry
     * semantics and delegates to the same native batch implementation. */
    recorded_count = 0;
    globe_quads_num_vis = 0;
    drv_draw_quad_flat_color(vertices + indices[0], vertices + indices[1],
                              vertices + indices[2], vertices + indices[3], colors[0]);
    assert(recorded_count == 1 && globe_quads_num_vis == 1);
    assert(recorded[0].color == colors[0] && recorded[0].count == 4);
    drv_draw_quadbatch_flat_color(0U, NULL, 0U, NULL, NULL);
    assert(recorded_count == 1 && globe_quads_num_vis == 1);
    SelectObject(gdi_buffer, previous);
    gdi_resize(640, 480);
    puts("PASS GDI quad packets: arbitrary shared-vertex indices, exact colors/winding, partial and 64-quad batches, native palette grouping and individual-quad compatibility");
}

static LPARAM packed_coordinates(int x, int y)
{
    return (LPARAM)((DWORD)(WORD)x | ((DWORD)(WORD)y << 16));
}

static void camera_triangle(double z0, double z1, double z2)
{
    gdi_float_points[0].x = -100.0f;
    gdi_float_points[0].y = -100.0f;
    gdi_float_points[0].z = (float)z0;
    gdi_float_points[1].x = 100.0f;
    gdi_float_points[1].y = -100.0f;
    gdi_float_points[1].z = (float)z1;
    gdi_float_points[2].x = 0.0f;
    gdi_float_points[2].y = 100.0f;
    gdi_float_points[2].z = (float)z2;
    gdi_fixed_points[0].x = -1600L;
    gdi_fixed_points[0].y = -1600L;
    gdi_fixed_points[0].z = (long)(z0 * 16.0);
    gdi_fixed_points[1].x = 1600L;
    gdi_fixed_points[1].y = -1600L;
    gdi_fixed_points[1].z = (long)(z1 * 16.0);
    gdi_fixed_points[2].x = 0L;
    gdi_fixed_points[2].y = 1600L;
    gdi_fixed_points[2].z = (long)(z2 * 16.0);
}

static void camera_quad(double z0, double z1, double z2, double z3)
{
    static const int x[4] = { -100, 100, 100, -100 };
    static const int y[4] = { -100, -100, 100, 100 };
    double z[4];
    int i;
    z[0] = z0; z[1] = z1; z[2] = z2; z[3] = z3;
    for (i = 0; i < 4; ++i) {
        gdi_float_points[i].x = (float)x[i];
        gdi_float_points[i].y = (float)y[i];
        gdi_float_points[i].z = (float)z[i];
        gdi_fixed_points[i].x = x[i] * 16L;
        gdi_fixed_points[i].y = y[i] * 16L;
        gdi_fixed_points[i].z = (long)(z[i] * 16.0);
    }
}

static void check_clipping(void)
{
    POINT polygon[10];
    static const unsigned int indices[3] = { 0, 1, 2 };
    static const unsigned int quad_indices[4] = { 0, 1, 2, 3 };
    static const unsigned int reversed[4] = { 3, 2, 1, 0 };
    int count, i;
    camera_triangle(-9000.0, -9000.0, -11000.0);
    count = gdi_prepare_polygon(3U, indices, 3U, polygon);
    assert(count == 4);
    camera_triangle(-11000.0, -11000.0, -11000.0);
    assert(gdi_prepare_polygon(3U, indices, 3U, polygon) == 0);
    camera_triangle(0.0, 0.0, 0.0);
    assert(gdi_prepare_polygon(3U, indices, 3U, polygon) == 0);
    camera_triangle(-2.0, -2.0, -0.5);
    count = gdi_prepare_polygon(3U, indices, 3U, polygon);
    assert(count >= 3 && count <= 10);
    for (i = 0; i < count; ++i) {
        assert(polygon[i].x >= 0 && polygon[i].x <= (int)drv_win_width);
        assert(polygon[i].y >= 0 && polygon[i].y <= (int)drv_win_height);
    }
    camera_quad(-9000.0, -9000.0, -11000.0, -11000.0);
    assert(gdi_prepare_polygon(4U, quad_indices, 4U, polygon) == 4);
    camera_quad(-11000.0, -11000.0, -11000.0, -11000.0);
    assert(gdi_prepare_polygon(4U, quad_indices, 4U, polygon) == 0);
    camera_quad(0.0, 0.0, 0.0, 0.0);
    assert(gdi_prepare_polygon(4U, quad_indices, 4U, polygon) == 0);
    camera_quad(-2.0, -2.0, -0.5, -0.5);
    count = gdi_prepare_polygon(4U, quad_indices, 4U, polygon);
    assert(count >= 3 && count <= 10);
    for (i = 0; i < count; ++i) {
        assert(polygon[i].x >= 0 && polygon[i].x <= (int)drv_win_width);
        assert(polygon[i].y >= 0 && polygon[i].y <= (int)drv_win_height);
    }
    camera_quad(-3000.0, -3000.0, -3000.0, -3000.0);
    assert(gdi_prepare_polygon(4U, reversed, 4U, polygon) == 0);
    for (i = 0; i < 10; ++i) {
        polygon[i].x = 1234;
        polygon[i].y = -1234;
    }
    assert(gdi_prepare_polygon(3U, quad_indices, 4U, polygon) == 0);
    assert(gdi_prepare_polygon(0U, quad_indices, 4U, polygon) == 0);
    assert(gdi_prepare_polygon(GDI_MAX_VERTICES + 1U,
                               quad_indices, 4U, polygon) == 0);
    for (i = 0; i < 10; ++i)
        assert(polygon[i].x == 1234 && polygon[i].y == -1234);
    /* Repeated pole vertices become one nondegenerate native triangle. */
    gdi_float_points[3] = gdi_float_points[0];
    gdi_fixed_points[3] = gdi_fixed_points[0];
    assert(gdi_prepare_polygon(4U, quad_indices, 4U, polygon) == 3);
    if (gdi_has_fpu) {
        gdi_float_points[0].x = FLT_MAX;
        assert(gdi_prepare_polygon(4U, quad_indices, 4U, polygon) == 0);
    }
    test_soft3d_shared_pipeline();
}

static int run_contract(void)
{
    int resolution, pose, i;
    float old_x, old_y;
    double expected_rotation;
    unsigned int realized, texts;
#if DEBUG
    unsigned int old_invalidations;
#endif
    static const int pitch[4] = { 0, 19, 247, 71 };
    static const int yaw[4] = { 180, 37, 121, 302 };
    assert(globe_longitude == 32 && globe_latitude == 16);
    assert(!drv_texture_avail && !drv_texture_enabled);
    assert(gdi_has_fpu == ((windows_flags & WF_80x87) != 0 &&
                          (windows_flags & (WF_CPU386 | WF_CPU486)) != 0));
    assert(palette_uploads == 1 && palette_realizations > 0 && timer_calls == 1);
    assert(drv_win_width == (unsigned int)(screen_width <= 640 && screen_height <= 480 ? 320 : 640));
    assert(drv_win_height == (unsigned int)(screen_width <= 640 && screen_height <= 480 ? 240 : 480));
    assert(allocated_count >= 4 && loaded_chunks > 0);
    if (strncmp(profile, "fatal-", 6) == 0) {
        if (strcmp(profile, "fatal-size") == 0) {
            gdi_cleanup();
            drv_init_window(0, 0, 32767, 300);
        } else {
            window_callback(main_window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
            window_callback(main_window, WM_RBUTTONDOWN, 0, 0);
            drv_auto_enabled = TRUE;
            if (strcmp(profile, "fatal-poly") == 0) {
                fail_polygon = TRUE;
                gdi_dirty = TRUE;
                drv_render();
            } else {
                assert(strcmp(profile, "fatal-oom") == 0);
                fail_allocation = 2U;
                allocation_attempts = 0;
                gdi_dirty = FALSE;
                window_callback(main_window, WM_CHAR, '5', 0);
            }
        }
        assert(!"Fatal GDI error must exit after the modal dialog");
    }
    recorded_count = 0;
    drv_render();
    assert(text_calls == 2 && last_status[0] != 0);
    window_callback(main_window, WM_CHAR, 'f', 0);
    assert(!drv_fps_enabled);
    recorded_count = 0;
    drv_render();
    assert(strstr(last_status, "frames per second") == NULL);
    window_callback(main_window, WM_CHAR, 'F', 0);
    assert(drv_fps_enabled);
    drv_osd_enabled = FALSE;
    for (resolution = 0; resolution < 2; ++resolution) {
        window_callback(main_window, WM_CHAR, (WPARAM)('1' + resolution), 0);
        assert(globe_longitude == (resolution == 0 ? 32 : 64));
        for (pose = 0; pose < 4; ++pose) {
            drv_rot_x = (float)pitch[pose];
            drv_rot_y = (float)yaw[pose];
            build_reference();
            recorded_count = 0;
            drv_render();
            assert(recorded_count == globe_quads_num_vis);
            check_projection();
        }
        window_callback(main_window, WM_CHAR, 't', 0);
        assert(!drv_texture_enabled);
    }
    /* Large real Earth meshes exercise huge allocations and all resource
     * chunks in linear time; exact quad topology is covered independently
     * by the geometry fixture and fully visible indexed packets below. */
    for (resolution = 2; resolution < 5; ++resolution) {
        unsigned long before;
        window_callback(main_window, WM_CHAR, (WPARAM)('1' + resolution), 0);
        assert(globe_longitude == (32 << resolution));
        before = submitted_polygons;
        stream_polygons = TRUE;
        drv_render();
        stream_polygons = FALSE;
        assert(submitted_polygons - before == globe_quads_num_vis);
        assert(globe_quads_num_vis > 0 && globe_quads_num_vis < globe_quads_num);
        window_callback(main_window, WM_CHAR, 't', 0);
        assert(!drv_texture_enabled);
    }
    assert(maximum_allocation > 65536UL);
    assert(loaded_chunks >= 12UL);
    window_callback(main_window, WM_CHAR, '2', 0);
    assert(globe_longitude == 64);
    check_quad_batches();
    assert(polypolygon_calls > 0 && polygon_calls == 0);
    check_clipping();
    puts("PASS GDI rendering: five Earth resolutions, huge allocations/resources, independent pose checks, bounded palette batches, near/far/screen clipping");

    old_x = drv_rot_x; old_y = drv_rot_y;
    window_callback(main_window, WM_LBUTTONDOWN, MK_LBUTTON, packed_coordinates(320, 240));
    assert(drv_lmouse_pressed && capture_window == main_window);
    window_callback(main_window, WM_MOUSEMOVE, MK_LBUTTON, packed_coordinates(384, 288));
    assert(drv_rot_x != old_x && drv_rot_y != old_y);
    window_callback(main_window, WM_LBUTTONUP, 0, packed_coordinates(384, 288));
    assert(!drv_lmouse_pressed && capture_window == 0);
#if DEBUG
    drv_osd_enabled = TRUE;
    gdi_dirty = FALSE;
    old_invalidations = invalidations;
#endif
    window_callback(main_window, WM_RBUTTONDOWN, 0, packed_coordinates(384, 288));
    assert(drv_rmouse_pressed && capture_window == main_window);
#if DEBUG
    assert(gdi_dirty && invalidations == old_invalidations + 1U);
    recorded_count = 0;
    window_callback(main_window, WM_PAINT, 0, 0);
    assert(strstr(last_status, "R=1") != NULL);
    assert(!gdi_dirty);
    old_invalidations = invalidations;
#endif
    window_callback(main_window, WM_RBUTTONUP, 0, packed_coordinates(384, 288));
    assert(!drv_rmouse_pressed && capture_window == 0);
#if DEBUG
    assert(gdi_dirty && invalidations == old_invalidations + 1U);
    recorded_count = 0;
    window_callback(main_window, WM_PAINT, 0, 0);
    assert(strstr(last_status, "R=0") != NULL);
#endif
    window_callback(main_window, WM_LBUTTONDOWN, MK_LBUTTON, packed_coordinates(0, 0));
    window_callback(main_window, WM_MOUSEMOVE, MK_LBUTTON, packed_coordinates(-10, -5));
    window_callback(main_window, WM_RBUTTONDOWN, 0, 0);
#if DEBUG
    gdi_dirty = FALSE;
    old_invalidations = invalidations;
#endif
    window_callback(main_window, WM_KILLFOCUS, 0, 0);
    assert(!drv_lmouse_pressed && !drv_rmouse_pressed && capture_window == 0);
#if DEBUG
    assert(gdi_dirty && invalidations == old_invalidations + 1U);
    recorded_count = 0;
    window_callback(main_window, WM_PAINT, 0, 0);
    assert(strstr(last_status, "R=0") != NULL);
    drv_osd_enabled = FALSE;
#endif
    assert(drv_rot_x >= 0 && drv_rot_x < 360 && drv_rot_y >= 0 && drv_rot_y < 360);
    texts = text_calls;
    window_callback(main_window, WM_CHAR, 'o', 0);
    assert(drv_osd_enabled);
    recorded_count = 0;
    window_callback(main_window, WM_PAINT, 0, 0);
    assert(text_calls == texts + 2 && bitblts > 0);
    window_callback(main_window, WM_CHAR, 'o', 0);
    assert(!drv_osd_enabled);
    old_y = drv_rot_y;
    window_callback(main_window, WM_CHAR, 'a', 0);
    assert(drv_auto_enabled);
    window_callback(main_window, WM_TIMER, 1, 0);
    assert(drv_rot_y != old_y);
    ticks = 0xfffffff0UL;
    gdi_auto_tick = ticks;
    old_y = drv_rot_y;
    window_callback(main_window, WM_TIMER, 1, 0);
    assert(fabs((drv_rot_y - old_y) - 0.16f) < 0.001);
    /* A full wrapping DWORD interval stays accurate without repeated
     * subtraction, including an invalid previous rotation. */
    ticks = 0xfffffff0UL;
    gdi_auto_tick = 1UL;
    drv_rot_y = 37.0f;
    expected_rotation = fmod(37.0 + 4294967295.0 * (double)DRV_ROTATE / 1000.0,
                              360.0);
    window_callback(main_window, WM_TIMER, 1, 0);
    assert(fabs((double)drv_rot_y - expected_rotation) < 0.001);
    drv_rot_y = FLT_MAX;
    gdi_auto_tick = ticks;
    window_callback(main_window, WM_TIMER, 1, 0);
    assert(drv_rot_y >= 0.0f && drv_rot_y < 360.0f);
    window_callback(main_window, WM_CHAR, 'a', 0);
    assert(!drv_auto_enabled);
    old_y = drv_rot_y;
    window_callback(main_window, WM_TIMER, 1, 0);
    assert(drv_rot_y == old_y);
    realized = palette_realizations;
    assert(window_callback(main_window, WM_QUERYNEWPALETTE, 0, 0));
    assert(palette_realizations > realized);
    assert(!last_realize_background);
    realized = palette_realizations;
    window_callback(main_window, WM_PALETTECHANGED, main_window, 0);
    assert(palette_realizations == realized);
    window_callback(main_window, WM_PALETTECHANGED, 899, 0);
    assert(palette_realizations > realized);
    assert(last_realize_background);
    window_callback(main_window, WM_ACTIVATE, WA_INACTIVE, 0);
    recorded_count = 0;
    window_callback(main_window, WM_PAINT, 0, 0);
    assert(last_realize_background);
    window_callback(main_window, WM_ACTIVATE, 1, 0);
    assert(!last_realize_background);
    client_width = 800; client_height = 600;
    window_callback(main_window, WM_SIZE, 0, packed_coordinates(800, 600));
    assert(drv_win_width == 800 && drv_win_height == 600);
    build_reference();
    recorded_count = 0;
    window_callback(main_window, WM_PAINT, 0, 0);
    check_projection();
    window_callback(main_window, WM_SIZE, SIZE_MINIMIZED, 0);
    assert(drv_win_width == 800 && drv_win_height == 600);
    gdi_resize(1, 1);
    camera_triangle(-2.0, -2.0, -0.5);
    {
        POINT tiny[10];
        static const unsigned int indices[3] = { 0, 1, 2 };
        assert(gdi_prepare_polygon(3U, indices, 3U, tiny) >= 3);
    }
    gdi_resize(800, 600);
    assert(drv_win_width == 800 && drv_win_height == 600);
    ticks = 0xffffffd0UL;
    for (i = 0; i < 61; ++i) { drv_fps_start(); drv_fps_stop(); }
    assert(fabs(drv_fps_average - 62.5f) < 0.01);
    /* Explicit client dimensions override the detected small-screen default. */
    gdi_cleanup();
    assert(resource_count == 0 && allocated_count == 0 && proc_instances == 0);
    if (strcmp(profile, "maxframe") == 0) {
        assert(drv_init_window(0, 0, 32759, 32739));
        assert(drv_win_width == 32759U && drv_win_height == 32739U);
        assert(client_width == 32759 && client_height == 32739);
    } else {
        assert(drv_init_window(0, 0, 444, 333));
        assert(drv_win_width == 444 && drv_win_height == 333);
        assert(client_width == 444 && client_height == 333);
    }
    puts("PASS GDI input: production WinMain/common startup, left/right mouse capture, drag, 1-5/O/A/F keys, palette events, screen defaults/overrides, resize, paint and 32-bit timer rollover");
    return 0;
}

static void check_fatal_cleanup(void)
{
    assert(fatal_dialogs == 1U && gdi_failed && !gdi_ready);
    assert(resource_count == 0 && allocated_count == 0 && proc_instances == 0);
    assert(timer_calls == 0 && capture_window == 0 && main_window == 0);
    puts("PASS GDI fatal error: modal paint/timer/input dispatch cannot reenter rendering or partial scene state, and cleanup releases all resources");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    profile = argv[1];
    if (strncmp(profile, "fatal-", 6) == 0)
        assert(atexit(check_fatal_cleanup) == 0);
    windows_flags = WF_CPU386;
    if (strstr(argv[1], "fpu") != NULL) windows_flags |= WF_80x87;
    if (strcmp(argv[1], "287") == 0) windows_flags = WF_CPU286 | WF_80x87;
    close_with_escape = strstr(argv[1], "escape") != NULL;
    if (strcmp(argv[1], "small640") == 0) { screen_width = 640; screen_height = 480; }
    if (strcmp(argv[1], "small512") == 0) { screen_width = 512; screen_height = 342; }
    if (strcmp(argv[1], "tall640") == 0) { screen_width = 640; screen_height = 600; }
    assert(WinMain(1, 0, "", SW_SHOW) == 0);
    assert(contract_started && created_windows == 2 && destroyed_windows == created_windows && quit_messages == created_windows);
    assert(resource_count == 0 && allocated_count == 0 && proc_instances == 0);
    assert(timer_calls == 0 && capture_window == 0 && main_window == 0);
    puts("PASS GDI cleanup: native object selection/lifetimes, global allocations, timer, window and callback thunk");
    return 0;
}
