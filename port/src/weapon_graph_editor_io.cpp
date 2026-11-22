/**
 * weapon_graph_editor_io.cpp -- between the Weapons creator's graph and the
 * clean `.pdweapon` behavior files.
 *
 * See weapon_graph_editor_io.h for the contract.
 */

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../external/imgui-node-editor/crude_json.h"
#include "modasset_gltf_document.h"
#include "weapon_graph_editor_io.h"

extern "C" {
#include "weapon_rules.h"
}

namespace {

using Json = crude_json::value;
using JsonObject = crude_json::object;
using JsonArray = crude_json::array;

const char *const kModes[2] = { "primary", "secondary" };

void setError(char *err, size_t cap, const char *fmt, ...)
{
    if (!err || cap == 0) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(err, cap, fmt, ap);
    va_end(ap);
    err[cap - 1] = '\0';
}

char *copyText(const std::string &text)
{
    char *out = static_cast<char *>(std::malloc(text.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

/* ---- JSON text out ------------------------------------------------------
 * crude_json prints every number with 18 significant digits (0.1 becomes
 * 0.100000000000000006). These files are read and edited by people, so
 * numbers are written in the shortest form that reads back to the same
 * value. */

void writeString(std::string &out, const std::string &text)
{
    out += '"';
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(c));
                out += escape;
            } else {
                out += raw;
            }
            break;
        }
    }
    out += '"';
}

void writeNumber(std::string &out, double value)
{
    char text[40];
    if (!(value >= -DBL_MAX && value <= DBL_MAX) || value == 0.0) {
        out += '0';
        return;
    }
    if (value == std::floor(value) && std::fabs(value) < 9007199254740992.0) {
        std::snprintf(text, sizeof(text), "%.0f", value);
    } else {
        for (int digits = 15; digits <= 17; digits++) {
            std::snprintf(text, sizeof(text), "%.*g", digits, value);
            if (std::strtod(text, nullptr) == value) break;
        }
    }
    out += text;
}

void writeValue(std::string &out, const Json &value)
{
    if (const JsonObject *object = value.get_ptr<JsonObject>()) {
        if (object->empty()) {
            out += "{}";
            return;
        }
        bool first = true;
        out += "{ ";
        for (const auto &member : *object) {
            if (!first) out += ", ";
            first = false;
            writeString(out, member.first);
            out += ": ";
            writeValue(out, member.second);
        }
        out += " }";
    } else if (const JsonArray *array = value.get_ptr<JsonArray>()) {
        if (array->empty()) {
            out += "[]";
            return;
        }
        bool first = true;
        out += "[ ";
        for (const Json &item : *array) {
            if (!first) out += ", ";
            first = false;
            writeValue(out, item);
        }
        out += " ]";
    } else if (const crude_json::string *text = value.get_ptr<crude_json::string>()) {
        writeString(out, *text);
    } else if (const crude_json::number *number = value.get_ptr<crude_json::number>()) {
        writeNumber(out, *number);
    } else if (const crude_json::boolean *flag = value.get_ptr<crude_json::boolean>()) {
        out += *flag ? "true" : "false";
    } else {
        out += "null";
    }
}

std::string quoted(const std::string &text)
{
    std::string out;
    writeString(out, text);
    return out;
}

/* ---- JSON text in ------------------------------------------------------- */

bool readObject(const char *json, Json &root, const char *what, char *err, size_t cap)
{
    if (!json || !json[0]) {
        setError(err, cap, "%s is empty", what);
        return false;
    }
    if (!modAssetJsonReadValue(json, std::strlen(json), root) || !root.is_object()) {
        setError(err, cap, "%s is not one strict JSON object", what);
        return false;
    }
    return true;
}

