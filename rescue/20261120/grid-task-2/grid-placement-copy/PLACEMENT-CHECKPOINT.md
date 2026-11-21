# The Grid native prop placement copy

Workbench T-MODDING-021, claim N-1082. Chat `6aff6b58-36d8-8191-a67f-0b2678a80000` at canonical dev `92200bbdca4127a3d6ab864f197738180ed71819`. Writer `placement_copy` worked only in this workspace directory. The canonical native editor and I/O files remain owned by their existing session.

## Source and patch evidence

The three exact baseline files were copied before editing. N-1059 placement, N-1065 editor flow, and N-1065 parent lineage applied in order after each SHA-256 matched the old receipt and each patch passed `git apply --check --whitespace=error-all`. Baseline, recovered, and final source bytes are preserved separately.

| Source | Baseline SHA-256 | Final SHA-256 |
| --- | --- | --- |
| `port/fast3d/pdgui_forge_editor.cpp` | `a50aa7d6b9d68d1bd04f6d4ff5f3e1eabd0fc15d5cc4b404df9245634e23e581` | `e5f9a078834bfcf3b86c2517a1dba2ccf398c55ef2e585d9dd85bbd407253b9c` |
| `port/include/forge_level_io.h` | `1efb8e31a7155325ddce7f90e6f887bc4b6044f3d914553dca59281d3f823d3e` | `fbdef5d52d8bc1268725d7e3d90d4f47155efdf713959867d01d4f98772319a2` |
| `port/src/forge_level_io.c` | `6b5a79af5ab82a75f1c44b54909fad9da60db17a5f4516920c20853aa131dfac` | `d16c099d6dc0347000bd94f1ca1bd4479397f03b7b9c5d721c034d3534359740` |

- `grid-native-placement-complete-copy.patch`, SHA-256 `2ac393c4edd53eb34a796be5803d801e58a0f94dc52f381b5db41647d0662499`, reproduced all three final files byte-for-byte from the baseline with the existing Windows Git line-ending convention. Forward apply and reverse apply-check passed. The rejected first combined diff generated with `core.autocrlf=false` is retained as a line-ending artifact.
- `grid-native-drag-placement-only.patch`, SHA-256 `cc2855140c8f4270aed87728a7e232680f1c1271a4b3775d0d1350649f539554`, reproduced all three final files byte-for-byte from the recovered drafts. Forward apply and reverse apply-check passed.
- The initial queued syntax check `q_20261119215243086_23bb56d9` rejected an implicit `isfinite` declaration because the project math header shadows the system macro. The cursor guard now uses `__builtin_isfinite`, consistent with the existing room-coordinate check in `forge_level_io.c`. Both earlier patches and the failed queue result are preserved. This finite-check correction changes only the I/O source hash above.

## Placement behavior

Selecting a model by click or mapped control arms the reticle. Dragging a palette row arms a pointer preview, then a release over a valid native floor places the prop. Both gestures call the same C command, which rechecks document and stage identity, catalog model availability, ray, half-grid snap, and native room before reserving one undo and adding the prop. The pointer uses the camera transform already used by game targeting. The floor footprint, text, and status colors show validity. Cancel, focus loss, blocked windows, and invalid drops leave the document and undo history alone. The prior controls are restored after the physical input is released.

## Verification and remaining work

This worker verified source-copy patch application only. The parent owns the queued MinGW syntax check, exact native build, runtime and hardware tests, and Workbench update. No game, build, test or canonical source mutation was done by this worker. Release the N-1082 workspace-copy writer lane after this freeze.

The preview is a projected floor footprint. Full-model rendering, stock controller B in the separate action map, overlap tests, and manual drag and mapped controls still need work or proof. N-1023 Save As then Play Draft, broader structural editing, and multiplayer acceptance remain separate. Before considering any canonical application, reconcile the existing native editor and I/O owners through the current Workbench.
