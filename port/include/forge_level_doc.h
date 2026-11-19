/**
 * forge_level_doc.h -- the placements of a level, as Forge edits them.
 *
 * A level is a .pdarena with its .pdscenario inside. These members hold its
 * placements and stage settings:
 *
 *   pads.json          pd2.scenario.pads.v1          pad position, orientation
 *                                                    and navigation flags
 *   volumes.json       pd2.scenario.volumes.v1       pad bounds and authored
 *                                                    trigger boxes
 *   spawns.json        pd2.scenario.spawns.v1        where players start: a
 *                                                    pad each
 *   intro.json         pd2.scenario.intro.v1         optional stage intro,
 *                                                    including mode markers
 *   objects.json       pd2.scenario.objects.v1       props, weapons and the
 *                                                    rest: a pad each, or none
 *   setup.fields.json  pd2.scenario.setup.fields.v1  every field of every
 *                                                    object, one row a field
 *   level.graph.json   pd2.level.graph.v1            the level's graph: among
 *                                                    much else, one node per
 *                                                    bounds row, and counts
 *   stage.json         pd2.scenario.stage.v1         optional stage settings
 *   navigation/paths.json pd2.scenario.paths.v1      offline bot route links
 *
 * This module reads those texts, lets a caller add, move and remove
 * spawns, move, duplicate and leave out objects, make a weapon place or an
 * ammo crate where there is none to copy, change what a weapon pickup gives
 * and which model a prop is drawn with, and gives the texts back for saving.
 * It holds no game state and touches no file: text in, text out. Forge's
 * editor fills it from the level's archive and hands the changed members to
 * the mod author (context/designs/modding/forge-mode-scope.md, step F2).
 *
 * Rules it keeps:
 *
 * - Rows keep their bytes. A row the caller did not change is written back
 *   exactly as it was read, with every key this module does not know. A
 *   document with no edit gives back the texts it was given.
 * - Nothing is repaired. A text that is not its schema, a row with no id, a
 *   repeated id, a pad out of sequence, a spawn, object or volume naming a
 *   pad the level does not have, or the two object files disagreeing about
 *   where an object stands is refused with the reason.
 * - Pad identities are append-only. The game numbers pads by their place in the
 *   file (pad_0000 is the first row), other members name pads too (the
 *   navigation tables, other fields of objects), and 299 pads of the base
 *   levels are shared by more than one spawn or object. So moving something
 *   that stands on a pad from the template gives it a new pad and leaves the
 *   old one where it is; only a pad this document added is moved in place.
 *   Required waypoints change their flag. Connecting pads adds a native path
 *   row, and a pad referenced by a path stays in place when a placement moves.
 * - A new pad comes with its bounds row, as every pad of a base level has
 *   one, and a moved pad's bounds move with it. A pad used by an authored
 *   trigger stays anchored when a start or object is moved off it.
 * - The level's graph keeps step with the bounds rows. When it loads a level
 *   the game counts the graph's trigger volume nodes (the text `"kind":
 *   "scenario.trigger.volume.source"`) and refuses the level unless that is
 *   the number of rows in volumes.json. So a new bounds row comes with its
 *   node and the link the extractor gives every such node, and a level whose
 *   two counts already differ is refused here as the game would refuse it.
 *   A trigger adds a separate box, its source node and an executable
 *   volume-entered rule. The graph's pad, volume, object and path counts follow
 *   the tables when they were true to begin with. Unrelated graph rows keep
 *   their bytes.
 * - Ids are not handed out twice. A new spawn takes the number above the
 *   highest spawn number the document has ever held.
 * - An object's place is written twice, in objects.json (`pad_ref`) and in
 *   setup.fields.json (`base.pad`, or `character.pad` for a character). Both
 *   follow a move.
 * - Objects are only ever added at the end. A record's id number is its
 *   place in the level's command list (setup_0012 is the thirteenth
 *   command), and the game turns a reference from one record to another into
 *   the distance between them. A new object takes the next number and
 *   disturbs nothing. Taking one out of the middle would shift every later
 *   record under the references that point at them, so a record is never
 *   deleted.
 * - An object is removed by leaving it out. An object's second flags word
 *   says on which of the four difficulties the level leaves it out; base
 *   levels use that to thin a level for one difficulty, and the game's setup
 *   loop then never creates the object while its record stays in place for
 *   whatever refers to it. Removing an object here sets all four, in both
 *   files that hold the word (`flags2` in objects.json, `base.flags2` in
 *   setup.fields.json); putting it back restores the bits it had. No base
 *   level leaves an object out on all four, so this is a state the game's
 *   code allows for but its levels have not used.
 * - What an object is drawn with and what a pickup gives are written twice
 *   too (objects.json's `model_catalog_id` and `weapon_catalog_id`, and the
 *   `base.model` and `pickup.weapon` rows of setup.fields.json, from which
 *   the game builds the object). They are changed only where the two files
 *   agreed when read (64 of the 1,905 `pickup.weapon` rows of the base
 *   levels do not), and always in both.
 *
 * Saving goes through the archive rebuild, which brings the scenario's
 * navigation record (its count of pads and volumes and its hash of each of
 * these files) up to date; the game refuses a level whose record does not
 * match its files.
 */
