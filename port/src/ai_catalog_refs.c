/*
 * ai_catalog_refs.c -- the asset arguments of AI commands by catalog id.
 * See ai_catalog_refs.h and context/designs/modding/ai-typed-operands.md
 * (slices 3 and 3b).
 */

#include <stdint.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "constants.h"
#include "data.h"
#include "assetcatalog.h"
#include "catalog_readable_ids.h"
#include "loader_enum_reverse.h"
#include "romextract_pd.h"
#include "weapondata_authored.h"
#include "ai_command_schema.h"
#include "ai_catalog_refs.h"

/* The entry an id names when it is of this type, or NULL. */
static const asset_entry_t *s_entryOfType(const char *id, asset_type_e type)
{
	const asset_entry_t *e;

	if (!id || !id[0]) {
		return NULL;
	}
	e = assetCatalogResolve(id);
	return e && e->occupied && e->type == type ? e : NULL;
}

static int s_isBaseId(const char *id)
{
	return id && strncmp(id, "base:", 5) == 0;
}

/* ---- weapons: the base weapon table first ---- */

static const char *s_weaponName(int64_t value)
{
	const char *id;

	if (value >= 0 && value < g_WeaponDataCount) {
		id = g_WeaponDataCatalogIds[value];
		if (id && id[0]) {
			return id;
		}
	}
	if (value < 0 || value > 0x7fff) {
		return NULL;
	}
	id = catalogWeaponIdByRuntimeWeaponNum((s32)value);
	return id && id[0] ? id : NULL;
}

static int s_weaponResolve(const char *id, int64_t *out)
{
	catalog_weapon_result_t result;

	for (s32 i = 0; i < g_WeaponDataCount; i++) {
		if (g_WeaponDataCatalogIds[i] && strcmp(g_WeaponDataCatalogIds[i], id) == 0) {
			*out = i;
			return 1;
		}
	}
	if (!s_entryOfType(id, ASSET_WEAPON) || !catalogResolveWeapon(id, &result)
			|| result.weapon_num < 0) {
		return 0;
	}
	*out = result.weapon_num;
	return 1;
}

/* ---- animations: the catalog's readable id for the slot ---- */

static int s_animIsSlot(const asset_entry_t *e, int64_t value)
{
	return e && e->occupied && e->type == ASSET_ANIMATION && e->runtime_index == value;
}

static const char *s_animName(int64_t value)
{
	char id[CATALOG_ID_LEN];
	const asset_entry_t *e;

	if (value < 0 || value > 0xffff) {
		return NULL;
	}
	/* The base registration's id (assetcatalog_base_extended.c), which is
	 * the one the slot's archive declares (B-1258). */
	catalogReadableAnimationId((s32)value, "character", id, sizeof(id));
	e = s_entryOfType(id, ASSET_ANIMATION);
	if (s_animIsSlot(e, value)) {
		return e->id;
	}
	/* A mod's animation slot: the entry that holds it, a base one first. */
	{
		const char *found = NULL;

		for (s32 i = 0; i < assetCatalogGetPoolSize(); i++) {
			e = assetCatalogGetByIndex(i);
			if (!s_animIsSlot(e, value) || strncmp(e->id, "base:invanim_", 13) == 0) {
				continue;
			}
			if (s_isBaseId(e->id)) {
				return e->id;
			}
			if (!found) {
				found = e->id;
			}
		}
		return found;
	}
}

static int s_animResolve(const char *id, int64_t *out)
{
	const asset_entry_t *e = s_entryOfType(id, ASSET_ANIMATION);

	if (!e || e->runtime_index < 0 || e->runtime_index > 0xffff
			|| strncmp(e->id, "base:invanim_", 13) == 0) {
		return 0;
	}
	*out = e->runtime_index;
	return 1;
}

/* ---- sounds: configured aliases, MP3 voice files and samples ---- */

static int s_soundIsAudio(const asset_entry_t *e)
{
	return e && e->occupied && e->type == ASSET_AUDIO
		&& e->ext.audio.category != AUDIO_CAT_MUSIC;
}

/* An audio entry with this sound number (a sample's index or a configured
 * alias's packed number), a base one first. */
static const char *s_soundByNumber(s32 soundnum)
{
	const char *found = NULL;

	for (s32 i = 0; i < assetCatalogGetPoolSize(); i++) {
		const asset_entry_t *e = assetCatalogGetByIndex(i);

		if (!s_soundIsAudio(e) || e->source_soundnum != soundnum) {
			continue;
		}
		if (s_isBaseId(e->id)) {
			return e->id;
		}
		if (!found) {
			found = e->id;
		}
	}
	return found;
}

/* An MP3 voice line's file: the id of its direct voice archive, by the rule
 * the voice extractor writes them with (romextract_pdsfx.c: every file
 * whose symbol is a direct MP3 voice file, named
 * catalogReadableVoiceIdForFile). The rule needs no catalog, because the
 * scenario extractor names and checks these rows before the loader
 * registers the voice archives; once they are registered, the resolver
 * reads the entry's file number instead (the same number). */
static int s_isDirectVoiceFile(s32 filenum)
{
	return filenum > 0 && filenum <= AI_SOUND_REF_ID_MASK
		&& romextractPdvoiceIsDirectMp3FileSymbol(loaderEnumNameForFileEnum(filenum));
}

