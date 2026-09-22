"""Real archive audio-contract tests; framing witnesses are not PCM decoding."""

import io
import os
import json
from pathlib import Path
import struct
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_archive_conformance import (
    AUDIO_WAV_NATIVE_FIELDS, parse_mp3_source_metadata, parse_wav_source_metadata,
    validate_archive_bytes,
    validate_audio_source_contract,
)
from audio_ogg_source import parse_vorbis_source_metadata, ogg_page_crc

FIXTURE_ROOT = Path(os.environ.get("PD_AUDIO_FIXTURE_DIR",
    str(Path(__file__).resolve().parents[1] / "tests/fixtures/audio/vorbis")))
MP3_FIXTURE_ROOT = Path(os.environ.get("PD_MP3_FIXTURE_DIR",
    str(Path(__file__).resolve().parents[1] / "tests/fixtures/audio/mp3")))


def id3v24_footer_tag(text=b"tone"):
    def synchsafe(size):
        return bytes((size >> shift) & 127 for shift in (21, 14, 7, 0))
    body = b"TIT2" + synchsafe(len(text) + 1) + b"\0\0\x03" + text
    header = b"ID3\x04\0\x10" + synchsafe(len(body))
    return header + body + b"3DI" + header[3:]


def mp3_frame(version=2, bitrate_index=3, rate_index=0, channels=1,
              padding=0, protection=1):
    """Synthetic complete Layer III frame with zero side/main data."""
    rates = (44100, 48000, 32000)
    bitrates = ((0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160)
                if version in (0, 2) else
                (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320))
    rate = rates[rate_index] // {0: 4, 2: 2, 3: 1}[version]
    size = (72000 if version in (0, 2) else 144000) * bitrates[bitrate_index] // rate + padding
    header = ((0x7ff << 21) | (version << 19) | (1 << 17) |
              (protection << 16) | (bitrate_index << 12) | (rate_index << 10) |
              (padding << 9) | ((3 if channels == 1 else 0) << 6))
    return struct.pack(">I", header) + bytes(size - 4)


