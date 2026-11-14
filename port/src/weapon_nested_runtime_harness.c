#include <stdio.h>
#include <stdint.h>
#include <float.h>
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_weapon_slots.h"
#include "assetcatalog_model_slots.h"
#include "assetprovider.h"
#include "assetprovider_checkpoint.h"
#include "assetcatalog_anim_slots.h"
#include "assetcatalog_sound_slots.h"
#include "catalog_entry_snapshot.h"
#include "catalog_animation_generation.h"
#include "catalog_audio_generation.h"
#include "catalog_command_generation.h"
#include "catalog_texture_generation.h"
#include "catalog_model_generation.h"
#include "texture_source_runtime.h"
#include "assetcatalog_texture_slots.h"
#include "game/tex.h"
#include "game/texdecompress.h"
#include "modasset_compiler.h"
#include "audio.h"
#include "mod.h"
#include "data.h"
#include "lib/anim.h"
#include "sha256.h"
#include "modarchive.h"
#include "types.h"
#include "weapon_graph_runtime.h"
#include "weapon_graph_v2_runtime.h"
#include "game/game_0b0fd0.h"
#include "bss.h"
#include "constants.h"
#include "fs.h"
#include "lib/lib_317f0.h"
#include "lib/snd.h"
#include "game/bondgun.h"
#include "loader_pool.h"
#include "system.h"
#include "weapon_nested_runtime_harness.h"
#include "smoke_harness.h"
#include "game/prop.h"
#include "game/training.h"
#include "game/chr.h"
#include "game/inv.h"
#include "game/camera.h"
#include "game/chraction.h"
#include "lib/mtx.h"
#include "net/net.h"
#include "theater.h"
#include <math.h>
extern u32 var80070590; /* Real mpstats shot-region marker. */

static int graph_component_observe;
static int graph_observer_fault_test;
static wg_v2_observation graph_component_consumed, graph_observer_last;
static wg_v2_delivery graph_component_noise[2];
typedef struct graph_action_row {
    wg_v2_delivery identity, shot_receipt, noise_receipt;
    uint64_t press, last_reservation;
    uint64_t dispatch_scope;
    uintptr_t dispatch_destination;
    int phase, mode, busy, disposition, admitted, debits[2], noises, consumes, effects, hits, damage;
    int shot_begins, noise_begins, interrupted, stats;
    float noise_delta;
} graph_action_row;
static struct {
    graph_action_row rows[256];
    int count, failed, accounted[2][2][2], blocked[2][2];
    uint64_t events[WG_V2_OBS_SHOT_STATS + 1];
} graph_ledger;

/* Explicit smoke instrumentation. Admission and damage stay owned by the
 * production runtime and ray/chrHit/objHit path. No fabricated hit records. */
static struct {
    int equipped, retired, reentry, phase, cells[2][2], trigger[2], observed_hits;
    int qualified[2], context_switched, interrupted_shots, interrupted_hits, interruption_recovered;
    struct player *interrupted_owner;
    struct prop *oracle;
    uint64_t oracle_generation;
    int closed_shots, closed_ammo[2][2];
    float closed_noise[2];
    struct prop *target;
    uint64_t target_generation;
    int target_type;
    float before, expected_delta;
    int hitpart;
    int stale_after_consumed;
    float stale_damage;
    int retirement_pending, retirement_shots, retirement_hits;
    s32 retirement_regions[7];
    wg_v2_delivery retirement_receipt;
    wg_v2_runtime_snapshot retirement_native, retirement_other;
    float retirement_damage;
    uint64_t retirement_target_generation;
} graph_world;
static s32 harnessWriteBytes(const char *path, const void *bytes, size_t size);
static int harnessGraphUnchanged(struct player *, int, const wg_v2_runtime_snapshot *,
    int shots, const s32 regions[7], int hits, const struct gset *, float damage);

static int graphWorldArmed(void)
{
    return smokeHarnessIsActive() && (sysArgCheck("--debug-graph-world-hit-proof")
        || sysArgCheck("--debug-graph-world-target-qualify"));
}

int weaponGraphObservationArmed(void)
{
    return graph_component_observe || (smokeHarnessIsActive() && sysArgCheck("--debug-graph-world-hit-proof"));
}

static void graphLedgerFail(const char *reason)
{
    graph_ledger.failed = 1;
    if (graph_observer_fault_test) {
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.OBSERVER.NEGATIVE: expected_reject=1 reason=%s", reason);
        return;
    }
    sysLogPrintf(LOG_WARNING, "GRAPH.V2.LEDGER: result=FAIL reason=%s noise_events=%llu consumption_events=%llu", reason,
        (unsigned long long)graph_ledger.events[WG_V2_OBS_NOISE_INCREMENT],
        (unsigned long long)graph_ledger.events[WG_V2_OBS_CONSUMED]);
    if (!graph_observer_fault_test) smokeHarnessExit(1, "graph_world_accounting_failed");
}

static int graphSameBinding(const wg_v2_delivery *a, const wg_v2_delivery *b)
{
    return a->owner == b->owner && a->owner_generation == b->owner_generation
        && a->hand == b->hand && a->hand_generation == b->hand_generation
        && a->source_generation == b->source_generation;
}

static graph_action_row *graphLedgerAction(const wg_v2_delivery *r)
{
    for (int i = 0; i < graph_ledger.count; ++i) {
        graph_action_row *row = &graph_ledger.rows[i];
        if (row->admitted && graphSameBinding(&row->identity, r)
                && row->identity.action_ticket == r->action_ticket) return row;
    }
    return NULL;
}

static void graphLedgerConsumed(graph_action_row *row)
{
    const int slot = row->mode; /* The public world fixture authors primary=0, secondary=1. */
    if (row->disposition || row->consumes != 1 || row->debits[slot] != 1 || row->debits[1-slot]
            || row->noises != 1 || row->noise_begins != 1 || row->effects != 1 || row->hits != 1
            || row->stats != 1 || !row->shot_begins || !row->damage) {
        graphLedgerFail("missing_duplicate_or_unmatched_native_action"); return;
    }
    row->disposition = 1;
    graph_ledger.accounted[row->phase][row->identity.hand][row->mode] = 1;
    graph_world.cells[row->identity.hand][row->mode] = 1;
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.LEDGER: accounting=PASS phase=%d hand=%d mode=%d press=%llu ticket=%llu debit=1 other_debit=0 noise_increment=1 noise_delta=%g consumption=1 hit_effect=1 native_shots=1 interrupted=%d",
        row->phase, row->identity.hand, row->mode, (unsigned long long)row->press,
        (unsigned long long)row->identity.action_ticket, row->noise_delta, row->interrupted);
    if (graph_world.cells[0][0] && graph_world.cells[0][1] && graph_world.cells[1][0] && graph_world.cells[1][1])
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: both_hands_both_modes=PASS phase=%d actual_damage=1", row->phase);
}

/* Passive observer: no input, runtime mutation or synthetic acknowledgment. */
void weaponGraphObserve(const wg_v2_observation *event)
{
    if (!event) return;
    const wg_v2_runtime_snapshot *s = &event->value;
    const wg_v2_delivery *r = &event->receipt;
    if (graph_component_observe && !graph_observer_fault_test) {
        if (event->kind == WG_V2_OBS_NOISE_BEGIN && r->hand >= 0 && r->hand < 2)
            graph_component_noise[r->hand] = *r;
        if (event->kind == WG_V2_OBS_CONSUMED) graph_component_consumed = *event;
        return;
    }
    if (graph_observer_fault_test) graph_observer_last = *event;
    if (!weaponGraphObservationArmed()) return;
    if (event->kind < WG_V2_OBS_PRESS || event->kind > WG_V2_OBS_SHOT_STATS
            || graph_ledger.events[event->kind] == UINT64_MAX) { graphLedgerFail("event_counter_overflow"); return; }
    ++graph_ledger.events[event->kind];
    if (graph_ledger.failed) return;
    if (!s->host_present || r->hand < 0 || r->hand > 1 || s->mode < 0 || s->mode > 1 || !r->owner_generation
            || !r->hand_generation || !r->source_generation || !isfinite(s->noise_radius)) {
        graphLedgerFail("invalid_observation_identity_or_value"); return;
    }
    if ((event->kind == WG_V2_OBS_NOISE_INCREMENT || event->kind == WG_V2_OBS_HIT_EFFECT
                || event->kind == WG_V2_OBS_SHOT_STATS || event->kind == WG_V2_OBS_DEBIT)
            && (!graphSameBinding(r, &s->delivery) || r->action_ticket != s->delivery.action_ticket
                || ((event->kind != WG_V2_OBS_DEBIT) && !s->pending))) {
        graphLedgerFail("native_effect_with_obsolete_receipt"); return;
    }
    if ((event->kind == WG_V2_OBS_HIT_EFFECT || event->kind == WG_V2_OBS_SHOT_STATS)
            && (!r->reservation_generation || r->reservation_generation != s->delivery.reservation_generation)) {
        graphLedgerFail("native_effect_with_obsolete_shot_reservation"); return;
    }
    graph_action_row *row = NULL;
    if (event->kind == WG_V2_OBS_PRESS || event->kind == WG_V2_OBS_BUSY_PRESS) {
        if (!s->press_sequence || graph_ledger.count == 256) { graphLedgerFail("press_ledger_overflow"); return; }
        for (int i = 0; i < graph_ledger.count; ++i)
            if (graphSameBinding(&graph_ledger.rows[i].identity, r) && graph_ledger.rows[i].press == s->press_sequence) {
                graphLedgerFail("duplicate_press"); return;
            }
        row = &graph_ledger.rows[graph_ledger.count++]; row->identity = *r; row->identity.action_ticket = 0;
        row->press = s->press_sequence; row->phase = graph_world.phase; row->mode = s->mode;
        row->busy = event->kind == WG_V2_OBS_BUSY_PRESS;
        return;
    }
    if (event->kind == WG_V2_OBS_ADMITTED || event->kind == WG_V2_OBS_BLOCKED
            || event->kind == WG_V2_OBS_COALESCED || event->kind == WG_V2_OBS_RELEASED) {
        for (int i = 0; i < graph_ledger.count; ++i)
            if (graphSameBinding(&graph_ledger.rows[i].identity, r) && graph_ledger.rows[i].press == s->press_sequence)
                row = &graph_ledger.rows[i];
        if (!row || row->admitted || row->disposition) { graphLedgerFail("unmatched_press_disposition"); return; }
        if (event->kind == WG_V2_OBS_ADMITTED) {
            if (!r->action_ticket || row->phase > 1) { graphLedgerFail("invalid_admission"); return; }
            row->identity = *r; row->admitted = 1;
            for (int i = 0; i < graph_ledger.count; ++i) {
                const graph_action_row *prior = &graph_ledger.rows[i];
                if (prior->interrupted && prior->disposition == 1 && graphSameBinding(&prior->identity, r)
                        && r->action_ticket != prior->identity.action_ticket && row->press > prior->press) {
                    graph_world.interruption_recovered = 1;
                    sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: interruption_recovered=PASS same_binding=1 native_idle_admission=1 new_press_ticket=1");
                }
            }
        } else {
            row->disposition = event->kind == WG_V2_OBS_BLOCKED ? 2 : 3;
            if (event->kind == WG_V2_OBS_BLOCKED && row->phase == 2) graph_ledger.blocked[r->hand][row->mode] = 1;
        }
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.LEDGER: press=%llu hand=%d disposition=%s busy=%d",
            (unsigned long long)row->press, r->hand, event->kind == WG_V2_OBS_ADMITTED ? "admitted" :
            event->kind == WG_V2_OBS_BLOCKED ? "gate_blocked" : event->kind == WG_V2_OBS_RELEASED ? "released" : "coalesced", row->busy);
        return;
    }
    /* Native recovery ticks can have no live action and no debit. */
    if (event->kind == WG_V2_OBS_DEBIT && event->ammo_before[0] == event->ammo_after[0]
            && event->ammo_before[1] == event->ammo_after[1]) return;
    row = graphLedgerAction(r);
    if (row && event->kind == WG_V2_OBS_CONSUMED) ++row->consumes;
    if (row && event->kind == WG_V2_OBS_NOISE_INCREMENT) ++row->noises;
    if (!row || row->disposition) {
        if (event->kind == WG_V2_OBS_RETIRED && !s->pending) return;
        graphLedgerFail("unmatched_or_post_terminal_effect"); return;
    }
    switch (event->kind) {
    case WG_V2_OBS_DEBIT:
        for (int slot = 0; slot < 2; ++slot) {
            const int delta = event->ammo_before[slot] - event->ammo_after[slot];
            if (delta < 0 || delta > 1 || row->debits[slot] + delta > 1) { graphLedgerFail("duplicate_or_invalid_debit"); return; }
            row->debits[slot] += delta;
        }
        break;
    case WG_V2_OBS_SHOT_BEGIN:
        if (!r->reservation_generation || r->reservation_generation <= row->last_reservation) { graphLedgerFail("duplicate_shot_reservation"); return; }
        row->last_reservation = r->reservation_generation; row->shot_receipt = *r;
        row->dispatch_scope = event->scope_token; row->dispatch_destination = event->scope_destination;
        if (!row->dispatch_scope || !row->dispatch_destination) { graphLedgerFail("missing_dispatch_scope"); return; }
        if (++row->shot_begins > 16) graphLedgerFail("reservation_overflow");
        break;
    case WG_V2_OBS_NOISE_BEGIN:
        if (++row->noise_begins != 1 || !r->noise_generation) { graphLedgerFail("duplicate_noise_reservation"); return; }
        row->noise_receipt = *r; break;
    case WG_V2_OBS_NOISE_INCREMENT: {
        const float expected = fminf(event->noise_before + 3, event->maximum);
        if (row->noises != 1 || row->noise_begins != 1 || r->noise_generation != row->noise_receipt.noise_generation
                || !isfinite(event->noise_before) || !isfinite(event->noise_after) || !isfinite(event->increment)
                || !isfinite(event->maximum) || event->increment != 3 || event->maximum != 100
                || event->noise_before < 0 || event->maximum < event->noise_before
                || fabsf(expected - event->noise_after) > 0.0001f) { graphLedgerFail("noise_arithmetic_or_duplicate"); return; }
        row->noise_delta = event->noise_after - event->noise_before; break;
    }
    case WG_V2_OBS_EFFECT: if (++row->effects != 1) graphLedgerFail("duplicate_effect_commit"); break;
    case WG_V2_OBS_HIT_EFFECT: if (++row->hits != 1) graphLedgerFail("duplicate_hit_effect"); break;
    case WG_V2_OBS_SHOT_STATS:
        if (++row->stats != 1 || event->shots_delta != 1 || event->region_delta != 1) graphLedgerFail("native_shot_statistics");
        break;
    case WG_V2_OBS_CONSUMED: graphLedgerConsumed(row); break;
    case WG_V2_OBS_RETIRED:
        if (row->consumes || row->debits[row->mode] != 1 || row->debits[1-row->mode]
                || row->noises > 1 || row->effects || row->hits || row->stats) { graphLedgerFail("retirement_accounting"); return; }
        row->disposition = 4; break;
    default: graphLedgerFail("unexpected_event"); break;
    }
}

static int graphWorldEligible(struct prop *target)
{
    return target && target->type == PROPTYPE_CHR && !target->parent && target->lifecycle_generation
        && chrPtrIsValid(target->chr) && target->chr->aibot && CHRRACE(target->chr) == RACE_HUMAN
        && !(target->chr->chrflags & (CHRCFLAG_HIDDEN | CHRCFLAG_INVINCIBLE))
        && !target->chr->cutscene_protect && target->chr->actiontype != ACT_DIE && target->chr->actiontype != ACT_DEAD
        && target->chr->damage >= 0 && target->chr->damage < target->chr->maxdamage && chrGetShield(target->chr) == 0;
}

static int graphWorldQualifyAim(struct player *p, int hand)
{
    if (g_NetMode || theaterIsRecording() || theaterIsReplaying() || !g_Vars.normmplayerisrunning
            || g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0 || (g_MpSetup.options & MPOPTION_ONEHITKILLS)
            || !camGetWorldToScreenMtxf() || !camGetProjectionMtxF() || !g_Vars.onscreenprops
            || !g_Vars.endonscreenprops || !p->hands[hand].inuse) return 0;
    const float saved[2] = {p->crosspos[0], p->crosspos[1]};
    for (struct prop **ptr = g_Vars.onscreenprops; ptr < g_Vars.endonscreenprops; ++ptr) {
        struct prop *target = *ptr;
        if (graph_world.oracle && (target != graph_world.oracle || target->lifecycle_generation != graph_world.oracle_generation)) continue;
        if (!graphWorldEligible(target)) continue;
        for (int height = 30; height <= 110; height += 20) {
            struct coord position = target->pos, view, gunpos2d = {0}, direction2d, gunpos3d, direction3d;
            position.y += height;
            mtx4TransformVec(camGetWorldToScreenMtxf(), &position, &view);
            if (view.z >= -1) continue;
            float screen[2]; cam0f0b4d04(&view, screen);
            if (!isfinite(screen[0]) || !isfinite(screen[1]) || screen[0] < camGetScreenLeft()
                    || screen[0] > camGetScreenLeft() + camGetScreenWidth() || screen[1] < camGetScreenTop()
                    || screen[1] > camGetScreenTop() + camGetScreenHeight()) continue;
            cam0f0b4c3c(screen, &direction2d, 1);
            mtx4TransformVec(camGetProjectionMtxF(), &gunpos2d, &gunpos3d);
            mtx4RotateVec(camGetProjectionMtxF(), &direction2d, &direction3d);
            /* Real ordinary query/collision path qualifies a visible target
             * before effects. It never inserts hits or invokes damage APIs. */
            struct prop *queried = shotCalculateHits(hand, false, &gunpos2d, &direction2d,
                &gunpos3d, &direction3d, 0, 4294836224, true);
            while (queried && queried->parent) queried = queried->parent;
            if (queried != target) continue;
            if (!graph_world.oracle) {
                graph_world.oracle = target; graph_world.oracle_generation = target->lifecycle_generation;
                /* Explicit isolated fixture setup keeps one real bot alive;
                 * no damage/shield value or hit list is fabricated. */
                target->chr->maxdamage = 10000;
            }
            p->crosspos[0] = screen[0]; p->crosspos[1] = screen[1];
            if (!graph_world.qualified[hand]) {
                graph_world.qualified[hand] = 1;
                sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: target_aim_qualified=PASS hand=%d target_generation=%llu oracle=human_unshielded_normal_mp actual_query=1 health_setup=10000",
                    hand, (unsigned long long)target->lifecycle_generation);
            }
            return 1;
        }
    }
    p->crosspos[0] = saved[0]; p->crosspos[1] = saved[1];
    return 0;
}

int weaponGraphWorldHitAim(const wg_v2_delivery *receipt)
{
    if (!graphWorldArmed()) return 1;
    return receipt && wgV2RuntimeDeliveryValid(receipt) && g_Vars.currentplayer == receipt->owner
        && graphWorldQualifyAim(receipt->owner, receipt->hand);
}

void weaponGraphWorldHitTick(void)
{
    if (!graphWorldArmed() || graph_ledger.failed) return;
    struct player *p = g_Vars.currentplayer;
    if (!p || g_Vars.currentplayernum != 0 || p->isdead || !p->prop
            || !chrPtrIsValid(p->prop->chr)) return;
    const asset_entry_t *entry = assetCatalogResolve("graphproof:weapon");
    if (!entry || !wgV2RuntimeIsWeapon(entry->runtime_index)
            || p->gunctrl.weaponnum != entry->runtime_index) return;
    if (!graph_world.equipped) {
        /* Normal inventory/equip producers, never direct hand/gset/firing edits. */
        invGiveDoubleWeapon(entry->runtime_index, entry->runtime_index);
        bgunSetAmmoQuantity(AMMOTYPE_PISTOL, 80);
        bgunSetAmmoQuantity(AMMOTYPE_SMG, 80);
        bgunEquipWeapon2(HAND_LEFT, entry->runtime_index);
        graph_world.equipped = 1;
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: armed=1 equipped=dual source=graphproof:weapon");
        return;
    }
    if (sysArgCheck("--debug-graph-world-target-qualify")) {
        const float saved[2] = {p->crosspos[0], p->crosspos[1]};
        int qualified = wgV2RuntimeEnsureHand(p, 0) && wgV2RuntimeEnsureHand(p, 1)
            && graphWorldQualifyAim(p, 0) && graphWorldQualifyAim(p, 1);
        p->crosspos[0] = saved[0]; p->crosspos[1] = saved[1];
        if (qualified) smokeHarnessExit(0, "graph_world_target_qualified");
        return;
    }
    if (graph_world.phase < 2 && graph_world.cells[0][0] && graph_world.cells[0][1]
            && graph_world.cells[1][0] && graph_world.cells[1][1]) {
        if (wgV2RuntimeDeliveryPending(p, 0) || wgV2RuntimeDeliveryPending(p, 1)
                || p->hands[0].triggeron || p->hands[1].triggeron) return;
        char public_path[FS_MAXPATH];
        fsFullPath("./mods/mod_graph_world/weapons/graphproof.pdweapon", public_path, sizeof(public_path));
        const char *source_path = entry->source.primary.provider == fileProvider()
            ? fileProviderPath(entry->source.primary) : NULL;
        u32 size = 0;
        void *bytes = fsFileLoad(graph_world.phase == 0
            ? "./asset-mesh-ingress/world_edit.pdweapon" : "./asset-mesh-ingress/world_closed.pdweapon", &size);
        int changed = bytes && source_path && !strcmp(source_path, public_path)
            && harnessWriteBytes(public_path, bytes, size)
            && catalogPrepareTypedAssetReplacement(ASSET_WEAPON, "graphproof:weapon")
            && catalogLoadTypedAsset(ASSET_WEAPON, "graphproof:weapon");
        sysMemFree(bytes);
        if (!changed || !wgV2RuntimeEnsureHand(p, 0) || !wgV2RuntimeEnsureHand(p, 1)) {
            smokeHarnessExit(1, "graph_world_public_source_replacement_failed"); return;
        }
        ++graph_world.phase;
        memset(graph_world.cells, 0, sizeof(graph_world.cells));
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: public_source_replacement=PASS phase=%d", graph_world.phase);
        if (graph_world.phase == 2) {
            graph_world.closed_shots = frGetData()->numshots;
            for (int hand = 0; hand < 2; ++hand) {
                graph_world.trigger[hand] = p->hands[hand].triggeron;
                graph_world.closed_noise[hand] = p->hands[hand].noiseradius;
                for (int slot = 0; slot < 2; ++slot) graph_world.closed_ammo[hand][slot] = p->hands[hand].loadedammo[slot];
            }
        }
    }
    if (graph_world.phase == 2) {
        if (frGetData()->numshots != graph_world.closed_shots) {
            smokeHarnessExit(1, "graph_world_closed_gate_shot_statistics"); return;
        }
        for (int hand = 0; hand < 2; ++hand) {
            if (p->hands[hand].loadedammo[0] != graph_world.closed_ammo[hand][0]
                    || p->hands[hand].loadedammo[1] != graph_world.closed_ammo[hand][1]
                    || p->hands[hand].noiseradius > graph_world.closed_noise[hand]
                    || wgV2RuntimeDeliveryPending(p, hand)) {
                smokeHarnessExit(1, "graph_world_closed_gate_native_effect"); return;
            }
            if (p->hands[hand].triggeron && !graph_world.trigger[hand] && p->hands[hand].gset.weaponfunc < 2)
                graph_world.cells[hand][p->hands[hand].gset.weaponfunc] = 1;
            graph_world.trigger[hand] = p->hands[hand].triggeron;
        }
        if (graph_ledger.blocked[0][0] && graph_ledger.blocked[0][1]
                && graph_ledger.blocked[1][0] && graph_ledger.blocked[1][1]) {
            for (int i = 0; i < graph_ledger.count; ++i) {
                if (!graph_ledger.rows[i].disposition) { graphLedgerFail("unresolved_press_or_action"); return; }
            }
            if (!graph_world.retired || graph_world.retirement_pending || !graph_world.context_switched
                    || !graph_world.interruption_recovered || !graph_world.stale_after_consumed) {
                graphLedgerFail("missing_retirement_or_interruption"); return;
            }
            sysLogPrintf(LOG_NOTE, "GRAPH.V2.LEDGER: reconciled=PASS missing=0 duplicate=0 unmatched=0 overflow=0");
            sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: closed_gate=PASS both_hands_both_modes=1 ammo_noise_shots_unchanged=1");
            smokeHarnessExit(0, "graph_world_hit_pass");
        }
    }
}

