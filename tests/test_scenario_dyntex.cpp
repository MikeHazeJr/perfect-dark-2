// Animated surfaces of scenario-source scenes (B-1172, the Room effects row):
// scenario_dyntex.c must move a vertex's texture coordinates exactly as
// src/game/dyntex.c does, pick the same textures and stage rules as tex.c
// and dyntexSetCurrentType, and convert coordinates to scene UVs exactly as
// the extractor does. The oracles below are transcribed from those files;
// the source pins at the end fail if the legacy code they mirror changes.

#include "catch.hpp"

extern "C" {
#include "scenario_dyntex.h"
}

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

extern "C" float sinf(float angle);
extern "C" float cosf(float angle);

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

const float kBadTau = 3.141092641f * 2;

// dyntexUpdateLinear, dyntexUpdateMonitor, dyntexUpdateOcean and
// dyntexUpdateArrows on one vertex (base s, t from g_DyntexVertices).
void oracleLinear(float frac, int16_t bs, int16_t bt, int16_t *s, int16_t *t)
{
    int16_t tmp = (int32_t)(frac * 10.0f * 4096.0f) % 4096;
    *t = bt + tmp;
    *s = bs;
}

void oracleMonitor(float frac, int16_t bs, int16_t bt, int16_t *s, int16_t *t)
{
    int16_t tmp = (int32_t)(frac * 4.0f * 4096.0f) % 4096;
    *t = bt - tmp;
    *s = bs;
}

void oracleOcean(float frac, int16_t bs, int16_t bt, int16_t *s, int16_t *t)
{
    float f24 = frac * 5.0f;
    float angle;
    uint32_t ripsize = 65;
    uint32_t modula = 22;

    angle = ((bt % modula) / (float)modula + f24) * kBadTau;
    *t = bt + (int16_t)(sinf(angle) * ripsize);
    angle = (((bs + 22) % modula) / (float)modula + f24) * kBadTau;
    *s = bs + (int16_t)(cosf(angle) * ripsize);
}

void oracleArrows(float frac, int16_t bs, int16_t bt, int16_t *s, int16_t *t)
{
    int32_t tmp = ((int32_t)((1.0f - frac) * 60.0f * 8.0f) % 8) * 256;
    *s = bs + tmp;
    *t = bt;
}

const float kFracs[] = {0.0f, 0.0003f, 0.1234f, 0.25f, 0.4999f, 0.5f, 0.77f, 0.9999f};
const int16_t kCoords[] = {-23700, -0x5d00, -4097, -23, -22, -1, 0, 1, 21, 22, 4095, 4096, 0x5d00, 23700};

} // namespace

TEST_CASE("dyntex presets have stable scene names", "[scenario][dyntex][B-1172]")
{
    const char *names[] = {"river", "monitor", "ocean", "arrows", "teleportal", "powerring", "powerjuice"};
    for (int i = 0; i < 7; i++) {
        INFO(names[i]);
        REQUIRE(scenarioDyntexKindFromName(names[i]) == SCENARIO_DYNTEX_RIVER + i);
        REQUIRE(std::string(scenarioDyntexKindName(SCENARIO_DYNTEX_RIVER + i)) == names[i]);
    }
    REQUIRE(scenarioDyntexKindName(SCENARIO_DYNTEX_NONE) == nullptr);
    REQUIRE(scenarioDyntexKindName(SCENARIO_DYNTEX_KIND_COUNT) == nullptr);
    REQUIRE(scenarioDyntexKindFromName("water") == -1);
    REQUIRE(scenarioDyntexKindFromName("River") == -1);
    REQUIRE(scenarioDyntexKindFromName("") == -1);
    REQUIRE(scenarioDyntexKindFromName(nullptr) == -1);
}

