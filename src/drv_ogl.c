/*
 * testglobe - display 3d globe to test 3d hardware
 * drv_ogl.c - OpenGL driver
 *
 * Copyright (C) 2024 trguhq
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include "testglob.h"
#include "drv.h"
#include "globe.h"
#include "glob_ext.h"
#include "drv_ext.h"
#include "tex_ext.h"
#include "texsoft.h"

#ifdef DRV_OGL

#ifdef OGL_AMIGA
#include "ogl_ami.h"
#elif defined(__APPLE__)
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#ifndef OGL_AMIGA
/* OpenGL font pointer */
static void *ogl_font;
static int ogl_font_ascent = 12, ogl_font_descent = 4;
static int ogl_font_step = 18;

static void ogl_select_font(int width, int height)
{
    const unsigned char *text;
    int tier, extent;
    tier = width < 480 || height < 320 ? 0 :
           (width >= 1024 && height >= 600 ? 2 : 1);
    /* Classic GLUT supplies widths but no bitmap-height query. These stock
     * font bounds include descenders; keep two pixels between lines. Prefer
     * a smaller font before wrapping the fixed help text, only on resize. */
    for (;;)
    {
        if (tier == 0)
        {
            ogl_font = GLUT_BITMAP_HELVETICA_10;
            ogl_font_ascent = 11; ogl_font_descent = 3;
        }
        else if (tier == 2)
        {
            ogl_font = GLUT_BITMAP_HELVETICA_18;
            ogl_font_ascent = 18; ogl_font_descent = 5;
        }
        else
        {
            ogl_font = GLUT_BITMAP_HELVETICA_12;
            ogl_font_ascent = 12; ogl_font_descent = 4;
        }
        if (tier == 0) break;
        extent = 0;
        for (text = (const unsigned char *)drv_help; *text != 0; ++text)
            extent += glutBitmapWidth(ogl_font, *text);
        if (extent <= width - 20) break;
        --tier;
    }
    ogl_font_step = ogl_font_ascent + ogl_font_descent + 2;
}
#endif

/* previous mouse pointer location */
static int ogl_old_x;
static int ogl_old_y;

/* display list for currently selected sphere tesselation. Lazily constructed
 * on first use.
 */
static GLuint dlist;
static int ogl_dlist_enabled = DRV_DLIST_DEFAULT;
static int ogl_dlist_used;
static int ogl_dlist_blocked;
static unsigned long ogl_dlist_revision;
static int ogl_dlist_textured;

static GLuint texture_id;
static int ogl_texture_failed;
static int ogl_texture_used;
static int ogl_window;
static int ogl_indexed;
static int ogl_double_buffer;
static int ogl_has_depth;
static unsigned int ogl_palette_size;
#ifndef OGL_AMIGA
static unsigned int ogl_foreground, ogl_background;
#endif
#ifdef OGL_AMIGA
/* MiniGL's immediate buffer is bounded, including while recording lists. */
static unsigned int ogl_triangle_vertices;
#endif

typedef struct Ogl_format
{
    int rgba, red, green, blue, alpha, buffer, depth, double_buffer, palette;
    int alpha_known;
} Ogl_format;

static void ogl_texture_failure(const char *message);
static void ogl_render_state(void);

static void ogl_invalidate_dlist(void)
{
    if (dlist != 0
#ifdef OGL_AMIGA
        && ogl_ami_ready()
#endif
       ) glDeleteLists(dlist, 1);
    dlist = 0;
    ogl_dlist_used = FALSE;
}

/* Called only between frames: never release data inside glBegin/glEnd or
 * while compiling a list. Keep user requests separate from cache pressure. */
void drv_release_optional(void)
{
    if (drv_texture_active) return;
    ogl_invalidate_dlist();
    ogl_dlist_blocked = TRUE;
    texsoft_release_raster();
    if (!drv_texture_enabled)
    {
        if (texture_id != 0
#ifdef OGL_AMIGA
            && ogl_ami_ready()
#endif
           ) glDeleteTextures(1, &texture_id);
        texture_id = 0;
        texsoft_free();
        texture_free();
        drv_texture_loaded = FALSE;
    }
}

static void ogl_cleanup(void)
{
#ifdef OGL_AMIGA
    if (ogl_ami_ready())
    {
        if (dlist != 0) glDeleteLists(dlist, 1);
        if (texture_id != 0) glDeleteTextures(1, &texture_id);
    }
    ogl_ami_cleanup();
#else
    if (ogl_window > 0 && glutGetWindow() != 0)
    {
        if (dlist != 0) glDeleteLists((GLuint)dlist, 1);
        if (texture_id != 0) glDeleteTextures(1, &texture_id);
        glutDestroyWindow(ogl_window);
    }
#endif
    ogl_window = 0; dlist = 0; texture_id = 0;
    ogl_dlist_used = FALSE;
    ogl_texture_failed = ogl_texture_used = FALSE;
    drv_texture_loaded = drv_texture_enabled = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
    texsoft_free();
}

#ifndef OGL_AMIGA
/* Apple GLUT can service pending redraws during window management. Every
 * window needs a display callback, even a short-lived format probe. Keep
 * drawing disabled until the application window and globe are ready. */
static void ogl_pending_display(void)
{
}

/* These queries describe the window actually created, including legacy GLUT
 * implementations which cannot check a format before creating a window. */
