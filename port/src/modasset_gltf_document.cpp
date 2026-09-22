#include <charconv>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../external/imgui-node-editor/crude_json.h"
#include "modasset_gltf_document.h"
#include "modasset_gltf_scene.h"

namespace {
using Json = crude_json::value;

// crude_json's stock parser silently accepts duplicate object keys. Build
// its value tree with a bounded, strict reader so admission and conversion
// share the same decoded keys, number grammar, and complete-input contract.
class Reader {
public:
    Reader(const char *text, size_t size) : at(text), end(text + size) {}
    bool read(Json &out)
    {
        if (!value(out, 0)) return false;
        whitespace();
        return at == end;
    }
private:
    const char *at;
    const char *end;

    void whitespace()
    {
        while (at < end && (*at == ' ' || *at == '\t' ||
                *at == '\r' || *at == '\n')) at++;
    }
    bool take(char c)
    {
        if (at == end || *at != c) return false;
        at++;
        return true;
    }
    bool hex(unsigned &out)
    {
        out = 0;
        for (int i = 0; i < 4; i++) {
            if (at == end) return false;
            unsigned char c = static_cast<unsigned char>(*at++);
            unsigned n = c >= '0' && c <= '9' ? c - '0' :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (n == 16) return false;
            out = (out << 4) | n;
        }
        return true;
    }
    static void utf8(std::string &out, unsigned cp)
    {
        if (cp <= 0x7f) out += static_cast<char>(cp);
        else if (cp <= 0x7ff) {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 63));
        } else if (cp <= 0xffff) {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 63));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        }
    }
    bool string(std::string &out)
    {
        if (!take('"')) return false;
        while (at < end) {
            unsigned char c = static_cast<unsigned char>(*at++);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c == '\\') {
                if (at == end) return false;
                c = static_cast<unsigned char>(*at++);
                switch (c) {
                case '"': case '\\': case '/': out += static_cast<char>(c); break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex(cp) || cp == 0) return false;
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        unsigned low;
                        if (!take('\\') || !take('u') || !hex(low) ||
                                low < 0xdc00 || low > 0xdfff) return false;
                        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                    utf8(out, cp);
                    break;
                }
                default: return false;
                }
            } else if (c < 0x80) out += static_cast<char>(c);
            else {
                unsigned cp = 0;
                unsigned minimum = 0;
                int remaining = 0;
                if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; remaining = 1; minimum = 0x80; }
                else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; remaining = 2; minimum = 0x800; }
                else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; remaining = 3; minimum = 0x10000; }
                else return false;
                for (int i = 0; i < remaining; i++) {
                    if (at == end) return false;
                    unsigned char next = static_cast<unsigned char>(*at++);
                    if ((next & 0xc0) != 0x80) return false;
                    cp = (cp << 6) | (next & 63);
                }
                if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
                utf8(out, cp);
            }
        }
        return false;
    }
    bool number(Json &out)
    {
        const char *start = at;
        take('-');
        if (!take('0')) {
            if (at == end || *at < '1' || *at > '9') return false;
            while (at < end && *at >= '0' && *at <= '9') at++;
        }
        if (take('.')) {
            const char *digits = at;
            while (at < end && *at >= '0' && *at <= '9') at++;
            if (at == digits) return false;
        }
        if (at < end && (*at == 'e' || *at == 'E')) {
            at++;
            if (at < end && (*at == '+' || *at == '-')) at++;
            const char *digits = at;
            while (at < end && *at >= '0' && *at <= '9') at++;
            if (at == digits) return false;
        }
        double result;
        const auto parsed = std::from_chars(start, at, result);
        if (parsed.ec != std::errc{} || parsed.ptr != at || !std::isfinite(result)) return false;
        out = result;
        return true;
    }
    bool value(Json &out, unsigned depth)
    {
        if (depth > 64) return false;
        whitespace();
        if (at == end) return false;
        if (*at == '"') {
            std::string text;
            if (!string(text)) return false;
            out = std::move(text);
            return true;
        }
        if (take('{')) {
            crude_json::object object;
            whitespace();
            if (!take('}')) {
                do {
                    whitespace();
                    std::string key;
                    Json child;
                    if (!string(key)) return false;
                    whitespace();
                    if (!take(':') || !value(child, depth + 1) ||
                            !object.emplace(std::move(key), std::move(child)).second) return false;
                    whitespace();
                    if (take('}')) { out = std::move(object); return true; }
                } while (take(','));
                return false;
            }
            out = std::move(object);
            return true;
        }
        if (take('[')) {
            crude_json::array array;
            whitespace();
            if (!take(']')) {
                do {
                    Json child;
                    if (!value(child, depth + 1)) return false;
                    array.emplace_back(std::move(child));
                    whitespace();
                    if (take(']')) { out = std::move(array); return true; }
                } while (take(','));
                return false;
            }
            out = std::move(array);
            return true;
        }
        const char *literals[] = { "null", "true", "false" };
        for (unsigned i = 0; i < 3; i++) {
            const size_t length = std::strlen(literals[i]);
            if (static_cast<size_t>(end - at) >= length &&
                    std::memcmp(at, literals[i], length) == 0) {
                at += length;
                out = i == 0 ? Json(nullptr) : Json(i == 1);
                return true;
            }
        }
        return number(out);
    }
};

