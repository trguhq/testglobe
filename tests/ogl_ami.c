/* Native Amiga MiniGL lifecycle/input contracts, without SDK or hardware. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ogl_ami.c"

static struct GfxBase graphics;
static struct IntuitionBase intuition;
static struct Library library, cybergraphics;
static struct ExecBase exec_base;
struct ExecBase *SysBase = &exec_base;
static struct Screen screen;
static struct Window window;
static struct RastPort port;
static struct Layer layer;
static unsigned int layer_locks, vertex_setting, depth_setting, mipmap_setting;
static unsigned int bitmap_count, bitmap_width, bitmap_height, blit_calls;
static int fail_bitmap, blitter_busy, gpu_finished, missing_cgx, unknown_bitmap;
static unsigned int bitmap_bytes = 4, bitmap_format = 12;
static unsigned int allocated_width, allocated_height;
static int rounded_bitmap, short_bitmap, huge_bitmap;
static struct BitMap owned_bitmap;
static struct TextFont font, osd_fonts[2];
static unsigned int font_opens, font_closes, font_draws[2];
static int fail_fonts, oversize_fonts, check_osd_layout, osd_last_bottom;
static struct ColorMap palette;
static struct MsgPort messages_port;
static struct Task task;
static unsigned char stack_memory[131072];
static struct GLcontext_t context;
static GLcontext current_context;
static MGLDispatchTable dispatch;
static struct IntuiMessage messages[16];
static unsigned int queued, next_message, outstanding;
static unsigned int libraries, windows, locks, creates, deletes, flips, text_calls;
static unsigned int replies, refreshes, draws, resize_calls, keys, mouse_calls, motions;
static unsigned int waits, clock_micros, notices;
static int fail_library, fail_context, short_stack, native_screen, fail_window, fail_lock;
static int expected_fatal, last_mouse_x, last_mouse_y;
static const char *profile;
int drv_lmouse_pressed, drv_rmouse_pressed;

static void dispatch_noop(void) {}
static void set_dispatch(void)
{
    memset(&dispatch, 0, sizeof(dispatch));
    dispatch.abiVersion = MINIGL_DISPATCH_ABI_VERSION;
    dispatch.structSize = sizeof(dispatch);
    dispatch.backendFlags = MINIGL_BACKEND_FLAG_PISTORM3D;
    dispatch.currentContext = &current_context;
    dispatch.GLBegin = dispatch_noop;
    dispatch.GLEnd = dispatch_noop;
    dispatch.GLBindTexture = dispatch_noop;
    dispatch.GLClear = dispatch_noop;
    dispatch.GLClearColor = dispatch_noop;
    dispatch.GLColor4ub = dispatch_noop;
    dispatch.GLColor4f = dispatch_noop;
    dispatch.GLCullFace = dispatch_noop;
    dispatch.GLDeleteTextures = dispatch_noop;
    dispatch.GLFlush = dispatch_noop;
    dispatch.GLFrontFace = dispatch_noop;
    dispatch.GLGenTextures = dispatch_noop;
    dispatch.GLGetError = dispatch_noop;
    dispatch.GLGetFloatv = dispatch_noop;
    dispatch.GLGetIntegerv = dispatch_noop;
    dispatch.GLGetString = dispatch_noop;
    dispatch.GLIsEnabled = dispatch_noop;
    dispatch.GLIsTexture = dispatch_noop;
    dispatch.GLLoadIdentity = dispatch_noop;
    dispatch.GLMatrixMode = dispatch_noop;
    dispatch.GLPopMatrix = dispatch_noop;
    dispatch.GLPushMatrix = dispatch_noop;
    dispatch.GLRotatef = dispatch_noop;
    dispatch.GLShadeModel = dispatch_noop;
    dispatch.GLTexCoord2fv = dispatch_noop;
    dispatch.GLTexEnvi = dispatch_noop;
    dispatch.GLTexImage2D = dispatch_noop;
    dispatch.GLTexParameteri = dispatch_noop;
    dispatch.GLTranslatef = dispatch_noop;
    dispatch.GLVertex3fv = dispatch_noop;
    dispatch.GLViewport = dispatch_noop;
    dispatch.GLPixelStorei = dispatch_noop;
    dispatch.GLUPerspective = dispatch_noop;
    dispatch.GLUBuild2DMipmaps = dispatch_noop;
    dispatch.GLGenLists = dispatch_noop;
    dispatch.GLDeleteLists = dispatch_noop;
    dispatch.GLNewList = dispatch_noop;
    dispatch.GLEndList = dispatch_noop;
    dispatch.GLCallList = dispatch_noop;
    dispatch.GLIsList = dispatch_noop;
    dispatch.MGLSetState = dispatch_noop;
    dispatch.MGLCreateContextFromBitMap = dispatch_noop;
    dispatch.MGLDeleteContext = dispatch_noop;
    dispatch.MGLSwitchDisplay = dispatch_noop;
    dispatch.mglChooseVertexBufferSize = mglChooseVertexBufferSize;
    dispatch.mglChooseZBufferDepth = mglChooseZBufferDepth;
    dispatch.mglProhibitMipMapping = mglProhibitMipMapping;
}
const MGLDispatchTable *ogl_ami_dispatch(struct Library *base)
{ assert(base == &library); return &dispatch; }
ULONG ogl_ami_cgx_attr(struct Library *base, struct BitMap *bitmap, ULONG attribute)
{
    assert(base == &cybergraphics && (bitmap == &screen.BitMap || bitmap == &owned_bitmap));
    if (attribute == OGL_AMI_CGX_IS)
        return bitmap == &screen.BitMap ? !native_screen : !unknown_bitmap;
    assert(bitmap == &owned_bitmap && !unknown_bitmap);
    if (attribute == OGL_AMI_CGX_BYTES) return bitmap_bytes;
    assert(attribute == OGL_AMI_CGX_FORMAT && bitmap_bytes == 4); return bitmap_format;
}
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    assert(!outstanding);
    if (!strcmp(name, "cybergraphics.library"))
    { assert(version == 40); if (missing_cgx) return NULL; ++libraries; return &cybergraphics; }
    if (!strcmp(name, MINIGLNAME))
    {
        assert(version == 29); if (fail_library) return NULL; ++libraries; return &library;
    }
    assert(version == 39); ++libraries;
    if (!strcmp(name, "graphics.library")) return (struct Library *)&graphics;
    assert(!strcmp(name, "intuition.library")); return (struct Library *)&intuition;
}
void CloseLibrary(struct Library *base)
{ assert(base && libraries > 0 && current_context == NULL && !bitmap_count); --libraries; }
struct Task *FindTask(CONST_STRPTR name)
{ assert(name == NULL); task.tc_SPLower = stack_memory; task.tc_SPUpper = stack_memory + (short_stack ? 32768 : sizeof(stack_memory)); return &task; }
struct Screen *LockPubScreen(CONST_STRPTR name)
{ assert(!strcmp(name, "Workbench") && !locks); if (fail_lock) return NULL; ++locks; return &screen; }
void UnlockPubScreen(CONST_STRPTR name, struct Screen *value)
{ assert(name == NULL && value == &screen && locks == 1); --locks; }
ULONG GetBitMapAttr(const struct BitMap *bitmap, ULONG attribute)
{
    if (bitmap == &screen.BitMap) { assert(attribute == BMA_FLAGS); return native_screen ? BMF_STANDARD : 0; }
    assert(bitmap == &owned_bitmap && bitmap_count == 1);
    if (attribute == BMA_WIDTH) return allocated_width;
    assert(attribute == BMA_HEIGHT); return allocated_height;
}
struct Window *OpenWindow(const struct NewWindow *settings)
{
    assert(locks == 1 && settings->Screen == &screen && settings->Type == PUBLICSCREEN);
    assert(!(settings->Flags & GIMMEZEROZERO) && (settings->Flags & WINDOWSIZING) && (settings->Flags & RMBTRAP));
    assert(settings->Width <= screen.Width && settings->Height <= screen.Height);
    if (fail_window) return NULL;
    window.Width = settings->Width; window.Height = settings->Height;
    window.BorderLeft = window.BorderRight = 8; window.BorderTop = 16; window.BorderBottom = 8;
    window.RPort = &port; window.WScreen = &screen; window.UserPort = &messages_port;
    ++windows; return &window;
}
void CloseWindow(struct Window *value)
{ assert(value == &window && windows == 1 && current_context == NULL && !outstanding && !bitmap_count); --windows; }
void mglChooseVertexBufferSize(int count) { assert(count == 256); vertex_setting = count; }
void mglChooseZBufferDepth(int depth) { assert(depth == 24); depth_setting = depth; }
void mglProhibitMipMapping(GLboolean value) { assert(value == GL_TRUE); mipmap_setting = 1; }
struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG depth, ULONG flags, const struct BitMap *friend_bitmap)
{
    assert(!bitmap_count && friend_bitmap == &screen.BitMap && depth == 32);
    assert(flags == (BMF_MINPLANES | BMF_DISPLAYABLE | BMF_CLEAR | 0x80UL | (12UL << 24)));
    assert(w > 0 && h > 0 && w <= OGL_AMI_MAX_DIMENSION && h <= OGL_AMI_MAX_DIMENSION);
    if (fail_bitmap) return NULL;
    ++bitmap_count; bitmap_width = w; bitmap_height = h;
    allocated_width = w + (rounded_bitmap ? 16U : 0U);
    allocated_height = h + (rounded_bitmap ? 4U : 0U);
    if (short_bitmap) --allocated_width;
    if (huge_bitmap) allocated_height = OGL_AMI_MAX_DIMENSION + 1U;
    owned_bitmap.Depth = !strcmp(profile, "rgb24-in32") ? 24 : 1; owned_bitmap.BytesPerRow = 65535;
    return &owned_bitmap;
}
void FreeBitMap(struct BitMap *bitmap)
{ assert(bitmap == &owned_bitmap && bitmap_count == 1 && !current_context && !blitter_busy); --bitmap_count; }
void BltBitMapRastPort(const struct BitMap *bitmap, LONG sx, LONG sy, struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h, ULONG minterm)
{
    assert(bitmap == &owned_bitmap && bitmap_count == 1 && gpu_finished && layer_locks == 1);
    assert(rp == &port && sx == 0 && sy == (LONG)(allocated_height - bitmap_height) && dx == window.BorderLeft && dy == window.BorderTop && minterm == 0xc0);
    assert(w > 0 && h > 0 && w <= (LONG)bitmap_width && h <= (LONG)bitmap_height);
    assert(w <= window.Width - window.BorderLeft - window.BorderRight);
    assert(h <= window.Height - window.BorderTop - window.BorderBottom);
    assert(sy + h <= (LONG)allocated_height);
    ++blit_calls; blitter_busy = 1; gpu_finished = 0;
}
void WaitBlit(void) { blitter_busy = 0; }
void *mglCreateContextFromBitMap(struct BitMap *value)
{
    assert(value == &owned_bitmap && bitmap_count == 1 && windows == 1 && !current_context && !outstanding);
    assert(vertex_setting == 256 && depth_setting == 24 && mipmap_setting == 1);
    ++creates; if (fail_context) return NULL;
    context.identifier = creates; current_context = &context; return current_context;
}
void mglDeleteContext(void)
{ assert(current_context == &context && windows == 1 && !outstanding); current_context = NULL; vertex_setting = depth_setting = mipmap_setting = 0; ++deletes; }
void mglSwitchDisplay(void)
{ assert(current_context == &context && windows == 1 && !layer_locks && !blitter_busy); ++flips; gpu_finished = 1; }
void LockLayerRom(struct Layer *value) { assert(value == &layer && !layer_locks); ++layer_locks; }
void UnlockLayerRom(struct Layer *value) { assert(value == &layer && layer_locks == 1); --layer_locks; }
void CurrentTime(ULONG *seconds, ULONG *micros)
{ clock_micros += 16000; *seconds = clock_micros / 1000000; *micros = clock_micros % 1000000; }
void GetRGB32(struct ColorMap *map, ULONG first, ULONG count, ULONG *values)
{
    assert(map == &palette && first < 4 && count == 1);
    values[0] = values[1] = values[2] = first == 1 ? 0UL : first == 2 ? 0xffffffffUL : 0x80808080UL;
}
void SetAPen(struct RastPort *value, ULONG pen)
{ assert(value == &port && pen == 2); port.FgPen = (UBYTE)pen; }
void SetBPen(struct RastPort *value, ULONG pen)
{ assert(value == &port && pen == 1); port.BgPen = (UBYTE)pen; }
void SetDrMd(struct RastPort *value, ULONG mode)
{ assert(value == &port && mode == JAM2); port.DrawMode = (UBYTE)mode; }
struct TextFont *OpenFont(struct TextAttr *attributes)
{
    unsigned int slot;
    assert(strcmp(attributes->ta_Name, "topaz.font") == 0);
    assert(attributes->ta_YSize == 8U || attributes->ta_YSize == 9U);
    if (fail_fonts) return NULL;
    slot = attributes->ta_YSize - 8U;
    osd_fonts[slot].tf_YSize = oversize_fonts ? 18 : attributes->ta_YSize;
    osd_fonts[slot].tf_XSize = 8;
    osd_fonts[slot].tf_Baseline = osd_fonts[slot].tf_YSize - 2;
    ++font_opens;
    return osd_fonts + slot;
}
void CloseFont(struct TextFont *value)
{
    assert(value == osd_fonts || value == osd_fonts + 1);
    ++font_closes;
    assert(font_closes <= font_opens);
}
void SetFont(struct RastPort *value, struct TextFont *selected)
{
    assert(value == &port && selected != NULL);
    value->Font = selected;
    value->TxHeight = selected->tf_YSize;
    value->TxWidth = selected->tf_XSize;
    value->TxBaseline = selected->tf_Baseline;
}
WORD TextLength(struct RastPort *value, CONST_STRPTR text, ULONG length)
{ assert(value == &port && text); return (WORD)(length * 8); }
void Move(struct RastPort *value, LONG x, LONG y)
{ assert(value == &port); port.cp_x = (WORD)x; port.cp_y = (WORD)y; }
void Text(struct RastPort *value, CONST_STRPTR text, ULONG length)
{
    assert(value == &port && text && length && layer_locks == 1);
    if (check_osd_layout)
    {
        assert(port.cp_y - port.TxBaseline > osd_last_bottom);
        osd_last_bottom = port.cp_y + port.TxHeight - port.TxBaseline - 1;
    }
    if (port.Font == osd_fonts) ++font_draws[0];
    if (port.Font == osd_fonts + 1) ++font_draws[1];
    assert(port.cp_x >= window.BorderLeft && port.cp_y - port.TxBaseline >= window.BorderTop);
    assert(port.cp_x + (long)length * 8 < window.Width - window.BorderRight);
    assert(port.cp_y + port.TxHeight - port.TxBaseline - 1 < window.Height - window.BorderBottom);
    ++text_calls;
}
void BeginRefresh(struct Window *value)
{ assert(value == &window && !refreshes && !outstanding); ++refreshes; }
void EndRefresh(struct Window *value, LONG complete)
{ assert(value == &window && complete && refreshes == 1); --refreshes; }
struct Message *GetMsg(struct MsgPort *value)
{
    assert(value == &messages_port && !outstanding);
    if (next_message == queued) return NULL;
    outstanding = 1; return (struct Message *)&messages[next_message++];
}
void ReplyMsg(struct Message *message)
{ assert(message && outstanding); outstanding = 0; ++replies; }
ULONG Wait(ULONG mask)
{
    assert(mask == (1UL << messages_port.mp_SigBit)); ++waits;
    assert(queued < 16); messages[queued++].Class = CLOSEWINDOW; return mask;
}
BOOL AutoRequest(struct Window *value, struct IntuiText *body, struct IntuiText *yes,
                 struct IntuiText *no, ULONG good, ULONG bad, WORD width, WORD height)
{
    (void)value; (void)yes; (void)no; (void)good; (void)bad; (void)width; (void)height;
    assert(expected_fatal && body->IText);
    if (short_stack) assert(strstr((char *)body->IText, "131072"));
    else if (missing_cgx) assert(strstr((char *)body->IText, "cybergraphics.library"));
    else assert(strstr((char *)body->IText, "dispatch ABI 5"));
    return 1;
}
void drv_notice(const char *message) { assert(message && *message); ++notices; }
void drv_reset_timing(void) {}
double drv_elapsed_time(Drv_clock *state, double sample)
{
    /* The clock accumulator's discontinuities are covered by driver_helpers;
     * this platform fixture checks delegation of the native seconds/micros. */
    assert(state != NULL && sample == (double)clock_micros / 1000000.0);
    return sample;
}

