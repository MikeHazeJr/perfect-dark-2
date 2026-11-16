---
name: pd-game-model
description: Use for Perfect Dark 2 when making a new 3D model for the game (a weapon, vehicle, character, prop or any mod asset) from a written description, with Tripo for the concept image and the 3D model and Blender for clean-up, then into a typed .pdmesh the game loads. Covers the concept sheet, review gates, Tripo credits, triangle budgets, rigged and stateful parts (turrets, crystals), PD units and axes, the PD mesh export and the mod-author recipe and smoke that put it in the game.
---

# PD game model pipeline (description -> concept -> Tripo 3D -> PD .pdmesh)

How Perfect Dark 2 makes original 3D models for the game and its mods. The
crystal pistol (Mike's asset session, `D:\Game Dev\PD2-Assets\crystal-pistol\`,
read its README) and the Maian hover vehicle
(`D:\Game Dev\PD2-Assets\maian-hover-vehicle\`) are the worked examples.

Rules that always hold:

- **Only the user's description.** The concept prompt is built from what the
  user wrote, plus layout requirements and fixes the user agreed to. Never
  feed existing concept art unless the user says so. Never add style the user
  did not ask for; propose it and let them decide.
- **Privacy.** Prompts go to an outside service: no project name, no person's
  name, no paths. Describe a look in plain words rather than naming the game
  (for a Maian look: "a small grey-skinned alien race; sleek alien technology,
  not Mayan or Aztec", since image models read "Maian" as Mayan).
- **BYOR.** Everything that ships is original. Never put geometry, textures or
  numbers extracted from the base game into a model meant to be shared.
- **Review gates.** Show the concept sheet before any 3D job, and renders of the
  3D model before anything goes into the game. Each gate is the user's call.
- **Credits.** Log the balance before and after every job, record every task id,
  and do at most one retry per step without asking. Say what each step cost.
- **The API key** is the Tripo Blender add-on's stored key, loaded by the
  add-on's own loader inside a background Blender. Never print, log or write
  it.

## 1. Plan the model

Write down, before any job:

- **Category and budget** (table below), the parts that move (rig: a turret's
  yaw and pitch, a door), and the parts that change state (crystals, lights).
  Moving and stateful parts are separate pieces, never fused into the body.
- **Scale and frame.** Model units are about 1 mm: held weapons are drawn at
  0.1 (`bondgun.c`), a mod vehicle takes the hoverbike's model-slot scale
  409/4096 (about 0.1) when it replaces one, and 1 world unit is 1 cm. Axes in
  glTF and in the game: **+Z forward (muzzle, nose), +Y up, +X left.** Blender
  is Z-up: import and export keep glTF's convention.

| Model | Base triangles in game | Tripo `face_limit` | Reference |
|---|---|---|---|
| Held weapon body | 2,500 (+46 per crystal or state piece) | 3,000 | Phoenix 314; crystal pistol body 2,566 |
| Vehicle, hull plus turret | 4,000 (hull about 2,800, turret about 1,200; +46 per crystal) | 5,000 | base hoverbike 257 |
| Character body / head | 2,000 / 600 | 2,500 / 800 | rigid skinning to the 14 body parts (`SKEL_CHR`) |
| Prop | 300 to 1,500 | 1,500 | |

Hard limits: a generated model emits at most 32,767 vertices
(`port/src/modasset_compiler.c`, about 10,000 triangles); at most 64 distinct
vertex colours; textures at most 255 x 255 px (`mod_texture_source.c`).
`face_limit` is set a little above the in-game budget so clean-up has room;
use `smart_low_poly` for clean low-poly topology.

## 2. Concept sheet (Tripo text-to-image)

`scripts/tripo_image.py`, run in a background Blender (not
`--factory-startup`, the add-on must load):

```
blender -b --python scripts/tripo_image.py -- <out_dir> balance none
blender -b --python scripts/tripo_image.py -- <out_dir> concept-v1 <prompt.txt> chat_image_2 1536x1024 medium
blender -b --python scripts/tripo_image.py -- <out_dir> concept-v2 <prompt.txt> chat_image_2.5_sunburst 1536x1024 medium <task id to edit>
```

- The prompt asks for a **multi-view orthographic sheet on a plain light grey
  background with small view labels**: TOP, BOTTOM, LEFT SIDE, RIGHT SIDE,
  FRONT, REAR, and a FRONT THREE-QUARTER hero view, the same object at the same
  scale in every view, no people, no riders, no scenery, then the user's
  description. End with `--no` terms (riders, people, text other than the
  labels).
- Fix inconsistent views with **image-to-image** on the previous task id
  (`chat_image_2.5_sunburst` keeps untouched areas): name the views that are
  right and say what the others must match. Change one thing per edit and
  check every view afterwards: an edit can quietly undo an earlier fix.
- **Style references** (an existing model of ours, for a matching look): pass
  local images after the task id. Tripo refuses a task id inside the
  reference list (code 1004), so give "-" as the task id and the image to edit
  as the first reference; the script uploads each (`/v3/files`, generic file
  name) and the prompt calls them image[1], image[2]...
- Observed cost: 10 credits per image at `medium`. Host: the add-on's key
  works on `https://openapi.tripo3d.ai/v3`; `openapi.tripo3d.com` rejects it.
- **If Tripo cannot make the image** (refused, failing, no credits), make it
  directly: build a quick block-out of the description in a headless Blender
  and render the same labelled views (`scripts/render_views.py`), and say so.
- Show the sheet to the user. Iterate on their notes.

## 3. 3D model (Tripo)

`scripts/tripo_model.py` (own client per job, resume by `task:<id>`, the
add-on's SDK with the `geo_utils` stub, see the script's header):

1. **Inputs without the separate pieces.** Image-to-image on the approved
   sheet: remove the stateful pieces (crystals) and leave their empty seats,
   keep everything else. For a single-image job also make a clean
   three-quarter hero image of the object alone.
2. **Generate.** `image_to_model` with `v3.0-20250812` on the hero image (best
   geometry), or `multiview_to_model` (`v2.5`, front, left, back, right crops)
   when one view hides too much. Set `face_limit` from the budget,
   `smart_low_poly`, `texture` and `pbr` on. Observed cost: 40 credits for a
   textured v3.0 image-to-model with `smart_low_poly` (the crystal pistol's
   plain textured tries were 30).
   - `generate_parts` only works with `texture` and `pbr` off and without
     `smart_low_poly` (Tripo refuses parts with textures, code 1004, and
     smart low-poly wins over parts). For a textured model, split the rigged
     parts by hand in Blender instead.
   - `smart_low_poly` takes `face_limit` from 500 to 20,000 and may overshoot
     it (5,000 asked, 6,510 returned for the Maian vehicle).
   - One input image leaves the unseen sides to Tripo's guess (the vehicle's
     rear came back invented). When the back matters, use multiview, or plan
     to rebuild the unseen side in clean-up.
