/**
 * forge_level_live.cpp -- what the world should show of the level Forge has
 * open (forge_level_live.h). The planning half: documents in, a list out, no
 * game state. The client's half is forge_level_live_world.c.
 */
#include "forge_level_live.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

/* The kinds the stage makes as one model standing on a pad. Doors, lifts,
 * characters and vehicles that fly a path are left to the markers: a door
 * hidden here would leave its portal shut and a character is not a model on
 * a pad. */
const char *const kShownKinds[] = {
	"prop", "weapon_pickup", "ammo_crate", "multi_ammo_crate", "hoverbike", "hover_prop",
	"key", "hat", "shield", "gas_bottle", "single_monitor", "multi_monitor",
	"hanging_monitors", "alarm", "cctv", "autogun", "safe", "glass", "tinted_glass",
};

void fail(char *err, size_t err_cap, const char *fmt, ...)
{
	if (!err || err_cap == 0) return;
	va_list args;
	va_start(args, fmt);
	vsnprintf(err, err_cap, fmt, args);
	va_end(args);
}

/* setup_0012 is the thirteenth command of the level (forge_level_doc.h). */
s32 commandOf(const char *record_id)
{
	static const char prefix[] = "setup_";
	if (!record_id || strncmp(record_id, prefix, sizeof(prefix) - 1) != 0) return -1;
	const char *digits = record_id + sizeof(prefix) - 1;
	if (!*digits) return -1;
	for (const char *p = digits; *p; p++) {
		if (*p < '0' || *p > '9') return -1;
	}
	const long number = strtol(digits, nullptr, 10);
	return number >= 0 && number < 0x8000 ? static_cast<s32>(number) : -1;
}

bool closeTo(const f32 a[3], const f32 b[3], f32 tolerance)
{
	for (int i = 0; i < 3; i++) {
		if (std::fabs(a[i] - b[i]) > tolerance) return false;
	}
	return true;
}

bool samePlace(const forge_level_pad_t &a, const forge_level_pad_t &b)
{
	return closeTo(a.position, b.position, 0.01f) && closeTo(a.look, b.look, 0.0001f) &&
		closeTo(a.up, b.up, 0.0001f);
}

void copyText(char *dst, size_t cap, const char *src)
{
	snprintf(dst, cap, "%s", src ? src : "");
}

} // namespace

s32 forgeLevelLiveKindShown(const char *kind)
{
	if (!kind) return 0;
	for (const char *shown : kShownKinds) {
		if (strcmp(kind, shown) == 0) return 1;
	}
	return 0;
}

s32 forgeLevelLivePlan(const forge_level_doc_t *opened, const forge_level_doc_t *now,
		forge_level_live_plan_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out) {
		fail(err, err_cap, "there is nowhere to put the plan");
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if (!opened || !now) {
		fail(err, err_cap, "no level is open");
		return 0;
	}

	const s32 before = forgeLevelDocObjectCount(opened);
	const s32 after = forgeLevelDocObjectCount(now);
	if (after < before) {
		fail(err, err_cap, "the level has %d objects and had %d when it was opened", after, before);
		return 0;
	}

	for (s32 i = 0; i < after; i++) {
		forge_level_object_t was;
		forge_level_object_t is;
		memset(&was, 0, sizeof(was));
		if (!forgeLevelDocGetObject(now, i, &is)) {
			fail(err, err_cap, "object %d of the level cannot be read", i);
			return 0;
		}
		const bool had = i < before;
		if (had) {
			if (!forgeLevelDocGetObject(opened, i, &was)) {
				fail(err, err_cap, "object %d of the level as opened cannot be read", i);
				return 0;
			}
			if (strcmp(was.record_id, is.record_id) != 0) {
				fail(err, err_cap, "object %d is %s and was %s when the level was opened", i, is.record_id,
					was.record_id);
				return 0;
			}
		}

		forge_level_pad_t wasPad;
		forge_level_pad_t isPad;
		memset(&wasPad, 0, sizeof(wasPad));
		memset(&isPad, 0, sizeof(isPad));
		const bool hasPad = is.pad_ref[0] && forgeLevelDocFindPad(now, is.pad_ref, &isPad);
		const s32 scale = forgeLevelDocObjectExtraScale(now, i);
		bool moved = false;
		bool redrawn = false;
		if (had) {
			const bool hadPad = was.pad_ref[0] && forgeLevelDocFindPad(opened, was.pad_ref, &wasPad);
			moved = strcmp(was.pad_ref, is.pad_ref) != 0 || hadPad != hasPad ||
				(hadPad && hasPad && !samePlace(wasPad, isPad));
			redrawn = strcmp(was.model_catalog_id, is.model_catalog_id) != 0 ||
				strcmp(was.weapon_catalog_id, is.weapon_catalog_id) != 0 ||
				forgeLevelDocObjectExtraScale(opened, i) != scale;
		}

		/* The stage made the object unless the level left it out when it was
		 * opened. What the document wants: the stage's object hidden when it
		 * has gone, moved or changed, and a stand-in wherever the object is
		 * now and the stage does not show it as it is. */
		const bool onStage = had && !was.removed;
		const bool hide = onStage && (is.removed || moved || redrawn);
		const bool draw = !is.removed && (!had || was.removed || moved || redrawn);
		if (!hide && !draw) continue;

		if (!forgeLevelLiveKindShown(is.kind)) {
			out->not_shown++;
			continue;
		}

		const s32 command = commandOf(is.record_id);
		/* An object only moved looks as the stage drew it, which for one of the
		 * match's weapon places is the weapon the match put there; anything else
		 * is drawn with the model its record names. */
		const bool looksAsLoaded = onStage && moved && !redrawn && command >= 0;
		const bool canHide = command >= 0 && out->hide_count < FORGE_LEVEL_LIVE_MAX;
		const bool canDraw = hasPad && (is.model_catalog_id[0] || looksAsLoaded) &&
			out->draw_count < FORGE_LEVEL_LIVE_MAX;
		/* Never a stand-in beside the object it stands in for. */
		if ((hide && !canHide) || (draw && !canDraw)) {
			out->not_shown++;
			continue;
		}
		if (hide) {
			forge_level_live_hide_t &entry = out->hide[out->hide_count++];
			copyText(entry.record_id, sizeof(entry.record_id), is.record_id);
			copyText(entry.kind, sizeof(entry.kind), is.kind);
			entry.command = command;
		}
		if (draw) {
			forge_level_live_draw_t &entry = out->draw[out->draw_count++];
			copyText(entry.record_id, sizeof(entry.record_id), is.record_id);
			copyText(entry.kind, sizeof(entry.kind), is.kind);
			copyText(entry.model_catalog_id, sizeof(entry.model_catalog_id), is.model_catalog_id);
			for (int k = 0; k < 3; k++) {
				entry.position[k] = isPad.position[k];
				entry.look[k] = isPad.look[k];
				entry.up[k] = isPad.up[k];
			}
			entry.extra_scale = scale;
			entry.looks_like_command = looksAsLoaded ? command : -1;
		}
	}
	return 1;
}
