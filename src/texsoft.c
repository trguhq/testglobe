/* Perspective-correct textured rasterization for palette OpenGL and IRIS GL.
 * Drivers supply their native model/view/projection matrices and present the
 * raster. Normal flat drawing and supported texture paths remain native.
 * Copyright (C) 2026 trguhq. SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "testglob.h"
#if defined(DRV_IGL) || defined(DRV_OGL)
#include "texsoft.h"

#define TEXSOFT_LEVELS 10
#define TEXSOFT_CLIP 12
typedef struct Texsoft_vertex { double p[4], u, v; } Texsoft_vertex;
typedef struct Texsoft_screen { double x, y, z, q, u, v; } Texsoft_screen;
static unsigned char *texsoft_mips[TEXSOFT_LEVELS];
static unsigned int texsoft_mip_w[TEXSOFT_LEVELS];
static unsigned int texsoft_mip_h[TEXSOFT_LEVELS];
static unsigned int texsoft_levels, texsoft_width, texsoft_height;
static unsigned long *texsoft_frame;
static float *texsoft_depth;
static double texsoft_matrix[4][4];
static int texsoft_indexed, texsoft_ready;
static unsigned char *texsoft_lut;
static unsigned char texsoft_palette[TEXTURE_PALETTE_BYTES];

static int texsoft_finite(double value)
{
    /* Both comparisons fail for NaN, without requiring C99 isfinite(). */
    return value >= -DBL_MAX && value <= DBL_MAX;
}

void texsoft_release_raster(void)
{
    free(texsoft_frame); free(texsoft_depth);
    texsoft_frame = NULL; texsoft_depth = NULL;
    texsoft_width = texsoft_height = 0;
    texsoft_ready = FALSE;
}

static void texsoft_free_mips(void)
{
    unsigned int i;
    for (i = 0; i < TEXSOFT_LEVELS; ++i)
    {
        free(texsoft_mips[i]);
        texsoft_mips[i] = NULL;
    }
    texsoft_levels = 0;
    texsoft_ready = FALSE;
}

void texsoft_free(void)
{
    texsoft_release_raster();
    texsoft_free_mips();
    free(texsoft_lut); texsoft_lut = NULL;
}

static int texsoft_dimensions(unsigned int width, unsigned int height)
{
    return width != 0 && height != 0 &&
           width <= TEXTURE_MAX_SIZE && height <= TEXTURE_MAX_SIZE &&
           (width & (width - 1U)) == 0 && (height & (height - 1U)) == 0 &&
           (unsigned long)width * height <= (size_t)-1 / 3U;
}

Drv_result texsoft_texture_take(unsigned int width, unsigned int height,
                                  unsigned char *rgb)
{
    unsigned int level, w, h, x, y, channel, px, py;
    unsigned int sum, dx, dy;
    unsigned long offset;
    if (rgb == NULL || !texsoft_dimensions(width, height))
    {
        free(rgb);
        return DRV_BAD_INPUT;
    }
    texsoft_free_mips();
    w = width; h = height;
    for (level = 0; level < TEXSOFT_LEVELS; ++level)
    {
        texsoft_mip_w[level] = w; texsoft_mip_h[level] = h;
        /* The base belongs to this function immediately; all later failures
         * release it with the partially built pyramid, exactly once. */
        texsoft_mips[level] = level == 0 ? rgb :
                         (unsigned char *)malloc((size_t)w * h * 3U);
        if (texsoft_mips[level] == NULL)
        {
            texsoft_free_mips();
            return DRV_NO_MEMORY;
        }
        if (level != 0)
            for (y = 0; y < h; ++y)
                for (x = 0; x < w; ++x)
                    for (channel = 0; channel < 3; ++channel)
                    {
                        sum = 0;
                        for (dy = 0; dy < 2; ++dy)
                            for (dx = 0; dx < 2; ++dx)
                            {
                                px = (2U * x + dx) % texsoft_mip_w[level - 1U];
                                py = (2U * y + dy) % texsoft_mip_h[level - 1U];
                                offset = ((unsigned long)py * texsoft_mip_w[level - 1U] + px) * 3UL + channel;
                                sum += texsoft_mips[level - 1U][offset];
                            }
                        texsoft_mips[level][((unsigned long)y * w + x) * 3UL + channel] =
                            (unsigned char)((sum + 2U) / 4U);
                    }
        ++texsoft_levels;
        if (w == 1 && h == 1) return DRV_OK;
        if (w > 1) w /= 2U;
        if (h > 1) h /= 2U;
    }
    texsoft_free_mips();
    return DRV_UNAVAILABLE;
}

