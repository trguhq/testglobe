/*
 * testglobe - display 3d globe to test 3d hardware
 * texture.c - generic texture support
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
#include "tex_ext.h"
#include "e_ext.h"

#ifdef TEXTURE

unsigned char* texture_data;
unsigned int texture_size;
static size_t texture_data_size;

/* Return the cause of failure so callers retry only resource/capacity errors. */
Drv_result texture_build(int size)
{
#ifdef TEXTURE_RGB
    unsigned long i, elements;
    unsigned int count, j;
    unsigned char data_row[512];
    size_t bytes;
    unsigned char *new_data, *destination;
#endif
    
    if (earth_data_size(size) == 0UL) return DRV_BAD_INPUT;
    
#ifdef TEXTURE_RGB
    elements = (unsigned long)size * ((unsigned int)size / 2U);
    if ((unsigned int)size > TEXTURE_MAX_SIZE ||
        elements > (size_t)-1 / 3U)
    {
        return DRV_UNAVAILABLE;
    }
    bytes = (size_t)elements * 3U;
    /* Regeneration at the same dimensions can reuse its owned staging
     * image. Size changes allocate first to preserve the previous image
     * if a replacement cannot be obtained. */
    new_data = texture_data_size == bytes ? texture_data : NULL;
    if (new_data == NULL) new_data = (unsigned char *)malloc(bytes);
    if (new_data == NULL)
    {
        return DRV_NO_MEMORY;
    }
    destination = new_data;
    for (i = 0; i < elements; i += count)
    {
        count = (unsigned int)size;
        /* The validated dimensions and generated chunk sizes guarantee each
         * row is readable. No second full indexed image is allocated. */
        if (!earth_read_data(size, i, count, data_row))
        {
            if (new_data != texture_data) free(new_data);
            else texture_free();
            return DRV_BAD_DATA;
        }
        for (j = 0; j < count; ++j)
        {
            *destination++ = earth_cmap[data_row[j]][0];
            *destination++ = earth_cmap[data_row[j]][1];
            *destination++ = earth_cmap[data_row[j]][2];
        }
    }
    /* Allocation failure preserves an existing image. A read failure while
     * reusing that image releases its now-partial bytes before returning. */
    if (texture_data != new_data) free(texture_data);
    texture_data = new_data;
    texture_data_size = bytes;
    texture_size = (unsigned int)size;
    return DRV_OK;
#else
    return DRV_UNAVAILABLE;
#endif
}

void texture_generate(int size)
{
    Drv_result result;
    result = texture_build(size);
    switch (result)
    {
        case DRV_OK: break;
        case DRV_NO_MEMORY: error("Could not allocate memory for texture!"); break;
        case DRV_BAD_DATA: error("Could not read embedded Earth data"); break;
        case DRV_BAD_INPUT: error("Invalid texture size"); break;
        default: error("Texture image is too large or RGB format is unavailable"); break;
    }
}

int texture_try_generate(int size)
{
    return texture_build(size) == DRV_OK;
}

unsigned char *texture_take_data(void)
{
    unsigned char *data;
    data = texture_data;
    texture_data = NULL;
    texture_data_size = 0;
    return data;
}

void texture_init(void)
{
    texture_free();
    texture_size = 0;
    texture_data_size = 0;
}

void texture_free(void)
{
    free(texture_data);
    texture_data = NULL;
    texture_data_size = 0;
}


#endif
