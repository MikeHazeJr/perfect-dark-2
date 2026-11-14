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
2c. NEXT: mod author core (`mod_author`): request (template path, mods
   root, display name, author, version, overrides) to a complete mod
   folder `mods/<Category>/<kind>_<slug>/` holding the rebuilt archive
   (`mod:<kind>_<slug>`) and a validated `mod.json` (`mod.<kind>_<slug>`),
   staged as a candidate and swapped in, refusing a folder owned by a
   different mod id. Naming follows the existing weapon creator. Then a
   headless recipe entry (`--mod-author <recipe.json>`) over the same
   function, then the hub screens call it.
3. Weapon author module (C, testable): editor state plus cloned template to a
   clean `.pdweapon`, staged, compiled with `weaponGraphCompileWeaponSourceJson`,
   release-validated, then renamed into place. The hub calls it; template
   loading merges clean primary and secondary graphs back into the editor.
4. One in-game Validate used by every save, pack and enable, held to the
   offline validator by shared fixtures. C-produced archives are also checked
   by `tools/asset_archive_conformance.py` in CI.
5. Recipe runner: a headless entry that drives the same author functions from
   a recipe file, for reproducible mods and CI.
6. Characters tool, vehicle runtime binding plus Vehicles tool, level tools.

## 5. Open questions

- CORE: v2 ingress versus conformance; magazine for v1 mod weapons.
- Mike: the four mod concepts ("Dry Dock" set) are proposed, not approved.
- Base references in mod archives: the offline validator only knows ids from
  the validated roots. Decide per dependency type whether to embed and
  re-identify or to reference base ids and validate with the base data root.