3. **Review** renders (`scripts/render_views.py ... studio`; `clay` shows the
   shape alone, for judging creases and silhouette) before clean-up.

## 4. Blender clean-up

Follow the crystal pistol's stages (`source/scripts/stage_*.py` in its asset
folder): import, clean shell, error-driven decimation to the budget, separate
the rigged parts (cut a turret at its ring mount; its pivot empty sits on the
yaw axis), sockets as named empties (seats, muzzle, crystal seats, boost
points), bake the textures to 1024 then a 128 px base colour, export glTF and
FBX, write a sockets JSON with transforms in the PD frame. For a model Tripo
already made low-poly and textured, the Maian vehicle's `scripts/stage_a` to
`stage_e` (asset folder) are the pattern: import and weld, split the rigged
parts, decimate, crease, colours into the vertices, then `export_pd_parts.py`.

- **Editing a Tripo mesh.** It comes split along every UV seam (the vehicle:
  8,573 vertices, 3,117 once welded) and carries custom normals. Before moving
  any vertex, weld it (`bmesh.ops.remove_doubles`; UVs stay per face corner) or
  the two sides of each seam move apart. Move the surface by smooth functions
  of position, never by a vertex-normal threshold, which folds it. Keep
  Tripo's normals wherever the shape did not change (sample them from the
  original mesh with a BVH tree): clearing them shows its big flat triangles
  everywhere. Measure outlines from cross-sections (edges cut by a plane), not
  from vertices, which are sparse on long faces.
- **Sharpened edges** (crisp feature lines, e.g. a hull's chine or a nose
  spine): cut an edge loop on the line, move both sides with a falloff that
  peaks on it, and blend split normals in on the crease only. The Maian
  vehicle's `scripts/sharpen_edges.py` in its asset folder does this, with the
  crease angle as an argument; run it after the decimate, so the decimate
  cannot smooth the crease away, and show the owner two strengths side by
  side.
