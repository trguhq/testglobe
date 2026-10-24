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

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#if !defined(NO_GENMIPMAP) && defined(FREEGLUT) && defined(FREEGLUT_VERSION_2_0)
#include <GL/freeglut_ext.h>
#ifdef APIENTRY
typedef void (APIENTRY *Ogl_generate_mipmap)(GLenum target);
#else
typedef void (*Ogl_generate_mipmap)(GLenum target);
#endif
#endif

/* OpenGL font pointer */
static void *ogl_font;

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
static unsigned int ogl_foreground, ogl_background;
static GLubyte *ogl_index_pixels;
static unsigned long ogl_index_capacity;

typedef struct Ogl_format
{
    int rgba, red, green, blue, alpha, buffer, depth, double_buffer, palette;
    int alpha_known;
} Ogl_format;

static void ogl_texture_failure(const char *message);

static void ogl_invalidate_dlist(void)
{
    if (dlist != 0) glDeleteLists(dlist, 1);
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
    free(ogl_index_pixels);
    ogl_index_pixels = NULL;
    ogl_index_capacity = 0;
    if (!drv_texture_enabled)
    {
        if (texture_id != 0) glDeleteTextures(1, &texture_id);
        texture_id = 0;
        texsoft_free();
        texture_free();
        drv_texture_loaded = FALSE;
    }
}

static void ogl_cleanup(void)
{
    if (ogl_window > 0 && glutGetWindow() != 0)
    {
        if (dlist != 0) glDeleteLists((GLuint)dlist, 1);
        if (texture_id != 0) glDeleteTextures(1, &texture_id);
        glutDestroyWindow(ogl_window);
    }
    ogl_window = 0; dlist = 0; texture_id = 0;
    ogl_dlist_used = FALSE;
    ogl_texture_failed = ogl_texture_used = FALSE;
    drv_texture_loaded = drv_texture_enabled = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
    free(ogl_index_pixels); ogl_index_pixels = NULL; ogl_index_capacity = 0;
    texsoft_free();
}

/* Apple GLUT can service pending redraws during window management. Every
 * window needs a display callback, even a short-lived format probe. Keep
 * drawing disabled until the application window and globe are ready. */
static void ogl_pending_display(void)
{
}

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

/* GLUT exposes requests and selected-window queries, rather than a portable
 * enumeration of every native pixel format. Probe supported request classes
 * and rank their actual results, giving color precision priority over buffers. */
