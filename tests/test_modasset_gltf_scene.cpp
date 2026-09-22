#include "catch.hpp"
#include "modasset_gltf_scene.h"

#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {
struct Plan {
    modasset_gltf_scene_plan_t value{};
    char error[128]{};
    ~Plan() { modAssetGltfScenePlanFree(&value); }
    bool build(const std::string &json)
    {
        modAssetGltfScenePlanFree(&value);
        return modAssetGltfScenePlanBuild(json.data(), json.size(), &value, error, sizeof(error)) != 0;
    }
};

std::string source(const std::string &fields)
{
    return "{\"asset\":{\"version\":\"2.0\"}," + fields + "}";
}

void point(const modasset_gltf_scene_instance_t &instance, const float input[3],
    float x, float y, float z)
{
    float out[3] = { -999, -999, -999 };
    REQUIRE(modAssetGltfSceneTransformPoint(&instance, input, out) == 1);
    REQUIRE(out[0] == Approx(x).margin(1e-6));
    REQUIRE(out[1] == Approx(y).margin(1e-6));
    REQUIRE(out[2] == Approx(z).margin(1e-6));
}
}

TEST_CASE("glTF scene plans honor selection and preserve repeated mesh instances",
    "[modding][pdxxx][c3842][gltf-scene]")
{
    Plan plan;
    const std::string fields =
        "\"meshes\":[{},{}],\"nodes\":[{\"mesh\":0,\"translation\":[10,0,0]},"
        "{\"mesh\":0,\"translation\":[20,0,0]},{\"mesh\":1,\"translation\":[999,0,0]}],"
        "\"scenes\":[{\"nodes\":[2]},{\"nodes\":[1,0]}]";
    REQUIRE(plan.build(source(fields + ",\"scene\":1")));
    REQUIRE(plan.value.selected_scene == 1);
    REQUIRE(plan.value.count == 2);
    REQUIRE(plan.value.mesh_count == 2);
    REQUIRE(plan.value.node_count == 3);
    REQUIRE(plan.value.instances[0].node_index == 1);
    REQUIRE(plan.value.instances[1].node_index == 0);
    REQUIRE(plan.value.instances[0].mesh_index == 0);
    REQUIRE(plan.value.instances[1].mesh_index == 0);
    const float origin[3] = { 0, 0, 0 };
    point(plan.value.instances[0], origin, 20, 0, 0);
    point(plan.value.instances[1], origin, 10, 0, 0);

    REQUIRE(plan.build(source(fields)));
    REQUIRE(plan.value.selected_scene == 0);
    REQUIRE(plan.value.count == 1);
    REQUIRE(plan.value.instances[0].node_index == 2);
    REQUIRE(plan.value.instances[0].mesh_index == 1);
}

TEST_CASE("glTF scene plans support geometry libraries hierarchy roots and shared scene roots",
    "[modding][pdxxx][c3842][gltf-scene]")
{
    Plan plan;
    REQUIRE(plan.build(source("\"meshes\":[{},{}]")));
    REQUIRE(plan.value.count == 2);
    REQUIRE(plan.value.selected_scene == -1);
    REQUIRE(plan.value.instances[0].node_index == -1);
    REQUIRE(plan.value.instances[1].mesh_index == 1);
    const float input[3] = { 2, 3, 4 };
    point(plan.value.instances[1], input, 2, 3, 4);

    const std::string nodes = "\"meshes\":[{}],\"nodes\":[{\"children\":[2]},"
        "{\"mesh\":0},{\"mesh\":0}]";
    REQUIRE(plan.build(source(nodes)));
    REQUIRE(plan.value.count == 2);
    REQUIRE(plan.value.instances[0].node_index == 2);
    REQUIRE(plan.value.instances[1].node_index == 1);
    REQUIRE(plan.build(source(nodes + ",\"scenes\":[{\"nodes\":[0]},{\"nodes\":[0,1]}],\"scene\":1")));
    REQUIRE(plan.value.count == 2);
    REQUIRE(plan.value.instances[0].node_index == 2);
    REQUIRE(plan.value.instances[1].node_index == 1);
    REQUIRE(plan.build(source(nodes + ",\"scenes\":[{}]")));
    REQUIRE(plan.value.count == 0); // Empty selected scene does not resurrect orphan meshes.
}

