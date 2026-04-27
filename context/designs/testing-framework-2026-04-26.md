# Testing Framework — 2026-04-26

> **Status:** ADR + execution plan. First cohort lands in this same session.
> **Author:** AI session `cool-dirac-4af9b8`, on Mike's directive.
> **Branch:** `claude/cool-dirac-4af9b8` -> `dev`.

---

## A. Goals and non-goals

### Goals

A **`pd-tests`** binary that builds via the existing CMake/ninja pipeline and
produces a green/red report on a set of unit, roundtrip, and state-machine
tests. The bar is low: a developer (or AI session) can run `ninja -C Build
pd-tests && ./Build/pd-tests` and see whether the project's most regression-
prone primitive layers still behave correctly.

The four kinds of bug this is designed to catch:

1. **Bit-pack drift.** A change to a savebuffer or netbuf primitive that
   silently changes the byte layout (the kind of bug that only surfaces when
   a v1 save loads on a v2 build, or a v43 client connects to a v44 server).
2. **Wire / save format migration regressions.** A new field added without
   the migration helper, or a migration that clamps the wrong range.
3. **Pure helper drift.** Functions like `connectCodeEncode/Decode`,
   `manifestComputeHash`, `manifestDiff`, `packWeaponSetRandomFilters` whose
   outputs must satisfy a roundtrip property — easy to break, easy to test.
4. **Constant pinning.** `NET_PROTOCOL_VER`, `MPSETUP_VERSION`, `MAX_PLAYERS
   + MAX_BOTS <= 64` (the active-mask wire format cap). A test that fails on
   a constant change forces the change to be deliberate, not accidental.

### Non-goals

- **Rendering correctness.** No GBI dispatch tests, no GL framebuffer
  comparisons, no font-rasterisation diffs. The renderer is tested by Mike
  in-game.
- **Animation playback.** Per-frame animation interpolation is timing-
  dependent and not a regression class we have evidence of suffering from.
- **Gameplay feel.** Movement, jump arc, weapon recoil, hitscan timing.
  These are tuned by Mike with the playtest dashboard.
- **AI behavior.** Bot decision trees, target selection, navigation. Same
  reason as gameplay feel — there is no oracle for "correct" behavior here.
- **Real-socket networking.** ENet handshake, UDP send/recv, NAT traversal.
  These need a live socket and are tested by Mike running listen + join in
  two clients. The closest unit-testable analog is the wire-format
  encode/decode pair (covered).
- **Visual diffing.** Screenshot comparisons. Out of scope.
- **Recorded-input replay.** A future capability, not this session.
- **Anything that needs the SDL2 / OpenGL / ImGui main loop.** The test
  binary deliberately does not link those.

---

## B. Framework choice and rationale

### Decision: **Catch2 v2 single-header**

Specifically `catch.hpp` from the Catch2 v2.x amalgam (the last stable
single-header release of the v2 line, designed exactly for this drop-in
use case). Drop into `port/include/catch.hpp`. No package manager
involvement, no submodule, no git LFS.

Why Catch2 v2 single-header:

- **Single header, no separate compile unit.** The whole framework is one
  `.hpp`. Drop in, `#include`, write tests. No build-system gymnastics.
- **Header-only fits the existing build.** No find_package, no FetchContent,
  no package manager. The CMakeLists.txt just adds an `add_executable`
  target that lists the source files — same pattern the project already
  uses for `pd`, `pd-server`, `logview`, `pd-updater`.
- **C++ host, C tests OK.** Catch2 macros are C++, but C code under test is
  trivially callable via `extern "C"`. The project already mixes C game
  code with C++ port code — this is the same pattern.
- **No runtime.** No fixtures-as-base-classes, no test-discovery DLL
  loader. `TEST_CASE` macros register themselves at static-init time. The
  binary self-discovers. Self-contained.
- **Zero existing test infrastructure to fight.** The project has no `tests/`
  directory yet. Picking Catch2 means we never have to migrate from
  something else.

### Alternatives considered