bool unsignedValue(const Json &value, u32 &out)
{
    if (!value.is_number()) return false;
    const double number = value.get<crude_json::number>();
    if (number < 0 || number > 2147483647.0 || std::floor(number) != number) return false;
    out = static_cast<u32>(number);
    return true;
}

bool document(const char *json, size_t size, bool glb, bool required,
    Json &root, const Json *&uri, u32 &declared)
{
    uri = nullptr;
    declared = 0;
    if (!modAssetGltfReadDocument(json, size, root)) return false;
    if (!root.contains("buffers")) {
        return !required && (!root.contains("bufferViews") ||
            (root["bufferViews"].is_array() && root["bufferViews"].get<crude_json::array>().empty()));
    }
    const Json &buffers = root["buffers"];
    if (!buffers.is_array() || buffers.get<crude_json::array>().size() != 1) return false;
    const Json &buffer = buffers[0];
    if (!buffer.is_object() || !buffer.contains("byteLength") ||
            !unsignedValue(buffer["byteLength"], declared) || !declared) return false;
    if (buffer.contains("uri")) {
        if (glb) return false;
        uri = &buffer["uri"];
        if (!uri->is_string() || uri->get<crude_json::string>().empty()) return false;
    } else if (!glb) return false;
    const Json &views = root["bufferViews"];
    if (!views.is_array() || views.get<crude_json::array>().empty()) return false;
    for (const Json &view : views.get<crude_json::array>()) {
        u32 index = 0, offset = 0, length = 0, stride = 0, target = 0;
        if (!view.is_object() || !view.contains("buffer") || !view.contains("byteLength") ||
                !unsignedValue(view["buffer"], index) || index != 0 ||
                !unsignedValue(view["byteLength"], length) || !length ||
                (view.contains("byteOffset") && !unsignedValue(view["byteOffset"], offset)) ||
                offset > declared || length > declared - offset ||
                (view.contains("byteStride") && (!unsignedValue(view["byteStride"], stride) ||
                    stride < 4 || stride > 252 || stride % 4 != 0)) ||
                (view.contains("target") && (!unsignedValue(view["target"], target) ||
                    (target != 34962 && target != 34963)))) return false;
    }
    return true;
}

bool visit(const Json &value, const Json *buffer_uri, u32 declared,
    modasset_gltf_string_visitor visitor, void *userdata)
{
    if (value.is_object()) {
        for (const auto &member : value.get<crude_json::object>()) {
            if (member.first == "uri" && (!member.second.is_string() ||
                    member.second.get<crude_json::string>().empty())) return false;
            if (member.second.is_string()) {
                const bool buffer = &member.second == buffer_uri;
                if (visitor(member.first.c_str(), member.second.get<crude_json::string>().c_str(), buffer,
                        buffer ? declared : 0, userdata) != 0) return false;
            } else if (!visit(member.second, buffer_uri, declared, visitor, userdata)) return false;
        }
    } else if (value.is_array()) {
        for (const Json &child : value.get<crude_json::array>()) {
            if (!visit(child, buffer_uri, declared, visitor, userdata)) return false;
        }
    }
    return true;
}
s32 bufferDocument(const char *json, size_t json_size, s32 glb, bool required,
    char *out_uri, size_t uri_cap, u32 *out_declared_size)
{
    if (out_uri && uri_cap) out_uri[0] = '\0';
    if (out_declared_size) *out_declared_size = 0;
    if (!out_uri || !uri_cap || !out_declared_size) return 0;
    try {
        Json root;
        const Json *uri;
        u32 declared;
        if (!document(json, json_size, glb != 0, required, root, uri, declared)) return 0;
        if (uri) {
            const std::string &text = uri->get<crude_json::string>();
            if (text.size() >= uri_cap) return 0;
            std::memcpy(out_uri, text.c_str(), text.size() + 1);
        }
        *out_declared_size = declared;
        return 1;
    } catch (...) { return 0; }
}
} // namespace

