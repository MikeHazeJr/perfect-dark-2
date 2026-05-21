/* Boot Progress Channel (Engine Phase 2)
 *
 * Thread-safe progress reporting between the boot orchestrator (worker
 * thread) and the main thread's overlay renderer.  Internal state lives
 * in s_State; a single SDL mutex guards every read and write so the
 * snapshot the renderer sees is always self-consistent.
 *
 * Overall progress is computed by accumulating completed-phase weights
 * (s_PhaseWeights[]) plus the in-progress phase's fractional contribution
 * (current/total * weight).
 *
 * Engine Phase 5 (2026-05-03): the weight table is config-backed.  At
 * each boot we capture per-phase elapsed_ms via SDL_GetTicks; on
 * MarkComplete we recompute fractions and the config layer auto-saves
 * them at shutdown, so the next boot's bar paces from the previous
 * boot's actual timings instead of the hand-calibrated defaults.
 *
 * 2026-05-21 polish: in-code duration estimates let the overlay flow inside
 * phases that do not expose a reliable item count, and item-count phases are
 * cross-checked against wall-clock timing instead of only filling by list
 * position.  Keep these estimates out of pd.ini: the config registry is close
 * to its current 512-entry cap.
 *
 * Telemetry: when Boot.Telemetry pd.ini flag is non-zero, the boot
 * timing breakdown is logged at MarkComplete.  Default off.
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>
#include "boot_pool.h"     /* bootPoolGetWorkerCount for telemetry */
#include "boot_progress.h"
#include "config.h"
#include "system.h"

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
 * Engine Phase 5 (2026-05-03): mutable + config-backed via
 * Boot.Weight.<phase> pd.ini keys, so the next boot's bar paces from
 * the previous boot's actual timings.  Defaults are the hand-calibrated
 * Phase 2 values; first launch (no cached weights yet) falls through
 * cleanly. */
