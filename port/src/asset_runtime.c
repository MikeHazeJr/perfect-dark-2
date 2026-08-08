#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "asset_runtime.h"
#include "prop_graph_runtime.h"
#ifndef PD_TESTS
#include "fs.h"
#endif

static asset_runtime_binding_t *s_Bindings = NULL;
static s32 s_BindingCount = 0;
static s32 s_BindingCapacity = 0;
static void *s_primaryFileLoad(const char *path, u32 *out_size);

static const char *s_jsonSkipWs(const char *p, const char *end)
{
    while (p && p < end && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

/*
 * Small bounded reader for the intentionally flat typed-source records in
 * material.json, skin.json, swatches.json, physics.json, behavior.graph.json,
 * and layout.json. It does not mutate source bytes and never scans past size.
 */
static const char *s_jsonFindValue(const char *json, u32 size, const char *key)
{
    const char *end;
    size_t key_len;

    if (!json || size == 0 || !key || !key[0]) {
        return NULL;
    }
    end = json + size;
    key_len = strlen(key);

    for (const char *p = json; p + key_len + 2 < end; p++) {
        if (*p != '"' || (size_t)(end - p) < key_len + 2 ||
                memcmp(p + 1, key, key_len) != 0 ||
                p[key_len + 1] != '"') {
            continue;
        }
        p = s_jsonSkipWs(p + key_len + 2, end);
        if (p >= end || *p != ':') {
            continue;
        }
        return s_jsonSkipWs(p + 1, end);
    }
    return NULL;
}

static s32 s_jsonReadFloat(const char *json, u32 size, const char *key,
                           f32 *out)
{
    const char *p = s_jsonFindValue(json, size, key);
    const char *end = json ? json + size : NULL;
    char *parsed_end = NULL;
    float value;

    if (!p || !end || p >= end || !out) {
        return 0;
    }
    value = strtof(p, &parsed_end);
    if (parsed_end == p || parsed_end > end) {
        return 0;
    }
    *out = value;
    return 1;
}

static s32 s_jsonReadInt(const char *json, u32 size, const char *key,
                         s32 *out)
{
    f32 value = 0.0f;
    if (!out || !s_jsonReadFloat(json, size, key, &value)) {
        return 0;
    }
    *out = (s32)value;
    return 1;
}

static s32 s_jsonReadBool(const char *json, u32 size, const char *key,
                          s32 *out)
{
    const char *p = s_jsonFindValue(json, size, key);
    const char *end = json ? json + size : NULL;
    if (!p || !end || !out) return 0;
    if (end - p >= 4 && memcmp(p, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if (end - p >= 5 && memcmp(p, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static s32 s_jsonReadString(const char *json, u32 size, const char *key,
                            char *out, size_t out_cap)
{
    const char *p = s_jsonFindValue(json, size, key);
    const char *end = json ? json + size : NULL;
    size_t wrote = 0;

    if (!p || !end || !out || out_cap == 0 || p >= end || *p != '"') {
        return 0;
    }
    p++;
    while (p < end && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\') {
            if (p >= end) return 0;
            ch = (unsigned char)*p++;
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
            else if (ch != '\\' && ch != '"' && ch != '/') return 0;
        }
        if (wrote + 1 >= out_cap) return 0;
        out[wrote++] = (char)ch;
    }
    if (p >= end || *p != '"') return 0;
    out[wrote] = '\0';
    return 1;
}

static s32 s_jsonReadFloatArray(const char *json, u32 size, const char *key,
                                f32 *out, s32 count)
{
    const char *p = s_jsonFindValue(json, size, key);
    const char *end = json ? json + size : NULL;

    if (!p || !end || !out || count <= 0 || p >= end || *p != '[') {
        return 0;
    }
    p++;
    for (s32 i = 0; i < count; i++) {
        char *parsed_end = NULL;
        p = s_jsonSkipWs(p, end);
        if (!p || p >= end) return 0;
        out[i] = strtof(p, &parsed_end);
        if (parsed_end == p || parsed_end > end) return 0;
        p = s_jsonSkipWs(parsed_end, end);
        if (i + 1 < count) {
            if (p >= end || *p != ',') return 0;
            p++;
        }
    }
    p = s_jsonSkipWs(p, end);
    return p < end && *p == ']';
}

static s32 s_sourceMemberPath(const char *primary_path, const char *member,
                              char *out, size_t out_cap)
{
    const char *sep;
    const char *slash;
    size_t prefix_len;

    if (!primary_path || !primary_path[0] || !member || !member[0] ||
            !out || out_cap == 0) {
        return 0;
    }

    if (strstr(member, "::")) {
        if (strlen(member) + 1 > out_cap) return 0;
        memcpy(out, member, strlen(member) + 1);
        return 1;
    }

    sep = strstr(primary_path, "::");
    if (sep) {
        prefix_len = (size_t)(sep - primary_path);
        if (prefix_len + 2 + strlen(member) + 1 > out_cap) return 0;
        memcpy(out, primary_path, prefix_len);
        out[prefix_len] = '\0';
        snprintf(out + prefix_len, out_cap - prefix_len, "::%s", member);
        return 1;
    }

    slash = strrchr(primary_path, '/');
    {
        const char *backslash = strrchr(primary_path, '\\');
        if (!slash || (backslash && backslash > slash)) slash = backslash;
    }
    prefix_len = slash ? (size_t)(slash - primary_path + 1) : 0;
    if (prefix_len + strlen(member) + 1 > out_cap) return 0;
    if (prefix_len) memcpy(out, primary_path, prefix_len);
    memcpy(out + prefix_len, member, strlen(member) + 1);
    return 1;
}

static char *s_loadSourceMember(const asset_runtime_binding_t *binding,
                                const char *member, u32 *out_size)
{
    char path[FS_MAXPATH];
    void *bytes;
    u32 size = 0;
    char *text;

    if (out_size) *out_size = 0;
    if (!binding || !s_sourceMemberPath(binding->primary_path, member,
            path, sizeof(path))) {
        return NULL;
    }
    bytes = s_primaryFileLoad(path, &size);
    if (!bytes) return NULL;
    text = (char *)malloc((size_t)size + 1u);
    if (!text) {
        free(bytes);
        return NULL;
    }
    if (size) memcpy(text, bytes, size);
    text[size] = '\0';
    free(bytes);
    if (out_size) *out_size = size;
    return text;
}

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

static s32 s_hasSuffix(const char *s, const char *suffix)
{
    size_t s_len;
    size_t suffix_len;

    if (!s || !suffix) {
        return 0;
    }
    s_len = strlen(s);
    suffix_len = strlen(suffix);
    if (s_len < suffix_len) {
        return 0;
    }
    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

static s32 s_fontSourceComplete(const asset_runtime_binding_t *binding)
{
    if (!binding || !s_hasText(binding->authored_file)) {
        return 0;
    }
    if (s_hasSuffix(binding->authored_file, ".pgm")) {
        return s_hasText(binding->dependency_a);
    }
    return 1;
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
	propGraphRuntimeReset();
    if (s_Bindings && s_BindingCapacity > 0) {
        memset(s_Bindings, 0,
               (size_t)s_BindingCapacity * sizeof(s_Bindings[0]));
    }
    s_BindingCount = 0;
}

s32 assetRuntimeSupportsType(asset_type_e type)
{
    switch (type) {
    case ASSET_ARENA:
    case ASSET_BODY:
    case ASSET_HEAD:
    case ASSET_CHARACTER:
    case ASSET_SKIN:
    case ASSET_EFFECT:
    case ASSET_PROP:
    case ASSET_VEHICLE:
    case ASSET_MISSION:
    case ASSET_WEAPON:
    case ASSET_PROJECTILE:
    case ASSET_ENTITY:
    case ASSET_GAMEMODE:
    case ASSET_BOT_PROFILE:
    case ASSET_HUD:
    case ASSET_MATERIAL:
    case ASSET_UI:
    case ASSET_FONT:
    case ASSET_LANG:
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

    if (s_BindingCount >= s_BindingCapacity) {
        s32 new_capacity = s_BindingCapacity > 0
            ? s_BindingCapacity * 2
            : ASSET_RUNTIME_INITIAL_BINDINGS;
        asset_runtime_binding_t *new_bindings;

        if (new_capacity <= s_BindingCapacity) {
            return NULL;
        }

        new_bindings = (asset_runtime_binding_t *)realloc(s_Bindings,
            (size_t)new_capacity * sizeof(s_Bindings[0]));
        if (!new_bindings) {
            return NULL;
        }
        memset(new_bindings + s_BindingCapacity, 0,
               (size_t)(new_capacity - s_BindingCapacity)
                    * sizeof(new_bindings[0]));
        s_Bindings = new_bindings;
        s_BindingCapacity = new_capacity;
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
    binding->source_enabled = 1;
    binding->source_opacity = 1.0f;
    s_copy(binding->id, sizeof(binding->id), entry->id);
    s_copy(binding->primary_path, sizeof(binding->primary_path), primary_path);

    switch (entry->type) {
    case ASSET_ARENA:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.arena.scenario_archive);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.arena.scenario_id);
        binding->runtime_id = entry->ext.arena.stagenum;
        binding->kind = entry->ext.arena.load_mode;
        binding->target_kind = entry->ext.arena.requirefeature;
        binding->name_langid = entry->ext.arena.name_langid;
        binding->requirefeature = entry->ext.arena.requirefeature;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_BODY:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.body.mesh_archive);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.body.hand_archive);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.body.rig_class);
        binding->runtime_id = entry->ext.body.bodynum;
        binding->kind = entry->ext.body.headnum;
        s_copy(binding->display_name, sizeof(binding->display_name),
               entry->ext.body.display_name);
        binding->name_langid = entry->ext.body.name_langid;
        binding->requirefeature = entry->ext.body.requirefeature;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_HEAD:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.head.mesh_archive);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.head.rig_class);
        binding->runtime_id = entry->ext.head.headnum;
        binding->requirefeature = entry->ext.head.requirefeature;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_CHARACTER:
		s_copy(binding->character_body_id,
		       sizeof(binding->character_body_id),
		       entry->ext.character.body_id);
		s_copy(binding->character_head_id,
		       sizeof(binding->character_head_id),
		       entry->ext.character.head_id);
		s_copy(binding->display_name, sizeof(binding->display_name),
		       entry->ext.character.display_name);
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.character.bodyfile);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.character.headfile);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.character.portrait_file);
        return s_finishFileBinding(binding,
            s_hasText(binding->character_body_id) &&
            s_hasText(binding->character_head_id) &&
            s_hasText(binding->authored_file) &&
            s_hasText(binding->dependency_a));

    case ASSET_SKIN:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.skin.skin_file[0]
                    ? entry->ext.skin.skin_file
                    : entry->ext.skin.texture_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.skin.texture_file);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.skin.swatches_file);
        s_copy(binding->dependency_c, sizeof(binding->dependency_c),
               entry->ext.skin.material_archive);
        s_copy(binding->dependency_d, sizeof(binding->dependency_d),
               entry->ext.skin.texture_archive);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.skin.target_id);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, binding->dependency_a,
                         binding->dependency_b, NULL));

    case ASSET_EFFECT:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.effect.effect_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.effect.timeline_file);
        s_copy(binding->shader_id, sizeof(binding->shader_id),
               entry->ext.effect.shader_id);
        binding->kind = entry->ext.effect.effect_type;
        binding->target_kind = entry->ext.effect.target;
        binding->value0 = entry->ext.effect.intensity;
        for (s32 i = 0; i < 4; i++) {
            binding->params[i] = entry->ext.effect.params[i];
        }
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, binding->dependency_a,
                         NULL, NULL));

    case ASSET_VEHICLE:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.vehicle.model_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.vehicle.physics_file);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.vehicle.behavior_graph);
        binding->runtime_id = entry->runtime_index;
        return s_finishFileBinding(binding,
            s_hasText(binding->dependency_a) &&
            s_hasAnyFile(binding->authored_file, binding->dependency_b,
                         NULL, NULL));

    case ASSET_PROP:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.prop.model_file[0]
                    ? entry->ext.prop.model_file
                    : entry->ext.prop.prop_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.prop.model_file[0] ? entry->ext.prop.prop_file : "");
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.prop.behavior_graph);
        binding->runtime_id = entry->ext.prop.prop_type;
        binding->kind = entry->ext.prop.prop_type;
        binding->target_kind = (s32)entry->ext.prop.flags;
        binding->value0 = entry->ext.prop.health;
        s_copy(binding->display_name, sizeof(binding->display_name),
               entry->ext.prop.name);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_WEAPON:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.weapon.primary_graph);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.weapon.secondary_graph);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.weapon.shared_context);
        s_copy(binding->dependency_c, sizeof(binding->dependency_c),
               entry->ext.weapon.settings_file);
        s_copy(binding->dependency_d, sizeof(binding->dependency_d),
               entry->ext.weapon.variables_file);
        s_copy(binding->dependency_e, sizeof(binding->dependency_e),
               entry->ext.weapon.model_file);
        s_copy(binding->weapon_model_file, sizeof(binding->weapon_model_file),
               entry->ext.weapon.model_file);
        binding->runtime_id = entry->runtime_index;
        binding->kind = entry->ext.weapon.weapon_id;
        binding->target_kind = entry->ext.weapon.requirefeature;
        binding->value0 = (f32)entry->ext.weapon.dual_wieldable;
        binding->params[0] = (f32)entry->mp_index;
        s_copy(binding->display_name, sizeof(binding->display_name),
               entry->ext.weapon.name);
        binding->requirefeature = entry->ext.weapon.requirefeature;
        return s_finishFileBinding(binding,
            s_hasText(binding->authored_file) &&
            s_hasText(binding->dependency_a) &&
            s_hasText(binding->dependency_b) &&
            s_hasText(binding->dependency_c) &&
            s_hasText(binding->dependency_d));

    case ASSET_PROJECTILE:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.projectile.behavior_graph);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.projectile.model_file);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.projectile.entity_ref);
        s_copy(binding->display_name, sizeof(binding->display_name),
               entry->ext.projectile.name);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_ENTITY:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.entity.behavior_graph);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.entity.model_file);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.entity.archetype);
        s_copy(binding->display_name, sizeof(binding->display_name),
               entry->ext.entity.name);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_MISSION:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.mission.mission_graph_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.mission.scenario_archive);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.mission.objectives_file);
        return s_finishFileBinding(binding,
            s_hasText(binding->authored_file) &&
            s_hasText(binding->dependency_a) &&
            s_hasText(binding->dependency_b));

    case ASSET_GAMEMODE:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.gamemode.rules_file);
        s_copy(binding->gamemode_name, sizeof(binding->gamemode_name),
               entry->ext.gamemode.name);
        s_copy(binding->gamemode_description,
               sizeof(binding->gamemode_description),
               entry->ext.gamemode.description);
        binding->runtime_id = entry->ext.gamemode.mode_id;
        binding->kind = entry->ext.gamemode.team_based;
        binding->target_kind = entry->ext.gamemode.min_players;
        binding->gamemode_min_players = entry->ext.gamemode.min_players;
        binding->gamemode_max_players = entry->ext.gamemode.max_players;
        binding->gamemode_team_based = entry->ext.gamemode.team_based;
        binding->gamemode_requirefeature = entry->ext.gamemode.requirefeature;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_BOT_PROFILE:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.bot_profile.profile_file);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.bot_profile.target_body);
        binding->runtime_id = entry->ext.bot_profile.type;
        binding->kind = entry->ext.bot_profile.difficulty;
        binding->target_kind = entry->ext.bot_profile.body;
        binding->bot_profile_type = entry->ext.bot_profile.type;
        binding->bot_profile_difficulty = entry->ext.bot_profile.difficulty;
        binding->bot_profile_body = entry->ext.bot_profile.body;
        binding->bot_profile_name_langid = entry->ext.bot_profile.name_langid;
        binding->bot_profile_requirefeature = entry->ext.bot_profile.requirefeature;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_HUD:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.hud.layout_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.hud.texture_file);
        binding->runtime_id = entry->ext.hud.hud_id;
        binding->kind = entry->ext.hud.element_type;
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_UI:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.ui.texture_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.ui.layout_file);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.ui.nineslice_file);
        s_copy(binding->target_id, sizeof(binding->target_id),
               entry->ext.ui.texture_name);
        binding->ui_width = entry->ext.ui.width;
        binding->ui_height = entry->ext.ui.height;
        binding->ui_data_size = entry->ext.ui.data_size;
        binding->ui_nineslice_left = entry->ext.ui.nineslice_left;
        binding->ui_nineslice_right = entry->ext.ui.nineslice_right;
        binding->ui_nineslice_top = entry->ext.ui.nineslice_top;
        binding->ui_nineslice_bottom = entry->ext.ui.nineslice_bottom;
        s_copy(binding->ui_nineslice_edge_mode,
               sizeof(binding->ui_nineslice_edge_mode),
               entry->ext.ui.nineslice_edge_mode);
        s_copy(binding->ui_nineslice_center_mode,
               sizeof(binding->ui_nineslice_center_mode),
               entry->ext.ui.nineslice_center_mode);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_MATERIAL:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.material.material_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.material.texture_archive);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.material.effect_archive);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    case ASSET_FONT:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.font.font_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.font.metrics_file);
        return s_finishFileBinding(binding, s_fontSourceComplete(binding));

    case ASSET_LANG:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.lang.strings_file);
        s_copy(binding->lang_locale, sizeof(binding->lang_locale),
               entry->ext.lang.locale);
        s_copy(binding->lang_category, sizeof(binding->lang_category),
               entry->ext.lang.lang_category);
        binding->runtime_id = entry->ext.lang.bank_id;
        binding->lang_string_count = entry->ext.lang.string_count;
        return s_finishFileBinding(binding,
            s_hasText(binding->authored_file) && entry->ext.lang.bank_id >= 0);

    case ASSET_SCENARIO:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.scenario.scene_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.scenario.collision_file);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.scenario.level_graph_file);
        s_copy(binding->scenario_rooms_file,
               sizeof(binding->scenario_rooms_file),
               entry->ext.scenario.rooms_file);
        s_copy(binding->scenario_portals_file,
               sizeof(binding->scenario_portals_file),
               entry->ext.scenario.portals_file);
        s_copy(binding->scenario_pads_file,
               sizeof(binding->scenario_pads_file),
               entry->ext.scenario.pads_file);
        s_copy(binding->scenario_spawns_file,
               sizeof(binding->scenario_spawns_file),
               entry->ext.scenario.spawns_file);
        s_copy(binding->scenario_volumes_file,
               sizeof(binding->scenario_volumes_file),
               entry->ext.scenario.volumes_file);
        s_copy(binding->scenario_objects_file,
               sizeof(binding->scenario_objects_file),
               entry->ext.scenario.objects_file);
        s_copy(binding->scenario_setup_fields_file,
               sizeof(binding->scenario_setup_fields_file),
               entry->ext.scenario.setup_fields_file);
        s_copy(binding->scenario_ai_lists_file,
               sizeof(binding->scenario_ai_lists_file),
               entry->ext.scenario.ai_lists_file);
        s_copy(binding->scenario_objectives_file,
               sizeof(binding->scenario_objectives_file),
               entry->ext.scenario.objectives_file);
        s_copy(binding->scenario_navigation_file,
               sizeof(binding->scenario_navigation_file),
               entry->ext.scenario.navigation_file);
        s_copy(binding->scenario_navigation_waypoints_file,
               sizeof(binding->scenario_navigation_waypoints_file),
               entry->ext.scenario.navigation_waypoints_file);
        s_copy(binding->scenario_navigation_waygroups_file,
               sizeof(binding->scenario_navigation_waygroups_file),
               entry->ext.scenario.navigation_waygroups_file);
        s_copy(binding->scenario_navigation_covers_file,
               sizeof(binding->scenario_navigation_covers_file),
               entry->ext.scenario.navigation_covers_file);
        s_copy(binding->scenario_navigation_paths_file,
               sizeof(binding->scenario_navigation_paths_file),
               entry->ext.scenario.navigation_paths_file);
        binding->runtime_id = entry->ext.scenario.stagenum;
        binding->kind = entry->ext.scenario.mode;
        return s_finishFileBinding(binding,
            s_hasText(binding->authored_file) &&
            s_hasText(binding->dependency_b) &&
            s_hasText(binding->scenario_portals_file) &&
            s_hasText(binding->scenario_pads_file) &&
            s_hasText(binding->scenario_spawns_file) &&
            s_hasText(binding->scenario_volumes_file) &&
            s_hasText(binding->scenario_objects_file) &&
            s_hasText(binding->scenario_setup_fields_file) &&
            s_hasText(binding->scenario_ai_lists_file) &&
            s_hasText(binding->scenario_objectives_file) &&
            s_hasText(binding->scenario_navigation_file) &&
            s_hasText(binding->scenario_navigation_waypoints_file) &&
            s_hasText(binding->scenario_navigation_waygroups_file) &&
            s_hasText(binding->scenario_navigation_covers_file) &&
            s_hasText(binding->scenario_navigation_paths_file));

    case ASSET_THEME:
        s_copy(binding->authored_file, sizeof(binding->authored_file),
               entry->ext.theme.theme_file);
        s_copy(binding->dependency_a, sizeof(binding->dependency_a),
               entry->ext.theme.ui_archive);
        s_copy(binding->dependency_b, sizeof(binding->dependency_b),
               entry->ext.theme.font_archive);
        s_copy(binding->dependency_c, sizeof(binding->dependency_c),
               entry->ext.theme.audio_archive);
        s_copy(binding->dependency_d, sizeof(binding->dependency_d),
               entry->ext.theme.music_archive);
        s_copy(binding->dependency_e, sizeof(binding->dependency_e),
               entry->ext.theme.effect_archive);
        return s_finishFileBinding(binding,
            s_hasAnyFile(binding->authored_file, NULL, NULL, NULL));

    default:
        binding->active = 0;
        return 0;
    }
}

