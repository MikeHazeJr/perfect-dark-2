#include "forge_room_builder.h"

#include "assetcatalog_scanner.h"
#include "modarchive.h"
#include "scenario_portal_source.h"
#include "modasset_gltf_document.h"
#include "modasset_gltf_source.h"
#include "sha256.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <locale>
#include <memory>
#include <sstream>

struct forge_room_extension {
    static constexpr u32 GeometryMembers = 3;
    std::array<std::string, GeometryMembers + 1> paths;
    std::array<std::string, GeometryMembers + 1> source;
    std::array<asset_archive_clone_override_t, GeometryMembers + 1> entries;
    u32 count = GeometryMembers;
};

namespace {

using Json = crude_json::value;
using Point = std::array<double, 3>;

constexpr const char *kScenario = "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario";
constexpr const char *kWaypoints = "navigation/waypoints.json";
constexpr const char *kWaygroups = "navigation/waygroups.json";
constexpr char kTemplateSha256[] = "2c420d72d99bffd8daaa63e3b50b713975a2ed227f06ea897fdee586822cfad8";
static_assert(sizeof(kTemplateSha256) == SHA256_HEX_SIZE);

enum SourceMember { Scene, Collision, Rooms, Portals, Pads, Volumes };

constexpr const char *kMembers[] = {
    "scene.glb", "collision.obj", "rooms.json", "portals.json", "pads.json", "volumes.json"
};

bool fail(char *err, size_t cap, const char *reason)
{
    if (err && cap) snprintf(err, cap, "%s", reason);
    return false;
}

double mapApertureAxis(double value, double aperture, double outer,
        double newAperture, double newOuter)
{
    return value <= aperture ? value * (newAperture / aperture)
        : newAperture + (value - aperture) * ((newOuter - newAperture) / (outer - aperture));
}

struct RoomMapping {
    const ForgeRoomSize &size;

    bool map(Point &point) const
    {
        using Room = ForgeRoomSize;
        const double length = point[0] < 0 ? size.firstLength : size.secondLength;
        point[0] *= length / Room::TemplateLength;
        /* Keep the floor, ceiling, outer walls and doorway on their exact
         * shared planes. Moving pads through the same map keeps the two
         * cross-room path links inside the opening. */
        point[1] = size.doorwayHeight ? mapApertureAxis(point[1], Room::TemplateDoorwayHeight,
            Room::TemplateHeight, size.doorwayHeight, size.height)
            : point[1] * (static_cast<double>(size.height) / Room::TemplateHeight);
        point[2] = size.doorwayWidth ? std::copysign(mapApertureAxis(std::abs(point[2]),
            Room::TemplateDoorwayWidth * 0.5, Room::TemplateWidth * 0.5,
            size.doorwayWidth * 0.5, size.width * 0.5), point[2])
            : point[2] * (static_cast<double>(size.width) / Room::TemplateWidth);
        for (double coordinate : point) {
            if (!std::isfinite(coordinate) || std::abs(coordinate) > 32767) return false;
        }
        return true;
    }
};

bool readPoint(const Json &value, Point &out)
{
    if (!value.is_array()) return false;
    const auto &parts = value.get<crude_json::array>();
    if (parts.size() != out.size()) return false;
    for (size_t i = 0; i < parts.size(); i++) {
        if (!parts[i].is_number()) return false;
        out[i] = parts[i].get<double>();
    }
    return true;
}

bool mapVector(Json &value, const RoomMapping &mapping, const Point *relativeTo = nullptr)
{
    Point point;
    if (!readPoint(value, point)) return false;
    Point mappedOrigin = {};
    if (relativeTo) {
        mappedOrigin = *relativeTo;
        if (!mapping.map(mappedOrigin)) return false;
        for (size_t i = 0; i < point.size(); i++) point[i] += (*relativeTo)[i];
    }
    if (!mapping.map(point)) return false;
    auto &parts = value.get<crude_json::array>();
    for (size_t i = 0; i < point.size(); i++) parts[i] = point[i] - mappedOrigin[i];
    return true;
}

bool mapFields(Json &row, const RoomMapping &mapping, std::initializer_list<const char *> fields,
        const Point *relativeTo = nullptr)
{
    if (!row.is_object()) return false;
    for (const char *field : fields) {
        if (!row.contains(field) || !mapVector(row[field], mapping, relativeTo)) return false;
    }
    return true;
}

bool mapTable(std::string &source, const RoomMapping &mapping, size_t member)
{
    Json root;
    if (!modAssetJsonReadValue(source.data(), source.size(), root)
            || !root.is_object() || !root.contains("rows") || !root["rows"].is_array()) return false;
    for (Json &row : root["rows"].get<crude_json::array>()) {
        switch (member) {
        case Rooms:
            if (!mapFields(row, mapping, {"origin", "min", "max"})) return false;
            break;
        case Portals:
            if (!row.is_object() || !row.contains("vertices") || !row["vertices"].is_array()) return false;
            for (Json &vertex : row["vertices"].get<crude_json::array>()) {
                if (!mapVector(vertex, mapping)) return false;
            }
            break;
        case Pads: {
            Point originalPosition;
            if (!row.is_object() || !row.contains("position") || !row.contains("bbox")
                    || !readPoint(row["position"], originalPosition)
                    || !mapVector(row["position"], mapping)
                    || !mapFields(row["bbox"], mapping, {"min", "max"}, &originalPosition)) return false;
            break;
        }
        case Volumes:
            if (!mapFields(row, mapping, {"min", "max"})) return false;
            break;
        }
    }
    source = root.dump(2);
    return true;
}

bool mapCollision(std::string &source, const RoomMapping &mapping)
{
    std::istringstream original(source);
    std::ostringstream changed;
    changed.imbue(std::locale::classic());
    for (std::string line; std::getline(original, line);) {
        if (line.compare(0, 2, "v ") == 0) {
            std::istringstream point(line.substr(2));
            point.imbue(std::locale::classic());
            Point vertex;
            if (!(point >> vertex[0] >> vertex[1] >> vertex[2]) || !mapping.map(vertex)) return false;
            changed << "v " << vertex[0] << ' ' << vertex[1] << ' ' << vertex[2] << '\n';
        } else {
            changed << line << '\n';
        }
    }
    source = changed.str();
    return true;
}

void appendU32(std::string &out, u32 value)
{
    for (s32 shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 255u));
    }
}

