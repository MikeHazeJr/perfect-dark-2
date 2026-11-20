#include "pdgui_grid_browser_data.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "crude_json.h"
#include "asset_archive_policy.h"
#include "assetcatalog_scanner.h"
#include "forge_revision_store.h"
#include "grid_archive_policy.h"
#include "grid_match_package_io.h"
#include "grid_match_runtime.h"
#include "modasset_gltf_document.h"

namespace {

constexpr u32 kDescriptorLimit = 1024 * 1024;
constexpr u32 kPreviewLimit = 8 * 1024 * 1024;
constexpr char kArenaDescriptor[] = "arena.ini";
constexpr char kPreview[] = "preview.png";
constexpr char kThumbnail[] = "thumbnail.png";
constexpr char kRoomTable[] = "rooms.json";
constexpr u32 kScenarioPreviewLimit = 32 * 1024 * 1024;
constexpr u32 kRoomTableLimit = 1024 * 1024;
constexpr std::size_t kRoomCountLimit = 1024;

using ArchiveFile = std::unique_ptr<mod_archive_t, decltype(&modArchiveClose)>;
using OwnedBytes = std::unique_ptr<void, decltype(&std::free)>;

s32 findExactMember(const mod_archive_t *archive, const char *name)
{
    s32 index = -1;
    for (s32 i = 0; i < modArchiveGetEntryCount(archive); ++i) {
        const char *candidate = modArchiveGetEntryName(archive, i);
        if (!candidate || std::strcmp(candidate, name) != 0) continue;
        if (index >= 0) return -1;
        index = i;
    }
    return index;
}

class ArenaArchive {
public:
    bool open(const char *root)
    {
        if (std::strstr(root, "::")) {
            // The shared owner reader checks the packed member size before
            // allocating. Never unpack archives to a writable mod directory.
            wrapped.reset(gridMatchPackageLoad(root, &wrappedSize));
            return wrapped != nullptr;
        }
        char path[FS_MAXPATH + 1];
        file.reset(modArchiveOpen(fsFullPath(root, path, sizeof(path))));
        return file != nullptr;
    }

    OwnedBytes read(const char *member, u32 limit, u32 &size) const
    {
        size = 0;
        if (wrapped) {
            return OwnedBytes(gridMatchPackageReadMember(wrapped.get(), wrappedSize,
                member, limit, &size, nullptr), std::free);
        }
        const s32 index = findExactMember(file.get(), member);
        const u32 length = modArchiveGetEntrySize(file.get(), index);
        if (length == 0 || length > limit) return OwnedBytes(nullptr, std::free);
        OwnedBytes bytes(modArchiveExtractAlloc(file.get(), index, &size), std::free);
        if (size != length) {
            bytes.reset();
            size = 0;
        }
        return bytes;
    }

    bool contains(const char *member, u32 limit) const
    {
        if (wrapped) {
            MemberInfo info{ member };
            if (modArchiveMemForEachEntry(wrapped.get(), wrappedSize, measureMember,
                    &info) != MODARCHIVE_OK) return false;
            return info.matches == 1 && info.size > 0 && info.size <= limit;
        }
        const u32 length = modArchiveGetEntrySize(file.get(), findExactMember(file.get(), member));
        return length > 0 && length <= limit;
    }

private:
    struct MemberInfo {
        const char *name;
        u32 size = 0;
        u32 matches = 0;
    };

    static s32 measureMember(const char *name, u32 size, void *userdata)
    {
        auto &info = *static_cast<MemberInfo *>(userdata);
        if (std::strcmp(name, info.name) == 0) {
            info.size = size;
            ++info.matches;
        }
        return 0;
    }

