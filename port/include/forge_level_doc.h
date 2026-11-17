/**
 * forge_level_doc.h -- the placements of a level, as Forge edits them.
 *
 * A level is a .pdarena with its .pdscenario inside. Five members of the
 * scenario say what stands where:
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
 *
 * This module reads those five texts, lets a caller add, move and remove
 * spawns and move, duplicate and leave out objects, and gives the texts back
 * for saving.
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

/* The five members' texts, indexed by FORGE_LEVEL_MEMBER_*. */
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
} forge_level_object_t;

/*
 * Reads the five members. Returns NULL with the reason in err when any of
 * them breaks a rule above. The texts are copied; the caller keeps its own.
 */
forge_level_doc_t *forgeLevelDocRead(const forge_level_texts_t *texts, char *err, size_t err_cap);

void forgeLevelDocFree(forge_level_doc_t *doc);

s32 forgeLevelDocPadCount(const forge_level_doc_t *doc);
s32 forgeLevelDocSpawnCount(const forge_level_doc_t *doc);
s32 forgeLevelDocObjectCount(const forge_level_doc_t *doc);
s32 forgeLevelDocVolumeCount(const forge_level_doc_t *doc);
s32 forgeLevelDocFieldRowCount(const forge_level_doc_t *doc);

/* Row `index` in file order. 1 when there is such a row. */
s32 forgeLevelDocGetPad(const forge_level_doc_t *doc, s32 index, forge_level_pad_t *out);
s32 forgeLevelDocGetSpawn(const forge_level_doc_t *doc, s32 index, forge_level_spawn_t *out);
s32 forgeLevelDocGetObject(const forge_level_doc_t *doc, s32 index, forge_level_object_t *out);

/* A pad by its name ("pad_0012"). 1 when the level has it. */
s32 forgeLevelDocFindPad(const forge_level_doc_t *doc, const char *pad_ref, forge_level_pad_t *out);

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

#ifdef __cplusplus
}
#endif

#endif