#ifndef _IN_FORGE_LEVEL_DOC_H
#define _IN_FORGE_LEVEL_DOC_H

#include <PR/ultratypes.h>
#include <stddef.h>
#include <stdio.h>
#include "asset_archive_clone.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct forge_level_doc forge_level_doc_t;

enum {
	FORGE_LEVEL_MEMBER_PADS = 0,
	FORGE_LEVEL_MEMBER_SPAWNS,
	FORGE_LEVEL_MEMBER_OBJECTS,
	FORGE_LEVEL_MEMBER_VOLUMES,
	FORGE_LEVEL_MEMBER_SETUP_FIELDS,
	FORGE_LEVEL_MEMBER_LEVEL_GRAPH,
	FORGE_LEVEL_MEMBER_INTRO,
	FORGE_LEVEL_MEMBER_STAGE,
	FORGE_LEVEL_MEMBER_COVERS,
	FORGE_LEVEL_MEMBER_PATHS,
	FORGE_LEVEL_MEMBER_ROOMS,
	FORGE_LEVEL_MEMBER_COUNT
};

/*
 * The most pads, and the highest spawn and object number, a level made here
 * may have. The game reads a pad number into 15 bits, but the archive
 * conformance rules want a spawn's pad and a spawn's id written with exactly
 * four digits (pad_0000 to pad_9999, spawn_0000 to spawn_9999), and every id
 * of the base levels has four. The largest base level has 1,384 pads and
 * 600 objects.
 */
#define FORGE_LEVEL_MAX_PADS 10000
#define FORGE_LEVEL_MAX_SPAWN_NUMBER 9999
#define FORGE_LEVEL_MAX_OBJECT_NUMBER 9999

/* Member texts indexed by FORGE_LEVEL_MEMBER_*. Optional members may be absent. */
typedef struct forge_level_texts {
	const char *text[FORGE_LEVEL_MEMBER_COUNT];
	size_t size[FORGE_LEVEL_MEMBER_COUNT];
} forge_level_texts_t;

typedef struct forge_level_pad {
	char pad_ref[32];
	char room_ref[32];
	f32 position[3];
	f32 up[3];
	f32 look[3];
	s32 added;            /* 1 when this document added the pad */
	s32 has_volume;       /* 1 when volumes.json has a bounds row for it */
	s32 walk_direct;      /* 1 when bots should follow this pad on their route */
} forge_level_pad_t;

typedef struct forge_level_spawn {
	char spawn_id[32];
	char pad_ref[32];
	char team[32];
} forge_level_spawn_t;

typedef struct forge_level_object {
	char record_id[32];
	char kind[32];
	char pad_ref[32];     /* empty for an object that stands on no pad */
	char model_catalog_id[64];
	char weapon_catalog_id[64];
	s32 order;            /* its place in the command list, or -1 when no field row says */
	s32 movable;          /* 1 when it stands on a pad and its pad field is known */
	s32 removable;        /* 1 when the level can leave it out (forgeLevelDocSetObjectRemoved) */
	s32 removed;          /* 1 when the level leaves it out on every difficulty */
	s32 weapon_slot;      /* 0 to 15 when it is one of the match's weapon places, else -1 */
	s32 model_settable;   /* 1 when both object files give its model and agree */
	s32 weapon_settable;  /* 1 for a weapon pickup whose weapon both files give and agree about,
	                       * and whose model and size can follow (forgeLevelDocSetPickupWeapon) */
} forge_level_object_t;

