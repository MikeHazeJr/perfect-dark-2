#ifndef _IN_WEAPON_RULES_H
#define _IN_WEAPON_RULES_H

#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "constants.h"
#include "mission_graph_runner.h"

/*
 * The modules and gates a held weapon program can run, the events that apply
 * them, and the original game's rules that the base programs wire
 * (context/designs/modding/held-weapon-events.md, slice 2). No game state
 * here, so the game, the extractor and pd-tests all read one table:
 *
 * - the extractor writes each base function's program from it;
 * - the game's seams compare a program's choice with it (the parity probe)
 *   and use it for a function with no program (a v1 graph);
 * - registration refuses a module wired to an event that does not apply it,
 *   because nothing would happen.
 *
 * As for projectiles, a module does not run code where the program fires it.
 * It is a choice: the seam that fired the event then runs the original code
 * for the modules chosen, where that code has always run.
 *
 * The table is header-only (static inline) so every target that includes it,
 * pd-tests included, has it without a source list of its own.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The modules, as bits. The three trigger modules (slice 3) each start the
 * hand's attack, with the cadence their settings name. */
#define WEAPON_MODULE_FIRE_ONCE (1u << 0) /* og.weapon.fire_once: the attack fires one round */
#define WEAPON_MODULE_DRY_FIRE  (1u << 1) /* og.weapon.dry_fire: the hand clicks on an empty clip */
#define WEAPON_MODULE_RELOAD    (1u << 2) /* og.weapon.reload: the hand reloads its function's clip */
#define WEAPON_MODULE_SET_STATE (1u << 3) /* og.weapon.set_state: a named state of the hand ("key") becomes "value" */
#define WEAPON_MODULE_BURST     (1u << 4) /* og.weapon.burst: the attack fires "count" rounds, "gap" ticks apart */
#define WEAPON_MODULE_AUTOMATIC (1u << 5) /* og.weapon.automatic: the attack fires at "rate" while the trigger is held */
#define WEAPON_MODULE_USE_AMMO  (1u << 6) /* og.weapon.use_ammo: each round of the attack takes "rounds" from the clip */
/* The shot modules (slice 4), one for each attack family: what each round
 * of the attack does. */
#define WEAPON_MODULE_HITSCAN     (1u << 7)  /* og.weapon.hitscan: a bullet ("pellets", "through_walls", "shield_damage") */
#define WEAPON_MODULE_BEAM        (1u << 8)  /* og.weapon.beam: a short beam ("range") */
#define WEAPON_MODULE_LAUNCH      (1u << 9)  /* og.weapon.launch: a projectile ("item", "item_function") */
#define WEAPON_MODULE_THROW       (1u << 10) /* og.weapon.throw: the function's thrown object ("give_up_gun") */
#define WEAPON_MODULE_MELEE       (1u << 11) /* og.weapon.melee: a strike */
#define WEAPON_MODULE_SPECIAL     (1u << 12) /* og.weapon.special: an action ("action") */
#define WEAPON_MODULE_MODE_TOGGLE (1u << 13) /* og.weapon.mode_toggle: a mode on or off ("mode", "drain") */
/* The two triggers of slice 5: a charge, then its release; a throw that
 * primes while the trigger is held. */
#define WEAPON_MODULE_CHARGE      (1u << 14) /* og.weapon.charge ("start", "time", "steps", "release", ...) */
#define WEAPON_MODULE_COOK        (1u << 15) /* og.weapon.cook ("fuse") */
#define WEAPON_MODULE_COUNT 16
#define WEAPON_MODULE_ALL ((1u << WEAPON_MODULE_COUNT) - 1)
#define WEAPON_MODULE_TRIGGERS (WEAPON_MODULE_FIRE_ONCE | WEAPON_MODULE_BURST | WEAPON_MODULE_AUTOMATIC \
	| WEAPON_MODULE_CHARGE | WEAPON_MODULE_COOK)
#define WEAPON_MODULE_SHOTS (WEAPON_MODULE_HITSCAN | WEAPON_MODULE_BEAM | WEAPON_MODULE_LAUNCH | WEAPON_MODULE_THROW \
	| WEAPON_MODULE_MELEE | WEAPON_MODULE_SPECIAL | WEAPON_MODULE_MODE_TOGGLE)
/* The modules that decide what an idle hand does; the seam runs the first
 * chosen of: a trigger (the first the walk reached, with its settings), the
 * empty click, the reload. */
#define WEAPON_MODULE_IDLE_ACTIONS (WEAPON_MODULE_TRIGGERS | WEAPON_MODULE_DRY_FIRE | WEAPON_MODULE_RELOAD)

/*
 * The gates. A gate reads the hand, so it can be answered at every weapon
 * event.
 *
 * - has_ammo: the clip holds what a shot needs. Its whole-number setting
 *   "rounds" (1 or more, default 1) is the least the clip must hold; a
 *   function that uses no ammunition, or whose clip is full, always passes.
 *   The original needs 2 for the Shotgun's double blast and 4 for the
 *   Tranquilizer's sedative shot, which the base programs set.
 * - has_reserve: there is ammunition to reload from (or the clip is a mod
 *   weapon's self-loading magazine); a function that uses none passes.
 * - ready_to_fire: the hand is idle (not attacking, reloading or switching).
 * - state_is: a named state of the hand ("key") is "equals" (true or false;
 *   every state is false when the hand takes the weapon).
 */
#define WEAPON_RULE_GATE_HAS_AMMO      (1u << 24)
#define WEAPON_RULE_GATE_HAS_RESERVE   (1u << 25)
#define WEAPON_RULE_GATE_READY_TO_FIRE (1u << 26)
#define WEAPON_RULE_GATE_STATE_IS      (1u << 27)
#define WEAPON_GATE_COUNT 4
#define WEAPON_RULE_GATE_ALL (((1u << WEAPON_GATE_COUNT) - 1) << 24)
#define WEAPON_GATE_HAS_AMMO_KIND "gate.weapon.has_ammo"
#define WEAPON_GATE_HAS_RESERVE_KIND "gate.weapon.has_reserve"
#define WEAPON_GATE_READY_TO_FIRE_KIND "gate.weapon.ready_to_fire"
#define WEAPON_GATE_STATE_IS_KIND "gate.weapon.state_is"

/* The node settings the table's modules and gates read. */
#define WEAPON_RULE_ROUNDS_PARAM "rounds"
#define WEAPON_RULE_KEY_PARAM "key"
#define WEAPON_RULE_EQUALS_PARAM "equals"
#define WEAPON_RULE_VALUE_PARAM "value"
#define WEAPON_RULE_MAX_ROUNDS 255
#define WEAPON_RULE_STATE_KEY_LEN 64
/* The distinct state keys one function's program may name. */
#define WEAPON_RULE_MAX_STATES 8

/* The modules by bit, in bit order. */
static inline const char *weaponRulesModuleKind(u32 bit)
{
	static const char *const kinds[WEAPON_MODULE_COUNT] = {
		"og.weapon.fire_once", "og.weapon.dry_fire", "og.weapon.reload", "og.weapon.set_state",
		"og.weapon.burst", "og.weapon.automatic", "og.weapon.use_ammo",
		"og.weapon.hitscan", "og.weapon.beam", "og.weapon.launch", "og.weapon.throw",
		"og.weapon.melee", "og.weapon.special", "og.weapon.mode_toggle",
		"og.weapon.charge", "og.weapon.cook",
	};
	for (s32 i = 0; i < WEAPON_MODULE_COUNT; i++) {
		if (bit == (1u << i)) return kinds[i];
	}
	return NULL;
}

/* The bit of a module kind, 0 when it is not a weapon module. */
static inline u32 weaponRulesModuleBit(const char *kind)
{
	if (!kind) return 0;
	for (s32 i = 0; i < WEAPON_MODULE_COUNT; i++) {
		if (strcmp(weaponRulesModuleKind(1u << i), kind) == 0) return 1u << i;
	}
	return 0;
}

/* "gate.weapon.has_ammo" for WEAPON_RULE_GATE_HAS_AMMO; NULL unless the
 * argument is exactly one gate bit. */
static inline const char *weaponRulesGateKind(u32 gate)
{
	static const char *const kinds[WEAPON_GATE_COUNT] = {
		WEAPON_GATE_HAS_AMMO_KIND, WEAPON_GATE_HAS_RESERVE_KIND,
		WEAPON_GATE_READY_TO_FIRE_KIND, WEAPON_GATE_STATE_IS_KIND,
	};
	for (s32 i = 0; i < WEAPON_GATE_COUNT; i++) {
		if (gate == (WEAPON_RULE_GATE_HAS_AMMO << i)) return kinds[i];
	}
	return NULL;
}

/* The bit of a gate kind, 0 when it is not a weapon gate. */
static inline u32 weaponRulesGateBit(const char *kind)
{
	if (!kind) return 0;
	for (s32 i = 0; i < WEAPON_GATE_COUNT; i++) {
		if (strcmp(weaponRulesGateKind(WEAPON_RULE_GATE_HAS_AMMO << i), kind) == 0) {
			return WEAPON_RULE_GATE_HAS_AMMO << i;
		}
	}
	return 0;
}

/* The weapon events, in kind order (MISSION_GRAPH_EVENT_WEAPON_*). */
#define WEAPON_RULE_EVENT_COUNT \
	(MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE - MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED + 1)

/* The index-th weapon event's kind, or -1 past the end. */
static inline s32 weaponRulesEventAt(s32 index)
{
	return index >= 0 && index < WEAPON_RULE_EVENT_COUNT
		? MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED + index : -1;
}

/* "event.weapon.trigger_pressed" for its kind; NULL for no weapon event. */
static inline const char *weaponRulesEventName(s32 event_kind)
{
	static const char *const names[WEAPON_RULE_EVENT_COUNT] = {
		"event.weapon.trigger_pressed", "event.weapon.ready_while_held",
		"event.weapon.clip_empty", "event.weapon.trigger_released",
		"event.weapon.reload_done",
	};
	const s32 index = event_kind - MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED;

	return index >= 0 && index < WEAPON_RULE_EVENT_COUNT ? names[index] : NULL;
}

/* The modules the event's seam applies. The idle seam's three rule events
 * (trigger_pressed, ready_while_held, clip_empty) apply the idle actions,
 * use_ammo and the shot modules (the attack a trigger starts) and set_state;
 * the records (trigger_released, reload_done) apply set_state only. */
static inline u32 weaponRulesEventModules(s32 event_kind)
{
	switch (event_kind) {
	case MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED:
	case MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD:
	case MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY:
		return WEAPON_MODULE_IDLE_ACTIONS | WEAPON_MODULE_USE_AMMO | WEAPON_MODULE_SHOTS | WEAPON_MODULE_SET_STATE;
	case MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED:
	case MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE:
		return WEAPON_MODULE_SET_STATE;
	}
	return 0;
}

