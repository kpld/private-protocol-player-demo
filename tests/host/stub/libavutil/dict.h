#ifndef STUB_AVUTIL_DICT_H
#define STUB_AVUTIL_DICT_H
#include <stdint.h>
typedef struct AVDictionary AVDictionary;
int av_dict_set_int(AVDictionary **pm, const char *key, int64_t value, int flags);
void av_dict_free(AVDictionary **m);
#endif
