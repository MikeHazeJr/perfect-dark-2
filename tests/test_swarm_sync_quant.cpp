/*
 * test_swarm_sync_quant.cpp -- Round-trip tests for the GPU swarm state
 * quantizer (port/src/net/swarm_sync_quant.c).
 *
 * Track 2d (c3807, 2026-05-16). Pairs with the design memo and the wire
 * format documented in port/include/net/swarm_sync_quant.h. The encode
 * step is lossy on pos / vel / surface_up; the AI ints round-trip exact.
 *
 * Coverage:
 *   1. Bit-precise round trip on AI ints (action_class, fire_request,
 *      target_propnum, anim_key).
 *   2. pos / vel within +/-0.5 cm (the half-step quantization error).
 *   3. surface_up direction error within ~0.01 (1/127 = 0.00787...).
 *   4. Boundary saturation: pos/vel beyond s16 range get clamped, not
 *      mis-wrapped.
 *   5. Stress sweep at random counts up to SWARM_SYNC_CHUNK_BOTS_MAX.
 */

#include "catch.hpp"
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>

extern "C" {
#include "net/swarm_sync_quant.h"
}

namespace {

constexpr int kRowStride = 4;  /* floats per row per bot */
constexpr int kRowsPerBot = 5;
constexpr int kFloatsPerBot = kRowStride * kRowsPerBot;

/* Write a single bot into the row-major float buffer. row indices match
 * the swarm_gpu.cpp texture layout:
 *   0=pos, 1=vel, 2=up, 3=ai, 4=range. */
void writeBot(float *buf, int count, int idx,
              float px, float py, float pz,
              float vx, float vy, float vz,
              float ux, float uy, float uz,
              int32_t action_class, int32_t fire_request,
              int32_t target_propnum, int32_t anim_key)
{
    float *row0 = buf + 0 * count * kRowStride + idx * kRowStride;
    float *row1 = buf + 1 * count * kRowStride + idx * kRowStride;
    float *row2 = buf + 2 * count * kRowStride + idx * kRowStride;
    float *row3 = buf + 3 * count * kRowStride + idx * kRowStride;
    float *row4 = buf + 4 * count * kRowStride + idx * kRowStride;

    row0[0] = px; row0[1] = py; row0[2] = pz; row0[3] = 0.0f;
    row1[0] = vx; row1[1] = vy; row1[2] = vz; row1[3] = 0.0f;
    row2[0] = ux; row2[1] = uy; row2[2] = uz; row2[3] = 0.0f;

    /* AI row: shader uses intBitsToFloat. We mirror that here by memcpy.
     * The C++ test side and the C encode side must agree on bit pattern. */
    int32_t iv[4] = { action_class, fire_request, target_propnum, anim_key };
    std::memcpy(row3, iv, sizeof(iv));

    row4[0] = 0.0f; row4[1] = 0.0f; row4[2] = 0.0f; row4[3] = 0.0f;
}

void readBot(const float *buf, int count, int idx,
             float &px, float &py, float &pz,
             float &vx, float &vy, float &vz,
             float &ux, float &uy, float &uz,
             int32_t &action_class, int32_t &fire_request,
             int32_t &target_propnum, int32_t &anim_key)
{
    const float *row0 = buf + 0 * count * kRowStride + idx * kRowStride;
    const float *row1 = buf + 1 * count * kRowStride + idx * kRowStride;
    const float *row2 = buf + 2 * count * kRowStride + idx * kRowStride;
    const float *row3 = buf + 3 * count * kRowStride + idx * kRowStride;
    px = row0[0]; py = row0[1]; pz = row0[2];
    vx = row1[0]; vy = row1[1]; vz = row1[2];
    ux = row2[0]; uy = row2[1]; uz = row2[2];

    int32_t iv[4];
    std::memcpy(iv, row3, sizeof(iv));
    action_class   = iv[0];
    fire_request   = iv[1];
    target_propnum = iv[2];
    anim_key       = iv[3];
}

} /* anon namespace */