TEST_CASE("glTF scene transforms compose parent TRS and equivalent column-major matrices",
    "[modding][pdxxx][c3842][gltf-scene]")
{
    Plan trs, matrix;
    REQUIRE(trs.build(source("\"meshes\":[{}],\"nodes\":["
        "{\"translation\":[5,0,0],\"children\":[1]},"
        "{\"mesh\":0,\"rotation\":[0,0,0.7071067811865476,0.7071067811865476],\"scale\":[2,3,4]}]")));
    REQUIRE(matrix.build(source("\"meshes\":[{}],\"nodes\":[{\"mesh\":0,"
        "\"matrix\":[0,2,0,0,-3,0,0,0,0,0,4,0,5,0,0,1]}]")));
    const float input[3] = { 1, 1, 1 };
    point(trs.value.instances[0], input, 2, 2, 4);
    point(matrix.value.instances[0], input, 2, 2, 4);
    REQUIRE(trs.value.instances[0].mirrored == 0);
    for (size_t i = 0; i < 16; i++)
        REQUIRE(trs.value.instances[0].world[i] == Approx(matrix.value.instances[0].world[i]).margin(1e-12));
    Plan nested;
    REQUIRE(nested.build(source("\"meshes\":[{}],\"nodes\":["
        "{\"rotation\":[0,0,0.7071067811865476,0.7071067811865476],\"scale\":[2,3,1],\"children\":[1]},"
        "{\"mesh\":0,\"translation\":[4,5,6]}]")));
    const float origin[3] = { 0, 0, 0 };
    point(nested.value.instances[0], origin, -15, 8, 6);
}

TEST_CASE("glTF scene orientation preserves negative and zero scale without unit conversion",
    "[modding][pdxxx][c3842][gltf-scene]")
{
    Plan plan;
    REQUIRE(plan.build(source("\"meshes\":[{}],\"nodes\":["
        "{\"mesh\":0,\"scale\":[-2,3,4],\"children\":[1]},"
        "{\"mesh\":0,\"scale\":[-1,1,1]},{\"mesh\":0,\"scale\":[0,0,0]}]")));
    REQUIRE(plan.value.count == 3);
    REQUIRE(plan.value.instances[0].mirrored == 1);
    REQUIRE(plan.value.instances[1].mirrored == 0);
    REQUIRE(plan.value.instances[2].mirrored == 0);
    const float input[3] = { 0.25f, 0.5f, 1.0f };
    point(plan.value.instances[0], input, -0.5f, 1.5f, 4);
    point(plan.value.instances[1], input, 0.5f, 1.5f, 4);
    point(plan.value.instances[2], input, 0, 0, 0);
}

TEST_CASE("glTF scene plans retain skin animation identity and accept quaternion roundoff",
    "[modding][pdxxx][c3842][gltf-scene]")
{
    Plan plan;
    REQUIRE(plan.build(source("\"meshes\":[{}],\"nodes\":[{\"mesh\":0,\"skin\":0,"
        "\"rotation\":[0,0,0.7071068,0.7071068]}],\"skins\":[{\"joints\":[0]}],"
        "\"animations\":[{\"channels\":[],\"samplers\":[]}]")));
    REQUIRE(plan.value.count == 1);
    REQUIRE(plan.value.instances[0].node_index == 0);
    const float input[3] = { 1, 0, 0 };
    point(plan.value.instances[0], input, 0, 1, 0);
    REQUIRE(plan.build(source("\"meshes\":[{}],\"no\\u0064es\":[{\"me\\u0073h\":0}],"
        "\"sce\\u006ee\":0,\"scenes\":[{\"nodes\":[0]}]")));
    REQUIRE(plan.value.count == 1);
    REQUIRE(plan.value.instances[0].node_index == 0);
}

TEST_CASE("glTF scene plans do not evaluate inactive mesh transforms",
    "[modding][pdxxx][c3842][gltf-scene]")
{
    const std::string fields = "\"meshes\":[{}],\"nodes\":[{\"mesh\":0},"
        "{\"scale\":[1e308,1,1],\"children\":[2]},{\"mesh\":0,\"scale\":[1e308,1,1]}],"
        "\"scenes\":[{\"nodes\":[0]},{\"nodes\":[1]}]";
    Plan plan;
    REQUIRE(plan.build(source(fields)));
    REQUIRE(plan.value.count == 1);
    REQUIRE(plan.value.instances[0].node_index == 0);
    REQUIRE_FALSE(plan.build(source(fields + ",\"scene\":1")));
    REQUIRE(std::string(plan.error) == "gltf_node_world_transform_overflow");
}

