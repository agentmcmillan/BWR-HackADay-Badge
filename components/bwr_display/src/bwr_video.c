// SPDX-License-Identifier: GPL-3.0-or-later
#include "bwr_video.h"
#include <string.h>
static uint32_t little32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
uint32_t bwr_video_crc32(const uint8_t *bytes, size_t size)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
bool bwr_video_parse(const uint8_t *bytes, size_t size, bwr_video *video)
{
    if (!bytes || !video || size < BWR_VIDEO_HEADER || size > BWR_VIDEO_MAX_BYTES)
        return false;
    bool native = !memcmp(bytes, "BWRINK1", 8);
    bool classic = !memcmp(bytes, "BWRVID1", 8);
    unsigned width = (unsigned)bytes[8] | (unsigned)bytes[9] << 8;
    unsigned height = (unsigned)bytes[10] | (unsigned)bytes[11] << 8;
    unsigned count = little32(bytes + 12), timing = little32(bytes + 16);
    if ((!native && !classic) || !count || little32(bytes + 24) || little32(bytes + 28))
        return false;
    unsigned stride;
    if (native) {
        if (width != 960 || height != 624 || count > 36 || timing < 1000 || timing > 30000)
            return false;
        stride = 74880;
    } else {
        if (width != 240 || height != 156 || count > BWR_VIDEO_MAX_FRAMES || timing < 500 ||
            timing > 30000)
            return false;
        stride = BWR_VIDEO_FRAME;
    }
    if (size != BWR_VIDEO_HEADER + (size_t)count * stride ||
        bwr_video_crc32(bytes + BWR_VIDEO_HEADER, size - BWR_VIDEO_HEADER) != little32(bytes + 20))
        return false;
    *video = (bwr_video){.frames = bytes + BWR_VIDEO_HEADER,
                         .count = count,
                         .fps_milli = native ? 0 : timing,
                         .frame_bytes = stride,
                         .hold_ms = native ? timing : 0,
                         .native_ink = native};
    return true;
}
bool bwr_video_draw(const bwr_video *video, unsigned frame, uint8_t *canvas)
{
    if (!video || !canvas || !video->frames || frame >= video->count)
        return false;
    const uint8_t *source = video->frames + (size_t)frame * video->frame_bytes;
    if (video->native_ink) {
        memcpy(canvas + 48 * 120, source, 74880);
        return true;
    }
    static const uint8_t pairs[4] = {0, 15, 240, 255};
    for (unsigned row = 0; row < 156; row++) {
        uint8_t *out = canvas + (48 + row * 4) * 120;
        for (unsigned col = 0; col < 30; col++) {
            uint8_t bits = source[row * 30 + col];
            for (unsigned pair = 0; pair < 4; pair++)
                out[col * 4 + pair] = pairs[(bits >> (6 - pair * 2)) & 3];
        }
        for (unsigned repeat = 1; repeat < 4; repeat++)
            memcpy(out + repeat * 120, out, 120);
    }
    return true;
}

size_t bwr_video_unpack(const uint8_t *packed, size_t size, uint8_t *output, size_t capacity)
{
    if (!packed || !output || size < 16 || memcmp(packed, "BWRPACK1", 8))
        return 0;
    size_t expected = little32(packed + 8);
    if (expected < BWR_VIDEO_HEADER || expected > BWR_VIDEO_MAX_BYTES || capacity < expected)
        return 0;
    size_t input = 16, used = 0;
    while (input < size) {
        unsigned code = packed[input++];
        if (code <= 127) {
            size_t count = code + 1;
            if (count > size - input || count > expected - used)
                return 0;
            memcpy(output + used, packed + input, count);
            input += count;
            used += count;
        } else if (code >= 129) {
            size_t count = 257 - code;
            if (input == size || count > expected - used)
                return 0;
            memset(output + used, packed[input++], count);
            used += count;
        } else
            return 0;
    }
    if (used != expected || bwr_video_crc32(output, used) != little32(packed + 12))
        return 0;
    bwr_video video;
    return bwr_video_parse(output, used, &video) ? used : 0;
}
