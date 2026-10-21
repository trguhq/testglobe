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

/* OpenGL font pointer */
void *ogl_font;

/* previous mouse pointer location */
static int ogl_old_x;
static int ogl_old_y;

/* display list for currently selected sphere tesselation. Lazily constructed
 * on first use.
 */
static int dlist;

static GLuint texture_id;
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

static void ogl_cleanup(void)
{
    if (ogl_window > 0 && glutGetWindow() != 0)
    {
        if (dlist != 0) glDeleteLists((GLuint)dlist, 1);
        if (texture_id != 0) glDeleteTextures(1, &texture_id);
        glutDestroyWindow(ogl_window);
    }
    ogl_window = 0; dlist = 0; texture_id = 0;
    free(ogl_index_pixels); ogl_index_pixels = NULL; ogl_index_capacity = 0;
    texsoft_free(); texture_free(); globe_free();
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
        "rgba red>=8 green>=8 blue>=8",
        "rgba red>=5 green>=6 blue>=5",
        "rgba red>=5 green>=5 blue>=5",
        "rgba", "index buffer>=8"
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
    return glutCreateWindow(TESTGLOBE_NAME);
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

/* error handling */
void ogl_message(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam)
{
    (void)source; (void)id; (void)length; (void)userParam;
    fprintf(stderr, "GL CALLBACK: type = 0x%x, severity = 0x%x, message = %s\n", type, severity, message );
}

/* keyboard input */
void ogl_keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    switch(key)
    {
        case 27:
            drv_close();
            return;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
            /* in this case the routines in globe.c understand the same keys we have for OpenGL */
            globe_toggle_res(key);
            if(dlist)
            {
                glDeleteLists(dlist, 1);
                dlist = 0;
            }
            glutPostRedisplay();
            break;
        case 'a':
        case 'A':
            drv_auto_enabled = !drv_auto_enabled;
            glutPostRedisplay();
            break;
        case 't':
        case 'T':
            globe_toggle_tex();
            if(dlist)
            {
                glDeleteLists(dlist, 1);
                dlist = 0;
            }
            glutPostRedisplay();
            break;
        case 'O':
        case 'o':
            drv_osd_enabled = !drv_osd_enabled;
            glutPostRedisplay();
            break;
        default:
            break;
    }
}

/* callback for mouse movement */
void ogl_mouse_move(int x, int y)
{
    int change_x;
    int change_y;
    
    if (drv_lmouse_pressed)
    {
        change_x = x - ogl_old_x;
        change_y = y - ogl_old_y;
        drv_rot_y += (float) change_x * 100.0f / (float)drv_win_width;
        if (drv_rot_y > 360.0f)
        {
            drv_rot_y -= 360.0f;
        } else if (drv_rot_y < 0.0f)
        {
            drv_rot_y += 360.0f;
        }
        drv_rot_x += (float) change_y * 100.0f / (float)drv_win_height;
        if (drv_rot_x > 360.0f)
        {
            drv_rot_x -= 360.0f;
        } else if (drv_rot_x < 0.0f)
        {
            drv_rot_x += 360.0f;
        }
        ogl_old_x = x;
        ogl_old_y = y;
        glutPostRedisplay();
    }
}

/* callback for mouse click */
void ogl_mouse(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
    {
        if (state == GLUT_DOWN)
        {
            if (drv_lmouse_pressed == TRUE)
            {
                
            }
            else
            {
                drv_lmouse_pressed = TRUE;
                ogl_old_x = x;
                ogl_old_y = y;
            }
        }
        if (state == GLUT_UP)
        {
            if (drv_lmouse_pressed == FALSE)
            {
                
            }
            else
            {
                drv_lmouse_pressed = FALSE;
                ogl_old_x = x;
                ogl_old_y = y;
            }
        }
    }
}

