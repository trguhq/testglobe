/* Load the shipped native resources and compare them with original Earth data.
 * This is a host fixture, not a replacement for either native resource manager. */
#ifndef TEST_EARTH_RESOURCES_H
#define TEST_EARTH_RESOURCES_H
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earthres.h"
#include "earth_reference.h"

static short test_earth_resource_id(int longitude)
{
    switch (longitude)
    {
        case 32: return TG_EARTH_32_ID;
        case 64: return TG_EARTH_64_ID;
        case 128: return TG_EARTH_128_ID;
        case 256: return TG_EARTH_256_ID;
        case 512: return TG_EARTH_512_ID;
    }
    assert(!"Unsupported Earth resource resolution");
    return 0;
}

static unsigned char *test_earth_resource(short id, unsigned long *size)
{
    FILE *stream;
    unsigned char *bytes;
    unsigned long total, offset;
    int longitude, chunk;
    char path[64];
    longitude = 0;
    for (chunk = 32; chunk <= 512; chunk *= 2)
    {
        short first = test_earth_resource_id(chunk);
        total = (unsigned long)chunk * chunk / 2UL;
        if (id >= first && (unsigned long)(id - first) <
            (total + TG_EARTH_CHUNK_SIZE - 1UL) / TG_EARTH_CHUNK_SIZE)
        {
            longitude = chunk;
            break;
        }
    }
    assert(longitude != 0 && size != NULL);
    chunk = id - test_earth_resource_id(longitude);
    total = (unsigned long)longitude * longitude / 2UL;
    offset = (unsigned long)chunk * TG_EARTH_CHUNK_SIZE;
    *size = total - offset;
    if (*size > TG_EARTH_CHUNK_SIZE) *size = TG_EARTH_CHUNK_SIZE;
    if (longitude < 256)
        sprintf(path, "data/e%i.bin", longitude);
    else
        sprintf(path, "data/e%i_%i.bin", longitude, chunk);
    stream = fopen(path, "rb");
    assert(stream != NULL);
    bytes = (unsigned char *)malloc((size_t)*size);
    assert(bytes != NULL);
    assert(fread(bytes, 1, (size_t)*size, stream) == (size_t)*size);
    assert(fgetc(stream) == EOF && !ferror(stream));
    fclose(stream);
    assert(memcmp(bytes, test_earth_data(longitude) + offset, (size_t)*size) == 0);
    return bytes;
}
#endif
