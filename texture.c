// testglobe - display 3d globe to test 3d hardware
// texture.c - generic texture support
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

#include <stdio.h>
#include <stdlib.h>
#include "texture.h"
#include "glob_ext.h"

#ifdef TEXTURE

unsigned char* texture_data;
unsigned int texture_size;
unsigned int texture_data_size;

void texture_generate(int size)
{
    int i;
    unsigned char *earth_data;
    unsigned char *texture_data_ptr;
    int texture_data_elements;
    
    texture_size = size;
    
    switch (size)
    {
#ifdef EARTH_32
        case 32:
            earth_data = earth_data_32;
            break;
#endif
#ifdef EARTH_64
        case 64:
            earth_data = earth_data_64;
            break;
#endif
#ifdef EARTH_128
        case 128:
            earth_data = earth_data_128;
            break;
#endif
#ifdef EARTH_256
        case 256:
            earth_data = earth_data_256;
            break;
#endif
#ifdef EARTH_512
        case 512:
            earth_data = earth_data_512;
            break;
#endif
        default:
            error("Invalid texture size");
    }
    
#ifdef TEXTURE_RGB
    texture_data_elements = size * (size/2);
    texture_data_size = texture_data_elements * 3;
    texture_data = malloc(texture_data_size);
    if (texture_data == NULL)
    {
        error("Could not allocate memory for texture!");
    }
    texture_data_ptr = texture_data;
    
    for (i=0; i<texture_data_elements; i++)
    {
        *texture_data_ptr = earth_cmap[earth_data[i]][0];
        texture_data_ptr++;
        *texture_data_ptr = earth_cmap[earth_data[i]][1];
        texture_data_ptr++;
        *texture_data_ptr = earth_cmap[earth_data[i]][2];
        texture_data_ptr++;
    }
#endif
}

void texture_init(void)
{
    texture_data = NULL;
    texture_size = 0;
    texture_data_size = 0;
}

void texture_free(void)
{
    if (texture_data != NULL)
    {
        free(texture_data);
        texture_data = NULL;
    }
}


#endif