static int ogl_window_format(Ogl_format *format)
{
    memset(format, 0, sizeof(*format));
    format->rgba = glutGet(GLUT_WINDOW_RGBA) > 0;
    format->buffer = glutGet(GLUT_WINDOW_BUFFER_SIZE);
    format->depth = glutGet(GLUT_WINDOW_DEPTH_SIZE);
    format->double_buffer = glutGet(GLUT_WINDOW_DOUBLEBUFFER) > 0;
    if (format->depth < 0) format->depth = 0;
    if (format->rgba)
    {
        format->red = glutGet(GLUT_WINDOW_RED_SIZE);
        format->green = glutGet(GLUT_WINDOW_GREEN_SIZE);
        format->blue = glutGet(GLUT_WINDOW_BLUE_SIZE);
        format->alpha = glutGet(GLUT_WINDOW_ALPHA_SIZE);
        format->alpha_known = format->alpha >= 0;
        if (format->alpha < 0) format->alpha = 0;
        if (format->red <= 0 || format->green <= 0 || format->blue <= 0)
            return FALSE;
        if (format->buffer <= 0)
            format->buffer = format->red + format->green + format->blue + format->alpha;
    }
    else
    {
        format->palette = glutGet(GLUT_WINDOW_COLORMAP_SIZE);
        if (format->buffer < 8 || format->palette < 256) return FALSE;
    }
    return TRUE;
}

#ifdef GLUT_DISPLAY_MODE_POSSIBLE
static int ogl_better_format(const Ogl_format *candidate, const Ogl_format *best)
{
    int total, previous, rgb, old_rgb;
    if (candidate->rgba != best->rgba) return candidate->rgba > best->rgba;
    if (!candidate->rgba)
    {
        if (candidate->buffer != best->buffer) return candidate->buffer > best->buffer;
    }
    else
    {
        rgb = candidate->red + candidate->green + candidate->blue;
        old_rgb = best->red + best->green + best->blue;
        total = rgb + candidate->alpha; previous = old_rgb + best->alpha;
        if (total != previous) return total > previous;
        if (candidate->alpha != best->alpha) return candidate->alpha > best->alpha;
        if (rgb != old_rgb) return rgb > old_rgb;
        if (candidate->alpha_known != best->alpha_known)
            return candidate->alpha_known > best->alpha_known;
        if (candidate->buffer != best->buffer) return candidate->buffer > best->buffer;
    }
    if (candidate->double_buffer != best->double_buffer)
        return candidate->double_buffer > best->double_buffer;
    return candidate->depth > best->depth;
}
#endif

/* GLUT exposes requests and selected-window queries, rather than a portable
 * enumeration of every native pixel format. Probe supported request classes
 * and rank their actual results, giving color precision priority over buffers. */
static int ogl_select_window(Ogl_format *selected, int x, int y, int width, int height)
{
#ifdef GLUT_DISPLAY_MODE_POSSIBLE
    static const char *requests[] = {
        "rgba red>=8 green>=8 blue>=8 alpha>=8",
        /* Classic GLUT's bare "rgba" also requires an alpha channel. */
        "rgb red>=8 green>=8 blue>=8",
        "rgb red>=5 green>=6 blue>=5",
        "rgb red>=5 green>=5 blue>=5",
        "rgb", "index buffer>=8"
    };
    Ogl_format current;
    char request[160], best_request[160];
    unsigned int mode, best_mode;
    int quality, double_buffer, depth, window, best;
    best = 0;
    best_mode = 0;
    best_request[0] = 0;
    glutInitWindowSize(1, 1);
    glutInitWindowPosition(-32768, -32768);
    for (quality = 0; quality < 6; ++quality)
        for (double_buffer = 1; double_buffer >= 0; --double_buffer)
            for (depth = 1; depth >= 0; --depth)
            {
                mode = quality == 5 ? GLUT_INDEX : GLUT_RGBA;
                if (quality == 0) mode |= GLUT_ALPHA;
                mode |= double_buffer ? GLUT_DOUBLE : GLUT_SINGLE;
                if (depth) mode |= GLUT_DEPTH;
                glutInitDisplayMode(mode);
#if defined(GLUT_API_VERSION) && GLUT_API_VERSION >= 4
                sprintf(request, "%s %s%s", requests[quality],
                         double_buffer ? "double" : "single",
                         depth ? " depth>=16" : "");
                glutInitDisplayString(request);
#else
                (void)requests; (void)request;
#endif
                if (!glutGet(GLUT_DISPLAY_MODE_POSSIBLE)) continue;
                window = glutCreateWindow("testglobe pixel-format probe");
                if (window <= 0) continue;
                glutDisplayFunc(ogl_pending_display);
                glutHideWindow();
                if (!ogl_window_format(&current))
                {
                    glutDestroyWindow(window);
                    continue;
                }
                if (best == 0 || ogl_better_format(&current, selected))
                {
                    best = 1;
                    best_mode = mode;
#if defined(GLUT_API_VERSION) && GLUT_API_VERSION >= 4
                    strcpy(best_request, request);
#endif
                    *selected = current;
                }
                glutDestroyWindow(window);
            }
    if (best == 0) return 0;
    /* Do not promote a hidden one-pixel probe to the application window.
     * Some GLUT implementations discard its initial visibility/resize redraw.
     * Reapply the winning request to a normal, freshly created window. */
    glutInitDisplayMode(best_mode);
#if defined(GLUT_API_VERSION) && GLUT_API_VERSION >= 4
    glutInitDisplayString(best_request);
#else
    (void)best_request;
#endif
    glutInitWindowSize(width, height);
    glutInitWindowPosition(x, y);
    window = glutCreateWindow(TESTGLOBE_NAME);
    if (window > 0) glutDisplayFunc(ogl_pending_display);
    return window;
#else
    int window;
    /* Older GLUT may terminate on an unsupported request. Use the original
     * RGB/double-buffer request once, without probing or requiring newer
     * display-string APIs, and validate the actual returned format. */
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutInitWindowSize(width, height);
    glutInitWindowPosition(x, y);
    window = glutCreateWindow(TESTGLOBE_NAME);
    if (window <= 0) return 0;
    glutDisplayFunc(ogl_pending_display);
    if (!ogl_window_format(selected))
    {
        glutDestroyWindow(window);
        return 0;
    }
    return window;
#endif
}

