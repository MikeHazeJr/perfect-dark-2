# Initial PD night implementation checkpoint

Owner `01a0f5c1-d031-77b0-a372-3391765df0d5`, model gpt-6. Active authorization
began 2026-10-02 01:51:51 UTC (October 1 21:51:51 Eastern), through October 2
07:00 Eastern / 11:00 UTC. Earlier pause entries are historical. The user
authorized coherent scoped commits and push to the existing project remote;
unrelated edits and generated/proprietary payloads remain excluded.

Six production/tooling units are pushed to existing `origin/dev` at
`https://github.com/MikeHazeJr/perfect-dark-2.git`. Remote HEAD is verified as
`8bda44832c4a3959108b9273699229b6a92f4716` at 02:54 UTC.

| Commit | Workbench | Delivered unit | Evidence |
| --- | --- | --- | --- |
| b0db47d7 | T-MODDING-010 | 27 graph boundaries, 67 references, 53 definitions | [Inventory](../../designs/modding/graph-operation-inventory-2026-10-01.md); 11 tests and source checker pass |
| 81fc120d | T-MENUS-007 | Complete 16-rule Settings/input/config metadata persistence | [Settings audit](pd-settings-metadata-persistence-2026-10-02.md); retained focused 10/82, overlap 246/10033, Settings 14/236 pass |
| 289d6ca1 | T-ASSETS-051 | Native preview FBO, viewport/projection and failed-selection repair | [Preview audit](pd-preview-publication-2026-10-02.md); accepted build and native tests, visual acceptance pending |
| a2648384 | T-TOOLING-008 | Wide unsigned native texture identities and allocation limits | [Texture audit](pd-texture-identity-2026-10-02.md); retained 12 cases / 189649 assertions and native cohort pass |
| 382b50af | T-TOOLING-009 | Actual model/texture member reads and parent native-slot source joins | [Native producer audit](pd-native-consumer-publication-2026-10-02.md); five C++ cases / 48 assertions and exact native gate 27/27 pass |
| 8bda4483 | T-TOOLING-008/009 | Native Windows Python path fix, immutable private smoke installs, bounded ledger/receipt/preparation controls | [Tooling audit](pd-bounded-verification-tooling-2026-10-02.md); fresh 93 asset / 19 storage / 17 Windows checks pass |

Fresh tooling verification also passes 27 native helper, 10 assertion JSON and
five readiness-output checks each in PS5.1 and PS7. The wrapper uses native
Windows Python with MSYS first on PATH and leaves the canonical build environment
unchanged. Trusted Git hooks and the native-source guard passed. Publication
records, exact index proofs, source freezes and small logs are under
`.claude/pd-initial-integration/20261002-night/`. No new build or game was needed
for the unchanged previously accepted production sources.

The retained client is `4ae22fa305a79342833c9daf0738f4cd2d144ecb948163b00d2eec4db738122f`,
frozen seed `6b8d026858e97f19b1ad75fb3edee9e77bebace58cd8a82c3662a46a172f3c6a`.
Its October 1 native cohort proves five archives, seven actual model/texture
source events and four parent texture source-generation/slot joins. Actual draw
and fidelity remain pending. The public source inventory is 121076 bytes;
combined source/artifact verification is 310558 bytes, with 25234 event bytes.
The 9018-archive metadata inventory and labeled pending cards are preparation,
not inspected images or full-corpus acceptance.

Normal user readiness passed on October 1 at 18:25 UTC on Default/WinSta0 with
input error zero. The separate child sandbox failure does not replace that
result. The supported native CLI runner recovery was reported by the parent;
Perfect Dark app approval and ordinary game input ownership remain unproved.
Canonical preview must preserve its 180-second readiness budget, 240-second
native cap and four captures. Keyboard traversal follows successful preview;
Settings UI editing/save/restart and real MKB/controller acceptance still need
their own proof. Full labeled contact sheets, animation, audio and nonvisual
fidelity remain open. Broader graph migration and multiplayer completion follow
the initial scope later.

