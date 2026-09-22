#include "catch.hpp"
#include <cstring>
#include <cstdio>

extern "C" {
#include "catalog_audio_source_identity.h"
#include "assetcatalog_sound_slots.h"
#include "catalog_activation_ledger.h"
#include "constants.h"
}

namespace {
asset_entry_t audioRow(const char *id, s32 category)
{
    asset_entry_t row{};
    std::strcpy(row.id, id);
    row.type = ASSET_AUDIO;
    row.occupied = row.enabled = 1;
    row.ext.audio.category = category;
    row.ext.audio.sound_id = row.source_soundnum = row.source_filenum = -1;
    return row;
}
}

TEST_CASE("audio source identity survives same-ID row reset and chained replacement",
    "[catalog][audio][source-identity][t-assets-046]")
{
    auto row = audioRow("base:voice_line", AUDIO_CAT_VOICE);
    row.ext.audio.sound_id = 48;
    row.source_soundnum = 48;
    row.source_filenum = 380;
    row.bundled = 1;
    for (int i = 0; i < 3; ++i) {
        catalog_audio_source_identity_t identity{};
        REQUIRE(catalogAudioSourceIdentityPrepare(&row, row.id,
            AUDIO_CAT_VOICE, &identity));
        row = audioRow("base:voice_line", AUDIO_CAT_SFX);
        REQUIRE(catalogAudioSourceIdentityApply(&row, &identity));
        REQUIRE(row.ext.audio.sound_id == 48);
        REQUIRE(row.source_soundnum == 48);
        REQUIRE(row.source_filenum == 380);
        REQUIRE(row.ext.audio.category == AUDIO_CAT_VOICE);
        REQUIRE(row.bundled == 0);
    }
}

TEST_CASE("audio source identity preserves file-only MP3 and track domains exactly",
    "[catalog][audio][source-identity][t-assets-046]")
{
    auto row = audioRow("base:direct_mp3", AUDIO_CAT_VOICE);
    row.source_filenum = 350;
    catalog_audio_source_identity_t identity{};
    REQUIRE(catalogAudioSourceIdentityPrepare(&row, row.id, AUDIO_CAT_SFX, &identity));
    REQUIRE(identity.sound_id == -1);
    REQUIRE(identity.source_soundnum == -1);
    REQUIRE(identity.source_filenum == 350);
    row = audioRow("base:track", AUDIO_CAT_MUSIC);
    row.ext.audio.sound_id = 7;
    REQUIRE(catalogAudioSourceIdentityPrepare(&row, row.id, AUDIO_CAT_MUSIC, &identity));
    REQUIRE(identity.sound_id == 7);
    REQUIRE(identity.source_soundnum == -1);
    REQUIRE(identity.source_filenum == -1);
}

TEST_CASE("audio source identity preserves packed MP3 configuration provenance",
    "[catalog][audio][source-identity][t-assets-046]")
{
    auto row = audioRow("base:sfx_carr_hello_joanna", AUDIO_CAT_VOICE);
    row.ext.audio.sound_id = row.source_soundnum = 32808;
    row.source_filenum = 1914;
    catalog_audio_source_identity_t identity{};
    REQUIRE(catalogAudioSourceIdentityPrepare(&row, row.id, AUDIO_CAT_VOICE, &identity));
    row = audioRow("base:sfx_carr_hello_joanna", AUDIO_CAT_VOICE);
    REQUIRE(catalogAudioSourceIdentityApply(&row, &identity));
    REQUIRE(row.ext.audio.sound_id == 32808);
    REQUIRE(row.source_soundnum == 32808);
    REQUIRE(row.source_filenum == 1914);
}

TEST_CASE("audio identity collision rejection does not mutate live prior state",
    "[catalog][audio][source-identity][t-assets-046]")
{
    auto row = audioRow("base:voice_line", AUDIO_CAT_VOICE);
    row.ext.audio.sound_id = row.source_soundnum = 48;
    row.source_filenum = 380;
    row.load_state = ASSET_STATE_ACTIVE;
    row.loaded_data = reinterpret_cast<void *>(0x1234);
    row.ref_count = 2;
    row.stage_ref_count = 1;
    const auto original = row;
    catalog_audio_source_identity_t identity{};
    REQUIRE_FALSE(catalogAudioSourceIdentityPrepare(&row, row.id, AUDIO_CAT_MUSIC, &identity));
    REQUIRE(std::memcmp(&row, &original, sizeof(row)) == 0);
    REQUIRE(identity.id[0] == '\0');
    REQUIRE(identity.source_filenum == -1);
    REQUIRE_FALSE(catalogAudioSourceIdentityPrepare(&row, "mod:different", AUDIO_CAT_VOICE, &identity));
    REQUIRE(std::memcmp(&row, &original, sizeof(row)) == 0);
    row.type = ASSET_TEXTURE;
    REQUIRE_FALSE(catalogAudioSourceIdentityPrepare(&row, row.id, AUDIO_CAT_VOICE, &identity));
}

