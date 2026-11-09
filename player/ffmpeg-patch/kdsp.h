/*
 * KDSP (Device Streaming Protocol) — public helpers of the kdsp
 * URLProtocol implemented in kdsp.c. See ../../protocol.md for the
 * wire format and receiver rules.
 *
 * This file is part of the KDSP demo patch for FFmpeg (LGPL).
 */

#ifndef AVFORMAT_KDSP_H
#define AVFORMAT_KDSP_H

#include <stdint.h>

#include "url.h"

/**
 * Average transfer rate of an open kdsp:// stream, in bits per second,
 * measured over the whole connection lifetime (wire bytes, headers
 * included, divided by the time since kdsp_open()).
 *
 * @param h  an open URLContext whose protocol is ff_kdsp_protocol
 * @return   bit rate, 0 while nothing has been received yet,
 *           -1 if h is not a kdsp context
 */
int64_t kdsp_average_speed(URLContext *h);

/**
 * Recent transfer rate of an open kdsp:// stream, in bits per second,
 * measured over a sliding window of roughly the last second (wire
 * bytes, headers included). Returns 0 when no data has arrived for
 * more than ~1.5 s, so a stalled stream reads as 0 instead of keeping
 * its last rate forever.
 *
 * @param h  an open URLContext whose protocol is ff_kdsp_protocol
 * @return   bit rate, or -1 if h is not a kdsp context
 */
int64_t kdsp_realtime_speed(URLContext *h);

#endif /* AVFORMAT_KDSP_H */
