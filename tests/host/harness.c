/*
 * Host test harness for kdsp.c (see player/ffmpeg-patch/kdsp.c).
 *
 * kdsp.c is written against FFmpeg's libavformat URLProtocol API.
 * This harness provides minimal stand-ins for the handful of FFmpeg
 * functions kdsp.c uses (ffurl_* layered on a POSIX socket instead of
 * the tcp protocol, av_malloc & friends, av_log, av_url_split), so
 * the exact same kdsp.c can be compiled with the host compiler and
 * exercised against the real Python KDSP server — no Android device
 * needed to validate the frame parsing, resync and statistics logic.
 *
 * Usage: harness kdsp://host:port outfile max_bytes
 *   Reads up to max_bytes of payload (0 = until EOF) and writes them
 *   to outfile. Exit 0 on success (EOF or byte target reached).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "avformat.h"
#include "kdsp.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "url.h"

/* ---------------- FFmpeg API stand-ins ---------------- */

const char *av_default_item_name(void *ctx) { (void) ctx; return "kdsp"; }

void av_log(void *ctx, int level, const char *fmt, ...)
{
    va_list ap;
    (void) ctx; (void) level;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void *av_malloc(size_t size) { return malloc(size); }
void *av_mallocz(size_t size) { return calloc(1, size); }

void av_freep(void *ptr)
{
    void **p = (void **) ptr;
    free(*p);
    *p = NULL;
}

void av_fast_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    void **p = (void **) ptr;
    if (*size < min_size) {
        free(*p);
        *p = malloc(min_size);
        *size = *p ? (unsigned int) min_size : 0;
    }
}

int64_t av_gettime_relative(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

struct AVDictionary { int dummy; };

int av_dict_set_int(AVDictionary **pm, const char *key, int64_t value, int flags)
{
    (void) key; (void) value; (void) flags;
    if (!*pm)
        *pm = calloc(1, sizeof(AVDictionary));
    return 0;
}

void av_dict_free(AVDictionary **m)
{
    free(*m);
    *m = NULL;
}

void av_url_split(char *proto, int proto_size,
                  char *authorization, int authorization_size,
                  char *hostname, int hostname_size, int *port_ptr,
                  char *path, int path_size, const char *url)
{
    const char *p = strstr(url, "://");
    const char *auth_start, *slash, *colon;
    size_t len;

    (void) authorization; (void) authorization_size;
    (void) path; (void) path_size;
    if (proto && proto_size > 0) {
        len = p ? (size_t) (p - url) : 0;
        if (len >= (size_t) proto_size) len = proto_size - 1;
        memcpy(proto, url, len);
        proto[len] = '\0';
    }
    auth_start = p ? p + 3 : url;
    slash = strchr(auth_start, '/');
    len = slash ? (size_t) (slash - auth_start) : strlen(auth_start);
    colon = memchr(auth_start, ':', len);
    if (hostname && hostname_size > 0) {
        size_t hlen = colon ? (size_t) (colon - auth_start) : len;
        if (hlen >= (size_t) hostname_size) hlen = hostname_size - 1;
        memcpy(hostname, auth_start, hlen);
        hostname[hlen] = '\0';
    }
    if (port_ptr)
        *port_ptr = colon ? atoi(colon + 1) : -1;
}

/* ---------------- ffurl_* over a POSIX socket ---------------- */

typedef struct ShimTcp { int fd; } ShimTcp;

int ffurl_open(URLContext **puc, const char *filename, int flags,
               const AVIOInterruptCB *int_cb, AVDictionary **options)
{
    char host[256];
    int port = -1, fd;
    struct hostent *he;
    struct sockaddr_in sin;
    URLContext *h;
    ShimTcp *t;

    (void) flags; (void) int_cb; (void) options;
    av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port, NULL, 0, filename);
    he = gethostbyname(host);
    if (!he)
        return AVERROR(EINVAL);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return AVERROR(errno);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t) port);
    memcpy(&sin.sin_addr, he->h_addr_list[0], he->h_length);
    if (connect(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        int e = errno;
        close(fd);
        return AVERROR(e);
    }
    h = calloc(1, sizeof(*h));
    t = malloc(sizeof(*t));
    t->fd = fd;
    h->priv_data = t;
    *puc = h;
    return 0;
}