- **Unity (pure C).** Considered. Rejected because the test code itself
  benefits from C++20 ergonomics (`std::vector`, range-based for, lambdas)
  while the *code under test* stays C. Unity would force the test runner
  to be pure C, which is awkward for the manifest-diff and string-roster
  tests where a `std::vector<std::string>` is the natural data model.
- **GoogleTest.** Considered. Rejected because it needs a separate
  `gtest_main` library, file-system layout conventions, and either a
  submodule or a vendored copy of the same. Heavier setup for the same
  outcome. Catch2's `TEST_CASE`/`SECTION`/`REQUIRE` is roughly equivalent
  to GoogleTest's `TEST`/`EXPECT` for our use case.
- **No framework, hand-rolled assert harness.** Considered briefly — it's
  what Unity boils down to. Rejected because the second time we want a
  parameterised test (`SECTION` with N cases) we'll regret not using a
  framework. Catch2 gives us this for the same compile cost.

### What this is NOT

- Not a runtime framework. The Catch2 v2 amalgam is build-time only — no
  DLL, no runtime hooks, no `LD_PRELOAD`-style instrumentation.
- Not a mocking framework. We do not mock function calls. Where a test
  needs a stub, we link a different `.c` file containing the stub.

---

## C. Build integration

### CMake target

A new `pd-tests` executable target in the top-level `CMakeLists.txt`,
parallel to `pd` / `pd-server` / `logview` / `pd-updater`.

The target's source list is **explicit**, not GLOB. Each test file
declares which `.c` files from `src/` and `port/src/` it needs by
including them via the `pd-tests` source list. This keeps the test
binary small and the link time fast.

Approximate shape:

```cmake
# pd-tests: unit/roundtrip/state-machine test runner.
# Self-contained; does not link the full game (no SDL, no GL, no ImGui).
set(SRC_TESTS
  tests/main.cpp                              # Catch2 entry point
  tests/test_smoke.cpp                        # Sanity check
  tests/test_netbuf.cpp                       # Wire format primitives
  tests/test_connectcode.cpp                  # IP <-> word sentence roundtrip
  tests/test_manifest.cpp                     # Container ops, hash, serialize/deserialize, diff
  tests/test_savebuffer.cpp                   # Bit-pack/unpack primitives
  tests/test_random_pool.cpp                  # mpSetRandomWeapons specification
  tests/test_versions.cpp                     # NET_PROTOCOL_VER + MPSETUP_VERSION pin
  tests/test_save_migration.cpp               # v1 -> v2 weapon-cull migration semantics
  tests/stubs.c                               # sysLogPrintf, assetCatalogResolve, etc.

  # Code under test (cherry-picked, not GLOB):
  port/src/net/netbuf.c
  port/src/connectcode.c
  port/src/net/netmanifest.c
  src/game/savebuffer.c
)

add_executable(pd-tests ${SRC_TESTS})
target_include_directories(pd-tests PRIVATE
  port/include
  ${CMAKE_BINARY_DIR}/port/include
  src/include
  include
  include/PR)
target_compile_definitions(pd-tests PRIVATE
  PD_TESTS=1
  CATCH_CONFIG_MAIN  # Catch2 generates main() in the TU that defines this
  AVOID_UB=1
  _LANGUAGE_C=1
  PAL=0
  VERSION=2)
```

Note that `tests/main.cpp` is the TU that defines `CATCH_CONFIG_MAIN` so
Catch2 generates the `main()` function exactly once. All other test
`.cpp` files just include `catch.hpp` without that define.

### How to run

```bash
source devtools/build-env.sh
ninja -C Build pd-tests
./Build/pd-tests
```

Catch2 reports `All tests passed (N assertions in M test cases)` on
success or a structured failure list otherwise. Exit code 0 = green,
non-zero = red.

Useful flags:
- `./Build/pd-tests --list-tests` — enumerate registered test cases
- `./Build/pd-tests "[manifest]"` — run only tests tagged `[manifest]`
- `./Build/pd-tests -s` — show successful assertions too (default is
  failures only)

### Build-environment integration

