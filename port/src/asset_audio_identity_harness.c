#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "asset_audio_identity_harness.h"
#include "asset_archive_writer.h"
#include "asset_path_contract.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_sound_slots.h"
#include "audio.h"
#include "constants.h"
#include "fs.h"
#include "lib/snd.h"
#include "modmusic.h"
#include "net/netdistrib.h"
#include "pdca_extract_transaction.h"
#include "smoke_harness.h"
#include "system.h"

enum { IDENTITY_FRAMES = 4096, IDENTITY_WAV_SIZE = 44 + IDENTITY_FRAMES * 4 };

typedef struct identity_target {
    char id[CATALOG_ID_LEN];
    s32 category;
    s32 sound_id;
    s32 source_soundnum;
    s32 source_filenum;
} identity_target_t;

static void put16(u8 *p, u16 n) { p[0] = (u8)n; p[1] = (u8)(n >> 8); }
static void put32(u8 *p, u32 n)
{
    for (int i = 0; i < 4; i++) p[i] = (u8)(n >> (8 * i));
}

/* Original synthetic PCM, generated at runtime without a codec or ROM asset.
 * Different phases alter both channels, making stale-source reuse observable. */
static s16 expectedSample(int frame, int channel, int phase)
{
    return (s16)(((frame * (channel ? 29 : 17) + phase * 251) % 2048) - 1024);
}

static void makeWav(u8 *wav, int phase)
{
    memset(wav, 0, IDENTITY_WAV_SIZE);
    memcpy(wav, "RIFF", 4); put32(wav + 4, IDENTITY_WAV_SIZE - 8);
    memcpy(wav + 8, "WAVEfmt ", 8); put32(wav + 16, 16);
    put16(wav + 20, 1); put16(wav + 22, 2); put32(wav + 24, 22050);
    put32(wav + 28, 22050 * 4); put16(wav + 32, 4); put16(wav + 34, 16);
    memcpy(wav + 36, "data", 4); put32(wav + 40, IDENTITY_FRAMES * 4);
    for (int i = 0; i < IDENTITY_FRAMES; i++) {
        put16(wav + 44 + i * 4, (u16)expectedSample(i, 0, phase));
        put16(wav + 46 + i * 4, (u16)expectedSample(i, 1, phase));
    }
}

