// Global AI lists as public source, slice 1 (design step 5,
// context/designs/modding/global-ai-lists.md): the binary's 46 global lists
// are written as the same pd2.scenario.ai.lists.v1 rows a scenario's
// ai/ailists.json holds and compiled back through the scenario reader and
// compiler; --check-global-ailists compares the result with the binary's
// bytes in the client. The writer and the compiler live in game-dependent
// files pd-tests cannot link, so these cases pin their shape.

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readTextFile(const char *path)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string functionBody(const std::string &src, const char *signature)
{
    const size_t start = src.find(signature);
    if (start == std::string::npos) {
        return {};
    }
    const size_t end = src.find("\n}\n", start);
    return src.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

} // namespace

TEST_CASE("global AI list rows carry every key the scenario AI list reader needs", "[ai][global-ailists]")
{
    const std::string arena = readTextFile("port/src/romextract_pdarena.c");
    REQUIRE_FALSE(arena.empty());
    const std::string writer = functionBody(arena, "char *romExtractGlobalAiListsJson(u32 *out_lists, u32 *out_commands)");
    REQUIRE_FALSE(writer.empty());

    // ai-typed-operands.md slice 3: a row gives its bytes by args alone (with
    // catalog ids for models, weapons, bodies and heads), so the reader needs
    // these keys and no operands or *_catalog_id fields.
    const char *keys[] = {"ailist_ref", "list_id", "graph_node", "command_index", "offset",
        "opcode", "opcode_name"};
    for (const char *key : keys) {
        INFO(key);
        REQUIRE(writer.find(std::string("\\\"") + key + "\\\": ") != std::string::npos);
    }
    REQUIRE(writer.find("pd2.scenario.ai.lists.v1") != std::string::npos);
    REQUIRE(writer.find("scenario.ai.global_%04x.command.%04u") != std::string::npos);
    REQUIRE(writer.find("g_GlobalAilists[i].list != NULL") != std::string::npos);
    REQUIRE(writer.find("chraiGetCommandLength(list, offset)") != std::string::npos);
    REQUIRE(writer.find("if (opcode == AICMD_END) {") != std::string::npos);
    REQUIRE(writer.find("s_appendAiArgsJson(&json, cmd, len)") != std::string::npos);
    for (const char *gone : {"operands", "model_catalog_id", "weapon_catalog_id",
            "body_catalog_id", "head_catalog_id"}) {
        INFO(gone);
        REQUIRE(writer.find(std::string("\\\"") + gone + "\\\": ") == std::string::npos);
    }
}

TEST_CASE("the exported AI list compiler lays commands out as the stage setup compiler does", "[ai][global-ailists]")
{
    const std::string runtime = readTextFile("port/src/scenario_source_runtime.c");
    REQUIRE_FALSE(runtime.empty());
    const std::string compile = functionBody(runtime, "s32 scenarioSourceAiListsCompile(const char *json_text,");
    REQUIRE_FALSE(compile.empty());
    REQUIRE(compile.find("s_loadAiListSourceJson(text, &table)") != std::string::npos);
    REQUIRE(compile.find("lists[i].bytes[cursor++] = (u8)((cmd->opcode >> 8) & 0xff);") != std::string::npos);
    REQUIRE(compile.find("lists[i].bytes[cursor++] = (u8)(cmd->opcode & 0xff);") != std::string::npos);
    // The stage setup compiler's layout, which the global one must match.
    REQUIRE(runtime.find("data[list_cursor++] = (u8)((cmd->opcode >> 8) & 0xff);") != std::string::npos);
    REQUIRE(runtime.find("data[list_cursor++] = (u8)(cmd->opcode & 0xff);") != std::string::npos);
    // scenario_source_runtime.c reads no new game global for this.
    REQUIRE(runtime.find("g_GlobalAilists") == std::string::npos);
}

TEST_CASE("the global AI list round trip compares every command and the end", "[ai][global-ailists]")
{
    const std::string check = readTextFile("port/src/scenario_global_ailists.c");
    const std::string mainc = readTextFile("port/src/main.c");
    REQUIRE_FALSE(check.empty());
    REQUIRE_FALSE(mainc.empty());
    REQUIRE(check.find("romExtractGlobalAiListsJson(&written_lists, &written_commands)") != std::string::npos);
    REQUIRE(check.find("scenarioSourceAiListsCompile(json, &lists, &count, error, sizeof(error))") != std::string::npos);
    REQUIRE(check.find("native_len = chraiGetCommandLength(native, offset);") != std::string::npos);
    REQUIRE(check.find("if (opcode == AICMD_END) {") != std::string::npos);
    REQUIRE(check.find("AI.GLOBAL: round trip %d/%d lists exact") != std::string::npos);
    REQUIRE(mainc.find("sysArgCheck(\"--check-global-ailists\")") != std::string::npos);
    REQUIRE(mainc.find("scenarioGlobalAiListsRoundTrip() != 0") != std::string::npos);
}