The `pd-tests` target uses the same MSYS2/MinGW toolchain as `pd` and
`pd-server`. No new toolchain dependencies. `source devtools/build-env.sh`
is the only setup needed. The `build-headless.ps1` script will also pick
up the new target since it builds whatever ninja knows about.

---

## D. Test categories and harness shapes

### Pure unit tests (no harness)

Functions whose behavior is fully determined by their arguments. No
globals, no state. The test calls the function with crafted inputs and
asserts the output.

Examples:
- `connectCodeEncode(0x7f000001, buf, sizeof(buf))` -> some 4-word string
- `connectCodeDecode("sneaky falcon chasing castle", &ip)` -> 0, ip set
- `s_fnv1a("base:dark_combat")` -> a specific u32 (computed once,
  pinned)

Shape:

```cpp
TEST_CASE("connectCode encode and decode roundtrip", "[connectcode]") {
  u32 ip = 0xC0A80101;  // 192.168.1.1
  char buf[CONNECT_CODE_MAX];
  REQUIRE(connectCodeEncode(ip, buf, sizeof(buf)) > 0);

  u32 decoded = 0;
  REQUIRE(connectCodeDecode(buf, &decoded) == 0);
  REQUIRE(decoded == ip);
}
```

### Roundtrip tests (minimal harness)

Encode something, decode it, assert equality. The "harness" is a buffer
on the stack and a pair of helper functions.

Examples:
- netbuf primitives: write u32 0xDEADBEEF, read u32, assert equal
- manifestSerialize -> manifestDeserialize -> assert manifests structurally
  equal
- savebufferOr / savebufferReadBits: write 13 bits of 0x1A4F, read 13 bits,
  assert equal

Shape:

```cpp
TEST_CASE("netbuf u32 little-endian roundtrip", "[netbuf]") {
  u8 backing[64] = {0};
  netbuf b{ backing, sizeof(backing), 0, 0, 0 };
  netbufStartWrite(&b);
  netbufWriteU32(&b, 0xDEADBEEFu);
  netbufStartRead(&b);
  REQUIRE(netbufReadU32(&b) == 0xDEADBEEFu);
  REQUIRE(b.error == 0);
}
```

### State-machine tests (synthetic event-driver harness)

A small driver loop that feeds events to a state machine and asserts
state transitions. The first cohort does not include a state-machine
test — they are queued for the second cohort (master loader, hand
state machine).

Shape (illustrative, for second cohort):

```cpp
TEST_CASE("Master loader transitions FLUX -> HANDS -> GUN -> CARTS -> LOADED") {
  ml_state_t s = ml_init();
  REQUIRE(s.phase == ML_FLUX);
  ml_step(&s, ML_EVENT_PHASE_DONE);
  REQUIRE(s.phase == ML_HANDS);
  // ...
}
```

---

## E. Mocking and global-state strategy

### Strategy: link-time stubs, not function pointers

The decompiled C code in `src/game/` leans heavily on globals. We do
NOT try to mock the entire global state surface. Instead:

- **Pure functions get tested directly.** No setup. No globals touched.
  `connectCodeEncode`, `netbufWriteU32`, `manifestComputeHash`,
  `savebufferOr`. These are the meat of the first cohort.

- **Functions with light global dependence get a `tests/stubs.c`.** A
  single `.c` file in `tests/` provides minimal definitions for
  `sysLogPrintf` (printf-to-stderr), `assetCatalogResolve` (returns
  NULL — forces the synthetic-fallback path in netmanifest), and a
  handful of other functions that are linker-required but not
  semantically interesting. The stub file is small and centralised.

- **Functions that touch heavy globals (g_MpSetup, g_NetClients, etc.)
  are tested through a smaller pure helper or skipped.** For the v1->v2
  save migration, instead of trying to set up a fake `g_MpSetup` and
  invoke `mpsetupfileLoadWad`, we replicate the migration rule in the
  test as a "specification" and verify the rule against a hand-built
  fake input. This tests the *semantics* without coupling to the giant
  global surface. Where the rule lives in the real code is documented in
  the test comment so a future drift is auditable.