/*
 * The original's choice at the idle seam (bgunTickIncIdle). ammo_status is
 * the native ammunition check (bgun0f098ca0): below 0 nothing to fire or
 * reload, 0 the clip is below what a shot needs but there is reserve, 1 or
 * more the clip can fire. At trigger_pressed and ready_while_held the
 * original fires when the clip can, clicks when it is empty with reserve
 * (unless the hand holds no weapon, when it reloads), and does nothing when
 * there is nothing at all; at clip_empty it reloads. 0 elsewhere.
 */
static inline u32 weaponRulesNativeIdle(s32 event_kind, s32 ammo_status, s32 weapon_is_none)
{
	switch (event_kind) {
	case MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED:
	case MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD:
		if (ammo_status > 0) return weapon_is_none ? 0 : WEAPON_MODULE_FIRE_ONCE;
		if (ammo_status == 0) return weapon_is_none ? WEAPON_MODULE_RELOAD : WEAPON_MODULE_DRY_FIRE;
		return 0;
	case MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY:
		return WEAPON_MODULE_RELOAD;
	}
	return 0;
}

/* The rounds a shot of this weapon number and function needs, by the
 * original (bgun0f098ca0's minimum): 2 for the Shotgun's secondary, 4 for
 * the Tranquilizer's, 1 for everything else. The base programs set it on
 * their has_ammo gate. */
static inline s32 weaponRulesNativeRounds(s32 weaponnum, s32 func)
{
	if (weaponnum == WEAPON_SHOTGUN && func == FUNC_SECONDARY) return 2;
	if (weaponnum == WEAPON_TRANQUILIZER && func == FUNC_SECONDARY) return 4;
	return 1;
}

/* The rounds a has_ammo node sets: its "rounds" text, NULL for none.
 * Returns the number (1 to WEAPON_RULE_MAX_ROUNDS), 1 for NULL, and -1 for
 * anything else. */
static inline s32 weaponRulesRoundsFromText(const char *text)
{
	long value;
	char *end = NULL;

	if (!text) return 1;
	if (!text[0]) return -1;
	for (const char *p = text; *p; p++) {
		if (*p < '0' || *p > '9') return -1;
	}
	value = strtol(text, &end, 10);
	if (!end || *end || value < 1 || value > WEAPON_RULE_MAX_ROUNDS) return -1;
	return (s32)value;
}

/* A state_is "equals" or set_state "value" text: 1 for "true", 0 for
 * "false", -1 for anything else (NULL included). */
static inline s32 weaponRulesBoolFromText(const char *text)
{
	if (!text) return -1;
	if (strcmp(text, "true") == 0) return 1;
	if (strcmp(text, "false") == 0) return 0;
	return -1;
}

/*
 * The hand as the gates read it. The seam fills it from the hand and its
 * function as the native ammunition check reads them; pd-tests fill it by
 * hand.
 */
typedef struct weapon_rule_hand {
	s32 uses_ammo; /* the function has a clip: an ammo slot whose ammo type exists */
	s32 loaded;    /* rounds in the clip */
	s32 clipsize;  /* the clip's size */
	s32 reserve;   /* there is ammunition to reload from, or the clip loads itself */
	s32 idle;      /* the hand is idle */
} weapon_rule_hand_t;

static inline s32 weaponRulesHasAmmo(const weapon_rule_hand_t *hand, s32 rounds)
{
	if (!hand) return 0;
	if (rounds < 1) rounds = 1;
	if (!hand->uses_ammo || hand->loaded >= hand->clipsize) return 1;
	return hand->loaded >= rounds;
}

static inline s32 weaponRulesHasReserve(const weapon_rule_hand_t *hand)
{
	if (!hand) return 0;
	return !hand->uses_ammo || hand->reserve;
}

/*
 * held-weapon-events.md slice 3: the attack a trigger module starts, as the
 * cadence code runs it (bondgun.c bgun0f09a3f8 and bgun0f09a6f8 call the
 * steps below): which trigger, its settings, and the rounds each shot
 * takes. The idle seam keeps the plan of the attack it started; with no
 * program the plan is the original's for the function
 * (weaponRulesNativeTrigger), so a v1 function fires as it always has.
 *
 * Settings (node fields, each with the unit it is read in):
 *   og.weapon.burst      count (rounds in the burst, 2 to 50), gap (60 Hz
 *                        ticks between its rounds, 0 to 255)
 *   og.weapon.automatic  rate and start_rate (rounds per minute, above 0 and
 *                        up to 3600), spin_up and spin_down (60 Hz ticks from
 *                        start_rate to rate and back, 0 to 10000), burst
 *                        (rounds it fires once started, released or not, 0
 *                        to 255), aimed_burst (whether that burst holds while
 *                        the player aims)
 *   og.weapon.use_ammo   rounds (rounds each shot takes, 1 to 255)
 * og.weapon.fire_once has none. A setting a node leaves out is the function
 * record's own (the base weapon's), so a node dropped in works before it is
 * edited; the base programs write every setting, so a base gun's quirks
 * (the Shotgun's slower double blast, the Reaper's spin, the bursts that
 * give way while aiming) are visible settings.
 */
#define WEAPON_RULE_COUNT_PARAM "count"
#define WEAPON_RULE_GAP_PARAM "gap"
#define WEAPON_RULE_RATE_PARAM "rate"
#define WEAPON_RULE_START_RATE_PARAM "start_rate"
#define WEAPON_RULE_SPIN_UP_PARAM "spin_up"
#define WEAPON_RULE_SPIN_DOWN_PARAM "spin_down"
#define WEAPON_RULE_BURST_PARAM "burst"
#define WEAPON_RULE_AIMED_BURST_PARAM "aimed_burst"
#define WEAPON_RULE_MAX_RATE 3600.0f
#define WEAPON_RULE_DEFAULT_RATE 600.0f
#define WEAPON_RULE_MAX_SPIN 10000
#define WEAPON_RULE_DEFAULT_GAP 3
#define WEAPON_RULE_MIN_BURST_COUNT 2
#define WEAPON_RULE_MAX_BURST_COUNT 50
#define WEAPON_RULE_MAX_GAP 255
#define WEAPON_RULE_MAX_AUTO_BURST 255

typedef struct weapon_rule_trigger {
	u32 module;      /* WEAPON_MODULE_FIRE_ONCE, _BURST or _AUTOMATIC; 0 none */
	s32 count;       /* burst: rounds in the burst */
	s32 gap;         /* burst: 60 Hz ticks between its rounds */
	f32 rate;        /* automatic: rounds per minute at full speed */
	f32 start_rate;  /* automatic: rounds per minute as it starts */
	s32 spin_up;     /* automatic: ticks from start_rate to rate (0: at once) */
	s32 spin_down;   /* automatic: ticks back down after release (0: at once) */
	s32 burst;       /* automatic: rounds it fires once started, released or not */
	s32 aimed_burst; /* automatic: that burst holds while the player aims */
	s32 rounds;      /* use_ammo: rounds a shot takes; 0 when the attack takes none */
	/* slice 4: the shot, WEAPON_MODULE_HITSCAN ... _MODE_TOGGLE (0: none, a
	 * device), and its settings. */
	u32 shot;
	s32 pellets;       /* hitscan: bullets each round fires */
	s32 through_walls; /* hitscan: the bullets pass walls */
	f32 shield_damage; /* hitscan: a hit on a shield does this many times its damage */
	f32 range;         /* beam: its reach in world units */
	s32 item;          /* launch: the weapon number of the item it fires; WEAPON_RULE_ITEM_OWN its own */
	s32 item_function; /* launch: the item's function; WEAPON_RULE_ITEM_FUNCTION_KEEP as made, _GUN the gun's */
	s32 give_up_gun;   /* throw: the gun itself goes with the throw */
	s32 action;        /* special: the HANDATTACKTYPE_* it performs */
	s32 mode;          /* mode toggle: WEAPON_RULE_MODE_CLOAK */
	f32 drain;         /* mode toggle: rounds a second while the mode is on */
	/* slice 5: a charge trigger's settings, and a cook trigger's fuse. */
	s32 charge_start;    /* WEAPON_RULE_CHARGE_WHILE_SELECTED (the Mauler) or _ON_PRESS (the crystal pistol) */
	s32 charge_time;     /* 60 Hz ticks from empty to full */
	s32 charge_steps;    /* steps to full */
	s32 charge_ammo;     /* rounds each step takes */
	s32 charge_min_clip; /* a step needs the clip to hold this many */
	s32 charge_fall;     /* 60 Hz ticks a step falls back while the other function is held; 0 never */
	s32 charge_release;  /* WEAPON_RULE_RELEASE_ON_PRESS or _ON_FULL */
	f32 charge_damage;   /* each step adds this many times the shot's damage */
	s32 cook_fuse;       /* cook: 60 Hz ticks the throw may be held before it goes off in the hand */
} weapon_rule_trigger_t;

#define WEAPON_RULE_ITEM_OWN (-1)              /* the function's own projectile object */
#define WEAPON_RULE_ITEM_UNKNOWN (-2)          /* an item no weapon number names: nothing is fired */
#define WEAPON_RULE_ITEM_FUNCTION_KEEP (-1)    /* the item keeps the function it was made with */
#define WEAPON_RULE_ITEM_FUNCTION_GUN (-2)     /* the item takes the firing gun's function */
#define WEAPON_RULE_MODE_CLOAK 1               /* the RCP120's cloak (DEVICE_CLOAKRCP120) */
#define WEAPON_RULE_ATTACK_CLOAK 9             /* HANDATTACKTYPE_RCP120CLOAK */
#define WEAPON_RULE_MAX_PELLETS 32
#define WEAPON_RULE_MAX_SHIELD_DAMAGE 100
#define WEAPON_RULE_MAX_RANGE 65536
#define WEAPON_RULE_DEFAULT_LASER_RANGE 300
#define WEAPON_RULE_DEFAULT_CLOAK_DRAIN 24.0f  /* the original's 0.4 rounds a 60 Hz tick */
#define WEAPON_RULE_ITEM_LEN 64

/* The cadence a function's record describes (bondgun.c reads the same
 * fields): the function type (its family in the low byte, 0x100 for an
 * automatic), its flags, its clip's ammo slot (-1 for none), and an
 * automatic's rates and spin. */
typedef struct weapon_rule_function {
	s32 weaponnum;
	s32 func;
	s32 functype;
	u32 flags;
	s32 ammo_slot;
	s32 specialfunc; /* a special's HANDATTACKTYPE_* (slice 4) */
	s32 incremental; /* its clip reloads a round at a time (AMMOFLAG_INCREMENTALRELOAD; slice 4b) */
	s32 regrow;      /* its clip regrows (a mod magazine's "regrow" reload) */
	s32 regrow_ticks60; /* 60 Hz ticks a regrowing round takes */
	s32 has_charge;     /* a mod function's committed charge launch (T-MODDING-008; slice 5) */
	s32 charge_ticks60; /* its charge time */
	s32 activation_time; /* a throw's activation time (60 Hz ticks): a cooked grenade's fuse */
	s32 has_rate;    /* the record names a rate (an automatic) */
	f32 initial_rpm;
	f32 max_rpm;
	s32 accel;
	s32 decel;
} weapon_rule_function_t;

