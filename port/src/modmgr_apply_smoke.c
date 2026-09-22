/* Actual prepared-Apply core probe, invoked only by an explicit smoke event.
 * Run in a disposable portable install; not an input/ImGui acceptance gate. */
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include "modmgr.h"
#include "modmgr_apply.h"
#include "modmgr_enabled_state.h"
#include "modarchive.h"
#include "file_transfer.h"
#include "save_atomic.h"
#include "sha256.h"
#include "asset_path_contract.h"
#include "system.h"
#include "fs.h"

static int modmgrProbeRegistryHash(u8 digest[SHA256_DIGEST_SIZE])
{
    int count = modmgrGetCount();
    if (count < 0 || count > MODMGR_MAX_MODS) return 0;
    sha256_ctx ctx;
    sha256Init(&ctx);
    sha256Update(&ctx, &count, sizeof(count));
    for (int i = 0; i < count; ++i) {
        const modinfo_t *mod = modmgrGetMod(i);
        if (!mod) return 0;
        /* Before-runtime failure must not mutate even the live load handles.
         * Compare an immediate same-process byte snapshot, not a saved format. */
        sha256Update(&ctx, mod, sizeof(*mod));
    }
    sha256Final(&ctx, digest);
    return 1;
}

static int modmgrProbeBaselineRetry(void)
{
    modmgr_apply_plan_t *plan = NULL;
    modmgr_apply_result_t result = {0};
    modmgr_component_result_t initial = {0};
    u8 registryBefore[SHA256_DIGEST_SIZE], registryAfter[SHA256_DIGEST_SIZE];
    u8 fileBefore[SHA256_DIGEST_SIZE], fileAfter[SHA256_DIGEST_SIZE];
    char path[FS_MAXPATH + 1];
    const char *mods = modmgrGetModsDir();
    const char *stage = "destination";
    int ok = 0;
    if (!mods || !assetPathJoinChecked(path, sizeof(path), mods, "/", ".modstate"))
        goto finished;

    /* Establish the destination through the real component persistence API.
     * Never arm injection before this path is writable and a plan is ready. */
    stage = "baseline_save";
    if (!modmgrSaveComponentStateChecked(&initial) || !initial.saved) goto finished;
    stage = "baseline_hash";
    if (sha256HashFile(path, fileBefore) != 0) goto finished;
    stage = "prepare";
    if (!modmgrPrepareApplyPlan(1, NULL, 0, &plan, &result)) goto finished;
    stage = "registry_hash";
    if (!modmgrProbeRegistryHash(registryBefore)) goto finished;

    stage = "injected_failure";
    saveAtomicDebugFailNextCommit();
    int applied = modmgrApplyPreparedChanges(plan, &result);
    int sameRegistry = modmgrProbeRegistryHash(registryAfter) &&
        memcmp(registryBefore, registryAfter, sizeof(registryBefore)) == 0;
    int sameFile = sha256HashFile(path, fileAfter) == 0 &&
        memcmp(fileBefore, fileAfter, sizeof(fileBefore)) == 0;
    int failedBeforeRuntime = !applied && result.phase == MODMGR_APPLY_COMPONENT_SAVE &&
        !result.runtime_started && !result.runtime_completed && !result.component.saved &&
        result.config.saved == 0 && modmgrIsDirty() && sameRegistry && sameFile;
    sysLogPrintf(failedBeforeRuntime ? LOG_NOTE : LOG_WARNING,
        "MODMGR.APPLY.PROBE: failed_save ok=%d applied=%d phase=%d runtime=%d completed=%d component_saved=%d config_saved=%u dirty=%d registry_same=%d file_same=%d",
        failedBeforeRuntime, applied, result.phase, result.runtime_started,
        result.runtime_completed, result.component.saved, result.config.saved,
        modmgrIsDirty(), sameRegistry, sameFile);
    if (!failedBeforeRuntime) goto finished;

    /* Retry the same owned plan, not a newly captured baseline. */
    stage = "same_plan_retry";
    applied = modmgrApplyPreparedChanges(plan, &result);
    const unsigned required = MODMGR_SAVED_ENABLED_JSON | MODMGR_SAVED_MACHINE;
    ok = applied && result.phase == MODMGR_APPLY_COMPLETE && result.runtime_started &&
        result.runtime_completed && result.component.saved &&
        (result.config.saved & required) == required && !modmgrIsDirty();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODMGR.APPLY.PROBE: retry ok=%d applied=%d phase=%d runtime=%d completed=%d component_saved=%d config_saved=%u dirty=%d restart=%d",
        ok, applied, result.phase, result.runtime_started, result.runtime_completed,
        result.component.saved, result.config.saved, modmgrIsDirty(), result.restart_count);
