// SPDX-License-Identifier: GPL-3.0-or-later
#include "bwr_video.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static uint8_t file[BWR_VIDEO_HEADER + BWR_VIDEO_FRAME * 2], canvas[80640];
static void word(unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        file[offset + i] = (uint8_t)(value >> (i * 8));
}
static void packed_check(void)
{
    uint8_t packed[10000] = {0}, decoded[sizeof file];
    memcpy(packed, "BWRPACK1", 8);
    for (unsigned i = 0; i < 4; i++) {
        packed[8 + i] = (uint8_t)(sizeof file >> (8 * i));
        packed[12 + i] = (uint8_t)(bwr_video_crc32(file, sizeof file) >> (8 * i));
    }
    size_t used = 16, pos = 0;
    while (pos < sizeof file) {
        size_t n = sizeof file - pos;
        if (n > 128)
            n = 128;
        packed[used++] = (uint8_t)(n - 1);
        memcpy(packed + used, file + pos, n);
        used += n;
        pos += n;
    }
    assert(bwr_video_unpack(packed, used, decoded, sizeof decoded) == sizeof file &&
           !memcmp(decoded, file, sizeof file));
    assert(!bwr_video_unpack(packed, used, decoded, sizeof decoded - 1));
    for (size_t i = 0; i < used; i++)
        assert(!bwr_video_unpack(packed, i, decoded, sizeof decoded));
    packed[16] = 128;
    assert(!bwr_video_unpack(packed, used, decoded, sizeof decoded));
    // Literal header followed by repeated zero payload exercises the repeat branch.
    memset(file + 32, 0, sizeof file - 32);
    word(20, bwr_video_crc32(file + 32, sizeof file - 32));
    for (unsigned i = 0; i < 4; i++)
        packed[12 + i] = (uint8_t)(bwr_video_crc32(file, sizeof file) >> (8 * i));
    used = 16;
    packed[used++] = 31;
    memcpy(packed + used, file, 32);
    used += 32;
    pos = 32;
    while (pos < sizeof file) {
        size_t n = sizeof file - pos;
        if (n > 128)
            n = 128;
        assert(n >= 2);
        packed[used++] = (uint8_t)(257 - n);
        packed[used++] = 0;
        pos += n;
    }
    assert(bwr_video_unpack(packed, used, decoded, sizeof decoded) == sizeof file &&
           !memcmp(decoded, file, sizeof file));
}
static void native_check(void)
{
    size_t size = 32 + 2 * 74880;
    uint8_t *bytes = calloc(1, size);
    assert(bytes);
    memcpy(bytes, "BWRINK1", 8);
    bytes[8] = 0xc0;
    bytes[9] = 3;
    bytes[10] = 0x70;
    bytes[11] = 2;
    bytes[12] = 2;
    bytes[16] = 0xa0;
    bytes[17] = 0x0f;
    for (size_t i = 32; i < size; i++)
        bytes[i] = (uint8_t)(i * 71);
    uint32_t crc = bwr_video_crc32(bytes + 32, size - 32);
    for (unsigned i = 0; i < 4; i++)
        bytes[20 + i] = (uint8_t)(crc >> (8 * i));
    bwr_video clip;
    assert(bwr_video_parse(bytes, size, &clip) && clip.native_ink && clip.hold_ms == 4000 &&
           clip.frame_bytes == 74880);
    memset(canvas, 0xa5, sizeof canvas);
    assert(bwr_video_draw(&clip, 1, canvas));
    for (unsigned i = 0; i < 48 * 120; i++)
        assert(canvas[i] == 0xa5);
    assert(!memcmp(canvas + 48 * 120, bytes + 32 + 74880, 74880));
    assert(!bwr_video_parse(bytes, size - 1, &clip));
    bytes[12] = 37;
    assert(!bwr_video_parse(bytes, size, &clip));
    bytes[12] = 2;
    bytes[16] = 0;
    bytes[17] = 0;
    assert(bwr_video_parse(bytes, size, &clip) && clip.native_ink && clip.hold_ms == 0);
    assert(bwr_video_draw(&clip, 1, canvas));
    assert(!memcmp(canvas + 48 * 120, bytes + 32 + 74880, 74880));
    // Only exactly zero or a fully bounded long hold is accepted.
    bytes[16] = 1;
    assert(!bwr_video_parse(bytes, size, &clip));
    bytes[16] = 0xe7;
    bytes[17] = 3; // 999 ms
    assert(!bwr_video_parse(bytes, size, &clip));
    free(bytes);
}
static void delta_check(void)
{
    memset(file + 32, 0, sizeof file - 32);
    memset(file + 32 + BWR_VIDEO_FRAME + 12 * 30, 0xff, 8 * 30);
    word(20, bwr_video_crc32(file + 32, sizeof file - 32));
    bwr_video video;
    assert(bwr_video_parse(file, sizeof file, &video));
    word(24, BWR_VIDEO_FLAG_CYCLIC_BASE);
    word(28, 1);
    assert(bwr_video_parse(file, sizeof file, &video) && video.loop_start == 1);
    word(28, 2);
    assert(!bwr_video_parse(file, sizeof file, &video));
    word(24, 2);
    word(28, 1);
    assert(!bwr_video_parse(file, sizeof file, &video));
    word(24, 0);
    assert(!bwr_video_parse(file, sizeof file, &video));
    word(28, 0);
    assert(bwr_video_parse(file, sizeof file, &video) && video.loop_start == 0);
    unsigned first, last;
    assert(bwr_video_delta_rows(&video, 0, UINT32_MAX, &first, &last) && first > last);
    assert(bwr_video_delta_rows(&video, 1, 0, &first, &last) && first == 12 && last == 19);
    assert(bwr_video_delta_rows(&video, 1, UINT32_MAX, &first, &last) && first == 12 &&
           last == 19);
    assert(!bwr_video_delta_rows(&video, 2, 0, &first, &last));
    assert(!bwr_video_delta_rows(&video, 1, 2, &first, &last));
    memset(canvas, 0xa5, sizeof canvas);
    assert(bwr_video_draw_rows(&video, 1, first, last, canvas));
    for (unsigned y = 0; y < 672; y++)
        for (unsigned x = 0; x < 120; x++) {
            uint8_t expected = y >= 48 + 12 * 4 && y < 48 + 20 * 4 ? 0xff : 0xa5;
            assert(canvas[y * 120 + x] == expected);
        }
    assert(!bwr_video_draw_rows(&video, 1, 20, 19, canvas));
    assert(!bwr_video_draw_rows(&video, 1, 0, 156, canvas));
}
int main(void)
{
    assert(bwr_video_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926u);
    memcpy(file, "BWRVID1", 8);
    file[8] = 240;
    file[10] = 156;
    word(12, 2);
    word(16, 1500);
    for (size_t i = 32; i < sizeof file; i++)
        file[i] = (uint8_t)(i * 137u);
    word(20, bwr_video_crc32(file + 32, sizeof file - 32));
    bwr_video video;
    assert(bwr_video_parse(file, sizeof file, &video) && video.count == 2 &&
           video.fps_milli == 1500);
    memset(canvas, 0xa5, sizeof canvas);
    for (unsigned frame = 0; frame < 2; frame++) {
        assert(bwr_video_draw(&video, frame, canvas));
        for (unsigned i = 0; i < 48 * 120; i++)
            assert(canvas[i] == 0xa5);
        for (unsigned y = 48; y < 672; y++)
            for (unsigned x = 0; x < 960; x++) {
                unsigned src = (y - 48) / 4 * 240 + x / 4;
                unsigned expected = (file[32 + frame * 4680 + src / 8] >> (7 - src % 8)) & 1;
                assert(((canvas[y * 120 + x / 8] >> (7 - x % 8)) & 1) == expected);
            }
    }
    assert(!bwr_video_draw(&video, 2, canvas));
    for (size_t length = 0; length < sizeof file; length++)
        assert(!bwr_video_parse(file, length, &video));
    for (size_t i = 0; i < sizeof file; i += 11) {
        file[i] ^= 1;
        assert(!bwr_video_parse(file, sizeof file, &video));
        file[i] ^= 1;
    }
    packed_check();
    delta_check();
    word(12, 601);
    assert(!bwr_video_parse(file, sizeof file, &video));
    native_check();
    puts("PASS native/legacy video: CRC known vector, strict format/size/count checks, "
         "corrupt/truncated input "
         "rejection, exact full/delta-row expansion and preserved status bar");
}
