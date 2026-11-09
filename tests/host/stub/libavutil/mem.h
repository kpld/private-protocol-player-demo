#ifndef STUB_AVUTIL_MEM_H
#define STUB_AVUTIL_MEM_H
#include <stddef.h>
void *av_malloc(size_t size);
void *av_mallocz(size_t size);
void av_freep(void *ptr);
void av_fast_malloc(void *ptr, unsigned int *size, size_t min_size);
#endif
