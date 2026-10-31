/*
 * testglobe - resizable Amiga Workbench window, native chipset and optional RTG.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile this object for the original 68000 with software IEEE arithmetic.
 * Only the separately compiled transform/FPU objects may use newer opcodes.
 * The window borrows Workbench's colors; it never alters a shared palette.
 */
#include "testglob.h"
#ifdef DRV_AMI
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#ifdef __VBCC__
/* Some NDK proto headers predate VBCC. These declarations supply its
 * register-call interface instead of linking C-stack calls to OS vectors. */
#include <inline/exec_protos.h>
#include <inline/graphics_protos.h>
#include <inline/intuition_protos.h>
#endif
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include "drv.h"
#include "drv_ext.h"
#include "globe.h"
#include "glob_ext.h"

/* Optional libraries are called through their documented m68k ABI. Keeping
 * these four private veneers here avoids imposing an RTG SDK on native builds.
 * Offsets/registers: cybergraphics.fd and Picasso96API_lib.fd; attributes:
 * cybergraphx/cybergraphics.h and libraries/Picasso96.h. */
#if AMI_RTG
#define AMI_CGX_BYTES 0x80000002UL
#define AMI_CGX_DEPTH 0x80000007UL
#define AMI_CGX_IS 0x80000008UL
#define AMI_P96_DEPTH 2UL
#define AMI_P96_BITS 6UL
#define AMI_P96_IS 8UL
#ifdef __VBCC__
ULONG ami_cgx_attr(__reg("a6") struct Library *base,
    __reg("a0") struct BitMap *bitmap, __reg("d0") ULONG attribute) =
    "\tjsr\t-96(a6)";
ULONG ami_cgx_fill(__reg("a6") struct Library *base,
    __reg("a1") struct RastPort *port, __reg("d0") UWORD x,
    __reg("d1") UWORD y, __reg("d2") UWORD width,
    __reg("d3") UWORD height, __reg("d4") ULONG argb) =
    "\tjsr\t-150(a6)";
ULONG ami_p96_attr(__reg("a6") struct Library *base,
    __reg("a0") struct BitMap *bitmap, __reg("d0") ULONG attribute) =
    "\tjsr\t-42(a6)";
void ami_p96_fill(__reg("a6") struct Library *base,
    __reg("a1") struct RastPort *port, __reg("d0") UWORD x0,
    __reg("d1") UWORD y0, __reg("d2") UWORD x1,
    __reg("d3") UWORD y1, __reg("d4") ULONG argb) =
    "\tjsr\t-126(a6)";
#else
/* Host contract tests provide recording implementations of these veneers. */
extern ULONG ami_cgx_attr(struct Library *, struct BitMap *, ULONG);
extern ULONG ami_cgx_fill(struct Library *, struct RastPort *,
                         UWORD, UWORD, UWORD, UWORD, ULONG);
extern ULONG ami_p96_attr(struct Library *, struct BitMap *, ULONG);
extern void ami_p96_fill(struct Library *, struct RastPort *,
                        UWORD, UWORD, UWORD, UWORD, ULONG);
#endif
#endif

#define AMI_MAX_POLY SOFT3D_MAX_POLY
#define AMI_AREA_VECTORS (AMI_MAX_POLY + 1)
#define AMI_MAX_DEPTH 8
#define AMI_MIN_WIDTH 96
#define AMI_MIN_HEIGHT 80
#define AMI_OCS_MAX_WIDTH 992U
#define AMI_OCS_MAX_HEIGHT 1024U

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;

static struct Window *ami_window;
static struct BitMap ami_bitmap;
static struct BitMap *ami_rtg_bitmap;
static int ami_rtg; /* 0: native, 1: CyberGraphX, 2: Picasso96 */
static unsigned int ami_storage_depth;
#if AMI_RTG
static struct Library *ami_cgx_base, *ami_p96_base;
#endif
static struct RastPort ami_back_port;
static struct RastPort *ami_draw_port;
static struct TmpRas ami_tmp_ras;
static struct AreaInfo ami_area_info;
/* AreaInfo needs five bytes per vector and word-aligned storage. */
static UWORD ami_area_storage[(AMI_AREA_VECTORS * 5 + 1) / 2];
static PLANEPTR ami_scratch;
static unsigned int ami_raster_width, ami_raster_height, ami_depth;
static unsigned int ami_back_planes;
static int ami_running, ami_ready, ami_active, ami_dirty;
static int ami_rendering, ami_paused, ami_area_failed;
static int ami_old_x, ami_old_y;
static int ami_cpu = 68000, ami_has_fpu;
static int ami_big_blits, ami_unsafe_bitmap;
static const char *ami_fpu_name = "none";
static const char *ami_chipset = "OCS/ECS";
static unsigned int ami_aspect_num = 1, ami_aspect_den = 1;
static unsigned char ami_palette[256][3];
static unsigned char ami_pen_a[256], ami_pen_b[256], ami_mix[256];
static unsigned int ami_color_count;
static int ami_palette_valid;
static unsigned int ami_black, ami_white;
static UWORD ami_pattern[4];
static double ami_auto_time;
static Soft3d_project_cache *ami_project_cache;
static unsigned long ami_previous_sequence;
static unsigned int ami_previous_count;
static union {
    long fixed[3][3];
    float floating[3][3];
} ami_matrix;
static union {
    Soft3d_fixed_point fixed[SOFT3D_MAX_VERTICES];
    Soft3d_float_point floating[SOFT3D_MAX_VERTICES];
} ami_points;

static void ami_drop_back_buffer(void)
{
    unsigned int plane;
    if (GfxBase != NULL) WaitBlit();
    for (plane = 0; plane < ami_back_planes; ++plane)
    {
        if (ami_bitmap.Planes[plane] != NULL)
            FreeRaster(ami_bitmap.Planes[plane], ami_raster_width, ami_raster_height);
        ami_bitmap.Planes[plane] = NULL;
    }
    ami_back_planes = 0;
#if AMI_RTG
    if (ami_rtg_bitmap != NULL) FreeBitMap(ami_rtg_bitmap);
#endif
    ami_rtg_bitmap = NULL;
}