static void fatal_cleanup_check(void)
{
    ogl_ami_cleanup();
    assert(libraries == 0 && windows == 0 && locks == 0 && !current_context && !outstanding);
    puts("PASS MiniGL startup rejection: missing/invalid runtime or insufficient stack leaves no native resources");
}
static void draw_callback(void) { assert(!outstanding); ++draws; ogl_ami_present(); }
static void resize_callback(int width, int height)
{ assert(!outstanding); ++resize_calls; (void)ogl_ami_resize_context(width, height); }
static void key_callback(unsigned char key, int x, int y)
{ assert(!outstanding && key == '3' && x == 20 && y == 30); ++keys; }
static void mouse_callback(int button, int state, int x, int y)
{ assert(!outstanding && (button == OGL_AMI_LEFT_BUTTON || button == OGL_AMI_RIGHT_BUTTON)); assert(state == OGL_AMI_DOWN || state == OGL_AMI_UP); last_mouse_x = x; last_mouse_y = y; ++mouse_calls; }
static void motion_callback(int x, int y)
{ assert(!outstanding && x == 40 && y == 50); ++motions; }
static void enqueue(ULONG kind, UWORD code, WORD x, WORD y)
{ assert(queued < 16); messages[queued].Class = kind; messages[queued].Code = code; messages[queued].MouseX = x + 8; messages[queued].MouseY = y + 16; ++queued; }

