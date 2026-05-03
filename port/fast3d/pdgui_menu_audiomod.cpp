/**
 * pdgui_menu_audiomod.cpp -- Audio Mod Menu (Batch A-3)
 *
 * New tab in the Modding Hub for browsing, auditioning, and importing
 * audio mods. Lists all ASSET_AUDIO catalog entries (SFX, Music, Voice),
 * provides Play/Stop preview, and imports new audio files as mod components.
 *
 * Acceptance criteria (from design doc §5, Batch A-3):
 *   - Tab visible in Modding Hub
 *   - Lists all ASSET_AUDIO entries filtered by category
 *   - Play works for SFX (audioPlayFileSound) and Music (modMusicPlay)
 *   - Import creates a valid mod directory that persists across restart
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Forward-declare all C symbols via extern "C" blocks.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_filebrowser.h"
#include "assetcatalog.h"
#include "fs.h"

/* ========================================================================
 * Forward declarations for C symbols
 * ======================================================================== */

extern "C" {

void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);
void sysLogPrintf(s32 level, const char *fmt, ...);
const char *fsFullPath(const char *relPath, char *out, size_t outSize);
#ifndef FS_MAXPATH
#define FS_MAXPATH 1024
#endif

/* audio.c */
s32  audioPlayFileSound(const char *path, u16 volume, u8 pan);
f32  audioGetMasterVolume(void);
f32  audioGetMusicVolume(void);

/* modmusic.c */
void modMusicPlay(const char *file_path);
void modMusicStop(void);
s32  modMusicIsPlaying(void);

/* fs.c */
s32 fsCreateDir(const char *path);

/* netdistrib.c — re-broadcast catalog to lobby clients after import */
void netDistribServerRebroadcastCatalog(void);
s32 netGetMode(void);
#define NETMODE_SERVER_AUDIOMOD 2

/* assetcatalog.c */
asset_entry_t *assetCatalogRegisterAudio(const char *id, s32 sound_id,
                                          const char *name, s32 category,
                                          s32 duration_ms,
                                          const char *file_path);
s32 assetCatalogHasEntry(const char *id);
void assetCatalogIterateByType(asset_type_e type,
                                void (*fn)(const asset_entry_t *, void *),
                                void *userdata);
/* B-303 (catalog universality sweep, 2026-05-01): variant that includes
 * disabled entries.  Audio Mod authoring is a modder UI; the user wants
 * to see disabled audio rows so they can re-enable them. */
void assetCatalogIterateByTypeIncludingDisabled(asset_type_e type,
                                                 void (*fn)(const asset_entry_t *, void *),
                                                 void *userdata);

/* modmgr.c — S309: register + enable newly-imported audio mods so they
 * survive restart. Without these calls the import writes audio.ini + a
 * transient catalog entry, but on next launch modmgrInit scans the
 * registry with enabled=0 by default, never calls modmgrLoadMod, and
 * the catalog entry never gets re-registered. */
void        modmgrRescanDirectory(void);
void        modmgrSetEnabled(s32 index, s32 enabled);
void        modmgrSaveConfig(void);
s32         modmgrGetCount(void);
const char *modmgrGetModId(s32 index);

/* Content-inset: cursor positioning clear of chrome border */
void pdguiSetCursorBelowTitle(float title_h);

} /* extern "C" */

/* ========================================================================
 * Constants
 * ======================================================================== */

#define AUDIOMOD_PATH_LEN    256
#define AUDIOMOD_NAME_LEN    64
#define AUDIOMOD_ID_LEN      CATALOG_ID_LEN
#define AUDIOMOD_INITIAL_CAP 256   /* initial alloc; grows 2x as needed */

/* Log level constants (matching system.h values) */
#ifndef LOG_NOTE
#define LOG_NOTE    2
#define LOG_WARNING 3
#endif

/* ========================================================================
 * PdButton helper (same as moddinghub — local copy to keep standalone)
 * ======================================================================== */

