"""Strict framing for the common WAV formats supported by SDL_LoadWAV.

The source file owns sample format/rate/count. This is independent of optional
native sound controls or stale generated metadata echoes. Decoder tests remain
required for actual sample conversion.
"""

import math
import struct


def parse_standard_wav_source_metadata(label: str, data: bytes, errors: list[str]):
    def fail(reason):
        errors.append(f"{label} WAV source {reason}")
        return None

    if len(data) > 0x7fffffff or len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return fail("must be a complete RIFF/WAVE file")
    if struct.unpack_from("<I", data, 4)[0] != len(data) - 8:
        return fail("RIFF length does not match source bytes")
    offset = 12
    fmt = None
    payload = None
    fact = None
    while offset < len(data):
        if len(data) - offset < 8:
            return fail("has a truncated chunk header")
        kind = data[offset:offset + 4]
        size = struct.unpack_from("<I", data, offset + 4)[0]
        start = offset + 8
        end = start + size
        if end + (size & 1) > len(data):
            return fail("has a truncated chunk or missing alignment byte")
        if kind == b"fmt ":
            if fmt is not None or size < 16:
                return fail("has a duplicate or truncated format chunk")
            if size != 16 and (size < 18 or struct.unpack_from("<H", data, start + 16)[0] != size - 18):
                return fail("has an inconsistent format extension size")
            fmt = struct.unpack_from("<HHIIHH", data, start)
        elif kind == b"fact":
            if fact is not None or size < 4:
                return fail("has a duplicate or truncated fact chunk")
            fact = struct.unpack_from("<I", data, start)[0]
        elif kind == b"data":
            if payload is not None:
                return fail("has multiple data chunks unsupported by the shared source loader")
            payload = (start, size)
        offset = end + (size & 1)
    if fmt is None or payload is None:
        return fail("requires format and data chunks")
    codec, channels, rate, byte_rate, align, bits = fmt
    if not ((codec == 1 and bits in (8, 16, 24, 32)) or (codec == 3 and bits == 32)):
        return fail("requires PCM8/16/24/32 or IEEE float32")
    if channels not in (1, 2) or not 0 < rate <= 0x7FFFFFFF:
        return fail("requires mono/stereo with a positive decoder-supported sample rate")
    expected_align = channels * (bits // 8)
    if align != expected_align or byte_rate != rate * align:
        return fail("has inconsistent channel/format alignment or byte rate")
    if payload[1] == 0 or payload[1] % align:
        return fail("must contain nonempty complete PCM frames")
    frames = payload[1] // align
    if fact is not None and fact != frames:
        return fail("fact sample count does not match complete source frames")
    if codec == 3:
        if fact is None:
            return fail("IEEE float source requires its standard fact sample count")
        if any(not math.isfinite(value[0]) for value in struct.iter_unpack("<f", data[payload[0]:payload[0] + payload[1]])):
            return fail("contains unsupported nonfinite float samples")
    return {"audio_format": codec, "channels": channels, "sample_rate_hz": rate,
            "byte_rate": byte_rate, "block_align": align, "bits_per_sample": bits,
            "decoded_sample_count": payload[1] // align, "data_bytes": payload[1]}