static void ogl_install_palette(void)
{
    unsigned int index, brightness, minimum, maximum;
    minimum = 0xffffffffU; maximum = 0;
    for (index = 0; index < 256; ++index)
    {
        glutSetColor((int)index, (GLfloat)earth_cmap[index][0] / 255.0f,
                      (GLfloat)earth_cmap[index][1] / 255.0f,
                      (GLfloat)earth_cmap[index][2] / 255.0f);
        brightness = (unsigned int)earth_cmap[index][0] * 77U +
                     (unsigned int)earth_cmap[index][1] * 150U +
                     (unsigned int)earth_cmap[index][2] * 29U;
        if (brightness < minimum) { minimum = brightness; ogl_background = index; }
        if (brightness > maximum) { maximum = brightness; ogl_foreground = index; }
    }
}
#endif /* !OGL_AMIGA: GLUT format selection and indexed palettes */

static void ogl_post_redisplay(void)
{
#ifdef OGL_AMIGA
    ogl_ami_post_redisplay();
#else
    glutPostRedisplay();
#endif
}

/* cleanly close window */
void drv_close(void)
{
    exit(0);
}

/* Portable diagnostics do not depend on optional debug-callback extensions. */
static void ogl_message(GLenum code, const char *message)
{
#if DEBUG
    fprintf(stderr, "OpenGL error 0x%x: %s!\n", (unsigned int)code, message);
#else
    (void)code;
    fprintf(stderr, "OpenGL error: %s!\n", message);
#endif
}

/* keyboard input */
static void ogl_keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 27)
    {
        drv_close();
        return;
    }
    if (key == 'd' || key == 'D')
    {
        ogl_dlist_enabled = !ogl_dlist_enabled;
        ogl_dlist_blocked = FALSE;
        ogl_invalidate_dlist();
        drv_reset_timing();
    }
    else if (!drv_handle_key(key)) return;
    ogl_dlist_used = FALSE;
    ogl_post_redisplay();
}

/* callback for mouse movement */
static void ogl_mouse_move(int x, int y)
{
    if (drv_lmouse_pressed)
    {
        drv_rotate_drag((long)x - ogl_old_x, (long)y - ogl_old_y);
        ogl_old_x = x;
        ogl_old_y = y;
        ogl_post_redisplay();
    }
}

/* callback for mouse click */
static void ogl_mouse(int button, int state, int x, int y)
{
    int pressed;
#ifdef OGL_AMIGA
    if (state != OGL_AMI_DOWN && state != OGL_AMI_UP) return;
    pressed = state == OGL_AMI_DOWN;
    if (button == OGL_AMI_RIGHT_BUTTON)
#else
    if (state != GLUT_DOWN && state != GLUT_UP) return;
    pressed = state == GLUT_DOWN;
    if (button == GLUT_RIGHT_BUTTON)
#endif
    {
        drv_rmouse_pressed = pressed;
        ogl_post_redisplay();
    }
#ifdef OGL_AMIGA
    else if (button == OGL_AMI_LEFT_BUTTON && drv_lmouse_pressed != pressed)
#else
    else if (button == GLUT_LEFT_BUTTON && drv_lmouse_pressed != pressed)
#endif
    {
        drv_lmouse_pressed = pressed;
        ogl_old_x = x;
        ogl_old_y = y;
    }
}

/* Only a failed image allocation may retry at a smaller size. Mixed errors
 * remain native failures; other operations retain their usual diagnostics. */
static Drv_result ogl_collect_errors(int image_allocation)
{
    const char *error_msg;
    GLenum code;
    unsigned int i;
    Drv_result result;
    result = DRV_OK;
    /* A lost context must not trap the application in an endless error loop. */
    for (i = 0; i < 16U; ++i)
    {
        code = glGetError();
        switch(code)
        {
        case GL_NO_ERROR:
            return result;
        case GL_INVALID_ENUM:
            error_msg = "Invalid enum";
            break;
        case GL_INVALID_VALUE:
            error_msg = "Invalid value";
            break;
        case GL_INVALID_OPERATION:
            error_msg = "Invalid operation";
            break;
        case GL_STACK_OVERFLOW:
            error_msg = "Stack overflow";
            break;
        case GL_STACK_UNDERFLOW:
            error_msg = "Stack underflow";
            break;
        case GL_OUT_OF_MEMORY:
            if (image_allocation)
            {
                if (result == DRV_OK) result = DRV_NO_MEMORY;
                continue;
            }
            error_msg = "Out of memory";
            break;
#ifdef GL_INVALID_FRAMEBUFFER_OPERATION
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            error_msg = "Invalid framebuffer operation";
            break;
#endif
#ifdef GL_CONTEXT_LOST
        case GL_CONTEXT_LOST:
            error_msg = "Context lost";
            break;
#endif
#ifdef GL_TABLE_TOO_LARGE1
        case GL_TABLE_TOO_LARGE1:
            error_msg = "Table too large";
            break;
#endif
        default:
            error_msg = "Unknown";
            break;
        }
        result = DRV_NATIVE_FAILED;
        ogl_message(code, error_msg);
    }
    return DRV_NATIVE_FAILED;
}

/* Drain reported errors before committing native resource state. */
static int ogl_checkerror(void)
{
    return ogl_collect_errors(FALSE) != DRV_OK;
}

/* initialize graphics subsystem */
void drv_init(int *argc, char **argv)
{
#ifdef OGL_AMIGA
    if (atexit(ogl_cleanup) != 0) error("Cannot register OpenGL cleanup");
    ogl_ami_init(argc, argv);
#else
    glutInit(argc, argv);
    if (atexit(ogl_cleanup) != 0) error("Cannot register OpenGL cleanup");
    ogl_select_font((int)drv_win_width, (int)drv_win_height);
#endif
}

#ifndef OGL_AMIGA
/* A color-index OpenGL framebuffer cannot perform RGB texture mapping.
 * Keep its texture toggle functional using the shared perspective-correct
 * rasterizer, then transfer tightly packed palette bytes through OpenGL. */