bool modAssetJsonReadValue(const char *json, size_t size, Json &out) noexcept
{
    out = nullptr;
    try {
        Json root;
        if (!json || !size || size > MODASSET_GLTF_DOCUMENT_LIMIT ||
                !Reader(json, size).read(root)) return false;
        out = std::move(root);
        return true;
    } catch (...) { return false; }
}

bool modAssetGltfReadDocument(const char *json, size_t size, Json &out) noexcept
{
    out = nullptr;
    try {
        Json root;
        if (!modAssetJsonReadValue(json, size, root) || !root.is_object() ||
                !root.contains("asset")) return false;
        const Json &asset = root["asset"];
        if (!asset.is_object() || !asset.contains("version") ||
                !asset["version"].is_string() ||
                asset["version"].get<crude_json::string>() != "2.0") return false;
        out = std::move(root);
        return true;
    } catch (...) { return false; }
}

extern "C" s32 modAssetGltfBufferDocument(const char *json, size_t json_size, s32 glb,
    char *out_uri, size_t uri_cap, u32 *out_declared_size)
{
    return bufferDocument(json, json_size, glb, true,
        out_uri, uri_cap, out_declared_size);
}

extern "C" s32 modAssetGltfOptionalBufferDocument(const char *json, size_t json_size, s32 glb,
    char *out_uri, size_t uri_cap, u32 *out_declared_size)
{
    return bufferDocument(json, json_size, glb, false,
        out_uri, uri_cap, out_declared_size);
}

extern "C" s32 modAssetGltfDocumentVisitStrings(const char *json, size_t json_size,
    modasset_gltf_string_visitor visitor, void *userdata)
{
    if (!visitor) return 0;
    try {
        Json root;
        const Json *uri;
        u32 declared;
        return document(json, json_size, false, false, root, uri, declared) &&
            visit(root, uri, declared, visitor, userdata);
    } catch (...) { return 0; }
}

