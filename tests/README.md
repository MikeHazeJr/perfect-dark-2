# pd-tests -- Perfect Dark 2 unit/roundtrip test suite

A self-contained test binary that exercises the regression-prone primitive
layers of the project (wire format, save format, manifest pipeline,
random-pool selector). Built alongside `pd` and `pd-updater` via the same
ninja pipeline.

## Why

A change to a `netbufWriteU32`, `savebufferOr`, `manifestSerialize`, or
`mpSetRandomWeapons` can silently break every wire packet, every save
file, every match start, or every random match -- in ways that don't
surface until a player actually plays through the affected path. These
tests give a 1-second feedback loop on those primitive layers.

See [`context/designs/testing-framework-2026-04-26.md`](../context/designs/testing-framework-2026-04-26.md)
for the framework ADR, coverage roadmap, and decision log.

## How to run

Preferred for parallel AI/code sessions on Windows:

```powershell
.\devtools\run-pd-tests.ps1 -Session qnet1 -Selector "[netbuf]"
.\devtools\run-pd-tests.ps1 -Session qcat1 -Selector "[catalog][provider][static]"
.\devtools\run-pd-tests.ps1 -Session qall1
.\devtools\build-session.ps1 -Remove -Session qnet1
```

`run-pd-tests.ps1` builds only the `pd-tests` target in
`.claude/session-builds/<session>/`, prepends the MinGW runtime path needed by
`pd-tests.exe`, and runs from the repository root so static source guards use
the same relative paths as the full suite. Use a short unique `-Session` per
parallel session and reuse it only for that session's reruns. The underlying
`build-session.ps1` wrapper is queued by default, so parallel sessions keep
isolated build trees but wait their turn for the expensive compile step.
Full build verification still uses
`.\devtools\build-session.ps1 -Session <id> -Target all`; do not use shared
`Build/` for verification when other sessions may be active.

From bash:

```bash
source devtools/build-env.sh
ninja -C Build pd-tests
./Build/pd-tests
```

From PowerShell:

```powershell
.\devtools\build-headless.ps1
.\Build\pd-tests.exe
```

On Windows, CMake copies the matching MSYS2 `libwinpthread-1.dll` beside
`pd-tests.exe`. Catch2/std::chrono can import `clock_gettime64` through
MinGW's C++ runtime, and keeping the known-good DLL next to the executable
prevents Windows from loading an older copy from PATH.

Expected output on a green run:

```
===============================================================================
All tests passed (770 assertions in 77 test cases)
```

(The two `[stub-log L2] NET: could not read N bytes` lines that appear
above the summary are EXPECTED -- they come from the netbuf
read-past-end safety tests.)

## Useful flags

```bash
./Build/pd-tests --list-tests                # enumerate all test cases
./Build/pd-tests "[manifest]"                # run only [manifest]-tagged cases
./Build/pd-tests "[manifest][diff]"          # run cases tagged both
./Build/pd-tests --success                   # show successful assertions too
./Build/pd-tests --reporter compact          # one-line-per-case format
./Build/pd-tests -? | less                   # full Catch2 v2 help
```

The PowerShell wrapper forwards the same Catch2 selectors:

```powershell
.\devtools\run-pd-tests.ps1 -Session qmanifest -Selector "[manifest]"
.\devtools\run-pd-tests.ps1 -Session qinput -Selector "[input]"
.\devtools\run-pd-tests.ps1 -Session qsave -Selector "[save][migration]"
.\devtools\run-pd-tests.ps1 -Session qcat -Scope catalog-provider
.\devtools\run-pd-tests.ps1 -Session qchecked -Selector "[catalog][checked][regression]"
.\devtools\run-pd-tests.ps1 -Session qcat -Scope catalog-provider -BuildTimeoutSeconds 180
.\devtools\run-pd-tests.ps1 -ListScopes
.\devtools\run-pd-tests.ps1 -Session qtags -ListTags
.\devtools\run-pd-tests.ps1 -Session qtests -ListTests -NoBuild
```

Common scope selectors:

| Scope | Selector |
|---|---|
| Catalog/provider identity | `[catalog]`, `[catalog][provider][static]`, `[catalog][identity][static]`, `[catalog][checked][regression]` |
| Input transitions and menu ownership | `[input]`, `[actionmap]`, `[inputctx]`, `[inputlayer]`, `[menu_graph]` |
| Mode lifecycle and packet parsing | `[netbuf]`, `[connectcode]`, `[lifecycle]`, `[static]` with a narrower subsystem tag |
| Manifest behavior | `[manifest]`, `[manifest][hash]`, `[manifest][diff]` |
| Save migration | `[savebuffer]`, `[save][migration]`, `[versions]` |
| Spawn weapon behavior | `[spawn-weapon]`, `[matchsetup][spawn-weapon]`, `[random-pool]` |
| QC/checklist alignment | `[connectcode][qc][static]` |

Use `-Scope` for the common first-pass selectors and `-Selector` when a slice
needs a more precise Catch2 expression.

## Layout