/*
 * Reads the level members. Returns NULL with the reason in err when any of
 * them breaks a rule above. The texts are copied; the caller keeps its own.
 */
forge_level_doc_t *forgeLevelDocRead(const forge_level_texts_t *texts, char *err, size_t err_cap);

void forgeLevelDocFree(forge_level_doc_t *doc);

s32 forgeLevelDocMemberOptional(s32 member);
struct scenario_stage_atmosphere;
s32 forgeLevelDocGetAtmosphere(const forge_level_doc_t *doc, struct scenario_stage_atmosphere *out);
s32 forgeLevelDocSetAtmosphere(forge_level_doc_t *doc, const struct scenario_stage_atmosphere *look,
		char *err, size_t err_cap);

typedef struct forge_level_room_brightness {
	s32 roomnum;
	s32 minimum;
	s32 maximum;
	s32 has_base_override; /* the native room rule can determine the base light */
} forge_level_room_brightness_t;

/* Authored, bounded rooms in source order. Absent brightness uses 255, 255. */
s32 forgeLevelDocRoomBrightnessCount(const forge_level_doc_t *doc);
s32 forgeLevelDocGetRoomBrightness(const forge_level_doc_t *doc, s32 index, forge_level_room_brightness_t *out);
s32 forgeLevelDocSetRoomBrightness(forge_level_doc_t *doc, s32 roomnum, s32 minimum, s32 maximum,
		char *err, size_t err_cap);

/* The startup override uses SCENARIO_STAGE_WEATHER_START_* from scenario_stage_rules.h.
 * NONE with intensity 0 removes the override and keeps the stage's weather rules. */
s32 forgeLevelDocGetWeatherStart(const forge_level_doc_t *doc, s32 *kind, s32 *intensity);
s32 forgeLevelDocSetWeatherStart(forge_level_doc_t *doc, s32 kind, s32 intensity,
		char *err, size_t err_cap);

/* Places or removes a King of the Hill marker at an existing pad. */
s32 forgeLevelDocHasHill(const forge_level_doc_t *doc, const char *pad_ref);
s32 forgeLevelDocHillCount(const forge_level_doc_t *doc);
s32 forgeLevelDocGetHillPad(const forge_level_doc_t *doc, s32 index, char *out, size_t cap);
s32 forgeLevelDocSetHill(forge_level_doc_t *doc, const char *pad_ref, s32 present,
		char *err, size_t err_cap);

/* Intro case rows in authored order. Capture the Case uses the team field as
 * a base slot. Hacker Central and Hold the Briefcase use both kinds as a pad bank. */
typedef struct forge_level_case_marker {
	char pad_ref[32];
	s32 team;
	s32 respawn;
} forge_level_case_marker_t;

s32 forgeLevelDocCaseMarkerCount(const forge_level_doc_t *doc);
s32 forgeLevelDocGetCaseMarker(const forge_level_doc_t *doc, s32 index, forge_level_case_marker_t *out);
/* Replace the base slot's effective home in place, or add one at an existing pad. */
s32 forgeLevelDocSetCaseHome(forge_level_doc_t *doc, s32 team, const char *pad_ref,
		char *err, size_t err_cap);
s32 forgeLevelDocAddCaseRespawn(forge_level_doc_t *doc, s32 team, const char *pad_ref,
		char *err, size_t err_cap);
s32 forgeLevelDocRemoveCaseMarker(forge_level_doc_t *doc, s32 index, char *err, size_t err_cap);

/* A pad-centered box with a native action for the player who enters it. */
#define FORGE_LEVEL_TRIGGER_MIN_HALF_SIZE 8
#define FORGE_LEVEL_TRIGGER_MAX_HALF_SIZE 2048
enum {
	FORGE_LEVEL_TRIGGER_OPEN_DOOR = 0,
	FORGE_LEVEL_TRIGGER_GIVE_PICKUP,
};
typedef struct forge_level_trigger {
	char volume_id[32];
	char pad_ref[32];
	char target_id[32];
	s32 action;
	s32 editable;              /* the existing bounds are a supported pad-centered box */
	f32 half_size[3];          /* meaningful when editable */
} forge_level_trigger_t;

