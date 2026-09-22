# Regional language source and runtime plan

Status: proposed remaining T-ASSETS-042 work, 2026-09-05. This document is a
design and source-tracing handoff, not a passing extraction or rendering receipt.
The first language unit repairs null/empty preservation, indexed table extent,
strict JSON/count parsing, and equivalent literal UTF-8/escaped Unicode conversion
to the supported native encoding. It does not implement regional selection or
Japanese text and fonts. See the [canonical asset audit](../../audits/2026/asset-source-runtime-contract-2026-09-05.md)
for the umbrella scope and evidence state.

## Observed source corpus and missing consumers

The fresh NTSC extraction under
`.claude/smoke-verify-runs/20260905T232802Z-v006_scenario_transaction_smoke/data/ntsc-final`
contains 476 nonempty raw language files: 68 banks for each of seven source
suffixes. The typed emitter publishes only the 68 canonical English banks.
The retained run log is `.claude/asset0905-evidence/v006-client.log`.
Nonempty raw files include compressed empty tables; availability does not prove
that every translation has text. Empty banks and null rows are source facts.

The authoritative runtime file-name table is populated from the active ROM in
`port/src/romdata.c:romdataInitFiles`, or from its extracted name table when no ROM
is resident. `romdataFileGetName` and `romdataFileGetNumForName` expose that map.
`romExtractRelPathForFilenum` resolves its extracted file path. Files found by a
directory glob, source-tree translations, and adjacent enum values are not
substitutes for that active-ROM identity map.

| Source suffix in the active name table | Public locale | Proposed ID suffix | Native source encoding |
|---|---|---|---|
| `E` | `en` | `en`, unchanged | Latin-1 |
| `P` | `en-GB` | `en_gb` | Latin-1 |
| `_str_fZ`, or a verified `_str_f` spelling | `fr` | `fr` | Latin-1 |
| `_str_gZ`, or a verified `_str_g` spelling | `de` | `de` | Latin-1 |
| `_str_iZ`, or a verified `_str_i` spelling | `it` | `it` | Latin-1 |
| `_str_sZ`, or a verified `_str_s` spelling | `es` | `es` | Latin-1 |
| `J` | `ja` | `ja` | Packed Japanese glyph encoding for `jpn-final`; Latin-1 otherwise |

`tools/assetmgr/mklang:make_binary` establishes the encoding distinction. Its
`encode_jp` and `jpnchars` mapping define the Japanese codec, including duplicate
glyph variants represented in the original editable input by `\hXXXX` notation.
Japanese is not Shift-JIS or Latin-1. A non-Japanese ROM's `J` file name alone
does not establish either a Japanese codec or a translated Japanese corpus.

## 1. Plan and emit every available variant

Refactor `port/src/romextract_pdlang.c` to build an explicit work list before its
existing boot-pool fanout. Each record carries the bank bridge, canonical bank
name, normalized locale, resolved source file identity/path, and source codec.

Derive canonical bank identity once from `g_LangFiles[bank]` and the matching
canonical English name. Preserve `base:lang_<bank>_en`. Resolve each other suffix
through the authoritative name API; never compute `EnglishFileId + offset`.
If alternative name spellings resolve to different files for the same bank and
locale, report ambiguity rather than selecting by iteration order. Pass the
canonical bank name into `s_emitOneLang`: stripping a selected French/German
file's suffix with the current English-only helper would produce the wrong ID.

Emit the same editable `lang.ini` and complete UTF-8 `strings.json` contract for
each variant, with descriptor/manifest count agreeing even when zero. A missing
source in the authoritative map is unavailable; a mapped source whose extracted
bytes are missing or invalid is a failure. Do not silently classify both as skip.

Produce a receipt with one row per planned available variant: bank, locale,
source name, source hash, catalog ID, table count, and `emitted`, `reused`, or
`failed`, with a reason. Reuse requires the source bytes, source inventory,
codec revision, and exporter contract to match. Changing a regional source must
invalidate its output even if the canonical English bank is unchanged. Summary
archive counts are supplemental to per-table semantic parity.

## 2. Deterministic locale and descriptor resolution

Introduce a small production locale helper, proposed `lang_locale.c/.h`, with
`langLocaleNormalize` and `langLocaleChooseBank`. Default to `en`, retaining the
current English IDs and default behavior. Accept legacy `jp` and `gb` aliases as
`ja` and `en-GB`; normalize tag case and reject malformed/truncated tags.

Choose by locale suitability first: exact requested locale, supported parent
locale, then `en`. Within the same locale prefer enabled nonbundled entries over
bundled entries, then use a documented stable catalog-ID tie break. The current
catalog has no authoritative global mod-load-priority field; do not invent one
from scan order. This makes a French base table beat an English mod when French
is requested, while a French mod overrides a French base table.

