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

/* Platform adapters: MiniGL with native Amiga windows, or GLUT. */
#ifdef OGL_AMIGA
#include "ogl_ami.h"
#define OGL_LEFT_BUTTON OGL_AMI_LEFT_BUTTON
#define OGL_RIGHT_BUTTON OGL_AMI_RIGHT_BUTTON
#define OGL_DOWN OGL_AMI_DOWN
#define OGL_UP OGL_AMI_UP
#define ogl_post_redisplay ogl_ami_post_redisplay
/* A MiniGL packet holds OGL_AMI_VERTEX_LIMIT vertices. Textured strips
 * spend two on their shared edge; flat runs may also add one degenerate
 * vertex. Strip packets carry an even number of facets, which keeps the
 * following packet's winding. Triangle batches keep the flat-run margin. */
#define OGL_AMI_UV_FACETS ((OGL_AMI_VERTEX_LIMIT - 2U) & ~1U)
#define OGL_AMI_FLAT_FACETS ((OGL_AMI_VERTEX_LIMIT - 3U) & ~1U)
#define OGL_AMI_TRIANGLE_VERTICES ((OGL_AMI_VERTEX_LIMIT - 3U) / 3U * 3U)
typedef struct Ogl_format
{
    int rgba, red, green, blue, alpha, buffer, depth, double_buffer, palette;
} Ogl_format;
#else
#include "ogl_glut.h"
#define OGL_LEFT_BUTTON OGL_GLUT_LEFT_BUTTON
#define OGL_RIGHT_BUTTON OGL_GLUT_RIGHT_BUTTON
#define OGL_DOWN OGL_GLUT_DOWN
#define OGL_UP OGL_GLUT_UP
#define ogl_post_redisplay ogl_glut_post_redisplay
typedef Ogl_glut_format Ogl_format;
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
#ifdef OGL_AMIGA
/* MiniGL's immediate buffer is bounded, including while recording lists. */
static unsigned int ogl_triangle_vertices;
#else
static unsigned int ogl_foreground, ogl_background;
/* Flat shading takes each triangle's color from its last vertex. The
 * current color persists through a batch, also when a list replays it, so
 * only a changed facet color is sent. -1 makes a batch send its first. */
static int ogl_batch_color = -1;
#endif

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
#else
    if (ogl_window > 0 && ogl_glut_ready())
#endif
    {
        if (dlist != 0) glDeleteLists(dlist, 1);
        if (texture_id != 0) glDeleteTextures(1, &texture_id);
    }
#ifdef OGL_AMIGA
    ogl_ami_cleanup();
#else
    ogl_glut_cleanup(ogl_window);
#endif
    ogl_window = 0; dlist = 0; texture_id = 0;
    ogl_dlist_used = FALSE;
    ogl_texture_failed = ogl_texture_used = FALSE;
    drv_texture_loaded = drv_texture_enabled = FALSE;
    drv_lmouse_pressed = drv_rmouse_pressed = FALSE;
    texsoft_free();
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
    if (state != OGL_DOWN && state != OGL_UP) return;
    pressed = state == OGL_DOWN;
    if (button == OGL_RIGHT_BUTTON)
    {
        drv_rmouse_pressed = pressed;
        ogl_post_redisplay();
    }
    else if (button == OGL_LEFT_BUTTON && drv_lmouse_pressed != pressed)
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

#ifndef OGL_AMIGA
/* Build glyph lists between frames, so their errors are never mistaken
 * for the globe's. Any error leaves that font drawn directly. */
static void ogl_prepare_font(void)
{
    int built;
    if (!ogl_glut_font_pending()) return;
    ogl_checkerror();
    built = ogl_glut_build_font();
    if (ogl_checkerror() || !built) ogl_glut_discard_font();
}
#endif

/* initialize graphics subsystem */
void drv_init(int *argc, char **argv)
{
#ifdef OGL_AMIGA
    if (atexit(ogl_cleanup) != 0) error("Cannot register OpenGL cleanup");
    ogl_ami_init(argc, argv);
#else
    ogl_glut_init(argc, argv);
    if (atexit(ogl_cleanup) != 0) error("Cannot register OpenGL cleanup");
    ogl_glut_select_font((int)drv_win_width, (int)drv_win_height);
#endif
}

