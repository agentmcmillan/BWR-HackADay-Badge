# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual app adapters with the public port header, without a badge OS."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
stubs = {
    "esp_attr.h": "#pragma once\n#define EXT_RAM_BSS_ATTR\n",
    "esp_timer.h": "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "esp_random.h": "#pragma once\n#include <stdint.h>\nuint32_t esp_random(void);\n",
    "esp_log.h": "#pragma once\n#define ESP_LOGI(...) ((void)0)\n",
    "freertos/FreeRTOS.h": "#pragma once\n#include <assert.h>\n#define portMAX_DELAY (-1)\n#define configASSERT assert\n",
    "freertos/semphr.h": "#pragma once\ntypedef void *SemaphoreHandle_t;\nSemaphoreHandle_t xSemaphoreCreateMutex(void);\nint xSemaphoreTake(SemaphoreHandle_t,int);\nint xSemaphoreGive(SemaphoreHandle_t);\n",
}
with tempfile.TemporaryDirectory() as directory:
    sdk = Path(directory)
    for name, contents in stubs.items():
        path = sdk / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
    for name in ["app_savers.c", "app_refresh_lab.c", "app_video.c"]:
        subprocess.run(
            [
                os.environ.get("CC", "cc"),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsyntax-only",
                "-I",
                str(sdk),
                "-I",
                str(root / "examples/esp-idf"),
                "-I",
                str(root / "components/bwr_display/include"),
                str(root / "examples/esp-idf" / name),
            ],
            check=True,
        )
print(
    "PASS app adapter contract: actual sources include the public port header and compile with minimal SDK declarations"
)