finished:
    modmgrFreeApplyPlan(plan);
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODMGR.APPLY.PROBE: result=%s stage=%s detail='%s'",
        ok ? "PASS" : "FAIL", stage, result.error[0] ? result.error : initial.error);
    return ok;
}

static int modmgrProbeSavedEnabled(const modinfo_t *mod, int expected)
{
    modinfo_t saved = *mod;
    saved.enabled = !expected;
    char path[FS_MAXPATH + 1], error[256];
    fsFullPath("$S/mods-enabled.json", path, sizeof(path));
    return modmgrLoadEnabledFile(path, &saved, 1, error, sizeof(error)) == 1 &&
        saved.enabled == expected;
}

static int modmgrProbeInstalledActivation(void)
{
    const char *id = "menu_activation_retry_probe";
    const char *mods = modmgrGetModsDir();
    const char *stage = "create_public_manifest";
    char directory[FS_MAXPATH + 1], path[FS_MAXPATH + 1];
    modmgr_apply_plan_t *plan = NULL;
    modmgr_apply_result_t result = {0};
    int ok = 0;
    if (!mods || modmgrFindMod(id) ||
            !assetPathJoinChecked(directory, sizeof(directory), mods, "/", id) ||
            !assetPathJoinChecked(path, sizeof(path), directory, "/", "mod.json") ||
            !fsCreateDir(directory)) goto finished;
    /* Exclusive create: this explicit smoke event never overwrites user source.
     * The fixture runs in a disposable portable install and retains artifacts. */
    stage = "exclusive_manifest_open";
    const int manifestFd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
        _S_IREAD | _S_IWRITE);
    if (manifestFd < 0) {
        snprintf(result.error, sizeof(result.error), "Exclusive manifest open failed: errno=%d", errno);
        goto finished;
    }
    stage = "manifest_stream";
    FILE *manifest = _fdopen(manifestFd, "wb");
    if (!manifest) {
        snprintf(result.error, sizeof(result.error), "Manifest stream failed: errno=%d", errno);
        _close(manifestFd);
        goto finished;
    }
    const char source[] = "{\"id\":\"menu_activation_retry_probe\","
        "\"name\":\"Menu activation retry probe\",\"version\":\"1.0.0\","
        "\"requires_restart\":true}\n";
    stage = "manifest_write";
    int written = fwrite(source, 1, sizeof(source) - 1, manifest) == sizeof(source) - 1;
    if (fclose(manifest) != 0) written = 0;
    if (!written) goto finished;
    stage = "discover_public_manifest";
    modmgrRescanDirectory();
    modinfo_t *mod = modmgrFindMod(id);
    if (!mod || !mod->valid || !mod->requires_restart || mod->enabled || mod->loaded)
        goto finished;
    int index = -1;
    for (int i = 0; i < modmgrGetCount(); ++i)
        if (modmgrGetMod(i) == mod) index = i;
    if (index < 0) goto finished;

    /* Pass zero publishes a new selection. Pass one proves an already-enabled
     * activation still saves, rather than silently taking the sync-only path. */
    for (int alreadyEnabled = 0; alreadyEnabled <= 1; ++alreadyEnabled) {
        stage = "prepare_installed_activation";
        if (!modmgrPrepareInstalledActivation(index, &plan, &result) || !plan ||
                result.activation_published != !alreadyEnabled || !mod->enabled)
            goto finished;
        stage = "activation_save_failure";
        saveAtomicDebugFailNextCommit();
        int applied = modmgrApplyPreparedChanges(plan, &result);
        int rejected = !applied && result.phase == MODMGR_APPLY_COMPONENT_SAVE &&
            !result.component.saved && !result.config.saved && !result.runtime_started &&
            result.activation_published == !alreadyEnabled && modmgrIsDirty() &&
            mod->enabled && !mod->loaded && modmgrProbeSavedEnabled(mod, alreadyEnabled);
        sysLogPrintf(rejected ? LOG_NOTE : LOG_WARNING,
            "MODMGR.ACTIVATION.PROBE: failed_save ok=%d already_enabled=%d published=%d enabled=%d loaded=%d dirty=%d runtime=%d",
            rejected, alreadyEnabled, result.activation_published, mod->enabled,
            mod->loaded, modmgrIsDirty(), result.runtime_started);
        if (!rejected) goto finished;
        stage = "activation_same_plan_retry";
        applied = modmgrApplyPreparedChanges(plan, &result);
        const unsigned required = MODMGR_SAVED_ENABLED_JSON | MODMGR_SAVED_MACHINE;
        int retried = applied && result.phase == MODMGR_APPLY_COMPLETE &&
            result.component.saved && (result.config.saved & required) == required &&
            result.runtime_completed && !modmgrIsDirty() &&
            result.activation_published == !alreadyEnabled &&
            result.restart_count > 0 && mod->pending_restart && mod->enabled && !mod->loaded &&
            modmgrProbeSavedEnabled(mod, 1);
        sysLogPrintf(retried ? LOG_NOTE : LOG_WARNING,
            "MODMGR.ACTIVATION.PROBE: retry ok=%d already_enabled=%d published=%d component_saved=%d config_saved=%u enabled=%d loaded=%d pending_restart=%d dirty=%d",
            retried, alreadyEnabled, result.activation_published, result.component.saved,
            result.config.saved, mod->enabled, mod->loaded, mod->pending_restart, modmgrIsDirty());
        if (!retried) goto finished;
        modmgrFreeApplyPlan(plan);
        plan = NULL;
    }
    ok = 1;
