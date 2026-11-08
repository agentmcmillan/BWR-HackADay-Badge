# USB benchmark control

The existing stop-and-wait USB settings protocol is extended with a fixed set
of benchmark commands. It cannot execute arbitrary code, write arbitrary
registers, inject general buttons, or select arbitrary clocks.

Requests are ASCII lines:

```
@BC1 <id> hello
@BC1 <id> <verb> <session> [arguments]
```

Responses are `@BC1 <id> <status> <base64-encoded body>`. The hello response
contains the firmware version and a session token. Treat that token as local
session data. Ordinary firmware log lines may appear between response lines.

| Verb | Arguments | Behavior |
| --- | --- | --- |
| `bench_open` | none | Open Refresh Lab from Home, or acknowledge an already open lab |
| `bench_status` | none | JSON snapshot of active/pending/running state, sequence and last results |
| `bench_run` | pattern, MHz, frame count | Queue one bounded trial |
| `bench_stop` | none | Stop at a frame boundary, leaving the lab open |
| `bench_exit` | none | Stop at a frame boundary and return Home |

Patterns are 0 moving block, 1 tall sweep, 2 checkerboard, 3 black/white,
and 4 unchanged scene. Allowed clocks are 4, 5, 8, 12 and 20 MHz; frame counts
are 1 through 60. One pending/running request excludes another. UI/display
work stays on the app task; the USB task only validates commands and queues
state. BUSY fault handling is unchanged and cannot be bypassed through this RPC.

Some macOS USB-serial opens reset an ESP32-S3. Start the controller while the
panel is idle; keep one serial connection open for the whole test sequence.
The controller waits for Home initialization before requesting the lab because
Home flushes early button events. Opening the serial port during a refresh is
not a supported way to start a trial.
