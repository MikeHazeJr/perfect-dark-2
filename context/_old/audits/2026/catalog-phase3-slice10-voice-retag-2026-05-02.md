# Catalog Phase 3 Pass B Slice 10 -- AUDIO_CAT_VOICE retag (Phase 1 audit)

> **Date**: 2026-05-02 PM
> **Session**: condescending-ellis-248824 (continuing the worktree S599 used; new lane assigned post-S600)
> **Predecessors**: S600 close-out at dev `08b07359` (Slices 2/5/6/8/11 segment extraction); arenas migration at `6d3bf4c9`.
> **Plan reference**: [`context/designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md`](../designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md) Slice 10 + Section 3.H of [`context/audits/catalog-coverage-audit-2026-05-01.md`](catalog-coverage-audit-2026-05-01.md).
> **Stop conditions watched**: per-russ-id mapping is stable across builds; the catalog SFX index space (`runtime_index = i` in `assetcatalog_base_extended.c:553-566`) aligns with the russ-id space consumed by `propsnd.c::g_AudioRussMappings[id]` lookups.

---

## A. Problem statement (Coverage audit Section 3.H)

Voice asset class is enumerated in `categoryToType` ("voice" maps to `AUDIO_CAT_VOICE = 2`) but no base-game voice entries register with `category = AUDIO_CAT_VOICE`. The SFX registration loop at [port/src/assetcatalog_base_extended.c:553-566](../../port/src/assetcatalog_base_extended.c:553) sets `category = 0` (AUDIO_CAT_SFX) for all 1545 base entries.

Effect: mod voice replacements have nowhere to register against; mission-briefing voiceovers, in-game banter shouts, civilian death cries all run through the SFX bank but are not distinguished as ASSET_AUDIO category VOICE. Future voice-channel mixing, modder voice-pack overrides, and the audio mod system's voice routing all need this distinction at the catalog row level.

The retag must work without re-extracting any sound data and without changing the SFX bank layout. It is a pure category re-classification on existing catalog rows.

---

## B. Where voice content lives in the codebase

### B.1 Three relevant tables

| Symbol | File:line | Shape | Role |
|---|---|---|---|
| `struct audiorussmapping g_AudioRussMappings[]` | [src/lib/snd.c:178-651](../../src/lib/snd.c:178) | `{ s16 soundnum; u16 audioconfig_index; }` | Maps russ-id (0..0x01bc, 444 entries) to the actual ROM SFX number + audioconfig slot. Consumed by `propsnd.c::psCreateChannel` ([src/game/propsnd.c:773-775](../../src/game/propsnd.c:773)) when a sound has the `hasconfig` bit set. |
| `struct audioconfig g_AudioConfigs[]` | [src/lib/snd.c:653-722](../../src/lib/snd.c:653) | `{ f32 dist1/2/3, pitch; s32 volpercentage, pan, volchangespeed; u32 flags; }` | Per-slot tuning parameters (distance falloff, pitch, pan, prop-flag bits). Slot count: 60 + 3 NTSC-1.0+ (61, 62, 63) = 63 slots total. **No category field.** |
| `enum audioconfig_e` | [src/lib/snd.c:105-176](../../src/lib/snd.c:105) | dense sequential | Enumerator names (`AUDIOCONFIG_00`..`AUDIOCONFIG_60` / `AUDIOCONFIG_63` per VERSION). |

The russ-id is the "PD-style sound id" propagated through the prop-sound channel system. Catalog `runtime_index = i` for `base:sfx_NNNN` represents the same id space (the catalog row is the resolution target for the same id that flows through propsnd / mod overrides).

### B.2 The voice-vs-SFX distinguishing signal

`struct audioconfig` has no explicit voice flag. The two semantically loaded flags are:

| Flag | Bit | Behavior in `propsnd.c::psCreateChannel` |
|---|---|---|
| `AUDIOCONFIGFLAG_RESPONDHELLO` | 0x04 | Sets `PSFLAG2_RESPONDHELLO` ([propsnd.c:801-803](../../src/game/propsnd.c:801)). Tells the engine this sound is the "reply to a friendly chr's hello" trigger. Used by NPC greeting lines. |
| `AUDIOCONFIGFLAG_OFFENSIVE` | 0x10 | Sets `PSFLAG2_OFFENSIVE` ([propsnd.c:813-815](../../src/game/propsnd.c:813)). Marks a sound that triggers nearby chr hostile reactions (turn, draw weapons). Applies to gunshots AND civilian / NPC fear-shouts. |

These are necessary but not sufficient: gunshots also use OFFENSIVE without being voice. Likewise, RESPONDHELLO is voice-only by audit (zero non-voice russ entries use it).