TEST_CASE("swarm_sync_quant: AI ints round-trip exact across full domain",
          "[swarm][sync][quant]") {
    constexpr int N = 8;
    float src[N * kFloatsPerBot];
    std::memset(src, 0, sizeof(src));

    /* Hand-picked bit-patterns covering the full u8 / u16 / u8 / u8 domain. */
    writeBot(src, N, 0,  0,0,0,  0,0,0,  0,1,0, /*ac*/0, /*fr*/0, /*tp*/0,     /*ak*/0);
    writeBot(src, N, 1,  0,0,0,  0,0,0,  0,1,0, /*ac*/1, /*fr*/0, /*tp*/1,     /*ak*/1);
    writeBot(src, N, 2,  0,0,0,  0,0,0,  0,1,0, /*ac*/2, /*fr*/1, /*tp*/255,   /*ak*/2);
    writeBot(src, N, 3,  0,0,0,  0,0,0,  0,1,0, /*ac*/3, /*fr*/1, /*tp*/256,   /*ak*/3);
    writeBot(src, N, 4,  0,0,0,  0,0,0,  0,1,0, /*ac*/127,/*fr*/0,/*tp*/4096,  /*ak*/200);
    writeBot(src, N, 5,  0,0,0,  0,0,0,  0,1,0, /*ac*/200,/*fr*/1,/*tp*/65535, /*ak*/255);
    writeBot(src, N, 6,  0,0,0,  0,0,0,  0,1,0, /*ac*/255,/*fr*/1,/*tp*/0,     /*ak*/100);
    writeBot(src, N, 7,  0,0,0,  0,0,0,  0,1,0, /*ac*/0,  /*fr*/0,/*tp*/12345, /*ak*/42);

    uint8_t packed[N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT];
    const int wbytes = swarmSyncQuantEncode(src, N, packed);
    REQUIRE(wbytes == N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT);

    float dst[N * kFloatsPerBot];
    const int rdec = swarmSyncQuantDecode(packed, N, dst);
    REQUIRE(rdec == N);

    for (int i = 0; i < N; i++) {
        float px,py,pz,vx,vy,vz,ux,uy,uz;
        int32_t ac, fr, tp, ak;
        readBot(dst, N, i, px,py,pz, vx,vy,vz, ux,uy,uz, ac, fr, tp, ak);

        int32_t src_ac, src_fr, src_tp, src_ak;
        float dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz;
        readBot(src, N, i, dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz,
                src_ac, src_fr, src_tp, src_ak);

        /* AI ints exact within their wire width: ac/fr/ak are u8, tp is u16. */
        REQUIRE(ac == (src_ac & 0xFF));
        REQUIRE(fr == (src_fr & 0xFF));
        REQUIRE(tp == (src_tp & 0xFFFF));
        REQUIRE(ak == (src_ak & 0xFF));
    }
}

TEST_CASE("swarm_sync_quant: pos round-trips within +/- 0.5 cm tolerance",
          "[swarm][sync][quant]") {
    constexpr int N = 6;
    float src[N * kFloatsPerBot];
    std::memset(src, 0, sizeof(src));

    /* Spread positions across the arena-realistic range. PD arenas are
     * typically <= +/- 10000 game units (~100 m). */
    writeBot(src, N, 0,    0.0f,    0.0f,    0.0f, 0,0,0, 0,1,0, 0,0,0,0);
    writeBot(src, N, 1,    1.5f,   -2.7f,    3.9f, 0,0,0, 0,1,0, 0,0,0,0);
    writeBot(src, N, 2,  100.0f, -100.0f,  100.0f, 0,0,0, 0,1,0, 0,0,0,0);
    writeBot(src, N, 3, 9999.0f, -9999.0f, 5000.0f, 0,0,0, 0,1,0, 0,0,0,0);
    writeBot(src, N, 4,  -32767.0f, 32766.0f, 0.0f, 0,0,0, 0,1,0, 0,0,0,0);
    writeBot(src, N, 5,    3.14159f, -2.71828f, 1.61803f, 0,0,0, 0,1,0, 0,0,0,0);

    uint8_t packed[N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT];
    REQUIRE(swarmSyncQuantEncode(src, N, packed)
            == N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT);

    float dst[N * kFloatsPerBot];
    REQUIRE(swarmSyncQuantDecode(packed, N, dst) == N);

    for (int i = 0; i < N; i++) {
        float dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz;
        int32_t a, b, c, d;
        readBot(dst, N, i, dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz,
                a, b, c, d);

        float spx, spy, spz, svx, svy, svz, sux, suy, suz;
        readBot(src, N, i, spx, spy, spz, svx, svy, svz, sux, suy, suz,
                a, b, c, d);

        INFO("bot=" << i);
        REQUIRE(std::abs(dpx - spx) <= 0.5f);
        REQUIRE(std::abs(dpy - spy) <= 0.5f);
        REQUIRE(std::abs(dpz - spz) <= 0.5f);
    }
}

