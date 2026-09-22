#include "catch.hpp"
#include "weapon_command_source.h"
#include <cstring>
#include <memory>
#include <string>
extern "C" {
#include "constants.h"
}
namespace {
using Source = std::unique_ptr<weapon_command_source, decltype(&weaponCommandSourceFree)>;
const char *source = R"({"source_format":"weapon_animation_commands","name":"Reload","command_count":12,"commands":[
{"command":"show_part","part":21,"value":42},
{"command":"hide_part","part":22,"value":41},
{"command":"wait_for_trigger_release","slot":5},
{"command":"wait_ticks","slot":6,"ticks":2},
{"command":"play_sound","slot":7,"sound":"soundmod:reload"},
{"command":"include_animation","slot":1,"animation":"firstmod:reload"},
{"command":"random_animation","weight":40,"animation":"secondmod:reload"},
{"command":"repeat_until_full","slot":9,"dont_loop":1,"goto_trigger":3},
{"command":"popout_sack_of_pills","slot":10},
{"command":"play_character_animation","animation":"clipmod:reload","direction":65535,"speed":55536},
{"command":"set_sound_speed","slot":11,"speed":900},
{"command":"end"}]})";
Source parse(const std::string &text, bool valid = true, const char *id = "testmod:reload") {
    char error[512]{};
    Source result(weaponCommandSourceRead(id, text.data(), text.size(), error, sizeof(error)), weaponCommandSourceFree);
    INFO(error); if (valid) REQUIRE(result); else { REQUIRE_FALSE(result); REQUIRE(error[0]); }
    return result;
}
weapon_command_source_item item(const weapon_command_source *s, size_t i) {
    weapon_command_source_item v{}; REQUIRE(weaponCommandSourceItem(s, i, &v)); return v;
}
std::string replace(std::string s, const char *old, const char *replacement) {
    const auto p = s.find(old); REQUIRE(p != std::string::npos); s.replace(p, std::strlen(old), replacement); return s;
}
}
TEST_CASE("public weapon command reader preserves all native command semantics and exact references", "[weapon-command-source]") {
    std::string json = source; auto s = parse(json); json.assign(json.size(), '?');
    REQUIRE(std::string(weaponCommandSourceId(s.get())) == "testmod:reload");
    REQUIRE(std::string(weaponCommandSourceName(s.get())) == "Reload"); REQUIRE(weaponCommandSourceCount(s.get()) == 12);
    const int types[] = {GUNCMD_SHOWPART, GUNCMD_HIDEPART, GUNCMD_WAITFORZRELEASED, GUNCMD_WAITTIME,
        GUNCMD_PLAYSOUND, GUNCMD_INCLUDE, GUNCMD_RANDOM, GUNCMD_REPEATUNTILFULL,
        GUNCMD_POPOUTSACKOFPILLS, GUNCMD_PLAYANIMATION, GUNCMD_SETSOUNDSPEED, GUNCMD_END};
    for (size_t i = 0; i < 12; ++i) REQUIRE(item(s.get(), i).type == types[i]);
    REQUIRE(item(s.get(), 0).trigger == 21); REQUIRE(item(s.get(), 0).value == 42);
    REQUIRE(item(s.get(), 1).trigger == 22); REQUIRE(item(s.get(), 1).value == 41);
    REQUIRE(item(s.get(), 2).trigger == 5); REQUIRE(item(s.get(), 3).value == 2);
    REQUIRE(item(s.get(), 4).reference_kind == WEAPON_COMMAND_SOUND);
    REQUIRE(std::string(item(s.get(), 4).reference) == "soundmod:reload");
    REQUIRE(item(s.get(), 5).selector == 1); REQUIRE(item(s.get(), 5).reference_kind == WEAPON_COMMAND_COMMANDS);
    REQUIRE(std::string(item(s.get(), 5).reference) == "firstmod:reload");
    REQUIRE(std::string(item(s.get(), 6).reference) == "secondmod:reload"); REQUIRE(item(s.get(), 6).trigger == 40);
    REQUIRE(item(s.get(), 7).trigger == 9); REQUIRE(item(s.get(), 7).value == ((intptr_t{1} << 16) | 3));
    REQUIRE(item(s.get(), 8).trigger == 10); REQUIRE(item(s.get(), 9).reference_kind == WEAPON_COMMAND_CLIP);
    REQUIRE(item(s.get(), 9).value == ((intptr_t{65535} << 16) | 55536));
    REQUIRE(item(s.get(), 10).value == 900); REQUIRE_FALSE(item(s.get(), 11).reference);
}
TEST_CASE("public weapon command reader rejects malformed source without partial records", "[weapon-command-source]") {
    const auto text = GENERATE_COPY(
        replace(source, "\"slot\":5", "\"slot\":5.5"),
        replace(source, "\"slot\":5", "\"slot\":5.0000000000000000001"),
        replace(source, "\"slot\":5", "\"slot\":1e-100"),
        replace(source, "\"slot\":5", "\"slot\":65536"),
        replace(source, "\"slot\":1,", "\"slot\":256,"),
        replace(source, "\"speed\":55536", "\"speed\":65536"),
        replace(source, "\"part\":21", "\"part\":-1"),
        replace(source, "\"slot\":5", "\"slot\":true"),
        replace(source, "\"weight\":40", "\"weight\":101"),
        replace(source, "\"command_count\":12", "\"command_count\":11"),
        replace(source, "\"part\":21,", ""),
        replace(source, "\"part\":21", "\"part\":21,\"part\":22"),
        replace(source, "\"part\":21", "\"part\":21,\"ignored\":5"),
        replace(source, "\"show_part\"", "\"unknown_command\""),
        replace(source, "\"weapon_animation_commands\"", "\"unknown_format\""),
        replace(source, "{\"command\":\"end\"}", "{\"command\":\"wait_ticks\",\"slot\":1,\"ticks\":1}"),
        std::string(source) + "garbage");
    parse(text, false);
}
TEST_CASE("public weapon command reader rejects anonymous foreign and unqualified dependencies", "[weapon-command-source]") {
    const auto text = GENERATE_COPY(
        replace(source, "\"firstmod:reload\"", "\"reload\""),
        replace(source, "\"soundmod:reload\"", "1"),
        replace(source, "\"clipmod:reload\"", "null"),
        replace(source, "\"firstmod:reload\"", "\"firstmod:reload\\u0000hidden\""),
        replace(source, "\"firstmod:reload\"", "\"firstmod:with space\""),
        replace(source, "\"name\":\"Reload\"", "\"name\":\"Reload\",\"id\":\"foreign:reload\""));
    parse(text, false);
}
TEST_CASE("public weapon command reader decodes JSON escapes before resolving identity", "[weapon-command-source]") {
    auto s = parse(replace(source, "\"firstmod:reload\"", "\"firstmod:\\u0072eload\""));
    REQUIRE(std::string(item(s.get(), 5).reference) == "firstmod:reload");
    auto exponent = parse(replace(source, "\"slot\":5", "\"slot\":50.00e-1"));
    REQUIRE(item(exponent.get(), 2).trigger == 5);
}
TEST_CASE("public weapon command reader owns dynamic source beyond legacy pool capacity", "[weapon-command-source]") {
    std::string json = "{\"source_format\":\"weapon_animation_commands\",\"name\":\"Large\",\"commands\":[";
    for (size_t i = 0; i < 4096; ++i) json += "{\"command\":\"wait_ticks\",\"slot\":1,\"ticks\":2},";
    json += "{\"command\":\"end\"}]}";
    auto s = parse(json); REQUIRE(weaponCommandSourceCount(s.get()) == 4097);
    REQUIRE(item(s.get(), 4095).value == 2); REQUIRE(item(s.get(), 4096).type == GUNCMD_END);
}
TEST_CASE("public weapon command reader bounds access and requires a final terminator", "[weapon-command-source]") {
    const auto empty = "{\"source_format\":\"weapon_animation_commands\",\"name\":\"Empty\",\"commands\":[{\"command\":\"end\"}]}";
    auto s = parse(empty); REQUIRE(weaponCommandSourceCount(s.get()) == 1);
    weapon_command_source_item out{}; out.reference = "stale";
    REQUIRE_FALSE(weaponCommandSourceItem(s.get(), 1, &out)); REQUIRE_FALSE(out.reference);
    REQUIRE_FALSE(weaponCommandSourceItem(nullptr, 0, &out)); REQUIRE_FALSE(weaponCommandSourceItem(s.get(), 0, nullptr));
    parse(replace(empty, "[{\"command\":\"end\"}]", "[]"), false);
    parse(replace(empty, "[{\"command\":\"end\"}]", "[{\"command\":\"end\"},{\"command\":\"end\"}]"), false);
}

TEST_CASE("public weapon command reader consumes extracted release trigger variants", "[weapon-command-source]") {
    const auto implicit = replace(source, "\"slot\":5", "\"trigger\":\"z\"");
    auto zero = parse(implicit);
    REQUIRE(item(zero.get(), 2).type == GUNCMD_WAITFORZRELEASED);
    REQUIRE(item(zero.get(), 2).trigger == 0);
    auto explicit_slot = parse(replace(source, "\"slot\":5", "\"trigger\":\"z\",\"slot\":5"));
    REQUIRE(item(explicit_slot.get(), 2).trigger == 5);
    parse(replace(implicit, "\"trigger\":\"z\"", "\"trigger\":\"other\""), false);
    parse(replace(implicit, "\"trigger\":\"z\"", "\"trigger\":0"), false);
}
