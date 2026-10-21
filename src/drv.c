/*
 * testglobe - display 3d globe to test 3d hardware
 * drv.c - graphics driver
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

#include "testglob.h"
#include "drv.h"
#include "globe.h"
#ifdef GDI
#include <windows.h>
#else
#ifdef SEG86
#include <time.h>
#else /* FLAT */
#include <sys/time.h>
#endif
#endif

unsigned int drv_win_x, drv_win_y;              /* window geometry */
unsigned int drv_win_width, drv_win_height;     /* window geometry */
int drv_colormapped;                    /* bool is colormap window */
int drv_texture_avail;                  /* is texturing available in the driver */
int drv_texture_loaded;                 /* has texture been loaded */
int drv_texture_enabled;                /* is texturing enabled */
int drv_osd_enabled;                    /* is OSD enabled */
unsigned int drv_bits_per_pixel;        /* bits depth */
unsigned int drv_colormap_bits;         /* bits per color */
unsigned long int drv_colors;           /* number of colors */
unsigned long int drv_colormap_colors;  /* number of colors in colormap */
unsigned int drv_bytes_per_pixel;       /* bytes per color: 1, 2 or 3 */
char drv_name[64];                      /* name of driver */
float drv_rot_x;                        /* rotation of globe on x axis */
float drv_rot_y;                        /* rotation of globe on y axis */
int drv_lmouse_pressed;                 /* mouse pressed */
int drv_rmouse_pressed;                 /* */
char drv_help[256];                     /* OSD help string to print */
int drv_fps_enabled;                    /* fps counter enabled */
float drv_fps_average;                  /* average fps */
#ifdef GDI
static unsigned long drv_frame_start, drv_frame_stop;
#else
#ifdef SEG86
static clock_t drv_frame_start, drv_frame_stop;
#else /* FLAT */
static struct timeval drv_frame_start, drv_frame_stop;
#endif
#endif
static float drv_frame_seconds;
static float drv_fps_history_seconds;
long drv_fps_history_num;               /* total number of frames recorded */
int drv_auto_enabled;                   /* automatically rotate */

/* call first */
void drv_ext_init(void)
{
#ifndef DRV
    error("No driver loaded");          /* won't compile anyway */
#endif
    drv_win_x = DRV_WIN_X;
    drv_win_y = DRV_WIN_Y;
    drv_win_width = DRV_WIN_WIDTH;
    drv_win_height = DRV_WIN_HEIGHT;
    drv_colormapped = DRV_COLORMAPPED;
#ifdef DRV_TEXTURE
    drv_texture_avail = DRV_TEXTURE;
#else
    drv_texture_avail = FALSE;
#endif
#ifdef DRV_TEXTURE_DEFAULT
    drv_texture_enabled = DRV_TEXTURE_DEFAULT;
#else
    drv_texture_enabled = FALSE;
#endif
    drv_osd_enabled = DRV_OSD_DEFAULT;
    drv_bits_per_pixel = DRV_DEPTH;
    drv_colormap_bits = DRV_COLORMAPDEPTH;
    /* An unsigned long is 32 bits even when the DOS compiler has 16-bit ints. */
    drv_colors = (drv_bits_per_pixel == 32 ? 0xFFFFFFFFUL : 1UL << drv_bits_per_pixel);
    drv_colormap_colors = (drv_colormap_bits == 32 ? 0xFFFFFFFFUL : 1UL << drv_colormap_bits);
    switch (drv_bits_per_pixel)
    {
        case 8:
            drv_bytes_per_pixel = 1;
            break;
        case 16:
            drv_bytes_per_pixel = 2;
            break;
        case 24:
            drv_bytes_per_pixel = 3;
            break;
        default:    /* this should really be an error as it's not handled */
            drv_bytes_per_pixel = 4;
            break;
    }
    strcpy(drv_name, DRV_NAME);
    strcpy(drv_help, DRV_HELP);
    drv_rot_x = GLOBE_DEFAULT_ROTATION_X;
    drv_rot_y = GLOBE_DEFAULT_ROTATION_Y;
    drv_lmouse_pressed = FALSE;
    drv_rmouse_pressed = FALSE;
    drv_fps_enabled = DRV_FPS_DEFAULT;
    drv_fps_average = 0;
    drv_fps_history_num = 0;
    drv_fps_history_seconds = 0;
    drv_frame_seconds = 0;
    drv_auto_enabled = FALSE;
    
}

/* frame start */
void drv_fps_start(void)
{
#ifdef GDI
    drv_frame_start = GetTickCount();
#else
#ifdef SEG86
    /* Microsoft C's DOS clock() reports elapsed time without POSIX headers. */
    drv_frame_start = clock();
#else /* FLAT */
    gettimeofday(&drv_frame_start, NULL);
#endif
#endif
}

/* frame stop */
void drv_fps_stop(void)
{
#ifdef GDI
    drv_frame_stop = GetTickCount();
    /* GetTickCount is a wrapping 32-bit counter, including on test hosts. */
    drv_frame_seconds = (float)((drv_frame_stop - drv_frame_start) &
                                0xFFFFFFFFUL) / 1000.0f;
#else
#ifdef SEG86
    drv_frame_stop = clock();
    if (drv_frame_start == (clock_t)-1 || drv_frame_stop == (clock_t)-1)
    {
        drv_frame_seconds = 0;
    }
    else
    {
        /* Unsigned subtraction also handles a 32-bit DOS clock rollover. */
        drv_frame_seconds = (float)((unsigned long)drv_frame_stop -
                                    (unsigned long)drv_frame_start) /
                            (float)CLOCKS_PER_SEC;
    }
#else /* FLAT */
    gettimeofday(&drv_frame_stop, NULL);
    drv_frame_seconds = (float)(drv_frame_stop.tv_sec - drv_frame_start.tv_sec) +
                        (float)(drv_frame_stop.tv_usec - drv_frame_start.tv_usec) /
                        1000000.0f;
    if (drv_frame_seconds < 0)
        drv_frame_seconds = 0;
#endif
#endif

    if (drv_frame_seconds >= 1.0f)
    {
        drv_fps_average = 1.0f / drv_frame_seconds;
        drv_fps_history_num = 0;
        drv_fps_history_seconds = 0;
    } else
    {
        drv_fps_history_seconds += drv_frame_seconds;
        drv_fps_history_num++;

        if (drv_fps_history_num >= DRV_FPS_HISTORY)
        {
            /* A coarse DOS timer can return zero for an entire short sample. */
            if (drv_fps_history_seconds > 0)
                drv_fps_average = (float)drv_fps_history_num /
                                  drv_fps_history_seconds;
            drv_fps_history_num = 0;
            drv_fps_history_seconds = 0;
        }
    }
}

/* rotate globe at a fixed speed */
void drv_globe_rotate(void)
{
    drv_rot_y += DRV_ROTATE * drv_frame_seconds;
    while (drv_rot_y >= 360.0f)
    {
        drv_rot_y -= 360.0f;
    }
    while (drv_rot_y < 0.0f)
    {
        drv_rot_y += 360.0f;
    }
}