static int ogl_present_indexed_texture(void)
{
    const unsigned char *pixels;
    GLint matrix_mode;
    pixels = texsoft_index_pixels();
    if (pixels == NULL) return FALSE;
    glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_PIXEL_MODE_BIT | GL_CURRENT_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelTransferi(GL_INDEX_SHIFT, 0);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
    glPixelZoom(1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, drv_win_width, 0, drv_win_height);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glRasterPos2i(0, 0);
    glDrawPixels((GLsizei)drv_win_width, (GLsizei)drv_win_height,
                  GL_COLOR_INDEX, GL_UNSIGNED_BYTE, pixels);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode((GLenum)matrix_mode);
    glPopClientAttrib(); glPopAttrib();
    return !ogl_checkerror();
}
#endif

/* Keep the pole triangles in one batch; each latitude band opens its own
 * native strip. Indexed textures expand the same geometry in software. */
static void ogl_draw_globe(void)
{
#ifdef OGL_AMIGA
    ogl_triangle_vertices = 0;
#endif
#ifdef TRI
#ifdef OGL_AMIGA
    if (drv_texture_active) glBegin(GL_TRIANGLES);
    globe_draw_tris();
    if (drv_texture_active) glEnd();
#else
    if (ogl_indexed && drv_texture_active)
        globe_draw_tris();
    else
    {
        glBegin(GL_TRIANGLES);
        globe_draw_tris();
        glEnd();
    }
#endif
#endif
#ifdef TRISTRIP
#ifdef DRV_TRI_CAPS
#ifdef OGL_AMIGA
    if (drv_texture_active) glBegin(GL_TRIANGLES);
    globe_draw_pole_tris();
    if (drv_texture_active) glEnd();
#else
    if (ogl_indexed && drv_texture_active)
        globe_draw_pole_tris();
    else
    {
        glBegin(GL_TRIANGLES);
        globe_draw_pole_tris();
        glEnd();
    }
#endif
#endif
    globe_draw_tristrips();
#endif
}

/* OpenGL 1.1 has no separate display-list support query. Compile and
 * validate the native object, then accept it only after error-free use. */
static int ogl_draw_dlist(void)
{
    GLboolean valid;
    int failed;
#ifdef OGL_AMIGA
    /* PiStorm3D V29.1 records uniform-color batches as color-agnostic:
     * replay substitutes the current color even after an explicit glColor.
     * Flat Earth colors therefore require immediate submissions. Textured
     * GL_REPLACE batches do not depend on that color and can use lists. */
    if (!drv_texture_active) return FALSE;
#endif
    if (ogl_dlist_blocked || !globe_ready()) return FALSE;
    if (dlist != 0 && (ogl_dlist_revision != globe_revision ||
                      ogl_dlist_textured != drv_texture_active))
        ogl_invalidate_dlist();
    ogl_checkerror();
    failed = FALSE;
    if (dlist == 0)
    {
        /* Compile before executing so a rejected or incomplete list does
         * not draw part of the globe before the immediate fallback. */
        dlist = glGenLists(1);
        failed = ogl_checkerror();
        if (dlist != 0 && !failed)
        {
            glNewList(dlist, GL_COMPILE);
            failed = ogl_checkerror();
            if (!failed)
            {
                ogl_draw_globe();
                glEndList();
                failed = ogl_checkerror() || drv_frame_failed;
                if (!failed)
                {
                    ogl_dlist_revision = globe_revision;
                    ogl_dlist_textured = drv_texture_active;
                }
            }
        }
        else failed = TRUE;
    }
    if (!failed)
    {
        valid = glIsList(dlist);
        failed = ogl_checkerror();
        if (!valid) failed = TRUE;
    }
    if (!failed)
    {
        glCallList(dlist);
        failed = ogl_checkerror();
    }
    if (failed)
    {
        if (dlist != 0) glDeleteLists(dlist, 1);
        dlist = 0;
        ogl_dlist_blocked = TRUE;
        if (!drv_frame_failed)
            drv_notice("Display list unavailable; drawing directly");
    }
    return !failed;
}

/* Texture object and enable/binding queries are available in OpenGL 1.1. */
static int ogl_texture_native_ready(void)
{
    GLboolean valid, enabled;
#ifndef OGL_AMIGA
    GLint binding;
#endif
    int failed;
    if (texture_id == 0) return FALSE;
#ifdef OGL_AMIGA
    /* MiniGL V29 defines the binding enum but does not implement its query.
     * Reassert our binding instead, then validate the object, enable, and
     * native errors. This driver owns all texture-binding changes. */
    glBindTexture(GL_TEXTURE_2D, texture_id);
#endif
    valid = glIsTexture(texture_id);
#ifndef OGL_AMIGA
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
#endif
    enabled = glIsEnabled(GL_TEXTURE_2D);
    failed = ogl_checkerror();
#ifdef OGL_AMIGA
    return !failed && valid && enabled;
#else
    return !failed && valid && enabled && (GLuint)binding == texture_id;
#endif
}

/* render current scene */
void drv_render(void)
{
#ifndef OGL_AMIGA
    float model[4][4], projection[4][4];
#endif
    int requested, ready, drawn;
#ifdef OGL_AMIGA
    if (!ogl_ami_ready()) return;
#endif
    ogl_dlist_used = FALSE;
    ogl_texture_used = FALSE;
    requested = drv_texture_enabled;
    drawn = FALSE;
    drv_fps_start();
    if (!globe_ready()) drv_frame_failed = TRUE;
    glClear(GL_COLOR_BUFFER_BIT | (ogl_has_depth ? GL_DEPTH_BUFFER_BIT : 0));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0,0, -GLOBE_VIEW_DISTANCE);
    glRotatef(drv_rot_x, 1, 0, 0);
    glRotatef(drv_rot_y, 0, 1, 0);
    glRotatef(-90, 1, 0, 0);
    glRotatef(-90, 0, 0, 1);

    ready = globe_ready() && requested && drv_texture_loaded && !ogl_texture_failed;
    if (globe_ready() && requested && !ogl_texture_failed && !drv_texture_loaded)
        ogl_texture_failure("OpenGL texture data is not loaded");
    if (ready && !ogl_indexed && !ogl_texture_native_ready())
    {
        ogl_texture_failure("OpenGL texture is not active");
        ready = FALSE;
    }
    drv_texture_active = ready;
