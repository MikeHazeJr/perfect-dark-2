/**
 * updater.h -- Game update system (D13).
 *
 * Provides:
 *   - Background update checking via GitHub Releases API
 *   - Download with SHA-256 verification
 *   - Rename-on-restart self-replacement
 *   - Release channel support (stable / dev)
 *   - Version picker for rollback/pinning
 *
 * The update system runs on background threads and never blocks the game loop.
 * Poll updaterGetStatus() from the main thread to check progress.
 *
 * Threading: all state is mutex-protected. Safe to call from any thread,
 * but updaterTick() and updaterApplyPending() must be called from main thread.
 */

#ifndef _IN_UPDATER_H
#define _IN_UPDATER_H

#include <PR/ultratypes.h>
#include "updateversion.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Status / state
 * ======================================================================== */

typedef enum {
	UPDATER_IDLE = 0,          /* no operation in progress */
	UPDATER_CHECKING,          /* background thread: querying GitHub API */
	UPDATER_CHECK_DONE,        /* check complete — results available */
	UPDATER_CHECK_FAILED,      /* check failed (no network, API error, etc.) */
	UPDATER_DOWNLOADING,       /* background thread: downloading asset */
	UPDATER_DOWNLOAD_DONE,     /* download + verification complete */
	UPDATER_DOWNLOAD_FAILED,   /* download or verification failed */
} updater_status_t;

/* ========================================================================
 * Release info (one per available version)
 * ======================================================================== */

#define UPDATER_MAX_RELEASES     64
#define UPDATER_MAX_URL_LEN     512
#define UPDATER_MAX_TAG_LEN      64
#define UPDATER_MAX_NAME_LEN    128
#define UPDATER_MAX_BODY_LEN   2048

typedef struct {
	pdversion_t version;
	char        tag[UPDATER_MAX_TAG_LEN];           /* e.g., "client-v1.2.0" */
	char        name[UPDATER_MAX_NAME_LEN];         /* release title */
	char        body[UPDATER_MAX_BODY_LEN];         /* changelog/description */
	char        assetUrl[UPDATER_MAX_URL_LEN];      /* download URL for exe */
	char        hashUrl[UPDATER_MAX_URL_LEN];       /* download URL for .sha256 */
	s64         assetSize;                           /* bytes, 0 if unknown */
	s32         isPrerelease;                        /* 1 if dev/test channel */
	s32         isDraft;                             /* 1 if draft (skip) */
} updater_release_t;

/* ========================================================================
 * Download progress
 * ======================================================================== */

typedef struct {
	s64  bytesDownloaded;
	s64  bytesTotal;       /* 0 if unknown */
	f32  percent;          /* 0.0 - 100.0 */
} updater_progress_t;

/* ========================================================================
 * Configuration
 * ======================================================================== */

#define UPDATER_GITHUB_OWNER  "MikeHazeJr"
#define UPDATER_GITHUB_REPO   "perfect-dark-2"

/* Tag prefixes */
#define UPDATER_TAG_CLIENT    "client-v"
#define UPDATER_TAG_SERVER    "server-v"

/* Asset filenames to look for in releases (must match CMake OUTPUT_NAME + .exe) */
#define UPDATER_ASSET_CLIENT  "PerfectDark.exe"
#define UPDATER_ASSET_SERVER  "PerfectDarkServer.exe"

/* Staging file suffixes */
#define UPDATER_SUFFIX_UPDATE ".update"
#define UPDATER_SUFFIX_OLD    ".old"

/* ========================================================================
 * Lifecycle
 * ======================================================================== */

/**
 * Initialize the update system. Call once at startup, after fsInit().
 * Sets up mutexes, reads channel preference from config.
 */
void updaterInit(void);

/**
 * Shut down the update system. Cancels any in-progress operations,
 * joins background threads, frees resources.
 */
void updaterShutdown(void);

/**
 * Main-thread tick. Call once per frame (or once per second — not critical).
 * Processes completed background operations, fires callbacks.
 */
void updaterTick(void);

/* ========================================================================
 * Update checking
 * ======================================================================== */

/**
 * Start a background update check. Non-blocking.
 * Results available when updaterGetStatus() returns UPDATER_CHECK_DONE.
 */
void updaterCheckAsync(void);

/**
 * Get current status.
 */
updater_status_t updaterGetStatus(void);

/**
 * Get the number of available releases (valid after CHECK_DONE).
 * Filtered by current channel and tag prefix (client vs server).
 */
s32 updaterGetReleaseCount(void);

/**
 * Get a release by index (0 = newest). Valid after CHECK_DONE.
 * Returns NULL if index out of range.
 */
const updater_release_t *updaterGetRelease(s32 index);

/**
 * Get the latest release that is newer than the current build.
 * Returns NULL if already up to date (or no releases found).
 */
const updater_release_t *updaterGetLatest(void);

/**
 * Check if an update is available (convenience).
 */
s32 updaterIsUpdateAvailable(void);

/* ========================================================================
 * Downloading
 * ======================================================================== */

/**
 * Start downloading a specific release. Non-blocking.
 * Progress available via updaterGetProgress().
 * Completion indicated by UPDATER_DOWNLOAD_DONE or UPDATER_DOWNLOAD_FAILED.
 *
 * The release must be one returned by updaterGetRelease().
 */
void updaterDownloadAsync(const updater_release_t *release);

/**
 * Cancel an in-progress download.
 */
void updaterDownloadCancel(void);

/**
 * Get download progress. Only meaningful when status is UPDATER_DOWNLOADING.
 */
updater_progress_t updaterGetProgress(void);

/**
 * Get error message for the last failed operation.
 * Returns empty string if no error.
 */
const char *updaterGetError(void);

/* ========================================================================
 * Self-replacement
 * ======================================================================== */

/**
 * Check for and apply a pending update (.update file).
 * Call VERY EARLY in main(), before any other subsystem init.
 *
 * Flow:
 *   1. Check if "pd.x86_64.exe.update" exists
 *   2. Rename current exe to .old
 *   3. Rename .update to current exe name
 *   4. Re-exec the new binary (execv)
 *   5. On failure: attempt rollback from .old
 *
 * On Windows, the .old file is cleaned up on the NEXT launch
 * (since the running exe can be renamed but not deleted).
 *
 * Returns: 0 if no pending update, 1 if update was applied (caller should
 * expect re-exec and thus not reach this point), -1 on error.
 */
s32 updaterApplyPending(void);

/**
 * Clean up leftover .old files from a previous update.
 * Safe to call anytime — no-op if nothing to clean.
 */
void updaterCleanupOld(void);

/* ========================================================================
 * Channel management
 * ======================================================================== */

/**
 * Get the current release channel.
 */
update_channel_t updaterGetChannel(void);

/**
 * Set the release channel. Persisted to config on next save.
 */
void updaterSetChannel(update_channel_t channel);

/* ========================================================================
 * Version info
 * ======================================================================== */

/**
 * Get the current build version.
 */
const pdversion_t *updaterGetCurrentVersion(void);

/**
 * Get a human-readable version string for the current build.
 * Includes channel info, e.g., "1.2.3" or "1.2.3-dev.4".
 */
const char *updaterGetVersionString(void);

/**
 * Check if running as dedicated server (uses server-v* tags).
 */
s32 updaterIsServer(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_UPDATER_H */