s32 forgeLevelDocTriggerCount(const forge_level_doc_t *doc);
s32 forgeLevelDocGetTrigger(const forge_level_doc_t *doc, s32 index, forge_level_trigger_t *out);
s32 forgeLevelDocCanTriggerObject(const forge_level_doc_t *doc, s32 action, const char *record_id);
s32 forgeLevelDocAddTrigger(forge_level_doc_t *doc, s32 action, const char *pad_ref, const char *target_id,
	const f32 half_size[3], char *err, size_t err_cap);
/* Updates the anchor, target and size of an editable trigger. The action and volume ID stay fixed. */
s32 forgeLevelDocUpdateTrigger(forge_level_doc_t *doc, const char *volume_id, const char *pad_ref, const char *target_id,
	const f32 half_size[3], char *err, size_t err_cap);
s32 forgeLevelDocRemoveTrigger(forge_level_doc_t *doc, s32 index, char *err, size_t err_cap);

/* Replace the executable part of the current native level.graph.json.
 * Submit the full graph. Data nodes, structural bindings, metadata and the
 * level's native pickup/door rule paths must be retained. Returns 1 on a
 * committed edit, 0 on refusal or an unchanged program (empty error). */
s32 forgeLevelDocReplaceLogic(forge_level_doc_t *doc, const char *json, size_t size,
	char *err, size_t err_cap);

/* A Grid-owned secondary native box. Its stable ID can be used by multiple
 * Level Logic events. Fixed quick triggers use the trigger API above. */
typedef struct forge_level_logic_zone {
	char volume_id[32];
	char pad_ref[32];
	f32 half_size[3];
	s32 referenced;
} forge_level_logic_zone_t;

enum {
	FORGE_LEVEL_LOGIC_ZONE_ENTER = 0,
	FORGE_LEVEL_LOGIC_ZONE_LEAVE,
};

s32 forgeLevelDocLogicZoneCount(const forge_level_doc_t *doc);
s32 forgeLevelDocGetLogicZone(const forge_level_doc_t *doc, s32 index, forge_level_logic_zone_t *out);
s32 forgeLevelDocAddLogicZone(forge_level_doc_t *doc, const char *pad_ref, const f32 half_size[3],
	char *volume_id, size_t volume_cap, char *err, size_t err_cap);
/* Returns 0 with an empty error when the anchor and box are unchanged. */
s32 forgeLevelDocUpdateLogicZone(forge_level_doc_t *doc, const char *volume_id, const char *pad_ref,
	const f32 half_size[3], char *err, size_t err_cap);
s32 forgeLevelDocRemoveLogicZone(forge_level_doc_t *doc, const char *volume_id, char *err, size_t err_cap);
/* Add an unconnected typed event through the executable Logic validator. */
s32 forgeLevelDocAddLogicZoneEvent(forge_level_doc_t *doc, const char *volume_id, s32 when,
	char *event_id, size_t event_cap, char *err, size_t err_cap);

/* Door-only entry points used by existing clients. */
typedef struct forge_level_door_trigger {
	char volume_id[32];
	char pad_ref[32];
	char door_id[32];
} forge_level_door_trigger_t;

s32 forgeLevelDocDoorTriggerCount(const forge_level_doc_t *doc);
s32 forgeLevelDocGetDoorTrigger(const forge_level_doc_t *doc, s32 index, forge_level_door_trigger_t *out);
s32 forgeLevelDocAddDoorTrigger(forge_level_doc_t *doc, const char *pad_ref, const char *door_id,
		const f32 half_size[3], char *err, size_t err_cap);
s32 forgeLevelDocRemoveDoorTrigger(forge_level_doc_t *doc, s32 index, char *err, size_t err_cap);

/* Private recovery document. An exact source archive digest binds these
 * placement members to the geometry and other unchanged archive members. */
#define FORGE_LEVEL_CHECKPOINT_MAX_BYTES (32u * 1024u * 1024u)
s32 forgeLevelDocWriteCheckpoint(forge_level_doc_t *doc, const char *source_sha256,
		FILE *stream, char *err, size_t err_cap);
forge_level_doc_t *forgeLevelDocReadCheckpoint(const char *json, size_t size, const char *source_sha256,
		const forge_level_doc_t *opened, char *err, size_t err_cap);