int main(int argc, char **argv)
{
    int width, height;
    unsigned int before;
    double time;
    profile = argc > 1 ? argv[1] : "normal";
    fail_fonts = strcmp(profile, "fontfail") == 0;
    oversize_fonts = strcmp(profile, "fontlarge") == 0;
    font.tf_YSize = 8; font.tf_XSize = 8; font.tf_Baseline = 6;
    set_dispatch();
    screen.Width = 800; screen.Height = 600; screen.RastPort.BitMap = &screen.BitMap;
    palette.Count = 4; screen.ViewPort.ColorMap = &palette;
    port.Layer = &layer; port.Font = &font; port.TxHeight = 8; port.TxBaseline = 6; messages_port.mp_SigBit = 4;
    if (!strcmp(profile, "missing")) fail_library = 1;
    if (!strcmp(profile, "missingcgx")) missing_cgx = 1;
    if (!strcmp(profile, "wrong16")) bitmap_bytes = 2;
    if (!strcmp(profile, "wrong24")) bitmap_bytes = 3;
    if (!strcmp(profile, "unknownbitmap")) unknown_bitmap = 1;
    if (!strcmp(profile, "wrongformat")) bitmap_format = 10;
    if (!strcmp(profile, "rounded")) rounded_bitmap = 1;
    if (!strcmp(profile, "shortbitmap")) short_bitmap = 1;
    if (!strcmp(profile, "hugebitmap")) huge_bitmap = 1;
    if (!strcmp(profile, "abi")) dispatch.abiVersion = 3;
    if (!strcmp(profile, "futureabi")) dispatch.abiVersion = 6;
    if (!strcmp(profile, "shorttable")) dispatch.structSize = sizeof(dispatch) - 1;
    if (!strcmp(profile, "nullfunction")) dispatch.GLViewport = NULL;
    if (!strcmp(profile, "nomipmap")) dispatch.GLUBuild2DMipmaps = NULL;
    if (!strcmp(profile, "stub")) dispatch.backendFlags |= MINIGL_BACKEND_FLAG_STUB;
    if (!strcmp(profile, "wrongbackend")) dispatch.backendFlags = MINIGL_BACKEND_FLAG_CLASSIC;
    if (!strcmp(profile, "stack")) short_stack = 1;
    if (!strcmp(profile, "native")) native_screen = 1;
    if (!strcmp(profile, "windowfail")) fail_window = 1;
    if (!strcmp(profile, "lockfail")) fail_lock = 1;
    if (!strcmp(profile, "contextfail")) fail_context = 1;
    if (!strcmp(profile, "bitmapfail")) fail_bitmap = 1;
    expected_fatal = fail_library || missing_cgx || short_stack || strcmp(profile, "abi") == 0 ||
        strcmp(profile, "futureabi") == 0 || strcmp(profile, "shorttable") == 0 ||
        strcmp(profile, "nullfunction") == 0 || strcmp(profile, "stub") == 0 || strcmp(profile, "wrongbackend") == 0;
    if (expected_fatal) assert(atexit(fatal_cleanup_check) == 0);
    ogl_ami_init(&argc, argv); assert(libraries == 4);
    if (native_screen || fail_window || fail_lock || fail_context || fail_bitmap || unknown_bitmap || bitmap_bytes != 4 || bitmap_format != 12 || short_bitmap || huge_bitmap)
    {
        assert(!ogl_ami_create_window(10, 20, 640, 480)); ogl_ami_cleanup();
        assert(!libraries && !windows && !locks && !current_context);
        puts("PASS MiniGL window rejection: incompatible desktop and allocation failures release screens, windows, contexts and libraries");
        return 0;
    }
    assert(ogl_ami_create_window(10, 20, 640, 480));
    assert(!locks && windows == 1 && creates == 1 && ogl_ami_ready());
    ogl_ami_get_size(&width, &height); assert(width == 640 && height == 480);
    assert(ogl_ami_resize_context(width, height) == 1 && creates == 1);
    time = ogl_ami_time_now(); assert(ogl_ami_time_now() > time);
    ogl_ami_present(); assert(flips == 1);
    ogl_ami_draw_string("Status line", 10, 18);
    ogl_ami_draw_string("A long line that should wrap safely inside the window even when resized", 10, 10);
    port.TxHeight = 18; port.TxBaseline = 10;
    ogl_ami_draw_string("Bottom line descenders stay inside the client area", 10, 0);
    port.TxHeight = 8; port.TxBaseline = 6;
    assert(text_calls >= 2);
    check_osd_layout = TRUE; osd_last_bottom = -1;
    ogl_ami_draw_osd("Status line", "Help line");
    check_osd_layout = FALSE;
    assert(port.Font == &font);
    if (!fail_fonts && !oversize_fonts) assert(font_draws[1] > 0U);
    window.Width = 336; window.Height = 264;
    before = flips; ogl_ami_present(); assert(flips == before + 1 && !layer_locks && !blitter_busy);
    assert(ogl_ami_resize_context(320, 240) == 2 && creates == 2 && deletes == 1);
    check_osd_layout = TRUE; osd_last_bottom = -1;
    ogl_ami_draw_osd("Status line", "Help line");
    check_osd_layout = FALSE;
    assert(port.Font == &font);
    if (!fail_fonts && !oversize_fonts) assert(font_draws[0] > 0U);
    window.Width = 112; window.Height = 104;
    check_osd_layout = TRUE; osd_last_bottom = -1;
    ogl_ami_draw_osd("A deliberately long status line occupies the tiny client area",
                     "Help must not overlap the full status block");
    check_osd_layout = FALSE;
    window.Width = 336; window.Height = 264;
    before = creates; fail_context = 1; window.Width = 400;
    assert(ogl_ami_resize_context(384, 240) == 0 && !ogl_ami_ready() && creates == before + 1);
    assert(deletes == 2 && notices > 0); fail_context = 0;
    assert(ogl_ami_resize_context(384, 240) == 2 && ogl_ami_ready());
    before = creates; fail_bitmap = 1; window.Width = 416;
    assert(ogl_ami_resize_context(400, 240) == 0 && !ogl_ami_ready() && !bitmap_count && creates == before);
    fail_bitmap = 0; bitmap_bytes = 2;
    assert(ogl_ami_resize_context(400, 240) == 0 && !ogl_ami_ready() && !bitmap_count && creates == before);
    bitmap_bytes = 4; assert(ogl_ami_resize_context(400, 240) == 2 && ogl_ami_ready());
    before = creates; window.Width = 5000;
    assert(ogl_ami_resize_context(4984, 240) == 0 && !ogl_ami_ready() && creates == before);
    window.Width = 400;
    enqueue(VANILLAKEY, '3', 20, 30); /* retry the missing context before input */
    enqueue(MOUSEBUTTONS, SELECTDOWN, 10, 20);
    enqueue(MOUSEMOVE, 0, 40, 50);
    enqueue(MOUSEBUTTONS, SELECTUP, 40, 50);
    enqueue(MOUSEBUTTONS, MENUDOWN, 40, 50);
    enqueue(MOUSEBUTTONS, MENUUP, 40, 50);
    enqueue(NEWSIZE, 0, 0, 0);
    enqueue(REFRESHWINDOW, 0, 0, 0);
    enqueue(INACTIVEWINDOW, 0, 0, 0);
    enqueue(ACTIVEWINDOW, 0, 0, 0);
    ogl_ami_loop(draw_callback, resize_callback, key_callback, mouse_callback, motion_callback);
    assert(keys == 1 && mouse_calls == 4 && motions == 1 && draws == 1 && resize_calls == 2);
    assert(last_mouse_x == 40 && last_mouse_y == 50 && !refreshes && replies == queued && waits == 1);
    ogl_ami_cleanup(); ogl_ami_cleanup();
    assert(!libraries && !windows && !locks && !current_context && !outstanding && !bitmap_count && !blitter_busy);
    assert(font_opens == font_closes);
    assert(deletes == creates - 1); /* One context creation was injected to fail. */
    puts("PASS MiniGL platform: exact ABI gate, screen/window ownership, context resize/retry, native OSD/input/clock and cleanup");
    return 0;
}
