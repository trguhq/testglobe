/*
 * testglobe - native Amiga window/input for the shared OpenGL renderer.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "testglob.h"
#ifdef OGL_AMIGA
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#ifdef __VBCC__
#include <inline/exec_protos.h>
#include <inline/graphics_protos.h>
#include <inline/intuition_protos.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include "drv.h"
#include "drv_ext.h"
#include "ogl_ami.h"

/* These SDK wrapper macros have the same spelling as their table fields. */
#undef mglChooseVertexBufferSize
#undef mglChooseZBufferDepth
#undef mglProhibitMipMapping

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *MiniGLBase;
const MGLDispatchTable *MiniGLDispatch;

/* cybergraphics.fd and cybergraphx/cybergraphics.h: query storage through
 * its public ABI, as the native Amiga driver does, without exposing fields. */
#define OGL_AMI_CGX_BYTES 0x80000002UL
#define OGL_AMI_CGX_FORMAT 0x80000004UL
#define OGL_AMI_CGX_IS 0x80000008UL
#define OGL_AMI_BGRA32 12UL
#define OGL_AMI_SPECIALFMT 0x80UL

/* This is the SDK's only library vector; all rendering uses its validated
 * dispatch table and the SDK's normal C argument ABI. */
#ifdef __VBCC__
const MGLDispatchTable *ogl_ami_dispatch(__reg("a6") struct Library *base) =
    "\tjsr\t-30(a6)";
ULONG ogl_ami_cgx_attr(__reg("a6") struct Library *base,
    __reg("a0") struct BitMap *bitmap, __reg("d0") ULONG attribute) =
    "\tjsr\t-96(a6)";
#else
extern const MGLDispatchTable *ogl_ami_dispatch(struct Library *base);
extern ULONG ogl_ami_cgx_attr(struct Library *base, struct BitMap *bitmap,
    ULONG attribute);
#endif

static struct Library *ogl_ami_cgx_base;
static struct Window *ogl_ami_window;
static struct BitMap *ogl_ami_bitmap;
static int ogl_ami_running, ogl_ami_dirty, ogl_ami_active;
static int ogl_ami_width, ogl_ami_height;
static int ogl_ami_source_y;
static unsigned int ogl_ami_white, ogl_ami_black;

static int ogl_ami_dispatch_valid(const MGLDispatchTable *api)
{
    if (api == NULL || api->abiVersion != MINIGL_DISPATCH_ABI_VERSION ||
        api->structSize < sizeof(*api) || api->currentContext == NULL ||
        !(api->backendFlags & MINIGL_BACKEND_FLAG_PISTORM3D) ||
        (api->backendFlags & MINIGL_BACKEND_FLAG_STUB)) return FALSE;
    /* A structurally valid diagnostic/stub table is not a renderer. Check
     * every entry this target calls before creating a window or GL object. */
    return api->GLBegin && api->GLEnd && api->GLBindTexture && api->GLClear &&
        api->GLClearColor && api->GLColor4f && api->GLColor4ub && api->GLCullFace &&
        api->GLDeleteTextures && api->GLFlush && api->GLFrontFace &&
        api->GLGenTextures && api->GLGetError && api->GLGetFloatv &&
        api->GLGetIntegerv && api->GLGetString && api->GLIsEnabled &&
        api->GLIsTexture && api->GLLoadIdentity && api->GLMatrixMode &&
        api->GLPopMatrix && api->GLPushMatrix && api->GLRotatef &&
        api->GLShadeModel && api->GLTexCoord2fv && api->GLTexEnvi &&
        api->GLTexImage2D && api->GLTexParameteri && api->GLTranslatef &&
        api->GLVertex3fv && api->GLViewport && api->GLPixelStorei &&
        api->GLUPerspective && api->GLUBuild2DMipmaps &&
        api->GLGenLists && api->GLDeleteLists && api->GLNewList &&
        api->GLEndList && api->GLCallList && api->GLIsList &&
        api->MGLSetState && api->MGLCreateContextFromBitMap &&
        api->MGLDeleteContext && api->MGLSwitchDisplay &&
        api->mglChooseVertexBufferSize && api->mglChooseZBufferDepth &&
        api->mglProhibitMipMapping;
}

