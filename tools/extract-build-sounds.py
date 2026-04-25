#!/usr/bin/env python3
"""
extract-build-sounds.py -- Extract specific Perfect Dark SFX from ROM as WAV files.

Parses the N64 ALBankFile format from sfx.ctl, reads ADPCM sample data from
sfx.tbl, decodes N64 VADPCM to 16-bit PCM, and writes .wav files.

Usage:
    python extract-build-sounds.py <rom.z64> [--outdir dist/build-sounds]

The script extracts sounds needed by the build tool (build-gui.ps1):
  - Menu click/select sounds
  - Menu focus/tick sounds
  - Item pickup sounds
  - Male exclamation sounds (enemy dialog)
"""

import os
import sys
import struct
import wave
import argparse
import random

# ============================================================================
# N64 ALBankFile format parsing (big-endian)
# ============================================================================

def read_u8(data, off):
    return data[off]

def read_s16(data, off):
    return struct.unpack_from('>h', data, off)[0]

def read_u16(data, off):
    return struct.unpack_from('>H', data, off)[0]

def read_s32(data, off):
    return struct.unpack_from('>i', data, off)[0]

def read_u32(data, off):
    return struct.unpack_from('>I', data, off)[0]

class ADPCMBook:
    def __init__(self, data, off):
        self.order = read_s32(data, off)
        self.npredictors = read_s32(data, off + 4)
        # Book coefficients: npredictors * order * 8 entries (each s16)
        num_entries = self.npredictors * self.order * 8
        self.book = []
        for i in range(num_entries):
            self.book.append(read_s16(data, off + 8 + i * 2))

class ADPCMLoop:
    def __init__(self, data, off):
        self.start = read_u32(data, off)
        self.end = read_u32(data, off + 4)
        self.count = read_u32(data, off + 8)
        # Loop state: 16 x s16
        self.state = []
        for i in range(16):
            self.state.append(read_s16(data, off + 12 + i * 2))

class WaveTable:
    def __init__(self, data, off):
        self.base = read_u32(data, off)        # offset into sfx.tbl
        self.length = read_s32(data, off + 4)   # length in bytes
        self.type = read_u8(data, off + 8)       # 0 = ADPCM, 1 = RAW16
        self.flags = read_u8(data, off + 9)
        self.book = None
        self.loop = None

        if self.type == 0:  # AL_ADPCM_WAVE
            loop_ptr = read_u32(data, off + 10)
            book_ptr = read_u32(data, off + 14)
            if loop_ptr != 0:
                self.loop = ADPCMLoop(data, loop_ptr)
            if book_ptr != 0:
                self.book = ADPCMBook(data, book_ptr)
        elif self.type == 1:  # AL_RAW16_WAVE
            loop_ptr = read_u32(data, off + 10)
            if loop_ptr != 0:
                self.loop = ADPCMLoop(data, loop_ptr)

class Sound:
    def __init__(self, data, off):
        env_ptr = read_u32(data, off)
        keymap_ptr = read_u32(data, off + 4)
        wave_ptr = read_u32(data, off + 8)
        self.sample_pan = read_u8(data, off + 12)
        self.sample_volume = read_u8(data, off + 13)
        self.flags = read_u8(data, off + 14)
        self.wavetable = WaveTable(data, wave_ptr) if wave_ptr != 0 else None

class Instrument:
    def __init__(self, data, off):
        self.volume = read_u8(data, off)
        self.pan = read_u8(data, off + 1)
        self.priority = read_u8(data, off + 2)
        self.flags = read_u8(data, off + 3)
        self.bend_range = read_s16(data, off + 12)
        self.sound_count = read_s16(data, off + 14)
        self.sounds = []
        for i in range(self.sound_count):
            sound_ptr = read_u32(data, off + 16 + i * 4)
            if sound_ptr != 0:
                self.sounds.append(Sound(data, sound_ptr))
            else:
                self.sounds.append(None)

