# T-MENUS-002 themed action-footer validation — 2026-08-08

## Result

The production Agent Select action footer is connected through a scale-safe,
theme-font-aware docked layout and is ordinary-client verified at 1280x720 and
1024x576 with 200% UI scale. Solo Briefing and Inventory use the same primitive
for the identical dynamic-hint pattern. Workbench status is `implemented`, not
`validated`, because a real physical controller/device-switch receipt remains
open under `V-004`.

## Root cause and implementation

Agent Select subtracted title and footer constants from the complete dialog
height after ImGui's cursor had already advanced below the title/header. The
scrolling list consumed the actual remaining footer space, placing the live
action-map labels below the themed panel.

`pdguiResolveHintFooterLayout` now partitions the actual
`GetContentRegionAvail()` height without overflow. `pdguiHintFooterHeight`
measures wrapped dynamic text with the active ImGui font, padding, and spacing;
`pdguiDrawHintFooter` draws a fixed non-scrolling child. Existing action labels
and handlers remain intact: Accept, Secondary/context/right-click, Delete,
Tertiary, Up, Down, and Cancel.

Production files:

- `port/include/pdgui_layout.h`
- `port/fast3d/pdgui_layout.cpp`
- `port/fast3d/pdgui_menu_agentselect.cpp`
- `port/fast3d/pdgui_menu_solomission.cpp`

## Source-frozen automated evidence

- Source fingerprint: `.claude/session-builds/tcat002/tcat002-source-freeze.sha256`
- Isolated combined build: `.claude/session-builds/tcat002/_build-session.out.log`
  — client and updater pass.
- Focused selector `[t-menus-002]`: 46 assertions, 1 test case, pass.
- Adjacent selectors `[input],[menus]`: 1,523 assertions, 68 test cases, pass.
- Geometry matrix covers 1280x720 default and 200% UI/font scale, a short
  viewport, 4K/theme-font growth, and impossible geometry without overflow.

## Ordinary-client evidence

Result file: `.claude/smoke-verify-runs/results-20260808T222543Z.json`.
The result and four compressed frames are also tracked durably under
`context/evidence/2026-08-08-menu-glyph-footer/`.

- `theme_archive_runtime_smoke`: 24/24 assertions, pass. Real SDL Down/Up
  keyboard events used. Both 1280x720 frames show the live MKB footer wholly
  inside the custom-theme panel:
  `.claude/smoke-verify-runs/screenshots/20260808T182203-theme_archive_runtime_smoke/`.
- `theme_menu_footer_scale_smoke`: 19/19 assertions, pass. Real SDL Down/Up
  keyboard events used. Both delayed 1024x576/200% frames show the larger,
  wrapped footer wholly inside the custom-theme panel:
  `.claude/smoke-verify-runs/screenshots/20260808T182333-theme_menu_footer_scale_smoke/`.
- Both scenarios exited normally with no crash, fatal, source-only, fallback,
  or ROM-provider marker.

The same combined binary's final dedicated `theme_stage_ownership_smoke`
receipt is `.claude/smoke-verify-runs/results-20260808T223427Z.json`: 33/33
assertions pass. Exactly five theme-closure rows remain `ref=1 stage_ref=0`
after the ordinary stage transition; exactly five shutdown-specific rows show
the same single explicit owner immediately before all five named releases free
`1->0`.

## Remaining physical-input gate

No physical controller button transition or keyboard-to-controller device
switch was observed in this session. Enumeration and scripted events are not
accepted as controller proof. `V-004` therefore remains partial for real
controller navigation, glyph switching, and the broader ordinary-client input
matrix.
