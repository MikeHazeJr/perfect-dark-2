#include "character_head_policy.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include <string.h>

static int asciiAlpha(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int asciiAlnum(unsigned char c)
{
    return asciiAlpha(c) || (c >= '0' && c <= '9');
}

static int catalogIdValid(const char *id)
{
    const unsigned char *p = (const unsigned char *)id;
    if (!p || !asciiAlpha(*p) || strlen(id) >= CATALOG_ID_LEN) return 0;
    for (++p; *p && *p != ':'; ++p)
        if (!asciiAlnum(*p) && *p != '_' && *p != '.' && *p != '-') return 0;
    if (*p++ != ':' || !asciiAlnum(*p)) return 0;
    for (++p; *p; ++p)
        if (!asciiAlnum(*p) && *p != '_' && *p != '.' && *p != '-' && *p != ':') return 0;
    return 1;
}

static int utf8Valid(const unsigned char *p)
{
    if (!p) return 1;
    while (*p) {
        unsigned int code = *p++, minimum;
        int remaining;
        if (code < 0x80) continue;
        if (code >= 0xc2 && code <= 0xdf) { code &= 0x1f; remaining = 1; minimum = 0x80; }
        else if (code >= 0xe0 && code <= 0xef) { code &= 0x0f; remaining = 2; minimum = 0x800; }
        else if (code >= 0xf0 && code <= 0xf4) { code &= 7; remaining = 3; minimum = 0x10000; }
        else return 0;
        while (remaining--) {
            if ((*p & 0xc0) != 0x80) return 0;
            code = (code << 6) | (*p++ & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}

const char *characterHeadPolicyName(character_head_policy_e policy)
{
    switch (policy) {
    case CHARACTER_HEAD_POLICY_FIXED: return "fixed";
    case CHARACTER_HEAD_POLICY_RANDOM_GENDER: return "random_gender";
    case CHARACTER_HEAD_POLICY_INTEGRATED: return "integrated";
    default: return NULL;
    }
}

character_head_policy_e characterHeadPolicyResolve(const char *policy,
    const char *body_id, const char *head_id,
    const char *bodyfile, const char *headfile)
{
    character_head_policy_e resolved;
    if (!catalogIdValid(body_id) || !bodyfile || !bodyfile[0] ||
            strlen(bodyfile) >= FS_MAXPATH || !utf8Valid((const unsigned char *)bodyfile))
        return CHARACTER_HEAD_POLICY_INVALID;
    if (!policy || strcmp(policy, "fixed") == 0)
        resolved = CHARACTER_HEAD_POLICY_FIXED;
    else if (strcmp(policy, "random_gender") == 0)
        resolved = CHARACTER_HEAD_POLICY_RANDOM_GENDER;
    else if (strcmp(policy, "integrated") == 0)
        resolved = CHARACTER_HEAD_POLICY_INTEGRATED;
    else return CHARACTER_HEAD_POLICY_INVALID;
    if (resolved == CHARACTER_HEAD_POLICY_FIXED) {
        if (!catalogIdValid(head_id) || !headfile || !headfile[0] ||
                strlen(headfile) >= FS_MAXPATH || !utf8Valid((const unsigned char *)headfile))
            return CHARACTER_HEAD_POLICY_INVALID;
    } else if ((head_id && head_id[0]) || (headfile && headfile[0])) {
        return CHARACTER_HEAD_POLICY_INVALID;
    }
    return resolved;
}

character_head_policy_e characterSourcePolicy(const asset_entry_t *entry)
{
    const char *policy;
    if (!entry || entry->type != ASSET_CHARACTER ||
            !memchr(entry->id, 0, sizeof(entry->id)) || !catalogIdValid(entry->id) ||
            !memchr(entry->ext.character.body_id, 0, sizeof(entry->ext.character.body_id)) ||
            !memchr(entry->ext.character.head_id, 0, sizeof(entry->ext.character.head_id)) ||
            !memchr(entry->ext.character.bodyfile, 0, sizeof(entry->ext.character.bodyfile)) ||
            !memchr(entry->ext.character.headfile, 0, sizeof(entry->ext.character.headfile)) ||
            !memchr(entry->ext.character.display_name, 0, sizeof(entry->ext.character.display_name)) ||
            !memchr(entry->ext.character.portrait_file, 0, sizeof(entry->ext.character.portrait_file)) ||
            !utf8Valid((const unsigned char *)entry->ext.character.display_name) ||
            !utf8Valid((const unsigned char *)entry->ext.character.portrait_file))
        return CHARACTER_HEAD_POLICY_INVALID;
    policy = characterHeadPolicyName((character_head_policy_e)entry->ext.character.head_policy);
    if (!policy && entry->ext.character.head_policy != CHARACTER_HEAD_POLICY_UNSPECIFIED)
        return CHARACTER_HEAD_POLICY_INVALID;
    return characterHeadPolicyResolve(policy, entry->ext.character.body_id,
        entry->ext.character.head_id, entry->ext.character.bodyfile,
        entry->ext.character.headfile);
}

static int sourceValue(const ini_section_t *ini, const char *key,
    const char *alias, const char **out)
{
    const char *value = NULL;
    int primary_seen = 0, alias_seen = 0;
    for (int i = 0; i < ini->count; ++i) {
        const ini_pair_t *pair = &ini->pairs[i];
        int primary, secondary;
        if (!memchr(pair->key, 0, sizeof(pair->key)) ||
                !memchr(pair->value, 0, sizeof(pair->value))) return 0;
        primary = strcmp(pair->key, key) == 0;
        secondary = alias && strcmp(pair->key, alias) == 0;
        if (!primary && !secondary) continue;
        if ((primary && primary_seen++) || (secondary && alias_seen++)) return 0;
        if (value && strcmp(value, pair->value) != 0) return 0;
        value = pair->value;
    }
    *out = value;
    return 1;
}

static int copyValue(char *out, size_t capacity, const char *value)
{
    size_t length = value ? strlen(value) : 0;
    if (length >= capacity || !utf8Valid((const unsigned char *)value)) return 0;
    if (length) memcpy(out, value, length);
    out[length] = 0;
    return 1;
}

int characterDependencySourceMatches(const ini_section_t *ini,
    const char *expected_id, int is_head, int require_complete)
{
    const char *id, *complete;
    if (!ini || ini->count < 0 || ini->count > INI_MAX_PAIRS ||
            !memchr(ini->type, 0, sizeof(ini->type)) ||
            strcmp(ini->type, is_head ? "head" : "body") != 0 ||
            !catalogIdValid(expected_id) ||
            !sourceValue(ini, "catalog_id", "id", &id) ||
            !id || strcmp(id, expected_id) != 0) return 0;
    if (!is_head) {
        if (!sourceValue(ini, "unk00_01", NULL, &complete)) return 0;
        if (complete && strcmp(complete, "0") != 0 && strcmp(complete, "1") != 0) return 0;
        if (require_complete && (!complete || strcmp(complete, "1") != 0)) return 0;
    }
    return !is_head || !require_complete;
}

int characterSourceApplyIni(asset_entry_t *entry, const ini_section_t *ini)
{
    const char *id, *body, *head, *bodyfile, *headfile, *policy, *name, *portrait;
    character_head_policy_e resolved;
    /* The temporary is just the character payload, not a catalog row copy. */
    char body_id[CATALOG_ID_LEN], head_id[CATALOG_ID_LEN], display_name[64];
    char body_path[FS_MAXPATH], head_path[FS_MAXPATH], portrait_path[FS_MAXPATH];
    if (!entry || entry->type != ASSET_CHARACTER || !ini ||
            ini->count < 0 || ini->count > INI_MAX_PAIRS ||
            !memchr(ini->type, 0, sizeof(ini->type)) ||
            strcmp(ini->type, "character") != 0 ||
            !sourceValue(ini, "catalog_id", "id", &id) ||
            !sourceValue(ini, "body_asset", "body_id", &body) ||
            !sourceValue(ini, "head_asset", "head_id", &head) ||
            !sourceValue(ini, "body_archive", "bodyfile", &bodyfile) ||
            !sourceValue(ini, "head_archive", "headfile", &headfile) ||
            !sourceValue(ini, "head_policy", NULL, &policy) ||
            !sourceValue(ini, "display_name", NULL, &name) ||
            !sourceValue(ini, "portrait_file", NULL, &portrait)) return 0;
    if (!memchr(entry->id, 0, sizeof(entry->id)) || !catalogIdValid(entry->id) ||
            (id && strcmp(entry->id, id) != 0)) return 0;
    resolved = characterHeadPolicyResolve(policy, body, head, bodyfile, headfile);
    if (resolved == CHARACTER_HEAD_POLICY_INVALID ||
            !copyValue(body_id, sizeof(body_id), body) ||
            !copyValue(head_id, sizeof(head_id), head) ||
            !copyValue(display_name, sizeof(display_name), name) ||
            !copyValue(body_path, sizeof(body_path), bodyfile) ||
            !copyValue(head_path, sizeof(head_path), headfile) ||
            !copyValue(portrait_path, sizeof(portrait_path), portrait)) return 0;
    memset(&entry->ext.character, 0, sizeof(entry->ext.character));
    strcpy(entry->ext.character.body_id, body_id);
    strcpy(entry->ext.character.head_id, head_id);
    strcpy(entry->ext.character.display_name, display_name);
    strcpy(entry->ext.character.bodyfile, body_path);
    strcpy(entry->ext.character.headfile, head_path);
    strcpy(entry->ext.character.portrait_file, portrait_path);
    entry->ext.character.head_policy = resolved;
    return 1;
}
