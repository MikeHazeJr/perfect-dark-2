/*
 * port/src/animdata_authored.c -- Catalog Universality BYOR Completion
 * (2026-05-03).
 *
 * AUTHORED EXTRACTOR SOURCE-OF-TRUTH for weapon-animation iteration.
 *
 * The actual invanim_*[] opcode arrays live alongside the weapons in
 * port/src/weapondata_authored.c (because they are referenced by
 * struct weapon function pointers). This file exposes a clean
 * iteration table so the .pdanim emitter (romextract_pdanim.c) can
 * walk all 110 weapon animations without poking at weapondata internals.
 *
 * Each entry maps a stable name (the historical invanim_* C symbol)
 * to the opcode array. The emitter mints catalog ID "base:<name>" and
 * writes one .pdanim file per entry to data/<romid>/animations/.
 *
 * Engine-API constraint: nothing in src/ or port/ outside the emitter
 * (port/src/romextract_pdanim.c) may include animdata_authored.h.
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>
#include "data.h"
#include "types.h"
#include "animdata_authored.h"
#include "weapondata_authored.h"

/* Forward extern declarations for every invanim_*[] array defined in
 * port/src/weapondata_authored.c. Auto-generated from the names list. */
extern struct guncmd invanim_punch_type3[];
extern struct guncmd invanim_punch_type1[];
extern struct guncmd invanim_punch_type2[];
extern struct guncmd invanim_punch_type4[];
extern struct guncmd invanim_punch[];
extern struct guncmd invanim_falcon2_reload_singlewield[];
extern struct guncmd invanim_falcon2scope_reload_singlewield[];
extern struct guncmd invanim_falcon2_reload_dualwield[];
extern struct guncmd invanim_falcon2_reload[];
extern struct guncmd invanim_falcon2scope_reload[];
extern struct guncmd invanim_falcon2_pistolwhip[];
extern struct guncmd invanim_falcon2_equip[];
extern struct guncmd invanim_falcon2_unequip[];
extern struct guncmd invanim_falcon2_shoot[];
extern struct guncmd invanim_magsec_reload_singlewield[];
extern struct guncmd invanim_magsec_reload_dualwield[];
extern struct guncmd invanim_magsec_reload[];
extern struct guncmd invanim_magsec_shoot[];
extern struct guncmd invanim_dy357_shoot[];
extern struct guncmd invanim_dy357_reload_singlewield[];
extern struct guncmd invanim_dy357_reload_dualwield[];
extern struct guncmd invanim_dy357_reload[];
extern struct guncmd invanim_dy357_pistolwhip[];
extern struct guncmd invanim_phoenix_reload_singlewield[];
extern struct guncmd invanim_phoenix_reload_dualwield[];
extern struct guncmd invanim_phoenix_equiporreload[];
extern struct guncmd invanim_phoenix_shoot[];
extern struct guncmd invanim_mauler_shoot[];
extern struct guncmd invanim_mauler_reload_singlewield[];
extern struct guncmd invanim_mauler_reload_dualwield[];
extern struct guncmd invanim_mauler_reload[];
extern struct guncmd invanim_unused_8007c0bc[];
extern struct guncmd invanim_cmp150_reload_singlewield[];
extern struct guncmd invanim_cmp150_reload_dualwield[];
extern struct guncmd invanim_cmp150_reload[];
extern struct guncmd invanim_cmp150_shoot[];
extern struct guncmd invanim_cyclone_reload_singlewield[];
extern struct guncmd invanim_cyclone_reload_dualwield[];
extern struct guncmd invanim_cyclone_equiporreload[];
extern struct guncmd invanim_cyclone_shoot[];
extern struct guncmd invanim_rcp120_reload[];
extern struct guncmd invanim_rcp120_shoot[];
extern struct guncmd invanim_callisto_reload[];
extern struct guncmd invanim_callisto_shoot[];
extern struct guncmd invanim_dragon_shoot[];
extern struct guncmd invanim_dragon_reload[];
extern struct guncmd invanim_superdragon_reload[];
extern struct guncmd invanim_superdragon_grenadereload[];
extern struct guncmd invanim_superdragon_shoot[];
extern struct guncmd invanim_superdragon_shootgrenade[];
extern struct guncmd invanim_superdragon_pritosec[];
extern struct guncmd invanim_superdragon_sectopri[];
extern struct guncmd invanim_ar34_reload[];
extern struct guncmd invanim_k7avenger_reload[];
extern struct guncmd invanim_k7avenger_equip[];
extern struct guncmd invanim_k7avenger_unequip[];
extern struct guncmd invanim_unused_8007ce6c[];
extern struct guncmd invanim_laptopgun_reload[];
extern struct guncmd invanim_laptopgun_shoot[];
extern struct guncmd invanim_laptopgun_equip[];
extern struct guncmd invanim_laptopgun_unequip[];
extern struct guncmd invanim_shotgun_reload[];
extern struct guncmd invanim_shotgun_singleshot[];
extern struct guncmd invanim_shotgun_doubleshot[];
extern struct guncmd invanim_reaper_shoot[];
extern struct guncmd invanim_reaper_reload[];
extern struct guncmd invanim_reaper_equip[];
extern struct guncmd invanim_reaper_unequip[];
extern struct guncmd invanim_rocketlauncher_reload[];
extern struct guncmd invanim_rockerlauncher_shoot[];
extern struct guncmd invanim_slayer_shoot[];
extern struct guncmd invanim_slayer_reload[];
extern struct guncmd invanim_devastator_shoot[];
extern struct guncmd invanim_devastator_reload[];
extern struct guncmd invanim_mine_equip[];
extern struct guncmd invanim_mine_unequip[];
extern struct guncmd invanim_mine_throw[];
extern struct guncmd invanim_remotemine_equip[];
extern struct guncmd invanim_remotemine_unequip[];
extern struct guncmd invanim_remotemine_throw[];
extern struct guncmd invanim_ecmmine_equip[];
extern struct guncmd invanim_ecmmine_unequip[];
extern struct guncmd invanim_ecmmine_throw[];
extern struct guncmd invanim_grenade_throw[];
extern struct guncmd invanim_grenade_equip[];
extern struct guncmd invanim_farsight_reload[];
extern struct guncmd invanim_farsight_shoot[];
extern struct guncmd invanim_crossbow_reload[];
extern struct guncmd invanim_crossbow_shoot[];
extern struct guncmd invanim_crossbow_unequip[];
extern struct guncmd invanim_crosbow_equip[];
extern struct guncmd invanim_tranquilizer_lethalinject[];
extern struct guncmd invanim_tranquilizer_shoot[];
extern struct guncmd invanim_tranquilizer_reload[];
extern struct guncmd invanim_sniperrifle_equip[];
extern struct guncmd invanim_sniperrifle_reload[];
extern struct guncmd invanim_laser_equip[];
extern struct guncmd invanim_laser_unequip[];
extern struct guncmd invanim_combatknife_equip[];
extern struct guncmd invanim_combatknife_slash2[];
extern struct guncmd invanim_combatknife_slash[];
extern struct guncmd invanim_combatknife_pritosec[];
extern struct guncmd invanim_combatknife_sectopri[];
extern struct guncmd invanim_combatknife_throw[];
extern struct guncmd invanim_unused_8007f05c[];
extern struct guncmd invanim_combatknife_reload[];
extern struct guncmd invanim_datauplink_equip[];
extern struct guncmd invanim_datauplink_unequip[];
extern struct guncmd invanim_unused_8007f794[];
extern struct guncmd invanim_tester_shoot[];