Legacy standalone descriptors with no locale mean `en`. For any new explicit
descriptor-parent feature, inherit locale only from a resolved named parent
dependency when the child omits it; an explicit child locale wins. Validate
parent type, bank identity, dependency closure, and cycles before publication.
Do not infer inheritance from folder names or neighboring catalog entries.

The first selection contract uses whole-bank locale fallback. Null strings,
empty strings, and explicitly empty tables retain their meanings and never
trigger implicit per-string inheritance. Authored `fallback_locale` support
must have a defined owner for the locale route, strict cycle validation, and a
real consumer before admission advertises it. A field copied into metadata
without changing selection is not implemented fallback support. Keep that
extension explicit instead of weakening the complete indexed-table contract.

The descriptor changes must agree across `assetcatalog.h`, the ASSET_LANG branch
in `assetcatalog_scanner.c` and `net/netdistrib.c`, `loader_walker_lang.c`, runtime
bindings, Modding Hub authoring, and `tools/asset_archive_conformance.py`.

## 3. Preparation, selection, and transactional reload

Current `assetcatalog_load.c:s_catalogLoadEntryLangPayload` calls
`langManifestEnsureId`, which immediately replaces a live bank with that entry.
Publishing every locale without fixing this would allow generic preload order
to select the displayed language. Catalog residence must not select a locale.

Refactor `langmanifest.c/.h` around these proposed operations:

- `langManifestPrepareEntry(entry, candidate, error)` reads the public source
  and produces an owned candidate without touching live banks or preferences.
- `langManifestPrepareLocale(locale, transaction, error)` resolves and prepares
  the replacements for all live banks and the required font/glyph closure.
- `langManifestCommitLocale(transaction)` publishes the complete candidate set
  and selected identities at one safe main-thread boundary.
- `langManifestAbortLocale(transaction)` releases only newly prepared resources.

Generic catalog activation should validate/retain a per-entry prepared payload.
`langLoad` and logical bank dependency requests bind the selected candidate.
An explicit retain of a different locale remains a retain; changing preference
is an explicit locale-selection operation. Ensure that lifetime ownership of
per-entry prepared data and selected bank references cannot double-free memory.

Reload must resolve the new locale instead of pinning `s_ModLangCatalogIds`.
Snapshot actual live banks as well as tracking identities: direct `langLoad`
currently does not automatically record tracking, and `langClearBank` clears a
pointer without updating the manifest. Centralize successful load/clear tracking.
Remove the PAL reset path's dummy nonnull bank pointers as part of the same
integration rather than passing them to the new live-bank snapshot.

Keep old banks, selected IDs, active locale, and fonts intact until all candidates
are ready. A failure in any bank, dependency, allocation, or glyph resource must
leave the old selection unchanged. On success publish the set, then release old
references after their users are safe. Candidate preparation must not invalidate
old strings or rebuild the live ImGui atlas in place.

Existing catalog rollback only detaches `ASSET_PAYLOAD_RUNTIME_ACTIVE` and leaves
language memory to its owner. Add language participation to activation failure,
typed replacement/retirement, and unload paths in `assetcatalog_load.c`; its
current detach is not a rollback of the previous selected bank. Coordinate with
catalog lifecycle ownership instead of adding a second independent transaction.

## 4. PC preference and UI

Register a PC string preference such as `Game.Locale` using
`configRegisterString`; `config.c` already supports delayed registration and
replaying values loaded from `pd.ini`. Add a picker to
`pdgui_menu_mainmenu.cpp:renderSettingsInterface`, which is also used by the
in-game settings redirect. Display only locales with a usable source/glyph
closure, and distinguish an unavailable translation from a malformed source.

The picker prepares and commits the language transaction before saving config.
On failure it retains the prior selected value and reports the failed source.
Legacy `langSetEuropean` and `langSetJpnEnabled` must delegate to the same path.
Preserve `mpGetTeamsWithDefaultName` / `mpSetTeamNamesToDefault` behavior around
a successful commit so customized team names are not overwritten.

Do not change save-file language bit layouts. If migration is needed, import
the legacy preference only when the PC preference is absent. A later save load
must not silently overwrite an explicit PC selection. The legacy language menu
and setters are region-gated today; their existence does not provide a working
PC locale preference in the current NTSC build.

## 5. UTF-8, native text, and font closure

Correct Latin-1 decoding alone does not make regional strings render correctly.
NTSC `src/game/game_1531a0.c:textRender` treats high bytes as pairs of packed
Japanese codes, whereas non-Japanese source tables contain individual Latin-1
bytes. Measurement, wrapping, and other text drawing paths also contain
region-specific code-unit handling. Enabling French/German tables unchanged
therefore does not establish usable language support.