const Json *member(const JsonObject &object, const char *key)
{
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

const std::string *memberString(const JsonObject &object, const char *key)
{
    const Json *value = member(object, key);
    return value ? value->get_ptr<crude_json::string>() : nullptr;
}

/* True with `out` set (NULL when the key is absent); false when the key
 * holds something other than a list. */
bool memberList(const JsonObject &object, const char *key, const char *what,
    const JsonArray *&out, char *err, size_t cap)
{
    out = nullptr;
    const Json *value = member(object, key);
    if (!value) return true;
    out = value->get_ptr<JsonArray>();
    if (!out) {
        setError(err, cap, "%s has a %s entry that is not a list", what, key);
        return false;
    }
    return true;
}

/* ---- the graph ---------------------------------------------------------- */

struct Node {
    std::string id;
    std::string kind;
    std::string scope;    /* "primary", "secondary" or "shared" */
    std::string params;   /* JSON object text */
    std::string extra;    /* ", \"key\": value" for every other member (a program node's settings) */
    int file;             /* 0 = primary file, 1 = secondary file */
};

struct Graph {
    std::vector<Node> nodes;
    std::vector<std::string> edges[2];
    std::vector<std::string> exports[2];
    std::vector<std::string> export_names;
};

bool isScope(const std::string &text)
{
    return text == "primary" || text == "secondary" || text == "shared";
}

/* held-weapon-events.md slice 2: a node of a function's program (a weapon
 * event, a gate or a native module), not of its record. A mode file that
 * holds one is pd.weapon_graph.v2. */
bool isProgramKind(const std::string &kind)
{
    return kind.compare(0, 13, "event.weapon.") == 0 || kind.compare(0, 12, "gate.weapon.") == 0 ||
        kind.compare(0, 3, "og.") == 0;
}

/* Kinds the runtime can make a mode's function from (the opcodes
 * heldOpcodeIsSupported accepts in weapon_graph_runtime.c). */
bool isFunctionKind(const std::string &kind)
{
    static const char *const prefixes[] = {
        "function.", "fire.", "spawn.", "melee.", "special.", "device.", "presentation.",
    };
    for (const char *prefix : prefixes) {
        if (kind.compare(0, std::strlen(prefix), prefix) == 0) return true;
    }
    return false;
}

int findNode(const Graph &graph, const std::string &id)
{
    for (size_t i = 0; i < graph.nodes.size(); i++) {
        if (graph.nodes[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

bool hasExport(const Graph &graph, const char *name)
{
    for (const std::string &seen : graph.export_names) {
        if (seen == name) return true;
    }
    return false;
}

void addExport(Graph &graph, int file, const std::string &name, const std::string &node)
{
    graph.exports[file].push_back("{ \"name\": " + quoted(name) + ", \"node\": " +
        quoted(node) + " }");
    graph.export_names.push_back(name);
}

/* The base game's own form of a mode with no function. Returns the id. */
std::string addEmptyFunction(Graph &graph, int mode)
{
    Node node;
    int suffix = 1;
    node.id = std::string(kModes[mode]) + "_empty";
    while (findNode(graph, node.id) >= 0) {
        node.id = std::string(kModes[mode]) + "_empty_" + std::to_string(++suffix);
    }
    node.kind = "function.empty";
    node.scope = kModes[mode];
    node.params = "{ \"function_type\": \"none\", \"mode\": " + quoted(kModes[mode]) +
        ", \"source\": \"empty_slot\" }";
    node.file = mode;
    const std::string id = node.id;
    graph.nodes.push_back(std::move(node));
    return id;
}

bool readNodes(const JsonObject &root, const char *what, int default_file, Graph &graph,
    char *err, size_t cap)
{
    const JsonArray *list = nullptr;
    if (!memberList(root, "nodes", what, list, err, cap)) return false;
    if (!list) return true;
    for (const Json &item : *list) {
        const JsonObject *object = item.get_ptr<JsonObject>();
        const std::string *id = object ? memberString(*object, "id") : nullptr;
        const std::string *kind = object ? memberString(*object, "kind") : nullptr;
        if (!id || id->empty() || !kind || kind->empty()) {
            setError(err, cap, "%s has a node with no id or no kind", what);
            return false;
        }
        if (findNode(graph, *id) >= 0) {
            setError(err, cap, "node id %s is used twice", id->c_str());
            return false;
        }
        Node node;
        node.id = *id;
        node.kind = *kind;
        node.scope = kModes[default_file];
        if (const Json *scope = member(*object, "subgraph")) {
            const std::string *text = scope->get_ptr<crude_json::string>();
            if (!text || (!text->empty() && !isScope(*text))) {
                setError(err, cap, "node %s is in a mode that is not primary, secondary or shared",
                    id->c_str());
                return false;
            }
            if (!text->empty()) node.scope = *text;
        }
        if (const Json *params = member(*object, "params")) {
            if (!params->is_object()) {
                setError(err, cap, "node %s has params that are not an object", id->c_str());
                return false;
            }
            writeValue(node.params, *params);
        } else {
            node.params = "{}";
        }
        /* A program node keeps its settings beside id and kind ("rounds",
         * "key", ...); they go back out as they came in. */
        for (const auto &entry : *object) {
            if (entry.first == "id" || entry.first == "kind" || entry.first == "subgraph" ||
                    entry.first == "params") {
                continue;
            }
            node.extra += ", " + quoted(entry.first) + ": ";
            writeValue(node.extra, entry.second);
        }
        node.file = node.scope == "secondary" ? 1 : 0;
        graph.nodes.push_back(std::move(node));
    }
    return true;
}

bool readEdges(const JsonObject &root, const char *what, Graph &graph, char *err, size_t cap)
{
    const JsonArray *list = nullptr;
    if (!memberList(root, "edges", what, list, err, cap)) return false;
    if (!list) return true;
    for (const Json &item : *list) {
        const JsonObject *object = item.get_ptr<JsonObject>();
        const std::string *from = object ? memberString(*object, "from") : nullptr;
        const std::string *to = object ? memberString(*object, "to") : nullptr;
        const int a = from ? findNode(graph, *from) : -1;
        const int b = to ? findNode(graph, *to) : -1;
        if (a < 0 || b < 0) {
            setError(err, cap, "%s has a link to a node that does not exist (%s to %s)", what,
                from ? from->c_str() : "?", to ? to->c_str() : "?");
            return false;
        }
        /* The loader joins both files before it resolves links, so a link
         * may name a node from the other file. It is filed with the
         * secondary mode as soon as either end belongs there. */
        const int file = (graph.nodes[a].file == 1 || graph.nodes[b].file == 1) ? 1 : 0;
        /* An execution edge of a function's program keeps its ports. */
        std::string row = "{ \"from\": " + quoted(*from);
        if (const std::string *output = memberString(*object, "output")) row += ", \"output\": " + quoted(*output);
        row += ", \"to\": " + quoted(*to);
        if (const std::string *input = memberString(*object, "input")) row += ", \"input\": " + quoted(*input);
        graph.edges[file].push_back(row + " }");
    }
    return true;
}

bool readExports(const JsonObject &root, const char *what, Graph &graph, char *err, size_t cap)
{
    const JsonArray *list = nullptr;
    if (!memberList(root, "exports", what, list, err, cap)) return false;
    if (!list) return true;
    for (const Json &item : *list) {
        const JsonObject *object = item.get_ptr<JsonObject>();
        const std::string *name = object ? memberString(*object, "name") : nullptr;
        const std::string *node = object ? memberString(*object, "node") : nullptr;
        if (!name || name->empty() || !node) {
            setError(err, cap, "%s has an export with no name or no node", what);
            return false;
        }
        const int index = findNode(graph, *node);
        if (index < 0) {
            setError(err, cap, "the %s function is set to node %s, which does not exist",
                name->c_str(), node->c_str());
            return false;
        }
        if (hasExport(graph, name->c_str())) {
            setError(err, cap, "the %s function is set twice", name->c_str());
            return false;
        }
        const int file = *name == "primary" ? 0 :
            (*name == "secondary" ? 1 : graph.nodes[index].file);
        addExport(graph, file, *name, *node);
    }
    return true;
}

/* Every mode file must hold a node and an export, and every mode should
 * end up with a function the game can use. */
void completeModes(Graph &graph)
{
    for (int mode = 0; mode < 2; mode++) {
        bool has_nodes = false;
        for (const Node &node : graph.nodes) {
            if (node.file == mode) has_nodes = true;
        }
        if (hasExport(graph, kModes[mode])) {
            if (!has_nodes) addEmptyFunction(graph, mode);
            continue;
        }
        int pick = -1;
        for (size_t i = 0; i < graph.nodes.size() && pick < 0; i++) {
            const Node &node = graph.nodes[i];
            if (node.file == mode && node.scope == kModes[mode] && isFunctionKind(node.kind)) {
                pick = static_cast<int>(i);
            }
        }
        for (size_t i = 0; i < graph.nodes.size() && pick < 0; i++) {
            const Node &node = graph.nodes[i];
            if (node.file == mode && isFunctionKind(node.kind)) pick = static_cast<int>(i);
        }
        const std::string id = pick >= 0 ? graph.nodes[pick].id : addEmptyFunction(graph, mode);
        addExport(graph, mode, kModes[mode], id);
    }
}

void appendRows(std::string &out, const char *key, const std::vector<std::string> &rows,
    bool last)
{
    out += "  \"";
    out += key;
    out += "\": [";
    for (size_t i = 0; i < rows.size(); i++) {
        out += i == 0 ? "\n    " : ",\n    ";
        out += rows[i];
    }
    out += rows.empty() ? "]" : "\n  ]";
    out += last ? "\n" : ",\n";
}

std::string nodeRow(const Node &node)
{
    return "{ \"id\": " + quoted(node.id) + ", \"kind\": " + quoted(node.kind) +
        ", \"subgraph\": " + quoted(node.scope) + ", \"params\": " + node.params + node.extra + " }";
}

/* held-weapon-events.md slice 2: the schema of a graph whose nodes are
 * these: v2 when any is a program node. mode is a mode file's, or -1 for
 * every node (the editor's joined graph). */
const char *schemaFor(const Graph &graph, int mode)
{
    for (const Node &node : graph.nodes) {
        if ((mode < 0 || node.file == mode) && isProgramKind(node.kind)) return "pd.weapon_graph.v2";
    }
    return "pd.weapon_graph.v1";
}

std::string modeText(const Graph &graph, int mode, const std::string &asset_id)
{
    std::vector<std::string> rows;
    for (const Node &node : graph.nodes) {
        if (node.file == mode) rows.push_back(nodeRow(node));
    }
    std::string out = std::string("{\n  \"schema\": \"") + schemaFor(graph, mode) + "\",\n  \"asset_id\": ";
    out += quoted(asset_id);
    out += ",\n  \"graph_id\": ";
    out += quoted(kModes[mode]);
    out += ",\n";
    appendRows(out, "nodes", rows, false);
    appendRows(out, "edges", graph.edges[mode], false);
    appendRows(out, "exports", graph.exports[mode], true);
    out += "}\n";
    return out;
}

/* One shared context as a row, the fields the loader needs first. */
bool contextRow(const Json &item, std::string &row, char *err, size_t cap)
{
    static const char *const order[] = { "name", "scope", "source", "type", "lifetime" };
    const JsonObject *object = item.get_ptr<JsonObject>();
    const std::string *name = object ? memberString(*object, "name") : nullptr;
    if (!name || name->empty()) {
        setError(err, cap, "a shared context has no name");
        return false;
    }
    bool first = true;
    row = "{ ";
    for (const char *key : order) {
        const Json *value = member(*object, key);
        if (!value) continue;
        if (!first) row += ", ";
        first = false;
        row += quoted(key) + ": ";
        writeValue(row, *value);
    }
    for (const auto &entry : *object) {
        bool known = false;
        for (const char *key : order) {
            if (entry.first == key) known = true;
        }
        if (known) continue;
        row += ", " + quoted(entry.first) + ": ";
        writeValue(row, entry.second);
    }
    row += " }";
    return true;
}

bool split(const char *editor_json, const char *asset_id, weapon_graph_editor_split_t *out,
    char *err, size_t cap)
{
    static const char *const what = "the editor graph";
    Json root;
    Graph graph;
    if (!asset_id || !asset_id[0]) {
        setError(err, cap, "no asset id for the graph files");
        return false;
    }
    if (!readObject(editor_json, root, what, err, cap)) return false;
    const JsonObject &object = *root.get_ptr<JsonObject>();
    if (!readNodes(object, what, 0, graph, err, cap) ||
            !readEdges(object, what, graph, err, cap) ||
            !readExports(object, what, graph, err, cap)) {
        return false;
    }
    completeModes(graph);

    /* A list of contexts replaces the template's file, even an empty one.
     * No list (or the split files' own form, the name of the context file)
     * leaves the template's file alone. */
    bool has_contexts = false;
    std::vector<std::string> contexts;
    if (const Json *value = member(object, "shared_context")) {
        if (const JsonArray *list = value->get_ptr<JsonArray>()) {
            has_contexts = true;
            for (const Json &item : *list) {
                std::string row;
                if (!contextRow(item, row, err, cap)) return false;
                contexts.push_back(std::move(row));
            }
        } else if (!value->is_string()) {
            setError(err, cap, "%s has a shared_context entry that is not a list", what);
            return false;
        }
    }

    out->primary = copyText(modeText(graph, 0, asset_id));
    out->secondary = copyText(modeText(graph, 1, asset_id));
    if (has_contexts) {
        std::string shared = "{\n  \"schema\": \"pd.weapon_shared_context.v1\",\n  \"asset_id\": ";
        shared += quoted(asset_id);
        shared += ",\n";
        appendRows(shared, "contexts", contexts, true);
        shared += "}\n";
        out->shared_context = copyText(shared);
    }
    if (!out->primary || !out->secondary || (has_contexts && !out->shared_context)) {
        setError(err, cap, "out of memory");
        return false;
    }
    return true;
}

char *compose(const char *primary_json, const char *secondary_json,
    const char *shared_context_json, const char *asset_id, char *err, size_t cap)
{
    static const char *const names[2] = { "the primary graph", "the secondary graph" };
    const char *const texts[2] = { primary_json, secondary_json };
    Graph graph;
    Json roots[2];
    if (!asset_id || !asset_id[0]) {
        setError(err, cap, "no asset id for the editor graph");
        return nullptr;
    }
    /* All nodes first: a link or an export in one file may name a node
     * from the other. */
    for (int file = 0; file < 2; file++) {
        if (!readObject(texts[file], roots[file], names[file], err, cap) ||
                !readNodes(*roots[file].get_ptr<JsonObject>(), names[file], file, graph,
                    err, cap)) {
            return nullptr;
        }
    }
    for (int file = 0; file < 2; file++) {
        const JsonObject &object = *roots[file].get_ptr<JsonObject>();
        if (!readEdges(object, names[file], graph, err, cap) ||
                !readExports(object, names[file], graph, err, cap)) {
            return nullptr;
        }
    }

    std::vector<std::string> contexts;
    if (shared_context_json && shared_context_json[0]) {
        Json root;
        const JsonArray *list = nullptr;
        if (!readObject(shared_context_json, root, "the shared context", err, cap) ||
                !memberList(*root.get_ptr<JsonObject>(), "contexts", "the shared context",
                    list, err, cap)) {
            return nullptr;
        }
        if (list) {
            for (const Json &item : *list) {
                std::string row;
                if (!contextRow(item, row, err, cap)) return nullptr;
                contexts.push_back(std::move(row));
            }
        }
    }

    std::vector<std::string> nodes;
    std::vector<std::string> edges;
    std::vector<std::string> exports;
    for (const Node &node : graph.nodes) nodes.push_back(nodeRow(node));
    for (int file = 0; file < 2; file++) {
        edges.insert(edges.end(), graph.edges[file].begin(), graph.edges[file].end());
        exports.insert(exports.end(), graph.exports[file].begin(), graph.exports[file].end());
    }

    std::string out = std::string("{\n  \"schema\": \"") + schemaFor(graph, -1) + "\",\n  \"asset_id\": ";
    out += quoted(asset_id);
    out += ",\n  \"graph_id\": \"custom_modular\",\n";
    appendRows(out, "shared_context", contexts, false);
    appendRows(out, "nodes", nodes, false);
    appendRows(out, "edges", edges, false);
    appendRows(out, "exports", exports, true);
    out += "}\n";
    char *text = copyText(out);
    if (!text) setError(err, cap, "out of memory");
    return text;
}

} // namespace

std::string weaponGraphEditorValueText(const crude_json::value &value)
{
    std::string out;
    const JsonObject *object = value.get_ptr<JsonObject>();
    if (!object || object->empty()) {
        writeValue(out, value);
        return out;
    }
    bool first = true;
    out = "{\n";
    for (const auto &entry : *object) {
        if (!first) out += ",\n";
        first = false;
        out += "  ";
        writeString(out, entry.first);
        out += ": ";
        writeValue(out, entry.second);
    }
    out += "\n}";
    return out;
}

extern "C" s32 weaponGraphEditorSplit(const char *editor_json, const char *asset_id,
    weapon_graph_editor_split_t *out, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    if (!out) {
        setError(err, err_cap, "no place for the result");
        return 0;
    }
    std::memset(out, 0, sizeof(*out));
    try {
        if (split(editor_json, asset_id, out, err, err_cap)) return 1;
    } catch (...) {
        setError(err, err_cap, "out of memory");
    }
    weaponGraphEditorSplitFree(out);
    return 0;
}

extern "C" void weaponGraphEditorSplitFree(weapon_graph_editor_split_t *split)
{
    if (!split) return;
    std::free(split->primary);
    std::free(split->secondary);
    std::free(split->shared_context);
    std::memset(split, 0, sizeof(*split));
}

extern "C" char *weaponGraphEditorCompose(const char *primary_json, const char *secondary_json,
    const char *shared_context_json, const char *asset_id, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    try {
        return compose(primary_json, secondary_json, shared_context_json, asset_id, err, err_cap);
    } catch (...) {
        setError(err, err_cap, "out of memory");
        return nullptr;
    }
}

/* ---- held-weapon-events.md slice 8a: the designer nodes ---------------- */

static const weapon_graph_designer_node_t s_WeaponGraphDesignerNodes[] = {
    { "event.weapon.trigger_pressed", "On trigger pressed", "Events" },
    { "event.weapon.ready_while_held", "On trigger held", "Events" },
    { "event.weapon.trigger_released", "On trigger released", "Events" },
    { "event.weapon.clip_empty", "On clip empty", "Events" },
    { "event.weapon.reload_done", "On reload done", "Events" },
    { "event.weapon.aiming", "While aiming", "Events" },
    { "gate.weapon.has_ammo", "Has ammo", "Checks" },
    { "gate.weapon.has_reserve", "Has reserve ammo", "Checks" },
    { "gate.weapon.ready_to_fire", "Ready to fire", "Checks" },
    { "gate.weapon.state_is", "State is", "Checks" },
    { "og.weapon.fire_once", "Fire once", "Trigger" },
    { "og.weapon.burst", "Burst", "Trigger" },
    { "og.weapon.automatic", "Automatic", "Trigger" },
    { "og.weapon.charge", "Charge then release", "Trigger" },
    { "og.weapon.cook", "Hold to cook", "Trigger" },
    { "og.weapon.use_ammo", "Use ammo", "Ammo" },
    { "og.weapon.reload", "Reload", "Ammo" },
    { "og.weapon.dry_fire", "Dry fire click", "Ammo" },
    { "og.weapon.hitscan", "Hitscan shot", "Shot" },
    { "og.weapon.beam", "Beam", "Shot" },
    { "og.weapon.launch", "Launch projectile", "Shot" },
    { "og.weapon.throw", "Throw", "Shot" },
    { "og.weapon.melee", "Melee strike", "Shot" },
    { "og.weapon.special", "Special action", "Shot" },
    { "og.weapon.mode_toggle", "Mode toggle", "Shot" },
    { "og.weapon.lock_on", "Lock-on", "Aim" },
    { "og.weapon.recoil", "Recoil", "Feel" },
    { "og.weapon.casing", "Casing", "Feel" },
    { "og.weapon.muzzle_flash", "Muzzle flash", "Feel" },
    { "og.weapon.shot_sound", "Shot sound", "Feel" },
    { "og.weapon.play_animation", "Play animation", "Feel" },
    { "og.weapon.set_state", "Set state", "State" },
};

extern "C" s32 weaponGraphDesignerNodeCount(void)
{
    return (s32)(sizeof(s_WeaponGraphDesignerNodes) / sizeof(s_WeaponGraphDesignerNodes[0]));
}

extern "C" const weapon_graph_designer_node_t *weaponGraphDesignerNodeAt(s32 index)
{
    return index >= 0 && index < weaponGraphDesignerNodeCount() ? &s_WeaponGraphDesignerNodes[index] : nullptr;
}

extern "C" const weapon_graph_designer_node_t *weaponGraphDesignerNodeFor(const char *kind)
{
    if (!kind) return nullptr;
    for (s32 i = 0; i < weaponGraphDesignerNodeCount(); i++) {
        if (std::strcmp(s_WeaponGraphDesignerNodes[i].kind, kind) == 0) return &s_WeaponGraphDesignerNodes[i];
    }
    return nullptr;
}

extern "C" s32 weaponGraphNodeIsProgram(const char *kind)
{
    return kind && isProgramKind(kind) ? 1 : 0;
}

extern "C" s32 weaponGraphNodeTakesExec(const char *kind)
{
    return !weaponGraphNodeIsProgram(kind) || std::strncmp(kind, "event.weapon.", 13) != 0 ? 1 : 0;
}

extern "C" s32 weaponGraphNodeOutputCount(const char *kind)
{
    return weaponGraphNodeIsProgram(kind) && std::strncmp(kind, "gate.weapon.", 12) == 0 ? 2 : 1;
}

extern "C" const char *weaponGraphNodeOutputName(const char *kind, s32 index)
{
    if (index < 0 || index >= weaponGraphNodeOutputCount(kind)) return nullptr;
    if (!weaponGraphNodeIsProgram(kind)) return "";
    if (std::strncmp(kind, "gate.weapon.", 12) == 0) return index == 0 ? "pass" : "fail";
    return "out";
}

/* ---- held-weapon-events.md slice 8a: the designer settings ------------- */

namespace {

struct DesignerLabel {
    const char *key;
    const char *label;
};

const DesignerLabel kDesignerLabels[] = {
    { "count", "Rounds per burst" }, { "gap", "Gap after a burst (ticks)" },
    { "rate", "Rate (rounds per minute)" }, { "start_rate", "Starting rate (rounds per minute)" },
    { "spin_up", "Spin-up (ticks)" }, { "spin_down", "Spin-down (ticks)" },
    { "burst", "Burst (rounds; 0 for none)" }, { "aimed_burst", "Burst while aiming" },
    { "rounds", "Rounds" }, { "pellets", "Pellets" }, { "through_walls", "Through walls" },
    { "shield_damage", "Shield damage" }, { "range", "Range" }, { "item", "Item" },
    { "item_function", "Item's function" }, { "give_up_gun", "Give up the gun" },
    { "action", "Action" }, { "mode", "Mode" }, { "drain", "Drain per second" }, { "style", "Style" },
    { "regrow_time", "Regrow time (ticks)" }, { "start", "Starts" }, { "time", "Time (ticks)" },
    { "steps", "Steps" }, { "ammo_per_step", "Ammo per step" }, { "min_clip", "Minimum clip" },
    { "fall_time", "Fall time (ticks)" }, { "release", "Release" }, { "damage_per_step", "Damage per step" },
    { "fuse", "Fuse (ticks)" }, { "locks", "Locks" }, { "aim_follows", "Aim follows the lock" },
    { "kick", "Kick (frames)" }, { "return", "Return (frames)" }, { "recovery", "Recovery (frames)" },
    { "refire_early", "Refire early" }, { "cut_after", "Cut after (frames)" },
    { "cut_return", "Cut return (frames)" }, { "distance", "Distance" }, { "angle", "Angle (degrees)" },
    { "slide", "Slide" }, { "shake_x", "Shake X" }, { "shake_y", "Shake Y" }, { "shake_z", "Shake Z" },
    { "spin_shake_from", "Shake at no spin" }, { "spin_shake_to", "Shake at full spin" },
    { "type", "Type" }, { "on", "On" }, { "sound", "Sound" }, { "duration", "Fire slot (frames)" },
    { "animation", "Animation" }, { "key", "State" }, { "equals", "Equals" }, { "value", "Value" },
    { "kinds", "Kinds" }, { "sight_targets_only", "Sight targets only" }, { "vehicles", "Vehicles" },
    { "relation", "Relation" }, { "alive", "Alive only" }, { "cloaked", "Cloaked" },
};

const char *designerLabel(const char *key)
{
    for (const DesignerLabel &row : kDesignerLabels) {
        if (std::strcmp(row.key, key) == 0) return row.label;
    }
    return key;
}

const weapon_graph_designer_setting_t kHasAmmoSettings[] = {
    { WEAPON_RULE_ROUNDS_PARAM, "Rounds", WEAPON_GRAPH_INPUT_WHOLE, 1.0f, (f32)WEAPON_RULE_MAX_ROUNDS, 1, nullptr },
};
const weapon_graph_designer_setting_t kStateIsSettings[] = {
    { WEAPON_RULE_KEY_PARAM, "State", WEAPON_GRAPH_INPUT_NAME, 0.0f, 0.0f, 0, nullptr },
    { WEAPON_RULE_EQUALS_PARAM, "Equals", WEAPON_GRAPH_INPUT_SWITCH, 0.0f, 0.0f, 0, nullptr },
};
const weapon_graph_designer_setting_t kSetStateSettings[] = {
    { WEAPON_RULE_KEY_PARAM, "State", WEAPON_GRAPH_INPUT_NAME, 0.0f, 0.0f, 0, nullptr },
    { WEAPON_RULE_VALUE_PARAM, "Value", WEAPON_GRAPH_INPUT_SWITCH, 0.0f, 0.0f, 0, nullptr },
};
const weapon_graph_designer_setting_t kTargetFilterSettings[] = {
    { TARGET_FILTER_KINDS_PARAM, "Kinds", WEAPON_GRAPH_INPUT_KINDS, 0.0f, 0.0f, 0,
        "player|simulant|guard|vehicle|sentry|object" },
    { TARGET_FILTER_SIGHT_TARGETS_PARAM, "Sight targets only", WEAPON_GRAPH_INPUT_SWITCH, 0.0f, 0.0f, 0, nullptr },
    { TARGET_FILTER_VEHICLES_PARAM, "Vehicles", WEAPON_GRAPH_INPUT_CHOICE, 0.0f, 0.0f, 0, "any|occupied" },
    { TARGET_FILTER_RELATION_PARAM, "Relation", WEAPON_GRAPH_INPUT_CHOICE, 0.0f, 0.0f, 0,
        "anyone|enemy|friend|owner|not_owner|not_friend" },
    { TARGET_FILTER_ALIVE_PARAM, "Alive only", WEAPON_GRAPH_INPUT_SWITCH, 0.0f, 0.0f, 0, nullptr },
    { TARGET_FILTER_CLOAKED_PARAM, "Cloaked", WEAPON_GRAPH_INPUT_CHOICE, 0.0f, 0.0f, 0, "never|with_ir|always" },
};

/* A rule table setting as the inspector takes it. */
weapon_graph_designer_setting_t designerSettingOf(const weapon_rule_setting_t *rule)
{
    weapon_graph_designer_setting_t out;

    std::memset(&out, 0, sizeof(out));
    out.key = rule->key;
    out.label = designerLabel(rule->key);
    out.lo = (f32)rule->lo;
    out.hi = (f32)rule->hi;
    switch (rule->type) {
    case WEAPON_RULE_SETTING_WHOLE:
        out.input = WEAPON_GRAPH_INPUT_WHOLE;
        out.lo_included = 1;
        break;
    case WEAPON_RULE_SETTING_RATE:
        out.input = WEAPON_GRAPH_INPUT_NUMBER;
        out.lo = 0.0f;
        out.hi = (f32)WEAPON_RULE_MAX_RATE;
        break;
    case WEAPON_RULE_SETTING_BOOL:
        out.input = WEAPON_GRAPH_INPUT_SWITCH;
        break;
    case WEAPON_RULE_SETTING_NUMBER:
        out.input = WEAPON_GRAPH_INPUT_NUMBER;
        out.lo = 0.0f;
        break;
    case WEAPON_RULE_SETTING_NONNEG:
        out.input = WEAPON_GRAPH_INPUT_NUMBER;
        out.lo = 0.0f;
        out.lo_included = 1;
        break;
    case WEAPON_RULE_SETTING_ITEM:
        out.input = WEAPON_GRAPH_INPUT_ID;
        out.choices = "own";
        break;
    case WEAPON_RULE_SETTING_SOUND:
    case WEAPON_RULE_SETTING_ANIMATION:
        out.input = WEAPON_GRAPH_INPUT_ID;
        out.choices = "none";
        break;
    case WEAPON_RULE_SETTING_ITEM_FUNCTION:
        out.input = WEAPON_GRAPH_INPUT_CHOICE;
        out.choices = "gun|0|1|2";
        break;
    case WEAPON_RULE_SETTING_ACTION:
        out.input = WEAPON_GRAPH_INPUT_CHOICE;
        out.choices = "detonate|boost|revert_boost|crouch|uplink";
        break;
    case WEAPON_RULE_SETTING_MODE:
        out.input = WEAPON_GRAPH_INPUT_CHOICE;
        out.choices = "cloak";
        break;
    case WEAPON_RULE_SETTING_STYLE:
        out.input = WEAPON_GRAPH_INPUT_CHOICE;
        out.choices = "magazine|one_at_a_time|regrow";
        break;
    case WEAPON_RULE_SETTING_CHARGE_START:
        out.input = WEAPON_GRAPH_INPUT_CHOICE;
        out.choices = "while_selected|on_press";
        break;
    case WEAPON_RULE_SETTING_RELEASE:
        out.input = WEAPON_GRAPH_INPUT_CHOICE;
        out.choices = "on_press|on_full";
        break;
    case WEAPON_RULE_SETTING_CASING:
        out.input = WEAPON_GRAPH_INPUT_CHOICE;
        out.choices = "none|standard|reaper|rifle|shotgun";
        break;
    default:
        out.input = WEAPON_GRAPH_INPUT_NAME;
        break;
    }
    return out;
}

} // namespace

extern "C" s32 weaponGraphDesignerSettings(const char *kind, weapon_graph_designer_setting_t *out, s32 cap)
{
    s32 n = 0;
    auto put = [&](const weapon_graph_designer_setting_t &setting) {
        if (out && n < cap) out[n] = setting;
        n++;
    };

    if (!kind) return 0;
    if (std::strcmp(kind, WEAPON_GATE_HAS_AMMO_KIND) == 0) {
        for (const weapon_graph_designer_setting_t &s : kHasAmmoSettings) put(s);
        return n;
    }
    if (std::strcmp(kind, WEAPON_GATE_STATE_IS_KIND) == 0) {
        for (const weapon_graph_designer_setting_t &s : kStateIsSettings) put(s);
        return n;
    }
    const u32 bit = weaponRulesModuleBit(kind);
    if (bit == WEAPON_MODULE_SET_STATE) {
        for (const weapon_graph_designer_setting_t &s : kSetStateSettings) put(s);
        return n;
    }
    if (!bit) return 0;
    for (s32 i = 0; weaponRulesSettingAt(i); i++) {
        const weapon_rule_setting_t *rule = weaponRulesSettingAt(i);
        if (rule->module == bit) put(designerSettingOf(rule));
    }
    if (bit == WEAPON_MODULE_LOCK_ON) {
        for (const weapon_graph_designer_setting_t &s : kTargetFilterSettings) put(s);
    }
    return n;
}