int texsoft_texture(unsigned int width, unsigned int height,
                      const unsigned char *rgb)
{
    unsigned char *copy;
    size_t bytes;
    if (rgb == NULL || !texsoft_dimensions(width, height)) return 0;
    bytes = (size_t)width * height * 3U;
    copy = (unsigned char *)malloc(bytes);
    if (copy == NULL) return 0;
    memcpy(copy, rgb, bytes);
    return texsoft_texture_take(width, height, copy) == DRV_OK;
}

static int texsoft_make_lut(const unsigned char *palette)
{
    unsigned int key, color, best;
    long r, g, b, dr, dg, db, distance, closest;
    if (palette == NULL) return 0;
    if (texsoft_lut != NULL &&
        memcmp(palette, texsoft_palette, TEXTURE_PALETTE_BYTES) == 0) return 1;
    if (texsoft_lut == NULL) texsoft_lut = (unsigned char *)malloc(32768U);
    if (texsoft_lut == NULL) return 0;
    memcpy(texsoft_palette, palette, TEXTURE_PALETTE_BYTES);
    for (key = 0; key < 32768U; ++key)
    {
        r = (long)((key >> 10) & 31U) * 255L / 31L;
        g = (long)((key >> 5) & 31U) * 255L / 31L;
        b = (long)(key & 31U) * 255L / 31L;
        closest = 195076L; best = 0;
        for (color = 0; color < TEXTURE_PALETTE_SIZE; ++color)
        {
            dr = r - palette[3U * color]; dg = g - palette[3U * color + 1U];
            db = b - palette[3U * color + 2U];
            distance = dr * dr + dg * dg + db * db;
            if (distance < closest) { closest = distance; best = color; }
        }
        texsoft_lut[key] = (unsigned char)best;
    }
    return 1;
}

int texsoft_begin(unsigned int width, unsigned int height,
                    float model[4][4], float projection[4][4],
                    int indexed, const unsigned char *colormap)
{
    unsigned long count, pixel;
    unsigned long *frame;
    float *depth;
    unsigned int i, j, k;
    double matrix[4][4];
    texsoft_ready = FALSE;
    if (width == 0 || height == 0 || width > 32767U || height > 32767U ||
        model == NULL || projection == NULL || texsoft_levels == 0)
        return 0;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
            if (!texsoft_finite(model[i][j]) || !texsoft_finite(projection[i][j]))
                return 0;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
        {
            matrix[i][j] = 0;
            for (k = 0; k < 4; ++k)
                matrix[i][j] += (double)model[i][k] * projection[k][j];
            if (!texsoft_finite(matrix[i][j])) return 0;
        }
    if (indexed && !texsoft_make_lut(colormap)) return 0;
    count = (unsigned long)width * height;
    if (count > (size_t)-1 / sizeof(unsigned long) || count > (size_t)-1 / sizeof(float))
        return 0;
    if (width != texsoft_width || height != texsoft_height)
    {
        /* The old viewport cannot serve this frame. Release it first so
         * low-memory resizes do not retain two complete frame/depth pairs. */
        texsoft_release_raster();
        frame = (unsigned long *)malloc((size_t)count * sizeof(unsigned long));
        depth = (float *)malloc((size_t)count * sizeof(float));
        if (frame == NULL || depth == NULL) { free(frame); free(depth); return 0; }
        texsoft_frame = frame; texsoft_depth = depth;
        texsoft_width = width; texsoft_height = height;
    }
    texsoft_indexed = indexed;
    memcpy(texsoft_matrix, matrix, sizeof(matrix));
    for (pixel = 0; pixel < count; ++pixel)
    {
        texsoft_frame[pixel] = indexed ? texsoft_lut[0] : 0xff000000UL;
        texsoft_depth[pixel] = 2.0f;
    }
    texsoft_ready = TRUE;
    return 1;
}