static int ogl_select_window(Ogl_format *selected, int x, int y, int width, int height)
{
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
#ifdef GLUT_DISPLAY_MODE_POSSIBLE
                if (!glutGet(GLUT_DISPLAY_MODE_POSSIBLE)) continue;
#else
                error("GLUT display-mode availability queries are required");
                return 0;
#endif
                window = glutCreateWindow("testglobe pixel-format probe");
                if (window <= 0) continue;
                glutDisplayFunc(ogl_pending_display);
                glutHideWindow();
                memset(&current, 0, sizeof(current));
                current.rgba = glutGet(GLUT_WINDOW_RGBA) > 0;
                current.buffer = glutGet(GLUT_WINDOW_BUFFER_SIZE);
                current.depth = glutGet(GLUT_WINDOW_DEPTH_SIZE);
                current.double_buffer = glutGet(GLUT_WINDOW_DOUBLEBUFFER) > 0;
                if (current.depth < 0) current.depth = 0;
                if (current.rgba)
                {
                    current.red = glutGet(GLUT_WINDOW_RED_SIZE);
                    current.green = glutGet(GLUT_WINDOW_GREEN_SIZE);
                    current.blue = glutGet(GLUT_WINDOW_BLUE_SIZE);
                    current.alpha = glutGet(GLUT_WINDOW_ALPHA_SIZE);
                    current.alpha_known = current.alpha >= 0;
                    if (current.alpha < 0) current.alpha = 0;
                    if (current.red <= 0 || current.green <= 0 || current.blue <= 0)
                    {
                        glutDestroyWindow(window);
                        continue;
                    }
                    if (current.buffer <= 0)
                        current.buffer = current.red + current.green + current.blue + current.alpha;
                }
                else
                {
                    current.palette = glutGet(GLUT_WINDOW_COLORMAP_SIZE);
                    if (current.buffer < 8 || current.palette < 256)
                    {
                        glutDestroyWindow(window);
                        continue;
                    }
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
    glutPostRedisplay();
}

/* callback for mouse movement */
static void ogl_mouse_move(int x, int y)
{
    if (drv_lmouse_pressed)
    {
        drv_rotate_drag((long)x - ogl_old_x, (long)y - ogl_old_y);
        ogl_old_x = x;
        ogl_old_y = y;
        glutPostRedisplay();
    }
}

/* callback for mouse click */
static void ogl_mouse(int button, int state, int x, int y)
{
    int pressed;
    if (state != GLUT_DOWN && state != GLUT_UP) return;
    pressed = state == GLUT_DOWN;
    if (button == GLUT_RIGHT_BUTTON)
    {
        drv_rmouse_pressed = pressed;
        glutPostRedisplay();
    }
    else if (button == GLUT_LEFT_BUTTON && drv_lmouse_pressed != pressed)
    {
        drv_lmouse_pressed = pressed;
        ogl_old_x = x;
        ogl_old_y = y;
    }
}

/* Drain reported errors before committing native resource state. */
static int ogl_checkerror(void)
{
    const char *error_msg;
    GLenum code;
    unsigned int i;
    int failed;
    failed = FALSE;
    /* A lost context must not trap the application in an endless error loop. */
    for (i = 0; i < 16U; ++i)
    {
        code = glGetError();
        switch(code)
        {
        case GL_NO_ERROR:
            return failed;
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
        failed = TRUE;
        ogl_message(code, error_msg);
    }
    return failed;
}

/* initialize graphics subsystem */
void drv_init(int *argc, char **argv)
{
    glutInit(argc, argv);
    if (atexit(ogl_cleanup) != 0) error("Cannot register OpenGL cleanup");
    ogl_font = GLUT_BITMAP_HELVETICA_12;
}

/* A color-index OpenGL framebuffer cannot perform RGB texture mapping.
 * Keep its texture toggle functional using the shared perspective-correct
 * rasterizer, then transfer tightly packed palette bytes through OpenGL. */
static int ogl_present_indexed_texture(void)
{
    unsigned long count, i;
    unsigned long *source;
    GLubyte *pixels;
    GLint matrix_mode;
    count = (unsigned long)drv_win_width * drv_win_height;
    if (count > (size_t)-1) return FALSE;
    if (count > ogl_index_capacity)
    {
        pixels = (GLubyte *)malloc((size_t)count);
        if (pixels == NULL) return FALSE;
        free(ogl_index_pixels); ogl_index_pixels = pixels; ogl_index_capacity = count;
    }
    source = texsoft_pixels();
    if (source == NULL) return FALSE;
    for (i = 0; i < count; ++i) ogl_index_pixels[i] = (GLubyte)source[i];
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
                  GL_COLOR_INDEX, GL_UNSIGNED_BYTE, ogl_index_pixels);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode((GLenum)matrix_mode);
    glPopClientAttrib(); glPopAttrib();
    return !ogl_checkerror();
}

/* Keep the pole triangles in one batch; each latitude band opens its own
 * native strip. Indexed textures expand the same geometry in software. */
static void ogl_draw_globe(void)
{
#ifdef TRI
    if (ogl_indexed && drv_texture_active)
        globe_draw_tris();
    else
    {
        glBegin(GL_TRIANGLES);
        globe_draw_tris();
        glEnd();
    }
#endif
#ifdef TRISTRIP
#ifdef DRV_TRI_CAPS
    if (ogl_indexed && drv_texture_active)
        globe_draw_pole_tris();
    else
    {
        glBegin(GL_TRIANGLES);
        globe_draw_pole_tris();
        glEnd();
    }
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
    GLint binding;
    int failed;
    if (texture_id == 0) return FALSE;
    valid = glIsTexture(texture_id);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    enabled = glIsEnabled(GL_TEXTURE_2D);
    failed = ogl_checkerror();
    return !failed && valid && enabled && (GLuint)binding == texture_id;
}

/* render current scene */
void drv_render(void)
{
    float model[4][4], projection[4][4];
    int requested, ready, drawn;
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
    if (ogl_indexed && ready)
    {
        /* Column-major OpenGL matrix memory is the equivalent row-vector
         * array consumed by texsoft; no further transpose is needed. */
        glGetFloatv(GL_MODELVIEW_MATRIX, &model[0][0]);
        glGetFloatv(GL_PROJECTION_MATRIX, &projection[0][0]);
        if (ogl_checkerror() ||
            !texsoft_begin(drv_win_width, drv_win_height, model, projection,
                             TRUE, &earth_cmap[0][0]))
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
    if (!drawn && globe_ready())
    {
        /* Keep the public flag as the user's request. Only the fallback
         * traversal sees texturing disabled, so T still toggles normally. */
        drv_texture_active = ready;
        if (!ready) glDisable(GL_TEXTURE_2D);
        if (requested && ogl_texture_failed)
            glClear(GL_COLOR_BUFFER_BIT | (ogl_has_depth ? GL_DEPTH_BUFFER_BIT : 0));
        if (ogl_dlist_enabled)
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

    if (drv_osd_enabled || !globe_ready())
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
    ogl_checkerror();
    drv_fps_stop();
    
    if (drv_auto_enabled == TRUE && globe_ready())
    {
        drv_globe_rotate();
        glutPostRedisplay();
    }
}

/* resize window */
static void ogl_resize(int width, int height) {
    if (width < 1 || height < 1) return;
    drv_win_width = width;
    drv_win_height = height;
    drv_reset_timing();
    
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(GLOBE_FIELD_OF_VIEW, (float)width/(float)height,
                     GLOBE_NEAR_DISTANCE, GLOBE_FAR_DISTANCE);
}

/* GLU is the OpenGL 1.1 path. Newer mipmap generation remains optional:
 * no later GL symbol is required by the headers or the linked GL library. */
static Drv_result ogl_build_mipmaps(void)
{
    int result, failed;
#if !defined(NO_GENMIPMAP) && defined(FREEGLUT) && defined(FREEGLUT_VERSION_2_0)
    const GLubyte *version;
    unsigned int major, minor;
    GLint maximum;
    Ogl_generate_mipmap generate;
    maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (ogl_checkerror()) return DRV_NATIVE_FAILED;
    if (maximum > 0 && texture_size <= (unsigned int)maximum &&
        texture_size / 2U <= (unsigned int)maximum)
    {
        version = glGetString(GL_VERSION);
        failed = ogl_checkerror();
        if (!failed && version != NULL &&
            sscanf((const char *)version, "%u.%u", &major, &minor) == 2 &&
            major >= 3U)
        {
            generate = (Ogl_generate_mipmap)glutGetProcAddress("glGenerateMipmap");
            if (generate != NULL)
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_size,
                              texture_size / 2U, 0, GL_RGB,
                              GL_UNSIGNED_BYTE, texture_data);
                if (ogl_checkerror()) return DRV_NATIVE_FAILED;
                generate(GL_TEXTURE_2D);
                return ogl_checkerror() ? DRV_NATIVE_FAILED : DRV_OK;
            }
        }
    }
#endif
    /* Let GLU scale to the implementation's limit before uploading. */
    result = gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, texture_size,
                                texture_size / 2U, GL_RGB,
                                GL_UNSIGNED_BYTE, texture_data);
    failed = ogl_checkerror();
    if (failed) return DRV_NATIVE_FAILED;
    /* A GLU host allocation failure does not imply a damaged GL context.
     * Never apply this retry classification to a native GL error. */
    if (result == GLU_OUT_OF_MEMORY) return DRV_NO_MEMORY;
    return result == 0 ? DRV_OK : DRV_NATIVE_FAILED;
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
    free(ogl_index_pixels);
    ogl_index_pixels = NULL;
    ogl_index_capacity = 0;
    fprintf(stderr, "OpenGL texture: %s\n", message);
    drv_reset_timing();
}

