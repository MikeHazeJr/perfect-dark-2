#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "asset_runtime.h"
#ifndef PD_TESTS
#include "fs.h"
#endif

static asset_runtime_binding_t s_Bindings[ASSET_RUNTIME_MAX_BINDINGS];
static s32 s_BindingCount = 0;

static void s_copy(char *dst, u32 cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static s32 s_hasText(const char *s)
{
    return s && s[0];
}

static s32 s_hasAnyFile(const char *a, const char *b, const char *c,
                        const char *d)
{
    return s_hasText(a) || s_hasText(b) || s_hasText(c) || s_hasText(d);
}

static s32 s_primaryFileSize(const char *path)
{
    if (!path || !path[0]) {
        return -1;
    }
#ifdef PD_TESTS
    {
        struct stat st;
        if (stat(path, &st) != 0) {
            return -1;
        }
        return (s32)st.st_size;
    }
#else
    return fsFileSize(path);
#endif
}

static void *s_primaryFileLoad(const char *path, u32 *out_size)
{
#ifdef PD_TESTS
    FILE *f;
    s32 size;
    void *data;

    if (out_size) {
        *out_size = 0;
    }
    size = s_primaryFileSize(path);
    if (size < 0) {
        return NULL;
    }

    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    data = malloc((size_t)size + 1u);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    ((char *)data)[size] = '\0';
    fclose(f);
    if (out_size) {
        *out_size = (u32)size;
    }
    return data;
#else
    return fsFileLoad(path, out_size);
#endif
}

static s32 s_finishFileBinding(asset_runtime_binding_t *binding, s32 ok)
{
    if (!ok && binding) {
        binding->active = 0;
    }
    return ok;
}

void assetRuntimeReset(void)
{
    memset(s_Bindings, 0, sizeof(s_Bindings));
    s_BindingCount = 0;
}

s32 assetRuntimeSupportsType(asset_type_e type)
{
    switch (type) {
    case ASSET_SKIN:
    case ASSET_EFFECT:
    case ASSET_PROP:
    case ASSET_VEHICLE:
    case ASSET_MISSION:
    case ASSET_GAMEMODE:
    case ASSET_BOT_PROFILE:
    case ASSET_HUD:
    case ASSET_MATERIAL:
    case ASSET_FONT:
    case ASSET_SCENARIO:
    case ASSET_THEME:
        return 1;
    default:
        return 0;
    }
}

static asset_runtime_binding_t *s_findMutable(const char *asset_id)
{
    if (!asset_id || !asset_id[0]) {
        return NULL;
    }

    for (s32 i = 0; i < s_BindingCount; i++) {
        if (s_Bindings[i].active
                && strncmp(s_Bindings[i].id, asset_id, CATALOG_ID_LEN) == 0) {
            return &s_Bindings[i];
        }
    }
    return NULL;
}

const asset_runtime_binding_t *assetRuntimeFind(const char *asset_id)
{
    return s_findMutable(asset_id);
}

const asset_runtime_binding_t *assetRuntimeFindByTypeAndId(asset_type_e type,
                                                          const char *asset_id)
{
    const asset_runtime_binding_t *binding = assetRuntimeFind(asset_id);
    if (!binding || binding->type != type) {
        return NULL;
    }
    return binding;
}

const asset_runtime_binding_t *assetRuntimeFindByTypeKind(asset_type_e type,
                                                         s32 kind)
{
    for (s32 i = 0; i < s_BindingCount; i++) {
        if (s_Bindings[i].active
                && s_Bindings[i].type == type
                && s_Bindings[i].kind == kind) {
            return &s_Bindings[i];
        }
    }
    return NULL;
}

const asset_runtime_binding_t *assetRuntimeFindByTarget(asset_type_e type,
                                                       const char *target_id)
{
    if (!target_id || !target_id[0]) {
        return NULL;
    }

    for (s32 i = 0; i < s_BindingCount; i++) {
        if (s_Bindings[i].active
                && s_Bindings[i].type == type
                && strncmp(s_Bindings[i].target_id, target_id,
                           CATALOG_ID_LEN) == 0) {
            return &s_Bindings[i];
        }
    }
    return NULL;
}

static asset_runtime_binding_t *s_allocBinding(const char *asset_id)
{
    asset_runtime_binding_t *binding = s_findMutable(asset_id);
    if (binding) {
        memset(binding, 0, sizeof(*binding));
        return binding;
    }

    if (s_BindingCount >= ASSET_RUNTIME_MAX_BINDINGS) {
        return NULL;
    }

    binding = &s_Bindings[s_BindingCount++];
    memset(binding, 0, sizeof(*binding));
    return binding;
}

s32 assetRuntimeActivateCatalogEntry(const asset_entry_t *entry,
                                     const char *primary_path)
{
    asset_runtime_binding_t *binding;

    if (!entry || !entry->id[0] || !assetRuntimeSupportsType(entry->type)) {
        return 0;
    }

    binding = s_allocBinding(entry->id);
    if (!binding) {
        return 0;
    }

    binding->active = 1;
    binding->type = entry->type;
    s_copy(binding->id, sizeof(binding->id), entry->id);
    s_copy(binding->primary_path, sizeof(binding->primary_path), primary_path);

    switch (entry->type) {
    case ASSET_SKIN:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.skin.texture_file);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.skin.target_id);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         NULL, NULL));

    case ASSET_EFFECT:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.effect.effect_file);
        s_copy(binding->shader_id, sizeof(binding->shader_id),
               entry->ext.effect.shader_id);
        binding->kind = entry->ext.effect.effect_type;
        binding->target_kind = entry->ext.effect.target;
        binding->value0 = entry->ext.effect.intensity;
        for (s32 i = 0; i < 4; i++) {
            binding->params[i] = entry->ext.effect.params[i];
        }
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         binding->shader_id, NULL));

    case ASSET_VEHICLE:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.vehicle.model_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.vehicle.physics_file);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.vehicle.behavior_graph);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         binding->dependency_a, binding->dependency_b));

    case ASSET_PROP:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.prop.model_file);
        binding->runtime_id = entry->ext.prop.prop_type;
        binding->kind = entry->ext.prop.prop_type;
        binding->target_kind = (s32)entry->ext.prop.flags;
        binding->value0 = entry->ext.prop.health;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         NULL, NULL));

    case ASSET_MISSION:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.mission.mission_graph_file[0]
                    ? entry->ext.mission.mission_graph_file
                    : entry->ext.mission.scenario_archive);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.mission.scenario_archive);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.mission.objectives_file[0]
                    ? entry->ext.mission.objectives_file
                    : entry->ext.mission.briefing_file);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         binding->dependency_a, binding->dependency_b));

    case ASSET_GAMEMODE:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.gamemode.rules_file);
        binding->runtime_id = entry->ext.gamemode.mode_id;
        binding->kind = entry->ext.gamemode.team_based;
        binding->target_kind = entry->ext.gamemode.min_players;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         NULL, NULL));

    case ASSET_BOT_PROFILE:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.bot_profile.profile_file);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.bot_profile.target_body);
        binding->runtime_id = entry->ext.bot_profile.type;
        binding->kind = entry->ext.bot_profile.difficulty;
        binding->target_kind = entry->ext.bot_profile.body;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         NULL, NULL));

    case ASSET_HUD:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.hud.texture_file);
        binding->runtime_id = entry->ext.hud.hud_id;
        binding->kind = entry->ext.hud.element_type;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         NULL, NULL));

    case ASSET_MATERIAL:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.material.material_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.material.texture_archive);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.material.effect_archive);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         binding->dependency_a, binding->dependency_b));

    case ASSET_FONT:
        return s_finishFileBinding(binding, s_hasText(binding->primary_path));

    case ASSET_SCENARIO:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.scenario.scene_file[0]
                    ? entry->ext.scenario.scene_file
                    : entry->ext.scenario.rooms_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.scenario.collision_file);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.scenario.level_graph_file);
        binding->runtime_id = entry->ext.scenario.stagenum;
        binding->kind = entry->ext.scenario.mode;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         binding->dependency_a, binding->dependency_b));

    case ASSET_THEME:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.theme.theme_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.theme.ui_archive);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.theme.font_archive);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->primary_path, binding->authored_file,
                         binding->dependency_a, binding->dependency_b));

    default:
        binding->active = 0;
        return 0;
    }
}