#ifndef OGL_AMIGA
    if (ogl_indexed && ready)
    {
        /* Column-major OpenGL matrix memory is the equivalent row-vector
         * array consumed by texsoft; no further transpose is needed. */
        glGetFloatv(GL_MODELVIEW_MATRIX, &model[0][0]);
        glGetFloatv(GL_PROJECTION_MATRIX, &projection[0][0]);
        if (ogl_checkerror() ||
            !texsoft_begin(drv_win_width, drv_win_height, model, projection,
                             TEXSOFT_INDEX_BYTES, &earth_cmap[0][0]))
            ogl_texture_failure("Cannot prepare indexed OpenGL texture raster");
        else
        {
            ogl_draw_globe();
            if (drv_frame_failed) ready = FALSE;
            else if (ogl_present_indexed_texture())
                drawn = ogl_texture_used = TRUE;
            else
                ogl_texture_failure("Cannot present indexed OpenGL texture raster");
        }
        if (!drawn) ready = FALSE;
    }
#endif
    if (!drawn && globe_ready())
    {
        /* Keep the public flag as the user's request. Only the fallback
         * traversal sees texturing disabled, so T still toggles normally. */
        drv_texture_active = ready;
        if (!ready) glDisable(GL_TEXTURE_2D);
#ifdef OGL_AMIGA
        else glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
#endif
        if (requested && ogl_texture_failed)
            glClear(GL_COLOR_BUFFER_BIT | (ogl_has_depth ? GL_DEPTH_BUFFER_BIT : 0));
        /* Whole-globe lists cannot select view-dependent longitude runs.
         * Preserve the requested D setting and cached full mesh while C
         * temporarily forces direct, pre-culled submissions. */
        if (ogl_dlist_enabled && !drv_cull_enabled)
            ogl_dlist_used = ogl_draw_dlist();
        if (!ogl_dlist_used) ogl_draw_globe();
        if (ready)
        {
            if (ogl_checkerror())
            {
                ogl_texture_failure("Cannot draw OpenGL texture");
                drv_texture_active = FALSE;
                glClear(GL_COLOR_BUFFER_BIT | (ogl_has_depth ? GL_DEPTH_BUFFER_BIT : 0));
                ogl_draw_globe();
            }
            else ogl_texture_used = TRUE;
        }
    }
    drv_texture_active = FALSE;
    if (drv_frame_failed)
    {
        ogl_texture_used = ogl_dlist_used = FALSE;
        glClear(GL_COLOR_BUFFER_BIT | (ogl_has_depth ? GL_DEPTH_BUFFER_BIT : 0));
    }

#ifndef OGL_AMIGA
    if (drv_status_required())
    {
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        drv_draw_osd();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }
    
    if (ogl_double_buffer) glutSwapBuffers();
    else glFlush();
#else
    ogl_ami_present();
    /* The native font writes to the presented Workbench window, outside
     * MiniGL's framebuffer. Its colors and state do not modify GL state. */
    if (drv_status_required()) drv_draw_osd();
#endif
    ogl_checkerror();
    drv_fps_stop();
    
    if (drv_animation_allowed())
    {
        drv_globe_rotate();
        ogl_post_redisplay();
    }
}

/* resize window */
static void ogl_resize(int width, int height) {
#ifdef OGL_AMIGA
    int changed;
    changed = ogl_ami_resize_context(width, height);
    if (changed != 1)
    {
        /* Context destruction owns its GPU objects. Their old names must
         * never be deleted or rebound in the replacement context. */
        dlist = texture_id = 0;
        ogl_dlist_used = ogl_dlist_blocked = FALSE;
        ogl_texture_used = ogl_texture_failed = FALSE;
        drv_texture_loaded = FALSE;
    }
    if (!changed) return;
    if (changed == 2) ogl_render_state();
#else
    if (width < 1 || height < 1) return;
    ogl_select_font(width, height);
#endif
    drv_win_width = width;
    drv_win_height = height;
    drv_reset_timing();
    
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(GLOBE_FIELD_OF_VIEW, (float)width/(float)height,
                     GLOBE_NEAR_DISTANCE, GLOBE_FAR_DISTANCE);
#ifdef OGL_AMIGA
    if (changed == 2 && drv_texture_enabled) drv_texture_enable();
#endif
}

/* Near the silhouette, isotropic mipmaps average distant land and ice into
 * narrow ocean footprints, producing pale edge speckles. Bilinear sampling
 * uses only level zero on every OpenGL 1.1 implementation, including MiniGL.
 * Upload no unused mip levels and keep allocation retries bounded below. */
static Drv_result ogl_upload_texture(void)
{
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_size,
                  texture_size / 2U, 0, GL_RGB, GL_UNSIGNED_BYTE, texture_data);
    return ogl_collect_errors(TRUE);
}

static void ogl_texture_failure(const char *message)
{
    ogl_texture_failed = TRUE;
    ogl_texture_used = FALSE;
    drv_texture_loaded = FALSE;
    ogl_invalidate_dlist();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (texture_id != 0) glDeleteTextures(1, &texture_id);
    texture_id = 0;
    texture_free();
    texsoft_free();
    fprintf(stderr, "OpenGL texture: %s\n", message);
    drv_reset_timing();
}