The reliable signal is the **audioconfig slot number**. Inspection of [src/lib/snd.c:178-651](../../src/lib/snd.c:178) russ-table comments (which were written by the original team and document each named voiceline) reveals seven configs that are exclusively used by voice content:

| Config | Flag set | Russ uses | Examples |
|---|---|---|---|
| AUDIOCONFIG_01 | none | 44 | Mission briefings (Carrington, Grimshaw, Jonathan, Elvis radio) |
| AUDIOCONFIG_02 | OFFENSIVE | 44 | Civilian / NPC combat barks ("Oh god I'm hit", "What the hell?", "Damn it") |
| AUDIOCONFIG_03 | OFFENSIVE \| 0x20 | 1 | Carrington urgent ("Damn it, my office. If they get access...") |
| AUDIOCONFIG_47 | none | 22 | Scripted scene dialogue (Cass office, receptionist, programmer, Elvis on Attack Ship) |
| AUDIOCONFIG_48 | none | 5 | Programmer multi-line cluster (Skedar Ruins) |
| AUDIOCONFIG_60 | RESPONDHELLO | 25 | NPC greetings ("Hi there", "Hello Joanna", "How's it going?") |
| AUDIOCONFIG_62 | none | 3 | Death scream / "Noooo!" (NTSC NSC1+ only) |

Total: 144 russ entries flagged as voice content via these configs. Inspection of every russ entry in those configs confirms each row is voice content (most have inline `// "..."` comments by the original team naming the line; the few without comments share the soundnum-prefix pattern of voice content -- `0x82xx`/`0x83xx` for combat barks, `0x90xx`-`0x9bxx`/`0xb3xx`-`0xf4xx` for dialogue / briefing).

**Negative space**: configs that share OFFENSIVE / RESPONDHELLO flags but cover non-voice content do not exist. CONFIG_02 is the only OFFENSIVE-flagged voice config; the other OFFENSIVE-bearing config is CONFIG_01 (briefings have neither flag), CONFIG_03 (urgent voice). All RESPONDHELLO-bearing entries are CONFIG_60 voice. Cross-checked: no russ entry uses a non-voice config to play voice content; no voice russ entry uses a config outside the seven-slot set above.

### B.3 Why russ id == catalog runtime_index

The catalog SFX registration loop assigns `runtime_index = i` for `i = 0..1544`. The russ table's index (the loop position) is the russ-id; russ-ids are the ids passed to `g_AudioRussMappings[id]` from `propsnd.c::psCreateChannel`.

Both spaces share the "PD-level scriptable sound id" semantic. The 1545-vs-444 size mismatch is a sparseness gap: russ entries 0..0x01bc are the explicitly-configured sounds; catalog rows 0x01bd..0x0608 are sounds that don't need russ-style configuration (they play through the default propsnd path with config 0). They still belong in the catalog as identifiable assets.

This means a catalog entry at `runtime_index = N` (`base:sfx_NNNN`) corresponds to the russ-id `N` if `N < ARRAYCOUNT(g_AudioRussMappings)`. The retag walks `i = 0..ARRAYCOUNT(g_AudioRussMappings)-1` and consults `g_AudioRussMappings[i].audioconfig_index`; entries beyond that are SFX by default.

Edge case: russ-id 0x01bc is the terminator entry `{ 0x0000, AUDIOCONFIG_00 }`. CONFIG_00 is non-voice; the terminator naturally classifies as SFX via the same predicate.

---

## C. Retag criteria (decided)

A catalog entry registered via the SFX loop at `assetcatalog_base_extended.c:553-566` is `AUDIO_CAT_VOICE` iff:

1. The entry's `runtime_index` is in `[0, ARRAYCOUNT(g_AudioRussMappings))`, AND
2. `g_AudioRussMappings[runtime_index].audioconfig_index` is one of:
   - `AUDIOCONFIG_01` (mission briefing)
   - `AUDIOCONFIG_02` (NPC combat bark; OFFENSIVE)
   - `AUDIOCONFIG_03` (Carrington urgent)
   - `AUDIOCONFIG_47` (scripted scene dialogue)
   - `AUDIOCONFIG_48` (programmer cluster)
   - `AUDIOCONFIG_60` (NPC greeting; RESPONDHELLO)
   - `AUDIOCONFIG_62` (death scream; NTSC-1.0+ only)

Otherwise the entry is `AUDIO_CAT_SFX` (the default 0; unchanged).

