# In-game creators: weapon, character, vehicle, level (week of 2026-11-15)

Status: in flight on branch `week-pd/mods`. Owner: MODS week session. Live
coordination is in `context/week-pd-coordination.md` in the canonical checkout.

Goal (Mike): custom mods (a weapon, a character, a vehicle, a level) that can
be created, edited, validated, packaged and loaded from tools inside the game.

"Read" below means read from source at dev `a8ab67f3`. Nothing in this note
has been proven by running the game unless it says so.

## 1. What exists (read 2026-11-14)

- Modding Hub (`port/fast3d/pdgui_menu_moddinghub.cpp`) has 10 tools: Mod
  Manager, INI Editor, Scale Tool, Mod Pack, Audio Mods, Skin Editor, Map
  Import, Menu Style, Font Mod, Weapons. The Grid (Forge) and the Theme Editor
  live elsewhere. `asset_mod_utility_contract.c` names hub tools for
  Characters, Scenarios and Props that do not exist.
- Weapon is the only create, save, enable, select path
  (`weaponToolSaveCustom`, hub about 2892-3316).
- Character: no creator. Skin Editor saves `texture.tga` and `skin.ini` with no
  `mod.json`.
- Vehicle: no creator, no placement. Runtime finds `.pdvehicle` physics by the
  placed object's modelnum (`asset_runtime.c` `assetRuntimeVehicleForModelnum`,
  `propobj.c` hoverbike tick), and scanned mod vehicles keep
  `runtime_index = -1`, so a custom vehicle is never used.
- Level: The Grid saves `mods/Forge Maps/<slug>/map.json`, read back only by
  the editor. Map Import stages `mods/imported_<name>/` without rescan or
  enable and writes no arena name. Bot navigation is not generated anywhere.
- Mesh import (general generated-model importer): positions, indices, room
  tags and flat base colors only. No UVs, textures or skinning.

## 2. Weapon creator defects (read, not yet run)

Status 2026-11-14 (c7d712d5, 59ee0fc6): 1, 2, 3 and 6 are fixed and proven by
`weapon_tool_create_smoke` (26/26); see plan step 5. 4 is open (the seed
buttons still write those defaults; a graph that does not compile is now
refused at save). 5 is open as B-1149.

1. Old layout. It writes `schema = pd.weapon.v1`,
   `behavior_graph = behavior.graph.json`, `nested_payloads = nested_payloads.json`
   and those two root members. `tools/asset_archive_conformance.py` rejects
   that (`FORBIDDEN_WEAPON_EXACT`, the `behavior_graph` key check, required
   members `behavior/primary.graph.json`, `behavior/secondary.graph.json`,
   `behavior/settings.json`, `behavior/variables.json`,
   `behavior/shared-context.json`).
2. Template loading reads only `behavior.graph.json` and `nested_payloads.json`
   (hub about 2103-2112). Clean base archives have neither, so a template
   starts from the default graph.
3. Template payloads are copied verbatim, keeping `base:` catalog IDs. The
   scanner requires every embedded typed archive to share the weapon's
   namespace (`assetcatalog_scanner.c` about 3982-3988,
   `weapon_graph_archive.c` about 902-910) and a mod weapon only activates with
   an embedded, same-namespace `.pdmesh` as `model_file`. Inferred: a weapon
   saved from a clean base template is disabled at scan.
4. Editor defaults that fail the catalog-reference rule at compile
   (`"soundnum":0`, `"MODEL_rocket"`, `user:*` placeholders,
   `reticle_ref:"default"` on a function node).
5. `posx/posy/posz/muzzlez` are never written, so the adapter places the gun
   at the origin (`loader_pool.c` about 410-413). Inferred.
6. No archive validation before enable; only the graph JSON is checked.

## 3. Clean `.pdweapon` contract (summary of the validator and runtime)

- Required members: `weapon.ini`, the five `behavior/` files,
  `_meta/manifest.json`.
- `weapon.ini` must contain these exact lines:
  `primary_graph = behavior/primary.graph.json`,
  `secondary_graph = behavior/secondary.graph.json`,
  `settings_file = behavior/settings.json`,
  `variables_file = behavior/variables.json`,
  `shared_context_file = behavior/shared-context.json`. The text
  `behavior_graph` must not appear. `_meta/manifest.json` mirrors them and
  `model_file`.