/* One complete candidate attempt. Allocation failures leave no partial GPU
 * image or staging allocation; the caller owns the single retry policy. */
static Drv_result ogl_texture_attempt(unsigned int size, const char **reason)
{
    Drv_result result;
    unsigned char *rgb;
    *reason = NULL;
    result = texture_build((int)size);
    if (result == DRV_NO_MEMORY) return result;
    if (result != DRV_OK) error("Invalid embedded OpenGL texture data");
    if (ogl_indexed)
    {
        rgb = texture_take_data();
        result = texsoft_texture_take_filtered(size, size / 2U, rgb,
                                               TEXSOFT_BILINEAR);
        if (result != DRV_OK && result != DRV_NO_MEMORY)
            error("Invalid OpenGL software texture image");
        return result;
    }
    ogl_checkerror();
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    if (ogl_checkerror() || texture_id == 0)
    {
        *reason = "Cannot allocate OpenGL texture";
        return DRV_NATIVE_FAILED;
    }
    result = ogl_upload_texture();
    if (result == DRV_NO_MEMORY)
    {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &texture_id);
        texture_id = 0;
        texture_free();
        if (ogl_checkerror())
        {
            *reason = "Cannot release OpenGL texture";
            return DRV_NATIVE_FAILED;
        }
        return DRV_NO_MEMORY;
    }
    if (result != DRV_OK)
    {
        *reason = "Cannot upload OpenGL texture";
        return result;
    }
    texture_free();
    return DRV_OK;
}

/* Try each supported size, with at most one optional-resource eviction. */
static int ogl_texture_load(void)
{
    Drv_result result;
    unsigned int size;
    GLint maximum;
    int released;
    const char *reason;
    char message[64];
    released = FALSE;
    size = TEXTURE_DEFAULT_SIZE;
    if (!ogl_indexed)
    {
        maximum = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
        if (ogl_checkerror())
        {
            ogl_texture_failure("Cannot query OpenGL texture capacity");
            return FALSE;
        }
        /* Use an existing power-of-two map that fits the OpenGL 1.1 limit. */
        if (maximum < 32)
        {
            ogl_texture_failure("OpenGL texture capacity is below 32 pixels");
            return FALSE;
        }
        while (size > (unsigned int)maximum && size > 32U)
            size /= 2U;
    }
    for (; size >= 32U; size /= 2U)
    {
        result = ogl_texture_attempt(size, &reason);
        if (result == DRV_NO_MEMORY && !released)
        {
            drv_release_optional();
            released = TRUE;
            result = ogl_texture_attempt(size, &reason);
        }
        if (result == DRV_NO_MEMORY) continue;
        if (result != DRV_OK)
        {
            ogl_texture_failure(reason != NULL ? reason : "Cannot load OpenGL texture");
            return FALSE;
        }
        drv_texture_loaded = TRUE;
        if (size < TEXTURE_DEFAULT_SIZE)
        {
            sprintf(message, "Texture reduced to %u x %u", size, size / 2U);
            drv_notice(message);
        }
        return TRUE;
    }
    ogl_texture_failure("Insufficient memory for texture; drawing flat colors");
    drv_notice("Texture unavailable; drawing flat colors");
    return FALSE;
}

/* set rendering pipeline to textures */
void drv_texture_enable(void)
{
    ogl_texture_failed = ogl_texture_used = FALSE;
    ogl_invalidate_dlist();
    ogl_dlist_blocked = FALSE;
    drv_texture_enabled = TRUE;
#ifdef OGL_AMIGA
    /* A resize allocation failure leaves the window interactive. Remember
     * the request and load it when the context can be recreated. */
    if (!ogl_ami_ready()) return;
#endif
    if (!drv_texture_loaded && !ogl_texture_load()) return;
    if (!ogl_indexed)
    {
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glEnable(GL_TEXTURE_2D);
        if (ogl_checkerror())
            ogl_texture_failure("Cannot enable OpenGL texture");
    }
}