static double texsoft_plane(const Texsoft_vertex *v, int plane)
{
    return v->p[3] + ((plane & 1) ? -v->p[plane / 2] : v->p[plane / 2]);
}

static int texsoft_clip(const Texsoft_vertex *in, int count,
                          Texsoft_vertex *out, int plane)
{
    Texsoft_vertex a, b, intersection;
    double da, db, fraction;
    int i, j, total;
    a = in[count - 1]; da = texsoft_plane(&a, plane); total = 0;
    for (i = 0; i < count; ++i)
    {
        b = in[i]; db = texsoft_plane(&b, plane);
        if ((da >= 0) != (db >= 0))
        {
            fraction = da / (da - db);
            if (!texsoft_finite(fraction)) return 0;
            for (j = 0; j < 4; ++j) intersection.p[j] = a.p[j] + fraction * (b.p[j] - a.p[j]);
            intersection.u = a.u + fraction * (b.u - a.u);
            intersection.v = a.v + fraction * (b.v - a.v);
            for (j = 0; j < 4; ++j)
                if (!texsoft_finite(intersection.p[j])) return 0;
            if (!texsoft_finite(intersection.u) || !texsoft_finite(intersection.v))
                return 0;
            if (total >= TEXSOFT_CLIP) return 0;
            out[total++] = intersection;
        }
        if (db >= 0)
        {
            if (total >= TEXSOFT_CLIP) return 0;
            out[total++] = b;
        }
        a = b; da = db;
    }
    return total;
}

static void texsoft_sample(unsigned int level, double u, double v, double rgb[3])
{
    int x, y, x0, y0, x1, y1, channel;
    unsigned int width, height;
    unsigned long a, b, c, d;
    double sx, sy, fx, fy;
    width = texsoft_mip_w[level]; height = texsoft_mip_h[level];
    sx = (u - floor(u)) * width - 0.5; sy = (v - floor(v)) * height - 0.5;
    x = (int)floor(sx); y = (int)floor(sy); fx = sx - x; fy = sy - y;
    x0 = (x + (int)width) % (int)width; y0 = (y + (int)height) % (int)height;
    x1 = (x0 + 1) % (int)width; y1 = (y0 + 1) % (int)height;
    a = ((unsigned long)y0 * width + x0) * 3UL;
    b = ((unsigned long)y0 * width + x1) * 3UL;
    c = ((unsigned long)y1 * width + x0) * 3UL;
    d = ((unsigned long)y1 * width + x1) * 3UL;
    for (channel = 0; channel < 3; ++channel)
        rgb[channel] = (1.0 - fy) * ((1.0 - fx) * texsoft_mips[level][a + channel] + fx * texsoft_mips[level][b + channel]) +
                       fy * ((1.0 - fx) * texsoft_mips[level][c + channel] + fx * texsoft_mips[level][d + channel]);
}