/*
 * A copy of the document as it is now, edits included, that shares nothing
 * with it: what the level was opened from, what changed and every id still
 * to hand out come along, so the copy saves, edits and reports changes
 * exactly as the document would have. It is how an edit is undone (the
 * session keeps copies, forge_level_io.h). NULL for NULL or when out of
 * memory. Free it with forgeLevelDocFree.
 */
forge_level_doc_t *forgeLevelDocClone(const forge_level_doc_t *doc);

s32 forgeLevelDocPadCount(const forge_level_doc_t *doc);
s32 forgeLevelDocSpawnCount(const forge_level_doc_t *doc);
s32 forgeLevelDocObjectCount(const forge_level_doc_t *doc);
s32 forgeLevelDocVolumeCount(const forge_level_doc_t *doc);
s32 forgeLevelDocFieldRowCount(const forge_level_doc_t *doc);

/* Row `index` in file order. 1 when there is such a row. */
s32 forgeLevelDocGetPad(const forge_level_doc_t *doc, s32 index, forge_level_pad_t *out);
s32 forgeLevelDocGetSpawn(const forge_level_doc_t *doc, s32 index, forge_level_spawn_t *out);
s32 forgeLevelDocGetObject(const forge_level_doc_t *doc, s32 index, forge_level_object_t *out);

/* The size object `index` is drawn at (`base.extra_scale`; 256 is life size),
 * or 256 when its record does not say or there is no such object. */
s32 forgeLevelDocObjectExtraScale(const forge_level_doc_t *doc, s32 index);

/*
 * Gives object `record_id` the size `extra_scale` (1 to 32767; 256 is life
 * size): its `base.extra_scale` row in setup.fields.json, the only file that
 * holds it, from which setup sizes the object. Refused, with the document
 * unchanged, for an object whose record has no such row and for one of the
 * match's weapon places, which the match's weapon set sizes whatever the
 * level says (setupPlaceWeapon).
 */
s32 forgeLevelDocSetObjectScale(forge_level_doc_t *doc, const char *record_id, s32 extra_scale,
	char *err, size_t err_cap);

/* A pad by its name ("pad_0012"). 1 when the level has it. */
s32 forgeLevelDocFindPad(const forge_level_doc_t *doc, const char *pad_ref, forge_level_pad_t *out);

/* Set the existing PADFLAG_AIWALKDIRECT bit on a pad. Returns 1 when the
 * document changed and 0 when the flag is already set as requested or the
 * edit is refused. Pad geometry and all other flags are preserved. */
s32 forgeLevelDocSetPadWalkDirect(forge_level_doc_t *doc, const char *pad_ref, s32 enabled,
	char *err, size_t err_cap);

typedef struct forge_level_cover {
	char cover_ref[32];
	f32 position[3];
} forge_level_cover_t;

/* Editable authored covers in native row order. Derived covers are not listed. */
s32 forgeLevelDocCoverCount(const forge_level_doc_t *doc);
s32 forgeLevelDocGetCover(const forge_level_doc_t *doc, s32 index, forge_level_cover_t *out);

/* Copy a pad's position into the same native cover row. Returns 1 for a change. */
s32 forgeLevelDocMoveCoverToPad(forge_level_doc_t *doc, const char *cover_ref, const char *pad_ref,
	char *err, size_t err_cap);

/* Append a native cover at a pad's position and horizontal facing. This needs
 * the source cover table open, and leaves its existing cover ids unchanged. */
s32 forgeLevelDocAddCoverAtPad(forge_level_doc_t *doc, const char *pad_ref,
	char *out_cover_id, size_t out_cap, char *err, size_t err_cap);

/* Append a bidirectional link to a pad already in a native path. The caller
 * checks the two draft positions against the resident collision and room. */
s32 forgeLevelDocConnectPads(forge_level_doc_t *doc, const char *first, const char *second,
	char *out_path_id, size_t out_cap, char *err, size_t err_cap);

/* Remove only a uniquely marked Grid Connect Pads row for this pair. */
s32 forgeLevelDocCanDisconnectPads(const forge_level_doc_t *doc, const char *first, const char *second);
s32 forgeLevelDocDisconnectPads(forge_level_doc_t *doc, const char *first, const char *second,
	char *out_path_id, size_t out_cap, char *err, size_t err_cap);