void weaponGraphWorldHitDispatch(const wg_v2_delivery *receipt, struct gset *gset)
{
    if (!graphWorldArmed() || graph_ledger.failed || !receipt || graph_world.reentry) return;
    if (graph_world.phase == 2) { smokeHarnessExit(1, "graph_world_closed_gate_dispatch"); return; }
    if (!wgV2RuntimeDeliveryValid(receipt) || !receipt->owner->prop
            || !chrPtrIsValid(receipt->owner->prop->chr)) return;
    const int shots = frGetData()->numshots;
    const int observed_hits = graph_world.observed_hits;
    const int cloakpause = receipt->owner->prop->chr->cloakpause;
    s32 shotcounts[7]; memcpy(shotcounts, g_Vars.currentplayerstats->shotcount, sizeof(shotcounts));
    const u32 hidden = receipt->owner->prop->chr->hidden;
    const int ammo0 = receipt->owner->hands[receipt->hand].loadedammo[0];
    const int ammo1 = receipt->owner->hands[receipt->hand].loadedammo[1];
    graph_action_row *current_row = graphLedgerAction(receipt);
    if (!graph_world.stale_after_consumed && current_row) {
        for (int i = 0; i < graph_ledger.count; ++i) {
            const graph_action_row *old = &graph_ledger.rows[i];
            if (old->disposition != 1 || !graphSameBinding(&old->identity, receipt)
                    || old->identity.action_ticket == receipt->action_ticket
                    || old->dispatch_destination != current_row->dispatch_destination) continue;
            wg_v2_runtime_snapshot before = {0};
            const float selected_damage = gsetGetDamage(gset);
            if (!graph_world.oracle || graph_world.oracle->lifecycle_generation != graph_world.oracle_generation
                    || !chrPtrIsValid(graph_world.oracle->chr)) { graphLedgerFail("stale_replay_damage_baseline"); return; }
            graph_world.stale_damage = graph_world.oracle->chr->damage;
            if (!wgV2RuntimeSnapshot(receipt->owner, receipt->hand, &before) || !before.pending
                    || !before.shot_reserved || !before.temporary_scopes || var80070590) {
                graphLedgerFail("stale_replay_live_b_baseline"); return;
            }
            for (int repeat = 0; repeat < 3; ++repeat) {
                wgV2RuntimeShotComplete(&old->shot_receipt);
                if (!harnessGraphUnchanged(receipt->owner, receipt->hand, &before, shots, shotcounts, observed_hits, gset, selected_damage)) { graphLedgerFail("stale_shot_completion"); return; }
                wgV2RuntimeNoiseComplete(&old->noise_receipt);
                if (!harnessGraphUnchanged(receipt->owner, receipt->hand, &before, shots, shotcounts, observed_hits, gset, selected_damage)) { graphLedgerFail("stale_noise_completion"); return; }
                wgV2RuntimeShotAbort(&old->shot_receipt);
                if (!harnessGraphUnchanged(receipt->owner, receipt->hand, &before, shots, shotcounts, observed_hits, gset, selected_damage)) { graphLedgerFail("stale_shot_abort"); return; }
                if (wgV2RuntimeShotEffectsBegin(&old->shot_receipt)
                        || !harnessGraphUnchanged(receipt->owner, receipt->hand, &before, shots, shotcounts, observed_hits, gset, selected_damage)) { graphLedgerFail("stale_effect_commit"); return; }
                wgV2RuntimeScopeClose(old->dispatch_scope);
                if (!harnessGraphUnchanged(receipt->owner, receipt->hand, &before, shots, shotcounts, observed_hits, gset, selected_damage)) { graphLedgerFail("stale_scope_close"); return; }
            }
            graph_world.stale_after_consumed = 1;
            sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: post_consumed_stale_replay=PASS same_binding=1 new_ticket=1 reused_live_scope_storage=1 callbacks_separate_repeated=1 native_effects_unchanged=1");
            break;
        }
    }
    graph_world.reentry = 1;
    handTickAttack(receipt->hand);
    graph_world.reentry = 0;
    if (frGetData()->numshots != shots || receipt->owner->prop->chr->hidden != hidden
            || receipt->owner->prop->chr->cloakpause != cloakpause
            || memcmp(shotcounts, g_Vars.currentplayerstats->shotcount, sizeof(shotcounts))
            || graph_world.observed_hits != observed_hits
            || receipt->owner->hands[receipt->hand].loadedammo[0] != ammo0
            || receipt->owner->hands[receipt->hand].loadedammo[1] != ammo1
            || !wgV2RuntimeDeliveryValid(receipt)) {
        smokeHarnessExit(1, "graph_world_reentry_side_effect"); return;
    }
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: reentrant_dispatch=PASS hand=%d mode=%d", receipt->hand, gset->weaponfunc);
    if (!graph_world.retired) {
        /* One actual synchronous dispatch callback retires its binding. The
         * retained gset remains readable while the obsolete permission fails. */
        const float damage = gsetGetDamage(gset);
        if (graph_world.retirement_pending || !wgV2RuntimeSnapshot(receipt->owner, receipt->hand, &graph_world.retirement_native)
                || !wgV2RuntimeSnapshot(receipt->owner, 1-receipt->hand, &graph_world.retirement_other)
                || !graph_world.oracle || graph_world.oracle->lifecycle_generation != graph_world.oracle_generation
                || !chrPtrIsValid(graph_world.oracle->chr)) { graphLedgerFail("retirement_baseline_missing"); return; }
        graph_world.retirement_receipt = *receipt;
        graph_world.retirement_damage = graph_world.oracle->chr->damage;
        graph_world.retirement_target_generation = graph_world.oracle_generation;
        graph_world.retirement_shots = shots; graph_world.retirement_hits = observed_hits;
        memcpy(graph_world.retirement_regions, shotcounts, sizeof(shotcounts));
        wgV2RuntimeRetireHand(receipt->owner, receipt->hand, "armed world dispatch retirement");
        if (!wgV2RuntimeEnsureHand(receipt->owner, receipt->hand)
                || wgV2RuntimeDeliveryValid(receipt) || gsetGetDamage(gset) != damage
                || frGetData()->numshots != shots || receipt->owner->prop->chr->cloakpause != cloakpause
                || memcmp(shotcounts, g_Vars.currentplayerstats->shotcount, sizeof(shotcounts))
                || graph_world.observed_hits != observed_hits) {
            smokeHarnessExit(1, "graph_world_dispatch_retirement_failed"); return;
        }
        wgV2RuntimeShotComplete(receipt); wgV2RuntimeNoiseComplete(receipt);
        graph_world.retirement_pending = 1;
    }
}

static int graphWorldDamage(struct prop *root, float *damage)
{
    if (root->type == PROPTYPE_CHR || root->type == PROPTYPE_PLAYER) {
        if (!chrPtrIsValid(root->chr)) return 0;
        *damage = root->chr->damage; return 1;
    }
    if ((root->type == PROPTYPE_OBJ || root->type == PROPTYPE_WEAPON || root->type == PROPTYPE_DOOR) && root->obj) {
        *damage = root->obj->damage; return 1;
    }
    return 0;
}

void weaponGraphWorldHitObserve(const wg_v2_delivery *receipt, struct gset *gset,
        struct hit *record, int after)
{
    if (!graphWorldArmed() || graph_ledger.failed || !receipt || !record || !record->prop) return;
    struct prop *hit = record->prop;
    if (!after) {
        ++graph_world.observed_hits;
        while (hit->parent) hit = hit->parent;
        graph_world.target = NULL;
        if (hit != graph_world.oracle || hit->lifecycle_generation != graph_world.oracle_generation
                || !graphWorldEligible(hit) || !graphWorldDamage(hit, &graph_world.before)) return;
        /* Independent authored expectation, restricted to unshielded human
         * bots in normal MP with no one-hit kills or special function flags.
         * chrDamage doubles torso, quadruples head, maps general -> torso/2
         * and generalhalf -> torso/4. Other body limbs apply the input once. */
        float multiplier = 1;
        if (record->hitpart == HITPART_HEAD) multiplier = 4;
        else if (record->hitpart == HITPART_TORSO) multiplier = 2;
        else if (record->hitpart == HITPART_GENERALHALF) multiplier = 0.5f;
        else if (record->hitpart != HITPART_GENERAL && (record->hitpart < HITPART_LFOOT || record->hitpart > HITPART_RBICEP)) return;
        if (receipt->hand < 0 || receipt->hand > 1 || gset->weaponfunc > 1 || graph_world.phase > 1) return;
        const float authored = gset->weaponfunc == 0 ? (graph_world.phase == 0 ? 3 : 6) : (graph_world.phase == 0 ? 9 : 12);
        struct weaponfunc *func = gsetGetWeaponFunction(gset);
        if (!func || func->flags || gsetGetDamage(gset) != authored) {
            smokeHarnessExit(1, "graph_world_oracle_function_mismatch"); return;
        }
        graph_world.expected_delta = authored * multiplier; graph_world.hitpart = record->hitpart;
        if (graph_world.before + graph_world.expected_delta >= hit->chr->maxdamage) return;
        graph_world.target = hit; graph_world.target_generation = hit->lifecycle_generation;
        graph_world.target_type = hit->type;
        return;
    }
    /* Compare allocation identity before touching the union after damage,
     * which can synchronously free or replace an ordinary target. */
    struct prop *root = graph_world.target; float damage;
    graph_world.target = NULL;
    if (!root || root->lifecycle_generation != graph_world.target_generation
            || root->type != graph_world.target_type || !graphWorldDamage(root, &damage)
            || !wgV2RuntimeDeliveryValid(receipt)) return;
    if (receipt->hand < 0 || receipt->hand > 1 || gset->weaponfunc > 1) return;
    const float delta = damage - graph_world.before;
    if (fabsf(delta - graph_world.expected_delta) > 0.0001f * graph_world.expected_delta) {
        sysLogPrintf(LOG_WARNING, "GRAPH.V2.WORLD: applied_delta=FAIL expected=%g actual=%g hitpart=%d", graph_world.expected_delta, delta, graph_world.hitpart);
        smokeHarnessExit(1, "graph_world_applied_damage_mismatch"); return;
    }
    graph_action_row *row = graphLedgerAction(receipt);
    if (!row || row->damage || row->disposition || !isfinite(damage) || !isfinite(delta)
            || !isfinite(graph_world.before) || !isfinite(graph_world.expected_delta)) {
        graphLedgerFail("duplicate_or_nonfinite_damage"); return;
    }
    row->damage = 1;
    const float expected = gset->weaponfunc == 0 ? (graph_world.phase == 0 ? 3 : 6) : (graph_world.phase == 0 ? 9 : 12);
    if (gsetGetDamage(gset) != expected) { smokeHarnessExit(1, "graph_world_selected_source_damage_mismatch"); return; }
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: damage=PASS phase=%d hand=%d mode=%d source_generation=%llu ticket=%llu selected_damage=%g delta=%g target_generation=%llu expected_delta=%g hitpart=%d oracle=human_unshielded_normal_mp",
        graph_world.phase, receipt->hand, gset->weaponfunc, (unsigned long long)receipt->source_generation,
        (unsigned long long)receipt->action_ticket, gsetGetDamage(gset), damage - graph_world.before,
        (unsigned long long)graph_world.target_generation, graph_world.expected_delta, graph_world.hitpart);
    if (!graph_world.context_switched) {
        /* Actual damage has happened. Lose the current-player context before
         * returning through the production interruption/abort closeout. */
        graph_world.context_switched = 1; graph_world.interrupted_owner = receipt->owner;
        row->interrupted = 1;
        graph_world.interrupted_shots = frGetData()->numshots; graph_world.interrupted_hits = graph_world.observed_hits;
        g_Vars.currentplayer = NULL;
    }
}

void weaponGraphWorldHitCloseout(const wg_v2_delivery *receipt)
{
    if (!graphWorldArmed() || graph_ledger.failed || !receipt) return;
    if (graph_world.retirement_pending && graphSameBinding(receipt, &graph_world.retirement_receipt)
            && receipt->action_ticket == graph_world.retirement_receipt.action_ticket) {
        wg_v2_runtime_snapshot current = {0}, other = {0};
        graph_action_row *row = graphLedgerAction(receipt);
        const wg_v2_runtime_snapshot *before = &graph_world.retirement_native;
        if (!wgV2RuntimeSnapshot(receipt->owner, receipt->hand, &current)
                || !wgV2RuntimeSnapshot(receipt->owner, 1-receipt->hand, &other)
                || before->temporary_scopes < 2 || other.temporary_scopes != before->temporary_scopes - 2
                || current.pending || wgV2RuntimeDeliveryValid(receipt) || var80070590
                || current.ammo[0] != before->ammo[0] || current.ammo[1] != before->ammo[1]
                || current.noise_radius != before->noise_radius || !row || row->disposition != 4
                || frGetData()->numshots != graph_world.retirement_shots
                || graph_world.observed_hits != graph_world.retirement_hits
                || memcmp(graph_world.retirement_regions, g_Vars.currentplayerstats->shotcount, sizeof(graph_world.retirement_regions))
                || !graph_world.oracle || graph_world.oracle->lifecycle_generation != graph_world.retirement_target_generation
                || !chrPtrIsValid(graph_world.oracle->chr) || graph_world.oracle->chr->damage != graph_world.retirement_damage) {
            graphLedgerFail("retirement_after_outer_return"); return;
        }
        other.temporary_scopes = graph_world.retirement_other.temporary_scopes;
        if (memcmp(&other, &graph_world.retirement_other, sizeof(other))) { graphLedgerFail("retirement_cross_hand_effect"); return; }
        graph_world.retirement_pending = 0; graph_world.retired = 1;
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: dispatch_retirement=PASS old_permission=denied after_outer_return=1 subsequent_damage_shots_hits=0 scopes_closed=1 shot_region_clear=1 other_hand_unchanged=1 committed_ammo_noise_preserved=1");
    }
    if (!graph_world.interrupted_owner) return;
    struct player *owner = graph_world.interrupted_owner;
    graph_world.interrupted_owner = NULL;
    g_Vars.currentplayer = owner; // Restore only the one explicitly changed root.
    if (!receipt || receipt->owner != owner || wgV2RuntimeDeliveryValid(receipt)) {
        smokeHarnessExit(1, "graph_world_interruption_still_retryable"); return;
    }
    struct gset retry = {0}; uint64_t retry_scope = 0; wg_v2_delivery retry_receipt = {0};
    int admitted = wgV2RuntimeShotBegin(owner, receipt->hand, &retry, &retry_scope, &retry_receipt);
    wgV2RuntimeScopeClose(retry_scope);
    if (admitted || retry_scope || retry_receipt.owner) { smokeHarnessExit(1, "graph_world_interruption_retry_admitted"); return; }
    handTickAttack(receipt->hand);
    if (frGetData()->numshots != graph_world.interrupted_shots || graph_world.observed_hits != graph_world.interrupted_hits) {
        smokeHarnessExit(1, "graph_world_interruption_repeated_effect"); return;
    }
    wg_v2_runtime_snapshot terminal = {0};
    graph_action_row *row = graphLedgerAction(receipt);
    if (!wgV2RuntimeSnapshot(owner, receipt->hand, &terminal) || terminal.pending || terminal.firing
            || !row || row->consumes != 1 || row->disposition != 1 || var80070590 || terminal.temporary_scopes) {
        graphLedgerFail("interruption_not_consumed_once"); return;
    }
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.WORLD: context_loss_after_actual_damage=PASS terminal=1 retry_denied=1 repeated_effects=0");
}

typedef struct harness_deps {
    char (*ids)[CATALOG_ID_LEN];
    asset_type_e *types;
    size_t count;
    size_t capacity;
    s32 invalid;
} harness_deps_t;

static void harnessCollectDep(const char *id, void *userdata)
{
    harness_deps_t *deps = (harness_deps_t *)userdata;
    const asset_entry_t *entry;
    if (!deps || !id || !id[0]) {
        if (deps) deps->invalid = 1;
        return;
    }
    if (deps->count == deps->capacity) {
        size_t next = deps->capacity ? deps->capacity * 2 : 16;
        if (next < deps->count + 1
                || next > SIZE_MAX / sizeof(*deps->ids)
                || next > SIZE_MAX / sizeof(*deps->types)) {
            deps->invalid = 1;
            return;
        }
        char (*ids)[CATALOG_ID_LEN] = malloc(next * sizeof(*deps->ids));
        asset_type_e *types = malloc(next * sizeof(*deps->types));
        if (!ids || !types) {
            free(ids);
            free(types);
            deps->invalid = 1;
            return;
        }
        if (deps->count) {
            memcpy(ids, deps->ids, deps->count * sizeof(*deps->ids));
            memcpy(types, deps->types, deps->count * sizeof(*deps->types));
        }
        free(deps->ids);
        free(deps->types);
        deps->ids = ids;
        deps->types = types;
        deps->capacity = next;
    }
    entry = assetCatalogResolve(id);
    if (!entry) {
        deps->invalid = 1;
        return;
    }
    strncpy(deps->ids[deps->count], id, CATALOG_ID_LEN - 1);
    deps->ids[deps->count][CATALOG_ID_LEN - 1] = '\0';
    deps->types[deps->count] = entry->type;
    deps->count++;
}

static s32 harnessFind(const harness_deps_t *deps, const char *id,
                       asset_type_e type)
{
    if (!deps || !id) return -1;
    for (size_t i = 0; i < deps->count; i++) {
        if (deps->types[i] == type && strcmp(deps->ids[i], id) == 0) return i;
    }
    return -1;
}

static asset_entry_t *harnessRegisterOwner(const char *owner,
                                           const char *archive)
{
    const asset_entry_t *donor = assetCatalogResolve("base:ar34");
    if (!donor || donor->type != ASSET_WEAPON || donor->runtime_index < 0)
        return NULL;
    asset_entry_t *entry = assetCatalogRegisterWeapon(owner, donor->mp_index,
        "Nested runtime harness", "", 0);
    if (!entry) return NULL;
    donor = assetCatalogResolve("base:ar34");
    if (!donor) {
        assetCatalogUnregister(owner);
        return NULL;
    }
    /* The disposable alias borrows AR34's fully bound public-member metadata
     * so the generic runtime adapter can activate. Its primary graph source
     * remains the fixture archive below and the process exits after proof. */
    entry->ext.weapon = donor->ext.weapon;
    entry->runtime_index = donor->runtime_index;
    entry->mp_index = donor->mp_index;
    entry->bundled = 0;
    entry->enabled = 1;
    entry->temporary = 1;
    catalogSetPrimaryFile(entry, archive);
    return entry;
}

static s32 harnessDropDependency(const char *id, asset_type_e type, void *userdata)
{
    (void)id; (void)type; (void)userdata;
    return 0;
}

static s32 harnessCleanup(const char *owner, const harness_deps_t *deps)
{
    s32 ok = 1;
    const asset_entry_t *root = assetCatalogResolve(owner);
    /* Retire payloads while every typed edge still resolves. The nested
     * registration order is not a valid dependency teardown order. */
    if (root && !catalogDeactivateTypedAsset(root->type, owner)) ok = 0;
    if (deps) {
        for (size_t i = deps->count; i-- > 0; ) {
            const asset_entry_t *entry = assetCatalogResolve(deps->ids[i]);
            if (entry && !catalogDeactivateTypedAsset(entry->type, deps->ids[i])) ok = 0;
        }
    }
    /* All fixture rows are now inactive. Detach the complete fixture graph
     * before deleting rows so shared/deep children never leave dangling edges. */
    if (ok) {
        catalogDepPruneOwner(owner, harnessDropDependency, NULL);
        for (size_t i = 0; deps && i < deps->count; i++) {
            catalogDepPruneOwner(deps->ids[i], harnessDropDependency, NULL);
        }
        for (size_t i = 0; deps && i < deps->count; i++) {
            if (assetCatalogResolve(deps->ids[i]) && !assetCatalogUnregister(deps->ids[i])) ok = 0;
        }
        if (assetCatalogResolve(owner) && !assetCatalogUnregister(owner)) ok = 0;
    }
    if (!ok) sysLogPrintf(LOG_WARNING,
        "PDWEAPON.NESTED.HARNESS.FAIL: mode=cleanup owner=%s", owner);
    if (deps) {
        free(deps->ids);
        free(deps->types);
    }
    return ok;
}

