#include "weapon_command_source.h"
#include "modasset_gltf_document.h"
#include "crude_json.h"
#include "assetcatalog.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include "constants.h"
}
namespace {
using Json = crude_json::value;
[[noreturn]] void bad(const std::string &s) { throw std::runtime_error(s); }
const Json &field(const Json &v, const char *key) {
    if (!v.is_object() || !v.contains(key)) bad(std::string("missing command field: ") + key);
    return v[key];
}
void keys(const Json &v, std::initializer_list<const char *> allowed) {
    if (!v.is_object()) bad("command requires an object");
    for (const auto &p : v.get<crude_json::object>()) {
        bool found = false;
        for (const auto *name : allowed) if (p.first == name) found = true;
        if (!found) bad("unsupported command field: " + p.first);
    }
}
std::string string(const Json &v) {
    if (!v.is_string()) bad("command field requires a string");
    const auto &s = v.get<crude_json::string>();
    if (s.empty() || s.find('\0') != std::string::npos) bad("empty or embedded-NUL command string");
    return s;
}
bool idValid(const char *s) {
    if (!s || !*s || std::strlen(s) >= CATALOG_ID_LEN) return false;
    const char *c = std::strchr(s, ':');
    if (!c || c == s || !c[1] || std::strchr(c + 1, ':')) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        if (*p <= 32 || *p == '\\') return false;
    return true;
}
uint32_t integer(const Json &v, uint32_t high) {
    if (!v.is_number()) bad("command scalar requires an integer");
    const double n = v.get<crude_json::number>();
    if (!(n >= 0 && n <= high) || std::floor(n) != n) bad("command scalar outside native range");
    return static_cast<uint32_t>(n);
}
/* All numbers in this source format are native integer fields. Inspect their
 * original validated spans so JSON-to-double rounding cannot admit fractions
 * such as 1.0000000000000000001 or underflow a nonzero value to zero. This is
 * not a second JSON parser: the shared reader has already checked the grammar. */
void integerSpellings(const char *json, size_t size) {
    const char *end = json + size;
    for (const char *p = json; p < end; ++p) {
        if (*p == '"') {
            for (++p; p < end; ++p) {
                if (*p == '\\') ++p;
                else if (*p == '"') break;
            }
        } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
            std::string digits;
            bool fraction = false;
            size_t places = 0;
            if (*p == '-') ++p;
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '.')) {
                if (*p == '.') fraction = true;
                else { digits += *p; if (fraction) ++places; }
                ++p;
            }
            int exponent = 0, sign = 1;
            if (p < end && (*p == 'e' || *p == 'E')) {
                ++p;
                if (*p == '-' || *p == '+') { if (*p == '-') sign = -1; ++p; }
                while (p < end && *p >= '0' && *p <= '9') {
                    exponent = std::min(1000000, exponent * 10 + (*p - '0')); ++p;
                }
            }
            if (digits.find_first_not_of('0') != std::string::npos) {
                const int64_t shift = int64_t(exponent) * sign - int64_t(places);
                if (shift < 0 && (uint64_t(-shift) > digits.size()
                        || digits.find_first_not_of('0', digits.size() - size_t(-shift)) != std::string::npos))
                    bad("weapon command scalar is not exactly an integer");
            }
            --p;
        }
    }
}
struct Command {
    weapon_command_source_item native{};
    std::string reference;
};
void reference(Command &c, const Json &v, weapon_command_reference kind) {
    c.reference = string(v);
    if (!idValid(c.reference.c_str())) bad("command dependency requires an exact catalog ID");
    c.native.reference_kind = kind;
}
Command read(const Json &v) {
    const auto name = string(field(v, "command"));
    Command c; auto &n = c.native;
    if (name == "end") {
        keys(v, {"command"}); n.type = GUNCMD_END;
    } else if (name == "show_part" || name == "hide_part") {
        keys(v, {"command", "part", "value"});
        n.type = name == "show_part" ? GUNCMD_SHOWPART : GUNCMD_HIDEPART;
        n.trigger = static_cast<uint16_t>(integer(field(v, "part"), UINT16_MAX));
        n.value = integer(field(v, "value"), INT16_MAX);
    } else if (name == "wait_for_trigger_release") {
        keys(v, {"command", "trigger", "slot"});
        if (v.contains("trigger") && string(v["trigger"]) != "z") bad("unsupported release trigger");
        n.type = GUNCMD_WAITFORZRELEASED;
        // Extracted v1 sources omit a zero slot and name the native trigger z.
        n.trigger = v.contains("slot") ? static_cast<uint16_t>(integer(v["slot"], UINT16_MAX)) : 0;
    } else if (name == "popout_sack_of_pills") {
        keys(v, {"command", "slot"});
        n.type = GUNCMD_POPOUTSACKOFPILLS;
        n.trigger = static_cast<uint16_t>(integer(field(v, "slot"), UINT16_MAX));
    } else if (name == "wait_ticks" || name == "set_sound_speed") {
        const char *key = name == "wait_ticks" ? "ticks" : "speed";
        keys(v, {"command", "slot", key});
        n.type = name == "wait_ticks" ? GUNCMD_WAITTIME : GUNCMD_SETSOUNDSPEED;
        n.trigger = static_cast<uint16_t>(integer(field(v, "slot"), UINT16_MAX));
        n.value = integer(field(v, key), INT32_MAX);
    } else if (name == "play_sound") {
        keys(v, {"command", "slot", "sound"}); n.type = GUNCMD_PLAYSOUND;
        n.trigger = static_cast<uint16_t>(integer(field(v, "slot"), UINT16_MAX));
        reference(c, field(v, "sound"), WEAPON_COMMAND_SOUND);
    } else if (name == "include_animation") {
        keys(v, {"command", "slot", "animation"}); n.type = GUNCMD_INCLUDE;
        n.selector = static_cast<uint8_t>(integer(field(v, "slot"), UINT8_MAX));
        reference(c, field(v, "animation"), WEAPON_COMMAND_COMMANDS);
    } else if (name == "random_animation") {
        keys(v, {"command", "weight", "animation"}); n.type = GUNCMD_RANDOM;
        n.trigger = static_cast<uint16_t>(integer(field(v, "weight"), 100));
        reference(c, field(v, "animation"), WEAPON_COMMAND_COMMANDS);
    } else if (name == "repeat_until_full") {
        keys(v, {"command", "slot", "dont_loop", "goto_trigger"}); n.type = GUNCMD_REPEATUNTILFULL;
        n.trigger = static_cast<uint16_t>(integer(field(v, "slot"), UINT16_MAX));
        n.value = (static_cast<intptr_t>(integer(field(v, "dont_loop"), UINT16_MAX)) << 16)
            | integer(field(v, "goto_trigger"), UINT16_MAX);
    } else if (name == "play_character_animation") {
        keys(v, {"command", "animation", "direction", "speed"}); n.type = GUNCMD_PLAYANIMATION;
        reference(c, field(v, "animation"), WEAPON_COMMAND_CLIP);
        // v1 source stores two 16-bit words, including signed native encodings.
        n.value = (static_cast<intptr_t>(integer(field(v, "direction"), UINT16_MAX)) << 16)
            | integer(field(v, "speed"), UINT16_MAX);
    } else bad("unknown weapon command: " + name);
    return c;
}
}
struct weapon_command_source {
    std::string id, name;
    std::vector<Command> commands;
};
extern "C" weapon_command_source *weaponCommandSourceRead(const char *id,
        const char *json, size_t size, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    try {
        if (!idValid(id)) bad("weapon command source requires exact catalog identity");
        Json root;
        if (!json || !modAssetJsonReadValue(json, size, root)) bad("weapon command source requires complete strict JSON");
        integerSpellings(json, size);
        keys(root, {"source_format", "id", "name", "command_count", "commands"});
        if (string(field(root, "source_format")) != "weapon_animation_commands") bad("unsupported weapon command source format");
        auto out = std::make_unique<weapon_command_source>(); out->id = id;
        if (root.contains("id") && string(root["id"]) != id) bad("weapon command source identity mismatch");
        out->name = string(field(root, "name"));
        const auto &commands = field(root, "commands");
        if (!commands.is_array() || commands.get<crude_json::array>().empty()) bad("weapon command source requires an END-terminated array");
        const auto &rows = commands.get<crude_json::array>();
        if (root.contains("command_count") && integer(root["command_count"], INT32_MAX) != rows.size()) bad("weapon command count does not match source");
        out->commands.reserve(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            Command c;
            try { c = read(rows[i]); }
            catch (const std::exception &e) { bad("command " + std::to_string(i) + ": " + e.what()); }
            if ((c.native.type == GUNCMD_END) != (i + 1 == rows.size())) bad("END must be the final weapon command");
            out->commands.push_back(std::move(c));
        }
        return out.release();
    } catch (const std::exception &e) { if (error && cap) std::snprintf(error, cap, "%s", e.what()); }
    catch (...) { if (error && cap) std::snprintf(error, cap, "%s", "weapon command allocation failed"); }
    return nullptr;
}
extern "C" void weaponCommandSourceFree(weapon_command_source *s) { delete s; }
extern "C" const char *weaponCommandSourceId(const weapon_command_source *s) { return s ? s->id.c_str() : ""; }
extern "C" const char *weaponCommandSourceName(const weapon_command_source *s) { return s ? s->name.c_str() : ""; }
extern "C" size_t weaponCommandSourceCount(const weapon_command_source *s) { return s ? s->commands.size() : 0; }
extern "C" int weaponCommandSourceItem(const weapon_command_source *s, size_t index, weapon_command_source_item *out) {
    if (out) *out = {};
    if (!s || !out || index >= s->commands.size()) return 0;
    *out = s->commands[index].native;
    out->reference = s->commands[index].reference.empty() ? nullptr : s->commands[index].reference.c_str();
    return 1;
}
