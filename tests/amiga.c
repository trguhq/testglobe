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
static char osd_text[4096];
static void *rasters[32];
static ULONG raster_sizes[32];
static unsigned int display_version = 34;
static unsigned int pixel_x = 1, pixel_y = 1;

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    assert(!event_outstanding);
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
    assert(port == &window_messages && !event_outstanding);
    if (message_next == message_count) return NULL;
    event_outstanding = 1;
    return (struct Message *)&messages[message_next++];
}
void ReplyMsg(struct Message *message)
{ assert(message != NULL && event_outstanding); event_outstanding = 0; ++reply_count; }
ULONG Wait(ULONG mask)
{
    assert(mask == (1UL << window_messages.mp_SigBit));
    assert(!event_outstanding);
    messages[message_count].Class = CLOSEWINDOW;
    ++message_count;
    return mask;
}
void CurrentTime(ULONG *seconds, ULONG *micros)
{ clock_micros += 16667UL; *seconds = clock_micros / 1000000UL; *micros = clock_micros % 1000000UL; }
LONG GetScreenData(APTR out, ULONG size, ULONG kind, struct Screen *screen)
{
    (void)screen; assert(size == sizeof(desktop) && kind == WBENCHSCREEN);
    memcpy(out, &desktop, sizeof(desktop)); return 1;
}
struct Window *OpenWindow(const struct NewWindow *settings)
{
    assert(settings->Type == WBENCHSCREEN && settings->Screen == NULL);
    assert((settings->Flags & (WINDOWSIZING | GIMMEZEROZERO | WINDOWCLOSE)) ==
           (WINDOWSIZING | GIMMEZEROZERO | WINDOWCLOSE));
    assert(settings->IDCMPFlags & NEWSIZE);
    memset(&window_record, 0, sizeof(window_record));
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
{ assert(window == &window_record && window_count == 1 && !event_outstanding); --window_count; }
void BeginRefresh(struct Window *window)
{ assert(window == &window_record && !refresh_count); ++refresh_count; }
void EndRefresh(struct Window *window, LONG complete)
{ assert(window == &window_record && complete && refresh_count == 1); --refresh_count; }
void InitBitMap(struct BitMap *bitmap, LONG depth, ULONG width, ULONG height)
{
    memset(bitmap, 0, sizeof(*bitmap)); bitmap->Depth = (UBYTE)depth;
    bitmap->BytesPerRow = (UWORD)(((width + 15UL) / 16UL) * 2UL); bitmap->Rows = (UWORD)height;
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
{ assert(pen < colormap.Count); port->FgPen = (UBYTE)pen; }
void SetBPen(struct RastPort *port, ULONG pen)
{ assert(pen < colormap.Count); port->BgPen = (UBYTE)pen; }
void SetDrMd(struct RastPort *port, ULONG mode) { port->DrawMode = (UBYTE)mode; }
void RectFill(struct RastPort *port, LONG x0, LONG y0, LONG x1, LONG y1)
{ assert(port != NULL && x0 == 0 && y0 == 0 && x1 == (LONG)drv_win_width - 1 && y1 == (LONG)drv_win_height - 1); ++rect_calls; }
void Move(struct RastPort *port, LONG x, LONG y)
{ port->cp_x = (WORD)x; port->cp_y = (WORD)y; }
void Text(struct RastPort *port, CONST_STRPTR text, ULONG length)
{
    size_t used;
    assert(port->cp_x >= 0 && port->cp_y >= 0 && port->cp_y < (WORD)drv_win_height);
    assert((ULONG)port->cp_x + length * 8UL <= drv_win_width);
    ++text_calls; used = strlen(osd_text);
    if (used + length + 2U < sizeof(osd_text)) { memcpy(osd_text + used, text, length); osd_text[used + length] = '\n'; osd_text[used + length + 1] = 0; }
}
WORD TextLength(struct RastPort *port, CONST_STRPTR text, ULONG length)
{ (void)port; (void)text; return (WORD)(8UL * length); }
static LONG record_corner(struct RastPort *port, LONG x, LONG y)
{
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
    for (i = 0; i < area_count; ++i) rendered_hash = rendered_hash * 33UL + (ULONG)(area_x[i] * 32768L + area_y[i]);
    rendered_hash = rendered_hash * 33UL + port->FgPen;
    ++rendered_count; ++area_calls; blitter_busy = 1; return 0;
}
void WaitBlit(void) { blitter_busy = 0; }
void BltBitMapRastPort(const struct BitMap *bitmap, LONG sx, LONG sy,
                      struct RastPort *port, LONG dx, LONG dy, LONG width, LONG height, ULONG minterm)
{
    assert(bitmap != NULL && bitmap->Depth == desktop.BitMap.Depth && port == &window_port);
    assert(sx == 0 && sy == 0 && dx == 0 && dy == 0 && minterm == 0xc0UL);
    assert(width == (LONG)drv_win_width && height == (LONG)drv_win_height);
    ++copy_calls; blitter_busy = 1;
}
ULONG GetRGB4(struct ColorMap *colors, LONG pen)
{
    assert(colors == &colormap && pen >= 0 && (ULONG)pen < colors->Count);
    return ((ULONG)(desktop_palette[pen][0] >> 4) << 8) |
           ((ULONG)(desktop_palette[pen][1] >> 4) << 4) | (desktop_palette[pen][2] >> 4);
}
void GetRGB32(struct ColorMap *colors, ULONG first, ULONG count, ULONG *out)
{
    unsigned int i;
    assert(display_version >= 39 && colors == &colormap && count == 1 && first < colors->Count);
    for (i = 0; i < 3; ++i) out[i] = (ULONG)desktop_palette[first][i] * 0x01010101UL;
}
ULONG GetVPModeID(struct ViewPort *view) { assert(display_version >= 36 && view == &desktop.ViewPort); return 0; }
ULONG GetDisplayInfoData(APTR handle, UBYTE *buffer, ULONG length, ULONG tag, ULONG id)
{
    struct DisplayInfo *info;
    assert(display_version >= 36 && handle == NULL && length == sizeof(*info) && tag == DTAG_DISP && id == 0);
    info = (struct DisplayInfo *)buffer; info->Resolution.x = (WORD)pixel_x; info->Resolution.y = (WORD)pixel_y;
    return sizeof(*info);
}
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
    assert(rendered_count == globe_quads_num_vis);
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

int main(int argc, char **argv)
{
    unsigned int i, saved_palette_size, saved_count, level;
    unsigned char saved_palette[sizeof(desktop_palette)];
    unsigned long reference_hash, reference_count;
    float old_rotation;
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
    if (strcmp(mode, "four") == 0) desktop.BitMap.Depth = 4;
    if (strcmp(mode, "ecs") == 0) { display_version = 36; graphics_base.ChipRevBits0 = GFXF_HR_AGNUS | GFXF_HR_DENISE; }
    if (strcmp(mode, "aga") == 0 || strcmp(mode, "fpu") == 0 || strcmp(mode, "020") == 0 || strcmp(mode, "040") == 0)
    { display_version = 39; desktop.BitMap.Depth = 8; graphics_base.ChipRevBits0 = GFXF_AA_ALICE; exec_base.AttnFlags = AFF_68020; }
    if (strcmp(mode, "denisewide") == 0 || strcmp(mode, "ecswide") == 0)
    { display_version = 39; graphics_base.ChipRevBits0 = strcmp(mode, "ecswide") == 0 ? GFXF_HR_AGNUS : GFXF_HR_DENISE; }
    if (strcmp(mode, "fpu") == 0) exec_base.AttnFlags |= AFF_68881;
    if (strcmp(mode, "040") == 0) exec_base.AttnFlags |= AFF_68040 | AFF_FPU40;
    if (strcmp(mode, "falsefpu") == 0) exec_base.AttnFlags = AFF_68881;
    if (strcmp(mode, "aspect") == 0) { display_version = 39; pixel_x = 1; pixel_y = 2; }
    if (strcmp(mode, "backfail") == 0) raster_fail_at = 3;
    if (strcmp(mode, "scratchfail") == 0) raster_fail_at = 1;
    graphics_base.LibNode.lib_Version = intuition_base.LibNode.lib_Version = (UWORD)display_version;
    colormap.Count = (UWORD)(1U << desktop.BitMap.Depth); desktop.ViewPort.ColorMap = &colormap;
    desktop.ViewPort.Modes = HIRES; graphics_base.DisplayFlags = PAL;
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
    drv_ext_init(); globe_init(); drv_init(&argc, argv);
    if (strcmp(mode, "ham") == 0 || strcmp(mode, "ocswide") == 0 || strcmp(mode, "denisewide") == 0)
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
    assert(window_count == 1 && drv_win_width < (unsigned int)desktop.Width && drv_win_height < 240);
    if (fail_cache_allocation) assert(ami_project_cache == NULL);
    assert(ami_cpu == (strcmp(mode, "040") == 0 ? 68040 : (exec_base.AttnFlags & AFF_68020 ? 68020 : 68000)));
    assert(ami_has_fpu == (strcmp(mode, "fpu") == 0 || strcmp(mode, "040") == 0));
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
    assert(baseline_vertices + enhanced_vertices + fpu_vertices == 561);
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
    ami_keyboard('f'); assert(!drv_fps_enabled); ami_keyboard('f'); assert(drv_fps_enabled);
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
    messages[0].Class = VANILLAKEY; messages[0].Code = 'f';
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