static s32 harnessCapacityAccept(const char *owner, const char *archive,
                                 const char *expected_text,
                                 const char *effect_count_text,
                                 const char *effect_dep_text)
{
    char err[512] = {0};
    harness_deps_t deps = {0};
    harness_deps_t effect_deps = {0};
    const long expected = strtol(expected_text ? expected_text : "", NULL, 10);
    const long expected_effect_deps = strtol(
        effect_dep_text ? effect_dep_text : "", NULL, 10);
    const long expected_effects = strtol(
        effect_count_text ? effect_count_text : "", NULL, 10);
    const s32 dep_baseline = catalogDepCount();
    size_t effect_count = 0;
    s32 new_runtime_weapon_id = -1;
    s32 new_mp_weapon_id = -1;
    s32 ok = 0;
    if (expected <= 64 || expected > INT32_MAX
            || expected_effects < 2 || expected_effects > INT32_MAX
            || expected_effect_deps <= 64 || expected_effect_deps > INT32_MAX
            ) goto done;
    /* This runs after the complete base catalog has booted. A genuinely new
     * creator weapon must receive its own pair, rather than borrowing a donor
     * or failing because base-only inventory rows consumed custom capacity. */
    if (!assetCatalogResolveWeaponPrivateSlots("capacity:weapon_new", -1, 0,
            &new_runtime_weapon_id, &new_mp_weapon_id)
            || new_runtime_weapon_id < WEAPON_CUSTOM_START
            || new_runtime_weapon_id >= WEAPON_CUSTOM_END
            || new_mp_weapon_id < MPWEAPON_CUSTOM_START
            || new_mp_weapon_id >= MPWEAPON_CUSTOM_END) goto done;
    sysLogPrintf(LOG_NOTE,
        "PDWEAPON.NESTED.WEAPON_SLOT: id=capacity:weapon_new runtime=%d mp=%d result=PASS",
        new_runtime_weapon_id, new_mp_weapon_id);

    if (!harnessRegisterOwner(owner, archive)) goto done;
    if (assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0,
            err, sizeof(err)) != (s32)expected) goto done;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    for (size_t i = 0; i < deps.count; i++) {
        if (deps.types[i] != ASSET_EFFECT) continue;
        effect_count++;
        catalogDepForEach(deps.ids[i], harnessCollectDep, &effect_deps);
    }
    if (deps.invalid || effect_deps.invalid
            || deps.count != (size_t)expected
            || effect_count != (size_t)expected_effects
            || effect_deps.count != (size_t)expected_effect_deps) goto done;

    /* Rebuild the production reverse index after dynamic registration, then
     * play all 70 creator SFX through sndStart's normal catalog/file route. */
    {
        s32 high_soundnum = -1;
        catalogLoadInit();
        for (s32 i = 0; i < 70; i++) {
            char sfx_id[CATALOG_ID_LEN];
            const asset_entry_t *sfx;
            struct sndstate *state;
            CatalogResolveResult route;
            snprintf(sfx_id, sizeof(sfx_id), "capacity:sfx_%03d", i);
            sfx = assetCatalogResolve(sfx_id);
            if (!sfx || sfx->type != ASSET_AUDIO
                    || sfx->ext.audio.category != AUDIO_CAT_SFX
                    || sfx->source_soundnum < SND_CUSTOM_START
                    || sfx->source_soundnum >= SND_CUSTOM_END) goto done;
            route = catalogResolveSound(sfx->source_soundnum);
            if (i == 0 || i == 69) {
                sysLogPrintf(LOG_NOTE,
                    "PDWEAPON.NESTED.PLAYBACK_ROUTE: id=%s sound=%d disabled=%d catalog=%d override=%d blocked=%d path=%s",
                    sfx_id, sfx->source_soundnum, sndIsDisabled() ? 1 : 0,
                    route.catalog_id, route.is_mod_override,
                    route.source_only_blocked,
                    route.path ? route.path : "(null)");
            }
            if (sndIsDisabled() || route.catalog_id < 0
                    || !route.is_mod_override || !route.path
                    || route.source_only_blocked) goto done;
            state = sndStart(var80095200, (s16)sfx->source_soundnum,
                NULL, -1, -1, -1.0f, -1, -1);
            if (!state) goto done;
            audioStop(state);
            high_soundnum = sfx->source_soundnum;
        }
        if (high_soundnum < SND_CUSTOM_START + 64) goto done;
        sysLogPrintf(LOG_NOTE,
            "PDWEAPON.NESTED.PLAYBACK: count=70 high_id=capacity:sfx_069 high_sound=%d result=PASS",
            high_soundnum);
    }
    ok = 1;
done:
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=capacity owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "capacity assertion failed");
    }
    free(effect_deps.ids);
    free(effect_deps.types);
    if (!harnessCleanup(owner, &deps)) ok = 0;
    if (catalogDepCount() != dep_baseline) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=capacity owner=%s leaked_edges=%d",
            owner, catalogDepCount() - dep_baseline);
        ok = 0;
    }
    return ok;
}

static s32 harnessCapacityReject(const char *owner, const char *archive,
                                 const char *id_prefix,
                                 const char *count_text,
                                 const char *effect_id)
{
    char err[512] = {0};
    harness_deps_t deps = {0};
    const long expected = strtol(count_text ? count_text : "", NULL, 10);
    const s32 dep_baseline = catalogDepCount();
    s32 ok = expected > 64 && expected <= INT32_MAX && id_prefix
        && id_prefix[0] && effect_id && effect_id[0];
    if (!ok || !harnessRegisterOwner(owner, archive)) return 0;
    ok = assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0,
        err, sizeof(err)) < 0;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    if (deps.count != 0 || deps.invalid) ok = 0;
    for (long i = 0; ok && i < expected; i++) {
        char id[CATALOG_ID_LEN];
        snprintf(id, sizeof(id), "%s%03ld", id_prefix, i);
        if (assetCatalogResolve(id)) ok = 0;
    }
    if (assetCatalogResolve(effect_id)) ok = 0;
    if (!harnessCleanup(owner, &deps)) ok = 0;
    if (catalogDepCount() != dep_baseline) ok = 0;
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=capacity_reject owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "rollback assertion failed");
    }
    return ok;
}

static s32 harnessChildrenAreLoaded(const harness_deps_t *deps, s32 min_ref)
{
    for (size_t i = 0; deps && i < deps->count; i++) {
        const asset_entry_t *entry = assetCatalogResolve(deps->ids[i]);
        if (!entry || entry->load_state < ASSET_STATE_LOADED
                || entry->ref_count < min_ref) return 0;
    }
    return deps && !deps->invalid;
}

static s32 harnessChildrenAreReleased(const harness_deps_t *deps)
{
    for (size_t i = 0; deps && i < deps->count; i++) {
        const asset_entry_t *entry = assetCatalogResolve(deps->ids[i]);
        if (!entry || entry->ref_count != 0 || entry->loaded_data != NULL
                || entry->load_state >= ASSET_STATE_LOADED) return 0;
    }
    return deps && !deps->invalid;
}

static s32 harnessAccept(const char *owner, const char *archive,
                         const char *audio_id, const char *anim_id)
{
    char err[512] = {0};
    harness_deps_t deps = {0};
    s32 ok = 0;
    if (!harnessRegisterOwner(owner, archive)) goto done;
    if (assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0,
            err, sizeof(err)) < 0) goto done;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    s32 audio_pos = harnessFind(&deps, audio_id, ASSET_AUDIO);
    s32 anim_pos = harnessFind(&deps, anim_id, ASSET_ANIMATION);
    if (deps.invalid || audio_pos < 0 || anim_pos < 0 || audio_pos >= anim_pos) goto done;

    /* Direct production lifecycle: parent owns one child reference. */
    if (!catalogLoadTypedAsset(ASSET_WEAPON, owner)
            || !harnessChildrenAreLoaded(&deps, 1)) goto done;
    catalogReleaseTypedAsset(ASSET_WEAPON, owner);
    if (!harnessChildrenAreReleased(&deps)) goto done;

    /* Manifest-style lifecycle: explicit child references coexist with the
     * parent's own closure. Parent release leaves one, manifest release frees. */
    for (size_t i = 0; i < deps.count; i++) {
        if (!catalogLoadTypedAsset(deps.types[i], deps.ids[i])) goto done;
    }
    if (!catalogLoadTypedAsset(ASSET_WEAPON, owner)
            || !harnessChildrenAreLoaded(&deps, 2)) goto done;
    catalogReleaseTypedAsset(ASSET_WEAPON, owner);
    if (!harnessChildrenAreLoaded(&deps, 1)) goto done;
    for (size_t i = deps.count; i-- > 0; ) {
        catalogReleaseTypedAsset(deps.types[i], deps.ids[i]);
    }
    if (!harnessChildrenAreReleased(&deps)) goto done;
    ok = 1;
done:
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=accept owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "runtime assertion failed");
    }
    if (!harnessCleanup(owner, &deps)) ok = 0;
    return ok;
}

static s32 harnessReject(const char *owner, const char *archive,
                         char *absent_csv)
{
    char err[512] = {0};
    harness_deps_t deps = {0};
    s32 ok;
    if (!harnessRegisterOwner(owner, archive)) return 0;
    ok = assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0,
        err, sizeof(err)) < 0;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    if (deps.count != 0 || deps.invalid) ok = 0;
    for (char *id = absent_csv; id && *id;) {
        char *next = strchr(id, ',');
        if (next) *next++ = '\0';
        if (id[0] && assetCatalogResolve(id)) ok = 0;
        id = next;
    }
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=reject owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "rollback assertion failed");
    }
    if (!harnessCleanup(owner, &deps)) ok = 0;
    return ok;
}

/* Production-backed source and native-record proof. This does not claim a
 * rendered frame, a fired projectile, or graph control-flow execution. */
static s32 harnessMeshVertices(const struct modeldef *model, s32 expected_edge)
{
	if (!model || !model->rootnode) return 0;
	for (const struct modelnode *node = model->rootnode->child; node; node = node->next) {
		if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
		const struct modelrodata_dl *dl = &node->rodata->dl;
		if (!dl->vertices || dl->numvertices != 3) continue;
		s32 max_x = 0, max_y = 0;
		for (s32 i = 0; i < dl->numvertices; i++) {
			if (dl->vertices[i].x > max_x) max_x = dl->vertices[i].x;
			if (dl->vertices[i].y > max_y) max_y = dl->vertices[i].y;
		}
		return max_x == expected_edge && max_y == expected_edge;
	}
	return 0;
}

static s32 harnessWriteBytes(const char *path, const void *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    s32 ok = fwrite(bytes, 1, size, file) == size;
    if (fclose(file)) ok = 0;
    return ok;
}

static s32 harnessGenerationFrame(s32 slot, f32 *translation_y)
{
    struct coord rotation, translation, scale;
    animLoadHeader((s16)slot);
    u8 frame = animLoadFrame((s16)slot, 1);
    if (frame >= ANIM_FRAME_CACHE_SIZE) return 0;
    animGetRotTranslateScale(0, 0, NULL, (s16)slot, frame, &rotation, &translation, &scale);
    *translation_y = translation.y;
    return 1;
}

typedef struct harness_model_inputs {
    char paths[4][FS_MAXPATH];
    u8 *bytes[4];
    u32 sizes[4];
    s32 reads, private_reads, textures, reject_texture;
    mod_texture_rgba32_source_t rgba;
    struct tex texture;
    struct texpool pool;
} harness_model_inputs_t;
static const char *harnessModelPath(const char *path)
{
    while (path[0] == '.' && path[1] == '/') path += 2;
    return path;
}
static void *harnessModelRead(void *context, const char *path, u32 *size)
{
    harness_model_inputs_t *inputs = context;
    *size = 0;
    if (strstr(path, "_meta/")) ++inputs->private_reads;
    for (size_t i = 0; i < 4; ++i) {
        if (strcmp(harnessModelPath(path), harnessModelPath(inputs->paths[i]))) continue;
        u8 *copy = malloc((size_t)inputs->sizes[i] + 1);
        if (!copy) return NULL;
        memcpy(copy, inputs->bytes[i], inputs->sizes[i]); copy[inputs->sizes[i]] = 0;
        *size = inputs->sizes[i]; ++inputs->reads;
        return copy;
    }
    return NULL; /* Snapshot records the optional sidecars as absent. */
}
static s32 harnessModelTexture(void *context, const char *source_path,
    const char *reference, s32 is_catalog, s32 secondary)
{
    harness_model_inputs_t *inputs = context;
    char error[160];
    if (inputs->reject_texture || is_catalog || secondary || !source_path
            || strcmp(reference, "sample.tga") || inputs->textures) return -1;
    if (modTextureDecodeRgba32Source(inputs->bytes[3], inputs->sizes[3],
            &inputs->rgba, error, sizeof(error)) <= 0) return -1;
    ++inputs->textures;
    inputs->texture.texturenum = 0;
    inputs->texture.data = inputs->rgba.pixels;
    inputs->texture.width = inputs->rgba.width;
    inputs->texture.height = inputs->rgba.height;
    inputs->texture.numlods = 1;
    inputs->texture.gbiformat = G_IM_FMT_RGBA;
    inputs->texture.depth = G_IM_SIZ_32b;
    inputs->pool.start = inputs->rgba.pixels;
    inputs->pool.leftpos = inputs->rgba.pixels + inputs->rgba.data_size;
    inputs->pool.rightpos = &inputs->texture;
    inputs->pool.end = &inputs->texture + 1;
    return 0;
}
static s32 harnessModelHasTexture(const struct modeldef *model, const void *pixels)
{
    if (!model || !model->rootnode) return 0;
    for (const struct modelnode *node = model->rootnode->child; node; node = node->next) {
        if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
        const Gfx *gdl = node->rodata->dl.opagdl;
        u32 bytes = 0;
        if (!gdl || !modAssetCompilerGeneratedGdlBytesRemaining(gdl, &bytes)) return 0;
        for (u32 i = 0; i < bytes / sizeof(Gfx); ++i) {
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_ENDDL) break;
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_SETTIMG
                    && gdl[i].words.w1 == (uintptr_t)pixels) return 1;
        }
    }
    return 0;
}
static const u32 *harnessModelFirstPixels(const struct modeldef *model)
{
    if (!model || !model->rootnode) return NULL;
    for (const struct modelnode *node = model->rootnode->child; node; node = node->next) {
        if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
        const Gfx *gdl = node->rodata->dl.opagdl;
        u32 bytes = 0;
        if (!gdl || !modAssetCompilerGeneratedGdlBytesRemaining(gdl, &bytes)) return NULL;
        for (u32 i = 0; i < bytes / sizeof(Gfx); ++i) {
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_ENDDL) break;
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_SETTIMG)
                return (const u32 *)(uintptr_t)gdl[i].words.w1;
        }
    }
    return NULL;
}
static s32 harnessModelInputs(const char *id, const char *folder)
{
    const char *names[] = {"model.obj", "model.mtl", "mesh.ini", "sample.tga"};
    harness_model_inputs_t first = {0}, second = {0}, rejected = {0};
    struct modeldef *a = NULL, *b = NULL, *bad = NULL;
    asset_entry_t *entry = calloc(1, sizeof(*entry));
    u8 *edit = NULL;
    const char *step = "capture";
    s32 ok = 0;
    if (!entry) goto done;
    strncpy(entry->id, id, sizeof(entry->id) - 1); entry->type = ASSET_MODEL;
    for (size_t i = 0; i < 4; ++i) {
        if (snprintf(first.paths[i], sizeof(first.paths[i]), "%s/%s", folder, names[i]) >= sizeof(first.paths[i])) goto done;
        first.bytes[i] = fsFileLoad(first.paths[i], &first.sizes[i]);
        if (!first.bytes[i] || !first.sizes[i]) goto done;
        strcpy(second.paths[i], first.paths[i]);
    }
    /* Edit disk AFTER capture. Both geometry/descriptor and image callbacks
     * must consume their captured bytes, with no hidden file reopen. */
    for (size_t i = 0; i < 4; ++i) {
        edit = malloc((size_t)first.sizes[i] + 1);
        if (!edit) goto done;
        memcpy(edit, first.bytes[i], first.sizes[i]); edit[first.sizes[i]] = 0;
        if (i == 0) {
            char *x = strstr((char *)edit, "v 2 0 0"), *y = strstr((char *)edit, "v 0 2 0");
            if (!x || !y) goto done;
            x[2] = '7'; y[4] = '7';
        }
        if (i == 2) {
            char *scale = strstr((char *)edit, "model_scale = 2");
            if (!scale) goto done;
            scale[strlen("model_scale = ")] = '3';
        }
        if (i == 3) {
            if (first.sizes[i] != 34) goto done;
            for (u32 c = 18; c < 34; c += 4) { edit[c] = 255; edit[c + 2] = 0; }
        }
        if (!harnessWriteBytes(first.paths[i], edit, first.sizes[i])) goto done;
        free(edit); edit = NULL;
        second.bytes[i] = fsFileLoad(second.paths[i], &second.sizes[i]);
        if (!second.bytes[i] || !second.sizes[i]) goto done;
    }
    step = "captured_compiler";
    modasset_model_inputs_t inputs = {&first, harnessModelRead, harnessModelTexture, &first.pool};
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, entry, first.paths[0], &a) <= 0 || !a
            || !harnessMeshVertices(a, 2) || a->scale != 2.0f
            || a->skel != modAssetCompilerSkeletonForSymbol("SKEL_BASIC")
            || first.private_reads || first.reads < 3 || first.textures != 1
            || ((u32 *)first.rgba.pixels)[0] != 0xff0000ffu
            || !harnessModelHasTexture(a, first.rgba.pixels)) goto done;
    step = "edited_compiler";
    inputs.context = &second; inputs.texture_pool = &second.pool;
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, entry, second.paths[0], &b) <= 0 || !b
            || !harnessMeshVertices(b, 7) || b->scale != 3.0f || second.private_reads
            || second.textures != 1 || ((u32 *)second.rgba.pixels)[0] != 0x0000ffffu
            || !harnessModelHasTexture(b, second.rgba.pixels)
            || !harnessMeshVertices(a, 2) || a->scale != 2.0f) goto done;
    step = "texture_reject";
    /* A source resolver failure cannot silently emit an untextured model. */
    memcpy(rejected.paths, first.paths, sizeof(first.paths));
    memcpy(rejected.bytes, first.bytes, sizeof(first.bytes));
    memcpy(rejected.sizes, first.sizes, sizeof(first.sizes));
    rejected.reject_texture = 1;
    inputs.context = &rejected; inputs.texture_pool = &rejected.pool;
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, entry, first.paths[0], &bad) >= 0 || bad) goto done;
    mod_texture_rgba32_source_t invalid;
    char error[128];
    if (modTextureDecodeRgba32Source("bad", 3, &invalid, error, sizeof(error)) >= 0 || invalid.pixels) goto done;
    ok = 1;
done:
    modAssetCompilerFreeModeldef(a); modAssetCompilerFreeModeldef(b); modAssetCompilerFreeModeldef(bad);
    modTextureFreeRgba32Source(&first.rgba); modTextureFreeRgba32Source(&second.rgba);
    for (size_t i = 0; i < 4; ++i) {
        if (first.bytes[i] && !harnessWriteBytes(first.paths[i], first.bytes[i], first.sizes[i])) ok = 0;
        free(first.bytes[i]); free(second.bytes[i]);
    }
    free(edit); free(entry);
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODEL.INPUTS.HARNESS: step=%s result=%s reads=%d,%d textures=%d,%d private_reads=%d,%d",
        step, ok ? "PASS" : "FAIL", first.reads, second.reads, first.textures, second.textures,
        first.private_reads, second.private_reads);
    return ok;
}



