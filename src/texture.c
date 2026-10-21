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

/* Keep both public policies on one transactional image construction path. */
static char *texture_build_image(int size)
{
    const unsigned char *earth_data;
#ifdef TEXTURE_RGB
    unsigned long i, elements;
    size_t bytes;
    unsigned char *new_data, *destination;
#endif
    
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
            return "Invalid texture size";
    }
    
#ifdef TEXTURE_RGB
    elements = (unsigned long)size * ((unsigned int)size / 2U);
    if ((unsigned int)size > TEXTURE_MAX_SIZE ||
        elements > (size_t)-1 / 3U)
    {
        return "Texture image is too large";
    }
    bytes = (size_t)elements * 3U;
    /* Regeneration at the same dimensions can reuse its owned staging
     * image. Size changes allocate first to preserve the previous image
     * if a replacement cannot be obtained. */
    new_data = texture_data_size == bytes ? texture_data : NULL;
    if (new_data == NULL) new_data = (unsigned char *)malloc(bytes);
    if (new_data == NULL)
    {
        return "Could not allocate memory for texture!";
    }
    destination = new_data;
    for (i = 0; i < elements; ++i)
    {
        *destination++ = earth_cmap[earth_data[i]][0];
        *destination++ = earth_cmap[earth_data[i]][1];
        *destination++ = earth_cmap[earth_data[i]][2];
    }
    /* Commit only after the replacement is complete. Failed generation
     * leaves the previously owned image intact for error-path cleanup. */
    if (texture_data != new_data) free(texture_data);
    texture_data = new_data;
    texture_data_size = bytes;
    texture_size = (unsigned int)size;
    return NULL;
#else
    (void)earth_data;
    return "Texture RGB format is required";
#endif
}

void texture_generate(int size)
{
    char *message;
    message = texture_build_image(size);
    if (message != NULL) error(message);
}

int texture_try_generate(int size)
{
    return texture_build_image(size) == NULL;
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