#define WEAPON_RULE_FUNCFLAG_BURST3  0x00000002u /* FUNCFLAG_BURST3 */
#define WEAPON_RULE_FUNCFLAG_BURST50 0x00000020u /* FUNCFLAG_BURST50 */
#define WEAPON_RULE_FUNCFLAG_BURST2  0x00001000u /* FUNCFLAG_BURST2 */
#define WEAPON_RULE_FUNCFLAG_BURST5  0x00020000u /* FUNCFLAG_BURST5 */

static inline s32 weaponRulesFunctionShoots(const weapon_rule_function_t *f)
{
	return f && (f->functype & 0xff) == 1; /* INVENTORYFUNCTYPE_SHOOT */
}

static inline s32 weaponRulesFunctionAutomatic(const weapon_rule_function_t *f)
{
	return f && (f->has_rate || (f->functype & 0xff00) == 0x100);
}

/*
 * The original's plan for a function: a function that does not shoot fires
 * once (its attack is a throw, a swing or a special) and spends nothing
 * here; an automatic is og.weapon.automatic at the record's rates and spin,
 * with the burst its flags give it (2, 3, 5 or 50 rounds; the 3-round one
 * gives way while aiming); any other shooting function with a 2-, 3- or
 * 5-round burst flag is og.weapon.burst (the shortest flagged, 3 ticks
 * apart, 13 for the Shotgun's double blast); the rest fire once. A shot
 * takes one round from the clip, except where the function has no clip and
 * for the Laser's single shot, which take none.
 *
 * Exact for every base function (each has at most one burst flag). For a v1
 * record whose flags give an automatic both the 3-round burst and the
 * 2-round one, the plan keeps the 3-round burst while aiming, where the
 * original kept 2.
 */
/*
 * held-weapon-events.md slice 4: the original's shot for a function, by its
 * family: a shooting function's bullet (six pellets for the Shotgun; the
 * Farsight's pass walls and do ten times their damage to shields), the
 * Laser stream's 300-unit beam, a projectile launcher's item (the rocket
 * launcher and the Slayer fire base:rocket, the crossbow base:bolt and the
 * Devastator base:grenaderound with the gun's function, the Super Dragon
 * base:grenaderound as function 2; any other launcher its own object), a
 * throw (the Laptop gun gives up the gun), a strike, a special's action, and
 * the RCP120's cloak toggle, which drains 24 rounds a second. A device has
 * none. The rounds each family's attack takes are the plan's (use_ammo): a
 * throw and a special take 1 when the function has a clip, the
 * Tranquilizer's sedative strike 4, any other strike none.
 */
static inline void weaponRulesNativeShot(const weapon_rule_function_t *f, weapon_rule_trigger_t *out)
{
	out->shot = 0;
	out->pellets = 1;
	out->through_walls = 0;
	out->shield_damage = 1.0f;
	out->range = 0.0f;
	out->item = WEAPON_RULE_ITEM_OWN;
	out->item_function = WEAPON_RULE_ITEM_FUNCTION_KEEP;
	out->give_up_gun = 0;
	out->action = 0;
	out->mode = 0;
	out->drain = 0.0f;
	if (!f) return;
	switch (f->functype & 0xff) {
	case 1: /* INVENTORYFUNCTYPE_SHOOT */
		if ((f->functype & 0xff00) == 0x200) {
			out->shot = WEAPON_MODULE_LAUNCH;
			if (f->weaponnum == WEAPON_ROCKETLAUNCHER || f->weaponnum == WEAPON_SLAYER) {
				out->item = WEAPON_ROCKET;
			} else if (f->weaponnum == WEAPON_CROSSBOW) {
				out->item = WEAPON_BOLT;
				out->item_function = WEAPON_RULE_ITEM_FUNCTION_GUN;
			} else if (f->weaponnum == WEAPON_DEVASTATOR) {
				out->item = WEAPON_GRENADEROUND;
				out->item_function = WEAPON_RULE_ITEM_FUNCTION_GUN;
			} else if (f->weaponnum == WEAPON_SUPERDRAGON) {
				out->item = WEAPON_GRENADEROUND;
				out->item_function = FUNC_2;
			}
		} else if (f->weaponnum == WEAPON_LASER && f->func == FUNC_SECONDARY) {
			out->shot = WEAPON_MODULE_BEAM;
			out->range = (f32)WEAPON_RULE_DEFAULT_LASER_RANGE;
		} else {
			out->shot = WEAPON_MODULE_HITSCAN;
			out->pellets = f->weaponnum == WEAPON_SHOTGUN ? 6 : 1;
			out->through_walls = f->weaponnum == WEAPON_FARSIGHT;
			out->shield_damage = f->weaponnum == WEAPON_FARSIGHT ? 10.0f : 1.0f;
		}
		break;
	case 2: /* INVENTORYFUNCTYPE_THROW */
		out->shot = WEAPON_MODULE_THROW;
		out->give_up_gun = f->weaponnum == WEAPON_LAPTOPGUN;
		break;
	case 3: /* INVENTORYFUNCTYPE_MELEE */
		out->shot = WEAPON_MODULE_MELEE;
		break;
	case 4: /* INVENTORYFUNCTYPE_SPECIAL */
		if (f->specialfunc == WEAPON_RULE_ATTACK_CLOAK) {
			out->shot = WEAPON_MODULE_MODE_TOGGLE;
			out->mode = WEAPON_RULE_MODE_CLOAK;
			out->drain = WEAPON_RULE_DEFAULT_CLOAK_DRAIN;
		} else {
			out->shot = WEAPON_MODULE_SPECIAL;
			out->action = f->specialfunc;
		}
		break;
	}
}

/* The family a shot module belongs to (an INVENTORYFUNCTYPE_* low byte, with
 * 0x200 for a projectile launcher), -1 for none. A program's shot must be
 * its function's family: the attack's states are still the record's. */
static inline s32 weaponRulesShotFamily(u32 shot)
{
	switch (shot) {
	case WEAPON_MODULE_HITSCAN:
	case WEAPON_MODULE_BEAM: return 1;
	case WEAPON_MODULE_LAUNCH: return 0x201;
	case WEAPON_MODULE_THROW: return 2;
	case WEAPON_MODULE_MELEE: return 3;
	case WEAPON_MODULE_SPECIAL:
	case WEAPON_MODULE_MODE_TOGGLE: return 4;
	}
	return -1;
}

/* Whether a shot module can be the shot of a function of this type. */
static inline s32 weaponRulesShotFits(u32 shot, s32 functype)
{
	const s32 family = weaponRulesShotFamily(shot);

	if (family < 0) return 0;
	if (family == 0x201) return (functype & 0xff) == 1 && (functype & 0xff00) == 0x200;
	if (family == 1) return (functype & 0xff) == 1 && (functype & 0xff00) != 0x200;
	return (functype & 0xff) == family;
}

#define WEAPON_RULE_CHARGE_WHILE_SELECTED 1 /* builds while the function is selected (the Mauler) */
#define WEAPON_RULE_CHARGE_ON_PRESS 2       /* a press starts it and the hand waits for it (the crystal pistol) */
#define WEAPON_RULE_RELEASE_ON_PRESS 1      /* the trigger fires the charged shot */
#define WEAPON_RULE_RELEASE_ON_FULL 2       /* it goes by itself when full */
#define WEAPON_RULE_MAULER_CHARGE_DAMAGE (10.0f / 3.0f)

/* A charge node's start: the one it names, else its function's (a custom
 * weapon's committed launch starts on a press; any other charge builds while
 * selected). -1 for a start it does not know. */
static inline s32 weaponRulesChargeStartFromText(const char *text, s32 committed)
{
	if (!text) return committed ? WEAPON_RULE_CHARGE_ON_PRESS : WEAPON_RULE_CHARGE_WHILE_SELECTED;
	if (strcmp(text, "while_selected") == 0) return WEAPON_RULE_CHARGE_WHILE_SELECTED;
	if (strcmp(text, "on_press") == 0) return WEAPON_RULE_CHARGE_ON_PRESS;
	return -1;
}

/* A charge node's release: the one it names, else its start's. -1 for a
 * release it does not know. */
static inline s32 weaponRulesChargeReleaseFromText(const char *text, s32 start)
{
	if (!text) return start == WEAPON_RULE_CHARGE_ON_PRESS ? WEAPON_RULE_RELEASE_ON_FULL : WEAPON_RULE_RELEASE_ON_PRESS;
	if (strcmp(text, "on_press") == 0) return WEAPON_RULE_RELEASE_ON_PRESS;
	if (strcmp(text, "on_full") == 0) return WEAPON_RULE_RELEASE_ON_FULL;
	return -1;
}

/*
 * held-weapon-events.md slice 5 (PR 31 review): the two charges the runtime
 * runs. One that builds while selected is fired by the trigger (release
 * on_press; the Mauler); one that a press starts is a custom weapon's
 * committed launch, which goes by itself when full (release on_full; the
 * crystal pistol). NULL when start and release are one of those, else why
 * not; custom is 1 for a custom weapon, 0 for a base one, -1 unknown.
 * Registration and conformance refuse the others, so no setting is accepted
 * and then ignored.
 */
static inline const char *weaponRulesChargeUnsupported(s32 start, s32 release, s32 custom)
{
	if (start == WEAPON_RULE_CHARGE_WHILE_SELECTED && release != WEAPON_RULE_RELEASE_ON_PRESS) {
		return "a charge that builds while_selected is fired by the trigger (release on_press)";
	}
	if (start == WEAPON_RULE_CHARGE_ON_PRESS) {
		if (release != WEAPON_RULE_RELEASE_ON_FULL) {
			return "a charge that a press starts goes by itself when full (release on_full)";
		}
		if (custom == 0) {
			return "start on_press is a custom weapon's committed launch; a base weapon's charge builds while_selected";
		}
	}
	return NULL;
}

/*
 * held-weapon-events.md slice 5: the Mauler's charge (its secondary): it
 * builds while the function is selected, five steps in 100 ticks, each step
 * taking a round while the clip holds two, falls back a step every 200 ticks
 * while the primary is held, and each step adds 10/3 of the shot's damage;
 * the trigger fires the charged shot.
 */
static inline void weaponRulesMaulerCharge(weapon_rule_trigger_t *out)
{
	out->module = WEAPON_MODULE_CHARGE;
	out->charge_start = WEAPON_RULE_CHARGE_WHILE_SELECTED;
	out->charge_time = 100;
	out->charge_steps = 5;
	out->charge_ammo = 1;
	out->charge_min_clip = 2;
	out->charge_fall = 200;
	out->charge_release = WEAPON_RULE_RELEASE_ON_PRESS;
	out->charge_damage = WEAPON_RULE_MAULER_CHARGE_DAMAGE;
}

/* The rounds a charge's step needs in the clip: its min_clip, and at
 * least the rounds the step takes (PR 31 review: a step is never granted for
 * less than it costs). The Mauler's is its min_clip, 2. */