typedef struct harness_texture_model {
    harness_model_inputs_t *source;
    catalog_texture_generation_t *texture;
    s32 texture_calls;
} harness_texture_model_t;
static void *harnessTextureModelRead(void *context, const char *path, u32 *size)
{
    return harnessModelRead(((harness_texture_model_t *)context)->source, path, size);
}
static s32 harnessTextureModelBind(void *context, const char *source_path,
    const char *reference, s32 is_catalog, s32 secondary)
{
    harness_texture_model_t *model = context;
    if (!source_path || is_catalog || secondary || strcmp(reference, "sample.tga")) return -1;
    ++model->texture_calls;
    return catalogTextureGenerationSlot(model->texture);
}
static s32 harnessModelHasTile(const struct modeldef *model, u32 tmem, u32 mask_s, u32 mask_t)
{
    if (!model || !model->rootnode) return 0;
    for (const struct modelnode *node = model->rootnode->child; node; node = node->next) {
        if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
        const Gfx *gdl = node->rodata->dl.opagdl;
        u32 bytes = 0;
        if (!gdl || !modAssetCompilerGeneratedGdlBytesRemaining(gdl, &bytes)) return 0;
        for (u32 i = 0; i < bytes / sizeof(Gfx); ++i) {
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_ENDDL) break;
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_SETTILE
                    && ((gdl[i].words.w1 >> 24) & 7) == 0
                    && (gdl[i].words.w0 & 0x1ff) == tmem
                    && ((gdl[i].words.w1 >> 4) & 15) == mask_s
                    && ((gdl[i].words.w1 >> 14) & 15) == mask_t) return 1;
        }
    }
    return 0;
}
static s32 harnessModelGeneration(const char *id, const char *folder)
{
    char source[FS_MAXPATH], image[FS_MAXPATH], error[256] = {0}, prior_hash[65] = {0};
    u32 image_size = 0;
    u8 *original = NULL, *edited = NULL;
    asset_entry_t *entry = calloc(1, sizeof(*entry));
    catalog_model_generation_t *first = NULL, *alias = NULL, *second = NULL, *rejected = NULL;
    struct modeldef *old_model = NULL, *new_model = NULL;
    const u32 *old_pixels = NULL, *new_pixels = NULL;
    struct player *queued_player = calloc(1, sizeof(*queued_player));
    struct modeldef *queued_result = NULL;
    file_provider_checkpoint_t provider;
    const char *step = "source";
    s32 checkpoint = fileProviderCheckpointCreate(&provider);
    void *slot_snapshot = assetCatalogSnapshotCustomModelSlots();
    s32 registered = 0, stage_loaded = 0, ok = 0;
    if (!checkpoint || !slot_snapshot || !entry || !queued_player || assetCatalogResolveAny(id) || strlen(id) >= sizeof(entry->id)
            || snprintf(source, sizeof(source), "%s/model.obj", folder) >= sizeof(source)
            || snprintf(image, sizeof(image), "%s/sample.tga", folder) >= sizeof(image)) goto done;
    strcpy(entry->id, id); entry->type = ASSET_MODEL;
    entry->runtime_index = assetCatalogResolveModelPrivateSlot(id);
    entry->source_filenum = assetCatalogModelPrivateSourceFilenum(entry->runtime_index);
    if (entry->runtime_index < MODEL_CUSTOM_START || entry->source_filenum <= 0) goto done;
    original = fsFileLoad(image, &image_size);
    if (!original || image_size != 34) goto done;
    edited = malloc(image_size);
    if (!edited) goto done;
    memcpy(edited, original, image_size);
    step = "dedup";
    first = catalogModelGenerationAcquireSource(entry, source, error, sizeof(error));
    alias = catalogModelGenerationAcquireSource(entry, source, error, sizeof(error));
    old_model = catalogModelGenerationModeldef(first);
    old_pixels = harnessModelFirstPixels(old_model);
    if (!first || alias != first || !old_model || !old_pixels
            || old_pixels[0] != 0xff0000ffu
            || catalogModelGenerationForModeldef(old_model) != first) goto done;
    strcpy(prior_hash, catalogModelGenerationHash(first));
    step = "queued_source";
    if (!bgunQueueRetainedModelLoad(queued_player, old_model, &queued_result)
            || queued_result || queued_player->gunctrl.loadretainedmodel != old_model
            || bgunQueueRetainedModelLoad(queued_player, NULL, &queued_result)
            || queued_player->gunctrl.loadretainedmodel != old_model) goto done;
    step = "edit";
    for (u32 i = 18; i < 34; i += 4) { edited[i] = 255; edited[i + 2] = 0; }
    if (!harnessWriteBytes(image, edited, image_size)) goto done;
    second = catalogModelGenerationAcquireSource(entry, source, error, sizeof(error));
    new_model = catalogModelGenerationModeldef(second);
    new_pixels = harnessModelFirstPixels(new_model);
    if (!second || second == first || new_model == old_model || !new_pixels
            || !strcmp(prior_hash, catalogModelGenerationHash(second))
            || new_pixels[0] != 0x0000ffffu || old_pixels[0] != 0xff0000ffu) goto done;
    step = "ordinary_catalog";
    asset_entry_t *catalog_entry = assetCatalogRegister(id, ASSET_MODEL);
    if (!catalog_entry) goto done;
    registered = 1;
    catalog_entry->runtime_index = entry->runtime_index;
    catalog_entry->source_filenum = entry->source_filenum;
    catalogSetPrimaryFile(catalog_entry, source);
    if (!catalogLoadStageAsset(ASSET_MODEL, id)) goto done;
    stage_loaded = 1;
    /* The queued old version must survive a same-ID ordinary catalog load
     * of the edited version and caller retirement. Execute the real load tick. */
    catalogModelGenerationRelease(alias); alias = NULL;
    catalogModelGenerationRelease(first); first = NULL;
    struct player *saved_player = g_Vars.currentplayer;
    g_Vars.currentplayer = queued_player;
    bgunTickGunLoad();
    g_Vars.currentplayer = saved_player;
    if (queued_result != old_model || queued_player->gunctrl.loadretainedmodel
            || queued_player->gunctrl.gunloadstate != 4
            || old_pixels[0] != 0xff0000ffu) goto done;
    sysLogPrintf(LOG_NOTE, "MODEL.QUEUE.GENERATION.HARNESS: exact_retained_source=PASS");
    if (catalogGetLoadedModeldef(id) != new_model
            || catalogModelGenerationForModeldef(new_model) != second
            || new_pixels[0] != 0x0000ffffu) goto done;
    catalogReleaseStageAsset(ASSET_MODEL, id); stage_loaded = 0;
    if (catalogGetLoadedModeldef(id)
            || catalogModelGenerationForModeldef(new_model) != second) goto done;
    step = "rejection";
    if (!harnessWriteBytes(image, "bad", 3)) goto done;
    if (catalogLoadStageAsset(ASSET_MODEL, id) || catalogGetLoadedModeldef(id)) goto done;
    rejected = catalogModelGenerationAcquireSource(entry, source, error, sizeof(error));
    if (rejected || !error[0] || old_pixels[0] != 0xff0000ffu
            || new_pixels[0] != 0x0000ffffu) goto done;
    step = "retirement";
    if (!catalogModelGenerationPinStage(catalogModelGenerationForModeldef(old_model))
            || !catalogModelGenerationPinStage(second)) goto done;
    catalogModelGenerationRelease(alias); alias = NULL;
    catalogModelGenerationRelease(first); first = NULL;
    assetCatalogResetCustomModelSlots();
    if (assetCatalogResolveModelPrivateSlot("meshproof:model_slot_other") == entry->runtime_index
            || !assetCatalogPinModelPrivateSlot(id, entry->runtime_index)) goto done;
    if (!assetCatalogReleaseModelPrivateSlot(id, entry->runtime_index)) goto done;
    /* Native held pointers remain valid after all old catalog/caller owners
     * retire. Repeated pins above must not leak an extra stage reference. */
    if (!catalogModelGenerationForModeldef(old_model)
            || old_pixels[0] != 0xff0000ffu || new_pixels[0] != 0x0000ffffu) goto done;
    step = "stage_retirement";
    queued_result = NULL; queued_player->gunctrl.loadtomodeldef = NULL;
    catalogModelGenerationReleaseStagePins();
    catalogModelGenerationReleaseStagePins();
    if (catalogModelGenerationForModeldef(old_model)
            || catalogModelGenerationForModeldef(new_model) != second
            || new_pixels[0] != 0x0000ffffu) goto done;
    if (!assetCatalogPinModelPrivateSlot(id, entry->runtime_index)) goto done;
    if (!assetCatalogReleaseModelPrivateSlot(id, entry->runtime_index)) goto done;
    sysLogPrintf(LOG_NOTE, "MODEL.SLOT.GENERATION.HARNESS: retained_after_catalog_reset=PASS");
    ok = 1;
done:
    if (stage_loaded) catalogReleaseStageAsset(ASSET_MODEL, id);
    if (registered && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    catalogModelGenerationRelease(alias);
    catalogModelGenerationRelease(first);
    catalogModelGenerationRelease(second);
    catalogModelGenerationRelease(rejected);
    free(queued_player); queued_player = NULL; queued_result = NULL;
    catalogModelGenerationReleaseStagePins();
    if (slot_snapshot && !assetCatalogRestoreCustomModelSlots(slot_snapshot)) ok = 0;
    assetCatalogDestroyCustomModelSlotSnapshot(slot_snapshot);
    if (original && !harnessWriteBytes(image, original, image_size)) ok = 0;
    free(original); free(edited); free(entry); free(queued_player);
    catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODEL.GENERATION.HARNESS: step=%s result=%s error=%s",
        step, ok ? "PASS" : "FAIL", error);
    return ok;
}

static s32 harnessTextureGeneration(const char *id, const char *folder)
{
    const char *names[] = {"model.obj", "model.mtl", "mesh.ini", "sample.tga"};
    harness_model_inputs_t sources = {0};
    char descriptor[FS_MAXPATH], error[256] = {0}, first_hash[65] = {0};
    char *ini = NULL, *ini_edit = NULL;
    u8 *image_edit = NULL;
    u32 ini_size = 0;
    catalog_texture_generation_t *first = NULL, *alias = NULL, *second = NULL,
        *third = NULL, *reused = NULL, *rejected = NULL;
    struct modeldef *model_a = NULL, *model_b = NULL;
    asset_entry_t *model_entry = calloc(1, sizeof(*model_entry));
    file_provider_checkpoint_t provider;
    void *slots = assetCatalogSnapshotCustomTextureSlots();
    s32 checkpoint = fileProviderCheckpointCreate(&provider), registered = 0, ok = 0;
    s32 first_slot = -1, second_slot = -1, third_slot = -1, changed_table = 0;
    struct texture saved_definition = {0};
    const char *step = "source";
    if (!model_entry || !slots || !checkpoint || assetCatalogResolveAny(id)) goto done;
    strcpy(model_entry->id, "modelinputs:mesh"); model_entry->type = ASSET_MODEL;
    if (snprintf(descriptor, sizeof(descriptor), "%s/texture.ini", folder) >= sizeof(descriptor)) goto done;
    ini = fsFileLoad(descriptor, &ini_size);
    if (!ini || !ini_size) goto done;
    ini_edit = malloc((size_t)ini_size + 1);
    if (!ini_edit) goto done;
    memcpy(ini_edit, ini, ini_size); ini_edit[ini_size] = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (snprintf(sources.paths[i], sizeof(sources.paths[i]), "%s/%s", folder, names[i]) >= sizeof(sources.paths[i])) goto done;
        sources.bytes[i] = fsFileLoad(sources.paths[i], &sources.sizes[i]);
        if (!sources.bytes[i] || !sources.sizes[i]) goto done;
    }
    if (sources.sizes[3] != 34) goto done;
    image_edit = malloc(sources.sizes[3]);
    if (!image_edit) goto done;
    memcpy(image_edit, sources.bytes[3], sources.sizes[3]);
    asset_entry_t *entry = assetCatalogRegisterTexture(id, -1, 2, 2, G_IM_FMT_RGBA, sources.paths[3]);
    if (!entry) goto done;
    registered = 1; strcpy(entry->descriptor_path, descriptor);
    catalogSetPrimaryFile(entry, sources.paths[3]);
    step = "acquire";
    first = catalogTextureGenerationAcquire(id, error, sizeof(error));
    alias = catalogTextureGenerationAcquire(id, error, sizeof(error));
    if (!first || alias != first) goto done;
    first_slot = catalogTextureGenerationSlot(first);
    strcpy(first_hash, catalogTextureGenerationHash(first));
    struct tex *first_native = catalogTextureGenerationTexture(first);
    if (first_slot < TEXTURE_CUSTOM_START || first_slot >= TEXTURE_CUSTOM_END
            || first_native->texturenum != first_slot
            || *(u16 *)(first_native->data - 8) != first_slot
            || ((u32 *)first_native->data)[0] != 0xff0000ffu
            || texGetDefinition(first_slot)->surfacetype != SURFACETYPE_WOOD
            || texGetDefinition(first_slot)->soundsurfacetype != SURFACETYPE_METAL) goto done;
    step = "image_edit";
    for (u32 i = 18; i < 34; i += 4) { image_edit[i] = 255; image_edit[i + 2] = 0; }
    if (!harnessWriteBytes(sources.paths[3], image_edit, sources.sizes[3])) goto done;
    second = catalogTextureGenerationAcquire(id, error, sizeof(error));
    if (!second || second == first || !strcmp(first_hash, catalogTextureGenerationHash(second))) goto done;
    second_slot = catalogTextureGenerationSlot(second);
    if (second_slot == first_slot || ((u32 *)catalogTextureGenerationTexture(second)->data)[0] != 0x0000ffffu) goto done;
    step = "properties_edit";
    char *surface = strstr(ini_edit, "surface_type = wood");
    char *sound = strstr(ini_edit, "sound_surface_type = metal");
    char *offset = strstr(ini_edit, "tile_column_offset = 1");
    if (!surface || !sound || !offset) goto done;
    memcpy(surface + strlen("surface_type = "), "snow", 4);
    memcpy(sound + strlen("sound_surface_type = "), "glass", 5);
    offset[strlen("tile_column_offset = ")] = '2';
    if (!harnessWriteBytes(descriptor, ini_edit, ini_size)) goto done;
    third = catalogTextureGenerationAcquire(id, error, sizeof(error));
    if (!third || third == second || !strcmp(catalogTextureGenerationHash(second), catalogTextureGenerationHash(third))) goto done;
    third_slot = catalogTextureGenerationSlot(third);
    if (texGetDefinition(third_slot)->surfacetype != SURFACETYPE_SNOW
            || texGetDefinition(third_slot)->soundsurfacetype != SURFACETYPE_GLASS
            || texGetDefinition(first_slot)->surfacetype != SURFACETYPE_WOOD
            || texGetDefinition(second_slot)->surfacetype != SURFACETYPE_WOOD) goto done;
    step = "reject";
    s32 available = assetCatalogReserveTextureGenerationSlot();
    if (available < 0) goto done;
    assetCatalogReleaseTextureGenerationSlot(available);
    rejected = catalogTextureGenerationAcquireSource(id, "bad", 3, ini, ini_size, error, sizeof(error));
    if (rejected) goto done;
    char *mask = strstr(ini_edit, "mask_s_reduction = 1");
    if (!mask) goto done;
    mask[strlen("mask_s_reduction = ")] = '2';
    rejected = catalogTextureGenerationAcquireSource(id, image_edit, sources.sizes[3], ini_edit, ini_size, error, sizeof(error));
    if (rejected) goto done;
    s32 after = assetCatalogReserveTextureGenerationSlot();
    if (after >= 0) assetCatalogReleaseTextureGenerationSlot(after);
    if (after != available) goto done;
    rejected = catalogTextureGenerationAcquireSource("texgen:wrong_identity", image_edit, sources.sizes[3], ini, ini_size, error, sizeof(error));
    if (rejected) goto done;
    char bad_descriptor[512];
    int bad_size = snprintf(bad_descriptor, sizeof(bad_descriptor), "[texture]\ncatalog_id = %s\nsurface_type = unknown_surface\n", id);
    if (bad_size <= 0 || bad_size >= sizeof(bad_descriptor)) goto done;
    rejected = catalogTextureGenerationAcquireSource(id, image_edit, sources.sizes[3], bad_descriptor, bad_size, error, sizeof(error));
    if (rejected) goto done;
    bad_size = snprintf(bad_descriptor, sizeof(bad_descriptor), "[texture]\ncatalog_id = %s\nsurface_type = wood\nsurface_type = metal\n", id);
    if (bad_size <= 0 || bad_size >= sizeof(bad_descriptor)) goto done;
    rejected = catalogTextureGenerationAcquireSource(id, image_edit, sources.sizes[3], bad_descriptor, bad_size, error, sizeof(error));
    if (rejected) goto done;
    step = "bundled_source_guard";
    bad_size = snprintf(bad_descriptor, sizeof(bad_descriptor), "[texture]\ncatalog_id = %s\ntexture_file = sample.tga\n", id);
    if (bad_size <= 0 || bad_size >= sizeof(bad_descriptor) || !harnessWriteBytes(descriptor, bad_descriptor, bad_size)) goto done;
    entry = assetCatalogGetMutable(id);
    if (!entry) goto done;
    entry->bundled = 1;
    rejected = catalogTextureGenerationAcquire(id, error, sizeof(error));
    entry = assetCatalogGetMutable(id);
    if (entry) entry->bundled = 0;
    if (rejected || !strstr(error, "extraction upgrade required")) goto done;
    step = "retirement";
    if (!assetCatalogUnregister(id)) goto done;
    registered = 0;
    assetCatalogResetCustomTextureSlots();
    if (assetCatalogResolveAny(id) || catalogTextureGenerationForSlot(first_slot) != first
            || catalogTextureGenerationForSlot(second_slot) != second
            || catalogTextureGenerationForSlot(third_slot) != third) goto done;
    saved_definition = g_Textures[first_slot]; changed_table = 1;
    g_Textures[first_slot].surfacetype = SURFACETYPE_DEEPWATER;
    g_Textures[first_slot].soundsurfacetype = SURFACETYPE_DEEPWATER;
    g_Textures[first_slot].unk04_00 = 15;
    struct texpool empty_pool = {0};
    texnum_t update = (texnum_t)(0xabcd0000u | (u32)first_slot);
    texLoad(&update, &empty_pool, 0);
    if (update != (texnum_t)(uintptr_t)first_native->data
            || texFindInPool(first_slot, &empty_pool) != first_native
            || texGetDefinition(first_slot)->surfacetype != SURFACETYPE_WOOD) goto done;
    step = "native_model";
    error[0] = 0;
    harness_texture_model_t binding = {&sources, first, 0};
    modasset_model_inputs_t inputs = {&binding, harnessTextureModelRead, harnessTextureModelBind, &empty_pool};
    texResetTiles();
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, model_entry, sources.paths[0], &model_a) <= 0
            || binding.texture_calls != 1 || !harnessModelHasTexture(model_a, first_native->data)
            || !harnessModelHasTile(model_a, 2, 0, 1)) goto done;
    binding.texture = third; binding.texture_calls = 0; texResetTiles();
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, model_entry, sources.paths[0], &model_b) <= 0
            || binding.texture_calls != 1 || !harnessModelHasTexture(model_b, catalogTextureGenerationTexture(third)->data)
            || !harnessModelHasTile(model_b, 3, 0, 1)
            || !harnessModelHasTexture(model_a, first_native->data)
            || ((u32 *)first_native->data)[0] != 0xff0000ffu) goto done;
    modAssetCompilerFreeModeldef(model_a); model_a = NULL;
    catalogTextureGenerationRelease(alias); alias = NULL;
    if (catalogTextureGenerationForSlot(first_slot) != first) goto done;
    catalogTextureGenerationRelease(first); first = NULL;
    if (catalogTextureGenerationForSlot(first_slot)) goto done;
    step = "reuse";
    for (u32 i = 18; i < 34; i += 4) { image_edit[i] = 0; image_edit[i + 1] = 255; }
    reused = catalogTextureGenerationAcquireSource(id, image_edit, sources.sizes[3], ini, ini_size, error, sizeof(error));
    if (!reused || catalogTextureGenerationSlot(reused) != first_slot) goto done;
    update = (texnum_t)(0xabcd0000u | (u32)first_slot);
    texLoad(&update, &empty_pool, 0);
    if (((u32 *)(uintptr_t)update)[0] != 0x00ff00ffu) goto done;
    ok = 1;
done:
    modAssetCompilerFreeModeldef(model_a); modAssetCompilerFreeModeldef(model_b);
    if (changed_table) g_Textures[first_slot] = saved_definition;
    catalogTextureGenerationRelease(alias); catalogTextureGenerationRelease(first);
    catalogTextureGenerationRelease(second); catalogTextureGenerationRelease(third);
    catalogTextureGenerationRelease(reused); catalogTextureGenerationRelease(rejected);
    if (registered && assetCatalogResolveAny(id) && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (slots && !assetCatalogRestoreCustomTextureSlots(slots)) ok = 0;
    assetCatalogDestroyCustomTextureSlotSnapshot(slots);
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    if (ini && !harnessWriteBytes(descriptor, ini, ini_size)) ok = 0;
    if (sources.bytes[3] && !harnessWriteBytes(sources.paths[3], sources.bytes[3], sources.sizes[3])) ok = 0;
    for (size_t i = 0; i < 4; ++i) free(sources.bytes[i]);
    free(ini); free(ini_edit); free(image_edit); free(model_entry);
    texResetTiles(); catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "TEXTURE.GENERATION.HARNESS: step=%s result=%s slots=%d,%d,%d error=%s",
        step, ok ? "PASS" : "FAIL", first_slot, second_slot, third_slot, error);
    return ok;
}


static s32 harnessTextureRuntime(const char *id, const char *folder)
{
    extern s32 g_NotLoadMod;
    const s32 prior_not_load_mod = g_NotLoadMod;
    char descriptor[FS_MAXPATH], image[FS_MAXPATH], error[256] = {0}, graph[1024];
    char *original = NULL, *edited = NULL;
    u32 original_size = 0;
    void *slots = assetCatalogSnapshotCustomTextureSlots();
    file_provider_checkpoint_t provider;
    s32 checkpoint = fileProviderCheckpointCreate(&provider), registered = 0, ok = 0;
    s32 slot = -1;
    u8 *storage = sysMemAlloc(4096);
    struct texpool pool = {0};
    const char *step = "source";
    if (!slots || !checkpoint || !storage || assetCatalogResolveAny(id)) goto done;
    if (snprintf(descriptor, sizeof(descriptor), "%s/texture.ini", folder) >= sizeof(descriptor)
            || snprintf(image, sizeof(image), "%s/sample.tga", folder) >= sizeof(image)) goto done;
    original = fsFileLoad(descriptor, &original_size);
    if (!original || !original_size) goto done;
    edited = malloc((size_t)original_size + 1);
    if (!edited) goto done;
    memcpy(edited, original, original_size); edited[original_size] = 0;
    slot = assetCatalogResolveTexturePrivateSlot(id);
    if (slot < 0) goto done;
    asset_entry_t *entry = assetCatalogRegisterTexture(id, slot, 2, 2, G_IM_FMT_RGBA, image);
    if (!entry) goto done;
    registered = 1; entry->enabled = 1; strcpy(entry->descriptor_path, descriptor);
    catalogSetPrimaryFile(entry, image); catalogLoadInit();
    step = "sparse_reverse_index";
    const CatalogResolveResult resolved = catalogResolveTexture(slot);
    const asset_entry_t *selected = assetCatalogGetByIndex(resolved.catalog_id);
    if (!selected || strcmp(selected->id, id) || !resolved.path) goto done;
    textureSourceRuntimeResetStage();
    step = "ordinary_native";
    texInitPool(&pool, storage, 4096);
    texnum_t update = (texnum_t)(0xabcd0000u | (u32)slot);
    texLoad(&update, &pool, 0);
    struct tex *native = texFindInPool(slot, &pool);
    if (!native || update != (texnum_t)(uintptr_t)native->data
            || ((u32 *)native->data)[0] != 0xff0000ffu
            || texGetDefinition(slot)->surfacetype != SURFACETYPE_WOOD
            || texGetDefinition(slot)->soundsurfacetype != SURFACETYPE_METAL) goto done;
    step = "descriptor_edit";
    char *surface = strstr(edited, "surface_type = wood");
    if (!surface) goto done;
    memcpy(surface + strlen("surface_type = "), "snow", 4);
    if (!harnessWriteBytes(descriptor, edited, original_size)) goto done;
    catalogLoadInit();
    if (texGetDefinition(slot)->surfacetype != SURFACETYPE_SNOW) goto done;
    step = "stage_modes";
    int length = snprintf(graph, sizeof(graph),
        "{\"texture_properties_version\":1,\"texture_properties\":{\"mode\":\"multiplayer\",\"entries\":[{\"texture\":\"%s\",\"surface_type\":\"glass\",\"sound_surface_type\":\"wood\",\"tile_column_offset\":2}]}}", id);
    if (length <= 0 || length >= sizeof(graph)
            || !textureSourceRuntimeStageGraph(graph, length, 0, 1, error, sizeof(error))
            || texGetDefinition(slot)->surfacetype != SURFACETYPE_SNOW
            || !textureSourceRuntimeStageGraph(graph, length, 1, 1, error, sizeof(error))
            || texGetDefinition(slot)->surfacetype != SURFACETYPE_GLASS
            || texGetDefinition(slot)->soundsurfacetype != SURFACETYPE_WOOD) goto done;
    Gfx commands[16]; texResetTiles();
    Gfx *end = texWriteTileFromDefinition(commands, native, 0, 0, 0, 0);
    s32 found_tile = 0;
    for (Gfx *command = commands; command < end; ++command)
        if ((command->words.w0 >> 24) == G_SETTILE && (command->words.w0 & 0x1ffu) == 3) found_tile = 1;
    if (!found_tile) goto done;
    step = "stage_reject_reset";
    const char *bad = "{\"texture_properties_version\":1,\"texture_properties\":{\"entries\":[{\"texture\":\"missing:texture\",\"surface_type\":\"wood\"}]}}";
    if (textureSourceRuntimeStageGraph(bad, strlen(bad), 1, 1, error, sizeof(error))
            || texGetDefinition(slot)->surfacetype != SURFACETYPE_GLASS) goto done;
    textureSourceRuntimeResetStage();
    if (texGetDefinition(slot)->surfacetype != SURFACETYPE_SNOW
            || texGetDefinition(slot)->soundsurfacetype != SURFACETYPE_METAL) goto done;
    step = "versioned_omission";
    length = snprintf(graph, sizeof(graph), "[texture]\ncatalog_id = %s\nproperties_version = 1\ntexture_file = sample.tga\n", id);
    if (length <= 0 || length >= sizeof(graph) || !harnessWriteBytes(descriptor, graph, length)) goto done;
    entry = assetCatalogGetMutable(id); if (!entry) goto done;
    entry->bundled = 1; textureSourceRuntimeInvalidate();
    const s32 defaults_ok = texGetDefinition(slot)->surfacetype == SURFACETYPE_DEFAULT;
    entry = assetCatalogGetMutable(id); if (entry) entry->bundled = 0;
    if (!defaults_ok) goto done;
    step = "base_overlay_selection";
    if (!harnessWriteBytes(descriptor, original, original_size)) goto done;
    entry = assetCatalogGetMutable(id); if (!entry) goto done;
    entry->source_texnum = 1; entry->ext.texture.texture_id = 1;
    catalogLoadInit();
    mod_texture_rgba32_source_t decoded;
    g_NotLoadMod = 0;
    if (modTextureLoadRgba32Source(1, &decoded) <= 0) goto done;
    const s32 overlay_ok = ((u32 *)decoded.pixels)[0] == 0xff0000ffu
        && texGetDefinition(1)->surfacetype == SURFACETYPE_WOOD;
    modTextureFreeRgba32Source(&decoded);
    if (!overlay_ok) goto done;
    g_NotLoadMod = 1;
    if (modTextureLoadRgba32Source(1, &decoded) <= 0) goto done;
    const asset_entry_t *base = assetCatalogGetByIndex(decoded.catalog_id);
    texture_source_properties_t base_properties;
    const s32 base_ok = base && base->bundled
        && textureSourceRuntimeReadSelected(base, decoded.path, &base_properties, error, sizeof(error))
        && texGetDefinition(1)->surfacetype == base_properties.surface_type;
    modTextureFreeRgba32Source(&decoded);
    if (!base_ok) goto done;
    g_NotLoadMod = 0;
    if (texGetDefinition(1)->surfacetype != SURFACETYPE_WOOD) goto done;
    ok = 1;
 done:
    g_NotLoadMod = prior_not_load_mod;
    textureSourceRuntimeResetStage();
    if (registered && assetCatalogResolveAny(id) && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (slots && !assetCatalogRestoreCustomTextureSlots(slots)) ok = 0;
    assetCatalogDestroyCustomTextureSlotSnapshot(slots);
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    if (original && !harnessWriteBytes(descriptor, original, original_size)) ok = 0;
    free(original); free(edited); sysMemFree(storage);
    texResetTiles(); catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "TEXTURE.SOURCE.RUNTIME.HARNESS: step=%s result=%s slot=%d error=%s",
        step, ok ? "PASS" : "FAIL", slot, error);
    return ok;
}

