/*
 * KDSP (Device Streaming Protocol) — URLProtocol for FFmpeg.
 *
 * KDSP is a fictional proprietary-style streaming protocol invented
 * for this demo: fixed 22-byte little-endian frame headers over TCP,
 * each frame carrying one H.264 access unit in Annex B format. The
 * full specification and the receiver rules implemented here live in
 * protocol.md at the root of the demo repository.
 *
 * Design notes:
 *  - The protocol is layered on top of FFmpeg's tcp protocol
 *    (ffurl_open("tcp://...")), the same way http.c layers on it, so
 *    connect/read timeouts and the interrupt callback come from the
 *    tcp implementation.
 *  - To the demuxer above, kdsp_read() presents a plain byte stream:
 *    frame headers are verified and stripped, payloads are buffered
 *    one frame at a time and served across as many read calls as the
 *    demuxer makes.
 *  - The stream is live and unseekable: kdsp_seek() always fails,
 *    which is also what lets the player's reconnect logic engage when
 *    the server goes away (reads report AVERROR_EOF, not a hang).
 *
 * This file is part of the KDSP demo patch for FFmpeg (LGPL).
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "avformat.h"
#include "kdsp.h"
#include "url.h"

#define KDSP_DEFAULT_PORT   19350
#define KDSP_HEADER_SIZE    22
#define KDSP_MAX_PAYLOAD    (8 * 1024 * 1024)   /* spec rule 2 sanity limit */
#define KDSP_VERSION        0x01
#define KDSP_FLAG_KEYFRAME  0x01

#define KDSP_SPEED_SAMPLES  8                   /* ring of (time, bytes) samples */
#define KDSP_SAMPLE_MIN_US  100000              /* take a sample at most every 100 ms */
#define KDSP_STALL_US       1500000             /* no sample for 1.5 s => realtime speed is 0 */

static const char kdsp_magic[4] = { 'K', 'D', 'S', 'P' };

typedef struct KDSPContext {
    const AVClass *class;
    URLContext *tcp;                    /* underlying tcp:// connection */
    int rw_timeout;                     /* microseconds, forwarded to tcp; -1 = none */

    /* current frame being served to the demuxer */
    uint8_t *frame_buf;
    unsigned int frame_buf_size;        /* allocated capacity (av_fast_malloc) */
    uint32_t frame_len;                 /* payload bytes in frame_buf */
    uint32_t frame_pos;                 /* payload bytes already served */

    /* statistics (spec rule 3 + resync accounting) */
    uint32_t last_seq;
    int seq_started;
    uint64_t frames_received;
    uint64_t frames_dropped;            /* sequence gaps */
    uint64_t resync_count;              /* times the magic scan had to hunt */
    uint64_t discarded_bytes;           /* bytes skipped while hunting for magic */
    uint64_t last_timestamp_ms;         /* sender clock of the last frame */
    int last_keyframe;

    /* transfer-rate accounting for kdsp_average_speed/kdsp_realtime_speed */
    uint64_t wire_bytes;                /* every byte read from tcp, headers included */
    int64_t start_time;                 /* av_gettime_relative() at open */
    int64_t sample_time[KDSP_SPEED_SAMPLES];
    uint64_t sample_bytes[KDSP_SPEED_SAMPLES];
    int sample_head;                    /* ring index of the newest sample */
    int sample_count;
} KDSPContext;

