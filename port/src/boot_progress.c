/* Boot Progress Channel (Engine Phase 2)
 *
 * Thread-safe progress reporting between the boot orchestrator (worker
 * thread) and the main thread's overlay renderer.  Internal state lives
 * in s_State; a single SDL mutex guards every read and write so the
 * snapshot the renderer sees is always self-consistent.
 *
 * Overall progress is computed by accumulating completed-phase weights
 * (k_PhaseWeight[]) plus the in-progress phase's fractional contribution
 * (current/total * weight).  Weights are calibrated against the timing
 * analysis in context/designs/engine/startup-acceleration.md; Phase 5
 * (telemetry) will drive them from last-launch measurements.
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "boot_progress.h"

static SDL_mutex *s_Mutex = NULL;

typedef struct {
    int           active;
    int           complete;
    boot_phase_t  current_phase;
    int           in_phase;            /* 1 between BeginPhase and EndPhase */
    int           current_value;
    int           current_total;
    char          phase_label[BOOT_PROGRESS_LABEL_LEN];
    char          status_label[BOOT_PROGRESS_LABEL_LEN];
    /* Bitmask of completed phases (one bit per boot_phase_t). */
    unsigned int  completed_mask;
} boot_progress_state_t;

static boot_progress_state_t s_State;

/* Static labels per phase (visible to the user in the overlay). */
static const char *k_PhaseLabel[BOOT_PHASE_COUNT] = {
    "Extracting ROM files",
    "Verifying assets",
    "Extracting ROM segments",
    "Verifying segments",
    "Releasing ROM mapping",
    "Building catalog",
    "Scanning per-asset content",
    "Extracting weapons",
    "Extracting meshes",
    "Extracting weapon animations",
    "Extracting heads",
    "Extracting bodies",
    "Extracting arenas",
    "Extracting character animations",
    "Extracting audio (SFX)",
    "Extracting audio (voice)",
    "Extracting audio (music)",
    "Extracting fonts",
    "Extracting languages",
    "Extracting UI textures",
    "Building runtime caches",
    "Ready"
};

/* Phase weights as fractions of total boot time.  Sum should be ~1.0.
 * Calibrated from the timing analysis in startup-acceleration.md.  These
 * are first-launch weights; on a cached boot most phases skip quickly so
 * the bar reaches the end well before nominal time, which is fine. */
static const float k_PhaseWeight[BOOT_PHASE_COUNT] = {
    0.20f,  /* EXTRACT_FILES   */
    0.30f,  /* VERIFY_FILES    */
    0.02f,  /* EXTRACT_SEGS    */
    0.03f,  /* VERIFY_SEGS     */
    0.01f,  /* RELEASE_ROM     */
    0.04f,  /* CATALOG_INIT    */
    0.05f,  /* WALKER          */
    0.03f,  /* EMIT_WPN        */
    0.04f,  /* EMIT_MESH       */
    0.02f,  /* EMIT_ANIM       */
    0.03f,  /* EMIT_HEAD       */
    0.03f,  /* EMIT_BODY       */
    0.02f,  /* EMIT_ARENA      */
    0.04f,  /* EMIT_ANIMCHR    */
    0.03f,  /* EMIT_SFX        */
    0.03f,  /* EMIT_VOICE      */
    0.03f,  /* EMIT_SONG       */
    0.01f,  /* EMIT_FONT       */
    0.01f,  /* EMIT_LANG       */
    0.01f,  /* EMIT_UI         */
    0.02f,  /* BUILD_CACHES    */
    0.00f   /* READY           */
};

static float s_computeOverall_locked(void)
{
    if (s_State.complete) {
        return 1.0f;
    }

    float overall = 0.0f;
    for (int i = 0; i < BOOT_PHASE_COUNT; i++) {
        if (s_State.completed_mask & (1u << i)) {
            overall += k_PhaseWeight[i];
        }
    }

    if (s_State.in_phase) {
        const float w = k_PhaseWeight[s_State.current_phase];
        if (s_State.current_total > 0 && w > 0.0f) {
            float frac = (float)s_State.current_value / (float)s_State.current_total;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            overall += w * frac;
        }
    }

    if (overall < 0.0f) overall = 0.0f;
    if (overall > 1.0f) overall = 1.0f;
    return overall;
}