    ArchiveFile file{nullptr, modArchiveClose};
    OwnedBytes wrapped{nullptr, std::free};
    u32 wrappedSize = 0;
};

bool resolveArenaMember(const char *locator, char root[FS_MAXPATH + 1], const char *&leaf)
{
    if (!locator) return false;
    const char *separator = std::strrchr(locator, ':');
    if (!separator || separator == locator || separator[-1] != ':') return false;
    const size_t ownerLength = size_t(separator - locator - 1);
    if (ownerLength == 0 || ownerLength > FS_MAXPATH) return false;

    leaf = separator + 1;
    if (std::strcmp(leaf, kArenaDescriptor) != 0
            && std::strcmp(leaf, kPreview) != 0 && std::strcmp(leaf, kThumbnail) != 0) return false;

    // Validate the same exact owner locator as the descriptor, even when the
    // member is optional artwork. Nested scenario owners do not define the map.
    char descriptor[FS_MAXPATH + sizeof("::arena.ini")];
    std::memcpy(descriptor, locator, ownerLength);
    std::memcpy(descriptor + ownerLength, "::arena.ini", sizeof("::arena.ini"));
    char chain[FS_MAXPATH + 1];
    return gridMatchPackageLocation(descriptor, root, FS_MAXPATH + 1, chain, sizeof(chain), nullptr)
        && chain[0] == '\0' && assetArchiveTypeForPath(root) == ASSET_ARENA;
}

const char *uniqueField(const ini_section_t &ini, const char *key, bool &ambiguous)
{
    ambiguous = false;
    const char *value = nullptr;
    for (s32 i = 0; i < ini.count; ++i) {
        const ini_pair_t &pair = ini.pairs[i];
        if (std::strcmp(pair.key, key) != 0) continue;
        if (value) {
            ambiguous = true;
            return nullptr;
        }
        value = pair.value;
    }
    return value;
}

std::unique_ptr<ini_section_t> readDescriptor(const ArenaArchive &archive)
{
    u32 size = 0;
    const OwnedBytes bytes = archive.read(kArenaDescriptor, kDescriptorLimit, size);
    if (!bytes || std::memchr(bytes.get(), '\0', size)) return nullptr;

    auto ini = std::make_unique<ini_section_t>();
    if (!iniParseText(static_cast<const char *>(bytes.get()), size, ini.get(), nullptr, 0)
            || std::strcmp(ini->type, "arena") != 0) return nullptr;
    return ini;
}

std::string idHash(const char *id)
{
    u8 digest[SHA256_DIGEST_SIZE];
    char text[SHA256_HEX_SIZE];
    sha256Hash(id, std::strlen(id), digest);
    sha256ToHex(digest, text);
    return text;
}

bool isDigest(const char *value)
{
    return value && std::strlen(value) == SHA256_HEX_SIZE - 1
        && std::strspn(value, "0123456789abcdef") == SHA256_HEX_SIZE - 1;
}

const char *dependencyName(const asset_entry_t &entry)
{
    switch (entry.type) {
    case ASSET_ARENA: return entry.ext.arena.display_name;
    case ASSET_ANIMATION: return entry.ext.anim.name;
    case ASSET_CHARACTER: return entry.ext.character.display_name;
    case ASSET_BODY: return entry.ext.body.display_name;
    case ASSET_WEAPON: return entry.ext.weapon.name;
    case ASSET_PROJECTILE: return entry.ext.projectile.name;
    case ASSET_ENTITY: return entry.ext.entity.name;
    case ASSET_PROP: return entry.ext.prop.name;
    case ASSET_GAMEMODE: return entry.ext.gamemode.name;
    case ASSET_MUSIC:
    case ASSET_AUDIO:
    case ASSET_SFX: return entry.ext.audio.name;
    default: return "";
    }
}

s32 collectDependency(const char *id, void *userdata)
{
    auto &dependencies = *static_cast<std::vector<GridBrowserDependency> *>(userdata);
    if (std::strlen(id) >= CATALOG_ID_LEN) return 0;

    for (const GridBrowserDependency &dependency : dependencies) {
        if (dependency.id == id) return 1;
    }

    const asset_entry_t *entry = assetCatalogResolveAny(id);
    const bool exact = entry && std::strcmp(entry->id, id) == 0;
    dependencies.push_back({id, exact ? dependencyName(*entry) : "",
        exact && entry->enabled});
    return 1;
}

bool readWorldPoint(const crude_json::value &point, double &x, double &z)
{
    if (!point.is_array()) return false;
    const auto &axes = point.get<crude_json::array>();
    if (axes.size() != 3) return false;
    for (const auto &axis : axes) {
        if (!axis.is_number() || !std::isfinite(static_cast<float>(axis.get<double>()))) return false;
    }
    x = static_cast<float>(axes[0].get<double>());
    z = static_cast<float>(axes[2].get<double>());
    return true;
}

} // namespace

