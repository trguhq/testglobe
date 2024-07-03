// testglobe - display 3d globe to test 3d hardware
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
#include <math.h>
#include "testglob.h"
#include "globe.h"
#include "drv_ext.h"
#include "types.h"

// data
#ifdef EARTH
#include "e_cmap.h"
#ifdef EARTH_32
#include "e_32.h"
#endif
#ifdef EARTH_64
#include "e_64.h"
#endif
#ifdef EARTH_128
#include "e_128.h"
#endif
#ifdef EARTH_256
#include "e_256.h"
#endif
#ifdef EARTH_512
#include "e_512.h"
#endif
#endif

typedef unsigned char cmap[256][3];
Vertex *globe_verts;
Color_RGB *globe_colors;

float globe_radius;

int globe_verts_num;
int globe_faces_num;
int globe_tris_num;
int globe_longs;
int globe_lats;
static unsigned char *globe_data;
static cmap *globe_cmap;

void globe_init(void)
{
    globe_verts = NULL;
    globe_colors = NULL;
    globe_radius = 0;
    globe_verts_num = 0;
    globe_faces_num = 0;
    globe_tris_num = 0;
    globe_longs = 0;
    globe_lats = 0;
}

void globe_free(void)
{
    if (globe_verts != NULL)
    {
        free(globe_verts);
        globe_verts = NULL;
    }
    if (globe_colors != NULL)
    {
        free(globe_colors);
        globe_colors = NULL;
    }

    globe_init();
}

int globe_alloc(void)
{
    globe_verts = (void *) malloc(globe_verts_num * sizeof(struct Vertex));
    if (globe_verts == NULL)
        error("Couldn't allocate memory for vertices!\n");
    globe_colors = (void *) malloc(globe_tris_num * sizeof(struct Color_RGB));
    if (globe_colors == NULL)
        error("Couldn't allocate memory for colors!\n");
    
    return 1;
}

inline void lookup_color()
{
    
}

void globe_calc_colors(void)
{
    int height;
    int width;
    int i;
    int j;
    Color_RGB *color_ptr;
    unsigned char val;
    unsigned char *data_ptr;

    switch (globe_longs)
    {
        case 32:
            globe_data = earth_data_32;
            break;
        case 64:
            globe_data = earth_data_64;
            break;
        case 128:
            globe_data = earth_data_128;
            break;
        case 256:
            globe_data = earth_data_256;
            break;
        case 512:
            globe_data = earth_data_512;
            break;
        default:
            error("Invalid globe resolution (longitude), must be 32, 64, 128, 256 or 512");
    }
    
    globe_cmap = &earth_cmap;
    width = globe_longs;
    height = globe_lats;
    
    color_ptr = globe_colors;
    data_ptr = globe_data;

    for (i=0; i<width; i++)
    {
        for(j=0; j<height; j++)
        {
            val=*data_ptr;
            color_ptr->r = (*globe_cmap)[val][0];
            color_ptr->g = (*globe_cmap)[val][1];
            color_ptr->b = (*globe_cmap)[val][2];
            data_ptr++;
            color_ptr++;
        }
    }
}

int globe_init_verts(int longs, int lats, float radius)
{
    int i, j;
    float u, v;
    float x, y, z, xy;
    float long_step, lat_step;
    float long_angle, lat_angle;
    int verts_added;
    
    Vertex *vertex_ptr;
       
    globe_free();
    
    globe_radius = radius;
    globe_longs = longs;
    if (lats<1) globe_lats = globe_longs/2;
    else globe_lats = lats;
    globe_verts_num = (globe_lats+1)*(globe_longs+1);
    globe_faces_num = globe_longs*globe_lats;
    globe_tris_num = ((globe_lats-2)*globe_longs*2)+(2*globe_longs);

    printf("Globe resolution: %i x %i\n", globe_longs, globe_lats);
    printf("Vertices: %i\n", globe_verts_num);
    printf("Triangles: %i\n", globe_tris_num);
    
    
    globe_alloc();

    vertex_ptr = globe_verts;
    verts_added = 0;
    
    long_step = 2.0f*PI/globe_longs;
    lat_step = PI/globe_lats;
    for(i=0; i<=globe_lats; ++i)
    {
        lat_angle = PI/2.0f-i*lat_step;
        xy = globe_radius * cosf(lat_angle);
        z = globe_radius * sinf(lat_angle);

        for(j = 0; j<=globe_longs; ++j)
        {
            long_angle = j*long_step;

            x = xy*cosf(long_angle);
            y = xy*sinf(long_angle);
            vertex_ptr->x = x;
            vertex_ptr->y = y;
            vertex_ptr->z = z;
            vertex_ptr++;
            
            u = (float)j/globe_longs;
            v = (float)i/globe_lats;
            // calc and store texture coords or colors or per vertex colors

        }
    }
    
    globe_calc_colors();
    
    return 1;
}

void globe_draw_tris(void)
{
    Color_RGB *color_ptr;
    
    Vertex *v1, *v2, *v1_1, *v2_1;
    int i,j;
    color_ptr = globe_colors;
    
    for(i=0; i<globe_lats; i++)
    {
        v1 = globe_verts + (i * (globe_longs + 1));
        v1_1 = v1 + 1;
        v2 = v1 + globe_longs + 1;
        v2_1 = v2 + 1;
        for(j=0; j<globe_longs; j++)
        {
            if(i != 0)
            {
                drv_draw_tri_flat_rgb(v1, v2, v1_1, color_ptr);
            }
            
            if(i != globe_lats-1)
            {
                drv_draw_tri_flat_rgb(v1_1, v2, v2_1, color_ptr);
            }
            color_ptr++;
            
            v1++;
            v1_1++;
            v2++;
            v2_1++;
        }
    }
}

void globe_toggle_res(char key)
{
    int new_longs;
    
    new_longs = 512;
    
    switch (key)
    {
        case '1':
            new_longs = 32;
            break;
        case '2':
            new_longs = 64;
            break;
        case '3':
            new_longs = 128;
            break;
        case '4':
            new_longs = 256;
            break;
        case '5':
        default:
            new_longs = 512;
            break;
    }
    
    globe_free();
    globe_init_verts(new_longs, -1, GLOBE_RADIUS);
}