static s32 s_sourceCatalogIdMatches(const asset_runtime_binding_t *binding,
                                    const char *json, u32 size)
{
    char catalog_id[CATALOG_ID_LEN];
    return binding && s_jsonReadString(json, size, "catalog_id",
        catalog_id, sizeof(catalog_id)) &&
        strncmp(catalog_id, binding->id, sizeof(catalog_id)) == 0;
}

static s32 s_hydrateHud(asset_runtime_binding_t *binding)
{
    u32 size = 0;
    char *json = s_loadSourceMember(binding, binding->authored_file, &size);
    char element[32];
    s32 enabled = 0;
    s32 ok;

    if (!json) return 0;
    ok = s_sourceCatalogIdMatches(binding, json, size) &&
        s_jsonReadString(json, size, "element", element, sizeof(element)) &&
        s_jsonReadBool(json, size, "visible", &enabled);
    if (ok) {
        static const char *const keys[] = {
            "crosshair", "ammo", "radar", "health", "timer", "score",
        };
        if (binding->kind < HUD_ELEM_CROSSHAIR ||
                binding->kind > HUD_ELEM_SCORE ||
                strcmp(element, keys[binding->kind]) != 0) {
            ok = 0;
        }
    }
    if (ok && (binding->kind == HUD_ELEM_TIMER ||
            binding->kind == HUD_ELEM_SCORE)) {
        f32 opacity = 1.0f;
        if (!s_jsonReadFloat(json, size, "opacity", &opacity) ||
                opacity < 0.0f || opacity > 1.0f) {
            ok = 0;
        } else {
            binding->source_opacity = opacity;
        }
    }
    if (ok) {
        binding->source_enabled = enabled;
        binding->source_hydrated = 1;
    }
    free(json);
    return ok;
}