GridBrowserMetadata gridBrowserReadMetadata(const char *arenaId)
{
    GridBrowserMetadata metadata;
    if (!arenaId || !arenaId[0]) return metadata;

    const asset_entry_t *arena = assetCatalogResolve(arenaId);
    if (!arena || arena->type != ASSET_ARENA || std::strcmp(arena->id, arenaId) != 0) {
        return metadata;
    }

    metadata.idHash = idHash(arenaId);
    if (arena->bundled) metadata.source = GridBrowserSource::BuiltIn;

    char locator[FS_MAXPATH + 1], root[FS_MAXPATH + 1];
    const char *leaf = nullptr;
    ArenaArchive archive;
    if (!gridMatchCatalogArchiveDescriptor(arena, locator, sizeof(locator))
            || !resolveArenaMember(locator, root, leaf) || std::strcmp(leaf, kArenaDescriptor) != 0
            || !archive.open(root)) return metadata;

    const auto ini = readDescriptor(archive);
    if (!ini) return metadata;

    bool ambiguous = false;
    const char *id = uniqueField(*ini, "catalog_id", ambiguous);
    if (!id || std::strcmp(id, arenaId) != 0) return metadata;
    metadata.hasNativeDescriptor = true;

    if (const char *description = uniqueField(*ini, "description", ambiguous)) {
        metadata.description = description;
    }

    const char *parent = uniqueField(*ini, FORGE_REVISION_PARENT_MAP_KEY, ambiguous);
    const char *revision = uniqueField(*ini, FORGE_REVISION_PARENT_DIGEST_KEY, ambiguous);
    if (isDigest(parent) && isDigest(revision) && metadata.idHash != parent) {
        metadata.parentIdHash = parent;
        metadata.parentRevision = revision;
    }

    const char *plan = uniqueField(*ini, FORGE_REVISION_DEPENDENCY_PLAN_KEY, ambiguous);
    if (isDigest(plan)) metadata.dependencyPlan = plan;

    const char *deps = uniqueField(*ini, "deps", ambiguous);
    if (!ambiguous && (!deps || std::strlen(deps) < ASSET_CATALOG_DECLARED_DEP_BYTES)
            && assetCatalogVisitDeclaredDependencies(deps, collectDependency, &metadata.dependencies)) {
        metadata.dependenciesKnown = true;
    } else {
        metadata.dependencies.clear();
    }

    const char *provenance = uniqueField(*ini, GRID_ARENA_PROVENANCE_KEY, ambiguous);
    if (metadata.source != GridBrowserSource::BuiltIn && (ambiguous || provenance)) {
        if (provenance && std::strcmp(provenance, GRID_ARENA_PROVENANCE_ORIGINAL) == 0) {
            metadata.source = GridBrowserSource::Original;
        } else if (provenance && std::strcmp(provenance, GRID_ARENA_PROVENANCE_BYOR) == 0) {
            metadata.source = GridBrowserSource::LocalOnly;
        } else {
            metadata.source = GridBrowserSource::Unverified;
        }
    }

    if (archive.contains(kPreview, kPreviewLimit)) {
        metadata.previewLocator = std::string(root) + "::" + kPreview;
    }
    if (archive.contains(kThumbnail, kPreviewLimit)) {
        metadata.thumbnailLocator = std::string(root) + "::" + kThumbnail;
    }
    return metadata;
}