Implementation note: AUDIOCONFIG_62 only exists in NTSC-1.0+ (per the `enum audioconfig_e` definition at [snd.c:105-176](../../src/lib/snd.c:105)). The retag predicate compares against the integer slot value, which is `62` on NTSC-1.0+ builds and undefined on the JP / NTSC-beta build (where the enum stops at AUDIOCONFIG_60). The russ entries that reference CONFIG_62 are themselves wrapped in `#if VERSION >= VERSION_NTSC_1_0`, so a build without CONFIG_62 also doesn't have CONFIG_62-using russ entries -- the predicate match is naturally version-correct.

The audit documents the criterion as a list of seven slot enumerator names so the implementation can `#if`-guard the CONFIG_62 entry against `VERSION >= VERSION_NTSC_1_0` cleanly.

---

## D. Implementation shape

The retag lives in the existing SFX registration loop. Adding a single helper + an `if` branch keeps the diff minimal.

### D.1 New helper

```c
/* Catalog Phase 3 Pass B Slice 10 (2026-05-02): identify the
 * audioconfig slots that exclusively carry voice content.  See
 * context/audits/catalog-phase3-slice10-voice-retag-2026-05-02.md
 * for the inventory + criteria.
 *
 * Mission briefings: 01.  Civilian / NPC combat barks: 02 (OFFENSIVE).
 * Carrington urgent: 03.  Scripted scene dialogue: 47.  Programmer
 * cluster: 48.  NPC greetings: 60 (RESPONDHELLO).  Death scream: 62
 * (NTSC-1.0+ only).
 */
static s32 s_audioConfigIsVoice(s32 audioconfig_idx)
{
    switch (audioconfig_idx) {
    case AUDIOCONFIG_01:
    case AUDIOCONFIG_02:
    case AUDIOCONFIG_03:
    case AUDIOCONFIG_47:
    case AUDIOCONFIG_48:
    case AUDIOCONFIG_60:
#if VERSION >= VERSION_NTSC_1_0
    case AUDIOCONFIG_62:
#endif
        return 1;
    default:
        return 0;
    }
}
```

The helper is file-static. The seven slot enums are already declared via `enum audioconfig_e` ([snd.c:105-176](../../src/lib/snd.c:105)) which is reachable from the registration TU via the same data path that already references `g_AudioRussMappings` / `g_AudioConfigs` (declared in [src/include/data.h:42-43](../../src/include/data.h:42)).

### D.2 Registration loop modification

```c
/* ---- audio (SFX + voice retag) ---- */
{
    extern struct audiorussmapping g_AudioRussMappings[];
    s32 sfx_n = 0;
    s32 voice_n = 0;
    for (s32 i = 0; i < NUM_BASE_SFX_ENTRIES; i++) {
        snprintf(idbuf, sizeof(idbuf), "base:sfx_%04x", i);
        s32 category = AUDIO_CAT_SFX;
        if (i < (s32)ARRAYCOUNT(g_AudioRussMappings)) {
            if (s_audioConfigIsVoice(g_AudioRussMappings[i].audioconfig_index)) {
                category = AUDIO_CAT_VOICE;
            }
        }
        asset_entry_t *e = assetCatalogRegisterAudio(
            idbuf, i, "", category, 0, "");
        if (!e) {
            sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register audio %s", idbuf);
            continue;
        }
        strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
        e->bundled = 1; e->enabled = 1;
        e->runtime_index = i;
        e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
        if (category == AUDIO_CAT_VOICE) voice_n++;
        else sfx_n++;
    }
    sysLogPrintf(LOG_NOTE,
        "assetcatalog: registered %d base audio entries (%d SFX + %d VOICE)",
        sfx_n + voice_n, sfx_n, voice_n);
    count += sfx_n + voice_n;
}
```

The `assetCatalogRegisterAudio` call signature already accepts the `category` argument; previously it was hardcoded to 0. The added third call argument is the only new value passed.

The new external declaration (`extern struct audiorussmapping g_AudioRussMappings[]`) joins the same data-section declarations that snd.c exports through `data.h`.

ARRAYCOUNT is the standard `sizeof / sizeof[0]` macro already in scope; the russ table size is statically known at compile time.

### D.3 Expected counts

By inspection of the russ-table comments and config-slot usage, the expected split on a clean NTSC-1.0+ build is:

- AUDIOCONFIG_01 entries: 44 voice
- AUDIOCONFIG_02 entries: 44 voice
- AUDIOCONFIG_03 entries: 1 voice
- AUDIOCONFIG_47 entries: 22 voice
- AUDIOCONFIG_48 entries: 5 voice
- AUDIOCONFIG_60 entries: 25 voice
- AUDIOCONFIG_62 entries: 3 voice
- Total: 144 voice

Remaining 1401 SFX entries stay as `AUDIO_CAT_SFX`. Log line should print `1401 SFX + 144 VOICE`.