- **No init helpers in this cohort.** If a test needs init, it
  initialises only the data structure on its own stack. The first
  cohort is deliberately scoped to functions that don't need a
  meaningful global init.

### Why not a full game-state init?

Pulling in `mainInit()` would cascade into every subsystem (audio,
graphics, network, mod manager, asset catalog, etc.) and the test binary
would become a parallel `pd` executable. That's the wrong shape. The
linker-stub approach keeps `pd-tests` a focused tool.

---

## F. Coverage roadmap

### First cohort (this session)

| Subsystem | Target file(s) | Why first |
|---|---|---|
| Wire format primitives | `port/src/net/netbuf.c` | The base layer of every wire message. If `netbufReadU32` ever produces the wrong byte order, every netmsg breaks. |
| Save format primitives | `src/game/savebuffer.c` | The base layer of every save file. Same logic — bit-pack drift breaks every save. |
| Connect codes | `port/src/connectcode.c` | Pure function, perfect roundtrip target, regression-prone (history of byte-order confusion). |
| Manifest container ops | `port/src/net/netmanifest.c` | Add / Clear / Free / ComputeHash / Serialize / Deserialize / Diff. Used by every match start. |
| Random-pool selector | `mpSetRandomWeapons` specification | Mike asked specifically. Tests the unlock-filter -> eligible-pool transformation. |
| Wire format roundtrip | netbuf + manifest + connect code | Per-type encode/decode pairs at the netbuf layer; full manifest roundtrip at the netmanifest layer. |
| Save format roundtrip + migration | savebuffer bit-pack + v1->v2 weapon-cull rule | Full bit-pack roundtrip at the primitive layer; migration semantics replicated as a unit test. |
| Constant pinning | `NET_PROTOCOL_VER`, `MPSETUP_VERSION`, `MAX_PLAYERS + MAX_BOTS <= 64` | A version bump without a corresponding test update is now visible. |

Estimated test count: **40-70 assertions across 20-30 test cases**.

### Second cohort (follow-up sessions)

| Subsystem | Target | Reason for deferring |
|---|---|---|
| IMC stack | `inputCtxPush/Pop`, active-scheme correctness | Requires SDL backend stubbing; manageable but bigger than first-cohort scope. |
| Menu stack | `menuPushDialog`, `menuPopDialog`, dedup logic, pool API | Requires menu-pool stubs; manageable but coupled to the legacy menu storage. |
| Master loader | FLUX -> HANDS -> GUN -> CARTS -> LOADED state machine | The state machine is real and worth testing — would have caught the round-7 charpreview-vs-master-loader race programmatically. Needs a small event-driver harness. |
| Hand state machine | CHANGEGUN / LOAD / IDLE / ATTACK transitions | Same shape as master loader — small synthetic event-driver. |

### Third cohort

- Mission state and mode transitions (campaign, MP, Counter-Op, Co-op)
- Asset catalog dependency-graph traversal
- Sessioncatalog ref allocation and lifetime
- Spawn pool L1-L4 fallback chain
- Ready gate countdown lifecycle (the SP-14 class)

### Permanently deferred (out of scope unless tooling shifts)

- Anything that needs `SDL_Init`, `glContext`, or `ENetHost`
- Anything that needs a real ROM file
- ImGui rendering paths
- Sound mixing
- Disk persistence (mod loading from `mods/`)

---

## G. CI / pre-merge integration (forward-looking)

### Intent

`pd-tests` should run before any merge to `dev`. The first cohort
landing in this session establishes the binary; wiring it into a
gate is a follow-up.

### Today (2026-04-26)

- No GitHub Actions or equivalent CI exists for this repository.
- The build is local (Mike on Windows, AI in worktrees on Windows).
- Pre-merge verification today is: AI runs `ninja -C Build pd
  pd-server`, eyeballs warnings, verifies binary sizes are roughly
  unchanged. After this session: also `ninja -C Build pd-tests &&
  ./Build/pd-tests`, eyeball "All tests passed".

### Suggested next steps (not done in this session)

1. Add a `pd-tests` invocation to `devtools/build-headless.ps1` so the
   automated build pipeline runs the suite as a final gate.