/*
 * `look` turned `degrees` about the vertical axis, flat (y 0) and of unit
 * length. Positive turns right, as seen by someone facing along `look`: the
 * way the editor camera's yaw grows (a camera at yaw t faces (-sin t, 0,
 * cos t), and its right is (-cos t, 0, -sin t), which the projection probe
 * measured on screen). 1 with the result; 0 when `look` has no flat part to
 * turn (straight up or down, or not finite).
 */
s32 forgeLevelTurnLook(const f32 look[3], f32 degrees, f32 out[3]);

/*
 * Adds a spawn at a position, facing along look (any length but zero; up is
 * straight up). team is "any" when NULL or empty. The new spawn's id goes to
 * out_spawn_id. 1 on success; 0 with the reason, and the document unchanged.
 */
s32 forgeLevelDocAddSpawn(forge_level_doc_t *doc, const f32 position[3], const f32 look[3],
	const char *team, char *out_spawn_id, size_t out_cap, char *err, size_t err_cap);

/* Moves a spawn to a position and facing. */
s32 forgeLevelDocMoveSpawn(forge_level_doc_t *doc, const char *spawn_id,
	const f32 position[3], const f32 look[3], char *err, size_t err_cap);

/* Removes a spawn. Its pad and the pad's bounds stay. */
s32 forgeLevelDocRemoveSpawn(forge_level_doc_t *doc, const char *spawn_id,
	char *err, size_t err_cap);

/*
 * Moves an object that stands on a pad (`movable`) to a position and facing.
 * Both files that name its pad follow.
 */
s32 forgeLevelDocMoveObject(forge_level_doc_t *doc, const char *record_id,
	const f32 position[3], const f32 look[3], char *err, size_t err_cap);

/*
 * A copy of a movable object at a new place: the same kind and the same
 * fields, on a pad of its own, with the next id at the end of the command
 * list. This is how a level gains another weapon spot, another crate or
 * another prop. The new id goes to out_record_id. An object the level leaves
 * out is not copied: put it back first.
 */
s32 forgeLevelDocDuplicateObject(forge_level_doc_t *doc, const char *record_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap);

/* The kinds of object this module makes where there is none to copy. */
enum {
	FORGE_LEVEL_NEW_WEAPON_PLACE = 0,   /* one of the match's weapon places */
	FORGE_LEVEL_NEW_AMMO_CRATE,         /* the crate a match fills with ammunition for its weapons */
	FORGE_LEVEL_NEW_HOVERBIKE,          /* a hoverbike a player can ride */
	FORGE_LEVEL_NEW_KIND_COUNT
};

/*
 * A new object at a place, made from nothing, for a level that has no object
 * of that kind to copy: an empty level has none of any kind. It gets a pad of
 * its own and the next id at the end of the command list, as a copy does.
 *
 * Its record is the one every such object of the base levels has: the same
 * list of fields (37 for a weapon place, 68 for an ammo crate, 63 for a
 * hoverbike; each kind has exactly one list across all the base levels) with
 * the values those objects share. A weapon place is weapon place
 * `weapon_slot` (0 to 15) of the match, with the stand-in model the arenas
 * give a place until the match's weapon set gives it a weapon
 * (`base:model_roofgun`). An ammo crate is `base:model_multi_ammo_crate` with
 * its nineteen slots empty, as in every arena: the match fills it. Both fall
 * to the floor under them (flags 0x00000001). A hoverbike is
 * `base:model_hovbike` with the three flag words all five base hoverbikes
 * have (it falls to rest without turning over and takes no damage from shots
 * or explosions), its hover state at rest. All are life size (256) and can be
 * moved, copied and left out; a place can be given a weapon afterwards.
 * `weapon_slot` is ignored for a crate and a hoverbike. A hoverbike faces
 * along `look`, so a rider who mounts it drives that way.
 *
 * The model ids are the game's own, so the level names assets every install
 * has; nothing of them is in the level.
 */
s32 forgeLevelDocAddObject(forge_level_doc_t *doc, s32 kind, s32 weapon_slot,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap);

/* Add the same native hoverbike record with a selected vehicle's exact
 * embedded model. The caller resolves the typed vehicle-model binding. */
