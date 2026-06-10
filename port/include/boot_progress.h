#ifndef BOOT_PROGRESS_H
#define BOOT_PROGRESS_H

/* Boot Progress Channel (Engine Phase 2)
 *
 * Thread-safe progress channel that the boot orchestrator (worker thread)
 * uses to push phase + per-item updates to the main thread.  The main
 * thread snapshots the channel each frame and renders the overlay bar
 * + status label.
 *
 * Phase identifiers cover the full boot sequence (file extract -> verify
 * -> segment extract / verify -> ROM release -> catalog scaffolding ->
 * walker -> per-asset emitters -> runtime caches -> ready).  Each phase
 * carries a fixed weight in the overall 0->1 progress estimate; weights
 * sum to 1.0.  Phase 5 (telemetry) will replace the fixed weights with
 * last-launch timing.
 *
 * API split:
 *   - Producer side (worker thread): bootProgressBeginPhase /
 *     bootProgressUpdate / bootProgressUpdateLabel / bootProgressEndPhase /
 *     bootProgressMarkComplete.
 *   - Consumer side (main thread): bootProgressSnapshot /
 *     bootProgressIsComplete.
 *
 * All entry points are safe to call concurrently with each other; locking
 * is internal (SDL mutex).
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_PHASE_EXTRACT_FILES = 0,
    BOOT_PHASE_VERIFY_FILES,
    BOOT_PHASE_EXTRACT_SEGS,
    BOOT_PHASE_VERIFY_SEGS,
    BOOT_PHASE_RELEASE_ROM,
    BOOT_PHASE_CATALOG_INIT,
    BOOT_PHASE_WALKER,
    BOOT_PHASE_EMIT_WPN,
    BOOT_PHASE_EMIT_MESH,
    BOOT_PHASE_EMIT_ANIM,
    BOOT_PHASE_EMIT_HEAD,
    BOOT_PHASE_EMIT_BODY,
    BOOT_PHASE_EMIT_ARENA,
    BOOT_PHASE_EMIT_ANIMCHR,
    BOOT_PHASE_EMIT_SFX,
    BOOT_PHASE_EMIT_VOICE,
    BOOT_PHASE_EMIT_SONG,
    BOOT_PHASE_EMIT_FONT,
    BOOT_PHASE_EMIT_LANG,
    BOOT_PHASE_EMIT_UI,
    BOOT_PHASE_EMIT_TEXTURE,
    BOOT_PHASE_EMIT_META,
    BOOT_PHASE_BUILD_CACHES,
    BOOT_PHASE_READY,
    BOOT_PHASE_COUNT
} boot_phase_t;

#define BOOT_PROGRESS_LABEL_LEN 192

typedef struct {
    int          active;            /* 1 between Init and Shutdown / MarkComplete */
    int          complete;          /* 1 once MarkComplete called                  */
    boot_phase_t current_phase;     /* most recently begun phase                   */
    int          current_value;     /* current item index within phase             */
    int          current_total;     /* total items in phase (0 if not iterated)    */
    char         phase_label[BOOT_PROGRESS_LABEL_LEN];  /* human-readable phase    */
    char         status_label[BOOT_PROGRESS_LABEL_LEN]; /* human-readable item     */
    float        overall_progress;  /* 0.0 -> 1.0                                  */
} boot_progress_snapshot_t;

void bootProgressInit(void);
void bootProgressShutdown(void);

int  bootProgressIsActive(void);
int  bootProgressIsComplete(void);

void bootProgressBeginPhase(boot_phase_t phase);
void bootProgressUpdate(int current, int total);
void bootProgressUpdateLabel(const char *label);
void bootProgressEndPhase(void);
void bootProgressMarkComplete(void);

void bootProgressSnapshot(boot_progress_snapshot_t *out);

const char *bootProgressGetPhaseLabel(boot_phase_t phase);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_PROGRESS_H */
