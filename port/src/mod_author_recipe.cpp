/**
 * mod_author_recipe.cpp -- a mod described in a file.
 *
 * See mod_author_recipe.h for the format and the contract.
 */

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "../external/imgui-node-editor/crude_json.h"
#include "mod_author_recipe.h"
#include "modasset_gltf_document.h"

struct mod_author_recipe {
    std::string template_id;
    std::string template_file;
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    /* Parallel to `overrides`: the storage its pointers refer to. */
    std::vector<std::string> entries;
    std::vector<std::string> data;
    std::vector<asset_archive_clone_override_t> overrides;
    /* Parallel to `descriptor_values`. A removal has no value. */
    std::vector<std::string> descriptor_keys;
    std::vector<std::string> descriptor_texts;
    std::vector<bool> descriptor_removes;
    std::vector<mod_author_descriptor_value_t> descriptor_values;
};

namespace {

void setError(char *err, size_t cap, const char *fmt, ...)
{
    if (!err || cap == 0) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(err, cap, fmt, ap);
    va_end(ap);
    err[cap - 1] = '\0';
}

bool isKnownKey(const std::string &key, const char *const *known, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (key == known[i]) return true;
    }
    return false;
}

/* A string member. Absent is fine unless `required`; any other type is not. */
bool readString(const crude_json::object &object, const char *key, bool required,
    std::string &out, bool &present, const char *where, char *err, size_t cap)
{
    present = false;
    out.clear();
    const auto it = object.find(key);
    if (it == object.end()) {
        if (required) setError(err, cap, "%s needs \"%s\"", where, key);
        return !required;
    }
    const crude_json::string *text = it->second.get_ptr<crude_json::string>();
    if (!text) {
        setError(err, cap, "%s: \"%s\" must be a string", where, key);
        return false;
    }
    if (text->find('\0') != std::string::npos) {
        setError(err, cap, "%s: \"%s\" holds a NUL character", where, key);
        return false;
    }
    out = *text;
    present = true;
    return true;
}

/* A path that stays inside the recipe's folder. */
bool pathIsContained(const std::string &path)
{
    if (path.empty() || path.size() >= MOD_AUTHOR_PATH_MAX) return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    if (path.find(':') != std::string::npos) return false;
    size_t at = 0;
    while (at <= path.size()) {
        size_t end = path.find_first_of("/\\", at);
        if (end == std::string::npos) end = path.size();
        const std::string step = path.substr(at, end - at);
        if (step.empty() || step == "." || step == "..") return false;
        at = end + 1;
    }
    return true;
}

std::string joinPath(const std::string &dir, const std::string &relative)
{
    if (dir.empty()) return relative;
    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + relative;
    return dir + "/" + relative;
}

bool readFileBytes(const std::string &path, size_t limit, std::string &out)
{
    out.clear();
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = false;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        const long size = std::ftell(f);
        if (size >= 0 && static_cast<unsigned long>(size) <= limit &&
                std::fseek(f, 0, SEEK_SET) == 0) {
            out.resize(static_cast<size_t>(size));
            ok = size == 0 ||
                std::fread(&out[0], 1, static_cast<size_t>(size), f) == static_cast<size_t>(size);
        }
    }
    std::fclose(f);
    if (!ok) out.clear();
    return ok;
}

bool parseOverride(const crude_json::value &item, size_t index, const std::string &recipe_dir,
    mod_author_recipe &recipe, char *err, size_t cap)
{
    static const char *const kKnown[] = { "entry", "file", "text", "remove" };
    char where[48];
    std::snprintf(where, sizeof(where), "override %u", static_cast<unsigned>(index + 1));

    const crude_json::object *object = item.get_ptr<crude_json::object>();
    if (!object) {
        setError(err, cap, "%s must be an object", where);
        return false;
    }
    for (const auto &member : *object) {
        if (!isKnownKey(member.first, kKnown, sizeof(kKnown) / sizeof(kKnown[0]))) {
            setError(err, cap, "%s has an unknown key \"%s\"", where, member.first.c_str());
            return false;
        }
    }

    std::string entry;
    std::string file;
    std::string text;
    bool has_entry = false;
    bool has_file = false;
    bool has_text = false;
    bool remove = false;
    bool has_remove = false;

    if (!readString(*object, "entry", true, entry, has_entry, where, err, cap) ||
            !readString(*object, "file", false, file, has_file, where, err, cap) ||
            !readString(*object, "text", false, text, has_text, where, err, cap)) {
        return false;
    }
    if (entry.empty()) {
        setError(err, cap, "%s: \"entry\" is empty", where);
        return false;
    }
    const auto remove_it = object->find("remove");
    if (remove_it != object->end()) {
        const crude_json::boolean *flag = remove_it->second.get_ptr<crude_json::boolean>();
        if (!flag) {
            setError(err, cap, "%s: \"remove\" must be true or false", where);
            return false;
        }
        has_remove = true;
        remove = *flag;
    }
    const int sources = (has_file ? 1 : 0) + (has_text ? 1 : 0) + (remove ? 1 : 0);
    if (sources != 1) {
        setError(err, cap, "%s needs exactly one of \"file\", \"text\" or \"remove\": true", where);
        return false;
    }
    (void)has_remove;

    std::string bytes;
    if (has_file) {
        if (!pathIsContained(file)) {
            setError(err, cap, "%s: \"file\" must be a path inside the recipe's folder", where);
            return false;
        }
        if (!readFileBytes(joinPath(recipe_dir, file), MOD_AUTHOR_RECIPE_MAX_OVERRIDE_BYTES, bytes)) {
            setError(err, cap, "%s: could not read %s", where, file.c_str());
            return false;
        }
    } else if (has_text) {
        bytes = text;
    }

    for (const std::string &seen : recipe.entries) {
        if (seen == entry) {
            setError(err, cap, "%s names %s, which an earlier override already does",
                where, entry.c_str());
            return false;
        }
    }
    recipe.entries.push_back(entry);
    recipe.data.push_back(bytes);
    asset_archive_clone_override_t ov;
    std::memset(&ov, 0, sizeof(ov));
    ov.remove = remove ? 1 : 0;
    recipe.overrides.push_back(ov);
    return true;
}