/* print out description of last error */
void ogl_checkerror(void)
{
    char *error_msg;
        
    switch(glGetError())
    {
        case GL_NO_ERROR:
            return;
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
    fprintf(stderr, "OpenGL error: %s!\n", error_msg);
}

/* draw a string with bitmap text */
void ogl_draw_string(char * str, int x, int y)
{
    int i;
    glRasterPos2i(x, y);
    for (i=0; *(str+i) != 0; str++)
    {
        glutBitmapCharacter(ogl_font, *(str+i));
    }
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
static void ogl_present_indexed_texture(void)
{
    unsigned long count, i;
    unsigned long *source;
    GLubyte *pixels;
    GLint matrix_mode;
    count = (unsigned long)drv_win_width * drv_win_height;
    if (count > (size_t)-1) error("OpenGL texture framebuffer is too large");
    if (count > ogl_index_capacity)
    {
        pixels = (GLubyte *)malloc((size_t)count);
        if (pixels == NULL) error("Cannot allocate indexed OpenGL framebuffer");
        free(ogl_index_pixels); ogl_index_pixels = pixels; ogl_index_capacity = count;
    }
    source = texsoft_pixels();
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
}

/* render current scene */
void drv_render(void)
{
    float model[4][4], projection[4][4];
    drv_fps_start();
    glClear(GL_COLOR_BUFFER_BIT | (ogl_has_depth ? GL_DEPTH_BUFFER_BIT : 0));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0,0, -3000.0f);
    glRotatef(drv_rot_x, 1, 0, 0);
    glRotatef(drv_rot_y, 0, 1, 0);
    glRotatef(-90, 1, 0, 0);
    glRotatef(-90, 0, 0, 1);

    if (ogl_indexed && drv_texture_enabled)
    {
        /* Column-major OpenGL matrix memory is the equivalent row-vector
         * array consumed by texsoft; no further transpose is needed. */
        glGetFloatv(GL_MODELVIEW_MATRIX, &model[0][0]);
        glGetFloatv(GL_PROJECTION_MATRIX, &projection[0][0]);
        if (!texsoft_begin(drv_win_width, drv_win_height, model, projection,
                             TRUE, &earth_cmap[0][0]))
            error("Cannot allocate indexed OpenGL texture raster");
        globe_draw_tris();
        ogl_present_indexed_texture();
    }
    else if(!dlist)
    {
        dlist = glGenLists(1);
        glNewList(dlist, GL_COMPILE_AND_EXECUTE);

        glBegin(GL_TRIANGLES);
        globe_draw_tris();
        glEnd();

        glEndList();
    } else
    {
        glCallList(dlist);
    }

    if (drv_osd_enabled)
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
    
    if (drv_auto_enabled == TRUE)
    {
        drv_globe_rotate();
        glutPostRedisplay();
    }
}

/* resize window */
void drv_resize(int width, int height) {
    if (width < 1 || height < 1) return;
    drv_win_width = width;
    drv_win_height = height;
    
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45, (float)width/(float)height, 1.0, 10000);
}

/* load texture */
void drv_texture_load(void)
{
    texture_generate(TEXTURE_DEFAULT_SIZE);
    if (ogl_indexed)
    {
        if (!texsoft_texture(texture_size, texture_size / 2U, texture_data))
            error("Cannot allocate indexed OpenGL texture mipmaps");
        texture_free();
        drv_texture_loaded = TRUE;
        return;
    }
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_size, texture_size/2, 0, GL_RGB, GL_UNSIGNED_BYTE, texture_data);
#ifndef NO_GENMIPMAP
    glGenerateMipmap(GL_TEXTURE_2D);
#else
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, texture_size, texture_size/2, GL_RGB, GL_UNSIGNED_BYTE, texture_data);
#endif
    texture_free();
    drv_texture_loaded = TRUE;
}

/* set rendering pipeline to textures */
void drv_texture_enable(void)
{
    if (!drv_texture_loaded) drv_texture_load();
    if (!ogl_indexed)
    {
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glEnable(GL_TEXTURE_2D);
    }
    drv_texture_enabled = TRUE;
}

/* turn off textures */
void drv_texture_disable(void)
{
    if (!ogl_indexed)
    {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    drv_texture_enabled = FALSE;
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
    glutDisplayFunc(drv_render);
    glutReshapeFunc(drv_resize);
    glutMouseFunc(ogl_mouse);
    glutMotionFunc(ogl_mouse_move);
    glutPassiveMotionFunc(ogl_mouse_move);
    glutKeyboardFunc(ogl_keyboard);

    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    if (ogl_indexed)
    {
        ogl_install_palette();
        glClearIndex((GLfloat)ogl_background);
    }
    else glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if (ogl_has_depth) glEnable(GL_DEPTH_TEST);
    drv_resize(in_width, in_height);
    glutShowWindow();
    glutPostRedisplay();

    ogl_checkerror();
    return 1;
}

/* main loop */
void drv_loop(void)
{
    glutMainLoop();
}

/* draw flat shaded colored triangle */
void drv_draw_tri_flat_rgb(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Color_RGB *color)
{
    glColor3ub(color->r, color->g, color->b);

    glVertex3fv((GLfloat *)xyz1);
    glVertex3fv((GLfloat *)xyz2);
    glVertex3fv((GLfloat *)xyz3);
}

void drv_draw_tri_flat_color(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3,
                              unsigned char color)
{
    if (ogl_indexed) glIndexi((GLint)color);
    else glColor3ub(earth_cmap[color][0], earth_cmap[color][1], earth_cmap[color][2]);
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

/* draw the OSD */
void drv_draw_osd(void)
{
    char status_str[256];

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
    strcat(status_str, drv_texture_enabled ?
             (ogl_indexed ? ", software texture mapping on" : ", texture mapping on") :
             ", texture mapping off");
    ogl_draw_string(status_str, 10, drv_win_height - 18);
    
    glPopAttrib();
}

#endif
