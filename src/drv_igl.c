/*
 * testglobe - native IRIS GL / IRIX 4.0 driver.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * IRIS GL owns transforms, clipping, filled polygons, fonts and buffering.
 * When its installed implementation cannot texture, texsoft rasterizes the
 * textured triangles and GL transfers the resulting pixels to the window.
 */
#include "testglob.h"
#ifdef DRV_IGL
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "drv.h"
#include "drv_ext.h"
#include "globe.h"
#include "glob_ext.h"
#include "tex_ext.h"
#include "texsoft.h"

/* The native headers use Coord for a scalar and FLAT for a shade mode.
 * Keep the application's UV type and avoid redefining its architecture flag.
 * Native FLAT remains defined, so architecture #ifdefs are still valid. */
#undef FLAT
#define Coord Igl_native_coord
#include <gl/gl.h>
#include <gl/get.h>
#include <gl/device.h>
#undef Coord

static long igl_window;
static int igl_running;
static int igl_dirty = TRUE;
static int igl_active = TRUE;
static int igl_double_buffer;
static int igl_indexed;
static int igl_has_zbuffer;
static int igl_native_texture;
static int igl_texture_perspective;
static int igl_texture_failed;
static int igl_texture_used;
static int igl_palette_saved;
static short igl_saved_palette[256][3];
static unsigned int igl_background;
static unsigned int igl_foreground;
static long igl_old_x, igl_old_y;
static Object igl_object = -1;
static Matrix igl_identity = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f}
};

static void igl_bind_texture(int enabled)
{
#ifdef GD_TEXTURE
    if (igl_native_texture)
    {
        texbind(TX_TEXTURE_0, enabled ? 1 : 0);
        tevbind(TV_ENV0, enabled ? 1 : 0);
#if defined(SS_DEPTH) && defined(SS_OFF)
        if (!igl_texture_perspective)
        {
            float subdivision[3];
            subdivision[0] = subdivision[1] = 0.0f;
            subdivision[2] = 10.0f;
            scrsubdivide(enabled ? SS_DEPTH : SS_OFF, subdivision);
        }
#endif
    }
#else
    (void)enabled;
#endif
}

static void igl_invalidate_object(void)
{
    if (igl_object >= 0 && isobj(igl_object)) delobj(igl_object);
    igl_object = -1;
}

/* Baseline IRIX 4.0 texture and pixel-transfer calls return void. Query
 * documented capabilities and report every failure the API can expose;
 * residency queries from later hardware are not definition-validity tests. */
static int igl_texture_native_ready(void)
{
#ifdef GD_TEXTURE
    if (!igl_native_texture || getgdesc(GD_TEXTURE) <= 0) return FALSE;
#ifdef GD_TEXTURE_PERSP
    if (igl_texture_perspective && getgdesc(GD_TEXTURE_PERSP) <= 0) return FALSE;
#endif
    return TRUE;
#else
    return FALSE;
#endif
}

static void igl_texture_failure(const char *message)
{
    igl_texture_failed = TRUE;
    igl_texture_used = FALSE;
    drv_texture_loaded = FALSE;
    igl_bind_texture(FALSE);
    igl_invalidate_object();
    texsoft_free();
    texture_free();
    /* Native texture definitions are retained by IRIS GL until redefined
     * or the graphics connection closes; unbind them before flat fallback. */
    fprintf(stderr, "IRIS GL texture: %s\n", message);
}

static void igl_cleanup(void)
{
    unsigned int i;
    if (igl_window > 0)
    {
        igl_invalidate_object();
        igl_bind_texture(FALSE);
        if (igl_palette_saved)
            for (i = 0; i < 256; ++i)
                mapcolor((Colorindex)i, igl_saved_palette[i][0],
                          igl_saved_palette[i][1], igl_saved_palette[i][2]);
        igl_palette_saved = FALSE;
        winclose(igl_window);
        igl_window = 0;
        gexit();
    }
    texsoft_free();
    texture_free();
    globe_free();
    drv_texture_loaded = FALSE;
    drv_texture_enabled = FALSE;
    igl_texture_failed = igl_texture_used = FALSE;
}