static bool PdButtonAudio(const char *label, const ImVec2 &size = ImVec2(0,0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

/* ========================================================================
 * State
 * ======================================================================== */

struct AudioModEntry {
    char id[AUDIOMOD_ID_LEN];
    char name[AUDIOMOD_NAME_LEN];
    char file_path[AUDIOMOD_PATH_LEN];
    s32  sound_id;
    s32  category;      /* AUDIO_CAT_SFX / MUSIC / VOICE */
    s32  duration_ms;
    s32  bundled;
};

static AudioModEntry *s_AudioEntries   = nullptr;
static int           s_AudioNumEntries = 0;
static int           s_AudioCapacity   = 0;
static int           s_AudioSelected   = -1;
static int           s_AudioCategoryTab = -1;    /* -1=All, 0=SFX, 1=Music, 2=Voice */
static bool          s_AudioPreviewing  = false;  /* true while preview is active */

/* Import fields */
static char s_ImportFilePath[AUDIOMOD_PATH_LEN] = "";
static char s_ImportName[AUDIOMOD_NAME_LEN]     = "";
static int  s_ImportCategory = 1;  /* default to Music */

/* Status line + timed flash for import success */
static char s_AudioStatusMsg[256] = "";
static bool s_AudioStatusOk       = true;
static Uint32 s_AudioStatusFlashStart = 0;  /* SDL_GetTicks() when status was set */
#define AUDIOMOD_FLASH_DURATION_MS 4000     /* how long the success flash lasts */

/* ========================================================================
 * A-5: Soundtrack Pack Creation state
 * ======================================================================== */

#define PACK_MAX_TRACKS 64

static bool  s_PackCreatorOpen   = false;
static char  s_PackName[128]     = "";
static char  s_PackVersion[32]   = "1.0.0";
static bool *s_PackTrackSelected = nullptr; /* parallel to s_AudioEntries, same capacity */
static int   s_PackNumMusicTracks = 0; /* count of music entries for display */

/* ========================================================================
 * Helpers — catalog iteration
 * ======================================================================== */

/** Ensure s_AudioEntries and s_PackTrackSelected can hold at least `needed` entries. */
static void audioModEnsureCapacity(int needed)
{
    if (needed <= s_AudioCapacity) return;
    int newCap = s_AudioCapacity ? s_AudioCapacity : AUDIOMOD_INITIAL_CAP;
    while (newCap < needed) newCap *= 2;

    s_AudioEntries = (AudioModEntry *)realloc(s_AudioEntries,
                                               (size_t)newCap * sizeof(AudioModEntry));
    s_PackTrackSelected = (bool *)realloc(s_PackTrackSelected,
                                           (size_t)newCap * sizeof(bool));
    /* Zero the newly allocated region */
    if (newCap > s_AudioCapacity) {
        memset(&s_AudioEntries[s_AudioCapacity], 0,
               (size_t)(newCap - s_AudioCapacity) * sizeof(AudioModEntry));
        memset(&s_PackTrackSelected[s_AudioCapacity], 0,
               (size_t)(newCap - s_AudioCapacity) * sizeof(bool));
    }
    s_AudioCapacity = newCap;
}

static void audioModCollectCallback(const asset_entry_t *e, void *ud)
{
    int *n = (int *)ud;
    audioModEnsureCapacity(*n + 1);

    AudioModEntry &ae = s_AudioEntries[*n];
    strncpy(ae.id, e->id, AUDIOMOD_ID_LEN - 1);
    ae.id[AUDIOMOD_ID_LEN - 1] = '\0';
    strncpy(ae.name, e->ext.audio.name, AUDIOMOD_NAME_LEN - 1);
    ae.name[AUDIOMOD_NAME_LEN - 1] = '\0';
    strncpy(ae.file_path, e->ext.audio.file_path, AUDIOMOD_PATH_LEN - 1);
    ae.file_path[AUDIOMOD_PATH_LEN - 1] = '\0';
    ae.sound_id    = e->ext.audio.sound_id;
    ae.category    = e->ext.audio.category;
    ae.duration_ms = e->ext.audio.duration_ms;
    ae.bundled     = e->bundled;
    (*n)++;
}

/* ========================================================================
 * Public API: refresh + render
 * ======================================================================== */

extern "C" {

void pdguiAudioModRefresh(void)
{
    s_AudioNumEntries = 0;
    /* Audio Mod authoring UI -- modder needs to see disabled audio mod
     * entries to re-enable them.  See B-303 (catalog universality sweep). */
    assetCatalogIterateByTypeIncludingDisabled(ASSET_AUDIO,
                                                audioModCollectCallback,
                                                &s_AudioNumEntries);
    /* Clear pack selection for current capacity */
    if (s_PackTrackSelected && s_AudioCapacity > 0) {
        memset(s_PackTrackSelected, 0, (size_t)s_AudioCapacity * sizeof(bool));
    }
    s_AudioSelected   = -1;
    s_AudioPreviewing = false;
    s_AudioStatusMsg[0] = '\0';
    sysLogPrintf(LOG_NOTE, "AUDIOMOD: refreshed, %d entries", s_AudioNumEntries);
}

} /* extern "C" */

/* ========================================================================
 * Helpers — import
 * ======================================================================== */

static void sanitizeDirName(const char *name, char *out, int maxLen)
{
    int len = 0;
    for (int i = 0; name[i] && len < maxLen - 1; i++) {
        char c = name[i];
        if (c == ' ') c = '-';
        else if (c >= 'A' && c <= 'Z') c = c + 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out[len++] = c;
        }
    }
    if (len == 0) {
        strncpy(out, "audio-mod", maxLen);
        len = 9;
    }
    out[len] = '\0';
}

static bool copyFile(const char *src, const char *dst)
{
    /* Resolve relative destination paths through fsFullPath so the copy
     * lands in the correct game directory regardless of CWD.  Source paths
     * from the file browser are already absolute. */
    const char *resolvedDst = dst;
    char absDst[FS_MAXPATH + 1];
    if (dst[0] != '/' && dst[0] != '\\' && !(dst[0] && dst[1] == ':')) {
        const char *full = fsFullPath(dst, absDst, sizeof(absDst));
        if (full && full[0]) {
            resolvedDst = absDst;
        }
    }

    sysLogPrintf(LOG_NOTE, "AUDIOMOD: copyFile src='%s' dst='%s'", src, resolvedDst);

    FILE *fin = fopen(src, "rb");
    if (!fin) {
        sysLogPrintf(LOG_WARNING, "AUDIOMOD: copyFile failed to open source '%s'", src);
        return false;
    }

    FILE *fout = fopen(resolvedDst, "wb");
    if (!fout) {
        sysLogPrintf(LOG_WARNING, "AUDIOMOD: copyFile failed to open dest '%s'", resolvedDst);
        fclose(fin);
        return false;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (fwrite(buf, 1, n, fout) != n) {
            fclose(fin);
            fclose(fout);
            return false;
        }
    }
    fclose(fin);
    fclose(fout);
    return true;
}

/**
 * Import an audio file as a new mod component.
 * Creates mods/<slug>/ directory with audio.ini + copied audio file.
 * Registers the new entry in the catalog immediately.
 */
static bool importAudioFile(const char *filePath, const char *displayName,
                            int category)
{
    if (!filePath || !filePath[0] || !displayName || !displayName[0]) {
        snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                 "Import failed: file path and name are required");
        s_AudioStatusOk = false;
        return false;
    }

    /* Extract filename from path */
    const char *fileName = filePath;
    for (const char *p = filePath; *p; p++) {
        if (*p == '/' || *p == '\\') fileName = p + 1;
    }
    if (!fileName[0]) {
        snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                 "Import failed: invalid file path");
        s_AudioStatusOk = false;
        return false;
    }

    /* Sanitize display name to directory slug */
    char slug[64];
    sanitizeDirName(displayName, slug, sizeof(slug));

    /* Build mod directory path: mods/<slug>/ */
    char modDir[FS_MAXPATH];
    snprintf(modDir, sizeof(modDir), "mods/%s", slug);

    /* Create directory */
    if (fsCreateDir(modDir) != 0) {
        /* Directory may already exist — that's fine for overwrite */
        struct stat st;
        if (stat(modDir, &st) != 0) {
            snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                     "Import failed: could not create directory %s", modDir);
            s_AudioStatusOk = false;
            return false;
        }
    }

    /* Copy audio file into mod directory */
    char dstFile[FS_MAXPATH];
    snprintf(dstFile, sizeof(dstFile), "%s/%s", modDir, fileName);
    if (!copyFile(filePath, dstFile)) {
        snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                 "Import failed: could not copy %s", fileName);
        s_AudioStatusOk = false;
        return false;
    }

    /* Write audio.ini -- resolve through fsFullPath for correct location */
    char iniPath[FS_MAXPATH];
    snprintf(iniPath, sizeof(iniPath), "%s/audio.ini", modDir);
    char absIniPathBuf[FS_MAXPATH + 1];
    const char *absIniPath = fsFullPath(iniPath, absIniPathBuf, sizeof(absIniPathBuf));
    FILE *f = fopen((absIniPath && absIniPath[0]) ? absIniPath : iniPath, "w");
    if (!f) {
        snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                 "Import failed: could not write audio.ini");
        s_AudioStatusOk = false;
        return false;
    }
    fprintf(f, "[audio]\n");
    fprintf(f, "type = audio\n");
    fprintf(f, "name = %s\n", displayName);
    fprintf(f, "category = %d\n", category);
    fprintf(f, "duration_ms = 0\n");
    fprintf(f, "file_path = %s\n", fileName);
    fclose(f);

    /* Build catalog ID: <slug>:<sanitized-name> */
    char catalogId[AUDIOMOD_ID_LEN];
    snprintf(catalogId, sizeof(catalogId), "%s:audio", slug);

    /* Register in catalog immediately (no restart needed) */
    asset_entry_t *e = assetCatalogRegisterAudio(
        catalogId, 0, displayName, category, 0, dstFile);
    if (e) {
        e->bundled = 0;
        e->enabled = 1;
        strncpy(e->dirpath, modDir, FS_MAXPATH - 1);
        e->dirpath[FS_MAXPATH - 1] = '\0';
    }

    snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
             "Imported '%s' as %s", displayName, catalogId);
    s_AudioStatusOk = true;
    s_AudioStatusFlashStart = SDL_GetTicks();
    pdguiPlaySound(PDGUI_SND_SUCCESS);

    sysLogPrintf(LOG_NOTE, "AUDIOMOD: imported '%s' -> %s (%s)",
                 filePath, catalogId, modDir);

    /* S309: rescan the mod registry so the new mod.json/audio.ini shows
     * up under Modding Hub > Mod Manager, then flip its enabled bit and
     * persist to pd.ini so the catalog entry gets re-registered on the
     * next launch (previously the mod defaulted to disabled and its
     * audio.ini was never re-parsed, so custom songs vanished from the
     * Combat Simulator Select Tunes screen after restart). */
    modmgrRescanDirectory();
    s32 regCount = modmgrGetCount();
    for (s32 i = 0; i < regCount; i++) {
        const char *id = modmgrGetModId(i);
        if (id && strcmp(id, slug) == 0) {
            modmgrSetEnabled(i, 1);
            modmgrSaveConfig();
            sysLogPrintf(LOG_NOTE,
                "AUDIOMOD: auto-enabled mod '%s' so it survives restart", slug);
            break;
        }
    }

    /* v34: If we're hosting a server, re-broadcast catalog so connected
     * clients learn about the new audio mod and can download it. */
    if (netGetMode() == NETMODE_SERVER_AUDIOMOD) {
        netDistribServerRebroadcastCatalog();
    }

    return true;
}

