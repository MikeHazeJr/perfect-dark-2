/**
 * level_tool.c -- the rules of the Modding Hub's Levels tool: see
 * level_tool.h.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "level_tool.h"

/* The kinds of record the summary tells apart, as objects.json names them. */
static const char *const kPickupKind = "weapon_pickup";
static const char *const kCrateKind = "multi_ammo_crate";
static const char *const kPropKind = "prop";

void levelToolSummarise(const forge_level_doc_t *doc, level_tool_summary_t *out)
{
	s32 count;

	if (!out) {
		return;
	}
	memset(out, 0, sizeof(*out));
	if (!doc) {
		return;
	}
	out->pads = forgeLevelDocPadCount(doc);
	out->starts = forgeLevelDocSpawnCount(doc);
	count = forgeLevelDocObjectCount(doc);
	out->objects = count;
	for (s32 i = 0; i < count; i++) {
		forge_level_object_t object;

		if (!forgeLevelDocGetObject(doc, i, &object)) {
			continue;
		}
		if (object.removed) {
			out->left_out++;
		} else if (strcmp(object.kind, kPickupKind) == 0) {
			if (object.weapon_slot >= 0) {
				out->weapon_places++;
			} else {
				out->fixed_weapons++;
			}
		} else if (strcmp(object.kind, kCrateKind) == 0) {
			out->ammo_crates++;
		} else if (strcmp(object.kind, kPropKind) == 0) {
			out->props++;
		} else {
			out->other++;
		}
	}
}

/* Appends to a line that is always terminated; what does not fit is cut. */
static void toolAppend(char *out, size_t cap, const char *fmt, ...)
{
	const size_t used = strlen(out);
	va_list args;

	if (used + 1 >= cap) {
		return;
	}
	va_start(args, fmt);
	vsnprintf(out + used, cap - used, fmt, args);
	va_end(args);
	out[cap - 1] = '\0';
}

/* ", " before every part but the first, then "<n> <one or many>". */
static void toolAppendCount(char *out, size_t cap, s32 count, const char *one, const char *many)
{
	if (count <= 0) {
		return;
	}
	toolAppend(out, cap, "%s%d %s", out[0] ? ", " : "", (int)count, count == 1 ? one : many);
}

void levelToolDescribe(const level_tool_summary_t *summary, char *out, size_t cap)
{
	if (!out || cap == 0) {
		return;
	}
	out[0] = '\0';
	if (!summary) {
		return;
	}
	if (summary->starts <= 0) {
		toolAppend(out, cap, "no starts");
	} else {
		toolAppendCount(out, cap, summary->starts, "start", "starts");
	}
	toolAppendCount(out, cap, summary->weapon_places, "weapon place", "weapon places");
	toolAppendCount(out, cap, summary->fixed_weapons, "fixed weapon", "fixed weapons");
	toolAppendCount(out, cap, summary->ammo_crates, "ammo crate", "ammo crates");
	toolAppendCount(out, cap, summary->props, "prop", "props");
	toolAppendCount(out, cap, summary->other, "other object", "other objects");
	toolAppendCount(out, cap, summary->left_out, "left out", "left out");
}

s32 levelToolNewLevelName(const char *display_name, const char *template_id, s32 plan_code,
		const mod_author_result_t *plan, const char *plan_reason, char *why, size_t why_cap)
{
	char unused[4];

	/* A caller that wants only the answer passes no place for the sentence. */
	if (!why || why_cap == 0) {
		why = unused;
		why_cap = sizeof(unused);
	}
	why[0] = '\0';
	if (!display_name || !display_name[0]) {
		return LEVEL_TOOL_NAME_EMPTY;
	}
	if (plan_code != MOD_AUTHOR_OK || !plan) {
		snprintf(why, why_cap, "That name cannot be used: %s.",
			(plan_reason && plan_reason[0]) ? plan_reason : "the mod author refused it");
		why[why_cap - 1] = '\0';
		return LEVEL_TOOL_NAME_REFUSED;
	}
	if (template_id && template_id[0] && strcmp(plan->catalog_id, template_id) == 0) {
		snprintf(why, why_cap, "That is this level's own name. Pick another name for the new level.");
		why[why_cap - 1] = '\0';
		return LEVEL_TOOL_NAME_TEMPLATE;
	}
	if (plan->updated) {
		snprintf(why, why_cap,
			"You already have a level with that name (%s). Open that level in Forge, or pick another name.",
			plan->catalog_id);
		why[why_cap - 1] = '\0';
		return LEVEL_TOOL_NAME_TAKEN;
	}
	return LEVEL_TOOL_NAME_OK;
}