void drv_close(void)
{
    igl_running = FALSE;
}

void drv_init(int *argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (atexit(igl_cleanup) != 0)
        error("Cannot register IRIS GL cleanup");
    igl_running = TRUE;
}

static unsigned int igl_nearest_color(int red, int green, int blue)
{
    unsigned int i, best;
    long dr, dg, db, distance, minimum;
    minimum = 0x7fffffffL;
    best = 0;
    for (i = 0; i < 256; ++i)
    {
        dr = red - (int)earth_cmap[i][0];
        dg = green - (int)earth_cmap[i][1];
        db = blue - (int)earth_cmap[i][2];
        distance = dr * dr + dg * dg + db * db;
        if (distance < minimum) { minimum = distance; best = i; }
    }
    return best;
}

static void igl_setup_palette(void)
{
    unsigned int i;
    for (i = 0; i < 256; ++i)
    {
        getmcolor((Colorindex)i, &igl_saved_palette[i][0],
                   &igl_saved_palette[i][1], &igl_saved_palette[i][2]);
        mapcolor((Colorindex)i, earth_cmap[i][0], earth_cmap[i][1], earth_cmap[i][2]);
    }
    igl_palette_saved = TRUE;
    igl_background = igl_nearest_color(0, 0, 0);
    igl_foreground = igl_nearest_color(255, 255, 255);
}

/* Prefer precision to buffering; equal formats use the back buffer. */
static long igl_rgb_depth(int double_buffer, long *rgb_bits, long *alpha_bits)
{
    long red, green, blue;
    red = getgdesc(double_buffer ? GD_BITS_NORM_DBL_RED : GD_BITS_NORM_SNG_RED);
    green = getgdesc(double_buffer ? GD_BITS_NORM_DBL_GREEN : GD_BITS_NORM_SNG_GREEN);
    blue = getgdesc(double_buffer ? GD_BITS_NORM_DBL_BLUE : GD_BITS_NORM_SNG_BLUE);
    *rgb_bits = *alpha_bits = 0;
    if (red <= 0 || green <= 0 || blue <= 0) return 0;
    *rgb_bits = red + green + blue;
#if defined(GD_BITS_NORM_DBL_ALPHA) && defined(GD_BITS_NORM_SNG_ALPHA)
    *alpha_bits = getgdesc(double_buffer ? GD_BITS_NORM_DBL_ALPHA :
                                         GD_BITS_NORM_SNG_ALPHA);
    if (*alpha_bits < 0) *alpha_bits = 0;
#endif
    return *rgb_bits + *alpha_bits;
}

static void igl_resize(int width, int height)
{
    if (width < 1 || height < 1) return;
    if (width > 32767 || height > 32767)
        error("IRIS GL window is too large");
    drv_win_width = (unsigned int)width;
    drv_win_height = (unsigned int)height;
    viewport(0, (Screencoord)(width - 1), 0, (Screencoord)(height - 1));
    mmode(MVIEWING);
    perspective(450, (float)width / (float)height, 1.0f, 10000.0f);
    igl_dirty = TRUE;
}

