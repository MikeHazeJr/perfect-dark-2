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

    REQUIRE_FALSE(header.empty());
    REQUIRE_FALSE(capsule.empty());
    REQUIRE_FALSE(bondwalk.empty());
    REQUIRE_FALSE(chr.empty());

    requireContains(header, "struct prop *selfprop");
    requireContains(header, "capsuleFindFloorForProp");
    requireContains(header, "capsuleFindCeilingForProp");
    requireContains(header, "capsuleFindRenderedFloor");
    requireContains(header, "capsuleClassifyNormal");

    requireContains(capsule, "capsuleRenderedSweepSamples");
    requireContains(capsule, "CAPSULE_RENDERED_SAMPLE_YS");
    requireContains(capsule, "CAPSULE_RENDERED_SAMPLE_SKINS");
    requireContains(capsule, "capsuleRenderedPropRayCast");
    requireContains(capsule, "func0f0849dc");
    requireContains(capsule, "capsuleFindRoomsForPos");
    requireContains(capsule, "stage2 vertical probe");
    requireContains(capsule, "stage2frac < safefrac");
    requireContains(capsule, "struct prop *selfprop = cast->selfprop");

    requireContains(bondwalk, "sweep.selfprop = g_Vars.currentplayer->prop");
    requireContains(bondwalk, "capsuleFindFloorForProp(g_Vars.currentplayer->prop");
    requireContains(bondwalk, "capsuleFindCeilingForProp(g_Vars.currentplayer->prop");
    requireContains(chr, "sweep.selfprop = chr->prop");
    requireContains(chr, "capsuleFindRenderedFloor(prop");

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

TEST_CASE("jump capsule rendered-triangle fixture catches unflagged floor and ceiling",
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
}

TEST_CASE("bot jump obstacle trigger is solver-gated and option-gated",
          "[physics][jump][bot][static]")
{
    const std::string bot = readTextFile("src/game/bot.c");

    REQUIRE_FALSE(bot.empty());

    requireContains(bot, "MPOPTION_BOTJUMP");
    requireContains(bot, "botJumpProbeObstacle");
    requireContains(bot, "capsuleSweep(&low) >= 1.0f");
    requireContains(bot, "capsuleSweep(&raised) < 1.0f");
    requireContains(bot, "capsuleFindFloorForProp(chr->prop");
    requireContains(bot, "floor < -100000.0f");
    requireContains(bot, "return false");
    requireContains(bot, "*out_reason = 1");
}