static s32 s_hydrateMaterial(asset_runtime_binding_t *binding)
{
    u32 size = 0;
    char *json = s_loadSourceMember(binding, binding->authored_file, &size);
    s32 ok;

    if (!json) return 0;
    ok = s_sourceCatalogIdMatches(binding, json, size) &&
        s_jsonReadString(json, size, "shading_model",
            binding->material_shading_model,
            sizeof(binding->material_shading_model)) &&
        s_jsonReadFloatArray(json, size, "base_color",
            binding->material_base_color, 4) &&
        s_jsonReadFloat(json, size, "roughness",
            &binding->material_roughness) &&
        s_jsonReadFloat(json, size, "metallic",
            &binding->material_metallic) &&
        s_jsonReadBool(json, size, "emissive",
            &binding->material_emissive);
    if (ok) {
        for (s32 i = 0; i < 4; i++) {
            if (binding->material_base_color[i] < 0.0f ||
                    binding->material_base_color[i] > 1.0f) {
                ok = 0;
            }
        }
        if (binding->material_roughness < 0.0f ||
                binding->material_roughness > 1.0f ||
                binding->material_metallic < 0.0f ||
                binding->material_metallic > 1.0f) {
            ok = 0;
        }
    }
    if (ok) binding->source_hydrated = 1;
    free(json);
    return ok;
}

