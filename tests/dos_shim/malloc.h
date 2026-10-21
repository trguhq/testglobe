/* Preserve _halloc's element-count contract without claiming huge emulation. */
#ifndef TEST_MALLOC_H
#define TEST_MALLOC_H
#include <stddef.h>
void *_halloc(long count, size_t size);
void _hfree(void *pointer);
#endif