int drv_init_window(int in_x, int in_y, int in_width, int in_height)
{
    long width, height;
    long double_depth, single_depth, double_rgb, single_rgb;
    long double_alpha, single_alpha, double_indexed, single_indexed;
    long selected_rgb, selected_alpha;
    unsigned int bits;
    if (in_x > 0) drv_win_x = (unsigned int)in_x;
    if (in_y > 0) drv_win_y = (unsigned int)in_y;
    if (in_width > 0) drv_win_width = (unsigned int)in_width;
    if (in_height > 0) drv_win_height = (unsigned int)in_height;
    double_depth = igl_rgb_depth(TRUE, &double_rgb, &double_alpha);
    single_depth = igl_rgb_depth(FALSE, &single_rgb, &single_alpha);
    igl_indexed = double_depth == 0 && single_depth == 0;
    igl_double_buffer = double_depth > single_depth ||
        (double_depth == single_depth &&
         (double_alpha > single_alpha ||
          (double_alpha == single_alpha && double_rgb >= single_rgb)));
    selected_rgb = igl_double_buffer ? double_rgb : single_rgb;
    selected_alpha = igl_double_buffer ? double_alpha : single_alpha;
    if (igl_indexed)
    {
        double_indexed = getgdesc(GD_BITS_NORM_DBL_CMODE);
        single_indexed = getgdesc(GD_BITS_NORM_SNG_CMODE);
        igl_double_buffer = double_indexed >= 8 && double_indexed >= single_indexed;
        if (double_indexed < 8 && single_indexed < 8)
            error("IRIS GL requires RGB or an 8-bit color-index window");
    }
    prefsize((long)drv_win_width, (long)drv_win_height);
    prefposition((long)drv_win_x, (long)drv_win_x + drv_win_width - 1L,
                  (long)drv_win_y, (long)drv_win_y + drv_win_height - 1L);
    igl_window = winopen(TESTGLOBE_NAME);
    if (igl_window <= 0) error("Cannot open IRIS GL window");
    /* winopen resets pending constraints. Applying those defaults releases
     * prefsize/prefposition's initial fixed size for interactive resizing. */
    winconstraints();
    if (igl_indexed) cmode();
    else RGBmode();
    if (igl_double_buffer) doublebuffer();
    else singlebuffer();
    gconfig();
    /* Newer native SDKs can report the actual configured channel sizes.
     * Earlier SDKs retain the selected mode's getgdesc capability values. */
#if defined(GC_BITS_RED) && defined(GC_BITS_GREEN) && defined(GC_BITS_BLUE)
    if (!igl_indexed)
    {
        long red, green, blue;
        red = getgconfig(GC_BITS_RED);
        green = getgconfig(GC_BITS_GREEN);
        blue = getgconfig(GC_BITS_BLUE);
        if (red > 0 && green > 0 && blue > 0) selected_rgb = red + green + blue;
#ifdef GC_BITS_ALPHA
        {
            long alpha;
            alpha = getgconfig(GC_BITS_ALPHA);
            if (alpha >= 0) selected_alpha = alpha;
        }
#endif
    }
#endif
    bits = (unsigned int)getplanes();
    drv_colormapped = igl_indexed;
    drv_bits_per_pixel = igl_indexed ? bits :
                         (unsigned int)(selected_rgb + selected_alpha);
    drv_colormap_bits = igl_indexed ? bits : (unsigned int)selected_rgb;
    drv_colors = drv_colormap_bits >= 32 ? 0xffffffffUL :
                 1UL << drv_colormap_bits;
    drv_colormap_colors = drv_colors;
    drv_bytes_per_pixel = (drv_bits_per_pixel + 7U) / 8U;
    if (igl_indexed) igl_setup_palette();
    igl_has_zbuffer = getgdesc(GD_BITS_NORM_ZBUFFER) > 0;
    if (igl_has_zbuffer)
    {
        zbuffer(TRUE);
        lsetdepth(getgdesc(GD_ZMIN), getgdesc(GD_ZMAX));
    }
    backface(TRUE);
    shademodel(FLAT);
    subpixel(TRUE);
    igl_native_texture = FALSE;
    igl_texture_perspective = FALSE;
#ifdef GD_TEXTURE
    igl_native_texture = !igl_indexed && getgdesc(GD_TEXTURE) > 0;
#ifdef GD_TEXTURE_PERSP
    igl_texture_perspective = getgdesc(GD_TEXTURE_PERSP) > 0;
#endif
#endif
    /* An affine-only implementation needs GL's documented subdivision.
     * Old SDKs without that facility use the perspective-correct CPU path. */
#if !defined(SS_DEPTH) || !defined(SS_OFF)
    if (!igl_texture_perspective) igl_native_texture = FALSE;
#endif
    /* On affine-only VGX hardware, binding a texture also enables GL's
     * depth subdivision. Projection-correct implementations need none. */
    igl_bind_texture(FALSE);
    qdevice(KEYBD);
    qdevice(LEFTMOUSE);
    qdevice(RIGHTMOUSE);
    qdevice(MOUSEX);
    qdevice(MOUSEY);
    qdevice(REDRAW);
    qdevice(INPUTCHANGE);
    qdevice(WINQUIT);
#ifdef WINSHUT
    qdevice(WINSHUT);
#endif
#ifdef QFULL
    qdevice(QFULL);
#endif
    tie(LEFTMOUSE, MOUSEX, MOUSEY);
    getsize(&width, &height);
    igl_resize((int)width, (int)height);
    igl_old_x = getvaluator(MOUSEX);
    igl_old_y = getvaluator(MOUSEY);
    return 1;
}