/* These are the SDK import-library entry points. Implementing its small
 * loader here avoids linking compiler-specific GCC startup/import objects. */
BOOL MiniGLOpen(void)
{
    const MGLDispatchTable *api;
    if (MiniGLBase != NULL && MiniGLDispatch != NULL) return TRUE;
    MiniGLBase = OpenLibrary(MINIGLNAME, MINIGL_VERSION);
    if (MiniGLBase == NULL) return FALSE;
    api = ogl_ami_dispatch(MiniGLBase);
    if (!ogl_ami_dispatch_valid(api))
    {
        CloseLibrary(MiniGLBase);
        MiniGLBase = NULL;
        return FALSE;
    }
    MiniGLDispatch = api;
    return TRUE;
}

void MiniGLClose(void)
{
    MiniGLDispatch = NULL;
    if (MiniGLBase != NULL) CloseLibrary(MiniGLBase);
    MiniGLBase = NULL;
}

int ogl_ami_ready(void)
{
    return MiniGLDispatch != NULL && *MiniGLDispatch->currentContext != NULL;
}

void ogl_ami_cleanup(void)
{
    struct Message *message;
    ogl_ami_running = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
    if (ogl_ami_ready()) mglDeleteContext();
    if (ogl_ami_bitmap != NULL) FreeBitMap(ogl_ami_bitmap);
    ogl_ami_bitmap = NULL;
    if (ogl_ami_window != NULL)
    {
        while ((message = GetMsg(ogl_ami_window->UserPort)) != NULL) ReplyMsg(message);
        CloseWindow(ogl_ami_window);
        ogl_ami_window = NULL;
    }
    MiniGLClose();
    if (ogl_ami_cgx_base != NULL) CloseLibrary(ogl_ami_cgx_base);
    ogl_ami_cgx_base = NULL;
    if (GfxBase != NULL) CloseLibrary((struct Library *)GfxBase);
    GfxBase = NULL;
    if (IntuitionBase != NULL) CloseLibrary((struct Library *)IntuitionBase);
    IntuitionBase = NULL;
    ogl_ami_width = ogl_ami_height = 0;
    ogl_ami_source_y = 0;
}

void ogl_ami_error(char *message)
{
    struct IntuiText body, button;
    ogl_ami_running = FALSE;
    if (IntuitionBase != NULL)
    {
        memset(&body, 0, sizeof(body));
        memset(&button, 0, sizeof(button));
        body.FrontPen = button.FrontPen = 1;
        body.DrawMode = button.DrawMode = JAM2;
        body.LeftEdge = body.TopEdge = 8;
        body.IText = (UBYTE *)message;
        button.IText = (UBYTE *)"Close";
        AutoRequest(ogl_ami_window, &body, NULL, &button, 0UL, 0UL, 360, 80);
    }
    else fprintf(stderr, "testglobe: %s\n", message);
    exit(1);
}

void ogl_ami_init(int *argc, char **argv)
{
    struct Task *task;
    ULONG lower, upper;
    (void)argc; (void)argv;
    ogl_ami_running = ogl_ami_active = ogl_ami_dirty = TRUE;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39UL);
    if (IntuitionBase == NULL) ogl_ami_error("AmigaOS 3.0 or later is required");
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39UL);
    if (GfxBase == NULL) ogl_ami_error("Cannot open graphics.library");
    task = FindTask(NULL);
    lower = task == NULL ? 0UL : (ULONG)task->tc_SPLower;
    upper = task == NULL ? 0UL : (ULONG)task->tc_SPUpper;
    if (upper < lower || upper - lower < OGL_AMI_STACK_BYTES)
        ogl_ami_error("Set Stack to 131072 bytes before starting PiStorm3D testglobe");
    ogl_ami_cgx_base = OpenLibrary("cybergraphics.library", 40UL);
    if (ogl_ami_cgx_base == NULL)
        ogl_ami_error("PiStorm3D requires cybergraphics.library V40 or later");
    if (!MiniGLOpen())
        ogl_ami_error("PiStorm3D minigl.library V29 with dispatch ABI 5 is required");
}

