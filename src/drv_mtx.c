/*
 * testglobe - display 3d globe to test 3d hardware
 * drv_mtx.c - Matrox LibShell driver
 *
 * Copyright (C) 2026 trguhq
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

#define DRV_MTX

#include <stdlib.h>
#include "testglob.h"
#include "drv.h"
#include "glob_ext.h"
#include "drv_ext.h"

#ifdef DRV_MTX

/* map of PG card interface */
struct pg_com pg;

#define LOBYTE(word_val) (word_val & 0xFF)
#define HIBYTE(word_val) ((word_val >> 8) & 0xFF)

/* previous mouse pointer location */
static int mtx_old_x;
static int mtx_old_y;

/* initialize graphics subsystem */
void drv_init(int *argc, char **argv)
{
    lsinit(0, 1);
    ioflush();
}

/* cleanly close window */
void drv_close(void)
{
    ioflush();
    lsemulen();
    exit(0);
}

/* keyboard input */
void mtx_keyboard(unsigned char key, int x, int y)
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
            break;
        case 'a':
        case 'A':
            drv_auto_enabled = !drv_auto_enabled;
            break;
        case 'O':
        case 'o':
            drv_osd_enabled = !drv_osd_enabled;
            break;
        default:
            break;
    }
}

/* callback for mouse movement */
void mtx_mouse_move(int x, int y)
{
    int change_x;
    int change_y;
    
    if (drv_lmouse_pressed)
    {
        change_x = x - mtx_old_x;
        change_y = y - mtx_old_y;
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
        mtx_old_x = x;
        mtx_old_y = y;
    }
}

/* callback for mouse click */
void mtx_mouse(int button, int state, int x, int y)
{

}

/* draw a string with bitmap text */
void mtx_draw_string(char * str, int x, int y)
{
}

/* render current scene */
void drv_render(void)
{
    drv_fps_start();
 
    if (drv_osd_enabled)
    {
        drv_draw_osd();
    }
    
    drv_fps_stop();
    
    if (drv_auto_enabled == TRUE)
    {
        drv_globe_rotate();
    }
}

/* resize window */
void drv_resize(int width, int height) {
    drv_win_width = width;
    drv_win_height = height;
    
}

/* load texture */
void drv_texture_load(void)
{
    drv_texture_loaded = TRUE;
}

/* set rendering pipeline to textures */
void drv_texture_enable(void)
{
    if (!drv_texture_loaded) drv_texture_load();
    drv_texture_enabled = TRUE;
}

/* turn off textures */
void drv_texture_disable(void)
{
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

    return 1;
}

/* main loop */
void drv_loop(void)
{
} 

/* draw flat shaded colored triangle */
void drv_draw_tri_flat_rgb(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Color_RGB *color)
{
}

/* draw flat shaded colored quad */
void drv_draw_quad_flat_rgb(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Vertex *xyz4, Color_RGB *color)
{
}

/* draw flat shaded textured triangle */
void drv_draw_tri_flat_uv(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Coord *uv1, Coord *uv2, Coord *uv3)
{
/* not available in this driver */
}

/* draw the OSD */
void drv_draw_osd(void)
{
    char status_str[256];
    
    /* due to differences in implementation this has to be disabled for Mesa */
    if (drv_texture_enabled)
    {
    }
    
    mtx_draw_string(drv_help, 10, 10);
    snprintf(status_str, 255, "%i triangles (%i visible), texture mapping %s, %.2f frames per second", globe_tris_num, globe_tris_num_vis, (drv_texture_enabled ? "on" : "off"), drv_fps_average);
    mtx_draw_string(status_str, 10, drv_win_height - 18);
    
    if (drv_texture_enabled)
    {
    }
}

#endif