- Optional: `bindings/{animations,audio,presentation}.json`, and typed
  dependencies under `dependencies/assets/{models,animations,audio,ui,effects,projectiles,entities}/`.
  No texture slot at weapon level; materials and textures belong in the mesh.
- Runtime composition (`composeWeaponGraphFromAuthoringFiles`,
  `weapon_graph_runtime.c` about 1951-2058): concatenates `nodes` and `exports`
  (both must be non-empty in both files) and `edges`, plus `contexts` from
  `shared-context.json`. Node ids and export names must be unique across both
  files. Used only when `weapon.ini` has no `behavior_graph` key.
- A mode with no behavior: a single `event.trigger_pressed` node with an
  export pointing at it (inferred to leave the function slot empty, which the
  adapter accepts). The base extractor's `function.empty` form is rejected for
  mod weapons by the public adapter (inferred).
- `settings.json` (`pd.weapon_settings.v1`): only `fire_cadence`, `spread`,
  `zoom_fov`, `sight`. `presentation.json`: `crosshair`, `sight`, `zoom_fov`.
- Runtime release policy (`asset_archive_policy.c` about 921-975): if the
  manifest lists dependency archives, it needs a `dependency_schema` and each
  record needs role, type, id, archive, required, version, sha256 and a
  fallback object.
- v1 graphs are parameter sheets: values are read, topology is not executed.
  v2 (`held_single_shot.v1`) executes but is offline single-shot only and
  cannot satisfy both its ingress and the validator; CORE owns that ruling.
  Mods stay on v1 until CORE says otherwise.

## 4. Plan

Foundation, in order. Each step lands with tests that run in CI.

1. DONE (694fe9ff): in-game `mod.json` admission in both `.pdmod` packers,
   shared verdict table with the offline validator.
2. DONE (`port/src/asset_archive_clone.c`, `assetArchiveCloneFile`), verified
   2026-11-14 by unit tests on the example weapon, character and vehicle and
   on the Needler dev mod: each clone passes the C release policy and
   `tools/asset_archive_conformance.py` (21 archives, 12 families). Not yet
   proven on a base-extracted template or in a game run. Known limits: ids
   inside binary `.glb` members cannot be rewritten and make the clone fail;
   scenario archives are untested.
   Original scope: copy a typed archive to a new catalog ID,
   re-identify nested typed archives into the new namespace with derived ids,
   rewrite id references in public INI and JSON members, regenerate `_meta`
   sidecars, validate the result in release mode. Every creator needs this
   (weapon template, character body and head, vehicle, scenario).
2b. DONE (`assetArchiveRebuildFile`, same file): the clone takes member
   overrides (replace, add, remove; `outer.pdxxx::member` for embedded
   archives; a typed override is a whole replacement archive) and can
   rebuild an archive under its own id as an edit. Verified 2026-11-14 the
   same way (8 cases; 6 authored archives pass conformance, 27 checked).
   This is the one authoring primitive: template in, edited mod archive out.
2c. DONE (`port/src/mod_author.c`, `modAuthorSave` / `modAuthorPlan`),
   verified 2026-11-14 by unit tests (9 cases; full pd-tests 1874 cases pass
   locally) and by the offline validator on what the tests author. A request
   (template archive, mods root, display name, optional author, version,
   description, overrides) becomes `mods/<Category>/<kind>_<slug>/` holding
   `<kind>_<slug>.pd<kind>` (catalog id `mod:<kind>_<slug>`) and a `mod.json`
   (`mod.<kind>_<slug>`) the packers' gate accepts. Covered: weapon,
   character, body, head, vehicle. Arena is in the table but untested.
   - The display name is written into the descriptor (`name`, or
     `display_name` for character, body, head), in the first section only,
     keeping the file's line endings. The strict body/head and character
     readers and both release validators accept the results.
   - Saving again under the same name updates in place. A folder whose
     `mod.json` has another id, cannot be read strictly, or is missing beside
     an existing archive is refused untouched.
   - The archive is staged as `<name>.pd<kind>.candidate`, which the scanner
     does not pick up, validated as the archive it will become by the file
     validator and the in-memory one the scanner loads with, then swapped in.
     `mod.json` is staged and swapped the same way, archive first.
   - Limits follow the loaders: display name 1 to 63 bytes of UTF-8 (catalog
     `display_name[64]`), author 63, version 31, description 255 (modmgr).
     The slug is capped at 48 so every embedded archive can still take a
     derived id (`ASSET_ARCHIVE_CLONE_ROOT_ID_MAX`); longer names are
     shortened with a hash.
   - The in-game folder packer packs the authored folder as it stands.
   - Base templates, local run 2026-11-14 against the shared extraction
     (`[base-sweep]` case, `PD2_BASE_TEMPLATE_DIR`, 17 seconds): every base
     weapon (86), character (63), body (68), head (84) and the hoverbike
     saves as a release-valid mod. Arenas: 6 of 47; the other 41 are not
     release-valid as extracted (B-1145, `collision.obj` names a
     `scenario.mtl` that is not in the archive), so no base arena can be a
     template until that is fixed. Base scenarios carry full navigation
     data, so a level made from a base arena keeps bot navigation.
   - Not covered: any game run; what the mod manager shows (see B-1141).
   Known gap (B-1142): the author guarantees release validation only, and
   release validation does not read member content. Step 4 closes it.