At 03:06 UTC source review found the audio probe could accept registration and
song compilation without starting a native player. The animation probe also
requested 128 parts regardless of the actual clip. Both boundaries are corrected
in source: audio requires file-voice/public-MP3 ownership or a native sequence
player, and animation counts the entire bounded descriptor stream before decoding
only those parts. Tiny meaningful animation cases cover short clips, invalid
streams, no partial count, and wide capacity arithmetic.

The first bounded window actually ran 03:28:17-03:54:04 UTC, with an upper limit
of 03:58:17. Client/tests builds passed; focused tests passed 12 cases / 483
assertions and the exact failed-preview diagnostic passed one case / four
assertions. Audio logged three native player starts, but inherited console output
overflowed the execution transport. The scheduler/exit receipt was missing.
This run is `aborted_unaccepted`, preserved as a failed pinned recipe through the
storage API; no deletion or passing verdict was fabricated. Its exact V1 client
and compiled-source patch remain distinct from later working-source provenance.

The V2 fix drains both native console pipes continuously, retaining at most
128 KiB per stream with explicit truncation, initial bytes and final bytes.
Ordinary game logs remain authoritative. Both single and multiple process launch
paths require successful drainage independently of log exit-code overrides.
PS5.1 and PS7 each pass 35 tiny synthetic checks, including dual 1 MiB pipes,
bounded retention, exact prefix/suffix and read failures. The normal mainTick
polls the armed delayed-exit probe after the smoke watchdog; ordinary launches
have no armed probe.

After parent confirmation of Astral's release, V2 actual heavy start is
04:19:15.6019149 UTC, with a 30-minute upper duration ending 04:49:15.6019149.
Actual release is **04:33:44.9211608 UTC**, with zero owned heavy process or FIFO.
The 16 owned source paths, source guard and three PowerShell AST checks pass.
Corrected client/tests builds pass; focused regression12cases488assertions and
named preview diagnostic1case4assertions pass. The unchanged accepted nested
texture harness also remains covered by the prior native cohort.

Client `7cb077e37398ffcd13d8a1d4a2bc12af35194369ce346921092343105cb688c0`, tests
`454ebdc7accda0c57eace53e5ae875512a0f5482fce54fd15649631678493cab`, and frozen
seed `28b2cf259739a19560125424ab2e122973308ac762fccfbb7ae9f72b14017f79` are retained
as immutable blobs/recipes. Public source hash remains46807f6b; only the client
input changed from the accepted parent seed. Fresh canonical native gates pass
audio23/23, animation20/20 and MP36/6 assertions with all ten native MP3 cases.
Audio attests three public player starts and a1006ms scheduler window; the real
console drains710587bytes and retains131072. Animation decodes15actual parts,
15non-default and5changed,123frames, with no layout rejection. It preserves the
canonical90-second scripted exit. MP3 covers native main-bus PCM, priority,
pause/resume, stop, repeat and response after EOF under the dummy sound driver;
it does not establish audible hardware output. All native exits are zero.

Compact results, hashes, source freeze and four per-asset operational witnesses
are in `.claude/pd-initial-integration/20261002-night/probe-native-v2/summary.json`
and `operational-ledger.json` (9781bytes,SHA6792ccca). That ledger reads existing
receipt logs and seed metadata only; archive identities are not new strict
consumed-member traces. Full contact sheets, played animation poses/interpolation/
root motion/loops, listening/fidelity and ordinary UI/devices remain pending.
The normal desktop readiness result remains valid, but this executor lacks the
supported CUA tool and separate PD app/input approval. Next acceptance needs that
interactive executor and a parent15-minute canonical preview window preserving
180-second readiness,240-second native cap and four captures; keyboard follows
successful preview, with Settings editing/restart and real devices separate.
No ordinary UI/capture/device claim follows from native source probes. Broader
graph migration and the full corpus remain separate. Unrelated edits, processes
and historical evidence are preserved; no reset, bulk stage, permission change
or historical cleanup occurred.