std::vector<unsigned char> gridBrowserReadPreview(const std::string &locator, std::size_t maxBytes)
{
    if (locator.empty() || maxBytes == 0) return {};

    char root[FS_MAXPATH + 1];
    const char *leaf = nullptr;
    ArenaArchive archive;
    if (!resolveArenaMember(locator.c_str(), root, leaf)
            || std::strcmp(leaf, kArenaDescriptor) == 0 || !archive.open(root)) return {};

    const u32 limit = static_cast<u32>(std::min(maxBytes, std::size_t(kPreviewLimit)));
    u32 size = 0;
    const OwnedBytes bytes = archive.read(leaf, limit, size);
    const auto *png = static_cast<const unsigned char *>(bytes.get());
    constexpr unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!png || size < sizeof(signature) || std::memcmp(png, signature, sizeof(signature)) != 0) return {};

    return {png, png + size};
}

std::vector<GridBrowserRoomFootprint> gridBrowserReadRoomLayout(const char *arenaId)
{
    if (!arenaId || !*arenaId) return {};
    const asset_entry_t *arena = assetCatalogResolve(arenaId);
    if (!arena || arena->type != ASSET_ARENA || std::strcmp(arena->id, arenaId) != 0) return {};

    const char *scenario = arena->ext.arena.scenario_archive;
    asset_type_e owner;
    const char *member = nullptr;
    if (!gridMatchPackageMemberOwner(ASSET_ARENA, scenario, &owner, &member)
            || owner != ASSET_ARENA || assetArchiveTypeForPath(member) != ASSET_SCENARIO) return {};

    char locator[FS_MAXPATH + 1], root[FS_MAXPATH + 1];
    const char *leaf = nullptr;
    ArenaArchive archive;
    if (!gridMatchCatalogArchiveDescriptor(arena, locator, sizeof(locator))
            || !resolveArenaMember(locator, root, leaf) || std::strcmp(leaf, kArenaDescriptor) != 0
            || !archive.open(root)) return {};
    u32 scenarioSize = 0, tableSize = 0;
    const OwnedBytes scenarioBytes = archive.read(member, kScenarioPreviewLimit, scenarioSize);
    if (!scenarioBytes) return {};
    const OwnedBytes table(gridMatchPackageReadMember(scenarioBytes.get(), scenarioSize,
        kRoomTable, kRoomTableLimit, &tableSize, nullptr), std::free);
    if (!table) return {};

    crude_json::value document;
    if (!modAssetJsonReadValue(static_cast<const char *>(table.get()), tableSize, document)
            || !document.is_object()) return {};
    const crude_json::value &roomTable = document;
    if (!roomTable.contains("schema") || !roomTable.contains("rows")) return {};
    const auto &schema = roomTable["schema"];
    const auto &roomRows = roomTable["rows"];
    if (!schema.is_string() || schema.get<std::string>() != "pd2.scenario.rooms.v1"
            || !roomRows.is_array()) return {};
    const auto &rows = roomRows.get<crude_json::array>();
    if (rows.size() > kRoomCountLimit) return {};

    std::vector<GridBrowserRoomFootprint> rooms;
    rooms.reserve(rows.size());
    for (const auto &row : rows) {
        if (!row.is_object() || !row.contains("min") || !row.contains("max")) continue;
        const auto enabled = [&row](const char *flag) {
            return row.contains(flag) && row[flag].is_boolean() && row[flag].get<bool>();
        };
        if (enabled("sky") || enabled("render_always")) continue;
        GridBrowserRoomFootprint bounds;
        if (!readWorldPoint(row["min"], bounds.minX, bounds.minZ)
                || !readWorldPoint(row["max"], bounds.maxX, bounds.maxZ)
                || bounds.maxX <= bounds.minX || bounds.maxZ <= bounds.minZ) continue;
        rooms.push_back(bounds);
    }
    return rooms;
}