double ogl_ami_time_now(void)
{
    ULONG seconds, micros;
    double current;
    static double previous;
    if (IntuitionBase == NULL) return previous;
    CurrentTime(&seconds, &micros);
    current = (double)seconds + (double)micros / 1000000.0;
    if (current < previous) current = previous;
    previous = current;
    return current;
}

void ogl_ami_get_size(int *width, int *height)
{
    int w, h;
    w = h = 0;
    if (ogl_ami_window != NULL)
    {
        w = (int)ogl_ami_window->Width - ogl_ami_window->BorderLeft - ogl_ami_window->BorderRight;
        h = (int)ogl_ami_window->Height - ogl_ami_window->BorderTop - ogl_ami_window->BorderBottom;
    }
    if (width != NULL) *width = w;
    if (height != NULL) *height = h;
}

static void ogl_ami_text_pens(void)
{
    struct ColorMap *map;
    ULONG rgb[3], black_distance, white_distance, value;
    unsigned int count, pen, red, green, blue;
    ogl_ami_black = 0; ogl_ami_white = 1;
    if (ogl_ami_window == NULL) return;
    map = ogl_ami_window->WScreen->ViewPort.ColorMap;
    if (map == NULL || map->Count == 0) return;
    count = map->Count > 256 ? 256U : map->Count;
    black_distance = white_distance = 0xffffffffUL;
    for (pen = 0; pen < count; ++pen)
    {
        GetRGB32(map, pen, 1UL, rgb);
        red = (unsigned int)(rgb[0] >> 24);
        green = (unsigned int)(rgb[1] >> 24);
        blue = (unsigned int)(rgb[2] >> 24);
        value = (ULONG)red * red + (ULONG)green * green + (ULONG)blue * blue;
        if (value < black_distance) { black_distance = value; ogl_ami_black = pen; }
        red = 255U - red; green = 255U - green; blue = 255U - blue;
        value = (ULONG)red * red + (ULONG)green * green + (ULONG)blue * blue;
        if (value < white_distance) { white_distance = value; ogl_ami_white = pen; }
    }
}