static void ami_drop_rasters(void)
{
    ami_drop_back_buffer();
    if (ami_window != NULL)
    {
        ami_window->RPort->AreaInfo = NULL;
        ami_window->RPort->TmpRas = NULL;
    }
    if (ami_scratch != NULL)
        FreeRaster(ami_scratch, ami_raster_width, ami_raster_height);
    ami_scratch = NULL;
    ami_raster_width = ami_raster_height = 0;
    ami_draw_port = NULL;
}

void drv_release_optional(void)
{
    /* The common recovery layer calls this only between frames. */
    if (ami_rendering) return;
    ami_drop_back_buffer();
    free(ami_project_cache);
    ami_project_cache = NULL;
    ami_previous_sequence = 0;
    ami_previous_count = 0;
    ami_dirty = TRUE;
    drv_notice("Amiga direct drawing; optional caches released");
}

static void ami_cleanup(void)
{
    struct Message *message;
    ami_running = ami_ready = ami_active = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
    ami_drop_rasters();
    free(ami_project_cache);
    ami_project_cache = NULL;
    if (ami_window != NULL)
    {
        while ((message = GetMsg(ami_window->UserPort)) != NULL) ReplyMsg(message);
        CloseWindow(ami_window);
        ami_window = NULL;
    }
    ami_data_close();
#if AMI_RTG
    if (ami_cgx_base != NULL) CloseLibrary(ami_cgx_base);
    if (ami_p96_base != NULL) CloseLibrary(ami_p96_base);
    ami_cgx_base = ami_p96_base = NULL;
#endif
    if (GfxBase != NULL) CloseLibrary((struct Library *)GfxBase);
    GfxBase = NULL;
    if (IntuitionBase != NULL) CloseLibrary((struct Library *)IntuitionBase);
    IntuitionBase = NULL;
}

void ami_error(char *message)
{
    struct IntuiText body, button;
    ami_running = ami_ready = ami_active = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
    if (GfxBase != NULL) WaitBlit();
    if (IntuitionBase != NULL && !ami_unsafe_bitmap)
    {
        memset(&body, 0, sizeof(body));
        memset(&button, 0, sizeof(button));
        body.FrontPen = button.FrontPen = 1;
        body.DrawMode = button.DrawMode = JAM2;
        body.LeftEdge = body.TopEdge = 8;
        body.IText = (UBYTE *)message;
        button.IText = (UBYTE *)"Close";
        AutoRequest(ami_window, &body, NULL, &button, 0UL, 0UL, 320, 80);
    }
    else fprintf(stderr, "testglobe: %s\n", message);
    exit(1);
}

double ami_time_now(void)
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

static void ami_detect_processor(void)
{
    UWORD flags;
    flags = SysBase->AttnFlags;
    ami_cpu = 68000;
    if (flags & AFF_68010) ami_cpu = 68010;
    if (flags & AFF_68020) ami_cpu = 68020;
    if (flags & AFF_68030) ami_cpu = 68030;
    if (flags & AFF_68040) ami_cpu = 68040;
#ifdef AFF_68060
    if (flags & AFF_68060) ami_cpu = 68060;
#endif
    ami_has_fpu = ami_cpu >= 68020 &&
                  (flags & (AFF_68881 | AFF_68882 | AFF_FPU40)) != 0;
    ami_fpu_name = "none";
    if (ami_has_fpu)
    {
        if (flags & AFF_FPU40) ami_fpu_name = ami_cpu >= 68060 ? "68060 FPU" : "68040 FPU";
        else ami_fpu_name = flags & AFF_68882 ? "68882" : "68881";
    }
    /* ChipRevBits0 is an extended GfxBase field. Older libraries support
     * the common planar path without needing to identify their chipset. */
    ami_chipset = "OCS/ECS";
    ami_big_blits = FALSE;
    if (GfxBase->LibNode.lib_Version >= 39)
    {
        /* Agnus/Alice supplies the blitter. An ECS Denise alone does not
         * remove OCS transfer limits. Unknown old-OS hardware stays bounded. */
        ami_big_blits = (GfxBase->ChipRevBits0 &
                       (GFXF_HR_AGNUS | GFXF_AA_ALICE)) != 0;
        if (GfxBase->ChipRevBits0 & GFXF_AA_ALICE) ami_chipset = "AGA";
        else if (GfxBase->ChipRevBits0 & (GFXF_HR_AGNUS | GFXF_HR_DENISE))
            ami_chipset = "ECS";
        else ami_chipset = "OCS";
    }
}

void drv_init(int *argc, char **argv)
{
    Drv_result result;
    struct Task *task;
    ULONG stack_lower, stack_upper;
    ami_ready = ami_rendering = ami_paused = FALSE;
    ami_running = ami_active = ami_dirty = TRUE;
    ami_palette_valid = FALSE;
    ami_unsafe_bitmap = FALSE;
    if (atexit(ami_cleanup) != 0) ami_error("Cannot register Amiga cleanup");
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 34UL);
    if (IntuitionBase == NULL) ami_error("AmigaOS 1.3 or later is required");
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 34UL);
    if (GfxBase == NULL) ami_error("Cannot open graphics.library");
    task = FindTask(NULL);
    stack_lower = task == NULL ? 0UL : (ULONG)task->tc_SPLower;
    stack_upper = task == NULL ? 0UL : (ULONG)task->tc_SPUpper;
    if (stack_upper < stack_lower || stack_upper - stack_lower < AMI_STACK_BYTES)
        ami_error("Set the program's Stack to 32768 bytes, then start it again.");
    ami_detect_processor();
#if AMI_RTG
    if (GfxBase->LibNode.lib_Version >= 39 &&
        IntuitionBase->LibNode.lib_Version >= 39)
    {
        ami_cgx_base = OpenLibrary("cybergraphics.library", 40UL);
        ami_p96_base = OpenLibrary("Picasso96API.library", 2UL);
    }
#endif
    result = ami_data_init(argc == NULL ? 0 : *argc, argv);
    if (result != DRV_OK) drv_notice("Earth data unavailable; select a level to retry");
}

