/* Ownership transfer and every mip-allocation failure in the software path. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *owned[16];
static unsigned int owned_count, allocation_calls, fail_call;
static size_t recent_sizes[2];

static void *tracked_malloc(size_t bytes)
{
    void *result;
    ++allocation_calls;
    recent_sizes[0]=recent_sizes[1];recent_sizes[1]=bytes;
    if (allocation_calls == fail_call) return NULL;
    result = malloc(bytes);
    assert(result != NULL);
    assert(owned_count < sizeof(owned) / sizeof(owned[0]));
    owned[owned_count++] = result;
    return result;
}

static void tracked_free(void *pointer)
{
    unsigned int i;
    if (pointer == NULL) return;
    for (i = 0; i < owned_count && owned[i] != pointer; ++i) { }
    assert(i < owned_count);
    owned[i] = owned[--owned_count];
    free(pointer);
}

#define malloc tracked_malloc
#define free tracked_free
#include "../src/texsoft.c"
#undef malloc
#undef free

static unsigned char *image(unsigned int width, unsigned int height)
{
    unsigned char *data;
    size_t bytes;
    bytes = (size_t)width * height * 3U;
    data = (unsigned char *)tracked_malloc(bytes);
    assert(data != NULL);
    memset(data, 93, bytes);
    return data;
}

static void raster_storage(void)
{
    float matrix[4][4];
    unsigned char palette[TEXTURE_PALETTE_BYTES];
    unsigned int i,j,calls,failure;
    unsigned char *pixels;
    memset(palette,0,sizeof(palette));
    for(i=0;i<4U;++i)for(j=0;j<4U;++j)matrix[i][j]=(float)(i==j);
    assert(texsoft_texture_take(1,1,image(1,1))==DRV_OK);
    assert(texsoft_begin(5,7,matrix,matrix,TEXSOFT_INDEX_BYTES,palette));
    assert(recent_sizes[0]==35U&&recent_sizes[1]==35U*sizeof(float));
    assert(owned_count==4&&texsoft_pixels()==NULL);
    calls=allocation_calls;pixels=texsoft_index_pixels();
    assert(texsoft_begin(5,7,matrix,matrix,TEXSOFT_INDEX_BYTES,palette));
    assert(calls==allocation_calls&&pixels==texsoft_index_pixels());
    assert(texsoft_begin(5,7,matrix,matrix,TEXSOFT_INDEX_WORDS,palette));
    assert(recent_sizes[0]==35U*sizeof(unsigned long)&&recent_sizes[1]==35U*sizeof(float));
    assert(owned_count==4&&texsoft_index_pixels()==NULL&&texsoft_pixels()!=NULL);
    for(failure=1;failure<=2U;++failure){
        fail_call=allocation_calls+failure;
        assert(!texsoft_begin(5,7,matrix,matrix,TEXSOFT_INDEX_BYTES,palette));
        assert(texsoft_pixels()==NULL&&texsoft_index_pixels()==NULL&&owned_count==2);
        fail_call=0;
        assert(texsoft_begin(5,7,matrix,matrix,TEXSOFT_INDEX_WORDS,palette));
        assert(owned_count==4);
    }
    texsoft_free();assert(owned_count==0);
    puts("PASS Software raster allocation: byte-sized indexed pixels, no duplicate frame, format replacement, cached reuse and allocation-failure cleanup");
}

int main(void)
{
    unsigned char *base, *invalid;
    unsigned int failure, calls;
    static const unsigned char borrowed[3] = { 1, 2, 3 };

    base = image(512, 256);
    calls = allocation_calls;
    assert(texsoft_texture_take_filtered(512, 256, base, TEXSOFT_BILINEAR) == DRV_OK);
    assert(texsoft_mips[0] == base && texsoft_levels == 1U);
    assert(allocation_calls == calls && owned_count == 1U);
    invalid = image(2, 2);
    assert(texsoft_texture_take_filtered(2, 2, invalid, -1) == DRV_BAD_INPUT);
    assert(owned_count == 1U && texsoft_mips[0] == base && texsoft_levels == 1U);
    assert(texsoft_texture_take_filtered(512, 256, NULL, TEXSOFT_BILINEAR) == DRV_BAD_INPUT);
    assert(owned_count == 1U && texsoft_mips[0] == base);
    texsoft_free();
    assert(owned_count == 0);
    puts("PASS Software base-image ownership: bilinear adopts without copying or allocating unused mip levels, invalid filter consumes only its replacement");

    base = image(512, 256);
    calls = allocation_calls;
    assert(texsoft_texture_take(512, 256, base) == DRV_OK);
    assert(texsoft_mips[0] == base);
    assert(texsoft_levels == 10 && allocation_calls == calls + 9);
    assert(owned_count == 10);
    base = image(512, 256);
    calls = allocation_calls;
    assert(texsoft_texture_take_filtered(512, 256, base, TEXSOFT_BILINEAR) == DRV_OK);
    assert(texsoft_mips[0] == base && texsoft_levels == 1U);
    assert(allocation_calls == calls && owned_count == 1U);
    texsoft_free(); texsoft_free();
    assert(owned_count == 0);

    /* Every lower-level allocation failure must release the adopted base
     * and every completed level, without keeping a partially usable pyramid. */
    for (failure = 1; failure <= 9; ++failure)
    {
        base = image(512, 256);
        fail_call = allocation_calls + failure;
        assert(texsoft_texture_take(512, 256, base) == DRV_NO_MEMORY);
        assert(owned_count == 0 && texsoft_levels == 0 && !texsoft_ready);
        texsoft_free();
        fail_call = 0;
    }

    base = image(2, 2);
    assert(texsoft_texture_take(2, 2, base) == DRV_OK);
    assert(owned_count == 2);
    invalid = image(3, 2);
    assert(texsoft_texture_take(3, 2, invalid) == DRV_BAD_INPUT);
    assert(owned_count == 2 && texsoft_mips[0] == base);
    assert(texsoft_texture_take(2, 2, NULL) == DRV_BAD_INPUT);
    assert(owned_count == 2 && texsoft_mips[0] == base);

    /* The retained copying interface must never adopt or free borrowed data. */
    fail_call = allocation_calls + 1;
    assert(!texsoft_texture(1, 1, borrowed));
    assert(owned_count == 2 && texsoft_mips[0] == base);
    fail_call = 0;
    assert(texsoft_texture(1, 1, borrowed));
    assert(owned_count == 1 && texsoft_mips[0] != borrowed);
    assert(memcmp(texsoft_mips[0], borrowed, sizeof(borrowed)) == 0);
    texsoft_free();
    assert(owned_count == 0);
    raster_storage();
    puts("PASS Software texture adoption: no duplicate base, all mip-allocation failures release ownership exactly once, invalid input and borrowed-copy contracts");
    return 0;
}
