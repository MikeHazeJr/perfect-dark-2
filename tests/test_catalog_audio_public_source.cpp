#include "catch.hpp"
#include <cstring>
#include <memory>
#include <string>
#include "catalog_audio_public_source.h"

namespace {
void pair(ini_section_t &ini, const char *key, const char *value)
{
    REQUIRE(ini.count < INI_MAX_PAIRS);
    REQUIRE(std::strlen(key) < sizeof(ini.pairs[0].key));
    REQUIRE(std::strlen(value) < sizeof(ini.pairs[0].value));
    std::strcpy(ini.pairs[ini.count].key, key);
    std::strcpy(ini.pairs[ini.count++].value, value);
}
ini_section_t source()
{
    ini_section_t ini = {};
    std::strcpy(ini.type, "sound");
    pair(ini, "file_path", "sample.ogg");
    return ini;
}
}

TEST_CASE("public audio candidate owns source controls but never native identity",
        "[audio][source][T-ASSETS-046]")
{
    auto ini = source();
    pair(ini, "format", "stale-private-echo");
    pair(ini, "sample_rate_hz", "wrong-old-rate");
    pair(ini, "decoded_sample_count", "1");
    pair(ini, "source_soundnum", "1");
    pair(ini, "source_filenum", "2");
    catalog_audio_public_source_t parsed;
    REQUIRE(catalogAudioPublicSourceParse(&ini, 1, &parsed) == 1);
    REQUIRE(std::string(parsed.file_path) == "sample.ogg");
    REQUIRE(parsed.key_base == 60);
    REQUIRE(parsed.key_max == 127);
    REQUIRE(parsed.sample_pan == 64);
    REQUIRE(parsed.sample_volume == 127);
    REQUIRE(parsed.has_keymap == 0);
    REQUIRE(parsed.has_loop == 0);
    REQUIRE(parsed.has_envelope == 0);
    auto row = std::make_unique<asset_entry_t>();
    row->type = ASSET_AUDIO;
    row->source_soundnum = 32808;
    row->source_filenum = 1914;
    row->ext.audio.sound_id = 32808;
    row->ext.audio.category = AUDIO_CAT_VOICE;
    catalogAudioPublicSourceApply(row.get(), &parsed);
    REQUIRE(std::string(row->ext.audio.file_path) == "sample.ogg");
    REQUIRE(row->source_soundnum == 32808);
    REQUIRE(row->source_filenum == 1914);
    REQUIRE(row->ext.audio.sound_id == 32808);
    REQUIRE(row->ext.audio.category == AUDIO_CAT_VOICE);
}

TEST_CASE("public audio controls retain native unsigned sentinels and exact values",
        "[audio][source][T-ASSETS-046]")
{
    auto ini = source();
    pair(ini, "key_base", "57");
    pair(ini, "key_detune", "-12");
    pair(ini, "sample_pan", "31");
    pair(ini, "sample_volume", "92");
    pair(ini, "loop_start_samples", "3");
    pair(ini, "loop_end_samples", "20");
    pair(ini, "loop_count", "4294967295");
    pair(ini, "decay_time_us", "-1");
    pair(ini, "attack_volume", "120");
    pair(ini, "decay_volume", "80");
    catalog_audio_public_source_t parsed;
    REQUIRE(catalogAudioPublicSourceParse(&ini, 1, &parsed) == 1);
    REQUIRE(parsed.has_keymap == 1);
    REQUIRE(parsed.key_base == 57);
    REQUIRE(parsed.key_detune == -12);
    REQUIRE(parsed.sample_pan == 31);
    REQUIRE(parsed.sample_volume == 92);
    REQUIRE(parsed.has_loop == 1);
    REQUIRE(parsed.loop_start_samples == 3);
    REQUIRE(parsed.loop_end_samples == 20);
    REQUIRE(parsed.loop_count == 0xffffffffu);
    REQUIRE(parsed.decay_time_us == 0xffffffffu);
    REQUIRE(parsed.has_envelope == 1);
    REQUIRE(parsed.attack_volume == 120);
    REQUIRE(parsed.decay_volume == 80);
}

