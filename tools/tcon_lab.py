# SPDX-License-Identifier: GPL-3.0-or-later
"""Run fixed, stop-on-fault iTC97 register suites against compatible firmware."""
from pathlib import Path
import argparse
import datetime
import json

from badge_rpc import BadgeRPC


TCON = [
    (0x25, 0x11, 128),
    (0x25, -1, 128),
    (0x24, -1, 128),
    (0x27, -1, 128),
    (0x21, -1, 128),
    (0x2D, -1, 128),
    (0x35, -1, 128),
    (0x05, -1, 128),
    (0x65, -1, 128),
    (0xA5, -1, 128),
    (0x25, -1, 624),
    (0x00, -1, 624),
]
DCTL = [
    (-1, 128),
    (0, 128),
    (1, 128),
    (8, 128),
    (9, 128),
    (8, 128),
    (0, 128),
    (9, 128),
    (-1, 128),
    (0, 128),
    (8, 128),
    (-1, 128),
    (9, 128),
    (8, 128),
    (9, 128),
    (0, 128),
    (-1, 128),
    (-1, 624),
    (9, 624),
    (9, 624),
    (-1, 624),
]
DRFW = [
    (0xA9, 128),
    (0xA8, 128),
    (0xA8, 128),
    (0xA9, 128),
    (0xA8, 128),
    (0xA9, 128),
    (0xA9, 128),
    (0xA8, 128),
    (0xA9, 128),
    (0xA8, 128),
    (0xA8, 128),
    (0xA9, 128),
    (0xA8, 128),
    (0xA9, 128),
    (0xA9, 128),
    (0xA8, 128),
    (0xA8, 624),
    (0xA9, 624),
    (0xA8, 624),
    (0xA9, 624),
]
SCAN_ROWS = (16, 32, 64, 128, 256, 384, 624)
SCAN = [(0xA9, rows) for rows in (*SCAN_ROWS, *reversed(SCAN_ROWS), *SCAN_ROWS)]
HOLD = [(rows, 8) for rows in (16, 8, 4, 2, 1)] + [(1, 120)]
PINGPONG = [(rows, 120) for rows in (16, 8, 4, 2, 1)]


def call_profile(rpc, suite, profile):
    if suite in ("hold", "pingpong"):
        rows, pulls = profile
        verb = "scan_hold" if suite == "hold" else "scan_pingpong"
        return rpc.call(verb, rows, pulls, timeout=30)
    if suite == "tcon":
        tcon, oscillator, rows = profile
        return rpc.call("tcon_trial", hex(tcon), oscillator, rows, timeout=20)
    value, rows = profile
    verb = "dctl_trial" if suite == "dctl" else "drfw_trial"
    argument = value if suite == "dctl" else hex(value)
    return rpc.call(verb, argument, rows, timeout=20)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--version", required=True)
    parser.add_argument(
        "--suite",
        required=True,
        choices=("otp", "tcon", "dctl", "drfw", "scan", "hold", "pingpong"),
    )
    args = parser.parse_args()

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = Path("artifacts") / f"tcon-{args.suite}-{stamp}"
    out.mkdir(parents=True)
    rpc = None
    results = []
    hello = None
    failure = None
    try:
        rpc = BadgeRPC(args.port, out)
        hello = rpc.connect(args.version)
        if args.suite == "otp":
            results.append({"result": rpc.call("otp_read", timeout=10)})
        else:
            profiles = {
                "tcon": TCON,
                "dctl": DCTL,
                "drfw": DRFW,
                "scan": SCAN,
                "hold": HOLD,
                "pingpong": PINGPONG,
            }[args.suite]
            for profile in profiles:
                result = call_profile(rpc, args.suite, profile)
                row = {"profile": profile, "result": result}
                results.append(row)
                (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
                print(json.dumps(row), flush=True)
    except Exception as exc:
        failure = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        if rpc is not None:
            rpc.close()
        record = {
            "hello": hello,
            "suite": args.suite,
            "results": results,
            "failure": failure,
        }
        (out / "results.json").write_text(json.dumps(record, indent=2) + "\n")

    print("SAVED", out)


if __name__ == "__main__":
    main()
