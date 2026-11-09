/* Independent front-face oracle for the production globe traversals.
 * The fixture deliberately checks every source triangle; production culling
 * must select longitude ranges and only classify their uncertain boundaries. */
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earth_reference.h"
#include "testglob.h"
#include "drv.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "globe.h"

#ifndef DRV_CULL
#error This fixture requires the CPU pre-culling capability
#endif

extern Vertex TG_HUGE *globe_verts;
extern float globe_radius;

typedef struct Reference_triangle
{
    unsigned long vertex[3], face;
    double normal[3];
    unsigned char expected, seen;
} Reference_triangle;

static Reference_triangle *reference;
static unsigned long *lookup;
static unsigned long lookup_mask, reference_count, submitted;
static unsigned char *map;
static unsigned long primitive_calls;
static int textured;

static int same_vertex(const Vertex *a, const Vertex *b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

static int vertex_order(const Vertex *a, const Vertex *b)
{
    if (a->x != b->x) return a->x < b->x ? -1 : 1;
    if (a->y != b->y) return a->y < b->y ? -1 : 1;
    if (a->z != b->z) return a->z < b->z ? -1 : 1;
    return 0;
}

static int canonical_corner(const Vertex *a, const Vertex *b, const Vertex *c)
{
    if (vertex_order(a, b) <= 0 && vertex_order(a, c) <= 0) return 0;
    return vertex_order(b, c) <= 0 ? 1 : 2;
}

/* Hash coordinates, not pointer identity: strips borrow a scratch packet.
 * Normalize signed zero so coincident pole vertices share a geometry key. */
static unsigned long triangle_hash(const Vertex *v[3], int first)
{
    float coordinate[3];
    unsigned char bytes[sizeof(float)];
    unsigned long hash;
    unsigned int i, axis, byte;
    hash = 2166136261UL;
    for (i = 0; i < 3U; ++i)
    {
        coordinate[0] = v[(first + i) % 3U]->x;
        coordinate[1] = v[(first + i) % 3U]->y;
        coordinate[2] = v[(first + i) % 3U]->z;
        for (axis = 0; axis < 3U; ++axis)
        {
            if (coordinate[axis] == 0.0f) coordinate[axis] = 0.0f;
            memcpy(bytes, coordinate + axis, sizeof(float));
            for (byte = 0; byte < sizeof(float); ++byte)
                hash = ((hash ^ bytes[byte]) * 16777619UL) & 0xffffffffUL;
        }
    }
    return hash & lookup_mask;
}

static void add_reference(unsigned long a, unsigned long b, unsigned long c,
                          unsigned long face)
{
    Reference_triangle *triangle;
    const Vertex *v[3];
    unsigned long indices[3], slot;
    double ux, uy, uz, vx, vy, vz;
    int first, i;
    indices[0] = a; indices[1] = b; indices[2] = c;
    for (i = 0; i < 3; ++i) v[i] = globe_verts + indices[i];
    first = canonical_corner(v[0], v[1], v[2]);
    triangle = reference + reference_count;
    for (i = 0; i < 3; ++i) triangle->vertex[i] = indices[(first + i) % 3];
    triangle->face = face;
    ux = (double)v[1]->x - v[0]->x;
    uy = (double)v[1]->y - v[0]->y;
    uz = (double)v[1]->z - v[0]->z;
    vx = (double)v[2]->x - v[0]->x;
    vy = (double)v[2]->y - v[0]->y;
    vz = (double)v[2]->z - v[0]->z;
    triangle->normal[0] = uy * vz - uz * vy;
    triangle->normal[1] = uz * vx - ux * vz;
    triangle->normal[2] = ux * vy - uy * vx;
    slot = triangle_hash(v, first);
    while (lookup[slot] != ULONG_MAX) slot = (slot + 1UL) & lookup_mask;
    lookup[slot] = reference_count++;
}

static void build_reference(void)
{
    unsigned long slots, top, bottom, face;
    int row, column;
    free(reference); free(lookup);
    reference = (Reference_triangle *)calloc((size_t)globe_tris_num,
                                             sizeof(*reference));
    assert(reference != NULL);
    slots = 1UL;
    while (slots < globe_tris_num * 2UL) slots *= 2UL;
    lookup = (unsigned long *)malloc((size_t)slots * sizeof(*lookup));
    assert(lookup != NULL);
    lookup_mask = slots - 1UL;
    for (top = 0; top < slots; ++top) lookup[top] = ULONG_MAX;
    reference_count = 0UL;
    for (row = 0; row < globe_latitude; ++row)
        for (column = 0; column < globe_longitude; ++column)
        {
            top = (unsigned long)row * (globe_longitude + 1UL) + column;
            bottom = top + globe_longitude + 1UL;
            face = (unsigned long)row * globe_longitude + column;
            if (row != 0) add_reference(top, bottom, top + 1UL, face);
            if (row != globe_latitude - 1)
                add_reference(top + 1UL, bottom, bottom + 1UL, face);
        }
    assert(reference_count == globe_tris_num);
    map = test_earth_data(globe_longitude);
}

static unsigned char reference_color(unsigned long face)
{
    unsigned long row, column, source_row;
    row = face / globe_longitude;
    column = face % globe_longitude;
    source_row = ((2UL * row + 1UL) * (globe_longitude / 2UL)) /
                 (2UL * globe_latitude);
    return map[source_row * globe_longitude + column];
}

static void record_triangle(const Vertex *a, const Vertex *b, const Vertex *c,
                            const Coord *ua, const Coord *ub, const Coord *uc,
                            const Color_RGB *rgb, unsigned char color)
{
    const Vertex *v[3];
    const Coord *uv[3];
    Reference_triangle *triangle;
    unsigned long slot, attempts, index;
    unsigned char expected_color;
    int first, corner, equal;
    /* A restart may use a repeated vertex to restore original strip parity. */
    if (same_vertex(a, b) || same_vertex(a, c) || same_vertex(b, c)) return;
    v[0] = a; v[1] = b; v[2] = c;
    uv[0] = ua; uv[1] = ub; uv[2] = uc;
    first = canonical_corner(a, b, c);
    slot = triangle_hash(v, first);
    triangle = NULL;
    for (attempts = 0; attempts <= lookup_mask; ++attempts)
    {
        assert(lookup[slot] != ULONG_MAX);
        triangle = reference + lookup[slot];
        equal = TRUE;
        for (corner = 0; corner < 3; ++corner)
            if (!same_vertex(v[(first + corner) % 3],
                             globe_verts + triangle->vertex[corner])) equal = FALSE;
        if (equal) break;
        slot = (slot + 1UL) & lookup_mask;
    }
    assert(attempts <= lookup_mask && triangle != NULL);
    if (triangle->seen || (drv_cull_enabled && triangle->expected == 0U))
    {
        fprintf(stderr, "Invalid culled triangle: face %lu, seen %u, expected %u, pose %.9g/%.9g, width %d\n",
                triangle->face, triangle->seen, triangle->expected,
                (double)drv_rot_x, (double)drv_rot_y, globe_longitude);
        abort();
    }
    triangle->seen = 1U;
    expected_color = reference_color(triangle->face);
    if (!textured)
    {
        assert(ua == NULL && ub == NULL && uc == NULL);
        if (rgb != NULL)
        {
            assert(rgb->r == earth_cmap[expected_color][0]);
            assert(rgb->g == earth_cmap[expected_color][1]);
            assert(rgb->b == earth_cmap[expected_color][2]);
        }
        else assert(color == expected_color);
    }
    else
        for (corner = 0; corner < 3; ++corner)
        {
            index = triangle->vertex[corner];
            assert(uv[(first + corner) % 3] != NULL);
            assert(uv[(first + corner) % 3]->u ==
                   (float)(index % (globe_longitude + 1UL)) / globe_longitude);
            assert(uv[(first + corner) % 3]->v ==
                   (float)(index / (globe_longitude + 1UL)) / globe_latitude);
        }
    ++submitted;
}

#ifdef DRV_TRI_COLOR
void drv_draw_tri_flat_color(Vertex *a, Vertex *b, Vertex *c, unsigned char color)
{
    ++primitive_calls;
    record_triangle(a, b, c, NULL, NULL, NULL, NULL, color);
}
#endif
#ifdef DRV_TRI_RGB
void drv_draw_tri_flat_rgb(Vertex *a, Vertex *b, Vertex *c, Color_RGB *color)
{
    ++primitive_calls;
    record_triangle(a, b, c, NULL, NULL, NULL, color, 0);
}
#endif
#ifdef DRV_TRI_UV
void drv_draw_tri_flat_uv(Vertex *a, Vertex *b, Vertex *c,
                          Coord *ua, Coord *ub, Coord *uc)
{
    ++primitive_calls;
    record_triangle(a, b, c, ua, ub, uc, NULL, 0);
}
#endif

#ifdef TRISTRIP
static void record_strip(unsigned int count, const Vertex *vertices,
                          const unsigned char *colors, const Color_RGB *rgb,
                          const Coord *coords)
{
    unsigned int i, first, second;
    assert(count >= 3U && count <= 2U * (globe_longitude + 1U));
    ++primitive_calls;
    for (i = 0; i + 2U < count; ++i)
    {
        first = (i & 1U) ? i + 1U : i;
        second = (i & 1U) ? i : i + 1U;
        record_triangle(vertices + first, vertices + second, vertices + i + 2U,
                        coords == NULL ? NULL : coords + first,
                        coords == NULL ? NULL : coords + second,
                        coords == NULL ? NULL : coords + i + 2U,
                        rgb == NULL ? NULL : rgb + i,
                        colors == NULL ? 0U : colors[i]);
    }
}
#ifdef DRV_COLORMAP
void drv_draw_tristrip_flat_color(unsigned int count, const Vertex *vertices,
                                  const unsigned char *colors)
{
    assert(!textured);
    record_strip(count, vertices, colors, NULL, NULL);
}
#else
void drv_draw_tristrip_flat_rgb(unsigned int count, const Vertex *vertices,
                                const Color_RGB *colors)
{
    assert(!textured);
    record_strip(count, vertices, NULL, colors, NULL);
}
#endif
void drv_draw_tristrip_flat_uv(unsigned int count, const Vertex *vertices,
                               const Coord *coords)
{
    assert(textured);
    record_strip(count, vertices, NULL, NULL, coords);
}
#endif

void drv_texture_enable(void) { drv_texture_enabled = drv_texture_active = TRUE; }
void drv_texture_disable(void) { drv_texture_enabled = drv_texture_active = FALSE; }
void drv_release_optional(void) {}

/* Invert the application's model rotations independently. The two fixed
 * right-angle rotations use exact coordinate swaps, avoiding sin(PI/2) noise. */
static void reference_eye(double eye[3])
{
    double x, y, z, angle, old_x;
    x = 0.0; y = 0.0; z = (double)GLOBE_VIEW_DISTANCE;
    angle = -(double)drv_rot_x * PI / 180.0;
    y = -sin(angle) * z;
    z = cos(angle) * z;
    angle = -(double)drv_rot_y * PI / 180.0;
    x = sin(angle) * z;
    z = cos(angle) * z;
    old_x = x;
    x = z;
    z = y;
    y = old_x;
    eye[0] = x; eye[1] = y; eye[2] = z;
}

static void check_frame(int cull, int texture)
{
    double eye[3], facing, scale, tolerance;
    const Vertex *a;
    Reference_triangle *triangle;
    unsigned long i, expected, ambiguous;
    int axis;
    drv_cull_enabled = cull;
    textured = texture;
    drv_texture_enabled = drv_texture_active = texture;
    drv_frame_failed = FALSE;
    submitted = primitive_calls = expected = ambiguous = 0UL;
    reference_eye(eye);
    for (i = 0; i < reference_count; ++i)
    {
        triangle = reference + i;
        triangle->seen = 0U;
        a = globe_verts + triangle->vertex[0];
        facing = triangle->normal[0] * (eye[0] - a->x) +
                 triangle->normal[1] * (eye[1] - a->y) +
                 triangle->normal[2] * (eye[2] - a->z);
        scale = 0.0;
        for (axis = 0; axis < 3; ++axis)
            scale += fabs(triangle->normal[axis]) * fabs(eye[axis]);
        scale += fabs(triangle->normal[0] * a->x) +
                 fabs(triangle->normal[1] * a->y) +
                 fabs(triangle->normal[2] * a->z);
        tolerance = 32.0 * DBL_EPSILON * scale;
        triangle->expected = facing > tolerance ? 1U :
                             (facing < -tolerance ? 0U : 2U);
        if (triangle->expected == 1U) ++expected;
        if (triangle->expected == 2U) ++ambiguous;
    }
#ifdef TRI
    globe_draw_tris();
#else
#ifdef DRV_TRI_CAPS
    globe_draw_pole_tris();
#endif
    globe_draw_tristrips();
#endif
    assert(!drv_frame_failed);
    for (i = 0; i < reference_count; ++i)
        if ((!cull || reference[i].expected == 1U) && !reference[i].seen)
        {
            fprintf(stderr, "Missing front triangle: face %lu, pose %.9g/%.9g, width %d, radius %.9g\n",
                    reference[i].face, (double)drv_rot_x, (double)drv_rot_y,
                    globe_longitude, (double)globe_radius);
            abort();
        }
    if (!cull) assert(submitted == globe_tris_num);
    else assert(submitted >= expected && submitted <= expected + ambiguous);
}

static void check_scene(int width, int height, float radius)
{
    static const float poses[][2] = {
        { 0, 180 }, { 0, 0 }, { 0, 359.999f }, { 0, 0.001f },
        { 19, 37 }, { 71, 302 }, { 90, 0 }, { -90, 0 },
        { 89.999f, 123 }, { -89.999f, 23 }, { 247, 121 },
        { 180, 180 }, { -37, -721 }
    };
    unsigned int pose;
    double a, b, plane, length, pitch;
    int row, texture;
    assert(globe_init_verts(width, height, radius));
    build_reference();
    for (texture = 0; texture < 2; ++texture)
    {
        check_frame(FALSE, texture);
        for (pose = 0; pose < sizeof(poses) / sizeof(poses[0]); ++pose)
        {
            drv_rot_x = poses[pose][0]; drv_rot_y = poses[pose][1];
            check_frame(TRUE, texture);
        }
        /* Aim at a polygon plane tangent, not just the smooth sphere limb. */
        row = globe_latitude / 3;
        a = (double)globe_verts[row * (width + 1)].z -
            globe_verts[(row + 1) * (width + 1)].z;
        b = ((double)globe_verts[(row + 1) * (width + 1)].x -
              globe_verts[row * (width + 1)].x) * cos(PI / width);
        plane = a * globe_verts[row * (width + 1)].x * cos(PI / width) +
                b * globe_verts[row * (width + 1)].z;
        length = sqrt(a * a + b * b);
        if (length > 0.0 && fabs(plane / length) < GLOBE_VIEW_DISTANCE)
        {
            pitch = atan2(b, a) + acos(plane / (length * GLOBE_VIEW_DISTANCE));
            drv_rot_y = (float)(-180.0 / width);
            for (pose = 0; pose < 3U; ++pose)
            {
                drv_rot_x = (float)(pitch * 180.0 / PI + ((int)pose - 1) * 0.0005);
                check_frame(TRUE, texture);
            }
        }
    }
}

int main(void)
{
    int width;
    char stats[128];
    drv_ext_init();
    globe_init();
    assert(!drv_cull_enabled);
    for (width = 32; width <= 512; width *= 2)
        check_scene(width, -1, GLOBE_RADIUS);
    check_scene(32, 2, 100.0f);
    check_scene(32, 8, 2999.0f);
    check_scene(32, 16, 3500.0f);
    drv_cull_enabled = FALSE;
    drv_fps_average = 100.0f;
    assert(drv_handle_key('c') && drv_cull_enabled && drv_fps_average == 0.0f);
    drv_format_stats(stats);
    assert(strstr(stats, "cull on") != NULL && strstr(stats, "poly/s") == NULL);
    assert(drv_handle_key('C') && !drv_cull_enabled);
    drv_format_stats(stats);
    assert(strstr(stats, "cull off") != NULL);
    free(reference); free(lookup);
    globe_free();
    puts("PASS band culling: independent front-face oracle, all resolutions, strip parity, exact colors/UVs, seams/poles/tangencies, custom radii/latitudes, retained TRI and C controls");
    return 0;
}