class Bank:
    def __init__(self, data, off):
        self.inst_count = read_s16(data, off)
        self.flags = read_u8(data, off + 2)
        self.sample_rate = read_s32(data, off + 4)
        perc_ptr = read_u32(data, off + 8)
        self.percussion = Instrument(data, perc_ptr) if perc_ptr != 0 else None
        self.instruments = []
        for i in range(self.inst_count):
            inst_ptr = read_u32(data, off + 12 + i * 4)
            if inst_ptr != 0:
                self.instruments.append(Instrument(data, inst_ptr))
            else:
                self.instruments.append(None)

class BankFile:
    def __init__(self, data):
        self.revision = read_s16(data, 0)
        self.bank_count = read_s16(data, 2)
        self.banks = []
        for i in range(self.bank_count):
            bank_ptr = read_u32(data, 4 + i * 4)
            if bank_ptr != 0:
                self.banks.append(Bank(data, bank_ptr))
            else:
                self.banks.append(None)

# ============================================================================
# N64 VADPCM decoder
# ============================================================================

def decode_vadpcm(adpcm_data, length, book):
    """Decode N64 VADPCM (4-bit ADPCM) to 16-bit PCM samples."""
    if book is None:
        return []

    order = book.order
    npredictors = book.npredictors
    coeffs = book.book

    # Each frame: 9 bytes → 16 samples
    num_frames = length // 9
    pcm = []
    state = [0] * 16  # Previous samples for prediction

    for frame_idx in range(num_frames):
        frame_off = frame_idx * 9
        if frame_off + 9 > len(adpcm_data):
            break

        header = adpcm_data[frame_off]
        scale = 1 << (header & 0x0F)
        predictor_idx = (header >> 4) & 0x0F

        if predictor_idx >= npredictors:
            predictor_idx = 0

        # Get predictor coefficients for this frame
        coeff_base = predictor_idx * order * 8
        pred_coeffs = coeffs[coeff_base:coeff_base + order * 8]

        # Extract 16 x 4-bit nibbles from 8 data bytes
        nibbles = []
        for byte_idx in range(8):
            byte = adpcm_data[frame_off + 1 + byte_idx]
            hi = (byte >> 4) & 0x0F
            lo = byte & 0x0F
            # Sign-extend 4-bit to signed int
            if hi >= 8: hi -= 16
            if lo >= 8: lo -= 16
            nibbles.append(hi)
            nibbles.append(lo)

        # Decode 16 samples using prediction
        frame_samples = [0] * 16
        for i in range(16):
            # Start with scaled input
            sample = nibbles[i] * scale

            # Add prediction from previous state and already-decoded samples
            prediction = 0
            # Contributions from previous frame's state
            for j in range(order):
                if j < len(pred_coeffs) // 8:
                    coeff_idx = j * 8 + (i if i < 8 else i)
                    if i < order:
                        # Use state from previous frame
                        if (order - 1 - j + (16 - order) + i) < 16:
                            state_idx = 16 - order + i - 1 - j
                            if state_idx >= 0 and state_idx < 16:
                                ci = j * 8 + i
                                if ci < len(pred_coeffs):
                                    prediction += pred_coeffs[ci] * state[16 - order + i - 1 - j]

            # Simplified VADPCM decode — use the standard algorithm
            # Reset and use proper vector multiply
            sample_out = nibbles[i] * scale
            for j in range(min(order, 2)):
                vec_off = predictor_idx * order * 8 + j * 8
                for k in range(8):
                    idx = vec_off + k
                    if idx < len(coeffs):
                        if i < 8:
                            # First 8 samples reference state
                            ref_idx = 8 + k - (1 - j) * 8 + j * 8
                            if j == 0:
                                ref_idx = 8 + k  # state[8..15]
                            else:
                                ref_idx = k       # state[0..7]
                            if ref_idx >= 0 and ref_idx < 16 and k == (i - j * 0):
                                pass  # Complex vector math
                        else:
                            pass

            frame_samples[i] = sample_out

        # Use a simpler, correct VADPCM implementation
        frame_samples = _decode_frame_proper(adpcm_data[frame_off:frame_off+9],
                                              state, book)
        pcm.extend(frame_samples)
        # Update state: last 16 samples
        state = frame_samples[:]

    return pcm

