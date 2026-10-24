/* Shared fallback for drivers without native texture mapping.
 * Copyright (C) 2026 trguhq. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef texsoft_h
#define texsoft_h
#include "texture.h"

/* Copy the RGB image and construct its complete mipmap pyramid. */
extern int texsoft_texture(unsigned int width, unsigned int height,
                             const unsigned char *rgb);
/* Adopt malloc-owned RGB bytes as level zero without copying them. This
 * consumes the pointer on EVERY result, including invalid dimensions and
 * partial mipmap failure. Only DRV_OK leaves a usable replacement texture.
 * The caller must not pass borrowed data or an already-adopted pointer. */
extern Drv_result texsoft_texture_take(unsigned int width, unsigned int height,
                                      unsigned char *rgb);
/* Matrices use row vectors: clip = vertex * model * projection. OpenGL's
 * column-major matrix memory has this same layout when viewed as [4][4].
 * Bottom-up numeric words contain 0xAABBGGRR or a color index. IRIS GL can
 * present these native unsigned-long words directly on 32-bit IRIX; OpenGL
 * converts them to its explicitly sized pixel type. Host contracts exercise
 * numeric words, not the IRIX ABI. */
extern int texsoft_begin(unsigned int width, unsigned int height,
                           float model[4][4], float projection[4][4],
                           int indexed, const unsigned char *colormap);
extern void texsoft_triangle(const Vertex *a, const Vertex *b, const Vertex *c,
                               const Coord *ua, const Coord *ub, const Coord *uc);
extern unsigned long *texsoft_pixels(void);
/* Release only viewport-sized buffers; retained mipmaps support re-enable. */
extern void texsoft_release_raster(void);
extern void texsoft_free(void);
#endif
