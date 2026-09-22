"""Ogg/Vorbis framing admission. It does not decode setup or audio packets.

References: https://xiph.org/ogg/doc/framing.html and
https://xiph.org/vorbis/doc/Vorbis_I_spec.html . Runtime PCM proof uses stb_vorbis.
The current shared runtime owns one logical stream; multiplex/chaining is explicit
unsupported input, not silently ignored content.
"""

import struct


def ogg_page_crc(page: bytes) -> int:
    crc = 0
    for index, value in enumerate(page):
        if 22 <= index < 26:
            value = 0
        crc ^= value << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
    return crc


def parse_vorbis_source_metadata(label: str, data: bytes, errors: list[str]):
    def fail(reason):
        errors.append(f"{label} Ogg Vorbis source {reason}")
        return None

    offset = 0
    serial = None
    sequence = 0
    pending = bytearray()
    headers = []
    audio_packets = 0
    eos = False
    last_granule = None
    if not data or len(data) > 0x7FFFFFFF:
        return fail("must be nonempty and fit the decoder byte domain")
    while offset < len(data):
        if eos or len(data) - offset < 27:
            return fail("has trailing data, chaining, or a truncated page header")
        if data[offset:offset + 5] != b"OggS\0":
            return fail("has an invalid capture pattern or version")
        flags = data[offset + 5]
        granule, page_serial, page_sequence, checksum = struct.unpack_from("<QIII", data, offset + 6)
        count = data[offset + 26]
        header_end = offset + 27 + count
        if flags & ~7 or header_end > len(data):
            return fail("has invalid flags or a truncated lacing table")
        lacing = data[offset + 27:header_end]
        page_end = header_end + sum(lacing)
        if page_end > len(data):
            return fail("has a truncated page body")
        if ogg_page_crc(data[offset:page_end]) != checksum:
            return fail("has a page checksum mismatch")
        if serial is None:
            if flags != 2 or page_sequence != 0:
                return fail("must begin with an uncontinued BOS page at sequence zero")
            serial = page_serial
        elif page_serial != serial or flags & 2:
            return fail("uses unsupported multiplexed or chained logical streams")
        if page_sequence != sequence or bool(flags & 1) != bool(pending):
            return fail("has a missing page or inconsistent packet continuation")
        sequence = (sequence + 1) & 0xFFFFFFFF
        payload = header_end
        completed = 0
        headers_at_page_start = len(headers)
        for size in lacing:
            pending.extend(data[payload:payload + size])
            payload += size
            if size < 255:
                packet = bytes(pending)
                pending.clear()
                completed += 1
                if len(headers) < 3:
                    expected = (1, 3, 5)[len(headers)]
                    if len(packet) < 7 or packet[:7] != bytes([expected]) + b"vorbis":
                        return fail("is missing ordered Vorbis identification/comment/setup headers")
                    headers.append(packet)
                else:
                    if headers_at_page_start < 3:
                        return fail("audio packets must start after the setup header page")
                    if not packet or packet[0] & 1:
                        return fail("has an invalid audio packet type")
                    audio_packets += 1
        if offset == 0 and (len(headers) != 1 or pending or granule != 0):
            return fail("identification header must be alone on its first page")
        if completed == 0 and granule != 0xFFFFFFFFFFFFFFFF:
            return fail("page without a completed packet has a sample position")
        if headers_at_page_start < 3 and completed and granule != 0:
            return fail("header page has a nonzero sample position")
        if flags & 4 and (granule == 0xFFFFFFFFFFFFFFFF or granule == 0):
            return fail("end-of-stream page requires a positive final sample position")
        if granule != 0xFFFFFFFFFFFFFFFF:
            if last_granule is not None and granule < last_granule:
                return fail("has decreasing sample positions")
            last_granule = granule
        eos = bool(flags & 4)
        if eos and pending:
            return fail("ends with an incomplete packet")
        offset = page_end
    if not eos or len(headers) != 3 or not audio_packets or last_granule is None:
        return fail("lacks complete headers, audio packets, or the end-of-stream page")
    identity = headers[0]
    if len(identity) != 30 or struct.unpack_from("<I", identity, 7)[0] != 0:
        return fail("has an invalid identification version or size")
    channels = identity[11]
    rate = struct.unpack_from("<I", identity, 12)[0]
    small, large = identity[28] & 15, identity[28] >> 4
    if channels not in (1, 2) or not 0 < rate <= 0x7FFFFFFF or not 6 <= small <= large <= 13 or identity[29] != 1:
        return fail("has invalid channel, rate, block size, or identification framing")
    if last_granule > 0x7fffffff // (channels * 2):
        return fail("decoded frame count exceeds the signed PCM byte domain")
    if len(headers[2]) <= 7:
        return fail("has no setup header payload")
    comment = headers[1]
    try:
        cursor = 7
        vendor_size = struct.unpack_from("<I", comment, cursor)[0]
        cursor += 4
        if vendor_size > len(comment) - cursor:
            return fail("has a truncated comment vendor")
        comment[cursor:cursor + vendor_size].decode("utf-8")
        cursor += vendor_size
        comment_count = struct.unpack_from("<I", comment, cursor)[0]
        cursor += 4
        if comment_count > (len(comment) - cursor) // 4:
            return fail("has a truncated comment count")
        for _ in range(comment_count):
            size = struct.unpack_from("<I", comment, cursor)[0]
            cursor += 4
            if size > len(comment) - cursor:
                return fail("has a truncated comment")
            comment[cursor:cursor + size].decode("utf-8")
            cursor += size
        if cursor >= len(comment) or comment[cursor] & 1 == 0:
            return fail("has no comment framing bit")
    except (struct.error, UnicodeDecodeError):
        return fail("has malformed comment lengths or UTF-8")
    return {"sample_rate_hz": rate, "channels": channels,
            "decoded_sample_count": last_granule, "ogg_page_count": sequence}