static unsigned int ami_nearest(unsigned int red, unsigned int green,
                                 unsigned int blue, int excluded)
{
    unsigned int pen, best;
    long r, g, b;
    unsigned long distance, minimum;
    minimum = 0xffffffffUL;
    best = 0;
    for (pen = 0; pen < ami_color_count; ++pen)
    {
        if ((int)pen == excluded) continue;
        r = (long)red - ami_palette[pen][0];
        g = (long)green - ami_palette[pen][1];
        b = (long)blue - ami_palette[pen][2];
        distance = (unsigned long)(r * r + g * g + b * b);
        if (distance < minimum) { minimum = distance; best = pen; }
    }
    return best;
}

static void ami_map_palette(void)
{
    struct ColorMap *colors;
    ULONG rgb[3], packed;
    unsigned char sample[3];
    unsigned int color, pen, a, b, channel, count;
    int changed;
    long numerator, denominator, delta, difference, mix;
    if (ami_window == NULL || ami_depth < 1) return;
    colors = ami_window->WScreen->ViewPort.ColorMap;
    count = ami_depth > AMI_MAX_DEPTH ? 256U : 1U << ami_depth;
    if (colors != NULL && colors->Count != 0 && colors->Count < count)
        count = colors->Count;
    if (count < 1) count = 1;
    changed = !ami_palette_valid || count != ami_color_count;
    ami_color_count = count;
    for (pen = 0; pen < ami_color_count; ++pen)
    {
        if (colors != NULL && GfxBase->LibNode.lib_Version >= 39)
        {
            GetRGB32(colors, pen, 1UL, rgb);
            for (channel = 0; channel < 3; ++channel)
                sample[channel] = (unsigned char)(rgb[channel] >> 24);
        }
        else
        {
            packed = colors == NULL ? (pen == 0 ? 0UL : 0xfffUL) : GetRGB4(colors, pen);
            sample[0] = (unsigned char)(((packed >> 8) & 15UL) * 17UL);
            sample[1] = (unsigned char)(((packed >> 4) & 15UL) * 17UL);
            sample[2] = (unsigned char)((packed & 15UL) * 17UL);
        }
        for (channel = 0; channel < 3; ++channel)
        {
            if (ami_palette[pen][channel] != sample[channel]) changed = TRUE;
            ami_palette[pen][channel] = sample[channel];
        }
    }
    /* Refresh/resize often leaves Workbench's pens unchanged. Rebuild the
     * two-pen lookup only when the native values or count actually change. */
    if (changed)
    {
        ami_black = ami_nearest(0, 0, 0, -1);
        ami_white = ami_nearest(255, 255, 255, -1);
        for (color = 0; ami_depth <= 8U && color < 256; ++color)
        {
            a = ami_nearest(earth_cmap[color][0], earth_cmap[color][1], earth_cmap[color][2], -1);
            b = ami_nearest(earth_cmap[color][0], earth_cmap[color][1], earth_cmap[color][2], (int)a);
            numerator = denominator = 0;
            for (channel = 0; channel < 3; ++channel)
            {
                delta = (long)ami_palette[b][channel] - ami_palette[a][channel];
                difference = (long)earth_cmap[color][channel] - ami_palette[a][channel];
                numerator += delta * difference;
                denominator += delta * delta;
            }
            mix = denominator == 0 ? 0 : (numerator * 16L + denominator / 2L) / denominator;
            if (mix < 0) mix = 0;
            if (mix > 16) mix = 16;
            ami_pen_a[color] = (unsigned char)a;
            ami_pen_b[color] = (unsigned char)b;
            ami_mix[color] = (unsigned char)mix;
        }
    }
    ami_palette_valid = TRUE;
    drv_bits_per_pixel = ami_storage_depth;
    drv_colormapped = ami_depth <= 8U;
    drv_colormap_bits = ami_depth > 24U ? 24U : ami_depth;
    drv_colormap_colors = ami_depth <= 8U ? ami_color_count : 1UL << drv_colormap_bits;
    drv_colors = ami_depth > 8U ? drv_colormap_colors : ami_color_count;
    /* Report storage separately: e.g. RGB15 occupies two bytes, RGB32 four. */
    drv_bytes_per_pixel = (ami_storage_depth + 7U) / 8U;
}

static unsigned int ami_gcd(unsigned int a, unsigned int b)
{
    unsigned int remainder;
    while (b != 0) { remainder = a % b; a = b; b = remainder; }
    return a;
}

static void ami_pixel_aspect(void)
{
    struct DisplayInfo display;
    ULONG mode;
    unsigned int divisor;
    UWORD modes;
    if (ami_rtg)
    {
        /* PAL/NTSC chipset timing does not describe an RTG monitor. */
        ami_aspect_num = ami_aspect_den = 1U;
        return;
    }
    modes = ami_window->WScreen->ViewPort.Modes;
    ami_aspect_num = (GfxBase->DisplayFlags & PAL) ? 15U : 6U;
    ami_aspect_den = (GfxBase->DisplayFlags & PAL) ? 16U : 5U;
    if (modes & HIRES) ami_aspect_num *= 2U;
    if (modes & LACE) ami_aspect_den *= 2U;
    if (GfxBase->LibNode.lib_Version >= 36)
    {
        mode = GetVPModeID(&ami_window->WScreen->ViewPort);
        memset(&display, 0, sizeof(display));
        if (mode != INVALID_ID && GetDisplayInfoData(NULL, (UBYTE *)&display,
                    sizeof(display), DTAG_DISP, mode) >= sizeof(display) &&
            display.Resolution.x > 0 && display.Resolution.y > 0)
        {
            ami_aspect_num = (unsigned int)display.Resolution.y;
            ami_aspect_den = (unsigned int)display.Resolution.x;
        }
    }
    divisor = ami_gcd(ami_aspect_num, ami_aspect_den);
    ami_aspect_num /= divisor;
    ami_aspect_den /= divisor;
    if (ami_aspect_num > 256U || ami_aspect_den > 256U)
        ami_aspect_num = ami_aspect_den = 1U;
}

