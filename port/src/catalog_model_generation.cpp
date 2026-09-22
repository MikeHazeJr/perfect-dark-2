#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <SDL.h>
#include "catalog_model_generation.h"
#include "catalog_texture_generation.h"
#include "asset_path_contract.h"
#include "assetprovider.h"
#include "model_source_path.h"
#include "modasset_compiler.h"
#include "fs.h"
#include "game/tex.h"
#include "sha256.h"
#include "system.h"
#include "types.h"
#undef bool

namespace {
struct Read {
    std::string path;
    std::string bytes;
    bool present = false;
};
struct Texture {
    catalog_texture_generation_t *generation = nullptr;
    ~Texture() { catalogTextureGenerationRelease(generation); }
};
struct Build {
    const asset_entry_t *entry = nullptr;
    std::vector<Read> reads;
    std::vector<std::unique_ptr<Texture>> textures;
    std::string failure;
    struct texpool pool{};
};
SDL_threadID client_thread;
catalog_model_generation *generations;

void message(char *error, size_t cap, const char *text) {
    if (error && cap) std::snprintf(error, cap, "%s", text);
}
bool onClientThread() {
    const SDL_threadID current = SDL_ThreadID();
    if (!client_thread) client_thread = current;
    if (client_thread == current) return true;
    sysLoudFailf("MODEL.GENERATION.THREAD", "model generation access outside its client thread");
    return false;
}
Read *capture(Build &build, const char *path) {
    if (!path || !path[0]) { build.failure = "empty model source dependency"; return nullptr; }
    for (auto &read : build.reads) if (read.path == path) return &read;
    u32 size = 0;
    void *raw = fsFileLoad(path, &size);
    try {
        Read read;
        read.path = path;
        read.present = raw && size;
        if (read.present) read.bytes.assign(static_cast<const char *>(raw), size);
        build.reads.push_back(std::move(read));
    } catch (...) {
        build.failure = "model source capture allocation failed";
    }
    free(raw);
    return build.failure.empty() ? &build.reads.back() : nullptr;
}
void *readModel(void *context, const char *path, u32 *size) {
    auto &build = *static_cast<Build *>(context);
    if (size) *size = 0;
    try {
        Read *read = capture(build, path);
        if (!read || !read->present || read->bytes.size() > UINT32_MAX) return nullptr;
        void *copy = std::malloc(read->bytes.size() + 1);
        if (!copy) { build.failure = "model source copy allocation failed"; return nullptr; }
        std::memcpy(copy, read->bytes.data(), read->bytes.size());
        static_cast<char *>(copy)[read->bytes.size()] = 0;
        if (size) *size = static_cast<u32>(read->bytes.size());
        return copy;
    } catch (...) { build.failure = "model source capture failed"; return nullptr; }
}
bool selectedTexturePath(const char *source, const char *reference, char *out, size_t cap) {
    if (!source || !reference || !reference[0] || assetPathHasParentTraversal(reference)) return false;
    if (assetPathIsAbsolute(reference) || std::strstr(reference, "::"))
        return assetPathCopyChecked(out, cap, reference) != 0;
    const char *archive = std::strstr(source, "::");
    if (archive) {
        std::string root(source, static_cast<size_t>(archive + 2 - source));
        return assetPathJoinChecked(out, cap, root.c_str(), "", reference) != 0;
    }
    const char *slash = std::strrchr(source, '/');
    const char *backslash = std::strrchr(source, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    std::string root(source, slash ? static_cast<size_t>(slash + 1 - source) : 0);
    return assetPathJoinChecked(out, cap, root.c_str(), "", reference) != 0;
}
int bindTexture(void *context, const char *source, const char *reference,
        s32 is_catalog_id, s32) {
    auto &build = *static_cast<Build *>(context);
    try {
        if (!reference || !reference[0]) { build.failure = "empty model texture reference"; return -1; }
        char error[256]{};
        catalog_texture_generation_t *generation = nullptr;
        const asset_entry_t *catalog = assetCatalogResolve(reference);
        if (is_catalog_id || (catalog && catalog->type == ASSET_TEXTURE)) {
            generation = catalogTextureGenerationAcquire(reference, error, sizeof(error));
        } else {
            char path[FS_MAXPATH + 1]{};
            if (!selectedTexturePath(source, reference, path, sizeof(path))) {
                build.failure = "invalid source-relative model texture path"; return -1;
            }
            Read *read = capture(build, path);
            if (!read || !read->present || read->bytes.size() > UINT32_MAX) {
                build.failure = "model texture source is missing"; return -1;
            }
            std::string id = std::string(build.entry->id) + "/" + path;
            generation = catalogTextureGenerationAcquireSource(id.c_str(), read->bytes.data(),
                static_cast<u32>(read->bytes.size()), nullptr, 0, error, sizeof(error));
        }
        if (!generation) { build.failure = error[0] ? error : "model texture generation failed"; return -1; }
        std::unique_ptr<catalog_texture_generation_t, decltype(&catalogTextureGenerationRelease)>
            owned(generation, catalogTextureGenerationRelease);
        auto retained = std::make_unique<Texture>();
        retained->generation = owned.release();
        const int slot = catalogTextureGenerationSlot(generation);
        build.textures.push_back(std::move(retained));
        return slot;
    } catch (...) { build.failure = "model texture binding failed"; return -1; }
}
bool stable(const Build &build) {
    for (const auto &read : build.reads) {
        u32 size = 0;
        void *raw = fsFileLoad(read.path.c_str(), &size);
        const bool same = read.present ? raw && size == read.bytes.size()
            && !std::memcmp(raw, read.bytes.data(), size) : !raw || !size;
        free(raw);
        if (!same) return false;
    }
    return true;
}
void hashPart(sha256_ctx &hash, const void *data, size_t size) {
    const u8 length[] = {static_cast<u8>(size >> 56), static_cast<u8>(size >> 48),
        static_cast<u8>(size >> 40), static_cast<u8>(size >> 32),
        static_cast<u8>(size >> 24), static_cast<u8>(size >> 16),
        static_cast<u8>(size >> 8), static_cast<u8>(size)};
    sha256Update(&hash, length, sizeof(length));
    if (size) sha256Update(&hash, data, size);
}
std::string closureHash(const Build &build, const char *selected) {
    sha256_ctx hash;
    u8 digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_SIZE];
    sha256Init(&hash);
    const char version[] = "pd.model.generation.v1";
    hashPart(hash, version, sizeof(version));
    hashPart(hash, build.entry->id, std::strlen(build.entry->id));
    hashPart(hash, selected, std::strlen(selected));
    for (const auto &read : build.reads) {
        hashPart(hash, read.path.data(), read.path.size());
        const u8 present = read.present ? 1 : 0;
        hashPart(hash, &present, 1);
        hashPart(hash, read.bytes.data(), read.bytes.size());
    }
    for (const auto &texture : build.textures) {
        const char *id = catalogTextureGenerationId(texture->generation);
        const char *source_hash = catalogTextureGenerationHash(texture->generation);
        hashPart(hash, id, std::strlen(id));
        hashPart(hash, source_hash, std::strlen(source_hash));
    }
    sha256Final(&hash, digest);
    sha256ToHex(digest, hex);
    return hex;
}
}

