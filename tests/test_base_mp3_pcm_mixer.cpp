#include "catch.hpp"
#include <array>
#include <algorithm>
#include "mixer.h"

namespace {
constexpr size_t frames = 184;
constexpr uint16_t mainLeft = 0x4e0;
constexpr uint16_t mainRight = 0x650;
constexpr uint16_t wetLeft = 0x7c0;
constexpr uint16_t wetRight = 0x930;
using Block = std::array<int16_t, frames>;
using Outputs = std::array<Block, 4>;

void prepareEnvelope(int16_t left, int16_t right, int16_t wet = 0) {
    aClearBufferImpl(0, 3072);
    aSetVolumeImpl(A_VOL | A_LEFT, left, 0x7ffc, wet);
    aSetVolumeImpl(A_VOL | A_RIGHT, right, 0, 0);
    aSetVolumeImpl(A_RATE, left, 0, 0);
}

Outputs outputs() {
    Outputs result{};
    const uint16_t addresses[] = {mainLeft, mainRight, wetLeft, wetRight};
    for (size_t i = 0; i < result.size(); ++i) {
        aSaveBufferImpl(addresses[i], result[i].data(), frames * sizeof(int16_t));
    }
    return result;
}
}

TEST_CASE("base stereo envelope is identical to the mono path for duplicated PCM",
        "[audio][base-mp3][mixer]") {
    Block mono{};
    std::array<int16_t, frames * 2> stereo{};
    for (size_t i = 0; i < frames; ++i) {
        mono[i] = static_cast<int16_t>(-14000 + static_cast<int>(i) * 151);
        stereo[i * 2] = stereo[i * 2 + 1] = mono[i];
    }
    for (const int16_t wet : {int16_t(0), int16_t(12000)}) {
        ENVMIX_STATE monoState{};
        ENVMIX_STATE stereoState{};
        prepareEnvelope(22000, 13000, wet);
        aLoadBufferImpl(mono.data(), 0, mono.size() * sizeof(int16_t));
        aEnvMixerImpl(A_INIT, monoState, 13000);
        const Outputs expected = outputs();
        prepareEnvelope(22000, 13000, wet);
        aEnvMixerStereoImpl(A_INIT, stereoState, 13000, stereo.data(), frames);
        REQUIRE(outputs() == expected);
        // Continue from saved envelope state on the next synthesis pull.
        aClearBufferImpl(mainLeft, frames * 4 * sizeof(int16_t));
        aLoadBufferImpl(mono.data(), 0, mono.size() * sizeof(int16_t));
        aEnvMixerImpl(0, monoState, 0);
        const Outputs continued = outputs();
        aClearBufferImpl(mainLeft, frames * 4 * sizeof(int16_t));
        aEnvMixerStereoImpl(0, stereoState, 0, stereo.data(), frames);
        REQUIRE(outputs() == continued);
    }
}

TEST_CASE("base stereo envelope preserves independent channels and final frame",
        "[audio][base-mp3][mixer][eof]") {
    std::array<int16_t, frames * 2> stereo{};
    for (size_t i = 0; i < frames; ++i) {
        stereo[i * 2] = 12000;
        stereo[i * 2 + 1] = -6000;
    }
    for (const uint32_t count : {1u, 183u, 184u}) {
        ENVMIX_STATE state{};
        prepareEnvelope(32767, 32767);
        aEnvMixerStereoImpl(A_INIT, state, 32767, stereo.data(), count);
        const Outputs result = outputs();
        for (size_t i = 0; i < frames; ++i) {
            if (i < count) {
                REQUIRE(result[0][i] > 11000);
                REQUIRE(result[1][i] < -5500);
            } else {
                REQUIRE(result[0][i] == 0);
                REQUIRE(result[1][i] == 0);
            }
        }
    }
}

TEST_CASE("base stereo envelope retains silence pan gain and bounded input",
        "[audio][base-mp3][mixer][pan]") {
    std::array<int16_t, frames * 2> stereo{};
    stereo.fill(10000);
    ENVMIX_STATE state{};
    prepareEnvelope(0, 32767);
    aEnvMixerStereoImpl(A_INIT, state, 32767, stereo.data(), frames);
    const Outputs panned = outputs();
    for (size_t i = 0; i < frames; ++i) {
        REQUIRE(panned[0][i] == 0);
        REQUIRE(panned[1][i] > 9000);
    }
    aEnvMixerStereoImpl(A_INIT, state, 32767, nullptr, frames);
    REQUIRE(outputs() == panned);
    aEnvMixerStereoImpl(A_INIT, state, 32767, stereo.data(), frames + 1);
    REQUIRE(outputs() == panned);
    prepareEnvelope(0, 0);
    aEnvMixerStereoImpl(A_INIT, state, 0, stereo.data(), frames);
    const Outputs silent{};
    REQUIRE(outputs() == silent);
}
