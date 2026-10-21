/* Original palette bytes, used only by host regressions and resource records. */
#ifndef TEST_EARTH_REFERENCE_H
#define TEST_EARTH_REFERENCE_H
#define earth_data_32 test_earth_data_32
#define earth_data_64 test_earth_data_64
#define earth_data_128 test_earth_data_128
#define earth_data_256 test_earth_data_256
#define earth_data_512 test_earth_data_512
#include "e_32.h"
#include "e_64.h"
#include "e_128.h"
#include "e_256.h"
#include "e_512.h"
#undef earth_data_32
#undef earth_data_64
#undef earth_data_128
#undef earth_data_256
#undef earth_data_512

static unsigned char *test_earth_data(int longitude)
{
    switch (longitude)
    {
        case 32: return test_earth_data_32;
        case 64: return test_earth_data_64;
        case 128: return test_earth_data_128;
        case 256: return test_earth_data_256;
        case 512: return test_earth_data_512;
    }
    return NULL;
}
#endif