3. DONE for save and pack (`port/src/mod_asset_validate.cpp`,
   `modAssetValidateArchiveBytes` / `File`), verified 2026-11-14 by unit tests
   (5 cases; full pd-tests 1884 cases pass) and by the base sweep (unchanged,
   308 of 349). Release validation first, then every public `.json`, `.gltf`
   and `.ini` member read strictly in the archive and in every embedded
   archive, then the family's own reader for bodies, heads and characters.
   `modAuthorSave` runs it on the staged archive; both `.pdmod` packers run it
   on every typed archive, before the descriptor reaches the shared INI
   parser.
   Extended 2026-11-14 (59ee0fc6): every `.pdweapon`, `.pdprojectile` and
   `.pdentity` in the tree is read by the compiler the game loads it with
   (v2 weapons through `wgV2ArchiveInspect`). `modAssetValidateArchiveStaged`
   judges a file on disk under the name it will have and also compiles a v1
   weapon with its variables file; `modAuthorSave` uses it on the staged
   candidate. Not yet: a content reader for vehicles, and the enable path.
4. DONE: recipe files (`port/src/mod_author_recipe.cpp`), the game-side save,
   rescan, enable and apply (`port/src/mod_author_runtime.c`) and the launch
   flag `--mod-author <recipe.json>`. RUN 2026-11-14 with this session's
   client on the shared base: `mod_author_weapon_create_smoke` PASS 22/22
   (one launch creates a weapon mod from `base:falcon2`, enables it and loads
   it through the typed catalog loader). `mod_author_weapon_play_smoke` is a
   known-red reproduction (20/21): the created weapon spawns in the player's
   hand but has no view position, no name and does not fire, because a base
   weapon archive carries none of what the mod weapon adapter needs for
   those.
