/**
 * romextract_pd.h -- Catalog universality pivot Step 1 (2026-05-02).
 *
 * Per-asset .pd* compound emitters. Walks the loader_pool pool
 * (populated by loaderWalkerLoadAll from the per-asset envelope) and writes
 * one .pdwpn JSON file per weapon, one .pdmesh ZIP compound per unique
 * weapon mesh, and one .pdanim JSON file per registered animation.
 *
 * Output paths under data/<romid>/:
 *   weapons/<id>.pdwpn       JSON metadata document
 *   meshes/<id>.pdmesh       ZIP compound (manifest.json + geometry.bin)
 *   animations/<id>.pdanim   JSON metadata document
 *
 * Where <id> is the catalog ID with the colon replaced by underscore.
 * For example "base:falcon2" -> "base_falcon2.pdwpn".
 *
 * Boot order requirement: must run AFTER loaderPoolFinalize
 * (so loader pools are populated) AND AFTER romExtractAllFiles (so the
 * source .bin files exist on disk for .pdmesh repackaging).  See
 * port/src/main.c wiring near the loaderWalkerLoadAll block.
 *
 * Server build: each function returns 0 immediately (no client weapon
 * data on the server side; loader is not active).
 *
 * Companion docs:
 *   context/designs/catalog/universality-pivot-schemas.md   schemas
 *   context/audits/catalog-universality-pivot-plan-2026-05-02.md  plan
 */
#ifndef _IN_ROMEXTRACT_PD_H
#define _IN_ROMEXTRACT_PD_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Emit one .pdwpn JSON file for every registered weapon (86 expected).
 * Idempotent: skips files that already exist with non-zero size unless
 * force_rewrite is non-zero.  Per-file failures emit LOUDFAIL.EXTRACT.PDWPN
 * but do not abort the walk.
 *
 * Returns: count of files newly written; -1 on infrastructure failure
 * (data dir creation, etc.).
 */
s32 romExtractAllPdwpn(s32 force_rewrite);

/**
 * Emit one .pdmesh ZIP compound for every unique weapon mesh referenced
 * by hi_model / lo_model fields.  Reads byte payload from the existing
 * data/<romid>/files/<sanitized_rom_name>.bin produced by Pass A.2 of
 * the romextract pipeline; this function does NOT touch g_RomFile (which
 * is freed by romdataReleaseRom before the loader runs).
 *
 * Returns: count of compounds newly written; -1 on infrastructure failure.
 */
s32 romExtractAllPdmesh(s32 force_rewrite);

/**
 * Emit one .pdanim JSON file per registered animation in the loader
 * pool.  Each file carries category="weapon_animation" per
 * universality-pivot-schemas.md Section 2.6.
 *
 * Returns: count of files newly written; -1 on infrastructure failure.
 */
s32 romExtractAllPdanim(s32 force_rewrite);

/* ============================================================
 * Catalog universality pivot Step 2 (2026-05-03).
 *
 * Per-asset emitters for the metadata-class kinds beyond weapons:
 *   .pdhead     JSON metadata document (one per registered head)
 *   .pdbody     JSON metadata document (one per registered body)
 *   .pdarena    JSON metadata document (one per registered arena)
 *   .pdscenario ZIP compound (one per arena's playable stage,
 *                              UNIFIED per Q-1 -- bg + tiles + pads
 *                              + setup + mpsetup + manifest in one
 *                              ZIP)
 *
 * Output paths under data/<romid>/:
 *   heads/<id>.pdhead
 *   bodies/<id>.pdbody
 *   arenas/<id>.pdarena
 *   scenarios/<id>.pdscenario
 *
 * Where <id> is the catalog ID with the colon replaced by underscore.
 * For example "base:head_carrington" -> "base_head_carrington.pdhead".
 *
 * Boot order requirement: must run AFTER loaderWalkerLoadAll +
 * loaderPoolFinalize / BuildBodyManager / BuildArenaManager
 * (so the typed pools are populated) AND AFTER stageTableInit (so
 * g_Stages[] is populated for the .pdscenario emitter to read per-stage
 * file IDs from). Wire alongside Step 1 in port/src/main.c.
 *
 * Server build: each function returns 0 immediately (no character/arena
 * data on the server side; loader is not active).
 * ============================================================ */