void writeScene(std::string &source, const Json &root, const std::string &buffer)
{
    std::string json = root.dump();
    while (json.size() % 4) json.push_back(' ');
    std::string rebuilt("glTF");
    appendU32(rebuilt, 2);
    appendU32(rebuilt, static_cast<u32>(28 + json.size() + buffer.size()));
    appendU32(rebuilt, static_cast<u32>(json.size()));
    rebuilt += "JSON";
    rebuilt += json;
    appendU32(rebuilt, static_cast<u32>(buffer.size()));
    rebuilt.append("BIN\0", 4);
    rebuilt += buffer;
    source = std::move(rebuilt);
}

bool mapScene(std::string &source, const RoomMapping &mapping)
{
    modasset_gltf_glb_chunks_t glb = {};
    if (!modAssetGltfGlbChunks(reinterpret_cast<const u8 *>(source.data()),
            static_cast<u32>(source.size()), &glb)) return false;
    Json root;
    if (!modAssetGltfReadDocument(reinterpret_cast<const char *>(glb.json), glb.json_size, root)) return false;
    std::string buffer(reinterpret_cast<const char *>(glb.bin), glb.bin_size);

    /* The exact original template has two non-interleaved float POSITION
     * streams. Leave its UVs, room tags, colors and embedded textures intact. */
    auto &accessors = root["accessors"].get<crude_json::array>();
    const auto &views = root["bufferViews"].get<crude_json::array>();
    for (const Json &mesh : root["meshes"].get<crude_json::array>()) {
        for (const Json &primitive : mesh["primitives"].get<crude_json::array>()) {
            const size_t index = static_cast<size_t>(primitive["attributes"]["POSITION"].get<double>());
            if (index >= accessors.size()) return false;
            Json &accessor = accessors[index];
            const size_t viewIndex = static_cast<size_t>(accessor["bufferView"].get<double>());
            if (viewIndex >= views.size()) return false;
            const Json &view = views[viewIndex];
            const u32 offset = static_cast<u32>(view["byteOffset"].get<double>());
            const u32 bytes = static_cast<u32>(view["byteLength"].get<double>());
            const u32 count = static_cast<u32>(accessor["count"].get<double>());
            constexpr u32 vertexSize = 3 * sizeof(f32);
            if (!modAssetGltfAccessorBounds(glb.bin_size, offset, bytes, 0, count, vertexSize, vertexSize)
                    || !mapFields(accessor, mapping, {"min", "max"})) return false;
            for (u32 vertex = 0; vertex < count; vertex++) {
                char *position = buffer.data() + offset + vertex * vertexSize;
                f32 coordinates[3];
                memcpy(coordinates, position, sizeof(coordinates));
                Point point = {coordinates[0], coordinates[1], coordinates[2]};
                if (!mapping.map(point)) return false;
                for (size_t axis = 0; axis < point.size(); axis++) {
                    coordinates[axis] = static_cast<f32>(point[axis]);
                }
                memcpy(position, coordinates, sizeof(coordinates));
            }
        }
    }

    writeScene(source, root, buffer);
    return true;
}