static int writeBytes(const char *path, const void *bytes, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(bytes, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static int makeFolder(const char *root, const char *leaf, char out[FS_MAXPATH])
{
    return assetPathJoinChecked(out, FS_MAXPATH, root, "/", leaf)
        && fsCreateDir(out);
}

static const char *categoryName(s32 category)
{
    return category == AUDIO_CAT_MUSIC ? "music"
        : category == AUDIO_CAT_VOICE ? "voice" : "sfx";
}

static int descriptorText(char *out, size_t cap, const identity_target_t *target,
    s32 category, int legacy)
{
    int n = snprintf(out, cap,
        "[%s]\ncatalog_id = %s\nname = Audio identity synthetic source\n"
        "audio_category = %s\nfile_path = sample.wav\n",
        legacy ? "audio" : categoryName(category), target->id, categoryName(category));
    return n > 0 && (size_t)n < cap;
}

static int writeTyped(const char *path, const identity_target_t *target,
    s32 category, const u8 *wav)
{
    const char *kind = category == AUDIO_CAT_MUSIC ? "song" : categoryName(category);
    const char *leaf = category == AUDIO_CAT_MUSIC ? "music.ini"
        : category == AUDIO_CAT_VOICE ? "voice.ini" : "sound.ini";
    char descriptor[1024], manifest[1024];
    mod_archive_writer_t *archive = NULL;
    asset_archive_writer_t *writer = NULL;
    int ok = 0;
    if (!descriptorText(descriptor, sizeof(descriptor), target, category, 0)) return 0;
    int n = snprintf(manifest, sizeof(manifest),
        "{\"pd_kind\":\"%s\",\"pd_schema_version\":1,\"id\":\"%s\","
        "\"data\":\"sample.wav\"}", kind, target->id);
    if (n < 0 || n >= (int)sizeof(manifest)) return 0;
    archive = modArchiveBegin(path);
    writer = calloc(1, sizeof(*writer));
    if (!archive || !writer) goto done;
    if (assetArchiveWriterInit(writer, archive, kind, target->id) != MODARCHIVE_OK
        || assetArchiveWriterAddDescriptor(writer, leaf, descriptor,
            (u32)strlen(descriptor)) != MODARCHIVE_OK
        || assetArchiveWriterAddManifestJson(writer, manifest,
            (u32)strlen(manifest)) != MODARCHIVE_OK
        || assetArchiveWriterAddPublicMem(writer, "sample.wav", wav,
            IDENTITY_WAV_SIZE, "audio") != MODARCHIVE_OK
        || assetArchiveWriterFinishMetadata(writer) != MODARCHIVE_OK) goto done;
    ok = modArchiveFinish(archive) == MODARCHIVE_OK;
    archive = NULL;
done:
    if (archive) modArchiveAbort(archive);
    free(writer);
    return ok;
}

/* Actual PDCA framing; the existing receive smoke seam compresses, hashes,
 * chunks, and passes this through HandleBegin/Chunk/End without a fake registrar. */
static int writePdca(const char *path, const char *const *names,
    const void *const *data, const u32 *sizes, int count)
{
    size_t size = 6;
    if (count < 1 || count > 4) return 0;
    for (int i = 0; i < count; i++) size += 2 + strlen(names[i]) + 1 + 4 + sizes[i];
    if (size > UINT32_MAX) return 0;
    u8 *bytes = malloc(size);
    if (!bytes) return 0;
    put32(bytes, PDCA_ARCHIVE_MAGIC); put16(bytes + 4, (u16)count);
    u8 *p = bytes + 6;
    for (int i = 0; i < count; i++) {
        size_t name_size = strlen(names[i]) + 1;
        put16(p, (u16)name_size); p += 2;
        memcpy(p, names[i], name_size); p += name_size;
        put32(p, sizes[i]); p += 4;
        memcpy(p, data[i], sizes[i]); p += sizes[i];
    }
    int ok = writeBytes(path, bytes, size);
    free(bytes);
    return ok;
}

static int receiveOne(const char *root, const char *pdca, const char *id,
    int expected_success)
{
    char list_path[FS_MAXPATH], line[FS_MAXPATH + CATALOG_ID_LEN + 64];
    distrib_client_status_t status;
    if (!assetPathJoinChecked(list_path, sizeof(list_path), root, "/", "receive.txt")) return 0;
    int n = snprintf(line, sizeof(line), "%s|%s|audio_identity_smoke|0\n", pdca, id);
    if (n < 0 || n >= (int)sizeof(line) || !writeBytes(list_path, line, (size_t)n)) return 0;
    if (netDistribDebugReceivePdcaListForSmoke(list_path) != 1) return 0;
    netDistribClientGetStatus(&status);
    /* Delivered counts attempts, not admission. Assert terminal status too. */
    return expected_success ? status.state == DISTRIB_CSTATE_DONE && status.received_count == 1
        : status.state == DISTRIB_CSTATE_ERROR && status.received_count == 0;
}

static int selectTargets(identity_target_t targets[3])
{
    int candidates[3] = {0}, admitted[3] = {0};
    memset(targets, 0, 3 * sizeof(*targets));
    for (int i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *row = assetCatalogGetByIndex(i);
        int index = -1;
        if (!row || !row->bundled || !row->enabled || row->type != ASSET_AUDIO
            || strncmp(row->id, "base:", 5)) continue;
        if (row->ext.audio.category == AUDIO_CAT_SFX && row->source_soundnum > 1
            && row->source_filenum < 0
            && row->source_soundnum < SND_CUSTOM_START) index = 0;
        if (row->ext.audio.category == AUDIO_CAT_VOICE && row->source_filenum > 0) index = 1;
        if (row->ext.audio.category == AUDIO_CAT_MUSIC && row->ext.audio.sound_id >= 0) index = 2;
        if (index < 0 || targets[index].id[0]) continue;
        candidates[index]++;
        CatalogResolveResult route = index == 0 ? catalogResolveSound(row->source_soundnum)
            : index == 1 ? catalogResolveFile(row->source_filenum)
            : catalogResolveMusicSequence(row->ext.audio.sound_id);
        /* Select only the identity currently authoritative at the real native
         * lookup. Aliases sharing an internal index must not be guessed. */
        if (assetCatalogGetByIndex(route.catalog_id) != row || !route.path
            || !route.is_mod_override || route.source_only_blocked) {
            if (candidates[index] == 1) {
                const asset_entry_t *owner = assetCatalogGetByIndex(route.catalog_id);
                sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.IDENTITY: preflight category=%d candidate=%s "
                    "sound=%d file=%d track=%d owner=%s path=%s override=%d blocked=%d",
                    index, row->id, row->source_soundnum, row->source_filenum,
                    row->ext.audio.sound_id, owner ? owner->id : "missing",
                    route.path ? route.path : "missing", route.is_mod_override,
                    route.source_only_blocked);
            }
            continue;
        }
        admitted[index]++;
        strcpy(targets[index].id, row->id);
        targets[index].category = row->ext.audio.category;
        targets[index].sound_id = row->ext.audio.sound_id;
        targets[index].source_soundnum = row->source_soundnum;
        targets[index].source_filenum = row->source_filenum;
    }
    sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.IDENTITY: preflight candidates=%d,%d,%d admitted=%d,%d,%d pool=%d count=%d",
        candidates[0], candidates[1], candidates[2], admitted[0], admitted[1], admitted[2],
        assetCatalogGetPoolSize(), assetCatalogGetCount());
    return targets[0].id[0] && targets[1].id[0] && targets[2].id[0];
}