int ogl_ami_resize_context(int width, int height)
{
    int actual_width, actual_height;
    ULONG bitmap_width, bitmap_height;
    ogl_ami_get_size(&actual_width, &actual_height);
    if (ogl_ami_ready() && width == ogl_ami_width && height == ogl_ami_height &&
        width == actual_width && height == actual_height) return 1;
    /* PiStorm3D V29's mglResizeContext only resizes fullscreen contexts.
     * Recreate our bitmap context and let the driver rebuild GL objects. */
    if (ogl_ami_ready()) mglDeleteContext();
    if (ogl_ami_bitmap != NULL) FreeBitMap(ogl_ami_bitmap);
    ogl_ami_bitmap = NULL;
    ogl_ami_width = ogl_ami_height = 0;
    ogl_ami_source_y = 0;
    if (ogl_ami_window == NULL || width != actual_width || height != actual_height ||
        width <= 0 || height <= 0 || width > OGL_AMI_MAX_DIMENSION ||
        height > OGL_AMI_MAX_DIMENSION) return 0;
    /* Match MiniGL's own RTG allocation, keeping presentation under our
     * control. Its window presenter uses live dimensions, which can exceed
     * an older framebuffer during an asynchronous Intuition resize. */
    ogl_ami_bitmap = AllocBitMap((ULONG)width, (ULONG)height, 32UL,
        BMF_MINPLANES | BMF_DISPLAYABLE | BMF_CLEAR | OGL_AMI_SPECIALFMT |
        (OGL_AMI_BGRA32 << 24),
        ogl_ami_window->WScreen->RastPort.BitMap);
    if (ogl_ami_bitmap == NULL)
    {
        drv_notice("PiStorm3D framebuffer unavailable; resize or press a key to retry");
        return 0;
    }
    /* PiStorm3D stores BGRA bytes. Row padding cannot prove either the
     * pixel size or order; color depth can report 24 for 32-bit storage. */
    if (!ogl_ami_cgx_attr(ogl_ami_cgx_base, ogl_ami_bitmap, OGL_AMI_CGX_IS) ||
        ogl_ami_cgx_attr(ogl_ami_cgx_base, ogl_ami_bitmap, OGL_AMI_CGX_BYTES) != 4UL ||
        ogl_ami_cgx_attr(ogl_ami_cgx_base, ogl_ami_bitmap, OGL_AMI_CGX_FORMAT) != OGL_AMI_BGRA32)
    {
        FreeBitMap(ogl_ami_bitmap);
        ogl_ami_bitmap = NULL;
        drv_notice("PiStorm3D requires a BGRA32 RTG framebuffer");
        return 0;
    }
    bitmap_width = GetBitMapAttr(ogl_ami_bitmap, BMA_WIDTH);
    bitmap_height = GetBitMapAttr(ogl_ami_bitmap, BMA_HEIGHT);
    if (bitmap_width < (ULONG)width || bitmap_height < (ULONG)height ||
        bitmap_width > OGL_AMI_MAX_DIMENSION || bitmap_height > OGL_AMI_MAX_DIMENSION)
    {
        FreeBitMap(ogl_ami_bitmap);
        ogl_ami_bitmap = NULL;
        drv_notice("PiStorm3D framebuffer dimensions are unsupported");
        return 0;
    }
    /* Deleting a context clears MiniGL's creation settings. Reapply them
     * on every creation and retry, including after a window resize. */
    MiniGLDispatch->mglChooseVertexBufferSize(OGL_AMI_VERTEX_LIMIT);
    MiniGLDispatch->mglChooseZBufferDepth(24);
    MiniGLDispatch->mglProhibitMipMapping(GL_FALSE);
    if (mglCreateContextFromBitMap(ogl_ami_bitmap) == NULL)
    {
        FreeBitMap(ogl_ami_bitmap);
        ogl_ami_bitmap = NULL;
        drv_notice("PiStorm3D context unavailable; resize or press a key to retry");
        return 0;
    }
    ogl_ami_width = width; ogl_ami_height = height;
    /* OpenGL's requested viewport starts at the bitmap's bottom edge. */
    ogl_ami_source_y = (int)bitmap_height - height;
    ogl_ami_dirty = TRUE;
    drv_reset_timing();
    return 2;
}

int ogl_ami_create_window(int x, int y, int width, int height)
{
    struct Screen *screen;
    struct NewWindow settings;
    int max_width, max_height;
    if (MiniGLDispatch == NULL || ogl_ami_window != NULL) return FALSE;
    screen = LockPubScreen((STRPTR)"Workbench");
    if (screen == NULL) return FALSE;
    /* Never hand a native planar screen to the GPU/RTG path. Its bitmap
     * metadata remains opaque; the context verifies the actual format. */
    if (screen->RastPort.BitMap == NULL ||
        !ogl_ami_cgx_attr(ogl_ami_cgx_base, screen->RastPort.BitMap, OGL_AMI_CGX_IS))
    {
        UnlockPubScreen(NULL, screen);
        drv_notice("PiStorm3D requires an RTG Workbench screen");
        return FALSE;
    }
    max_width = screen->Width;
    max_height = screen->Height;
    if (max_width < 112 || max_height < 104) { UnlockPubScreen(NULL, screen); return FALSE; }
    if (max_width > OGL_AMI_MAX_DIMENSION) max_width = OGL_AMI_MAX_DIMENSION;
    if (max_height > OGL_AMI_MAX_DIMENSION) max_height = OGL_AMI_MAX_DIMENSION;
    if (x < 0 || x > screen->Width - 112) x = 0;
    if (y < 0 || y > screen->Height - 104) y = 0;
    if (width <= 0) width = 640;
    if (height <= 0) height = 480;
    if (max_width > screen->Width - x) max_width = screen->Width - x;
    if (max_height > screen->Height - y) max_height = screen->Height - y;
    /* Estimate borders before creation, then query their actual dimensions. */
    width = width > max_width - 16 ? max_width : width + 16;
    height = height > max_height - 24 ? max_height : height + 24;
    if (width < 112) width = 112;
    if (height < 104) height = 104;
    memset(&settings, 0, sizeof(settings));
    settings.LeftEdge = (WORD)x; settings.TopEdge = (WORD)y;
    settings.Width = (WORD)width; settings.Height = (WORD)height;
    settings.DetailPen = 0; settings.BlockPen = 1;
    settings.IDCMPFlags = CLOSEWINDOW | NEWSIZE | REFRESHWINDOW | MOUSEBUTTONS |
        MOUSEMOVE | VANILLAKEY | ACTIVEWINDOW | INACTIVEWINDOW;
    /* Mouse, bitmap, and text coordinates use explicit client offsets. */
    settings.Flags = WINDOWCLOSE | WINDOWDRAG | WINDOWDEPTH | WINDOWSIZING |
        ACTIVATE | SMART_REFRESH | REPORTMOUSE | RMBTRAP;
    settings.Title = (UBYTE *)"testglobe - PiStorm3D OpenGL";
    settings.MinWidth = 112; settings.MinHeight = 104;
    settings.MaxWidth = (UWORD)max_width; settings.MaxHeight = (UWORD)max_height;
    settings.Type = PUBLICSCREEN; settings.Screen = screen;
    ogl_ami_window = OpenWindow(&settings);
    UnlockPubScreen(NULL, screen);
    if (ogl_ami_window == NULL) return FALSE;
    ogl_ami_text_pens();
    ogl_ami_get_size(&width, &height);
    return ogl_ami_resize_context(width, height) != 0;
}