static inline s32 weaponRulesChargeStepNeeds(const weapon_rule_trigger_t *plan)
{
	return plan->charge_ammo > plan->charge_min_clip ? plan->charge_ammo : plan->charge_min_clip;
}

/* A mod function's committed charge launch (the crystal pistol): a press
 * starts it, the hand waits its charge time, and it goes by itself. */
static inline void weaponRulesCommittedCharge(const weapon_rule_function_t *f, weapon_rule_trigger_t *out)
{
	out->module = WEAPON_MODULE_CHARGE;
	out->charge_start = WEAPON_RULE_CHARGE_ON_PRESS;
	out->charge_time = f && f->charge_ticks60 > 0 ? f->charge_ticks60 : 1;
	out->charge_steps = 1;
	out->charge_ammo = 0;
	out->charge_min_clip = 1;
	out->charge_fall = 0;
	out->charge_release = WEAPON_RULE_RELEASE_ON_FULL;
	out->charge_damage = 0.0f;
}

static inline void weaponRulesNativeTrigger(const weapon_rule_function_t *f, weapon_rule_trigger_t *out)
{
	memset(out, 0, sizeof(*out));
	out->aimed_burst = 1;
	weaponRulesNativeShot(f, out);
	if (f && f->has_charge && f->weaponnum >= WEAPON_CUSTOM_START) {
		/* slice 5: the committed charge spends its own rounds on release. */
		weaponRulesCommittedCharge(f, out);
		return;
	}
	if (!f || !weaponRulesFunctionShoots(f)) {
		out->module = WEAPON_MODULE_FIRE_ONCE;
		if (f && (f->functype & 0xff) == 2 && f->weaponnum == WEAPON_GRENADE && f->func == FUNC_PRIMARY) {
			/* slice 5: the hand grenade cooks while held. */
			out->module = WEAPON_MODULE_COOK;
			out->cook_fuse = f->activation_time;
		}
		if (f && f->ammo_slot >= 0) {
			if ((f->functype & 0xff) == 2 || (f->functype & 0xff) == 4) {
				out->rounds = 1; /* a throw or a special */
			} else if ((f->functype & 0xff) == 3 && f->weaponnum == WEAPON_TRANQUILIZER
					&& f->func == FUNC_SECONDARY) {
				out->rounds = 4; /* the sedative strike */
			}
		}
		return;
	}
	if (f->weaponnum == WEAPON_MAULER && f->func == FUNC_SECONDARY) {
		weaponRulesMaulerCharge(out);
		out->rounds = f->ammo_slot >= 0 ? 1 : 0;
		return;
	}
	if (weaponRulesFunctionAutomatic(f)) {
		out->module = WEAPON_MODULE_AUTOMATIC;
		out->rate = f->max_rpm;
		out->start_rate = f->initial_rpm;
		out->spin_up = f->accel > 0 ? f->accel : 0;
		out->spin_down = f->decel > 0 ? f->decel : 0;
		if (f->flags & WEAPON_RULE_FUNCFLAG_BURST2) out->burst = 2;
		if (f->flags & WEAPON_RULE_FUNCFLAG_BURST3) out->burst = 3;
		if (f->flags & WEAPON_RULE_FUNCFLAG_BURST5) out->burst = 5;
		if (f->flags & WEAPON_RULE_FUNCFLAG_BURST50) out->burst = 50;
		out->aimed_burst = !((f->flags & WEAPON_RULE_FUNCFLAG_BURST3)
			&& !(f->flags & (WEAPON_RULE_FUNCFLAG_BURST5 | WEAPON_RULE_FUNCFLAG_BURST50)));
		out->rounds = f->ammo_slot >= 0 ? 1 : 0;
		return;
	}
	if (f->flags & (WEAPON_RULE_FUNCFLAG_BURST2 | WEAPON_RULE_FUNCFLAG_BURST3 | WEAPON_RULE_FUNCFLAG_BURST5)) {
		out->module = WEAPON_MODULE_BURST;
		out->count = (f->flags & WEAPON_RULE_FUNCFLAG_BURST2) ? 2 : (f->flags & WEAPON_RULE_FUNCFLAG_BURST3) ? 3 : 5;
		out->gap = f->weaponnum == WEAPON_SHOTGUN ? TICKS(13) : WEAPON_RULE_DEFAULT_GAP;
	} else {
		out->module = WEAPON_MODULE_FIRE_ONCE;
	}
	out->rounds = f->ammo_slot >= 0 && f->weaponnum != WEAPON_LASER ? 1 : 0;
}

/* A whole number setting: its text (NULL for none) within lo..hi. Returns
 * the number, `dflt` for NULL, and -1 for anything else. */
static inline s32 weaponRulesWholeFromText(const char *text, s32 lo, s32 hi, s32 dflt)
{
	long value;
	char *end = NULL;

	if (!text) return dflt;
	if (!text[0]) return -1;
	for (const char *p = text; *p; p++) {
		if (*p < '0' || *p > '9') return -1;
	}
	value = strtol(text, &end, 10);
	if (!end || *end || value < lo || value > hi) return -1;
	return (s32)value;
}

/* A rate setting in rounds per minute: above 0 and up to
 * WEAPON_RULE_MAX_RATE. Returns the rate, `dflt` for NULL, and -1 for
 * anything else. */
static inline f32 weaponRulesRateFromText(const char *text, f32 dflt)
{
	char *end = NULL;
	double value;

	if (!text) return dflt;
	if (!text[0] || text[0] == '+' || text[0] == '-' || text[0] == ' ') return -1.0f;
	value = strtod(text, &end);
	if (!end || *end || !(value > 0.0) || value > WEAPON_RULE_MAX_RATE) return -1.0f;
	return (f32)value;
}

/* The settings of the trigger and use_ammo modules, by kind of value, for
 * registration's check, conformance's mirror and the creator. */
#define WEAPON_RULE_SETTING_WHOLE 1 /* a whole number from lo to hi */
#define WEAPON_RULE_SETTING_RATE  2 /* rounds per minute, above 0 and up to WEAPON_RULE_MAX_RATE */
#define WEAPON_RULE_SETTING_BOOL  3 /* true or false */
#define WEAPON_RULE_SETTING_NUMBER 4        /* a number above 0 and up to hi */
#define WEAPON_RULE_SETTING_ITEM 5          /* "own" or an item's catalog id (namespace:name) */
#define WEAPON_RULE_SETTING_ITEM_FUNCTION 6 /* "gun" or a function, 0 to 2 */
#define WEAPON_RULE_SETTING_ACTION 7        /* a special's action by name */
#define WEAPON_RULE_SETTING_MODE 8          /* a toggled mode by name ("cloak") */
#define WEAPON_RULE_SETTING_STYLE 9         /* a reload style by name */
#define WEAPON_RULE_SETTING_CHARGE_START 10 /* "while_selected" or "on_press" */
#define WEAPON_RULE_SETTING_RELEASE 11      /* "on_press" or "on_full" */
#define WEAPON_RULE_SETTING_NONNEG 12       /* a number from 0 up to hi */
#define WEAPON_RULE_START_PARAM "start"
#define WEAPON_RULE_TIME_PARAM "time"
#define WEAPON_RULE_STEPS_PARAM "steps"
#define WEAPON_RULE_AMMO_PER_STEP_PARAM "ammo_per_step"
#define WEAPON_RULE_MIN_CLIP_PARAM "min_clip"
#define WEAPON_RULE_FALL_TIME_PARAM "fall_time"
#define WEAPON_RULE_RELEASE_PARAM "release"
#define WEAPON_RULE_DAMAGE_PER_STEP_PARAM "damage_per_step"
#define WEAPON_RULE_FUSE_PARAM "fuse"
#define WEAPON_RULE_STYLE_PARAM "style"
#define WEAPON_RULE_REGROW_TIME_PARAM "regrow_time"
#define WEAPON_RULE_PELLETS_PARAM "pellets"
#define WEAPON_RULE_THROUGH_WALLS_PARAM "through_walls"
#define WEAPON_RULE_SHIELD_DAMAGE_PARAM "shield_damage"
#define WEAPON_RULE_RANGE_PARAM "range"
#define WEAPON_RULE_ITEM_PARAM "item"
#define WEAPON_RULE_ITEM_FUNCTION_PARAM "item_function"
#define WEAPON_RULE_GIVE_UP_GUN_PARAM "give_up_gun"
#define WEAPON_RULE_ACTION_PARAM "action"
#define WEAPON_RULE_MODE_PARAM "mode"
#define WEAPON_RULE_DRAIN_PARAM "drain"
typedef struct weapon_rule_setting {
	u32 module;
	const char *key;
	s32 type;
	s32 lo;
	s32 hi;
} weapon_rule_setting_t;
#define WEAPON_RULE_SETTING_COUNT 30

