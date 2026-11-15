# Menus/Input initial Settings persistence repair, 2026-10-01

09:53 current checkpoint: independent native asset/source/animation/body/head and
all63-character gates pass on rebuilt clientD9CFC0B8. No ordinary menu, device,
Settings save/restart, keyboard/mouse/controller or screenshot acceptance was
attempted during the headless grant. Previous helper/config/profile passes remain
valid; ordinary acceptance still needs an authorized interactive executor.
Heavy resources released09:40:56.814UTC and Astral notified; no owned game remains.
See `.claude/pd-initial-integration/20261001-native-validation/summary.json`.
Existing Settings implementation attributiongpt-6.1-sol remains unchanged.

Historical08:23 takeover: own-worker read-only facts show CodexSandboxDesktop, foreground0
and input desktop read denied5. Authorized interactive executor required before
ordinary menu/input/capture retry; no bypass, focus control or policy change used.
Prior Settings source/tests remain; no keyboard/mouse/controller/visual acceptance
added. Independent asset/native probes can run next parent window. Evidence:
`.claude/pd-initial-integration/20261001-texture-repair/desktop-prerequisite.json`.

Workbench: T-MENUS-007, dependent on T-TOOLING-008 and T-MENUS-003/004/005,
T-INPUT-005. Coordination identity: codex-0e8900ca-467b81e8, inherited from
the parent. Implementation model: gpt-6.1-sol. Mike's October 1 approval
resumes the assigned initial lane; old pause evidence remains historical.

## Proven source defects

The Settings Input editor supports sixteen device rules, each with a
127-character stable key, 63-character alias and one profile digit. The
complete existing delimiter format needs 3,103 payload bytes plus NUL.
`inputUiAppend` silently clipped those rules into a 1,024-byte local buffer,
and `input.c` stored the same clipped capacity. It could drop later devices
or retain a partial final key.

Following the write through restart found two additional clipping boundaries:
`configLoad` read physical fragments into a 2,048-byte line buffer, and
unregistered settings were retained in a 256-byte pending value. Production
startup calls `configInit` before `inputInit`, so the pending path is required.

## Source candidate

- `port/include/input_profile_metadata.h`: one lean shared field/count contract
  derives complete device-rule and profile-name capacities. `input.h` imports
  it for `input.c`; the pure serializer imports the lean header directly.
- `pdgui_input_profile_state.h`: a bounded, complete two-field serializer and
  the existing delimiter sanitization. Invalid bounds or capacity reject
  without changing either output field.
- `pdgui_menu_mainmenu.cpp`: uses the shared serializer/storage contract;
  rejection restores accepted in-memory config and explains the failure.
  A rejected new-device registration does not retain a fabricated UI rule.
- `config.c`: reads complete logical lines with checked buffer growth and
  EOF-without-newline handling. Pending strings own complete heap copies;
  replacement releases the prior copy, replay frees the consumed copy, and
  allocation/stream failure returns a failed load instead of parsing a prefix.
  The string writer adds an outer quote pair only for leading quotes or
  boundary whitespace, preserving valid Input names through the existing
  trim/unquote reader while retaining ordinary raw string output.

The existing delimiter format and public config keys remain compatible.
ASCII field sanitization remains the established behavior, including replacing
reserved `|;~=` characters with spaces.

## Verification contract and current evidence

Cheap scoped `git diff --check` passes. No native build, automated test run,
game launch, capture or physical-device intervention has occurred in this
worker. Parent coordinates the Astral verification window and CMake
registration for `tests/test_pdgui_input_profile_state.cpp`.

The parent's first native tests build failed in `pd-settings-save-tests`:
the pure helper imported `input.h`, which pulled in `PR/os_message.h` without
its `OSThread` engine prerequisite. Failure logs remain with the parent.
The corrected source imports only `input_profile_metadata.h` from the helper;
the game input header imports that same single contract. No engine typedef
or duplicated constants were introduced. Native retry is pending; this
correction expands the source freeze to ten owned files.

New direct behavioral tests cover the exact maximum supported payload,
atomic capacity rejection, invalid profile/unterminated fields, reserved
delimiter sanitization and omission of empty-key rules. A small source pin
requires production serialization/rejection before either setter or save.

Three new tests in the existing real `pd-settings-save-tests` target cover all
six maximal profile names and sixteen maximal rules through unregistered
load, pending save, registration replay, registered save/reload, failed
atomic save and retry. They exercise LF, CRLF, a final line without newline,
following-key separation, literal quote/backslash content and replacement
of an 8,500-byte pending value. Literal leading/trailing quotes, backslashes
and boundary whitespace pass through both pending and registered save/reload
paths in the test contract. Required native selectors:
`[profile-state]`, plus the existing broad input/menu/settings regression gate
and the complete dedicated Settings save target.

## Acceptance still open

The root subsequently compiled the client, pd-tests and dedicated Settings-save
target. Current production gates pass: combined preview/profile/character
10 cases/82 assertions, broad input/menu/settings 246/10,033 and complete
Settings-save 14/236, each zero failures (selectors overlap). Exact binaries/XML:
`.claude/pd-initial-integration/20261001T041916343Z-naAive/`. The corrected lean
header therefore has native compile/test proof; the earlier failures are retained.
The older 241/9,995 gate is historical, and the historical complete suite retains
eighteen failed names. Ordinary-client acceptance remains pending.

Ordinary Agent Create/Back on the current client remains unverified. The latest
October 1 retry failed before controller attachment when a shortened private
30s readiness wait expired during weapon bootstrap, before main-menu arming:
10/29 assertions, zero input events or captures. Post-run foreground=0 and an
invalid-window-handle diagnostic do not establish the readiness cause. The
keyboard fixture was not launched. New private variants restore all canonical
input steps, 180s readiness and preview 240s/keyboard 280s overall limits; static
checks pass, native retry needs a new parent window. See the initial integration
audit and `.claude/pd-initial-integration/20261001-combined-run/summary.json`. Runtime
Settings keyboard/mouse/controller traversal, save/restart/error UI, physical
controller disconnect/capture, glyph changes, scaled layouts and human visual
acceptance remain distinct gates. Focus-independent helper/config tests can
prove serialization and disk persistence but cannot establish those gates.
Character preview/model rendering belongs to the asset worker. This lane does
not change `menu.c`, model rendering, network or asset source adapters.
