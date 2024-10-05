// testglobe - display 3d globe to test 3d hardware
// drv_xgl.c - Sun XGL driver
//
// Copyright (C) 2024 trguhq
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <stdlib.h>
#include <xview/xview.h>
#include <xview/frame.h>
#include <xview/panel.h>
#include <xview/testsw.h>
#include <xview/canvas.h>
#include <xview/xv_xrect.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <xgl/xgl.h>
#include "testglob.h"
#include "glob_ext.h"
#include "drv_ext.h"
#include "tex_ext.h"

// previous position of mouse pointer
static int xgl_old_x;
static int xgl_old_y;

// cleanly close window
void drv_close(void)
{
    exit(0);
}

// keyboard input
void xgl_keyboard(unsigned char key, int x, int y)
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
            globe_toggle_res(key);
            break;
        case 't':
        case 'T':
            globe_toggle_tex();
            break;
        case 'O':
        case 'o':
            drv_osd_enabled = !drv_osd_enabled;
            break;
        default:
            break;
    }
}

// mouse movement
void xgl_mouse_move(int x, int y)
{
    int change_x;
    int change_y;
    
    if (drv_lmouse_pressed)
    {
        change_x = x - xgl_old_x;
        change_y = y - xgl_old_y;
        drv_rot_y += (float) change_x * 100.0f / (float)drv_win_width;
        drv_rot_x += (float) change_y * 100.0f / (float)drv_win_height;
        xgl_old_x = x;
        xgl_old_y = y;
    }
}

// mouse click
void xgl_mouse(int button, int state, int x, int y)
{
}

// check for errors
void xgl_checkerrors (void)
{
}

// draw a string with bitmap text
void xgl_draw_string(char * str, int x, int y)
{
}

// initialize graphics subsystem
void drv_init(int *argc, char **argv)
{
}

// render current scene
void drv_render(void)
{
    globe_draw_tris();

    if (drv_osd_enabled)
    {
        drv_draw_osd();
    }
}

// resize window
void drv_resize(int width, int height)
{
    drv_win_width = width;
    drv_win_height = height;
}

// load texture
void drv_texture_load(void)
{
    drv_texture_loaded = TRUE;
}

// set rendering pipeline to textures
void drv_texture_enable(void)
{
    if (!drv_texture_loaded) drv_texture_load();
    drv_texture_enabled = TRUE;
}

// turn off textures
void drv_texture_disable(void)
{
    drv_texture_enabled = FALSE;
}

// initialize a window, all values < 1 for full screen/default
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

    xgl_checkerrors();
    return 1;
}

// main program loop
void drv_loop(void)
{
}

// draw a flat shaded triangle with a color
void drv_draw_tri_flat_rgb(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Color_RGB *color)
{
}

// draw a flat shaded triangle with texture coords
void drv_draw_tri_flat_uv(Vertex *xyz1, Vertex *xyz2, Vertex *xyz3, Coord *uv1, Coord *uv2, Coord *uv3)
{
}

// draw on screen display
void drv_draw_osd(void)
{
    char status_str[256];
    
    snprintf(status_str, 255, "%i triangles (%i visible), texture mapping %s", globe_tris_num, globe_tris_num_vis, (drv_texture_enabled ? "on" : "off"));
}