/* Fingerprints of the bundled Grid seed's canonical GLB JSON, BIN chunk,
 * and OBJ vertex/face stream. Normalizing only the known room planes allows
 * both the seed and rooms already resized by the original builder. */
constexpr char kSceneLayoutSha[] = "a77ce1d0f49fa4240bc07dcd9070c5354e0a192f8363c57aaa4f268253dbbad1";
constexpr char kSceneBufferSha[] = "7f8b43f72fb08c16d8750119152a2973cba08c3d6cb1d925b9d1bf042ebc0726";
constexpr char kCollisionShapeSha[] = "2d9d64800ad60a42e1cf35bba107903d9d930615f88a753129aee275fee0bf87";
constexpr u32 kArenaLimit = 128u * 1024u * 1024u;
constexpr u32 kRoomSourceLimit = 1024u * 1024u;

bool bytesMatch(const std::string &bytes, const char *expected)
{
    u8 digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_SIZE];
    sha256Hash(bytes.data(), bytes.size(), digest);
    sha256ToHex(digest, hex);
    return strcmp(hex, expected) == 0;
}

bool near(double a, double b, double precision = 0.001)
{
    return std::abs(a - b) <= precision;
}

bool pointMatches(const Point &a, const Point &b)
{
    return near(a[0], b[0]) && near(a[1], b[1]) && near(a[2], b[2]);
}

bool readPointField(const Json &row, const char *key, Point &point)
{
    return row.is_object() && row.contains(key) && readPoint(row[key], point);
}

bool isText(const Json &row, const char *key, const char *text)
{
    return row.is_object() && row.contains(key) && row[key].is_string()
        && row[key].get<std::string>() == text;
}

struct RoomShape {
    std::array<double, 2> length;
    double halfWidth;
    double height;
    double doorHalfWidth;
    double doorHeight;

    double wall(size_t index) const { return index == 0 ? -length[index] : length[index]; }
    Point low(size_t index) const { return {index == 0 ? wall(index) : 0, 0, -halfWidth}; }
    Point high(size_t index) const { return {index == 0 ? 0 : wall(index), height, halfWidth}; }

    bool toTemplate(Point &point, size_t room, double precision) const
    {
        using Size = ForgeRoomSize;
        const double templateWall = room == 0 ? -Size::TemplateLength : Size::TemplateLength;
        const double planes[3][4] = {
            {0, wall(room)},
            {0, doorHeight, height},
            {-halfWidth, -doorHalfWidth, doorHalfWidth, halfWidth},
        };
        const double canonical[3][4] = {
            {0, templateWall},
            {0, Size::TemplateDoorwayHeight, Size::TemplateHeight},
            {-Size::TemplateWidth / 2, -Size::TemplateDoorwayWidth / 2,
                Size::TemplateDoorwayWidth / 2, Size::TemplateWidth / 2},
        };
        for (size_t axis = 0; axis < point.size(); ++axis) {
            size_t plane = 0;
            while (plane < axis + 2 && !near(point[axis], planes[axis][plane], precision)) ++plane;
            if (plane == axis + 2) return false;
            point[axis] = canonical[axis][plane];
        }
        return true;
    }
};

bool bounded(double value, double low, double high)
{
    return value >= low && value <= high;
}