The exact NTSC-1.0+ count may differ by 1-2 if any russ entry was version-conditional in a way I missed; the post-build `LOG_NOTE` line is the authoritative count and the audit's pinned numbers are the test target.

---

## E. Tests

A static-text grep test pins:
- The retag helper exists in `port/src/assetcatalog_base_extended.c`.
- The registration loop passes `category` to `assetCatalogRegisterAudio` (not hardcoded 0).
- The seven voice configs are all listed in `s_audioConfigIsVoice` switch.
- AUDIOCONFIG_62 is `#if VERSION >= VERSION_NTSC_1_0` guarded.

A runtime-derivable test would compare the registered-voice count against an expected number, but that requires the catalog initialization path which is `pd`-only and not linked into `pd-tests`. Static text pins are the practical surface.

`tests/test_audio_voice_retag.cpp` (new) handles the static pins.

---

## F. Wire / save format implications

Voice category is local catalog metadata. No wire format change. No save format change.

The `category` field is already part of `ext.audio` in the public catalog API ([port/include/assetcatalog.h:364](../../port/include/assetcatalog.h:364)). Existing consumers iterate `assetCatalogIterateByType(ASSET_AUDIO, ...)` and may filter by category at their discretion -- nothing breaks.

---

## G. Cross-cuts

### G.1 Server build

`pd-server` does not link `assetcatalog_base_extended.c` per [CMakeLists.txt:570-695](../../CMakeLists.txt:570) `SRC_SERVER` (only `_base.c` + `_api.c` for catalog). The retag is client-only by construction; server build is unaffected.

The `g_AudioRussMappings` symbol lives in `src/lib/snd.c` which is also not linked into pd-server (audio is client-only). Adding the `extern` declaration in `assetcatalog_base_extended.c` is fine because the file itself is client-only.

### G.2 Mod loading

Mod-supplied audio entries register through `assetcatalog_scanner.c::registerComponent` which calls `assetCatalogRegisterAudio` with the modder-specified category (parsed from the INI's `category` key). Mods can already specify `category=voice` and the scanner maps it to `AUDIO_CAT_VOICE` ([assetcatalog_scanner.c:268-290](../../port/src/assetcatalog_scanner.c:268)). The base retag aligns base-game entries with what mods can already declare.

### G.3 Audio mod manager UI

[port/fast3d/pdgui_menu_audiomod.cpp](../../port/fast3d/pdgui_menu_audiomod.cpp) iterates ASSET_AUDIO + filters by category for SFX vs Music vs Voice tabs. Currently the Voice tab is empty (no base entries match). After retag, ~144 entries surface in the Voice tab, modder voice replacements can target any of them.

### G.4 ARENA_LOADMODE / B-254 invariants and other slices

Slice 10 touches only the SFX registration loop. Stage scene files (Slice 9), prop models (Slice 7), lang banks (Slice 3), weapon models (Slice 1), character models (Slice 4), sound bank extraction (Slice 2/5/6/8), music sequences (Slice 11) are all in different code paths. No conflict.

The Universality Sweep B-303 enabled-filter (`assetCatalogIterateByType` skips `!entry->enabled`) is unaffected: voice entries respect the enabled flag the same way SFX entries do.

---

## H. Phase 2 commit plan (1 commit)

Single commit:

1. Add `s_audioConfigIsVoice` helper file-static in `port/src/assetcatalog_base_extended.c`.
2. Modify the SFX registration loop to consult `g_AudioRussMappings[i]` when `i < ARRAYCOUNT(g_AudioRussMappings)`, passing the category to `assetCatalogRegisterAudio` instead of hardcoded 0.
3. Update the LOG_NOTE summary to include both counts.
4. Add `tests/test_audio_voice_retag.cpp` with the static-text pins.
5. Wire the test into CMakeLists `pd-tests` source list.

---

## I. Stop conditions checked

- AUDIOCONFIG enum slot stability: `enum audioconfig_e` is dense sequential and a single header definition; confirmed at [snd.c:105-176](../../src/lib/snd.c:105). Cross-build determinism guaranteed (NTSC-1.0+ has 63 slots; pre-NTSC-1.0 has 60; CONFIG_62 is gated on the same VERSION macro as the russ entries that use it).
- ARRAYCOUNT alignment: `g_AudioRussMappings[]` is statically sized by initializer count; the macro returns the correct entry count.
- Mod-overlay non-interference: the retag runs in `assetCatalogRegisterBaseGame` BEFORE mod scan; mod-supplied audio entries with their own `category` value are registered separately and override per the existing mod path.
- Wire / save format: no protocol bump, no save format change.
- Server build: `assetcatalog_base_extended.c` is client-only; no server stub needed.

Phase 2 begins immediately.