s32 forgeLevelDocAddHoverbike(forge_level_doc_t *doc, const char *model_catalog_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap);

/*
 * A plain prop (`kind` "prop") at a place, drawn with the model
 * `model_catalog_id` ("base:model_crate2_cq", or a mod's model), made from
 * nothing as forgeLevelDocAddObject makes its kinds: a pad of its own, the
 * next id. Its record is the one every plain prop of the base levels has (30
 * field rows, one list across all 2,226 of them) with the values most share:
 * life size (256), it falls to the floor under it (flags 0x00000001), no other
 * flags, 1000 damage before it breaks. It can be moved, copied, left out and
 * given another model afterwards. The id must look like a catalog id (lower-
 * case letters, digits, '_', '-', '.', one ':'); whether the catalog has that
 * model is the caller's to check. 1 with the new record's id in
 * out_record_id; 0 with the reason, and the document unchanged.
 */
s32 forgeLevelDocAddProp(forge_level_doc_t *doc, const char *model_catalog_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap);

/* A basic prop with native pass-through, stationary and non-interactive
 * flags. It uses the same model, pad and save contracts as a plain prop. */
s32 forgeLevelDocAddDecoration(forge_level_doc_t *doc, const char *model_catalog_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap);
/* Exact canonical source flags, independently of a record's references. */
s32 forgeLevelDecorationFlagsMatch(u32 flags, u32 flags2, u32 flags3);

/* A decoration must still have the exact native flags and an independent
 * pad. These queries check the current Level File, without changing it. */
s32 forgeLevelDocIsDecoration(const forge_level_doc_t *doc, const char *record_id);
s32 forgeLevelDocGetDecorationPose(const forge_level_doc_t *doc, const char *record_id,
	forge_level_pad_t *out);
s32 forgeLevelDocMoveDecoration(forge_level_doc_t *doc, const char *record_id,
	const f32 position[3], const f32 look[3], char *err, size_t err_cap);

/*
 * Leaves an object out of the level (removed = 1) or puts it back (0). The
 * record keeps its place in the command list. Only a `removable` object: one
 * whose flags word is in both files and agrees, and of a kind the game always
 * filters by it (a lift or an escalator step is kept on some levels whatever
 * its flags say, and a character has no such word). Asking for the state it
 * is already in changes nothing and succeeds. Putting back an object that
 * this document left out gives back the exact text it had.
 */
s32 forgeLevelDocSetObjectRemoved(forge_level_doc_t *doc, const char *record_id, s32 removed,
	char *err, size_t err_cap);

/*
 * What a weapon pickup gives.
 *
 * A base level writes it one of two ways, in the `pickup.weapon` row of
 * setup.fields.json. A weapon of the catalog: `type` "weapon_catalog_id" with
 * the id in `catalog_id`, and the same id in objects.json's
 * `weapon_catalog_id`. Or one of the match's sixteen weapon places: `type`
 * "weapon_selector" with `value` "mp_location_00" to "mp_location_15" and no
 * id in either file. When a match starts, the game gives a weapon place the
 * weapon, the model and the size of that place in the match's weapon set
 * (setup.c, setupPlaceWeapon); a pickup of a catalog weapon keeps the model
 * and the size its own record names.
 *
 * forgeLevelDocSetPickupWeapon makes the pickup a weapon of the catalog and
 * so writes all three: the weapon, the model that shows it (`base.model`,
 * repeated in objects.json's `model_catalog_id`) and its size
 * (`base.extra_scale`, 256 is life size). The caller knows which model and
 * size go with a weapon; this module does not. forgeLevelDocSetPickupSlot
 * makes it a weapon place (0 to 15); the model and size in its record no
 * longer matter to the game then and are left as they are.
 *
 * Only a `weapon_settable` object. The ids must be catalog ids
 * ("base:dragon", "mod:weapon_crystal_pistol"): lower-case letters, digits,
 * '_', '-', '.' and one ':' with something on both sides. Whether the catalog
 * has them is for the caller; the game refuses a level that names a weapon or
 * a model it does not have.
 */
s32 forgeLevelDocSetPickupWeapon(forge_level_doc_t *doc, const char *record_id,
	const char *weapon_catalog_id, const char *model_catalog_id, s32 extra_scale,
	char *err, size_t err_cap);
