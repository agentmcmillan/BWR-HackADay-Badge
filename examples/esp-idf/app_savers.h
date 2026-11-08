// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "saver_shapes.h"
void app_spirograph(void);
void app_dvd(void);
// Saver gallery owns navigation; returns the button that leaves this mode.
int app_shapes_run(unsigned kind, bool gallery);