static int igl_texture_load(void)
{
    unsigned long *image, i, count;
#ifdef GD_TEXTURE
    float texture_properties[7];
    float environment[2];
#endif
    if (!texture_try_generate(TEXTURE_DEFAULT_SIZE))
    {
        igl_texture_failure("Cannot generate IRIS GL texture image");
        return FALSE;
    }
    if (!igl_native_texture)
    {
        if (!texsoft_texture(texture_size, texture_size / 2U, texture_data))
        {
            igl_texture_failure("Cannot allocate IRIS GL software texture mipmaps");
            return FALSE;
        }
    }
    else
    {
#ifdef GD_TEXTURE
        count = (unsigned long)texture_size * (texture_size / 2U);
        image = (unsigned long *)malloc((size_t)count * sizeof(unsigned long));
        if (image == NULL)
        {
            igl_texture_failure("Cannot allocate IRIS GL texture image");
            return FALSE;
        }
        for (i = 0; i < count; ++i)
            image[i] = 0xff000000UL | (unsigned long)texture_data[i * 3UL] |
                       ((unsigned long)texture_data[i * 3UL + 1UL] << 8) |
                       ((unsigned long)texture_data[i * 3UL + 2UL] << 16);
        texture_properties[0] = TX_MINFILTER;
        /* IRIX 4.0's broadly supported filter: native GL builds the mip
         * pyramid. Later trilinear tokens are not supported on every board. */
        texture_properties[1] = TX_MIPMAP_BILINEAR;
        texture_properties[2] = TX_MAGFILTER;
        texture_properties[3] = TX_BILINEAR;
        texture_properties[4] = TX_WRAP;
        texture_properties[5] = TX_REPEAT;
        texture_properties[6] = TX_NULL;
        texdef2d(1, 4, (long)texture_size, (long)(texture_size / 2U),
                   image, 7, texture_properties);
        free(image);
        environment[0] = TV_DECAL;
        environment[1] = TV_NULL;
        tevdef(1, 2, environment);
        if (!igl_texture_native_ready())
        {
            igl_texture_failure("IRIS GL native texturing is unavailable");
            return FALSE;
        }
#else
        image = NULL; i = count = 0;
        (void)image; (void)i; (void)count;
#endif
    }
    texture_free();
    drv_texture_loaded = TRUE;
    return TRUE;
}

void drv_texture_enable(void)
{
    igl_texture_failed = igl_texture_used = FALSE;
    drv_texture_enabled = TRUE;
    igl_invalidate_object();
    igl_dirty = TRUE;
    if (igl_native_texture && !igl_texture_native_ready())
    {
        igl_texture_failure("IRIS GL native texturing is unavailable");
        return;
    }
    if (!drv_texture_loaded && !igl_texture_load()) return;
    igl_bind_texture(TRUE);
}

void drv_texture_disable(void)
{
    igl_bind_texture(FALSE);
    drv_texture_enabled = FALSE;
    igl_texture_failed = igl_texture_used = FALSE;
#if !TEXTURE_CACHE_RASTER
    texsoft_release_raster();
#endif
    igl_invalidate_object();
    igl_dirty = TRUE;
}