/* Query a real OS bitmap, never a copied opaque RTG BitMap structure. */
static int ami_bitmap_format(struct BitMap *bitmap, int *backend,
                             unsigned int *depth, unsigned int *storage)
{
#if AMI_RTG
    ULONG bits, bytes, stored;
#endif
    *backend = 0;
    if (bitmap == NULL) return FALSE;
#if AMI_RTG
    if (ami_cgx_base != NULL && ami_cgx_attr(ami_cgx_base, bitmap, AMI_CGX_IS))
    {
        bits = ami_cgx_attr(ami_cgx_base, bitmap, AMI_CGX_DEPTH);
        bytes = ami_cgx_attr(ami_cgx_base, bitmap, AMI_CGX_BYTES);
        if (bytes < 1UL || bytes > 4UL) return FALSE;
        stored = bytes * 8UL;
        *backend = 1;
    }
    else if (ami_p96_base != NULL && ami_p96_attr(ami_p96_base, bitmap, AMI_P96_IS))
    {
        bits = ami_p96_attr(ami_p96_base, bitmap, AMI_P96_BITS);
        stored = ami_p96_attr(ami_p96_base, bitmap, AMI_P96_DEPTH);
        *backend = 2;
    }
    else { bits = stored = 0; }
    if (*backend)
    {
        if ((bits != 8UL && bits != 15UL && bits != 16UL && bits != 24UL && bits != 32UL) ||
            (stored != 8UL && stored != 16UL && stored != 24UL && stored != 32UL) ||
            stored < bits || (bits == 8UL && stored != 8UL)) return FALSE;
        *depth = (unsigned int)bits;
        *storage = (unsigned int)stored;
        return TRUE;
    }
#endif
    /* An unidentified nonstandard bitmap cannot safely use the chipset path,
     * including in a native-only executable on an RTG Workbench. */
    if (GfxBase->LibNode.lib_Version >= 39 &&
        !(GetBitMapAttr(bitmap, BMA_FLAGS) & BMF_STANDARD)) return FALSE;
    if (bitmap->Depth < 1 || bitmap->Depth > AMI_MAX_DEPTH) return FALSE;
    *depth = *storage = bitmap->Depth;
    return TRUE;
}

static int ami_screen_format(struct Screen *screen, struct BitMap *bitmap)
{
    int backend;
    unsigned int depth, storage;
    if (screen == NULL || !ami_bitmap_format(bitmap, &backend, &depth, &storage) ||
        (!backend && (screen->ViewPort.Modes & (HAM | EXTRA_HALFBRITE | DUALPF)) != 0))
    {
        ami_ready = FALSE;
        ami_paused = TRUE;
        ami_unsafe_bitmap = TRUE;
        drv_notice("Unsupported Workbench bitmap or unavailable RTG library");
        return FALSE;
    }
    if (ami_rtg != backend || ami_depth != depth || ami_storage_depth != storage)
        ami_palette_valid = FALSE;
    ami_rtg = backend;
    ami_depth = depth;
    ami_storage_depth = storage;
    return TRUE;
}

static int ami_check_raster_bounds(unsigned int width, unsigned int height)
{
    ami_unsafe_bitmap = !ami_rtg && !ami_big_blits &&
        ami_window->RPort->BitMap->BytesPerRow > 128U;
    if (width == 0 || height == 0 || width > 32767U || height > 32767U ||
        ami_unsafe_bitmap || (!ami_rtg && !ami_big_blits &&
        (width > AMI_OCS_MAX_WIDTH || height > AMI_OCS_MAX_HEIGHT)))
    {
        ami_ready = FALSE;
        ami_paused = TRUE;
        drv_notice(ami_unsafe_bitmap ? "Workbench bitmap exceeds OCS blitter limits" :
                   "Window exceeds drawing limits; resize smaller");
        drv_reset_timing();
        return FALSE;
    }
    return TRUE;
}

static int ami_resize(void)
{
    unsigned int width, height, plane;
#if AMI_RTG
    unsigned int depth, storage;
    int backend;
#endif
    ULONG bytes;
    width = ami_window->GZZWidth;
    height = ami_window->GZZHeight;
    if (!ami_screen_format(ami_window->WScreen, ami_window->RPort->BitMap) ||
        !ami_check_raster_bounds(width, height)) return FALSE;
    drv_win_width = width;
    drv_win_height = height;
    ami_drop_rasters();
    ami_raster_width = width;
    ami_raster_height = height;
    ami_paused = FALSE;
    if (ami_depth <= 8U)
    {
        bytes = ((width + 15UL) / 16UL) * 2UL * height;
        ami_scratch = AllocRaster(width, height);
        if (ami_scratch != NULL) InitTmpRas(&ami_tmp_ras, ami_scratch, bytes);
        ami_paused = ami_scratch == NULL;
        if (ami_paused) drv_notice("Not enough Chip RAM; resize or press a key to retry");
    }
    if (!ami_paused)
    {
#if AMI_RTG
        if (ami_rtg)
        {
            ami_rtg_bitmap = AllocBitMap(width, height, ami_depth,
                BMF_MINPLANES | BMF_CLEAR, ami_window->RPort->BitMap);
            /* A failed or incompatible friend bitmap is optional. Never send
             * RGB drawing calls to an allocation that fell back to planar. */
            if (ami_rtg_bitmap != NULL &&
                (!ami_bitmap_format(ami_rtg_bitmap, &backend, &depth, &storage) ||
                 backend != ami_rtg || depth != ami_depth || storage != ami_storage_depth))
            {
                FreeBitMap(ami_rtg_bitmap);
                ami_rtg_bitmap = NULL;
            }
        }
        else
#endif
        {
            InitBitMap(&ami_bitmap, ami_depth, width, height);
            for (plane = 0; plane < ami_depth; ++plane)
            {
                ami_bitmap.Planes[plane] = AllocRaster(width, height);
                if (ami_bitmap.Planes[plane] == NULL) break;
                ++ami_back_planes;
            }
            if (ami_back_planes != ami_depth) ami_drop_back_buffer();
        }
        if (ami_back_planes != 0 || ami_rtg_bitmap != NULL)
        {
            InitRastPort(&ami_back_port);
            ami_back_port.BitMap = ami_rtg_bitmap != NULL ? ami_rtg_bitmap : &ami_bitmap;
            SetFont(&ami_back_port, ami_window->RPort->Font);
        }
        else drv_notice("Amiga direct drawing; no back buffer");
    }
    ami_map_palette();
    ami_pixel_aspect();
    ami_previous_sequence = 0;
    ami_previous_count = 0;
    soft3d_project_cache_reset(ami_project_cache);
    ami_ready = TRUE;
    ami_dirty = TRUE;
    ami_auto_time = ami_time_now();
    drv_reset_timing();
    return TRUE;
}