2. Add a `tests` line to `dev-window-v2`'s status panel so Mike sees
   green/red without typing.
3. If/when GitHub Actions is wired up, add a `tests` job that runs
   `pd-tests` on every push.

---

## H. Test conventions

### File naming

- One `.cpp` file per subsystem under test: `tests/test_<subsystem>.cpp`.
- Catch2 entry point: `tests/main.cpp`.
- Stubs: `tests/stubs.c`.
- Shared test helpers (if needed): `tests/test_support.{h,cpp}`.

### Test case naming

```cpp
TEST_CASE("netbuf u32 little-endian roundtrip", "[netbuf]");
TEST_CASE("connectCodeEncodeWithPort: non-default port preserves on roundtrip", "[connectcode]");
TEST_CASE("manifestDiff: empty current vs populated needed -> all to_load", "[manifest][diff]");
```

The convention:
1. Lead with the subject under test (function or struct).
2. Describe the property being asserted in human terms.
3. Tag with `[<subsystem>]` so you can run subset suites.

### Assertion conventions

- `REQUIRE(x)` for invariants — failing aborts the test case.
- `CHECK(x)` for soft assertions — failing logs but continues.
- Prefer `REQUIRE` by default. `CHECK` only when one failure shouldn't
  prevent learning about subsequent failures in the same case.
- `REQUIRE(a == b)` over `REQUIRE(a); REQUIRE(b == 5);` — Catch2 prints
  both sides on failure.

### How to add a new test

1. Pick the subsystem. If a `tests/test_<subsystem>.cpp` exists, add to
   it. Else create one.
2. If your subsystem needs new code under test, add the `.c` file to
   the `SRC_TESTS` list in `CMakeLists.txt`.
3. If your subsystem needs new stubs, add them to `tests/stubs.c`.
4. Run `ninja -C Build pd-tests && ./Build/pd-tests` and verify green.

### One test, one behavior

Don't bundle. A test that asserts "manifest add, then hash, then diff,
then serialize, then deserialize" tells you nothing useful when one
step fails. Split into one `TEST_CASE` per behavior, group with a
shared `[manifest]` tag.

---

## I. Scope guards

### Out of scope, period

- Any test that requires `SDL_Init` to succeed. The test binary does
  not link SDL2.
- Any test that requires an OpenGL context. The test binary does not
  link GL.
- Any test that requires `enet_initialize` to succeed. The test binary
  does not link ENet.
- Any test that opens a real socket (TCP, UDP, ICMP, anything).
- Any test that reads or writes a real file outside the test working
  directory.
- Any test that requires a ROM file or `assets/` content.
- Any test that depends on a specific render frame timing, audio
  buffer state, or input event from a real device.

### Out of scope unless explicitly added

- Tests that mutate global state across test cases. Catch2 runs tests
  in registration order but they should be order-independent. Any
  test that mutates a global must reset it on entry.
- Tests that call into `mainInit` or any equivalent broad-init
  function. The cascade is too wide.

---

## J. Decisions made during execution

This section is a running log of any non-trivial calls made
autonomously while landing the framework. Mike can audit here without
re-reading the diff.

