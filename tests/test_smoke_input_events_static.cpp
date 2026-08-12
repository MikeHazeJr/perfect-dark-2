#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {
std::string readTextFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void requireContains(const std::string &text, const char *needle)
{
    REQUIRE(text.find(needle) != std::string::npos);
}
}

TEST_CASE("smoke input events drive mouse hover and wheel through SDL",
    "[input][menus][smoke][v004][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");

    requireContains(source, "SMOKE_EVENT_MOUSE_MOVE");
    requireContains(source, "SMOKE_EVENT_MOUSE_WHEEL");
    requireContains(source, "!strcmp(type_str, \"mouse_move\")");
    requireContains(source, "!strcmp(type_str, \"mouse_wheel\")");
    requireContains(source, "ev.type = SDL_MOUSEMOTION;");
    requireContains(source, "ev.type = SDL_MOUSEWHEEL;");
    requireContains(source, "static void smokeWarpMouseTo(s32 x, s32 y)");
    requireContains(source, "SDL_WarpMouseInWindow(w, x, y);");
    requireContains(source, "smokeWarpMouseTo(x, y);");
    requireContains(source, "ev.motion.windowID = smokeResolveWindowId();");
    requireContains(source, "ev.wheel.windowID = smokeResolveWindowId();");
    requireContains(source, "ev.wheel.preciseY = (float)wheel_y;");
    requireContains(source, "SMOKE: mouse move xy=(%d,%d) at_ms=%d");
    requireContains(source, "SMOKE: mouse wheel delta=(%d,%d) at_ms=%d");
}

TEST_CASE("smoke mouse motion and wheel reject incomplete no-op events",
    "[input][menus][smoke][v004][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");

    requireContains(source, "SMOKE: mouse_move event missing x/y");
    requireContains(source, "SMOKE: mouse_wheel event missing wheel_x/wheel_y");
    requireContains(source, "SMOKE: mouse_wheel event has zero delta");
}

TEST_CASE("smoke can deliver received archives after a live owner activates",
    "[catalog][network][smoke][v009][b1027][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");
    const std::string network = readTextFile("port/src/net/netdistrib.c");

    requireContains(source, "SMOKE_EVENT_RECEIVE_PDCA_LIST");
    requireContains(source, "!strcmp(type_str, \"receive_pdca_list\")");
    requireContains(source, "netDistribDebugReceivePdcaListForSmoke(ev->path)");
    requireContains(source, "SMOKE: receive_pdca_list path='%s' delivered=%d");
    requireContains(network, "pdcaExtractTransactionRollback(&install_transaction)");
    requireContains(network, "catalogReloadInvalidatedTypedAssets()");
    requireContains(network, "DISTRIB.CATALOG.RELOAD: id=%s result=%d");
}

TEST_CASE("Needler replacement smoke generates valid then invalid same-slot PDCA",
    "[catalog][network][modding][v009][b1027][static]")
{
    const std::string generator = readTextFile(
        "devtools/generate-needler-effect-replacement-fixtures.py");
    const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/needler_effect_replacement_rollback_smoke.json");

    requireContains(generator, "burst_tint=(0.0, 1.0, 1.0, 1.0)");
    requireContains(generator, "mod_needler:missing_replacement_sfx");
    requireContains(generator,
        "f\"{pdca_relative}|needler_effect|received|1\\n\"");
    requireContains(runner, "needler_effect_replacement_fixtures");
    requireContains(runner, "New-NeedlerEffectReplacementFixtures");
    requireContains(runner, "[System.IO.Path]::GetRelativePath(");
    requireContains(runner,
        "devtools/generate-needler-effect-replacement-fixtures.py");
    requireContains(scenario, "\"type\": \"receive_pdca_list\"");
    requireContains(scenario, "valid-list.txt");
    requireContains(scenario, "invalid-list.txt");
    requireContains(scenario,
        "DISTRIB\\\\.CATALOG\\\\.RELOAD: id=needler_effect result=1");
}

TEST_CASE("smoke runner filters auxiliary pipeline output before summary",
    "[smoke][tooling][static][b1029]")
{
    const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
    requireContains(runner, "$invokeOutput = @(Invoke-SmokeTest");
    requireContains(runner, "Properties.Match('Passed').Count -gt 0");
    requireContains(runner, "Properties.Match('AssertionsTotal').Count -gt 0");
    requireContains(runner, "$resultCandidates.Count -ne 1");
    requireContains(runner, "$results += $resultCandidates[0]");
}

TEST_CASE("smoke can prove overlapping weapon owners through production lifecycle",
    "[catalog][weapon][effect][smoke][v009][owner][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");
    const std::string scenario = readTextFile(
        "tools/smoke-verify/tests/needler_effect_overlapping_owner_smoke.json");

    requireContains(source, "SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE");
    requireContains(source, "SMOKE_EVENT_CATALOG_WEAPON_RELEASE");
    requireContains(source, "!strcmp(type_str, \"catalog_weapon_acquire\")");
    requireContains(source, "catalogLoadTypedAsset(ASSET_WEAPON, ev->path)");
    requireContains(source, "catalogReleaseTypedAsset(ASSET_WEAPON, ev->path)");
    requireContains(source, "SMOKE: catalog_weapon_owner op=%s id='%s' result=%d");
    requireContains(scenario, "\"type\": \"catalog_weapon_acquire\"");
    requireContains(scenario, "\"type\": \"catalog_weapon_release\"");
    requireContains(scenario, "modmgr: applying changes");
    requireContains(scenario, "PopStyleColor\\\\(\\\\) too many times");
    requireContains(scenario, "Missing PopStyleColor");
    requireContains(scenario, "ref=2->1 \\\\(retained\\\\)");
    requireContains(scenario, "ref=1->0 \\\\(freed\\\\)");
}
