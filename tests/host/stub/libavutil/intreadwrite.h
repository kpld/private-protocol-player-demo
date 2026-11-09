#ifndef STUB_AVUTIL_INTREADWRITE_H
#define STUB_AVUTIL_INTREADWRITE_H
#include <stdint.h>
#define AV_RL32(p) ((uint32_t)((const uint8_t *)(p))[0] | \
                    ((uint32_t)((const uint8_t *)(p))[1] << 8) | \
                    ((uint32_t)((const uint8_t *)(p))[2] << 16) | \
                    ((uint32_t)((const uint8_t *)(p))[3] << 24))
#define AV_RL64(p) ((uint64_t)AV_RL32(p) | ((uint64_t)AV_RL32((const uint8_t *)(p) + 4) << 32))
#endif