int drv_init_window(int in_x, int in_y, int in_width, int in_height)
{
    struct NewWindow settings;
    struct Screen screen, *locked;
    int width, height, left, top, available, max_width, max_height;
    int screen_width, screen_height;
    memset(&screen, 0, sizeof(screen));
    locked = NULL;
    screen_width = 640;
    screen_height = 200;
    if (GfxBase->LibNode.lib_Version >= 39 &&
        IntuitionBase->LibNode.lib_Version >= 39)
    {
        locked = LockPubScreen((STRPTR)"Workbench");
        if (locked == NULL) return FALSE;
        if (!ami_screen_format(locked, locked->RastPort.BitMap)) goto failed;
        screen_width = locked->Width;
        screen_height = locked->Height;
        if (!ami_rtg && !ami_big_blits && locked->RastPort.BitMap->BytesPerRow > 128U)
            goto unsafe;
    }
    else if (GetScreenData(&screen, sizeof(screen), WBENCHSCREEN, NULL))
    {
        if (!ami_screen_format(&screen, &screen.BitMap)) goto failed;
        screen_width = screen.Width;
        screen_height = screen.Height;
        if (!ami_big_blits && screen.BitMap.BytesPerRow > 128U) goto unsafe;
    }
    else goto failed;
    if (screen_width < AMI_MIN_WIDTH || screen_height < AMI_MIN_HEIGHT ||
        screen_width > 32767 || screen_height > 32767) goto failed;
    max_width = screen_width;
    max_height = screen_height;
    if (!ami_rtg && !ami_big_blits)
    {
        /* Conservative outer-window ceilings also bound its client area
         * without assuming the system font's native border dimensions. */
        if (max_width > (int)AMI_OCS_MAX_WIDTH) max_width = AMI_OCS_MAX_WIDTH;
        if (max_height > (int)AMI_OCS_MAX_HEIGHT) max_height = AMI_OCS_MAX_HEIGHT;
    }
    width = in_width > 0 ? in_width :
            (screen_width <= 640 && screen_height <= 480 ? 320 : DRV_WIN_WIDTH);
    height = in_height > 0 ? in_height :
             (screen_width <= 640 && screen_height <= 480 ? 240 : DRV_WIN_HEIGHT);
    memset(&settings, 0, sizeof(settings));
    left = in_x > 0 ? in_x : DRV_WIN_X;
    top = in_y > 0 ? in_y : DRV_WIN_Y;
    if (left > screen_width - AMI_MIN_WIDTH) left = 0;
    if (top > screen_height - AMI_MIN_HEIGHT) top = 0;
    settings.LeftEdge = (WORD)left;
    settings.TopEdge = (WORD)top;
    /* Bound caller-supplied integers before adding borders or narrowing
     * to the native signed-word window dimensions. */
    available = screen_width - left;
    width = width > available - 16 ? available : width + 16;
    if (width > max_width) width = max_width;
    available = screen_height - top;
    height = height > available - 24 ? available : height + 24;
    if (height > max_height) height = max_height;
    if (width < AMI_MIN_WIDTH || height < AMI_MIN_HEIGHT) goto failed;
    settings.Width = (WORD)width;
    settings.Height = (WORD)height;
    settings.DetailPen = 0;
    settings.BlockPen = 1;
    settings.IDCMPFlags = CLOSEWINDOW | NEWSIZE | REFRESHWINDOW | MOUSEBUTTONS |
                         MOUSEMOVE | VANILLAKEY | ACTIVEWINDOW | INACTIVEWINDOW | INTUITICKS;
    settings.Flags = WINDOWCLOSE | WINDOWDRAG | WINDOWDEPTH | WINDOWSIZING |
                     ACTIVATE | GIMMEZEROZERO | SMART_REFRESH | REPORTMOUSE;
    settings.Title = (UBYTE *)"testglobe - Amiga";
    settings.MinWidth = AMI_MIN_WIDTH;
    settings.MinHeight = AMI_MIN_HEIGHT;
    settings.MaxWidth = (UWORD)max_width;
    settings.MaxHeight = (UWORD)max_height;
    settings.Type = locked != NULL ? PUBLICSCREEN : WBENCHSCREEN;
    settings.Screen = locked;
    ami_window = OpenWindow(&settings);
    if (locked != NULL) UnlockPubScreen(NULL, locked);
    locked = NULL;
    if (ami_window == NULL) return FALSE;
    ami_project_cache = (Soft3d_project_cache *)malloc(sizeof(Soft3d_project_cache));
    return ami_resize();
unsafe:
    ami_unsafe_bitmap = TRUE;
    drv_notice("Workbench bitmap exceeds OCS blitter limits");
failed:
    if (locked != NULL) UnlockPubScreen(NULL, locked);
    return FALSE;
}

static void ami_set_color(unsigned char color)
{
    static const unsigned char bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}
    };
    unsigned int x, y;
    UWORD row;
    SetAPen(ami_draw_port, ami_pen_a[color]);
    if (ami_mix[color] == 0)
    {
        SetDrMd(ami_draw_port, JAM1);
        SetAfPt(ami_draw_port, NULL, 0);
        return;
    }
    SetBPen(ami_draw_port, ami_pen_b[color]);
    SetDrMd(ami_draw_port, JAM2);
    for (y = 0; y < 4; ++y)
    {
        row = 0;
        for (x = 0; x < 16; ++x)
            if (bayer[y][x & 3U] >= ami_mix[color]) row |= (UWORD)(0x8000U >> x);
        ami_pattern[y] = row;
    }
    SetAfPt(ami_draw_port, ami_pattern, 2);
}