5. PARTLY DONE (cc8176e7, c7d712d5, f740bfee). The hub's Weapons creator
   saves through `modAuthorRuntimeSave` in the clean layout and writes no
   archive itself.
   - `port/src/weapon_graph_editor_io.cpp`: the editor's one graph document
     to and from `behavior/primary.graph.json`, `secondary.graph.json` and
     `shared-context.json`, text to text. A mode with no function gets a
     `function.empty` node (the base game's own form); shared nodes stay
     shared; every link is kept; numbers are written in their shortest
     exact form.
   - Opening a template composes its three files into the editor. A
     template whose behavior cannot be read or shown is refused.
   - Save: split, compile with the game's compiler, overrides for the three
     files, `dual_wieldable` in the descriptor, an imported typed `.pdmesh`
     as a whole-archive override of the template's model member.
   - Refused by name for now: a different catalog model, texture,
     animation, sound, projectile or entity. The template's own assets are
     kept. Swapping an existing dependency slot is the next step here;
     adding a slot the template does not have also needs the archive's
     `_meta/manifest.json` mirror updated, which the rebuild cannot do yet.
   - The catalog name shown before saving is `modAuthorNames`, the author's
     own answer.
   - `--debug-weapon-tool-create <weapon id>` with
     `--debug-weapon-tool-name <name>` runs the creator's own template
     start and save without the mouse.
   - RUN 2026-11-14 on shared base 3 at 40552210:
     `weapon_tool_create_smoke` PASS 26/26 (the Falcon 2's own graph reaches
     the editor: 4 nodes, 2 links; saved, enabled, typed load OK).
   - Not covered: clicking through the hub's windows; settings, variables
     and presentation editing; the view position (B-1149); a template whose
     graph uses `$variables` (none exists; the creator's own check compiles
     without the variables file).
6. Characters tool, vehicle runtime binding plus Vehicles tool, level tools.
   Level tools and the level mod wait until Forge mode authoring is
   finished (Mike, 2026-11-15); the vehicle, the veteran and the crystal
   pistol go first (`maian-vehicle-and-veteran.md`).
   Not started. All 47 base arenas, 68 bodies, 84 heads, 63 characters and
   the hoverbike now save as release-valid mods through `modAuthorSave`
   (shared base 3), so each of these tools starts from a working save.
7. The weapon mod itself: `dev-mods/maian_crystal_pistol/` (3c31a844), a
   recipe the mod author builds. First game run 2026-11-15: PASS 22/22,
   builds, loads, equips and fires both functions without error; the gun
   and the crystals are not visible yet. Status and next steps are in
   `maian-crystal-pistol-handoff.md` section 9.
8. B-1149 (view position of a mod made from a base gun) is fixed in the
   author (8f0ac3a3) and seen in the game on 2026-11-15: the Falcon copy is
   held at the lower right. `mod_author_weapon_play_smoke` stays red, 20/21,
   for B-1151 only, until mod weapons have ammo. New from that run: B-1154,
   the copy's surface looks striped.

9. Mods must survive a client rebuild and a cache rebuild (Mike, 2026-11-15).
   READ 2026-11-15:
   - `devtools/build-headless.ps1` removed the whole build directory on a
     clean (explicit `-Clean` or the smart-clean heuristic) and copied back
     only the git-tracked dev mods. A mod made with the in-game tools in
     `$E/mods`, and `mods-enabled.json` in a portable build, went with it.
     FIXED in the wrapper (36d4e5bb): both are moved beside the build
     directory for the wipe and moved back; a keep folder left by an
     interrupted clean stops the build instead of being written over.
     Verified on scratch folders (13 checks) and RUN 2026-11-15 06:21 UTC:
     a marker mod put in `.claude/session-builds/mods/mods/` came back
     byte for byte after `build-session.ps1 -Session mods -Target client
     -Clean` (258 s; the log shows "Kept aside for the clean" and "Mods
     put back into"), with no `.mods-keep` folder left behind.
   - Base asset extraction and its regeneration remove single stale files
     only (`romextract*.c`), never a directory and nothing under `mods`.
   - Both updaters protect `mods`, `data`, `extracted`, `saves`, `logs` and
     `pd.ini` when they clean an install (`UPDATER_DEFAULT_PROTECTED`).
   - `build-session.ps1 -Remove` and build retention delete a whole build
     directory on purpose, mods included.
   - Where the game looks for mods today (`modmgr.c` about 1740-1810): an
     explicit `--moddir`, else the first that exists of `$E/../mods`,
     `./mods`, `$E/mods`, `mods` under the base dir. One root. For
     `Build/PerfectDark.exe` that is the repo's tracked `mods/` folder; for
     a session build or a release install it is `$E/mods`.
   DECIDED by Mike 2026-11-15: "go on the two-folder design (global folder
   plus the install's own mods folder, global wins on a clash). Put the
   global folder in LocalAppData, %LOCALAPPDATA%\perfectdark\mods, not
   Roaming. Saves stay where they are. Keep --portable and --savedir
   isolating test runs."
   BUILT 91198952: `port/src/mods_roots.c` names the user folder
   (LocalAppData; `<save folder>/mods` under `--portable` or `--savedir`,
   which with `--portable` alone is the install's own folder, so a portable
   install keeps one folder) and creates it. `modmgr.c` reads it first and
   writes only to it (`modmgrGetModsDir`); the install's candidates follow
   and a mod id already found is not used again (logged "is already
   installed from an earlier mods folder; this copy is not used").
   Components and bot variants are scanned in every folder, the user folder
   last, because the catalog keeps the last registration of an id
   (`modmgrGetModsRootCount`, `modmgrGetModsRoot`). The audio mod tool and
   Forge maps now write to the user folder too. `--moddir` and the
   existing-install reuse policy keep a single folder. Not done: moving
   mods already in an install's folder (they are still read, never
   deleted); the hub's file browsers still start in `mods`.
   RUN 2026-11-15 06:17 UTC on shared base 3, client at 91198952:
   `mods_user_folder_smoke` PASS 23/23
   (`D:\PD2-wt\mods\.claude\smoke-verify-runs\results-20261115T061939Z.json`).
   With `--portable --savedir ./user-home`: the user folder
   `./user-home/mods` is scanned first and its copy of `mod.clash_test`
   is the one registered; the install's copy is logged as not used;
   `mod.install_only` from the install's `./mods` is found; the mod
   author's weapon is saved under `./user-home/mods/Weapons/` and loads;
   the install folder reachable as `./mods` and `$E/mods` is read once.
   Not covered by a run: a launch without `--portable`/`--savedir` (it
   would write to the real LocalAppData).
   NEXT, with CORE (Mike, 2026-11-15): CORE implements option A for the
   install's mods root (B-1169: without `--moddir` the client tried
   `$E/../mods` first and ran the one-time folder migration there, which
   could rename another game's mods). Option A: a player install uses only
   the mods folder next to the game (`$E/mods`); a source-checkout build
   keeps the repo-root folder; both alongside the LocalAppData user folder.
   Overlap: CORE's B-1160 (90b9acda on `week-pd/core`) already moved the
   root list into `fsModRoots()` in `fs.c`, which fonts, themes and the
   migration bench walk too, and rewrote the same block of
   `modmgrScanDirectory` that 91198952 rewrote here. Proposed to CORE: the
   user folder becomes entry 0 of `fsModRoots()` (the one writable root,
   named and created by `mods_roots.c`), option A supplies the install
   roots after it, and the dedupe log, the user-folder-last component scans
   and the two direct writers of 91198952 stay. Whichever lands in dev
   second rebases on the other; until CORE answers, nothing here changes.
   In my change the migration already runs in the user folder, not in
   `$E/../mods`, unless the user folder cannot be created.
   The proposal as it was:
   - Writable user root: `$S/mods`. `$S` is already where saves live "so
     they persist across game updates and reinstalls" (`fs.c`):
     `%APPDATA%\perfectdark` normally, the install folder with
     `--portable`, the given folder with `--savedir`. So smokes and manual
     test launches stay isolated with no new flag, and `--moddir` stays an
     explicit override.
   - Read-only bundled root: the install's own `mods/` (what a release zip
     ships and what the build copies from `dev-mods/`), scanned after the
     user root; on the same mod id the user's copy wins.
   - Everything that writes (the mod author, install from file, map import,
     bot variants, network downloads, `.modstate`) keeps using
     `modmgrGetModsDir()`, which becomes the user root. Scanning, the
     component scans in the apply path and the trusted-path check learn the
     second root. About 19 call sites in 8 files; `modmgr.c` holds most.
   - One-time migration: mods found in the install's `mods/` that are not
     dev or release content are offered to be moved to the user root, never
     deleted.
   - Open for Mike: Roaming (beside the saves, follows the user) or
     LocalAppData (stays on this PC; mods can be large).

Found along the way, recorded in `context/bugs.md`: B-1140 (shared INI parser
hangs on a section line with no `=`), B-1141 (mod manager shows JSON escapes
raw), B-1142 (above), B-1143 (pd-tests misreports when stdout is a file),
B-1144 (theme and voice staging names are still typed archives), B-1153 (a
mesh with no sidecars logs errors for them), B-1146 to
B-1148 (Weapons creator: old layout, template behavior replaced by a
default, editor cut settings at 512 bytes; all fixed), B-1149 to B-1152
(what a mod weapon made from a base gun still lacks in play). B-1140 is
fixed (d377380f). B-1145 is fixed by CORE: on shared base 3 all 349 base
templates, the 47 arenas included, save as release-valid mods.

## 5. Open questions

- CORE: v2 ingress versus conformance; magazine for v1 mod weapons.
- Mike: the four mod concepts ("Dry Dock" set) are proposed, not approved.
- Base references in mod archives: the offline validator only knows ids from
  the validated roots. Decide per dependency type whether to embed and
  re-identify or to reference base ids and validate with the base data root.
