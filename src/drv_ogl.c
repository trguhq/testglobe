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
#include "testglob.h"
#include "glob_ext.h"
#include "drv_ext.h"
#include "tex_ext.h"

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

/* cleanly close window */
void drv_close(void)
{
    exit(0);
}

/* error handling */
void ogl_message(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam)
{
    fprintf(stderr, "GL CALLBACK: type = 0x%x, severity = 0x%x, message = %s\n", type, severity, message );
}

/* keyboard input */
void ogl_keyboard(unsigned char key, int x, int y)
{
    switch(key)
    {
        case 27:
            drv_close();
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
    glRasterPos2i(x, y);
    int i;
    for (i=0; *(str+i) != 0; str++)
    {
        glutBitmapCharacter(ogl_font, *(str+i));
    }
}

/* initialize graphics subsystem */
void drv_init(int *argc, char **argv)
{
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    ogl_font = GLUT_BITMAP_HELVETICA_12;
}

/* render current scene */
void drv_render(void)
{
    drv_fps_start();
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0,0, -3000.0f);
    glRotatef(drv_rot_x, 1, 0, 0);
    glRotatef(drv_rot_y, 0, 1, 0);
    glRotatef(-90, 1, 0, 0);
    glRotatef(-90, 0, 0, 1);

    if(!dlist)
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
    
    glutSwapBuffers();
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
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEnable(GL_TEXTURE_2D);
    drv_texture_enabled = TRUE;
}

/* turn off textures */
void drv_texture_disable(void)
{
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    drv_texture_enabled = FALSE;
}

/* initialize a window, all values < 1 for full screen/default */
int drv_init_window(int in_x, int in_y, int in_width, int in_height)
{
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

    glutInitDisplayMode(GLUT_RGB|GLUT_DOUBLE);
    glutInitWindowSize(in_width, in_height);
    glutInitWindowPosition(in_x, in_y);
    glutCreateWindow(TESTGLOBE_NAME);
    glutDisplayFunc(drv_render);
    glutReshapeFunc(drv_resize);
    glutMouseFunc(ogl_mouse);
    glutMotionFunc(ogl_mouse_move);
    glutPassiveMotionFunc(ogl_mouse_move);
    glutKeyboardFunc(ogl_keyboard);

    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

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

/* draw flat shaded textured triangle */
void drv_draw_tri_flat_uv(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Coord *uv1, Coord *uv2, Coord *uv3)
{
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
    
    /* due to differences in implementation this has to be disabled for Mesa */
    if (drv_texture_enabled)
    {
        glDisable(GL_TEXTURE_2D);
    }
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, drv_win_width, 0, drv_win_height);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    ogl_draw_string(drv_help, 10, 10);
    snprintf(status_str, 255, "%i triangles (%i visible), texture mapping %s, %.2f frames per second", globe_tris_num, globe_tris_num_vis, (drv_texture_enabled ? "on" : "off"), drv_fps_average);
    ogl_draw_string(status_str, 10, drv_win_height - 18);
    
    if (drv_texture_enabled)
    {
        glEnable(GL_TEXTURE_2D);
    }
}