static int identityUnchanged(const identity_target_t *target)
{
    const asset_entry_t *row = assetCatalogResolve(target->id);
    return row && row->type == ASSET_AUDIO && row->enabled
        && row->ext.audio.category == target->category
        && row->ext.audio.sound_id == target->sound_id
        && row->source_soundnum == target->source_soundnum
        && row->source_filenum == target->source_filenum;
}

static int routeMatches(CatalogResolveResult route, const identity_target_t *target,
    const u8 *wav, int phase, int decode)
{
    const asset_entry_t *row = assetCatalogGetByIndex(route.catalog_id);
    u32 size = 0, samples = 0;
    s32 rate = 0;
    if (!row || strcmp(row->id, target->id) || !route.is_mod_override
        || route.source_only_blocked || !route.path) return 0;
    void *bytes = fsFileLoad(route.path, &size);
    int ok = bytes && size == IDENTITY_WAV_SIZE && !memcmp(bytes, wav, size);
    free(bytes);
    if (!ok || !decode) return ok;
    s16 *pcm = modMusicLoadAudioPcm22050(route.path, &samples, &rate);
    ok = pcm && samples == IDENTITY_FRAMES * 2 && rate == 22050;
    for (u32 i = 0; ok && i < samples; i++) {
        if (pcm[i] != expectedSample((int)(i / 2), (int)(i % 2), phase)) ok = 0;
    }
    SDL_free(pcm);
    return ok;
}

static int verifyTarget(const identity_target_t *target, const u8 *wav,
    int phase, int file_handle)
{
    if (!identityUnchanged(target)) return 0;
    if (target->category == AUDIO_CAT_MUSIC) {
        if (!routeMatches(catalogResolveMusicSequence(target->sound_id), target, wav, phase, 1)) return 0;
    } else if (target->source_filenum >= 0) {
        /* MP3 source_index can be a packed configuration ID (e.g. 0x8028),
         * not an index into the ordinary sound table. Its file route is the
         * production entry point; preserve the packed metadata unchanged. */
        if (!routeMatches(catalogResolveFile(target->source_filenum), target, wav, phase, 1)) return 0;
    } else if (target->source_soundnum >= 0) {
        if (!routeMatches(catalogResolveSound(target->source_soundnum), target, wav, phase, 1)) return 0;
    } else return 0;
    if (file_handle) {
        if (sndIsDisabled() || target->source_soundnum <= 0) return 0;
        struct sndstate *handle = sndStart(0, (s16)target->source_soundnum,
            NULL, 0x2000, 64, 1.0f, 0, 0);
        int ok = handle && audioFileSoundOwnsHandle(handle)
            && audioFileSoundGetState(handle) == AL_PLAYING;
        if (handle) audioFileSoundStop(handle);
        if (!ok) return 0;
    }
    return 1;
}

static int folderSet(const char *folder, const identity_target_t targets[3],
    const u8 *wav)
{
    static const char *leaves[] = { "sample.pdsfx", "dialogue.pdvoice", "track.pdsong" };
    char path[FS_MAXPATH];
    for (int i = 0; i < 3; i++) {
        if (!assetPathJoinChecked(path, sizeof(path), folder, "/", leaves[i])
            || !writeTyped(path, &targets[i], targets[i].category, wav)) return 0;
    }
    return assetCatalogScanExternalLayoutFolder("audio_identity_smoke", folder) == 3;
}

static int allTargets(const identity_target_t targets[3], const u8 *wav, int phase)
{
    catalogLoadInit();
    for (int i = 0; i < 3; i++) if (!verifyTarget(&targets[i], wav, phase, i == 0)) return 0;
    return 1;
}

static int reportCase(const char *name, int ok)
{
    sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.IDENTITY: case=%s result=%s", name, ok ? "PASS" : "FAIL");
    return ok;
}