static s32 harnessAnimationGeneration(const char *id, const char *folder)
{
    catalog_animation_generation_t *first = NULL, *alias = NULL, *second = NULL, *reused = NULL;
    file_provider_checkpoint_t provider;
    void *slots = assetCatalogSnapshotCustomAnimSlots();
    s32 checkpoint = fileProviderCheckpointCreate(&provider), ok = 0, registered = 0;
    char path[FS_MAXPATH], error[256] = {0}, first_hash[65];
    const char *step = "source";
    u32 source_size = 0;
    u8 *source = NULL, *edited = NULL;
    u8 native_before[32], native_after[32];
    s32 first_slot = -1, second_slot = -1;
    f32 first_y = 0, second_y = 0, reused_y = 0;
    if (!slots || !checkpoint || assetCatalogResolveAny(id)
            || snprintf(path, sizeof(path), "%s/frames.bin", folder) >= sizeof(path)) goto done;
    source = fsFileLoad(path, &source_size);
    if (!source || source_size != 32) goto done;
    edited = malloc(source_size);
    if (!edited) goto done;
    memcpy(edited, source, source_size);
    if (assetCatalogScanExternalLayoutFolderDeferred("genproof", folder) != 1) goto done;
    registered = 1;
    step = "acquire";
    first = catalogAnimationGenerationAcquire(id, error, sizeof(error));
    alias = catalogAnimationGenerationAcquire(id, error, sizeof(error));
    if (!first || first != alias) goto done;
    first_slot = catalogAnimationGenerationSlot(first);
    strcpy(first_hash, catalogAnimationGenerationHash(first));
    const void *first_data = catalogAnimationGenerationData(first_slot);
    sha256Hash(first_data, catalogAnimationGenerationSize(first), native_before);
    if (!harnessGenerationFrame(first_slot, &first_y) || !(first_y > 0)) goto done;
    /* Only the declared external glTF buffer changes. JSON/descriptor stay exact. */
    f32 position = 600.0f;
    memcpy(edited + 24, &position, sizeof(position));
    if (!harnessWriteBytes(path, edited, source_size)) goto done;
    step = "source_edit";
    second = catalogAnimationGenerationAcquire(id, error, sizeof(error));
    if (!second || second == first || !strcmp(first_hash, catalogAnimationGenerationHash(second))) goto done;
    second_slot = catalogAnimationGenerationSlot(second);
    if (first_slot == second_slot || !harnessGenerationFrame(second_slot, &second_y) || !(second_y > first_y)) goto done;
    if (!assetCatalogUnregister(id)) goto done;
    registered = 0;
    assetCatalogResetCustomAnimSlots();
    step = "retired_source";
    if (assetCatalogResolveAny(id) || modAnimationLoadData((u16)first_slot) != first_data
            || !catalogAnimationGenerationData(second_slot)
            || !harnessGenerationFrame(first_slot, &reused_y) || reused_y != first_y) goto done;
    sha256Hash(first_data, catalogAnimationGenerationSize(first), native_after);
    if (memcmp(native_before, native_after, sizeof(native_before))) goto done;
    catalogAnimationGenerationRelease(alias); alias = NULL;
    if (catalogAnimationGenerationData(first_slot) != first_data) goto done;
    catalogAnimationGenerationRelease(first); first = NULL;
    if (catalogAnimationGenerationData(first_slot)) goto done;
    /* Reuse the released ID with different bytes; cached frames must not survive. */
    position = 900.0f;
    memcpy(edited + 24, &position, sizeof(position));
    if (!harnessWriteBytes(path, edited, source_size)
            || assetCatalogScanExternalLayoutFolderDeferred("genproof", folder) != 1) goto done;
    registered = 1;
    step = "cache_reuse";
    reused = catalogAnimationGenerationAcquire(id, error, sizeof(error));
    if (!reused || catalogAnimationGenerationSlot(reused) != first_slot
            || !harnessGenerationFrame(first_slot, &reused_y) || !(reused_y > second_y)
            || !harnessGenerationFrame(second_slot, &first_y) || first_y != second_y) goto done;
    ok = 1;
done:
    if (source && !harnessWriteBytes(path, source, source_size)) ok = 0;
    catalogAnimationGenerationRelease(alias);
    catalogAnimationGenerationRelease(first);
    catalogAnimationGenerationRelease(second);
    catalogAnimationGenerationRelease(reused);
    if (registered && assetCatalogResolveAny(id) && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (slots && !assetCatalogRestoreCustomAnimSlots(slots)) ok = 0;
    assetCatalogDestroyCustomAnimSlotSnapshot(slots);
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    free(edited); free(source);
    catalogBuildRuntimeCaches();
    catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "ANIMATION.GENERATION.HARNESS: step=%s result=%s error=%s", step, ok ? "PASS" : "FAIL", error);
    return ok;
}

/* The actual ordinary file-voice mixer writes deterministic stereo samples;
 * SDL dummy output is initialized by this fixture, with no hardware claim. */
static void harnessAudioMixFrame(s16 output[2])
{
    output[0] = output[1] = 0;
    audioMixFileSoundsInto(output, 1);
}
static s32 harnessAudioGeneration(const char *id, const char *folder)
{
    catalog_audio_generation_t *first = NULL, *alias = NULL, *second = NULL, *reused = NULL;
    struct sndstate *first_voice = NULL, *second_voice = NULL, *reused_voice = NULL;
    file_provider_checkpoint_t provider;
    void *slots = assetCatalogSnapshotCustomSoundSlots();
    s32 checkpoint = fileProviderCheckpointCreate(&provider), registered = 0, ok = 0;
    const char *step = "source";
    char path[FS_MAXPATH], error[256] = {0}, hash[65];
    u8 *source = NULL, *edited = NULL, native_hash[32], retained_hash[32];
    u32 size = 0, frames = 0;
    s32 rate = 0, first_slot = -1, second_slot = -1;
    s16 first_mix[2] = {0}, second_mix[2] = {0}, mixed[2] = {0};
    s32 nonzero_frames = 0;
    if (g_SndDisabled || !slots || !checkpoint || assetCatalogResolveAny(id)
            || snprintf(path, sizeof(path), "%s/sample.wav", folder) >= sizeof(path)) goto done;
    source = fsFileLoad(path, &size);
    if (!source || size != 108) goto done;
    edited = malloc(size);
    if (!edited) goto done;
    memcpy(edited, source, size);
    if (assetCatalogScanExternalLayoutFolderDeferred("audiogenproof", folder) != 1) goto done;
    registered = 1; step = "acquire";
    first = catalogAudioGenerationAcquire(id, error, sizeof(error));
    alias = catalogAudioGenerationAcquire(id, error, sizeof(error));
    if (!first || alias != first) goto done;
    first_slot = catalogAudioGenerationSlot(first);
    strcpy(hash, catalogAudioGenerationHash(first));
    const s16 *pcm = catalogAudioGenerationPcm(first, &frames, &rate);
    if (!pcm || frames != 16 || rate != 22050) goto done;
    sha256Hash(pcm, frames * 2 * sizeof(s16), native_hash);
    audio_sample_parameters_t bounds = {0};
    bounds.base_pitch = 2.0f;
    const int allocations = SDL_GetNumAllocations();
    if (audioStartPcmSound(pcm, frames, rate, &bounds, 0x7fff, 64, FLT_MAX,
            0, 0, &first_voice) || first_voice || SDL_GetNumAllocations() != allocations) goto done;
    first_voice = sndStart(0, (s16)first_slot, &first_voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!first_voice || !audioFileSoundOwnsHandle(first_voice)) goto done;
    harnessAudioMixFrame(first_mix);
    if (first_mix[0] <= 0 || first_mix[1] <= first_mix[0]) goto done;
    /* Edit only the original WAV frames. Existing native voice owns its PCM. */
    for (u32 i = 0; i < frames; ++i) {
        const s16 values[2] = {4000, 5000};
        memcpy(edited + 44 + i * 4, values, sizeof(values));
    }
    if (!harnessWriteBytes(path, edited, size)) goto done;
    step = "source_edit";
    second = catalogAudioGenerationAcquire(id, error, sizeof(error));
    if (!second || second == first || !strcmp(hash, catalogAudioGenerationHash(second))) goto done;
    second_slot = catalogAudioGenerationSlot(second);
    if (second_slot == first_slot || !assetCatalogUnregister(id)) goto done;
    registered = 0;
    assetCatalogResetCustomSoundSlots();
    step = "retired_source";
    if (assetCatalogResolveAny(id) || catalogAudioGenerationForSlot(first_slot) != first
            || catalogAudioGenerationForSlot(second_slot) != second) goto done;
    harnessAudioMixFrame(mixed);
    if (memcmp(mixed, first_mix, sizeof(mixed))) goto done;
    sha256Hash(pcm, frames * 2 * sizeof(s16), retained_hash);
    if (memcmp(native_hash, retained_hash, sizeof(native_hash))) goto done;
    audioFileSoundStop(first_voice); first_voice = NULL;
    second_voice = sndStart(0, (s16)second_slot, &second_voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!second_voice) goto done;
    harnessAudioMixFrame(second_mix);
    if (second_mix[0] <= first_mix[0] || second_mix[1] <= first_mix[1]) goto done;
    audioFileSoundStop(second_voice); second_voice = NULL;
    first_voice = sndStart(0, (s16)first_slot, &first_voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!first_voice) goto done;
    catalogAudioGenerationRelease(alias); alias = NULL;
    if (catalogAudioGenerationForSlot(first_slot) != first) goto done;
    catalogAudioGenerationRelease(first); first = NULL;
    if (catalogAudioGenerationForSlot(first_slot)) goto done;
    step = "playing_after_release";
    harnessAudioMixFrame(mixed);
    if (!first_voice || memcmp(mixed, first_mix, sizeof(mixed))) goto done;
    audioFileSoundStop(first_voice); first_voice = NULL;
    for (u32 i = 0; i < frames; ++i) {
        const s16 values[2] = {8000, 9000};
        memcpy(edited + 44 + i * 4, values, sizeof(values));
    }
    if (!harnessWriteBytes(path, edited, size)
            || assetCatalogScanExternalLayoutFolderDeferred("audiogenproof", folder) != 1) goto done;
    registered = 1; step = "slot_reuse_and_loop";
    reused = catalogAudioGenerationAcquire(id, error, sizeof(error));
    if (!reused || catalogAudioGenerationSlot(reused) != first_slot) goto done;
    reused_voice = sndStart(0, (s16)first_slot, &reused_voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!reused_voice || !assetCatalogUnregister(id)) goto done;
    registered = 0;
    catalogAudioGenerationRelease(reused); reused = NULL;
    harnessAudioMixFrame(mixed);
    if (mixed[0] <= second_mix[0] || mixed[1] <= second_mix[1]) goto done;
    s16 tail[64] = {0};
    audioMixFileSoundsInto(tail, 32);
    for (s32 i = 0; i < 32; ++i) if (tail[i * 2] || tail[i * 2 + 1]) ++nonzero_frames;
    /* Sixteen source frames plus one two-frame loop, less first frame above. */
    if (nonzero_frames != 17 || reused_voice) goto done;
    ok = 1;
done:
    if (first_voice) audioFileSoundStop(first_voice);
    if (second_voice) audioFileSoundStop(second_voice);
    if (reused_voice) audioFileSoundStop(reused_voice);
    if (source && !harnessWriteBytes(path, source, size)) ok = 0;
    catalogAudioGenerationRelease(alias); catalogAudioGenerationRelease(first);
    catalogAudioGenerationRelease(second); catalogAudioGenerationRelease(reused);
    if (registered && assetCatalogResolveAny(id) && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (slots && !assetCatalogRestoreCustomSoundSlots(slots)) ok = 0;
    assetCatalogDestroyCustomSoundSlotSnapshot(slots);
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    free(edited); free(source);
    catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "AUDIO.GENERATION.HARNESS: step=%s result=%s first=%d,%d second=%d,%d tail=%d error=%s",
        step, ok ? "PASS" : "FAIL", first_mix[0], first_mix[1], second_mix[0], second_mix[1], nonzero_frames, error);
    return ok;
}

static s32 harnessCommandGeneration(const char *id, const char *folder)
{
    const char *ids[] = {"cmdimport_generation:parent", "cmdimport_generation:left",
        "cmdimport_generation:right", "cmdimport_generation:clip", "cmdimport_generation:sound"};
    const char *files[] = {"animations/left/commands.json", "animations/right/commands.json",
        "animations/clip/frames.bin", "audio/sfx/sound/sample.wav", "behavior.graph.json"};
    const char *cycle = "{\"source_format\":\"weapon_animation_commands\",\"name\":\"Reload\",\"command_count\":2,\"commands\":[{\"command\":\"include_animation\",\"slot\":0,\"animation\":\"cmdimport_generation:parent\"},{\"command\":\"end\"}]}";
    const char *missing = "{\"source_format\":\"weapon_animation_commands\",\"name\":\"Reload\",\"command_count\":2,\"commands\":[{\"command\":\"include_animation\",\"slot\":0,\"animation\":\"absent:commands\"},{\"command\":\"end\"}]}";
    char paths[5][FS_MAXPATH], error[256] = {0};
    u8 *source[5] = {0}, *edit = NULL;
    u32 sizes[5] = {0};
    catalog_command_generation_t *generations[4] = {0}, *rejected = NULL;
    wg_v2_native_bundle *native = NULL, *latest = NULL;
    wg_v2_program *program = NULL;
    struct sndstate *voice = NULL;
    file_provider_checkpoint_t provider;
    loader_pool_snapshot_t *pool = loaderPoolSnapshotCreate();
    void *sound_slots = assetCatalogSnapshotCustomSoundSlots(), *anim_slots = assetCatalogSnapshotCustomAnimSlots();
    s32 checkpoint = fileProviderCheckpointCreate(&provider), scanned = 0, ok = 0;
    const char *step = "source";
    s32 old_clip = -1, new_clip = -1, old_sound = -1, new_sound = -1;
    f32 old_y = 0, new_y = 0, retained_y = 0;
    s16 old_mix[2] = {0}, new_mix[2] = {0};
    const struct guncmd *commands = NULL, *last_commands = NULL;
    const struct weaponfunc_shoot *function = NULL;
    for (size_t i = 0; i < 5; ++i) if (assetCatalogResolveAny(ids[i])) goto done;
    if (!pool || !sound_slots || !anim_slots || !checkpoint || g_SndDisabled) goto done;
    for (size_t i = 0; i < 5; ++i) {
        if (snprintf(paths[i], sizeof(paths[i]), "%s/%s", folder, files[i]) >= sizeof(paths[i])) goto done;
        source[i] = fsFileLoad(paths[i], &sizes[i]);
        if (!source[i] || !sizes[i]) goto done;
    }
    if (sizes[2] != 32 || sizes[3] != 108) goto done;
    scanned = 1;
    if (assetCatalogScanExternalLayoutFolderDeferred("cmdimport_generation", folder) != 5) goto done;
    step = "native_resolver";
    generations[0] = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!generations[0]) goto done;
    commands = catalogCommandGenerationCommands(generations[0]);
    const struct guncmd *left = (const struct guncmd *)commands[0].unk04;
    const struct guncmd *right = (const struct guncmd *)commands[1].unk04;
    if (!left || !right || left == right || left[0].unk04 != 11 || right[0].unk04 != 22
            || left[1].unk04 != (intptr_t)right || commands == loaderPoolFindAnimationByCatalogId(id)) goto done;
    rejected = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!rejected || strcmp(catalogCommandGenerationHash(rejected), catalogCommandGenerationHash(generations[0]))) goto done;
    catalogCommandGenerationRelease(rejected); rejected = NULL;
    program = wgV2Compile((const char *)source[4], sizes[4], 1,
        catalogCommandGenerationHash(generations[0]), error, sizeof(error));
    if (!program || !(native = wgV2NativePrepare(program, catalogGraphNativeResolve, NULL, error, sizeof(error)))) goto done;
    function = (const struct weaponfunc_shoot *)wgV2NativeFunction(wgV2NativeFind(native, "shot"));
    if (!function || !function->base.fire_animation || function->base.fire_animation == commands
            || function->shootsound != commands[3].unk04
            || ((const struct guncmd *)function->base.fire_animation[0].unk04)[0].unk04 != 11) goto done;
    old_clip = commands[2].unk02; old_sound = commands[3].unk04;
    if (!harnessGenerationFrame(old_clip, &old_y) || !(old_y > 0)) goto done;
    /* Change only one child command's public scalar. */
    step = "command_edit";
    edit = malloc(sizes[0] + 1);
    if (!edit) goto done;
    memcpy(edit, source[0], sizes[0]); edit[sizes[0]] = 0;
    char *ticks = strstr((char *)edit, "\"ticks\": 11");
    if (!ticks) goto done;
    ticks[strlen("\"ticks\": ")] = '9';
    if (!harnessWriteBytes(paths[0], edit, sizes[0])) goto done;
    free(edit); edit = NULL;
    generations[1] = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!generations[1] || !strcmp(catalogCommandGenerationHash(generations[0]), catalogCommandGenerationHash(generations[1]))
            || ((const struct guncmd *)catalogCommandGenerationCommands(generations[1])[0].unk04)[0].unk04 != 91
            || left[0].unk04 != 11) goto done;
    step = "clip_edit";
    edit = malloc(sizes[2]);
    if (!edit) goto done;
    memcpy(edit, source[2], sizes[2]);
    f32 position = 600.0f; memcpy(edit + 24, &position, sizeof(position));
    if (!harnessWriteBytes(paths[2], edit, sizes[2])) goto done;
    free(edit); edit = NULL;
    generations[2] = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!generations[2] || !strcmp(catalogCommandGenerationHash(generations[1]), catalogCommandGenerationHash(generations[2]))) goto done;
    new_clip = catalogCommandGenerationCommands(generations[2])[2].unk02;
    if (new_clip == old_clip || !harnessGenerationFrame(new_clip, &new_y) || !(new_y > old_y)) goto done;
    step = "audio_edit";
    edit = malloc(sizes[3]);
    if (!edit) goto done;
    memcpy(edit, source[3], sizes[3]);
    for (size_t i = 0; i < 16; ++i) {
        const s16 pcm[] = {4000, 5000}; memcpy(edit + 44 + i * 4, pcm, sizeof(pcm));
    }
    if (!harnessWriteBytes(paths[3], edit, sizes[3])) goto done;
    free(edit); edit = NULL;
    generations[3] = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!generations[3] || !strcmp(catalogCommandGenerationHash(generations[2]), catalogCommandGenerationHash(generations[3]))) goto done;
    last_commands = catalogCommandGenerationCommands(generations[3]);
    new_sound = last_commands[3].unk04;
    if (new_sound == old_sound || last_commands[2].unk02 != new_clip) goto done;
    latest = wgV2NativePrepare(program, catalogGraphNativeResolve, NULL, error, sizeof(error));
    if (!latest || !strcmp(wgV2NativeClosureHash(native), wgV2NativeClosureHash(latest))) goto done;
    step = "cycle_and_missing_rejection";
    if (!harnessWriteBytes(paths[1], cycle, strlen(cycle))) goto done;
    rejected = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (rejected || !strstr(error, "cycle")) goto done;
    if (!harnessWriteBytes(paths[1], missing, strlen(missing))) goto done;
    rejected = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (rejected || !strstr(error, "absent:commands")) goto done;
    if (!harnessWriteBytes(paths[1], source[1], sizes[1])) goto done;
    wg_v2_native_dependency invalid = {0};
    if (catalogGraphNativeResolve(NULL, WG_V2_COMMAND_SOURCE, ids[4], &invalid, error, sizeof(error))
            || invalid.lease || invalid.commands || invalid.release || invalid.catalog_id) {
        if (invalid.release) invalid.release(invalid.lease);
        goto done;
    }
    step = "late_decode_rollback";
    if (!harnessWriteBytes(paths[3], "bad", 3)) goto done;
    rejected = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (rejected) goto done;
    /* Retire all source rows and reset ordinary slots. The real native action
     * still owns the original command closure, animation and sound generations. */
    step = "retired_native_consumers";
    for (size_t i = 0; i < 5; ++i) if (!assetCatalogUnregister(ids[i])) goto done;
    assetCatalogResetCustomAnimSlots(); assetCatalogResetCustomSoundSlots();
    for (size_t i = 0; i < 3; ++i) {
        catalogCommandGenerationRelease(generations[i]); generations[i] = NULL;
    }
    commands = function->base.fire_animation;
    if (((const struct guncmd *)commands[0].unk04)[0].unk04 != 11
            || !harnessGenerationFrame(commands[2].unk02, &retained_y) || retained_y != old_y
            || !harnessGenerationFrame(new_clip, &retained_y) || retained_y != new_y) goto done;
    voice = sndStart(0, function->shootsound, &voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!voice) goto done;
    harnessAudioMixFrame(old_mix); audioFileSoundStop(voice); voice = NULL;
    voice = sndStart(0, new_sound, &voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!voice) goto done;
    harnessAudioMixFrame(new_mix); audioFileSoundStop(voice); voice = NULL;
    if (old_mix[0] <= 0 || old_mix[1] <= old_mix[0] || new_mix[0] <= old_mix[0] || new_mix[1] <= old_mix[1]) goto done;
    step = "release";
    wgV2NativeRelease(native); native = NULL;
    if (catalogAnimationGenerationData(old_clip) || catalogAudioGenerationForSlot(old_sound)) goto done;
    wgV2NativeRelease(latest); latest = NULL;
    catalogCommandGenerationRelease(generations[3]); generations[3] = NULL;
    if (catalogAnimationGenerationData(new_clip) || catalogAudioGenerationForSlot(new_sound)) goto done;
    error[0] = 0; ok = 1;
