/* Canonical binary map bytes, read only by host regression fixtures.
 * Production applications embed these bytes and never open these paths. */
#ifndef TEST_EARTH_REFERENCE_H
#define TEST_EARTH_REFERENCE_H
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char *test_earth_data(int longitude)
{
    static unsigned char *maps[5];
    unsigned char *bytes;
    unsigned long total, offset, length;
    int slot, width, chunk;
    char path[64];
    FILE *stream;
    slot = 0;
    for (width = 32; width < longitude && width < 512; width *= 2)
        ++slot;
    assert(width == longitude && slot < 5);
    if (maps[slot] != NULL) return maps[slot];
    total = (unsigned long)longitude * (unsigned long)longitude / 2UL;
    bytes = (unsigned char *)malloc((size_t)total);
    assert(bytes != NULL);
    offset = 0;
    chunk = 0;
    while (offset < total)
    {
        length = total - offset;
        if (length > 16384UL) length = 16384UL;
        if (longitude < 256)
            sprintf(path, "data/e%i.bin", longitude);
        else
            sprintf(path, "data/e%i_%i.bin", longitude, chunk);
        stream = fopen(path, "rb");
        assert(stream != NULL);
        assert(fread(bytes + offset, 1, (size_t)length, stream) ==
               (size_t)length);
        assert(fgetc(stream) == EOF && !ferror(stream));
        assert(fclose(stream) == 0);
        offset += length;
        ++chunk;
    }
    maps[slot] = bytes;
    return bytes;
}
#endif
