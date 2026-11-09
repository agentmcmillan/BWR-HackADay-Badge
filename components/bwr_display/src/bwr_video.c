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
    unsigned flags = little32(bytes + 24), loop_start = little32(bytes + 28);
    if ((!native && !classic) || !count)
        return false;
    unsigned stride;
    if (native) {
        if (flags || loop_start)
            return false;
        if (width != 960 || height != 624 || count > 36 || (timing != 0 && timing < 1000) ||
            timing > 30000)
            return false;
        stride = 74880;
    } else {
        if ((flags == 0 && loop_start != 0) ||
            (flags == BWR_VIDEO_FLAG_CYCLIC_BASE &&
             (loop_start < 1 || loop_start >= count)) ||
            (flags != 0 && flags != BWR_VIDEO_FLAG_CYCLIC_BASE))
            return false;
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
                         .loop_start = native ? 0 : loop_start,
                         .frame_bytes = stride,
                         .hold_ms = native ? timing : 0,
                         .native_ink = native};
    return true;
}
bool bwr_video_delta_rows(const bwr_video *video, unsigned frame, unsigned previous_frame,
                          unsigned *first_row, unsigned *last_row)
{
    if (!video || !video->frames || !first_row || !last_row || frame >= video->count ||
        (previous_frame != UINT32_MAX && previous_frame >= video->count))
        return false;
    unsigned rows = video->native_ink ? 624 : 156;
    unsigned row_bytes = video->native_ink ? 120 : 30;
    static const uint8_t blank[120];
    const uint8_t *current = video->frames + (size_t)frame * video->frame_bytes;
    const uint8_t *previous = previous_frame == UINT32_MAX
                                  ? NULL
                                  : video->frames + (size_t)previous_frame * video->frame_bytes;
    *first_row = rows;
    *last_row = 0;
    for (unsigned row = 0; row < rows; row++) {
        const uint8_t *p = current + (size_t)row * row_bytes;
        bool changed = previous ? memcmp(p, previous + (size_t)row * row_bytes, row_bytes) != 0
                                : memcmp(p, blank, row_bytes) != 0;
        if (changed) {
            if (*first_row == rows)
                *first_row = row;
            *last_row = row;
        }
    }
    return true;
}

bool bwr_video_draw_rows(const bwr_video *video, unsigned frame, unsigned first_row,
                         unsigned last_row, uint8_t *canvas)
{
    unsigned rows = video && video->native_ink ? 624 : 156;
    if (!video || !canvas || !video->frames || frame >= video->count || first_row > last_row ||
        last_row >= rows)
        return false;
    const uint8_t *source = video->frames + (size_t)frame * video->frame_bytes;
    if (video->native_ink) {
        memcpy(canvas + (48 + first_row) * 120, source + first_row * 120,
               (size_t)(last_row - first_row + 1) * 120);
        return true;
    }
    static const uint8_t pairs[4] = {0, 15, 240, 255};
    for (unsigned row = first_row; row <= last_row; row++) {
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

bool bwr_video_draw_rows_smol(const bwr_video *video, unsigned frame, unsigned first_row,
                              unsigned last_row, uint8_t *canvas)
{
    // The display body is 960 x 624 below a 48-row bar. Half each dimension
    // is a centered viewport with one quarter of the full-size video area.
    enum { stride = 120, x_byte = 30, y = 204 };
    static const uint8_t double_nibble[16] = {
        0x00, 0x03, 0x0c, 0x0f, 0x30, 0x33, 0x3c, 0x3f,
        0xc0, 0xc3, 0xcc, 0xcf, 0xf0, 0xf3, 0xfc, 0xff,
    };
    if (!video || !canvas || !video->frames || video->native_ink || frame >= video->count ||
        first_row > last_row || last_row >= 156)
        return false;
    const uint8_t *source = video->frames + (size_t)frame * video->frame_bytes;
    for (unsigned row = first_row; row <= last_row; row++) {
        uint8_t *out = canvas + (y + row * 2) * stride + x_byte;
        for (unsigned col = 0; col < 30; col++) {
            uint8_t bits = source[row * 30 + col];
            out[col * 2] = double_nibble[bits >> 4];
            out[col * 2 + 1] = double_nibble[bits & 15];
        }
        memcpy(out + stride, out, 60);
    }
    return true;
}

bool bwr_video_draw(const bwr_video *video, unsigned frame, uint8_t *canvas)
{
    return bwr_video_draw_rows(video, frame, 0, video && video->native_ink ? 623 : 155, canvas);
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