s32 forgeLevelDocSetPickupSlot(forge_level_doc_t *doc, const char *record_id, s32 slot,
	char *err, size_t err_cap);

/*
 * Gives a plain prop (kind "prop") another model of the catalog, in both
 * files that name it. Only a `model_settable` object. Other kinds are refused:
 * a door, a lift or a monitor needs a model built for it, and a weapon
 * pickup's model goes with its weapon (above).
 */
s32 forgeLevelDocSetObjectModel(forge_level_doc_t *doc, const char *record_id,
	const char *model_catalog_id, char *err, size_t err_cap);

typedef enum forge_level_placement_fallback_kind {
	FORGE_LEVEL_FALLBACK_PICKUP,
	FORGE_LEVEL_FALLBACK_PROP,
	FORGE_LEVEL_FALLBACK_HOVERBIKE
} forge_level_placement_fallback_kind_e;

typedef struct forge_level_placement_fallback {
	forge_level_placement_fallback_kind_e kind;
	const char *record_id;
	const char *expected_catalog_id;
	const char *weapon_catalog_id;
	const char *model_catalog_id;
	s32 extra_scale;
} forge_level_placement_fallback_t;

/* Apply a set of placement fallbacks to an owned copy, freed with forgeLevelDocFree.
 * Each record may appear once. expected_catalog_id is the current non-base weapon
 * for a pickup, or the current non-base model for a prop or hoverbike. A pickup
 * takes base:falcon2 or base:slayer and the caller supplies its resolved base pickup
 * model and scale. Props and hoverbikes take fixed base models, with the replacement
 * fields left zero. Returns NULL on refusal and leaves the original intact. */
forge_level_doc_t *forgeLevelDocCloneWithPlacementFallbacks(const forge_level_doc_t *doc,
	const forge_level_placement_fallback_t *edits, size_t count, char *err, size_t err_cap);

/* 1 when the member's text is no longer what was read. */
s32 forgeLevelDocMemberChanged(const forge_level_doc_t *doc, s32 member);

/*
 * The member's text as it should be saved. The pointer is the document's own
 * and holds until the next edit or forgeLevelDocFree. NULL for a bad member.
 */
const char *forgeLevelDocMemberText(forge_level_doc_t *doc, s32 member, size_t *out_size);

/* The member's file name inside the scenario ("pads.json"), or NULL. */
const char *forgeLevelDocMemberName(s32 member);

/*
 * Saving. The mod author makes a level mod from a template archive plus
 * overrides (mod_author.h); these are the overrides for what this document
 * changed, one per changed member.
 */
#define FORGE_LEVEL_OVERRIDE_NAME_MAX 512

typedef struct forge_level_overrides {
	asset_archive_clone_override_t entries[FORGE_LEVEL_MEMBER_COUNT];
	char names[FORGE_LEVEL_MEMBER_COUNT][FORGE_LEVEL_OVERRIDE_NAME_MAX];
	u32 count;            /* how many entries are filled; 0 when nothing changed */
} forge_level_overrides_t;

/*
 * Fills `out` for a level archive that keeps its scenario at
 * `scenario_member`: an arena's scenario_archive, such as
 * "dependencies/assets/scenarios/base_scenario_mp_complex.pdscenario". NULL
 * or "" when the archive being saved is the scenario itself. Each entry names
 * a member the way the archive rebuild addresses one inside an embedded
 * archive ("<scenario_member>::pads.json").
 *
 * The entries point into `out->names` and into the document's own texts, so
 * `out` must stay where it is and the document must not be edited until the
 * save is done. 1 on success; 0 with the reason.
 */
s32 forgeLevelDocOverrides(forge_level_doc_t *doc, const char *scenario_member,
	forge_level_overrides_t *out, char *err, size_t err_cap);

/*
 * The same, with every member whether it changed or not. For a save that
 * replaces the very archive the document was read from (a level saved again
 * under its own name). "Changed" above is measured against what was read, and
 * after such a save the archive no longer holds what was read: an edit taken
 * back since the last save leaves its member "unchanged", so
 * forgeLevelDocOverrides would not write it and the archive would keep the
 * edit. Writing every member makes the archive what the document says.
 */
s32 forgeLevelDocOverridesAll(forge_level_doc_t *doc, const char *scenario_member,
	forge_level_overrides_t *out, char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
