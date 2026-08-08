#!/usr/bin/env python3
"""Validate public .pdsfx, .pdvoice, and .pdsong source timing without launching the game."""

from __future__ import annotations

import argparse
import io
import json
import struct
import sys
import wave
import zipfile
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path


AUDIO_EXTS = {".pdsfx", ".pdvoice", ".pdsong"}
MPEG_SAMPLE_RATES = {
    0b11: (44100, 48000, 32000),
    0b10: (22050, 24000, 16000),
    0b00: (11025, 12000, 8000),
}
MPEG_BITRATES = {
    (0b11, 0b11): (0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448),
    (0b11, 0b10): (0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384),
    (0b11, 0b01): (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320),
    (0b10, 0b11): (0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256),
    (0b10, 0b10): (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160),
    (0b10, 0b01): (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160),
    (0b00, 0b11): (0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256),
    (0b00, 0b10): (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160),
    (0b00, 0b01): (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160),
}
MPEG_LAYER_SAMPLES = {
    0b01: {0b11: 1152, 0b10: 576, 0b00: 576},  # Layer III
    0b10: {0b11: 1152, 0b10: 1152, 0b00: 1152},  # Layer II
    0b11: {0b11: 384, 0b10: 384, 0b00: 384},  # Layer I
}


@dataclass
class WavInfo:
    channels: int
    sample_width: int
    sample_rate: int
    frames: int


@dataclass
class CompressedInfo:
    sample_rate: int = 0
    channels: int = 0
    frames: int = 0


@dataclass
class AudioStats:
    archives: int = 0
    sfx: int = 0
    voice: int = 0
    song: int = 0
    wav: int = 0
    mp3: int = 0
    ogg: int = 0
    midi: int = 0
    sequence_events: int = 0
    sample_rates: Counter[int] = field(default_factory=Counter)
    effective_pitch_buckets: Counter[str] = field(default_factory=Counter)