bool readRoomShape(const std::string &source, Json &rooms, RoomShape &shape)
{
    if (!modAssetJsonReadValue(source.data(), source.size(), rooms)
            || !isText(rooms, "schema", "pd2.scenario.rooms.v1")
            || !rooms.contains("rows") || !rooms["rows"].is_array()) return false;
    const auto &rows = rooms["rows"].get<crude_json::array>();
    if (rows.size() != 2) return false;

    Point lo[2], hi[2], origin[2];
    for (size_t i = 0; i < 2; ++i) {
        const auto &row = rows[i];
        if (!isText(row, "room", i == 0 ? "room_1" : "room_2")
                || !row.contains("roomnum") || !row["roomnum"].is_number()
                || row["roomnum"].get<double>() != i + 1
                || !readPointField(row, "min", lo[i]) || !readPointField(row, "max", hi[i])
                || !readPointField(row, "origin", origin[i])) return false;
    }
    shape.length = {std::round(-lo[0][0]), std::round(hi[1][0])};
    shape.halfWidth = hi[0][2];
    shape.height = hi[0][1];
    using Size = ForgeRoomSize;
    if (!bounded(shape.halfWidth * 2.0, Size::MinWidth, Size::MaxWidth)
            || !bounded(shape.height, Size::MinHeight, Size::MaxHeight)) return false;
    for (size_t i = 0; i < 2; ++i) {
        if (!bounded(shape.length[i], Size::MinLength, Size::MaxLength)
                || !pointMatches(lo[i], shape.low(i)) || !pointMatches(hi[i], shape.high(i))
                || !near(origin[i][0], shape.wall(i) * 0.5)
                || !bounded(origin[i][1], 0, shape.height) || !near(origin[i][2], 0)) return false;
    }
    return true;
}

bool readBrightnessPair(const Json &value)
{
    if (!value.is_array()) return false;
    const auto &pair = value.get<crude_json::array>();
    if (pair.size() != 2 || !pair[0].is_number() || !pair[1].is_number()) return false;
    const double minimum = pair[0].get<double>();
    const double maximum = pair[1].get<double>();
    return bounded(minimum, 0, 255) && bounded(maximum, minimum, 255)
        && std::trunc(minimum) == minimum && std::trunc(maximum) == maximum;
}

bool applyDraftRoomBrightness(const std::string &pinned, Json &rooms, const char *draftText, size_t draftSize)
{
    if (!draftSize) return true;
    if (!draftText || draftSize > kRoomSourceLimit) return false;
    if (draftSize == pinned.size() && memcmp(draftText, pinned.data(), draftSize) == 0) return true;

    Json draft;
    if (!modAssetJsonReadValue(draftText, draftSize, draft)
            || !draft.is_object() || !draft.contains("rows") || !draft["rows"].is_array()) return false;
    auto &draftRows = draft["rows"].get<crude_json::array>();
    std::array<Json, 2> brightness;
    if (draftRows.size() != brightness.size()) return false;

    Json geometry = rooms;
    auto &geometryRows = geometry["rows"].get<crude_json::array>();
    for (size_t i = 0; i < draftRows.size(); ++i) {
        auto &row = draftRows[i];
        if (!row.is_object()) return false;
        if (row.contains("brightness")) {
            if (!readBrightnessPair(row["brightness"])) return false;
            brightness[i] = row["brightness"];
            row.erase("brightness");
        }
        geometryRows[i].erase("brightness");
    }
    if (draft.dump() != geometry.dump()) return false;

    auto &rows = rooms["rows"].get<crude_json::array>();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (brightness[i].is_null()) rows[i].erase("brightness");
        else rows[i]["brightness"] = std::move(brightness[i]);
    }
    return true;
}

bool readDoorShape(const std::string &source, RoomShape &shape)
{
    scenario_source_portal_row_t *data = nullptr;
    s32 count = 0;
    if (!scenarioPortalSourceRead(source.data(), source.size(), &data, &count, nullptr, 0)) return false;
    const std::unique_ptr<scenario_source_portal_row_t, decltype(&free)> rows(data, free);
    if (count != 1 || !scenarioPortalSourceCheckCandidate(rows.get(), count, 3, nullptr, 0)) return false;
    const auto &portal = data[0];
    if (portal.room1 != 1 || portal.room2 != 2 || portal.vertex_count != 4) return false;
    using Size = ForgeRoomSize;
    shape.doorHalfWidth = portal.vertices[0][2];
    shape.doorHeight = portal.vertices[2][1];
    if (!bounded(shape.doorHalfWidth * 2.0, Size::MinDoorwayWidth, 2.0 * (shape.halfWidth - Size::MinSideWallWidth))
            || !bounded(shape.doorHeight, Size::MinDoorwayHeight, shape.height - Size::MinLintelHeight)) return false;
    const Point corners[4] = {
        {0, 0, shape.doorHalfWidth}, {0, 0, -shape.doorHalfWidth},
        {0, shape.doorHeight, -shape.doorHalfWidth}, {0, shape.doorHeight, shape.doorHalfWidth},
    };
    for (size_t i = 0; i < 4; ++i) {
        if (!pointMatches({portal.vertices[i][0], portal.vertices[i][1], portal.vertices[i][2]}, corners[i])) return false;
    }
    return true;
}