done:
    if (voice) audioFileSoundStop(voice);
    wgV2NativeRelease(native); wgV2NativeRelease(latest); wgV2ProgramRelease(program);
    catalogCommandGenerationRelease(rejected);
    for (size_t i = 0; i < 4; ++i) catalogCommandGenerationRelease(generations[i]);
    for (size_t i = 0; i < 5; ++i) {
        if (source[i] && !harnessWriteBytes(paths[i], source[i], sizes[i])) ok = 0;
        free(source[i]);
        if (scanned && assetCatalogResolveAny(ids[i]) && !assetCatalogRollbackUnactivatedRegistration(ids[i])) ok = 0;
    }
    free(edit);
    if (pool && !loaderPoolSnapshotRestore(pool)) ok = 0;
    if (sound_slots && !assetCatalogRestoreCustomSoundSlots(sound_slots)) ok = 0;
    if (anim_slots && !assetCatalogRestoreCustomAnimSlots(anim_slots)) ok = 0;
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    loaderPoolSnapshotDestroy(pool);
    assetCatalogDestroyCustomSoundSlotSnapshot(sound_slots); assetCatalogDestroyCustomAnimSlotSnapshot(anim_slots);
    catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "COMMAND.GENERATION.HARNESS: step=%s result=%s old_y=%.1f new_y=%.1f old_mix=%d,%d new_mix=%d,%d error=%s",
        step, ok ? "PASS" : "FAIL", old_y, new_y, old_mix[0], old_mix[1], new_mix[0], new_mix[1], error);
    return ok;
}


static s32 harnessReadCommands(const char *directory, const char *file, const char *id)
{
	char path[FS_MAXPATH];
	if (snprintf(path, sizeof(path), "%s/%s", directory, file) >= sizeof(path)) return 0;
	u32 size = 0;
	char *json = fsFileLoad(path, &size);
	if (!json) return 0;
	s32 ok = loaderPoolParseAnimationCatalogSourceJson(json, size, path, id);
	free(json);
	return ok;
}

static s32 harnessStageCommands(loader_animation_source_batch_t *batch,
        const char *directory, const char *file, const char *id)
{
    char path[FS_MAXPATH];
    if (snprintf(path, sizeof(path), "%s/%s", directory, file) >= sizeof(path)) return 0;
    u32 size = 0;
    char *json = fsFileLoad(path, &size);
    s32 ok = json && loaderAnimationSourceBatchStage(batch, json, size, path, id);
    free(json);
    return ok;
}

static s32 harnessBatchBoundaries(const char *directory)
{
    const char *left_id = "command_a:reload", *right_id = "command_b:reload";
    const struct guncmd *left = loaderPoolFindAnimationByCatalogId(left_id);
    const struct guncmd *right = loaderPoolFindAnimationByCatalogId(right_id);
    loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
    s32 ok = batch && left && right;
    if (ok) ok = harnessStageCommands(batch, directory, "left.json", left_id)
        && harnessStageCommands(batch, directory, "left.json", left_id)
        && !harnessStageCommands(batch, directory, "right.json", left_id)
        && !loaderAnimationSourceBatchCommit(batch);
    loaderAnimationSourceBatchDestroy(batch);
    batch = loaderAnimationSourceBatchCreate();
    if (ok) ok = batch && harnessStageCommands(batch, directory, "left.json", left_id)
        && harnessStageCommands(batch, directory, "rejected.json", right_id)
        && loaderPoolFindAnimationByCatalogId(left_id) == left
        && !loaderAnimationSourceBatchCommit(batch)
        && loaderPoolFindAnimationByCatalogId(left_id) == left
        && loaderPoolFindAnimationByCatalogId(right_id) == right;
    loaderAnimationSourceBatchDestroy(batch);
    batch = loaderAnimationSourceBatchCreate();
    char path[FS_MAXPATH];
    u32 size = 0;
    char *original = NULL;
    if (ok && snprintf(path, sizeof(path), "%s/left.json", directory) < sizeof(path)) {
        original = fsFileLoad(path, &size);
        ok = batch && original && harnessStageCommands(batch, directory, "left.json", left_id);
        FILE *file = ok ? fopen(path, "ab") : NULL;
        if (!file) ok = 0;
        else {
            if (fputc(' ', file) == EOF) ok = 0;
            if (fclose(file)) ok = 0;
            if (ok) ok = !loaderAnimationSourceBatchCommit(batch);
            /* Restore the exact public fixture bytes even if the assertion failed. */
            file = fopen(path, "wb");
            if (!file) ok = 0;
            else {
                if (fwrite(original, 1, size, file) != size) ok = 0;
                if (fclose(file)) ok = 0;
            }
        }
    } else ok = 0;
    free(original);
    loaderAnimationSourceBatchDestroy(batch);
    if (loaderPoolFindAnimationByCatalogId(left_id) != left
            || loaderPoolFindAnimationByCatalogId(right_id) != right) ok = 0;
    /* Reverse an existing edge. New child is deliberately staged last; old
     * source dependencies must not veto the valid submitted replacement DAG. */
    batch = loaderAnimationSourceBatchCreate();
    if (ok) ok = batch && harnessStageCommands(batch, directory, "order_left.json", left_id)
        && harnessStageCommands(batch, directory, "right.json", right_id)
        && loaderAnimationSourceBatchCommit(batch);
    loaderAnimationSourceBatchDestroy(batch);
    batch = loaderAnimationSourceBatchCreate();
    if (ok) ok = batch && harnessStageCommands(batch, directory, "order_right.json", right_id)
        && harnessStageCommands(batch, directory, "left.json", left_id)
        && loaderAnimationSourceBatchCommit(batch);
    loaderAnimationSourceBatchDestroy(batch);
    if (ok) {
        left = loaderPoolFindAnimationByCatalogId(left_id);
        right = loaderPoolFindAnimationByCatalogId(right_id);
        ok = left && right && left[0].unk04 == 1 && right[0].unk04 == (intptr_t)left;
    }
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "PDWEAPON.COMMAND_BATCH.HARNESS: duplicate_edit_rollback=%s", ok ? "PASS" : "FAIL");
    return ok;
}

static s32 harnessScanCommandImport(const char *owner, const char *prefix,
        const char *kind, const char *variant)
{
    char path[FS_MAXPATH];
    s32 folder = !strcmp(kind, "folder");
    if (snprintf(path, sizeof(path), "%s_%s%s", prefix, variant, folder ? "" : ".pdmod") >= sizeof(path)) return -1;
    if (folder) return assetCatalogScanExternalLayoutFolderDeferred(owner, path);
    mod_archive_t *archive = modArchiveOpen(path);
    if (!archive) return -1;
    s32 count = assetCatalogScanComponentsFromArchive(owner, archive);
    modArchiveClose(archive);
    return count;
}

static s32 harnessCommandImport(const char *owner, const char *prefix, const char *kind)
{
    const char *names[] = {"parent", "left", "right", "clip", "sound", "late"};
    char ids[6][CATALOG_ID_LEN];
    for (size_t i = 0; i < 6; ++i) {
        if (snprintf(ids[i], sizeof(ids[i]), "%s:%s", owner, names[i]) >= sizeof(ids[i])
                || assetCatalogResolveAny(ids[i])) return 0;
    }
    file_provider_checkpoint_t provider_before, provider_good, provider_after;
    loader_pool_snapshot_t *pool = loaderPoolSnapshotCreate();
    void *sound_slots = assetCatalogSnapshotCustomSoundSlots();
    void *anim_slots = assetCatalogSnapshotCustomAnimSlots();
    catalog_entry_snapshot_t *rows = calloc(5, sizeof(*rows));
    s32 checkpoint = fileProviderCheckpointCreate(&provider_before);
    s32 ok = 0;
    const char *step = "admission";
    if (!pool || !sound_slots || !anim_slots || !rows || !checkpoint) goto done;
    if (harnessScanCommandImport(owner, prefix, kind, "good") < 5) goto done;
    const struct guncmd *parent = loaderPoolFindAnimationByCatalogId(ids[0]);
    const struct guncmd *left = loaderPoolFindAnimationByCatalogId(ids[1]);
    const struct guncmd *right = loaderPoolFindAnimationByCatalogId(ids[2]);
    const asset_entry_t *clip = assetCatalogResolve(ids[3]);
    const asset_entry_t *sound = assetCatalogResolve(ids[4]);
    step = "native_dependencies";
    if (!parent || !left || !right || left == right || left[0].unk04 != 11 || right[0].unk04 != 22
            || parent[0].unk04 != (intptr_t)left || parent[1].unk04 != (intptr_t)right
            || !clip || !sound || parent[2].unk02 != clip->ext.anim.anim_id
            || parent[3].unk04 != sound->ext.audio.sound_id
            || clip->ext.anim.anim_id < 0 || sound->ext.audio.sound_id <= 0) goto done;
    for (size_t i = 0; i < 5; ++i) {
        const asset_entry_t *row = assetCatalogResolve(ids[i]);
        if (!row) goto done;
        const char *source = fileProviderPath(row->source.primary);
        if (!source || !strstr(source, !strncmp(prefix, "./", 2) ? prefix + 2 : prefix)) goto done;
        if (strcmp(kind, "folder") && !strstr(source, "_good.pdmod::")) goto done;
        catalogEntrySnapshotCapture(&rows[i], row);
    }
    if (!fileProviderCheckpointCreate(&provider_good)) goto done;
    const char *rejects[] = {"late", "cycle"};
    for (size_t r = 0; r < 2; ++r) {
        step = rejects[r];
        if (harnessScanCommandImport(owner, prefix, kind, rejects[r]) >= 0
                || assetCatalogResolveAny(ids[5])
                || loaderPoolFindAnimationByCatalogId(ids[0]) != parent
                || loaderPoolFindAnimationByCatalogId(ids[1]) != left
                || loaderPoolFindAnimationByCatalogId(ids[2]) != right
                || parent[0].unk04 != (intptr_t)left || left[0].unk04 != 11
                || right[0].unk04 != 22) goto done;
        step = r == 0 ? "late_rows" : "cycle_rows";
        for (size_t i = 0; i < 5; ++i) {
            const asset_entry_t *row = assetCatalogResolve(ids[i]);
            if (!row || !catalogEntrySnapshotMatches(&rows[i], row)) {
                sysLogPrintf(LOG_WARNING, "PDWEAPON.COMMAND_IMPORT.ROW_DIFF: id=%s state=%d->%d ref=%d->%d",
                    ids[i], rows[i].row.load_state, row ? row->load_state : -1,
                    rows[i].row.ref_count, row ? row->ref_count : -1);
                goto done;
            }
        }
        step = r == 0 ? "late_provider" : "cycle_provider";
        if (!fileProviderCheckpointCreate(&provider_after)
                || provider_after.pool_used != provider_good.pool_used
                || provider_after.path_count != provider_good.path_count) goto done;
    }
    ok = 1;
done:
    for (size_t i = 6; i-- > 0;)
        if (assetCatalogResolveAny(ids[i]) && !assetCatalogRollbackUnactivatedRegistration(ids[i])) ok = 0;
    if (pool && !loaderPoolSnapshotRestore(pool)) ok = 0;
    if (sound_slots && !assetCatalogRestoreCustomSoundSlots(sound_slots)) ok = 0;
    if (anim_slots && !assetCatalogRestoreCustomAnimSlots(anim_slots)) ok = 0;
    if (checkpoint && !fileProviderCheckpointRestore(&provider_before)) ok = 0;
    loaderPoolSnapshotDestroy(pool);
    assetCatalogDestroyCustomSoundSlotSnapshot(sound_slots);
    assetCatalogDestroyCustomAnimSlotSnapshot(anim_slots);
    free(rows);
    catalogBuildRuntimeCaches();
    catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "PDWEAPON.COMMAND_IMPORT.HARNESS: kind=%s step=%s result=%s", kind, step, ok ? "PASS" : "FAIL");
    return ok;
}

static s32 harnessCommandSources(const char *directory)
{
	const char *ids[] = {"command_a:reload", "command_b:reload", "commandproof:parent"};
	for (size_t i = 0; i < 3; ++i) if (assetCatalogResolveAny(ids[i])) return 0;
	loader_pool_snapshot_t *snapshot = loaderPoolSnapshotCreate();
	if (!snapshot) return 0;
	s32 created = 0, ok = 0;
	for (; created < 3; ++created) {
		if (!assetCatalogRegisterAnimation(ids[created], -1, "Reload", 0, NULL)) goto done;
		assetCatalogSetCategoryById(ids[created], "weapon_animation");
	}
	/* Load the parent first to exercise exact names through deferred binding. */
	if (!harnessReadCommands(directory, "parent.json", ids[2])
			|| !harnessReadCommands(directory, "left.json", ids[0])
			|| !harnessReadCommands(directory, "right.json", ids[1])) goto done;
	loaderPoolFinalize();
	const struct guncmd *left = loaderPoolFindAnimationByCatalogId(ids[0]);
	const struct guncmd *right = loaderPoolFindAnimationByCatalogId(ids[1]);
	const struct guncmd *parent = loaderPoolFindAnimationByCatalogId(ids[2]);
	if (!left || !right || !parent || left == right || left[0].unk04 != 1 || right[0].unk04 != 2
			|| parent[0].unk04 != (intptr_t)left || parent[1].unk04 != (intptr_t)right) goto done;
	if (harnessReadCommands(directory, "rejected.json", ids[0])
			|| loaderPoolFindAnimationByCatalogId(ids[0]) != left || left[0].unk04 != 1) goto done;
	if (harnessReadCommands(directory, "cycle.json", ids[0])
			|| loaderPoolFindAnimationByCatalogId(ids[0]) != left || parent[0].unk04 != (intptr_t)left) goto done;
	if (!harnessReadCommands(directory, "replaced.json", ids[0])) goto done;
	loaderPoolFinalize();
	const struct guncmd *replacement = loaderPoolFindAnimationByCatalogId(ids[0]);
	if (!replacement || replacement == left || replacement[0].unk04 != 3 || left[0].unk04 != 1
			|| parent[0].unk04 != (intptr_t)replacement || parent[1].unk04 != (intptr_t)right) goto done;
	if (!harnessBatchBoundaries(directory)) goto done;
	ok = 1;
done:
	while (created > 0) if (!assetCatalogRollbackUnactivatedRegistration(ids[--created])) ok = 0;
	if (!loaderPoolSnapshotRestore(snapshot)) ok = 0;
	loaderPoolSnapshotDestroy(snapshot);
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING, "PDWEAPON.COMMAND_SOURCE.HARNESS: result=%s", ok ? "PASS" : "FAIL");
	return ok;
}

static s32 harnessMeshAccept(const char *owner, const char *archive,
		const char *mesh_id, const char *payload_id, const char *expected_text,
		s32 reject_geometry)
{
	/* Exercise the ordinary native consumer without valid hand/model storage:
	 * both representations of an empty authored list must return before access. */
	struct gunviscmd empty_visibility = {0};
	empty_visibility.type = GUNVISCMD_END;
	bgunExecuteGunVisCommands(NULL, NULL, NULL);
	bgunExecuteGunVisCommands(NULL, NULL, &empty_visibility);
	sysLogPrintf(LOG_NOTE, "PDWEAPON.MESH.EMPTY_VISIBILITY: owner=%s result=PASS", owner);
	char err[512] = {0};
	harness_deps_t deps = {0};
	s32 weapon_slot = -1, mp_slot = -1, loaded = 0, ok = 0;
	char *count_text = NULL;
	long edge_value = strtol(expected_text ? expected_text : "", &count_text, 10);
	long expected_models = count_text && *count_text == ',' ? strtol(count_text + 1, NULL, 10) : 0;
	s32 expected_edge = edge_value > 0 && edge_value <= 32767 ? (s32)edge_value : -1;
	const s32 baseline_edges = catalogDepCount();
	void *model_slots = assetCatalogSnapshotCustomModelSlots();
	void *weapon_slots = assetCatalogSnapshotCustomWeaponSlots();
	if (!model_slots || !weapon_slots || expected_edge <= 0) goto done;
	if (!assetCatalogResolveWeaponPrivateSlots(owner, -1, 0, &weapon_slot, &mp_slot)
			|| weapon_slot < WEAPON_CUSTOM_START) goto done;
	asset_entry_t *entry = harnessRegisterOwner(owner, archive);
	if (!entry) goto done;
	entry->runtime_index = weapon_slot;
	entry->mp_index = mp_slot;
	entry->ext.weapon.weapon_id = mp_slot;
	if (assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0, err, sizeof(err)) < 0) goto done;
	catalogDepForEach(owner, harnessCollectDep, &deps);
	if (deps.invalid || harnessFind(&deps, mesh_id, ASSET_MODEL) < 0
			|| !catalogDepContains(payload_id, mesh_id)) goto done;
	if (expected_models > 0) {
		size_t model_count = 0;
		for (size_t i = 0; i < deps.count; i++) if (deps.types[i] == ASSET_MODEL) model_count++;
		if (model_count != (size_t)expected_models) goto done;
	}
	const asset_entry_t *mesh = assetCatalogResolve(mesh_id);
	if (!mesh || mesh->runtime_index < MODEL_CUSTOM_START || mesh->source_filenum <= 0
			|| mesh->source.primary.provider != fileProvider()) goto done;
	const char *path = fileProviderPath(mesh->source.primary);
	if (!path || !strstr(path, ".pdprojectile::") || !strstr(path, ".pdmesh::")) goto done;
	/* Ordinary scanner callers rebuild their hot indexes after publication. */
	catalogBuildRuntimeCaches();
	catalogLoadInit();
	if (weaponGraphRuntimeRegisterWeaponArchive(weapon_slot, archive, err, sizeof(err)) != 0) goto done;
	const weapon_graph_projectile_runtime_t *projectile = weaponGraphRuntimeGetProjectile(payload_id);
	mesh = assetCatalogResolve(mesh_id);
	if (!projectile || !projectile->has_projectile_modelnum || !mesh
			|| projectile->projectile_modelnum != mesh->runtime_index) goto done;
	const asset_entry_t *held = assetCatalogResolve(owner);
	char held_id[CATALOG_ID_LEN];
	snprintf(held_id, sizeof(held_id), "%s_held_selected", owner);
	const asset_entry_t *held_mesh = assetCatalogResolve(held_id);
	if (!held || !held_mesh || held->source_filenum != held_mesh->source_filenum) goto done;
	loaded = catalogLoadTypedAsset(ASSET_MODEL, mesh_id);
	if (reject_geometry) {
		mesh = assetCatalogResolve(mesh_id);
		ok = !loaded && mesh && mesh->load_state < ASSET_STATE_LOADED
			&& mesh->payload_kind == ASSET_PAYLOAD_NONE && !mesh->loaded_data;
	} else {
		ok = loaded && harnessMeshVertices(catalogGetLoadedModeldef(mesh_id), expected_edge);
	}
done:
	if (loaded) catalogReleaseTypedAsset(ASSET_MODEL, mesh_id);
	if (weapon_slot >= WEAPON_CUSTOM_START) weaponGraphRuntimeClearWeapon(weapon_slot);
	if (!harnessCleanup(owner, &deps)) ok = 0;
	if (model_slots && !assetCatalogRestoreCustomModelSlots(model_slots)) ok = 0;
	if (weapon_slots && !assetCatalogRestoreCustomWeaponSlots(weapon_slots)) ok = 0;
	assetCatalogDestroyCustomModelSlotSnapshot(model_slots);
	assetCatalogDestroyCustomWeaponSlotSnapshot(weapon_slots);
	catalogBuildRuntimeCaches();
	catalogLoadInit();
	if (catalogDepCount() != baseline_edges) ok = 0;
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
		"PDWEAPON.MESH.HARNESS: owner=%s case=%s result=%s error=%s",
		owner, reject_geometry ? "malformed_geometry" : "source_to_native_record",
		ok ? "PASS" : "FAIL", err[0] ? err : "(none)");
	return ok;
}