def parse_ini(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("[") or line.startswith("#") or line.startswith(";"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def int_value(values: dict[str, object] | dict[str, str], field: str) -> int | None:
    raw = values.get(field)
    if isinstance(raw, bool) or raw is None:
        return None
    try:
        return int(raw)
    except (TypeError, ValueError):
        return None


def bool_value(values: dict[str, object] | dict[str, str], field: str) -> bool | None:
    raw = values.get(field)
    if isinstance(raw, bool):
        return raw
    if isinstance(raw, str):
        lowered = raw.strip().lower()
        if lowered in {"1", "true", "yes"}:
            return True
        if lowered in {"0", "false", "no"}:
            return False
    return None


def iter_audio_inputs(paths: Iterable[Path]) -> Iterable[Path]:
    for path in paths:
        if path.is_dir():
            for ext in sorted(AUDIO_EXTS):
                yield from sorted(path.rglob(f"*{ext}"))
        elif path.suffix.lower() in AUDIO_EXTS:
            yield path


def read_json(archive: zipfile.ZipFile, name: str, label: str,
              errors: list[str]) -> dict[str, object]:
    try:
        value = json.loads(archive.read(name).decode("utf-8"))
    except KeyError:
        errors.append(f"{label} is missing {name}")
        return {}
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        errors.append(f"{label} {name} is invalid JSON: {exc}")
        return {}
    if not isinstance(value, dict):
        errors.append(f"{label} {name} must be a JSON object")
        return {}
    return value


def read_ini(archive: zipfile.ZipFile, name: str, label: str,
             errors: list[str]) -> dict[str, str]:
    try:
        return parse_ini(archive.read(name).decode("utf-8"))
    except KeyError:
        errors.append(f"{label} is missing {name}")
    except UnicodeDecodeError as exc:
        errors.append(f"{label} {name} is not UTF-8: {exc}")
    return {}


def read_wav_info(data: bytes, label: str, errors: list[str]) -> WavInfo | None:
    try:
        with wave.open(io.BytesIO(data), "rb") as wav:
            return WavInfo(
                channels=wav.getnchannels(),
                sample_width=wav.getsampwidth(),
                sample_rate=wav.getframerate(),
                frames=wav.getnframes(),
            )
    except (wave.Error, EOFError) as exc:
        errors.append(f"{label} is not a readable WAV file: {exc}")
        return None


def skip_id3v2(data: bytes) -> int:
    if len(data) < 10 or data[:3] != b"ID3":
        return 0
    size = ((data[6] & 0x7f) << 21) | ((data[7] & 0x7f) << 14) | \
        ((data[8] & 0x7f) << 7) | (data[9] & 0x7f)
    return min(len(data), 10 + size)


def parse_mp3_info(data: bytes, label: str, errors: list[str]) -> CompressedInfo | None:
    pos = skip_id3v2(data)
    frames = 0
    sample_rate = 0
    channels = 0
    valid_frames = 0
    while pos + 4 <= len(data):
        b0, b1, b2, b3 = data[pos:pos + 4]
        if b0 != 0xff or (b1 & 0xe0) != 0xe0:
            pos += 1
            continue
        version = (b1 >> 3) & 0x03
        layer = (b1 >> 1) & 0x03
        bitrate_index = (b2 >> 4) & 0x0f
        sample_index = (b2 >> 2) & 0x03
        padding = (b2 >> 1) & 0x01
        channel_mode = (b3 >> 6) & 0x03
        if version == 0b01 or layer == 0 or bitrate_index in {0, 0x0f} or sample_index == 0x03:
            pos += 1
            continue
        rates = MPEG_SAMPLE_RATES.get(version)
        bitrates = MPEG_BITRATES.get((version, layer))
        layer_samples = MPEG_LAYER_SAMPLES.get(layer, {}).get(version)
        if not rates or not bitrates or not layer_samples:
            pos += 1
            continue
        rate = rates[sample_index]
        bitrate_kbps = bitrates[bitrate_index]
        if bitrate_kbps <= 0:
            pos += 1
            continue
        if layer == 0b11:
            frame_bytes = int(((12 * bitrate_kbps * 1000) / rate + padding) * 4)
        elif layer == 0b01 and version != 0b11:
            frame_bytes = int((72 * bitrate_kbps * 1000) / rate + padding)
        else:
            frame_bytes = int((144 * bitrate_kbps * 1000) / rate + padding)
        if frame_bytes <= 4 or pos + frame_bytes > len(data) + 1:
            pos += 1
            continue
        # The computed frame size lets us avoid counting sync-looking bytes inside
        # compressed payloads while still accepting VBR streams.
        if sample_rate == 0:
            sample_rate = rate
            channels = 1 if channel_mode == 0b11 else 2
        elif rate != sample_rate:
            errors.append(f"{label} changes MP3 sample rate from {sample_rate} to {rate}")
            return None
        frames += layer_samples
        valid_frames += 1
        pos += frame_bytes
    if valid_frames == 0:
        errors.append(f"{label} has no readable MP3 frames")
        return None
    return CompressedInfo(sample_rate=sample_rate, channels=channels, frames=frames)


def parse_ogg_info(data: bytes, label: str, errors: list[str]) -> CompressedInfo | None:
    if not data.startswith(b"OggS"):
        errors.append(f"{label} is not an OGG stream")
        return None
    pos = 0
    sample_rate = 0
    channels = 0
    last_granule = 0
    saw_vorbis_ident = False
    while pos + 27 <= len(data):
        if data[pos:pos + 4] != b"OggS":
            next_page = data.find(b"OggS", pos + 1)
            if next_page < 0:
                break
            pos = next_page
            continue
        granule = struct.unpack_from("<q", data, pos + 6)[0]
        segment_count = data[pos + 26]
        seg_table_start = pos + 27
        payload_start = seg_table_start + segment_count
        if payload_start > len(data):
            errors.append(f"{label} OGG page segment table exceeds file size")
            return None
        payload_size = sum(data[seg_table_start:payload_start])
        payload_end = payload_start + payload_size
        if payload_end > len(data):
            errors.append(f"{label} OGG page payload exceeds file size")
            return None
        payload = data[payload_start:payload_end]
        if not saw_vorbis_ident and payload.startswith(b"\x01vorbis") and len(payload) >= 16:
            channels = payload[11]
            sample_rate = struct.unpack_from("<I", payload, 12)[0]
            saw_vorbis_ident = True
        if granule > last_granule:
            last_granule = granule
        pos = payload_end
    if not saw_vorbis_ident or sample_rate <= 0 or channels <= 0:
        errors.append(f"{label} has no readable Vorbis identification header")
        return None
    return CompressedInfo(sample_rate=sample_rate, channels=channels, frames=int(last_granule))


def compare_int(label: str, source: str, field: str, actual: int,
                values: dict[str, object] | dict[str, str], errors: list[str]) -> None:
    declared = int_value(values, field)
    if declared is not None and declared != actual:
        errors.append(f"{label} {source} {field} {declared} does not match source {actual}")


def validate_loop_fields(label: str, values: dict[str, object] | dict[str, str],
                         frames: int, errors: list[str]) -> None:
    start = int_value(values, "loop_start_samples") or 0
    end = int_value(values, "loop_end_samples") or 0
    count = int_value(values, "loop_count") or 0
    has_loop = bool_value(values, "has_loop")
    if start < 0 or end < 0 or start > frames or end > frames:
        errors.append(f"{label} loop sample bounds {start}..{end} exceed source frames {frames}")
    if end and end <= start:
        errors.append(f"{label} loop_end_samples must be greater than loop_start_samples")
    if has_loop is True and end <= start and count == 0:
        errors.append(f"{label} declares has_loop but has no loop span or loop_count")


def record_pitch(stats: AudioStats, values: dict[str, object] | dict[str, str]) -> None:
    key_base = int_value(values, "key_base")
    key_detune = int_value(values, "key_detune") or 0
    if key_base is None:
        return
    cents = key_base * 100 + key_detune - 6000
    if cents < -2400:
        bucket = "very_low"
    elif cents > 2400:
        bucket = "very_high"
    else:
        bucket = "normal"
    stats.effective_pitch_buckets[bucket] += 1


def validate_sfx_or_voice(path: Path, archive: zipfile.ZipFile, names: set[str],
                          manifest: dict[str, object], stats: AudioStats,
                          errors: list[str]) -> None:
    label = path.as_posix()
    ext = path.suffix.lower()
    descriptor_name = "sound.ini" if ext == ".pdsfx" else "voice.ini"
    ini = read_ini(archive, descriptor_name, label, errors)
    if ext == ".pdsfx":
        stats.sfx += 1
    else:
        stats.voice += 1

    data_member = str(manifest.get("data") or ini.get("file_path") or "sample.wav")
    if data_member not in names:
        errors.append(f"{label} declares missing audio source {data_member}")
        return

    lower = data_member.lower()
    source_bytes = archive.read(data_member)
    if lower.endswith(".wav"):
        info = read_wav_info(source_bytes, f"{label}::{data_member}", errors)
        if info is None:
            return
        stats.wav += 1
        stats.sample_rates[info.sample_rate] += 1
        if info.channels != 1:
            errors.append(f"{label}::{data_member} must be mono for native SFX/voice parity")
        if info.sample_width != 2:
            errors.append(f"{label}::{data_member} must be PCM16 for native SFX/voice parity")
        for source, values in ((descriptor_name, ini), ("_meta/manifest.json", manifest)):
            compare_int(label, source, "sample_rate_hz", info.sample_rate, values, errors)
            compare_int(label, source, "decoded_sample_count", info.frames, values, errors)
            validate_loop_fields(f"{label} {source}", values, info.frames, errors)
        record_pitch(stats, ini)
    elif lower.endswith(".mp3"):
        if ext != ".pdvoice":
            errors.append(f"{label} MP3 source is only valid for .pdvoice in this verifier")
            return
        info = parse_mp3_info(source_bytes, f"{label}::{data_member}", errors)
        if info is None:
            return
        stats.mp3 += 1
        stats.sample_rates[info.sample_rate] += 1
        if int_value(manifest, "source_filenum") is None:
            errors.append(f"{label} MP3 voice source must preserve source_filenum provenance in _meta/manifest.json")
    else:
        errors.append(f"{label} unsupported SFX/voice source member {data_member}")

    if ext == ".pdvoice":
        for locale in ("en", "fr", "de", "it", "es", "ja"):
            key = f"locale_{locale}_file"
            member = ini.get(key, "")
            if not member:
                continue
            if member not in names:
                errors.append(f"{label} {key} declares missing audio source {member}")
                continue
            localized = archive.read(member)
            localized_label = f"{label}::{member}"
            suffix = Path(member).suffix.lower()
            if suffix == ".wav":
                localized_info = read_wav_info(localized, localized_label, errors)
                if localized_info is None:
                    continue
                stats.wav += 1
                stats.sample_rates[localized_info.sample_rate] += 1
                if localized_info.channels != 1:
                    errors.append(f"{localized_label} must be mono for native voice parity")
                if localized_info.sample_width != 2:
                    errors.append(f"{localized_label} must be PCM16 for native voice parity")
            elif suffix == ".ogg":
                localized_info = parse_ogg_info(localized, localized_label, errors)
                if localized_info is not None:
                    stats.ogg += 1
                    stats.sample_rates[localized_info.sample_rate] += 1
            elif suffix == ".mp3":
                localized_info = parse_mp3_info(localized, localized_label, errors)
                if localized_info is not None:
                    stats.mp3 += 1
                    stats.sample_rates[localized_info.sample_rate] += 1
            else:
                errors.append(
                    f"{localized_label} must use a public .wav, .ogg, or .mp3 source"
                )


def validate_song(path: Path, archive: zipfile.ZipFile, names: set[str],
                  manifest: dict[str, object], stats: AudioStats,
                  errors: list[str]) -> None:
    label = path.as_posix()
    stats.song += 1
    ini = read_ini(archive, "music.ini", label, errors)
    if "sequence.mid" in names or str(manifest.get("midi") or ini.get("midi_file")) == "sequence.mid":
        if "sequence.mid" not in names:
            errors.append(f"{label} declares missing sequence.mid")
        if "sequence.json" not in names:
            errors.append(f"{label} sequence-backed song is missing sequence.json")
            return
        seq = read_json(archive, "sequence.json", label, errors)
        events = seq.get("events")
        if not isinstance(events, list) or not events:
            errors.append(f"{label} sequence.json must contain non-empty events")
            return
        stats.midi += 1
        stats.sequence_events += len(events)
        for source, values in (("music.ini", ini), ("_meta/manifest.json", manifest)):
            compare_int(label, source, "event_count", len(events), values, errors)
        return

    track_member = str(
        manifest.get("data")
        or ini.get("music_file")
        or ini.get("file_path")
        or ""
    )
    if not track_member:
        for candidate in ("track.wav", "track.ogg", "track.mp3"):
            if candidate in names:
                track_member = candidate
                break
    if track_member not in names:
        errors.append(f"{label} declares missing music source {track_member or '(none)'}")
        return
    data = archive.read(track_member)
    lower = track_member.lower()
    if lower.endswith(".wav"):
        info = read_wav_info(data, f"{label}::{track_member}", errors)
        if info is None:
            return
        stats.wav += 1
        stats.sample_rates[info.sample_rate] += 1
    elif lower.endswith(".mp3"):
        info = parse_mp3_info(data, f"{label}::{track_member}", errors)
        if info is None:
            return
        stats.mp3 += 1
        stats.sample_rates[info.sample_rate] += 1
    elif lower.endswith(".ogg"):
        info = parse_ogg_info(data, f"{label}::{track_member}", errors)
        if info is None:
            return
        stats.ogg += 1
        stats.sample_rates[info.sample_rate] += 1
    else:
        errors.append(f"{label} unsupported music source member {track_member}")


def validate_archive(path: Path, stats: AudioStats, errors: list[str]) -> None:
    label = path.as_posix()
    stats.archives += 1
    try:
        with zipfile.ZipFile(path) as archive:
            names = set(archive.namelist())
            manifest = read_json(archive, "_meta/manifest.json", label, errors)
            ext = path.suffix.lower()
            if ext in {".pdsfx", ".pdvoice"}:
                validate_sfx_or_voice(path, archive, names, manifest, stats, errors)
            elif ext == ".pdsong":
                validate_song(path, archive, names, manifest, stats, errors)
            else:
                errors.append(f"{label} is not an audio archive")
    except (OSError, zipfile.BadZipFile, KeyError) as exc:
        errors.append(f"{label}: {exc}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "paths",
        nargs="+",
        type=Path,
        help=".pdsfx/.pdvoice/.pdsong archives or directories containing them.",
    )
    parser.add_argument(
        "--require-all-categories",
        action="store_true",
        help="Fail unless SFX, voice, and song archives are present.",
    )
    parser.add_argument(
        "--max-errors",
        type=int,
        default=80,
        help="Maximum detailed failures to print before summarizing the rest.",
    )
    args = parser.parse_args(argv)

    stats = AudioStats()
    errors: list[str] = []
    for path in iter_audio_inputs(args.paths):
        validate_archive(path, stats, errors)

    if stats.archives == 0:
        errors.append("no audio archives were found")
    if args.require_all_categories and (stats.sfx == 0 or stats.voice == 0 or stats.song == 0):
        errors.append(".pdsfx, .pdvoice, and .pdsong archives are all required")

    if errors:
        print("audio source contract failed:", file=sys.stderr)
        max_errors = max(args.max_errors, 1)
        for error in errors[:max_errors]:
            print(f"  - {error}", file=sys.stderr)
        if len(errors) > max_errors:
            print(f"  - ... {len(errors) - max_errors} more error(s)", file=sys.stderr)
        return 1

    rates = ",".join(f"{rate}={count}" for rate, count in sorted(stats.sample_rates.items()))
    pitches = ",".join(
        f"{name}={stats.effective_pitch_buckets[name]}"
        for name in sorted(stats.effective_pitch_buckets)
    )
    print(
        "audio source contract ok: "
        f"archives={stats.archives} "
        f"sfx={stats.sfx} "
        f"voice={stats.voice} "
        f"song={stats.song} "
        f"wav={stats.wav} "
        f"mp3={stats.mp3} "
        f"ogg={stats.ogg} "
        f"midi={stats.midi} "
        f"sequence_events={stats.sequence_events} "
        f"sample_rates={rates} "
        f"effective_pitch={pitches}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