def _decode_frame_proper(frame, prev_state, book):
    """Proper N64 VADPCM frame decode using vector convolution."""
    header = frame[0]
    scale = header & 0x0F
    predictor_idx = (header >> 4) & 0x0F

    order = book.order
    npredictors = book.npredictors

    if predictor_idx >= npredictors:
        predictor_idx = 0

    # Extract signed nibbles
    nibbles = []
    for i in range(8):
        byte = frame[1 + i]
        hi = (byte >> 4) & 0x0F
        lo = byte & 0x0F
        if hi >= 8: hi -= 16
        if lo >= 8: lo -= 16
        nibbles.append(hi)
        nibbles.append(lo)

    # Scale nibbles
    scaled = [n << scale for n in nibbles]

    # Get predictor vectors (order rows of 8 coefficients each)
    vectors = []
    base = predictor_idx * order * 8
    for i in range(order):
        row = []
        for j in range(8):
            idx = base + i * 8 + j
            if idx < len(book.book):
                row.append(book.book[idx])
            else:
                row.append(0)
        vectors.append(row)

    # Decode using convolution with predictor vectors
    out = [0] * 16

    # First 8 samples use previous frame's state
    for i in range(8):
        sample = scaled[i]
        for v in range(order):
            if v == 0:
                # First predictor vector convolves with prev_state[8:16]
                for k in range(8):
                    if (i - 1 - k) >= 0:
                        sample += (vectors[v][k] * out[i - 1 - k]) >> 11
                    else:
                        state_idx = 16 + (i - 1 - k)
                        if 0 <= state_idx < 16:
                            sample += (vectors[v][k] * prev_state[state_idx]) >> 11
            elif v == 1:
                for k in range(8):
                    if (i - 9 - k) >= 0:
                        sample += (vectors[v][k] * out[i - 9 - k]) >> 11
                    else:
                        state_idx = 16 + (i - 9 - k)
                        if 0 <= state_idx < 16:
                            sample += (vectors[v][k] * prev_state[state_idx]) >> 11

        # Clamp to 16-bit range
        sample = max(-32768, min(32767, sample))
        out[i] = sample

    # Second 8 samples
    for i in range(8, 16):
        sample = scaled[i]
        for v in range(order):
            if v == 0:
                for k in range(8):
                    if (i - 1 - k) >= 0:
                        sample += (vectors[v][k] * out[i - 1 - k]) >> 11
                    else:
                        state_idx = 16 + (i - 1 - k)
                        if 0 <= state_idx < 16:
                            sample += (vectors[v][k] * prev_state[state_idx]) >> 11
            elif v == 1:
                for k in range(8):
                    if (i - 9 - k) >= 0:
                        sample += (vectors[v][k] * out[i - 9 - k]) >> 11
                    else:
                        state_idx = 16 + (i - 9 - k)
                        if 0 <= state_idx < 16:
                            sample += (vectors[v][k] * prev_state[state_idx]) >> 11

        sample = max(-32768, min(32767, sample))
        out[i] = sample

    return out

# ============================================================================
# WAV writer
# ============================================================================

def write_wav(filename, samples, sample_rate):
    """Write 16-bit mono PCM samples to a .wav file."""
    with wave.open(filename, 'w') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        pcm_data = struct.pack('<%dh' % len(samples), *samples)
        f.writeframes(pcm_data)

# ============================================================================
# ROM segment addresses (ntsc-final)
# ============================================================================

# These come from tools/extract — the sfx.ctl and sfx.tbl offsets in the ROM
ROM_OFFSETS = {
    'ntsc-final': {
        'sfxctl': None,  # Will be found from ROM
        'sfxctl_size': 0x2fb80,
        'sfxtbl_size': 0x4c2160,
    }
}

