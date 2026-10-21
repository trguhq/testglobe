/* Execute the real Win16 driver against recorded Windows calls.
 * This host contract is not a Windows SDK, ABI check, or renderer. */
#include <assert.h>
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
    client_width = width; client_height = height;
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
        area = (long)(points[1].x - points[0].x) * (points[2].y - points[0].y) -
               (long)(points[1].y - points[0].y) * (points[2].x - points[0].x);
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
    assert(polygons > 0 && polygons <= 128);
    ++polypolygon_calls;
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
        assert(strstr(last_status, "triangles") != NULL);
        assert(strstr(last_status, "quads") == NULL);
        assert(strstr(last_status, "frames per second") != NULL);
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
    (void)window; (void)title; (void)flags;
    fprintf(stderr, "Unexpected GDI error dialog: %s\n", text);
    assert(!"Unexpected error dialog");
    return 1;
}

/* Exercise the production startup sequence, not a test replacement. */
#include "../src/main.c"

typedef struct { double x, y; } ReferencePoint;
typedef struct {
    ReferencePoint point[3];
    unsigned char color;
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
    triangle->color = color;
    triangle->seen = FALSE;
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
    if (actual->count != 3 || actual->color != expected->color) return best;
    for (shift = 0; shift < 3; ++shift) {
        close = TRUE;
        distance = 0;
        for (point = 0; point < 3; ++point) {
            next = (point + shift) % 3;
            dx = actual->points[point].x - expected->point[next].x;
            dy = actual->points[point].y - expected->point[next].y;
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
        area = (long)(recorded[polygon].points[1].x - recorded[polygon].points[0].x) *
               (recorded[polygon].points[2].y - recorded[polygon].points[0].y) -
               (long)(recorded[polygon].points[1].y - recorded[polygon].points[0].y) *
               (recorded[polygon].points[2].x - recorded[polygon].points[0].x);
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

/* A fully visible strip makes every chunk-boundary facet observable.
 * Its 1,024 triangles stay several pixels wide even at the largest strip
 * count, so rounding cannot conceal missing seams or wrong parity/colors. */
static void check_large_strip(void)
{
    Vertex *vertices, *saved_vertices;
    unsigned char *colors;
    unsigned int i, facet, sizes[3], test;
    vertices = (Vertex *)calloc(1026U, sizeof(*vertices));
    colors = (unsigned char *)malloc(1024U);
    assert(vertices != NULL && colors != NULL);
    for (i = 0; i < 1026U; ++i) {
        vertices[i].x = (float)((int)(i / 2U) * 2 - 512);
        vertices[i].y = 0.0f;
        vertices[i].z = (i & 1U) ? -100.0f : 100.0f;
    }
    for (i = 0; i < 1024U; ++i) colors[i] = (unsigned char)(i * 37U);
    sizes[0] = 3U; sizes[1] = 259U; sizes[2] = 1026U;
    drv_resize(32767, 32767);
    drv_rot_x = 0.0f; drv_rot_y = 90.0f;
    gdi_set_transform();
    saved_vertices = globe_verts;
    for (test = 0; test < 3U; ++test) {
        reference_count = 0;
        globe_verts = vertices;
        for (facet = 0; facet < sizes[test] - 2U; ++facet) {
            if (facet & 1U) add_reference(facet + 1U, facet, facet + 2U, colors[facet]);
            else add_reference(facet, facet + 1U, facet + 2U, colors[facet]);
        }
        globe_verts = saved_vertices;
        recorded_count = 0;
        globe_tris_num_vis = 0;
        {
            HPEN previous;
            previous = (HPEN)SelectObject(gdi_buffer, GetStockObject(NULL_PEN));
            drv_draw_tristrip_flat_color(sizes[test], vertices, colors);
            SelectObject(gdi_buffer, previous);
        }
        assert(recorded_count == sizes[test] - 2U);
        assert(globe_tris_num_vis == sizes[test] - 2U);
        check_projection();
        for (facet = 0; facet < reference_count; ++facet) assert(reference[facet].seen);
    }
    free(colors);
    free(vertices);
    drv_resize(640, 480);
    puts("PASS GDI strip packets: 1,026 vertices, exact colors/winding and full seam coverage, bounded 128-facet native calls");
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

static void check_clipping(void)
{
    POINT polygon[10];
    int count, i;
    camera_triangle(-9000.0, -9000.0, -11000.0);
    count = gdi_prepare_polygon(0, 1, 2, polygon);
    assert(count == 4);
    camera_triangle(-11000.0, -11000.0, -11000.0);
    assert(gdi_prepare_polygon(0, 1, 2, polygon) == 0);
    camera_triangle(0.0, 0.0, 0.0);
    assert(gdi_prepare_polygon(0, 1, 2, polygon) == 0);
    camera_triangle(-2.0, -2.0, -0.5);
    count = gdi_prepare_polygon(0, 1, 2, polygon);
    assert(count >= 3 && count <= 10);
    for (i = 0; i < count; ++i) {
        assert(polygon[i].x >= 0 && polygon[i].x <= (int)drv_win_width);
        assert(polygon[i].y >= 0 && polygon[i].y <= (int)drv_win_height);
    }
    test_soft3d_shared_pipeline();
}

static int run_contract(void)
{
    int resolution, pose, i;
    float old_x, old_y;
    unsigned int realized, texts;
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
    recorded_count = 0;
    drv_render();
    assert(text_calls == 2 && last_status[0] != 0);
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
            assert(recorded_count == globe_tris_num_vis);
            check_projection();
        }
        window_callback(main_window, WM_CHAR, 't', 0);
        assert(!drv_texture_enabled);
    }
    /* Large real Earth meshes exercise huge allocations and all resource
     * chunks in linear time; exact strip topology is covered independently
     * by gdi_strips and the fully visible synthetic strip below. */
    for (resolution = 2; resolution < 5; ++resolution) {
        unsigned long before;
        window_callback(main_window, WM_CHAR, (WPARAM)('1' + resolution), 0);
        assert(globe_longitude == (32 << resolution));
        before = submitted_polygons;
        stream_polygons = TRUE;
        drv_render();
        stream_polygons = FALSE;
        assert(submitted_polygons - before == globe_tris_num_vis);
        assert(globe_tris_num_vis > 0 && globe_tris_num_vis < globe_tris_num);
        window_callback(main_window, WM_CHAR, 't', 0);
        assert(!drv_texture_enabled);
    }
    assert(maximum_allocation > 65536UL);
    assert(loaded_chunks >= 12UL);
    window_callback(main_window, WM_CHAR, '2', 0);
    assert(globe_longitude == 64);
    check_large_strip();
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
    window_callback(main_window, WM_LBUTTONDOWN, MK_LBUTTON, packed_coordinates(0, 0));
    window_callback(main_window, WM_MOUSEMOVE, MK_LBUTTON, packed_coordinates(-10, -5));
    window_callback(main_window, WM_KILLFOCUS, 0, 0);
    assert(!drv_lmouse_pressed && capture_window == 0);
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
    drv_resize(1, 1);
    camera_triangle(-2.0, -2.0, -0.5);
    {
        POINT tiny[10];
        assert(gdi_prepare_polygon(0, 1, 2, tiny) >= 3);
    }
    drv_resize(800, 600);
    assert(drv_win_width == 800 && drv_win_height == 600);
    ticks = 0xffffffd0UL;
    for (i = 0; i < 61; ++i) { drv_fps_start(); drv_fps_stop(); }
    assert(fabs(drv_fps_average - 62.5f) < 0.01);
    /* Explicit client dimensions override the detected small-screen default. */
    gdi_cleanup();
    assert(resource_count == 0 && allocated_count == 0 && proc_instances == 0);
    assert(drv_init_window(0, 0, 444, 333));
    assert(drv_win_width == 444 && drv_win_height == 333);
    assert(client_width == 444 && client_height == 333);
    puts("PASS GDI input: production WinMain/common startup, mouse capture/drag, 1-5/O/A keys, palette events, screen defaults/overrides, resize, paint and 32-bit timer rollover");
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
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