- **Splitting rigged parts** (the vehicle's `stage_b_split.py`): cut at
  measured heights (horizontal cross-sections find a neck's narrowest point and
  a ring's top), then take each part by connectivity from the cut, never by a
  height band (one bent a head's lower rim into spikes). The cut loop's
  vertices are shared until the parts are separate objects, so move a part
  (lift a head) only after separating it. In bmesh, create a face layer before
  collecting faces into sets: adding a layer later lost every tag. Close each
  cut with `holes_fill`, and leave a pivot empty on each joint.
- **Colour for the game.** PD images stop at 255 px. A small model (a gun) can
  carry a 128 px texture (the pistol); on a large one (the vehicle) a 128 px
  atlas smeared everything, so bake Tripo's colour into the vertices instead:
  Cycles, selected-to-active, `EMIT` from Tripo's colour map with
  `bake.target = "VERTEX_COLORS"` into a face-corner colour attribute
  (`stage_e_vertex_colours.py`). Bake ambient occlusion from the new geometry
  with every part in place: Tripo's ORM red channel can be flat white. If a
  texture is still wanted: Smart UV Project shreds a decimated mesh into
  one-triangle islands, `edges_select_sharp` over-selects, and setting custom
  normals tags sharp edges everywhere, so take seams from bmesh face angles,
  and keep a pit (a cockpit) out of the deck's island or it folds over it.
- **Stateful pieces** (crystals): one piece per state (full, regrowing,
  charged or hot, and black or empty as the design needs), origin on its seat,
  plus a regrow clip (node scale from 0.1 to 1 about the seat). Reuse the
  crystal pistol's crystal piece, scaled, when the design says "like the
  pistol's".

## 5. Into the game

- **Mesh.** One `.pdmesh` per model, drawn unlit. Three ways in:
  - OBJ with vertex colours (the default for large models): one vertex per
    triangle corner, `v x y z r g b a`, each `g` group a display list of at
    most 64 colours (colour x occlusion x a fixed key light, k-means
    quantised); `dev-mods/maian_hover_vehicle/build_vehicle_mesh.py` is the
    pattern. The recipe removes the template mesh's glTF; the mod author moves
    the mesh manifest's `model_file` and `geometry` with the descriptor (a
    recipe cannot replace `_meta/manifest.json`).
  - OBJ + MTL with `vt` in texel units and a texture: by catalog id as its own
    `.pdtexture` beside the model (the crystal pistol's `export_pd2_mesh.py`),
    or a PNG inside the mesh named by `map_Kd` (`bindTexture` loads it into a
    private slot).
  - glTF: positions, triangles and each material's `baseColorFactor` only
    (the stand-in, `make_standin_mesh.py`).
  Review the game's look with `render_views.py ... vflat` (glTF `COLOR_0` is
  linear, so a preview converts the sRGB bytes).
  `mesh.ini` uses `[model]` with `kind = mesh`, the geometry key and the four
  sidecars, and `model_scale` = the culling radius (the farthest vertex from
  the origin, in model units). Set `pd_render_class = 3` on opaque materials
  (otherwise far faces show through, seen on the pistol).
- **Parts** come from `model.nodes.json` and `model.parts.json` (node names and
  glTF extras are ignored), listed in ascending part order. A vehicle needs the
  hoverbike's layout: a position root, a bounding-box node, the display list.
  Rigged pieces get their own position node (vertices in that node's frame,
  each triangle's matrix in `model.faces.json`); toggle parts for pieces whose
  visibility follows ammo or state. Toggles start visible (`modelInitRwData`),
  so an alternative state that must stay hidden until the engine switches it
  goes inside the default one (the vehicle's black crystals at 97 % inside the
  seafoam ones).
- **Archive.** A recipe for the in-game mod author in `dev-mods/<name>/`
  (template from `examples/modding/typed-pdxxx-basic/` plus overrides), a
  pd-tests case that builds and validates it (`tests/test_maian_hover_vehicle.cpp`
  is the pattern), and a smoke with screenshots
  (`tools/smoke-verify/tests/maian_vehicle_drive_smoke.json`).

## 6. Records

Keep the asset folder (`D:\Game Dev\PD2-Assets\<name>\`) as the source of
truth: `concept/` (prompts, sheets, `tripo_image_runs.json`), `tripo/` (inputs,
raw models, `tripo_runs.json`), `source/` (.blend, scripts), `export/`,
`renders/`, a README with scale, frame, sockets, budgets, credits and what is
left. Log the work in the session log and the week note.