TEST_CASE("dyntex base kinds follow tex.c's textures and the stage rules", "[scenario][dyntex][B-1172]")
{
    const int chicago = 0x1d, deepsea = 0x38, airbase = 0x27, villa = 0x2c;
    const int investigation = 0x33, attackship = 0x34, airforceone = 0x31;

    REQUIRE(scenarioDyntexBaseKind(0x0dae, chicago) == SCENARIO_DYNTEX_RIVER);
    REQUIRE(scenarioDyntexBaseKind(0x06cb, deepsea) == SCENARIO_DYNTEX_RIVER);
    REQUIRE(scenarioDyntexBaseKind(0x06e2, deepsea) == SCENARIO_DYNTEX_TELEPORTAL);
    REQUIRE(scenarioDyntexBaseKind(0x029b, airforceone) == SCENARIO_DYNTEX_MONITOR);
    REQUIRE(scenarioDyntexBaseKind(0x090f, villa) == SCENARIO_DYNTEX_OCEAN);
    REQUIRE(scenarioDyntexBaseKind(0x0a42, attackship) == SCENARIO_DYNTEX_ARROWS);

    // Air Base animates nothing; Investigation and Villa keep their rivers still.
    REQUIRE(scenarioDyntexBaseKind(0x01c7, airbase) == SCENARIO_DYNTEX_NONE);
    REQUIRE(scenarioDyntexBaseKind(0x090f, airbase) == SCENARIO_DYNTEX_NONE);
    REQUIRE(scenarioDyntexBaseKind(0x01c7, investigation) == SCENARIO_DYNTEX_NONE);
    REQUIRE(scenarioDyntexBaseKind(0x0dae, villa) == SCENARIO_DYNTEX_NONE);
    REQUIRE(scenarioDyntexBaseKind(0x01c7, chicago) == SCENARIO_DYNTEX_RIVER);

    // The power node's conditional presets stay on Attack Ship only.
    REQUIRE(scenarioDyntexBaseKind(0x0a6a, attackship) == SCENARIO_DYNTEX_POWERJUICE);
    REQUIRE(scenarioDyntexBaseKind(0x0a69, attackship) == SCENARIO_DYNTEX_POWERRING);
    REQUIRE(scenarioDyntexBaseKind(0x0a6a, deepsea) == SCENARIO_DYNTEX_RIVER);
    REQUIRE(scenarioDyntexBaseKind(0x0a69, deepsea) == SCENARIO_DYNTEX_RIVER);

    REQUIRE(scenarioDyntexBaseKind(0x0001, chicago) == SCENARIO_DYNTEX_NONE);
    REQUIRE(scenarioDyntexBaseKind(0x06cc, deepsea) == SCENARIO_DYNTEX_NONE);
}

TEST_CASE("dyntex group shift matches dyntexTickRoom's first use", "[scenario][dyntex][B-1172]")
{
    int s = 99, t = 99;
    scenarioDyntexGroupShift(-100, 100, -100, 100, &s, &t);
    REQUIRE((s == 0 && t == 0));
    scenarioDyntexGroupShift(-0x5d01, 0, 0, 0, &s, &t);
    REQUIRE((s == 0x2000 && t == 0));
    scenarioDyntexGroupShift(0, 0, -0x5d01, 0, &s, &t);
    REQUIRE((s == 0 && t == 0x2000));
    scenarioDyntexGroupShift(0, 0x5d01, 0, 0x5d01, &s, &t);
    REQUIRE((s == -0x2000 && t == -0x2000));
    // Both ends past the limit: the later (upper) rule wins, as in dyntex.c.
    scenarioDyntexGroupShift(-0x6000, 0x6000, -0x6000, 0x6000, &s, &t);
    REQUIRE((s == -0x2000 && t == -0x2000));
    // Exactly at the limit is not past it.
    scenarioDyntexGroupShift(-0x5d00, 0x5d00, -0x5d00, 0x5d00, &s, &t);
    REQUIRE((s == 0 && t == 0));
}

TEST_CASE("dyntex presets move a vertex exactly as dyntex.c does", "[scenario][dyntex][B-1172]")
{
    scenario_dyntex_frame_t frame = {};
    frame.teleports_on = 1;
    for (float frac : kFracs) {
        frame.interval80 = frac;
        for (int16_t bs : kCoords) {
            for (int16_t bt : kCoords) {
                int16_t want_s, want_t, got_s, got_t;
                INFO("frac " << frac << " s " << bs << " t " << bt);

                oracleLinear(frac, bs, bt, &want_s, &want_t);
                REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_RIVER, &frame, bs, bt, &got_s, &got_t) == 1);
                REQUIRE((got_s == want_s && got_t == want_t));
                REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_TELEPORTAL, &frame, bs, bt, &got_s, &got_t) == 1);
                REQUIRE((got_s == want_s && got_t == want_t));
                REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_POWERRING, &frame, bs, bt, &got_s, &got_t) == 1);
                REQUIRE((got_s == want_s && got_t == want_t));
                REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_POWERJUICE, &frame, bs, bt, &got_s, &got_t) == 1);
                REQUIRE((got_s == want_s && got_t == want_t));

                oracleMonitor(frac, bs, bt, &want_s, &want_t);
                REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_MONITOR, &frame, bs, bt, &got_s, &got_t) == 1);
                REQUIRE((got_s == want_s && got_t == want_t));

                oracleOcean(frac, bs, bt, &want_s, &want_t);
                REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_OCEAN, &frame, bs, bt, &got_s, &got_t) == 1);
                REQUIRE((got_s == want_s && got_t == want_t));

                oracleArrows(frac, bs, bt, &want_s, &want_t);
                REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_ARROWS, &frame, bs, bt, &got_s, &got_t) == 1);
                REQUIRE((got_s == want_s && got_t == want_t));
            }
        }
    }
}