static double texsoft_edge(const Texsoft_screen *a, const Texsoft_screen *b, double x, double y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static int texsoft_edge_inside(double edge, const Texsoft_screen *a, const Texsoft_screen *b)
{
    return edge > 0 || (edge == 0 && (b->y < a->y || (b->y == a->y && b->x < a->x)));
}

static void texsoft_raster(const Texsoft_screen *a, const Texsoft_screen *b, const Texsoft_screen *c)
{
    double area, minx, maxx, miny, maxy, ea, eb, ec, wa, wb, wc;
    double q, u, v, z, dux, duy, dvx, dvy, dqx, dqy, rho, lod, blend;
    double sx, sy, tx, ty, rgb[3], lower[3];
    unsigned int level, r, g, blue;
    unsigned long index;
    int x, y, left, right, bottom, top;
    area = texsoft_edge(a, b, c->x, c->y);
    if (!(area > 0) || !texsoft_finite(area)) return;
    minx = a->x; maxx = a->x; miny = a->y; maxy = a->y;
    if (b->x < minx) minx = b->x;
    if (c->x < minx) minx = c->x;
    if (b->x > maxx) maxx = b->x;
    if (c->x > maxx) maxx = c->x;
    if (b->y < miny) miny = b->y;
    if (c->y < miny) miny = c->y;
    if (b->y > maxy) maxy = b->y;
    if (c->y > maxy) maxy = c->y;
    left = (int)ceil(minx - 0.5); right = (int)floor(maxx - 0.5);
    bottom = (int)ceil(miny - 0.5); top = (int)floor(maxy - 0.5);
    if (left < 0) left = 0;
    if (bottom < 0) bottom = 0;
    if (right >= (int)texsoft_width) right = (int)texsoft_width - 1;
    if (top >= (int)texsoft_height) top = (int)texsoft_height - 1;
    dux = ((b->u - a->u) * (c->y - a->y) - (c->u - a->u) * (b->y - a->y)) / area;
    duy = ((b->x - a->x) * (c->u - a->u) - (c->x - a->x) * (b->u - a->u)) / area;
    dvx = ((b->v - a->v) * (c->y - a->y) - (c->v - a->v) * (b->y - a->y)) / area;
    dvy = ((b->x - a->x) * (c->v - a->v) - (c->x - a->x) * (b->v - a->v)) / area;
    dqx = ((b->q - a->q) * (c->y - a->y) - (c->q - a->q) * (b->y - a->y)) / area;
    dqy = ((b->x - a->x) * (c->q - a->q) - (c->x - a->x) * (b->q - a->q)) / area;
    if (!texsoft_finite(dux) || !texsoft_finite(duy) ||
        !texsoft_finite(dvx) || !texsoft_finite(dvy) ||
        !texsoft_finite(dqx) || !texsoft_finite(dqy)) return;
    for (y = bottom; y <= top; ++y)
        for (x = left; x <= right; ++x)
        {
            ea = texsoft_edge(b, c, x + 0.5, y + 0.5);
            eb = texsoft_edge(c, a, x + 0.5, y + 0.5);
            ec = texsoft_edge(a, b, x + 0.5, y + 0.5);
            if (!texsoft_edge_inside(ea, b, c) || !texsoft_edge_inside(eb, c, a) || !texsoft_edge_inside(ec, a, b)) continue;
            wa = ea / area; wb = eb / area; wc = ec / area;
            z = wa * a->z + wb * b->z + wc * c->z;
            if (!(z >= -FLT_MAX && z <= FLT_MAX)) continue;
            index = (unsigned long)y * texsoft_width + x;
            if ((float)z >= texsoft_depth[index]) continue;
            q = wa * a->q + wb * b->q + wc * c->q;
            if (!(q > 0) || !texsoft_finite(q)) continue;
            u = (wa * a->u + wb * b->u + wc * c->u) / q;
            v = (wa * a->v + wb * b->v + wc * c->v) / q;
            if (!texsoft_finite(u) || !texsoft_finite(v)) continue;
            sx = (dux - u * dqx) / q * texsoft_mip_w[0];
            sy = (dvx - v * dqx) / q * texsoft_mip_h[0];
            tx = (duy - u * dqy) / q * texsoft_mip_w[0];
            ty = (dvy - v * dqy) / q * texsoft_mip_h[0];
            if (!texsoft_finite(sx) || !texsoft_finite(sy) ||
                !texsoft_finite(tx) || !texsoft_finite(ty)) continue;
            rho = sx * sx + sy * sy; lod = tx * tx + ty * ty;
            if (lod > rho) rho = lod;
            lod = rho > 1.0 ? log(rho) * 0.7213475204444817 : 0.0;
            /* Infinite squared footprint means maximal minification. It
             * must select the final mip rather than reach an integer cast. */
            if (!texsoft_finite(lod)) lod = texsoft_levels - 1U;
            if (lod > texsoft_levels - 1U) lod = texsoft_levels - 1U;
            level = (unsigned int)lod; blend = lod - level;
            texsoft_sample(level, u, v, rgb);
            if (blend > 0 && level + 1U < texsoft_levels)
            {
                texsoft_sample(level + 1U, u, v, lower);
                rgb[0] += blend * (lower[0] - rgb[0]);
                rgb[1] += blend * (lower[1] - rgb[1]);
                rgb[2] += blend * (lower[2] - rgb[2]);
            }
            r = (unsigned int)(rgb[0] + 0.5); g = (unsigned int)(rgb[1] + 0.5); blue = (unsigned int)(rgb[2] + 0.5);
            texsoft_frame[index] = texsoft_indexed ? texsoft_lut[((r >> 3) << 10) | ((g >> 3) << 5) | (blue >> 3)] :
                                     0xff000000UL | ((unsigned long)blue << 16) | ((unsigned long)g << 8) | r;
            texsoft_depth[index] = (float)z;
        }
}

void texsoft_triangle(const Vertex *a, const Vertex *b, const Vertex *c,
                        const Coord *ua, const Coord *ub, const Coord *uc)
{
    const Vertex *vertices[3]; const Coord *coords[3];
    Texsoft_vertex first[TEXSOFT_CLIP], second[TEXSOFT_CLIP];
    Texsoft_screen screen[TEXSOFT_CLIP];
    double source[4]; int i, j, k, plane, count;
    if (a == NULL || b == NULL || c == NULL || ua == NULL || ub == NULL || uc == NULL ||
        !texsoft_ready || texsoft_frame == NULL || texsoft_depth == NULL ||
        texsoft_levels == 0) return;
    vertices[0] = a; vertices[1] = b; vertices[2] = c;
    coords[0] = ua; coords[1] = ub; coords[2] = uc;
    for (i = 0; i < 3; ++i)
    {
        source[0] = vertices[i]->x; source[1] = vertices[i]->y;
        source[2] = vertices[i]->z; source[3] = 1;
        if (!texsoft_finite(source[0]) || !texsoft_finite(source[1]) ||
            !texsoft_finite(source[2]) || !texsoft_finite(coords[i]->u) ||
            !texsoft_finite(coords[i]->v)) return;
        for (j = 0; j < 4; ++j)
        {
            first[i].p[j] = 0;
            for (k = 0; k < 4; ++k) first[i].p[j] += source[k] * texsoft_matrix[k][j];
            if (!texsoft_finite(first[i].p[j])) return;
        }
        first[i].u = coords[i]->u; first[i].v = coords[i]->v;
    }
    count = 3;
    for (plane = 0; plane < 6; plane += 2)
    {
        count = texsoft_clip(first, count, second, plane); if (count < 3) return;
        count = texsoft_clip(second, count, first, plane + 1); if (count < 3) return;
    }
    for (i = 0; i < count; ++i)
    {
        if (!(first[i].p[3] > 0)) return;
        screen[i].q = 1.0 / first[i].p[3];
        screen[i].x = (first[i].p[0] * screen[i].q + 1) * texsoft_width / 2.0;
        screen[i].y = (first[i].p[1] * screen[i].q + 1) * texsoft_height / 2.0;
        screen[i].z = first[i].p[2] * screen[i].q;
        screen[i].u = first[i].u * screen[i].q; screen[i].v = first[i].v * screen[i].q;
        if (!texsoft_finite(screen[i].q) || !texsoft_finite(screen[i].u) ||
            !texsoft_finite(screen[i].v) ||
            !(screen[i].x >= -1.0 && screen[i].x <= texsoft_width + 1.0) ||
            !(screen[i].y >= -1.0 && screen[i].y <= texsoft_height + 1.0) ||
            !(screen[i].z >= -1.000001 && screen[i].z <= 1.000001)) return;
    }
    for (i = 1; i < count - 1; ++i) texsoft_raster(&screen[0], &screen[i], &screen[i + 1]);
}

unsigned long *texsoft_pixels(void) { return texsoft_frame; }
#endif /* DRV_IGL || DRV_OGL */