static inline const weapon_rule_setting_t *weaponRulesSettingAt(s32 index)
{
	static const weapon_rule_setting_t settings[WEAPON_RULE_SETTING_COUNT] = {
		{ WEAPON_MODULE_BURST, WEAPON_RULE_COUNT_PARAM, WEAPON_RULE_SETTING_WHOLE,
			WEAPON_RULE_MIN_BURST_COUNT, WEAPON_RULE_MAX_BURST_COUNT },
		{ WEAPON_MODULE_BURST, WEAPON_RULE_GAP_PARAM, WEAPON_RULE_SETTING_WHOLE, 0, WEAPON_RULE_MAX_GAP },
		{ WEAPON_MODULE_AUTOMATIC, WEAPON_RULE_RATE_PARAM, WEAPON_RULE_SETTING_RATE, 0, 0 },
		{ WEAPON_MODULE_AUTOMATIC, WEAPON_RULE_START_RATE_PARAM, WEAPON_RULE_SETTING_RATE, 0, 0 },
		{ WEAPON_MODULE_AUTOMATIC, WEAPON_RULE_SPIN_UP_PARAM, WEAPON_RULE_SETTING_WHOLE, 0, WEAPON_RULE_MAX_SPIN },
		{ WEAPON_MODULE_AUTOMATIC, WEAPON_RULE_SPIN_DOWN_PARAM, WEAPON_RULE_SETTING_WHOLE, 0, WEAPON_RULE_MAX_SPIN },
		{ WEAPON_MODULE_AUTOMATIC, WEAPON_RULE_BURST_PARAM, WEAPON_RULE_SETTING_WHOLE, 0, WEAPON_RULE_MAX_AUTO_BURST },
		{ WEAPON_MODULE_AUTOMATIC, WEAPON_RULE_AIMED_BURST_PARAM, WEAPON_RULE_SETTING_BOOL, 0, 0 },
		{ WEAPON_MODULE_USE_AMMO, WEAPON_RULE_ROUNDS_PARAM, WEAPON_RULE_SETTING_WHOLE, 1, WEAPON_RULE_MAX_ROUNDS },
		{ WEAPON_MODULE_HITSCAN, WEAPON_RULE_PELLETS_PARAM, WEAPON_RULE_SETTING_WHOLE, 1, WEAPON_RULE_MAX_PELLETS },
		{ WEAPON_MODULE_HITSCAN, WEAPON_RULE_THROUGH_WALLS_PARAM, WEAPON_RULE_SETTING_BOOL, 0, 0 },
		{ WEAPON_MODULE_HITSCAN, WEAPON_RULE_SHIELD_DAMAGE_PARAM, WEAPON_RULE_SETTING_NUMBER, 0,
			WEAPON_RULE_MAX_SHIELD_DAMAGE },
		{ WEAPON_MODULE_BEAM, WEAPON_RULE_RANGE_PARAM, WEAPON_RULE_SETTING_WHOLE, 1, WEAPON_RULE_MAX_RANGE },
		{ WEAPON_MODULE_LAUNCH, WEAPON_RULE_ITEM_PARAM, WEAPON_RULE_SETTING_ITEM, 0, 0 },
		{ WEAPON_MODULE_LAUNCH, WEAPON_RULE_ITEM_FUNCTION_PARAM, WEAPON_RULE_SETTING_ITEM_FUNCTION, 0, 2 },
		{ WEAPON_MODULE_THROW, WEAPON_RULE_GIVE_UP_GUN_PARAM, WEAPON_RULE_SETTING_BOOL, 0, 0 },
		{ WEAPON_MODULE_SPECIAL, WEAPON_RULE_ACTION_PARAM, WEAPON_RULE_SETTING_ACTION, 0, 0 },
		{ WEAPON_MODULE_MODE_TOGGLE, WEAPON_RULE_MODE_PARAM, WEAPON_RULE_SETTING_MODE, 0, 0 },
		{ WEAPON_MODULE_MODE_TOGGLE, WEAPON_RULE_DRAIN_PARAM, WEAPON_RULE_SETTING_NUMBER, 0, (s32)WEAPON_RULE_MAX_RATE },
		{ WEAPON_MODULE_RELOAD, WEAPON_RULE_STYLE_PARAM, WEAPON_RULE_SETTING_STYLE, 0, 0 },
		{ WEAPON_MODULE_RELOAD, WEAPON_RULE_REGROW_TIME_PARAM, WEAPON_RULE_SETTING_WHOLE, 1, WEAPON_RULE_MAX_SPIN },
		{ WEAPON_MODULE_CHARGE, WEAPON_RULE_START_PARAM, WEAPON_RULE_SETTING_CHARGE_START, 0, 0 },
		{ WEAPON_MODULE_CHARGE, WEAPON_RULE_TIME_PARAM, WEAPON_RULE_SETTING_WHOLE, 1, WEAPON_RULE_MAX_SPIN },
		{ WEAPON_MODULE_CHARGE, WEAPON_RULE_STEPS_PARAM, WEAPON_RULE_SETTING_WHOLE, 1, 50 },
		{ WEAPON_MODULE_CHARGE, WEAPON_RULE_AMMO_PER_STEP_PARAM, WEAPON_RULE_SETTING_WHOLE, 0, WEAPON_RULE_MAX_ROUNDS },
		{ WEAPON_MODULE_CHARGE, WEAPON_RULE_MIN_CLIP_PARAM, WEAPON_RULE_SETTING_WHOLE, 0, WEAPON_RULE_MAX_ROUNDS },
		{ WEAPON_MODULE_CHARGE, WEAPON_RULE_FALL_TIME_PARAM, WEAPON_RULE_SETTING_WHOLE, 0, WEAPON_RULE_MAX_SPIN },
		{ WEAPON_MODULE_CHARGE, WEAPON_RULE_RELEASE_PARAM, WEAPON_RULE_SETTING_RELEASE, 0, 0 },
		{ WEAPON_MODULE_CHARGE, WEAPON_RULE_DAMAGE_PER_STEP_PARAM, WEAPON_RULE_SETTING_NONNEG, 0, 100 },
		{ WEAPON_MODULE_COOK, WEAPON_RULE_FUSE_PARAM, WEAPON_RULE_SETTING_WHOLE, 1, WEAPON_RULE_MAX_SPIN },
	};
	return index >= 0 && index < WEAPON_RULE_SETTING_COUNT ? &settings[index] : NULL;
}

/* A positive number up to hi: the number, `dflt` for NULL, -1 for anything
 * else. */
static inline f32 weaponRulesNumberFromText(const char *text, f32 hi, f32 dflt)
{
	char *end = NULL;
	double value;

	if (!text) return dflt;
	if (!text[0] || text[0] == '+' || text[0] == '-' || text[0] == ' ') return -1.0f;
	value = strtod(text, &end);
	if (!end || *end || !(value > 0.0) || value > hi) return -1.0f;
	return (f32)value;
}

/* A special's action names (HANDATTACKTYPE_*): the cloak is a mode toggle,
 * 11 is unused. */
static inline const char *weaponRulesActionName(s32 action)
{
	switch (action) {
	case 1: return "shoot";
	case 2: return "launch";
	case 3: return "throw";
	case 4: return "melee";
	case 5: return "detonate";
	case 6: return "boost";
	case 7: return "revert_boost";
	case 8: return "crouch";
	case 10: return "melee_no_uncloak";
	case 12: return "uplink";
	}
	return NULL;
}

static inline s32 weaponRulesActionFromText(const char *text)
{
	if (!text) return -1;
	for (s32 a = 1; a <= 12; a++) {
		const char *name = weaponRulesActionName(a);

		if (name && strcmp(name, text) == 0) return a;
	}
	return -1;
}

static inline s32 weaponRulesModeFromText(const char *text)
{
	return text && strcmp(text, "cloak") == 0 ? WEAPON_RULE_MODE_CLOAK : -1;
}

/* held-weapon-events.md slice 4b: how og.weapon.reload fills the clip. */
#define WEAPON_RULE_RELOAD_MAGAZINE 1      /* the reload animation loads the clip */
#define WEAPON_RULE_RELOAD_ONE_AT_A_TIME 2 /* a round each time the animation loops; the trigger stops it */
#define WEAPON_RULE_RELOAD_REGROW 3        /* a round each "regrow_time" ticks, from no reserve */
#define WEAPON_RULE_DEFAULT_REGROW_TIME 60

static inline const char *weaponRulesStyleName(s32 style)
{
	switch (style) {
	case WEAPON_RULE_RELOAD_MAGAZINE: return "magazine";
	case WEAPON_RULE_RELOAD_ONE_AT_A_TIME: return "one_at_a_time";
	case WEAPON_RULE_RELOAD_REGROW: return "regrow";
	}
	return NULL;
}

static inline s32 weaponRulesStyleFromText(const char *text)
{
	for (s32 s = WEAPON_RULE_RELOAD_MAGAZINE; s <= WEAPON_RULE_RELOAD_REGROW; s++) {
		if (text && strcmp(text, weaponRulesStyleName(s)) == 0) return s;
	}
	return -1;
}

/* The reload a function's record gives it: a mod magazine's regrow, the
 * Shotgun's and the crossbow's round at a time (their ammo's
 * AMMOFLAG_INCREMENTALRELOAD), else the magazine. */
typedef struct weapon_rule_reload {
	s32 style;       /* WEAPON_RULE_RELOAD_*; 0 when none was planned */
	s32 regrow_time; /* regrow: 60 Hz ticks a round takes */
} weapon_rule_reload_t;

static inline void weaponRulesNativeReload(const weapon_rule_function_t *f, weapon_rule_reload_t *out)
{
	out->style = WEAPON_RULE_RELOAD_MAGAZINE;
	out->regrow_time = f && f->regrow_ticks60 > 0 ? f->regrow_ticks60 : WEAPON_RULE_DEFAULT_REGROW_TIME;
	if (f && f->regrow) {
		out->style = WEAPON_RULE_RELOAD_REGROW;
	} else if (f && f->incremental) {
		out->style = WEAPON_RULE_RELOAD_ONE_AT_A_TIME;
	}
}

static inline s32 weaponRulesReloadEqual(const weapon_rule_reload_t *a, const weapon_rule_reload_t *b)
{
	return a->style == b->style
		&& (a->style != WEAPON_RULE_RELOAD_REGROW || a->regrow_time == b->regrow_time);
}

/* "gun" or a function number, 0 to 2: WEAPON_RULE_ITEM_FUNCTION_GUN or the
 * number, -3 for anything else (NULL included). */
static inline s32 weaponRulesItemFunctionFromText(const char *text)
{
	if (!text) return -3;
	if (strcmp(text, "gun") == 0) return WEAPON_RULE_ITEM_FUNCTION_GUN;
	if (text[0] >= '0' && text[0] <= '2' && !text[1]) return text[0] - '0';
	return -3;
}

/* An item's catalog id as a setting writes it: a namespace and a name of
 * lower-case letters, digits and underscores ("base:rocket"). */
static inline s32 weaponRulesItemIdValid(const char *text)
{
	s32 colon = 0;
	size_t n;

	if (!text || !text[0]) return 0;
	n = strlen(text);
	if (n >= WEAPON_RULE_ITEM_LEN || text[0] == ':' || text[n - 1] == ':') return 0;
	for (const char *p = text; *p; p++) {
		if (*p == ':') {
			if (colon++) return 0;
		} else if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
			return 0;
		}
	}
	return colon == 1;
}

/* Whether a setting's text (not NULL) is a value of its kind. */
static inline s32 weaponRulesSettingValid(const weapon_rule_setting_t *setting, const char *text)
{
	if (!setting || !text) return 0;
	switch (setting->type) {
	case WEAPON_RULE_SETTING_WHOLE:
		return weaponRulesWholeFromText(text, setting->lo, setting->hi, -1) >= 0;
	case WEAPON_RULE_SETTING_RATE:
		return weaponRulesRateFromText(text, -1.0f) > 0.0f;
	case WEAPON_RULE_SETTING_BOOL:
		return weaponRulesBoolFromText(text) >= 0;
	case WEAPON_RULE_SETTING_NUMBER:
		return weaponRulesNumberFromText(text, (f32)setting->hi, -1.0f) > 0.0f;
	case WEAPON_RULE_SETTING_ITEM:
		return strcmp(text, "own") == 0 || weaponRulesItemIdValid(text);
	case WEAPON_RULE_SETTING_ITEM_FUNCTION:
		return weaponRulesItemFunctionFromText(text) != -3;
	case WEAPON_RULE_SETTING_ACTION:
		return weaponRulesActionFromText(text) > 0;
	case WEAPON_RULE_SETTING_MODE:
		return weaponRulesModeFromText(text) > 0;
	case WEAPON_RULE_SETTING_STYLE:
		return weaponRulesStyleFromText(text) > 0;
	case WEAPON_RULE_SETTING_CHARGE_START:
		return strcmp(text, "while_selected") == 0 || strcmp(text, "on_press") == 0;
	case WEAPON_RULE_SETTING_RELEASE:
		return strcmp(text, "on_press") == 0 || strcmp(text, "on_full") == 0;
	case WEAPON_RULE_SETTING_NONNEG:
		return strcmp(text, "0") == 0 || weaponRulesNumberFromText(text, (f32)setting->hi, -1.0f) > 0.0f;
	}
	return 0;
}