TEST_CASE("dyntex conditional presets hold or reset as dyntexTickRoom does", "[scenario][dyntex][B-1172]")
{
    scenario_dyntex_frame_t frame = {};
    int16_t s = 7, t = 7;
    frame.interval80 = 0.3f;

    // Deep Sea teleport: holds until the teleports are on.
    frame.teleports_on = 0;
    REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_TELEPORTAL, &frame, 100, 200, &s, &t) == 0);
    REQUIRE((s == 7 && t == 7));

    // Attack Ship engines destroyed: rings go to (0, 0), juice holds.
    frame.engines_destroyed = 1;
    REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_POWERRING, &frame, 100, 200, &s, &t) == 1);
    REQUIRE((s == 0 && t == 0));
    s = 7;
    t = 7;
    REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_POWERJUICE, &frame, 100, 200, &s, &t) == 0);
    REQUIRE((s == 7 && t == 7));

    // Not a preset, or no frame: nothing moves.
    REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_NONE, &frame, 1, 2, &s, &t) == 0);
    REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_KIND_COUNT, &frame, 1, 2, &s, &t) == 0);
    REQUIRE(scenarioDyntexAnimate(SCENARIO_DYNTEX_RIVER, nullptr, 1, 2, &s, &t) == 0);
}

TEST_CASE("dyntex UVs match the extractor's conversion", "[scenario][dyntex][B-1172]")
{
    const unsigned scales[] = {0xffffu, 0x8000u, 0x0800u, 0u};
    const float uv_scales[] = {1.0f / 32.0f, 1.0f / 64.0f, 0.5f / 128.0f, 2.0f / 16.0f};
    for (unsigned scale : scales) {
        for (float us : uv_scales) {
            for (int16_t s : kCoords) {
                for (int16_t t : kCoords) {
                    // romextract_pdarena.c: G_VTX scale, then s_bgSceneAddTri, then s_bgMaterialGlbUv.
                    int32_t ss = ((int32_t)s * (int32_t)scale) >> 16;
                    int32_t st = ((int32_t)t * (int32_t)scale) >> 16;
                    float raw_u = (float)ss / 32.0f;
                    float raw_v = 1.0f - ((float)st / 32.0f);
                    float want_u = raw_u * us;
                    float want_v = 1.0f - ((1.0f - raw_v) * us);
                    float u = -1, v = -1;
                    scenarioDyntexUv(s, t, scale, scale, us, us, &u, &v);
                    INFO("scale " << scale << " s " << s << " t " << t);
                    REQUIRE(u == want_u);
                    REQUIRE(v == want_v);
                }
            }
        }
    }
}