/* Step 2: emit one .pdhead JSON file per registered head (152 slots,
 * ~30-40 standalone heads expected based on per-asset envelope contents).
 * Idempotent: skips files that already exist with non-zero size unless
 * force_rewrite is non-zero. Per-file failures emit
 * LOUDFAIL.EXTRACT.PDHEAD but do not abort the walk.
 *
 * Returns: count of files newly written; -1 on infrastructure failure
 * (data dir creation, etc.). */
s32 romExtractAllPdhead(s32 force_rewrite);

/* Step 2: emit one .pdbody JSON file per registered body (152 slots,
 * 68 expected from the in-binary baseline: 63 named bodies + 5 SP fallbacks).
 * Idempotent. Returns: count of files newly written; -1 on
 * infrastructure failure. */
s32 romExtractAllPdbody(s32 force_rewrite);

/* Step 2: emit one .pdarena JSON metadata file per registered arena
 * AND one .pdscenario ZIP compound per arena's playable stage.
 *
 * The .pdarena (Section 2.4) carries arena_index / slug / category /
 * stagenum / requirefeature / name_langid / load_mode and a `scenario`
 * catalog ID reference. The .pdscenario (Section 2.10) is the UNIFIED
 * ZIP per Q-1 carrying geometry / tiles / pads / setup / mpsetup
 * binaries plus a manifest.json.
 *
 * 47 arenas total (CATALOG_MGR_ARENA_COUNT). The CANVAS-mode arenas
 * (Solo Missions group) carry a stagenum of 0 and have no playable
 * scene files; for those the .pdarena still emits but the
 * .pdscenario is skipped (the .pdarena's `scenario` field is null).
 *
 * Idempotent. Returns: count of arena files newly written
 * (.pdarena + .pdscenario both count); -1 on infrastructure failure. */
s32 romExtractAllPdarena(s32 force_rewrite);

/* ============================================================
 * Catalog universality pivot Step 3a (2026-05-03).
 *
 * Character-animation emitter. Companion to romExtractAllPdanim
 * (Step 1, weapon-animation gunscript opcodes). Walks the
 * chr-animation lump exposed via _animationsTableRomStart /
 * _animationsTableRomEnd (post-preprocessAnimations, byte-swapped
 * native-endian) and emits one .pdanim ZIP compound per registered
 * chr animation at data/<romid>/animations/<id>.pdanim.
 *
 * Compound layout per universality-pivot-schemas.md Section 2.6:
 *   manifest.json     envelope + animation metadata + provenance
 *   frames.bin        contiguous header + frame bytes
 *                     (length = headerlen + numframes * bytesperframe)
 *   frames.bin.sha256 outer-file SHA-256 sidecar
 *
 * Catalog IDs derive from the loaderEnumNameForAnimEnum reverse
 * lookup over k_AnimEnum (port/src/loader_enum_reverse.c). For named
 * animations (e.g. "ANIM_HEROHIT") the ID is "base:anim_herohit". For
 * unnamed slots (auto-named "ANIM_NNNN" hex) the ID is "base:anim_NNNN".
 *
 * Boot order requirement: must run AFTER romdataInit (which calls
 * preprocessAnimations and remaps _animationsTableRomStart/End to the
 * disk-migrated buffer per romdata.c::romdataReleaseRom, OR points
 * them into g_RomFile pre-Pass-C). EITHER pointer state is fine here:
 * the table bytes are byte-swapped at preprocess time before this
 * emitter sees them.
 *
 * Server build: returns 0 immediately (no segments populated; ROM not
 * loaded; nothing to extract).
 *
 * Per Mike's Q-3 ruling (2026-05-02): "DO NOT DEFER beyond the scope
 * of the catalog work. Catalog is not complete unless it is COMPLETE."
 * Step 3a closes that ruling -- the chr-animation lump becomes
 * per-asset .pdanim files alongside the Step 1 weapon-animation files.
 * ============================================================ */