#if AMI_RTG
static void ami_rtg_fill(struct RastPort *port, unsigned int x0, unsigned int y0,
                          unsigned int x1, unsigned int y1, ULONG rgb)
{
    if (ami_rtg == 1)
    {
        /* The documented return count is incorrect through CGX V41; a zero
         * count also legitimately means the whole rectangle was obscured. */
        (void)ami_cgx_fill(ami_cgx_base, port, (UWORD)x0, (UWORD)y0,
                         (UWORD)(x1 - x0 + 1U), (UWORD)(y1 - y0 + 1U), rgb);
    }
    else ami_p96_fill(ami_p96_base, port, (UWORD)x0, (UWORD)y0,
                      (UWORD)x1, (UWORD)y1, rgb);
}

/* Viewport-clipped polygon rasterization. Sample pixel centers with
 * an inclusive left/exclusive right edge, so adjacent polygons share neither
 * gaps nor pixels. Pair sorted crossings (even/odd fill), because rounding
 * near the silhouette can make an otherwise convex globe quad concave.
 * Only filled horizontal spans reach the RTG drawing API; native graphics
 * still use AreaEnd, and no framebuffer memory is accessed. */
static int ami_fill_polygon(const Soft3d_screen_point *points, int count,
                             unsigned char color)
{
    long min_y, max_y, y, x0, y0, x1, y1, dx, dy, numerator, denominator;
    long quotient, remainder, boundary, swap, crossings[AMI_MAX_POLY];
    ULONG rgb;
    int i, next, intersections, position;
    if (points == NULL || count < 3 || count > AMI_MAX_POLY ||
        ami_draw_port == NULL || !ami_rtg || ami_depth <= 8U) return FALSE;
    min_y = max_y = points[0].y;
    for (i = 0; i < count; ++i)
    {
        if (points[i].x < 0 || points[i].y < 0 ||
            points[i].x >= (long)drv_win_width || points[i].y >= (long)drv_win_height ||
            points[i].x > 32766L || points[i].y > 32766L) return FALSE;
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    rgb = ((ULONG)earth_cmap[color][0] << 16) |
          ((ULONG)earth_cmap[color][1] << 8) | (ULONG)earth_cmap[color][2];
    for (y = min_y; y < max_y; ++y)
    {
        intersections = 0;
        for (i = 0; i < count; ++i)
        {
            next = i + 1 == count ? 0 : i + 1;
            x0 = points[i].x; y0 = points[i].y;
            x1 = points[next].x; y1 = points[next].y;
            if (y0 > y1)
            {
                swap = x0; x0 = x1; x1 = swap;
                swap = y0; y0 = y1; y1 = swap;
            }
            if (y < y0 || y >= y1) continue;
            dx = x1 - x0;
            dy = y1 - y0;
            /* |dx| <= 32766, 2*(y-y0)+1 <= 65531: product fits signed
             * 32 bits. Separate the integer part to avoid x0*denominator
             * overflowing when added to this numerator. Normalize negative
             * remainders explicitly rather than assuming signed division
             * rounds downward on every ANSI C89 compiler. */
            numerator = dx * (2L * (y - y0) + 1L);
            denominator = 2L * dy;
            quotient = numerator / denominator;
            remainder = numerator % denominator;
            if (remainder < 0) { --quotient; remainder += denominator; }
            boundary = x0 + quotient + (remainder > dy ? 1L : 0L);
            position = intersections;
            while (position > 0 && crossings[position - 1] > boundary)
            {
                crossings[position] = crossings[position - 1];
                --position;
            }
            crossings[position] = boundary;
            ++intersections;
        }
        for (i = 0; i + 1 < intersections; i += 2)
            if (crossings[i] < crossings[i + 1])
                ami_rtg_fill(ami_draw_port, (unsigned int)crossings[i], (unsigned int)y,
                             (unsigned int)(crossings[i + 1] - 1L), (unsigned int)y, rgb);
    }
    return TRUE;
}
#endif

static int ami_aspect_polygon(Soft3d_screen_point *points, int count)
{
    long center, delta, scaled;
    int i;
    if (ami_aspect_num == ami_aspect_den) return count;
    center = (long)drv_win_width / 2L;
    for (i = 0; i < count; ++i)
    {
        delta = points[i].x - center;
        if (delta > LONG_MAX / (long)ami_aspect_num ||
            delta < LONG_MIN / (long)ami_aspect_num) return 0;
        scaled = delta * (long)ami_aspect_num / (long)ami_aspect_den;
        if (scaled > LONG_MAX - center) return 0;
        points[i].x = center + scaled;
    }
    return count;
}

void drv_draw_quad_flat_color(Vertex TG_HUGE *first, Vertex TG_HUGE *second,
                               Vertex TG_HUGE *third, Vertex TG_HUGE *fourth,
                               unsigned char color)
{
    drv_submit_quad_as_batch(first, second, third, fourth, color);
}

void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                   const Vertex TG_HUGE *vertices,
                                   unsigned int quad_count,
                                   const unsigned int TG_HUGE *indices,
                                   const unsigned char TG_HUGE *colors)
{
    Drv_quad_context context;
    Soft3d_screen_point polygon[AMI_MAX_POLY];
    unsigned int corners[4], reuse, facet, corner;
    int count, point, transformed;
    if (!ami_rendering || ami_draw_port == NULL || drv_frame_failed || ami_paused) return;
    if (vertices == NULL || colors == NULL || !drv_validate_quadbatch(vertex_count, quad_count, indices))
    {
        drv_abort_frame("Amiga quad packet is invalid");
        return;
    }
    reuse = 0;
    if (drv_quadbatch_context(vertex_count, vertices, &context) && context.reuse_row &&
        context.sequence == ami_previous_sequence + 1UL && vertex_count == ami_previous_count)
        reuse = vertex_count / 2U;
    soft3d_project_cache_begin(ami_project_cache, vertex_count, drv_win_width, drv_win_height, reuse);
    if (ami_has_fpu)
        transformed = soft3d_float_transform_reuse(ami_matrix.floating, vertex_count,
                               vertices, ami_points.floating, reuse, SOFT3D_FLOAT_DOUBLE);
    else if (ami_cpu >= 68020)
        transformed = soft3d_020_transform_reuse(ami_matrix.fixed, vertex_count,
                               vertices, ami_points.fixed, reuse);
    else
        transformed = soft3d_fixed_transform_reuse(ami_matrix.fixed, vertex_count,
                               vertices, ami_points.fixed, reuse);
    if (!transformed)
    {
        drv_abort_frame("Amiga coordinate is out of range");
        ami_previous_sequence = 0;
        return;
    }
    ami_previous_sequence = context.sequence;
    ami_previous_count = vertex_count;
    for (facet = 0; facet < quad_count; ++facet)
    {
        for (corner = 0; corner < 4U; ++corner) corners[corner] = indices[facet * 4U + corner];
        if (ami_has_fpu)
            count = soft3d_float_project_cached(ami_points.floating, vertex_count, corners, 4U,
                drv_win_width, drv_win_height, SOFT3D_FLOAT_DOUBLE, ami_project_cache, polygon);
        else
            count = soft3d_fixed_project_cached(ami_points.fixed, vertex_count, corners, 4U,
                drv_win_width, drv_win_height, ami_project_cache, polygon);
        count = ami_aspect_polygon(polygon, count);
        count = soft3d_finish_polygon(polygon, count, drv_win_width - 1U, drv_win_height - 1U);
        if (count < 3) continue;
#if AMI_RTG
        if (ami_rtg && ami_depth > 8U)
        {
            if (!ami_fill_polygon(polygon, count, colors[facet])) goto failed;
            ++globe_quads_num_vis;
            continue;
        }
#endif
        ami_set_color(colors[facet]);
        InitArea(&ami_area_info, ami_area_storage, AMI_AREA_VECTORS);
        if (AreaMove(ami_draw_port, (WORD)polygon[0].x, (WORD)polygon[0].y) != 0) goto failed;
        for (point = 1; point < count; ++point)
            if (AreaDraw(ami_draw_port, (WORD)polygon[point].x, (WORD)polygon[point].y) != 0) goto failed;
        if (AreaEnd(ami_draw_port) != 0) goto failed;
        ++globe_quads_num_vis;
    }
    return;
failed:
    InitArea(&ami_area_info, ami_area_storage, AMI_AREA_VECTORS);
    ami_area_failed = TRUE;
    drv_abort_frame("Amiga area fill failed");
}

