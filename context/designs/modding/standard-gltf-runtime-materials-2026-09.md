# Standard glTF material and runtime consumption

**Date:** 2026-09-05, 08:25 PM ET
**Status:** Proposed implementation sequence; source inspection only. No new build, runtime, GPU, or visual verdict is recorded here.
**Related work:** T-ASSETS-043 scene geometry; T-ASSETS-039 standard glTF buffer storage. Behavior-graph architecture decisions are independent of this design.

## Purpose and current boundary

Make the material, attribute, image, and animation data authored in public glTF/GLB sources reach actual game consumers. Base-game extraction and mod authoring use the same catalog/provider source chain. Any decoded pixels, compiled draw packets, native display lists, or GPU allocations are owned, source-hashed, rebuildable products. They are not additional files the author must maintain.

T-ASSETS-043 implements shared static scene selection, node instancing, parent transforms, reflected winding, and transformed-position validation for collision, generated models, and Scenario CPU geometry. Its production integration and validation are tracked separately. That work does not establish material, normal, skin, morph-target, animation, GPU, or human-visual parity. Retaining a node index for later consumers does not apply its skin or animation.

The next bounded unit is complete base-color/vertex-color consumption, including the native palette fix required to avoid silently losing colors. Texture sampling and the float rendering bridge follow using the same source representation. A parser that recognizes a field, a preview that displays it, or a rejection of every source using it does not count as implementation.

## Existing production surfaces and required changes

| Surface | Current behavior found in source | Required change |
| --- | --- | --- |
| `port/src/modasset_gltf_document.cpp`: `modAssetJsonReadValue`, `modAssetGltfReadDocument` | Shared complete JSON reader with decoded keys and strict lexical checks | Reuse the parsed tree for material and attribute interpretation; do not introduce another substring parser. |
| `port/include/modasset_gltf_scene.h`: scene plan and point transform | Selected static instances, world matrices, node identity, reflection flag | Feed the same selected instances into material/draw preparation. Keep node identity available for later deformation. |
| `port/src/modasset_compiler.c`: `parseGltfMeshFromJson`, `gltfApplyPrimitiveBaseColor` | Appends positions/triangles and applies a material factor as one byte color to every primitive vertex; does not consume standard vertex colors, UVs, or texture bindings in this generic path | Read attributes and material association through the shared representation. Multiply vertex color and factor rather than replacing one with the other. |
| `obj_vertex_t`, `obj_texcoord_t`, `obj_triangle_t`, `obj_material_t` | Existing generic native adapter has byte colors, one UV pair per corner, material indexes, and native texture/render state | Use as a native conversion boundary, not as the complete glTF source model. Preserve float color, UV, normal, and material data in the shared representation before conversion. |
| `fillGeneratedVertex` | Converts positions and UVs to signed-16 native fields; UV conversion is `u * 32`, `(1 - v) * 32` | Keep this documented OBJ/native conversion distinct from standard normalized glTF UVs. Do not feed glTF UVs into it unchanged and assume texture-coordinate parity. |
| `generatedColourTableIntern`, `GENERATED_COLOUR_TABLE_CAP` | After 64 distinct byte colors, chooses a nearest existing color and warns | Replace this approximation with ordered palette pages and explicit color loads before enabling arbitrary glTF vertex color. |
| `buildGeneratedModeldefFromMesh`, `fillGeneratedTriVertices`, generated hierarchy payload emission | Emits `Vtx`, `Col`, matrices, and display-list commands; static model node limits emitted vertices to 32767 | Make palette pages reach both relevant generated emission paths while preserving triangle, matrix, material, and render-command order. Larger source geometry belongs in the subsequent float bridge or correctly partitioned native nodes, not a new authored limit. |
| `port/fast3d/gfx_pc.cpp`: vertex load, `gfx_sp_set_vertex_colors` | `Vtx.colour >> 2` selects one of at most 64 addresses encodable in the vertex byte; color data is interpreted as normals when lighting is enabled | Verify actual load-time palette selection. Do not overwrite normal data with colors or claim the native union carries both independently. |
| `port/fast3d/scenario_scene_renderer.cpp`: `readFloatVec2`, `readFloatVec4`, `appendPrimitive`, `parseMaterials` | UVs require float VEC2; colors require float VEC4; base-color factor is not applied; texture wrapping is read but min/mag filters are not | Replace duplicate decoding with shared typed attributes/materials. Preserve existing extracted secondary-texture metadata through an explicit adapter. |
| Scenario `parseImages`, `ensureShader`, `renderSceneGroup` | Images are decoded from buffer views; shader multiplies sampled texture and vertex color; current draw path does not establish full standard alpha/PBR semantics | Add standard image sources, factor/color product, complete sampler state, and independently verified alpha/culling behavior. |
| `modTextureLoadRgba32Source`, `assetCatalogResolveTexturePrivateSlot` | Public catalog image loader uses a native 255x255 header limit and private numeric slots | Reuse provider selection and image decoding patterns, not the dimension/slot restriction as a glTF authoring requirement. Embedded glTF images must not require authored numeric texture IDs or duplicate `.pdtexture` sources. |
| `GfxRenderingAPI::set_sampler_parameters` | Wrap modes plus a single linear-filter Boolean | Add a material-capable sampler descriptor for the float path; separate minification, magnification, and mip selection cannot be represented by that Boolean. |

