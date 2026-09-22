#ifndef PD_MODASSET_GLTF_SCENE_H
#define PD_MODASSET_GLTF_SCENE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct modasset_gltf_scene_instance {
    s32 mesh_index;
    s32 node_index;
    double world[16]; /* Column-major parent * local, with local = T * R * S. */
    s32 mirrored;
} modasset_gltf_scene_instance_t;

typedef struct modasset_gltf_scene_plan {
    modasset_gltf_scene_instance_t *instances;
    u32 count;
    u32 mesh_count;
    u32 node_count;
    s32 selected_scene; /* -1 for implicit hierarchy/geometry-library selection. */
} modasset_gltf_scene_plan_t;

/* Strict complete JSON and scene graph validation. An authored default scene
 * wins; otherwise select the first scene, hierarchy roots when scenes are
 * absent, or one identity instance per mesh when nodes are also absent.
 * Distinct nodes may instance one mesh. Roots may be shared across scenes;
 * duplicate children, multiple parents and cycles are invalid.
 * Scene selection does not evaluate animations or skin deformation; node
 * identity remains available to those consumers. No global unit conversion is
 * applied. Quaternion squared length may differ from one by at most 1e-4;
 * only that numerical roundoff is normalized.
 * Output must be fresh/freed before calling; failures leave an empty plan. */
s32 modAssetGltfScenePlanBuild(const char *json, size_t json_size,
    modasset_gltf_scene_plan_t *out, char *error, size_t error_cap);
void modAssetGltfScenePlanFree(modasset_gltf_scene_plan_t *plan);

/* Transform one position without native integer quantization. Rejects
 * non-finite input/results and results outside the float runtime domain. */
s32 modAssetGltfSceneTransformPoint(const modasset_gltf_scene_instance_t *instance,
    const float source[3], float out[3]);

#ifdef __cplusplus
}
#endif
#endif