static void ogl_clear(void)
{
    glClear(GL_COLOR_BUFFER_BIT | (ogl_has_depth ? GL_DEPTH_BUFFER_BIT : 0));
}

/* One batch per call; MiniGL flat triangles open their own uniform-color
 * batches and software-textured triangles are rasterized on the CPU. */
static void ogl_draw_triangles(void (*traverse)(void))
{
    int batch;
#ifdef OGL_AMIGA
    batch = drv_texture_active;
#else
    batch = !(ogl_indexed && drv_texture_active);
    ogl_batch_color = -1;
#endif
    if (batch) glBegin(GL_TRIANGLES);
    traverse();
    if (batch) glEnd();
}

/* Keep the pole triangles in one batch; each latitude band opens its own
 * native strip. Indexed textures expand the same geometry in software. */
static void ogl_draw_globe(void)
{
#ifdef OGL_AMIGA
    ogl_triangle_vertices = 0;
#endif
#ifdef TRI
    ogl_draw_triangles(globe_draw_tris);
#endif
#ifdef TRISTRIP
#ifdef DRV_TRI_CAPS
    ogl_draw_triangles(globe_draw_pole_tris);
#endif
    globe_draw_tristrips();
#endif
}

static void ogl_list_failed(void)
{
    if (dlist != 0) glDeleteLists(dlist, 1);
    dlist = 0;
    ogl_dlist_used = FALSE;
    ogl_dlist_blocked = TRUE;
    if (!drv_frame_failed)
        drv_notice("Display list unavailable; drawing directly");
}

/* OpenGL 1.1 has no separate display-list support query. Compile and
 * validate the native object once; each frame's error check covers its use. */
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
    if (dlist == 0)
    {
        /* Drain older errors so only compilation is checked. Compile before
         * executing so a rejected or incomplete list does not draw part of
         * the globe before the immediate fallback. */
        ogl_checkerror();
        dlist = glGenLists(1);
        failed = ogl_checkerror() || dlist == 0;
        if (!failed)
        {
            glNewList(dlist, GL_COMPILE);
            failed = ogl_checkerror();
            if (!failed)
            {
                ogl_draw_globe();
                glEndList();
                failed = ogl_checkerror() || drv_frame_failed;
            }
        }
        if (!failed)
        {
            valid = glIsList(dlist);
            failed = ogl_checkerror() || !valid;
        }
        if (failed)
        {
            ogl_list_failed();
            return FALSE;
        }
        ogl_dlist_revision = globe_revision;
        ogl_dlist_textured = drv_texture_active;
    }
    glCallList(dlist);
    return TRUE;
}

/* Texture object and enable/binding queries are available in OpenGL 1.1.
 * Desktop OpenGL checks them once after enabling; MiniGL, which is local,
 * reasserts and checks its binding every frame. */
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

/* Flat colors, or the native texture when it is ready. */
static void ogl_draw_native(int textured)
{
    /* Keep the public flag as the user's request. Only the traversal sees
     * texturing disabled, so T still toggles normally. */
    drv_texture_active = textured;
    if (!textured) glDisable(GL_TEXTURE_2D);
#ifdef OGL_AMIGA
    else glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
#endif
    /* Whole-globe lists cannot select view-dependent longitude runs.
     * Preserve the requested D setting and cached full mesh while C
     * temporarily forces direct, pre-culled submissions. */
    if (ogl_dlist_enabled && !drv_cull_enabled)
        ogl_dlist_used = ogl_draw_dlist();
    if (!ogl_dlist_used && !drv_frame_failed) ogl_draw_globe();
    drv_texture_active = FALSE;
    ogl_texture_used = textured;
}

#ifndef OGL_AMIGA
/* A color-index OpenGL framebuffer cannot perform RGB texture mapping.
 * Keep its texture toggle functional using the shared perspective-correct
 * rasterizer, then transfer tightly packed palette bytes through OpenGL.
 * The raster covers the whole viewport, so it needs no color clear, and
 * nothing in this frame tests or writes depth. */