/* Step 3a: emit one .pdanim ZIP compound per chr animation entry in
 * the segs/animations.bin lump.  Idempotent: skips files that already
 * exist with non-zero size unless force_rewrite is non-zero.  Per-file
 * failures emit LOUDFAIL.EXTRACT.PDANIM_CHR but do not abort the walk.
 *
 * Returns: count of compounds newly written; -1 on infrastructure
 * failure (data dir creation, segment lookup, etc.). */
s32 romExtractAllPdanimChr(s32 force_rewrite);

/* ============================================================
 * Catalog universality pivot Step 3 audio half (2026-05-03).
 *
 * Per-asset emitters for the byte-payload audio classes:
 *   .pdsfx     ZIP compound (one per leaf SFX in the SFX bank that is
 *              NOT classified as voice via the slot-7 predicate from
 *              Slice 10)
 *   .pdvoice   ZIP compound (one per leaf SFX classified as voice via
 *              g_AudioRussMappings + s_audioConfigIsVoice predicate;
 *              same audio bytes as a .pdsfx, distinct kind so modders
 *              can target voice content separately per Q-2)
 *   .pdsong    ZIP compound (one per sequencer entry in the sequences
 *              segment seqtable)
 *
 * Output paths under data/<romid>/:
 *   audio/sfx/<id>.pdsfx
 *   audio/voice/<id>.pdvoice
 *   audio/music/<id>.pdsong
 *
 * Catalog ID format follows feedback_human_readable_ids:
 *   .pdsfx    base:sfx_<lowered_symbol>  (e.g. base:sfx_launch_rocket)
 *             OR  base:sfx_<NNNN>        (4-digit hex, Q-4 Bucket 2)
 *   .pdvoice  base:voice_<NNNN>          (always hex; symbolic names
 *             are SFX-shaped so curation later can rename per actor)
 *   .pdsong   base:song_<NNNN>           (4-digit hex; curation maps
 *             to track_dark_combat etc. in a follow-up pass)
 *
 * Boot order requirement: must run AFTER romdataInit (segments
 * populated + preprocessSegments byte-swapped the audio bank +
 * sequence table headers AND remapped them to the disk-migrated
 * buffers) AND AFTER romExtractAllSegments (so the source segs
 * files exist on disk). Wire alongside Step 3a in port/src/main.c.
 *
 * Server build: each function returns 0 immediately (no audio
 * segments populated; no russ-mapping linkage; nothing to extract).
 *
 * Per Mike's Q-3 ruling: "Catalog is not complete unless it is
 * COMPLETE. IT IS FOUNDATIONAL TO EVERYTHING." Step 3 audio half
 * (sfx + voice + song) closes the audio side of that ruling. The
 * remaining Step 3 classes (.pdui, .pdfont, .pdlang, .pdscenario)
 * ship as Step 3b in a follow-up worktree.
 *
 * Per Mike's Q-2 (audio type-tolerance): a weapon's shootsound
 * field accepts a .pdvoice ID just as readily as a .pdsfx one.
 * Misclassification at extract time is recoverable -- the audio
 * playback layer reads pd_kind at resolve time and routes to the
 * right decoder. Voice classification here is the slot-predicate
 * heuristic from Slice 10 (audioconfig 1/2/3/47/48/60/62 = voice).
 * ============================================================ */

/* Step 3 audio: emit one .pdsfx ZIP compound per leaf SFX entry that
 * is NOT classified as voice. Walks the post-preprocess ALBankFile
 * via the disk-migrated sfxctl segment (instrument 0's soundArray
 * iteration). Sample bytes are sliced from the disk-migrated sfxtbl
 * segment per ALSound's ALWaveTable.base / .len fields.
 *
 * Idempotent: skips files that already exist with non-zero size unless
 * force_rewrite is non-zero. Per-file failures emit
 * LOUDFAIL.EXTRACT.PDSFX but do not abort the walk.
 *
 * Returns: count of files newly written; -1 on infrastructure failure
 * (segment lookup, data dir creation, etc.). */
s32 romExtractAllPdsfx(s32 force_rewrite);