const animdata_record_t g_AnimData[] = {
	{ "invanim_punch_type3", invanim_punch_type3 },
	{ "invanim_punch_type1", invanim_punch_type1 },
	{ "invanim_punch_type2", invanim_punch_type2 },
	{ "invanim_punch_type4", invanim_punch_type4 },
	{ "invanim_punch", invanim_punch },
	{ "invanim_falcon2_reload_singlewield", invanim_falcon2_reload_singlewield },
	{ "invanim_falcon2scope_reload_singlewield", invanim_falcon2scope_reload_singlewield },
	{ "invanim_falcon2_reload_dualwield", invanim_falcon2_reload_dualwield },
	{ "invanim_falcon2_reload", invanim_falcon2_reload },
	{ "invanim_falcon2scope_reload", invanim_falcon2scope_reload },
	{ "invanim_falcon2_pistolwhip", invanim_falcon2_pistolwhip },
	{ "invanim_falcon2_equip", invanim_falcon2_equip },
	{ "invanim_falcon2_unequip", invanim_falcon2_unequip },
	{ "invanim_falcon2_shoot", invanim_falcon2_shoot },
	{ "invanim_magsec_reload_singlewield", invanim_magsec_reload_singlewield },
	{ "invanim_magsec_reload_dualwield", invanim_magsec_reload_dualwield },
	{ "invanim_magsec_reload", invanim_magsec_reload },
	{ "invanim_magsec_shoot", invanim_magsec_shoot },
	{ "invanim_dy357_shoot", invanim_dy357_shoot },
	{ "invanim_dy357_reload_singlewield", invanim_dy357_reload_singlewield },
	{ "invanim_dy357_reload_dualwield", invanim_dy357_reload_dualwield },
	{ "invanim_dy357_reload", invanim_dy357_reload },
	{ "invanim_dy357_pistolwhip", invanim_dy357_pistolwhip },
	{ "invanim_phoenix_reload_singlewield", invanim_phoenix_reload_singlewield },
	{ "invanim_phoenix_reload_dualwield", invanim_phoenix_reload_dualwield },
	{ "invanim_phoenix_equiporreload", invanim_phoenix_equiporreload },
	{ "invanim_phoenix_shoot", invanim_phoenix_shoot },
	{ "invanim_mauler_shoot", invanim_mauler_shoot },
	{ "invanim_mauler_reload_singlewield", invanim_mauler_reload_singlewield },
	{ "invanim_mauler_reload_dualwield", invanim_mauler_reload_dualwield },
	{ "invanim_mauler_reload", invanim_mauler_reload },
	{ "invanim_unused_8007c0bc", invanim_unused_8007c0bc },
	{ "invanim_cmp150_reload_singlewield", invanim_cmp150_reload_singlewield },
	{ "invanim_cmp150_reload_dualwield", invanim_cmp150_reload_dualwield },
	{ "invanim_cmp150_reload", invanim_cmp150_reload },
	{ "invanim_cmp150_shoot", invanim_cmp150_shoot },
	{ "invanim_cyclone_reload_singlewield", invanim_cyclone_reload_singlewield },
	{ "invanim_cyclone_reload_dualwield", invanim_cyclone_reload_dualwield },
	{ "invanim_cyclone_equiporreload", invanim_cyclone_equiporreload },
	{ "invanim_cyclone_shoot", invanim_cyclone_shoot },
	{ "invanim_rcp120_reload", invanim_rcp120_reload },
	{ "invanim_rcp120_shoot", invanim_rcp120_shoot },
	{ "invanim_callisto_reload", invanim_callisto_reload },
	{ "invanim_callisto_shoot", invanim_callisto_shoot },
	{ "invanim_dragon_shoot", invanim_dragon_shoot },
	{ "invanim_dragon_reload", invanim_dragon_reload },
	{ "invanim_superdragon_reload", invanim_superdragon_reload },
	{ "invanim_superdragon_grenadereload", invanim_superdragon_grenadereload },
	{ "invanim_superdragon_shoot", invanim_superdragon_shoot },
	{ "invanim_superdragon_shootgrenade", invanim_superdragon_shootgrenade },
	{ "invanim_superdragon_pritosec", invanim_superdragon_pritosec },
	{ "invanim_superdragon_sectopri", invanim_superdragon_sectopri },
	{ "invanim_ar34_reload", invanim_ar34_reload },
	{ "invanim_k7avenger_reload", invanim_k7avenger_reload },
	{ "invanim_k7avenger_equip", invanim_k7avenger_equip },
	{ "invanim_k7avenger_unequip", invanim_k7avenger_unequip },
	{ "invanim_unused_8007ce6c", invanim_unused_8007ce6c },
	{ "invanim_laptopgun_reload", invanim_laptopgun_reload },
	{ "invanim_laptopgun_shoot", invanim_laptopgun_shoot },
	{ "invanim_laptopgun_equip", invanim_laptopgun_equip },
	{ "invanim_laptopgun_unequip", invanim_laptopgun_unequip },
	{ "invanim_shotgun_reload", invanim_shotgun_reload },
	{ "invanim_shotgun_singleshot", invanim_shotgun_singleshot },
	{ "invanim_shotgun_doubleshot", invanim_shotgun_doubleshot },
	{ "invanim_reaper_shoot", invanim_reaper_shoot },
	{ "invanim_reaper_reload", invanim_reaper_reload },
	{ "invanim_reaper_equip", invanim_reaper_equip },
	{ "invanim_reaper_unequip", invanim_reaper_unequip },
	{ "invanim_rocketlauncher_reload", invanim_rocketlauncher_reload },
	{ "invanim_rockerlauncher_shoot", invanim_rockerlauncher_shoot },
	{ "invanim_slayer_shoot", invanim_slayer_shoot },
	{ "invanim_slayer_reload", invanim_slayer_reload },
	{ "invanim_devastator_shoot", invanim_devastator_shoot },
	{ "invanim_devastator_reload", invanim_devastator_reload },
	{ "invanim_mine_equip", invanim_mine_equip },
	{ "invanim_mine_unequip", invanim_mine_unequip },
	{ "invanim_mine_throw", invanim_mine_throw },
	{ "invanim_remotemine_equip", invanim_remotemine_equip },
	{ "invanim_remotemine_unequip", invanim_remotemine_unequip },
	{ "invanim_remotemine_throw", invanim_remotemine_throw },
	{ "invanim_ecmmine_equip", invanim_ecmmine_equip },
	{ "invanim_ecmmine_unequip", invanim_ecmmine_unequip },
	{ "invanim_ecmmine_throw", invanim_ecmmine_throw },
	{ "invanim_grenade_throw", invanim_grenade_throw },
	{ "invanim_grenade_equip", invanim_grenade_equip },
	{ "invanim_farsight_reload", invanim_farsight_reload },
	{ "invanim_farsight_shoot", invanim_farsight_shoot },
	{ "invanim_crossbow_reload", invanim_crossbow_reload },
	{ "invanim_crossbow_shoot", invanim_crossbow_shoot },
	{ "invanim_crossbow_unequip", invanim_crossbow_unequip },
	{ "invanim_crosbow_equip", invanim_crosbow_equip },
	{ "invanim_tranquilizer_lethalinject", invanim_tranquilizer_lethalinject },
	{ "invanim_tranquilizer_shoot", invanim_tranquilizer_shoot },
	{ "invanim_tranquilizer_reload", invanim_tranquilizer_reload },
	{ "invanim_sniperrifle_equip", invanim_sniperrifle_equip },
	{ "invanim_sniperrifle_reload", invanim_sniperrifle_reload },
	{ "invanim_laser_equip", invanim_laser_equip },
	{ "invanim_laser_unequip", invanim_laser_unequip },
	{ "invanim_combatknife_equip", invanim_combatknife_equip },
	{ "invanim_combatknife_slash2", invanim_combatknife_slash2 },
	{ "invanim_combatknife_slash", invanim_combatknife_slash },
	{ "invanim_combatknife_pritosec", invanim_combatknife_pritosec },
	{ "invanim_combatknife_sectopri", invanim_combatknife_sectopri },
	{ "invanim_combatknife_throw", invanim_combatknife_throw },
	{ "invanim_unused_8007f05c", invanim_unused_8007f05c },
	{ "invanim_combatknife_reload", invanim_combatknife_reload },
	{ "invanim_datauplink_equip", invanim_datauplink_equip },
	{ "invanim_datauplink_unequip", invanim_datauplink_unequip },
	{ "invanim_unused_8007f794", invanim_unused_8007f794 },
	{ "invanim_tester_shoot", invanim_tester_shoot },
};

const s32 g_AnimDataCount = (s32)(sizeof(g_AnimData) / sizeof(g_AnimData[0]));