void bootProgressInit(void)
{
    if (s_Mutex == NULL) {
        s_Mutex = SDL_CreateMutex();
    }

    SDL_LockMutex(s_Mutex);
    memset(&s_State, 0, sizeof(s_State));
    s_State.active = 1;
    s_State.complete = 0;
    s_State.current_phase = BOOT_PHASE_EXTRACT_FILES;
    snprintf(s_State.phase_label, sizeof(s_State.phase_label), "%s",
             k_PhaseLabel[BOOT_PHASE_EXTRACT_FILES]);
    s_State.status_label[0] = '\0';
    SDL_UnlockMutex(s_Mutex);
}

void bootProgressShutdown(void)
{
    if (s_Mutex == NULL) {
        return;
    }

    SDL_LockMutex(s_Mutex);
    s_State.active = 0;
    SDL_UnlockMutex(s_Mutex);

    SDL_DestroyMutex(s_Mutex);
    s_Mutex = NULL;
}

int bootProgressIsActive(void)
{
    if (s_Mutex == NULL) {
        return 0;
    }
    SDL_LockMutex(s_Mutex);
    int active = s_State.active;
    SDL_UnlockMutex(s_Mutex);
    return active;
}

int bootProgressIsComplete(void)
{
    if (s_Mutex == NULL) {
        return 1;
    }
    SDL_LockMutex(s_Mutex);
    int complete = s_State.complete;
    SDL_UnlockMutex(s_Mutex);
    return complete;
}

void bootProgressBeginPhase(boot_phase_t phase)
{
    if (s_Mutex == NULL || phase < 0 || phase >= BOOT_PHASE_COUNT) {
        return;
    }

    SDL_LockMutex(s_Mutex);
    s_State.current_phase = phase;
    s_State.in_phase = 1;
    s_State.current_value = 0;
    s_State.current_total = 0;
    snprintf(s_State.phase_label, sizeof(s_State.phase_label), "%s",
             k_PhaseLabel[phase]);
    s_State.status_label[0] = '\0';
    SDL_UnlockMutex(s_Mutex);
}

void bootProgressUpdate(int current, int total)
{
    if (s_Mutex == NULL) {
        return;
    }
    SDL_LockMutex(s_Mutex);
    s_State.current_value = current;
    s_State.current_total = total;
    SDL_UnlockMutex(s_Mutex);
}

void bootProgressUpdateLabel(const char *label)
{
    if (s_Mutex == NULL) {
        return;
    }
    SDL_LockMutex(s_Mutex);
    if (label) {
        snprintf(s_State.status_label, sizeof(s_State.status_label), "%s", label);
    } else {
        s_State.status_label[0] = '\0';
    }
    SDL_UnlockMutex(s_Mutex);
}

void bootProgressEndPhase(void)
{
    if (s_Mutex == NULL) {
        return;
    }
    SDL_LockMutex(s_Mutex);
    s_State.completed_mask |= (1u << (unsigned)s_State.current_phase);
    s_State.in_phase = 0;
    s_State.current_value = 0;
    s_State.current_total = 0;
    SDL_UnlockMutex(s_Mutex);
}

void bootProgressMarkComplete(void)
{
    if (s_Mutex == NULL) {
        return;
    }
    SDL_LockMutex(s_Mutex);
    s_State.complete = 1;
    s_State.in_phase = 0;
    s_State.current_phase = BOOT_PHASE_READY;
    snprintf(s_State.phase_label, sizeof(s_State.phase_label), "%s",
             k_PhaseLabel[BOOT_PHASE_READY]);
    s_State.status_label[0] = '\0';
    SDL_UnlockMutex(s_Mutex);
}

void bootProgressSnapshot(boot_progress_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }

    if (s_Mutex == NULL) {
        memset(out, 0, sizeof(*out));
        out->complete = 1;
        out->overall_progress = 1.0f;
        return;
    }

    SDL_LockMutex(s_Mutex);
    out->active           = s_State.active;
    out->complete         = s_State.complete;
    out->current_phase    = s_State.current_phase;
    out->current_value    = s_State.current_value;
    out->current_total    = s_State.current_total;
    out->overall_progress = s_computeOverall_locked();
    snprintf(out->phase_label,  sizeof(out->phase_label),
             "%s", s_State.phase_label);
    snprintf(out->status_label, sizeof(out->status_label),
             "%s", s_State.status_label);
    SDL_UnlockMutex(s_Mutex);
}

const char *bootProgressGetPhaseLabel(boot_phase_t phase)
{
    if (phase < 0 || phase >= BOOT_PHASE_COUNT) {
        return "";
    }
    return k_PhaseLabel[phase];
}
