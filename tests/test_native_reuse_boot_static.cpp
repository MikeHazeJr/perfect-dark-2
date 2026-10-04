#include "catch.hpp"

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

namespace {
std::string reuseSource(const char *path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}
}

TEST_CASE("native reuse activates before startup writers", "[native-reuse][static]")
{
    const auto source = reuseSource("port/src/main.c");
    const auto start = source.find("int main(int argc, const char **argv)");
    REQUIRE(start != std::string::npos);
    const auto body = source.substr(start);
    const auto init = body.find("bootInitNativeReuse(argc, argv)");
    REQUIRE(init != std::string::npos);
    REQUIRE(body.find("sysArgCheck(\"--reuse-write-policy-info\")") < init);
    REQUIRE(body.find("if (argc != 2) return 2;") < init);
    REQUIRE(body.find("nativeWritePolicyLinkContract()") < init);
    REQUIRE(init < body.find("updaterApplyPending()"));
    REQUIRE(init < body.find("crashInit()"));
    REQUIRE(init < body.find("sysInit();"));
    REQUIRE(init < body.find("fsInit();"));
    REQUIRE(body.find("nativeWritePolicyRefused()") < body.find("smokeHarnessInit()"));
    REQUIRE(source.find("nativeWritePolicyAllowsPath(path)") != std::string::npos);
    REQUIRE(source.find("!bootReuseAbsolutePath(path)") != std::string::npos);
    const auto reuseStart = source.find("static s32 bootInitNativeReuse(");
    REQUIRE(reuseStart != std::string::npos);
    const auto reuse = source.substr(reuseStart, start - reuseStart);
    const auto activation = reuse.find("nativeWritePolicyInit(base, root)");
    REQUIRE(activation != std::string::npos);
    REQUIRE(reuse.substr(0, activation).find("fprintf(") == std::string::npos);
    REQUIRE(reuse.substr(0, activation).find("printf(") == std::string::npos);
    REQUIRE(reuse.find("!strcmp(outputs[i], \"--debug-home-path\") && !strcmp(path, root)")
            != std::string::npos);
    REQUIRE(reuse.find("(!debugRoot && !nativeWritePolicyAllowsPath(path))") != std::string::npos);
    for (const char *flag : {"--smoke", "--no-net", "--no-update-check", "--savedir",
                            "--debug-home-path", "--smoke-screenshot-dir", "--portable",
                            "--moddir", "--extract-", "--import-", "--migrate-"}) {
        REQUIRE(source.find(flag) != std::string::npos);
    }
}

TEST_CASE("native reuse UI discovery uses only verified base mods", "[native-reuse][static]")
{
    for (const char *path : {"port/fast3d/pdgui_font_mod.cpp",
                            "port/fast3d/pdgui_theme_loader.cpp",
                            "port/fast3d/pdgui_theme.cpp"}) {
        const auto source = reuseSource(path);
        REQUIRE(source.find("#include \"native_write_policy.h\"") != std::string::npos);
        REQUIRE(source.find("nativeWritePolicyActive()") < source.find("fsFullPath(\"$B/mods\""));
        REQUIRE(source.find("fsFullPath(\"$B/mods\"") != std::string::npos);
    }
}

TEST_CASE("device policy refusal cannot become scripted smoke success", "[native-reuse][static]")
{
    const auto source = reuseSource("port/src/smoke_harness.c");
    for (const char *signature : {"int smokeHarnessInit(void)", "void smokeHarnessTick(void)",
                                 "void smokeHarnessExit(int code, const char *reason)",
                                 "static s32 smokePadTickTransition(const SmokeEvent *ev, u32 now)"}) {
        const auto start = source.find(signature);
        REQUIRE(start != std::string::npos);
        REQUIRE(source.substr(start, 240).find("nativeWritePolicyRefused()") != std::string::npos);
    }
}

TEST_CASE("native reuse preserves production typed source admission", "[native-reuse][static]")
{
    const auto source = reuseSource("port/src/main.c");
    const auto start = source.find("static void bootRunCatalogWork(void *arg)");
    const auto end = source.find("static s32 bootInitNativeReuse", start);
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    const auto boot = source.substr(start, end - start);
    for (const char *call : {"romExtractAllFiles()", "romExtractVerifyAll()",
                            "romExtractAllSegments()", "romExtractVerifyAllSegments()",
                            "romExtractAllPdmesh(0)", "romExtractAllPdweapon(0)",
                            "romExtractAllPdarena(0)", "romExtractAllPdtexture(0)"}) {
        REQUIRE(boot.find(call) != std::string::npos);
    }
    REQUIRE(source.find("boot stopped: %d extraction/load failure(s)") != std::string::npos);
}

TEST_CASE("native reuse routes derived extraction metadata away from source", "[native-reuse][static]")
{
    const auto raw = reuseSource("port/src/romextract.c");
    REQUIRE(raw.find("nativeWritePolicyCachePath(\"extraction-sha256\"") != std::string::npos);
    REQUIRE(raw.find("romExtractReuseDigestMatches(outFull, romData") != std::string::npos);
    REQUIRE(raw.find("romExtractReuseDigestMatches(outFull, segData") != std::string::npos);
    REQUIRE(raw.find("immutable reuse requires existing raw file") != std::string::npos);
    REQUIRE(raw.find("immutable reuse requires existing raw segment") != std::string::npos);
    REQUIRE(raw.find("immutable reuse refuses sidecar mismatch") != std::string::npos);
    const auto cache = reuseSource("port/src/romextract_pd_cache.c");
    const auto writer = cache.find("void romExtractPdFastCacheWrite(");
    REQUIRE(writer != std::string::npos);
    const auto body = cache.substr(writer);
    REQUIRE(body.find("nativeWritePolicyCachePath(\"pdextract-stamp\"")
            < body.find("fopen(stamp_path, \"wb\")"));
}

TEST_CASE("native reuse disallows cache fallback and install maintenance", "[native-reuse][static]")
{
    const auto compiler = reuseSource("port/src/modasset_compiler.c");
    REQUIRE(compiler.find("nativeWritePolicyActive() && strcmp(cache_roots[i], \"$S/mod-cache\")")
            != std::string::npos);
    REQUIRE(compiler.find("nativeWritePolicyActive() && strcmp(compact_roots[i], \"$S/mc\")")
            != std::string::npos);
    const auto mods = reuseSource("port/src/modmgr.c");
    REQUIRE(mods.find("if (!nativeWritePolicyActive()) modMigrateRun(modsdir, &mig)")
            != std::string::npos);
    REQUIRE(mods.find("fsFullPath(\"$B/\" MODMGR_MODS_DIR, candidateBufs[0]")
            != std::string::npos);
    const auto updater = reuseSource("port/src/updater.c");
    for (const char *call : {"void updaterCheckAsync(void)", "void updaterDownloadAsync(",
                            "s32 updaterApplyPending(void)", "void updaterCleanupOld(void)"}) {
        const auto start = updater.find(call);
        REQUIRE(start != std::string::npos);
        const auto brace = updater.find('{', start);
        REQUIRE(updater.substr(brace, 80).find("if (nativeWritePolicyActive())") != std::string::npos);
    }
}
