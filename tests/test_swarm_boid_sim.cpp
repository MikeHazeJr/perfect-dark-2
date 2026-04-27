/*
 * test_swarm_boid_sim.cpp -- pure-CPU mock of the swarm seek-player
 * step (S483).
 *
 * The "real" sim runs in two places: the GLSL compute shader in
 * port/fast3d/swarm_gpu.cpp (#version 430 core) and the C function
 * cpu_seek_tick in port/src/swarm_test.c. Both implement the same
 * algorithm so the GPU vs CPU benchmark is apples-to-apples.
 *
 * This file mirrors that algorithm in plain C++ as a host-only mock
 * the test runner can exercise without GL or SDL. If the real
 * implementation drifts (e.g. when separation/alignment/cohesion get
 * turned on), update both sites and the mock here in lock-step.
 *
 * Per design doc gpu-swarm-and-test-scenarios-2026-04-27.md Section E.
 */

#include "catch.hpp"
#include <cmath>

namespace {

constexpr float kSwarmMaxSpeed = 18.0f;
constexpr float kSwarmDt       = 1.0f / 60.0f;
constexpr float kSeekDeadzone  = 1.0f;

struct vec3 { float x, y, z; };

/* Mock of the seek-player step. Takes current pos + player pos,
 * returns updated pos. Mirror of:
 *   port/src/swarm_test.c::cpu_seek_tick (CPU)
 *   port/fast3d/swarm_gpu.cpp::kSwarmCs main()  (GPU)
 *
 * Both production sites do exactly: ground-locked Y, length-checked
 * direction vector, scaled by max_speed, integrated with dt * 60. */
vec3 swarm_seek_step(vec3 pos, vec3 player_pos)
{
    float dx = player_pos.x - pos.x;
    float dz = player_pos.z - pos.z;
    float d2 = dx * dx + dz * dz;
    if (d2 < kSeekDeadzone) return pos;
    float d  = std::sqrt(d2);
    float vx = (dx / d) * kSwarmMaxSpeed;
    float vz = (dz / d) * kSwarmMaxSpeed;
    return { pos.x + vx * (60.0f * kSwarmDt),
             pos.y,
             pos.z + vz * (60.0f * kSwarmDt) };
}

float dist_xz(vec3 a, vec3 b)
{
    float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

TEST_CASE("swarm seek: boid moves toward player along direction vector",
          "[swarm][sim]")
{
    vec3 pos      = {  100.0f, 0.0f,  100.0f };
    vec3 player   = {    0.0f, 0.0f,    0.0f };

    vec3 next = swarm_seek_step(pos, player);

    /* X and Z should move toward player; Y unchanged (ground-locked). */
    REQUIRE(next.x < pos.x);
    REQUIRE(next.z < pos.z);
    REQUIRE(next.y == Approx(pos.y));
    /* Distance to player decreases by exactly max_speed (since seek
     * deadzone is far away). */
    float d_before = dist_xz(pos, player);
    float d_after  = dist_xz(next, player);
    REQUIRE(d_after == Approx(d_before - kSwarmMaxSpeed).margin(0.001f));
}

TEST_CASE("swarm seek: ground-locked Y axis", "[swarm][sim]")
{
    vec3 pos    = { 100.0f, 50.0f, 100.0f };
    vec3 player = {   0.0f,  0.0f,   0.0f };

    vec3 next = swarm_seek_step(pos, player);

    /* Y must not change even when player is at a different elevation. */
    REQUIRE(next.y == Approx(pos.y));
}

TEST_CASE("swarm seek: speed clamped to max_speed", "[swarm][sim]")
{
    /* From very far away. The seek vector is normalized * max_speed
     * regardless of how big the actual distance is. */
    vec3 pos    = { 10000.0f, 0.0f, 0.0f };
    vec3 player = {     0.0f, 0.0f, 0.0f };

    vec3 next = swarm_seek_step(pos, player);

    float step = std::abs(pos.x - next.x);
    REQUIRE(step == Approx(kSwarmMaxSpeed).margin(0.001f));
}

TEST_CASE("swarm seek: deadzone within 1 unit holds position",
          "[swarm][sim]")
{
    vec3 pos    = { 0.5f, 0.0f, 0.0f };
    vec3 player = { 0.0f, 0.0f, 0.0f };

    vec3 next = swarm_seek_step(pos, player);

    REQUIRE(next.x == Approx(pos.x));
    REQUIRE(next.z == Approx(pos.z));
}

TEST_CASE("swarm seek: monotonic convergence over many steps",
          "[swarm][sim]")
{
    vec3 pos    = { 600.0f, 0.0f, 0.0f };
    vec3 player = {   0.0f, 0.0f, 0.0f };

    float prev_d = dist_xz(pos, player);
    for (int i = 0; i < 50; ++i) {
        pos = swarm_seek_step(pos, player);
        float d = dist_xz(pos, player);
        REQUIRE(d <= prev_d);
        prev_d = d;
    }
    /* After 50 steps, distance has decreased by ~50 * max_speed. */
    REQUIRE(prev_d < 600.0f - 30.0f);
}

TEST_CASE("swarm seek: ring spawn distances are equal", "[swarm][sim]")
{
    /* Ring respawn (swarm_test.c::respawn_ring) places N skedars at
     * SWARM_RING_RADIUS = 600.0f around the player at evenly spaced
     * azimuth. Confirm the math: every spawn point is exactly
     * SWARM_RING_RADIUS away from the player. */
    constexpr float kRing = 600.0f;
    vec3 player = { 1000.0f, 100.0f, 2000.0f };

    for (int N : { 4, 8, 16, 32, 64, 128, 256 }) {
        for (int i = 0; i < N; ++i) {
            float ang = (2.0f * 3.14159265f) * ((float)i / (float)N);
            vec3 spawn = { player.x + kRing * std::cos(ang),
                           player.y,
                           player.z + kRing * std::sin(ang) };
            float d = dist_xz(spawn, player);
            REQUIRE(d == Approx(kRing).margin(0.001f));
        }
    }
}