namespace {
using Matrix = std::array<double, 16>;

Matrix identityMatrix()
{
    Matrix out{};
    out[0] = out[5] = out[10] = out[15] = 1;
    return out;
}

struct SceneNode {
    s32 mesh = -1;
    s32 parent = -1;
    std::vector<u32> children;
    Matrix local = identityMatrix();
    Matrix world = identityMatrix();
};

bool matrixMultiply(const Matrix &parent, const Matrix &local, Matrix &out)
{
    for (size_t column = 0; column < 4; column++) {
        for (size_t row = 0; row < 4; row++) {
            double value = 0;
            for (size_t k = 0; k < 4; k++)
                value += parent[k * 4 + row] * local[column * 4 + k];
            if (!std::isfinite(value)) return false;
            out[column * 4 + row] = value;
        }
    }
    return true;
}

bool numberArray(const Json &value, double *out, size_t count)
{
    if (!value.is_array() || value.get<crude_json::array>().size() != count) return false;
    for (size_t i = 0; i < count; i++) {
        const Json &element = value[i];
        if (!element.is_number()) return false;
        out[i] = element.get<crude_json::number>();
        if (!std::isfinite(out[i])) return false;
    }
    return true;
}

bool nodeMatrix(const Json &node, Matrix &out)
{
    if (node.contains("matrix")) {
        if (node.contains("translation") || node.contains("rotation") || node.contains("scale") ||
                !numberArray(node["matrix"], out.data(), 16)) return false;
        return out[3] == 0 && out[7] == 0 && out[11] == 0 && out[15] == 1;
    }
    double translation[3] = { 0, 0, 0 }, scale[3] = { 1, 1, 1 };
    double rotation[4] = { 0, 0, 0, 1 };
    if ((node.contains("translation") && !numberArray(node["translation"], translation, 3)) ||
            (node.contains("scale") && !numberArray(node["scale"], scale, 3)) ||
            (node.contains("rotation") && !numberArray(node["rotation"], rotation, 4))) return false;
    double norm = 0;
    for (double component : rotation) norm += component * component;
    if (!std::isfinite(norm) || std::fabs(norm - 1) > 1e-4) return false;
    const double inverse = 1 / std::sqrt(norm);
    const double x = rotation[0] * inverse, y = rotation[1] * inverse;
    const double z = rotation[2] * inverse, w = rotation[3] * inverse;
    out = identityMatrix();
    out[0] = (1 - 2 * (y * y + z * z)) * scale[0];
    out[1] = (2 * (x * y + z * w)) * scale[0];
    out[2] = (2 * (x * z - y * w)) * scale[0];
    out[4] = (2 * (x * y - z * w)) * scale[1];
    out[5] = (1 - 2 * (x * x + z * z)) * scale[1];
    out[6] = (2 * (y * z + x * w)) * scale[1];
    out[8] = (2 * (x * z + y * w)) * scale[2];
    out[9] = (2 * (y * z - x * w)) * scale[2];
    out[10] = (1 - 2 * (x * x + y * y)) * scale[2];
    out[12] = translation[0]; out[13] = translation[1]; out[14] = translation[2];
    for (double value : out) if (!std::isfinite(value)) return false;
    return true;
}

bool arrayField(const Json &root, const char *key, const crude_json::array *&out)
{
    out = nullptr;
    if (!root.contains(key)) return true;
    if (!root[key].is_array()) return false;
    out = &root[key].get<crude_json::array>();
    return out->size() <= 2147483647u;
}

bool reference(const Json &value, size_t count, u32 &index)
{
    return unsignedValue(value, index) && index < count;
}

modasset_gltf_scene_instance_t sceneInstance(s32 mesh, s32 node, const Matrix &world)
{
    modasset_gltf_scene_instance_t out{};
    out.mesh_index = mesh;
    out.node_index = node;
    std::copy(world.begin(), world.end(), out.world);
    double linear[9];
    for (size_t column = 0; column < 3; column++) {
        const double scale = std::max({ std::fabs(world[column * 4]),
            std::fabs(world[column * 4 + 1]), std::fabs(world[column * 4 + 2]) });
        if (scale == 0) return out;
        for (size_t row = 0; row < 3; row++) linear[column * 3 + row] = world[column * 4 + row] / scale;
    }
    const double determinant = linear[0] * (linear[4] * linear[8] - linear[7] * linear[5]) -
        linear[3] * (linear[1] * linear[8] - linear[7] * linear[2]) +
        linear[6] * (linear[1] * linear[5] - linear[4] * linear[2]);
    out.mirrored = determinant < 0;
    return out;
}

bool buildScenePlan(const Json &root, modasset_gltf_scene_plan_t &out, const char *&error)
{
    const crude_json::array *mesh_objects, *node_objects, *scenes;
    error = "gltf_scene_arrays_invalid";
    if (!arrayField(root, "meshes", mesh_objects) || !arrayField(root, "nodes", node_objects) ||
            !arrayField(root, "scenes", scenes)) return false;
    const size_t mesh_count = mesh_objects ? mesh_objects->size() : 0;
    const size_t node_count = node_objects ? node_objects->size() : 0;
    if (mesh_objects) for (const Json &mesh : *mesh_objects) if (!mesh.is_object()) return false;
    std::vector<SceneNode> nodes(node_count);
    error = "gltf_node_invalid";
    for (size_t i = 0; i < node_count; i++) {
        const Json &object = (*node_objects)[i];
        if (!object.is_object() || !nodeMatrix(object, nodes[i].local)) return false;
        u32 index;
        if (object.contains("mesh")) {
            if (!reference(object["mesh"], mesh_count, index)) return false;
            nodes[i].mesh = static_cast<s32>(index);
        }
        const crude_json::array *children;
        if (!arrayField(object, "children", children)) return false;
        if (children) for (const Json &child : *children) {
            if (!reference(child, node_count, index)) return false;
            if (nodes[index].parent >= 0) {
                error = "gltf_node_duplicate_child_or_multiple_parents";
                return false;
            }
            nodes[index].parent = static_cast<s32>(i);
            nodes[i].children.push_back(index);
        }
    }

    // A forest can be validated without recursive traversal or
    // imposing a depth cap on a legal, flat JSON node array.
    std::vector<u32> hierarchy_roots, queue;
    for (size_t i = 0; i < node_count; i++) if (nodes[i].parent < 0) {
        hierarchy_roots.push_back(static_cast<u32>(i));
        queue.push_back(static_cast<u32>(i));
    }
    for (size_t cursor = 0; cursor < queue.size(); cursor++) {
        const SceneNode &node = nodes[queue[cursor]];
        queue.insert(queue.end(), node.children.begin(), node.children.end());
    }
    if (queue.size() != node_count) {
        error = "gltf_node_cycle";
        return false;
    }

    s32 selected = -1;
    u32 selected_index = 0;
    error = "gltf_scene_selection_invalid";
    if (root.contains("scene") && (!scenes || !reference(root["scene"], scenes->size(), selected_index))) return false;
    if (scenes && !scenes->empty()) selected = static_cast<s32>(selected_index);
    std::vector<u32> selected_roots;
    if (scenes) {
        std::vector<s32> root_scene(node_count, -1);
        for (size_t i = 0; i < scenes->size(); i++) {
            const Json &scene = (*scenes)[i];
            const crude_json::array *roots;
            if (!scene.is_object() || !arrayField(scene, "nodes", roots)) return false;
            if (roots) for (const Json &root_node : *roots) {
                u32 index;
                if (!reference(root_node, node_count, index) || nodes[index].parent >= 0 ||
                        root_scene[index] == static_cast<s32>(i)) return false;
                root_scene[index] = static_cast<s32>(i);
                if (selected == static_cast<s32>(i)) selected_roots.push_back(index);
            }
        }
    } else selected_roots = hierarchy_roots;

    std::vector<modasset_gltf_scene_instance_t> instances;
    if (!scenes && !node_objects) {
        for (size_t i = 0; i < mesh_count; i++)
            instances.push_back(sceneInstance(static_cast<s32>(i), -1, identityMatrix()));
    } else {
        // Stack reversal preserves authored root/child order, including repeated
        // mesh references through distinct nodes.
        std::vector<u32> pending(selected_roots.rbegin(), selected_roots.rend());
        while (!pending.empty()) {
            const u32 index = pending.back();
            pending.pop_back();
            SceneNode &node = nodes[index];
            const Matrix parent = node.parent >= 0 ? nodes[static_cast<size_t>(node.parent)].world : identityMatrix();
            if (!matrixMultiply(parent, node.local, node.world)) {
                error = "gltf_node_world_transform_overflow";
                return false;
            }
            if (node.mesh >= 0) instances.push_back(sceneInstance(node.mesh, static_cast<s32>(index), node.world));
            pending.insert(pending.end(), node.children.rbegin(), node.children.rend());
        }
    }
    error = "gltf_scene_allocation_failed";
    if (!instances.empty()) {
        out.instances = static_cast<modasset_gltf_scene_instance_t *>(std::malloc(instances.size() * sizeof(*out.instances)));
        if (!out.instances) return false;
        std::copy(instances.begin(), instances.end(), out.instances);
    }
    out.count = static_cast<u32>(instances.size());
    out.mesh_count = static_cast<u32>(mesh_count);
    out.node_count = static_cast<u32>(node_count);
    out.selected_scene = selected;
    return true;
}
} // namespace