static float s_PhaseWeights[BOOT_PHASE_COUNT] = {
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

/* Wall-clock estimates per phase.  These are intentionally not config-backed
 * because configRegister* is capped at 512 entries in the current port. */
static float s_PhaseExpectedMs[BOOT_PHASE_COUNT] = {
    750.0f,  /* EXTRACT_FILES   */
    150.0f,  /* VERIFY_FILES    */
    25.0f,   /* EXTRACT_SEGS    */
    80.0f,   /* VERIFY_SEGS     */
    20.0f,   /* RELEASE_ROM     */
    250.0f,  /* CATALOG_INIT    */
    650.0f,  /* WALKER          */
    40.0f,   /* EMIT_WPN        */
    80.0f,   /* EMIT_MESH       */
    35.0f,   /* EMIT_ANIM       */
    30.0f,   /* EMIT_HEAD       */
    40.0f,   /* EMIT_BODY       */
    80.0f,   /* EMIT_ARENA      */
    120.0f,  /* EMIT_ANIMCHR    */
    250.0f,  /* EMIT_SFX        */
    35.0f,   /* EMIT_VOICE      */
    30.0f,   /* EMIT_SONG       */
    20.0f,   /* EMIT_FONT       */
    20.0f,   /* EMIT_LANG       */
    10.0f,   /* EMIT_UI         */
    100.0f,  /* BUILD_CACHES    */
    0.0f     /* READY           */
};

/* Engine Phase 5: per-phase timing capture for weight self-tuning +
 * telemetry.  s_PhaseStartMs[i] is non-zero between bootProgressBeginPhase
 * and bootProgressEndPhase for phase i; s_PhaseElapsedMs[i] is the
 * accumulated time across one boot. */
static u64 s_PhaseStartMs[BOOT_PHASE_COUNT];
static u64 s_PhaseElapsedMs[BOOT_PHASE_COUNT];

/* Boot.Telemetry pd.ini flag.  Non-zero -> dump per-phase elapsed_ms
 * at MarkComplete.  Default off so normal users don't see the chatter. */
static s32 s_TelemetryEnabled = 0;

/* Short keys per phase used to register Boot.Weight.<key>.  These are
 * stable identifiers (do NOT rename) so cached weights survive across
 * builds. */
static const char *k_PhaseConfigKey[BOOT_PHASE_COUNT] = {
    "extract_files",
    "verify_files",
    "extract_segs",
    "verify_segs",
    "release_rom",
    "catalog_init",
    "walker",
    "emit_wpn",
    "emit_mesh",
    "emit_anim",
    "emit_head",
    "emit_body",
    "emit_arena",
    "emit_animchr",
    "emit_sfx",
    "emit_voice",
    "emit_song",
    "emit_font",
    "emit_lang",
    "emit_ui",
    "build_caches",
    "ready"
};

/* Idempotent: registers each Boot.Weight.<phase> + Boot.Telemetry once.
 * Called at first bootProgressInit so configLoad has populated the
 * stored values before we read s_PhaseWeights for the bar fill. */
static void s_registerConfig(void)
{
    static int registered = 0;
    if (registered) return;
    registered = 1;

    for (int i = 0; i < BOOT_PHASE_COUNT; i++) {
        char key[48];
        snprintf(key, sizeof(key), "Boot.Weight.%s", k_PhaseConfigKey[i]);
        configRegisterFloat(key, &s_PhaseWeights[i], 0.0f, 1.0f);
    }
    configRegisterInt("Boot.Telemetry", &s_TelemetryEnabled, 0, 1);
}

static float s_computeOverall_locked(void)
{
    if (s_State.complete) {
        return 1.0f;
    }

    float overall = 0.0f;
    for (int i = 0; i < BOOT_PHASE_COUNT; i++) {
        if (s_State.completed_mask & (1u << i)) {
            overall += s_PhaseWeights[i];
        }
    }

    if (s_State.in_phase) {
        const float w = s_PhaseWeights[s_State.current_phase];
        if (w > 0.0f) {
            float frac = 0.0f;
            if (s_State.current_total > 0) {
                frac = (float)s_State.current_value / (float)s_State.current_total;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
            }
            if (s_State.current_phase >= 0 &&
                s_State.current_phase < BOOT_PHASE_COUNT &&
                s_PhaseStartMs[s_State.current_phase] != 0 &&
                s_PhaseExpectedMs[s_State.current_phase] > 1.0f) {
                u64 now = (u64)SDL_GetTicks();
                u64 elapsed = (now > s_PhaseStartMs[s_State.current_phase])
                    ? now - s_PhaseStartMs[s_State.current_phase] : 0;
                float timeFrac = (float)elapsed / s_PhaseExpectedMs[s_State.current_phase];
                if (timeFrac < 0.0f) timeFrac = 0.0f;
                if (timeFrac > 0.985f) timeFrac = 0.985f;
                if (timeFrac > frac) frac = timeFrac;
            }
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

    /* Engine Phase 5: register Boot.Weight.<phase> + Boot.Telemetry on
     * the first init.  configLoad has already run by this point (boot
     * orchestrator runs after configInit), so the weights array now
     * carries last-launch's measured fractions if pd.ini had them. */
    s_registerConfig();

    SDL_LockMutex(s_Mutex);
    memset(&s_State, 0, sizeof(s_State));
    s_State.active = 1;
    s_State.complete = 0;
    s_State.current_phase = BOOT_PHASE_EXTRACT_FILES;
    snprintf(s_State.phase_label, sizeof(s_State.phase_label), "%s",
             k_PhaseLabel[BOOT_PHASE_EXTRACT_FILES]);
    s_State.status_label[0] = '\0';
    /* Reset per-phase timing for this boot. */
    memset(s_PhaseStartMs,   0, sizeof(s_PhaseStartMs));
    memset(s_PhaseElapsedMs, 0, sizeof(s_PhaseElapsedMs));
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
    /* Engine Phase 5: capture phase start for weight self-tuning +
     * telemetry.  SDL_GetTicks rolls over after ~49 days of uptime;
     * we store as u64 so the math survives but never approach that
     * window during a boot. */
    s_PhaseStartMs[phase] = (u64)SDL_GetTicks();
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
    boot_phase_t phase = s_State.current_phase;
    /* Engine Phase 5: capture elapsed for this phase.  Defensive: only
     * update if BeginPhase recorded a start (non-zero); otherwise the
     * phase was skipped or never entered. */
    if (phase >= 0 && phase < BOOT_PHASE_COUNT && s_PhaseStartMs[phase] != 0) {
        u64 now = (u64)SDL_GetTicks();
        if (now > s_PhaseStartMs[phase]) {
            s_PhaseElapsedMs[phase] += now - s_PhaseStartMs[phase];
        }
        s_PhaseStartMs[phase] = 0;
    }
    s_State.completed_mask |= (1u << (unsigned)phase);
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

    /* Engine Phase 5: recompute per-phase weights from the timings we
     * captured this boot.  configSave at shutdown writes them to pd.ini
     * so the next launch's bar paces correctly. */
    u64 total = 0;
    for (int i = 0; i < BOOT_PHASE_COUNT; i++) {
        total += s_PhaseElapsedMs[i];
    }
    if (total > 0) {
        for (int i = 0; i < BOOT_PHASE_COUNT; i++) {
            float frac = (float)s_PhaseElapsedMs[i] / (float)total;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            s_PhaseWeights[i] = frac;
        }
    }

    /* Boot.Telemetry: emit a per-phase breakdown once at end of boot. */
    if (s_TelemetryEnabled) {
        sysLogPrintf(LOG_NOTE,
            "BOOT_TELEMETRY: total=%lums workers=%d phases:",
            (unsigned long)total, bootPoolGetWorkerCount());
        for (int i = 0; i < BOOT_PHASE_COUNT; i++) {
            if (s_PhaseElapsedMs[i] == 0 && i != BOOT_PHASE_READY) continue;
            float frac = (total > 0)
                ? (float)s_PhaseElapsedMs[i] / (float)total : 0.0f;
            sysLogPrintf(LOG_NOTE,
                "BOOT_TELEMETRY:   %-14s %6lums (%4.1f%%)",
                k_PhaseConfigKey[i],
                (unsigned long)s_PhaseElapsedMs[i],
                frac * 100.0f);
        }
    }

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