void ogl_ami_present(void)
{
    int width, height;
    if (!ogl_ami_ready() || ogl_ami_window == NULL || ogl_ami_bitmap == NULL) return;
    /* Bitmap contexts finish GPU work and unlock the bitmap without a
     * window blit. Then copy only pixels within our allocation, even if
     * Intuition resized the window during rendering. */
    mglSwitchDisplay();
    LockLayerRom(ogl_ami_window->RPort->Layer);
    ogl_ami_get_size(&width, &height);
    if (width != ogl_ami_width || height != ogl_ami_height) ogl_ami_dirty = TRUE;
    if (width > ogl_ami_width) width = ogl_ami_width;
    if (height > ogl_ami_height) height = ogl_ami_height;
    if (width > 0 && height > 0)
        BltBitMapRastPort(ogl_ami_bitmap, 0, ogl_ami_source_y, ogl_ami_window->RPort,
            ogl_ami_window->BorderLeft, ogl_ami_window->BorderTop,
            width, height, 0xc0UL);
    UnlockLayerRom(ogl_ami_window->RPort->Layer);
    /* The next frame may reuse or free the source bitmap. */
    WaitBlit();
}

void ogl_ami_post_redisplay(void)
{
    ogl_ami_dirty = TRUE;
}

void ogl_ami_draw_string(const char *text, int x, int y)
{
    struct RastPort *port;
    const char *line;
    unsigned int count;
    int width, height, baseline, step, lines, descent, bottom;
    if (ogl_ami_window == NULL || text == NULL || *text == 0) return;
    port = ogl_ami_window->RPort;
    LockLayerRom(port->Layer);
    ogl_ami_get_size(&width, &height);
    if (x < 0) x = 0;
    if (x >= width - 2 || width <= 0 || height <= 0) goto done;
    step = port->TxHeight + 1;
    if (step < 2) step = 9;
    descent = port->TxHeight - port->TxBaseline - 1;
    if (descent < 0) descent = 0;
    bottom = height - 2 - descent;
    lines = 0; line = text;
    while (*line != 0)
    {
        count = (unsigned int)strlen(line);
        while (count > 0 && TextLength(port, (STRPTR)line, count) > width - x - 2) --count;
        if (count == 0) goto done;
        ++lines; line += count;
    }
    baseline = height - y;
    if (baseline + (lines - 1) * step > bottom)
        baseline = bottom - (lines - 1) * step;
    if (baseline < port->TxBaseline) baseline = port->TxBaseline;
    SetAfPt(port, NULL, 0);
    SetDrMd(port, JAM2);
    SetAPen(port, ogl_ami_white); SetBPen(port, ogl_ami_black);
    while (*text != 0 && baseline <= bottom)
    {
        count = (unsigned int)strlen(text);
        while (count > 0 && TextLength(port, (STRPTR)text, count) > width - x - 2) --count;
        if (count == 0) break;
        Move(port, x + ogl_ami_window->BorderLeft, baseline + ogl_ami_window->BorderTop);
        Text(port, (STRPTR)text, count);
        baseline += step; text += count;
    }
done:
    UnlockLayerRom(port->Layer);
}

