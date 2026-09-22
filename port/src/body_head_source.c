#include "body_head_source.h"
#include "head_body_rig.h"
#include "modarchive.h"
#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *error, size_t capacity, const char *field)
{
    if (error && capacity) snprintf(error, capacity, "invalid public body/head field: %s", field);
    return 0;
}

static int utf8Valid(const unsigned char *p, size_t size)
{
    size_t i = 0;
    while (i < size) {
        unsigned code = p[i++], minimum;
        unsigned remaining;
        if (code == 0) return 0;
        if (code < 0x80) continue;
        if (code >= 0xc2 && code <= 0xdf) { code &= 0x1f; remaining = 1; minimum = 0x80; }
        else if (code >= 0xe0 && code <= 0xef) { code &= 0x0f; remaining = 2; minimum = 0x800; }
        else if (code >= 0xf0 && code <= 0xf4) { code &= 7; remaining = 3; minimum = 0x10000; }
        else return 0;
        if (remaining > size - i) return 0;
        while (remaining--) {
            if ((p[i] & 0xc0) != 0x80) return 0;
            code = (code << 6) | (p[i++] & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}

static int value(const ini_section_t *ini, const char *key, const char **out)
{
    *out = NULL;
    for (s32 i = 0; i < ini->count; ++i) {
        const ini_pair_t *p = &ini->pairs[i];
        if (!memchr(p->key, 0, sizeof(p->key)) || !memchr(p->value, 0, sizeof(p->value))) return 0;
        if (strcmp(p->key, key)) continue;
        if (*out) return 0;
        *out = p->value;
    }
    return 1;
}

static int alpha(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int alnum(unsigned char c) { return alpha(c) || (c >= '0' && c <= '9'); }
s32 bodyHeadSourceCatalogIdValid(const char *id)
{
    const unsigned char *p = (const unsigned char *)id;
    if (!p || !alpha(*p) || strlen(id) >= CATALOG_ID_LEN) return 0;
    for (++p; *p && *p != ':'; ++p) if (!alnum(*p) && *p != '_' && *p != '.' && *p != '-') return 0;
    if (*p++ != ':' || !alnum(*p)) return 0;
    for (++p; *p; ++p) if (!alnum(*p) && *p != '_' && *p != '.' && *p != '-' && *p != ':') return 0;
    return 1;
}
#define validId bodyHeadSourceCatalogIdValid

static int number(const ini_section_t *ini, const char *key, unsigned limit, unsigned *out)
{
    const char *text;
    unsigned n = 0;
    if (!value(ini, key, &text)) return 0;
    if (!text) { *out = 0; return 1; }
    if (!text[0]) return 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9' || n > limit / 10 ||
                (n == limit / 10 && (unsigned)(*p - '0') > limit % 10)) return 0;
        n = n * 10 + (unsigned)(*p - '0');
        if (n > limit) return 0;
    }
    *out = n;
    return 1;
}

static int positiveFloat(const ini_section_t *ini, const char *key, f32 *out)
{
    const char *text;
    char *end;
    double n;
    if (!value(ini, key, &text)) return 0;
    if (!text) { *out = 1; return 1; }
    if (!text[0]) return 0;
    for (const char *p = text; *p; ++p)
        if ((*p < '0' || *p > '9') && *p != '.' && *p != '+' && *p != '-' && *p != 'e' && *p != 'E') return 0;
    errno = 0;
    n = strtod(text, &end);
    if (end == text || *end || errno == ERANGE || !(n > 0 && n <= FLT_MAX)) return 0;
    *out = (f32)n;
    return *out > 0 && *out <= FLT_MAX;
}

static int ref(const ini_section_t *ini, const char *key, char *out, size_t capacity, int is_id)
{
    const char *text;
    if (!value(ini, key, &text)) return 0;
    if (!text) text = "";
    if (strlen(text) >= capacity || !utf8Valid((const unsigned char *)text, strlen(text)) ||
            (is_id && text[0] && !validId(text))) return 0;
    strcpy(out, text);
    return 1;
}

s32 bodyHeadSourceParseIni(const ini_section_t *ini, const char *expected_id,
    s32 is_head, body_head_source_t *out, char *error, size_t error_capacity)
{
    static const struct { const char *name; unsigned native_type; } types[] = {
        {"HEADBODYTYPE_DEFAULT", HEADBODYTYPE_DEFAULT},
        {"HEADBODYTYPE_FEMALE", HEADBODYTYPE_FEMALE},
        {"HEADBODYTYPE_FEMALEGUARD", HEADBODYTYPE_FEMALEGUARD},
        {"HEADBODYTYPE_CASS", HEADBODYTYPE_CASS},
        {"HEADBODYTYPE_MAIAN", HEADBODYTYPE_MAIAN},
        {"HEADBODYTYPE_MRBLONDE", HEADBODYTYPE_MRBLONDE}
    };
    body_head_source_t candidate = {0};
    const char *id, *alias, *type, *present;
    unsigned n;
    if (error && error_capacity) error[0] = 0;
    if (!out || !ini || ini->count < 0 || ini->count > INI_MAX_PAIRS ||
            !memchr(ini->type, 0, sizeof(ini->type)) ||
            strcmp(ini->type, is_head ? "head" : "body") || !validId(expected_id))
        return fail(error, error_capacity, "identity/type");
    if (!value(ini, "catalog_id", &id) || !value(ini, "id", &alias) ||
            (id && alias && strcmp(id, alias)) || (!id && !(id = alias)) || strcmp(id, expected_id))
        return fail(error, error_capacity, "catalog_id");
    candidate.is_head = !!is_head;
    candidate.enabled = 1;
    strcpy(candidate.id, expected_id);
#define READ_UINT(key, max, field) do { if (!number(ini, key, max, &n)) return fail(error, error_capacity, key); candidate.field = n; } while (0)
    READ_UINT("ismale", 1, ismale);
    READ_UINT("unk00_01", 1, complete);
    READ_UINT("height", 65535, height);
    READ_UINT("bundled", 1, bundled);
    READ_UINT("requirefeature", 255, requirefeature);
    if (!value(ini, "enabled", &present)) return fail(error, error_capacity, "enabled");
    if (present) { READ_UINT("enabled", 1, enabled); }
    if (!value(ini, "name_langid", &present)) return fail(error, error_capacity, "name_langid");
    if (present) { READ_UINT("name_langid", 32767, name_langid); candidate.has_name_langid = 1; }
    if (!is_head) { READ_UINT("canvaryheight", 1, canvaryheight); }
#undef READ_UINT
    if (!value(ini, "type", &type)) return fail(error, error_capacity, "type");
    if (type) {
        int found = 0;
        for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
            if (!strcmp(type, types[i].name)) { candidate.type = types[i].native_type; found = 1; break; }
        if (!found) {
            if (!number(ini, "type", 5, &n)) return fail(error, error_capacity, "type");
            candidate.type = n;
        }
    }
    if (!positiveFloat(ini, "scale", &candidate.scale) ||
            !positiveFloat(ini, "animscale", &candidate.animscale) ||
            !positiveFloat(ini, "model_scale", &candidate.model_scale)) return fail(error, error_capacity, "scale/animscale/model_scale");
    if (!value(ini, "rig_class", &present) ||
            !ref(ini, "rig_class", candidate.rig_class, sizeof(candidate.rig_class), 0))
        return fail(error, error_capacity, "rig class");
    if (!present) strcpy(candidate.rig_class, catalogRigClassForHeadBodyType(candidate.type));
    if (!value(ini, "display_name", &present)) return fail(error, error_capacity, "display name");
    const char *display_key = present ? "display_name" : "name";
    if (!present && !value(ini, "name", &present)) return fail(error, error_capacity, "name");
    candidate.has_display_name = present != NULL;
    if (!ref(ini, display_key, candidate.display_name, sizeof(candidate.display_name), 0))
        return fail(error, error_capacity, "rig/display name");
    if (!ref(ini, "mesh_catalog_id", candidate.mesh_id, sizeof(candidate.mesh_id), 1) ||
            !ref(ini, "mesh_archive", candidate.mesh_archive, sizeof(candidate.mesh_archive), 0) ||
            (!is_head && (!ref(ini, "hand_catalog_id", candidate.hand_id, sizeof(candidate.hand_id), 1) ||
            !ref(ini, "hand_archive", candidate.hand_archive, sizeof(candidate.hand_archive), 0))))
        return fail(error, error_capacity, "typed mesh reference");
    if ((candidate.mesh_id[0] && !candidate.mesh_archive[0]) ||
            (candidate.hand_id[0] && !candidate.hand_archive[0])) return fail(error, error_capacity, "missing mesh archive");
    *out = candidate;
    return 1;
}

static int space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

s32 bodyHeadSourceReadIniBytes(const char *text, u32 size, const char *kind,
    ini_section_t *out, char *error, size_t error_capacity)
{
    ini_section_t *ini = calloc(1, sizeof(*ini));
    int found = 0, selected = 0, ok = 0;
    if (error && error_capacity) error[0] = 0;
    if (!text || !ini || !kind || !kind[0] || !out ||
            strlen(kind) >= sizeof(ini->type) || !utf8Valid((const unsigned char *)text, size)) goto done;
    strcpy(ini->type, kind);
    for (size_t start = 0; start < size;) {
        size_t end = start;
        while (end < size && text[end] != '\n') ++end;
        const char *a = text + start, *b = text + end;
        start = end + 1;
        while (a < b && space(*a)) ++a;
        while (b > a && space(b[-1])) --b;
        if (a == b || *a == ';' || *a == '#') continue;
        if (*a == '[') {
            if (b[-1] != ']') goto done;
            selected = (size_t)(b - a) == strlen(kind) + 2 && !memcmp(a + 1, kind, strlen(kind));
            if (selected && found++) goto done;
            continue;
        }
        if (!selected) continue;
        const char *eq = memchr(a, '=', (size_t)(b - a));
        if (!eq || ini->count >= INI_MAX_PAIRS) goto done;
        const char *key_end = eq, *val = eq + 1;
        while (key_end > a && space(key_end[-1])) --key_end;
        while (val < b && space(*val)) ++val;
        if (key_end == a || (size_t)(key_end - a) >= sizeof(ini->pairs[0].key) ||
                (size_t)(b - val) >= sizeof(ini->pairs[0].value)) goto done;
        ini_pair_t *p = &ini->pairs[ini->count++];
        memcpy(p->key, a, (size_t)(key_end - a));
        memcpy(p->value, val, (size_t)(b - val));
        for (s32 i = 0; i < ini->count - 1; ++i)
            if (!strcmp(ini->pairs[i].key, p->key)) goto done;
    }
    if (found) { *out = *ini; ok = 1; }
done:
    if (!ok && error && error_capacity && !error[0]) fail(error, error_capacity, "public descriptor");
    free(ini);
    return ok;
}

s32 bodyHeadSourceParseArchiveBytes(const void *archive, u32 archive_size,
    const char *expected_id, s32 is_head, body_head_source_t *out,
    char *error, size_t error_capacity)
{
    u32 size = 0;
    char *text = modArchiveExtractMemAlloc(archive, archive_size,
        is_head ? "head.ini" : "body.ini", &size);
    ini_section_t *ini = calloc(1, sizeof(*ini));
    int ok = bodyHeadSourceReadIniBytes(text, size, is_head ? "head" : "body",
        ini, error, error_capacity) &&
        bodyHeadSourceParseIni(ini, expected_id, is_head, out, error, error_capacity);
    free(ini);
    free(text);
    return ok;
}

static int validSource(const body_head_source_t *source)
{
    return source && (source->is_head == 0 || source->is_head == 1) &&
        memchr(source->id, 0, sizeof(source->id)) && validId(source->id) &&
        memchr(source->mesh_id, 0, sizeof(source->mesh_id)) &&
        memchr(source->hand_id, 0, sizeof(source->hand_id)) &&
        memchr(source->mesh_archive, 0, sizeof(source->mesh_archive)) &&
        memchr(source->hand_archive, 0, sizeof(source->hand_archive)) &&
        (!source->mesh_id[0] || validId(source->mesh_id)) &&
        (!source->hand_id[0] || validId(source->hand_id)) &&
        source->ismale <= 1 && source->complete <= 1 && source->canvaryheight <= 1 &&
        source->type <= 5 && source->scale > 0 && source->scale <= FLT_MAX &&
        source->animscale > 0 && source->animscale <= FLT_MAX;
}

s32 bodyHeadSourceBuildBody(const body_head_source_t *source, s32 slot,
    s32 mesh_filenum, s32 hand_filenum, body_data_t *out)
{
    body_data_t candidate = {0};
    if (!validSource(source) || source->is_head || !out || slot < 0 || slot >= CATALOG_MGR_BODY_TOTAL ||
            mesh_filenum < 0 || mesh_filenum > 65535 || hand_filenum < 0 || hand_filenum > 65535 ||
            (!!source->hand_archive[0] != (hand_filenum > 0)) ||
            (!!source->hand_id[0] != (hand_filenum > 0))) return 0;
    candidate.bodynum = slot;
    strcpy(candidate.catalog_id, source->id);
    candidate.ismale = source->ismale; candidate.unk00_01 = source->complete;
    candidate.canvaryheight = source->canvaryheight; candidate.type = source->type;
    candidate.height = source->height; candidate.scale = source->scale;
    candidate.animscale = source->animscale; candidate.filenum = mesh_filenum;
    candidate.handfilenum = hand_filenum;
    strcpy(candidate.hand_catalog_id, source->hand_id);
    *out = candidate;
    return 1;
}

s32 bodyHeadSourceBuildHead(const body_head_source_t *source, s32 slot,
    s32 mesh_filenum, head_data_t *out)
{
    head_data_t candidate = {0};
    if (!validSource(source) || !source->is_head || !out || slot < 0 || slot >= CATALOG_MGR_HEAD_TOTAL ||
            mesh_filenum < 0 || mesh_filenum > 65535) return 0;
    candidate.headnum = slot;
    strcpy(candidate.catalog_id, source->id);
    candidate.ismale = source->ismale; candidate.unk00_01 = source->complete;
    candidate.type = source->type; candidate.height = source->height;
    candidate.scale = source->scale; candidate.animscale = source->animscale;
    candidate.filenum = mesh_filenum;
    *out = candidate;
    return 1;
}