/* ========================================================================
 * A-5: Soundtrack Pack — create pack as mod directory with mod.json
 * ======================================================================== */

/**
 * Create a soundtrack pack mod directory containing:
 *   mods/<slug>/mod.json        — multi-component manifest
 *   mods/<slug>/tracks/<file>   — copied audio files
 *
 * Each selected music track becomes a component in mod.json.
 * After creation, registers all components in the catalog immediately.
 */
static bool createSoundtrackPack(const char *packName, const char *version)
{
    if (!packName || !packName[0]) {
        snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                 "Pack creation failed: name is required");
        s_AudioStatusOk = false;
        return false;
    }

    /* Count selected music tracks */
    int selCount = 0;
    for (int i = 0; i < s_AudioNumEntries; i++) {
        if (s_PackTrackSelected[i] &&
            s_AudioEntries[i].category == AUDIO_CAT_MUSIC &&
            s_AudioEntries[i].file_path[0] &&
            !s_AudioEntries[i].bundled) {
            selCount++;
        }
    }
    if (selCount == 0) {
        snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                 "Pack creation failed: select at least one mod music track");
        s_AudioStatusOk = false;
        return false;
    }

    /* Sanitize pack name to directory slug */
    char slug[64];
    sanitizeDirName(packName, slug, sizeof(slug));

    /* Create directory structure: mods/<slug>/tracks/ */
    char modDir[FS_MAXPATH];
    snprintf(modDir, sizeof(modDir), "mods/%s", slug);
    fsCreateDir(modDir);

    char tracksDir[FS_MAXPATH];
    snprintf(tracksDir, sizeof(tracksDir), "mods/%s/tracks", slug);
    fsCreateDir(tracksDir);

    /* Build mod.json */
    char jsonPath[FS_MAXPATH];
    snprintf(jsonPath, sizeof(jsonPath), "%s/mod.json", modDir);
    FILE *f = fopen(jsonPath, "w");
    if (!f) {
        snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                 "Pack creation failed: could not write mod.json");
        s_AudioStatusOk = false;
        return false;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"%s\",\n", slug);
    fprintf(f, "  \"display_name\": \"%s\",\n", packName);
    fprintf(f, "  \"version\": \"%s\",\n", version && version[0] ? version : "1.0.0");
    fprintf(f, "  \"category\": \"music\",\n");
    fprintf(f, "  \"components\": [\n");

    int written = 0;
    for (int i = 0; i < s_AudioNumEntries; i++) {
        if (!s_PackTrackSelected[i]) continue;
        const AudioModEntry &ae = s_AudioEntries[i];
        if (ae.category != AUDIO_CAT_MUSIC || !ae.file_path[0] || ae.bundled) continue;

        /* Extract filename from source path */
        const char *srcFile = ae.file_path;
        for (const char *p = ae.file_path; *p; p++) {
            if (*p == '/' || *p == '\\') srcFile = p + 1;
        }

        /* Copy audio file to tracks/ subfolder */
        char dstPath[FS_MAXPATH];
        snprintf(dstPath, sizeof(dstPath), "%s/%s", tracksDir, srcFile);
        if (!copyFile(ae.file_path, dstPath)) {
            sysLogPrintf(LOG_WARNING, "AUDIOMOD: pack: failed to copy '%s'", ae.file_path);
            continue;
        }

        /* Write component entry */
        if (written > 0) fprintf(f, ",\n");
        char trackId[AUDIOMOD_ID_LEN];
        snprintf(trackId, sizeof(trackId), "%s:track%d", slug, written);

        fprintf(f, "    {\n");
        fprintf(f, "      \"type\": \"audio\",\n");
        fprintf(f, "      \"catalog_id\": \"%s\",\n", trackId);
        fprintf(f, "      \"name\": \"%s\",\n", ae.name[0] ? ae.name : srcFile);
        fprintf(f, "      \"category\": 1,\n");
        fprintf(f, "      \"file_path\": \"tracks/%s\"\n", srcFile);
        fprintf(f, "    }");

        /* Register component in catalog immediately */
        char fullTrackPath[FS_MAXPATH];
        snprintf(fullTrackPath, sizeof(fullTrackPath), "%s/%s", tracksDir, srcFile);
        asset_entry_t *e = assetCatalogRegisterAudio(
            trackId, 0, ae.name[0] ? ae.name : srcFile,
            AUDIO_CAT_MUSIC, ae.duration_ms, fullTrackPath);
        if (e) {
            e->bundled = 0;
            e->enabled = 1;
            strncpy(e->dirpath, modDir, FS_MAXPATH - 1);
            e->dirpath[FS_MAXPATH - 1] = '\0';
        }

        written++;
    }

    fprintf(f, "\n  ]\n");
    fprintf(f, "}\n");
    fclose(f);

    snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
             "Created pack '%s' with %d tracks in mods/%s/",
             packName, written, slug);
    s_AudioStatusOk = true;
    s_AudioStatusFlashStart = SDL_GetTicks();

    sysLogPrintf(LOG_NOTE, "AUDIOMOD: created pack '%s' -> mods/%s/ (%d tracks)",
                 packName, slug, written);

    return true;
}

