#include <PR/ultratypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>

#include "glad/glad.h"
#include "../external/stb_image.h"

extern "C" {
#include "asset_runtime.h"
#include "assetcatalog.h"
#include "fs.h"
#include "pdgui_character_portrait.h"
#include "system.h"
}

struct CharacterPortraitCacheEntry {
    GLuint texture;
    u32 width;
    u32 height;
    std::string source_path;
};

static std::unordered_map<std::string, CharacterPortraitCacheEntry>
    s_CharacterPortraits;

static GLuint uploadPortrait(const u8 *rgba, u32 width, u32 height)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width,
                 (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return texture;
}

extern "C" s32 pdguiCharacterPortraitGet(
    const char *body_id, const char *head_id, u32 *out_texture,
    u32 *out_width, u32 *out_height)
{
    if (out_texture) *out_texture = 0;
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;

    const asset_entry_t *character =
        assetCatalogFindCharacterByBodyHead(body_id, head_id);
    if (!character) return 0;

    const asset_runtime_binding_t *binding =
        assetRuntimeFindByTypeAndId(ASSET_CHARACTER, character->id);
    if (!binding || strcmp(binding->character_body_id, body_id) != 0 ||
            strcmp(binding->character_head_id, head_id) != 0) {
        sysLogPrintf(LOG_ERROR,
            "CHARACTER.PORTRAIT.FAIL: character=%s body_id=%s head_id=%s has no active matching public runtime binding",
            character->id, body_id, head_id);
        return -1;
    }

    if (!binding->dependency_b[0]) return 0;

    auto cached = s_CharacterPortraits.find(character->id);
    if (cached != s_CharacterPortraits.end() &&
            cached->second.source_path == binding->dependency_b) {
        if (out_texture) *out_texture = (u32)cached->second.texture;
        if (out_width) *out_width = cached->second.width;
        if (out_height) *out_height = cached->second.height;
        return 1;
    }

    u32 source_size = 0;
    u8 *source = (u8 *)fsFileLoad(binding->dependency_b, &source_size);
    if (!source || source_size == 0) {
        if (source) sysMemFree(source);
        sysLogPrintf(LOG_ERROR,
            "CHARACTER.PORTRAIT.FAIL: character=%s declared source=%s is unreadable; refusing generated portrait fallback",
            character->id, binding->dependency_b);
        return -1;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    u8 *rgba = stbi_load_from_memory(source, (int)source_size,
                                     &width, &height, &channels, 4);
    sysMemFree(source);
    if (!rgba || width <= 0 || height <= 0) {
        sysLogPrintf(LOG_ERROR,
            "CHARACTER.PORTRAIT.FAIL: character=%s declared source=%s cannot decode; refusing generated portrait fallback",
            character->id, binding->dependency_b);
        if (rgba) stbi_image_free(rgba);
        return -1;
    }

    GLuint texture = uploadPortrait(rgba, (u32)width, (u32)height);
    stbi_image_free(rgba);
    if (!texture) {
        sysLogPrintf(LOG_ERROR,
            "CHARACTER.PORTRAIT.FAIL: character=%s source=%s GL upload failed",
            character->id, binding->dependency_b);
        return -1;
    }

    if (cached != s_CharacterPortraits.end() && cached->second.texture) {
        GLuint old = cached->second.texture;
        glDeleteTextures(1, &old);
    }
    CharacterPortraitCacheEntry entry = {
        texture, (u32)width, (u32)height, binding->dependency_b
    };
    s_CharacterPortraits[character->id] = entry;
    if (out_texture) *out_texture = (u32)texture;
    if (out_width) *out_width = (u32)width;
    if (out_height) *out_height = (u32)height;
    sysLogPrintf(LOG_NOTE,
        "CHARACTER.PORTRAIT.SOURCE: character=%s body_id=%s head_id=%s source=%s size=%dx%d",
        character->id, body_id, head_id, binding->dependency_b, width, height);
    return 1;
}

extern "C" void pdguiCharacterPortraitReset(void)
{
    for (auto &item : s_CharacterPortraits) {
        if (item.second.texture) {
            GLuint texture = item.second.texture;
            glDeleteTextures(1, &texture);
        }
    }
    s_CharacterPortraits.clear();
}