#define OFFSET(x) offsetof(KDSPContext, x)
static const AVOption kdsp_options[] = {
    { "timeout", "set timeout (in microseconds) of socket I/O operations",
      OFFSET(rw_timeout), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass kdsp_class = {
    .class_name = "kdsp",
    .item_name  = av_default_item_name,
    .option     = kdsp_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/* defined at the bottom of this file; the speed helpers check
 * h->prot against it */
extern const URLProtocol ff_kdsp_protocol;

/* ------------------------------------------------------------------ */
/* low-level receive helpers                                          */
/* ------------------------------------------------------------------ */

static void kdsp_speed_sample(KDSPContext *s)
{
    int64_t now = av_gettime_relative();
    int last = s->sample_head;

    if (s->sample_count > 0 && now - s->sample_time[last] < KDSP_SAMPLE_MIN_US)
        return;
    s->sample_head = (last + 1) % KDSP_SPEED_SAMPLES;
    s->sample_time[s->sample_head]  = now;
    s->sample_bytes[s->sample_head] = s->wire_bytes;
    if (s->sample_count < KDSP_SPEED_SAMPLES)
        s->sample_count++;
}

/* Read exactly len bytes from the tcp connection into buf.
 * Returns 0 on success, AVERROR_EOF if the connection closes first
 * (even mid-frame: a partial frame is unusable, and reporting EOF is
 * what lets the player's reconnect logic engage), or the negative
 * error returned by the tcp layer (timeouts, interrupts). */
static int kdsp_recv(KDSPContext *s, uint8_t *buf, int len)
{
    int done = 0;

    while (done < len) {
        int ret = ffurl_read(s->tcp, buf + done, len - done);
        if (ret == 0)
            return AVERROR_EOF;
        if (ret < 0)
            return ret;
        done += ret;
        s->wire_bytes += ret;
        kdsp_speed_sample(s);
    }
    return 0;
}

static int kdsp_recv_byte(KDSPContext *s, uint8_t *byte)
{
    return kdsp_recv(s, byte, 1);
}

/* ------------------------------------------------------------------ */
/* frame layer                                                        */
/* ------------------------------------------------------------------ */

/* Scan the byte stream until the 4-byte KDSP magic is found (spec
 * rule 1). Bytes skipped on the way are counted; finding the magic
 * after skipping at least one byte counts as one resynchronisation. */
static int kdsp_find_magic(URLContext *h)
{
    KDSPContext *s = h->priv_data;
    int matched = 0;
    uint64_t consumed = 0;

    while (matched < 4) {
        uint8_t byte;
        int ret = kdsp_recv_byte(s, &byte);
        if (ret < 0)
            return ret;
        consumed++;
        if (byte == (uint8_t) kdsp_magic[matched]) {
            matched++;
        } else {
            /* restart: only 'K' itself can begin a new match */
            matched = (byte == (uint8_t) kdsp_magic[0]) ? 1 : 0;
        }
    }
    if (consumed > 4) {                 /* had to hunt: exact discard count */
        uint64_t skipped = consumed - 4;
        s->resync_count++;
        s->discarded_bytes += skipped;
        av_log(h, AV_LOG_WARNING,
               "kdsp: resync, skipped %"PRIu64" bytes to find magic\n", skipped);
    }
    return 0;
}

/* Load the next valid frame's payload into s->frame_buf.
 * Returns 0 on success (frame_len may be re-checked by the caller),
 * AVERROR_EOF when the stream ends, or a negative error. */
static int kdsp_next_frame(URLContext *h)
{
    KDSPContext *s = h->priv_data;
    uint8_t hdr[KDSP_HEADER_SIZE - 4];  /* header after the magic */
    uint32_t seq, payload_length;
    uint8_t version, flags;

    for (;;) {
        int ret = kdsp_find_magic(h);
        if (ret < 0)
            return ret;
        ret = kdsp_recv(s, hdr, sizeof(hdr));
        if (ret < 0)
            return ret;

        version        = hdr[0];
        flags          = hdr[1];
        seq            = AV_RL32(hdr + 2);
        payload_length = AV_RL32(hdr + 14);

        /* A bad version or an absurd length means we are not really
         * aligned on a frame boundary (spec rule 2): do not allocate,
         * hunt for the next magic instead. */
        if (version != KDSP_VERSION || payload_length > KDSP_MAX_PAYLOAD) {
            av_log(h, AV_LOG_WARNING,
                   "kdsp: bad header (version %u, payload_length %u), resync\n",
                   version, payload_length);
            continue;
        }

        /* Sequence tracking (spec rule 3): count the gap, keep playing. */
        if (s->seq_started) {
            uint32_t expected = s->last_seq + 1;
            if (seq != expected) {
                uint32_t gap = seq - expected;  /* wraps naturally */
                if (gap < 0x80000000U) {
                    s->frames_dropped += gap;
                    av_log(h, AV_LOG_WARNING,
                           "kdsp: sequence gap, %"PRIu32" frame(s) dropped "
                           "(expected %u, got %u)\n", gap, expected, seq);
                }
            }
        } else {
            s->seq_started = 1;
        }
        s->last_seq         = seq;
        s->last_timestamp_ms = AV_RL64(hdr + 6);
        s->last_keyframe     = !!(flags & KDSP_FLAG_KEYFRAME);
        s->frames_received++;

        if (payload_length == 0) {
            s->frame_len = s->frame_pos = 0;
            continue;                   /* nothing to serve, take the next frame */
        }

        av_fast_malloc(&s->frame_buf, &s->frame_buf_size, payload_length);
        if (!s->frame_buf)
            return AVERROR(ENOMEM);
        ret = kdsp_recv(s, s->frame_buf, payload_length);
        if (ret < 0)
            return ret;                 /* EOF mid-payload: frame is unusable */

        s->frame_len = payload_length;
        s->frame_pos = 0;
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* URLProtocol callbacks                                              */
/* ------------------------------------------------------------------ */

static int kdsp_open(URLContext *h, const char *uri, int flags)
{
    KDSPContext *s = h->priv_data;
    char hostname[256], tcp_url[320];
    int port = KDSP_DEFAULT_PORT;
    AVDictionary *opts = NULL;
    int ret;

    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port,
                 NULL, 0, uri);
    if (hostname[0] == '\0')
        return AVERROR(EINVAL);
    if (port <= 0)
        port = KDSP_DEFAULT_PORT;

    snprintf(tcp_url, sizeof(tcp_url), "tcp://%s:%d", hostname, port);
    if (s->rw_timeout >= 0)
        av_dict_set_int(&opts, "timeout", s->rw_timeout, 0);

    ret = ffurl_open(&s->tcp, tcp_url, AVIO_FLAG_READ,
                     &h->interrupt_callback, &opts);
    av_dict_free(&opts);
    if (ret < 0)
        return ret;

    h->is_streamed = 1;                 /* live stream: no seeking */

    s->start_time = av_gettime_relative();
    s->sample_time[0]  = s->start_time;
    s->sample_bytes[0] = 0;
    s->sample_head  = 0;
    s->sample_count = 1;
    return 0;
}

static int kdsp_read(URLContext *h, unsigned char *buf, int size)
{
    KDSPContext *s = h->priv_data;
    int n;

    if (s->frame_pos >= s->frame_len) {
        int ret = kdsp_next_frame(h);
        if (ret < 0)
            return ret;
    }
    n = FFMIN(size, (int) (s->frame_len - s->frame_pos));
    memcpy(buf, s->frame_buf + s->frame_pos, n);
    s->frame_pos += n;
    return n;
}

static int64_t kdsp_seek(URLContext *h, int64_t pos, int whence)
{
    /* Live stream: no size, no seeking (same contract as tcp). */
    return AVERROR(ENOSYS);
}

static int kdsp_close(URLContext *h)
{
    KDSPContext *s = h->priv_data;

    av_log(h, AV_LOG_INFO,
           "kdsp: closed, frames received %"PRIu64", dropped %"PRIu64
           ", resyncs %"PRIu64", discarded %"PRIu64" bytes\n",
           s->frames_received, s->frames_dropped,
           s->resync_count, s->discarded_bytes);
    ffurl_closep(&s->tcp);
    av_freep(&s->frame_buf);
    return 0;
}

/* ------------------------------------------------------------------ */
/* transfer-rate helpers (see kdsp.h)                                 */
/* ------------------------------------------------------------------ */

static KDSPContext *kdsp_check(URLContext *h)
{
    if (!h || h->prot != &ff_kdsp_protocol)
        return NULL;
    return h->priv_data;
}

int64_t kdsp_average_speed(URLContext *h)
{
    KDSPContext *s = kdsp_check(h);
    int64_t elapsed;

    if (!s)
        return -1;
    elapsed = av_gettime_relative() - s->start_time;
    if (elapsed <= 0 || s->wire_bytes == 0)
        return 0;
    return (int64_t) (s->wire_bytes * 8 * 1000000 / elapsed);
}

int64_t kdsp_realtime_speed(URLContext *h)
{
    KDSPContext *s = kdsp_check(h);
    int newest, oldest;
    int64_t span, now = av_gettime_relative();

    if (!s)
        return -1;
    if (s->sample_count < 2)
        return kdsp_average_speed(h);
    newest = s->sample_head;
    if (now - s->sample_time[newest] > KDSP_STALL_US)
        return 0;                       /* stream stalled: do not fake a rate */
    oldest = (newest - (s->sample_count - 1) + KDSP_SPEED_SAMPLES) % KDSP_SPEED_SAMPLES;
    span = s->sample_time[newest] - s->sample_time[oldest];
    if (span <= 0)
        return kdsp_average_speed(h);
    return (int64_t) ((s->sample_bytes[newest] - s->sample_bytes[oldest])
                      * 8 * 1000000 / span);
}

const URLProtocol ff_kdsp_protocol = {
    .name           = "kdsp",
    .url_open       = kdsp_open,
    .url_read       = kdsp_read,
    .url_seek       = kdsp_seek,
    .url_close      = kdsp_close,
    .priv_data_size = sizeof(KDSPContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &kdsp_class,
};