void assetRuntimeReleaseCatalogEntry(const char *asset_id)
{
    asset_runtime_binding_t *binding = s_findMutable(asset_id);
    if (binding) {
        binding->active = 0;
    }
}

s32 assetRuntimePrimaryFileAccessible(const asset_runtime_binding_t *binding)
{
    if (!binding || !binding->active || !binding->primary_path[0]) {
        return 0;
    }

    return s_primaryFileSize(binding->primary_path) >= 0;
}

void *assetRuntimeLoadPrimaryFile(const asset_runtime_binding_t *binding,
                                  u32 *out_size)
{
    if (out_size) {
        *out_size = 0;
    }
    if (!assetRuntimePrimaryFileAccessible(binding)) {
        return NULL;
    }
    return s_primaryFileLoad(binding->primary_path, out_size);
}

s32 assetRuntimeCount(asset_type_e type)
{
    s32 count = 0;
    for (s32 i = 0; i < s_BindingCount; i++) {
        if (s_Bindings[i].active
                && (type == ASSET_NONE || s_Bindings[i].type == type)) {
            count++;
        }
    }
    return count;
}

s32 assetRuntimeAccessibleCount(asset_type_e type)
{
    s32 count = 0;
    for (s32 i = 0; i < s_BindingCount; i++) {
        if (s_Bindings[i].active
                && (type == ASSET_NONE || s_Bindings[i].type == type)
                && assetRuntimePrimaryFileAccessible(&s_Bindings[i])) {
            count++;
        }
    }
    return count;
}
