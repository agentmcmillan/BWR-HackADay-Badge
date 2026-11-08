# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import os, subprocess, tempfile, json

root = Path(__file__).resolve().parents[1]
s = (
    (root / "components/bwr_display/src/refresh_remote.c")
    .read_text()
    .replace('#include "freertos/FreeRTOS.h"', "")
)
pre = r"""
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static int locked;
#define taskENTER_CRITICAL(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define taskEXIT_CRITICAL(p) do{(void)(p);assert(locked);locked=0;}while(0)
"""
post = r"""
int main(void){refresh_request q;char json[1024];refresh_result result={.calls=60,.pulls=60};bool leave;
 assert(!refresh_remote_submit(0,4,60));refresh_remote_active(true);
 assert(!refresh_remote_submit(5,4,60)&&!refresh_remote_submit(0,40,60)&&!refresh_remote_submit(0,4,61)&&!refresh_remote_submit(0,4,0));
 assert(refresh_remote_submit(2,5,60)&&!refresh_remote_submit(0,4,60));
 assert(refresh_remote_take(&q)&&q.pattern==2&&q.mhz==5&&q.frames==60);
 assert(!refresh_remote_take(&q)&&!refresh_remote_submit(0,4,60));
 refresh_remote_finish(2,5,&result,42000000);refresh_remote_status(json,sizeof json);puts(json);
 assert(refresh_remote_submit(0,8,12));refresh_remote_stop(false);
 assert(!refresh_remote_take(&q)&&refresh_remote_control(&leave)&&!leave);
 refresh_remote_running(false);assert(refresh_remote_submit(0,20,1));
 assert(refresh_remote_take(&q));refresh_remote_stop(true);refresh_remote_stop(false);assert(refresh_remote_control(&leave)&&leave);
 refresh_remote_active(false);assert(!refresh_remote_is_active()&&!refresh_remote_submit(0,4,60)&&!locked);
 puts("PASS bounded remote benchmark queue, concurrency exclusion, status snapshot, cancel/exit and invalid-input rejection");}
"""
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p / "test.c").write_text(pre + s + post)
    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            "-std=c11",
            "-O1",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-I",
            str(root / "components/bwr_display/include"),
            str(p / "test.c"),
            "-o",
            str(p / "test"),
        ],
        check=True,
    )
    lines = subprocess.check_output([str(p / "test")], text=True).splitlines()
    data = json.loads(lines[0])
    assert (
        data["calls"] == 60
        and data["sequence"] == 1
        and not data["running"]
        and data["elapsed_us"] == 42000000
    )
    print(lines[-1])