```
tests/
  main.cpp                  # Catch2 entry point (CATCH_CONFIG_MAIN)
  stubs.c                   # Linker stubs for game-side symbols not exercised by tests
  test_smoke.cpp            # Sanity check (Catch2 itself works)
  test_netbuf.cpp           # Wire format primitives (uses real port/src/net/netbuf.c)
  test_connectcode.cpp      # IP <-> word sentence (uses real port/src/connectcode.c)
  test_versions.cpp         # NET_PROTOCOL_VER + MPSETUP_VERSION pin
  test_versions_pin.c       # C TU that reads the live constants
  test_savebuffer.cpp       # Bit-pack primitives (against test-local copy)
  savebuffer_pure.{c,h}     # Pure subset copy of src/game/savebuffer.c
  test_save_migration.cpp   # v1 -> v2 weapon-cull migration semantics
  test_manifest.cpp         # Container ops + hash + diff + serialize/deserialize
  manifest_pure.{c,h}       # Pure subset copy of port/src/net/netmanifest.c
  test_random_pool.cpp      # mpSetRandomWeapons specification
```

## Source-vs-copy split

Two patterns are in use depending on dependency footprint:

**Real source files** (cherry-picked into `pd-tests` source list):
- `port/src/net/netbuf.c` -- pure, no globals, drops in
- `port/src/connectcode.c` -- pure, no globals, drops in

**Test-local copies** (in `tests/`):
- `savebuffer_pure.{c,h}` -- the 5 bit-pack functions from
  `src/game/savebuffer.c` lines 381-471. The rest of savebuffer.c
  pulls GBI/VI/Mtx which would cascade through the renderer.
- `manifest_pure.{c,h}` -- the pure container/hash/diff/serialize
  subset of `port/src/net/netmanifest.c`. The full file references
  7 globals + 11 subsystem functions.
- `tests/test_save_migration.cpp` -- replicates the v1 -> v2 weapon
  clamp rule from `mpsetupfileLoadWad`.
- `tests/test_random_pool.cpp` -- replicates the mpSetRandomWeapons
  rule with explicit unlock-predicate parameter.

Each copy has `@SYNC` markers in comments pointing back to the source
line range. **If you change the source, re-sync the copy and the test
will catch behavior drift on the next run.**

## Coverage today (first cohort, 2026-04-26)

| Subsystem | Cases | Asserts | Notes |
|---|---:|---:|---|
| smoke | 3 | 6 | Catch2 framework sanity |
| netbuf wire primitives | 16 | 77 | u8/u16/u32/u64/s8/s16/s32/s64/f32/str/mixed |
| connect codes | 6 | 99 | encode/decode roundtrip + case + garbage |
| version pins | 4 | ~10 | NET_PROTOCOL_VER 58, MPSETUP_VERSION 3 |
| savebuffer bit-pack | 10 | ~80 | 1/7/8/13/32/63/64-bit + cross-byte |
| v1 -> v2 save migration | 8 | ~30 | Weapon clamp + zero-mask filter |
| manifest container | 8 | ~30 | Clear/Free/Add/AddMod/grow |
| manifest hash | 3 | ~12 | Determinism + sensitivity |
| manifest serialize | 5 | ~50 | Full roundtrip + truncation + SEC-5 |
| manifest diff | 5 | ~30 | 4-way classification |
| random-pool selector | 10 | ~60 | Lock + filter + fallback + determinism |
| **Total** | **77** | **770** | |

## Coverage queued (second cohort)

These were called out in Mike's directive but deferred for follow-up
session(s) -- see the design doc Section F:

- IMC stack (input-context push/pop, active-scheme correctness)
- Menu stack (push/pop dedup, pool API)
- Master loader state machine (FLUX -> HANDS -> GUN -> CARTS -> LOADED)
- Hand state machine (CHANGEGUN / LOAD / IDLE / ATTACK transitions)
- Mission / mode transitions
- Catalog dependency-graph traversal
- Per-message netmsg encode/decode roundtrips (would need ~7 globals
  + 11 stub functions; queued)

## Adding a new test

1. Pick the subsystem. If a `tests/test_<subsystem>.cpp` exists, add a
   new `TEST_CASE` to it. Else create a new file.
2. If the new test exercises a `.c` file not yet in the build, add it
   to the `SRC_TESTS` list in the top-level `CMakeLists.txt`.
3. If the new test needs a stub for a linker-required symbol, add it
   to `tests/stubs.c`. Keep the stub semantically inert (return NULL,
   no-op) unless the test itself injects different behavior.
4. Tag your `TEST_CASE` with `[<subsystem>]` so the subset filter works.
5. Build and run:
   ```bash
   source devtools/build-env.sh && ninja -C Build pd-tests && ./Build/pd-tests
   ```

## When to add a test

Per the design doc Section F, add a test when:

- A bug fix touches a primitive layer (bit-pack, byte order, hash,
  diff, parser). The next regression in this layer should be caught
  by your test, not by a player.
- A constant gets bumped (NET_PROTOCOL_VER, MPSETUP_VERSION). Update
  the pin to match.
- A new wire field or save field is added. Add a roundtrip test that
  the field survives encode -> decode unchanged.

When NOT to add a test:

- Rendering output. Out of scope for `pd-tests`.
- Anything needing SDL2, OpenGL, ENet. The test binary doesn't link
  these.
- One-shot bug repros that aren't a regression class. Use the playtest
  dashboard.