/* load texture */
static int ogl_texture_load(void)
{
    Drv_result result;
    unsigned int size;
    unsigned char *rgb;
    GLint maximum;
    int released;
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
        /* This query is OpenGL 1.1 compatible. Select an existing source
         * map before allocating it; GLU can still scale the smallest map. */
        while (maximum > 0 && size > (unsigned int)maximum && size > 32U)
            size /= 2U;
    }
    for (; size >= 32U; size /= 2U)
    {
retry_image:
        result = texture_build((int)size);
        if (result == DRV_NO_MEMORY && !released)
        {
            drv_release_optional();
            released = TRUE;
            result = texture_build((int)size);
        }
        if (result == DRV_NO_MEMORY) continue;
        if (result != DRV_OK) error("Invalid embedded OpenGL texture data");
        if (ogl_indexed)
        {
            rgb = texture_take_data();
            result = texsoft_texture_take(size, size / 2U, rgb);
            if (result == DRV_NO_MEMORY)
            {
                if (!released)
                {
                    drv_release_optional();
                    released = TRUE;
                    goto retry_image;
                }
                continue;
            }
            if (result != DRV_OK) error("Invalid OpenGL software texture image");
        }
        else
        {
            ogl_checkerror();
            glGenTextures(1, &texture_id);
            glBindTexture(GL_TEXTURE_2D, texture_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            if (ogl_checkerror() || texture_id == 0)
            {
                ogl_texture_failure("Cannot allocate OpenGL texture");
                return FALSE;
            }
            result = ogl_build_mipmaps();
            if (result == DRV_NO_MEMORY)
            {
                glBindTexture(GL_TEXTURE_2D, 0);
                glDeleteTextures(1, &texture_id);
                texture_id = 0;
                texture_free();
                if (ogl_checkerror())
                {
                    ogl_texture_failure("Cannot release OpenGL texture");
                    return FALSE;
                }
                if (!released)
                {
                    drv_release_optional();
                    released = TRUE;
                    goto retry_image;
                }
                continue;
            }
            if (result != DRV_OK)
            {
                ogl_texture_failure("Cannot build OpenGL texture mipmaps");
                return FALSE;
            }
            texture_free();
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
    if (!drv_texture_loaded && !ogl_texture_load()) return;
    if (!ogl_indexed)
    {
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
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
    if (!ogl_indexed)
    {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    drv_texture_enabled = FALSE;
#if !TEXTURE_CACHE_RASTER
    texsoft_release_raster();
    free(ogl_index_pixels);
    ogl_index_pixels = NULL;
    ogl_index_capacity = 0;
#endif
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

    ogl_window = ogl_select_window(&selected, in_x, in_y, in_width, in_height);
    if (ogl_window == 0) error("No usable OpenGL RGB(A) or 8-bit palette window format");
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
    glutReshapeWindow(in_width, in_height);
    glutPositionWindow(in_x, in_y);
    glutSetWindowTitle(TESTGLOBE_NAME);
    glutReshapeFunc(ogl_resize);
    glutMouseFunc(ogl_mouse);
    glutMotionFunc(ogl_mouse_move);
    glutPassiveMotionFunc(ogl_mouse_move);
    glutKeyboardFunc(ogl_keyboard);

    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glShadeModel(GL_FLAT);
    if (ogl_indexed)
    {
        ogl_install_palette();
        glClearIndex((GLfloat)ogl_background);
    }
    else glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if (ogl_has_depth) glEnable(GL_DEPTH_TEST);
    ogl_resize(in_width, in_height);
    glutShowWindow();
    glutPostRedisplay();

    ogl_checkerror();
    return 1;
}

/* main loop */
void drv_loop(void)
{
    /* main initializes the globe after drv_init_window returns. Window
     * operations may redraw earlier, so only enable rendering here. */
    glutDisplayFunc(drv_render);
    glutPostRedisplay();
    glutMainLoop();
}

static void ogl_set_index_color(unsigned char color)
{
    if (ogl_indexed) glIndexi((GLint)color);
    else glColor3ub(earth_cmap[color][0], earth_cmap[color][1], earth_cmap[color][2]);
}

/* Native TRI callbacks submit inside ogl_draw_globe's GL_TRIANGLES scope.
 * Software UV callbacks and each TRISTRIP call are complete submissions.
 * Resource allocation, validation and recovery stay outside these scopes. */
void drv_draw_tri_flat_color(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3,
                              unsigned char color)
{
    ogl_set_index_color(color);
    glVertex3fv((GLfloat *)xyz1);
    glVertex3fv((GLfloat *)xyz2);
    glVertex3fv((GLfloat *)xyz3);
}

void drv_draw_tri_flat_rgb(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Color_RGB *color)
{
    glColor3ub(color->r, color->g, color->b);
    glVertex3fv((GLfloat *)xyz1);
    glVertex3fv((GLfloat *)xyz2);
    glVertex3fv((GLfloat *)xyz3);
}

/* draw flat shaded textured triangle */
void drv_draw_tri_flat_uv(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Coord *uv1, Coord *uv2, Coord *uv3)
{
    if (ogl_indexed)
    {
        texsoft_triangle(xyz1, xyz2, xyz3, uv1, uv2, uv3);
        return;
    }
    glTexCoord2fv((GLfloat *)uv1);
    glVertex3fv((GLfloat *)xyz1);
    glTexCoord2fv((GLfloat *)uv2);
    glVertex3fv((GLfloat *)xyz2);
    glTexCoord2fv((GLfloat *)uv3);
    glVertex3fv((GLfloat *)xyz3);
}

#ifdef TRISTRIP
/* In OpenGL flat shading, a strip's newly submitted vertex supplies that
 * triangle's color. The first two vertices do not complete a triangle. */
void drv_draw_tristrip_flat_color(unsigned int count,
                                const Vertex TG_HUGE *vertices,
                                const unsigned char TG_HUGE *facet_colors)
{
    unsigned int i;
    if (count < 3U || vertices == NULL || facet_colors == NULL) return;
    glBegin(GL_TRIANGLE_STRIP);
    ogl_set_index_color(facet_colors[0]);
    for (i = 0; i < count; ++i)
    {
        if (i >= 2U) ogl_set_index_color(facet_colors[i - 2U]);
        glVertex3fv((const GLfloat *)(vertices + i));
    }
    glEnd();
}

void drv_draw_tristrip_flat_uv(unsigned int count,
                             const Vertex TG_HUGE *vertices,
                             const Coord TG_HUGE *coords)
{
    unsigned int i;
    if (count < 3U || vertices == NULL || coords == NULL) return;
    if (ogl_indexed)
    {
        drv_expand_tristrip_uv(count, vertices, coords, texsoft_triangle);
        return;
    }
    glBegin(GL_TRIANGLE_STRIP);
    for (i = 0; i < count; ++i)
    {
        glTexCoord2fv((const GLfloat *)(coords + i));
        glVertex3fv((const GLfloat *)(vertices + i));
    }
    glEnd();
}
#endif /* TRISTRIP */

/* Private bitmap text helper stays with the OSD drawing code. */
static void ogl_draw_string(const char *str, int x, int y)
{
    glRasterPos2i(x, y);
    while (*str != 0)
        glutBitmapCharacter(ogl_font, *str++);
}

/* draw the OSD */
void drv_draw_osd(void)
{
    char status_str[256];
    char dimensions[24];

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
    
    ogl_draw_string(drv_help, 10, 10);
    drv_format_stats(status_str);
    drv_append_status(status_str, sizeof(status_str), !ogl_dlist_enabled ? ", dlist off" :
             (ogl_dlist_used ? ", dlist on" : ", dlist fail"));
    drv_append_status(status_str, sizeof(status_str), !drv_texture_enabled ? ", texture off" :
             (ogl_texture_used ? (ogl_indexed ? ", texture soft" : ", texture on") :
                                  ", texture fail"));
    if (ogl_texture_used && texture_size != TEXTURE_DEFAULT_SIZE)
    {
        sprintf(dimensions, " (%u)", texture_size);
        drv_append_status(status_str, sizeof(status_str), dimensions);
    }
    ogl_draw_string(status_str, 10, drv_win_height - 18);
    
    glPopAttrib();
}

#endif