/* The settings of one trigger or use_ammo node, read by `get` (a node's
 * text for a key, NULL when it has none), with the record's plan as the
 * defaults. A trigger module sets the plan's trigger and its settings (and
 * leaves its rounds); use_ammo sets the rounds. Returns 1, or 0 when a
 * setting is malformed, which then keeps its default. */
typedef const char *(*weapon_rule_text_fn)(const char *key, void *user);
static inline s32 weaponRulesApplySettings(u32 module, weapon_rule_text_fn get, void *user,
	const weapon_rule_function_t *record, weapon_rule_trigger_t *plan)
{
	weapon_rule_trigger_t native;
	s32 ok = 1;

	weaponRulesNativeTrigger(record, &native);
	if (module == WEAPON_MODULE_USE_AMMO) {
		const s32 rounds = weaponRulesWholeFromText(get(WEAPON_RULE_ROUNDS_PARAM, user), 1, WEAPON_RULE_MAX_ROUNDS, 1);

		plan->rounds = rounds < 0 ? 1 : rounds;
		return rounds >= 0;
	}
	if (!(module & WEAPON_MODULE_TRIGGERS) || (module & (module - 1))) {
		return 0;
	}
	plan->module = module;
	plan->count = plan->gap = plan->spin_up = plan->spin_down = plan->burst = 0;
	plan->rate = plan->start_rate = 0.0f;
	plan->aimed_burst = 1;
	plan->charge_start = plan->charge_time = plan->charge_steps = plan->charge_ammo = 0;
	plan->charge_min_clip = plan->charge_fall = plan->charge_release = plan->cook_fuse = 0;
	plan->charge_damage = 0.0f;
	if (module == WEAPON_MODULE_BURST) {
		const s32 burst = native.module == WEAPON_MODULE_BURST;
		const s32 dcount = burst ? native.count : 3;
		const s32 dgap = burst ? native.gap : WEAPON_RULE_DEFAULT_GAP;
		const s32 count = weaponRulesWholeFromText(get(WEAPON_RULE_COUNT_PARAM, user),
			WEAPON_RULE_MIN_BURST_COUNT, WEAPON_RULE_MAX_BURST_COUNT, dcount);
		const s32 gap = weaponRulesWholeFromText(get(WEAPON_RULE_GAP_PARAM, user), 0, WEAPON_RULE_MAX_GAP, dgap);

		ok = count >= 0 && gap >= 0;
		plan->count = count >= 0 ? count : dcount;
		plan->gap = gap >= 0 ? gap : dgap;
	} else if (module == WEAPON_MODULE_CHARGE) {
		/* slice 5: the record's own charge, else the Mauler's. Each setting
		 * is parsed as soon as it is read (review: a "$name" setting's text
		 * lives in a read buffer). */
		weapon_rule_trigger_t base;
		const s32 committed = native.module == WEAPON_MODULE_CHARGE
			&& native.charge_start == WEAPON_RULE_CHARGE_ON_PRESS;
		const s32 custom = record ? record->weaponnum >= WEAPON_CUSTOM_START : -1;
		const char *damage_text;
		s32 v;

		memset(&base, 0, sizeof(base));
		if (native.module == WEAPON_MODULE_CHARGE) base = native;
		else weaponRulesMaulerCharge(&base);
		plan->charge_start = base.charge_start;
		plan->charge_time = base.charge_time;
		plan->charge_steps = base.charge_steps;
		plan->charge_ammo = base.charge_ammo;
		plan->charge_min_clip = base.charge_min_clip;
		plan->charge_fall = base.charge_fall;
		plan->charge_release = base.charge_release;
		plan->charge_damage = base.charge_damage;
		v = weaponRulesChargeStartFromText(get(WEAPON_RULE_START_PARAM, user), committed);
		if (v < 0) ok = 0; else plan->charge_start = v;
		/* A release left out is its start's (review). */
		v = weaponRulesChargeReleaseFromText(get(WEAPON_RULE_RELEASE_PARAM, user), plan->charge_start);
		if (v < 0) ok = 0; else plan->charge_release = v;
		if (weaponRulesChargeUnsupported(plan->charge_start, plan->charge_release, custom)) {
			/* Registration refuses it; the walk keeps a charge the runtime runs. */
			ok = 0;
			if (plan->charge_start == WEAPON_RULE_CHARGE_ON_PRESS && custom == 0) {
				plan->charge_start = WEAPON_RULE_CHARGE_WHILE_SELECTED;
			}
			plan->charge_release = weaponRulesChargeReleaseFromText(NULL, plan->charge_start);
		}
		v = weaponRulesWholeFromText(get(WEAPON_RULE_TIME_PARAM, user), 1, WEAPON_RULE_MAX_SPIN, plan->charge_time);
		if (v < 0) ok = 0; else plan->charge_time = v;
		v = weaponRulesWholeFromText(get(WEAPON_RULE_STEPS_PARAM, user), 1, 50, plan->charge_steps);
		if (v < 0) ok = 0; else plan->charge_steps = v;
		v = weaponRulesWholeFromText(get(WEAPON_RULE_AMMO_PER_STEP_PARAM, user), 0, WEAPON_RULE_MAX_ROUNDS,
			plan->charge_ammo);
		if (v < 0) ok = 0; else plan->charge_ammo = v;
		v = weaponRulesWholeFromText(get(WEAPON_RULE_MIN_CLIP_PARAM, user), 0, WEAPON_RULE_MAX_ROUNDS,
			plan->charge_min_clip);
		if (v < 0) ok = 0; else plan->charge_min_clip = v;
		v = weaponRulesWholeFromText(get(WEAPON_RULE_FALL_TIME_PARAM, user), 0, WEAPON_RULE_MAX_SPIN, plan->charge_fall);
		if (v < 0) ok = 0; else plan->charge_fall = v;
		damage_text = get(WEAPON_RULE_DAMAGE_PER_STEP_PARAM, user);
		if (damage_text) {
			if (strcmp(damage_text, "0") == 0) {
				plan->charge_damage = 0.0f;
			} else {
				const f32 d = weaponRulesNumberFromText(damage_text, 100.0f, -1.0f);

				if (d > 0.0f) plan->charge_damage = d;
				else ok = 0;
			}
		}
	} else if (module == WEAPON_MODULE_COOK) {
		const s32 dfuse = native.module == WEAPON_MODULE_COOK && native.cook_fuse > 0 ? native.cook_fuse
			: (record && record->activation_time > 0 ? record->activation_time : 240);
		const s32 fuse = weaponRulesWholeFromText(get(WEAPON_RULE_FUSE_PARAM, user), 1, WEAPON_RULE_MAX_SPIN, dfuse);

		ok = fuse >= 0;
		plan->cook_fuse = fuse >= 0 ? fuse : dfuse;
	} else if (module == WEAPON_MODULE_AUTOMATIC) {
		const s32 automatic = native.module == WEAPON_MODULE_AUTOMATIC;
		const char *rate_text = get(WEAPON_RULE_RATE_PARAM, user);
		const f32 drate = automatic && native.rate > 0.0f ? native.rate : WEAPON_RULE_DEFAULT_RATE;
		const f32 rate = weaponRulesRateFromText(rate_text, drate);
		/* A start rate left out is the record's when the rate is too, else
		 * the rate (no spin from a rate the record never had). */
		const f32 dstart = !rate_text && automatic && native.start_rate > 0.0f ? native.start_rate
			: (rate > 0.0f ? rate : drate);
		const f32 start = weaponRulesRateFromText(get(WEAPON_RULE_START_RATE_PARAM, user), dstart);
		const s32 dup = automatic ? native.spin_up : 0;
		const s32 ddown = automatic ? native.spin_down : 0;
		const s32 dburst = automatic ? native.burst : 0;
		const s32 daimed = automatic ? native.aimed_burst : 1;
		const s32 up = weaponRulesWholeFromText(get(WEAPON_RULE_SPIN_UP_PARAM, user), 0, WEAPON_RULE_MAX_SPIN, dup);
		const s32 down = weaponRulesWholeFromText(get(WEAPON_RULE_SPIN_DOWN_PARAM, user), 0, WEAPON_RULE_MAX_SPIN, ddown);
		const s32 burst = weaponRulesWholeFromText(get(WEAPON_RULE_BURST_PARAM, user), 0, WEAPON_RULE_MAX_AUTO_BURST, dburst);
		const char *aimed_text = get(WEAPON_RULE_AIMED_BURST_PARAM, user);
		const s32 aimed = aimed_text ? weaponRulesBoolFromText(aimed_text) : daimed;

		ok = rate > 0.0f && start > 0.0f && up >= 0 && down >= 0 && burst >= 0 && aimed >= 0;
		plan->rate = rate > 0.0f ? rate : drate;
		plan->start_rate = start > 0.0f ? start : dstart;
		plan->spin_up = up >= 0 ? up : dup;
		plan->spin_down = down >= 0 ? down : ddown;
		plan->burst = burst >= 0 ? burst : dburst;
		plan->aimed_burst = aimed >= 0 ? aimed : daimed;
	}
	return ok;
}

static inline s32 weaponRulesTriggerEqual(const weapon_rule_trigger_t *a, const weapon_rule_trigger_t *b)
{
	return a->module == b->module && a->count == b->count && a->gap == b->gap
		&& a->rate == b->rate && a->start_rate == b->start_rate
		&& a->spin_up == b->spin_up && a->spin_down == b->spin_down
		&& a->burst == b->burst && a->aimed_burst == b->aimed_burst && a->rounds == b->rounds
		&& a->shot == b->shot && a->pellets == b->pellets && a->through_walls == b->through_walls
		&& a->shield_damage == b->shield_damage && a->range == b->range && a->item == b->item
		&& a->item_function == b->item_function && a->give_up_gun == b->give_up_gun
		&& a->action == b->action && a->mode == b->mode && a->drain == b->drain
		&& a->charge_start == b->charge_start && a->charge_time == b->charge_time
		&& a->charge_steps == b->charge_steps && a->charge_ammo == b->charge_ammo
		&& a->charge_min_clip == b->charge_min_clip && a->charge_fall == b->charge_fall
		&& a->charge_release == b->charge_release && a->charge_damage == b->charge_damage
		&& a->cook_fuse == b->cook_fuse;
}

/* A catalog id's weapon number, -1 for none (the game reads its weapon
 * table; pd-tests pass their own). */
typedef s32 (*weapon_rule_item_fn)(const char *catalog_id);

/*
 * The settings of one shot module (slice 4), read by `get` on top of the
 * function's own shot: a setting left out is the record's when the module is
 * the record's own shot, else the module's default. The shot fields of
 * `plan` are set; its trigger and rounds are left. Returns 1, or 0 when a
 * setting is malformed (it then keeps its default). An item that names no
 * weapon is WEAPON_RULE_ITEM_UNKNOWN.
 */
