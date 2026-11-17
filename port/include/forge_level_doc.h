/**
 * forge_level_doc.h -- the placements of a level, as Forge edits them.
 *
 * A level is a .pdarena with its .pdscenario inside. Five members of the
 * scenario say what stands where, and a sixth repeats part of it:
 *
 *   pads.json          pd2.scenario.pads.v1          places: a position, an up
 *                                                    and a look direction each
 *   volumes.json       pd2.scenario.volumes.v1       each pad's bounds in the
 *                                                    world, one row per pad
 *   spawns.json        pd2.scenario.spawns.v1        where players start: a
 *                                                    pad each
 *   objects.json       pd2.scenario.objects.v1       props, weapons and the
 *                                                    rest: a pad each, or none
 *   setup.fields.json  pd2.scenario.setup.fields.v1  every field of every
 *                                                    object, one row a field
 *   level.graph.json   pd2.level.graph.v1            the level's graph: among
 *                                                    much else, one node per
 *                                                    bounds row, and counts
 *
 * This module reads those six texts, lets a caller add, move and remove
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
 * - Pads are only ever added. The game numbers pads by their place in the
 *   file (pad_0000 is the first row), other members name pads too (the
 *   navigation tables, other fields of objects), and 299 pads of the base
 *   levels are shared by more than one spawn or object. So moving something
 *   that stands on a pad from the template gives it a new pad and leaves the
 *   old one where it is; only a pad this document added is moved in place.
 * - A new pad comes with its bounds row, as every pad of a base level has
 *   one, and a moved pad's bounds move with it.
 * - The level's graph keeps step with the bounds rows. When it loads a level
 *   the game counts the graph's trigger volume nodes (the text `"kind":
 *   "scenario.trigger.volume.source"`) and refuses the level unless that is
 *   the number of rows in volumes.json. So a new bounds row comes with its
 *   node and the link the extractor gives every such node, and a level whose
 *   two counts already differ is refused here as the game would refuse it.
 *   The graph's `counts` of pads, volumes and objects follow the tables when
 *   they were true to begin with. Nothing else in the graph is read or
 *   changed, and every other byte of it is kept.
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

/* The six members' texts, indexed by FORGE_LEVEL_MEMBER_*. */
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
 * Reads the six members. Returns NULL with the reason in err when any of
 * them breaks a rule above. The texts are copied; the caller keeps its own.
 */
forge_level_doc_t *forgeLevelDocRead(const forge_level_texts_t *texts, char *err, size_t err_cap);

void forgeLevelDocFree(forge_level_doc_t *doc);

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

/* A pad by its name ("pad_0012"). 1 when the level has it. */
s32 forgeLevelDocFindPad(const forge_level_doc_t *doc, const char *pad_ref, forge_level_pad_t *out);

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