TEST_CASE("glTF scene plans reject malformed graph and transform data atomically",
    "[modding][pdxxx][c3842][gltf-scene]")
{
    const std::string meshes = "\"meshes\":[{}],";
    const std::vector<std::string> fields = {
        "\"meshes\":{}", "\"meshes\":[false]", meshes + "\"nodes\":{}",
        meshes + "\"nodes\":[false]", meshes + "\"nodes\":[{\"mesh\":true}]",
        meshes + "\"nodes\":[{\"mesh\":0.5}]", meshes + "\"nodes\":[{\"mesh\":1}]",
        meshes + "\"nodes\":[{\"children\":[0]}]",
        meshes + "\"nodes\":[{\"children\":[1,1]},{\"mesh\":0}]",
        meshes + "\"nodes\":[{\"children\":[2]},{\"children\":[2]},{\"mesh\":0}]",
        meshes + "\"nodes\":[{\"mesh\":0},{\"children\":[2]},{\"children\":[1]}]",
        meshes + "\"nodes\":[{\"children\":[9]}]",
        meshes + "\"nodes\":[{\"mesh\":0,\"translation\":[1,2]}]",
        meshes + "\"nodes\":[{\"mesh\":0,\"scale\":[1,true,1]}]",
        meshes + "\"nodes\":[{\"mesh\":0,\"rotation\":[0,0,0,0]}]",
        meshes + "\"nodes\":[{\"mesh\":0,\"rotation\":[0,0,0,2]}]",
        meshes + "\"nodes\":[{\"mesh\":0,\"matrix\":[1,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1]}]",
        meshes + "\"nodes\":[{\"mesh\":0,\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],\"translation\":[0,0,0]}]",
        meshes + "\"nodes\":[{\"mesh\":0,\"translation\":[1e9999,0,0]}]",
        meshes + "\"nodes\":[{\"mesh\":0}],\"scene\":0",
        meshes + "\"nodes\":[{\"mesh\":0}],\"scenes\":[{}],\"scene\":1",
        meshes + "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0,0]}]",
        meshes + "\"nodes\":[{\"children\":[1]},{\"mesh\":0}],\"scenes\":[{\"nodes\":[1]}]",
        meshes + "\"nodes\":[{\"mesh\":0}],\"nodes\":[]"
    };
    for (const std::string &field : fields) {
        Plan plan;
        INFO(field);
        REQUIRE_FALSE(plan.build(source(field)));
        REQUIRE(plan.value.instances == nullptr);
        REQUIRE(plan.value.count == 0);
        REQUIRE(plan.value.selected_scene == -1);
        REQUIRE(plan.error[0] != '\0');
    }
    Plan plan;
    REQUIRE_FALSE(plan.build(source("\"meshes\":[{}]") + " trailing"));
}

TEST_CASE("glTF scene traversal supports deep flat node hierarchies and finite runtime positions",
    "[modding][pdxxx][c3842][gltf-scene]")
{
    constexpr size_t count = 4096;
    std::string fields = "\"meshes\":[{}],\"nodes\":[";
    for (size_t i = 0; i < count; i++) {
        if (i) fields += ',';
        fields += "{\"translation\":[1,0,0],";
        fields += i + 1 < count ? "\"children\":[" + std::to_string(i + 1) + "]}" : "\"mesh\":0}";
    }
    fields += ']';
    Plan plan;
    REQUIRE(plan.build(source(fields)));
    REQUIRE(plan.value.count == 1);
    REQUIRE(plan.value.instances[0].node_index == count - 1);
    const float origin[3] = { 0, 0, 0 };
    point(plan.value.instances[0], origin, static_cast<float>(count), 0, 0);

    float out[3] = { 8, 9, 10 };
    const float invalid[3] = { std::numeric_limits<float>::infinity(), 0, 0 };
    REQUIRE(modAssetGltfSceneTransformPoint(&plan.value.instances[0], invalid, out) == 0);
    REQUIRE(out[0] == 8);
    REQUIRE(out[1] == 9);
    REQUIRE(out[2] == 10);
    auto instance = plan.value.instances[0];
    instance.world[0] = 2;
    const float overflow[3] = { std::numeric_limits<float>::max(), 0, 0 };
    REQUIRE(modAssetGltfSceneTransformPoint(&instance, overflow, out) == 0);
    REQUIRE(out[0] == 8);
    instance.world[0] = 1;
    instance.world[12] = 32759.4991;
    const float near_native_limit[3] = { 8, 0, 0 };
    point(instance, near_native_limit, 32767.5f, 0, 0);
}