static void igl_set_index_color(unsigned char index)
{
    if (igl_indexed) color((Colorindex)index);
    else cpack(0xff000000UL | (unsigned long)earth_cmap[index][0] |
                 ((unsigned long)earth_cmap[index][1] << 8) |
                 ((unsigned long)earth_cmap[index][2] << 16));
}

void drv_draw_tri_flat_color(Vertex *a, Vertex *b, Vertex *c, unsigned char index)
{
    igl_set_index_color(index);
    bgnpolygon();
    v3f((float *)a); v3f((float *)b); v3f((float *)c);
    endpolygon();
}

void drv_draw_tri_flat_uv(Vertex *a, Vertex *b, Vertex *c,
                          Coord *ua, Coord *ub, Coord *uc)
{
    if (!igl_native_texture)
    {
        texsoft_triangle(a, b, c, ua, ub, uc);
        return;
    }
#ifdef GD_TEXTURE
    cpack(0xffffffffUL);
    bgnpolygon();
    t2f((float *)ua); v3f((float *)a);
    t2f((float *)ub); v3f((float *)b);
    t2f((float *)uc); v3f((float *)c);
    endpolygon();
#endif
}

void drv_draw_tristrip_flat_color(unsigned int count,
                                  const Vertex TG_HUGE *vertices,
                                  const unsigned char TG_HUGE *facet_colors)
{
    unsigned int i;
    if (count < 3 || vertices == NULL || facet_colors == NULL) return;
    /* IRIS GL retains two vertices and alternates mesh winding. In FLAT
     * mode triangle n takes the color of vertex n+2, so every new vertex
     * supplies exactly one facet's original palette color. */
    bgntmesh();
    igl_set_index_color(facet_colors[0]);
    for (i = 0; i < count; ++i)
    {
        if (i >= 2U) igl_set_index_color(facet_colors[i - 2U]);
        v3f((float *)(vertices + i));
    }
    endtmesh();
}

void drv_draw_tristrip_flat_uv(unsigned int count,
                               const Vertex TG_HUGE *vertices,
                               const Coord TG_HUGE *coords)
{
#ifdef GD_TEXTURE
    unsigned int i;
#endif
    if (count < 3 || vertices == NULL || coords == NULL) return;
    if (!igl_native_texture)
    {
        drv_expand_tristrip_uv(count, vertices, coords, texsoft_triangle);
        return;
    }
#ifdef GD_TEXTURE
    cpack(0xffffffffUL);
    bgntmesh();
    for (i = 0; i < count; ++i)
    {
        t2f((float *)(coords + i));
        v3f((float *)(vertices + i));
    }
    endtmesh();
#endif
}

/* Font 0 is native GL's built-in bitmap font; no Font Manager is needed. */
static int igl_osd_text(const char *text, long baseline, long width, int draw)
{
    char line[256];
    unsigned int count, space;
    int lines;
    long height;
    height = getheight();
    if (height < 1) height = 14;
    lines = 0;
    while (*text != 0)
    {
        count = space = 0;
        while (text[count] != 0 && count < sizeof(line) - 1U)
        {
            line[count] = text[count]; line[count + 1U] = 0;
            if (strwidth(line) > width) break;
            if (text[count] == ' ') space = count;
            ++count;
        }
        if (text[count] != 0 && space > 0) count = space;
        if (count == 0) count = 1;
        memcpy(line, text, count); line[count] = 0;
        if (draw && baseline >= 0 && baseline < (long)drv_win_height)
        {
            cmov2i(8, baseline);
            charstr(line);
        }
        baseline -= height;
        ++lines;
        text += count;
        while (*text == ' ') ++text;
    }
    return lines;
}

