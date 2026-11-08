// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bwr_video.h"
void video_init(void);
bool video_accept(uint8_t *bytes,
                  size_t size); // takes ownership only on success
bool video_request(unsigned mhz, unsigned frames);
void video_stop(bool leave);
void video_status(char *out, size_t capacity);
bool video_active(void);
bool ui_video_open(void);
void app_video(void);