bool extendScene(std::string &source, const RoomShape &shape, size_t room, double wall)
{
    modasset_gltf_glb_chunks_t glb = {};
    if (!modAssetGltfGlbChunks(reinterpret_cast<const u8 *>(source.data()),
            static_cast<u32>(source.size()), &glb) || glb.bin_size != 10760) return false;
    Json root;
    if (!modAssetGltfReadDocument(reinterpret_cast<const char *>(glb.json), glb.json_size, root)
            || !root.contains("accessors") || !root["accessors"].is_array()
            || root["accessors"].get<crude_json::array>().size() != 8) return false;
    auto &accessors = root["accessors"].get<crude_json::array>();
    const Json originalBounds[2] = {accessors[0], accessors[4]};

    /* The normalized JSON check proves the layout and interpretation of
     * every byte we touch before using the seed's known POSITION offsets. */
    using Size = ForgeRoomSize;
    for (size_t i = 0; i < 2; ++i) {
        auto &accessor = accessors[i * 4];
        Point lo, hi;
        if (!readPointField(accessor, "min", lo) || !pointMatches(lo, shape.low(i))
                || !readPointField(accessor, "max", hi) || !pointMatches(hi, shape.high(i))) return false;
        accessor["min"] = crude_json::array{i == 0 ? -static_cast<double>(Size::TemplateLength) : 0.0,
            0.0, -Size::TemplateWidth * 0.5};
        accessor["max"] = crude_json::array{i == 0 ? 0.0 : static_cast<double>(Size::TemplateLength),
            static_cast<double>(Size::TemplateHeight), Size::TemplateWidth * 0.5};
    }
    if (!bytesMatch(root.dump(), kSceneLayoutSha)) return false;

    std::string buffer(reinterpret_cast<const char *>(glb.bin), glb.bin_size);
    std::string normalized = buffer;
    constexpr size_t stride = 3 * sizeof(f32);
    constexpr size_t offsets[2] = {0, 1824};
    for (size_t i = 0; i < 2; ++i) {
        for (size_t vertex = 0; vertex < 48; ++vertex) {
            const size_t offset = offsets[i] + vertex * stride;
            f32 xyz[3];
            memcpy(xyz, buffer.data() + offset, stride);
            Point point = {xyz[0], xyz[1], xyz[2]};
            if (!shape.toTemplate(point, i, 0.001)) return false;
            for (size_t axis = 0; axis < point.size(); ++axis) xyz[axis] = static_cast<f32>(point[axis]);
            memcpy(normalized.data() + offset, xyz, stride);
            if (i == room && point[0] != 0) {
                const auto wallX = static_cast<f32>(wall);
                memcpy(buffer.data() + offset, &wallX, sizeof(wallX));
            }
        }
    }
    if (!bytesMatch(normalized, kSceneBufferSha)) return false;

    accessors[0] = originalBounds[0];
    accessors[4] = originalBounds[1];
    accessors[room * 4][room == 0 ? "min" : "max"].get<crude_json::array>()[0] = wall;
    writeScene(source, root, buffer);
    return true;
}

bool extendCollision(std::string &source, const RoomShape &shape, size_t room, double wall)
{
    std::istringstream input(source);
    std::ostringstream output, normalized;
    output.imbue(std::locale::classic());
    normalized.imbue(std::locale::classic());
    size_t currentRoom = 2;
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "o room_1" || line == "o room_2") {
            currentRoom = line == "o room_1" ? 0 : 1;
            normalized << line << '\n';
        } else if (line.compare(0, 2, "v ") == 0) {
            std::istringstream vertex(line.substr(2));
            vertex.imbue(std::locale::classic());
            Point point;
            std::string remainder;
            if (currentRoom > 1 || !(vertex >> point[0] >> point[1] >> point[2])
                    || (vertex >> remainder) || !shape.toTemplate(point, currentRoom, 0.01)) return false;
            normalized << "v " << point[0] << ' ' << point[1] << ' ' << point[2] << '\n';
            if (currentRoom == room && point[0] != 0) {
                const size_t first = line.find_first_not_of(" \t", 2);
                const size_t end = line.find_first_of(" \t", first);
                line.replace(first, end - first, std::to_string(static_cast<s32>(wall)));
            }
        } else if (line.compare(0, 2, "f ") == 0) {
            normalized << line << '\n';
        } else if (!line.empty() && line[0] != '#') {
            return false;
        }
        output << line << '\n';
    }
    if (!bytesMatch(normalized.str(), kCollisionShapeSha)) return false;
    source = output.str();
    return true;
}

