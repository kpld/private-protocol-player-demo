#ifndef STUB_AVUTIL_OPT_H
#define STUB_AVUTIL_OPT_H
#include <stdint.h>
enum AVOptionType { AV_OPT_TYPE_INT, AV_OPT_TYPE_INT64 };
#define AV_OPT_FLAG_DECODING_PARAM 1
#define AV_OPT_FLAG_ENCODING_PARAM 2
typedef struct AVOption {
    const char *name;
    const char *help;
    int offset;
    enum AVOptionType type;
    union { int64_t i64; double dbl; } default_val;
    double min;
    double max;
    int flags;
    const char *unit;
} AVOption;
#endif
