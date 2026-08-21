#ifndef ATM_MP3_H
#define ATM_MP3_H

/* Bounded MPEG Audio Layer III stream inspection. This parser validates and
 * describes frames; it deliberately does not decode MPEG psychoacoustic data
 * into PCM and does not claim audio playback without a PCM hardware sink. */

#include <stdint.h>

#define ATM_MP3_SCAN_MAX (128u * 1024u)

typedef struct {
    uint32_t first_frame_offset;
    uint32_t first_frame_size;
    uint32_t frame_count;
    uint32_t sample_rate_hz;
    uint32_t bitrate_kbps;
    uint32_t samples_per_frame;
    uint32_t duration_ms_estimate;
    uint8_t  mpeg_version; /* 1, 2, or 25 for MPEG 2.5 */
    uint8_t  channels;     /* 1 or 2 */
    uint8_t  has_id3v2;
    uint8_t  vbr;
} atm_mp3_info_t;

/* Parses a bounded memory prefix. It requires three valid Layer III frame
 * headers with consistent version/sample rate/channel count. The duration is
 * an estimate over the inspected bytes, exact only when the complete stream
 * prefix is supplied and frame layout is constant. */
int atm_mp3_probe(const uint8_t *data,uint32_t size,atm_mp3_info_t *out);

/* Deterministic format-parser regression; no audio hardware is needed. */
int atm_mp3_selftest(void);

#endif
