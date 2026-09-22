#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <SDL.h>
#include "catalog_command_generation.h"
#include "weapon_command_source.h"
#include "catalog_animation_generation.h"
#include "catalog_audio_generation.h"
#include "assetcatalog.h"
#include "assetprovider.h"
#include "fs.h"
#include "sha256.h"
#include "system.h"
#include "types.h"
#undef bool

namespace {
using Source = std::unique_ptr<weapon_command_source, decltype(&weaponCommandSourceFree)>;
using Clip = std::unique_ptr<catalog_animation_generation_t, decltype(&catalogAnimationGenerationRelease)>;
using Sound = std::unique_ptr<catalog_audio_generation_t, decltype(&catalogAudioGenerationRelease)>;
struct Node {
    Source source{nullptr, weaponCommandSourceFree};
    char hash[65]{};
    std::vector<guncmd> commands;
    std::set<std::string> children;
    size_t waiting = 0;
    std::vector<Node *> parents;
};
struct Selection { const char *id; char path[FS_MAXPATH + 1]{}; };
/* Iteration holds the catalog lock: no allocations, exceptions or borrowed rows. */
void capture(const asset_entry_t *entry, void *userdata) {
    auto &selected = *static_cast<Selection *>(userdata);
    if (std::strcmp(entry->id, selected.id) || std::strcmp(entry->category, "weapon_animation")) return;
    const asset_data_handle_t source = catalogEffectiveHandle(entry);
    if (source.provider != fileProvider()) return;
    const char *path = fileProviderPath(source);
    if (path && path[0] && std::strlen(path) < sizeof(selected.path)) std::strcpy(selected.path, path);
}
void message(char *error, size_t cap, const char *text) {
    if (error && cap) std::snprintf(error, cap, "%s", text);
}
[[noreturn]] void bad(const std::string &text) { throw std::runtime_error(text); }
SDL_threadID clientThread;
bool onClientThread() {
    const SDL_threadID current = SDL_ThreadID();
    if (!clientThread) clientThread = current;
    if (clientThread == current) return true;
    sysLoudFailf("COMMAND.GENERATION.THREAD", "command generation access outside its client thread");
    return false;
}
void frame(sha256_ctx &hash, const char *value) {
    const size_t size = std::strlen(value);
    const uint64_t length = size;
    u8 prefix[8];
    for (unsigned i = 0; i < 8; ++i) prefix[i] = static_cast<u8>(length >> (56 - i * 8));
    sha256Update(&hash, prefix, sizeof(prefix));
    sha256Update(&hash, value, size);
}
void load(Node &node, const std::string &id) {
    Selection selected{}; selected.id = id.c_str();
    assetCatalogIterateByType(ASSET_ANIMATION, capture, &selected);
    if (!selected.path[0]) bad("selected public command source unavailable: " + id);
    u32 size = 0;
    std::unique_ptr<void, decltype(&std::free)> bytes(fsFileLoad(selected.path, &size), std::free);
    if (!bytes || !size) bad("cannot read public command source: " + id);
    char error[256]{};
    node.source.reset(weaponCommandSourceRead(id.c_str(), static_cast<const char *>(bytes.get()), size, error, sizeof(error)));
    if (!node.source) bad(error);
    u8 digest[32]; sha256Hash(bytes.get(), size, digest); sha256ToHex(digest, node.hash);
    node.commands.resize(weaponCommandSourceCount(node.source.get()));
    for (size_t i = 0; i < node.commands.size(); ++i) {
        weapon_command_source_item item{};
        if (!weaponCommandSourceItem(node.source.get(), i, &item)) bad("invalid parsed command item");
        if (item.reference_kind == WEAPON_COMMAND_COMMANDS) node.children.emplace(item.reference);
    }
}
}

struct catalog_command_generation {
    size_t references = 1;
    std::string id;
    char hash[65]{};
    Node *root = nullptr;
    std::map<std::string, std::unique_ptr<Node>> nodes;
    std::map<std::string, Clip> clips;
    std::map<std::string, Sound> sounds;
};