mod_author_recipe *parse(const void *json, size_t size, const std::string &recipe_dir,
    char *err, size_t cap)
{
    static const char *const kKnown[] = { "schema", "template", "template_file", "name",
        "author", "version", "description", "descriptor", "overrides" };

    if (!json || size == 0) {
        setError(err, cap, "the recipe is empty");
        return nullptr;
    }
    if (size > MOD_AUTHOR_RECIPE_MAX_BYTES) {
        setError(err, cap, "the recipe is larger than 1 MiB");
        return nullptr;
    }

    crude_json::value root;
    if (!modAssetJsonReadValue(static_cast<const char *>(json), size, root)) {
        setError(err, cap, "the recipe is not one complete UTF-8 JSON value "
            "(no byte order mark, no duplicate keys, no trailing text)");
        return nullptr;
    }
    const crude_json::object *object = root.get_ptr<crude_json::object>();
    if (!object) {
        setError(err, cap, "the recipe must be a JSON object");
        return nullptr;
    }
    for (const auto &member : *object) {
        if (!isKnownKey(member.first, kKnown, sizeof(kKnown) / sizeof(kKnown[0]))) {
            setError(err, cap, "the recipe has an unknown key \"%s\"", member.first.c_str());
            return nullptr;
        }
    }

    std::unique_ptr<mod_author_recipe> recipe(new mod_author_recipe());
    std::string schema;
    bool present = false;
    bool has_id = false;
    bool has_file = false;
    bool has_name = false;

    if (!readString(*object, "schema", true, schema, present, "the recipe", err, cap)) return nullptr;
    if (schema != MOD_AUTHOR_RECIPE_SCHEMA) {
        setError(err, cap, "the recipe's schema must be \"%s\"", MOD_AUTHOR_RECIPE_SCHEMA);
        return nullptr;
    }
    if (!readString(*object, "template", false, recipe->template_id, has_id, "the recipe", err, cap) ||
            !readString(*object, "template_file", false, recipe->template_file, has_file,
                "the recipe", err, cap) ||
            !readString(*object, "name", true, recipe->name, has_name, "the recipe", err, cap) ||
            !readString(*object, "author", false, recipe->author, present, "the recipe", err, cap) ||
            !readString(*object, "version", false, recipe->version, present, "the recipe", err, cap) ||
            !readString(*object, "description", false, recipe->description, present,
                "the recipe", err, cap)) {
        return nullptr;
    }
    if (has_id == has_file) {
        setError(err, cap, "the recipe needs exactly one of \"template\" and \"template_file\"");
        return nullptr;
    }
    if (has_id && recipe->template_id.empty()) {
        setError(err, cap, "the recipe's \"template\" is empty");
        return nullptr;
    }
    if (has_file) {
        if (!pathIsContained(recipe->template_file)) {
            setError(err, cap, "\"template_file\" must be a path inside the recipe's folder");
            return nullptr;
        }
        recipe->template_file = joinPath(recipe_dir, recipe->template_file);
    }

    const auto descriptor_it = object->find("descriptor");
    if (descriptor_it != object->end()) {
        const crude_json::object *values = descriptor_it->second.get_ptr<crude_json::object>();
        if (!values) {
            setError(err, cap, "the recipe's \"descriptor\" must be an object of key: value");
            return nullptr;
        }
        if (values->size() > MOD_AUTHOR_MAX_DESCRIPTOR_VALUES) {
            setError(err, cap, "the recipe's \"descriptor\" has more than %d keys",
                static_cast<int>(MOD_AUTHOR_MAX_DESCRIPTOR_VALUES));
            return nullptr;
        }
        for (const auto &member : *values) {
            const crude_json::string *text = member.second.get_ptr<crude_json::string>();
            if (!text && !member.second.is_null()) {
                setError(err, cap, "descriptor key \"%s\" must be a string, or null to remove it",
                    member.first.c_str());
                return nullptr;
            }
            if (member.first.find('\0') != std::string::npos ||
                    (text && text->find('\0') != std::string::npos)) {
                setError(err, cap, "descriptor key \"%s\" holds a NUL character",
                    member.first.c_str());
                return nullptr;
            }
            recipe->descriptor_keys.push_back(member.first);
            recipe->descriptor_texts.push_back(text ? *text : std::string());
            recipe->descriptor_removes.push_back(text == nullptr);
        }
    }

    const auto overrides_it = object->find("overrides");
    if (overrides_it != object->end()) {
        const crude_json::array *items = overrides_it->second.get_ptr<crude_json::array>();
        if (!items) {
            setError(err, cap, "the recipe's \"overrides\" must be an array");
            return nullptr;
        }
        if (items->size() > MOD_AUTHOR_RECIPE_MAX_OVERRIDES) {
            setError(err, cap, "the recipe has more than %d overrides",
                static_cast<int>(MOD_AUTHOR_RECIPE_MAX_OVERRIDES));
            return nullptr;
        }
        recipe->entries.reserve(items->size());
        recipe->data.reserve(items->size());
        recipe->overrides.reserve(items->size());
        for (size_t i = 0; i < items->size(); i++) {
            if (!parseOverride((*items)[i], i, recipe_dir, *recipe, err, cap)) return nullptr;
        }
    }

    /* Storage no longer moves: bind the pointers. */
    recipe->descriptor_values.resize(recipe->descriptor_keys.size());
    for (size_t i = 0; i < recipe->descriptor_keys.size(); i++) {
        recipe->descriptor_values[i].key = recipe->descriptor_keys[i].c_str();
        recipe->descriptor_values[i].value =
            recipe->descriptor_removes[i] ? nullptr : recipe->descriptor_texts[i].c_str();
    }
    for (size_t i = 0; i < recipe->overrides.size(); i++) {
        asset_archive_clone_override_t &ov = recipe->overrides[i];
        ov.entry = recipe->entries[i].c_str();
        if (!ov.remove) {
            ov.data = recipe->data[i].data();
            ov.len = static_cast<u32>(recipe->data[i].size());
        }
    }
    return recipe.release();
}

} // namespace