static inline s32 weaponRulesApplyShotSettings(u32 module, weapon_rule_text_fn get, void *user,
	weapon_rule_item_fn item_number, const weapon_rule_function_t *record, weapon_rule_trigger_t *plan)
{
	weapon_rule_trigger_t native;
	s32 same;
	s32 ok = 1;

	if (!(module & WEAPON_MODULE_SHOTS) || (module & (module - 1))) {
		return 0;
	}
	memset(&native, 0, sizeof(native));
	weaponRulesNativeShot(record, &native);
	same = native.shot == module;
	plan->shot = module;
	plan->pellets = 1;
	plan->through_walls = 0;
	plan->shield_damage = 1.0f;
	plan->range = 0.0f;
	plan->item = WEAPON_RULE_ITEM_OWN;
	plan->item_function = WEAPON_RULE_ITEM_FUNCTION_KEEP;
	plan->give_up_gun = 0;
	plan->action = 0;
	plan->mode = 0;
	plan->drain = 0.0f;
	switch (module) {
	case WEAPON_MODULE_HITSCAN: {
		const s32 dpellets = same ? native.pellets : 1;
		const s32 dwalls = same ? native.through_walls : 0;
		const f32 dshield = same ? native.shield_damage : 1.0f;
		const s32 pellets = weaponRulesWholeFromText(get(WEAPON_RULE_PELLETS_PARAM, user), 1,
			WEAPON_RULE_MAX_PELLETS, dpellets);
		const char *walls_text = get(WEAPON_RULE_THROUGH_WALLS_PARAM, user);
		const s32 walls = walls_text ? weaponRulesBoolFromText(walls_text) : dwalls;
		const f32 shield = weaponRulesNumberFromText(get(WEAPON_RULE_SHIELD_DAMAGE_PARAM, user),
			(f32)WEAPON_RULE_MAX_SHIELD_DAMAGE, dshield);

		ok = pellets >= 0 && walls >= 0 && shield > 0.0f;
		plan->pellets = pellets >= 0 ? pellets : dpellets;
		plan->through_walls = walls >= 0 ? walls : dwalls;
		plan->shield_damage = shield > 0.0f ? shield : dshield;
		break;
	}
	case WEAPON_MODULE_BEAM: {
		const s32 drange = same && native.range > 0.0f ? (s32)native.range : WEAPON_RULE_DEFAULT_LASER_RANGE;
		const s32 range = weaponRulesWholeFromText(get(WEAPON_RULE_RANGE_PARAM, user), 1, WEAPON_RULE_MAX_RANGE,
			drange);

		ok = range >= 0;
		plan->range = (f32)(range >= 0 ? range : drange);
		break;
	}
	case WEAPON_MODULE_LAUNCH: {
		const char *item_text = get(WEAPON_RULE_ITEM_PARAM, user);
		const char *function_text = get(WEAPON_RULE_ITEM_FUNCTION_PARAM, user);

		plan->item = same ? native.item : WEAPON_RULE_ITEM_OWN;
		plan->item_function = same ? native.item_function : WEAPON_RULE_ITEM_FUNCTION_KEEP;
		if (item_text) {
			if (strcmp(item_text, "own") == 0) {
				plan->item = WEAPON_RULE_ITEM_OWN;
			} else if (weaponRulesItemIdValid(item_text)) {
				const s32 number = item_number ? item_number(item_text) : -1;

				plan->item = number >= 0 ? number : WEAPON_RULE_ITEM_UNKNOWN;
			} else {
				ok = 0;
			}
		}
		if (function_text) {
			const s32 f = weaponRulesItemFunctionFromText(function_text);

			if (f == -3) ok = 0;
			else plan->item_function = f;
		}
		break;
	}
	case WEAPON_MODULE_THROW: {
		const char *text = get(WEAPON_RULE_GIVE_UP_GUN_PARAM, user);
		const s32 give = text ? weaponRulesBoolFromText(text) : (same ? native.give_up_gun : 0);

		ok = give >= 0;
		plan->give_up_gun = give >= 0 ? give : (same ? native.give_up_gun : 0);
		break;
	}
	case WEAPON_MODULE_SPECIAL: {
		const char *text = get(WEAPON_RULE_ACTION_PARAM, user);
		const s32 daction = same ? native.action
			: (record && record->specialfunc != WEAPON_RULE_ATTACK_CLOAK ? record->specialfunc : 0);
		const s32 action = text ? weaponRulesActionFromText(text) : daction;

		ok = action >= 0;
		plan->action = action >= 0 ? action : daction;
		break;
	}
	case WEAPON_MODULE_MODE_TOGGLE: {
		const char *mode_text = get(WEAPON_RULE_MODE_PARAM, user);
		const s32 mode = mode_text ? weaponRulesModeFromText(mode_text) : WEAPON_RULE_MODE_CLOAK;
		const f32 ddrain = same && native.drain > 0.0f ? native.drain : WEAPON_RULE_DEFAULT_CLOAK_DRAIN;
		const f32 drain = weaponRulesNumberFromText(get(WEAPON_RULE_DRAIN_PARAM, user), WEAPON_RULE_MAX_RATE, ddrain);

		ok = mode > 0 && drain > 0.0f;
		plan->mode = mode > 0 ? mode : WEAPON_RULE_MODE_CLOAK;
		plan->drain = drain > 0.0f ? drain : ddrain;
		break;
	}
	}
	return ok;
}

/* A throw, a special or a strike takes its rounds from the clip; a clip
 * that holds fewer is emptied (bondgun.c's original for each: a throw and a
 * special take 1, the Tranquilizer's strike 4). */
static inline void weaponRulesSpendRounds(s32 rounds, s32 *loaded)
{
	if (rounds <= 0 || !loaded) return;
	if (*loaded > rounds) *loaded -= rounds;
	else *loaded = 0;
}

/* og.weapon.reload's settings (slice 4b): its style and a regrow's time,
 * defaults the record's. Returns 1, or 0 when one is malformed (it then
 * keeps its default). */
static inline s32 weaponRulesApplyReloadSettings(weapon_rule_text_fn get, void *user,
	const weapon_rule_function_t *record, weapon_rule_reload_t *out)
{
	weapon_rule_reload_t native;
	const char *style_text;
	s32 style;
	s32 grow;

	weaponRulesNativeReload(record, &native);
	style_text = get(WEAPON_RULE_STYLE_PARAM, user);
	style = style_text ? weaponRulesStyleFromText(style_text) : native.style;
	grow = weaponRulesWholeFromText(get(WEAPON_RULE_REGROW_TIME_PARAM, user), 1, WEAPON_RULE_MAX_SPIN,
		native.regrow_time);
	out->style = style > 0 ? style : native.style;
	out->regrow_time = grow > 0 ? grow : native.regrow_time;
	return style > 0 && grow > 0;
}

/*
 * The hand's cadence as the trigger reads and moves it each tick of the
 * attack (bondgun.c copies these from the hand and back).
 */
typedef struct weapon_rule_cadence {
	s32 triggeron;    /* the trigger is down */
	s32 fired;        /* the attack has fired a round (HANDSTATEFLAG_00000010) */
	s32 aiming;       /* the player aims (insightaimmode) */
	s32 burstbullets; /* rounds the attack has fired */
	s32 stateframes;  /* ticks since the burst's last round; the step resets it */
	f32 spin;         /* an automatic's speed, 0 stopped to 1 at rate */
	s32 clip_empty;   /* the clip cannot pay for a shot */
	f32 lvupdate60;   /* this tick's length in 60 Hz ticks */
} weapon_rule_cadence_t;

/* The clip cannot pay for a shot: the function has a clip with an ammo type,
 * the attack takes rounds, and fewer than a shot's are loaded. */
static inline s32 weaponRulesClipEmpty(const weapon_rule_trigger_t *plan, s32 has_clip, s32 loaded)
{
	return has_clip && plan && plan->rounds > 0 && loaded < plan->rounds;
}

/*
 * One tick of the attack's trigger (bgun0f09a3f8): -1 the attack stops, 0
 * it waits (between a burst's rounds), 1 it fires and goes on, 2 it fires
 * its last round. A burst goes on after the trigger is released; an
 * automatic fires while it is held (and through its burst), speeding up
 * over spin_up ticks and, released, slowing over spin_down.
 */
static inline s32 weaponRulesCadenceStep(const weapon_rule_trigger_t *plan, weapon_rule_cadence_t *c)
{
	const s32 automatic = plan->module == WEAPON_MODULE_AUTOMATIC;
	s32 burst = 0;
	s32 smallburst = 0;

	if (automatic) {
		burst = plan->burst > 0 && c->burstbullets < plan->burst && (plan->aimed_burst || !c->aiming);
	} else if (plan->module == WEAPON_MODULE_BURST) {
		smallburst = c->burstbullets < plan->count;
		burst = smallburst;
	}
	if (c->triggeron || !c->fired || burst) {
		if (c->clip_empty) {
			return -1;
		}
		if (automatic) {
			if (plan->spin_up > 0) {
				if (c->spin < 1) {
					c->spin += c->lvupdate60 / plan->spin_up;
					if (c->spin > 1) {
						c->spin = 1;
						return 1;
					}
				}
			} else {
				c->spin = 1;
			}
			return 1;
		}
		c->spin = 1;
		if (smallburst) {
			if (c->burstbullets > 0 && c->stateframes < plan->gap) {
				return 0;
			}
			c->stateframes = 0;
			if (c->burstbullets == plan->count - 1) {
				smallburst = 0; /* the burst's last round */
			}
		}
		return smallburst ? 1 : 2;
	}
	if (automatic) {
		if (plan->spin_down > 0) {
			if (c->spin > 0) {
				c->spin -= c->lvupdate60 / plan->spin_down;
				if (c->spin < 0) {
					c->spin = 0;
					return -1;
				}
				return 1;
			}
		} else {
			c->spin = 0;
		}
		return -1;
	}
	return -1;
}

/* The rounds per minute an automatic fires at, at its spin (bgun0f09a6f8). */
static inline f32 weaponRulesRateAt(const weapon_rule_trigger_t *plan, f32 spin)
{
	return plan->start_rate + (plan->rate - plan->start_rate) * spin;
}

/* The shots the clip pays for, of `shots` wanted, each `rounds` from
 * *loaded (bgun0f09a6f8): all of them, or as many as it holds. A shot that
 * takes no rounds is free. */
static inline s32 weaponRulesPayShots(s32 rounds, s32 *loaded, s32 shots)
{
	s32 cost;

	if (rounds <= 0 || shots <= 0) {
		return shots;
	}
	cost = shots * rounds;
	if (*loaded >= cost) {
		*loaded -= cost;
		return shots;
	}
	shots = *loaded > 0 ? *loaded / rounds : 0;
	*loaded -= shots * rounds;
	if (*loaded < 0) *loaded = 0;
	return shots;
}

/* The ticks between a simulant's or a guard's shots (chrTickShoot): an
 * automatic's rate as whole 60 Hz ticks, 0 for one round per decision
 * (weaponGetNumTicksPerShot gives the same for its plan). At most 127, the
 * most the callers' byte holds. */