/**
 * Render the inline "Create Pack" section below the import section.
 * Shows a list of mod music tracks with checkboxes, name/version fields,
 * and a Create button.
 */
static void renderPackCreator(float contentW, float scale)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.4f, 1.0f));
    ImGui::TextUnformatted("CREATE SOUNDTRACK PACK");
    ImGui::PopStyleColor();

    if (!s_PackCreatorOpen) {
        if (PdButtonAudio("Open Pack Creator##aud_pk", ImVec2(180.0f * scale, 0.0f))) {
            s_PackCreatorOpen = true;
            if (s_PackTrackSelected) memset(s_PackTrackSelected, 0, (size_t)s_AudioCapacity * sizeof(bool));
            s_PackName[0] = '\0';
            snprintf(s_PackVersion, sizeof(s_PackVersion), "1.0.0");
        }
        return;
    }

    /* Name + Version row */
    {
        float nameW = contentW * 0.55f;
        float verW  = contentW * 0.20f;

        ImGui::SetNextItemWidth(nameW);
        ImGui::InputText("##pk_name", s_PackName, sizeof(s_PackName));
        ImGui::SameLine();
        ImGui::TextDisabled("Pack Name");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(verW);
        ImGui::InputText("##pk_ver", s_PackVersion, sizeof(s_PackVersion));
        ImGui::SameLine();
        ImGui::TextDisabled("Ver");
    }

    /* Track selection: show only non-bundled music entries with file paths */
    int musicCount = 0;
    int selectedCount = 0;

    float listH = 120.0f * scale;
    ImGui::BeginChild("##pk_tracklist", ImVec2(contentW, listH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    for (int i = 0; i < s_AudioNumEntries; i++) {
        const AudioModEntry &ae = s_AudioEntries[i];
        if (ae.category != AUDIO_CAT_MUSIC) continue;
        if (!ae.file_path[0]) continue;
        if (ae.bundled) continue;

        musicCount++;
        char cbLabel[128];
        snprintf(cbLabel, sizeof(cbLabel), "%s##pk_%d",
                 ae.name[0] ? ae.name : ae.id, i);
        ImGui::Checkbox(cbLabel, &s_PackTrackSelected[i]);
        if (s_PackTrackSelected[i]) selectedCount++;
    }

    if (musicCount == 0) {
        ImGui::TextDisabled("No mod music tracks available. Import some first.");
    }

    ImGui::EndChild();

    /* Select All / Clear / Create / Close buttons */
    if (PdButtonAudio("All##pk", ImVec2(42.0f * scale, 22.0f * scale))) {
        for (int i = 0; i < s_AudioNumEntries; i++) {
            if (s_AudioEntries[i].category == AUDIO_CAT_MUSIC &&
                s_AudioEntries[i].file_path[0] && !s_AudioEntries[i].bundled) {
                s_PackTrackSelected[i] = true;
            }
        }
    }
    ImGui::SameLine();
    if (PdButtonAudio("None##pk", ImVec2(48.0f * scale, 22.0f * scale))) {
        if (s_PackTrackSelected) memset(s_PackTrackSelected, 0, (size_t)s_AudioCapacity * sizeof(bool));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d / %d selected", selectedCount, musicCount);

    ImGui::SameLine(contentW - 240.0f * scale);

    bool canCreate = (s_PackName[0] != '\0' && selectedCount > 0);
    if (!canCreate) ImGui::BeginDisabled();
    if (PdButtonAudio("Create Pack##aud_pk", ImVec2(120.0f * scale, 0.0f))) {
        if (createSoundtrackPack(s_PackName, s_PackVersion)) {
            pdguiAudioModRefresh();
            s_PackCreatorOpen = false;
            pdguiPlaySound(PDGUI_SND_SUCCESS);
        } else {
            pdguiPlaySound(PDGUI_SND_ERROR);
        }
    }
    if (!canCreate) ImGui::EndDisabled();

    ImGui::SameLine();
    if (PdButtonAudio("Close##pk", ImVec2(80.0f * scale, 0.0f))) {
        s_PackCreatorOpen = false;
    }
}

/* ========================================================================
 * Helpers — category display
 * ======================================================================== */

static const char *categoryName(int cat)
{
    switch (cat) {
        case AUDIO_CAT_SFX:   return "SFX";
        case AUDIO_CAT_MUSIC: return "Music";
        case AUDIO_CAT_VOICE: return "Voice";
        default:               return "Unknown";
    }
}

static const char *formatDuration(int ms, char *buf, int bufLen)
{
    if (ms <= 0) {
        snprintf(buf, bufLen, "--:--");
    } else {
        int sec = ms / 1000;
        int min = sec / 60;
        sec = sec % 60;
        snprintf(buf, bufLen, "%d:%02d", min, sec);
    }
    return buf;
}

/* ========================================================================
 * Render — called from moddinghub when Audio Mods tab is active
 * ======================================================================== */

extern "C" {

void pdguiAudioModRender(float contentW, float contentH, float scale)
{
    float btnW = 80.0f * scale;
    float btnH = 28.0f * scale;

    pdguiSetCursorBelowTitle(0.0f); /* content-inset: protect left edge from chrome border */

    /* ================================================================
     * TOP: Mod Creation Tools
     * ================================================================ */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.4f, 1.0f));
    ImGui::TextUnformatted("MOD CREATION TOOLS");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    /* Soundtrack Pack Creator (inline) */
    renderPackCreator(contentW, scale);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* ================================================================
     * MIDDLE: Export Track as Mod — music track list + play/export
     * ================================================================ */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.4f, 1.0f));
    ImGui::TextUnformatted("EXPORT TRACK AS MOD");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("  Select a music track, preview, then export");
    ImGui::Spacing();

    /* Count music entries for list sizing */
    int musicCount = 0;
    for (int i = 0; i < s_AudioNumEntries; i++) {
        if (s_AudioEntries[i].category == AUDIO_CAT_MUSIC) musicCount++;
    }

    /* Music track list */
    float listH = (contentH > 400.0f * scale) ? 160.0f * scale : 100.0f * scale;
    ImGui::BeginChild("##audiomod_tracklist", ImVec2(contentW, listH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    int visibleIdx = 0;
    for (int i = 0; i < s_AudioNumEntries; i++) {
        const AudioModEntry &ae = s_AudioEntries[i];
        if (ae.category != AUDIO_CAT_MUSIC) continue;

        char label[128];
        if (ae.name[0]) {
            snprintf(label, sizeof(label), "%s##aud_tk%d", ae.name, i);
        } else {
            snprintf(label, sizeof(label), "%s##aud_tk%d", ae.id, i);
        }

        /* Dim bundled (ROM) entries */
        bool isBundled = (ae.bundled != 0);
        if (isBundled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 0.9f));
        }

        bool sel = (s_AudioSelected == i);
        if (ImGui::Selectable(label, sel)) {
            s_AudioSelected = i;
            if (s_AudioPreviewing) {
                if (modMusicIsPlaying()) modMusicStop();
                s_AudioPreviewing = false;
            }
        }

        if (isBundled) ImGui::PopStyleColor();
        visibleIdx++;
    }

    if (visibleIdx == 0) {
        ImGui::TextDisabled("No music tracks registered.");
    }

    ImGui::EndChild();

    /* Play/Stop + Export controls for selected track */
    if (s_AudioSelected >= 0 && s_AudioSelected < s_AudioNumEntries &&
        s_AudioEntries[s_AudioSelected].category == AUDIO_CAT_MUSIC) {
        const AudioModEntry &ae = s_AudioEntries[s_AudioSelected];

        /* S311: "Selected" accent follows theme title glow. */
        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
        ImGui::Text("Selected: %s", ae.name[0] ? ae.name : ae.id);
        ImGui::PopStyleColor();

        char durBuf[16];
        ImGui::SameLine();
        ImGui::TextDisabled("  (%s)", formatDuration(ae.duration_ms, durBuf, sizeof(durBuf)));

        /* Play/Stop button */
        if (ae.file_path[0]) {
            bool isPlaying = (s_AudioPreviewing && modMusicIsPlaying());
            if (!isPlaying) {
                if (PdButtonAudio("Play##aud_exp", ImVec2(btnW, btnH))) {
                    modMusicPlay(ae.file_path);
                    s_AudioPreviewing = true;
                    snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg),
                             "Playing: %s", ae.name[0] ? ae.name : ae.id);
                    s_AudioStatusOk = true;
                }
            } else {
                if (PdButtonAudio("Stop##aud_exp", ImVec2(btnW, btnH))) {
                    modMusicStop();
                    s_AudioPreviewing = false;
                    snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg), "Stopped");
                    s_AudioStatusOk = true;
                }
            }
            /* Auto-detect track end */
            if (s_AudioPreviewing && !modMusicIsPlaying()) {
                s_AudioPreviewing = false;
                snprintf(s_AudioStatusMsg, sizeof(s_AudioStatusMsg), "Track finished");
                s_AudioStatusOk = true;
            }

            /* Export button (only for non-bundled tracks with file paths) */
            if (!ae.bundled) {
                ImGui::SameLine();
                if (PdButtonAudio("Export as Mod##aud_exp", ImVec2(140.0f * scale, btnH))) {
                    if (importAudioFile(ae.file_path, ae.name[0] ? ae.name : ae.id,
                                        AUDIO_CAT_MUSIC)) {
                        pdguiAudioModRefresh();
                    }
                }
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("Play##aud_exp", ImVec2(btnW, btnH));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(ROM music — not exportable)");
        }
    } else if (s_AudioSelected >= 0) {
        /* Selected something that's not music — clear selection */
        ImGui::TextDisabled("Select a music track from the list above.");
    } else {
        ImGui::TextDisabled("Select a music track to preview or export.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* ================================================================
     * BOTTOM: Import Audio
     * ================================================================ */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.4f, 1.0f));
    ImGui::TextUnformatted("IMPORT AUDIO");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("  MP3, WAV, OGG");
    ImGui::Spacing();

    /* File path + Browse button — description text ABOVE the input */
    {
        float browseW = 80.0f * scale;
        ImGui::SetNextItemWidth(contentW - browseW - 16.0f * scale);
        ImGui::InputText("##aud_imppath", s_ImportFilePath, sizeof(s_ImportFilePath));
        ImGui::SameLine();
        if (PdButtonAudio("Browse##aud", ImVec2(browseW, 0.0f))) {
            pdguiFileBrowserOpen("Import Audio", "mods", ".mp3;.wav;.ogg");
        }
    }

    /* Handle file browser result */
    if (pdguiFileBrowserIsOpen()) {
        if (pdguiFileBrowserRender()) {
            strncpy(s_ImportFilePath, pdguiFileBrowserGetPath(), sizeof(s_ImportFilePath) - 1);
            s_ImportFilePath[sizeof(s_ImportFilePath) - 1] = '\0';
            if (!s_ImportName[0]) {
                const char *fname = s_ImportFilePath;
                for (const char *p = s_ImportFilePath; *p; p++) {
                    if (*p == '/' || *p == '\\') fname = p + 1;
                }
                strncpy(s_ImportName, fname, sizeof(s_ImportName) - 1);
                s_ImportName[sizeof(s_ImportName) - 1] = '\0';
                char *dot = NULL;
                for (char *p = s_ImportName; *p; p++) {
                    if (*p == '.') dot = p;
                }
                if (dot) *dot = '\0';
            }
            pdguiFileBrowserClose();
        }
    }

    /* Name + Category + Import button */
    {
        float nameW = contentW * 0.40f;
        float catW  = 100.0f * scale;
        float impBtnW = 100.0f * scale;

        ImGui::SetNextItemWidth(nameW);
        ImGui::InputText("##aud_impname", s_ImportName, sizeof(s_ImportName));
        ImGui::SameLine();
        ImGui::TextDisabled("Name");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(catW);
        const char *catItems[] = { "SFX", "Music", "Voice" };
        ImGui::Combo("##aud_impcat", &s_ImportCategory, catItems, 3);

        ImGui::SameLine();

        bool canImport = (s_ImportFilePath[0] != '\0' && s_ImportName[0] != '\0');
        if (!canImport) ImGui::BeginDisabled();
        if (PdButtonAudio("Import##aud", ImVec2(impBtnW, 0.0f))) {
            if (importAudioFile(s_ImportFilePath, s_ImportName, s_ImportCategory)) {
                pdguiAudioModRefresh();
                s_ImportFilePath[0] = '\0';
                s_ImportName[0]     = '\0';
            } else {
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }
        if (!canImport) ImGui::EndDisabled();
    }

    /* ---- Footer: status with flash effect ---- */
    ImGui::Spacing();
    ImGui::Separator();
    if (s_AudioStatusMsg[0]) {
        if (s_AudioStatusOk) {
            /* Pulsing green flash for success — fades over AUDIOMOD_FLASH_DURATION_MS */
            Uint32 elapsed = SDL_GetTicks() - s_AudioStatusFlashStart;
            float flashAlpha = 1.0f;
            if (s_AudioStatusFlashStart > 0 && elapsed < AUDIOMOD_FLASH_DURATION_MS) {
                /* Pulse: bright for first 500ms, then fade */
                float t = (float)elapsed / (float)AUDIOMOD_FLASH_DURATION_MS;
                flashAlpha = (t < 0.125f) ? 1.0f : (1.0f - (t - 0.125f) / 0.875f);

                /* Draw a subtle green highlight bar behind the text */
                ImVec2 cpos = ImGui::GetCursorScreenPos();
                float barW = ImGui::GetContentRegionAvail().x;
                float barH = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    cpos,
                    ImVec2(cpos.x + barW, cpos.y + barH),
                    ImGui::GetColorU32(ImVec4(0.1f, 0.5f, 0.1f, 0.3f * flashAlpha)),
                    4.0f);
            }
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, flashAlpha),
                               "%s", s_AudioStatusMsg);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", s_AudioStatusMsg);
        }
    } else {
        ImGui::TextDisabled("Audio Mods — create, export, and import audio");
    }
}

} /* extern "C" */
