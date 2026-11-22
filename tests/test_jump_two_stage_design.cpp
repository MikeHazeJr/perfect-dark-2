/*
 * test_jump_two_stage_design.cpp -- c038 jump/capsule regression guards.
 *
 * This is intentionally source-level coverage for the live movement code.
 * capsule.c is client-only and depends on the full room/model/prop runtime,
 * so pd-tests pins the implementation shape and tests the exported normal
 * classification helpers directly.
 */

#include "catch.hpp"
#include "lib/capsule.h"

#include <fstream>
#include <cmath>
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

void requireContains(const std::string &haystack, const char *needle)
{
    INFO("missing: " << needle);
    REQUIRE(haystack.find(needle) != std::string::npos);
}

void requireNotContains(const std::string &haystack, const char *needle)
{
    INFO("unexpected: " << needle);
    REQUIRE(haystack.find(needle) == std::string::npos);
}

struct coord sub(const struct coord &a, const struct coord &b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

struct coord cross(const struct coord &a, const struct coord &b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float dot(const struct coord &a, const struct coord &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct coord normalized(const struct coord &v)
{
    const float len2 = dot(v, v);
    if (len2 <= 0.0001f) {
        return {0.0f, 1.0f, 0.0f};
    }
    const float inv = 1.0f / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

bool segmentTriangleHit(const struct coord &from, const struct coord &to,
        const struct coord triangle[3], struct coord *outNormal)
{
    const struct coord dir = sub(to, from);
    const struct coord edge1 = sub(triangle[1], triangle[0]);
    const struct coord edge2 = sub(triangle[2], triangle[0]);
    const struct coord pvec = cross(dir, edge2);
    const float det = dot(edge1, pvec);

    if (std::fabs(det) < 0.0001f) {
        return false;
    }

    const float invDet = 1.0f / det;
    const struct coord tvec = sub(from, triangle[0]);
    const float u = dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const struct coord qvec = cross(tvec, edge1);
    const float v = dot(dir, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    const float t = dot(edge2, qvec) * invDet;
    if (t < 0.0f || t > 1.0f) {
        return false;
    }

    if (outNormal) {
        *outNormal = normalized(cross(edge1, edge2));
    }
    return true;
}

bool renderedTriangleWouldBlockCapsuleSampleBundle(const struct coord &start,
        const struct coord &move, float radius, float ymin, float ymax,
        const struct coord triangle[3], int expectedType)
{
    const float mid = (ymin + ymax) * 0.5f;
    const float ys[3] = {ymax, mid, ymin};
    const float skin = radius * 0.85f;
    const float skins[5][2] = {
        {0.0f, 0.0f},
        {skin, 0.0f},
        {-skin, 0.0f},
        {0.0f, skin},
        {0.0f, -skin},
    };

    for (const float y : ys) {
        for (const auto &sample : skins) {
            struct coord from = {
                start.x + sample[0],
                start.y + y,
                start.z + sample[1],
            };
            struct coord to = {
                from.x + move.x,
                from.y + move.y,
                from.z + move.z,
            };
            struct coord normal;
            if (!segmentTriangleHit(from, to, triangle, &normal)) {
                continue;
            }
            capsuleOrientNormalAgainstMove(&normal, &move);
            if (capsuleClassifyNormal(&normal) == expectedType) {
                return true;
            }
        }
    }

    return false;
}

} // namespace

TEST_CASE("jump capsule implementation is multi-sample and generic",
          "[physics][jump][capsule][static]")
{
    const std::string header = readTextFile("src/include/lib/capsule.h");
    const std::string capsule = readTextFile("src/lib/capsule.c");
    const std::string bondwalk = readTextFile("src/game/bondwalk.c");
    const std::string chr = readTextFile("src/game/chr.c");
    const std::string propobj = readTextFile("src/game/propobj.c");
    const std::string meshcollision = readTextFile("src/lib/meshcollision.c");
    const std::string lv = readTextFile("src/game/lv.c");
    const std::string varsreset = readTextFile("src/game/varsreset.c");
    const std::string libmain = readTextFile("src/lib/main.c");
    const std::string pdmain = readTextFile("port/src/pdmain.c");
    const std::string forgecore = readTextFile("port/src/forge/forge_core.c");
    const std::string forgeruntime = readTextFile("port/src/forge/forge_runtime.c");

    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(capsule.empty());
    REQUIRE_FALSE(bondwalk.empty());
    REQUIRE_FALSE(chr.empty());
    REQUIRE_FALSE(propobj.empty());
    REQUIRE_FALSE(meshcollision.empty());
    REQUIRE_FALSE(lv.empty());
    REQUIRE_FALSE(varsreset.empty());
    REQUIRE_FALSE(libmain.empty());
    REQUIRE_FALSE(pdmain.empty());
    REQUIRE_FALSE(forgecore.empty());
    REQUIRE_FALSE(forgeruntime.empty());

    requireContains(header, "struct prop *selfprop");
    requireContains(header, "capsuleFindFloorForProp");
    requireContains(header, "capsuleFindCeilingForProp");
    requireContains(header, "capsuleFindRenderedFloor");
    requireContains(header, "capsuleClassifyNormal");

    requireContains(capsule, "capsuleMeshSweepSamples");
    requireContains(capsule, "CAPSULE_RENDERED_SAMPLE_YS");
    requireContains(capsule, "CAPSULE_RENDERED_SAMPLE_SKINS");
    requireContains(capsule, "meshRayCastWorld");
    requireContains(capsule, "meshRayCastWorldMasked(from, to, 0, out_normal, NULL)");
    requireContains(capsule, "meshRayCastDynamicProps");
    requireContains(capsule, "capsuleFindRoomsForPos");
    requireContains(capsule, "stage2 vertical probe");
    requireContains(capsule, "g_JumpLoggingEnabled");
    requireContains(capsule, "stage2frac < safefrac");
    requireContains(capsule, "struct prop *selfprop = cast->selfprop");
    requireNotContains(capsule, "capsuleRenderedPropRayCast");
    requireNotContains(capsule, "func0f0849dc");
    requireNotContains(capsule, "model->matrices");

    requireContains(meshcollision, "#include \"model_rodata_guard.h\"");
    requireContains(meshcollision, "modelRodataIsReadable(node->rodata, sizeof(struct modelrodata_dl))");
    requireContains(meshcollision, "modelRodataIsReadable(node->rodata, sizeof(struct modelrodata_gundl))");
    requireContains(meshcollision, "modelRodataIsReadable(&gdl[cmdidx], sizeof(Gfx))");
    requireContains(meshcollision, "modelRodataIsReadable(vbuf, vbytes)");
    requireContains(meshcollision, "meshFree(mesh);");
    requireContains(meshcollision, "free(mesh);");
    requireContains(meshcollision, "void meshDetachAllStageProps(void)");
    requireContains(meshcollision, "prop = g_Vars.activeprops");
    requireContains(meshcollision, "visited < g_Vars.maxprops");
    requireNotContains(meshcollision, "g_Vars.props[i].colmesh");
    requireContains(varsreset, "g_Vars.props[i].colmesh = NULL;");
    requireContains(varsreset, "g_Vars.props[g_Vars.maxprops - 1].next = NULL;");
    requireContains(varsreset, "g_Vars.props[g_Vars.maxprops - 1].colmesh = NULL;");
    requireContains(libmain, "meshDetachAllStageProps();");
    requireContains(libmain, "mempResetPool(MEMPOOL_STAGE);");
    REQUIRE(libmain.find("meshDetachAllStageProps();") < libmain.find("mempResetPool(MEMPOOL_STAGE);"));
    requireContains(pdmain, "meshDetachAllStageProps();");
    requireContains(pdmain, "mempResetPool(MEMPOOL_STAGE);");
    REQUIRE(pdmain.find("meshDetachAllStageProps();") < pdmain.find("mempResetPool(MEMPOOL_STAGE);"));
    requireContains(lv, "scenarioResetForStageLoad(stagenum);");
    requireNotContains(lv, "meshDetachFromProp(&g_Vars.props[i])");
    requireContains(bondwalk, "sweep.selfprop = g_Vars.currentplayer->prop");
    requireContains(bondwalk, "bwalkClampAirborneSideEntry");
    requireContains(bondwalk, "bwalkClampAirborneLateralEntry");
    requireContains(bondwalk, "bwalkPlayerNeedsAirborneLateralClamp");
    requireContains(bondwalk, "player->bondprevpos");
    requireContains(bondwalk, "sweep.move.x = lateralMove.x");
    requireContains(bondwalk, "sweep.move.y = 0.0f");
    requireContains(bondwalk, "sweep.move.y = *verticalDelta");
    requireContains(bondwalk, "geomtype == CAPSULE_HIT_FLOOR");
    requireContains(bondwalk, "JUMP_LATERAL_SWEEP");
    requireContains(bondwalk, "JUMP_SIDE_SWEEP");
    requireContains(bondwalk, "capsuleFindSupportForProp(g_Vars.currentplayer->prop");
    requireContains(bondwalk, "capsuleFindCeilingForProp(g_Vars.currentplayer->prop");
    requireContains(chr, "sweep.selfprop = chr->prop");
    requireContains(chr, "capsuleFindRenderedFloor(prop");
    requireContains(chr, "capsuleFindSupportForProp(chr->prop");
    requireContains(propobj, "objModelMatrixIndexIsValid");
    requireContains(propobj, "mtxindex < model->definition->nummatrices");
    requireContains(propobj, "continue;");
    requireContains(propobj, "meshAttachModelToProp(prop, obj->model)");
    requireContains(meshcollision, "meshWorldAddRenderedRoom");
    requireContains(meshcollision, "bgLoadRoom(roomnum)");
    requireContains(meshcollision, "if (!room->gfxdata)");
    requireContains(meshcollision, "meshPropIsMovementSolid");
    requireContains(meshcollision, "meshBuildPropTransform");
    requireContains(meshcollision, "prop->pos");
    requireContains(meshcollision, "realrot");
    requireContains(meshcollision, "OBJFLAG3_WALKTHROUGH");
    requireContains(forgecore, "FORGE_CAT_PICKUP");
    requireContains(forgecore, "FORGE_COLLISION_PASSTHROUGH");
    requireContains(forgeruntime, "o->collision_mode != FORGE_COLLISION_SOLID");
    requireContains(forgeruntime, "OBJFLAG3_WALKTHROUGH");
    requireContains(forgeruntime, "meshAttachModelToProp");

    requireNotContains(capsule, "single ray is sufficient");
    requireNotContains(capsule, "g_Vars.currentplayer->prop, false");
    requireNotContains(capsule, "g_Vars.currentplayer->prop, true");
}

TEST_CASE("jump capsule normal classification is orientation-aware",
          "[physics][jump][capsule][normal]")
{
    struct coord floor = {0.0f, 0.9f, 0.1f};
    struct coord ceiling = {0.0f, -0.8f, 0.2f};
    struct coord wall = {0.9f, 0.1f, 0.0f};

    REQUIRE(capsuleClassifyNormal(&floor) == CAPSULE_HIT_FLOOR);
    REQUIRE(capsuleClassifyNormal(&ceiling) == CAPSULE_HIT_CEILING);
    REQUIRE(capsuleClassifyNormal(&wall) == CAPSULE_HIT_WALL);

    struct coord move_up = {0.0f, 10.0f, 0.0f};
    struct coord wrong_way_ceiling = {0.0f, 1.0f, 0.0f};
    capsuleOrientNormalAgainstMove(&wrong_way_ceiling, &move_up);
    REQUIRE(wrong_way_ceiling.y == Approx(-1.0f));
    REQUIRE(capsuleClassifyNormal(&wrong_way_ceiling) == CAPSULE_HIT_CEILING);
}

TEST_CASE("jump capsule rendered-triangle fixture catches unflagged floor, ceiling, and side walls",
          "[physics][jump][capsule][fixture]")
{
    const struct coord start = {0.0f, 70.0f, 0.0f};
    const float radius = 20.0f;
    const float ymin = -40.0f;
    const float ymax = 40.0f;

    const struct coord floorTri[3] = {
        {-50.0f, 10.0f, -50.0f},
        {0.0f, 10.0f, 50.0f},
        {50.0f, 10.0f, -50.0f},
    };
    const struct coord moveDown = {0.0f, -40.0f, 0.0f};
    REQUIRE(renderedTriangleWouldBlockCapsuleSampleBundle(start, moveDown,
        radius, ymin, ymax, floorTri, CAPSULE_HIT_FLOOR));

    const struct coord ceilingTri[3] = {
        {-50.0f, 130.0f, -50.0f},
        {0.0f, 130.0f, 50.0f},
        {50.0f, 130.0f, -50.0f},
    };
    const struct coord moveUp = {0.0f, 40.0f, 0.0f};
    REQUIRE(renderedTriangleWouldBlockCapsuleSampleBundle(start, moveUp,
        radius, ymin, ymax, ceilingTri, CAPSULE_HIT_CEILING));

    const struct coord sideWallTri[3] = {
        {25.0f, 0.0f, -60.0f},
        {25.0f, 150.0f, 0.0f},
        {25.0f, 0.0f, 60.0f},
    };
    const struct coord moveSideOnly = {40.0f, 0.0f, 0.0f};
    REQUIRE(renderedTriangleWouldBlockCapsuleSampleBundle(start, moveSideOnly,
        radius, ymin, ymax, sideWallTri, CAPSULE_HIT_WALL));

    const struct coord moveSideUp = {40.0f, 40.0f, 0.0f};
    REQUIRE(renderedTriangleWouldBlockCapsuleSampleBundle(start, moveSideUp,
        radius, ymin, ymax, sideWallTri, CAPSULE_HIT_WALL));
}

TEST_CASE("simulant jumps require a swept arc to safe support",
          "[physics][jump][bot][static]")
{
    const std::string bot = readTextFile("src/game/bot.c");
    const std::string chr = readTextFile("src/game/chr.c");

    REQUIRE_FALSE(bot.empty());
    REQUIRE_FALSE(chr.empty());

    requireContains(bot, "MPOPTION_BOTJUMP");
    requireContains(bot, "const f32 clear_fraction = capsuleSweep(&low);");
    requireContains(bot, "moved_ticks <= clear_ticks");
    requireContains(bot, "botJumpProbeFlight");
    requireContains(bot, "func0f0965e4(&step_y, &impulse,");
    requireContains(bot, "capsuleFindSupportForProp(body->selfprop");
    requireContains(bot, "landing = floor > -30000.0f");
    requireContains(bot, "support.flags & GEOFLAG_DIE");
    requireContains(bot, "capsuleSweep(&arc) < 1.0f");
    requireContains(chr, "chrStepSimulantAirborne");
    requireContains(chr, "chr->manground += delta * safe;");
    requireContains(chr, "return reaches_support && safe >= 1.0f;");
}

TEST_CASE("simulant jump cooldown follows the actor through landing and resets on placement",
          "[physics][jump][bot][static]")
{
    const std::string bot = readTextFile("src/game/bot.c");
    const std::string botmgr = readTextFile("src/game/botmgr.c");
    const std::string chr = readTextFile("src/game/chr.c");

    REQUIRE_FALSE(bot.empty());
    REQUIRE_FALSE(botmgr.empty());
    REQUIRE_FALSE(chr.empty());

    requireNotContains(bot, "botJumpChrIndex");
    requireNotContains(bot, "s_BotJumpCooldownFrame");
    requireContains(bot, "aibot->jumpcooldownuntil60 = BOT_JUMP_AWAITING_LANDING;");
    requireContains(chr, "botJumpOnLand(chr);");
    requireContains(bot, "aibot->jumpcooldownuntil60 = g_Vars.lvframe60 + BOT_JUMP_COOLDOWN_TICKS60;");
    requireContains(bot, "aibot->jumplastevalms = 0;");
    requireContains(bot, "aibot->jumpcooldownuntil60 = 0;");
    requireContains(botmgr, "aibot->jumplastevalms = 0;");
    requireContains(botmgr, "aibot->jumpcooldownuntil60 = 0;");
}

TEST_CASE("walking simulants keep the original ground search; only a jump's flight takes support under the feet (B-1302)",
          "[physics][jump][bot][static]")
{
    const std::string bot = readTextFile("src/game/bot.c");
    const std::string botHeader = readTextFile("src/include/game/bot.h");
    const std::string chr = readTextFile("src/game/chr.c");

    REQUIRE_FALSE(bot.empty());
    REQUIRE_FALSE(botHeader.empty());
    REQUIRE_FALSE(chr.empty());

    // The flight is the jump's own, from its impulse to its landing.
    requireContains(botHeader, "bool botJumpInFlight(const struct chrdata *chr);");
    requireContains(bot, "return chr && chr->aibot && chr->aibot->jumpcooldownuntil60 == BOT_JUMP_AWAITING_LANDING;");
    requireContains(chr, "return chrUsesSimulantSupport(chr) && botJumpInFlight(chr)");

    // The ground: a flight's support under the feet, else the original walking
    // search (from 69 over the feet), which the support may only raise.
    const size_t fn = chr.find("static f32 chrFindMovementGround(");
    REQUIRE(fn != std::string::npos);
    const size_t fnEnd = chr.find("\treturn ground;\n}", fn);
    REQUIRE(fnEnd != std::string::npos);
    const std::string body = chr.substr(fn, fnEnd - fn);
    const size_t flight = body.find("if (chrSimulantInFlight(chr)) {");
    const size_t underFeet = body.find("chr->radius, rooms, chr->manground + 0.5f);");
    const size_t walking = body.find("f32 ground = cdFindGroundInfoAtCyl(pos, chr->radius, rooms,");
    const size_t tile = body.find("const f32 tileground = ground;");
    const size_t raise = body.find(
        "if (chrUsesSimulantSupport(chr) && (chr->manground > tileground + 2.0f || chr->floorroom < 0)) {");
    // The support may name the surface the rendered floor found (its room, flags
    // and lift) or raise the ground, never lower it.
    const size_t sameOrHigher = body.find("support.y >= ground - 1.0f");
    const size_t overTile = body.find("&& (support.y > tileground + 1.0f || chr->floorroom < 0)) {");
    const size_t onlyRaises = body.find("if (support.y > ground) {\n\t\t\t\tground = support.y;");
    const size_t liftNamed = body.find("*lift = support.lift;", raise);
    REQUIRE(flight != std::string::npos);
    REQUIRE(underFeet != std::string::npos);
    REQUIRE(walking != std::string::npos);
    REQUIRE(tile != std::string::npos);
    REQUIRE(raise != std::string::npos);
    REQUIRE(sameOrHigher != std::string::npos);
    REQUIRE(overTile != std::string::npos);
    REQUIRE(onlyRaises != std::string::npos);
    REQUIRE(liftNamed != std::string::npos);
    REQUIRE(flight < underFeet);
    REQUIRE(underFeet < walking);
    REQUIRE(walking < tile);
    REQUIRE(tile < raise);
    REQUIRE(raise < sameOrHigher);
    REQUIRE(sameOrHigher < overTile);
    REQUIRE(overTile < onlyRaises);
    REQUIRE(onlyRaises < liftNamed);

    // A walking simulant's search from 20 over its feet lost stairs and ramps:
    // climbing, manground trails the floor by up to 30. No path picks the support
    // on the jumping option alone any more.
    requireNotContains(chr, "airborne ? 0.5f : 20.0f");
    requireNotContains(chr, "if (chrUsesSimulantSupport(chr)) {");

    // A flight, and a simulant's fall onto a ground under its feet, step with
    // chrStepSimulantAirborne, which clamps the last step of a long fall to the
    // ground (chr0f01f264 refuses a step the sweep finds blocked, and the
    // simulant would hang over the floor). A ground at or over the feet keeps
    // chr0f01f264's landing.
    const size_t step = chr.find("landed = chrStepSimulantAirborne(chr, arg2, spfc, yincrement, sp68);");
    REQUIRE(step != std::string::npos);
    const size_t stepGate = chr.rfind("if (chrSimulantInFlight(chr)", step);
    const size_t fallGate = chr.rfind(
        "|| (chrUsesSimulantSupport(chr) && chr->ground < chr->manground)) {", step);
    REQUIRE(stepGate != std::string::npos);
    REQUIRE(fallGate != std::string::npos);
    REQUIRE(stepGate < fallGate);
    REQUIRE(step - stepGate < 160);
    const size_t originalFall = chr.find("if (chr0f01f264(chr, arg2, spfc, yincrement, false))", step);
    REQUIRE(originalFall != std::string::npos);
    REQUIRE(originalFall - step < 200);

    // On the ground a flight is over even when no landing ended it.
    const size_t climb = chr.find("} else if (chr->manground <= chr->ground) {");
    REQUIRE(climb != std::string::npos);
    const size_t climbLand = chr.find("botJumpOnLand(chr);", climb);
    const size_t climbLoop = chr.find("for (i = 0; i < g_Vars.lvupdate60; i++) {", climb);
    REQUIRE(climbLand != std::string::npos);
    REQUIRE(climbLoop != std::string::npos);
    REQUIRE(climbLand < climbLoop);
}

TEST_CASE("the ledge and tabletop smokes drive the movement they test (B-1310)",
          "[physics][jump][smokes][static]")
{
    const std::string harness = readTextFile("port/src/smoke_harness.c");
    const std::string header = readTextFile("port/include/smoke_harness.h");
    const std::string bot = readTextFile("src/game/bot.c");

    REQUIRE_FALSE(harness.empty());
    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(bot.empty());

    // Player movement reads the analog axis, not the digital forward action, so
    // the player fixtures press the axis; the walk-off turns the player back.
    for (const char *name : {"complex_ledge_player_smoke", "villa_tabletop_player_smoke"}) {
        INFO(name);
        const std::string fixture = readTextFile((std::string("tools/smoke-verify/tests/") + name + ".json").c_str());
        REQUIRE_FALSE(fixture.empty());
        requireContains(fixture, "\"name\": \"ACTION_AXIS_MOVE_Y\", \"action\": \"press\"");
        requireNotContains(fixture, "ACTION_MOVE_FORWARD");
        requireNotContains(fixture, "ACTION_MOVE_BACKWARD");
        requireContains(fixture, "phase=turn actor=player theta=180");
    }
    requireContains(harness, "player->vv_theta = 180.0f;");
    requireContains(harness, "\"%s: phase=turn actor=player theta=%.1f\"");

    // The tabletop is measured from the live table, and its fixtures expect it.
    requireContains(harness, ".measure_table = 1,");
    requireContains(harness, "static s32 smokeLedgeMeasureTable(SmokeLedge *ledge, const SmokeSupportFixture *authored)");
    requireContains(harness, "\"%s: phase=table_measured pos=");
    requireContains(harness, "if (fixture->measure_table) {");
    for (const char *name : {"villa_tabletop_player_smoke", "villa_tabletop_simulant_smoke"}) {
        INFO(name);
        const std::string fixture = readTextFile((std::string("tools/smoke-verify/tests/") + name + ".json").c_str());
        REQUIRE_FALSE(fixture.empty());
        requireContains(fixture, "SMOKE\\\\.TABLETOP: phase=table_measured pos=");
        requireNotContains(fixture, "feet=(?:-24[0-2]|-23[89])");
    }

    // A held simulant takes no items while the smoke holds it.
    requireContains(header, "int  smokeHarnessBotPickupsHeld(const struct chrdata *chr);");
    requireContains(harness, "int smokeHarnessBotPickupsHeld(const struct chrdata *chr)");
    const size_t pickup = bot.find("struct prop *botFindPickup(struct chrdata *chr, s32 criteria)");
    REQUIRE(pickup != std::string::npos);
    const size_t held = bot.find("if (smokeHarnessBotPickupsHeld(chr)) {\n\t\treturn NULL;", pickup);
    REQUIRE(held != std::string::npos);
    REQUIRE(held < bot.find("if (&aibot);", pickup));

    // After takeoff the arc is traced, and the failure line names its peak.
    requireContains(harness, "#define SMOKE_LEDGE_TRACE_FRAMES 150");
    requireContains(harness, "\"%s: sample n=%d frame=%d feet=%.2f");
    requireContains(harness, "lower_landed=%d peak_feet=%.2f\"");
}

TEST_CASE("the ledge and tabletop smokes judge what the actor did (B-1310, candidate 4)",
          "[physics][jump][smokes][static][b1310]")
{
    const std::string harness = readTextFile("port/src/smoke_harness.c");
    const std::string bot = readTextFile("src/game/bot.c");
    REQUIRE_FALSE(harness.empty());
    REQUIRE_FALSE(bot.empty());

    // R16: the Complex simulant landed on the ledge and stood 39.4 off the
    // lane's line, inside its hold's post (MA_AIBOTDEFEND sends it back only
    // past 40 units on an axis). A simulant's lane allows that post.
    requireContains(bot, "if (xdist > 40 || zdist > 40 || (ydist > 200 && !chr->inlift)) {");
    requireContains(harness, "#define SMOKE_LEDGE_BOT_POST_HALF_WIDTH 44.0f");
    requireContains(harness, "const f32 lane = ledge->bot_slot >= 0 && fixture->lane_half_width < SMOKE_LEDGE_BOT_POST_HALF_WIDTH\n"
        "        ? SMOKE_LEDGE_BOT_POST_HALF_WIDTH : fixture->lane_half_width;");
    requireContains(harness, "const s32 in_lane = fabsf(pos->x - fixture->x) < lane;");
    requireNotContains(harness, "const s32 in_lane = fabsf(pos->x - fixture->x) < fixture->lane_half_width;");

    // The peak starts at takeoff: from 0 it hid every Villa arc.
    const size_t takeoff = harness.find("        ledge->took_off = 1;\n");
    REQUIRE(takeoff != std::string::npos);
    const size_t peak = harness.find("        ledge->peak_feet = feet;\n", takeoff);
    REQUIRE(peak != std::string::npos);
    REQUIRE(peak < harness.find("\"%s: phase=takeoff chrnum=%d", takeoff));

    // The player's move axis is released by the stop its coast predicts, for
    // the jump and for the walk-off; the timed release ran on to three times
    // the distance. Predicted and actual stops are logged.
    requireContains(harness, "#define SMOKE_LEDGE_PLAYER_COAST_FRAMES 13.3f");
    const size_t release = harness.find("static void smokeLedgeReleaseMoveAtGoal(SmokeLedge *ledge, f32 z, f32 speed_z)\n{");
    REQUIRE(release != std::string::npos);
    const size_t release_end = harness.find("\n}\n", release);
    REQUIRE(release_end != std::string::npos);
    const std::string body = harness.substr(release, release_end - release);
    requireContains(body, "const f32 predicted_z = z + speed_z * SMOKE_LEDGE_PLAYER_COAST_FRAMES;");
    requireContains(body, "predicted_z >= fixture->upper_goal_z");
    requireContains(body, "predicted_z <= fixture->lower_goal_z");
    requireContains(body, "stage = \"approach\";");
    requireContains(body, "stage = \"walk_off\";");
    requireContains(body, "actionmapInjectStateForSmoke(0, ACTION_AXIS_MOVE_Y, 0);");
    requireContains(body, "\"%s: phase=release stage=%s z=%.1f speed_z=%.2f predicted_z=%.1f goal_z=%.1f\"");
    requireContains(body, "\"%s: phase=coast stage=approach predicted_z=%.1f actual_z=%.1f error=%.1f\"");
    requireContains(body, "\"%s: phase=coast stage=walk_off predicted_z=%.1f actual_z=%.1f error=%.1f\"");
    requireContains(harness, "    if (ledge->bot_slot < 0) {\n        smokeLedgeReleaseMoveAtGoal(ledge, pos->z, speed_z);\n    }");
    requireContains(harness, "        ledge->last_z = chr->prop->pos.z;");
    for (const char *name : {"complex_ledge_player_smoke", "villa_tabletop_player_smoke"}) {
        INFO(name);
        const std::string fixture = readTextFile((std::string("tools/smoke-verify/tests/") + name + ".json").c_str());
        REQUIRE_FALSE(fixture.empty());
        requireContains(fixture, ": phase=release stage=approach \"");
        requireContains(fixture, ": phase=release stage=walk_off \"");
    }

    // Before takeoff the actor is sampled too, with a simulant's route, so a
    // missing takeoff shows where it went (R16's Villa simulant left nothing).
    requireContains(harness, "#define SMOKE_LEDGE_APPROACH_EVERY   10");
    requireContains(harness, "\"%s: approach n=%d frame=%d feet=%.2f ground=%.2f velocity=%.2f x=%.1f z=%.1f speed_z=%.2f room=%d action=%d myaction=%d routed=%d");
    requireContains(harness, "const s32 routed = chr->actiontype == ACT_GOPOS;");
}