struct catalog_model_generation {
    catalog_model_generation *next = nullptr;
    std::string id, hash;
    struct modeldef *model = nullptr;
    std::vector<std::unique_ptr<Texture>> textures;
    unsigned references = 1;
};

extern "C" catalog_model_generation_t *catalogModelGenerationAcquireSource(
        const asset_entry_t *entry, const char *source, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (!onClientThread() || !entry || !entry->id[0] || !source || !source[0]) {
        message(error, cap, "invalid selected model source"); return nullptr;
    }
    try {
        char selected[FS_MAXPATH + 1]{};
        if (!modelSourceResolvePath(source, selected, sizeof(selected), error, cap)) return nullptr;
        Build build;
        build.entry = entry;
        /* The archive descriptor selects the mesh. Capture both it and every
         * subsequent compiler read so a concurrent edit cannot mix sources. */
        Read *archive = capture(build, source);
        if (!archive || !archive->present) {
            message(error, cap, "selected model source is missing"); return nullptr;
        }
        modasset_model_inputs_t inputs{};
        inputs.context = &build;
        inputs.read = readModel;
        inputs.texture = bindTexture;
        inputs.texture_pool = &build.pool;
        struct modeldef *model = nullptr;
        if (modAssetCompilerBuildModeldefWithInputs(&inputs, entry, selected, &model) <= 0 || !model) {
            if (model) modAssetCompilerFreeModeldef(model);
            message(error, cap, build.failure.empty() ? "public model conversion failed" : build.failure.c_str());
            return nullptr;
        }
        std::unique_ptr<struct modeldef, decltype(&modAssetCompilerFreeModeldef)>
            candidate(model, modAssetCompilerFreeModeldef);
        char selected_again[FS_MAXPATH + 1]{};
        if (!build.failure.empty() || !stable(build)
                || !modelSourceResolvePath(source, selected_again, sizeof(selected_again), nullptr, 0)
                || std::strcmp(selected, selected_again)) {
            message(error, cap, build.failure.empty() ? "model source changed during conversion" : build.failure.c_str());
            return nullptr;
        }
        const std::string hash = closureHash(build, selected);
        for (auto *old = generations; old; old = old->next) {
            if (old->id == entry->id && old->hash == hash) {
                if (old->references == std::numeric_limits<unsigned>::max()) {
                    message(error, cap, "model generation reference count overflow"); return nullptr;
                }
                ++old->references;
                return old;
            }
        }
        auto generation = std::make_unique<catalog_model_generation>();
        generation->id = entry->id;
        generation->hash = hash;
        generation->model = candidate.release();
        generation->textures = std::move(build.textures);
        generation->next = generations;
        generations = generation.get();
        return generation.release();
    } catch (...) {
        message(error, cap, "model generation allocation failed"); return nullptr;
    }
}
extern "C" void catalogModelGenerationRetain(catalog_model_generation_t *g) {
    if (!g || !onClientThread()) return;
    if (g->references == std::numeric_limits<unsigned>::max()) {
        sysLoudFailf("MODEL.GENERATION.REFCOUNT", "model generation reference count overflow"); return;
    }
    ++g->references;
}
extern "C" void catalogModelGenerationRelease(catalog_model_generation_t *g) {
    if (!g || !onClientThread() || --g->references) return;
    auto **link = &generations;
    while (*link && *link != g) link = &(*link)->next;
    if (*link) *link = g->next;
    modAssetCompilerFreeModeldef(g->model);
    delete g;
}
extern "C" const char *catalogModelGenerationHash(const catalog_model_generation_t *g) {
    return g ? g->hash.c_str() : nullptr;
}
extern "C" struct modeldef *catalogModelGenerationModeldef(catalog_model_generation_t *g) {
    return g ? g->model : nullptr;
}
extern "C" catalog_model_generation_t *catalogModelGenerationForModeldef(const struct modeldef *model) {
    if (!model || !onClientThread()) return nullptr;
    for (auto *g = generations; g; g = g->next) if (g->model == model) return g;
    return nullptr;
}
