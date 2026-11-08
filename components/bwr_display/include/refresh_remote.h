// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "refresh_lab.h"
#include <stddef.h>
typedef struct {
    unsigned pattern, mhz, frames;
} refresh_request;
void refresh_remote_active(bool active);
bool refresh_remote_is_active(void);
bool refresh_remote_submit(unsigned pattern, unsigned mhz, unsigned frames);
bool refresh_remote_take(refresh_request *request);
void refresh_remote_running(bool running);
void refresh_remote_finish(unsigned pattern, unsigned mhz, const refresh_result *r,
                           uint64_t elapsed);
void refresh_remote_stop(bool exit_app);
bool refresh_remote_control(bool *exit_app);
void refresh_remote_status(char *out, size_t size);
bool ui_refresh_lab_open(void);
