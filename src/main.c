/*
 * testglobe - display 3d globe to test 3d hardware
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

#include <stdio.h>

#include "testglob.h"
#include "drv.h"
#include "globe.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "tex_ext.h"

#ifdef GDI
/* The Win16 driver owns WinMain and its instance/show-window arguments. */
int testglobe_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    drv_ext_init();
    drv_init(&argc, argv);

#if !defined(GDI) && !defined(SEG68)
    printf("%s %i.%i\n", TESTGLOBE_NAME, TESTGLOBE_VERSION_MAJOR, TESTGLOBE_VERSION_MINOR);
    printf("Driver: %s\n", drv_name);
    printf("Resolution: %u x %u\n", drv_win_width, drv_win_height);
    printf("Color depth: %u bpp (%lu colors)\n", drv_bits_per_pixel, drv_colors);
    printf("Color mapped: %s\n", (drv_colormapped == TRUE ? "yes" : "no"));
    if (drv_colormapped == TRUE) {
        printf("Palette: %u (%lu colors)\n", drv_colormap_bits, drv_colormap_colors);
    }
#ifdef TEXTURE
    printf("Texture mapping: %s (%s)\n", (drv_texture_avail == TRUE ? "available" : "none"),
           (drv_texture_enabled == TRUE ? "enabled" : "disabled"));
#endif
#endif /* Native GUI applications have no console. */
    
    drv_init_window(0,0,0,0);
    globe_init();
    globe_init_verts(GLOBE_DEFAULT_LONGITUDE, -1, GLOBE_RADIUS);
#ifdef TEXTURE
    texture_init();
#endif
    drv_loop();

    return 0;
}
