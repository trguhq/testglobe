/* Perspective-correct textured rasterization for palette OpenGL and IRIS GL.
 * Drivers supply their native model/view/projection matrices and present the
 * raster. Normal flat drawing and supported texture paths remain native.
 * Copyright (C) 2026 trguhq. SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "testglob.h"
#if defined(DRV_IGL) || defined(DRV_OGL)
#include "texsoft.h"

#define TEXSOFT_LEVELS 10
#define TEXSOFT_CLIP 12
typedef struct Igl_soft_vertex { double p[4], u, v; } Igl_soft_vertex;
typedef struct Igl_soft_screen { double x, y, z, q, u, v; } Igl_soft_screen;
static unsigned char *texsoft_mips[TEXSOFT_LEVELS];
static unsigned int texsoft_mip_w[TEXSOFT_LEVELS];
static unsigned int texsoft_mip_h[TEXSOFT_LEVELS];
static unsigned int texsoft_levels, texsoft_width, texsoft_height;
static unsigned long *texsoft_frame;
static float *texsoft_depth;
static double texsoft_matrix[4][4];
static int texsoft_indexed;
static unsigned char *texsoft_lut;
static unsigned char texsoft_palette[768];

void texsoft_free(void)
{
    unsigned int i;
    for (i = 0; i < TEXSOFT_LEVELS; ++i)
    {
        free(texsoft_mips[i]);
        texsoft_mips[i] = NULL;
    }
    free(texsoft_frame); free(texsoft_depth); free(texsoft_lut);
    texsoft_frame = NULL; texsoft_depth = NULL; texsoft_lut = NULL;
    texsoft_width = texsoft_height = texsoft_levels = 0;
}

int texsoft_texture(unsigned int width, unsigned int height,
                      const unsigned char *rgb)
{
    unsigned int i, level, w, h, x, y, channel, px, py;
    unsigned int sum, dx, dy;
    unsigned long offset;
    if (rgb == NULL || width == 0 || height == 0 || width > 512 || height > 512 ||
        (width & (width - 1U)) != 0 || (height & (height - 1U)) != 0)
        return 0;
    for (i = 0; i < TEXSOFT_LEVELS; ++i)
    {
        free(texsoft_mips[i]); texsoft_mips[i] = NULL;
    }
    texsoft_levels = 0;
    w = width; h = height;
    for (level = 0; level < TEXSOFT_LEVELS; ++level)
    {
        texsoft_mip_w[level] = w; texsoft_mip_h[level] = h;
        texsoft_mips[level] = (unsigned char *)malloc((size_t)w * h * 3U);
        if (texsoft_mips[level] == NULL) return 0;
        if (level == 0) memcpy(texsoft_mips[0], rgb, (size_t)w * h * 3U);
        else
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
        if (w == 1 && h == 1) return 1;
        if (w > 1) w /= 2U;
        if (h > 1) h /= 2U;
    }
    return 0;
}

static int texsoft_make_lut(const unsigned char *palette)
{
    unsigned int key, color, best;
    long r, g, b, dr, dg, db, distance, closest;
    if (palette == NULL) return 0;
    if (texsoft_lut != NULL && memcmp(palette, texsoft_palette, 768) == 0) return 1;
    if (texsoft_lut == NULL) texsoft_lut = (unsigned char *)malloc(32768U);
    if (texsoft_lut == NULL) return 0;
    memcpy(texsoft_palette, palette, 768);
    for (key = 0; key < 32768U; ++key)
    {
        r = (long)((key >> 10) & 31U) * 255L / 31L;
        g = (long)((key >> 5) & 31U) * 255L / 31L;
        b = (long)(key & 31U) * 255L / 31L;
        closest = 195076L; best = 0;
        for (color = 0; color < 256; ++color)
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
    if (width == 0 || height == 0 || width > 32767U || height > 32767U ||
        model == NULL || projection == NULL || texsoft_levels == 0)
        return 0;
    count = (unsigned long)width * height;
    if (count > (size_t)-1 / sizeof(unsigned long) || count > (size_t)-1 / sizeof(float))
        return 0;
    if (width != texsoft_width || height != texsoft_height)
    {
        frame = (unsigned long *)malloc((size_t)count * sizeof(unsigned long));
        depth = (float *)malloc((size_t)count * sizeof(float));
        if (frame == NULL || depth == NULL) { free(frame); free(depth); return 0; }
        free(texsoft_frame); free(texsoft_depth);
        texsoft_frame = frame; texsoft_depth = depth;
        texsoft_width = width; texsoft_height = height;
    }
    texsoft_indexed = indexed;
    if (indexed && !texsoft_make_lut(colormap)) return 0;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
        {
            texsoft_matrix[i][j] = 0;
            for (k = 0; k < 4; ++k)
                texsoft_matrix[i][j] += (double)model[i][k] * projection[k][j];
        }
    for (pixel = 0; pixel < count; ++pixel)
    {
        texsoft_frame[pixel] = indexed ? texsoft_lut[0] : 0xff000000UL;
        texsoft_depth[pixel] = 2.0f;
    }
    return 1;
}

static double texsoft_plane(const Igl_soft_vertex *v, int plane)
{
    return v->p[3] + ((plane & 1) ? -v->p[plane / 2] : v->p[plane / 2]);
}

static int texsoft_clip(const Igl_soft_vertex *in, int count,
                          Igl_soft_vertex *out, int plane)
{
    Igl_soft_vertex a, b, intersection;
    double da, db, fraction;
    int i, j, total;
    a = in[count - 1]; da = texsoft_plane(&a, plane); total = 0;
    for (i = 0; i < count; ++i)
    {
        b = in[i]; db = texsoft_plane(&b, plane);
        if ((da >= 0) != (db >= 0))
        {
            fraction = da / (da - db);
            for (j = 0; j < 4; ++j) intersection.p[j] = a.p[j] + fraction * (b.p[j] - a.p[j]);
            intersection.u = a.u + fraction * (b.u - a.u);
            intersection.v = a.v + fraction * (b.v - a.v);
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

static double texsoft_edge(const Igl_soft_screen *a, const Igl_soft_screen *b, double x, double y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static int texsoft_edge_inside(double edge, const Igl_soft_screen *a, const Igl_soft_screen *b)
{
    return edge > 0 || (edge == 0 && (b->y < a->y || (b->y == a->y && b->x < a->x)));
}

static void texsoft_raster(const Igl_soft_screen *a, const Igl_soft_screen *b, const Igl_soft_screen *c)
{
    double area, minx, maxx, miny, maxy, ea, eb, ec, wa, wb, wc;
    double q, u, v, z, dux, duy, dvx, dvy, dqx, dqy, rho, lod, blend;
    double sx, sy, tx, ty, rgb[3], lower[3];
    unsigned int level, r, g, blue;
    unsigned long index;
    int x, y, left, right, bottom, top;
    area = texsoft_edge(a, b, c->x, c->y);
    if (!(area > 0)) return;
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
    for (y = bottom; y <= top; ++y)
        for (x = left; x <= right; ++x)
        {
            ea = texsoft_edge(b, c, x + 0.5, y + 0.5);
            eb = texsoft_edge(c, a, x + 0.5, y + 0.5);
            ec = texsoft_edge(a, b, x + 0.5, y + 0.5);
            if (!texsoft_edge_inside(ea, b, c) || !texsoft_edge_inside(eb, c, a) || !texsoft_edge_inside(ec, a, b)) continue;
            wa = ea / area; wb = eb / area; wc = ec / area;
            z = wa * a->z + wb * b->z + wc * c->z;
            index = (unsigned long)y * texsoft_width + x;
            if ((float)z >= texsoft_depth[index]) continue;
            q = wa * a->q + wb * b->q + wc * c->q;
            if (!(q > 0)) continue;
            u = (wa * a->u + wb * b->u + wc * c->u) / q;
            v = (wa * a->v + wb * b->v + wc * c->v) / q;
            sx = (dux - u * dqx) / q * texsoft_mip_w[0];
            sy = (dvx - v * dqx) / q * texsoft_mip_h[0];
            tx = (duy - u * dqy) / q * texsoft_mip_w[0];
            ty = (dvy - v * dqy) / q * texsoft_mip_h[0];
            rho = sx * sx + sy * sy; lod = tx * tx + ty * ty;
            if (lod > rho) rho = lod;
            lod = rho > 1.0 ? log(rho) * 0.7213475204444817 : 0.0;
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
    Igl_soft_vertex first[TEXSOFT_CLIP], second[TEXSOFT_CLIP];
    Igl_soft_screen screen[TEXSOFT_CLIP];
    double source[4]; int i, j, k, plane, count;
    if (a == NULL || b == NULL || c == NULL || ua == NULL || ub == NULL || uc == NULL ||
        texsoft_frame == NULL || texsoft_levels == 0) return;
    vertices[0] = a; vertices[1] = b; vertices[2] = c;
    coords[0] = ua; coords[1] = ub; coords[2] = uc;
    for (i = 0; i < 3; ++i)
    {
        source[0] = vertices[i]->x; source[1] = vertices[i]->y;
        source[2] = vertices[i]->z; source[3] = 1;
        for (j = 0; j < 4; ++j)
        {
            first[i].p[j] = 0;
            for (k = 0; k < 4; ++k) first[i].p[j] += source[k] * texsoft_matrix[k][j];
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
    }
    for (i = 1; i < count - 1; ++i) texsoft_raster(&screen[0], &screen[i], &screen[i + 1]);
}

unsigned long *texsoft_pixels(void) { return texsoft_frame; }
#endif /* DRV_IGL || DRV_OGL */