## Shared source model

Introduce `port/include/modasset_gltf_material.h` and `port/src/modasset_gltf_material.cpp`, or equivalently named small source modules, with a C-compatible owned result and C++ implementation using the strict tree. Keep responsibilities separate:

1. **Attribute views and decoding:** component type, normalized flag, dimensions, stride, count, bounded source span, and optional sparse overrides. Reuse `modAssetGltfAccessorBounds`. Decode float and normalized unsigned-byte/unsigned-short color and UV formats. Validate finite values and equal primitive attribute counts. Decode only the selected geometry's attribute payloads while validating referenced descriptors. Sparse accessors need real decoding before sparse support is claimed.
2. **Material records:** base-color factor, texture/image identity, selected coordinate set, sampler descriptor, alpha mode/cutoff, double-sided flag, and later metallic/roughness, normal, emissive, and occlusion bindings. Retain glTF material indexes inside the source document; public cross-asset identity remains a catalog ID or a standard source-relative reference.
3. **Source resource resolution:** read local images through the same provider/archive boundary as the glTF document; decode buffer-view and embedded image sources without writing a second authored file. Return owned decoded images separately from immutable material records.
4. **Consumer conversion:** the generated native adapter and Scenario preparation consume the same decoded values. Neither consumer invents alternate defaults or reparses material JSON independently.

The first unit supplies `COLOR_0` VEC3/VEC4 as float, normalized unsigned byte, or normalized unsigned short. VEC3 alpha is one; absent color and factor are white. Clamp vertex color components as specified, then compute the component-wise linear product with `baseColorFactor`. Retain float values until final native-byte or GPU conversion. Do not multiply in sRGB space or quantize the two inputs separately before multiplication. Khronos defines the color formats, defaults, and additional linear multiplier in the [glTF specification](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/Specification.adoc).

## First implementation unit: color through both consumers

In the generic compiler, associate each primitive's material with its emitted triangles and decode its color accessor beside its positions. Apply factor/color products before final `Col` encoding. Primitive-local copies already provide an opportunity to keep two materials referencing one position accessor from overwriting each other's colors. Preserve triangle corner association when reflection changes winding.

In Scenario preparation, replace its float-VEC4-only color reader with the same decoded product. Feed that product to the existing vertex-color shader input. Verify material alpha behavior separately: merely carrying an alpha byte is not proof that the selected rendering pass and fragment operation honor `alphaMode`.

### Native palette paging

Build consecutive triangle batches whose distinct encoded colors fit in a 64-entry page. Store every page in the generated owner's color allocation. Emit a `gSPColor` referencing the page base and exact page count before the affected `gSPVertex` commands. Each emitted vertex carries the index within that page. A triangle can always fit its three colors in a fresh page.

Color selection happens at vertex load, so changing the page after loading a vertex is insufficient. Reload affected vertices. Do not sort faces to improve color deduplication: authored transparency and display-command order must remain intact. Respect material changes, per-corner matrix loads, and source render-state boundaries already handled by `generatedTriComputeBatch` and hierarchy emission. Adjust allocation and command budgets from the resulting batch plan rather than retaining the old one-table assumptions.

Apply the fix to the shared generated-color behavior where used; do not leave the same nearest-color approximation active in a sibling path. Preserve the ordinary extracted native-source path and its semantic metadata. Any propagation into that path needs its own byte/order and production validation, especially per-corner matrix and geometry-mode behavior.

## Subsequent unit: image, UV, sampler, and material drawing

Use a source-owned float draw packet for authored glTF data that the native fields cannot faithfully express. The packet should contain selected primitive/node identity, float vertex attributes, index topology, material bindings, and owned resource handles. It is a private rebuildable runtime object, not a new public file format.

Connect generated-model render dispatch to the packet using an explicit owner/adapter registered with the model's lifecycle. Reuse model transforms, visibility, culling, and render ordering rather than drawing a second unrelated scene. Scenario consumes the same packet/material bindings. Retain the normal extracted-native adapter for source that requires its part, matrix, display-command, room, or portal semantics. A float visual bridge does not erase gameplay collision or native model metadata requirements.

For standard textures:

- Load PNG/JPEG from archive-local image URIs, embedded image URIs, or valid buffer views. Resolve through provider-owned paths and share decoded-image ownership; do not register hand-maintained duplicate catalog rows just to satisfy a texture marker.
- Keep normalized glTF coordinate sets and choose the material's declared set. Apply coordinate origin conversion once at the consumer boundary with an asymmetric image test. Do not inherit the OBJ V flip or native texel-unit scaling by accident.
- Represent wrap S/T, min filter, mag filter, and mip behavior independently. The source's sampler identity is distinct from image identity: two materials may share image pixels while requiring different samplers. The supported standard filter/wrap values are specified by the [sampler schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/sampler.schema.json).
- Decode base-color texture RGB using the sRGB transfer function; alpha remains linear. Multiply sampled color by the shared factor/color product in linear space, then use the output transfer convention of the selected rendering target. Missing base-color texture samples as white. See the [material schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.pbrMetallicRoughness.schema.json).
- Implement OPAQUE, MASK cutoff, BLEND, and double-sided behavior explicitly. The current tiny alpha-discard floor and legacy inferred cutout behavior must not override standard material semantics. Keep compatibility behavior restricted to the extracted-source adapter that requires it.