void drv_draw_osd(void)
{
    Matrix model, projection;
    char status[256];
    long height;
    int help_lines;
    if (drv_win_width <= 16 || drv_win_height <= 16) return;
    mmode(MVIEWING); getmatrix(model);
    mmode(MPROJECTION); getmatrix(projection);
    if (igl_texture_used) igl_bind_texture(FALSE);
    if (igl_has_zbuffer) zbuffer(FALSE);
    backface(FALSE);
    mmode(MVIEWING); loadmatrix(igl_identity);
    ortho2(-0.5f, (float)drv_win_width - 0.5f,
             -0.5f, (float)drv_win_height - 0.5f);
    if (igl_indexed) color((Colorindex)igl_foreground);
    else cpack(0xffffffffUL);
    font(0);
    height = getheight(); if (height < 1) height = 14;
    drv_format_stats(status);
    strcat(status, !drv_texture_enabled ? ", texture off" :
             (igl_texture_used ? (igl_native_texture ? ", texture on" : ", texture soft") :
                                  ", texture fail"));
    igl_osd_text(status, (long)drv_win_height - height - 4L,
                   (long)drv_win_width - 16L, TRUE);
    help_lines = igl_osd_text(drv_help, 0, (long)drv_win_width - 16L, FALSE);
    igl_osd_text(drv_help, 8L + (help_lines - 1L) * height,
                   (long)drv_win_width - 16L, TRUE);
    mmode(MPROJECTION); loadmatrix(projection);
    mmode(MVIEWING); loadmatrix(model);
    backface(TRUE);
    if (igl_has_zbuffer) zbuffer(TRUE);
    if (igl_texture_used) igl_bind_texture(TRUE);
}

void drv_render(void)
{
    Matrix model, projection;
    unsigned long *pixels;
    int requested, ready, drawn;
    if (igl_window <= 0 || globe_verts_num == 0) return;
    igl_texture_used = FALSE;
    requested = drv_texture_enabled;
    drawn = FALSE;
    drv_fps_start();
    if (igl_indexed) color((Colorindex)igl_background);
    else cpack(0xff000000UL);
    clear();
    if (igl_has_zbuffer) zclear();
    mmode(MVIEWING); loadmatrix(igl_identity);
    translate(0.0f, 0.0f, -3000.0f);
    rot(drv_rot_x, 'x'); rot(drv_rot_y, 'y');
    rot(-90.0f, 'x'); rot(-90.0f, 'z');
    ready = requested && drv_texture_loaded && !igl_texture_failed;
    if (requested && !igl_texture_failed && !drv_texture_loaded)
        igl_texture_failure("IRIS GL texture data is not loaded");
    if (ready && igl_native_texture && !igl_texture_native_ready())
    {
        igl_texture_failure("IRIS GL native texturing is unavailable");
        ready = FALSE;
    }
    if (ready && !igl_native_texture)
    {
        getmatrix(model);
        mmode(MPROJECTION); getmatrix(projection);
        mmode(MVIEWING);
        if (!texsoft_begin(drv_win_width, drv_win_height, model, projection,
                              igl_indexed, &earth_cmap[0][0]))
            igl_texture_failure("Cannot prepare IRIS GL software texture framebuffer");
        else
        {
            globe_draw_tristrips();
            pixels = texsoft_pixels();
            if (pixels == NULL)
                igl_texture_failure("IRIS GL software texture pixels are unavailable");
            else
            {
                /* IRIX 4.0 lrectwrite is void: it transfers window pixels
                 * independently of the current object matrices. */
                if (igl_has_zbuffer) zbuffer(FALSE);
                lrectwrite(0, 0, (Screencoord)(drv_win_width - 1U),
                            (Screencoord)(drv_win_height - 1U), pixels);
                if (igl_has_zbuffer) zbuffer(TRUE);
                drawn = igl_texture_used = TRUE;
            }
        }
        if (!drawn) ready = FALSE;
    }
    if (!drawn)
    {
        /* Preserve the common flag as user intent; only the fallback
         * traversal sees flat mode, so the next T still toggles off. */
        drv_texture_enabled = ready;
        if (!ready) igl_bind_texture(FALSE);
        if (igl_object < 0)
        {
            igl_object = genobj();
            if (igl_object < 0) error("Cannot allocate IRIS GL geometry object");
            makeobj(igl_object);
            globe_draw_tristrips();
            closeobj();
        }
        if (isobj(igl_object)) callobj(igl_object);
        else
        {
            /* callobj silently ignores an undefined object. Submit this
             * frame directly, then rebuild the optional cache next time. */
            igl_object = -1;
            globe_draw_tristrips();
        }
        igl_texture_used = ready;
        drv_texture_enabled = requested;
    }
    if (drv_osd_enabled) drv_draw_osd();
    if (igl_double_buffer) swapbuffers();
    else gflush();
    drv_fps_stop();
    igl_dirty = FALSE;
    if (drv_auto_enabled && igl_active)
    {
        drv_globe_rotate();
        igl_dirty = TRUE;
    }
}