static s32 s_hydrateSkin(asset_runtime_binding_t *binding)
{
    u32 skin_size = 0;
    u32 swatch_size = 0;
    char *skin = s_loadSourceMember(binding, binding->authored_file,
        &skin_size);
    char *swatches = s_loadSourceMember(binding, binding->dependency_b,
        &swatch_size);
    char target[CATALOG_ID_LEN];
    s32 ok;

    if (!skin || !swatches) {
        free(skin);
        free(swatches);
        return 0;
    }
    ok = s_sourceCatalogIdMatches(binding, skin, skin_size) &&
        s_jsonReadString(skin, skin_size, "target", target,
            sizeof(target)) &&
        strcmp(target, binding->target_id) == 0 &&
        s_jsonReadString(skin, skin_size, "material",
            binding->skin_material_id, sizeof(binding->skin_material_id)) &&
        s_jsonReadFloatArray(swatches, swatch_size, "rgba",
            binding->skin_swatch_color, 4);
    if (ok) {
        for (s32 i = 0; i < 4; i++) {
            if (binding->skin_swatch_color[i] < 0.0f ||
                    binding->skin_swatch_color[i] > 1.0f) {
                ok = 0;
            }
        }
    }
    if (ok) binding->source_hydrated = 1;
    free(skin);
    free(swatches);
    return ok;
}

