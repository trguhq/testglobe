/* Ownership transfer and every mip-allocation failure in the software path. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *owned[16];
static unsigned int owned_count, allocation_calls, fail_call;

static void *tracked_malloc(size_t bytes)
{
    void *result;
    ++allocation_calls;
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

int main(void)
{
    unsigned char *base, *invalid;
    unsigned int failure, calls;
    static const unsigned char borrowed[3] = { 1, 2, 3 };

    base = image(512, 256);
    calls = allocation_calls;
    assert(texsoft_texture_take(512, 256, base) == DRV_OK);
    assert(texsoft_mips[0] == base);
    assert(texsoft_levels == 10 && allocation_calls == calls + 9);
    assert(owned_count == 10);
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
    puts("PASS Software texture adoption: no duplicate base, all mip-allocation failures release ownership exactly once, invalid input and borrowed-copy contracts");
    return 0;
}