static int ogl_draw_indexed_texture(void)
{
    float model[4][4], projection[4][4];
    const unsigned char *pixels;
    int cull;
    /* Column-major OpenGL matrix memory is the equivalent row-vector
     * array consumed by texsoft; no further transpose is needed. */
    glGetFloatv(GL_MODELVIEW_MATRIX, &model[0][0]);
    glGetFloatv(GL_PROJECTION_MATRIX, &projection[0][0]);
    if (!texsoft_begin(drv_win_width, drv_win_height, model, projection,
                         TEXSOFT_INDEX_BYTES, &earth_cmap[0][0]))
    {
        ogl_texture_failure("Cannot prepare indexed OpenGL texture raster");
        return FALSE;
    }
    /* texsoft discards back faces after transforming them. Skipping them in
     * the CPU traversal first leaves the same pixels for less work. Restore
     * the user's C setting before the OSD reports it. */
    cull = drv_cull_enabled;
    drv_cull_enabled = TRUE;
    drv_texture_active = TRUE;
    ogl_draw_globe();
    drv_texture_active = FALSE;
    drv_cull_enabled = cull;
    if (drv_frame_failed) return FALSE;
    pixels = texsoft_index_pixels();
    if (pixels == NULL)
    {
        ogl_texture_failure("Cannot present indexed OpenGL texture raster");
        return FALSE;
    }
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
    /* drv_render leaves the modelview matrix selected. */
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, drv_win_width, 0, drv_win_height);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glRasterPos2i(0, 0);
    glDrawPixels((GLsizei)drv_win_width, (GLsizei)drv_win_height,
                  GL_COLOR_INDEX, GL_UNSIGNED_BYTE, pixels);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopClientAttrib(); glPopAttrib();
    ogl_texture_used = TRUE;
    return TRUE;
}
#endif