extern "C" s32 modAssetGltfScenePlanBuild(const char *json, size_t json_size,
    modasset_gltf_scene_plan_t *out, char *error, size_t error_cap)
{
    if (error && error_cap) error[0] = '\0';
    if (!out) return 0;
    *out = {};
    out->selected_scene = -1;
    const char *reason = "gltf_document_invalid";
    try {
        Json root;
        if (modAssetGltfReadDocument(json, json_size, root) && buildScenePlan(root, *out, reason)) return 1;
    } catch (...) { reason = "gltf_scene_allocation_failed"; }
    modAssetGltfScenePlanFree(out);
    if (error && error_cap) std::snprintf(error, error_cap, "%s", reason);
    return 0;
}

extern "C" void modAssetGltfScenePlanFree(modasset_gltf_scene_plan_t *plan)
{
    if (!plan) return;
    std::free(plan->instances);
    *plan = {};
    plan->selected_scene = -1;
}

extern "C" s32 modAssetGltfSceneTransformPoint(const modasset_gltf_scene_instance_t *instance,
    const float source[3], float out[3])
{
    if (!instance || !source || !out) return 0;
    for (size_t i = 0; i < 3; i++) if (!std::isfinite(source[i])) return 0;
    float candidate[3];
    for (size_t row = 0; row < 3; row++) {
        double value = instance->world[12 + row];
        for (size_t k = 0; k < 3; k++) value += instance->world[k * 4 + row] * source[k];
        if (!std::isfinite(value) || value < -FLT_MAX || value > FLT_MAX) return 0;
        candidate[row] = static_cast<float>(value);
    }
    std::copy(candidate, candidate + 3, out);
    return 1;
}