def find_sfx_offset(rom):
    """Find sfx.ctl offset by scanning for ALBankFile signature.
    ALBankFile starts with revision (s16) and bankCount (s16).
    Perfect Dark has revision=0x0002 (or similar) and a small bankCount."""
    # The extract script uses self.val('sfxctl') which reads from a config.
    # We need to find it. In ntsc-final, the audio segment is after the code.
    # Let's scan for the pattern: a valid ALBankFile header followed by
    # reasonable bank pointers.
    for offset in range(0x1000, len(rom) - 0x100, 0x10):
        revision = struct.unpack_from('>h', rom, offset)[0]
        bank_count = struct.unpack_from('>h', rom, offset + 2)[0]
        if revision in (0x4231, 0x0002, 0x4232) and 1 <= bank_count <= 8:
            # Check if first bank pointer is reasonable (within ctl range)
            bank_ptr = struct.unpack_from('>I', rom, offset + 4)[0]
            if 0x10 < bank_ptr < 0x30000:
                # Verify the bank has a reasonable inst count
                try:
                    inst_count = struct.unpack_from('>h', rom, offset + bank_ptr)[0]
                    if 0 < inst_count < 300:
                        sample_rate = struct.unpack_from('>i', rom, offset + bank_ptr + 4)[0]
                        if sample_rate in (22050, 32000, 44100, 11025, 16000):
                            return offset
                except:
                    pass
    return None

# ============================================================================
# SFX ID mapping
# ============================================================================

# SFX enum values (0-based, from sfx.h)
# The enum starts at SFX_0000 = 0 and increments sequentially.
# We map specific SFX names to their enum index.

# Count lines in sfx.h to get exact indices
def build_sfx_map(sfx_h_path):
    """Parse sfx.h and build a map of SFX name → index."""
    sfx_map = {}
    index = 0
    with open(sfx_h_path, 'r') as f:
        in_enum = False
        for line in f:
            stripped = line.strip()
            if stripped.startswith('enum sfx'):
                in_enum = True
                continue
            if in_enum:
                if stripped.startswith('};') or stripped.startswith('}'):
                    break
                # Skip comments and empty lines
                if stripped.startswith('//') or stripped.startswith('/*') or stripped == '':
                    continue
                # Extract enum name
                name = stripped.rstrip(',').strip()
                if name and name.startswith('SFX_'):
                    # Check for explicit value assignment
                    if '=' in name:
                        parts = name.split('=')
                        name = parts[0].strip()
                        val = parts[1].strip()
                        if val.startswith('0x'):
                            index = int(val, 16)
                        else:
                            index = int(val)
                    sfx_map[name] = index
                    index += 1
    return sfx_map

# Sounds we want to extract for the build tool:
WANTED_SOUNDS = {
    # Category: menu_click (UI button press)
    'menu_click': ['SFX_MENU_SELECT'],
    # Category: menu_tick (focus change / scroll)
    'menu_tick': ['SFX_MENU_FOCUS', 'SFX_MENU_SUBFOCUS'],
    # Category: item_pickup (build step start)
    'item_pickup': ['SFX_PICKUP_AMMO'],
    # Category: enemy_argh (build success - "another one bites the dust" category)
    'enemy_argh': [
        'SFX_ARGH_MALE_0086', 'SFX_ARGH_MALE_0087', 'SFX_ARGH_MALE_0088',
        'SFX_ARGH_MALE_0089', 'SFX_ARGH_MALE_008A', 'SFX_ARGH_MALE_008B',
        'SFX_ARGH_MALE_008C', 'SFX_ARGH_MALE_008D', 'SFX_ARGH_MALE_008E',
        'SFX_ARGH_MALE_008F', 'SFX_ARGH_MALE_0090', 'SFX_ARGH_MALE_0091',
        'SFX_ARGH_MALE_0092', 'SFX_ARGH_MALE_0093', 'SFX_ARGH_MALE_0094',
        'SFX_ARGH_MALE_0095', 'SFX_ARGH_MALE_0096', 'SFX_ARGH_MALE_0097',
        'SFX_ARGH_MALE_0098', 'SFX_ARGH_MALE_0099', 'SFX_ARGH_MALE_009A',
        'SFX_ARGH_MALE_009B', 'SFX_ARGH_MALE_009C', 'SFX_ARGH_MALE_009D',
        'SFX_ARGH_MALE_009E',
    ],
}