static inline s32 weaponRulesTicksPerShot(const weapon_rule_trigger_t *plan)
{
	s32 ticks;

	if (!plan || plan->module != WEAPON_MODULE_AUTOMATIC || !(plan->rate > 0.0f)) return 0;
	ticks = (s32)(3600.0f / plan->rate);
	if (ticks > 3) ticks = TICKS(ticks);
	return ticks > 127 ? 127 : ticks;
}

/*
 * One walk of a held program for one weapon event, shared by the game's
 * seams (weapon_events.c) and pd-tests, so what a program chooses is
 * decided by one piece of code:
 *
 * - the gates read `hand` (a snapshot the seam takes before the walk: the
 *   walk spends nothing, so it stays true) and the named states;
 * - og.weapon.set_state writes a named state as the walk reaches it, so a
 *   later gate of the same walk sees it;
 * - the first trigger reached plans the attack with its settings, and
 *   og.weapon.use_ammo names the rounds a shot takes (0 without one); a
 *   later trigger of the same walk is chosen too but plans nothing;
 * - every setting is read through `param_text`, which resolves a "$name"
 *   setting from the weapon's variables (weaponGraphRuntimeHeldParamText).
 *
 * A walk for a simulant or a guard passes no states (`states` NULL): every
 * state is false and set_state writes nothing.
 */
typedef struct weapon_rule_states {
	s32 count;
	char keys[WEAPON_RULE_MAX_STATES][WEAPON_RULE_STATE_KEY_LEN];
	u8 values[WEAPON_RULE_MAX_STATES];
} weapon_rule_states_t;

static inline s32 weaponRulesStateIndex(const weapon_rule_states_t *states, const char *key)
{
	if (!states || !key) return -1;
	for (s32 i = 0; i < states->count; i++) {
		if (strcmp(states->keys[i], key) == 0) return i;
	}
	return -1;
}

typedef const char *(*weapon_rule_param_fn)(s32 weaponnum, const mission_graph_params_t *params,
	const char *key, char *buf, size_t cap);

typedef struct weapon_rule_walk {
	s32 weaponnum;
	weapon_rule_hand_t hand;                /* what the gates read */
	weapon_rule_states_t *states;           /* the hand's named states; NULL: all false, none written */
	const weapon_rule_function_t *function; /* the function's record: its settings' defaults */
	weapon_rule_param_fn param_text;
	weapon_rule_item_fn item_number;        /* a launch's item by catalog id; NULL: none is known */
	u32 modules;                            /* what the walk chose */
	weapon_rule_trigger_t plan;             /* the first trigger reached, with use_ammo's rounds */
	weapon_rule_reload_t reload;            /* the first reload reached (slice 4b) */
} weapon_rule_walk_t;

/* A node's settings as one module reads them: each read gets a buffer of
 * its own (a module reads at most WEAPON_RULE_SETTING_READS settings), as
 * long as an item's catalog id, so a "$name" setting read after another does
 * not overwrite it. */
#define WEAPON_RULE_SETTING_READS 12
typedef struct weapon_rule_setting_read {
	const weapon_rule_walk_t *walk;
	const mission_graph_params_t *params;
	s32 next;
	char bufs[WEAPON_RULE_SETTING_READS][WEAPON_RULE_ITEM_LEN];
} weapon_rule_setting_read_t;

static inline const char *weaponRulesWalkSettingText(const char *key, void *user)
{
	weapon_rule_setting_read_t *read = (weapon_rule_setting_read_t *)user;
	char *buf = read->bufs[read->next % WEAPON_RULE_SETTING_READS];

	read->next++;
	return read->walk->param_text(read->walk->weaponnum, read->params, key, buf, WEAPON_RULE_ITEM_LEN);
}

/* The runner's module callback for a walk (mission_graph_module_run_fn). */
static inline s32 weaponRulesWalkModule(const char *kind, const char *node_id,
	const mission_graph_params_t *params, const mission_graph_event_t *event, void *user)
{
	weapon_rule_walk_t *walk = (weapon_rule_walk_t *)user;
	const u32 bit = weaponRulesModuleBit(kind);
	const u32 gate = weaponRulesGateBit(kind);
	char buf[WEAPON_RULE_STATE_KEY_LEN];
	char vbuf[16];

	(void)node_id;
	(void)event;
	if (!walk || !kind || !walk->param_text) {
		return 0;
	}
	if (gate) {
		switch (gate) {
		case WEAPON_RULE_GATE_HAS_AMMO: {
			const s32 rounds = weaponRulesRoundsFromText(walk->param_text(walk->weaponnum, params,
				WEAPON_RULE_ROUNDS_PARAM, vbuf, sizeof(vbuf)));

			return weaponRulesHasAmmo(&walk->hand, rounds > 0 ? rounds : 1) ? 1 : 0;
		}
		case WEAPON_RULE_GATE_HAS_RESERVE:
			return weaponRulesHasReserve(&walk->hand) ? 1 : 0;
		case WEAPON_RULE_GATE_READY_TO_FIRE:
			return walk->hand.idle ? 1 : 0;
		case WEAPON_RULE_GATE_STATE_IS: {
			const char *key = walk->param_text(walk->weaponnum, params, WEAPON_RULE_KEY_PARAM, buf, sizeof(buf));
			const s32 equals = weaponRulesBoolFromText(walk->param_text(walk->weaponnum, params,
				WEAPON_RULE_EQUALS_PARAM, vbuf, sizeof(vbuf)));
			const s32 at = weaponRulesStateIndex(walk->states, key);
			const s32 value = at >= 0 ? walk->states->values[at] : 0;

			return equals >= 0 && value == equals ? 1 : 0;
		}
		}
		return 0;
	}
	if (bit == WEAPON_MODULE_SET_STATE) {
		const char *key = walk->param_text(walk->weaponnum, params, WEAPON_RULE_KEY_PARAM, buf, sizeof(buf));
		const s32 value = weaponRulesBoolFromText(walk->param_text(walk->weaponnum, params,
			WEAPON_RULE_VALUE_PARAM, vbuf, sizeof(vbuf)));
		s32 at;

		/* Registration refused a node without a key or a boolean value and a
		 * program with more keys than the table holds. */
		if (!key || !key[0] || strlen(key) >= WEAPON_RULE_STATE_KEY_LEN || value < 0) {
			return 0;
		}
		if (walk->states) {
			at = weaponRulesStateIndex(walk->states, key);
			if (at < 0) {
				if (walk->states->count >= WEAPON_RULE_MAX_STATES) return 0;
				at = walk->states->count++;
				memcpy(walk->states->keys[at], key, strlen(key) + 1);
			}
			walk->states->values[at] = (u8)value;
		}
		walk->modules |= bit;
		return 1;
	}
	if (bit == WEAPON_MODULE_RELOAD) {
		/* slice 4b: the first reload reached is the hand's. */
		if (!(walk->modules & WEAPON_MODULE_RELOAD)) {
			weapon_rule_setting_read_t read;

			read.walk = walk;
			read.params = params;
			read.next = 0;
			weaponRulesApplyReloadSettings(weaponRulesWalkSettingText, &read, walk->function, &walk->reload);
		}
		walk->modules |= bit;
		return 1;
	}
	if (bit & WEAPON_MODULE_SHOTS) {
		/* slice 4: the first shot reached is the attack's. */
		if (!(walk->modules & WEAPON_MODULE_SHOTS)) {
			weapon_rule_setting_read_t read;

			read.walk = walk;
			read.params = params;
			read.next = 0;
			weaponRulesApplyShotSettings(bit, weaponRulesWalkSettingText, &read, walk->item_number,
				walk->function, &walk->plan);
		}
		walk->modules |= bit;
		return 1;
	}
	if (bit & (WEAPON_MODULE_TRIGGERS | WEAPON_MODULE_USE_AMMO)) {
		const u32 first = (bit & WEAPON_MODULE_TRIGGERS) ? WEAPON_MODULE_TRIGGERS : WEAPON_MODULE_USE_AMMO;

		if (!(walk->modules & first)) {
			weapon_rule_setting_read_t read;

			read.walk = walk;
			read.params = params;
			read.next = 0;
			weaponRulesApplySettings(bit, weaponRulesWalkSettingText, &read, walk->function, &walk->plan);
		}
		walk->modules |= bit;
		return 1;
	}
	if (bit) {
		walk->modules |= bit;
		return 1;
	}
	return 0;
}

/* Walks one event of `program` for `walk`: 1 when the program handles the
 * event (walk->modules and walk->plan then say what it chose; a module that
 * refused chooses nothing), 0 when it does not. instance must not be NULL
 * (the runner refuses a weapon event without one). */
static inline s32 weaponRulesWalk(const mission_graph_program_t *program, s32 event_kind, s32 player,
	const char *catalog_id, void *instance, weapon_rule_walk_t *walk)
{
	mission_graph_event_t event;

	walk->modules = 0;
	memset(&walk->plan, 0, sizeof(walk->plan));
	memset(&walk->reload, 0, sizeof(walk->reload));
	walk->plan.aimed_burst = 1;
	if (!program || !missionGraphProgramHandlesEvent(program, event_kind)) {
		return 0;
	}
	memset(&event, 0, sizeof(event));
	event.kind = event_kind;
	event.player = player;
	event.catalog_id = catalog_id;
	event.instance = instance;
	if (missionGraphProgramFire(program, &event, weaponRulesWalkModule, walk) < 0) {
		walk->modules = 0;
	}
	walk->modules &= weaponRulesEventModules(event_kind);
	if (!(walk->modules & WEAPON_MODULE_RELOAD)) {
		memset(&walk->reload, 0, sizeof(walk->reload));
	}
	if (!(walk->modules & WEAPON_MODULE_TRIGGERS)) {
		memset(&walk->plan, 0, sizeof(walk->plan));
		walk->plan.aimed_burst = 1;
	} else {
		if (!(walk->modules & WEAPON_MODULE_USE_AMMO)) {
			walk->plan.rounds = 0;
		}
		if (!(walk->modules & WEAPON_MODULE_SHOTS)) {
			/* A shot left out is the function's own. */
			weaponRulesNativeShot(walk->function, &walk->plan);
		}
	}
	return 1;
}

/* The hand a simulant or a guard walks a press with: ready, its function
 * spending from no clip (has_ammo and has_reserve pass). */
static inline void weaponRulesReadyHand(weapon_rule_hand_t *hand)
{
	memset(hand, 0, sizeof(*hand));
	hand->idle = 1;
}

/* The native ammunition check (bgun0f098ca0) of a hand that holds a
 * function, from the same reads the gates make: -1 nothing to fire or reload,
 * 0 below `rounds` with reserve, 1 or 2 can fire (2 with no reserve), 3 a
 * full clip or no ammunition. pd-tests compare the base wiring with the
 * table through it. */
static inline s32 weaponRulesNativeAmmoStatus(const weapon_rule_hand_t *hand, s32 rounds)
{
	if (!hand || !hand->uses_ammo || hand->loaded >= hand->clipsize) return 3;
	if (hand->loaded < rounds) return hand->reserve ? 0 : -1;
	return hand->reserve ? 1 : 2;
}

#ifdef __cplusplus
}
#endif

#endif