bool conventionalSourcePaths(const std::string &descriptor, const std::string &graph, bool &conventionalNavigation)
{
    const auto ini = std::make_unique<ini_section_t>();
    if (!iniParseText(descriptor.data(), static_cast<u32>(descriptor.size()), ini.get(), nullptr, 0)
            || strcmp(ini->type, "scenario") != 0) return false;
    const char *const paths[][2] = {
        {"source_room_count", "3"}, {"scene_file", "scene.glb"}, {"runtime_source_file", "scene.glb"},
        {"collision_source", "collision.obj"}, {"collision_fallback", "override"}, {"portals_file", "portals.json"},
        {"level_graph_file", "level.graph.json"}, {"paths_file", "navigation/paths.json"},
    };
    for (const auto &pair : paths) {
        s32 count = 0;
        for (s32 i = 0; i < ini->count; ++i) {
            if (strcmp(ini->pairs[i].key, pair[0]) != 0) continue;
            if (strcmp(ini->pairs[i].value, pair[1]) != 0) return false;
            ++count;
        }
        if (count != 1) return false;
    }
    Json root;
    if (!modAssetJsonReadValue(graph.data(), graph.size(), root) || !isText(root, "source", "scene.glb")
            || !root.contains("tables") || !isText(root["tables"], "portals", "portals.json")
            || !isText(root["tables"], "paths", "navigation/paths.json")
            || !root.contains("nodes") || !root["nodes"].is_array()) return false;
    conventionalNavigation = isText(root["tables"], "waypoints", kWaypoints)
        && isText(root["tables"], "waygroups", kWaygroups);
    s32 scene = 0, collision = 0;
    for (const auto &node : root["nodes"].get<crude_json::array>()) {
        if (isText(node, "kind", "scenario.scene.source")) {
            if (!isText(node, "file", "scene.glb")) return false;
            ++scene;
        } else if (isText(node, "kind", "scenario.collision.generate")) {
            if (!isText(node, "source", "collision.obj")) return false;
            ++collision;
        }
    }
    return scene == 1 && collision == 1;
}

struct MemberCheck {
    const char *name;
    u32 sizeLimit;
    u32 matches;
    bool allowed;
};

void *readBoundedMember(const void *archive, u32 size, const char *name, u32 limit, u32 &outSize)
{
    MemberCheck check = {name, limit, 0, false};
    if (modArchiveMemForEachEntry(archive, size, [](const char *entry, u32 bytes, void *state) -> s32 {
            auto &match = *static_cast<MemberCheck *>(state);
            if (strcmp(entry, match.name) == 0) {
                ++match.matches;
                match.allowed = bytes > 0 && bytes <= match.sizeLimit;
            }
            return 0;
        }, &check) != MODARCHIVE_OK || check.matches != 1 || !check.allowed) return nullptr;
    return modArchiveExtractMemAlloc(archive, size, name, &outSize);
}

bool readRoomSource(const void *scenario, u32 size, const char *name, std::string &out)
{
    u32 bytes = 0;
    const std::unique_ptr<void, decltype(&free)> data(readBoundedMember(scenario, size,
        name, kRoomSourceLimit, bytes), free);
    if (!data) return false;
    out.assign(static_cast<const char *>(data.get()), bytes);
    return true;
}

bool readNavigationRows(const std::string &text, const char *schema, Json &table)
{
    return modAssetJsonReadValue(text.data(), text.size(), table) && isText(table, "schema", schema)
        && table.contains("rows") && table["rows"].is_array();
}