int assetAudioIdentityHarnessRun(void)
{
    identity_target_t targets[3], custom = {0};
    char root[FS_MAXPATH], folder[FS_MAXPATH], path[FS_MAXPATH], pdca[FS_MAXPATH];
    char descriptor[1024], original_path[FS_MAXPATH];
    u8 *wav = NULL, *prior_wav = NULL;
    void *archive_bytes = NULL;
    u32 archive_size = 0;
    int passed = 0, active = 0, ok = 0;
    const char *custom_id = "audio_identity_smoke:custom";
    if (!smokeHarnessIsActive()) return -1;
    fsFullPath("$S/audio-identity-smoke", root, sizeof(root));
    if (!fsCreateDir(root)) {
        sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.IDENTITY: preflight root=%s creation=failed", root);
        goto done;
    }
    wav = malloc(IDENTITY_WAV_SIZE); prior_wav = malloc(IDENTITY_WAV_SIZE);
    if (!wav || !prior_wav) {
        sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.IDENTITY: preflight allocation=failed");
        goto done;
    }
    if (!selectTargets(targets)) goto done;
    for (int i = 0; i < 3; i++) {
        sysLogPrintf(LOG_NOTE,
            "ASSET.AUDIO.IDENTITY: native id=%s category=%d sound=%d file=%d track_or_sound=%d",
            targets[i].id, targets[i].category, targets[i].source_soundnum,
            targets[i].source_filenum, targets[i].sound_id);
    }
    makeWav(wav, 1);
    ok = makeFolder(root, "folder-a", folder) && folderSet(folder, targets, wav)
        && allTargets(targets, wav, 1);
    if (!reportCase("folder_native_reverse_and_file_handle", ok)) goto done;
    passed++;

    makeWav(wav, 2);
    ok = makeFolder(root, "folder-b", folder) && folderSet(folder, targets, wav)
        && allTargets(targets, wav, 2);
    if (!reportCase("second_same_id_source_replacement", ok)) goto done;
    passed++;
    memcpy(prior_wav, wav, IDENTITY_WAV_SIZE);

    strcpy(custom.id, custom_id); custom.category = AUDIO_CAT_SFX;
    custom.sound_id = custom.source_soundnum = custom.source_filenum = -1;
    if (assetCatalogResolve(custom.id) || !makeFolder(root, "custom", folder)
        || !assetPathJoinChecked(path, sizeof(path), folder, "/", "custom.pdsfx")
        || !writeTyped(path, &custom, custom.category, wav)
        || assetCatalogScanExternalLayoutFolder("audio_identity_smoke", folder) != 1) goto done;
    {
        const asset_entry_t *row = assetCatalogResolve(custom.id);
        if (!row) goto done;
        custom.sound_id = row->ext.audio.sound_id;
        custom.source_soundnum = row->source_soundnum;
    }
    catalogLoadInit();
    ok = custom.source_soundnum >= SND_CUSTOM_START && custom.source_soundnum < SND_CUSTOM_END
        && verifyTarget(&custom, wav, 2, 1);
    if (!reportCase("new_private_slot_file_handle", ok)) goto done;
    passed++;

    /* Reject a category collision before retiring the active prior row. */
    active = catalogLoadStageAsset(ASSET_AUDIO, targets[0].id);
    if (!active || !makeFolder(root, "category-conflict", folder)
        || !assetPathJoinChecked(path, sizeof(path), folder, "/", "conflict.pdsong")
        || !writeTyped(path, &targets[0], AUDIO_CAT_MUSIC, wav)) goto done;
    ok = assetCatalogScanExternalLayoutFolder("audio_identity_smoke", folder) < 0
        && allTargets(targets, prior_wav, 2);
    {
        const asset_entry_t *row = assetCatalogResolve(targets[0].id);
        ok = ok && row && row->stage_ref_count && row->loaded_data == row;
    }
    if (!reportCase("category_collision_preserves_live_mapping", ok)) goto done;
    passed++;

    /* Root INI registration always precedes recursive typed descriptors. Thus
     * this late corrupt archive must undo a real preceding replacement. */
    makeWav(wav, 3);
    if (!makeFolder(root, "late-reject", folder)
        || !assetPathJoinChecked(path, sizeof(path), folder, "/", "sample.wav")
        || !writeBytes(path, wav, IDENTITY_WAV_SIZE)
        || !descriptorText(descriptor, sizeof(descriptor), &targets[0], AUDIO_CAT_SFX, 0)
        || !assetPathJoinChecked(path, sizeof(path), folder, "/", "sound.ini")
        || !writeBytes(path, descriptor, strlen(descriptor))
        || !assetPathJoinChecked(path, sizeof(path), folder, "/", "late.pdsfx")
        || !writeBytes(path, "invalid archive", 15)) goto done;
    ok = assetCatalogScanExternalLayoutFolder("audio_identity_smoke", folder) < -1
        && allTargets(targets, prior_wav, 2);
    {
        const asset_entry_t *row = assetCatalogResolve(targets[0].id);
        ok = ok && row && row->stage_ref_count && row->loaded_data == row;
    }
    if (!reportCase("late_scanner_rejection_restores_live_mapping", ok)) goto done;
    passed++;
    catalogReleaseStageAsset(ASSET_AUDIO, targets[0].id); active = 0;

    /* Received typed archive: real compression/hash/chunk/extraction/admission. */
    if (!assetPathJoinChecked(path, sizeof(path), root, "/", "received.pdsfx")
        || !writeTyped(path, &targets[0], AUDIO_CAT_SFX, wav)) goto done;
    archive_bytes = fsFileLoad(path, &archive_size);
    if (!archive_bytes || !assetPathJoinChecked(pdca, sizeof(pdca), root, "/", "received.pdca")) goto done;
    {
        const char *names[] = { "source.pdsfx" };
        const void *data[] = { archive_bytes };
        const u32 sizes[] = { archive_size };
        ok = writePdca(pdca, names, data, sizes, 1) && receiveOne(root, pdca, targets[0].id, 1)
            && verifyTarget(&targets[0], wav, 3, 1);
    }
    free(archive_bytes); archive_bytes = NULL;
    if (!reportCase("received_typed_native_reverse_and_file_handle", ok)) goto done;
    passed++;
    memcpy(prior_wav, wav, IDENTITY_WAV_SIZE);
    {
        CatalogResolveResult route = catalogResolveSound(targets[0].source_soundnum);
        if (!route.path || !assetPathCopyChecked(original_path, sizeof(original_path), route.path)) goto done;
    }

    /* Same destination receives a good loose source followed by corrupt typed
     * content. Catalog restores first, then PDCA restores the old ZIP bytes. */
    active = catalogLoadStageAsset(ASSET_AUDIO, targets[0].id);
    makeWav(wav, 4);
    if (!active || !descriptorText(descriptor, sizeof(descriptor), &targets[0], AUDIO_CAT_SFX, 0)) goto done;
    {
        const char *names[] = { "sound.ini", "sample.wav", "late.pdsfx" };
        const void *data[] = { descriptor, wav, "invalid archive" };
        const u32 sizes[] = { (u32)strlen(descriptor), IDENTITY_WAV_SIZE, 15 };
        ok = writePdca(pdca, names, data, sizes, 3) && receiveOne(root, pdca, targets[0].id, 0)
            && verifyTarget(&targets[0], prior_wav, 3, 1);
        CatalogResolveResult route = catalogResolveSound(targets[0].source_soundnum);
        const asset_entry_t *row = assetCatalogResolve(targets[0].id);
        ok = ok && route.path && !strcmp(route.path, original_path)
            && row && row->loaded_data == row && row->stage_ref_count;
    }
    if (!reportCase("received_late_rejection_restores_catalog_and_bytes", ok)) goto done;
    passed++;
    catalogReleaseStageAsset(ASSET_AUDIO, targets[0].id); active = 0;

    /* Received compatibility descriptor uses the same identity transaction.
     * Legacy numeric metadata must not become source identity authority. */
    if (!descriptorText(descriptor, sizeof(descriptor), &targets[0], AUDIO_CAT_SFX, 1)) goto done;
    strcat(descriptor, "sound_id = 1\nsource_soundnum = 1\nsource_filenum = 1\n");
    {
        const char *names[] = { "audio.ini", "sample.wav" };
        const void *data[] = { descriptor, wav };
        const u32 sizes[] = { (u32)strlen(descriptor), IDENTITY_WAV_SIZE };
        ok = writePdca(pdca, names, data, sizes, 2) && receiveOne(root, pdca, targets[0].id, 1)
            && verifyTarget(&targets[0], wav, 4, 1);
    }
    if (!reportCase("received_audio_ini_ignores_numeric_identity", ok)) goto done;
    passed++;
done:
    if (active) catalogReleaseStageAsset(ASSET_AUDIO, targets[0].id);
    free(archive_bytes); free(wav); free(prior_wav);
    sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.IDENTITY: passed=%d cases=8 result=%s",
        passed, passed == 8 ? "PASS" : "FAIL");
    return passed == 8 ? 0 : -1;
}
