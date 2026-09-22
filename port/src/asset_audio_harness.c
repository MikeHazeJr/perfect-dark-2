#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "asset_source_harness.h"
#include "asset_archive_writer.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "audio.h"
#include "fs.h"
#include "modmusic.h"
#include "smoke_harness.h"
#include "system.h"

/* This smoke starts at a real editable typed archive, then uses the catalog
 * synchronized playlist route used by gameplay. It opens no device; PCM is checked
 * independently against the same tone decoded directly from its loose file. */
int assetSourceAudioHarnessRun(void)
{
    const char *id = "smoke_vorbis:synthetic_tone";
    const char *fixture = "asset-source-tone.ogg";
    const char *descriptor = "[music]\ncatalog_id = smoke_vorbis:synthetic_tone\n"
        "name = Synthetic Vorbis source\naudio_category = music\n"
        "file_path = track.ogg\n";
    const char *manifest = "{\"pd_kind\":\"song\",\"pd_schema_version\":1,"
        "\"id\":\"smoke_vorbis:synthetic_tone\",\"data\":\"track.ogg\"}";
    char folder[FS_MAXPATH + 1], archive_path[FS_MAXPATH + 1];
    u32 encoded_size = 0, direct_count = 0, nested_count = 0;
    s32 direct_rate = 0, nested_rate = 0;
    void *encoded = NULL;
    s16 *direct = NULL, *nested = NULL, *mixed = NULL;
    mod_archive_writer_t *archive = NULL;
    asset_archive_writer_t *writer = NULL;
    catalog_audio_result_t resolved = {0};
    int archive_ok = 0, catalog_ok = 0, decode_ok = 0, playback_ok = 0;
    f32 master = audioGetMasterVolume(), music = audioGetMusicVolume();
    f32 volume = modMusicGetVolume();
    if (!smokeHarnessIsActive()) return -1;
    if (!fsCreateDir("$S/asset-audio-smoke")) goto done;
    fsFullPath("$S/asset-audio-smoke", folder, sizeof(folder));
    fsFullPath("$S/asset-audio-smoke/source.pdsong", archive_path, sizeof(archive_path));
    encoded = fsFileLoad(fixture, &encoded_size);
    if (!encoded || !encoded_size) goto done;
    direct = modMusicLoadAudioPcm22050(fixture, &direct_count, &direct_rate);
    if (!direct || direct_count != 8820 || direct_rate != 22050) goto done;
    writer = calloc(1, sizeof(*writer));
    archive = modArchiveBegin(archive_path);
    if (!writer || !archive) goto done;
    if (assetArchiveWriterInit(writer, archive, "song", id) != MODARCHIVE_OK
            || assetArchiveWriterAddDescriptor(writer, "music.ini", descriptor,
                (u32)strlen(descriptor)) != MODARCHIVE_OK
            || assetArchiveWriterAddManifestJson(writer, manifest,
                (u32)strlen(manifest)) != MODARCHIVE_OK
            || assetArchiveWriterAddPublicMem(writer, "track.ogg", encoded,
                encoded_size, "audio") != MODARCHIVE_OK
            || assetArchiveWriterFinishMetadata(writer) != MODARCHIVE_OK) goto done;
    archive_ok = modArchiveFinish(archive) == MODARCHIVE_OK;
    archive = NULL;
    if (!archive_ok) goto done;
    catalog_ok = assetCatalogScanExternalLayoutFolder("smoke_vorbis", folder) == 1
        && catalogResolveAudio(id, &resolved)
        && resolved.category == AUDIO_CAT_MUSIC && resolved.file_path
        && strstr(resolved.file_path, "source.pdsong::track.ogg") != NULL;
    if (!catalog_ok) goto done;
    nested = modMusicLoadAudioPcm22050(resolved.file_path, &nested_count, &nested_rate);
    decode_ok = nested && nested_count == direct_count && nested_rate == direct_rate
        && memcmp(nested, direct, direct_count * sizeof(s16)) == 0;
    if (!decode_ok) goto done;
    mixed = calloc(direct_count + 4, sizeof(s16));
    if (!mixed) goto done;
    audioSetMasterVolume(1.0f);
    audioSetMusicVolume(1.0f);
    modMusicSetVolume(1.0f);
    audioMusicSyncReceive(id, 0);
    if (!modMusicIsPlaying()) goto done;
    modMusicMixInto(mixed, direct_count / 2 + 2);
    playback_ok = !modMusicIsPlaying()
        && memcmp(mixed, direct, direct_count * sizeof(s16)) == 0
        && mixed[direct_count] == 0 && mixed[direct_count + 1] == 0;
done:
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.AUDIO: archive=%d catalog=%d nested_pcm=%d "
        "playlist_pcm=%d frames=%u result=%s", archive_ok, catalog_ok,
        decode_ok, playback_ok, direct_count / 2,
        playback_ok ? "PASS" : "FAIL");
    if (archive) modArchiveAbort(archive);
    free(writer);
    free(encoded);
    SDL_free(direct);
    SDL_free(nested);
    free(mixed);
    modMusicStop();
    audioSetMasterVolume(master);
    audioSetMusicVolume(music);
    modMusicSetVolume(volume);
    return playback_ok ? 0 : -1;
}