extern "C" mod_author_recipe_t *modAuthorRecipeParse(const void *json, size_t size,
    const char *recipe_dir, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    try {
        return parse(json, size, recipe_dir ? recipe_dir : "", err, err_cap);
    } catch (...) {
        setError(err, err_cap, "out of memory while reading the recipe");
        return nullptr;
    }
}

extern "C" mod_author_recipe_t *modAuthorRecipeLoadFile(const char *path, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    if (!path || !path[0]) {
        setError(err, err_cap, "no recipe file was named");
        return nullptr;
    }
    try {
        const std::string file(path);
        std::string bytes;
        if (!readFileBytes(file, MOD_AUTHOR_RECIPE_MAX_BYTES, bytes)) {
            setError(err, err_cap, "the recipe could not be read (missing, or larger than 1 MiB)");
            return nullptr;
        }
        const size_t slash = file.find_last_of("/\\");
        const std::string dir = slash == std::string::npos ? std::string(".") : file.substr(0, slash);
        return parse(bytes.data(), bytes.size(), dir, err, err_cap);
    } catch (...) {
        setError(err, err_cap, "out of memory while reading the recipe");
        return nullptr;
    }
}

extern "C" void modAuthorRecipeFree(mod_author_recipe_t *recipe)
{
    delete recipe;
}

extern "C" const char *modAuthorRecipeTemplateId(const mod_author_recipe_t *recipe)
{
    return recipe ? recipe->template_id.c_str() : "";
}

extern "C" const char *modAuthorRecipeTemplateFile(const mod_author_recipe_t *recipe)
{
    return (recipe && recipe->template_id.empty()) ? recipe->template_file.c_str() : "";
}

extern "C" void modAuthorRecipeToRequest(const mod_author_recipe_t *recipe,
    const char *template_path, const char *mods_root, mod_author_request_t *out)
{
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    if (!recipe) return;
    out->template_path = (template_path && template_path[0])
        ? template_path
        : (recipe->template_file.empty() ? nullptr : recipe->template_file.c_str());
    out->mods_root = mods_root;
    out->display_name = recipe->name.c_str();
    out->author = recipe->author.empty() ? nullptr : recipe->author.c_str();
    out->version = recipe->version.empty() ? nullptr : recipe->version.c_str();
    out->description = recipe->description.empty() ? nullptr : recipe->description.c_str();
    out->tool = "mod-author-recipe";
    out->overrides = recipe->overrides.empty() ? nullptr : recipe->overrides.data();
    out->override_count = static_cast<u32>(recipe->overrides.size());
    out->descriptor_values =
        recipe->descriptor_values.empty() ? nullptr : recipe->descriptor_values.data();
    out->descriptor_value_count = static_cast<u32>(recipe->descriptor_values.size());
}
