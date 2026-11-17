/**
 * forge_level_doc.h -- the placements of a level, as Forge edits them.
 *
 * A level is a .pdarena with its .pdscenario inside. Four members of the
 * scenario say what stands where:
 *
 *   pads.json     pd2.scenario.pads.v1     places: a position, an up and a
 *                                          look direction each
 *   volumes.json  pd2.scenario.volumes.v1  each pad's bounds in the world,
 *                                          one row per pad
 *   spawns.json   pd2.scenario.spawns.v1   where players start: a pad each
 *   objects.json  pd2.scenario.objects.v1  props, weapons and the rest: a pad
 *                                          each, or none
 *
 * This module reads those four texts, lets a caller add, move and remove
 * spawns, and gives the texts back for saving. It holds no game state and
 * touches no file: text in, text out. Forge's editor fills it from the
 * level's archive and hands the changed members to the mod author
 * (context/designs/modding/forge-mode-scope.md, step F2).
 *
 * Rules it keeps:
 *
 * - Rows keep their bytes. A row the caller did not change is written back
 *   exactly as it was read, with every key this module does not know. A
 *   document with no edit gives back the texts it was given.
 * - Nothing is repaired. A text that is not its schema, a row with no id, a
 *   repeated id, a pad out of sequence, or a spawn, object or volume naming a
 *   pad the level does not have is refused with the reason.
 * - Pads are only ever added. The game numbers pads by their place in the
 *   file (pad_0000 is the first row), other members name pads too
 *   (setup.fields.json, the navigation tables), and 299 pads of the base
 *   levels are shared by more than one spawn or object. So moving something
 *   that stands on a pad from the template gives it a new pad and leaves the
 *   old one where it is; only a pad this document added is moved in place.
 * - A new pad comes with its bounds row, as every pad of a base level has
 *   one, and a moved pad's bounds move with it.
 * - Ids are not handed out twice. A new spawn takes the number above the
 *   highest spawn number the document has ever held.
 *
 * Saving goes through the archive rebuild, which brings the scenario's
 * navigation record (its count of pads and volumes and its hash of each of
 * these files) up to date; the game refuses a level whose record does not
 * match its files.
 *
 * Objects are read and kept, not edited yet. An object's kind and fields are
 * rows of setup.fields.json, which also repeats the object's pad
 * (`base.pad`); moving, adding or removing one has to change both files
 * together, and this module does not read that file yet.
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
	FORGE_LEVEL_MEMBER_COUNT
};

/*
 * The most pads, and the highest spawn number, a level made here may have.
 * The game reads a pad number into 15 bits, but the archive conformance rules
 * want a spawn's pad and a spawn's id written with exactly four digits
 * (pad_0000 to pad_9999, spawn_0000 to spawn_9999). The largest base level
 * has fewer than 2,000 pads.
 */
#define FORGE_LEVEL_MAX_PADS 10000
#define FORGE_LEVEL_MAX_SPAWN_NUMBER 9999

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
} forge_level_object_t;

/*
 * Reads the four members. Returns NULL with the reason in err when any of
 * them breaks a rule above. The texts are copied; the caller keeps its own.
 */
forge_level_doc_t *forgeLevelDocRead(const char *pads_json, size_t pads_size,
	const char *spawns_json, size_t spawns_size,
	const char *objects_json, size_t objects_size,
	const char *volumes_json, size_t volumes_size,
	char *err, size_t err_cap);

void forgeLevelDocFree(forge_level_doc_t *doc);

s32 forgeLevelDocPadCount(const forge_level_doc_t *doc);
s32 forgeLevelDocSpawnCount(const forge_level_doc_t *doc);
s32 forgeLevelDocObjectCount(const forge_level_doc_t *doc);
s32 forgeLevelDocVolumeCount(const forge_level_doc_t *doc);

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
