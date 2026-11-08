# SPDX-License-Identifier: GPL-3.0-or-later
"""Local authenticated stop-and-wait serial RPC for the bounded test interfaces."""
import base64
import json
import re
import time
import serial


class BadgeRPC:
    def __init__(self, port, directory):
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.log = (directory / "serial.log").open("w")
        self.timed = (directory / "serial-timed.jsonl").open("w")
        self.rx_pending = bytearray()
        self.sequence = 0
        self.session = None
        self.fault = False
        self.home_ready = False

    def read(self):
        data = self.serial.readline()
        if not data:
            return ""
        self.rx_pending.extend(data)
        if len(self.rx_pending) > 8192:
            raise RuntimeError("Oversized serial line")
        if not self.rx_pending.endswith(b"\n"):
            return ""
        line = self.rx_pending.decode(errors="replace").strip()
        self.rx_pending.clear()
        if not line:
            return ""
        self.log.write(line + "\n")
        self.log.flush()
        self.timed.write(
            json.dumps({"monotonic": time.monotonic(), "line": line}) + "\n"
        )
        self.timed.flush()
        if "Home ready:" in line:
            self.home_ready = True
        if "Panel stopped;" in line or "Panel BUSY timeout" in line:
            self.fault = True
            raise RuntimeError("Panel fail-stop: do not reset or retry automatically")
        return line

    def call(self, verb, *arguments, timeout=10):
        if self.fault:
            raise RuntimeError("Panel fail-stop: no further commands are allowed")
        self.sequence += 1
        fields = (
            ["@BC1", str(self.sequence), verb]
            + ([] if verb == "hello" else [self.session])
            + list(map(str, arguments))
        )
        self.serial.write((" ".join(fields) + "\n").encode())
        self.serial.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            match = re.fullmatch(r"@BC1 (\d+) (\d+) (\S+)", self.read())
            if match and int(match[1]) == self.sequence:
                value = base64.b64decode(match[3], validate=True).decode()
                if int(match[2]) != 200:
                    raise RuntimeError(f"{verb}: {match[2]}: {value}")
                try:
                    return json.loads(value)
                except json.JSONDecodeError:
                    return value
        raise TimeoutError(f"{verb} did not reply")

    def connect(self, expected_version):
        hello = self.call("hello")
        self.session = hello["session"]
        if hello["version"] != expected_version:
            raise RuntimeError("Unexpected test firmware")
        deadline = time.monotonic() + 20
        while not self.home_ready and time.monotonic() < deadline:
            self.read()
        return hello

    def close(self):
        self.serial.close()
        self.log.close()
        self.timed.close()
