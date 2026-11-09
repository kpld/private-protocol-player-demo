#ifndef STUB_AVFORMAT_H
#define STUB_AVFORMAT_H
#include <stdint.h>
#include "libavutil/dict.h"
#define FFMIN(a, b) ((a) < (b) ? (a) : (b))
#define AVIO_FLAG_READ 1
typedef struct AVIOInterruptCB {
    int (*callback)(void *);
    void *opaque;
} AVIOInterruptCB;
void av_url_split(char *proto, int proto_size,
                  char *authorization, int authorization_size,
                  char *hostname, int hostname_size, int *port_ptr,
                  char *path, int path_size, const char *url);
#endif
