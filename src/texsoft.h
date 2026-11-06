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
/* Filtering belongs to the texture, independently of the raster's pixel
 * format. Bilinear retains only level zero; trilinear builds the full pyramid.
 * Ownership and invalid-input rules match the adoption entry above, which
 * retains its trilinear behavior. */
#define TEXSOFT_BILINEAR 0
#define TEXSOFT_TRILINEAR 1
extern Drv_result texsoft_texture_take_filtered(unsigned int width,
                                      unsigned int height,
                                      unsigned char *rgb, int filter);
/* The first two formats retain the original numeric-word API. Byte indices
 * let OpenGL present the raster directly without a second transfer buffer. */
#define TEXSOFT_RGB_WORDS 0
#define TEXSOFT_INDEX_WORDS 1
#define TEXSOFT_INDEX_BYTES 2

/* Matrices use row vectors: clip = vertex * model * projection. OpenGL's
 * column-major matrix memory has this same layout when viewed as [4][4].
 * Select one TEXSOFT_* output format. Bottom-up words contain 0xAABBGGRR
 * or a color index for IRIS GL; byte output contains one palette index per
 * pixel for OpenGL. Only the matching accessor returns storage. Changing
 * dimensions or format releases the old raster before allocating its replacement.
 * Host contracts exercise numeric words, not the native IRIX ABI. */
extern int texsoft_begin(unsigned int width, unsigned int height,
                           float model[4][4], float projection[4][4],
                           int format, const unsigned char *colormap);
extern void texsoft_triangle(const Vertex *a, const Vertex *b, const Vertex *c,
                               const Coord *ua, const Coord *ub, const Coord *uc);
extern unsigned long *texsoft_pixels(void);
extern unsigned char *texsoft_index_pixels(void);
/* Release only viewport-sized buffers; retained mipmaps support re-enable. */
extern void texsoft_release_raster(void);
extern void texsoft_free(void);
#endif