Keep public text valid UTF-8. Add one PC codepoint/glyph adapter serving native
measurement, wrapping, projected rendering, ordinary rendering, and the ImGui
text boundary. Existing `langGet` consumers also concatenate and format strings;
the design cannot rely solely on recovering a bank from the original pointer.
Use a consistent internal text contract or explicit conversion boundaries, with
legacy controls handled deliberately. Source-derived native/UTF-8 representations
are caches, not separately authored tables. Do not pass Latin-1 bytes directly
to an ImGui UTF-8 API.

The smallest foundation uses the existing editable `.pdfont` PGM atlas plus
JSON metrics, extended with Unicode codepoints and stable named glyph keys.
Export Japanese single/multi glyph banks into that source form and bind
`langGetJpnCharPixels` through the catalog. Its present raw-segment DMA path is
not acceptable steady-state closure. `romextract_pdfont.c` currently excludes
those raw banks, and `fontcatalog.c` requires the build's fixed 94/135-glyph
layout and only accepts PGM imagery.

Japanese export must invert the actual `mklang` mapping. Distinct native glyph
variants representing the same Unicode character cannot be silently collapsed.
Preserve those variants through named glyph keys and explicit source spans or
runs, while the text remains UTF-8. Native packed codes belong in extraction
provenance or rebuildable cache, not as opaque authored runtime payloads or a
proprietary non-JSON escape syntax. This schema and codec must round-trip actual
duplicate-glyph examples before Japanese admission is enabled.

For standard TTF/OTF input, a rasterizer should feed the same codepoint/glyph
adapter through the `.pdfont` provider path. Hash the font bytes, chosen face,
size/rasterization settings, requested glyph set, metrics/variant source, and
compiler revision into any generated atlas cache. The ImGui font manager already
loads vector bytes, but its default glyph range and separate atlas do not serve
native HUD rendering or Japanese coverage. Reuse useful rasterization plumbing;
do not mistake that existing UI-only loader for a complete font implementation.

## Implementable subunits and proof

The work can be divided at source-freeze boundaries, but T-ASSETS-042 remains
partial until the connected chain passes. An emitter-only variant multiplication
or a selector that renders accents/Japanese incorrectly is not completion.

| Subunit | Production surfaces | Smallest meaningful proof |
|---|---|---|
| Variant planning and extraction | `romextract_pdlang.c`, shared locale/name planner, `lang_source` codec | Production planner with reordered/nonadjacent file IDs, absent names, ambiguous aliases; actual emitter produces every mapped variant with stable English IDs and a complete receipt. |
| Source semantic parity | Exporter and strict production parser | For every available native table compare logical count, null bitmap, empty strings, text bytes/codepoints, and Japanese glyph variants after export/reparse; archive counts alone cannot pass. |
| Locale selection and descriptor propagation | Locale helper, scanner, walker, distribution, conformance | Randomized catalog order produces identical choice; exact locale beats fallback; matching mod beats base; omitted legacy locale, aliases, invalid tags, parent cycles, and declared-zero tables behave identically through each entry path. |
| Prepared catalog payloads and atomic reload | `langmanifest`, `assetcatalog_load`, `lang.c`, `langreset.c` | Preload every locale without changing live banks; switch two or more live banks; fail preparation of the second bank/font and verify all old pointers, selected IDs, preference, team names, and ownership survive. |
| Public source edits and lifetime | Same loader/transaction paths | Edit one selected `strings.json`, reload through production, observe changed text; edit an unselected locale and prove it does not bind; retire/replace selected entries without dangling banks or leaked references. |
| Glyph source and Unicode consumers | `romextract_pdfont`, `fontcatalog`, `game_1531a0.c`, `langGetJpnCharPixels`, ImGui bridge | Actual atlas/vector source changes alter glyph pixels/metrics; accents, Japanese single/multi glyphs, duplicate variants, multiline measurement/wrapping, and native/ImGui drawing agree. No raw ROM/segment fallback. |
| Preference and ordinary client use | PC config/settings and legacy setter bridges | Restart retains locale, failed switch retains old preference, save load does not overwrite explicit PC choice; ordinary client shows menus, HUD, subtitles, and default team names after successful change. |

Use production helpers and real catalog/provider operations in focused tests,
not static token assertions or a mirrored selector/exporter. Full extraction
proof must retain the source inventory and per-table parity artifacts. Functional
test, extraction, graphical capture, and human visual verdicts remain separate
evidence classes. Execute through the existing coordinated queues after a
coherent source freeze; this design document itself runs no gates.