/* Draw one complete attempt at the frame, including the desktop OSD. */
static void ogl_draw_frame(int ready)
{
    ogl_dlist_used = ogl_texture_used = FALSE;
#ifndef OGL_AMIGA
    if (!(ogl_indexed && ready && ogl_draw_indexed_texture()))
#endif
    {
        ogl_clear();
        if (globe_ready() && !drv_frame_failed)
        {
            ogl_draw_native(ready && !ogl_indexed && !ogl_texture_failed);
            if (drv_frame_failed)
            {
                /* Do not present part of the globe. */
                ogl_dlist_used = ogl_texture_used = FALSE;
                ogl_clear();
            }
        }
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
#endif
}

/* render current scene */
void drv_render(void)
{
    int requested, ready, pass;
#ifdef OGL_AMIGA
    if (!ogl_ami_ready()) return;
#endif
    requested = drv_texture_enabled;
    drv_fps_start();
    if (!globe_ready()) drv_frame_failed = TRUE;
    ready = globe_ready() && requested && drv_texture_loaded && !ogl_texture_failed;
    if (globe_ready() && requested && !ogl_texture_failed && !drv_texture_loaded)
        ogl_texture_failure("OpenGL texture data is not loaded");
#ifdef OGL_AMIGA
    if (ready && !ogl_texture_native_ready())
    {
        ogl_texture_failure("OpenGL texture is not active");
        ready = FALSE;
    }
#endif
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0,0, -GLOBE_VIEW_DISTANCE);
    glRotatef(drv_rot_x, 1, 0, 0);
    glRotatef(drv_rot_y, 0, 1, 0);
    glRotatef(-90, 1, 0, 0);
    glRotatef(-90, 0, 0, 1);

    /* Each synchronous query is a round trip on indirect GLX, so a normal
     * frame makes one error check, just before presentation. An error
     * redraws the frame without the list, then without the texture. Errors
     * neither fallback can avoid are reported once and the frame is kept. */
    for (pass = 0; pass < 3; ++pass)
    {
        ogl_draw_frame(ready);
        if (!ogl_checkerror() || drv_frame_failed) break;
        if (ogl_dlist_used) ogl_list_failed();
        else if (ogl_texture_used)
        {
            ogl_texture_failure(ogl_indexed ?
                "Cannot present indexed OpenGL texture raster" :
                "Cannot draw OpenGL texture");
            ready = FALSE;
        }
        else break;
    }

#ifndef OGL_AMIGA
    ogl_glut_present(ogl_double_buffer);
#else
    ogl_ami_present();
    /* MiniGL queries are local calls. Drain presentation errors here, so a
     * later frame's texture check cannot take them for its own. */
    ogl_checkerror();
    /* The native font writes to the presented Workbench window, outside
     * MiniGL's framebuffer. Its colors and state do not modify GL state. */
    if (drv_status_required()) drv_draw_osd();
#endif
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
    ogl_glut_select_font(width, height);
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
#else
    ogl_prepare_font();
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

/* The size search passes only a width; keep the failure reason here. */
static const char *ogl_texture_reason;

static Drv_result ogl_texture_try(unsigned int size)
{
    return ogl_texture_attempt(size, &ogl_texture_reason);
}

/* Try each supported size, with at most one optional-resource eviction. */
static int ogl_texture_load(void)
{
    Drv_result result;
    unsigned int size;
    GLint maximum;
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
    ogl_texture_reason = NULL;
    result = drv_texture_fit(size, ogl_texture_try);
    if (result == DRV_OK)
    {
        drv_texture_loaded = TRUE;
        return TRUE;
    }
    if (result != DRV_NO_MEMORY)
    {
        ogl_texture_failure(ogl_texture_reason != NULL ? ogl_texture_reason :
                            "Cannot load OpenGL texture");
        return FALSE;
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
#ifndef OGL_AMIGA
        /* This driver makes every later binding and enable change, so the
         * object, binding and enable are checked here, not every frame. */
        else if (!ogl_texture_native_ready())
            ogl_texture_failure("OpenGL texture is not active");
#endif
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
        ogl_glut_install_palette(&ogl_background, &ogl_foreground);
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
    ogl_window = ogl_glut_create_window(&selected, in_x, in_y, in_width, in_height);
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
    ogl_glut_callbacks(ogl_resize, ogl_keyboard, ogl_mouse, ogl_mouse_move);
#endif

    ogl_render_state();
    ogl_resize(in_width, in_height);
#ifndef OGL_AMIGA
    ogl_glut_show();
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
    ogl_glut_loop(drv_render);
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
    if (ogl_triangle_vertices == OGL_AMI_TRIANGLE_VERTICES)
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
    ogl_set_index_color(color);
#else
    if ((int)color != ogl_batch_color)
    {
        ogl_set_index_color(color);
        ogl_batch_color = color;
    }
#endif
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
#else
    ogl_batch_color = -1;
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
 * an extra triangle. The outer splitter keeps every packet within
 * OGL_AMI_VERTEX_LIMIT vertices, including this optional degenerate vertex. */
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
 * triangle's color. The first two vertices do not complete a triangle.
 * Each strip starts with its first facet color, then sends only changes. */
void drv_draw_tristrip_flat_color(unsigned int count,
                                const Vertex TG_HUGE *vertices,
                                const unsigned char TG_HUGE *facet_colors)
{
#ifndef OGL_AMIGA
    unsigned int i;
    unsigned char color;
#endif
    if (count < 3U || vertices == NULL || facet_colors == NULL) return;
#ifdef OGL_AMIGA
    drv_split_tristrip_batches(count, vertices, facet_colors,
                               OGL_AMI_FLAT_FACETS, ogl_draw_strip_batch);
#else
    glBegin(GL_TRIANGLE_STRIP);
    color = facet_colors[0];
    ogl_set_index_color(color);
    for (i = 0; i < count; ++i)
    {
        if (i >= 2U && facet_colors[i - 2U] != color)
        {
            color = facet_colors[i - 2U];
            ogl_set_index_color(color);
        }
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
        if (facets > OGL_AMI_UV_FACETS) facets = OGL_AMI_UV_FACETS;
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

/* draw the OSD */
void drv_draw_osd(void)
{
    char status_str[256];
    char dimensions[24];

#ifndef OGL_AMIGA
    /* GL_LIST_BIT keeps the glyph-list base private to the OSD. */
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LIST_BIT);
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
    ogl_glut_draw_osd(status_str, drv_help);
    glPopAttrib();
#endif
}

#endif
