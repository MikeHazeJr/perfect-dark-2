# Decision: libopus for voice chat (Phase 5)

> 2026-04-25 -- Mike's "complete the list" green light. Documented per
> the standing rule that reasonable-but-not-trivial design choices get a
> rationale + rollback note.

## Decision

Vendor **libopus 1.6.1** as a statically linked dependency. The codec
backs Phase 5's push-to-talk voice channel; the SDL audio device handles
capture (16 kHz mono PCM) and playback; PDVOC frames carry encoded
20 ms slices over a dedicated signed UDP socket on port 27108.

## Rationale

- **Best-in-class for low-latency voice.** Opus is the modern standard
  for VoIP and game-voice (Discord, WhatsApp, Zoom, Mumble all use
  it). 20 ms frame size + variable bit-rate yield <50 ms end-to-end
  capture-to-playback latency, well within the bar for game-voice.
- **License compatible.** BSD 3-clause + the IETF patent disclaimer
  permits static linking + redistribution without conflict with the
  PD2 port's existing license posture.
- **Available cross-platform via package managers.** MSYS2 has
  `mingw-w64-x86_64-opus` (`pacman -S mingw-w64-x86_64-opus`); Linux
  distros all carry a recent libopus; macOS ships via brew. No need
  to vendor source in `port/external/`.
- **Existing toolchain stays.** OpenSSL is already statically linked
  via libcurl; libopus is the only additional native dep this phase
  adds. Zero new dynamic dependencies (the existing zero-DLL release
  rule survives -- libopus links statically).
- **Bandwidth fits the design budget.** 16 kHz wide-band Opus at
  ~24 kbps per voice stream gives understandable game-voice quality
  with comfortable bandwidth headroom. 4-peer mesh worst case:
  4 inbound streams * 24 kbps + 1 outbound * 24 kbps = ~120 kbps; well
  inside any modern home connection.

## Rollback path

If libopus turns out to be impractical (build-tooling friction,
unforeseen license conflict, etc.), the alternatives are:

- **Speex** (predecessor; lower quality + higher CPU; deprecated
  upstream). Only worth considering if libopus has a regression in a
  specific MSYS2 package that takes too long to fix.
- **G.711 / mu-law** (built-in ITU codec; trivial to implement
  manually; ~64 kbps narrow-band only). The fallback if a native dep
  becomes unviable.
- **Disable voice entirely.** The Phase 5 scaffold (default off,
  per-friend mute, settings toggle) survives without the codec; the
  only loss is the actual capture / encode / decode pipeline.

The rollback path means we are not architecturally locked in to Opus.
The wire frame (PDVOC) carries `kind` + `payload_len` so a future
codec swap reuses the framing.

## Build / install

MSYS2 (developer):

```
pacman -S mingw-w64-x86_64-opus
```

CMakeLists.txt: optional dependency. If `pkg-config --modversion opus`
succeeds, the build defines `HAVE_OPUS=1` and links the static
`libopus.a`. If not, voice falls back to the Phase 5 scaffold (no
codec; PTT toggle is no-op). This makes Phase 5 ship safely on
machines that have not yet installed the package.

## Wire format reminder (from voice.h)

```
off len  field
0   5    magic "PDVOC"
5   1    version (1)
6   1    kind (0=opus_frame, 1=ptt_start, 2=ptt_stop)
7   1    flags
8   4    sender handle
12  4    target handle (group room id when broadcast)
16  2    seq (per-sender)
18  2    payload_len
20  N    opus encoded frame (~60-160 bytes per 20 ms slice at 16 kHz)
20+N 32  sender pubkey
52+N 64  signature
```

Per-frame Ed25519 signature is the same trust model as chat / file_transfer
/ social_share. The amortisation note in voice.h about per-burst signing
remains a future polish; this commit signs every frame.

## Phase 5 scope

This decision lands the codec; the implementation that consumes it
ships in the same commit:
- voice.c: real `voiceTick` body that captures, encodes, sends, and
  decodes received frames into the SDL audio playback queue.
- voice_wire: new file owning the dedicated UDP socket on port 27108
  + the inbound dispatch.
- pdgui_friends.cpp Settings tab: existing PTT key (V) is unchanged;
  the indicator that "you are transmitting" shows a coloured dot on
  the status pill while live.