| Date | Decision | Why |
|---|---|---|
| 2026-04-26 | Catch2 v2 single-header (`catch.hpp`), not v3 (split-header) | v2 amalgam is one file, drops in cleanly with no extra build setup. v3 requires building the framework as a separate static lib. The functional difference is invisible for our test set. |
| 2026-04-26 | Cherry-pick `.c` files into `pd-tests` source list, not `file(GLOB)` | The full game's GLOB pulls in 600+ files and cascades into globals. Cherry-picking keeps the test binary at ~10 source files. |
| 2026-04-26 | Stub `assetCatalogResolve` to return NULL in tests | Forces the synthetic-fallback path in `manifestAddEntry`. The synthetic path uses `s_fnv1a(id)` which is deterministic and pure. Avoids pulling the entire asset catalog into the test binary. |
| 2026-04-26 | Replicate v1->v2 migration rule as a pure helper in the test | The real migration lives inside `mpsetupfileLoadWad` which depends on `g_MpSetup`, `g_BotConfigsArray`, scenario state, etc. A unit test of the migration *rule* (clamp values >= 0x27 to MPWEAPON_DISABLED) is the meaningful invariant. The test comment links to the source line so a drift between rule and implementation is auditable. |
| 2026-04-26 | Compile-time pin: assert `NET_PROTOCOL_VER == 44` and `MPSETUP_VERSION == 2` | A future bump without a corresponding test update is now an explicit decision to update the pin. Catches accidental version bumps in unrelated PRs. |
| 2026-04-26 | First cohort excludes the netmsg encode/decode pairs themselves | The `netmsgSvc*` and `netmsgClc*` functions in `netmsg.c` (7800 lines) heavily depend on `g_MpSetup`, `g_NetClients`, `g_BotConfigsArray`, `g_MatchConfig`. Testing one would cascade into a much larger stub surface. The roundtrip property at the netbuf primitive layer + the manifest serialise layer covers the actual risk class (byte-order drift, length-prefix misalignment, version-field omission). Per-message encode/decode tests are queued for the second cohort. |
| 2026-04-26 | "Cross-version reject" tested as a constant pin + fake-version-mismatch helper, not as a real ENet handshake | The actual reject (`netServerEvConnect` in `port/src/net/net.c`) requires a live ENet host. The semantic invariant — the protocol version constant is what we say it is, and the ENet auth handler will reject any other value — is captured by pinning the constant and asserting that a version other than `NET_PROTOCOL_VER` does not match. |

---

## K. Anti-patterns the framework is designed to discourage

- **A test that passes by luck.** If the test would still pass after a
  one-character change in the function under test, the test is too
  weak. Roundtrip and constant-pin tests are designed to fail loudly
  on small drift.
- **A test with hidden setup.** If a test depends on a global that's
  set by another test running first, it's broken. Catch2 will run
  cases in different orders. Initialise on entry.
- **A test that requires the full game.** If `pd-tests` ever starts
  pulling in 200+ source files, something is wrong. Add stubs, not
  more dependencies.
- **A test that's actually three tests.** If a `TEST_CASE` has more
  than one logical behavior under test, split it. The first failure
  hides the others.
- **A test that's a smoke test masquerading as a unit test.** "It
  doesn't crash" is not an interesting assertion unless the
  not-crashing is the property under test (e.g., NULL safety in a
  zero-init manifest).

---

## L. Open questions for follow-up sessions

These do not block the first cohort, but should be revisited:

1. Should `pd-tests` be a separate build target, or a `add_test()`
   integration with `ctest`? Right now we just build and run it
   directly. `ctest` would let us register tagged subsets, which is
   nice for CI.
2. Should the netmsg per-message encode/decode tests be done at the
   `netmsgSvc*Write/Read` level (requires global stubs) or via a
   parallel "pure encoder" extracted from the existing functions?
   The latter is cleaner but doubles the maintenance surface.
3. State-machine tests: do we extract the state machines to their
   own `.c` files (clean, testable) or add an event-injector for the
   existing in-place state? The cleaner answer is extraction; the
   smaller-diff answer is in-place. Decide per state machine.
4. Property-based testing (rapidcheck, etc.): nice for roundtrip
   tests but not worth the dependency for our scale today. Revisit
   if the test surface grows past ~200 cases.

---

## M. References

- Catch2 v2 documentation: https://github.com/catchorg/Catch2/tree/v2.x/docs
- `port/src/net/netbuf.c` -- wire format primitives under test
- `port/src/connectcode.c` -- pure roundtrip target
- `port/src/net/netmanifest.c` -- container ops + serialize/deserialize +
  diff
- `src/game/savebuffer.c` -- save format primitives under test
- `port/include/net/net.h:12` -- `NET_PROTOCOL_VER` definition (currently 44)
- `port/src/mpsetups.c:35` -- `MPSETUP_VERSION` definition (currently 2)
- `src/game/mplayer/mplayer.c:4474-4493` -- v1 -> v2 weapon-cull migration rule
- Mike's directive: this session's prompt