void ogl_ami_loop(void (*draw)(void), void (*resize)(int, int),
    void (*key)(unsigned char, int, int), void (*mouse)(int, int, int, int),
    void (*motion)(int, int))
{
    struct IntuiMessage *message;
    ULONG kind;
    UWORD code;
    int x, y, width, height;
    unsigned int drained;
    if (ogl_ami_window == NULL) return;
    ogl_ami_dirty = TRUE;
    while (ogl_ami_running)
    {
        for (drained = 0; drained < 64U && ogl_ami_running; ++drained)
        {
            message = (struct IntuiMessage *)GetMsg(ogl_ami_window->UserPort);
            if (message == NULL) break;
            kind = message->Class; code = message->Code;
            x = message->MouseX - ogl_ami_window->BorderLeft;
            y = message->MouseY - ogl_ami_window->BorderTop;
            ReplyMsg((struct Message *)message);
            switch (kind)
            {
                case CLOSEWINDOW: ogl_ami_running = FALSE; break;
                case NEWSIZE:
                    ogl_ami_get_size(&width, &height); resize(width, height);
                    ogl_ami_dirty = TRUE;
                    break;
                case REFRESHWINDOW:
                    BeginRefresh(ogl_ami_window); EndRefresh(ogl_ami_window, TRUE);
                    ogl_ami_text_pens(); ogl_ami_dirty = TRUE;
                    break;
                case ACTIVEWINDOW:
                    ogl_ami_active = TRUE; ogl_ami_text_pens();
                    drv_reset_timing(); ogl_ami_dirty = TRUE;
                    break;
                case INACTIVEWINDOW:
                    ogl_ami_active = FALSE;
                    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
                    drv_reset_timing();
                    break;
                case VANILLAKEY:
                    if (!ogl_ami_ready())
                    { ogl_ami_get_size(&width, &height); resize(width, height); }
                    key((unsigned char)code, x, y);
                    break;
                case MOUSEBUTTONS:
                    if (code == SELECTDOWN || code == SELECTUP)
                        mouse(OGL_AMI_LEFT_BUTTON, code == SELECTDOWN ? OGL_AMI_DOWN : OGL_AMI_UP, x, y);
                    else if (code == MENUDOWN || code == MENUUP)
                        mouse(OGL_AMI_RIGHT_BUTTON, code == MENUDOWN ? OGL_AMI_DOWN : OGL_AMI_UP, x, y);
                    break;
                case MOUSEMOVE: motion(x, y); break;
                default: break;
            }
        }
        if (!ogl_ami_running) break;
        /* A busy input queue can leave NEWSIZE pending after the batch
         * above. Synchronize the context before submitting another frame. */
        ogl_ami_get_size(&width, &height);
        if (ogl_ami_ready() && (width != ogl_ami_width || height != ogl_ami_height))
        { resize(width, height); ogl_ami_dirty = TRUE; }
        if (ogl_ami_dirty)
        {
            ogl_ami_dirty = FALSE;
            if (ogl_ami_ready()) draw();
            else ogl_ami_draw_string("PiStorm3D context unavailable; resize or press a key to retry", 4, 24);
        }
        /* draw() reposts while rotating. Without a context or an active
         * window, wait for input rather than spinning on a failed frame. */
        if (!ogl_ami_ready() || !ogl_ami_active) ogl_ami_dirty = FALSE;
        if (!ogl_ami_dirty && drained < 64U)
            Wait(1UL << ogl_ami_window->UserPort->mp_SigBit);
    }
}
#endif