TEST_CASE("dyntex mirrors the legacy sources it was transcribed from", "[scenario][dyntex][B-1172][source]")
{
    const std::string dyntex = readTextFile("src/game/dyntex.c");
    const std::string tex = readTextFile("src/game/tex.c");
    const std::string arena = readTextFile("port/src/romextract_pdarena.c");
    REQUIRE_FALSE(dyntex.empty());
    REQUIRE_FALSE(tex.empty());
    REQUIRE_FALSE(arena.empty());

    REQUIRE(dyntex.find("s16 tmp = (s32) (g_Lv80SecIntervalFrac * 10.0f * 4096.0f) % 4096;") != std::string::npos);
    REQUIRE(dyntex.find("s16 tmp = (s32) (g_Lv80SecIntervalFrac * 4.0f * 4096.0f) % 4096;") != std::string::npos);
    REQUIRE(dyntex.find("f32 f24 = g_Lv80SecIntervalFrac * 5.0f;") != std::string::npos);
    REQUIRE(dyntex.find("static u32 ripsize = 65;") != std::string::npos);
    REQUIRE(dyntex.find("static u32 modula = 22;") != std::string::npos);
    REQUIRE(dyntex.find("s32 tmp = ((s32) ((1.0f - g_Lv80SecIntervalFrac) * 60.0f * 8.0f) % 8) * 256;") != std::string::npos);
    REQUIRE(dyntex.find("if (mins < -0x5d00) {") != std::string::npos);
    REQUIRE(dyntex.find("if (g_StageIndex == STAGEINDEX_AIRBASE) {") != std::string::npos);
    REQUIRE(dyntex.find("if (g_StageIndex == STAGEINDEX_VILLA && type == DYNTEXTYPE_RIVER) {") != std::string::npos);
    REQUIRE(dyntex.find("if (g_StageIndex == STAGEINDEX_INVESTIGATION && type == DYNTEXTYPE_RIVER) {") != std::string::npos);
    REQUIRE(dyntex.find("if (chrHasStageFlag(0, 0x00000100) && !chrHasStageFlag(0, 0x00010000)) {") != std::string::npos);

    const char *textures[] = {"TEXTURE_06CB", "TEXTURE_0A6A", "TEXTURE_0A69", "TEXTURE_06E2",
        "TEXTURE_01C7", "TEXTURE_0DAE", "TEXTURE_029B", "TEXTURE_090F", "TEXTURE_0A42"};
    for (const char *name : textures) {
        INFO(name);
        REQUIRE(tex.find(std::string("texturenum == ") + name) != std::string::npos);
    }

    REQUIRE(arena.find("((s32)src[v].s * (s32)texture_scale_s) >> 16;") != std::string::npos);
    REQUIRE(arena.find("f32 u0 = (f32)s0 / 32.0f;") != std::string::npos);
    REQUIRE(arena.find("f32 v0 = 1.0f - ((f32)t0 / 32.0f);") != std::string::npos);
    REQUIRE(arena.find("if (out_v) *out_v = 1.0f - ((1.0f - raw_v) * v_scale);") != std::string::npos);
}

TEST_CASE("dyntex is wired from extraction through the renderer to the game", "[scenario][dyntex][B-1172][source]")
{
    const std::string arena = readTextFile("port/src/romextract_pdarena.c");
    const std::string renderer = readTextFile("port/fast3d/scenario_scene_renderer.cpp");
    const std::string bg = readTextFile("src/game/bg.c");
    REQUIRE_FALSE(arena.empty());
    REQUIRE_FALSE(renderer.empty());
    REQUIRE_FALSE(bg.empty());

    // The extractor picks each material's preset from the stage and writes
    // it, the UV scale and the per-vertex _PD_DYNTEX accessor.
    REQUIRE(arena.find("scenarioDyntexBaseKind(texnum, scene->stagenum)") != std::string::npos);
    REQUIRE(arena.find("scene->stagenum = stagenum;") != std::string::npos);
    REQUIRE(arena.find("\\\"_PD_DYNTEX\\\":%u") != std::string::npos);
    REQUIRE(arena.find("\\\"animation\\\":\\\"%s\\\",\\\"uv_scale\\\":[%.9g,%.9g]") != std::string::npos);
    // The binary, bufferView and accessor passes agree on which materials move.
    size_t passes = 0;
    for (size_t at = arena.find("if (s_bgMaterialAnimated(scene, m)) {"); at != std::string::npos;
            at = arena.find("if (s_bgMaterialAnimated(scene, m)) {", at + 1)) {
        passes++;
    }
    REQUIRE(passes == 3);

    // The renderer reads them and moves the runtime UVs each frame.
    REQUIRE(renderer.find("findAttr(\"_PD_DYNTEX\")") != std::string::npos);
    REQUIRE(renderer.find("scenarioDyntexKindFromName(") != std::string::npos);
    REQUIRE(renderer.find("prepareDyntex(next);") != std::string::npos);
    REQUIRE(renderer.find("scenarioDyntexAnimate(d.kind, &scene.dyntex_frame,") != std::string::npos);
    REQUIRE(renderer.find("vertex.u1 = u;") != std::string::npos);

    // The game passes the interval and the stage flags every frame.
    REQUIRE(bg.find("scenarioSceneRendererSetDyntexFrame(g_Lv80SecIntervalFrac,") != std::string::npos);
}