bool appendGeneratedWaypointPolicy(const void *scenario, u32 size, const char *scenarioMember,
        forge_room_extension &result)
{
    const u32 index = result.count;
    std::string &source = result.source[index];
    std::string groups;
    Json waypoints, waygroups;
    if (!readRoomSource(scenario, size, kWaypoints, source)
            || !readRoomSource(scenario, size, kWaygroups, groups)
            || !readNavigationRows(source, "pd2.scenario.waypoints.v1", waypoints)
            || !readNavigationRows(groups, "pd2.scenario.waygroups.v1", waygroups)) return false;
    if (!waypoints["rows"].get<crude_json::array>().empty()
            || !waygroups["rows"].get<crude_json::array>().empty()) return true;

    const char key[] = "only_linked_pads";
    bool enabled = false;
    if (waypoints.contains(key)) {
        if (!waypoints[key].is_boolean()) return false;
        enabled = waypoints[key].get<crude_json::boolean>();
        if (!enabled) {
            u32 length = static_cast<u32>(source.size());
            if (!assetArchiveCloneJsonDropTopMember(source.data(), &length, key)) return false;
            source.resize(length);
        }
    }
    if (!enabled) source.insert(source.find('{') + 1, "\n  \"only_linked_pads\": true,");
    result.paths[index] = std::string(scenarioMember) + "::" + kWaypoints;
    ++result.count;
    return true;
}

} // namespace

bool forgeRoomBuildOverrides(const char *templatePath, const ForgeRoomSize &size,
        ForgeRoomOverrides &out, char *err, size_t errCap)
{
    if (err && errCap) err[0] = '\0';
    using Room = ForgeRoomSize;
    if (size.firstLength < Room::MinLength || size.firstLength > Room::MaxLength
            || size.secondLength < Room::MinLength || size.secondLength > Room::MaxLength
            || size.width < Room::MinWidth || size.width > Room::MaxWidth
            || size.height < Room::MinHeight || size.height > Room::MaxHeight) {
        return fail(err, errCap, "Each room must be 10–40 m long, 12–40 m wide and 4–10 m high.");
    }
    const double doorwayWidth = size.effectiveDoorwayWidth();
    const double doorwayHeight = size.effectiveDoorwayHeight();
    if (doorwayWidth < Room::MinDoorwayWidth || doorwayWidth > size.width - 2 * Room::MinSideWallWidth
            || doorwayHeight < Room::MinDoorwayHeight || doorwayHeight > size.height - Room::MinLintelHeight) {
        return fail(err, errCap, "The doorway must be at least 3 m wide and 2.5 m high, "
            "with 1.5 m of wall on each side and 0.8 m above.");
    }
    if (!templatePath || sha256VerifyFile(templatePath, kTemplateSha256) != 1) {
        return fail(err, errCap, "The original Grid Test Rooms template is missing or has changed.");
    }
    try {
        const RoomMapping mapping = {size};
        std::unique_ptr<mod_archive_t, decltype(&modArchiveClose)> archive(modArchiveOpen(templatePath), modArchiveClose);
        if (!archive) return fail(err, errCap, "The room template could not be opened.");
        u32 scenarioSize = 0;
        std::unique_ptr<void, decltype(&free)> scenario(modArchiveExtractAlloc(archive.get(),
            modArchiveFindEntry(archive.get(), kScenario), &scenarioSize), free);
        if (!scenario) return fail(err, errCap, "The room template has no scenario.");

        for (size_t i = 0; i < out.entries.size(); i++) {
            u32 bytes = 0;
            std::unique_ptr<void, decltype(&free)> data(modArchiveExtractMemAlloc(
                scenario.get(), scenarioSize, kMembers[i], &bytes), free);
            if (!data) return fail(err, errCap, "A room template source member could not be read.");
            out.paths[i] = std::string(kScenario) + "::" + kMembers[i];
            out.source[i].assign(static_cast<const char *>(data.get()), bytes);
            auto &member = out.source[i];
            const bool valid = i == Scene ? mapScene(member, mapping)
                : i == Collision ? mapCollision(member, mapping) : mapTable(member, mapping, i);
            if (!valid) {
                if (err && errCap) snprintf(err, errCap, "Could not build %s with these dimensions.", kMembers[i]);
                return false;
            }
            out.entries[i] = {out.paths[i].c_str(), member.data(), static_cast<u32>(member.size()), 0};
        }
        return true;
    } catch (const std::bad_alloc &) {
        return fail(err, errCap, "The room builder ran out of memory.");
    }
}

