/* RGB staging image ownership and exact Earth palette expansion contracts. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earth_reference.h"
#include "e_ext.h"

static void *allocations[4];
static unsigned int allocation_count, allocation_calls;
static int fail_allocation, expect_error, errors;
static const char *last_error;
static unsigned int read_calls, fail_read_at;

static int tracked_earth_read_data(int longitude, unsigned long offset,
                                   unsigned int count,
                                   unsigned char TG_HUGE *destination)
{
    ++read_calls;
    if (read_calls == fail_read_at) return 0;
    return earth_read_data(longitude, offset, count, destination);
}

static void *tracked_malloc(size_t bytes)
{
    void *result;
    ++allocation_calls;
    if(fail_allocation) { fail_allocation=0;return NULL; }
    result=malloc(bytes);
    assert(result!=NULL);
    assert(allocation_count<sizeof(allocations)/sizeof(allocations[0]));
    allocations[allocation_count++]=result;
    return result;
}

static void tracked_free(void *pointer)
{
    unsigned int i;
    if(pointer==NULL)return;
    for(i=0;i<allocation_count;++i)if(allocations[i]==pointer)break;
    assert(i<allocation_count);
    allocations[i]=allocations[--allocation_count];
    free(pointer);
}

/* Include the module to observe private byte accounting and inject allocation
 * failure without adding a test-only public API to the application. */
#define malloc tracked_malloc
#define free tracked_free
#define earth_read_data tracked_earth_read_data
#include "../src/texture.c"
#undef malloc
#undef free
#undef earth_read_data

#include "e_cmap.h"

void error(char *message)
{
    assert(expect_error);
    ++errors;last_error=message;
}

static void check_image(unsigned int size,const unsigned char *source)
{
    unsigned long i,elements;
    unsigned int channel;
    elements=(unsigned long)size*(size/2U);
    assert(texture_size==size);
    assert(texture_data_size==(size_t)elements*3U);
    assert(allocation_count==1);
    for(i=0;i<elements;++i)for(channel=0;channel<3;++channel)
        assert(texture_data[i*3UL+channel]==earth_cmap[source[i]][channel]);
}

int main(void)
{
    unsigned char *saved;
    unsigned int calls;
    size_t saved_bytes;
    texture_init();
    assert(texture_data==NULL&&texture_size==0&&allocation_count==0);
    calls=allocation_calls;
    assert(texture_build(33)==DRV_BAD_INPUT);
    assert(!texture_try_generate(33)&&!texture_try_generate(-1));
    assert(errors==0&&allocation_calls==calls&&texture_data==NULL&&texture_size==0);
    fail_allocation=1;
    assert(!texture_try_generate(32));
    assert(errors==0&&texture_data==NULL&&texture_size==0&&allocation_count==0);
    assert(texture_try_generate(32));check_image(32,test_earth_data(32));
    texture_generate(32);check_image(32,test_earth_data(32));
    texture_generate(64);check_image(64,test_earth_data(64));
    texture_generate(128);check_image(128,test_earth_data(128));
    texture_generate(256);check_image(256,test_earth_data(256));
    texture_generate(512);check_image(512,test_earth_data(512));
    texture_generate(32);check_image(32,test_earth_data(32));
    saved=texture_data;saved_bytes=texture_data_size;
    calls=allocation_calls;
    texture_generate(32);check_image(32,test_earth_data(32));
    assert(texture_data==saved&&allocation_calls==calls);
    assert(texture_try_generate(32));check_image(32,test_earth_data(32));
    assert(texture_data==saved&&allocation_calls==calls);
    fail_allocation=1;
    assert(!texture_try_generate(64));
    assert(errors==0&&texture_data==saved&&texture_data_size==saved_bytes);
    calls=allocation_calls;
    assert(!texture_try_generate(33)&&!texture_try_generate(-1));
    assert(errors==0&&allocation_calls==calls&&texture_data==saved);
    check_image(32,test_earth_data(32));
    expect_error=1;fail_allocation=1;
    texture_generate(64);
    assert(errors==1&&strcmp(last_error,"Could not allocate memory for texture!")==0);
    assert(texture_data==saved&&texture_data_size==saved_bytes);
    check_image(32,test_earth_data(32));
    calls=allocation_calls;
    texture_generate(33);texture_generate(-1);
    assert(errors==3&&strcmp(last_error,"Invalid texture size")==0);
    assert(allocation_calls==calls&&texture_data==saved);
    check_image(32,test_earth_data(32));
    expect_error=0;
    texture_free();texture_free();
    assert(texture_data==NULL&&texture_data_size==0&&allocation_count==0);
    /* XGL uploads release RGB staging before reading the last dimensions. */
    assert(texture_size==32);
    assert(texture_try_generate(64));check_image(64,test_earth_data(64));
    saved=texture_data;
    fail_read_at=read_calls+2;
    assert(texture_build(128)==DRV_BAD_DATA);
    assert(texture_data==saved&&allocation_count==1&&texture_size==64);
    fail_read_at=0;
    check_image(64,test_earth_data(64));
    fail_read_at=read_calls+2;
    assert(texture_build(64)==DRV_BAD_DATA);
    assert(texture_data==NULL&&texture_data_size==0&&allocation_count==0);
    fail_read_at=0;
    fail_allocation=1;
    assert(texture_build(64)==DRV_NO_MEMORY);
    assert(allocation_count==0);
    assert(texture_build(64)==DRV_OK);
    saved=texture_take_data();
    assert(saved!=NULL&&texture_data==NULL&&texture_data_size==0&&texture_size==64);
    texture_free();
    assert(allocation_count==1);
    assert(texture_take_data()==NULL);
    tracked_free(saved);
    texture_init();texture_init();
    assert(texture_data==NULL&&texture_size==0&&allocation_count==0);
    puts("PASS Earth texture RGB expansion: all five resolutions match every source palette index");
    puts("PASS Texture ownership: repeated generation, transactional allocation failure, invalid sizes and idempotent cleanup");
    puts("PASS Recoverable texture generation: invalid sizes and allocation failure preserve owned bytes and dimensions without fatal errors");
    puts("PASS Texture result and transfer contracts: data failures are distinct from allocation failures, partial reused images are invalidated and detached bytes are freed by one owner");
    return 0;
}