/* Step 3 audio: emit one .pdvoice ZIP compound per leaf SFX entry
 * that IS classified as voice via g_AudioRussMappings +
 * s_audioConfigIsVoice (slots 1/2/3/47/48/60/62 per Slice 10).
 *
 * Same ALBankFile walker as romExtractAllPdsfx but with the inverse
 * filter; manifest carries actor="unknown" pending a curation pass
 * (Step 5 cleanup) that maps voice slots to actor names.
 *
 * Returns: count of files newly written; -1 on infrastructure failure. */
s32 romExtractAllPdvoice(s32 force_rewrite);

/* Step 3 audio: emit one .pdsong ZIP compound per entry in the
 * sequences-segment seqtable. Walks the byte-swapped struct seqtable
 * at the head of the disk-migrated sequences segment; sequence bytes
 * are sliced from segment offset entry.romaddr for entry.binlen
 * bytes.
 *
 * Returns: count of files newly written; -1 on infrastructure failure. */
s32 romExtractAllPdsong(s32 force_rewrite);

/* Voice + song parity checks (byte-level structural integrity). The
 * sfx parity check retired with the loader_pool migration; voice +
 * song are byte-stream-only and their parity is preserved. */
s32 romExtractParityCheckPdvoice(void);
s32 romExtractParityCheckPdsong(void);

/* ============================================================
 * Catalog universality pivot Step 3b part 1 (2026-05-03).
 *
 * Per-asset emitters for the raw-payload classes that wrap files
 * already on disk under data/<romid>/{segs,files}/:
 *   .pdfont  ZIP compound (one per font face/size segment, 10
 *            segments NTSC -- bankgothic / zurich / tahoma /
 *            numeric / handelgothic{xs,sm,md,lg} / ocra{md,lg};
 *            JPN adds fontjpn / fontjpnsingle)
 *   .pdlang  ZIP compound (one per language string-table bank
 *            from the g_LangFiles[] table. NTSC English-only ship
 *            emits 68 entries; PAL/JPN locale extension folds in
 *            as a Step 5 cleanup or follow-up worktree)
 *
 * Output paths under data/<romid>/:
 *   fonts/<id>.pdfont
 *   lang/<id>.pdlang
 *
 * Catalog ID format follows feedback_human_readable_ids:
 *   .pdfont  base:font_<facename>      (e.g. base:font_handelgothicsm)
 *   .pdlang  base:lang_<bank>_<locale> (e.g. base:lang_gun_en)
 *
 * Step 3b part 2 (.pdui + pdguiThemeExtractRomTextures rewrite +
 * pdguiThemeLateInit reader migration) is sized as its own coherent
 * unit because it cross-cuts the GL render path; ships in a
 * follow-up worktree.
 *
 * Boot order requirement: must run AFTER romdataInit + after
 * romExtractAllFiles + romExtractAllSegments (the source bytes
 * must exist on disk for the wrapper to read).
 *
 * Server build: each function returns 0 immediately. Fonts are
 * not used server-side; lang banks are not loaded server-side.
 * ============================================================ */

/* Step 3b part 1: emit one .pdfont ZIP per font segment. Reads raw
 * bytes from the disk-migrated data/<romid>/segs/<face>.bin (Pass A)
 * and wraps them with manifest envelope + provenance. The Step 4
 * loader runs preprocessFont on the raw bytes at load time; this
 * emitter does not run preprocess so the .pdfont byte payload is
 * the same as what's already on disk under segs/.
 *
 * Idempotent: skips files that already exist with non-zero size
 * unless force_rewrite is non-zero. Per-file failures emit
 * LOUDFAIL.EXTRACT.PDFONT but do not abort the walk.
 *
 * Returns: count of files newly written; -1 on infrastructure
 * failure (data dir creation, etc.). */
s32 romExtractAllPdfont(s32 force_rewrite);

/* Step 3b part 1: emit one .pdlang ZIP per language string-table
 * bank. Walks g_LangFiles[1..68], reads raw bytes from
 * data/<romid>/files/<sanitized>.bin (Pass A), and wraps with
 * manifest envelope. The Step 4 loader runs preprocessLangFile at
 * load time. Catalog ID is base:lang_<bank_name>_en for NTSC; PAL
 * locale extension is a follow-up.
 *
 * Returns: count of files newly written; -1 on infrastructure
 * failure. */
s32 romExtractAllPdlang(s32 force_rewrite);