int ffurl_read(URLContext *h, unsigned char *buf, int size)
{
    ShimTcp *t = h->priv_data;
    ssize_t n = recv(t->fd, buf, size, 0);
    if (n == 0)
        return AVERROR_EOF;
    if (n < 0)
        return AVERROR(errno);
    return (int) n;
}

int ffurl_closep(URLContext **h)
{
    ShimTcp *t;
    if (!h || !*h)
        return 0;
    t = (*h)->priv_data;
    if (t) {
        close(t->fd);
        free(t);
    }
    free(*h);
    *h = NULL;
    return 0;
}

/* ---------------- test driver ---------------- */

extern const URLProtocol ff_kdsp_protocol;

int main(int argc, char **argv)
{
    URLContext h;
    FILE *out;
    uint8_t buf[4096];
    long max_bytes, total = 0;
    int64_t last_speed_print = 0;
    int ret;

    if (argc != 4) {
        fprintf(stderr, "usage: %s kdsp://host:port outfile max_bytes\n", argv[0]);
        return 1;
    }
    max_bytes = atol(argv[3]);
    out = fopen(argv[2], "wb");
    if (!out) {
        perror("fopen");
        return 1;
    }

    memset(&h, 0, sizeof(h));
    h.prot = &ff_kdsp_protocol;
    h.priv_data = calloc(1, ff_kdsp_protocol.priv_data_size);
    /* mimic ffurl_alloc(): apply the AVOption defaults */
    if (ff_kdsp_protocol.priv_data_class) {
        const AVOption *o = ff_kdsp_protocol.priv_data_class->option;
        *(const void **) h.priv_data = ff_kdsp_protocol.priv_data_class;
        for (; o && o->name; o++)
            if (o->type == AV_OPT_TYPE_INT)
                *(int *) ((char *) h.priv_data + o->offset) = (int) o->default_val.i64;
    }

    ret = ff_kdsp_protocol.url_open(&h, argv[1], 0);
    if (ret < 0) {
        fprintf(stderr, "kdsp_open failed: %d\n", ret);
        return 2;
    }

    for (;;) {
        int want = (total % 5000 < 2500) ? 4096 : 997;  /* cross frame boundaries unevenly */
        int n;
        if (max_bytes > 0 && total + want > max_bytes)
            want = (int) (max_bytes - total);
        if (want <= 0)
            break;
        n = ff_kdsp_protocol.url_read(&h, buf, want);
        if (n < 0) {
            if (n == AVERROR_EOF) {
                printf("EOF after %ld payload bytes\n", total);
                break;
            }
            fprintf(stderr, "kdsp_read failed: %d after %ld bytes\n", n, total);
            ff_kdsp_protocol.url_close(&h);
            return 2;
        }
        fwrite(buf, 1, n, out);
        total += n;
        if (av_gettime_relative() - last_speed_print > 500000) {
            last_speed_print = av_gettime_relative();
            printf("avg=%lld bps realtime=%lld bps\n",
                   (long long) kdsp_average_speed(&h),
                   (long long) kdsp_realtime_speed(&h));
        }
        if (max_bytes > 0 && total >= max_bytes)
            break;
    }

    printf("done: %ld payload bytes, avg=%lld bps realtime=%lld bps\n",
           total,
           (long long) kdsp_average_speed(&h),
           (long long) kdsp_realtime_speed(&h));
    ff_kdsp_protocol.url_close(&h);
    fclose(out);
    free(h.priv_data);
    return 0;
}