Normals and PBR follow through separate float attributes, not through a `Col` union with lighting-dependent interpretation. Apply inverse-transpose normal transforms, account for reflection and tangent handedness, and supply missing normal/tangent generation where required. Add metallic/roughness, normal, occlusion, and emissive sampling through the same material record. A base-color-only shader is not full PBR support.

Skins, morph targets, and animation are a further connected consumer unit. Retain node/mesh identity, joints, weights, inverse bind matrices, morph weights, and channel targets in the shared source representation. Evaluate deformation and animation into the same draw packet, preserving the model's runtime transform and lifecycle. Define corresponding collision/bounds behavior explicitly. Accepting skin metadata without evaluating it must not be described as skin support.

## Resource identity, rollback, and reload

Derive cache identity from the strict source document, all consumed buffer and image bytes, compiler/material schema versions, and relevant conversion settings. The same image reached through two texture records can share pixel storage while retaining distinct samplers. An image-only or buffer-only edit must invalidate the affected product even when the JSON is unchanged.

Build replacements as owned candidates. Publish model/Scenario adapters only after required resources are ready; on failure release only newly acquired resources. Preserve the previous live asset until replacement succeeds. On unload, release draw packets, decoded images, GPU objects, and provider references through their recorded ownership. Pointer-based texture cache entries must be invalidated before backing pixels are freed or reused. Process-private slots or handles never become public authored, save, or wire identity.

No fallback may silently drop a declared texture, selected UV set, color, sampler, normal, material mode, skin, or animation. Temporary unsupported-feature diagnostics are an honest incomplete state; they are not a substitute for implementing valid source semantics and must not be used to close full-support work.

## Required proof and acceptance

Run one source-frozen batch through the parent-owned coordination/build process. The following evidence classes remain separate:

| Evidence | Required witness |
| --- | --- |
| Shared decoder tests | Float/unorm8/unorm16; RGB/RGBA; absent defaults; factor multiplication; finite/range/count/stride failures; decoded keys; sparse cases once implemented. |
| Actual native compiler output | Public glTF source through `modAssetCompilerBuildModeldef`; inspect generated material association, emitted `Vtx`, and `Col` products. Include reused mesh/accessors with different materials and reflected winding. |
| Actual display-list palette use | More than 64 distinct encoded colors crossing multiple pages; execute the production Gfx command interpretation with a recording rendering backend and verify every emitted vertex resolves the intended page/color. Merely counting palettes or inspecting source text is insufficient. Include page changes around matrix/material boundaries. |
| Scenario CPU consumption | `scenarioSceneRendererProbeSource` exposes decoded final color/UV/material/image/sampler witnesses, not just geometry counts. Compare with independently authored expected values, including native quantization tolerance where relevant. |
| Actual texture/sampler use | Recording production backend checks uploaded image bytes, selected coordinate sets, complete sampler state, and draw bindings. Sharing one image with two samplers must not leak state between draws. |
| GPU result | Small controlled render with asymmetric texture, distinct UV sets, factor/color modulation, wrap and min/mag behavior, MASK/BLEND/OPAQUE, and double-sided cases. Capture pixels/framebuffer results with tolerances appropriate to filtering. Keep human visual acceptance distinct from numerical GPU checks. |
| Reload/lifecycle | Change only external image bytes, then only buffer color/UV bytes; verify cache identity and rendered output change. Exercise rejected replacement, corrected replacement, unload/reload, and shared-resource ownership without stale adapters. |
| Extracted-source propagation | Existing native model/Scenario sources retain material, matrix, command-order, and rendering behavior. No ROM fallback and no new duplicate runtime-authored payloads. |

The current `asset_source_harness.c` scene matrix can be extended for these production witnesses. Its existing model test examines generated native geometry and its Scenario test exercises CPU preparation; neither existing witness should be relabeled as display-list execution, texture use, GPU, or visual proof.

## Suggested ownership sequence

1. Shared strict material/attribute module plus focused decoder tests.
2. Native color paging and generic compiler integration, with exact ownership of the large compiler source.
3. Scenario integration and production color/DL witnesses, then one coherent validation batch.
4. Source-owned image resources and float draw packet bridge across generated-model dispatch and Scenario; complete sampler/UV/alpha tests before expanding material claims.
5. Normals/PBR, then skin/morph/animation evaluation with independent production proof for each capability.

Workbench records should track these as implementation and validation units with explicit dependencies. This document proposes the sequence; it does not create item IDs or mark any unit validated.