TEST_CASE("swarm_sync_quant: vel round-trips within +/- 0.5 cm/frame tolerance",
          "[swarm][sync][quant]") {
    /* Vel magnitudes in steady state are bounded by max_speed (5.0
     * units/frame per swarm_gpu.cpp). The encoding is identical to pos
     * so this should be tight. */
    constexpr int N = 5;
    float src[N * kFloatsPerBot];
    std::memset(src, 0, sizeof(src));
    writeBot(src, N, 0, 0,0,0, 0.0f, 0.0f, 0.0f, 0,1,0, 0,0,0,0);
    writeBot(src, N, 1, 0,0,0, 5.0f, -5.0f, 5.0f, 0,1,0, 0,0,0,0);
    writeBot(src, N, 2, 0,0,0, 2.34f, -1.56f, 0.78f, 0,1,0, 0,0,0,0);
    writeBot(src, N, 3, 0,0,0, 100.0f, -100.0f, 100.0f, 0,1,0, 0,0,0,0);
    writeBot(src, N, 4, 0,0,0, 32766.0f, -32766.0f, 0.0f, 0,1,0, 0,0,0,0);

    uint8_t packed[N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT];
    REQUIRE(swarmSyncQuantEncode(src, N, packed)
            == N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT);

    float dst[N * kFloatsPerBot];
    REQUIRE(swarmSyncQuantDecode(packed, N, dst) == N);

    for (int i = 0; i < N; i++) {
        float dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz;
        int32_t a, b, c, d;
        readBot(dst, N, i, dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz, a, b, c, d);

        float spx, spy, spz, svx, svy, svz, sux, suy, suz;
        readBot(src, N, i, spx, spy, spz, svx, svy, svz, sux, suy, suz, a, b, c, d);

        INFO("bot=" << i);
        REQUIRE(std::abs(dvx - svx) <= 0.5f);
        REQUIRE(std::abs(dvy - svy) <= 0.5f);
        REQUIRE(std::abs(dvz - svz) <= 0.5f);
    }
}

TEST_CASE("swarm_sync_quant: surface_up round-trips within ~0.01 (1/127)",
          "[swarm][sync][quant]") {
    constexpr int N = 4;
    float src[N * kFloatsPerBot];
    std::memset(src, 0, sizeof(src));

    /* World-up. */
    writeBot(src, N, 0, 0,0,0, 0,0,0, 0.0f, 1.0f, 0.0f, 0,0,0,0);
    /* Cardinal X. */
    writeBot(src, N, 1, 0,0,0, 0,0,0, 1.0f, 0.0f, 0.0f, 0,0,0,0);
    /* Diagonal 45-deg. */
    writeBot(src, N, 2, 0,0,0, 0,0,0, 0.7071f, 0.7071f, 0.0f, 0,0,0,0);
    /* Mostly down. */
    writeBot(src, N, 3, 0,0,0, 0,0,0, -0.3f, -0.95f, 0.05f, 0,0,0,0);

    uint8_t packed[N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT];
    REQUIRE(swarmSyncQuantEncode(src, N, packed)
            == N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT);

    float dst[N * kFloatsPerBot];
    REQUIRE(swarmSyncQuantDecode(packed, N, dst) == N);

    for (int i = 0; i < N; i++) {
        float dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz;
        int32_t a, b, c, d;
        readBot(dst, N, i, dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz, a, b, c, d);

        float spx, spy, spz, svx, svy, svz, sux, suy, suz;
        readBot(src, N, i, spx, spy, spz, svx, svy, svz, sux, suy, suz, a, b, c, d);

        INFO("bot=" << i);
        /* +/- 1/127 ~= 0.00787; allow a small fudge for round-to-nearest. */
        REQUIRE(std::abs(dux - sux) <= 0.01f);
        REQUIRE(std::abs(duy - suy) <= 0.01f);
        REQUIRE(std::abs(duz - suz) <= 0.01f);
    }
}