static int ami_text_lines(const char *text, int baseline, int draw)
{
    unsigned int remaining, count;
    int line_height, margin;
    line_height = ami_draw_port->TxHeight + 1;
    if (line_height < 2) line_height = 9;
    margin = 4;
    while (*text != 0)
    {
        remaining = (unsigned int)strlen(text);
        count = remaining;
        while (count > 0 && TextLength(ami_draw_port, (STRPTR)text, count) >
               (long)drv_win_width - 2L * margin) --count;
        if (count == 0) break;
        if (draw && baseline < (int)drv_win_height)
        {
            Move(ami_draw_port, margin, baseline);
            Text(ami_draw_port, (STRPTR)text, count);
        }
        baseline += line_height;
        text += count;
    }
    return baseline;
}

void drv_draw_osd(void)
{
    char status[256], detail[128];
    int baseline, help_height;
    if (ami_draw_port == NULL) return;
    SetAfPt(ami_draw_port, NULL, 0);
    SetDrMd(ami_draw_port, JAM2);
    SetAPen(ami_draw_port, ami_white);
    SetBPen(ami_draw_port, ami_black);
    drv_format_stats(status);
    if (drv_fps_enabled)
    {
        sprintf(detail, " %.1f FPS", (double)drv_fps_average);
        drv_append_status(status, sizeof(status), detail);
    }
    baseline = ami_draw_port->TxBaseline + 4;
    baseline = ami_text_lines(status, baseline, TRUE);
    if (ami_rtg)
        sprintf(detail, "%s RTG, %u-bit, %d, %s, %s",
                ami_rtg == 1 ? "CyberGraphX" : "Picasso96", ami_storage_depth, ami_cpu,
                ami_has_fpu ? ami_fpu_name : "fixed-point",
                ami_rtg_bitmap != NULL ? "buffered" : "direct");
    else
        sprintf(detail, "%s, %d, %s, %u colors, %s", ami_chipset, ami_cpu,
                ami_has_fpu ? ami_fpu_name : "fixed-point", ami_color_count,
                ami_back_planes != 0 ? "buffered" : "direct");
    (void)ami_text_lines(detail, baseline, TRUE);
    help_height = ami_text_lines(drv_help, 0, FALSE);
    baseline = (int)drv_win_height - help_height + ami_draw_port->TxBaseline - 2;
    if (baseline > ami_draw_port->TxBaseline) (void)ami_text_lines(drv_help, baseline, TRUE);
}