static const char *s_soundByVoiceFile(s32 filenum)
{
	/* Per thread: the writer copies the id before it names the next value. */
	static __thread char id[CATALOG_ID_LEN];

	if (!s_isDirectVoiceFile(filenum)) {
		return NULL;
	}
	catalogReadableVoiceIdForFile(filenum, id, sizeof(id));
	return id[0] ? id : NULL;
}

/* The file number an MP3 voice line's id names, or -1: a registered direct
 * voice entry's, else (before the loader registers them) the rule above
 * backwards, for a base voice id only. */
static s32 s_voiceFileOfId(const char *id)
{
	char rule_id[CATALOG_ID_LEN];

	if (strncmp(id, "base:voice_", 11) != 0) {
		return -1;
	}
	for (s32 filenum = 1; filenum <= AI_SOUND_REF_ID_MASK; filenum++) {
		if (!s_isDirectVoiceFile(filenum)) {
			continue;
		}
		catalogReadableVoiceIdForFile(filenum, rule_id, sizeof(rule_id));
		if (strcmp(rule_id, id) == 0) {
			return filenum;
		}
	}
	return -1;
}

static const char *s_soundName(int64_t asset)
{
	char id[CATALOG_ID_LEN];
	const asset_entry_t *e;

	if (asset < 0 || asset > 0xffff) {
		return NULL;
	}
	if (asset & AI_SOUND_REF_CONFIGURED) {
		catalogReadableSoundRefId((s32)asset, id, sizeof(id));
		e = s_entryOfType(id, ASSET_AUDIO);
		if (s_soundIsAudio(e) && e->source_soundnum == (s32)asset) {
			return e->id;
		}
		return s_soundByNumber((s32)asset);
	}
	if (asset & AI_SOUND_REF_MP3) {
		return s_soundByVoiceFile((s32)(asset & AI_SOUND_REF_ID_MASK));
	}
	catalogReadableSoundRefId((s32)asset, id, sizeof(id));
	e = s_entryOfType(id, ASSET_AUDIO);
	if (s_soundIsAudio(e) && e->source_soundnum == (s32)asset) {
		return e->id;
	}
	return s_soundByNumber((s32)asset);
}

static int s_soundResolve(const char *id, int64_t *out)
{
	const asset_entry_t *e = s_entryOfType(id, ASSET_AUDIO);
	s32 filenum;

	if (e && !s_soundIsAudio(e)) {
		return 0;
	}
	if (e && e->source_soundnum >= AI_SOUND_REF_CONFIGURED && e->source_soundnum <= 0xffff) {
		*out = e->source_soundnum;
		return 1;
	}
	if (e && e->source_soundnum >= 0 && e->source_soundnum <= AI_SOUND_REF_ID_MASK) {
		*out = e->source_soundnum;
		return 1;
	}
	if (e && e->source_soundnum < 0 && e->source_filenum > 0
			&& e->source_filenum <= AI_SOUND_REF_ID_MASK) {
		*out = AI_SOUND_REF_MP3 | e->source_filenum;
		return 1;
	}
	if (e) {
		return 0;
	}
	/* Not registered: a base MP3 voice line before the loader's voice scan. */
	filenum = s_voiceFileOfId(id);
	if (filenum < 0) {
		return 0;
	}
	*out = AI_SOUND_REF_MP3 | filenum;
	return 1;
}

/* ---- every kind ---- */

const char *aiCatalogRefName(ai_operand_kind_t kind, int64_t value)
{
	switch (kind) {
	case AI_OPERAND_MODEL:
		return value >= 0 && value <= 0x7fff ? catalogModelIdByModelnum((s32)value) : NULL;
	case AI_OPERAND_WEAPON:
		return s_weaponName(value);
	case AI_OPERAND_BODY:
		return value >= 0 && value <= 0x7fff ? catalogBodyIdByBodynum((s32)value) : NULL;
	case AI_OPERAND_HEAD:
		return value >= 0 && value <= 0x7fff ? catalogHeadIdByHeadnum((s32)value) : NULL;
	case AI_OPERAND_ANIM:
		return s_animName(value);
	case AI_OPERAND_SOUND:
		return s_soundName(value);
	default:
		return NULL;
	}
}

int aiCatalogRefResolve(ai_operand_kind_t kind, const char *id, int64_t *out_value)
{
	if (!id || !id[0] || !out_value) {
		return 0;
	}
	switch (kind) {
	/* Models, bodies and heads resolve as the stage setup resolves them. */
	case AI_OPERAND_MODEL: {
		catalog_model_result_t result;

		if (!catalogResolveModel(id, &result)) {
			return 0;
		}
		*out_value = result.modelnum;
		return 1;
	}
	case AI_OPERAND_WEAPON:
		return s_weaponResolve(id, out_value);
	case AI_OPERAND_BODY: {
		catalog_body_result_t result;

		if (!catalogResolveBody(id, &result) || !result.entry) {
			return 0;
		}
		*out_value = result.entry->runtime_index;
		return 1;
	}
	case AI_OPERAND_HEAD: {
		catalog_head_result_t result;

		if (!catalogResolveHead(id, &result) || !result.entry) {
			return 0;
		}
		*out_value = result.entry->runtime_index;
		return 1;
	}
	case AI_OPERAND_ANIM:
		return s_animResolve(id, out_value);
	case AI_OPERAND_SOUND:
		return s_soundResolve(id, out_value);
	default:
		return 0;
	}
}