static s32 s_propKindFromKey(const char *key)
{
    static const char *keys[] = {
        "", "object", "door", "character", "weapon_pickup",
        "eyespy", "player", "explosion", "smoke"
    };
    if (!key) return -1;
    for (s32 i = 1; i < (s32)(sizeof(keys) / sizeof(keys[0])); i++) {
        if (strcmp(key, keys[i]) == 0) return i;
    }
    return -1;
}

static s32 s_gamemodeFromKey(const char *key)
{
    static const char *keys[] = {
        "combat", "hold_the_briefcase", "hacker_central",
        "pop_a_cap", "king_of_the_hill", "capture_the_case"
    };
    if (!key) return -1;
    for (s32 i = 0; i < (s32)(sizeof(keys) / sizeof(keys[0])); i++) {
        if (strcmp(key, keys[i]) == 0) return i;
    }
    return -1;
}

static s32 s_botTypeFromKey(const char *key)
{
    static const char *keys[] = {
        "general", "peace", "shield", "rocket", "kaze", "fist", "prey",
        "coward", "judge", "feud", "speed", "turtle", "venge"
    };
    if (!key) return -1;
    for (s32 i = 0; i < (s32)(sizeof(keys) / sizeof(keys[0])); i++) {
        if (strcmp(key, keys[i]) == 0) return i;
    }
    return -1;
}

