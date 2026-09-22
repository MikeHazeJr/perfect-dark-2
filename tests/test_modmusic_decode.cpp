#include "catch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <SDL.h>

extern "C" {
#include "modmusic.h"
#include "audio_pcm_oneshot.h"
#include "external/stb_vorbis.h"
#include "external/minimp3.h"
void testStubFsFileLoadWith(const char *path, const void *bytes, u32 size);
s16 *modMusicTestLoadOggWithByteLimit(const char *path, u32 *outLen,
    s32 *outSourceRate, u32 maxPcmBytes);
}

#ifndef PD_AUDIO_FIXTURE_DIR
#define PD_AUDIO_FIXTURE_DIR PD_SOURCE_DIR "/tests/fixtures/audio/vorbis"
#endif

namespace {
float masterVolume = 1.0f;
float musicVolume = 1.0f;
unsigned restoreCount = 0;
unsigned muteCount = 0;

struct AudioScope {
    AudioScope() {
        modMusicStop();
        testStubFsFileLoadWith(nullptr, nullptr, 0);
        masterVolume = musicVolume = 1.0f;
        modMusicSetVolume(1.0f);
        restoreCount = muteCount = 0;
    }
    ~AudioScope() {
        modMusicStop();
        testStubFsFileLoadWith(nullptr, nullptr, 0);
    }
};

using Decoded = std::unique_ptr<s16, decltype(&SDL_free)>;
using VorbisDecoder = std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)>;

