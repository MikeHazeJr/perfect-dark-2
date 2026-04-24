# Evening Decision Log - 2026-04-23

Tracks judgement calls made while Mike is away. Each decision is reviewable and reversible per the note on rollback.

## Decision: Grid blank-map target stage (REVISED -- escalated back to Mike)
- Context: Mike asked for "the fallback map that loads when a map is invalid, a plane basically" as the Grid Blank Map template. Initial plan was to ship CI Training as a soft-fallback with `GRID_BLANK_STAGE` as a single-constant override. Mike then firmly rejected that: "DO NOT ship CI Training as the blank. ... either find it, or escalate."
- Search passes (all empty):
  - Symbol grep: `STAGE_FALLBACK`, `STAGE_ERROR`, `STAGE_INVALID`, `STAGE_UNKNOWN`, `STAGE_BLANK`, `STAGE_DEFAULT`
  - Handler grep: `fallback_stage`, `safe_stage`, `default_stage`, `invalid_stage`, `blank_stage`, `stagenumDefault`, `boot_safe`
  - Natural-language: "just a plane", "bare plane", "flat plane", "single plane", "blank map", "empty map"
  - Asset-file: `bg_test`, `bg_fallback`, `bg_blank`, `FILE_BG_GEN`
  - Existing debug/test stages: `STAGE_TEST_LEN/LAM/UFF/OLD/ASH/ARCH/DEST/SILO/RUN/MP2/MP6-8/MP14/MP16-20` all reference normal bg files; nothing labelled or commented as a bare plane
  - Stage-load error path: `stageGetIndex(unknown)` returns -1 with a warning; no substitution. `stageSanitizeLoadStagenum(0x00) -> STAGE_CITRAINING` is the only invalid-stage coercer.
- Choice: Ship the Grid submenu WITHOUT a Blank Map entry. `port/include/pdgui_menu_grid.h` declares `GRID_BLANK_STAGE` as undefined; the submenu conditionally surfaces the Blank Map row only when the macro is defined. Zero user-visible reference to CI Training.
- Rationale: The user's statement presumes the fallback exists, but my searches came up empty. Per the escalation directive, shipping CI Training -- even behind FIXME -- would misrepresent the feature. Omitting the entry is truthful and a one-line follow-up swap once Mike names the stagenum.
- Rollback: Define `GRID_BLANK_STAGE <stagenum>` in `port/include/pdgui_menu_grid.h`. Nothing else needed; the submenu picks up the row automatically.
- Follow-up required from Mike: point at the specific stagenum he has in mind, or confirm that no bare-plane stage exists and one needs to be authored.
- Timestamp: start of evening batch (revised before any commit)