namespace {
void build(catalog_command_generation &g) {
    std::vector<std::string> pending{g.id};
    g.nodes.emplace(g.id, std::make_unique<Node>());
    /* Iterative discovery and topological validation; neither graph depth nor
     * shared subgraphs consume the C stack or a fixed legacy loader pool. */
    for (size_t i = 0; i < pending.size(); ++i) {
        const std::string id = pending[i];
        Node &node = *g.nodes.at(id);
        load(node, id);
        for (const auto &child : node.children) {
            if (g.nodes.find(child) == g.nodes.end()) {
                g.nodes.emplace(child, std::make_unique<Node>());
                pending.push_back(child);
            }
            g.nodes.at(child)->parents.push_back(&node);
        }
        node.waiting = node.children.size();
    }
    std::vector<Node *> ready;
    for (auto &entry : g.nodes) if (!entry.second->waiting) ready.push_back(entry.second.get());
    for (size_t i = 0; i < ready.size(); ++i)
        for (Node *parent : ready[i]->parents) if (!--parent->waiting) ready.push_back(parent);
    if (ready.size() != g.nodes.size()) bad("cycle in public command source closure");
    for (auto &entry : g.nodes) {
        Node &node = *entry.second;
        for (size_t i = 0; i < node.commands.size(); ++i) {
            weapon_command_source_item item{};
            if (!weaponCommandSourceItem(node.source.get(), i, &item)) bad("invalid parsed command item");
            guncmd &command = node.commands[i];
            command.type = item.type; command.unk01 = item.selector;
            command.unk02 = item.trigger; command.unk04 = item.value;
            char error[256]{};
            switch (item.reference_kind) {
            case WEAPON_COMMAND_COMMANDS:
                command.unk04 = reinterpret_cast<intptr_t>(g.nodes.at(item.reference)->commands.data());
                break;
            case WEAPON_COMMAND_CLIP: {
                auto found = g.clips.find(item.reference);
                if (found == g.clips.end()) {
                    Clip lease(catalogAnimationGenerationAcquire(item.reference, error, sizeof(error)), catalogAnimationGenerationRelease);
                    if (!lease) bad(error);
                    found = g.clips.emplace(item.reference, std::move(lease)).first;
                }
                command.unk02 = static_cast<u16>(catalogAnimationGenerationSlot(found->second.get()));
                break;
            }
            case WEAPON_COMMAND_SOUND: {
                auto found = g.sounds.find(item.reference);
                if (found == g.sounds.end()) {
                    Sound lease(catalogAudioGenerationAcquire(item.reference, error, sizeof(error)), catalogAudioGenerationRelease);
                    if (!lease) bad(error);
                    found = g.sounds.emplace(item.reference, std::move(lease)).first;
                }
                command.unk04 = catalogAudioGenerationSlot(found->second.get());
                break;
            }
            case WEAPON_COMMAND_NO_REFERENCE: break;
            default: bad("unknown parsed command reference");
            }
        }
    }
    sha256_ctx hash; sha256Init(&hash);
    frame(hash, "pd.command.generation.v1"); frame(hash, g.id.c_str());
    for (const auto &entry : g.nodes) {
        frame(hash, "commands"); frame(hash, entry.first.c_str()); frame(hash, entry.second->hash);
    }
    for (const auto &entry : g.clips) {
        frame(hash, "clip"); frame(hash, entry.first.c_str()); frame(hash, catalogAnimationGenerationHash(entry.second.get()));
    }
    for (const auto &entry : g.sounds) {
        frame(hash, "sound"); frame(hash, entry.first.c_str()); frame(hash, catalogAudioGenerationHash(entry.second.get()));
    }
    u8 digest[32]; sha256Final(&hash, digest); sha256ToHex(digest, g.hash);
    g.root = g.nodes.at(g.id).get();
    /* Parsed authoring trees and build edges are no longer needed. Native
     * arrays and generation leases alone form the immutable runtime closure. */
    for (auto &entry : g.nodes) {
        entry.second->source.reset(); entry.second->children.clear(); entry.second->parents.clear();
    }
}
struct SoundDependency {
    std::string id;
    Sound sound{nullptr, catalogAudioGenerationRelease};
};
void releaseCommands(void *lease) { catalogCommandGenerationRelease(static_cast<catalog_command_generation_t *>(lease)); }
void releaseSound(void *lease) {
    if (onClientThread()) delete static_cast<SoundDependency *>(lease);
}
}

catalog_command_generation_t *catalogCommandGenerationAcquire(const char *id, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (!onClientThread() || !id || !std::strchr(id, ':') || std::strlen(id) >= CATALOG_ID_LEN) {
        message(error, cap, "invalid command generation identity"); return nullptr;
    }
    try {
        auto candidate = std::make_unique<catalog_command_generation>();
        candidate->id = id; build(*candidate); return candidate.release();
    } catch (const std::exception &e) { message(error, cap, e.what()); }
      catch (...) { message(error, cap, "command generation preparation failed"); }
    return nullptr;
}
void catalogCommandGenerationRetain(catalog_command_generation_t *g) {
    if (!g || !onClientThread()) return;
    if (g->references == std::numeric_limits<size_t>::max()) {
        sysLoudFailf("COMMAND.GENERATION.REFCOUNT", "command generation reference count overflow"); return;
    }
    ++g->references;
}
void catalogCommandGenerationRelease(catalog_command_generation_t *g) {
    if (g && onClientThread() && !--g->references) delete g;
}
const char *catalogCommandGenerationId(const catalog_command_generation_t *g) { return g ? g->id.c_str() : nullptr; }
const char *catalogCommandGenerationHash(const catalog_command_generation_t *g) { return g ? g->hash : nullptr; }
const struct guncmd *catalogCommandGenerationCommands(const catalog_command_generation_t *g) { return g ? g->root->commands.data() : nullptr; }
int catalogGraphNativeResolve(void *, wg_v2_dependency_kind kind, const char *id,
    wg_v2_native_dependency *out, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (!out) { message(error, cap, "missing native dependency output"); return 0; }
    *out = {};
    if (!onClientThread()) { message(error, cap, "native dependency requires client thread"); return 0; }
    if (kind == WG_V2_COMMAND_SOURCE) {
        auto *g = catalogCommandGenerationAcquire(id, error, cap);
        if (!g) return 0;
        out->catalog_id = catalogCommandGenerationId(g);
        out->source_closure_sha256 = catalogCommandGenerationHash(g);
        out->commands = catalogCommandGenerationCommands(g);
        out->lease = g; out->release = releaseCommands;
        return 1;
    }
    if (kind != WG_V2_SOUND_SOURCE || !id) { message(error, cap, "invalid native dependency kind or identity"); return 0; }
    try {
        auto lease = std::make_unique<SoundDependency>();
        lease->id = id;
        lease->sound.reset(catalogAudioGenerationAcquire(id, error, cap));
        if (!lease->sound) return 0;
        out->catalog_id = lease->id.c_str();
        out->source_closure_sha256 = catalogAudioGenerationHash(lease->sound.get());
        out->sound_id = catalogAudioGenerationSlot(lease->sound.get());
        out->release = releaseSound; out->lease = lease.release();
        return 1;
    } catch (const std::exception &e) { message(error, cap, e.what()); }
      catch (...) { message(error, cap, "sound dependency preparation failed"); }
    return 0;
}