static void igl_keyboard(unsigned char key)
{
    if (key == 27)
    {
        drv_close();
        return;
    }
    if (!drv_handle_key(key)) return;
    if (key >= '1' && key <= '5') igl_invalidate_object();
    igl_dirty = TRUE;
}

static void igl_mouse_move(long x, long y)
{
    if (drv_lmouse_pressed && drv_win_width != 0 && drv_win_height != 0)
    {
        drv_rotate_drag(x - igl_old_x, igl_old_y - y);
        igl_dirty = TRUE;
    }
    igl_old_x = x; igl_old_y = y;
}

static void igl_event(long device, short value)
{
    long width, height;
    long x_device, y_device;
    short x, y;
    switch (device)
    {
        case KEYBD: igl_keyboard((unsigned char)value); break;
        case LEFTMOUSE:
            /* tie() guarantees that these two samples follow both edges. */
            x_device = qread(&x);
            y_device = qread(&y);
            if (x_device == MOUSEX && y_device == MOUSEY)
            {
                if (drv_lmouse_pressed) igl_mouse_move(x, y);
                else { igl_old_x = x; igl_old_y = y; }
                drv_lmouse_pressed = value != 0;
            }
            else
            {
                qreset();
                drv_lmouse_pressed = getbutton(LEFTMOUSE) != 0;
                drv_rmouse_pressed = getbutton(RIGHTMOUSE) != 0;
                igl_old_x = getvaluator(MOUSEX);
                igl_old_y = getvaluator(MOUSEY);
            }
            break;
        case MOUSEX: igl_mouse_move(value, igl_old_y); break;
        case MOUSEY: igl_mouse_move(igl_old_x, value); break;
        case RIGHTMOUSE:
            drv_rmouse_pressed = value != 0;
            igl_dirty = TRUE;
            break;
        case REDRAW:
            if (value != 0 && value != igl_window) break;
            reshapeviewport(); getsize(&width, &height);
            igl_resize((int)width, (int)height);
            break;
        case INPUTCHANGE:
            igl_active = value == igl_window;
            drv_lmouse_pressed = FALSE;
            drv_rmouse_pressed = FALSE;
            igl_old_x = getvaluator(MOUSEX); igl_old_y = getvaluator(MOUSEY);
            igl_dirty = TRUE;
            break;
        case WINQUIT:
#ifdef WINSHUT
        case WINSHUT:
#endif
            if (value == 0 || value == igl_window) drv_close();
            break;
#ifdef QFULL
        case QFULL:
            qreset();
            drv_lmouse_pressed = getbutton(LEFTMOUSE) != 0;
            drv_rmouse_pressed = getbutton(RIGHTMOUSE) != 0;
            igl_old_x = getvaluator(MOUSEX); igl_old_y = getvaluator(MOUSEY);
            igl_dirty = TRUE;
            break;
#endif
        default: break;
    }
}

void drv_loop(void)
{
    short value;
    long device;
    while (igl_running)
    {
        while (igl_running && qtest())
        {
            device = qread(&value);
            igl_event(device, value);
        }
        if (!igl_running) break;
        if (igl_dirty || (drv_auto_enabled && igl_active)) drv_render();
        else
        {
            device = qread(&value);
            igl_event(device, value);
        }
    }
    igl_cleanup();
}
#endif /* DRV_IGL */
