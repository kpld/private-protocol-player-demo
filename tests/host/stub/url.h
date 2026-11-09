#ifndef STUB_URL_H
#define STUB_URL_H
#include <stdint.h>
#include "avformat.h"
#include "libavutil/log.h"
#define URL_PROTOCOL_FLAG_NETWORK 1
typedef struct URLContext {
    const struct URLProtocol *prot;
    void *priv_data;
    AVIOInterruptCB interrupt_callback;
    int is_streamed;
} URLContext;
typedef struct URLProtocol {
    const char *name;
    int (*url_open)(URLContext *h, const char *url, int flags);
    int (*url_read)(URLContext *h, unsigned char *buf, int size);
    int64_t (*url_seek)(URLContext *h, int64_t pos, int whence);
    int (*url_close)(URLContext *h);
    size_t priv_data_size;
    int flags;
    const AVClass *priv_data_class;
} URLProtocol;
int ffurl_open(URLContext **puc, const char *filename, int flags,
               const AVIOInterruptCB *int_cb, AVDictionary **options);
int ffurl_read(URLContext *h, unsigned char *buf, int size);
int ffurl_closep(URLContext **h);
#endif
