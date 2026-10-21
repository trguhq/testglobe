/*
 * testglobe - display 3d globe to test 3d hardware
 * drv_ext.h - graphics driver externs
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

#ifndef drv_ext_h
#define drv_ext_h

#include "testglob.h"
#include "drv.h"
#include "types.h"

extern unsigned int drv_win_x;
extern unsigned int drv_win_y;
extern unsigned int drv_win_width;
extern unsigned int drv_win_height;
extern int drv_colormapped;
extern int drv_texture_avail;
extern int drv_texture_loaded;
extern int drv_texture_enabled;
extern int drv_osd_enabled;
extern unsigned int drv_bits_per_pixel;
extern unsigned int drv_bytes_per_pixel;
extern unsigned int drv_colormap_bits;
extern unsigned long int drv_colors;
extern unsigned long int drv_colormap_colors;
extern char drv_name[];
extern char drv_help[];
extern float drv_rot_x;
extern float drv_rot_y;
extern int drv_lmouse_pressed;
extern int drv_rmouse_pressed;
extern float drv_fps_average;
extern int drv_fps_enabled;
extern int drv_auto_enabled;


extern void drv_ext_init(void);
extern void drv_init(int *argc, char **argv);
extern int drv_init_window(int in_x, int in_y, int in_width, int in_height);
extern void drv_render(void);
extern void drv_close(void);
/* TRI: selected traversal, separate caps, and retained driver capabilities. */
#ifdef DRV_TRI_COLOR
extern void drv_draw_tri_flat_color(Vertex TG_HUGE *xyz1, Vertex TG_HUGE *xyz2,
                                    Vertex TG_HUGE *xyz3, unsigned char color);
#endif
#ifdef DRV_TRI_RGB
extern void drv_draw_tri_flat_rgb(Vertex TG_HUGE *xyz1, Vertex TG_HUGE *xyz2,
                                  Vertex TG_HUGE *xyz3, Color_RGB TG_HUGE *color);
#endif
#ifdef DRV_TRI_UV
extern void drv_draw_tri_flat_uv(Vertex TG_HUGE *xyz1, Vertex TG_HUGE *xyz2,
                                 Vertex TG_HUGE *xyz3, Coord TG_HUGE *uv1,
                                 Coord TG_HUGE *uv2, Coord TG_HUGE *uv3);
#endif
/* TRISTRIP: one strip per call, with an independent texture entry point. */
#ifdef TRISTRIP
#ifdef DRV_COLORMAP
/* One palette byte for each of the count-2 strip facets. */
extern void drv_draw_tristrip_flat_color(unsigned int count,
                                        const Vertex TG_HUGE *vertices,
                                        const unsigned char TG_HUGE *facet_colors);
#else
/* One RGB color for each of the count-2 strip facets. */
extern void drv_draw_tristrip_flat_rgb(unsigned int count,
                                      const Vertex TG_HUGE *vertices,
                                      const Color_RGB TG_HUGE *facet_colors);
#endif
#ifdef TEXTURE
/* One UV per vertex; count-2 facets use the same strip winding as above. */
extern void drv_draw_tristrip_flat_uv(unsigned int count,
                                     const Vertex TG_HUGE *vertices,
                                     const Coord TG_HUGE *coords);
#endif
#endif
/* QUAD: individual polygons and optional indexed batches use one topology. */
#ifdef QUAD
#ifdef DRV_COLORMAP
extern void drv_draw_quad_flat_color(Vertex TG_HUGE *xyz1, Vertex TG_HUGE *xyz2,
                                     Vertex TG_HUGE *xyz3, Vertex TG_HUGE *xyz4,
                                     unsigned char color);
#else
extern void drv_draw_quad_flat_rgb(Vertex TG_HUGE *xyz1, Vertex TG_HUGE *xyz2,
                                   Vertex TG_HUGE *xyz3, Vertex TG_HUGE *xyz4,
                                   Color_RGB TG_HUGE *color);
#endif
#ifdef DRV_QUAD_BATCH
/* Four ordered vertex indices and one palette byte per quad. Vertices may
 * be shared; all indices are relative to this bounded vertex packet. */
extern void drv_draw_quadbatch_flat_color(unsigned int vertex_count,
                                          const Vertex TG_HUGE *vertices,
                                          unsigned int quad_count,
                                          const unsigned int TG_HUGE *indices,
                                          const unsigned char TG_HUGE *colors);
#endif
#endif

/* Shared submission utilities; native drawing remains in each driver. */
#if defined(TRISTRIP) && defined(DRV_COLORMAP)
typedef void (*Drv_strip_batch)(unsigned int count,
                                const Vertex TG_HUGE *vertices,
                                const unsigned char TG_HUGE *facet_colors);
/* Even facet batches retain winding and overlap their last two vertices. */
extern void drv_split_tristrip_batches(unsigned int count,
                                   const Vertex TG_HUGE *vertices,
                                   const unsigned char TG_HUGE *facet_colors,
                                   unsigned int max_facets,
                                   Drv_strip_batch draw_batch);
#endif
#if defined(TRISTRIP) && defined(TEXTURE)
typedef void (*Drv_uv_triangle)(const Vertex TG_HUGE *first,
                                const Vertex TG_HUGE *second,
                                const Vertex TG_HUGE *third,
                                const Coord TG_HUGE *first_uv,
                                const Coord TG_HUGE *second_uv,
                                const Coord TG_HUGE *third_uv);
/* Expand alternating strip facets without changing their winding or UVs. */
extern void drv_expand_tristrip_uv(unsigned int count,
                                   const Vertex TG_HUGE *vertices,
                                   const Coord TG_HUGE *coords,
                                   Drv_uv_triangle draw_triangle);
#endif
#ifdef DRV_QUAD_BATCH
extern int drv_validate_quadbatch(unsigned int vertex_count,
                                   unsigned int quad_count,
                                   const unsigned int TG_HUGE *indices);
extern void drv_submit_quad_as_batch(Vertex TG_HUGE *first,
                                      Vertex TG_HUGE *second,
                                      Vertex TG_HUGE *third,
                                      Vertex TG_HUGE *fourth,
                                      unsigned char color);
#endif
extern void drv_loop(void);
#ifdef TEXTURE
extern void drv_texture_enable(void);
extern void drv_texture_disable(void);
#endif
extern void drv_draw_osd(void);
/* Common OSD prefix; caller supplies at least 128 bytes. */
extern void drv_format_stats(char *buffer);
#ifdef DRV_EARTH_RESOURCES
extern void drv_load_earth_data(int longitude,
                                unsigned char TG_HUGE *destination);
/* Native adapter: validate and copy one bounded resource into the heap. */
extern void drv_load_earth_chunk(unsigned int id, unsigned long length,
                                 unsigned char TG_HUGE *destination);
#endif
extern void drv_fps_start(void);
extern void drv_fps_stop(void);
/* Common application actions; platform event translation stays in drivers. */
extern int drv_handle_key(unsigned char key);
extern void drv_rotate_drag(long dx, long dy);
extern void drv_rotate_elapsed(double seconds);
extern void drv_globe_rotate(void);

#endif /* drv_ext_h */