static s32 harnessBaseMeshPreserve(const char *owner, const char *archive,
		const char *high_id, const char *low_id)
{
	const asset_entry_t *high = assetCatalogResolve(high_id);
	const asset_entry_t *low = assetCatalogResolve(low_id);
	if (!high || !low) return 0;
	const s32 high_runtime = high->runtime_index, low_runtime = low->runtime_index;
	const s32 high_file = high->source_filenum, low_file = low->source_filenum;
	const asset_data_handle_t high_source = high->source.primary, low_source = low->source.primary;
	char err[512] = {0};
	s32 ok = assetCatalogRegisterWeaponNestedDependencies(owner, archive, 1, err, sizeof(err)) >= 0;
	high = assetCatalogResolve(high_id);
	low = assetCatalogResolve(low_id);
	ok = ok && high && low && high->runtime_index == high_runtime && low->runtime_index == low_runtime
		&& high->source_filenum == high_file && low->source_filenum == low_file
		&& memcmp(&high->source.primary, &high_source, sizeof(high_source)) == 0
		&& memcmp(&low->source.primary, &low_source, sizeof(low_source)) == 0;
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
		"PDWEAPON.MESH.HARNESS: owner=%s case=base_identity result=%s high=%d low=%d error=%s",
		owner, ok ? "PASS" : "FAIL", high_file, low_file, err);
	return ok;
}

static s32 harnessMeshSlotRollback(const char *owner, const char *archive, char *absent_csv)
{
	void *snapshot = assetCatalogSnapshotCustomModelSlots();
	if (!snapshot) return 0;
	s32 expected = assetCatalogResolveModelPrivateSlot("meshproof:rollback_probe");
	s32 ok = expected >= MODEL_CUSTOM_START && assetCatalogRestoreCustomModelSlots(snapshot);
	if (ok) ok = harnessReject(owner, archive, absent_csv);
	if (ok) ok = assetCatalogResolveModelPrivateSlot("meshproof:rollback_probe") == expected;
	if (!assetCatalogRestoreCustomModelSlots(snapshot)) ok = 0;
	assetCatalogDestroyCustomModelSlotSnapshot(snapshot);
	catalogBuildRuntimeCaches();
	catalogLoadInit();
	return ok;
}

static s32 harnessReceiptRetirement(struct player *p, s32 cause)
{
    struct gset retained = {0}, current = {0};
    uint64_t retained_scope = 0, current_scope = 0;
    wg_v2_delivery old = {0}, fresh = {0};
    struct handweaponinfo info;
    s32 ok = 0;
    wgV2RuntimeRetireHand(p, 0, "receipt fixture setup");
    p->isdead = 0; p->hands[0].gset.weaponfunc = 0;
    p->hands[0].loadedammo[0] = p->hands[0].loadedammo[1] = 7;
    if (!wgV2RuntimeEnsureHand(p, 0)) goto done;
    bgunGetWeaponInfo(&info, 0); bgunSetTriggerOn(0, false); bgunTickInc(&info, 0, 1);
    bgunSetTriggerOn(0, true); bgunTickInc(&info, 0, 1); bgunTickInc(&info, 0, 1);
    if (!wgV2RuntimeShotBegin(p, 0, &retained, &retained_scope, &old)) goto done;
    const float damage = gsetGetDamage(&retained);
    if (cause == 0) { p->hands[0].gset.weaponfunc = 1; }
    if (cause == 1) wgV2RuntimeRetireHand(p, 0, "receipt fixture same-mode rebind");
    if (cause == 2) wgV2RuntimePlayerBegin(p);
    if (cause == 3) { p->isdead = 1; if (wgV2RuntimeEnsureHand(p, 0)) goto done; p->isdead = 0; }
    if (!wgV2RuntimeEnsureHand(p, 0) || wgV2RuntimeDeliveryValid(&old)
            || gsetGetDamage(&retained) != damage) goto done;
    bgunGetWeaponInfo(&info, 0); bgunSetTriggerOn(0, false); bgunTickInc(&info, 0, 1);
    bgunSetTriggerOn(0, true); bgunTickInc(&info, 0, 1); bgunTickInc(&info, 0, 1);
    if (!wgV2RuntimeShotBegin(p, 0, &current, &current_scope, &fresh)) goto done;
    if (old.owner_generation == fresh.owner_generation && old.hand_generation == fresh.hand_generation) goto done;
    wgV2RuntimeShotComplete(&old); wgV2RuntimeNoiseComplete(&old); wgV2RuntimeShotAbort(&old);
    if (!wgV2RuntimeDeliveryValid(&fresh) || !wgV2RuntimeDeliveryPending(p, 0)) goto done;
    wgV2RuntimeScopeClose(retained_scope); retained_scope = 0;
    if (gsetGetDamage(&current) <= 0) goto done;
    ok = 1;
done:
    wgV2RuntimeScopeClose(retained_scope); wgV2RuntimeScopeClose(current_scope);
    wgV2RuntimeRetireHand(p, 0, "receipt fixture closeout");
    return ok;
}

static s32 harnessSpreadCapture(struct player *p)
{
    struct handweaponinfo info; struct gset dispatch = {0};
    uint64_t scope = 0; wg_v2_delivery receipt = {0}; s32 ok = 0;
    p->hands[0].gset.weaponfunc = 0; p->hands[0].loadedammo[0] = p->hands[0].loadedammo[1] = 7;
    if (!wgV2RuntimeEnsureHand(p, 0)) goto done;
    bgunGetWeaponInfo(&info, 0); bgunSetTriggerOn(0, false); bgunTickInc(&info, 0, 1);
    bgunSetTriggerOn(0, true); bgunTickInc(&info, 0, 1); bgunTickInc(&info, 0, 1);
    /* This native field is the burst producer; the unrelated public gset byte
     * deliberately disagrees. Mutating the hand after Begin must not change
     * the captured spread input. No random-camera stand-in is used. */
    p->hands[0].burstbullets = 1; p->hands[0].gset.unk063a = 17;
    if (!wgV2RuntimeShotBegin(p, 0, &dispatch, &scope, &receipt) || receipt.burstbullets != 1) goto done;
    p->hands[0].burstbullets = 2;
    if (dispatch.unk063a != 17 || bgunGetPlayerShotSpreadFromGset(&dispatch, receipt.burstbullets) != 2
            || bgunGetPlayerShotSpreadFromGset(&dispatch, p->hands[0].burstbullets) != 8
            || bgunGetPlayerShotSpreadFromGset(&dispatch, 0) != 8) goto done;
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.PRODUCTION.HARNESS: component=captured_native_burst_spread authored_spread=8 aim=accurate_single_shot captured_burst=1 live_burst=2 gset_byte=17 expected_spread=2 result=PASS");
    ok = 1;
done:
    wgV2RuntimeScopeClose(scope); wgV2RuntimeRetireHand(p, 0, "spread fixture closeout");
    return ok;
}

static int harnessGraphRecover(struct player *p, int hand)
{
    struct handweaponinfo info;
    bgunSetTriggerOn(hand, false);
    for (int tick = 0; tick < 256; ++tick) {
        bgunGetWeaponInfo(&info, hand); bgunTickInc(&info, hand, 1);
        if (p->hands[hand].state == HANDSTATE_IDLE && !wgV2RuntimeDeliveryPending(p, hand)) return 1;
    }
    return 0;
}

static int harnessGraphPress(struct player *p, int hand)
{
    struct handweaponinfo info;
    if (p->hands[hand].state != HANDSTATE_IDLE) return 0;
    bgunSetTriggerOn(hand, true);
    for (int tick = 0; tick < 32; ++tick) {
        bgunGetWeaponInfo(&info, hand); bgunTickInc(&info, hand, 1);
        if (wgV2RuntimeDeliveryPending(p, hand)) return p->hands[hand].firing;
    }
    return 0;
}

static int harnessGraphUnchanged(struct player *p, int hand, const wg_v2_runtime_snapshot *before,
        int shots, const s32 regions[7], int hits, const struct gset *dispatch, float damage)
{
    wg_v2_runtime_snapshot after = {0};
    const int target_unchanged = graph_component_observe || (graph_world.oracle
        && graph_world.oracle->lifecycle_generation == graph_world.oracle_generation
        && chrPtrIsValid(graph_world.oracle->chr) && graph_world.oracle->chr->damage == graph_world.stale_damage);
    return target_unchanged && wgV2RuntimeSnapshot(p, hand, &after) && !memcmp(before, &after, sizeof(after))
        && frGetData()->numshots == shots && !memcmp(regions, g_Vars.currentplayerstats->shotcount, 7*sizeof(s32))
        && graph_world.observed_hits == hits && !var80070590 && gsetGetDamage((struct gset *)dispatch) == damage;
}

static void harnessObserverSeed(const wg_v2_delivery *receipt, int disposition)
{
    memset(&graph_ledger, 0, sizeof(graph_ledger));
    graph_ledger.count = 1; graph_ledger.rows[0].identity = *receipt;
    graph_ledger.rows[0].admitted = 1; graph_ledger.rows[0].disposition = disposition;
    graph_observer_fault_test = 1;
}

/* Negative observer inputs only. No native noise, stats, hits or damage are
 * fabricated as gameplay evidence; these calls test the fail-closed ledger. */
static int harnessObserverFaults(struct player *p, const wg_v2_delivery *consumed_receipt,
        const wg_v2_delivery *consumed_noise)
{
    void *saved_ledger = malloc(sizeof(graph_ledger));
    wg_v2_observation consumed_event = graph_component_consumed;
    wg_v2_delivery current = {0}, current_noise = {0};
    struct gset storage = {0}; uint64_t scope = 0;
    int ok = 0;
    if (!saved_ledger) return 0;
    memcpy(saved_ledger, &graph_ledger, sizeof(graph_ledger));
    for (int kind = 0; kind < 3; ++kind) {
        const wg_v2_delivery *r = kind == 0 ? consumed_noise : consumed_receipt;
        harnessObserverSeed(r, 1);
        const int event_kind = kind == 0 ? WG_V2_OBS_NOISE_INCREMENT : kind == 1 ? WG_V2_OBS_HIT_EFFECT : WG_V2_OBS_SHOT_STATS;
        if (kind == 0) wgV2RuntimeObserveNoise(r, 0, 3, 3, 100);
        if (kind == 1) wgV2RuntimeObserveHit(r);
        if (kind == 2) wgV2RuntimeObserveShotStats(r, 1, 1);
        if (!graph_ledger.failed || graph_ledger.events[event_kind] != 1
                || graph_observer_last.receipt.action_ticket != r->action_ticket) goto done;
    }
    harnessObserverSeed(&consumed_event.receipt, 1);
    graph_ledger.rows[0].consumes = 1; graph_ledger.events[WG_V2_OBS_CONSUMED] = 1;
    weaponGraphObserve(&consumed_event);
    if (!graph_ledger.failed || graph_ledger.rows[0].consumes != 2 || graph_ledger.events[WG_V2_OBS_CONSUMED] != 2) goto done;
    graph_observer_fault_test = 0;
    if (!harnessGraphPress(p, 0) || !wgV2RuntimeShotBegin(p, 0, &storage, &scope, &current)
            || !wgV2RuntimeNoiseBegin(p, 0, &current_noise)) goto done;
    const wg_v2_delivery aborted = current;
    wgV2RuntimeShotAbort(&aborted); wgV2RuntimeScopeClose(scope); scope = 0;
    if (!wgV2RuntimeShotBegin(p, 0, &storage, &scope, &current)
            || current.reservation_generation == aborted.reservation_generation) goto done;
    for (int kind = 0; kind < 2; ++kind) {
        harnessObserverSeed(&aborted, 0);
        const int event_kind = kind == 0 ? WG_V2_OBS_HIT_EFFECT : WG_V2_OBS_SHOT_STATS;
        if (kind == 0) wgV2RuntimeObserveHit(&aborted);
        else wgV2RuntimeObserveShotStats(&aborted, 1, 1);
        if (!graph_ledger.failed || graph_ledger.events[event_kind] != 1
                || graph_observer_last.receipt.reservation_generation != aborted.reservation_generation
                || graph_observer_last.value.delivery.reservation_generation != current.reservation_generation
                || graph_ledger.rows[0].hits || graph_ledger.rows[0].stats) goto done;
    }
    harnessObserverSeed(&current_noise, 0);
    graph_ledger.rows[0].noise_begins = 1; graph_ledger.rows[0].noise_receipt = current_noise;
    wgV2RuntimeObserveNoise(&current_noise, 0, NAN, 3, 100);
    if (!graph_ledger.failed || graph_ledger.events[WG_V2_OBS_NOISE_INCREMENT] != 1
            || !graph_observer_last.value.host_present || !isnan(graph_observer_last.noise_after)) goto done;
    harnessObserverSeed(&current_noise, 0);
    graph_ledger.rows[0].noise_begins = 1; graph_ledger.rows[0].noise_receipt = current_noise;
    wgV2RuntimeObserveNoise(&current_noise, 0, 3, 3, 100);
    if (graph_ledger.failed || graph_ledger.events[WG_V2_OBS_NOISE_INCREMENT] != 1) goto done;
    wgV2RuntimeObserveNoise(&current_noise, 0, 3, 3, 100);
    if (!graph_ledger.failed || graph_ledger.rows[0].noises != 2 || graph_ledger.events[WG_V2_OBS_NOISE_INCREMENT] != 2) goto done;
    graph_observer_fault_test = 0;
    wgV2RuntimeRetireHand(p, 0, "observer negative missing-host setup");
    for (int kind = 0; kind < 4; ++kind) {
        const wg_v2_delivery *r = kind == 0 || kind == 3 ? &current_noise : &current;
        harnessObserverSeed(r, 4);
        const int event_kind = kind == 0 || kind == 3 ? WG_V2_OBS_NOISE_INCREMENT : kind == 1 ? WG_V2_OBS_HIT_EFFECT : WG_V2_OBS_SHOT_STATS;
        if (kind == 0 || kind == 3) wgV2RuntimeObserveNoise(r, 0, kind == 3 ? NAN : 3, 3, 100);
        if (kind == 1) wgV2RuntimeObserveHit(r);
        if (kind == 2) wgV2RuntimeObserveShotStats(r, 1, 1);
        if (!graph_ledger.failed || graph_ledger.events[event_kind] != 1 || graph_observer_last.value.host_present
                || graph_observer_last.receipt.action_ticket != r->action_ticket
                || (kind == 3 && !isnan(graph_observer_last.noise_after))) goto done;
    }
    graph_observer_fault_test = 0;
    if (!wgV2RuntimeEnsureHand(p, 0)) goto done;
    harnessObserverSeed(&current, 4); wgV2RuntimeObserveHit(&current);
    if (!graph_ledger.failed || graph_ledger.events[WG_V2_OBS_HIT_EFFECT] != 1
            || !graph_observer_last.value.host_present
            || graphSameBinding(&current, &graph_observer_last.value.delivery)) goto done;
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.PRODUCTION.HARNESS: component=observer_negative_inputs post_consumed_noise_hit_stats_rejected=1 duplicate_noise_consumption_counted=1 retired_missing_host_rejected=1 stale_new_binding_rejected=1 aborted_shot_reservation_rejected=1 independent_noise_survives_retry=1 nonfinite_live_payload_rejected=1 synthetic_observer_inputs_only=1 result=PASS world_hit=unproven");
    ok = 1;
done:
    graph_observer_fault_test = 0;
    memcpy(&graph_ledger, saved_ledger, sizeof(graph_ledger)); free(saved_ledger);
    wgV2RuntimeScopeClose(scope);
    wgV2RuntimeRetireHand(p, 0, "observer negative closeout");
    return ok && wgV2RuntimeEnsureHand(p, 0) && harnessGraphRecover(p, 0);
}

/* Actual linked adapter, ordinary native ticks. Receipt acknowledgments are
 * component permission proof; the separate world ledger proves real effects. */
static s32 harnessConsumedLifecycle(struct player *p)
{
    struct gset storage = {0}, other_storage = {0};
    uint64_t scope = 0, other_scope = 0;
    wg_v2_delivery a = {0}, noise_a = {0}, b = {0}, noise_b = {0};
    wg_v2_runtime_snapshot initial = {0}, consumed = {0}, before_b = {0}, after = {0};
    int ok = 0; graph_component_observe = 1;
    wgV2RuntimeRetireHand(p, 0, "lifecycle component setup");
    wgV2RuntimeRetireHand(p, 1, "lifecycle component setup");
    for (int hand = 0; hand < 2; ++hand) {
        p->hands[hand].gset.weaponfunc = 0; p->hands[hand].loadedammo[0] = p->hands[hand].loadedammo[1] = 32;
        p->hands[hand].noiseradius = 0;
        if (!wgV2RuntimeEnsureHand(p, hand) || !harnessGraphRecover(p, hand)) goto done;
    }
    for (int order = 0; order < 3; ++order) {
        if (!wgV2RuntimeSnapshot(p, 0, &initial) || !harnessGraphPress(p, 0)
                || !wgV2RuntimeShotBegin(p, 0, &storage, &scope, &a)) goto done;
        uint64_t old_scope = scope;
        if (order == 2) {
            /* A genuine noise admission survives a pre-effect shot retry. */
            if (!wgV2RuntimeNoiseBegin(p, 0, &noise_a)) goto done;
            wg_v2_delivery aborted = a;
            wgV2RuntimeShotAbort(&aborted); wgV2RuntimeScopeClose(scope); scope = 0;
            if (!wgV2RuntimeShotBegin(p, 0, &storage, &scope, &a)
                    || a.reservation_generation == aborted.reservation_generation
                    || !wgV2RuntimeSnapshot(p, 0, &before_b)) goto done;
            wgV2RuntimeNoiseComplete(&aborted); wgV2RuntimeShotComplete(&aborted); wgV2RuntimeShotAbort(&aborted);
            if (wgV2RuntimeShotEffectsBegin(&aborted)
                    || !wgV2RuntimeSnapshot(p, 0, &after) || memcmp(&after, &before_b, sizeof(after))
                    || !wgV2RuntimeShotEffectsBegin(&a)) goto done;
            /* Still-live binding, terminal shot interruption with outstanding
             * noise. Completion clears delivery once; ordinary ticks recover. */
            g_Vars.currentplayer = NULL; wgV2RuntimeShotAbort(&a); g_Vars.currentplayer = p;
            struct gset retry = {0}; uint64_t retry_scope = 0; wg_v2_delivery retry_receipt = {0};
            if (!wgV2RuntimeDeliveryPending(p, 0) || wgV2RuntimeDeliveryValid(&a)
                    || wgV2RuntimeShotBegin(p, 0, &retry, &retry_scope, &retry_receipt)) {
                wgV2RuntimeScopeClose(retry_scope); goto done;
            }
            wgV2RuntimeNoiseComplete(&noise_a); wgV2RuntimeNoiseComplete(&noise_a);
        } else {
            if (!wgV2RuntimeShotEffectsBegin(&a)) goto done;
            if (order == 1) wgV2RuntimeShotComplete(&a);
            memset(&graph_component_noise[0], 0, sizeof(graph_component_noise[0]));
            bgunDecreaseNoiseRadius(); noise_a = graph_component_noise[0];
            if (!noise_a.noise_generation || !graphSameBinding(&a, &noise_a)
                    || noise_a.action_ticket != a.action_ticket) goto done;
            if (order == 0) wgV2RuntimeShotComplete(&a);
        }
        old_scope = scope;
        wgV2RuntimeScopeClose(scope); scope = 0;
        if (!wgV2RuntimeSnapshot(p, 0, &consumed) || consumed.pending || consumed.firing
                || consumed.consumptions != initial.consumptions + 1 || !harnessGraphRecover(p, 0)
                || !harnessGraphPress(p, 0) || !wgV2RuntimeShotBegin(p, 0, &storage, &scope, &b)
                || !graphSameBinding(&a, &b) || b.action_ticket == a.action_ticket || scope == old_scope
                || !wgV2RuntimeNoiseBegin(p, 0, &noise_b) || !wgV2RuntimeSnapshot(p, 0, &before_b)
                || !before_b.pending || !before_b.noise_reserved || before_b.noise
                || before_b.press_sequence <= consumed.press_sequence) goto done;
        const float damage = gsetGetDamage(&storage);
        const int shots = frGetData()->numshots, hits = graph_world.observed_hits;
        s32 regions[7]; memcpy(regions, g_Vars.currentplayerstats->shotcount, sizeof(regions));
        /* Reuse A's destination address with a NEW live generation for B.
         * Check each old callback separately, then repeat the whole replay. */
        for (int repeat = 0; repeat < 3; ++repeat) {
            wgV2RuntimeShotComplete(&a);
            if (!harnessGraphUnchanged(p, 0, &before_b, shots, regions, hits, &storage, damage)) goto done;
            wgV2RuntimeNoiseComplete(&noise_a);
            if (!harnessGraphUnchanged(p, 0, &before_b, shots, regions, hits, &storage, damage)) goto done;
            wgV2RuntimeShotAbort(&a);
            if (!harnessGraphUnchanged(p, 0, &before_b, shots, regions, hits, &storage, damage)) goto done;
            if (wgV2RuntimeShotEffectsBegin(&a)
                    || !harnessGraphUnchanged(p, 0, &before_b, shots, regions, hits, &storage, damage)) goto done;
            wgV2RuntimeScopeClose(old_scope);
            if (!harnessGraphUnchanged(p, 0, &before_b, shots, regions, hits, &storage, damage)) goto done;
        }
        if (!wgV2RuntimeShotEffectsBegin(&b)) goto done;
        wgV2RuntimeNoiseComplete(&noise_b); wgV2RuntimeShotComplete(&b);
        wgV2RuntimeNoiseComplete(&noise_b); wgV2RuntimeShotComplete(&b); wgV2RuntimeShotAbort(&b);
        if (!wgV2RuntimeSnapshot(p, 0, &after) || after.pending || after.firing
                || after.consumptions != consumed.consumptions + 1 || !harnessGraphRecover(p, 0)) goto done;
        wgV2RuntimeScopeClose(scope); scope = 0;
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.PRODUCTION.HARNESS: component=post_consumed_same_binding order=%d new_ticket=1 reused_scope_storage=1 old_callbacks_separate_repeated=1 outstanding_noise=1 consumption_once=1 recovery_native_ticks=1 result=PASS world_hit=unproven", order);
    }
    if (!harnessObserverFaults(p, &b, &noise_b)) goto done;
    /* Leave the other hand genuinely pending; retirement must not cancel it. */
    if (!harnessGraphPress(p, 1) || !wgV2RuntimeShotBegin(p, 1, &other_storage, &other_scope, &b)
            || !harnessGraphPress(p, 0) || !wgV2RuntimeShotBegin(p, 0, &storage, &scope, &a)) goto done;
    bgunDecreaseNoiseRadius();
    if (!wgV2RuntimeSnapshot(p, 1, &before_b) || !before_b.pending || before_b.shot || !before_b.noise
            || !wgV2RuntimeSnapshot(p, 0, &initial) || !initial.noise || !initial.noise_increments) goto done;
    wgV2RuntimeRetireHand(p, 0, "component pending-other-hand retirement");
    if (!wgV2RuntimeSnapshot(p, 1, &after) || memcmp(&before_b, &after, sizeof(after))
            || p->hands[0].loadedammo[0] != initial.ammo[0] || p->hands[0].loadedammo[1] != initial.ammo[1]
            || p->hands[0].noiseradius != initial.noise_radius || wgV2RuntimeDeliveryValid(&a)) goto done;
    wgV2RuntimeShotComplete(&a); wgV2RuntimeNoiseComplete(&graph_component_noise[0]); wgV2RuntimeShotAbort(&a);
    if (wgV2RuntimeShotEffectsBegin(&a) || !wgV2RuntimeSnapshot(p, 1, &after)
            || memcmp(&before_b, &after, sizeof(after)) || !wgV2RuntimeDeliveryValid(&b)) goto done;
    wgV2RuntimeScopeClose(scope); scope = 0;
    if (gsetGetDamage(&other_storage) <= 0 || !wgV2RuntimeShotEffectsBegin(&b)) goto done;
    wgV2RuntimeShotComplete(&b);
    if (wgV2RuntimeDeliveryPending(p, 1)) goto done;
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.PRODUCTION.HARNESS: component=retire_with_other_pending other_binding_source_ticket_halves_native_effects_unchanged=1 committed_ammo_noise_preserved=1 result=PASS world_hit=unproven");
    ok = 1;
done:
    wgV2RuntimeScopeClose(scope); wgV2RuntimeScopeClose(other_scope);
    wgV2RuntimeRetireHand(p, 0, "lifecycle component closeout"); wgV2RuntimeRetireHand(p, 1, "lifecycle component closeout");
    graph_component_observe = 0;
    return ok;
}