extern "C" forge_room_extension_t *forgeRoomCreateExtensionWithRoomBrightness(const void *arenaBytes, u32 arenaSize,
        const char *scenarioMember, s32 room, s32 extraLength, const char *draftRooms,
        size_t draftRoomsLength, char *err, size_t cap)
{
    if (err && cap) err[0] = '\0';
    const auto refuse = [err, cap](const char *message) -> forge_room_extension_t * {
        fail(err, cap, message);
        return nullptr;
    };
    if (!arenaBytes || !arenaSize || arenaSize > kArenaLimit || !scenarioMember || !scenarioMember[0]
            || room < 1 || room > 2 || extraLength <= 0 || extraLength > ForgeRoomSize::MaxLength) {
        return refuse("Choose a room and a positive extension within the room builder's range.");
    }
    try {
        u32 scenarioSize = 0;
        const std::unique_ptr<void, decltype(&free)> scenario(
            readBoundedMember(arenaBytes, arenaSize, scenarioMember, kArenaLimit, scenarioSize), free);
        if (!scenario) return refuse("The pinned arena's exact scenario could not be read.");
        auto result = std::make_unique<forge_room_extension>();
        for (u32 i = 0; i < result->count; ++i) {
            if (!readRoomSource(scenario.get(), scenarioSize, kMembers[i], result->source[i])) {
                return refuse("A native room source member is missing or too large.");
            }
            result->paths[i] = std::string(scenarioMember) + "::" + kMembers[i];
        }
        std::string portals, descriptor, graph;
        bool conventionalNavigation = false;
        if (!readRoomSource(scenario.get(), scenarioSize, "portals.json", portals)
                || !readRoomSource(scenario.get(), scenarioSize, "scenario.ini", descriptor)
                || !readRoomSource(scenario.get(), scenarioSize, "level.graph.json", graph)
                || !conventionalSourcePaths(descriptor, graph, conventionalNavigation)) {
            return refuse("The arena does not use the standard two-room native sources.");
        }
        Json rooms;
        RoomShape shape = {};
        if (!readRoomShape(result->source[Rooms], rooms, shape) || !readDoorShape(portals, shape)) {
            return refuse("Only the original Grid two-room layout can extend an outer wall.");
        }
        if (!applyDraftRoomBrightness(result->source[Rooms], rooms, draftRooms, draftRoomsLength)) {
            return refuse("The draft room table must match the pinned layout apart from valid brightness ranges.");
        }
        const size_t index = static_cast<size_t>(room - 1);
        if (shape.length[index] + extraLength > ForgeRoomSize::MaxLength) {
            return refuse("The extended room would exceed 40 m.");
        }
        const double newWall = shape.wall(index) + (index == 0 ? -extraLength : extraLength);
        if (!extendScene(result->source[Scene], shape, index, newWall)
                || !extendCollision(result->source[Collision], shape, index, newWall)) {
            return refuse("The arena's render or collision geometry is outside the Grid room builder shape.");
        }
        auto &row = rooms["rows"].get<crude_json::array>()[index];
        row[index == 0 ? "min" : "max"].get<crude_json::array>()[0] = newWall;
        row["origin"].get<crude_json::array>()[0] = newWall * 0.5;
        result->source[Rooms] = rooms.dump(2);
        if (!modAssetJsonReadValue(result->source[Rooms].data(), result->source[Rooms].size(), rooms)) {
            return refuse("The resized native room table could not be written.");
        }
        if (conventionalNavigation && !appendGeneratedWaypointPolicy(scenario.get(), scenarioSize, scenarioMember, *result)) {
            return refuse("The arena's native waypoint sources are unreadable or invalid.");
        }
        for (u32 i = 0; i < result->count; ++i) {
            const auto &source = result->source[i];
            result->entries[i] = {result->paths[i].c_str(), source.data(), static_cast<u32>(source.size()), 0};
        }
        return result.release();
    } catch (const std::bad_alloc &) {
        return refuse("The room builder ran out of memory.");
    }
}

extern "C" forge_room_extension_t *forgeRoomCreateExtension(const void *arenaBytes, u32 arenaSize,
        const char *scenarioMember, s32 room, s32 extraLength, char *err, size_t cap)
{
    return forgeRoomCreateExtensionWithRoomBrightness(arenaBytes, arenaSize, scenarioMember, room,
            extraLength, nullptr, 0, err, cap);
}

extern "C" const asset_archive_clone_override_t *forgeRoomExtensionEntries(
        const forge_room_extension_t *extension, u32 *count)
{
    if (count) *count = extension ? extension->count : 0;
    return extension ? extension->entries.data() : nullptr;
}

extern "C" void forgeRoomFreeExtension(forge_room_extension_t *extension)
{
    delete extension;
}