static void ami_render_pass(void)
{
    int prepared;
    if (!ami_ready || ami_window == NULL || ami_rendering) return;
    if (ami_paused && ami_depth <= 8U && ami_scratch == NULL)
        drv_notice("Not enough Chip RAM; resize or press a key to retry");
    drv_fps_start();
    ami_rendering = TRUE;
    ami_draw_port = ami_back_planes != 0 || ami_rtg_bitmap != NULL ?
                    &ami_back_port : ami_window->RPort;
    ami_draw_port->AreaInfo = &ami_area_info;
    ami_draw_port->TmpRas = ami_scratch != NULL ? &ami_tmp_ras : NULL;
    ami_draw_port->Flags &= ~AREAOUTLINE;
    InitArea(&ami_area_info, ami_area_storage, AMI_AREA_VECTORS);
    SetAfPt(ami_draw_port, NULL, 0);
    SetDrMd(ami_draw_port, JAM1);
    SetAPen(ami_draw_port, ami_black);
#if AMI_RTG
    if (ami_rtg && ami_depth > 8U)
        ami_rtg_fill(ami_draw_port, 0, 0, drv_win_width - 1U, drv_win_height - 1U, 0UL);
    else
#endif
        RectFill(ami_draw_port, 0, 0, drv_win_width - 1U, drv_win_height - 1U);
    globe_quads_num_vis = 0;
    ami_previous_sequence = 0;
    ami_previous_count = 0;
    soft3d_project_cache_reset(ami_project_cache);
    if (globe_ready() && !ami_paused)
    {
        if (ami_has_fpu)
            prepared = soft3d_float_prepare(ami_matrix.floating, &drv_rot_x, &drv_rot_y);
        else prepared = soft3d_fixed_prepare(ami_matrix.fixed, &drv_rot_x, &drv_rot_y);
        if (prepared) globe_draw_quadbatches();
        else drv_abort_frame("Amiga rotation is out of range");
    }
    if (!drv_frame_failed)
    {
        if (drv_osd_enabled || ami_paused || !globe_ready()) drv_draw_osd();
        if (ami_back_planes != 0 || ami_rtg_bitmap != NULL)
            BltBitMapRastPort(ami_rtg_bitmap != NULL ? ami_rtg_bitmap : &ami_bitmap, 0, 0, ami_window->RPort, 0, 0,
                              drv_win_width, drv_win_height, 0xc0);
        WaitBlit();
        ami_dirty = FALSE;
        if (!ami_paused) drv_fps_stop();
        else drv_reset_timing();
    }
    else drv_reset_timing();
    ami_rendering = FALSE;
    ami_draw_port = NULL;
}

void drv_render(void)
{
    int attempt;
    if (ami_window == NULL || ami_rendering) return;
    if (!ami_check_raster_bounds(ami_window->GZZWidth, ami_window->GZZHeight)) return;
    if (!ami_ready) return;
    for (attempt = 0; attempt < 2; ++attempt)
    {
        ami_area_failed = FALSE;
        ami_render_pass();
        if (!drv_frame_failed) return;
        if (!ami_area_failed) break;
        if (attempt == 0) drv_release_optional();
    }
    ami_paused = TRUE;
    drv_auto_enabled = FALSE;
    drv_notice("Amiga drawing paused; press a control or resize to retry");
    ami_render_pass();
}

void drv_close(void)
{
    ami_running = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
}

static void ami_keyboard(unsigned char key)
{
    if (key == 27) { drv_close(); return; }
    if (!drv_handle_key(key)) return;
    if ((ami_depth <= 8U && ami_scratch == NULL) || !ami_ready) (void)ami_resize();
    else ami_paused = FALSE;
    ami_auto_time = ami_time_now();
    ami_dirty = TRUE;
}

static void ami_event(ULONG kind, UWORD code, WORD mouse_x, WORD mouse_y)
{
    switch (kind)
    {
        case CLOSEWINDOW: drv_close(); break;
        case NEWSIZE: (void)ami_resize(); break;
        case REFRESHWINDOW:
            if (!ami_check_raster_bounds(ami_window->GZZWidth, ami_window->GZZHeight)) break;
            ami_map_palette();
            /* A queued pose/LOD change also affects undamaged pixels. A
             * render inside BeginRefresh would be clipped to the damage
             * region yet could incorrectly mark that new scene complete.
             * Acknowledge damage here, then repaint the whole client area
             * once outside the restricted refresh scope. */
            BeginRefresh(ami_window);
            EndRefresh(ami_window, TRUE);
            ami_dirty = TRUE;
            break;
        case ACTIVEWINDOW:
            ami_active = TRUE;
            if (!ami_check_raster_bounds(ami_window->GZZWidth, ami_window->GZZHeight)) break;
            ami_map_palette();
            ami_pixel_aspect();
            ami_auto_time = ami_time_now();
            ami_dirty = TRUE;
            break;
        case INACTIVEWINDOW:
            ami_active = FALSE;
            drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
            ami_auto_time = ami_time_now();
            break;
        case VANILLAKEY: ami_keyboard((unsigned char)code); break;
        case MOUSEBUTTONS:
            if (code == SELECTDOWN)
            {
                drv_lmouse_pressed = TRUE;
                ami_old_x = mouse_x;
                ami_old_y = mouse_y;
            }
            else if (code == SELECTUP) drv_lmouse_pressed = FALSE;
            break;
        case MOUSEMOVE:
            if (drv_lmouse_pressed)
            {
                drv_rotate_drag((long)mouse_x - ami_old_x, (long)mouse_y - ami_old_y);
                ami_paused = ami_depth <= 8U && ami_scratch == NULL;
                ami_dirty = TRUE;
            }
            ami_old_x = mouse_x;
            ami_old_y = mouse_y;
            break;
        default: break;
    }
}

void drv_loop(void)
{
    struct IntuiMessage *message;
    ULONG kind;
    UWORD code;
    WORD x, y;
    unsigned int count;
    double now;
    if (ami_window == NULL || !ami_ready) return;
    ami_auto_time = ami_time_now();
    while (ami_running)
    {
        /* Reply before actions that can allocate, resize, or close the window.
         * A bounded drain lets animation progress during a long mouse drag. */
        for (count = 0; count < 64U && ami_running; ++count)
        {
            message = (struct IntuiMessage *)GetMsg(ami_window->UserPort);
            if (message == NULL) break;
            kind = message->Class;
            code = message->Code;
            x = message->MouseX;
            y = message->MouseY;
            ReplyMsg((struct Message *)message);
            ami_event(kind, code, x, y);
        }
        if (!ami_running) break;
        now = ami_time_now();
        if (ami_ready && ami_active && drv_auto_enabled && globe_ready() && !ami_paused)
        {
            drv_rotate_elapsed(now - ami_auto_time);
            ami_dirty = TRUE;
        }
        ami_auto_time = now;
        if (ami_dirty) drv_render();
        if (!(ami_ready && ami_active && drv_auto_enabled && globe_ready() && !ami_paused) && count < 64U)
            Wait(1UL << ami_window->UserPort->mp_SigBit);
    }
    ami_cleanup();
}
#endif /* DRV_AMI */