static s32 s_botDifficultyFromKey(const char *key)
{
    static const char *keys[] = {
        "meat", "easy", "normal", "hard", "perfect", "dark"
    };
    if (!key) return -1;
    for (s32 i = 0; i < (s32)(sizeof(keys) / sizeof(keys[0])); i++) {
        if (strcmp(key, keys[i]) == 0) return i;
    }
    return -1;
}

static s32 s_hydrateProp(asset_runtime_binding_t *binding)
{
    u32 size = 0;
    char *json = s_loadSourceMember(binding, binding->dependency_a[0]
        ? binding->dependency_a : binding->authored_file, &size);
    char schema[48];
    char key[32];
    s32 flags = 0;
    s32 kind;
    s32 ok;
	char graph_err[256];

    if (!json) return 0;
    ok = s_jsonReadString(json, size, "schema", schema, sizeof(schema)) &&
        strcmp(schema, "pd2.prop.v2") == 0 &&
        s_sourceCatalogIdMatches(binding, json, size) &&
        s_jsonReadString(json, size, "prop_key", key, sizeof(key)) &&
        (kind = s_propKindFromKey(key)) >= 0 &&
        s_jsonReadString(json, size, "display_name", binding->display_name,
            sizeof(binding->display_name)) &&
        s_jsonReadFloat(json, size, "health", &binding->prop_health) &&
        binding->prop_health >= 0.0f &&
        s_jsonReadInt(json, size, "flags", &flags) && flags >= 0;
    free(json);
    if (ok) {
        binding->kind = kind;
        binding->runtime_id = kind;
        binding->prop_flags = (u32)flags;
        binding->source_hydrated = 1;
		if (binding->dependency_b[0]) {
			u32 graph_size = 0;
			char *graph = s_loadSourceMember(binding, binding->dependency_b,
				&graph_size);
			if (!graph) {
				binding->source_hydrated = 0;
				return 0;
			}
			graph_err[0] = '\0';
			ok = propGraphRuntimeRegisterJson(binding->id, graph, graph_size,
				graph_err, sizeof(graph_err)) == 0;
			free(graph);
			if (!ok) {
#ifndef PD_TESTS
				fprintf(stderr, "ASSET.PROP.GRAPH: %s rejected: %s\n",
					binding->id, graph_err[0] ? graph_err : "invalid graph");
#endif
				binding->source_hydrated = 0;
			}
		}
    }
    return ok;
}

static s32 s_hydrateGamemode(asset_runtime_binding_t *binding)
{
    u32 size = 0;
    char *json = s_loadSourceMember(binding, binding->authored_file, &size);
    char schema[48];
    char mode_key[32];
    s32 required = 0;
    s32 min_players = 0;
    s32 max_players = 0;
    s32 requirefeature = 0;
    s32 mode;
    s32 ok;

    if (!json) return 0;
    ok = s_jsonReadString(json, size, "schema", schema, sizeof(schema)) &&
        strcmp(schema, "pd2.gamemode.rules.v2") == 0 &&
        s_sourceCatalogIdMatches(binding, json, size) &&
        s_jsonReadString(json, size, "mode_key", mode_key,
            sizeof(mode_key)) &&
        (mode = s_gamemodeFromKey(mode_key)) >= 0 &&
        s_jsonReadString(json, size, "name", binding->gamemode_name,
            sizeof(binding->gamemode_name)) &&
        s_jsonReadString(json, size, "description",
            binding->gamemode_description,
            sizeof(binding->gamemode_description)) &&
        s_jsonReadInt(json, size, "min", &min_players) &&
        s_jsonReadInt(json, size, "max", &max_players) &&
        min_players >= 1 && max_players >= min_players && max_players <= 32 &&
        s_jsonReadBool(json, size, "required", &required) &&
        s_jsonReadInt(json, size, "requirefeature", &requirefeature) &&
        requirefeature >= 0 && requirefeature <= 255;
    free(json);
    if (ok) {
        binding->runtime_id = mode;
        binding->gamemode_min_players = min_players;
        binding->gamemode_max_players = max_players;
        binding->gamemode_team_based = required;
        binding->gamemode_requirefeature = requirefeature;
        binding->source_hydrated = 1;
    }
    return ok;
}

static s32 s_hydrateBotProfile(asset_runtime_binding_t *binding)
{
    u32 size = 0;
    char *json = s_loadSourceMember(binding, binding->authored_file, &size);
    char schema[48];
    char type_key[32];
    char difficulty_key[32];
    s32 requirefeature = 0;
    s32 type;
    s32 difficulty;
    s32 ok;

    if (!json) return 0;
    ok = s_jsonReadString(json, size, "schema", schema, sizeof(schema)) &&
        strcmp(schema, "pd2.botprofile.v2") == 0 &&
        s_sourceCatalogIdMatches(binding, json, size) &&
        s_jsonReadString(json, size, "type_key", type_key,
            sizeof(type_key)) &&
        (type = s_botTypeFromKey(type_key)) >= 0 &&
        s_jsonReadString(json, size, "difficulty_key", difficulty_key,
            sizeof(difficulty_key)) &&
        (difficulty = s_botDifficultyFromKey(difficulty_key)) >= 0 &&
        s_jsonReadString(json, size, "target_body", binding->target_id,
            sizeof(binding->target_id)) &&
        strchr(binding->target_id, ':') != NULL &&
        s_jsonReadInt(json, size, "requirefeature", &requirefeature) &&
        requirefeature >= 0 && requirefeature <= 255;
    free(json);
    if (ok) {
        binding->runtime_id = type;
        binding->kind = difficulty;
        binding->bot_profile_type = type;
        binding->bot_profile_difficulty = difficulty;
        binding->bot_profile_requirefeature = requirefeature;
        binding->source_hydrated = 1;
    }
    return ok;
}