/* Font parity check (byte-level structural integrity). The lang parity
 * check retired with the loader_pool migration; font remains as the
 * byte-stream-only check. */
s32 romExtractParityCheckPdfont(void);

/* ============================================================
 * Catalog universality pivot Step 3b part 2 (2026-05-03).
 *
 * Per-asset UI texture emitter. Closes the Step 3 / 3a / 3b series
 * at 13 of 13 universality kinds (weapon, mesh, animation, head,
 * body, arena, scenario, sfx, voice, song, font, lang, ui).
 *
 *   .pdui  ZIP compound (one per UI texture in the canonical
 *          k_PduiEntries[] table inside port/fast3d/pdgui_theme.cpp;
 *          14 textures expected for the base game)
 *
 * Output paths under data/<romid>/:
 *   ui/<slug>.pdui
 *
 * Where <slug> matches the catalog ID minus the "base:" prefix
 * (e.g. base:ui_bg_haze -> ui_bg_haze.pdui). Each ZIP contains:
 *   manifest.json        envelope (pd_kind="ui", texture_count=1,
 *                        baked-in nineslice insets, source_index)
 *   texture.tga          uncompressed 32-bit RGBA top-down TGA
 *   texture.tga.sha256   outer-file SHA-256 sidecar
 *
 * Cross-cut from Step 3b part 1: the .pdui pipeline depends on
 * g_TexGeneralConfigs (populated by texInit/texReset in pdmain.c
 * mainInit). On the boot main.c block this typically runs before
 * texInit, so pdguiThemeEmitPduiZips returns 0 cleanly when the
 * texture system is not yet ready. The actual emit fires from the
 * render-loop fallback trigger inside pdguiThemeCheckExtract once
 * GL is up. Subsequent boots find the .pdui files already on disk
 * and the call is an idempotent skip.
 *
 * The reader migration in pdguiThemeLateInit consumes these via
 * modArchiveOpen + modArchiveExtractAlloc + s_loadTgaFromMem. The
 * legacy loose-files writers (s_writeTga / s_writePng /
 * s_writeNinesliceJson) become dead code retired in Step 5.
 *
 * Server build: returns 0 immediately. Server has no GL context,
 * no texture system, no UI rendering.
 *
 * Per universality-pivot-schemas.md Section 2.11.
 * Per audits/catalog-universality-pivot-plan-2026-05-02.md Step 3b
 * part 2.
 * ============================================================ */

/* Step 3b part 2: emit one .pdui ZIP per canonical UI texture entry.
 * Reads from g_TexGeneralConfigs[idx] (populated by texInit), decodes
 * RGBA32, encodes a top-down 32-bit TGA in memory via s_writeTgaToMem,
 * and writes a ZIP at data/<romid>/ui/<slug>.pdui via modArchive.
 *
 * Idempotent: skips files that already exist with non-zero size unless
 * force_rewrite is non-zero. Per-file failures emit
 * LOUDFAIL.EXTRACT.PDUI but do not abort the walk.
 *
 * Returns: count of files newly written. Returns 0 (not -1) when the
 * texture system is not yet ready (g_TexGeneralConfigs == NULL); this
 * is the normal state at the boot main.c block, and the actual emit
 * fires later from pdguiThemeCheckExtract in the render-loop fallback
 * trigger. -1 reserved for infrastructure failure (data dir creation). */
s32 romExtractAllPdui(s32 force_rewrite);

/* Step 3b part 2 parity check (Q-5 ruling). Re-walks the canonical
 * 14-texture list, opens each .pdui ZIP, parses manifest.json, and
 * verifies envelope + id + texture_count + source_index round-trip the
 * source descriptor. Missing files are treated as skip (not failure)
 * because the .pdui emitter is deferred to the render-loop on first
 * launch and parity at the boot main.c block runs before .pdui files
 * exist. Failures emit LOADER.UNIVERSAL.PARITY_FAIL with diagnostic
 * detail. Same structural integrity contract as the audio half + part
 * 1; full field-by-field round-trip arrives at Step 4 with the
 * universal loader. */
s32 romExtractParityCheckPdui(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ROMEXTRACT_PD_H */