TEST_CASE("public audio malformed candidate clears outputs without touching a live row",
        "[audio][source][T-ASSETS-046]")
{
    const struct { const char *key; const char *value; } invalid[] = {
        {"key_base", "60tail"}, {"key_base", "1.5"}, {"key_detune", "-129"},
        {"sample_pan", "128"}, {"loop_count", "4294967296"},
        {"key_min", "256"}, {"key_min", "-1"}, {"key_max", "256"},
        {"velocity_min", "256"}, {"velocity_max", "-1"}, {"velocity_max", "256"},
        {"loop_count", "-2"}, {"has_loop", "true"}, {"has_envelope", "unknown"},
        {"file_path", "../sample.wav"}, {"key_base", ""},
    };
    for (const auto &item : invalid) {
        INFO(item.key << '=' << item.value);
        auto ini = source();
        if (!std::strcmp(item.key, "file_path")) ini.count = 0;
        pair(ini, item.key, item.value);
        catalog_audio_public_source_t parsed;
        std::memset(&parsed, 0x55, sizeof(parsed));
        REQUIRE(catalogAudioPublicSourceParse(&ini, 1, &parsed) == 0);
        catalog_audio_public_source_t empty = {};
        REQUIRE(std::memcmp(&parsed, &empty, sizeof(parsed)) == 0);
    }
    auto duplicate = source();
    pair(duplicate, "file_path", "sample.mp3");
    catalog_audio_public_source_t parsed;
    REQUIRE(catalogAudioPublicSourceParse(&duplicate, 1, &parsed) == 0);
    auto oversized = source();
    pair(oversized, "actor", std::string(64, 'a').c_str());
    REQUIRE(catalogAudioPublicSourceParse(&oversized, 1, &parsed) == 0);
}

TEST_CASE("public sample keymap preserves packed native bytes without MIDI range rules",
        "[audio][source][T-ASSETS-046]")
{
    /* Actual extracted cases: alarm_airbase, argh_female_000d, and the
     * explosion-profile dependency sfx_unlabeled_avrm. The all-255 row proves
     * four byte boundaries independently of the current base corpus. */
    const int controls[][4] = {{8, 4, 0, 0}, {4, 0, 0, 0}, {5, 0, 176, 16},
        {255, 255, 255, 255}, {0, 0, 0, 215}};
    for (const auto &control : controls) {
        auto ini = source();
        pair(ini, "key_min", std::to_string(control[0]).c_str());
        pair(ini, "key_max", std::to_string(control[1]).c_str());
        pair(ini, "velocity_min", std::to_string(control[2]).c_str());
        pair(ini, "velocity_max", std::to_string(control[3]).c_str());
        catalog_audio_public_source_t parsed;
        REQUIRE(catalogAudioPublicSourceParse(&ini, 1, &parsed) == 1);
        auto row = std::make_unique<asset_entry_t>();
        row->type = ASSET_AUDIO;
        row->source_soundnum = 174;
        catalogAudioPublicSourceApply(row.get(), &parsed);
        REQUIRE(row->ext.audio.key_min == control[0]);
        REQUIRE(row->ext.audio.key_max == control[1]);
        REQUIRE(row->ext.audio.velocity_min == control[2]);
        REQUIRE(row->ext.audio.velocity_max == control[3]);
        REQUIRE(row->source_soundnum == 174);
    }
}

TEST_CASE("public voice metadata and declared companions share the source candidate",
        "[audio][source][voice][T-ASSETS-046]")
{
    auto ini = source();
    pair(ini, "actor", "Edited actor");
    pair(ini, "transcript", "Public words");
    pair(ini, "language", "en");
    pair(ini, "context", "greeting");
    pair(ini, "subtitle_file", "archive.pdvoice::subtitle.json");
    pair(ini, "locale_fr_file", "archive.pdvoice::locales/fr.ogg");
    pair(ini, "fallback_locale", "en");
    catalog_audio_public_source_t parsed;
    REQUIRE(catalogAudioPublicSourceParse(&ini, 1, &parsed) == 1);
    auto row = std::make_unique<asset_entry_t>();
    row->type = ASSET_AUDIO;
    catalogAudioPublicSourceApply(row.get(), &parsed);
    REQUIRE(std::string(row->ext.audio.voice_actor) == "Edited actor");
    REQUIRE(std::string(row->ext.audio.voice_transcript) == "Public words");
    REQUIRE(std::string(row->ext.audio.subtitle_file) == "archive.pdvoice::subtitle.json");
    REQUIRE(std::string(row->ext.audio.locale_audio_files[1]) == "archive.pdvoice::locales/fr.ogg");
}

TEST_CASE("private native audio identity uses complete strict JSON and bounded integers",
        "[audio][source][identity][T-ASSETS-046]")
{
    s32 soundnum = 0, filenum = 0;
    const std::string valid = R"({"source_ind\u0065x":32808,"source_filenum":1914})";
    REQUIRE(catalogAudioNativeIdentityParse(valid.data(), valid.size(), &soundnum, &filenum) == 1);
    REQUIRE(soundnum == 32808);
    REQUIRE(filenum == 1914);
    for (const char *invalid : {"{}", "{\"source_index\":1}junk",
            "{\"source_index\":1,\"source_index\":2}", "{\"source_index\":1.5}",
            "{\"source_index\":65536}", "{\"source_index\":NaN}",
            "{\"source_index\":1e99}", "{\"source_filenum\":0}",
            "{\"source_index\":1,\"source_filenum\":2048}"}) {
        REQUIRE(catalogAudioNativeIdentityParse(invalid, std::strlen(invalid), &soundnum, &filenum) == 0);
        REQUIRE(soundnum == -1);
        REQUIRE(filenum == -1);
    }
}
