/* Generated data must reproduce canonical inputs without runtime file access. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "e_ext.h"
#include "earth_reference.h"

int main(int argc, char **argv)
{
    unsigned char storage[4095], *buffer, *reference;
    unsigned long total, offset, length, checksum;
    static unsigned long boundaries[] = {
        0UL, 511UL, 16383UL, 16384UL, 32767UL, 32768UL,
        65535UL, 65536UL, 98303UL, 98304UL, 131071UL
    };
    unsigned int i, j, count, available;
    const unsigned char *span;
    int width, highest, files;
    (void)argv;
    files = argc == 1;
#ifdef DRV_MTX
    highest = 64;
#else
    highest = 512;
#endif
    buffer = storage + 1;
    checksum = 2166136261UL;
    assert(earth_data_size(-1) == 0 && earth_data_size(0) == 0 &&
           earth_data_size(33) == 0 && earth_data_size(1024) == 0);
    assert(!earth_read_data(33, 0UL, 0U, NULL));
    for (width = 32; width <= 512; width *= 2)
    {
        total = (unsigned long)width * (unsigned long)width / 2UL;
        if (width > highest)
        {
            assert(earth_data_size(width) == 0);
            assert(!earth_read_data(width, 0UL, 1U, buffer));
            continue;
        }
        assert(earth_data_size(width) == total);
        assert(earth_data_span(width, 0UL, NULL) == NULL);
        available = 7U;
        assert(earth_data_span(width, total, &available) == NULL && available == 0U);
        reference = files ? test_earth_data(width) : NULL;
        for (offset = 0UL; offset < total; offset += available)
        {
            span = earth_data_span(width, offset, &available);
            assert(span != NULL && available > 0U && available <= 16384U);
            assert(available <= total - offset);
            if (files) assert(memcmp(span, reference + offset, available) == 0);
        }
        assert(earth_read_data(width, 0UL, 0U, NULL));
        assert(earth_read_data(width, total, 0U, NULL));
        assert(!earth_read_data(width, total + 1UL, 0U, NULL));
        assert(!earth_read_data(width, 0UL, 1U, NULL));
        memset(storage, 0x5a, sizeof(storage));
        assert(!earth_read_data(width, ULONG_MAX, 1U, buffer));
        assert(!earth_read_data(width, total, 1U, buffer));
        assert(!earth_read_data(width, total - 1UL, 2U, buffer));
        assert(!earth_read_data(width, total - 1UL, UINT_MAX, buffer));
        for (j = 0; j < sizeof(storage); ++j) assert(storage[j] == 0x5a);
        for (offset = 0; offset < total; offset += length)
        {
            length = total - offset;
            if (length > sizeof(storage) - 2U)
                length = sizeof(storage) - 2U;
            count = (unsigned int)length;
            storage[0] = storage[count + 1U] = 0xa5;
            assert(earth_read_data(width, offset, count, buffer));
            assert(storage[0] == 0xa5 && storage[count + 1U] == 0xa5);
            if (files) assert(memcmp(buffer, reference + offset, count) == 0);
            for (j = 0; j < count; ++j)
                checksum = ((checksum ^ buffer[j]) * 16777619UL) & 0xffffffffUL;
        }
        for (i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i)
        {
            offset = boundaries[i];
            if (offset >= total) continue;
            span = earth_data_span(width, offset, &available);
            assert(span != NULL && available <= 16384UL - offset % 16384UL);
            if (files) assert(*span == reference[offset]);
            count = (unsigned int)(total - offset < 2UL ? 1U : 2U);
            assert(earth_read_data(width, offset, count, buffer));
            if (files) assert(memcmp(buffer, reference + offset, count) == 0);
        }
    }
    assert(checksum != 0);
    if (files)
        puts("PASS Earth data: exact canonical bytes, all selected levels, invalid reads, chunk seams and 64KiB boundaries");
    else
        printf("PASS embedded Earth data: no runtime map files needed (%08lx)\n", checksum);
    return 0;
}