# ============================================================================
# Main extraction
# ============================================================================

def extract_sound_by_index(bankfile, tbl_data, sound_index, sample_rate):
    """Extract a single sound by its index across all banks/instruments.

    The SFX system indexes sounds globally across all instruments in all banks.
    In PD's sfx bank, instrument 0 typically has all SFX sounds.

    For direct (non-config-mapped) SFX IDs, the ID maps to:
    - Bank 0, enumerate all sounds across all instruments sequentially.
    """
    # Collect all sounds from all banks
    global_idx = 0
    for bank in bankfile.banks:
        if bank is None:
            continue

        # Percussion instrument first (if any)
        if bank.percussion is not None:
            for sound in bank.percussion.sounds:
                if global_idx == sound_index:
                    return extract_sound_data(sound, tbl_data, bank.sample_rate)
                global_idx += 1

        # Then regular instruments
        for inst in bank.instruments:
            if inst is None:
                continue
            for sound in inst.sounds:
                if sound is None:
                    global_idx += 1
                    continue
                if global_idx == sound_index:
                    return extract_sound_data(sound, tbl_data, bank.sample_rate)
                global_idx += 1

    return None, 0

def extract_sound_data(sound, tbl_data, sample_rate):
    """Extract and decode a single sound's waveform."""
    if sound is None or sound.wavetable is None:
        return None, sample_rate

    wt = sound.wavetable
    base = wt.base
    length = wt.length

    if base + length > len(tbl_data):
        print(f"  WARNING: Sound data out of range (base={base:#x}, len={length:#x}, tbl_size={len(tbl_data):#x})")
        return None, sample_rate

    raw_data = tbl_data[base:base + length]

    if wt.type == 0:  # ADPCM
        if wt.book is None:
            print("  WARNING: ADPCM sound with no book")
            return None, sample_rate
        samples = decode_vadpcm(raw_data, length, wt.book)
        return samples, sample_rate
    elif wt.type == 1:  # RAW16 (big-endian PCM)
        samples = []
        for i in range(0, length, 2):
            if i + 1 < length:
                samples.append(struct.unpack_from('>h', raw_data, i)[0])
        return samples, sample_rate

    return None, sample_rate