static s32 harnessExecutableGraph(const char *owner, const char *archive)
{
    char error[512] = {0}; const char *step = "registration";
    harness_deps_t deps = {0}; s32 slot = -1, mp = -1, loaded = 0, ok = 0;
    void *weapon_slots = assetCatalogSnapshotCustomWeaponSlots();
    void *model_slots = assetCatalogSnapshotCustomModelSlots();
    struct player *temporary = calloc(1, sizeof(*temporary));
    struct g_vars saved = g_Vars;
    struct gset copy = {0}; uint64_t copy_token = 0;
    struct gset dispatch = {0}; uint64_t dispatch_token = 0;
    wg_v2_delivery replaced_receipt = {0}, current_receipt = {0}, noise_receipt = {0};
    void *original = NULL, *edited = NULL, *bad = NULL, *closed = NULL, *spread_source = NULL;
    u32 size = 0, edit_size = 0, bad_size = 0, closed_size = 0, spread_size = 0;
    if (!temporary || !weapon_slots || !model_slots) goto done;
    original = fsFileLoad(archive, &size);
    edited = fsFileLoad("./asset-mesh-ingress/executable_edit.pdweapon", &edit_size);
    bad = fsFileLoad("./asset-mesh-ingress/executable_bad.pdweapon", &bad_size);
    closed = fsFileLoad("./asset-mesh-ingress/executable_closed.pdweapon", &closed_size);
    spread_source = fsFileLoad("./asset-mesh-ingress/executable_spread.pdweapon", &spread_size);
    if (!original || !edited || !bad || !closed || !spread_source) goto done;
    if (!assetCatalogResolveWeaponPrivateSlots(owner, -1, 0, &slot, &mp)) goto done;
    asset_entry_t *entry = harnessRegisterOwner(owner, archive);
    if (!entry) goto done;
    entry->runtime_index = slot; entry->mp_index = mp;
    if (assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0, error, sizeof(error)) < 0) goto done;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    if (deps.invalid || !catalogLoadTypedAsset(ASSET_WEAPON, owner)) goto done;
    loaded = 1;
    if (!wgV2RuntimeIsWeapon(slot) || !wgV2RuntimeWeapon(slot) || !wgV2RuntimeModel(slot)
            || weaponGraphRuntimeGetHeldFunction(slot, 0)) goto done;
    if (wgV2RuntimeCanEnterUse(1, 0, error, sizeof(error)) || wgV2RuntimeCanEnterUse(0, 1, error, sizeof(error))) goto done;
    step = "idle_source";
    g_Vars.currentplayer = temporary; g_Vars.currentplayernum = 0;
    g_Vars.currentplayerstats = &g_Vars.playerstats[0]; g_Vars.currentplayerstats->mpindex = 0;
    g_Vars.lvupdate60 = 1; g_Vars.lvupdate240 = 4; g_Vars.lvupdate60freal = 1;
    temporary->gunctrl.weaponnum = slot; temporary->gunctrl.switchtoweaponnum = -1;
    temporary->gunctrl.ammotypes[0] = AMMOTYPE_PISTOL; temporary->gunctrl.ammotypes[1] = AMMOTYPE_SMG;
    temporary->ammoheldarr[AMMOTYPE_PISTOL] = temporary->ammoheldarr[AMMOTYPE_SMG] = 20;
    bgunInitHandAnims();
    for (s32 hand = 0; hand < 2; ++hand) {
        temporary->hands[hand].gset.weaponnum = slot; temporary->hands[hand].gset.weaponfunc = 0;
        temporary->hands[hand].inuse = 1; temporary->hands[hand].loadedammo[0] = temporary->hands[hand].loadedammo[1] = 4;
        temporary->hands[hand].clipsizes[0] = temporary->hands[hand].clipsizes[1] = 7;
        if (!wgV2RuntimeEnsureHand(temporary, hand) || gsetGetAmmoDefinition(&temporary->hands[hand].gset)->clipsize != 7
                || gsetGetWeaponFunction(&temporary->hands[hand].gset)->type != INVENTORYFUNCTYPE_NONE) goto done;
    }
    step = "native_fire";
    for (s32 hand = 0; hand < 2; ++hand) {
        struct handweaponinfo info; bgunGetWeaponInfo(&info, hand);
        bgunSetTriggerOn(hand, true); bgunTickInc(&info, hand, 1); bgunTickInc(&info, hand, 1);
        if (!temporary->hands[hand].firing || temporary->hands[hand].loadedammo[0] != 3
                || temporary->hands[hand].loadedammo[1] != 4 || !wgV2RuntimeDeliveryPending(temporary, hand)) goto done;
        gsetPopulateFromCurrentPlayer(hand, &copy); copy_token = wgV2RuntimeScopeOpen(temporary, hand, &copy);
        if (!copy_token || gsetGetDamage(&copy) != 3) goto done;
        wgV2RuntimeScopeClose(copy_token); copy_token = 0;
    }
    step = "skipped_delivery";
    bgunDecreaseNoiseRadius(); f32 radius = temporary->hands[0].noiseradius;
    for (s32 i = 0; i < 3; ++i) {
        struct handweaponinfo info; bgunGetWeaponInfo(&info, 0); bgunTickInc(&info, 0, 1); bgunDecreaseNoiseRadius();
    }
    if (!temporary->hands[0].firing || temporary->hands[0].loadedammo[0] != 3 || temporary->hands[0].noiseradius > radius) goto done;
    step = "delivery_receipts";
    for (s32 hand = 0; hand < 2; ++hand) {
        struct gset dispatch = {0}, duplicate = {0};
        uint64_t scope = 0, duplicate_scope = 123;
        wg_v2_delivery receipt = {0}, rejected = {0};
        if (!wgV2RuntimeShotBegin(temporary, hand, &dispatch, &scope, &receipt)
                || !scope || !wgV2RuntimeDeliveryValid(&receipt) || gsetGetDamage(&dispatch) != 3) {
            wgV2RuntimeScopeClose(scope); goto done;
        }
        if (wgV2RuntimeShotBegin(temporary, hand, &duplicate, &duplicate_scope, &rejected)
                || duplicate_scope || rejected.owner) {
            wgV2RuntimeScopeClose(duplicate_scope); wgV2RuntimeScopeClose(scope); goto done;
        }
        wg_v2_delivery aborted = receipt;
        wgV2RuntimeShotAbort(&aborted);
        if (wgV2RuntimeDeliveryValid(&aborted)
                || !wgV2RuntimeShotBegin(temporary, hand, &duplicate, &duplicate_scope, &receipt)
                || receipt.reservation_generation == aborted.reservation_generation) {
            wgV2RuntimeScopeClose(duplicate_scope); wgV2RuntimeScopeClose(scope); goto done;
        }
        wgV2RuntimeShotComplete(&aborted); wgV2RuntimeShotAbort(&aborted);
        if (wgV2RuntimeShotEffectsBegin(&aborted) || !wgV2RuntimeDeliveryValid(&receipt)) {
            wgV2RuntimeScopeClose(duplicate_scope); wgV2RuntimeScopeClose(scope); goto done;
        }
        wgV2RuntimeScopeClose(scope); scope = duplicate_scope;
        if (!wgV2RuntimeShotEffectsBegin(&receipt) || wgV2RuntimeShotEffectsBegin(&receipt)) {
            wgV2RuntimeScopeClose(scope); goto done;
        }
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.PRODUCTION.HARNESS: component=abort_retry_reservation hand=%d old=%llu new=%llu result=PASS world_hit=unproven",
            hand, (unsigned long long)aborted.reservation_generation, (unsigned long long)receipt.reservation_generation);
        /* This installed adapter test acknowledges delivery as a component
         * operation. Only the separate ordinary-world observer proves hits. */
        for (s32 identity = 0; identity < 6; ++identity) {
            wg_v2_delivery stale = receipt;
            if (identity == 0) ++stale.owner_generation;
            if (identity == 1) ++stale.hand_generation;
            if (identity == 2) ++stale.source_generation;
            if (identity == 3) ++stale.action_ticket;
            if (identity == 4) stale.hand = 1 - hand;
            if (identity == 5) ++stale.reservation_generation;
            wgV2RuntimeShotComplete(&stale); wgV2RuntimeNoiseComplete(&stale);
            if (wgV2RuntimeDeliveryValid(&stale) || !wgV2RuntimeDeliveryValid(&receipt)) {
                wgV2RuntimeScopeClose(scope); goto done;
            }
        }
        wgV2RuntimeScopeClose(scope);
        wgV2RuntimeShotComplete(&receipt); wgV2RuntimeShotComplete(&receipt);
        if (wgV2RuntimeDeliveryPending(temporary, hand) || wgV2RuntimeDeliveryValid(&receipt)) goto done;
    }
    for (s32 hand = 0; hand < 2; ++hand) {
        struct handweaponinfo info; bgunGetWeaponInfo(&info, hand); bgunSetTriggerOn(hand, false); bgunTickInc(&info, hand, 1);
        if (temporary->hands[hand].state != HANDSTATE_IDLE || temporary->hands[hand].loadedammo[0] != 3) goto done;
    }
    step = "state_ammo_route";
    bgunSetTriggerOn(0, true);
    struct handweaponinfo info; bgunGetWeaponInfo(&info, 0); bgunTickInc(&info, 0, 1); bgunTickInc(&info, 0, 1);
    if (!temporary->hands[0].firing || temporary->hands[0].loadedammo[0] != 3 || temporary->hands[0].loadedammo[1] != 3) goto done;
    gsetPopulateFromCurrentPlayer(0, &copy); copy_token = wgV2RuntimeScopeOpen(temporary, 0, &copy);
    if (!copy_token || gsetGetDamage(&copy) != 7) goto done;
    if (!wgV2RuntimeShotBegin(temporary, 0, &dispatch, &dispatch_token, &replaced_receipt)) goto done;
    step = "failed_replacement";
    const struct weapon *prior = wgV2RuntimeWeapon(slot);
    if (!harnessWriteBytes(archive, bad, bad_size) || wgV2RuntimeRegisterArchive(slot, owner, archive, error, sizeof(error))
            || wgV2RuntimeWeapon(slot) != prior || gsetGetDamage(&copy) != 7) goto done;
    step = "source_replacement";
    if (!harnessWriteBytes(archive, edited, edit_size) || !wgV2RuntimeRegisterArchive(slot, owner, archive, error, sizeof(error))
            || wgV2RuntimeWeapon(slot) == prior || temporary->hands[0].firing || gsetGetDamage(&copy) != 7) goto done;
    if (wgV2RuntimeDeliveryValid(&replaced_receipt) || gsetGetDamage(&dispatch) != 7) goto done;
    wgV2RuntimeScopeClose(dispatch_token); dispatch_token = 0;
    wgV2RuntimeScopeClose(copy_token); copy_token = 0;
    if (!wgV2RuntimeEnsureHand(temporary, 0)) goto done;
    bgunSetTriggerOn(0, false); bgunSetTriggerOn(0, true); bgunGetWeaponInfo(&info, 0);
    bgunTickInc(&info, 0, 1); bgunTickInc(&info, 0, 1);
    if (temporary->hands[0].loadedammo[0] != 2 || temporary->hands[0].loadedammo[1] != 3) goto done;
    gsetPopulateFromCurrentPlayer(0, &copy); copy_token = wgV2RuntimeScopeOpen(temporary, 0, &copy);
    if (!copy_token || gsetGetDamage(&copy) != 6) goto done;
    step = "stale_completion_and_noise_order";
    if (!wgV2RuntimeShotBegin(temporary, 0, &dispatch, &dispatch_token, &current_receipt)) goto done;
    wgV2RuntimeShotComplete(&replaced_receipt); wgV2RuntimeNoiseComplete(&replaced_receipt);
    if (!wgV2RuntimeDeliveryValid(&current_receipt)) goto done;
    if (!wgV2RuntimeShotEffectsBegin(&current_receipt)) goto done;
    g_Vars.currentplayer = NULL;
    wgV2RuntimeShotAbort(&current_receipt);
    g_Vars.currentplayer = temporary;
    if (!wgV2RuntimeDeliveryPending(temporary, 0) || wgV2RuntimeDeliveryValid(&current_receipt)) goto done;
    struct gset forbidden_retry = {0}; uint64_t forbidden_scope = 0; wg_v2_delivery forbidden_receipt = {0};
    if (wgV2RuntimeShotBegin(temporary, 0, &forbidden_retry, &forbidden_scope, &forbidden_receipt) || forbidden_scope || forbidden_receipt.owner) {
        wgV2RuntimeScopeClose(forbidden_scope); goto done;
    }
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.PRODUCTION.HARNESS: component=effects_abort_terminal current_player_changed=1 retry_denied=1 result=PASS world_hit=unproven");
    if (!wgV2RuntimeNoiseBegin(temporary, 0, &noise_receipt)) goto done;
    wg_v2_delivery duplicate_noise = {0};
    if (wgV2RuntimeNoiseBegin(temporary, 0, &duplicate_noise) || duplicate_noise.owner) goto done;
    wgV2RuntimeNoiseComplete(&current_receipt); // A shot receipt is not a noise admission.
    if (!wgV2RuntimeDeliveryPending(temporary, 0)) goto done;
    wgV2RuntimeNoiseComplete(&noise_receipt); wgV2RuntimeNoiseComplete(&noise_receipt);
    if (wgV2RuntimeDeliveryPending(temporary, 0)) goto done;
    wgV2RuntimeScopeClose(dispatch_token); dispatch_token = 0;
    step = "retirement";
    temporary->isdead = 1;
    if (wgV2RuntimeEnsureHand(temporary, 0)) goto done;
    if (temporary->hands[0].firing || gsetGetDamage(&copy) != 6) goto done;
    wgV2RuntimeScopeClose(copy_token); copy_token = 0;
    step = "receipt_retirement";
    temporary->isdead = 0;
    for (s32 cause = 0; cause < 4; ++cause) if (!harnessReceiptRetirement(temporary, cause)) goto done;
    step = "post_consumed_lifecycle";
    if (!harnessConsumedLifecycle(temporary)) goto done;
    step = "closed_gate";
    temporary->isdead = 0;
    if (!harnessWriteBytes(archive, closed, closed_size)
            || !wgV2RuntimeRegisterArchive(slot, owner, archive, error, sizeof(error))) goto done;
    for (s32 mode = 0; mode < 2; ++mode) {
        for (s32 hand = 0; hand < 2; ++hand) {
            temporary->hands[hand].gset.weaponfunc = mode;
            if (!wgV2RuntimeEnsureHand(temporary, hand)) goto done;
            const int ammo0 = temporary->hands[hand].loadedammo[0], ammo1 = temporary->hands[hand].loadedammo[1];
            const float before_noise = temporary->hands[hand].noiseradius;
            struct handweaponinfo closed_info; bgunGetWeaponInfo(&closed_info, hand);
            bgunSetTriggerOn(hand, false); bgunTickInc(&closed_info, hand, 1);
            bgunSetTriggerOn(hand, true); bgunTickInc(&closed_info, hand, 1); bgunTickInc(&closed_info, hand, 1);
            bgunDecreaseNoiseRadius();
            if (temporary->hands[hand].firing || wgV2RuntimeDeliveryPending(temporary, hand)
                    || temporary->hands[hand].loadedammo[0] != ammo0 || temporary->hands[hand].loadedammo[1] != ammo1
                    || temporary->hands[hand].noiseradius > before_noise) goto done;
        }
    }
    sysLogPrintf(LOG_NOTE, "GRAPH.V2.PRODUCTION.HARNESS: component=receipt_orders_stale_identity_closed_gate result=PASS world_hit=unproven");
    step = "captured_spread";
    if (!harnessWriteBytes(archive, spread_source, spread_size)
            || !wgV2RuntimeRegisterArchive(slot, owner, archive, error, sizeof(error))
            || !harnessSpreadCapture(temporary)) goto done;
    step = "retirement";
    ok = 1;
done:
    wgV2RuntimeScopeClose(dispatch_token);
    wgV2RuntimeScopeClose(copy_token);
    if (temporary) {
        wgV2RuntimeRetireHand(temporary, 0, "harness cleanup"); wgV2RuntimeRetireHand(temporary, 1, "harness cleanup");
    }
    wgV2RuntimeRetireWeapon(slot, owner);
    if (loaded) catalogReleaseTypedAsset(ASSET_WEAPON, owner);
    if (!harnessCleanup(owner, &deps)) ok = 0;
    /* This fixture runs before gameplay. Retire its explicit owner before
     * restoring the real native roots; no temporary player may remain live. */
    wgV2RuntimeRetirePlayers(); g_Vars = saved; free(temporary);
    catalogModelGenerationReleaseStagePins();
    if (original && !harnessWriteBytes(archive, original, size)) ok = 0;
    sysMemFree(original); sysMemFree(edited); sysMemFree(bad); sysMemFree(closed); sysMemFree(spread_source);
    if (weapon_slots && !assetCatalogRestoreCustomWeaponSlots(weapon_slots)) ok = 0;
    if (model_slots && !assetCatalogRestoreCustomModelSlots(model_slots)) ok = 0;
    assetCatalogDestroyCustomWeaponSlotSnapshot(weapon_slots); assetCatalogDestroyCustomModelSlotSnapshot(model_slots);
    catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING, "GRAPH.V2.PRODUCTION.HARNESS: step=%s result=%s error=%s", step, ok ? "PASS" : "FAIL", error);
    return ok;
}

s32 weaponNestedRuntimeHarnessRun(const char *plan_path)
{
    FILE *f;
    char line[4096];
    s32 cases = 0;
    s32 passed = 0;
    if (!plan_path || !plan_path[0]) return 0;
    f = fopen(plan_path, "rb");
    if (!f) f = fsFileOpenRead(plan_path);
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\r' || line[len - 1] == '\n'))
            line[--len] = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *mode = line;
        char *owner = strchr(mode, '|');
        if (!owner) continue;
        *owner++ = '\0';
        char *archive = strchr(owner, '|');
        if (!archive) continue;
        *archive++ = '\0';
        char *arg1 = strchr(archive, '|');
        if (!arg1) continue;
        *arg1++ = '\0';
        char *arg2 = strchr(arg1, '|');
        if (arg2) *arg2++ = '\0';
        char *arg3 = arg2 ? strchr(arg2, '|') : NULL;
        if (arg3) *arg3++ = '\0';
        cases++;
        if (strcmp(mode, "accept") == 0 && arg2
                && harnessAccept(owner, archive, arg1, arg2)) passed++;
        else if (strcmp(mode, "reject") == 0
                && harnessReject(owner, archive, arg1)) passed++;
        else if (strcmp(mode, "capacity") == 0 && arg2 && arg3
                && harnessCapacityAccept(owner, archive, arg1, arg2, arg3)) passed++;
        else if (strcmp(mode, "capacity_reject") == 0 && arg2 && arg3
                && harnessCapacityReject(owner, archive, arg1, arg2, arg3)) passed++;
        else if (strcmp(mode, "mesh_accept") == 0 && arg2 && arg3
                && harnessMeshAccept(owner, archive, arg1, arg2, arg3, 0)) passed++;
        else if (strcmp(mode, "mesh_load_reject") == 0 && arg2 && arg3
                && harnessMeshAccept(owner, archive, arg1, arg2, arg3, 1)) passed++;
        else if (strcmp(mode, "mesh_slots_reject") == 0
                && harnessMeshSlotRollback(owner, archive, arg1)) passed++;
        else if (strcmp(mode, "mesh_base") == 0 && arg2
                && harnessBaseMeshPreserve(owner, archive, arg1, arg2)) passed++;
        else if (strcmp(mode, "model_inputs") == 0
                && harnessModelInputs(owner, archive)) passed++;
        else if (strcmp(mode, "model_generation") == 0
                && harnessModelGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "texture_generation") == 0
                && harnessTextureGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "texture_runtime") == 0
                && harnessTextureRuntime(owner, archive)) passed++;
        else if (strcmp(mode, "command_generation") == 0
                && harnessCommandGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "audio_generation") == 0
                && harnessAudioGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "animation_generation") == 0
                && harnessAnimationGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "command_import") == 0
                && harnessCommandImport(owner, archive, arg1)) passed++;
        else if (strcmp(mode, "executable_graph") == 0
                && harnessExecutableGraph(owner, archive)) passed++;
        else if (strcmp(mode, "command_source") == 0
                && harnessCommandSources(archive)) passed++;
    }
    fclose(f);
    sysLogPrintf(passed == cases && cases > 0 ? LOG_NOTE : LOG_WARNING,
        "PDWEAPON.NESTED.HARNESS: passed=%d cases=%d result=%s",
        passed, cases, passed == cases && cases > 0 ? "PASS" : "FAIL");
    return passed == cases && cases > 0;
}