TEST_CASE("audio source identity new rows remain unbound and private slots stay stable",
    "[catalog][audio][source-identity][t-assets-046]")
{
    assetCatalogResetCustomSoundSlots();
    catalog_audio_source_identity_t identity{};
    REQUIRE(catalogAudioSourceIdentityPrepare(nullptr, "mod:zap", AUDIO_CAT_SFX, &identity));
    REQUIRE(identity.sound_id == -1);
    REQUIRE(identity.source_soundnum == -1);
    REQUIRE(identity.source_filenum == -1);
    auto row = audioRow("mod:zap", AUDIO_CAT_SFX);
    REQUIRE(catalogAudioSourceIdentityApply(&row, &identity));
    row.ext.audio.sound_id = row.source_soundnum = assetCatalogResolveSoundPrivateSlot(row.id);
    REQUIRE(row.source_soundnum == SND_CUSTOM_START);
    REQUIRE(catalogAudioSourceIdentityPrepare(&row, row.id, AUDIO_CAT_VOICE, &identity));
    row = audioRow("mod:zap", AUDIO_CAT_VOICE);
    REQUIRE(catalogAudioSourceIdentityApply(&row, &identity));
    REQUIRE(row.source_soundnum == SND_CUSTOM_START);
    REQUIRE(assetCatalogResolveSoundPrivateSlot("mod:next") == SND_CUSTOM_START + 1);
    assetCatalogResetCustomSoundSlots();
}

TEST_CASE("audio identity invalid inputs and publication mismatch fail without changes",
    "[catalog][audio][source-identity][t-assets-046]")
{
    catalog_audio_source_identity_t identity{};
    char unterminated[CATALOG_ID_LEN];
    std::memset(unterminated, 'x', sizeof(unterminated));
    REQUIRE_FALSE(catalogAudioSourceIdentityPrepare(nullptr, nullptr, AUDIO_CAT_SFX, &identity));
    REQUIRE_FALSE(catalogAudioSourceIdentityPrepare(nullptr, "", AUDIO_CAT_SFX, &identity));
    REQUIRE_FALSE(catalogAudioSourceIdentityPrepare(nullptr, unterminated, AUDIO_CAT_SFX, &identity));
    REQUIRE_FALSE(catalogAudioSourceIdentityPrepare(nullptr, "mod:a", 99, &identity));
    REQUIRE_FALSE(catalogAudioSourceIdentityPrepare(nullptr, "mod:a", AUDIO_CAT_SFX, nullptr));
    REQUIRE(catalogAudioSourceIdentityPrepare(nullptr, "mod:a", AUDIO_CAT_SFX, &identity));
    auto row = audioRow("mod:b", AUDIO_CAT_SFX);
    const auto original = row;
    REQUIRE_FALSE(catalogAudioSourceIdentityApply(&row, &identity));
    REQUIRE(std::memcmp(&row, &original, sizeof(row)) == 0);
}

TEST_CASE("audio retired-snapshot rollback preserves reverse identity without stale payloads",
    "[catalog][audio][source-identity][t-assets-046]")
{
    auto original = audioRow("base:voice_line", AUDIO_CAT_VOICE);
    original.ext.audio.sound_id = original.source_soundnum = 48;
    original.source_filenum = 380;
    original.load_state = ASSET_STATE_ACTIVE;
    original.loaded_data = reinterpret_cast<void *>(0x1234);
    original.ref_count = 2;
    original.stage_ref_count = 1;
    auto row = audioRow("base:voice_line", AUDIO_CAT_VOICE);
    catalogActivationLedgerRestoreRetiredSnapshot(&row, &original);
    REQUIRE(row.ext.audio.sound_id == 48);
    REQUIRE(row.source_soundnum == 48);
    REQUIRE(row.source_filenum == 380);
    REQUIRE(row.loaded_data == nullptr);
    REQUIRE(row.ref_count == 0);
    REQUIRE(row.stage_ref_count == 0);
}