finished:
    modmgrFreeApplyPlan(plan);
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODMGR.ACTIVATION.PROBE: result=%s stage=%s detail='%s'",
        ok ? "PASS" : "FAIL", stage, result.error);
    return ok;
}

static int modmgrProbeLateActivationRetry(void)
{
    const char *folderId = "menu_activation_retry_probe";
    const char *archiveId = "menu_activation_late_probe";
    const char *stage = "load_restart_baseline";
    modmgr_apply_plan_t *plan = NULL;
    modmgr_apply_result_t result = {0};
    char archive[FS_MAXPATH + 1], held[FS_MAXPATH + 1];
    int moved = 0, ok = 0;
    /* Explicit sync establishes a real loaded restart package, without
     * pretending that a second persistent Apply simulates a process restart. */
    if (!modmgrSyncCatalogToRegistryChecked(&result)) goto finished;
    modinfo_t *folder = modmgrFindMod(folderId);
    if (!folder || !folder->loaded || !folder->enabled || !folder->requires_restart)
        goto finished;
    stage = "create_archive";
    const char *mods = modmgrGetModsDir();
    if (!mods || modmgrFindMod(archiveId) ||
            !assetPathJoinChecked(archive, sizeof(archive), mods, "/", "menu_activation_late_probe.pdmod") ||
            !assetPathJoinChecked(held, sizeof(held), mods, "/", "menu_activation_late_probe.held") ||
            fsFileSize(archive) >= 0 || fsFileSize(held) >= 0) goto finished;
    mod_archive_writer_t *writer = modArchiveBegin(archive);
    if (!writer) goto finished;
    const char source[] = "{\"id\":\"menu_activation_late_probe\","
        "\"name\":\"! Menu late activation probe\",\"version\":\"1.0.0\"}";
    if (modArchiveAddFileMem(writer, "mod.json", source, sizeof(source) - 1) != MODARCHIVE_OK) {
        modArchiveAbort(writer);
        goto finished;
    }
    if (modArchiveFinish(writer) != MODARCHIVE_OK) goto finished;
    modmgrRescanDirectory();
    folder = modmgrFindMod(folderId);
    modinfo_t *package = modmgrFindMod(archiveId);
    if (!folder || !folder->loaded || !package || !package->valid ||
            package->enabled || package->loaded) goto finished;
    int folderIndex = -1, archiveIndex = -1;
    for (int i = 0; i < modmgrGetCount(); ++i) {
        if (modmgrGetMod(i) == folder) folderIndex = i;
        if (modmgrGetMod(i) == package) archiveIndex = i;
    }
    /* The failing archive must be reached before the restart folder is reloaded. */
    if (archiveIndex < 0 || folderIndex < 0 || archiveIndex >= folderIndex) goto finished;
    modmgrSetEnabled(folderIndex, 0);
    stage = "prepare_late_activation";
    if (!modmgrPrepareInstalledActivation(archiveIndex, &plan, &result) || !plan)
        goto finished;
    stage = "move_archive";
    if (rename(archive, held) != 0) goto finished;
    moved = 1;
    stage = "late_package_failure";
    int applied = modmgrApplyPreparedChanges(plan, &result);
    const unsigned required = MODMGR_SAVED_ENABLED_JSON | MODMGR_SAVED_MACHINE;
    int rejected = !applied && result.phase == MODMGR_APPLY_PACKAGE_LOAD &&
        strcmp(result.id, archiveId) == 0 && result.runtime_started && !result.runtime_completed &&
        result.component.saved && (result.config.saved & required) == required &&
        result.activation_published && modmgrIsDirty() &&
        !folder->enabled && !folder->loaded && folder->pending_restart && package->enabled;
    sysLogPrintf(rejected ? LOG_NOTE : LOG_WARNING,
        "MODMGR.ACTIVATION.PROBE: late_failure ok=%d phase=%d published=%d runtime=%d completed=%d folder_enabled=%d folder_loaded=%d folder_restart=%d dirty=%d",
        rejected, result.phase, result.activation_published, result.runtime_started,
        result.runtime_completed, folder->enabled, folder->loaded, folder->pending_restart, modmgrIsDirty());
    if (rename(held, archive) != 0) goto finished;
    moved = 0;
    if (!rejected) goto finished;
    stage = "late_same_plan_retry";
    applied = modmgrApplyPreparedChanges(plan, &result);
    ok = applied && result.phase == MODMGR_APPLY_COMPLETE && result.runtime_completed &&
        result.activation_published && !modmgrIsDirty() &&
        !folder->enabled && folder->loaded && folder->pending_restart &&
        package->enabled && package->loaded && modmgrProbeSavedEnabled(folder, 0) &&
        modmgrProbeSavedEnabled(package, 1);
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODMGR.ACTIVATION.PROBE: late_retry ok=%d published=%d folder_enabled=%d folder_loaded=%d folder_restart=%d archive_loaded=%d dirty=%d",
        ok, result.activation_published, folder->enabled, folder->loaded,
        folder->pending_restart, package->loaded, modmgrIsDirty());