static s32 s_hydrateVehicle(asset_runtime_binding_t *binding)
{
    u32 physics_size = 0;
    u32 behavior_size = 0;
    char *physics = s_loadSourceMember(binding, binding->dependency_a,
        &physics_size);
    char *behavior = s_loadSourceMember(binding, binding->dependency_b,
        &behavior_size);
    s32 ok;

    if (!physics || !behavior) {
        free(physics);
        free(behavior);
        return 0;
    }
    ok = s_sourceCatalogIdMatches(binding, physics, physics_size) &&
        s_sourceCatalogIdMatches(binding, behavior, behavior_size) &&
        s_jsonReadFloat(physics, physics_size, "turn_input_scale",
            &binding->vehicle_turn_input_scale) &&
        s_jsonReadFloat(physics, physics_size, "reverse_turn_gain",
            &binding->vehicle_reverse_turn_gain) &&
        s_jsonReadFloat(physics, physics_size, "steering_response_ntsc",
            &binding->vehicle_steering_response_ntsc) &&
        s_jsonReadFloat(physics, physics_size, "steering_response_pal",
            &binding->vehicle_steering_response_pal) &&
        s_jsonReadFloat(physics, physics_size, "turn_visual_scale",
            &binding->vehicle_turn_visual_scale) &&
        s_jsonReadFloat(physics, physics_size, "input_response",
            &binding->vehicle_input_response) &&
        s_jsonReadFloat(physics, physics_size, "forward_input_scale",
            &binding->vehicle_forward_input_scale) &&
        s_jsonReadFloat(physics, physics_size, "lateral_input_scale",
            &binding->vehicle_lateral_input_scale) &&
        s_jsonReadFloat(physics, physics_size, "lean_response",
            &binding->vehicle_lean_response) &&
        s_jsonReadFloat(physics, physics_size, "forward_base",
            &binding->vehicle_forward_base) &&
        s_jsonReadFloat(physics, physics_size, "forward_accel_gain",
            &binding->vehicle_forward_accel_gain) &&
        s_jsonReadFloat(physics, physics_size, "reverse_base",
            &binding->vehicle_reverse_base) &&
        s_jsonReadFloat(physics, physics_size, "drag_ntsc",
            &binding->vehicle_drag_ntsc) &&
        s_jsonReadFloat(physics, physics_size, "drag_pal",
            &binding->vehicle_drag_pal) &&
        s_jsonReadFloat(physics, physics_size, "forward_thrust",
            &binding->vehicle_forward_thrust) &&
        s_jsonReadFloat(physics, physics_size, "lateral_thrust",
            &binding->vehicle_lateral_thrust) &&
        s_jsonReadFloat(physics, physics_size, "forward_tilt",
            &binding->vehicle_forward_tilt) &&
        s_jsonReadFloat(physics, physics_size, "lateral_tilt",
            &binding->vehicle_lateral_tilt) &&
        s_jsonReadFloat(physics, physics_size, "tilt_response_ntsc",
            &binding->vehicle_tilt_response_ntsc) &&
        s_jsonReadFloat(physics, physics_size, "tilt_response_pal",
            &binding->vehicle_tilt_response_pal) &&
        s_jsonReadFloat(physics, physics_size, "yaw_response_ntsc",
            &binding->vehicle_yaw_response_ntsc) &&
        s_jsonReadFloat(physics, physics_size, "yaw_response_pal",
            &binding->vehicle_yaw_response_pal) &&
        s_jsonReadFloat(physics, physics_size, "boost_speed",
            &binding->vehicle_boost_speed) &&
        s_jsonReadInt(physics, physics_size, "boost_time_ticks60",
            &binding->vehicle_boost_time_ticks60) &&
        s_jsonReadFloatArray(physics, physics_size, "hover",
            binding->vehicle_hover, 13) &&
        s_jsonReadBool(behavior, behavior_size, "allow_mount",
            &binding->vehicle_allow_mount) &&
        s_jsonReadBool(behavior, behavior_size, "allow_drive",
            &binding->vehicle_allow_drive) &&
        s_jsonReadBool(behavior, behavior_size, "allow_dismount",
            &binding->vehicle_allow_dismount);
    if (ok) {
        if (binding->vehicle_turn_input_scale < 0.0f ||
                binding->vehicle_reverse_turn_gain < 0.0f ||
                binding->vehicle_steering_response_ntsc < 0.0f ||
                binding->vehicle_steering_response_ntsc > 1.0f ||
                binding->vehicle_steering_response_pal < 0.0f ||
                binding->vehicle_steering_response_pal > 1.0f ||
                binding->vehicle_input_response < 0.0f ||
                binding->vehicle_forward_input_scale < 0.0f ||
                binding->vehicle_lateral_input_scale < 0.0f ||
                binding->vehicle_lean_response < 0.0f ||
                binding->vehicle_forward_base < 0.0f ||
                binding->vehicle_forward_accel_gain < 0.0f ||
                binding->vehicle_reverse_base < 0.0f ||
                binding->vehicle_drag_ntsc < 0.0f ||
                binding->vehicle_drag_ntsc > 1.0f ||
                binding->vehicle_drag_pal < 0.0f ||
                binding->vehicle_drag_pal > 1.0f ||
                binding->vehicle_boost_speed < 0.0f ||
                binding->vehicle_boost_time_ticks60 <= 0) {
            ok = 0;
        }
    }
    if (ok) binding->source_hydrated = 1;
    free(physics);
    free(behavior);
    return ok;
}

