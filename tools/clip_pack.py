# SPDX-License-Identifier: GPL-3.0-or-later
"""Optional bounded PackBits envelope shared by local clip converters."""
import struct
import zlib


def packbits_envelope(data):
    packed = bytearray()
    index = 0
    while index < len(data):
        run = 1
        while (
            index + run < len(data) and data[index + run] == data[index] and run < 128
        ):
            run += 1
        if run >= 3:
            packed.extend((257 - run, data[index]))
            index += run
            continue
        start = index
        index += run
        while index < len(data) and index - start < 128:
            run = 1
            while (
                index + run < len(data)
                and data[index + run] == data[index]
                and run < 128
            ):
                run += 1
            if run >= 3:
                break
            index += min(run, 128 - (index - start))
        packed.append(index - start - 1)
        packed.extend(data[start:index])
    return struct.pack("<8sII", b"BWRPACK1", len(data), zlib.crc32(data)) + packed