finished:
    if (moved && rename(held, archive) != 0) {
        stage = "restore_archive_failed";
        ok = 0;
    }
    modmgrFreeApplyPlan(plan);
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODMGR.ACTIVATION.PROBE: late_result=%s stage=%s detail='%s'",
        ok ? "PASS" : "FAIL", stage, result.error);
    return ok;
}

static int modmgrProbeReceivedActivationRetry(void)
{
    const char *id = "menu_activation_retry_probe";
    const char *stage = "received_queue";
    int queued = 0, ok = 0;
    if (fileTransferPendingModEnableCount() != 0) goto finished;
    modinfo_t *mod = modmgrFindMod(id);
    if (!mod || mod->enabled || !mod->loaded) goto finished;
    int index = -1;
    for (int i = 0; i < modmgrGetCount(); ++i)
        if (modmgrGetMod(i) == mod) index = i;
    if (index < 0) goto finished;
    for (int discard = 0; discard <= 1; ++discard) {
        modmgrSetEnabled(index, 0);
        if (fileTransferDebugQueueInstalledModForSmoke(id) != 0) goto finished;
        queued = 1;
        if (fileTransferPendingModEnableCount() != 1) goto finished;
        stage = "received_save_failure";
        saveAtomicDebugFailNextCommit();
        const int accepted = fileTransferPendingModEnableAccept();
        const int rejected = accepted == -1 && fileTransferPendingModEnableCount() == 1 &&
            fileTransferPendingModEnableError()[0] && fileTransferPendingModEnableHasEffects() &&
            mod->enabled && modmgrIsDirty();
        sysLogPrintf(rejected ? LOG_NOTE : LOG_WARNING,
            "MODMGR.FT.PROBE: failed_save ok=%d discard=%d pending=%d effects=%d enabled=%d dirty=%d",
            rejected, discard, fileTransferPendingModEnableCount(),
            fileTransferPendingModEnableHasEffects(), mod->enabled, modmgrIsDirty());
        if (!rejected) goto finished;
        if (!discard) {
            stage = "received_same_plan_retry";
            const int retried = fileTransferPendingModEnableAccept() == 0 &&
                fileTransferPendingModEnableCount() == 0 &&
                !fileTransferPendingModEnableError()[0] && mod->enabled && mod->loaded &&
                !modmgrIsDirty() && modmgrProbeSavedEnabled(mod, 1);
            sysLogPrintf(retried ? LOG_NOTE : LOG_WARNING,
                "MODMGR.FT.PROBE: retry ok=%d pending=%d enabled=%d loaded=%d dirty=%d",
                retried, fileTransferPendingModEnableCount(), mod->enabled, mod->loaded, modmgrIsDirty());
            if (!retried) goto finished;
        } else {
            stage = "received_explicit_discard";
            fileTransferPendingModEnableDecline();
            const int discarded = fileTransferPendingModEnableCount() == 0 &&
                !fileTransferPendingModEnableError()[0] && mod->enabled && modmgrIsDirty();
            sysLogPrintf(discarded ? LOG_NOTE : LOG_WARNING,
                "MODMGR.FT.PROBE: discard ok=%d pending=%d enabled=%d dirty=%d",
                discarded, fileTransferPendingModEnableCount(), mod->enabled, modmgrIsDirty());
            if (!discarded) goto finished;
        }
        queued = 0;
    }
    ok = 1;
finished:
    if (queued) fileTransferPendingModEnableDecline();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODMGR.FT.PROBE: result=%s stage=%s", ok ? "PASS" : "FAIL", stage);
    return ok;
}

int modmgrSmokeProbePreparedRetry(void)
{
    return modmgrProbeBaselineRetry() && modmgrProbeInstalledActivation() &&
        modmgrProbeLateActivationRetry() && modmgrProbeReceivedActivationRetry();
}