s32 assetRuntimeHydrateCatalogEntry(const asset_entry_t *entry)
{
    asset_runtime_binding_t *binding;
    s32 ok = 1;

    if (!entry || !entry->id[0]) return 0;
    binding = s_findMutable(entry->id);
    if (!binding || binding->type != entry->type) return 0;

    switch (entry->type) {
    case ASSET_HUD:      ok = s_hydrateHud(binding); break;
    case ASSET_MATERIAL: ok = s_hydrateMaterial(binding); break;
    case ASSET_SKIN:     ok = s_hydrateSkin(binding); break;
    case ASSET_PROP:     ok = s_hydrateProp(binding); break;
    case ASSET_VEHICLE:  ok = s_hydrateVehicle(binding); break;
    case ASSET_GAMEMODE: ok = s_hydrateGamemode(binding); break;
    case ASSET_BOT_PROFILE: ok = s_hydrateBotProfile(binding); break;
    default:
        return 1;
    }
    if (!ok) {
        binding->active = 0;
    }
    return ok;
}

const asset_runtime_binding_t *assetRuntimeFindByRuntimeId(asset_type_e type,
                                                           s32 runtime_id)
{
    for (s32 i = s_BindingCount - 1; i >= 0; i--) {
        if (s_Bindings[i].active && s_Bindings[i].type == type &&
                s_Bindings[i].runtime_id == runtime_id) {
            return &s_Bindings[i];
        }
    }
    return NULL;
}

const asset_runtime_binding_t *assetRuntimeHudElement(s32 element_type)
{
    for (s32 i = s_BindingCount - 1; i >= 0; i--) {
        if (s_Bindings[i].active && s_Bindings[i].source_hydrated &&
                s_Bindings[i].type == ASSET_HUD &&
                s_Bindings[i].kind == element_type) {
            return &s_Bindings[i];
        }
    }
    return NULL;
}

const asset_runtime_binding_t *assetRuntimeVehicleForModelnum(s32 modelnum)
{
    const asset_runtime_binding_t *binding =
        assetRuntimeFindByRuntimeId(ASSET_VEHICLE, modelnum);
    return binding && binding->source_hydrated ? binding : NULL;
}

s32 assetRuntimeHudElementEnabled(s32 element_type)
{
    const asset_runtime_binding_t *binding =
        assetRuntimeHudElement(element_type);
    return !binding || binding->source_enabled;
}

s32 assetRuntimeVehicleAllows(s32 modelnum, const char *action)
{
    const asset_runtime_binding_t *binding =
        assetRuntimeVehicleForModelnum(modelnum);
    if (!binding || !action) return 0;
    if (strcmp(action, "mount") == 0) return binding->vehicle_allow_mount;
    if (strcmp(action, "drive") == 0) return binding->vehicle_allow_drive;
    if (strcmp(action, "dismount") == 0) return binding->vehicle_allow_dismount;
    return 0;
}

s32 assetRuntimeVehicleHover(s32 modelnum, f32 out_values[13])
{
    const asset_runtime_binding_t *binding =
        assetRuntimeVehicleForModelnum(modelnum);
    if (!binding || !out_values) return 0;
    memcpy(out_values, binding->vehicle_hover,
        sizeof(binding->vehicle_hover));
    return 1;
}

s32 assetRuntimeSkinAppearance(const char *target_id, f32 out_rgba[4],
                               f32 *out_roughness, f32 *out_metallic,
                               s32 *out_emissive)
{
    const asset_runtime_binding_t *skin = NULL;
    const asset_runtime_binding_t *material;

    if (!target_id || !target_id[0] || !out_rgba || !out_roughness ||
            !out_metallic || !out_emissive) {
        return 0;
    }
    for (s32 i = s_BindingCount - 1; i >= 0; i--) {
        if (s_Bindings[i].active && s_Bindings[i].source_hydrated &&
                s_Bindings[i].type == ASSET_SKIN &&
                strcmp(s_Bindings[i].target_id, target_id) == 0) {
            skin = &s_Bindings[i];
            break;
        }
    }
    if (!skin || !skin->skin_material_id[0]) return 0;
    material = assetRuntimeFindByTypeAndId(ASSET_MATERIAL,
        skin->skin_material_id);
    if (!material || !material->source_hydrated) return 0;

    for (s32 i = 0; i < 4; i++) {
        out_rgba[i] = material->material_base_color[i] *
            skin->skin_swatch_color[i];
    }
    *out_roughness = material->material_roughness;
    *out_metallic = material->material_metallic;
    *out_emissive = material->material_emissive;
    return 1;
}

void assetRuntimeReleaseCatalogEntry(const char *asset_id)
{
    asset_runtime_binding_t *binding = s_findMutable(asset_id);
    if (binding) {
		if (binding->type == ASSET_PROP) {
			propGraphRuntimeClearAsset(asset_id);
		}
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