std::vector<unsigned char> readFixture(const char *name) {
    std::ifstream input(std::string(PD_AUDIO_FIXTURE_DIR) + "/" + name,
        std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void append16(std::vector<unsigned char>& out, uint16_t value) {
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
}
void append32(std::vector<unsigned char>& out, uint32_t value) {
    append16(out, static_cast<uint16_t>(value));
    append16(out, static_cast<uint16_t>(value >> 16));
}
std::vector<unsigned char> wav(const std::vector<s16>& interleaved) {
    std::vector<unsigned char> out{'R','I','F','F'};
    append32(out, static_cast<uint32_t>(36 + interleaved.size() * 2));
    const char format[] = "WAVEfmt ";
    out.insert(out.end(), format, format + 8);
    append32(out, 16);
    append16(out, 1); // signed PCM
    append16(out, 2);
    append32(out, 22050);
    append32(out, 22050 * 4);
    append16(out, 4);
    append16(out, 16);
    out.insert(out.end(), {'d','a','t','a'});
    append32(out, static_cast<uint32_t>(interleaved.size() * 2));
    for (s16 sample : interleaved) append16(out, static_cast<uint16_t>(sample));
    return out;
}

double rms(const s16 *pcm, size_t frames, size_t channel, size_t channels) {
    double sum = 0;
    for (size_t i = 0; i < frames; ++i) {
        const double value = pcm[i * channels + channel];
        sum += value * value;
    }
    return std::sqrt(sum / frames);
}
}

// These five dependencies are absent from tests/stubs.c and all other test
// objects. Reuse its fsFileLoad/sysLogPrintf and the real linked modvfs.c.
// Do not add any second decoder, fsFileLoad, logging or VFS implementation.
extern "C" float audioGetMasterVolume(void) { return masterVolume; }
extern "C" float audioGetMusicVolume(void) { return musicVolume; }
extern "C" void audioApplyVolumes(void) { ++restoreCount; }
extern "C" void musicSetVolume(u16 volume) { if (volume == 0) ++muteCount; }
extern "C" const char *fsFullPath(const char *path, char *out, size_t size) {
    if (!out || size == 0) return path ? path : "";
    std::snprintf(out, size, "%s", path ? path : "");
    return out;
}

TEST_CASE("upstream incremental Vorbis preserves real mono and stereo frame counts",
        "[audio][vorbis][decode]") {
    const struct { const char *file; int channels; int rate; int frames; } cases[] = {
        {"tone_mono_22050.ogg", 1, 22050, 4410},
        {"tone_stereo_44100.ogg", 2, 44100, 8820},
        {"tone_stereo_22050.ogg", 2, 22050, 4410},
    };
    for (const auto& item : cases) {
        INFO(item.file);
        auto bytes = readFixture(item.file);
        REQUIRE(bytes.size() > 4);
        REQUIRE(std::memcmp(bytes.data(), "OggS", 4) == 0);
        int error = 0;
        VorbisDecoder memory(stb_vorbis_open_memory(bytes.data(),
            static_cast<int>(bytes.size()), &error, nullptr), &stb_vorbis_close);
        REQUIRE(memory != nullptr);
        const stb_vorbis_info info = stb_vorbis_get_info(memory.get());
        REQUIRE(info.channels == item.channels);
        REQUIRE(info.sample_rate == static_cast<unsigned int>(item.rate));
        std::vector<short> memoryPcm(item.frames * item.channels);
        const int frames = stb_vorbis_get_samples_short_interleaved(memory.get(),
            info.channels, memoryPcm.data(), static_cast<int>(memoryPcm.size()));
        REQUIRE(stb_vorbis_get_error(memory.get()) == 0);
        REQUIRE(frames == item.frames);
        REQUIRE(rms(memoryPcm.data(), frames, 0, info.channels) > 1000.0);
        short tail[2] = {};
        REQUIRE(stb_vorbis_get_samples_short_interleaved(memory.get(),
            info.channels, tail, info.channels) == 0);
        REQUIRE(stb_vorbis_get_error(memory.get()) == 0);
        const std::string path = std::string(PD_AUDIO_FIXTURE_DIR) + "/" + item.file;
        VorbisDecoder file(stb_vorbis_open_filename(path.c_str(), &error, nullptr),
            &stb_vorbis_close);
        REQUIRE(file != nullptr);
        const stb_vorbis_info fileInfo = stb_vorbis_get_info(file.get());
        REQUIRE(fileInfo.channels == info.channels);
        REQUIRE(fileInfo.sample_rate == info.sample_rate);
        std::vector<short> filePcm(item.frames * item.channels);
        const int fileFrames = stb_vorbis_get_samples_short_interleaved(file.get(),
            fileInfo.channels, filePcm.data(), static_cast<int>(filePcm.size()));
        REQUIRE(stb_vorbis_get_error(file.get()) == 0);
        REQUIRE(fileFrames == frames);
        REQUIRE(memoryPcm == filePcm);
    }
}

TEST_CASE("production Vorbis loader uses provider bytes and yields stereo PCM at 22050",
        "[audio][vorbis][decode][provider]") {
    AudioScope scope;
    for (const auto *file : {"tone_mono_22050.ogg", "tone_stereo_44100.ogg",
            "tone_stereo_22050.ogg"}) {
        INFO(file);
        auto bytes = readFixture(file);
        const char *path = "fixture:voice.pdvoice::sample.ogg";
        testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
        u32 samples = 0;
        s32 sourceRate = 0;
        Decoded pcm(modMusicLoadAudioPcm22050(path, &samples, &sourceRate), &SDL_free);
        REQUIRE(pcm != nullptr);
        REQUIRE(samples % 2 == 0);
        REQUIRE(samples / 2 >= 4406);
        REQUIRE(samples / 2 <= 4414);
        REQUIRE(sourceRate == (std::strstr(file, "44100") ? 44100 : 22050));
        REQUIRE(rms(pcm.get(), samples / 2, 0, 2) > 1000.0);
        REQUIRE(rms(pcm.get(), samples / 2, 1, 2) > 1000.0);
        double channelDifference = 0;
        for (u32 i = 0; i < samples; i += 2) {
            const double difference = pcm.get()[i] - pcm.get()[i + 1];
            channelDifference += difference * difference;
        }
        if (std::strstr(file, "mono")) REQUIRE(channelDifference == 0.0);
        else REQUIRE(channelDifference / (samples / 2) > 1000000.0);
    }
}

TEST_CASE("production Vorbis filename loading matches provider-owned input",
        "[audio][vorbis][decode][ownership]") {
    AudioScope scope;
    auto bytes = readFixture("tone_stereo_22050.ogg");
    const char *providerPath = "fixture:memory.ogg";
    testStubFsFileLoadWith(providerPath, bytes.data(), static_cast<u32>(bytes.size()));
    u32 memorySamples = 0;
    s32 memoryRate = 0;
    Decoded memory(modMusicLoadAudioPcm22050(providerPath, &memorySamples,
        &memoryRate), &SDL_free);
    REQUIRE(memory != nullptr);
    REQUIRE(memorySamples == 4410 * 2);
    REQUIRE(memoryRate == 22050);

    // No filesystem stub matches: exercise the real incremental filename path.
    testStubFsFileLoadWith(nullptr, nullptr, 0);
    const std::string path = std::string(PD_AUDIO_FIXTURE_DIR) +
        "/tone_stereo_22050.ogg";
    u32 fileSamples = 0;
    s32 fileRate = 0;
    Decoded file(modMusicLoadAudioPcm22050(path.c_str(), &fileSamples, &fileRate),
        &SDL_free);
    REQUIRE(file != nullptr);
    REQUIRE(fileSamples == memorySamples);
    REQUIRE(fileRate == memoryRate);
    REQUIRE(std::equal(memory.get(), memory.get() + memorySamples, file.get()));
}

TEST_CASE("production Vorbis accumulation rejects excess PCM before growth and cleans up",
        "[audio][vorbis][decode][ownership][bounds]") {
    AudioScope scope;
    auto bytes = readFixture("tone_stereo_22050.ogg");
    const auto originalBytes = bytes;
    const char *path = "fixture:bounded.ogg";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    u32 samples = 0;
    // Warm SDL setup using the real decoder before checking live allocations.
    Decoded warm(modMusicLoadAudioPcm22050(path, &samples, nullptr), &SDL_free);
    REQUIRE(warm != nullptr);
    REQUIRE(samples == 4410 * 2);
    warm.reset();
    const int baseline = SDL_GetNumAllocations();
    // 8192 permits the first chunk, so the later rejection must free residency.
    // 17639 is one byte less than the exact 4410-frame stereo source requires.
    for (const u32 ceiling : {0u, 8191u, 8192u, 17639u}) {
        INFO(ceiling);
        samples = 99;
        s32 rate = 99;
        Decoded pcm(modMusicTestLoadOggWithByteLimit(path, &samples, &rate,
            ceiling), &SDL_free);
        REQUIRE(pcm == nullptr);
        REQUIRE(samples == 0);
        REQUIRE(rate == 22050);
        REQUIRE(SDL_GetNumAllocations() == baseline);
        REQUIRE(bytes == originalBytes);
    }
    // Exactly enough space succeeds even when geometric growth would overshoot.
    Decoded exact(modMusicTestLoadOggWithByteLimit(path, &samples, nullptr,
        17640), &SDL_free);
    REQUIRE(exact != nullptr);
    REQUIRE(samples == 4410 * 2);
    REQUIRE(SDL_GetNumAllocations() == baseline + 1);
    exact.reset();
    REQUIRE(SDL_GetNumAllocations() == baseline);

    // The production ceiling is retained when the test seam is given UINT32_MAX.
    Decoded full(modMusicTestLoadOggWithByteLimit(path, &samples, nullptr,
        UINT32_MAX), &SDL_free);
    REQUIRE(full != nullptr);
    REQUIRE(samples == 4410 * 2);
}

TEST_CASE("production Vorbis ceiling includes SDL intermediate conversion storage",
        "[audio][vorbis][decode][ownership][bounds]") {
    AudioScope scope;
    auto bytes = readFixture("tone_mono_22050.ogg");
    const char *path = "fixture:bounded_mono.ogg";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    SDL_AudioCVT conversion;
    REQUIRE(SDL_BuildAudioCVT(&conversion, AUDIO_S16SYS, 1, 22050,
        AUDIO_S16SYS, 2, 22050) > 0);
    REQUIRE(conversion.len_mult > 1);
    // The fixture is 4410 mono S16 frames. SDL's actual multiplier describes
    // all conversion workspace, which can exceed the final stereo output.
    const u32 requiredCeiling = 4410u * sizeof(s16) * conversion.len_mult;
    u32 samples = 0;
    Decoded warm(modMusicLoadAudioPcm22050(path, &samples, nullptr), &SDL_free);
    REQUIRE(warm != nullptr);
    warm.reset();
    const int baseline = SDL_GetNumAllocations();
    Decoded rejected(modMusicTestLoadOggWithByteLimit(path, &samples, nullptr,
        requiredCeiling - 1), &SDL_free);
    REQUIRE(rejected == nullptr);
    REQUIRE(samples == 0);
    REQUIRE(SDL_GetNumAllocations() == baseline);
    Decoded exact(modMusicTestLoadOggWithByteLimit(path, &samples, nullptr,
        requiredCeiling), &SDL_free);
    REQUIRE(exact != nullptr);
    REQUIRE(samples == 4410 * 2);
    REQUIRE(SDL_GetNumAllocations() == baseline + 1);
    for (u32 i = 0; i < samples; i += 2) REQUIRE(exact.get()[i] == exact.get()[i + 1]);
    exact.reset();
    REQUIRE(SDL_GetNumAllocations() == baseline);
}

TEST_CASE("production music mixer consumes actual Vorbis PCM and volume controls",
        "[audio][vorbis][mixer]") {
    AudioScope scope;
    auto bytes = readFixture("tone_stereo_44100.ogg");
    const char *path = "fixture:song.ogg";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    u32 samples = 0;
    Decoded expected(modMusicLoadAudioPcm22050(path, &samples, nullptr), &SDL_free);
    REQUIRE(expected != nullptr);
    REQUIRE(samples > 1024);
    masterVolume = 0.8f;
    musicVolume = 0.5f;
    modMusicSetVolume(0.5f);
    modMusicPlay(path);
    REQUIRE(modMusicIsPlaying() == 1);
    REQUIRE(muteCount == 1);
    REQUIRE(modMusicGetDurationMs() >= 199);
    REQUIRE(modMusicGetDurationMs() <= 201);
    std::vector<s16> actual(1024, 0);
    modMusicMixInto(actual.data(), 512);
    const float gain = masterVolume * musicVolume * 0.5f;
    for (size_t i = 0; i < actual.size(); ++i) {
        REQUIRE(actual[i] == static_cast<s16>(expected.get()[i] * gain));
    }
    REQUIRE(rms(actual.data(), 512, 0, 2) > 100.0);
    modMusicStop();
    REQUIRE(modMusicIsPlaying() == 0);
    REQUIRE(restoreCount == 1);
    auto stopped = actual;
    modMusicMixInto(actual.data(), 512);
    REQUIRE(actual == stopped);
}

TEST_CASE("production decoder rejects non-Vorbis input without publishing PCM",
        "[audio][vorbis][decode]") {
    AudioScope scope;
    auto wavBytes = wav({1000, -1000, 2000, -2000});
    auto ogg = readFixture("tone_mono_22050.ogg");
    REQUIRE(ogg.size() > 32);
    const std::vector<unsigned char> junk{'O','g','g','S',0,0,0,0};
    const std::vector<unsigned char> truncated(ogg.begin(), ogg.begin() + 32);
    for (const auto& bytes : {wavBytes, junk, truncated}) {
        const char *path = "fixture:invalid.ogg";
        testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
        u32 samples = 99;
        s32 rate = 0;
        Decoded pcm(modMusicLoadAudioPcm22050(path, &samples, &rate), &SDL_free);
        REQUIRE(pcm == nullptr);
        REQUIRE(samples == 0);
    }
}

TEST_CASE("production WAV path preserves every final stereo frame through mixer EOF",
        "[audio][wav][mixer][eof]") {
    AudioScope scope;
    // One-frame input pins the edge case, then an uneven block pins the tail.
    for (const auto& samples : {std::vector<s16>{1234, -2345},
            std::vector<s16>{111, -222, 333, -444, 555, -666}}) {
        auto bytes = wav(samples);
        const char *path = "fixture:tail.wav";
        testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
        u32 count = 0;
        s32 rate = 0;
        Decoded loaded(modMusicLoadAudioPcm22050(path, &count, &rate), &SDL_free);
        REQUIRE(loaded != nullptr);
        REQUIRE(count == samples.size());
        REQUIRE(rate == 22050);
        REQUIRE(std::equal(samples.begin(), samples.end(), loaded.get()));
        modMusicPlay(path);
        REQUIRE(modMusicIsPlaying() == 1);
        std::vector<s16> output(samples.size() + 4, 0);
        modMusicMixInto(output.data(), static_cast<u32>(output.size() / 2));
        REQUIRE(std::equal(samples.begin(), samples.end(), output.begin()));
        REQUIRE(output[samples.size()] == 0);
        REQUIRE(output[samples.size() + 1] == 0);
        REQUIRE(modMusicIsPlaying() == 0);
        modMusicStop();
    }
}

TEST_CASE("completed music replacement releases the retained PCM allocation",
        "[audio][vorbis][mixer][ownership]") {
    AudioScope scope;
    auto bytes = readFixture("tone_stereo_22050.ogg");
    const char *path = "fixture:replay.ogg";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    // Warm decoder/SDL lazy setup before taking the live allocation baseline.
    modMusicPlay(path);
    REQUIRE(modMusicIsPlaying() == 1);
    modMusicStop();
    const int baseline = SDL_GetNumAllocations();
    for (int replay = 0; replay < 3; ++replay) {
        modMusicPlay(path);
        REQUIRE(modMusicIsPlaying() == 1);
        std::vector<s16> output(5000 * 2, 0);
        modMusicMixInto(output.data(), 5000);
        REQUIRE(modMusicIsPlaying() == 0);
        REQUIRE(rms(output.data(), 4410, 0, 2) > 1000.0);
        // Exactly the one deliberately retained SDL-owned PCM allocation.
        REQUIRE(SDL_GetNumAllocations() == baseline + 1);
    }
    modMusicStop();
    REQUIRE(SDL_GetNumAllocations() == baseline);
}

TEST_CASE("music interpolation retains the final frame at adjusted rates",
        "[audio][wav][mixer][rate]") {
    AudioScope scope;
    const std::vector<s16> samples{100, -100, 200, -200, 300, -300, 400, -400};
    auto bytes = wav(samples);
    const char *path = "fixture:rate.wav";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    for (const float rate : {0.97f, 1.0f, 1.03f}) {
        INFO(rate);
        modMusicPlay(path);
        modMusicSetRate(rate);
        std::vector<s16> output(16, 0);
        modMusicMixInto(output.data(), 8);
        double cursor = 0.0;
        size_t written = 0;
        while (cursor < 4.0) {
            const size_t first = static_cast<size_t>(cursor);
            const size_t next = std::min(first + 1, size_t(3));
            const double fraction = cursor - first;
            const float expected = static_cast<float>(samples[first * 2]
                + (samples[next * 2] - samples[first * 2]) * fraction);
            REQUIRE(output[written * 2] == static_cast<s16>(expected));
            REQUIRE(output[written * 2 + 1] == static_cast<s16>(-expected));
            ++written;
            cursor += static_cast<double>(rate);
        }
        for (size_t i = written * 2; i < output.size(); ++i) REQUIRE(output[i] == 0);
        REQUIRE(modMusicIsPlaying() == 0);
        modMusicStop();
    }
}

TEST_CASE("music sync clamps seeks and rejects an unordered playback rate",
        "[audio][wav][mixer][rate]") {
    AudioScope scope;
    auto bytes = wav({100, -100, 200, -200, 300, -300, 400, -400});
    const char *path = "fixture:seek.wav";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    modMusicPlay(path);
    modMusicSetRate(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(modMusicGetRate() == 1.0f);
    modMusicSetPositionMs(std::numeric_limits<u32>::max());
    s16 output[4] = {};
    modMusicMixInto(output, 2);
    REQUIRE(output[0] == 400);
    REQUIRE(output[1] == -400);
    REQUIRE(output[2] == 0);
    REQUIRE(output[3] == 0);
    REQUIRE(modMusicIsPlaying() == 0);
}

#ifndef PD_MP3_FIXTURE_DIR
#define PD_MP3_FIXTURE_DIR PD_SOURCE_DIR "/tests/fixtures/audio/mp3"
#endif

TEST_CASE("production MP3 loader preserves complete MPEG-1 MPEG-2 and MPEG-2.5 frames and channels",
        "[audio][mp3][decode][T-ASSETS-046]") {
    AudioScope scope;
    const struct { const char *file; int channels; int rate; int frameSize; } cases[] = {
        {"tone_mpeg1_stereo_44100.mp3", 2, 44100, 1152},
        {"tone_mpeg2_stereo_22050.mp3", 2, 22050, 576},
        {"tone_mpeg2_mono_22050.mp3", 1, 22050, 576},
        {"tone_mpeg25_mono_11025.mp3", 1, 11025, 576},
    };
    for (const auto& item : cases) {
        INFO(item.file);
        std::ifstream input(std::string(PD_MP3_FIXTURE_DIR) + "/" + item.file,
            std::ios::binary);
        REQUIRE(input.good());
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        REQUIRE(!bytes.empty());
        mp3dec_t decoder;
        mp3dec_init(&decoder);
        std::vector<s16> reference;
        size_t position = 0;
        while (position < bytes.size()) {
            mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
            mp3dec_frame_info_t info;
            const int frames = mp3dec_decode_frame(&decoder, bytes.data() + position,
                static_cast<int>(bytes.size() - position), frame, &info);
            REQUIRE(info.frame_bytes >= 0);
            REQUIRE(static_cast<size_t>(info.frame_bytes) <= bytes.size() - position);
            if (!info.frame_bytes) break;
            position += info.frame_bytes;
            if (!frames) continue;
            REQUIRE(frames == item.frameSize);
            REQUIRE(info.channels == item.channels);
            REQUIRE(info.hz == item.rate);
            reference.insert(reference.end(), frame, frame + frames * info.channels);
        }
        REQUIRE(reference.size() > 580 * 2);
        const char *path = "fixture:base_voice.mp3";
        testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
        u32 samples = 0;
        s32 sourceRate = 0;
        Decoded pcm(modMusicLoadAudioPcm22050(path, &samples, &sourceRate), &SDL_free);
        REQUIRE(pcm != nullptr);
        REQUIRE(sourceRate == item.rate);
        REQUIRE(samples % 2 == 0);
        const size_t sourceFrames = reference.size() / item.channels;
        const size_t convertedFrames = sourceFrames * 22050 / item.rate;
        REQUIRE(samples / 2 + 4 >= convertedFrames);
        REQUIRE(samples / 2 <= convertedFrames + 4);
        REQUIRE(rms(pcm.get(), samples / 2, 0, 2) > 1000.0);
        REQUIRE(rms(pcm.get(), samples / 2, 1, 2) > 1000.0);
        if (item.rate == 22050 && item.channels == 2) {
            REQUIRE(samples == reference.size());
            REQUIRE(std::equal(reference.begin(), reference.end(), pcm.get()));
        }
        double difference = 0;
        for (u32 i = 0; i < samples; i += 2) {
            const double d = pcm.get()[i] - pcm.get()[i + 1];
            difference += d * d;
        }
        if (item.channels == 1) REQUIRE(difference == 0.0);
        else REQUIRE(difference / (samples / 2) > 1000000.0);
    }
}

TEST_CASE("production MP3 PCM has SDL ownership with and without conversion",
        "[audio][mp3][ownership][T-ASSETS-046]") {
    AudioScope scope;
    for (const char *name : {"tone_mpeg1_stereo_44100.mp3", "tone_mpeg2_stereo_22050.mp3",
            "tone_mpeg25_mono_11025.mp3"}) {
        INFO(name);
        std::ifstream input(std::string(PD_MP3_FIXTURE_DIR) + "/" + name,
            std::ios::binary);
        REQUIRE(input.good());
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        const char *path = "fixture:ownership.mp3";
        testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
        u32 samples = 0;
        Decoded warm(modMusicLoadAudioPcm22050(path, &samples, nullptr), &SDL_free);
        REQUIRE(warm != nullptr);
        warm.reset();
        const int baseline = SDL_GetNumAllocations();
        for (int attempt = 0; attempt < 3; ++attempt) {
            Decoded pcm(modMusicLoadAudioPcm22050(path, &samples, nullptr), &SDL_free);
            REQUIRE(pcm != nullptr);
            REQUIRE(samples > 580 * 2);
            REQUIRE(SDL_GetNumAllocations() == baseline + 1);
            pcm.reset();
            REQUIRE(SDL_GetNumAllocations() == baseline);
        }
    }
}

namespace {
std::vector<unsigned char> mp3FramingFixture(const char *name) {
    std::ifstream input(std::string(PD_MP3_FIXTURE_DIR) + "/" + name, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<unsigned char> mp3Id3Prefix(unsigned version, bool footer = false) {
    const std::vector<unsigned char> body = version == 2
        ? std::vector<unsigned char>{'T','T','2',0,0,5,0,'t','o','n','e'}
        : std::vector<unsigned char>{'T','I','T','2',0,0,0,5,0,0,
            static_cast<unsigned char>(version == 4 ? 3 : 0),'t','o','n','e'};
    std::vector<unsigned char> tag{'I','D','3', static_cast<unsigned char>(version), 0,
        static_cast<unsigned char>(footer ? 0x10 : 0), 0,0,0,
        static_cast<unsigned char>(body.size())};
    tag.insert(tag.end(), body.begin(), body.end());
    if (footer) {
        std::vector<unsigned char> end(tag.begin(), tag.begin() + 10);
        end[0] = '3'; end[1] = 'D'; end[2] = 'I';
        tag.insert(tag.end(), end.begin(), end.end());
    }
    return tag;
}

mp3dec_frame_info_t mp3FirstFrameInfo(const std::vector<unsigned char>& bytes) {
    mp3dec_t decoder;
    mp3dec_frame_info_t info{};
    mp3dec_init(&decoder);
    REQUIRE(mp3dec_decode_frame(&decoder, bytes.data(), static_cast<int>(bytes.size()),
        nullptr, &info) > 0);
    REQUIRE(info.frame_offset >= 0);
    REQUIRE(info.frame_bytes > info.frame_offset + 4);
    REQUIRE(static_cast<size_t>(info.frame_bytes) < bytes.size());
    return info;
}

void mp3SetBits(std::vector<unsigned char>& bytes, size_t bit, unsigned count, unsigned value) {
    REQUIRE(bit + count <= bytes.size() * 8);
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char mask = static_cast<unsigned char>(1u << (7 - ((bit + i) % 8)));
        auto& byte = bytes[(bit + i) / 8];
        byte = static_cast<unsigned char>((byte & ~mask)
            | (((value >> (count - i - 1)) & 1u) ? mask : 0));
    }
}

void requireMp3PcmEquals(const std::vector<unsigned char>& bytes,
        const s16 *expected, u32 expectedSamples, s32 expectedRate) {
    const char *path = "fixture:framing.mp3";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    u32 samples = 0;
    s32 rate = 0;
    Decoded pcm(modMusicLoadAudioPcm22050(path, &samples, &rate), &SDL_free);
    REQUIRE(pcm != nullptr);
    REQUIRE(samples == expectedSamples);
    REQUIRE(rate == expectedRate);
    REQUIRE(std::equal(expected, expected + expectedSamples, pcm.get()));
}
}

TEST_CASE("production MP3 framing preserves standard tags padding and MPEG variants",
        "[audio][mp3][framing][T-ASSETS-046]") {
    AudioScope scope;
    for (const char *name : {"tone_mpeg1_stereo_44100.mp3", "tone_mpeg2_stereo_22050.mp3",
            "tone_mpeg2_mono_22050.mp3", "tone_mpeg25_mono_11025.mp3"}) {
        INFO(name);
        const auto bytes = mp3FramingFixture(name);
        testStubFsFileLoadWith("fixture:reference.mp3", bytes.data(), static_cast<u32>(bytes.size()));
        u32 samples = 0;
        s32 rate = 0;
        Decoded reference(modMusicLoadAudioPcm22050("fixture:reference.mp3", &samples, &rate), &SDL_free);
        REQUIRE(reference != nullptr);
        for (unsigned version : {2u, 3u, 4u}) {
            auto decorated = mp3Id3Prefix(version, version == 4);
            decorated.insert(decorated.end(), bytes.begin(), bytes.end());
            decorated.insert(decorated.end(), 17, 0); // extracted file alignment
            std::vector<unsigned char> id3v1(128, 0);
            id3v1[0] = 'T'; id3v1[1] = 'A'; id3v1[2] = 'G';
            decorated.insert(decorated.end(), id3v1.begin(), id3v1.end());
            requireMp3PcmEquals(decorated, reference.get(), samples, rate);
        }
        auto aligned = bytes;
        aligned.push_back(0);
        requireMp3PcmEquals(aligned, reference.get(), samples, rate);
        // The non-audio original flag is allowed to vary between frames.
        auto originalFlag = bytes;
        const auto first = mp3FirstFrameInfo(bytes);
        originalFlag[static_cast<size_t>(first.frame_offset) + 3] ^= 4;
        requireMp3PcmEquals(originalFlag, reference.get(), samples, rate);
        // ID3v2.4 permits appended tags with a matching footer, before ID3v1.
        for (bool withV1 : {false, true}) {
            auto appended = bytes;
            appended.insert(appended.end(), 3, 0);
            const auto tag = mp3Id3Prefix(4, true);
            appended.insert(appended.end(), tag.begin(), tag.end());
            if (withV1) {
                std::vector<unsigned char> v1(128, 0);
                v1[0] = 'T'; v1[1] = 'A'; v1[2] = 'G';
                appended.insert(appended.end(), v1.begin(), v1.end());
            }
            requireMp3PcmEquals(appended, reference.get(), samples, rate);
        }
        // A valid long title can contain TAG exactly 128 bytes before EOF.
        // Its checked v2.4 footer must outrank that incidental v1 signature.
        auto titleTag = mp3Id3Prefix(4, true);
        titleTag.insert(titleTag.end() - 10, 130, 'a');
        titleTag[8] = 1; titleTag[9] = 17; // 145-byte tag body, synchsafe
        titleTag[16] = 1; titleTag[17] = 7; // 135-byte title field, synchsafe
        std::copy(titleTag.begin() + 3, titleTag.begin() + 10, titleTag.end() - 7);
        std::copy_n("TAG", 3, titleTag.end() - 128);
        auto titled = bytes;
        titled.insert(titled.end(), titleTag.begin(), titleTag.end());
        requireMp3PcmEquals(titled, reference.get(), samples, rate);
    }
}

TEST_CASE("production MP3 loader distinguishes reservoir priming from corrupt side information",
        "[audio][mp3][framing][ownership][T-ASSETS-046]") {
    AudioScope scope;
    for (const char *name : {"tone_mpeg1_stereo_44100.mp3", "tone_mpeg2_stereo_22050.mp3",
            "tone_mpeg2_mono_22050.mp3", "tone_mpeg25_mono_11025.mp3"}) {
        INFO(name);
        const auto bytes = mp3FramingFixture(name);
        const auto first = mp3FirstFrameInfo(bytes);
        const std::vector<unsigned char> frame(bytes.begin() + first.frame_offset,
            bytes.begin() + first.frame_bytes);
        const bool mpeg1 = ((frame[1] >> 3) & 3) == 3;
        const size_t sideByte = 4 + ((frame[1] & 1) ? 0 : 2);
        const unsigned initialReservoir = mpeg1
            ? (static_cast<unsigned>(frame[sideByte]) << 1) | (frame[sideByte + 1] >> 7)
            : frame[sideByte];
        REQUIRE(initialReservoir == 0);
        auto primer = frame;
        mp3SetBits(primer, sideByte * 8, mpeg1 ? 9 : 8, mpeg1 ? 511 : 255);
        mp3dec_t decoder;
        mp3dec_frame_info_t info{};
        mp3d_sample_t upstreamPcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        mp3dec_init(&decoder);
        REQUIRE(mp3dec_decode_frame(&decoder, primer.data(), static_cast<int>(primer.size()),
            upstreamPcm, &info) == 0);
        REQUIRE(decoder.header[0] == 0xff);
        REQUIRE(info.frame_bytes == static_cast<int>(primer.size()));

        testStubFsFileLoadWith("fixture:reference.mp3", bytes.data(), static_cast<u32>(bytes.size()));
        u32 samples = 0;
        s32 rate = 0;
        Decoded reference(modMusicLoadAudioPcm22050("fixture:reference.mp3", &samples, &rate), &SDL_free);
        REQUIRE(reference != nullptr);
        auto primed = primer;
        primed.insert(primed.end(), bytes.begin() + first.frame_offset, bytes.end());
        requireMp3PcmEquals(primed, reference.get(), samples, rate);

        // big_values is a 9-bit field but 511 exceeds its Layer III limit of 288.
        auto corrupt = frame;
        const size_t prefixBits = mpeg1 ? (first.channels == 1 ? 18 : 20)
            : (first.channels == 1 ? 9 : 10);
        mp3SetBits(corrupt, sideByte * 8 + prefixBits + 12, 9, 511);
        mp3dec_init(&decoder);
        info = {};
        REQUIRE(mp3dec_decode_frame(&decoder, corrupt.data(), static_cast<int>(corrupt.size()),
            upstreamPcm, &info) == 0);
        REQUIRE(decoder.header[0] == 0);
        REQUIRE(info.frame_bytes == static_cast<int>(corrupt.size()));
        auto brokenTail = bytes;
        brokenTail.insert(brokenTail.end(), corrupt.begin(), corrupt.end());
        const int allocations = SDL_GetNumAllocations();
        testStubFsFileLoadWith("fixture:side-info.mp3", brokenTail.data(), static_cast<u32>(brokenTail.size()));
        samples = 999;
        rate = 999;
        Decoded rejected(modMusicLoadAudioPcm22050("fixture:side-info.mp3", &samples, &rate), &SDL_free);
        REQUIRE(rejected == nullptr);
        REQUIRE(samples == 0);
        REQUIRE(rate == 22050);
        REQUIRE(SDL_GetNumAllocations() == allocations);
    }
}

TEST_CASE("production MP3 framing rejects malformed appended ID3v2.4 tags",
        "[audio][mp3][framing][id3][T-ASSETS-046]") {
    AudioScope scope;
    const auto bytes = mp3FramingFixture("tone_mpeg2_stereo_22050.mp3");
    std::vector<std::vector<unsigned char>> bad;
    auto tag = mp3Id3Prefix(4, true);
    tag[4] ^= 1; // header/footer mismatch
    bad.push_back(tag);
    tag = mp3Id3Prefix(4, true);
    tag[tag.size() - 4] = 0x80; // not synchsafe
    bad.push_back(tag);
    tag = mp3Id3Prefix(4, true);
    std::fill(tag.end() - 4, tag.end(), 0x7f); // size extends before source
    bad.push_back(tag);
    tag = mp3Id3Prefix(4, true);
    tag[5] = tag[tag.size() - 5] = 0; // footer flag absent
    bad.push_back(tag);
    tag = mp3Id3Prefix(4, true);
    tag.erase(tag.begin(), tag.begin() + 10); // missing header
    bad.push_back(tag);
    tag = mp3Id3Prefix(4, true);
    tag.pop_back(); // truncated footer
    bad.push_back(tag);
    bad.push_back(mp3Id3Prefix(4)); // no footer
    bad.push_back(mp3Id3Prefix(3)); // only v2.4 defines appended footer
    for (size_t i = 0; i < bad.size(); ++i) {
        INFO("malformed appended ID3 case " << i);
        auto candidate = bytes;
        candidate.insert(candidate.end(), bad[i].begin(), bad[i].end());
        testStubFsFileLoadWith("fixture:bad-appended.mp3", candidate.data(), static_cast<u32>(candidate.size()));
        u32 samples = 999;
        Decoded pcm(modMusicLoadAudioPcm22050("fixture:bad-appended.mp3", &samples, nullptr), &SDL_free);
        REQUIRE(pcm == nullptr);
        REQUIRE(samples == 0);
    }
}

TEST_CASE("production MP3 framing rejects malformed tails and resynchronization gaps",
        "[audio][mp3][framing][ownership][T-ASSETS-046]") {
    AudioScope scope;
    for (const char *name : {"tone_mpeg1_stereo_44100.mp3", "tone_mpeg2_stereo_22050.mp3",
            "tone_mpeg2_mono_22050.mp3", "tone_mpeg25_mono_11025.mp3"}) {
        INFO(name);
        const auto bytes = mp3FramingFixture(name);
        const auto first = mp3FirstFrameInfo(bytes);
        std::vector<std::pair<std::string, std::vector<unsigned char>>> bad;
        auto changed = bytes;
        changed.push_back(1);
        bad.push_back({"nonzero trailer", changed});
        changed = bytes;
        changed.insert(changed.end(), {0,0,0,1});
        bad.push_back({"nonzero byte after alignment", changed});
        changed = bytes;
        changed.insert(changed.end(), bytes.begin() + first.frame_offset,
            bytes.begin() + first.frame_offset + 4);
        bad.push_back({"complete header without frame payload", changed});
        changed = bytes;
        changed.push_back(0xff);
        bad.push_back({"truncated frame header", changed});
        changed = bytes;
        changed.pop_back();
        bad.push_back({"truncated final real frame", changed});
        changed = bytes;
        changed.insert(changed.begin() + first.frame_bytes, 0x7f);
        bad.push_back({"junk between real frames", changed});
        changed = bytes;
        changed.insert(changed.begin() + first.frame_bytes, 0);
        bad.push_back({"zero gap before another frame", changed});
        changed = bytes;
        changed[static_cast<size_t>(first.frame_bytes)] = 0;
        bad.push_back({"destroyed second frame synchronization", changed});
        changed = bytes;
        changed.insert(changed.begin(), 1);
        bad.push_back({"unclassified leading byte", changed});
        changed = bytes;
        changed.insert(changed.end(), {'T','A','G'});
        bad.push_back({"truncated ID3v1 trailer", changed});
        changed = bytes;
        changed.insert(changed.end(), {'A','P','E','T','A','G','E','X'});
        bad.push_back({"unsupported APE trailer", changed});
        const int allocations = SDL_GetNumAllocations();
        for (const auto& item : bad) {
            INFO(item.first);
            testStubFsFileLoadWith("fixture:broken.mp3", item.second.data(),
                static_cast<u32>(item.second.size()));
            u32 samples = 999;
            s32 rate = 999;
            Decoded pcm(modMusicLoadAudioPcm22050("fixture:broken.mp3", &samples, &rate), &SDL_free);
            REQUIRE(pcm == nullptr);
            REQUIRE(samples == 0);
            REQUIRE(rate == 22050);
            REQUIRE(SDL_GetNumAllocations() == allocations);
        }
    }
}

TEST_CASE("production MP3 framing rejects malformed ID3 declarations before PCM publication",
        "[audio][mp3][framing][id3][T-ASSETS-046]") {
    AudioScope scope;
    const auto bytes = mp3FramingFixture("tone_mpeg2_stereo_22050.mp3");
    std::vector<std::vector<unsigned char>> prefixes;
    prefixes.push_back({'I','D','3'});
    auto tag = mp3Id3Prefix(3);
    tag[3] = 5;
    prefixes.push_back(tag);
    tag = mp3Id3Prefix(3);
    tag[4] = 255;
    prefixes.push_back(tag);
    tag = mp3Id3Prefix(3);
    tag[5] = 1;
    prefixes.push_back(tag);
    tag = mp3Id3Prefix(3);
    tag[6] = 0x80;
    prefixes.push_back(tag);
    tag = mp3Id3Prefix(3);
    tag[6] = tag[7] = tag[8] = tag[9] = 0x7f;
    prefixes.push_back(tag);
    tag = mp3Id3Prefix(4, true);
    tag.back() ^= 1;
    prefixes.push_back(tag);
    tag = mp3Id3Prefix(4, true);
    tag.resize(tag.size() - 10);
    prefixes.push_back(tag);
    for (size_t i = 0; i < prefixes.size(); ++i) {
        INFO("malformed ID3 case " << i);
        auto candidate = prefixes[i];
        candidate.insert(candidate.end(), bytes.begin(), bytes.end());
        testStubFsFileLoadWith("fixture:bad-id3.mp3", candidate.data(), static_cast<u32>(candidate.size()));
        u32 samples = 999;
        Decoded pcm(modMusicLoadAudioPcm22050("fixture:bad-id3.mp3", &samples, nullptr), &SDL_free);
        REQUIRE(pcm == nullptr);
        REQUIRE(samples == 0);
    }
}

TEST_CASE("production WAV conversion retains complete mono frames and SDL ownership",
        "[audio][wav][conversion][T-ASSETS-046]") {
    AudioScope scope;
    std::vector<unsigned char> bytes{'R','I','F','F'};
    const u32 frames = 8820;
    append32(bytes, 36 + frames * 2);
    const char format[] = "WAVEfmt ";
    bytes.insert(bytes.end(), format, format + 8);
    append32(bytes, 16);
    append16(bytes, 1);
    append16(bytes, 1);
    append32(bytes, 44100);
    append32(bytes, 44100 * 2);
    append16(bytes, 2);
    append16(bytes, 16);
    bytes.insert(bytes.end(), {'d','a','t','a'});
    append32(bytes, frames * 2);
    for (u32 i = 0; i < frames; ++i) {
        const s16 sample = static_cast<s16>(10000.0 * std::sin(i * 440.0 * 6.28318530718 / 44100.0));
        append16(bytes, static_cast<u16>(sample));
    }
    const char *path = "fixture:converted.wav";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    u32 samples = 0;
    s32 sourceRate = 0;
    Decoded pcm(modMusicLoadAudioPcm22050(path, &samples, &sourceRate), &SDL_free);
    REQUIRE(pcm != nullptr);
    REQUIRE(sourceRate == 44100);
    REQUIRE(samples / 2 >= 4406);
    REQUIRE(samples / 2 <= 4414);
    REQUIRE(rms(pcm.get(), samples / 2, 0, 2) > 1000.0);
    for (u32 i = 0; i < samples; i += 2) REQUIRE(pcm.get()[i] == pcm.get()[i + 1]);
}

TEST_CASE("production WAV decoder consumes standard stereo PCM24 and IEEE float32",
        "[audio][wav][conversion][T-ASSETS-046]") {
    AudioScope scope;
    const struct { u16 format; u16 bits; } cases[] = {{1, 24}, {3, 32}};
    const s16 values[] = {0, 8192, -16384, 24576, -8192, 16384};
    const u32 frames = 48;
    for (const auto& item : cases) {
        INFO("format=" << item.format << " bits=" << item.bits);
        const u16 frameBytes = 2 * (item.bits / 8);
        std::vector<unsigned char> bytes{'R', 'I', 'F', 'F'};
        append32(bytes, 0);
        bytes.insert(bytes.end(), {'W','A','V','E','f','m','t',' '});
        append32(bytes, item.format == 3 ? 18 : 16);
        append16(bytes, item.format);
        append16(bytes, 2);
        append32(bytes, 22050);
        append32(bytes, 22050 * frameBytes);
        append16(bytes, frameBytes);
        append16(bytes, item.bits);
        if (item.format == 3) {
            append16(bytes, 0); // WAVEFORMATEX cbSize
            bytes.insert(bytes.end(), {'f','a','c','t'});
            append32(bytes, 4);
            append32(bytes, frames);
        }
        bytes.insert(bytes.end(), {'d','a','t','a'});
        append32(bytes, frames * frameBytes);
        std::vector<s16> expected;
        for (u32 i = 0; i < frames * 2; ++i) {
            const s16 sample = values[i % 6];
            expected.push_back(sample);
            if (item.format == 1) {
                const u32 pcm24 = static_cast<u32>(static_cast<int32_t>(sample) * 256);
                bytes.push_back(static_cast<unsigned char>(pcm24));
                bytes.push_back(static_cast<unsigned char>(pcm24 >> 8));
                bytes.push_back(static_cast<unsigned char>(pcm24 >> 16));
            } else {
                const float value = static_cast<float>(sample) / 32768.0f;
                u32 bits;
                static_assert(sizeof(bits) == sizeof(value), "IEEE float32 fixture");
                std::memcpy(&bits, &value, sizeof(bits));
                append32(bytes, bits);
            }
        }
        const u32 riffSize = static_cast<u32>(bytes.size() - 8);
        for (unsigned i = 0; i < 4; ++i) bytes[4 + i] = static_cast<unsigned char>(riffSize >> (8 * i));
        const char *path = "fixture:standard-stereo.wav";
        testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
        u32 samples = 0;
        s32 sourceRate = 0;
        Decoded pcm(modMusicLoadAudioPcm22050(path, &samples, &sourceRate), &SDL_free);
        REQUIRE(pcm != nullptr);
        REQUIRE(samples == frames * 2);
        REQUIRE(sourceRate == 22050);
        for (u32 i = 0; i < samples; ++i) {
            // SDL may use 32767 or 32768 as the final float->S16 scale.
            REQUIRE(std::abs(static_cast<int>(pcm.get()[i]) - expected[i]) <= 1);
        }
        REQUIRE(pcm.get()[0] != pcm.get()[1]);
        REQUIRE(pcm.get()[samples - 2] != pcm.get()[samples - 1]);
    }
}


TEST_CASE("one-shot production PCM keeps stereo frames pan and existing volume layers",
        "[audio][oneshot][wav][T-ASSETS-046]") {
    AudioScope scope;
    const std::vector<s16> expected{8000,-12000,16000,24000,-30000,30000};
    auto bytes = wav(expected);
    const char *path = "fixture:oneshot.wav";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    u32 samples = 0;
    Decoded left(audioPrepareFileOneShotPcm(path, 0x7fff, 0, 1.0f,
        0.5f, 0.5f, &samples), &SDL_free);
    REQUIRE(left != nullptr);
    REQUIRE(samples == expected.size());
    for (u32 i = 0; i < samples; i += 2) {
        REQUIRE(left.get()[i] == expected[i] / 2);
        REQUIRE(left.get()[i + 1] == 0);
    }
    Decoded right(audioPrepareFileOneShotPcm(path, 0x7fff, 127, 1.0f,
        1.0f, 1.0f, &samples), &SDL_free);
    REQUIRE(right != nullptr);
    REQUIRE(samples == expected.size());
    for (u32 i = 0; i < samples; i += 2) {
        REQUIRE(right.get()[i] == 0);
        REQUIRE(right.get()[i + 1] == expected[i + 1]);
    }
    Decoded muted(audioPrepareFileOneShotPcm(path, 0, 64, 1.0f,
        1.0f, 1.0f, &samples), &SDL_free);
    REQUIRE(muted != nullptr);
    for (u32 i = 0; i < samples; ++i) REQUIRE(muted.get()[i] == 0);
}

TEST_CASE("one-shot production pitch bounds reject nonfinite controls and preserve ownership",
        "[audio][oneshot][wav][ownership][T-ASSETS-046]") {
    AudioScope scope;
    std::vector<s16> wave(8820, 8000);
    auto bytes = wav(wave);
    const char *path = "fixture:pitch.wav";
    testStubFsFileLoadWith(path, bytes.data(), static_cast<u32>(bytes.size()));
    u32 samples = 0;
    Decoded warm(audioPrepareFileOneShotPcm(path, 0x7fff, 64, 2.0f,
        1.0f, 1.0f, &samples), &SDL_free);
    REQUIRE(warm != nullptr);
    REQUIRE(samples / 2 >= 2203);
    REQUIRE(samples / 2 <= 2207);
    warm.reset();
    const int allocations = SDL_GetNumAllocations();
    for (float pitch : {std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()}) {
        samples = 999;
        Decoded pcm(audioPrepareFileOneShotPcm(path, 0x7fff, 64, pitch,
            1.0f, 1.0f, &samples), &SDL_free);
        REQUIRE(pcm == nullptr);
        REQUIRE(samples == 0);
        REQUIRE(SDL_GetNumAllocations() == allocations);
    }
    for (float pitch : {0.0f, -1.0f, 0.5f, 2.0f, std::numeric_limits<float>::max()}) {
        Decoded pcm(audioPrepareFileOneShotPcm(path, 0x7fff, 64, pitch,
            1.0f, 1.0f, &samples), &SDL_free);
        REQUIRE(pcm != nullptr);
        REQUIRE(samples > 0);
        REQUIRE(samples % 2 == 0);
        if (pitch <= 0.0f) REQUIRE(samples == wave.size());
        if (pitch == 0.5f) {
            REQUIRE(samples / 2 >= 8818);
            REQUIRE(samples / 2 <= 8822);
        }
        pcm.reset();
        REQUIRE(SDL_GetNumAllocations() == allocations);
    }
    samples = 999;
    Decoded invalidGain(audioPrepareFileOneShotPcm(path, 0x7fff, 64, 1.0f,
        std::numeric_limits<float>::quiet_NaN(), 1.0f, &samples), &SDL_free);
    REQUIRE(invalidGain == nullptr);
    REQUIRE(samples == 0);
}

TEST_CASE("public audio duration covers final frames without changing active playback",
        "[audio][duration][wav][ownership][T-ASSETS-046]") {
    AudioScope scope;
    auto playing = wav(std::vector<s16>(44100, 1000));
    testStubFsFileLoadWith("fixture:playing.wav", playing.data(), static_cast<u32>(playing.size()));
    modMusicPlay("fixture:playing.wav");
    REQUIRE(modMusicIsPlaying() == 1);
    modMusicSetPositionMs(125);
    const u32 position = modMusicGetPositionMs();
    const unsigned restores = restoreCount, mutes = muteCount;
    for (const auto& item : std::vector<std::pair<unsigned, int>>{{1, 1}, {22050, 60}, {22051, 61}}) {
        auto bytes = wav(std::vector<s16>(item.first * 2, 1234));
        testStubFsFileLoadWith("fixture:duration.wav", bytes.data(), static_cast<u32>(bytes.size()));
        REQUIRE(modMusicAudioSourceDuration60("fixture:duration.wav") == item.second);
        REQUIRE(modMusicIsPlaying() == 1);
        REQUIRE(modMusicGetPositionMs() == position);
        REQUIRE(restoreCount == restores);
        REQUIRE(muteCount == mutes);
    }
    const unsigned char invalid[] = {'x', 'y', 'z'};
    testStubFsFileLoadWith("fixture:invalid.ogg", invalid, sizeof(invalid));
    REQUIRE(modMusicAudioSourceDuration60("fixture:invalid.ogg") == -1);
    REQUIRE(modMusicIsPlaying() == 1);
    REQUIRE(modMusicGetPositionMs() == position);
}

TEST_CASE("public compressed audio duration follows decoded frames across source rates",
        "[audio][duration][mp3][vorbis][T-ASSETS-046]") {
    AudioScope scope;
    const auto ogg = readFixture("tone_stereo_22050.ogg");
    testStubFsFileLoadWith("fixture:duration.ogg", ogg.data(), static_cast<u32>(ogg.size()));
    REQUIRE(modMusicAudioSourceDuration60("fixture:duration.ogg") == 12);
    for (const char *name : {"tone_mpeg1_stereo_44100.mp3", "tone_mpeg2_stereo_22050.mp3",
            "tone_mpeg2_mono_22050.mp3", "tone_mpeg25_mono_11025.mp3"}) {
        INFO(name);
        std::ifstream input(std::string(PD_MP3_FIXTURE_DIR) + "/" + name, std::ios::binary);
        REQUIRE(input.good());
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        testStubFsFileLoadWith("fixture:duration.mp3", bytes.data(), static_cast<u32>(bytes.size()));
        const s32 duration = modMusicAudioSourceDuration60("fixture:duration.mp3");
        // Original0.2s signal plus MPEG frame padding and encoder delay; a
        // fixed24kbps byte estimate violates this interval for these fixtures.
        REQUIRE(duration >= 12);
        REQUIRE(duration <= 20);
    }
}

TEST_CASE("one-shot production queue consumes WAV MP3 and Vorbis and reports SDL errors",
        "[audio][oneshot][mp3][vorbis][queue][T-ASSETS-046]") {
    AudioScope scope;
    // Isolated dummy output proves queue copying, not hardware or audible output.
    REQUIRE((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0);
    struct DeviceScope {
        bool initialized = false;
        SDL_AudioDeviceID device = 0;
        ~DeviceScope() {
            if (device) SDL_CloseAudioDevice(device);
            if (initialized) SDL_AudioQuit();
        }
    } device;
    REQUIRE(SDL_AudioInit("dummy") == 0);
    device.initialized = true;
    SDL_AudioSpec wanted = {};
    wanted.freq = 22050;
    wanted.channels = 2;
    wanted.format = AUDIO_S16SYS;
    wanted.samples = 512;
    device.device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    REQUIRE(device.device != 0); // Device remains paused, so queue length is exact.
    std::ifstream input(std::string(PD_MP3_FIXTURE_DIR) + "/tone_mpeg25_mono_11025.mp3",
        std::ios::binary);
    REQUIRE(input.good());
    std::vector<unsigned char> mp3{std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const std::vector<std::pair<std::string, std::vector<unsigned char>>> sources = {
        {"fixture:queue.wav", wav({1234, -2345, 3456, -4567})},
        {"fixture:queue.mp3", mp3},
        {"fixture:queue.ogg", readFixture("tone_stereo_22050.ogg")},
    };
    for (const auto& source : sources) {
        INFO(source.first);
        testStubFsFileLoadWith(source.first.c_str(), source.second.data(),
            static_cast<u32>(source.second.size()));
        u32 samples = 0;
        Decoded expected(audioPrepareFileOneShotPcm(source.first.c_str(), 0x7fff,
            64, 1.0f, 1.0f, 1.0f, &samples), &SDL_free);
        REQUIRE(expected != nullptr);
        expected.reset();
        SDL_ClearQueuedAudio(device.device);
        REQUIRE(audioQueueFileOneShot(device.device, source.first.c_str(), 0x7fff,
            64, 1.0f, 1.0f, 1.0f) == 1);
        REQUIRE(SDL_GetQueuedAudioSize(device.device) == samples * sizeof(s16));
        SDL_ClearQueuedAudio(device.device);
        // Warm SDL's per-thread error storage before the ownership observation.
        REQUIRE(audioQueueFileOneShot(0, source.first.c_str(), 0x7fff,
            64, 1.0f, 1.0f, 1.0f) == 0);
        const int allocations = SDL_GetNumAllocations();
        REQUIRE(audioQueueFileOneShot(0, source.first.c_str(), 0x7fff,
            64, 1.0f, 1.0f, 1.0f) == 0);
        REQUIRE(SDL_GetNumAllocations() == allocations);
        REQUIRE(SDL_GetQueuedAudioSize(device.device) == 0);
    }
}

TEST_CASE("immutable audio snapshots decode WAV MP3 and Vorbis independently of provider changes",
        "[audio][snapshot][ownership]") {
    AudioScope scope;
    std::ifstream input(std::string(PD_MP3_FIXTURE_DIR) + "/tone_mpeg25_mono_11025.mp3", std::ios::binary);
    REQUIRE(input.good());
    std::vector<unsigned char> mp3{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<std::pair<std::string, std::vector<unsigned char>>> sources = {
        {"snapshot:retained.wav", wav({1234, -2345, 3456, -4567})},
        {"snapshot:retained.mp3", mp3},
        {"snapshot:retained.ogg", readFixture("tone_stereo_44100.ogg")},
    };
    for (auto& source : sources) {
        INFO(source.first);
        u32 expected_samples = 0, samples = 0;
        s32 expected_rate = 0, rate = 0;
        testStubFsFileLoadWith(source.first.c_str(), source.second.data(), static_cast<u32>(source.second.size()));
        Decoded expected(modMusicLoadAudioPcm22050(source.first.c_str(), &expected_samples, &expected_rate), &SDL_free);
        REQUIRE(expected != nullptr);
        testStubFsFileLoadWith(nullptr, nullptr, 0);
        Decoded retained(modMusicDecodeAudioPcm22050(source.first.c_str(), source.second.data(),
            static_cast<u32>(source.second.size()), &samples, &rate), &SDL_free);
        REQUIRE(retained != nullptr);
        REQUIRE(samples == expected_samples);
        REQUIRE(rate == expected_rate);
        REQUIRE(std::equal(retained.get(), retained.get() + samples, expected.get()));
        std::fill(source.second.begin(), source.second.end(), 0);
        REQUIRE(std::equal(retained.get(), retained.get() + samples, expected.get()));
    }
}

TEST_CASE("invalid audio snapshot never falls back to a valid selected provider or disk file",
        "[audio][snapshot][rejection]") {
    AudioScope scope;
    const std::vector<unsigned char> bad{0, 1, 2, 3};
    const auto good = wav({1000, -1000, 2000, -2000});
    testStubFsFileLoadWith("snapshot:invalid.wav", good.data(), static_cast<u32>(good.size()));
    for (const auto& name : {std::string("snapshot:invalid.wav"),
            std::string(PD_AUDIO_FIXTURE_DIR) + "/tone_mono_22050.ogg",
            std::string(PD_MP3_FIXTURE_DIR) + "/tone_mpeg25_mono_11025.mp3"}) {
        INFO(name);
        u32 samples = 99;
        s32 rate = -1;
        Decoded rejected(modMusicDecodeAudioPcm22050(name.c_str(), bad.data(),
            static_cast<u32>(bad.size()), &samples, &rate), &SDL_free);
        REQUIRE(rejected == nullptr);
        REQUIRE(samples == 0);
        REQUIRE(rate == 22050);
    }
    u32 samples = 99;
    REQUIRE(modMusicDecodeAudioPcm22050("snapshot:invalid.wav", nullptr, 4, &samples, nullptr) == nullptr);
    REQUIRE(samples == 0);
    REQUIRE(modMusicDecodeAudioPcm22050("snapshot:invalid.wav", good.data(), 0, &samples, nullptr) == nullptr);
    REQUIRE(modMusicDecodeAudioPcm22050("snapshot:invalid.wav", good.data(),
        std::numeric_limits<u32>::max(), &samples, nullptr) == nullptr);
    REQUIRE(modMusicDecodeAudioPcm22050("snapshot:invalid.wav", good.data(),
        static_cast<u32>(good.size()), nullptr, nullptr) == nullptr);
}
