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
#include <stdlib.h>

#include "testglob.h"
#include "drv.h"
#include "globe.h"
#include "drv_ext.h"
#include "glob_ext.h"
#ifdef TEXTURE
#include "tex_ext.h"
#endif

/* The application owns the shared scene; each driver owns native objects. */
static void testglobe_cleanup(void)
{
#ifdef TEXTURE
    texture_free();
#endif
    globe_free();
}

#ifdef GDI
/* The Win16 driver owns WinMain and its instance/show-window arguments. */
int testglobe_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    drv_ext_init();
    globe_init();
#ifdef TEXTURE
    texture_init();
#endif
    /* Register shared ownership first: native cleanup runs before this
     * callback when a legacy event loop must terminate through exit(). */
    if (atexit(testglobe_cleanup) != 0)
        error("Cannot register scene cleanup");
    drv_init(&argc, argv);
    /* Native window creation determines the actual color format and size. */
    if (!drv_init_window(0,0,0,0))
        error("Cannot initialize application window");

#ifndef DRV_NO_CONSOLE
    printf("%s %i.%i\n", TESTGLOBE_NAME, TESTGLOBE_VERSION_MAJOR, TESTGLOBE_VERSION_MINOR);
    printf("Driver: %s\n", drv_name);
    printf("Resolution: %u x %u\n", drv_win_width, drv_win_height);
    printf("Color depth: %u bpp (%lu colors)\n", drv_bits_per_pixel, drv_colors);
    printf("Pixel storage: %u bytes per pixel\n", drv_bytes_per_pixel);
    printf("Color mapped: %s\n", (drv_colormapped == TRUE ? "yes" : "no"));
    if (drv_colormapped == TRUE) {
        printf("Palette: %u (%lu colors)\n", drv_colormap_bits, drv_colormap_colors);
    }
#ifdef TEXTURE
    printf("Texture mapping: %s (%s)\n", (drv_texture_avail == TRUE ? "available" : "none"),
           (drv_texture_enabled == TRUE ? "enabled" : "disabled"));
#endif
#endif /* Native GUI applications have no console. */
    
    /* A valid native window can remain interactive even if no mesh fits. */
    (void)globe_set_resolution(GLOBE_DEFAULT_LONGITUDE, -1, GLOBE_RADIUS);
    drv_loop();

    return 0;
}