class AudioSourceConformanceTests(unittest.TestCase):
    def setUp(self):
        self.payload = mp3_frame() * 3 + b"\0\0"
        self.descriptor = {
            "catalog_id": "mod_test:voice", "format": "MP3", "source_format": "MP3",
            "file_path": "sample.mp3", "data_size": len(self.payload),
            "actor": "Narrator", "transcript": "Hello", "language": "en", "context": "greeting",
        }
        self.manifest = dict(self.descriptor, id="mod_test:voice", pd_kind="voice",
                             pd_schema_version=1, data="sample.mp3")
        del self.manifest["file_path"]
        del self.manifest["catalog_id"]

    def check(self, descriptor=None, manifest=None, members=None, ext=".pdvoice", full=False):
        descriptor = self.descriptor if descriptor is None else descriptor
        manifest = self.manifest if manifest is None else manifest
        members = {"sample.mp3": self.payload} if members is None else members
        archive = io.BytesIO()
        descriptor_name = "sound.ini" if ext == ".pdsfx" else "voice.ini"
        with zipfile.ZipFile(archive, "w") as zf:
            zf.writestr(descriptor_name, ("[sound]\n" if ext == ".pdsfx" else "[voice]\n") + "".join(
                f"{key} = {str(value).lower() if type(value) is bool else value}\n"
                for key, value in descriptor.items()))
            zf.writestr("_meta/manifest.json", json.dumps(manifest))
            for name, data in members.items():
                zf.writestr(name, data)
        archive.seek(0)
        if full:
            return validate_archive_bytes(archive.getvalue(), "fixture" + ext, ext).errors
        errors = []
        with zipfile.ZipFile(archive) as zf:
            validate_audio_source_contract("fixture", ext, zf, set(zf.namelist()), errors)
        return errors

    def test_authored_mp3_uses_intrinsic_format_without_native_wav_fields(self):
        self.assertEqual(self.check(), [])
        errors = []
        metadata = parse_mp3_source_metadata("fixture", self.payload, errors)
        self.assertEqual(errors, [])
        self.assertEqual(metadata, {"sample_rate_hz": 22050, "channels": 1,
                                    "mpeg_frame_count": 3, "mpeg_sample_frames": 1728})
        self.assertNotIn("source_index", self.descriptor)  # No numeric mod identity requirement.

    def test_extracted_alias_and_direct_file_metadata_match(self):
        for kind, fields in (
            ("configured_alias", {"source_index": 32808, "mapped_soundnum": 38778,
                                  "mp3_priority": 2}),
            ("direct_file", {"source_index": -1, "mapped_soundnum": -1, "mp3_priority": 0}),
        ):
            fields.update(source_symbol="SFX_CARR_HELLO_JOANNA", source_file_symbol="FILE_ACICARR06M")
            descriptor = dict(self.descriptor, **fields)
            manifest = dict(self.manifest, **fields, source_filenum=1914, source_reference_kind=kind)
            with self.subTest(kind=kind):
                self.assertEqual(self.check(descriptor, manifest), [])
                descriptor["mp3_priority"] = 3
                self.assertTrue(any("provenance" in e for e in self.check(descriptor, manifest)))
                descriptor["mp3_priority"] = fields["mp3_priority"]
                manifest["source_filenum"] = 4096
                self.assertTrue(any("source_filenum" in e for e in self.check(descriptor, manifest)))

    def test_public_selection_and_source_bytes_need_no_intrinsic_metadata_echo(self):
        for field, value in (("format", "WAV_PCM16"), ("source_format", "ALADPCM"),
                             ("data_size", 1), ("sample_rate_hz", 7), ("decoded_sample_count", 2)):
            with self.subTest(field=field):
                self.assertEqual(self.check(dict(self.descriptor, **{field: value})), [])
        for change in ({"data": "sample.wav"}, {"format": "WAV_PCM16"},
                       {"data_size": True}, {"data_size": len(self.payload) + 0.5},
                       {"sample_rate_hz": 44100}, {"channels": 2}):
            with self.subTest(change=change):
                self.assertEqual(self.check(manifest=dict(self.manifest, **change)), [])
        self.assertTrue(self.check(members={}))
        for path in ("../sample.mp3", "/sample.mp3", "https://example.test/sample.mp3", "sample.flac"):
            self.assertTrue(self.check(dict(self.descriptor, file_path=path)))
        # An alternate member does not change the explicitly selected source.
        self.assertEqual(self.check(members={"sample.mp3": self.payload, "sample.wav": self.wav()[2]}), [])

    def test_public_voice_metadata_needs_no_private_duplicate(self):
        fields = ("actor", "transcript", "language", "context")
        private = {key: value for key, value in self.manifest.items() if key not in fields}
        self.assertEqual(self.check(manifest=private), [])
        edited = dict(self.descriptor, actor="Edited narrator", transcript="Changed words",
                      context="new scene")
        self.assertEqual(self.check(edited, self.manifest), [])
        descriptor, manifest, payload = self.wav()
        private = {key: value for key, value in manifest.items() if key not in fields}
        self.assertEqual(self.check(descriptor, private, {"sample.wav": payload}), [])

    def test_mp3_container_rejects_empty_truncated_and_invalid_sources(self):
        valid = mp3_frame()
        for payload in (b"", b"not audio", valid[:-1], valid + b"\xff\xf3", valid + b"junk",
                        b"ID3\x04\x00\x00\x00\x00\x01\x00", b"ID3\x04\x00\x00\x80\0\0\0"):
            with self.subTest(payload=payload[:12]):
                descriptor = dict(self.descriptor, data_size=len(payload))
                manifest = dict(self.manifest, data_size=len(payload))
                self.assertTrue(self.check(descriptor, manifest, {"sample.mp3": payload}))

    def test_mp3_supported_frames_tags_and_bitrate_variation(self):
        payload = b"ID3\x04\x00\x00\0\0\0\x03abc" + mp3_frame() + mp3_frame(bitrate_index=4)
        payload += b"TAG" + bytes(125)
        errors = []
        metadata = parse_mp3_source_metadata("fixture", payload, errors)
        self.assertEqual(errors, [])
        self.assertEqual(metadata["mpeg_frame_count"], 2)
        errors = []
        metadata = parse_mp3_source_metadata("fixture", mp3_frame(version=3, channels=2, protection=0), errors)
        self.assertEqual(errors, [])
        self.assertEqual(metadata["sample_rate_hz"], 44100)
        self.assertEqual(metadata["channels"], 2)

    def test_real_mp3_appended_id3v24_tags_preserve_source_admission(self):
        for name in ("tone_mpeg1_stereo_44100.mp3", "tone_mpeg2_stereo_22050.mp3",
                     "tone_mpeg2_mono_22050.mp3", "tone_mpeg25_mono_11025.mp3"):
            payload = (MP3_FIXTURE_ROOT / name).read_bytes()
            errors = []
            expected = parse_mp3_source_metadata("fixture", payload, errors)
            self.assertEqual(errors, [])
            self.assertIsNotNone(expected)
            title_tag = bytearray(id3v24_footer_tag(b"tone" + b"a" * 130))
            title_tag[-128:-125] = b"TAG"  # text, not a terminal ID3v1 tag
            for suffix in (id3v24_footer_tag(), id3v24_footer_tag() * 2,
                           id3v24_footer_tag() + b"TAG" + bytes(125), bytes(title_tag)):
                with self.subTest(file=name, suffix_length=len(suffix)):
                    decorated = payload + bytes(3) + suffix
                    errors = []
                    actual = parse_mp3_source_metadata("fixture", decorated, errors)
                    self.assertEqual(errors, [])
                    self.assertEqual(actual, expected)
                    self.assertEqual(self.check(members={"sample.mp3": decorated}), [])

    def test_real_mp3_malformed_appended_id3v24_rejects(self):
        payload = (MP3_FIXTURE_ROOT / "tone_mpeg2_stereo_22050.mp3").read_bytes()
        bad = []
        tag = bytearray(id3v24_footer_tag())
        tag[4] ^= 1
        bad.append(bytes(tag))
        tag = bytearray(id3v24_footer_tag())
        tag[-4] = 0x80
        bad.append(bytes(tag))
        tag = bytearray(id3v24_footer_tag())
        tag[-4:] = b"\x7f" * 4
        bad.append(bytes(tag))
        tag = bytearray(id3v24_footer_tag())
        tag[5] = tag[-5] = 0
        bad.append(bytes(tag))
        bad.extend((id3v24_footer_tag()[10:], id3v24_footer_tag()[:-1],
                    id3v24_footer_tag()[:-10]))
        for index, suffix in enumerate(bad):
            with self.subTest(case=index):
                errors = []
                candidate = payload + suffix
                self.assertIsNone(parse_mp3_source_metadata("fixture", candidate, errors))
                self.assertTrue(errors)
                self.assertTrue(self.check(members={"sample.mp3": candidate}))

    def test_mp3_runtime_unsupported_headers_and_midstream_changes_reject(self):
        frame = mp3_frame()
        for offset, mask, bits in ((1, 0x18, 0x08), (1, 6, 4), (2, 0xf0, 0),
                                   (2, 0xf0, 0xf0), (2, 0x0c, 0x0c), (3, 3, 2)):
            changed = bytearray(frame)
            changed[offset] = (changed[offset] & ~mask) | bits
            with self.subTest(offset=offset, bits=bits):
                errors = []
                self.assertIsNone(parse_mp3_source_metadata("fixture", bytes(changed), errors))
                self.assertTrue(errors)
        for second in (mp3_frame(rate_index=1), mp3_frame(channels=2)):
            errors = []
            self.assertIsNone(parse_mp3_source_metadata("fixture", frame + second, errors))
            self.assertTrue(any("stay fixed" in e for e in errors))

    def wav(self):
        samples = struct.pack("<3h", 0, 123, -123)
        payload = (b"RIFF" + struct.pack("<I", 36 + len(samples)) + b"WAVEfmt " +
                   struct.pack("<IHHIIHH", 16, 1, 1, 22050, 44100, 2, 16) +
                   b"data" + struct.pack("<I", len(samples)) + samples)
        values = {field: 0 for field in AUDIO_WAV_NATIVE_FIELDS}
        values.update(self.descriptor, file_path="sample.wav", format="WAV_PCM16",
                      source_format="ALADPCM", data_size=len(payload),
                      sample_rate_hz=22050, decoded_sample_count=3,
                      has_loop=False, has_envelope=False)
        manifest = dict(values, data="sample.wav")
        del manifest["file_path"]
        return values, manifest, payload

    def test_all_default_formats_are_actual_typed_archive_alternatives(self):
        sources = {"sample.wav": self.wav()[2], "sample.mp3": self.payload,
                   "sample.ogg": (FIXTURE_ROOT / "tone_stereo_22050.ogg").read_bytes()}
        for ext, kind in ((".pdsfx", "sfx"), (".pdvoice", "voice")):
            for source, payload in sources.items():
                with self.subTest(ext=ext, source=source):
                    descriptor = dict(self.descriptor, file_path=source)
                    manifest = dict(self.manifest, pd_kind=kind, data=source)
                    self.assertEqual(self.check(descriptor, manifest, {source: payload}, ext, full=True), [])
                    # Format changes never require private intrinsic mirror edits.
                    self.assertEqual(self.check(descriptor, dict(manifest, data="sample.wav",
                        data_size=1, format="ALADPCM", sample_rate_hz=1), {source: payload}, ext), [])
                    self.assertTrue(self.check(dict(descriptor, file_path="sample.flac"),
                        manifest, {"sample.flac": payload}, ext, full=True))

    def test_public_controls_remain_authored_optional_and_format_independent(self):
        for source, payload in (("sample.wav", self.wav()[2]), ("sample.mp3", self.payload),
                ("sample.ogg", (FIXTURE_ROOT / "tone_mono_22050.ogg").read_bytes())):
            descriptor = dict(self.descriptor, file_path=source, has_loop=True,
                loop_start_samples=0, loop_end_samples=2, loop_count=4294967295,
                key_min=0, key_max=127, key_base=60, key_detune=-12,
                sample_pan=32, sample_volume=90, has_envelope=True,
                attack_time_us=100, decay_time_us=200, release_time_us=300,
                attack_volume=127, decay_volume=70)
            for ext in (".pdsfx", ".pdvoice"):
                with self.subTest(source=source, ext=ext):
                    self.assertEqual(self.check(descriptor, {}, {source: payload}, ext), [])
                    for fields in ({"loop_end_samples": 0}, {"loop_end_samples": 999999999},
                            {"sample_pan": "2junk"}, {"key_min": 256, "key_max": 20},
                            {"has_envelope": "sometimes"}, {"attack_time_us": -2}):
                        self.assertTrue(self.check(dict(descriptor, **fields), {}, {source: payload}, ext))
        descriptor, manifest, payload = self.wav()
        # Existing valid native archive mirrors still pass, but are optional.
        self.assertEqual(self.check(descriptor, manifest, {"sample.wav": payload}), [])
        self.assertEqual(self.check(descriptor, dict(manifest, decoded_sample_count=9),
            {"sample.wav": payload}), [])

    def test_sample_keymap_packed_bytes_are_not_midi_ranges(self):
        controls = ((8, 4, 0, 0), (4, 0, 0, 0), (5, 0, 176, 16),
                    (255, 255, 255, 255), (0, 0, 0, 215))
        keys = ("key_min", "key_max", "velocity_min", "velocity_max")
        for ext in (".pdsfx", ".pdvoice"):
            for row in controls:
                with self.subTest(ext=ext, controls=row):
                    descriptor = dict(self.descriptor, **dict(zip(keys, row)))
                    self.assertEqual(self.check(descriptor, {}, ext=ext), [])
            for key in keys:
                for value in (-1, 256, "4junk", "2.5"):
                    with self.subTest(ext=ext, key=key, value=value):
                        self.assertTrue(self.check(dict(self.descriptor, **{key: value}), {}, ext=ext))

    def test_mpeg25_and_supported_header_flag_variation(self):
        for version, rate in ((3, 44100), (2, 22050), (0, 11025)):
            payload = mp3_frame(version=version, channels=2)
            variant = bytearray(mp3_frame(version=version, channels=2, protection=0))
            variant[3] |= 0x40 | 0x04  # joint stereo/original: still two channels.
            errors = []
            metadata = parse_mp3_source_metadata("fixture", payload + bytes(variant), errors)
            self.assertEqual(errors, [])
            self.assertEqual(metadata["sample_rate_hz"], rate)
            self.assertEqual(metadata["channels"], 2)

    def test_wav_standard_integer_and_finite_float_sources(self):
        for channels in (1, 2):
            for codec, bits in ((1, 8), (1, 16), (1, 24), (1, 32), (3, 32)):
                with self.subTest(channels=channels, codec=codec, bits=bits):
                    payload = standard_wav(codec, bits, channels)
                    errors = []
                    metadata = parse_wav_source_metadata("fixture", payload, errors)
                    self.assertEqual(errors, [])
                    self.assertEqual(metadata["channels"], channels)
                    self.assertEqual(metadata["decoded_sample_count"], 4)
                    self.assertEqual(self.check(dict(self.descriptor, file_path="sample.wav"), {},
                        {"sample.wav": payload}), [])

    def test_wav_truncation_duplicates_alignment_and_float_contract_reject(self):
        valid = standard_wav()
        duplicate = bytearray(valid[:12] + valid[12:36] + valid[12:])
        struct.pack_into("<I", duplicate, 4, len(duplicate) - 8)
        corrupt = [b"", valid[:-1], valid + b"x", bytes(duplicate),
                   standard_wav(channels=3), standard_wav(codec=6, bits=8)]
        for offset, value in ((20, 7), (24, 0), (28, 7), (32, 3)):
            value_bytes = bytearray(valid)
            struct.pack_into("<H" if offset in (20, 32) else "<I", value_bytes, offset, value)
            corrupt.append(bytes(value_bytes))
        floating = bytearray(standard_wav(codec=3, bits=32))
        floating[-4:] = struct.pack("<f", float("nan"))
        corrupt.append(bytes(floating))
        corrupt.append(standard_wav(codec=3, bits=32, fact=False))
        for payload in corrupt:
            with self.subTest(prefix=payload[:40]):
                errors = []
                self.assertIsNone(parse_wav_source_metadata("fixture", payload, errors))
                self.assertTrue(errors)

    def test_real_vorbis_fixture_metadata_and_all_pages(self):
        for name, channels, rate, frames in (("tone_mono_22050.ogg", 1, 22050, 4410),
                ("tone_stereo_22050.ogg", 2, 22050, 4410),
                ("tone_stereo_44100.ogg", 2, 44100, 8820)):
            errors = []
            metadata = parse_vorbis_source_metadata(name, (FIXTURE_ROOT / name).read_bytes(), errors)
            self.assertEqual(errors, [])
            self.assertEqual(metadata["channels"], channels)
            self.assertEqual(metadata["sample_rate_hz"], rate)
            self.assertEqual(metadata["decoded_sample_count"], frames)
            self.assertGreaterEqual(metadata["ogg_page_count"], 3)

    def test_vorbis_crc_truncation_chaining_and_semantic_header_corruption(self):
        valid = (FIXTURE_ROOT / "tone_stereo_22050.ogg").read_bytes()
        pages = split_ogg_pages(valid)
        corrupt = [b"", valid[:-1], valid + b"junk", valid + valid,
                   pages[0] + b"".join(pages[2:])]
        bad_crc = bytearray(valid)
        bad_crc[-1] ^= 1
        corrupt.append(bytes(bad_crc))
        # Recompute CRC so these exercise framing/header rules beyond checksum.
        for page_index, offset, payload in ((0, 5, b"\0"), (1, 18, struct.pack("<I", 9)),
                (1, 14, struct.pack("<I", 1234567)), (1, 5, b"\x01"),
                (-1, 6, struct.pack("<Q", 0xffffffffffffffff)), (-1, 5, b"\0")):
            variant = list(pages)
            page = bytearray(variant[page_index])
            page[offset:offset + len(payload)] = payload
            struct.pack_into("<I", page, 22, ogg_page_crc(page))
            variant[page_index] = bytes(page)
            corrupt.append(b"".join(variant))
        for channels in (0, 3):
            variant = list(pages)
            page = bytearray(variant[0])
            header_end = 27 + page[26]
            page[header_end + 11] = channels
            struct.pack_into("<I", page, 22, ogg_page_crc(page))
            variant[0] = bytes(page)
            corrupt.append(b"".join(variant))
        for payload in corrupt:
            with self.subTest(size=len(payload)):
                errors = []
                self.assertIsNone(parse_vorbis_source_metadata("fixture", payload, errors))
                self.assertTrue(errors)
                self.assertTrue(self.check(dict(self.descriptor, file_path="sample.ogg"), {},
                    {"sample.ogg": payload}))

    def test_declared_locale_sources_are_checked_without_changing_voice_metadata(self):
        descriptor = dict(self.descriptor, locale_fr_file="locales/fr.ogg", fallback_locale="en")
        members = {"sample.mp3": self.payload,
            "locales/fr.ogg": (FIXTURE_ROOT / "tone_mono_22050.ogg").read_bytes()}
        self.assertEqual(self.check(descriptor, {}, members), [])
        members["locales/fr.ogg"] = b"not ogg"
        self.assertTrue(self.check(descriptor, {}, members))
        for field in ("actor", "transcript", "language", "context"):
            missing = dict(self.descriptor)
            del missing[field]
            self.assertTrue(self.check(missing))


def standard_wav(codec=1, bits=16, channels=2, fact=True):
    frames = 4
    align = channels * bits // 8
    source = struct.pack("<f", 0.25) * frames * channels if codec == 3 else bytes(frames * align)
    chunks = (b"fmt " + struct.pack("<IHHIIHH", 18 if codec == 3 else 16,
        codec, channels, 22050, 22050 * align, align, bits))
    if codec == 3:
        chunks += b"\0\0"
        if fact:
            chunks += b"fact" + struct.pack("<II", 4, frames)
    chunks += b"data" + struct.pack("<I", len(source)) + source
    if len(source) & 1:
        chunks += b"\0"
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def split_ogg_pages(data):
    pages = []
    offset = 0
    while offset < len(data):
        header_end = offset + 27 + data[offset + 26]
        end = header_end + sum(data[offset + 27:header_end])
        pages.append(data[offset:end])
        offset = end
    return pages


if __name__ == "__main__":
    unittest.main()