def main():
    parser = argparse.ArgumentParser(description='Extract Perfect Dark SFX as WAV files')
    parser.add_argument('rom', help='Path to pd.ntsc-final.z64 ROM file')
    parser.add_argument('--sfx-h', default=None, help='Path to sfx.h (auto-detected if not specified)')
    parser.add_argument('--outdir', default='dist/build-sounds', help='Output directory for WAV files')
    parser.add_argument('--list-all', action='store_true', help='List all sounds in the bank (don\'t extract)')
    args = parser.parse_args()

    # Find sfx.h
    sfx_h = args.sfx_h
    if sfx_h is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_dir = os.path.dirname(script_dir)
        sfx_h = os.path.join(project_dir, 'src', 'include', 'sfx.h')
        if not os.path.exists(sfx_h):
            # Try relative to ROM
            sfx_h = os.path.join(os.path.dirname(args.rom), 'src', 'include', 'sfx.h')

    print(f"Reading ROM: {args.rom}")
    with open(args.rom, 'rb') as f:
        rom = f.read()

    print(f"ROM size: {len(rom)} bytes ({len(rom) / 1024 / 1024:.1f} MB)")

    # Find sfx.ctl offset
    print("Scanning for sfx.ctl ALBankFile header...")
    sfxctl_offset = find_sfx_offset(rom)
    if sfxctl_offset is None:
        print("ERROR: Could not find sfx.ctl in ROM. Ensure this is a valid PD ntsc-final ROM.")
        sys.exit(1)

    print(f"Found sfx.ctl at ROM offset {sfxctl_offset:#x}")

    # Extract ctl and tbl from ROM
    ctl_size = 0x2fb80
    tbl_offset = sfxctl_offset + ctl_size
    tbl_size = 0x4c2160

    ctl_data = rom[sfxctl_offset:sfxctl_offset + ctl_size]
    tbl_data = rom[tbl_offset:tbl_offset + tbl_size]

    print(f"sfx.ctl: {len(ctl_data)} bytes, sfx.tbl: {len(tbl_data)} bytes")

    # Parse bank file
    print("Parsing ALBankFile...")
    bankfile = BankFile(ctl_data)
    print(f"  Revision: {bankfile.revision:#06x}, Banks: {bankfile.bank_count}")

    total_sounds = 0
    for bi, bank in enumerate(bankfile.banks):
        if bank is None:
            continue
        inst_sounds = 0
        for inst in bank.instruments:
            if inst is not None:
                inst_sounds += len(inst.sounds)
        if bank.percussion:
            inst_sounds += len(bank.percussion.sounds)
        print(f"  Bank {bi}: {bank.inst_count} instruments, {inst_sounds} sounds, rate={bank.sample_rate}")
        total_sounds += inst_sounds

    print(f"  Total sounds in bank: {total_sounds}")

    if args.list_all:
        # Just list all sounds
        idx = 0
        for bank in bankfile.banks:
            if bank is None:
                continue
            if bank.percussion:
                for s in bank.percussion.sounds:
                    if s and s.wavetable:
                        wt = s.wavetable
                        print(f"  [{idx:4d}] type={wt.type} base={wt.base:#08x} len={wt.length:#06x}")
                    else:
                        print(f"  [{idx:4d}] (null)")
                    idx += 1
            for inst in bank.instruments:
                if inst is None:
                    continue
                for s in inst.sounds:
                    if s and s.wavetable:
                        wt = s.wavetable
                        print(f"  [{idx:4d}] type={wt.type} base={wt.base:#08x} len={wt.length:#06x}")
                    else:
                        print(f"  [{idx:4d}] (null)")
                    idx += 1
        return

    # Build SFX name → index map
    if os.path.exists(sfx_h):
        print(f"Parsing SFX IDs from: {sfx_h}")
        sfx_map = build_sfx_map(sfx_h)
        print(f"  Found {len(sfx_map)} SFX entries")
    else:
        print(f"WARNING: sfx.h not found at {sfx_h}")
        print("  Using hardcoded indices (may be inaccurate)")
        sfx_map = {}

    # Create output directories
    os.makedirs(args.outdir, exist_ok=True)
    for category in WANTED_SOUNDS:
        os.makedirs(os.path.join(args.outdir, category), exist_ok=True)

    # Extract sounds
    extracted = 0
    failed = 0

    for category, sfx_names in WANTED_SOUNDS.items():
        print(f"\n--- {category} ---")
        for sfx_name in sfx_names:
            if sfx_name in sfx_map:
                sfx_idx = sfx_map[sfx_name]
            else:
                # Fallback: parse hex from name
                parts = sfx_name.split('_')
                try:
                    sfx_idx = int(parts[-1], 16)
                except:
                    print(f"  SKIP: {sfx_name} (not found in sfx.h and can't parse index)")
                    failed += 1
                    continue

            print(f"  Extracting {sfx_name} (index {sfx_idx:#06x} = {sfx_idx})...")
            samples, sr = extract_sound_by_index(bankfile, tbl_data, sfx_idx, 22050)

            if samples is None or len(samples) == 0:
                print(f"    FAILED: no audio data")
                failed += 1
                continue

            filename = f"{sfx_name.lower()}.wav"
            filepath = os.path.join(args.outdir, category, filename)
            write_wav(filepath, samples, sr)
            duration_ms = len(samples) * 1000 / sr
            print(f"    OK: {len(samples)} samples, {sr}Hz, {duration_ms:.0f}ms → {filepath}")
            extracted += 1

    print(f"\n=== Extraction complete: {extracted} sounds, {failed} failures ===")

    if extracted > 0:
        print(f"\nBuild sounds written to: {args.outdir}/")
        print("Categories:")
        for cat in WANTED_SOUNDS:
            wav_count = len([f for f in os.listdir(os.path.join(args.outdir, cat)) if f.endswith('.wav')])
            print(f"  {cat}: {wav_count} files")

if __name__ == '__main__':
    main()
