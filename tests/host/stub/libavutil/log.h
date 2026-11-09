#ifndef STUB_AVUTIL_LOG_H
#define STUB_AVUTIL_LOG_H
#include <stdarg.h>
#define LIBAVUTIL_VERSION_INT 0
#define AV_LOG_ERROR   16
#define AV_LOG_WARNING 24
#define AV_LOG_INFO    32
#define AV_LOG_DEBUG   48
struct AVOption;
typedef struct AVClass {
    const char *class_name;
    const char *(*item_name)(void *ctx);
    const struct AVOption *option;
    int version;
} AVClass;
const char *av_default_item_name(void *ctx);
void av_log(void *ctx, int level, const char *fmt, ...);
#endif