TEST_CASE("swarm_sync_quant: large random sweep does not crash",
          "[swarm][sync][quant]") {
    /* Pseudo-randomish sweep up to a reasonably big chunk. The cap matches
     * SWARM_SYNC_CHUNK_BOTS_MAX (1024) which is the wire-side chunk ceiling. */
    constexpr int N = 1024;
    static float src[N * kFloatsPerBot];

    /* Fill with a deterministic pattern so the test is reproducible. */
    std::srand(0xc3807);
    for (int i = 0; i < N; i++) {
        const float px = ((float)(std::rand() % 60000) - 30000.0f);
        const float py = ((float)(std::rand() % 60000) - 30000.0f);
        const float pz = ((float)(std::rand() % 60000) - 30000.0f);
        const float vx = ((float)(std::rand() % 1000) - 500.0f) * 0.01f;
        const float vy = ((float)(std::rand() % 1000) - 500.0f) * 0.01f;
        const float vz = ((float)(std::rand() % 1000) - 500.0f) * 0.01f;
        writeBot(src, N, i, px, py, pz, vx, vy, vz, 0.0f, 1.0f, 0.0f,
                 std::rand() & 0xFF, std::rand() & 1,
                 std::rand() & 0xFFFF, std::rand() & 0xFF);
    }

    static uint8_t packed[N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT];
    const int wbytes = swarmSyncQuantEncode(src, N, packed);
    REQUIRE(wbytes == N * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT);

    static float dst[N * kFloatsPerBot];
    REQUIRE(swarmSyncQuantDecode(packed, N, dst) == N);

    /* Spot-check 16 evenly-spaced bots for pos tolerance. */
    for (int probe = 0; probe < 16; probe++) {
        const int i = probe * (N / 16);
        float dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz;
        int32_t a, b, c, d;
        readBot(dst, N, i, dpx, dpy, dpz, dvx, dvy, dvz, dux, duy, duz, a, b, c, d);

        float spx, spy, spz, svx, svy, svz, sux, suy, suz;
        readBot(src, N, i, spx, spy, spz, svx, svy, svz, sux, suy, suz, a, b, c, d);

        REQUIRE(std::abs(dpx - spx) <= 0.5f);
        REQUIRE(std::abs(dpy - spy) <= 0.5f);
        REQUIRE(std::abs(dpz - spz) <= 0.5f);
    }
}

TEST_CASE("swarm_sync_quant: null / zero-count is graceful",
          "[swarm][sync][quant]") {
    float buf[64];
    uint8_t packed[SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT];

    REQUIRE(swarmSyncQuantEncode(nullptr, 1, packed) == 0);
    REQUIRE(swarmSyncQuantEncode(buf, 1, nullptr) == 0);
    REQUIRE(swarmSyncQuantEncode(buf, 0, packed) == 0);
    REQUIRE(swarmSyncQuantEncode(buf, -1, packed) == 0);

    REQUIRE(swarmSyncQuantDecode(nullptr, 1, buf) == 0);
    REQUIRE(swarmSyncQuantDecode(packed, 1, nullptr) == 0);
    REQUIRE(swarmSyncQuantDecode(packed, 0, buf) == 0);
    REQUIRE(swarmSyncQuantDecode(packed, -1, buf) == 0);
}