/* turn off textures */
void drv_texture_disable(void)
{
    ogl_invalidate_dlist();
    ogl_texture_failed = ogl_texture_used = FALSE;
    if (!ogl_indexed
#ifdef OGL_AMIGA
        && ogl_ami_ready()
#endif
       )
    {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    drv_texture_enabled = FALSE;
#if !TEXTURE_CACHE_RASTER
    texsoft_release_raster();
#endif
}

static void ogl_render_state(void)
{
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glShadeModel(GL_FLAT);
#ifndef OGL_AMIGA
    if (ogl_indexed)
    {
        ogl_install_palette();
        glClearIndex((GLfloat)ogl_background);
    }
    else
#endif
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if (ogl_has_depth) glEnable(GL_DEPTH_TEST);
}

/* initialize a window, all values < 1 for full screen/default */
int drv_init_window(int in_x, int in_y, int in_width, int in_height)
{
    Ogl_format selected;
    unsigned int rgb_bits;
    if (in_x > 0)
        drv_win_x = in_x;
    else
        in_x = drv_win_x;
    if (in_y > 0)
        drv_win_y = in_y;
    else
        in_y = drv_win_y;
    if (in_width > 0)
        drv_win_width = in_width;
    else
        in_width = drv_win_width;
    if (in_height > 0)
        drv_win_height = in_height;
    else
        in_height = drv_win_height;

#ifdef OGL_AMIGA
    memset(&selected, 0, sizeof(selected));
    ogl_window = ogl_ami_create_window(in_x, in_y, in_width, in_height);
    if (ogl_window == 0) return FALSE;
    ogl_ami_get_size(&in_width, &in_height);
    selected.rgba = TRUE;
    /* MiniGL presents its offscreen bitmap; this is not GL double buffering. */
    selected.double_buffer = FALSE;
    glGetIntegerv(GL_RED_BITS, &selected.red);
    glGetIntegerv(GL_GREEN_BITS, &selected.green);
    glGetIntegerv(GL_BLUE_BITS, &selected.blue);
    glGetIntegerv(GL_ALPHA_BITS, &selected.alpha);
    glGetIntegerv(GL_DEPTH_BITS, &selected.depth);
    if (ogl_checkerror() || selected.red <= 0 || selected.green <= 0 ||
        selected.blue <= 0 || selected.alpha < 0 || selected.depth < 0)
        return FALSE;
    selected.buffer = selected.red + selected.green + selected.blue + selected.alpha;
#else
    ogl_window = ogl_select_window(&selected, in_x, in_y, in_width, in_height);
    if (ogl_window == 0) error("No usable OpenGL RGB(A) or 8-bit palette window format");
#endif
    ogl_indexed = !selected.rgba;
    ogl_double_buffer = selected.double_buffer;
    ogl_has_depth = selected.depth > 0;
    ogl_palette_size = ogl_indexed ? (unsigned int)selected.palette : 0;
    rgb_bits = (unsigned int)(selected.red + selected.green + selected.blue);
    drv_colormapped = ogl_indexed;
    drv_bits_per_pixel = ogl_indexed ? (unsigned int)selected.buffer : rgb_bits + (unsigned int)selected.alpha;
    drv_colormap_bits = ogl_indexed ? (unsigned int)selected.buffer : rgb_bits;
    drv_colors = drv_colormap_bits >= 32 ? 0xffffffffUL : 1UL << drv_colormap_bits;
    drv_colormap_colors = ogl_indexed ? ogl_palette_size : drv_colors;
    drv_bytes_per_pixel = ((unsigned int)selected.buffer + 7U) / 8U;
#ifndef OGL_AMIGA
    glutReshapeWindow(in_width, in_height);
    glutPositionWindow(in_x, in_y);
    glutSetWindowTitle(TESTGLOBE_NAME);
    glutReshapeFunc(ogl_resize);
    glutMouseFunc(ogl_mouse);
    glutMotionFunc(ogl_mouse_move);
    glutPassiveMotionFunc(ogl_mouse_move);
    glutKeyboardFunc(ogl_keyboard);
#endif

    ogl_render_state();
    ogl_resize(in_width, in_height);
#ifndef OGL_AMIGA
    glutShowWindow();
#endif
    ogl_post_redisplay();

    ogl_checkerror();
    return 1;
}

/* main loop */
void drv_loop(void)
{
#ifdef OGL_AMIGA
    ogl_ami_loop(drv_render, ogl_resize, ogl_keyboard, ogl_mouse, ogl_mouse_move);
#else
    /* main initializes the globe after drv_init_window returns. Window
     * operations may redraw earlier, so only enable rendering here. */
    glutDisplayFunc(drv_render);
    glutPostRedisplay();
    glutMainLoop();
#endif
}

static void ogl_set_index_color(unsigned char color)
{
#ifndef OGL_AMIGA
    if (ogl_indexed) glIndexi((GLint)color);
    else
#endif
        glColor3ub(earth_cmap[color][0], earth_cmap[color][1], earth_cmap[color][2]);
}

#ifdef OGL_AMIGA
static void ogl_triangle_room(void)
{
    if (ogl_triangle_vertices == 252U)
    {
        glEnd();
        glBegin(GL_TRIANGLES);
        ogl_triangle_vertices = 0;
    }
    ogl_triangle_vertices += 3U;
}
#endif

/* Desktop TRI callbacks submit inside ogl_draw_globe's GL_TRIANGLES scope.
 * MiniGL colored triangles own their uniform-color batches; textured poles
 * share bounded batches. Software UV callbacks and each TRISTRIP call are
 * complete submissions. Allocation and recovery stay outside these scopes. */
void drv_draw_tri_flat_color(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3,
                              unsigned char color)
{
#ifdef OGL_AMIGA
    /* MiniGL flat color is uniform over one immediate batch. */
    glBegin(GL_TRIANGLES);
#endif
    ogl_set_index_color(color);
    glVertex3fv((GLfloat *)xyz1);
    glVertex3fv((GLfloat *)xyz2);
    glVertex3fv((GLfloat *)xyz3);
#ifdef OGL_AMIGA
    glEnd();
#endif
}

void drv_draw_tri_flat_rgb(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Color_RGB *color)
{
#ifdef OGL_AMIGA
    glBegin(GL_TRIANGLES);
#endif
    glColor3ub(color->r, color->g, color->b);
    glVertex3fv((GLfloat *)xyz1);
    glVertex3fv((GLfloat *)xyz2);
    glVertex3fv((GLfloat *)xyz3);
#ifdef OGL_AMIGA
    glEnd();
#endif
}

/* draw flat shaded textured triangle */
void drv_draw_tri_flat_uv(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Coord *uv1, Coord *uv2, Coord *uv3)
{
    if (ogl_indexed)
    {
        texsoft_triangle(xyz1, xyz2, xyz3, uv1, uv2, uv3);
        return;
    }
#ifdef OGL_AMIGA
    ogl_triangle_room();
#endif
    glTexCoord2fv((GLfloat *)uv1);
    glVertex3fv((GLfloat *)xyz1);
    glTexCoord2fv((GLfloat *)uv2);
    glVertex3fv((GLfloat *)xyz2);
    glTexCoord2fv((GLfloat *)uv3);
    glVertex3fv((GLfloat *)xyz3);
}

#ifdef TRISTRIP
#ifdef OGL_AMIGA
/* MiniGL's flat renderer uses one uniform color per glBegin/glEnd batch.
 * Keep contiguous equal-color facets together. A duplicated first vertex
 * advances odd-start runs to their original strip winding without drawing
 * an extra triangle. The outer splitter keeps every packet below 256
 * vertices, including this optional degenerate vertex. */
static void ogl_draw_strip_batch(unsigned int count,
                                 const Vertex TG_HUGE *vertices,
                                 const unsigned char TG_HUGE *facet_colors)
{
    unsigned int first, end, i;
    first = 0;
    while (first < count - 2U)
    {
        end = first + 1U;
        while (end < count - 2U && facet_colors[end] == facet_colors[first]) ++end;
        glBegin(GL_TRIANGLE_STRIP);
        ogl_set_index_color(facet_colors[first]);
        if (first & 1U) glVertex3fv((GLfloat *)(vertices + first));
        for (i = first; i < end + 2U; ++i)
            glVertex3fv((GLfloat *)(vertices + i));
        glEnd();
        first = end;
    }
}
#endif

/* In OpenGL flat shading, a strip's newly submitted vertex supplies that
 * triangle's color. The first two vertices do not complete a triangle. */
void drv_draw_tristrip_flat_color(unsigned int count,
                                const Vertex TG_HUGE *vertices,
                                const unsigned char TG_HUGE *facet_colors)
{
#ifndef OGL_AMIGA
    unsigned int i;
#endif
    if (count < 3U || vertices == NULL || facet_colors == NULL) return;
#ifdef OGL_AMIGA
    drv_split_tristrip_batches(count, vertices, facet_colors, 252U, ogl_draw_strip_batch);
#else
    glBegin(GL_TRIANGLE_STRIP);
    ogl_set_index_color(facet_colors[0]);
    for (i = 0; i < count; ++i)
    {
        if (i >= 2U) ogl_set_index_color(facet_colors[i - 2U]);
        glVertex3fv((const GLfloat *)(vertices + i));
    }
    glEnd();
#endif
}

void drv_draw_tristrip_flat_uv(unsigned int count,
                             const Vertex TG_HUGE *vertices,
                             const Coord TG_HUGE *coords)
{
    unsigned int i;
#ifdef OGL_AMIGA
    unsigned int first, facets;
#endif
    if (count < 3U || vertices == NULL || coords == NULL) return;
    if (ogl_indexed)
    {
        drv_expand_tristrip_uv(count, vertices, coords, texsoft_triangle);
        return;
    }
#ifdef OGL_AMIGA
    /* Two shared vertices preserve the boundary. An even number of facets
     * per packet keeps every following packet's original strip winding. */
    first = 0;
    while (first < count - 2U)
    {
        facets = count - 2U - first;
        if (facets > 254U) facets = 254U;
        glBegin(GL_TRIANGLE_STRIP);
        for (i = first; i < first + facets + 2U; ++i)
        {
            glTexCoord2fv((GLfloat *)(coords + i));
            glVertex3fv((GLfloat *)(vertices + i));
        }
        glEnd();
        first += facets;
    }
#else
    glBegin(GL_TRIANGLE_STRIP);
    for (i = 0; i < count; ++i)
    {
        glTexCoord2fv((const GLfloat *)(coords + i));
        glVertex3fv((const GLfloat *)(vertices + i));
    }
    glEnd();
#endif
}
#endif /* TRISTRIP */

#ifndef OGL_AMIGA
/* Native bitmap widths preserve proportional spacing. Measure and draw the
 * same word wrapping; no glyph may extend outside the drawable viewport. */
static int ogl_osd_text(const char *text, int baseline, int draw)
{
    int count, space, width, next, lines;
    width = (int)drv_win_width - 20;
    lines = 0;
    while (*text != 0 && width > 0)
    {
        count = space = next = 0;
        while (text[count] != 0)
        {
            next += glutBitmapWidth(ogl_font, (unsigned char)text[count]);
            if (next > width) break;
            if (text[count] == ' ') space = count;
            ++count;
        }
        if (text[count] != 0 && space > 0) count = space;
        if (count == 0) break;
        if (draw && baseline - ogl_font_descent >= 4 &&
            baseline + ogl_font_ascent <= (int)drv_win_height - 4)
        {
            int i;
            glRasterPos2i(10, baseline);
            for (i = 0; i < count; ++i)
                glutBitmapCharacter(ogl_font, (unsigned char)text[i]);
        }
        ++lines;
        baseline -= ogl_font_step;
        text += count;
        while (*text == ' ') ++text;
    }
    return lines;
}
#endif

/* draw the OSD */
void drv_draw_osd(void)
{
    char status_str[256];
    char dimensions[24];

#ifndef OGL_AMIGA
    int status_lines, help_lines, top, bottom;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, drv_win_width, 0, drv_win_height);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (ogl_indexed) glIndexi((GLint)ogl_foreground);
    else glColor3ub(255, 255, 255);
#endif
    
    drv_format_stats(status_str);
    drv_append_status(status_str, sizeof(status_str),
             (!ogl_dlist_enabled || drv_cull_enabled) ? ", dlist off" :
             (ogl_dlist_used ? ", dlist on" : ", dlist fail"));
    drv_append_status(status_str, sizeof(status_str), !drv_texture_enabled ? ", texture off" :
             (ogl_texture_used ? (ogl_indexed ? ", texture soft" : ", texture on") :
                                  ", texture fail"));
    if (ogl_texture_used && texture_size != TEXTURE_DEFAULT_SIZE)
    {
        sprintf(dimensions, " (%u)", texture_size);
        drv_append_status(status_str, sizeof(status_str), dimensions);
    }
#ifdef OGL_AMIGA
    ogl_ami_draw_osd(status_str, drv_help);
#else
    top = (int)drv_win_height - 4 - ogl_font_ascent;
    status_lines = ogl_osd_text(status_str, top, TRUE);
    help_lines = ogl_osd_text(drv_help, 0, FALSE);
    bottom = 4 + ogl_font_descent + (help_lines - 1) * ogl_font_step;
    if (help_lines > 0 && bottom + ogl_font_ascent + 2 <
        top - (status_lines - 1) * ogl_font_step - ogl_font_descent)
        (void)ogl_osd_text(drv_help, bottom, TRUE);
    glPopAttrib();
#endif
}

#endif
