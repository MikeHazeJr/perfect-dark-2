/**
 * pdgui_forge_editor.cpp -- The Grid level editor ImGui overlay (F1-F8).
 *
 * Single ImGui window, tabbed.  Tabs:
 *   Catalog       F1  catalog tree + placement reticle trigger
 *   Properties    F2  per-selection property editor
 *   Zones         F4  zone list + runtime toggles
 *   Lighting      F4  skylight + atmosphere + sky selection
 *   Logic         F5  node list + channel management + wire listing
 *   Game Type     F5b game type + waves + boss + role + modifiers
 *   Mission       F7  objectives + briefing
 *   Settings      F3  map settings + save/load as mod + budget
 *
 * The editor window is draggable/resizable by the user, and auto-hides in
 * NORMAL sub-mode (only drawn in FREEFLY, so players can still playtest).
 *
 * IMPORTANT: C++ TU -- never include types.h (`#define bool s32` breaks
 * C++).  Reach into game/port via extern "C".
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "pdgui_forge.h"
#include "pdgui_glyphs.h"
#include "pdgui_menu_grid.h"
#include "pdgui_grid_variant_picker.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_nav.h"
#include "pdgui_nav_input.h"
#include "pdgui.h"   /* B-195 / Fix 2+3 (2026-05-01): pdguiClearImGuiFocusAndNav */
#include "system.h"  /* Issue 9 placeholder TODO log lines */
#include "inputctx.h"
#include "menupool.h"
#include "asset_runtime.h"
#include "forge_level_io.h"   /* Forge F2: the level's own placements */
#include "forge_room_builder.h"
#include "net/net_scenario_state_wire.h"
#include "scenario_stage_rules.h"
#include "forge_level_live.h" /* Forge F3: the world follows the level file */

extern "C" {
#include "game/forgemode.h"
#include "pdgui_constants.h"
#include "forge/forge_core.h"
/* Issue 8b (2026-04-24): direct actionmap reads for the editor's
 * sidebar / tab-cycle actions. The enum and query API are extern "C"
 * in actionmap.h; including it here is safe (no types.h drag). */
#include "actionmap.h"

s32 netGetMode(void);
s32 mainStageChangePending(void);
s32 mainGetStageNum(void);
s32 videoGetNativeWidth(void);
s32 videoGetNativeHeight(void);

/* B-304 hotfix (2026-05-01): gfx debug toggles consumed by the Level
 * tab's Debug Rendering subsection in forgeDrawLevelExtras. Defined as
 * extern "C" in port/fast3d/gfx_opengl.cpp; declared here at file scope
 * (NOT inside a function body, which is not a valid scope for linkage
 * specifications in C++). */
int  gfxDebugWireframeGet(void);
void gfxDebugWireframeSet(int on);
int  gfxDebugCullModeGet(void);
void gfxDebugCullModeSet(int mode);

/* main.c: the --launch-forge-level and --debug-forge-session-save one-shots,
 * driven from pdguiForgeEditorTick. */
s32 bootForgeLevelTick(void);
}

/* Issue 8b (2026-04-24): tab ids moved here from their later declaration
 * so the forward-declared sidebar helper can accept one, and the tab
 * draw functions (which register scroll anchors) can name them. The
 * values match the original enum -- ordering is load-bearing because
 * forgeTabLabel switches on them. */
enum {
	FGT_FILE = 0,   /* the level being made: its starts and objects, saved in its archive */
	FGT_GAMEPLAY,   /* Playtest: loaded level and live bots */
	FGT_LEVEL,      /* "Look": lighting, mission, sky, music (an old Grid map's) */
	FGT_OBJECTS,    /* "Grid Objects": The Grid's own catalog (an old Grid map's) */
	FGT_CONTROL,
	FGT_SETUP,      /* "Grid Map": an old Grid map's settings and its save */
	FGT_COUNT
};

/* Forward decl: used by the tab draw functions (defined much later in
 * this file) to register scroll-to-section anchors that the sidebar
 * activates. Keeping the helper near the sidebar data for readability;
 * this declaration makes it visible at the earlier call sites. */
static bool forgeScrollAnchorMaybe(int tab, int anchor_id);

static bool s_TransientEditorControls;

static bool forgeEditorControlsActive(void)
{
	return menupoolIsActive(MENU_TYPE_GRID_EDITOR_CONTROLS)
		&& inputCtxGetTop() == &g_CtxImGuiMenu;
}

static bool forgeEditorAcquireControls(void)
{
	if (forgeEditorControlsActive()) return true;
	if (inputCtxGetTop() != &g_CtxForgeEditor) return false;

	menupoolAcquire(MENU_TYPE_GRID_EDITOR_CONTROLS, nullptr, &g_CtxImGuiMenu);
	if (forgeEditorControlsActive()) return true;
	menupoolRelease(MENU_TYPE_GRID_EDITOR_CONTROLS);
	return false;
}

static void forgeEditorReleaseControls(void)
{
	s_TransientEditorControls = false;
	menupoolRelease(MENU_TYPE_GRID_EDITOR_CONTROLS);
}

/* ============================================================
 * Helpers
 * ============================================================ */

static const char *fcat_label(int c) {
	return forgeCategoryName((forge_category_t)c);
}

static const char *flogic_kind_name(int k) {
	switch (k) {
	case FORGE_LOGIC_EVENT:     return "Event";
	case FORGE_LOGIC_CONDITION: return "Condition";
	case FORGE_LOGIC_ACTION:    return "Action";
	default:                    return "?";
	}
}

static const char *flogic_op_name(int op)
{
	switch (op) {
	case FORGE_OP_ON_PLAYER_ENTER:   return "On Player Enter";
	case FORGE_OP_ON_PLAYER_EXIT:    return "On Player Exit";
	case FORGE_OP_ON_OBJECT_DESTROYED:return "On Object Destroyed";
	case FORGE_OP_ON_SWITCH:         return "On Switch Activated";
	case FORGE_OP_ON_KILL:           return "On Kill";
	case FORGE_OP_ON_KILL_COUNT:     return "On Kill Count";
	case FORGE_OP_ON_ITEM_PICKUP:    return "On Item Pickup";
	case FORGE_OP_ON_TIMER:          return "On Timer";
	case FORGE_OP_ON_ROUND_START:    return "On Round Start";
	case FORGE_OP_ON_ROUND_END:      return "On Round End";
	case FORGE_OP_ON_CHANNEL:        return "On Channel";
	case FORGE_OP_ON_INTERACT:       return "On Interact";
	case FORGE_OP_HAS_ITEM:          return "Has Item";
	case FORGE_OP_KILL_COUNT_GE:     return "Kill Count >= N";
	case FORGE_OP_ALL_ENEMIES_DEAD:  return "All Enemies Dead";
	case FORGE_OP_SWITCH_STATE:      return "Switch State";
	case FORGE_OP_CHANNEL_STATE:     return "Channel State";
	case FORGE_OP_TEAM_SCORE_GE:     return "Team Score >= N";
	case FORGE_OP_PLAYER_COUNT:      return "Player Count";
	case FORGE_OP_TIMER_ELAPSED:     return "Timer Elapsed";
	case FORGE_OP_RANDOM:            return "Random";
	case FORGE_OP_OPEN_DOOR:         return "Open Door";
	case FORGE_OP_CLOSE_DOOR:        return "Close Door";
	case FORGE_OP_ACTIVATE_ELEVATOR: return "Activate Elevator";
	case FORGE_OP_SPAWN_OBJECT:      return "Spawn Object";
	case FORGE_OP_SPAWN_AI:          return "Spawn AI";
	case FORGE_OP_DESTROY_OBJECT:    return "Destroy Object";
	case FORGE_OP_PLAY_SOUND:        return "Play Sound";
	case FORGE_OP_SHOW_MESSAGE:      return "Show Message";
	case FORGE_OP_SET_CHANNEL:       return "Set Channel";
	case FORGE_OP_TELEPORT_PLAYER:   return "Teleport Player";
	case FORGE_OP_CHANGE_ZONE:       return "Change Zone";
	case FORGE_OP_SET_TIMER:         return "Set Timer";
	case FORGE_OP_AWARD_SCORE:       return "Award Score";
	case FORGE_OP_END_MISSION:       return "End Mission";
	case FORGE_OP_LOCK_DOOR:         return "Lock Door";
	case FORGE_OP_UNLOCK_DOOR:       return "Unlock Door";
	case FORGE_OP_ENABLE_OBJECT:     return "Enable Object";
	case FORGE_OP_DISABLE_OBJECT:    return "Disable Object";
	case FORGE_OP_CAMERA_EVENT:      return "Camera Event";
	case FORGE_OP_SPAWN_WAVE:        return "Spawn Wave";
	case FORGE_OP_TRIGGER_BOSS_PHASE:return "Trigger Boss Phase";
	case FORGE_OP_OBJECTIVE_COMPLETE:return "Objective Complete";
	case FORGE_OP_OBJECTIVE_FAIL:    return "Objective Fail";
	default:                         return "?";
	}
}

static ImU32 flogic_kind_color(int k) {
	switch (k) {
	case FORGE_LOGIC_EVENT:     return IM_COL32(240, 240, 240, 220);
	case FORGE_LOGIC_CONDITION: return IM_COL32(240, 220, 100, 220);
	case FORGE_LOGIC_ACTION:    return IM_COL32(120, 220, 140, 220);
	default:                    return IM_COL32(180, 180, 180, 220);
	}
}

static const char *fzone_name(int t) {
	switch (t) {
	case FORGE_ZONE_TRIGGER:    return "Trigger";
	case FORGE_ZONE_RADIATION:  return "Radiation";
	case FORGE_ZONE_GRAVITY:    return "Gravity";
	case FORGE_ZONE_TELEPORTER: return "Teleporter";
	case FORGE_ZONE_KILL:       return "Kill";
	case FORGE_ZONE_WATER:      return "Water";
	case FORGE_ZONE_SOUND:      return "Sound";
	case FORGE_ZONE_NO_WEAPON:  return "No-Weapon";
	case FORGE_ZONE_FOG:        return "Fog";
	case FORGE_ZONE_SOFT_BOUND: return "Soft Bound";
	case FORGE_ZONE_SPAWN_AREA: return "Team Spawn";
	case FORGE_ZONE_HILL:       return "Hill (KotH)";
	case FORGE_ZONE_TERRITORY:  return "Territory";
	default:                    return "?";
	}
}

static const char *flight_name(int t) {
	switch (t) {
	case FORGE_LIGHT_POINT:    return "Point";
	case FORGE_LIGHT_SPOT:     return "Spot";
	case FORGE_LIGHT_AREA:     return "Area";
	case FORGE_LIGHT_EMISSIVE: return "Emissive";
	default:                   return "?";
	}
}

/* Case-insensitive substring search.  Matches the forge_core helper in
 * behaviour but local so the editor TU doesn't leak internals across the
 * C/C++ boundary. */
static int f_icontains(const char *hay, const char *needle)
{
	if (!hay || !needle || !*needle) return 1;
	for (const char *p = hay; *p; ++p) {
		const char *a = p;
		const char *b = needle;
		while (*a && *b) {
			char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + ('a' - 'A')) : *a;
			char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + ('a' - 'A')) : *b;
			if (ca != cb) break;
			++a; ++b;
		}
		if (!*b) return 1;
	}
	return 0;
}

static void f_slug_from_name(const char *name, char *out, size_t n)
{
	if (n == 0) return;
	size_t i = 0;
	for (; name && *name && i + 1 < n; ++name) {
		char c = *name;
		if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
			out[i++] = c;
		} else if (c == ' ') {
			out[i++] = '-';
		}
	}
	if (i == 0) { forgeObjectNextUid(); snprintf(out, n, "grid-map-%u", (unsigned)forgeObjectNextUid()); return; }
	out[i] = '\0';
}

/* ============================================================
 * Tabs
 * ============================================================ */

static void forgeDrawCatalogTab(void)
{
	forge_editor_state_t *ed = forgeGetEditor();
	forge_budget_stats_t b; forgeBudgetCompute(&b);

	/* S313 -- active ghost placement controls.  When a catalog entry
	 * has been picked, show a banner with Place / Cancel buttons so
	 * the author commits or aborts without having to re-find the
	 * entry in the catalog tree. */
	{
		forge_placement_state_t *pp = forgeGetPlacement();
		if (pp && pp->ghost_active) {
			ImU32 ring = pp->ghost_valid ? IM_COL32(120, 220, 140, 255)
			                             : IM_COL32(240, 130, 130, 255);
			ImGui::PushStyleColor(ImGuiCol_Border, ring);
			ImGui::BeginChild("PlaceBanner", ImVec2(0, 44 * pdguiScale(1.0f)),
					ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
					ImGuiWindowFlags_NoScrollbar);
			ImGui::Text("Placing:  %s", pp->pending_catalog_id);
			ImGui::SameLine();
			ImGui::TextColored(pp->ghost_valid
					? ImVec4(0.5f, 0.95f, 0.6f, 1.0f)
					: ImVec4(0.95f, 0.5f, 0.5f, 1.0f),
					pp->ghost_valid ? "[VALID]" : "[INVALID]");
			ImGui::SameLine();
			if (ImGui::Button("Place Here")) {
				s32 uid = forgePlaceCommit();
				if (uid > 0) forgeSelectionSelectOnly((u32)uid);
				forgePlaceCancel();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel (Esc)")) {
				forgePlaceCancel();
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			/* Escape cancels placement when the editor has focus. */
			if (actionPressed(0, ACTION_FORGE_PLACE_CANCEL)) {
				forgePlaceCancel();
			}
		}
	}

	/* Search + category filter */
	ImGui::SetNextItemWidth(-160 * pdguiScale(1.0f));
	ImGui::InputTextWithHint("##Search", "Search catalog...", ed->search_filter, FORGE_NAME_LEN);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	const char *cats[] = {
		"(All)","Geometry","Props","Weapons","Spawn Points","Pickups",
		"Lighting","Effects","Zones","Interactables","Characters","Logic","Prefabs"
	};
	int filter = (int)ed->category_filter;
	if (filter < 0 || filter > (int)FORGE_CAT_COUNT) filter = (int)FORGE_CAT_COUNT;
	int combo_idx = (filter == (int)FORGE_CAT_COUNT) ? 0 : (filter + 1);
	if (ImGui::Combo("##CatFilter", &combo_idx, cats, IM_ARRAYSIZE(cats))) {
		ed->category_filter = (combo_idx == 0) ? (u8)FORGE_CAT_COUNT : (u8)(combo_idx - 1);
	}

	/* Budget bar */
	ImU32 col = IM_COL32(100, 200, 120, 255);
	if (forgeBudgetOverSoft(&b)) col = IM_COL32(240, 200, 60, 255);
	if (forgeBudgetOverHard(&b)) col = IM_COL32(240, 100, 80, 255);
	ImGui::TextColored(ImColor(col), "Budget: %d/%d objects  %d/%d tris  %d/%d lights  %d/%d logic",
			b.objects, b.objects_soft, b.triangles, b.triangles_soft,
			b.lights, b.lights_soft, b.logic_nodes, b.logic_nodes_soft);

	ImGui::Separator();

	/* Categorised scrollable list */
	ImGui::BeginChild("CatalogList", ImVec2(0, 0),
		ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);

	/* Group by category for clarity */
	for (int c = 0; c < (int)FORGE_CAT_COUNT; ++c) {
		if (ed->category_filter != FORGE_CAT_COUNT && ed->category_filter != (u8)c) continue;
		int matched = 0;
		/* Count filter matches in this category first so we can skip empty headers. */
		for (int i = 0; i < forgeCatalogCount(); ++i) {
			const forge_catalog_entry_t *e = forgeCatalogGet(i);
			if (!e) continue;
			if ((int)e->category != c) continue;
			if (ed->search_filter[0]) {
				if (!f_icontains(e->name, ed->search_filter) &&
				    !f_icontains(e->id,   ed->search_filter) &&
				    !(e->tags && f_icontains(e->tags, ed->search_filter))) {
					continue;
				}
			}
			++matched;
		}
		if (!matched) continue;

		bool open = ImGui::CollapsingHeader(forgeCategoryName((forge_category_t)c),
				ed->search_filter[0] ? ImGuiTreeNodeFlags_DefaultOpen : 0);
		if (!open) continue;

		ImGui::Indent();
		for (int i = 0; i < forgeCatalogCount(); ++i) {
			const forge_catalog_entry_t *e = forgeCatalogGet(i);
			if (!e) continue;
			if ((int)e->category != c) continue;
			if (ed->search_filter[0]) {
				if (!f_icontains(e->name, ed->search_filter) &&
				    !f_icontains(e->id,   ed->search_filter) &&
				    !(e->tags && f_icontains(e->tags, ed->search_filter))) {
					continue;
				}
			}
			ImGui::PushID(i);
			char lbl[128];
			snprintf(lbl, sizeof(lbl), "%s##c%d", e->name, i);
			bool pressed = ImGui::Button(lbl, ImVec2(-1, 0));
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::Text("%s", e->id);
				if (e->tags) ImGui::TextColored(ImVec4(0.7f,0.85f,1.0f,1.0f), "tags: %s", e->tags);
				ImGui::Text("tri cost: %d", (int)e->tri_cost);
				ImGui::EndTooltip();
			}
			if (pressed) {
				/* Fix 8 (2026-05-01, Mike playtest): one-shot select-spawn-
				 * attach replaces the prior ghost-only flow. Picking a
				 * catalog entry now spawns the real object IMMEDIATELY at
				 * the freefly reticle and attaches it to the camera as
				 * the held object. The user can fly the object into place
				 * and press Activate (A on pad / D-pad RIGHT) to release,
				 * or Toggle (X / Tab) to switch to its Properties context.
				 *
				 * Old flow kept available as a fallback for edge cases:
				 * if the spawn-and-attach fails (budget hard-cap, OOM
				 * from forgeObjectAllocate, etc.) we fall back to the
				 * ghost flow so the user still sees a Cancel/Place Here
				 * banner instead of the click silently doing nothing. */
				forgePlaceBegin(e->id);

				/* Seed the ghost position from the camera so the very
				 * first commit lands at a sensible spot rather than
				 * at the origin (forgePlaceUpdate writes ghost_pos but
				 * is otherwise driven by the editor tick which fires
				 * AFTER this click has already committed). */
				struct forge_editor_coord { f32 x, y, z; } cpos = { 0.0f, 0.0f, 0.0f };
				forgeGetCameraPos((struct coord *)&cpos);
				f32 camera_pos[3] = { cpos.x, cpos.y, cpos.z };
				forgePlaceUpdate(camera_pos,
				                 forgeGetCameraYawDeg(),
				                 forgeGetCameraPitchDeg(),
				                 400.0f);
				s32 uid = forgePlaceCommit();
				forgePlaceCancel();  /* clear ghost state regardless of outcome */
				if (uid > 0) {
					forgeHeldSetUid((u32)uid);
					forgeSelectionSelectOnly((u32)uid);
				}
			}
			ImGui::PopID();
		}
		ImGui::Unindent();
	}

	ImGui::EndChild();
}

static void forgeEditVec3(const char *label, f32 v[3], f32 step = 1.0f)
{
	ImGui::InputFloat3(label, v, "%.2f");
	(void)step;
}

static void forgeEditColor(const char *label, f32 v[3])
{
	ImGui::ColorEdit3(label, v);
}

static void forgeDrawPropertiesTab(void)
{
	if (forgeSelectionCount() == 0) {
		ImGui::TextWrapped("No object selected.  Use the Catalog tab to place objects, "
				"or right-click a placed object in-world to select it (F2: integrate with "
				"3D picking pending).");
		return;
	}

	u32 focus_uid = forgeSelectionGet(0);
	forge_object_t *o = forgeObjectFindByUid(focus_uid);
	if (!o) { ImGui::Text("(stale selection)"); return; }

	ImGui::Text("UID %u  -  %s  [%s]", o->uid, o->catalog_id, fcat_label(o->category));
	if (o->from_base) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "[BASE]");
	}
	ImGui::Separator();

	/* S313 -- Variant delta controls for base-imported objects. */
	if (o->from_base) {
		ImGui::TextDisabled("This object came from the variant's base.  "
				"Changes are stored as a delta.");
		if (ImGui::Button("Reset to Base")) {
			forgeObjectResetToBase(o->uid);
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove Base Object (delta-delete)")) {
			u32 uid = o->uid;
			forgeObjectRemoveFromBase(uid);
			return; /* object is gone; early-out */
		}
		ImGui::Separator();
	}

	/* Label */
	ImGui::InputText("Label", o->label, FORGE_LABEL_LEN);

	/* Transform */
	ImGui::SeparatorText("Transform");
	forgeEditVec3("Position", o->pos);
	forgeEditVec3("Rotation (deg)", o->rot);
	forgeEditVec3("Scale",    o->scale);

	ImGui::SeparatorText("Appearance");
	bool visible = o->visible;
	if (ImGui::Checkbox("Visible", &visible)) o->visible = visible ? 1 : 0;
	ImGui::SameLine();
	bool shadows = o->cast_shadows;
	if (ImGui::Checkbox("Cast Shadows", &shadows)) o->cast_shadows = shadows ? 1 : 0;
	forgeEditColor("Tint", o->tint);
	ImGui::SliderFloat("Emissive", &o->emissive, 0.0f, 1.0f);
	int colmode = o->collision_mode;
	const char *collision_names[] = { "Solid", "Passthrough", "Projectile-Only" };
	if (ImGui::Combo("Collision", &colmode, collision_names, 3)) o->collision_mode = (u8)colmode;
	int team = o->team;
	if (ImGui::SliderInt("Team", &team, 0, 8, "%d (0=neutral)")) o->team = (u8)team;

	ImGui::SeparatorText("Type-Specific");
	switch (o->category) {
	case FORGE_CAT_WEAPON_PAD: {
		auto *p = &o->props.weapon;
		ImGui::InputText("Weapon ID (Default)", p->weapon_id, FORGE_ID_LEN);
		ImGui::TextDisabled("This is the map-author default.  At runtime the "
				"match-setup Weapon Source (Settings tab) decides whether "
				"this default, the lobby weapon set, or a hybrid wins.");
		/* Preview: what will this pad actually spawn based on current map source? */
		const forge_map_settings_t *ms = forgeMapSettings();
		const char *src_label =
			(ms->weapon_source == FORGE_WEAPONS_MAP_DEFAULTS)   ? "Map Defaults -> uses this weapon" :
			(ms->weapon_source == FORGE_WEAPONS_MATCH_OVERRIDE) ? "Match Override -> uses lobby weapon set" :
			                                                     "Prefer Map -> uses this (specific) weapon";
		ImGui::TextColored(ImVec4(0.7f, 0.95f, 1.0f, 1.0f), "Preview: %s", src_label);
		ImGui::InputInt("Ammo (-1 = weapon default)", &p->ammo);
		bool dual = p->dual_wield; if (ImGui::Checkbox("Dual Wield", &dual)) p->dual_wield = dual;
		ImGui::SliderFloat("Respawn (sec, 0 = one-time pickup)", &p->respawn_sec, 0.0f, 60.0f);
		int team_lock = p->team_lock;
		if (ImGui::SliderInt("Team Lock (0=any, 1..8)", &team_lock, 0, 8)) p->team_lock = (u8)team_lock;
		int respawn_effect = p->respawn_effect;
		const char *effects[] = { "None", "Glow", "Hologram" };
		if (ImGui::Combo("Respawn Effect", &respawn_effect, effects, 3)) p->respawn_effect = (u8)respawn_effect;
		break;
	}
	case FORGE_CAT_SPAWN_POINT: {
		auto *p = &o->props.spawn;
		const char *types[] = { "Initial", "Respawn", "Both" };
		int t = p->type; if (ImGui::Combo("Type", &t, types, 3)) p->type = (u8)t;
		int tm = p->team; if (ImGui::SliderInt("Team", &tm, 0, 8)) p->team = (u8)tm;
		ImGui::InputInt("Priority", &p->priority);
		ImGui::SliderFloat("Facing (deg)", &p->facing_deg, 0.0f, 360.0f);
		ImGui::SliderFloat("Zone Radius", &p->radius, 0.0f, 2000.0f);
		break;
	}
	case FORGE_CAT_AI: {
		auto *p = &o->props.ai;
		ImGui::InputText("Body ID",   p->body_id, FORGE_ID_LEN);
		ImGui::InputText("Head ID",   p->head_id, FORGE_ID_LEN);
		ImGui::InputText("Weapon ID", p->weapon_id, FORGE_ID_LEN);
		const char *behaviors[] = { "Patrol","Guard","Aggressive","Passive","Scripted" };
		int b = p->behavior; if (ImGui::Combo("Behavior", &b, behaviors, 5)) p->behavior = (u8)b;
		const char *factions[] = { "Friendly", "Hostile", "Neutral" };
		int fc = p->faction; if (ImGui::Combo("Faction", &fc, factions, 3)) p->faction = (u8)fc;
		ImGui::SliderFloat("Health x", &p->health_mult, 0.25f, 8.0f);
		ImGui::SliderFloat("Alert Radius", &p->alert_radius, 0.0f, 5000.0f);
		bool rs = p->respawn; if (ImGui::Checkbox("Respawn", &rs)) p->respawn = rs;
		if (p->respawn) ImGui::SliderFloat("Respawn Delay (s)", &p->respawn_delay_sec, 0.0f, 60.0f);

		ImGui::SeparatorText("Boss (F5b)");
		bool is_boss = p->is_boss;
		if (ImGui::Checkbox("Boss", &is_boss)) p->is_boss = is_boss;
		if (p->is_boss) {
			ImGui::InputText("Boss Name", p->boss_name, FORGE_NAME_LEN);
			ImGui::SliderFloat("Boss Scale", &p->boss_scale, 1.0f, 5.0f);
			int n = p->num_phase_thresholds;
			if (ImGui::SliderInt("Phase Threshold Count", &n, 0, 4)) p->num_phase_thresholds = (u8)n;
			for (int i = 0; i < p->num_phase_thresholds; ++i) {
				char buf[24]; snprintf(buf, sizeof(buf), "Phase %d threshold", i + 1);
				ImGui::SliderFloat(buf, &p->phase_thresholds[i], 0.0f, 100.0f, "%.0f%%");
			}
		}
		break;
	}
	case FORGE_CAT_INTERACTABLE:
		if (strncmp(o->catalog_id, "base:door_", 10) == 0) {
			auto *p = &o->props.door;
			const char *dirs[] = { "Slide Left","Slide Right","Slide Up","Swing" };
			int d = p->open_dir; if (ImGui::Combo("Open Direction", &d, dirs, 4)) p->open_dir = (u8)d;
			ImGui::SliderFloat("Open Speed (s)", &p->open_speed_sec, 0.1f, 10.0f);
			bool ac = p->auto_close; if (ImGui::Checkbox("Auto Close", &ac)) p->auto_close = ac;
			if (p->auto_close) ImGui::SliderFloat("Auto Close Delay", &p->auto_close_delay_sec, 0.5f, 10.0f);
			bool lk = p->locked; if (ImGui::Checkbox("Locked", &lk)) p->locked = lk;
			if (p->locked) ImGui::InputText("Key ID", p->key_id, FORGE_ID_LEN);
		} else if (strncmp(o->catalog_id, "base:elevator_", 14) == 0) {
			auto *p = &o->props.elev;
			int n = p->num_stops; if (ImGui::SliderInt("Stops", &n, 2, 8)) p->num_stops = (u8)n;
			ImGui::SliderFloat("Speed", &p->speed_units_per_sec, 50.0f, 2000.0f);
			ImGui::SliderFloat("Wait (s)", &p->wait_time_sec, 0.0f, 10.0f);
			bool cb = p->call_button; if (ImGui::Checkbox("Call Button", &cb)) p->call_button = cb;
			bool lp = p->loop; if (ImGui::Checkbox("Loop", &lp)) p->loop = lp;
			for (int i = 0; i < p->num_stops; ++i) {
				char buf[24]; snprintf(buf, sizeof(buf), "Stop %d Y", i);
				ImGui::InputFloat(buf, &p->stops_y[i]);
			}
		} else if (strncmp(o->catalog_id, "base:switch_", 12) == 0) {
			auto *p = &o->props.sw;
			const char *types[] = { "Toggle","Momentary","Hold" };
			int t = p->type; if (ImGui::Combo("Type", &t, types, 3)) p->type = (u8)t;
			const char *acts[] = { "Interact","Shoot","Proximity" };
			int a = p->activation; if (ImGui::Combo("Activation", &a, acts, 3)) p->activation = (u8)a;
			ImGui::SliderFloat("Cooldown (s)", &p->cooldown_sec, 0.0f, 30.0f);
			ImGui::InputText("Channel Out", p->channel_out, FORGE_NAME_LEN);
		}
		break;
	case FORGE_CAT_LIGHT: {
		auto *p = &o->props.light;
		const char *types[] = { "Point","Spot","Area","Emissive" };
		int t = p->type; if (ImGui::Combo("Light Type", &t, types, 4)) p->type = (u8)t;
		ImGui::ColorEdit3("Color", p->color);
		ImGui::SliderFloat("Intensity", &p->intensity, 0.0f, 4.0f);
		ImGui::SliderFloat("Range", &p->range, 50.0f, 5000.0f);
		if (p->type == FORGE_LIGHT_SPOT) {
			ImGui::SliderFloat("Inner Cone", &p->inner_cone_deg, 0.0f, 90.0f);
			ImGui::SliderFloat("Outer Cone", &p->outer_cone_deg, 0.0f, 90.0f);
			ImGui::InputText("Cookie Texture", p->cookie_texture, FORGE_NAME_LEN);
		}
		if (p->type == FORGE_LIGHT_AREA) {
			ImGui::SliderFloat("Width",  &p->width,  50.0f, 1000.0f);
			ImGui::SliderFloat("Height", &p->height, 50.0f, 1000.0f);
		}
		bool cs = p->cast_shadows; if (ImGui::Checkbox("Cast Shadows", &cs)) p->cast_shadows = cs;
		ImGui::SameLine();
		bool no = p->night_only; if (ImGui::Checkbox("Night Only", &no)) p->night_only = no;
		break;
	}
	case FORGE_CAT_ZONE: {
		auto *p = &o->props.zone;
		const char *types[] = {
			"Trigger","Radiation","Gravity","Teleporter","Kill","Water","Sound",
			"No-Weapon","Fog","Soft Bound","Team Spawn","Hill","Territory"
		};
		int t = p->type; if (ImGui::Combo("Zone Type", &t, types, IM_ARRAYSIZE(types))) p->type = (u8)t;
		const char *shapes[] = { "Box","Sphere" };
		int sh = p->shape; if (ImGui::Combo("Shape", &sh, shapes, 2)) p->shape = (u8)sh;
		forgeEditVec3("Size", p->size);
		int tf = p->team_filter; if (ImGui::SliderInt("Team Filter", &tf, 0, 8)) p->team_filter = (u8)tf;
		bool rep = p->once_or_repeat; if (ImGui::Checkbox("Repeating", &rep)) p->once_or_repeat = rep;
		ImGui::SliderFloat("Trigger Delay", &p->trigger_delay_sec, 0.0f, 10.0f);
		if (p->type == FORGE_ZONE_RADIATION)  ImGui::SliderFloat("Damage/sec", &p->damage_per_sec, 0.0f, 200.0f);
		if (p->type == FORGE_ZONE_GRAVITY) {
			ImGui::SliderFloat("Gravity Mult", &p->gravity_mult, 0.0f, 4.0f);
			forgeEditVec3("Gravity Dir", p->gravity_dir);
		}
		if (p->type == FORGE_ZONE_WATER)   forgeEditVec3("Current Dir", p->current_dir),
		                                   ImGui::SliderFloat("Current Speed", &p->current_speed, 0.0f, 500.0f);
		if (p->type == FORGE_ZONE_FOG)     forgeEditColor("Fog Color", p->fog_color),
		                                   ImGui::SliderFloat("Fog Density", &p->fog_density, 0.0f, 1.0f);
		if (p->type == FORGE_ZONE_TELEPORTER) {
			int tgt = (int)p->teleport_target_uid;
			if (ImGui::InputInt("Target UID", &tgt)) p->teleport_target_uid = (u32)tgt;
		}
		if (p->type == FORGE_ZONE_SOUND)   ImGui::InputText("Sound Loop ID", p->sound_loop_id, FORGE_NAME_LEN);
		if (p->type == FORGE_ZONE_KILL)    ImGui::InputText("Death Message", p->death_message, FORGE_NAME_LEN);
		ImGui::InputText("Channel on Enter", p->channel_on_enter, FORGE_NAME_LEN);
		ImGui::InputText("Channel on Exit",  p->channel_on_exit, FORGE_NAME_LEN);
		break;
	}
	case FORGE_CAT_EFFECT: {
		auto *p = &o->props.effect;
		const char *kinds[] = { "Particle","Sound","Decal","Screen Shake","Post-Process" };
		int k = p->kind; if (ImGui::Combo("Kind", &k, kinds, 5)) p->kind = (u8)k;
		ImGui::InputText("Asset ID", p->asset_id, FORGE_ID_LEN);
		bool lp = p->looping; if (ImGui::Checkbox("Looping", &lp)) p->looping = lp;
		ImGui::SliderFloat("Intensity", &p->intensity, 0.0f, 4.0f);
		ImGui::SliderFloat("Range", &p->range, 0.0f, 5000.0f);
		ImGui::ColorEdit3("Color", p->color);
		break;
	}
	case FORGE_CAT_PICKUP: {
		auto *p = &o->props.pickup;
		ImGui::InputText("Item ID", p->item_id, FORGE_ID_LEN);
		ImGui::InputInt("Quantity", &p->quantity);
		ImGui::SliderFloat("Respawn (s)", &p->respawn_sec, 0.0f, 120.0f);
		int tl = p->team_lock; if (ImGui::SliderInt("Team Lock", &tl, 0, 8)) p->team_lock = (u8)tl;
		break;
	}
	default:
		ImGui::TextDisabled("(no type-specific properties)");
		break;
	}

	ImGui::SeparatorText("Actions");
	if (ImGui::Button("Duplicate (+1 grid)")) {
		forgeSelectionDuplicate(forgeGetEditor()->grid_size, 0.0f, 0.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete")) forgeSelectionDelete();
	ImGui::SameLine();
	if (ImGui::Button("Deselect")) forgeSelectionClear();

	if (forgeSelectionCount() > 1) {
		ImGui::TextDisabled("(%d selected; properties shown for first)", forgeSelectionCount());
	}

	ImGui::SeparatorText("Gizmo (F2)");
	ImGui::TextWrapped("Direct 3D gizmo handles integrate with the freefly camera; "
			"for now, edit transform numerically above.  Grid snap is configurable in Settings.");
}

static void forgeDrawZonesTab(void)
{
	if (ImGui::Button("New Trigger Zone")) {
		forge_object_t *o = forgeObjectAllocate(FORGE_CAT_ZONE, "base:zone_trigger");
		if (o) { forgeSelectionSelectOnly(o->uid); }
	}
	ImGui::SameLine();
	if (ImGui::Button("New Kill Zone")) {
		forge_object_t *o = forgeObjectAllocate(FORGE_CAT_ZONE, "base:zone_kill");
		if (o) { forgeSelectionSelectOnly(o->uid); }
	}
	ImGui::SameLine();
	if (ImGui::Button("New Teleporter")) {
		forge_object_t *o = forgeObjectAllocate(FORGE_CAT_ZONE, "base:zone_teleporter");
		if (o) { forgeSelectionSelectOnly(o->uid); }
	}

	forge_editor_state_t *ed = forgeGetEditor();
	bool viz = ed->zone_viz_enabled;
	if (ImGui::Checkbox("Show Zone Wireframes (V)", &viz)) ed->zone_viz_enabled = viz ? 1 : 0;

	ImGui::Separator();
	ImGui::BeginTable("zones", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders);
	ImGui::TableSetupColumn("UID");
	ImGui::TableSetupColumn("Label");
	ImGui::TableSetupColumn("Type");
	ImGui::TableSetupColumn("Shape");
	ImGui::TableSetupColumn("Pos");
	ImGui::TableSetupColumn("");
	ImGui::TableHeadersRow();
	for (int i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		forge_object_t *o = forgeObjectGet(i);
		if (!o || !o->in_use) continue;
		if (o->category != FORGE_CAT_ZONE) continue;
		ImGui::PushID(i);
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::Text("%u", o->uid);
		ImGui::TableNextColumn(); ImGui::Text("%s", o->label[0] ? o->label : "(unnamed)");
		ImGui::TableNextColumn(); ImGui::Text("%s", fzone_name(o->props.zone.type));
		ImGui::TableNextColumn(); ImGui::Text("%s", o->props.zone.shape ? "Sphere" : "Box");
		ImGui::TableNextColumn(); ImGui::Text("%.0f %.0f %.0f", o->pos[0], o->pos[1], o->pos[2]);
		ImGui::TableNextColumn();
		if (ImGui::SmallButton("Select")) forgeSelectionSelectOnly(o->uid);
		ImGui::SameLine();
		if (ImGui::SmallButton("X")) forgeObjectRemove(o->uid);
		ImGui::PopID();
	}
	ImGui::EndTable();
}

static void forgeDrawLightingTab(void)
{
	forge_skylight_t *sky = forgeSkylight();
	forge_atmosphere_t *atm = forgeAtmosphere();

	ImGui::SeparatorText("Skylight (Directional)");
	ImGui::SliderFloat("Yaw (deg)",   &sky->direction_yaw_deg,   0.0f, 360.0f);
	ImGui::SliderFloat("Pitch (deg)", &sky->direction_pitch_deg, -89.0f, 89.0f);
	ImGui::ColorEdit3("Color", sky->color);
	ImGui::SliderFloat("Intensity", &sky->intensity, 0.0f, 4.0f);
	bool cs = sky->cast_shadow;
	if (ImGui::Checkbox("Cast Shadow", &cs)) sky->cast_shadow = cs ? 1 : 0;
	int sft = sky->shadow_softness;
	if (ImGui::SliderInt("Shadow Softness", &sft, 0, 4)) sky->shadow_softness = (u8)sft;

	ImGui::SeparatorText("Sky");
	ImGui::InputText("Sky ID (catalog)", atm->sky_id, FORGE_ID_LEN);
	ImGui::TextDisabled("examples: base:datadyne_night, base:carrington_day, "
			"base:skedar_homeworld, base:space_station, base:underground, "
			"base:storm, base:sunset");

	ImGui::SeparatorText("Atmosphere");
	bool fog = atm->fog_enable;
	if (ImGui::Checkbox("Fog", &fog)) atm->fog_enable = fog ? 1 : 0;
	if (atm->fog_enable) {
		ImGui::ColorEdit3("Fog Color", atm->fog_color);
		ImGui::SliderFloat("Fog Near", &atm->fog_near, 0.0f, 20000.0f);
		ImGui::SliderFloat("Fog Far",  &atm->fog_far,  0.0f, 50000.0f);
		ImGui::SliderFloat("Fog Height", &atm->fog_height, -4000.0f, 4000.0f);
	}
	ImGui::ColorEdit3("Ambient Color", atm->ambient_color);
	ImGui::SliderFloat("Ambient Intensity", &atm->ambient_intensity, 0.0f, 2.0f);
	ImGui::SliderFloat("Exposure", &atm->exposure, 0.1f, 4.0f);
	bool bloom = atm->bloom_enable;
	if (ImGui::Checkbox("Bloom", &bloom)) atm->bloom_enable = bloom ? 1 : 0;
	if (atm->bloom_enable)
		ImGui::SliderFloat("Bloom Strength", &atm->bloom_strength, 0.0f, 2.0f);
	const char *grades[] = { "Neutral","Warm","Cool","Noir","Alien" };
	int cg = atm->color_grade;
	if (ImGui::Combo("Color Grade", &cg, grades, 5)) atm->color_grade = (u8)cg;

	ImGui::SeparatorText("F8: Weather & Time of Day");
	const char *weather[] = { "Clear","Rain","Snow","Sandstorm","Storm" };
	int wk = atm->weather_kind;
	if (ImGui::Combo("Weather", &wk, weather, 5)) atm->weather_kind = (u8)wk;
	if (atm->weather_kind != 0) {
		int wi = atm->weather_intensity;
		if (ImGui::SliderInt("Weather Intensity", &wi, 0, 100)) atm->weather_intensity = (u8)wi;
	}
	bool tod = atm->tod_enable;
	if (ImGui::Checkbox("Time-of-Day Cycle", &tod)) atm->tod_enable = tod ? 1 : 0;
	if (atm->tod_enable) {
		ImGui::SliderFloat("Cycle (sec)", &atm->tod_cycle_sec, 60.0f, 3600.0f);
		ImGui::SliderFloat("Phase", &atm->tod_current_phase, 0.0f, 1.0f);
	}

	ImGui::SeparatorText("Placed Lights");
	if (ImGui::Button("New Point Light")) {
		forge_object_t *o = forgeObjectAllocate(FORGE_CAT_LIGHT, "base:light_point");
		if (o) forgeSelectionSelectOnly(o->uid);
	}
	ImGui::SameLine();
	if (ImGui::Button("New Spot Light")) {
		forge_object_t *o = forgeObjectAllocate(FORGE_CAT_LIGHT, "base:light_spot");
		if (o) forgeSelectionSelectOnly(o->uid);
	}
	ImGui::SameLine();
	if (ImGui::Button("New Area Light")) {
		forge_object_t *o = forgeObjectAllocate(FORGE_CAT_LIGHT, "base:light_area");
		if (o) forgeSelectionSelectOnly(o->uid);
	}

	int count = 0;
	for (int i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		forge_object_t *o = forgeObjectGet(i);
		if (!o || !o->in_use || o->category != FORGE_CAT_LIGHT) continue;
		++count;
	}
	ImGui::Text("Total placed lights: %d / %d (soft cap)", count, 32);
}

static void forgeDrawLogicTab(void)
{
	forgeScrollAnchorMaybe(FGT_CONTROL, 3);   /* sidebar "Channels" */
	ImGui::SeparatorText("Channels (global booleans)");
	static char new_channel[FORGE_NAME_LEN] = "";
	ImGui::InputText("Name##newch", new_channel, FORGE_NAME_LEN);
	ImGui::SameLine();
	if (ImGui::Button("Add")) {
		if (new_channel[0]) { forgeChannelCreate(new_channel); new_channel[0] = '\0'; }
	}
	if (ImGui::BeginTable("channels", 3, ImGuiTableFlags_Borders)) {
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("State");
		ImGui::TableSetupColumn("");
		ImGui::TableHeadersRow();
		for (int i = 0; i < FORGE_MAX_CHANNELS; ++i) {
			forge_channel_t *c = forgeChannelGet(i);
			if (!c || !c->in_use) continue;
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::Text("%s", c->name);
			ImGui::TableNextColumn();
			bool st = c->state;
			if (ImGui::Checkbox("##st", &st)) forgeChannelSet(c->name, st ? 1 : 0);
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("X")) c->in_use = 0;
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	forgeScrollAnchorMaybe(FGT_CONTROL, 0);   /* sidebar "Logic Nodes" */
	ImGui::SeparatorText("Nodes");
	static int add_kind = FORGE_LOGIC_EVENT;
	static int add_op = FORGE_OP_ON_ROUND_START;
	ImGui::RadioButton("Event",     &add_kind, FORGE_LOGIC_EVENT);     ImGui::SameLine();
	ImGui::RadioButton("Condition", &add_kind, FORGE_LOGIC_CONDITION); ImGui::SameLine();
	ImGui::RadioButton("Action",    &add_kind, FORGE_LOGIC_ACTION);

	/* Filter op list by kind. */
	int event_ops[] = {
		FORGE_OP_ON_PLAYER_ENTER, FORGE_OP_ON_PLAYER_EXIT, FORGE_OP_ON_OBJECT_DESTROYED,
		FORGE_OP_ON_SWITCH, FORGE_OP_ON_KILL, FORGE_OP_ON_KILL_COUNT, FORGE_OP_ON_ITEM_PICKUP,
		FORGE_OP_ON_TIMER, FORGE_OP_ON_ROUND_START, FORGE_OP_ON_ROUND_END, FORGE_OP_ON_CHANNEL,
		FORGE_OP_ON_INTERACT
	};
	int cond_ops[] = {
		FORGE_OP_HAS_ITEM, FORGE_OP_KILL_COUNT_GE, FORGE_OP_ALL_ENEMIES_DEAD,
		FORGE_OP_SWITCH_STATE, FORGE_OP_CHANNEL_STATE, FORGE_OP_TEAM_SCORE_GE,
		FORGE_OP_PLAYER_COUNT, FORGE_OP_TIMER_ELAPSED, FORGE_OP_RANDOM
	};
	int action_ops[] = {
		FORGE_OP_OPEN_DOOR, FORGE_OP_CLOSE_DOOR, FORGE_OP_ACTIVATE_ELEVATOR,
		FORGE_OP_SPAWN_OBJECT, FORGE_OP_SPAWN_AI, FORGE_OP_DESTROY_OBJECT,
		FORGE_OP_PLAY_SOUND, FORGE_OP_SHOW_MESSAGE, FORGE_OP_SET_CHANNEL,
		FORGE_OP_TELEPORT_PLAYER, FORGE_OP_CHANGE_ZONE, FORGE_OP_SET_TIMER,
		FORGE_OP_AWARD_SCORE, FORGE_OP_END_MISSION, FORGE_OP_LOCK_DOOR,
		FORGE_OP_UNLOCK_DOOR, FORGE_OP_ENABLE_OBJECT, FORGE_OP_DISABLE_OBJECT,
		FORGE_OP_CAMERA_EVENT, FORGE_OP_SPAWN_WAVE, FORGE_OP_TRIGGER_BOSS_PHASE,
		FORGE_OP_OBJECTIVE_COMPLETE, FORGE_OP_OBJECTIVE_FAIL
	};
	int *ops; int ops_count;
	switch (add_kind) {
	case FORGE_LOGIC_EVENT:     ops = event_ops;  ops_count = IM_ARRAYSIZE(event_ops);  break;
	case FORGE_LOGIC_CONDITION: ops = cond_ops;   ops_count = IM_ARRAYSIZE(cond_ops);   break;
	default:                    ops = action_ops; ops_count = IM_ARRAYSIZE(action_ops); break;
	}

	/* Find current op in list or reset to first. */
	int sel_idx = 0;
	for (int i = 0; i < ops_count; ++i) if (ops[i] == add_op) { sel_idx = i; break; }
	if (ImGui::BeginCombo("Op", flogic_op_name(ops[sel_idx]))) {
		for (int i = 0; i < ops_count; ++i) {
			bool is_sel = (i == sel_idx);
			if (ImGui::Selectable(flogic_op_name(ops[i]), is_sel)) {
				add_op = ops[i];
			}
		}
		ImGui::EndCombo();
	}
	add_op = ops[sel_idx];
	if (ImGui::Button("Add Node")) {
		forgeLogicNodeAllocate((forge_logic_kind_t)add_kind, (forge_logic_op_t)add_op);
	}
	ImGui::SameLine();
	if (ImGui::Button("Detect Cycles")) {
		s32 w = 0;
		forgeLogicDetectCycles(&w);
		if (w == 0) ImGui::OpenPopup("cycles_ok");
	}
	if (ImGui::BeginPopup("cycles_ok")) {
		ImGui::Text("No cycles detected.");
		ImGui::EndPopup();
	}

	ImGui::Separator();
	if (ImGui::BeginTable("nodes", 7, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
		ImGui::TableSetupColumn("UID");
		ImGui::TableSetupColumn("Kind");
		ImGui::TableSetupColumn("Op");
		ImGui::TableSetupColumn("Label");
		ImGui::TableSetupColumn("Param A");
		ImGui::TableSetupColumn("Target A");
		ImGui::TableSetupColumn("");
		ImGui::TableHeadersRow();
		for (int i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
			forge_logic_node_t *n = forgeLogicNodeGet(i);
			if (!n || !n->in_use) continue;
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::Text("%u", n->uid);
			ImGui::TableNextColumn();
			ImGui::TextColored(ImColor(flogic_kind_color(n->kind)), "%s", flogic_kind_name(n->kind));
			ImGui::TableNextColumn(); ImGui::Text("%s", flogic_op_name(n->op));
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			ImGui::InputText("##lbl", n->label, FORGE_LABEL_LEN);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			ImGui::InputText("##pa", n->param_text_a, FORGE_TEXT_LEN);
			ImGui::TableNextColumn();
			int tgt = (int)n->target_uid_a;
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputInt("##tgt", &tgt, 0, 0)) n->target_uid_a = (u32)tgt;
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("Fire")) {
				if (n->kind == FORGE_LOGIC_EVENT) forgeLogicFireEvent((forge_logic_op_t)n->op, n->target_uid_a);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X")) forgeLogicNodeRemove(n->uid);
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	forgeScrollAnchorMaybe(FGT_CONTROL, 1);   /* sidebar "Wires" */
	ImGui::SeparatorText("Wires");
	static u32 wire_src = 0, wire_dst = 0;
	int src = (int)wire_src, dst = (int)wire_dst;
	ImGui::InputInt("Source UID", &src);
	ImGui::InputInt("Dest UID",   &dst);
	wire_src = (u32)src; wire_dst = (u32)dst;
	if (ImGui::Button("Create Wire")) {
		s32 idx = forgeLogicWireCreate(wire_src, wire_dst, 0, 0);
		if (idx < 0) ImGui::OpenPopup("wire_err");
	}
	if (ImGui::BeginPopup("wire_err")) {
		ImGui::TextColored(ImVec4(1,0.6f,0.6f,1), "Wire rejected (invalid UIDs or pool full).");
		ImGui::EndPopup();
	}

	if (ImGui::BeginTable("wires", 4, ImGuiTableFlags_Borders)) {
		ImGui::TableSetupColumn("#");
		ImGui::TableSetupColumn("Src");
		ImGui::TableSetupColumn("Dst");
		ImGui::TableSetupColumn("");
		ImGui::TableHeadersRow();
		for (int i = 0; i < FORGE_MAX_LOGIC_WIRES; ++i) {
			forge_logic_wire_t *wi = forgeLogicWireGet(i);
			if (!wi || !wi->in_use) continue;
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::Text("%d", i);
			ImGui::TableNextColumn(); ImGui::Text("%u", wi->src_node_uid);
			ImGui::TableNextColumn(); ImGui::Text("%u", wi->dst_node_uid);
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("X")) forgeLogicWireRemove(i);
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}

static void forgeDrawGameTypeTab(void)
{
	forge_gametype_t *gt = forgeGameType();
	ImGui::InputText("Name", gt->name, FORGE_NAME_LEN);
	ImGui::InputTextMultiline("Description", gt->description, FORGE_DESC_LEN, ImVec2(-1, 60));

	const char *structures[] = { "Single Round","Best of N","Wave-Based (PvE)","Phase-Based" };
	int s = gt->structure;
	if (ImGui::Combo("Structure", &s, structures, 4)) gt->structure = (u8)s;
	if (gt->structure == FORGE_GT_BEST_OF_N) {
		int nr = gt->num_rounds;
		if (ImGui::SliderInt("Rounds", &nr, 1, 9)) gt->num_rounds = (u8)nr;
	}
	const char *wins[] = { "Score Target","Last Alive","Timer Expires","Boss Killed","Objectives Complete" };
	int wc = gt->win_condition;
	if (ImGui::Combo("Win Condition", &wc, wins, 5)) gt->win_condition = (u8)wc;
	ImGui::InputInt("Score Limit", &gt->score_limit);
	ImGui::InputInt("Time Limit (sec)", &gt->time_limit_sec);

	forgeScrollAnchorMaybe(FGT_GAMEPLAY, 1);   /* sidebar "Scoring" */
	ImGui::SeparatorText("Scoring");
	ImGui::InputInt("Per Kill",      &gt->score_per_kill);
	ImGui::InputInt("Per Headshot",  &gt->score_per_headshot);
	ImGui::InputInt("Per Objective", &gt->score_per_objective);
	ImGui::InputInt("Per Survive Sec",&gt->score_per_survive_sec);

	forgeScrollAnchorMaybe(FGT_GAMEPLAY, 2);   /* sidebar "Players" */
	ImGui::SeparatorText("Player Setup");
	ImGui::InputText("Starting Weapon", gt->starting_weapon, FORGE_ID_LEN);
	ImGui::SliderFloat("Health x", &gt->health_mult, 0.25f, 8.0f);
	const char *roles[] = { "None","Infection","VIP","Juggernaut","Horde Defender","Gun-Game Hunter" };
	int rm = gt->role_mode;
	if (ImGui::Combo("Role Mode", &rm, roles, 6)) gt->role_mode = (u8)rm;

	forgeScrollAnchorMaybe(FGT_GAMEPLAY, 3);   /* sidebar "Modifiers" */
	ImGui::SeparatorText("Modifiers");
	u32 mflags = gt->modifier_flags;
	bool mlow = (mflags & FORGE_MOD_LOW_GRAVITY)   != 0;
	bool mone = (mflags & FORGE_MOD_ONE_HIT_KILLS) != 0;
	bool mina = (mflags & FORGE_MOD_INFINITE_AMMO) != 0;
	bool mrad = (mflags & FORGE_MOD_NO_RADAR)      != 0;
	bool mff  = (mflags & FORGE_MOD_FRIENDLY_FIRE) != 0;
	bool maa  = (mflags & FORGE_MOD_NO_AUTO_AIM)   != 0;
	bool mfm  = (mflags & FORGE_MOD_FAST_MOVE)     != 0;
	bool mts  = (mflags & FORGE_MOD_TEAM_SHUFFLE)  != 0;
	if (ImGui::Checkbox("Low Gravity",    &mlow)) mflags = mlow ? (mflags | FORGE_MOD_LOW_GRAVITY) : (mflags & ~FORGE_MOD_LOW_GRAVITY);
	ImGui::SameLine();
	if (ImGui::Checkbox("One-Hit Kills",  &mone)) mflags = mone ? (mflags | FORGE_MOD_ONE_HIT_KILLS) : (mflags & ~FORGE_MOD_ONE_HIT_KILLS);
	if (ImGui::Checkbox("Infinite Ammo",  &mina)) mflags = mina ? (mflags | FORGE_MOD_INFINITE_AMMO) : (mflags & ~FORGE_MOD_INFINITE_AMMO);
	ImGui::SameLine();
	if (ImGui::Checkbox("No Radar",       &mrad)) mflags = mrad ? (mflags | FORGE_MOD_NO_RADAR) : (mflags & ~FORGE_MOD_NO_RADAR);
	if (ImGui::Checkbox("Friendly Fire",  &mff))  mflags = mff  ? (mflags | FORGE_MOD_FRIENDLY_FIRE) : (mflags & ~FORGE_MOD_FRIENDLY_FIRE);
	ImGui::SameLine();
	if (ImGui::Checkbox("No Auto-Aim",    &maa))  mflags = maa  ? (mflags | FORGE_MOD_NO_AUTO_AIM) : (mflags & ~FORGE_MOD_NO_AUTO_AIM);
	if (ImGui::Checkbox("Fast Movement",  &mfm))  mflags = mfm  ? (mflags | FORGE_MOD_FAST_MOVE) : (mflags & ~FORGE_MOD_FAST_MOVE);
	ImGui::SameLine();
	if (ImGui::Checkbox("Team Shuffle",   &mts))  mflags = mts  ? (mflags | FORGE_MOD_TEAM_SHUFFLE) : (mflags & ~FORGE_MOD_TEAM_SHUFFLE);
	gt->modifier_flags = mflags;

	forgeScrollAnchorMaybe(FGT_GAMEPLAY, 4);   /* sidebar "HUD" */
	ImGui::SeparatorText("HUD");
	bool hw = gt->show_wave_counter; if (ImGui::Checkbox("Wave Counter",  &hw)) gt->show_wave_counter = hw;
	ImGui::SameLine();
	bool hb = gt->show_boss_bar;     if (ImGui::Checkbox("Boss Bar",       &hb)) gt->show_boss_bar = hb;
	bool hr = gt->show_role_indicator;if (ImGui::Checkbox("Role Indicator",&hr)) gt->show_role_indicator = hr;
	ImGui::SameLine();
	bool hs = gt->show_survival_timer;if (ImGui::Checkbox("Survival Timer",&hs)) gt->show_survival_timer = hs;

	forgeScrollAnchorMaybe(FGT_GAMEPLAY, 5);   /* sidebar "Waves" */
	ImGui::SeparatorText("Waves (F5b Wave Spawner)");
	if (ImGui::Button("Add Wave")) {
		if (gt->num_waves < FORGE_MAX_WAVES) {
			forge_wave_t *w = &gt->waves[gt->num_waves++];
			memset(w, 0, sizeof(*w));
			w->in_use = 1;
			w->enemy_count = 4;
			w->enemy_scale = 1.0f;
			w->enemy_health_mult = 1.0f;
			w->enemy_speed_mult = 1.0f;
			w->spawn_delay_sec = 1.0f;
			w->intermission_sec = 10.0f;
			snprintf(w->enemy_catalog_id, FORGE_ID_LEN, "base:ai_guard_skedar");
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear Waves")) gt->num_waves = 0;

	if (ImGui::BeginTable("waves", 9, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
		ImGui::TableSetupColumn("#");
		ImGui::TableSetupColumn("Enemy");
		ImGui::TableSetupColumn("Count");
		ImGui::TableSetupColumn("Scale");
		ImGui::TableSetupColumn("HP x");
		ImGui::TableSetupColumn("Delay");
		ImGui::TableSetupColumn("Interm.");
		ImGui::TableSetupColumn("Zone UID");
		ImGui::TableSetupColumn("Boss?");
		ImGui::TableHeadersRow();
		for (int i = 0; i < gt->num_waves; ++i) {
			forge_wave_t *w = &gt->waves[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::Text("%d", i + 1);
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputText("##e",  w->enemy_catalog_id, FORGE_ID_LEN);
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputInt  ("##c", &w->enemy_count);
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##s", &w->enemy_scale);
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##h", &w->enemy_health_mult);
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##d", &w->spawn_delay_sec);
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##im", &w->intermission_sec);
			ImGui::TableNextColumn();
			int zu = (int)w->spawn_zone_uid;
			ImGui::SetNextItemWidth(-1); if (ImGui::InputInt("##z", &zu)) w->spawn_zone_uid = (u32)zu;
			ImGui::TableNextColumn();
			bool boss = w->is_boss; if (ImGui::Checkbox("##b", &boss)) w->is_boss = boss;
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Boss State (runtime)");
	forge_boss_state_t *bs = forgeBossState();
	if (bs->active) {
		ImGui::Text("Active: '%s'", bs->name);
		ImGui::ProgressBar(bs->max_health > 0.0f ? bs->current_health / bs->max_health : 0.0f,
				ImVec2(-1, 0), "");
		ImGui::Text("HP %.0f / %.0f (phase %d/%d)",
				bs->current_health, bs->max_health, bs->current_phase + 1, bs->num_phases);
		if (ImGui::Button("Damage 50 HP")) forgeBossApplyDamage(bs->chr_uid, 50.0f);
	} else {
		ImGui::TextDisabled("No boss currently active.");
	}
	if (ImGui::Button("Simulate: Activate Boss (HP=500)")) {
		f32 phases[3] = { 75.0f, 50.0f, 25.0f };
		forgeBossSetActive(1, "Simulated Boss", 500.0f, phases, 3);
	}
}

static void forgeDrawMissionTab(void)
{
	forge_map_settings_t *s = forgeMapSettings();
	bool is_mission = s->is_mission;
	if (ImGui::Checkbox("Is Mission (appears in solo/co-op)", &is_mission)) s->is_mission = is_mission;
	if (!s->is_mission) {
		ImGui::TextDisabled("Enable 'Is Mission' to author objectives and briefings.");
		return;
	}

	ImGui::InputTextMultiline("Briefing", s->briefing_text, FORGE_DESC_LEN, ImVec2(-1, 80));
	ImGui::InputTextMultiline("Debrief",  s->debrief_text,  FORGE_DESC_LEN, ImVec2(-1, 60));
	bool seq = s->sequential_objectives;
	if (ImGui::Checkbox("Sequential objectives (must complete in order)", &seq))
		s->sequential_objectives = seq;

	ImGui::SeparatorText("Objectives");
	if (ImGui::Button("Add Primary"))   forgeObjectiveAllocate(FORGE_OBJ_PRIMARY);
	ImGui::SameLine();
	if (ImGui::Button("Add Secondary")) forgeObjectiveAllocate(FORGE_OBJ_SECONDARY);
	ImGui::SameLine();
	if (ImGui::Button("Add Bonus"))     forgeObjectiveAllocate(FORGE_OBJ_BONUS);

	if (ImGui::BeginTable("objs", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
		ImGui::TableSetupColumn("#");
		ImGui::TableSetupColumn("Kind");
		ImGui::TableSetupColumn("Desc");
		ImGui::TableSetupColumn("Complete Node");
		ImGui::TableSetupColumn("Fail Node");
		ImGui::TableSetupColumn("");
		ImGui::TableHeadersRow();
		for (int i = 0; i < FORGE_MAX_OBJECTIVES; ++i) {
			forge_objective_t *o = forgeObjectiveGet(i);
			if (!o || !o->in_use) continue;
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::Text("%d", i + 1);
			ImGui::TableNextColumn();
			const char *kinds[] = { "Primary","Secondary","Bonus" };
			int k = o->kind; ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo("##k", &k, kinds, 3)) o->kind = (u8)k;
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputText("##d", o->description, FORGE_TEXT_LEN);
			ImGui::TableNextColumn(); int cn = (int)o->completion_node_uid; ImGui::SetNextItemWidth(-1);
			if (ImGui::InputInt("##cn", &cn, 0, 0)) o->completion_node_uid = (u32)cn;
			ImGui::TableNextColumn(); int fn = (int)o->failure_node_uid; ImGui::SetNextItemWidth(-1);
			if (ImGui::InputInt("##fn", &fn, 0, 0)) o->failure_node_uid = (u32)fn;
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("Complete")) forgeObjectiveSetStatus(i, FORGE_OBJ_COMPLETE);
			ImGui::SameLine();
			if (ImGui::SmallButton("Fail"))     forgeObjectiveSetStatus(i, FORGE_OBJ_FAILED);
			ImGui::SameLine();
			if (ImGui::SmallButton("X"))        forgeObjectiveRemove(i);
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::TextDisabled("Objectives update their status when the linked logic node fires "
			"(use OBJECTIVE_COMPLETE / OBJECTIVE_FAIL action nodes in the Logic tab).");
}

/* ============================================================
 * S313 -- Live Bot testing tab.
 *
 * Author can add/remove bots on the fly without a match restart.
 * Bots can be Active (fighting) or Frozen (for spawn-spot testing).
 * Spawn mode picks whether respawns happen anywhere, near the
 * requesting player, or bias toward combat flow.
 *
 * Requests are consumed by forgeRuntimeTick while the playtest is active.
 * ============================================================ */

static void forgeDrawBotsTab(void)
{
	forge_bot_settings_t *bs = forgeBotSettings();

	ImGui::SeparatorText("Live Bot Testing");
	ImGui::TextWrapped(
			"Spawn bots on-the-fly without a match restart.  Active bots fight; "
			"frozen bots hold their spawn spot so you can validate placement "
			"without combat noise. Spawn mode controls placement or bot skill.");
	ImGui::Spacing();

	/* Live counter readout. */
	ImGui::Text("Requested active: %d    Requested frozen: %d    Global freeze: %s",
			(int)bs->active_count, (int)bs->frozen_count,
			bs->all_frozen ? "YES" : "no");

	ImGui::SeparatorText("Add / Remove");
	const bool atCapacity = (int)bs->active_count + (int)bs->frozen_count >= MAX_BOTS;
	if (atCapacity) ImGui::BeginDisabled();
	if (ImGui::Button("+ Add Active Bot", ImVec2(-1, 0))) {
		forgeBotAddRequest(1);
	}
	if (ImGui::Button("+ Add Frozen Bot", ImVec2(-1, 0))) {
		forgeBotAddRequest(0);
	}
	if (atCapacity) ImGui::EndDisabled();
	if (atCapacity) ImGui::TextDisabled("Maximum of %d requested bots reached.", MAX_BOTS);
	if (ImGui::Button("Remove All Bots", ImVec2(-1, 0))) {
		forgeBotRemoveAll();
	}

	ImGui::SeparatorText("Freeze State");
	bool freeze = bs->all_frozen;
	if (ImGui::Checkbox("Freeze all bots (hold positions)", &freeze)) {
		forgeBotFreezeAll(freeze ? 1 : 0);
	}
	ImGui::TextDisabled("Frozen bots keep their spawn coords fixed -- useful "
			"for validating team-spawn zones and LoS to key pads.");

	ImGui::SeparatorText("Spawn Mode");
	const char *modes[] = {
			"Any valid spawn point (default)",
			"Near Me -- place ahead of the player",
			"Smart -- choose skill from Aggression"
	};
	int sm = bs->spawn_mode;
	if (ImGui::Combo("Mode", &sm, modes, 3)) bs->spawn_mode = (u8)sm;

	if (bs->spawn_mode == FORGE_BOT_SPAWN_NEAR_ME) {
		ImGui::SliderFloat("Near-Me Radius (units)",
				&bs->near_me_radius, 100.0f, 5000.0f, "%.0f");
		ImGui::TextDisabled("New bots spawn this far ahead of the player.");
	} else if (bs->spawn_mode == FORGE_BOT_SPAWN_SMART) {
		ImGui::SliderFloat("Smart Aggression",
				&bs->smart_aggression, 0.0f, 1.0f, "%.2f");
		ImGui::TextDisabled("0 = Meat bot skill, 1 = Dark bot skill. "
				"Applied when each new bot is created.");
	}

	ImGui::SeparatorText("Bot Defaults");
	ImGui::InputText("Default Body ID", bs->default_body_id, FORGE_ID_LEN);
	const char *difficulties[] = { "meat", "easy", "normal", "hard", "perfect", "dark" };
	int difficulty = 2;
	for (int i = 0; i < 6; ++i) {
		if (strcmp(bs->default_difficulty, difficulties[i]) == 0) difficulty = i;
	}
	if (ImGui::Combo("Difficulty", &difficulty, difficulties, 6)) {
		snprintf(bs->default_difficulty, FORGE_NAME_LEN, "%s", difficulties[difficulty]);
	}
	ImGui::TextDisabled("Used for Any and Near Me; Smart uses Aggression instead.");

	ImGui::SeparatorText("Runtime Requests");
	ImGui::TextDisabled("Pending: +%d active  +%d frozen  %d remove-all",
			bs->pending_add_active, bs->pending_add_frozen, bs->pending_remove_all);
}

static void forgeDrawSettingsTab(void)
{
	forge_map_settings_t *s = forgeMapSettings();
	forge_editor_state_t *ed = forgeGetEditor();
	forge_budget_stats_t b; forgeBudgetCompute(&b);

	forgeScrollAnchorMaybe(FGT_SETUP, 0);   /* sidebar "Map Metadata" */
	ImGui::SeparatorText("Map Metadata");
	ImGui::InputText("Map Name",    s->map_name, FORGE_NAME_LEN);
	ImGui::InputText("Author",      s->author,   FORGE_NAME_LEN);
	ImGui::InputTextMultiline("Description", s->description, FORGE_DESC_LEN, ImVec2(-1, 60));

	forgeScrollAnchorMaybe(FGT_SETUP, 1);   /* sidebar "Variant" */
	ImGui::SeparatorText("Variant Mode (S313)");
	const char *variant_labels[] = {
		"New Empty Map -- blank canvas, author-placed objects only",
		"Edit Existing Stage -- start from an engine stage, track deltas",
		"Edit Existing Map -- start from another Grid map, track deltas",
	};
	int vm = s->variant_mode;
	if (ImGui::Combo("Variant", &vm, variant_labels, 3)) {
		s->variant_mode = (u8)vm;
	}
	if (s->variant_mode == FORGE_VARIANT_NEW_EMPTY) {
		ImGui::InputText("Base Stage ID", s->base_stage_id, FORGE_ID_LEN);
		ImGui::TextDisabled("Loader will drop the author's objects onto this "
				"engine stage.  Use base:training for the default sandbox.");
	} else if (s->variant_mode == FORGE_VARIANT_EDIT_STAGE) {
		ImGui::InputText("Base Stage ID", s->base_stage_id, FORGE_ID_LEN);
		if (ImGui::Button("Import Base Stage Now -> mark placed objects as from_base")) {
			forgeImportBaseStageObjects();
		}
		ImGui::TextDisabled("%d base objects  |  %d author deltas",
				forgeCountBaseObjects(), forgeCountDeltaObjects());
		ImGui::TextDisabled("The save format elides unchanged base objects; only "
				"added / moved / modified objects land in map.json as deltas.");
	} else { /* FORGE_VARIANT_EDIT_MAP */
		ImGui::InputText("Variant Source Slug", s->variant_source_slug, FORGE_NAME_LEN);
		ImGui::TextDisabled("Points to another Grid map mod.  The loader opens "
				"that map's base_stage + objects first, then layers the current "
				"map's author changes on top.");
		ImGui::TextDisabled("%d base objects  |  %d author deltas",
				forgeCountBaseObjects(), forgeCountDeltaObjects());
	}

	forgeScrollAnchorMaybe(FGT_SETUP, 2);   /* sidebar "Players" */
	ImGui::SeparatorText("Players & Spawn");
	int mp = s->max_players;
	if (ImGui::SliderInt("Max Players", &mp, 2, 32)) s->max_players = (u8)mp;
	int rp = s->recommended_players;
	if (ImGui::SliderInt("Recommended Players", &rp, 1, 32)) s->recommended_players = (u8)rp;
	const char *sizes[] = { "Small","Medium","Large" };
	int sz = s->map_size_tag; if (ImGui::Combo("Size Tag", &sz, sizes, 3)) s->map_size_tag = (u8)sz;
	const char *tsm[] = { "Scattered","Zoned","Symmetric" };
	int tsmv = s->team_spawn_mode; if (ImGui::Combo("Team Spawn Mode", &tsmv, tsm, 3)) s->team_spawn_mode = (u8)tsmv;
	ImGui::SliderFloat("Respawn Delay (s)", &s->respawn_delay_sec, 0.0f, 10.0f);
	ImGui::SliderFloat("Spawn Protection (s)", &s->spawn_protection_sec, 0.0f, 10.0f);

	ImGui::SeparatorText("Weapon Source (R2/R4)");
	const char *wsrc[] = {
		"Map Defaults -- each pad spawns its author-chosen weapon",
		"Match Override -- lobby weapon set overrides every pad",
		"Prefer Map -- specific pads keep their weapon; generic pads use lobby"
	};
	int ws = s->weapon_source;
	if (ImGui::Combo("Weapon Source", &ws, wsrc, 3)) s->weapon_source = (u8)ws;
	bool amo = s->allow_match_override;
	if (ImGui::Checkbox("Allow match to offer 'Use Map Defaults' checkbox", &amo))
		s->allow_match_override = amo ? 1 : 0;
	ImGui::TextDisabled("Modded weapons participate automatically; the map's "
			"mod.json dependencies list ensures clients have them available.");

	ImGui::SeparatorText("Default Rules");
	ImGui::InputInt("Time Limit (s)", &s->default_time_limit_sec);
	ImGui::InputInt("Score Limit",    &s->default_score_limit);
	const char *hp[] = { "Normal","200%","50%" };
	int h = s->health_setting; if (ImGui::Combo("Health", &h, hp, 3)) s->health_setting = (u8)h;
	const char *rad[] = { "On","Off","Proximity" };
	int r = s->radar_setting; if (ImGui::Combo("Radar", &r, rad, 3)) s->radar_setting = (u8)r;
	bool aa = s->auto_aim; if (ImGui::Checkbox("Auto-Aim", &aa)) s->auto_aim = aa;
	ImGui::SameLine();
	bool ff = s->friendly_fire; if (ImGui::Checkbox("Friendly Fire", &ff)) s->friendly_fire = ff;
	ImGui::SameLine();
	bool ohk = s->one_hit_kills; if (ImGui::Checkbox("One-Hit Kills", &ohk)) s->one_hit_kills = ohk;

	ImGui::SeparatorText("Bounds");
	ImGui::InputFloat3("Bounds Min", s->bounds_min);
	ImGui::InputFloat3("Bounds Max", s->bounds_max);
	ImGui::SliderFloat("Soft-Bound Timer (s)", &s->soft_bounds_timer_sec, 0.0f, 30.0f);

	ImGui::SeparatorText("Grid / Snap");
	ImGui::SliderFloat("Grid Size", &s->grid_size, 10.0f, 2000.0f);
	ed->grid_size = s->grid_size;
	ImGui::SliderFloat("Rotation Snap (deg)", &s->rotation_snap_deg, 1.0f, 90.0f);
	ed->rotation_snap_deg = s->rotation_snap_deg;
	bool ss = s->surface_snap; if (ImGui::Checkbox("Surface Snap", &ss)) s->surface_snap = ss;
	ImGui::SameLine();
	bool es = s->edge_snap; if (ImGui::Checkbox("Edge Snap", &es)) s->edge_snap = es;

	ImGui::SeparatorText("Editor Toggles");
	bool gs = ed->snap_grid_enabled;
	if (ImGui::Checkbox("Grid Snap (editor)", &gs)) ed->snap_grid_enabled = gs ? 1 : 0;
	ImGui::SameLine();
	bool srf = ed->snap_surface_enabled;
	if (ImGui::Checkbox("Surface Snap (editor)", &srf)) ed->snap_surface_enabled = srf ? 1 : 0;
	bool ps = ed->paused_sim;
	if (ImGui::Checkbox("Pause Simulation While Editing", &ps)) ed->paused_sim = ps ? 1 : 0;
	bool zv = ed->zone_viz_enabled;
	if (ImGui::Checkbox("Show Zone Wireframes (V)", &zv)) ed->zone_viz_enabled = zv ? 1 : 0;
	ImGui::SameLine();
	bool lv = ed->logic_wires_viz_enabled;
	if (ImGui::Checkbox("Show Logic Wires (L)", &lv)) ed->logic_wires_viz_enabled = lv ? 1 : 0;

	ImGui::SeparatorText("Budget");
	ImGui::Text("Objects  %d / %d soft / %d hard", b.objects, b.objects_soft, b.objects_hard);
	ImGui::Text("Triangles %d / %d soft / %d hard", b.triangles, b.triangles_soft, b.triangles_hard);
	ImGui::Text("Lights   %d / %d soft / %d hard",  b.lights, b.lights_soft, b.lights_hard);
	ImGui::Text("Logic    %d / %d soft / %d hard",  b.logic_nodes, b.logic_nodes_soft, b.logic_nodes_hard);
	ImGui::Text("Effects  %d / %d soft / %d hard",  b.effects, b.effects_soft, b.effects_hard);
	ImGui::Text("Audio    %d / %d soft / %d hard",  b.audio_emitters, b.audio_emitters_soft, b.audio_emitters_hard);

	ImGui::SeparatorText("Mod Dependencies (R3)");
	{
		/* Collect dependencies live each frame so the preview stays
		 * fresh while the author is editing.  Capped at FORGE_MAX_DEPENDENCIES. */
		char deps[FORGE_MAX_DEPENDENCIES][FORGE_ID_LEN];
		s32 nd = forgeCollectDependencies(deps, FORGE_MAX_DEPENDENCIES);
		if (nd == 0) {
			ImGui::TextDisabled("No mod dependencies.  This map only uses base-game assets.");
		} else {
			ImGui::Text("%d unique mod asset(s) referenced (auto-included with map on share):", nd);
			ImGui::BeginChild("DepsList", ImVec2(-1, 120), true);
			for (s32 i = 0; i < nd; ++i) {
				ImGui::BulletText("%s", deps[i]);
			}
			ImGui::EndChild();
		}
		ImGui::TextDisabled("The mod distribution system recursively resolves these -- "
				"e.g. a custom weapon mod's own texture-pack dep comes along too.");
	}

	ImGui::SeparatorText("Save / Load");
	static bool s_ShowOldGridSave = false;
	ImGui::Checkbox("Show the old Grid map save (mods/Forge Maps)", &s_ShowOldGridSave);
	if (!s_ShowOldGridSave) {
		ImGui::TextDisabled("Level File saves a level mod for %s and match setup.", forgeDisplayName());
	}
	static const char savePopup[] = "Confirm Save -- Mod Dependencies";
	static char save_slug[FORGE_NAME_LEN] = "";
	if (s_ShowOldGridSave) {
	if (save_slug[0] == '\0') f_slug_from_name(s->map_name, save_slug, sizeof(save_slug));
	ImGui::InputText("Mod Slug", save_slug, FORGE_NAME_LEN);
	if (ImGui::Button("Save As Mod") && save_slug[0]) {
		const bool controlsWereActive = forgeEditorControlsActive();
		if (forgeEditorAcquireControls()) {
			if (!controlsWereActive) s_TransientEditorControls = true;
			ImGui::OpenPopup(savePopup);
			pdguiNavSuppressOpeningGesture();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Quick Re-Derive Slug")) {
		f_slug_from_name(s->map_name, save_slug, sizeof(save_slug));
	}
	ImGui::SameLine();
	if (ImGui::Button("Load From Mod")) {
		if (save_slug[0]) forgeSerializeLoadFromMod(save_slug);
	}
	ImGui::TextDisabled("Saves a legacy map (mod.json + map.json) to mods/Forge Maps/<slug>/.");
	ImGui::TextDisabled("Only this editor reads that format. To make a level that "
			"shows in %s and in match setup, use the Level File tab.", forgeDisplayName());
	}

	/* Save confirmation modal -- lists mod dependencies so the author
	 * knows the full bundle before pressing Save. */
	{
		if (ImGui::BeginPopupModal(savePopup, nullptr,
				ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Saving legacy map '%s'", s->map_name);
			ImGui::Text("Slug:  mods/Forge Maps/%s/", save_slug);
			ImGui::Separator();

			char deps[FORGE_MAX_DEPENDENCIES][FORGE_ID_LEN];
			s32 nd = forgeCollectDependencies(deps, FORGE_MAX_DEPENDENCIES);
			if (nd == 0) {
				ImGui::TextColored(ImVec4(0.6f, 0.95f, 0.6f, 1.0f),
						"No mod dependencies.  Map uses only base-game assets.");
			} else {
				ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
						"This map references %d mod asset(s) that will be bundled:", nd);
				ImGui::BeginChild("DepsListModal", ImVec2(420 * pdguiScale(1.0f),
						120 * pdguiScale(1.0f)), true);
				for (s32 i = 0; i < nd; ++i) {
					ImGui::BulletText("%s", deps[i]);
				}
				ImGui::EndChild();
				ImGui::TextDisabled("The distribution pipeline will recursively "
						"include each mod's own dependencies too.");
			}

			ImGui::Separator();
			const bool save = ImGui::Button("Save", ImVec2(120 * pdguiScale(1.0f), 0));
			ImGui::SameLine();
			const bool cancel = ImGui::Button("Cancel", ImVec2(120 * pdguiScale(1.0f), 0))
				|| pdguiMenuCancelPressed();
			ImGui::SetItemDefaultFocus();
			if (save && !cancel) {
				forgeSerializeSaveToMod(save_slug);
			}
			if (save || cancel) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	ImGui::SeparatorText("Reset");
	if (ImGui::Button("Reset Map (destructive)")) {
		forgeCoreReset();
	}
}

/* ============================================================
 * Main entry
 * ============================================================ */

/* S313 -- Persistent tab index so bumper navigation (LB/RB on gamepad,
 * keyboard PageUp/PageDown) can cycle through tabs without relying on
 * ImGui's tab-bar click state. */
/* Issue 9 (2026-04-24): four-tab skeleton per Mike's spec.
 *
 * The editor's 9 original tabs are kept as draw functions but grouped
 * into 4 top-level tabs for navigation:
 *   Level    -> Lighting + Mission + (future) skybox / skylight / music
 *   Objects  -> Catalog + Properties
 *   Control  -> Logic + Zones
 *   Playtest -> Bots + session rules
 *   Setup    -> Settings (map metadata -- stays as a 5th tab rather
 *               than folding under another category; accessed via
 *               the tab-bar gear position).
 *
 * Each top-level tab draws one or more existing section bodies in
 * sequence with ImGui::SeparatorText between them.  Skybox / skylight /
 * background-music controls live inline in the Level tab draw.
 *
 * The FGT_* enum is declared near the top of the file (see the
 * forward-decl block after the includes) so the earlier tab draw
 * functions can reference tab ids when registering scroll anchors. */

static int s_forge_active_tab = FGT_FILE;
static int s_forge_tab_set_request = -1; /* -1 = free, else force-set to this tab this frame */

static const char *forgeTabLabel(int idx)
{
	switch (idx) {
	case FGT_FILE:     return "Level File";
	case FGT_LEVEL:    return "Legacy Look";
	case FGT_OBJECTS:  return "Legacy Objects";
	case FGT_CONTROL:  return "Legacy Control";
	case FGT_GAMEPLAY: return "Playtest";
	case FGT_SETUP:    return "Legacy Map";
	default:           return "?";
	}
}

static bool forgeTabUsesLegacyMap(int tab)
{
	return tab == FGT_LEVEL || tab == FGT_OBJECTS || tab == FGT_CONTROL || tab == FGT_SETUP;
}

/* ============================================================
 * Issue 8b (2026-04-24): sidebar state + data
 * ============================================================ */

/* Each sidebar row carries a label and an integer payload. For the
 * Objects tab the payload is a forge_category_t (FORGE_CAT_*) applied
 * directly to the editor's catalog filter. For every other tab the
 * payload is an anchor id -- sending a "scroll-to-anchor" request that
 * forgeScrollAnchorMaybe consumes when it's drawn mid-tab. */
struct ForgeSidebarEntry {
	const char *label;
	int         payload;
};

/* Level File anchors and offline actions. */
static constexpr int kForgeFileEditPlacement = 4;
static constexpr int kForgeFileSaveOpened = 5;
static constexpr int kForgeFileEditTriggers = 6;
static constexpr int kForgeFileEditNavigation = 7;
static constexpr int kForgeFileEditRoomBrightness = 8;
static constexpr int kForgeFileOpen = 9;
static constexpr int kForgeFileEditLogic = 10;
static constexpr int kForgeFileEditLogicZones = 11;
static constexpr int kForgeFileAddHoverbike = 12;
static constexpr int kForgeGameplayPlayDraft = 8;
static constexpr int kForgeNativeGameplayEntryCount = 2; /* Play Draft and Bots */
static constexpr int kForgeFileSharedEntryCount = 4;
static bool s_LevelFileOpenRequested;
static bool s_LevelFilePlayDraftRequested;
static bool s_LevelFilePoseRequested;
static bool s_LevelFileSaveRequested;
static int s_LevelFileToolRequested;

struct ForgePropPlacement {
	bool active;
	bool dragging;
	bool awaitRelease;
	bool restoreControls;
	u32 generation;
	std::string modelId;
	std::string name;
};
static ForgePropPlacement s_PropPlacement;
static char s_PropPaletteSearch[64];
static char s_PropPaletteSelection[CATALOG_ID_LEN];
static bool s_PropPaletteReturnRequested;
static bool s_PropPaletteRestoreControls;

static bool forgeCanOpenLevelFileFromEditor(void)
{
	const char *arena = forgeLevelSessionArena();
	return netGetMode() == 0 && forgeIsFreefly() && !mainStageChangePending()
		&& arena && arena[0] && !forgeLevelSessionDoc();
}

static bool forgeCanPlayDraftFromEditor(void)
{
	return netGetMode() == 0 && forgeIsFreefly() && !forgeIsCanvasMode()
		&& !mainStageChangePending() && !forgeLevelDraftOwnsStageTransition()
		&& forgeLevelSessionCanPlayDraft();
}

static const ForgeSidebarEntry s_SbFile[] = {
	{ "Open Level File", kForgeFileOpen },
	{ "Starts",  0 },
	{ "Objects", 1 },
	{ "Atmosphere", 3 },
	{ "Save & Share", 2 },
	{ "Edit Placement", kForgeFileEditPlacement },
	{ "Save Opened Level", kForgeFileSaveOpened },
	{ "Triggers", kForgeFileEditTriggers },
	{ "Logic Zones", kForgeFileEditLogicZones },
	{ "Level Logic", kForgeFileEditLogic },
	{ "Bot Navigation", kForgeFileEditNavigation },
	{ "Room Brightness", kForgeFileEditRoomBrightness },
	{ "Choose Hoverbike", kForgeFileAddHoverbike },
};

static const ForgeSidebarEntry s_SbLevel[] = {
	{ "Lighting",          0 },
	{ "Mission",           1 },
	{ "Skybox",            2 },
	{ "Skylight",          3 },
	{ "Background Music",  4 },
};

/* Objects sidebar: payload is forge_category_t, or FORGE_CAT_COUNT to
 * unset the filter (show all). */
static const ForgeSidebarEntry s_SbObjects[] = {
	{ "All",         FORGE_CAT_COUNT },
	{ "Geometry",    FORGE_CAT_GEOMETRY },
	{ "Props",       FORGE_CAT_PROP },
	{ "Weapon Pads", FORGE_CAT_WEAPON_PAD },
	{ "Spawns",      FORGE_CAT_SPAWN_POINT },
	{ "Pickups",     FORGE_CAT_PICKUP },
	{ "Lights",      FORGE_CAT_LIGHT },
	{ "Effects",     FORGE_CAT_EFFECT },
	{ "Zones",       FORGE_CAT_ZONE },
	{ "Interact",    FORGE_CAT_INTERACTABLE },
	{ "AI / NPCs",   FORGE_CAT_AI },
	{ "Logic",       FORGE_CAT_LOGIC },
	{ "Prefabs",     FORGE_CAT_PREFAB },
};

static const ForgeSidebarEntry s_SbControl[] = {
	{ "Logic Nodes", 0 },
	{ "Wires",       1 },
	{ "Zones",       2 },
	{ "Channels",    3 },
};

static const ForgeSidebarEntry s_SbGameplay[] = {
	{ "Play Draft",  kForgeGameplayPlayDraft },
	{ "Bots",        7 },
	{ "Game Type",   0 },
	{ "Scoring",     1 },
	{ "Players",     2 },
	{ "Modifiers",   3 },
	{ "HUD",         4 },
	{ "Waves",       5 },
};

static const ForgeSidebarEntry s_SbSetup[] = {
	{ "Map Metadata", 0 },
	{ "Variant",      1 },
	{ "Players",      2 },
};

struct ForgeSidebarList {
	const ForgeSidebarEntry *items;
	int                      count;
	int                      first; /* Array index of the first visible row. */
};

static ForgeSidebarList forgeSidebarForTab(int tab)
{
	ForgeSidebarList out = {};
	if (forgeLevelSessionDoc() && forgeTabUsesLegacyMap(tab)) return out;

	switch (tab) {
	case FGT_FILE:
		out.first = forgeCanOpenLevelFileFromEditor() ? 0 : 1;
		out.items = s_SbFile + out.first;
		out.count = IM_ARRAYSIZE(s_SbFile) - out.first;
		if (netGetMode() != 0) out.count = kForgeFileSharedEntryCount;
		break;
	case FGT_LEVEL:
		out.items = s_SbLevel;
		out.count = (int)(sizeof(s_SbLevel) / sizeof(s_SbLevel[0]));
		break;
	case FGT_OBJECTS:
		out.items = s_SbObjects;
		out.count = (int)(sizeof(s_SbObjects) / sizeof(s_SbObjects[0]));
		break;
	case FGT_CONTROL:
		out.items = s_SbControl;
		out.count = (int)(sizeof(s_SbControl) / sizeof(s_SbControl[0]));
		break;
	case FGT_GAMEPLAY:
		out.first = forgeCanPlayDraftFromEditor() ? 0 : 1;
		out.items = s_SbGameplay + out.first;
		out.count = (forgeLevelSessionDoc() ? kForgeNativeGameplayEntryCount : IM_ARRAYSIZE(s_SbGameplay)) - out.first;
		break;
	case FGT_SETUP:
		out.items = s_SbSetup;
		out.count = (int)(sizeof(s_SbSetup) / sizeof(s_SbSetup[0]));
		break;
	default:
		out.items = nullptr;
		out.count = 0;
		break;
	}
	return out;
}

/* Per-session state. All defaults chosen so the editor opens in a
 * sensible spot: editor visible (with sidebar), first row focused
 * on each tab, no pending scroll-anchor request.
 *
 * Fix 2+3 (2026-05-01, playtest 2026-05-01): s_EditorVisible (formerly
 * s_SidebarVisible) gates the entire editor window, not just the
 * left rail. ACTION_FORGE_SIDEBAR_TOGGLE (X on pad, Tab on key) now
 * shows / hides the whole "The Grid -- Editor" panel. Promoting the
 * gate to the outer Begin call is what Mike's mental model expects
 * (one toggle, one visible thing) and is also where the B-195 leak
 * surfaces: when the editor closes, child widgets that held
 * keyboard focus retain it past Begin/End teardown. We pair the
 * visibility flip with pdguiClearImGuiFocusAndNav so WantCapture-
 * Keyboard drops back to false on close. */
static bool s_EditorVisible             = true;
static int  s_SidebarSelection[FGT_COUNT] = { 0, 0, 0, 0, 0 };
static int  s_SidebarScrollRequest[FGT_COUNT] = { -1, -1, -1, -1, -1 };

/* Draw-time helper: if the current tab has a pending scroll-to-anchor
 * request that matches `anchor_id`, scroll here and clear the request.
 * Otherwise no-op. Drop a call to this right before each meaningful
 * section header inside a tab's draw function to make the section a
 * valid sidebar scroll target. */
static bool forgeScrollAnchorMaybe(int tab, int anchor_id)
{
	if (tab < 0 || tab >= FGT_COUNT || s_SidebarScrollRequest[tab] != anchor_id) return false;
	ImGui::SetScrollHereY(0.0f);
	s_SidebarScrollRequest[tab] = -1;
	return true;
}

/* Apply the payload of the currently selected sidebar row to the
 * editor state. Objects tab sets the catalog category filter directly;
 * every other tab posts a scroll-to-anchor request that the tab's
 * draw function consumes via forgeScrollAnchorMaybe on the next frame. */
static void forgeSidebarActivate(int tab, int sel)
{
	ForgeSidebarList lst = forgeSidebarForTab(tab);
	if (!lst.items || sel < 0 || sel >= lst.count) return;
	int payload = lst.items[sel].payload;

	if (tab == FGT_FILE && payload == kForgeFileOpen) {
		s_LevelFileOpenRequested = true;
		return;
	}
	if (tab == FGT_GAMEPLAY && payload == kForgeGameplayPlayDraft) {
		s_LevelFilePlayDraftRequested = true;
		return;
	}
	if (tab == FGT_FILE && payload == kForgeFileEditPlacement) {
		s_LevelFilePoseRequested = true;
		return;
	}
	if (tab == FGT_FILE && payload == kForgeFileSaveOpened) {
		s_LevelFileSaveRequested = true;
		s_SidebarScrollRequest[tab] = 2;
		return;
	}
	if (tab == FGT_FILE && (payload == kForgeFileEditTriggers || payload == kForgeFileEditNavigation
			|| payload == kForgeFileEditRoomBrightness || payload == kForgeFileEditLogic
			|| payload == kForgeFileEditLogicZones || payload == kForgeFileAddHoverbike)) {
		s_LevelFileToolRequested = payload;
		return;
	}
	if (tab == FGT_OBJECTS) {
		forge_editor_state_t *ed = forgeGetEditor();
		if (ed) {
			ed->category_filter = (u8)payload;
		}
		return;
	}

	s_SidebarScrollRequest[tab] = payload;
}

/* Priority C (2026-04-24): Level tab ambience controls -- real.
 *
 * Skybox: a "sky stage" picker that swaps the live environment (sky
 * colour, sun position, cloud preset, fog distances) to another stage's
 * sky via forgeLevelSetSkyStage. Cheap, non-destructive -- restoring the
 * original sky is one more combo pick or a scene transition.
 *
 * Skylight: direct editors on the live environment fields.  Sky colour
 * is an ImGui colour picker; fog / clouds are straightforward knobs.
 * Changes apply immediately (no Apply button).
 *
 * Background music: a curated list of MUSIC_* tracks that map
 * 1:1 to forgeLevelPlayMusic.  Includes a Stop row for silence. */

extern "C" {
	void forgeLevelGetSkyColor(u8 *r, u8 *g, u8 *b);
	void forgeLevelSetSkyColor(u8 r, u8 g, u8 b);
	void forgeLevelGetFog(s32 *fogmin, s32 *fogmax);
	void forgeLevelSetFog(s32 fogmin, s32 fogmax);
	s32  forgeLevelGetCloudsEnabled(void);
	void forgeLevelSetCloudsEnabled(s32 enabled);
	void forgeLevelGetCloudColor(f32 *r, f32 *g, f32 *b);
	void forgeLevelSetCloudColor(f32 r, f32 g, f32 b);
	void forgeLevelSetSkyStage(s32 stagenum);
	void forgeLevelPlayMusic(s32 tracknum);
	void forgeLevelStopMusic(void);
}

namespace {

struct SkyStageOption {
	const char *label;
	s32         stagenum;
};

/* Curated list of stages with distinct, recognisable skies.  Numeric
 * values mirror src/include/constants.h STAGE_* defines; the labels are
 * English for editor legibility. */
static const SkyStageOption s_SkyStageOptions[] = {
	{ "CI Training (default)", 0x59 /* STAGE_CITRAINING */ },
	{ "Villa (overcast)",      0x27 /* STAGE_VILLA */ },
	{ "Chicago (night)",       0x28 /* STAGE_CHICAGO */ },
	{ "G5 Building",           0x29 /* STAGE_G5BUILDING */ },
	{ "Airbase (clear)",       0x30 /* STAGE_AIRBASE */ },
	{ "Crash Site (alien)",    0x37 /* STAGE_CRASHSITE */ },
	{ "Skedar Ruins",          0x47 /* STAGE_SKEDARRUINS */ },
	{ "Attack Ship",           0x44 /* STAGE_ATTACKSHIP */ },
	{ "MP: Skedar Ruins",      0x1c /* STAGE_MP_SKEDAR */ },
	{ "MP: Temple",            0x1a /* STAGE_MP_TEMPLE */ },
	{ "MP: Complex",           0x1b /* STAGE_MP_COMPLEX */ },
	{ "MP: Warehouse",         0x5b /* STAGE_MP_WAREHOUSE -- approximate */ },
};
static const int s_SkyStageCount = (int)(sizeof(s_SkyStageOptions) /
                                          sizeof(s_SkyStageOptions[0]));

struct MusicOption {
	const char *label;
	s32         tracknum;
};

/* Curated music picker -- common MP and cinematic tracks.  The
 * MUSIC_* enum is generated (Build/src/generated/ntsc-final/sequences.h)
 * so using literal values here avoids the fragile include chain for
 * the C++ editor TU.  Values match the generated enum ordering. */
static const MusicOption s_MusicOptions[] = {
	{ "Silence (stop)",       -1 },
	{ "Villa",                 12 /* MUSIC_VILLA */ },
	{ "CI",                    13 /* MUSIC_CI */ },
	{ "Chicago",               14 /* MUSIC_CHICAGO */ },
	{ "G5",                    15 /* MUSIC_G5 */ },
	{ "Investigation",         18 /* MUSIC_INVESTIGATION */ },
	{ "Infiltration",          20 /* MUSIC_INFILTRATION */ },
	{ "Airbase",               23 /* MUSIC_AIRBASE */ },
	{ "Air Force One",         24 /* MUSIC_AIRFORCEONE */ },
	{ "Pelagic",               28 /* MUSIC_PELAGIC */ },
	{ "Crash Site",            29 /* MUSIC_CRASHSITE */ },
	{ "Attack Ship",           31 /* MUSIC_ATTACKSHIP */ },
	{ "Skedar Ruins",          33 /* MUSIC_SKEDARRUINS */ },
	{ "Dark Combat (MP)",      58 /* MUSIC_DARK_COMBAT */ },
	{ "Alien Conflict",        62 /* MUSIC_ALIEN_CONFLICT */ },
	{ "Defence",                4 /* MUSIC_DEFENSE */ },
	{ "Rescue",                22 /* MUSIC_RESCUE */ },
	{ "Escape",                 6 /* MUSIC_ESCAPE */ },
	{ "Deep Sea",               7 /* MUSIC_DEEPSEA */ },
};
static const int s_MusicCount = (int)(sizeof(s_MusicOptions) /
                                       sizeof(s_MusicOptions[0]));

static int s_SelectedSkyStageIdx = 0;
static int s_SelectedMusicIdx    = 0;

} /* namespace */

static void forgeDrawLevelExtras(void)
{
	/* -------------------------------------------------------------
	 * Skybox / sky stage
	 * ------------------------------------------------------------- */
	forgeScrollAnchorMaybe(FGT_LEVEL, 2);   /* sidebar "Skybox" */
	ImGui::SeparatorText("Skybox");
	ImGui::TextWrapped(
		"Swap the live sky environment to another stage's sky. "
		"Changes the sun position, cloud preset, and fog distances "
		"immediately; use the scene transition to restore the original.");
	if (ImGui::BeginCombo("Sky stage", s_SkyStageOptions[s_SelectedSkyStageIdx].label)) {
		for (int i = 0; i < s_SkyStageCount; i++) {
			bool sel = (i == s_SelectedSkyStageIdx);
			if (ImGui::Selectable(s_SkyStageOptions[i].label, sel)) {
				s_SelectedSkyStageIdx = i;
				forgeLevelSetSkyStage(s_SkyStageOptions[i].stagenum);
			}
			if (sel) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	/* -------------------------------------------------------------
	 * Skylight / atmosphere
	 * ------------------------------------------------------------- */
	forgeScrollAnchorMaybe(FGT_LEVEL, 3);   /* sidebar "Skylight" */
	ImGui::SeparatorText("Skylight / Atmosphere");

	/* Sky colour (top-of-sky gradient seed). */
	{
		u8 r = 0, g = 0, b = 0;
		forgeLevelGetSkyColor(&r, &g, &b);
		float col[3] = { r / 255.0f, g / 255.0f, b / 255.0f };
		if (ImGui::ColorEdit3("Sky colour", col)) {
			forgeLevelSetSkyColor(
				(u8)(col[0] * 255.0f + 0.5f),
				(u8)(col[1] * 255.0f + 0.5f),
				(u8)(col[2] * 255.0f + 0.5f));
		}
	}

	/* Fog range. */
	{
		s32 fogmin = 0, fogmax = 0;
		forgeLevelGetFog(&fogmin, &fogmax);
		int fmin = (int)fogmin;
		int fmax = (int)fogmax;
		bool dirty = false;
		if (ImGui::SliderInt("Fog near", &fmin, 0, 20000, "%d units")) dirty = true;
		if (ImGui::SliderInt("Fog far",  &fmax, 0, 60000, "%d units")) dirty = true;
		if (dirty) {
			if (fmax < fmin) fmax = fmin + 1;
			forgeLevelSetFog((s32)fmin, (s32)fmax);
		}
	}

	/* Clouds. */
	{
		bool enabled = (forgeLevelGetCloudsEnabled() != 0);
		if (ImGui::Checkbox("Clouds visible", &enabled)) {
			forgeLevelSetCloudsEnabled(enabled ? 1 : 0);
		}
		if (enabled) {
			f32 cr = 1.0f, cg = 1.0f, cb = 1.0f;
			forgeLevelGetCloudColor(&cr, &cg, &cb);
			float col[3] = { cr, cg, cb };
			if (ImGui::ColorEdit3("Cloud tint", col)) {
				forgeLevelSetCloudColor(col[0], col[1], col[2]);
			}
		}
	}

	/* -------------------------------------------------------------
	 * Background music
	 * ------------------------------------------------------------- */
	forgeScrollAnchorMaybe(FGT_LEVEL, 4);   /* sidebar "Background Music" */
	ImGui::SeparatorText("Background Music");
	if (ImGui::BeginCombo("Track", s_MusicOptions[s_SelectedMusicIdx].label)) {
		for (int i = 0; i < s_MusicCount; i++) {
			bool sel = (i == s_SelectedMusicIdx);
			if (ImGui::Selectable(s_MusicOptions[i].label, sel)) {
				s_SelectedMusicIdx = i;
				if (s_MusicOptions[i].tracknum < 0) {
					forgeLevelStopMusic();
				} else {
					forgeLevelPlayMusic(s_MusicOptions[i].tracknum);
				}
			}
			if (sel) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::TextDisabled(
		"Changes are session-local -- the original stage track resumes "
		"on the next scene transition.");

	/* -------------------------------------------------------------
	 * Debug Rendering (B-304, 2026-05-01)
	 *
	 * Surface the wireframe + cull-mode debug toggles inline so authors
	 * can flip them from the editor without hunting for Shift+F1 / F2.
	 * The save/restore in forgeApplyDebugRender{Entry,Exit} ensures
	 * any state set here is scoped to the forge session -- exit restores
	 * the pre-forge values regardless of mid-session toggling.
	 * ------------------------------------------------------------- */
	ImGui::SeparatorText("Debug Rendering");
	{
		/* B-304 hotfix (2026-05-01): gfxDebug* extern "C" decls live at
		 * file scope (top of TU) -- linkage specifications inside a
		 * function-body block are not valid C++ and were the cause of
		 * the original B-304 merge build break. */
		bool wire = (gfxDebugWireframeGet() != 0);
		if (ImGui::Checkbox("Wireframe overlay (Shift+F2)", &wire)) {
			gfxDebugWireframeSet(wire ? 1 : 0);
		}

		int cull = gfxDebugCullModeGet();
		const char *cullLabels[3] = {
			"Cull None (default)",
			"Cull Back (default winding)",
			"Cull Front (show backfaces)",
		};
		if (ImGui::BeginCombo("Cull mode (Shift+F1)", cullLabels[cull & 3])) {
			for (int i = 0; i < 3; i++) {
				bool sel = (cull == i);
				if (ImGui::Selectable(cullLabels[i], sel)) {
					gfxDebugCullModeSet(i);
				}
				if (sel) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::TextDisabled(
			"Wireframe and cull state are session-local: pre-forge "
			"values are restored when the session ends.");
	}
}

/* ============================================================
 * Level File (Forge F2): the level's own placements
 *
 * The rest of this editor still works on its own object list and saves
 * map.json. This section edits the level the session was started on as its
 * archive stores it: player starts and objects, each standing on a pad
 * (forge_level_io.h, forge_level_doc.h). Save makes a level mod through the
 * mod author, the save the other in-game creators use.
 *
 * Everything is placed where the editor camera is, facing the way it
 * faces. The HUD marks each start and each object in the world
 * (pdgui_forge_hud.cpp, forgehudDrawLevelFile); the lists here say where
 * each stands and pick out one for the HUD to show in yellow.
 * ============================================================ */

static char s_LevelFileName[MOD_AUTHOR_NAME_MAX] = "";
static char s_LevelFileStatus[400]               = "";
static bool s_LevelFileStatusBad                 = false;
static char s_LevelFileStart[32]                 = "";   /* selected player start, by id */
static char s_LevelFileCasePad[32]               = "";
static int  s_LevelFileCaseTeam                  = 0;
static char s_LevelFileTriggerPad[32]            = "";
static char s_LevelFileTriggerTarget[32]         = "";
static s32 s_LevelFileTriggerAction              = FORGE_LEVEL_TRIGGER_OPEN_DOOR;
static char s_LevelFileWaypointPad[32]           = "";
static char s_LevelFileCover[32]                 = "";
static char s_LevelFileRouteTarget[32]           = "";
static char s_LevelFileObject[32]                = "";   /* selected object, by id */
static int  s_LevelFileObjectIndex               = -1;   /* its row; objects are only added, so rows do not move */
static bool s_LevelFilePosePreferObject;
static int  s_LevelFileSizePercent               = 100;  /* the Size section's slider */

/* Where the editor's panel was drawn last (pdguiForgeEditorPanelRect). */
static bool   s_EditorPanelShown = false;
static ImVec2 s_EditorPanelMin   = ImVec2(0.0f, 0.0f);
static ImVec2 s_EditorPanelMax   = ImVec2(0.0f, 0.0f);

s32 pdguiForgeEditorPanelRect(float *x0, float *y0, float *x1, float *y1)
{
	if (!s_EditorPanelShown || !forgeSessionIsActive()) return 0;
	if (x0) *x0 = s_EditorPanelMin.x;
	if (y0) *y0 = s_EditorPanelMin.y;
	if (x1) *x1 = s_EditorPanelMax.x;
	if (y1) *y1 = s_EditorPanelMax.y;
	return 1;
}
static bool s_LevelFileCloseArmed                = false;
static char s_LevelFileWeapon[CATALOG_ID_LEN]    = "";   /* the weapon chosen for a pickup, by catalog id */
static int  s_LevelFilePlace                     = 1;    /* the weapon place chosen for a pickup, 1 to 16 */
static char s_LevelFileModel[CATALOG_ID_LEN]     = "";   /* the model chosen for a prop, by catalog id */
static char s_LevelFileModelFilter[64]           = "";   /* text the model list must contain */

/* One line of the weapon list: every weapon the catalog has enabled, a mod's
 * included, by its catalog id. */
static void forgeLevelFileWeaponRow(const asset_entry_t *entry, void *userdata)
{
	(void)userdata;
	if (!entry || !entry->id[0]) return;
	if (ImGui::Selectable(entry->id, strcmp(entry->id, s_LevelFileWeapon) == 0)) {
		snprintf(s_LevelFileWeapon, sizeof(s_LevelFileWeapon), "%s", entry->id);
	}
}

/* One line of the model list: every model the catalog has, a mod's included,
 * by its catalog id, when it contains the filter text. The ids are drawn as
 * the list is walked; nothing of the catalog is kept past the walk. */
static void forgeLevelFileModelRow(const asset_entry_t *entry, void *userdata)
{
	(void)userdata;
	if (!entry || !entry->id[0]) return;
	if (s_LevelFileModelFilter[0] && !strstr(entry->id, s_LevelFileModelFilter)) return;
	if (ImGui::Selectable(entry->id, strcmp(entry->id, s_LevelFileModel) == 0)) {
		snprintf(s_LevelFileModel, sizeof(s_LevelFileModel), "%s", entry->id);
	}
}

/* The pad a player start stands on, by the start's id. */
static bool forgeLevelFileStartPad(forge_level_doc_t *doc, const char *spawn_id, forge_level_pad_t *pad)
{
	for (s32 i = 0; i < forgeLevelDocSpawnCount(doc); i++) {
		forge_level_spawn_t spawn;
		if (forgeLevelDocGetSpawn(doc, i, &spawn) && strcmp(spawn.spawn_id, spawn_id) == 0) {
			return forgeLevelDocFindPad(doc, spawn.pad_ref, pad) != 0;
		}
	}
	return false;
}

static void forgeLevelFileStatus(bool bad, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s_LevelFileStatus, sizeof(s_LevelFileStatus), fmt, ap);
	va_end(ap);
	s_LevelFileStatus[sizeof(s_LevelFileStatus) - 1] = '\0';
	s_LevelFileStatusBad = bad;
	s_LevelFileCloseArmed = false;
}

static void forgeLevelFileDrawStatus(void)
{
	if (!s_LevelFileStatus[0]) return;
	ImGui::PushStyleColor(ImGuiCol_Text, s_LevelFileStatusBad
		? ImVec4(1.0f, 0.55f, 0.5f, 1.0f) : ImVec4(0.6f, 0.95f, 0.6f, 1.0f));
	ImGui::TextWrapped("%s", s_LevelFileStatus);
	ImGui::PopStyleColor();
}

static bool forgeLevelFilePickPad(forge_level_doc_t *doc, const char *label, char (&selected)[32],
	const forge_level_pad_t *start, forge_level_pad_t &pad)
{
	const bool useStart = selected[0] == '\0';
	bool havePad = useStart ? start != nullptr : forgeLevelDocFindPad(doc, selected, &pad) != 0;
	if (useStart && start) pad = *start;
	if (ImGui::BeginCombo(label, havePad ? pad.pad_ref : "Select a pad")) {
		if (ImGui::Selectable("Use selected start", useStart)) {
			selected[0] = '\0';
			havePad = start != nullptr;
			if (start) pad = *start;
		}
		ImGuiListClipper clipper;
		clipper.Begin(forgeLevelDocPadCount(doc));
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				forge_level_pad_t item;
				if (!forgeLevelDocGetPad(doc, i, &item)) continue;
				char entry[112];
				snprintf(entry, sizeof(entry), "%s   (%.0f, %.0f, %.0f)", item.pad_ref,
					item.position[0], item.position[1], item.position[2]);
				if (ImGui::Selectable(entry, strcmp(item.pad_ref, selected) == 0)) {
					snprintf(selected, sizeof(selected), "%s", item.pad_ref);
					pad = item;
					havePad = true;
				}
			}
		}
		ImGui::EndCombo();
	}
	return havePad;
}

static void forgeDrawLevelFileCoverMove(forge_level_doc_t *doc, const forge_level_pad_t *pad)
{
	const s32 count = forgeLevelDocCoverCount(doc);
	if (!count) return;
	s32 selectedRow = -1;
	if (s_LevelFileCover[0]) {
		for (s32 i = 0; i < count; ++i) {
			forge_level_cover_t cover;
			if (forgeLevelDocGetCover(doc, i, &cover) && strcmp(cover.cover_ref, s_LevelFileCover) == 0) {
				selectedRow = i;
				break;
			}
		}
	}
	if (ImGui::BeginCombo("Cover marker", selectedRow >= 0 ? s_LevelFileCover : "Select a cover marker")) {
		ImGuiListClipper clipper;
		clipper.Begin(count);
		if (selectedRow >= 0) clipper.IncludeItemByIndex(selectedRow);
		while (clipper.Step()) {
			for (s32 i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				forge_level_cover_t cover;
				if (!forgeLevelDocGetCover(doc, i, &cover)) continue;
				char entry[192];
				snprintf(entry, sizeof(entry), "%s   (%.0f, %.0f, %.0f)###%d", cover.cover_ref,
					cover.position[0], cover.position[1], cover.position[2], i);
				if (ImGui::Selectable(entry, i == selectedRow)) {
					snprintf(s_LevelFileCover, sizeof(s_LevelFileCover), "%s", cover.cover_ref);
					selectedRow = i;
				}
				if (i == selectedRow) ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::BeginDisabled(!pad || selectedRow < 0);
	const bool move = ImGui::Button("Move Cover to Pad");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move this marker to the selected waypoint pad.");
	ImGui::EndDisabled();
	if (move) {
		char err[256] = {};
		if (forgeLevelSessionMoveCoverToPad(s_LevelFileCover, pad->pad_ref, err, sizeof(err))) {
			forgeLevelFileStatus(false, "Moved %s to %s.", s_LevelFileCover, pad->pad_ref);
		} else {
			forgeLevelFileStatus(true, "Could not move cover. %s", err);
		}
	}
}

static void forgeDrawLevelFileWaypoints(forge_level_doc_t *doc, const forge_level_pad_t *start)
{
	forge_level_pad_t pad = {};
	const bool havePad = forgeLevelFilePickPad(doc, "Waypoint pad", s_LevelFileWaypointPad, start, pad);
	bool required = pad.walk_direct != 0;
	ImGui::BeginDisabled(!havePad);
	const bool toggle = ImGui::Checkbox("Require Bot Waypoint", &required);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bots follow this pad when it is on their route.");
	ImGui::EndDisabled();
	if (toggle) {
		char err[256] = {};
		forgeLevelSessionEditBegin();
		const s32 changed = forgeLevelDocSetPadWalkDirect(doc, pad.pad_ref, required, err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) forgeLevelFileStatus(false, "Updated bot waypoint %s.", pad.pad_ref);
		else forgeLevelFileStatus(true, "Could not update waypoint: %s", err);
	}
	const bool coverEditable = forgeLevelSessionCanEditCover() != 0;
	ImGui::BeginDisabled(!havePad || !coverEditable);
	const bool addCover = ImGui::Button("Add Cover at Pad");
	ImGui::EndDisabled();
	if (addCover) {
		char err[256] = {};
		char cover[32];
		forgeLevelSessionEditBegin();
		const s32 changed = forgeLevelDocAddCoverAtPad(doc, pad.pad_ref, cover, sizeof(cover), err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) {
			snprintf(s_LevelFileCover, sizeof(s_LevelFileCover), "%s", cover);
			forgeLevelFileStatus(false, "Added %s at %s.", cover, pad.pad_ref);
		} else {
			forgeLevelFileStatus(true, "Could not add cover: %s", err);
		}
	}
	if (coverEditable) forgeDrawLevelFileCoverMove(doc, havePad ? &pad : nullptr);
	forge_level_pad_t target = {};
	const bool routeEditable = forgeLevelSessionCanConnectPads() != 0;
	ImGui::BeginDisabled(!routeEditable);
	const bool haveTarget = forgeLevelFilePickPad(doc, "Destination pad", s_LevelFileRouteTarget, start, target);
	const bool distinctPads = havePad && haveTarget && strcmp(pad.pad_ref, target.pad_ref) != 0;
	static struct {
		char first[32];
		char second[32];
		u32 generation;
		bool removable;
	} route = {};
	const u32 generation = forgeLevelSessionGeneration();
	if (routeEditable && distinctPads && (route.generation != generation
			|| strcmp(route.first, pad.pad_ref) != 0 || strcmp(route.second, target.pad_ref) != 0)) {
		snprintf(route.first, sizeof(route.first), "%s", pad.pad_ref);
		snprintf(route.second, sizeof(route.second), "%s", target.pad_ref);
		route.generation = generation;
		route.removable = forgeLevelDocCanDisconnectPads(doc, pad.pad_ref, target.pad_ref) != 0;
	}
	const bool disconnect = routeEditable && distinctPads && route.removable;
	ImGui::BeginDisabled(!distinctPads);
	const bool changeRoute = ImGui::Button(disconnect ? "Disconnect Pads###LevelFileRoute" : "Connect Pads###LevelFileRoute");
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	if (changeRoute) {
		char err[256] = {};
		char path[32];
		if (disconnect) {
			forgeLevelSessionEditBegin();
			const s32 changed = forgeLevelDocDisconnectPads(doc, pad.pad_ref, target.pad_ref, path, sizeof(path), err, sizeof(err));
			forgeLevelSessionEditEnd(changed);
			if (changed) forgeLevelFileStatus(false, "Disconnected %s and %s.", pad.pad_ref, target.pad_ref);
			else forgeLevelFileStatus(true, "Could not disconnect the pads: %s", err);
		} else if (forgeLevelSessionConnectPads(pad.pad_ref, target.pad_ref, path, sizeof(path), err, sizeof(err))) {
			forgeLevelFileStatus(false, "Connected %s and %s with %s.", pad.pad_ref, target.pad_ref, path);
		} else {
			forgeLevelFileStatus(true, "Could not connect the pads: %s", err);
		}
	}
}

static void forgeDrawLevelFileCaseMarkers(forge_level_doc_t *doc, const forge_level_pad_t *start)
{
	ImGui::SeparatorText("Case Markers");
	ImGui::TextWrapped("Hacker Central and Hold the Briefcase also use these pads.");
	char base[32];
	snprintf(base, sizeof(base), "Base %d", s_LevelFileCaseTeam + 1);
	if (ImGui::BeginCombo("Base slot", base)) {
		for (int i = 0; i < NET_SCENARIO_CTC_BASES; ++i) {
			snprintf(base, sizeof(base), "Base %d", i + 1);
			if (ImGui::Selectable(base, i == s_LevelFileCaseTeam)) s_LevelFileCaseTeam = i;
		}
		ImGui::EndCombo();
	}
	forge_level_pad_t pad = {};
	const bool havePad = forgeLevelFilePickPad(doc, "Case pad", s_LevelFileCasePad, start, pad);
	char home[32] = "";
	s32 respawns = 0;
	bool respawnAtPad = false;
	for (s32 i = 0; i < forgeLevelDocCaseMarkerCount(doc); ++i) {
		forge_level_case_marker_t marker;
		if (!forgeLevelDocGetCaseMarker(doc, i, &marker) || marker.team != s_LevelFileCaseTeam) continue;
		if (marker.respawn) {
			++respawns;
			respawnAtPad |= strcmp(marker.pad_ref, pad.pad_ref) == 0;
		} else {
			snprintf(home, sizeof(home), "%s", marker.pad_ref);
		}
	}
	ImGui::BeginDisabled(!havePad || strcmp(home, pad.pad_ref) == 0);
	const bool setHome = ImGui::Button(home[0] ? "Replace Case Home###LevelFileCaseHome"
		: "Set Case Home###LevelFileCaseHome");
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!havePad || respawnAtPad || respawns >= NET_SCENARIO_CTC_SPAWNPADS);
	const bool addRespawn = ImGui::Button("Add Team Respawn");
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text("%d/%d", respawns, NET_SCENARIO_CTC_SPAWNPADS);
	if (setHome || addRespawn) {
		char err[256] = {};
		forgeLevelSessionEditBegin();
		const s32 changed = setHome
			? forgeLevelDocSetCaseHome(doc, s_LevelFileCaseTeam, pad.pad_ref, err, sizeof(err))
			: forgeLevelDocAddCaseRespawn(doc, s_LevelFileCaseTeam, pad.pad_ref, err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) {
			forgeLevelFileStatus(false, "%s at %s.", setHome ? "Set case home" : "Added team respawn", pad.pad_ref);
		} else {
			forgeLevelFileStatus(true, "Could not update case markers: %s", err);
		}
	}

	const s32 count = forgeLevelDocCaseMarkerCount(doc);
	if (count == 0) return;
	s32 remove = -1;
	ImGui::BeginChild("LevelFileCaseMarkers", ImVec2(-1, 104 * pdguiScale(1.0f)), true);
	{
		ImGuiListClipper clipper;
		clipper.Begin(count);
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				forge_level_case_marker_t marker;
				if (!forgeLevelDocGetCaseMarker(doc, i, &marker)) continue;
				char label[96];
				snprintf(label, sizeof(label), "Base %d   %s   %s", marker.team + 1,
					marker.respawn ? "Team respawn" : "Case home", marker.pad_ref);
				ImGui::PushID(i);
				const bool selected = marker.team == s_LevelFileCaseTeam && strcmp(marker.pad_ref, pad.pad_ref) == 0;
				if (ImGui::Selectable(label, selected, 0, ImVec2(ImGui::CalcTextSize(label).x, 0))) {
					s_LevelFileCaseTeam = marker.team;
					snprintf(s_LevelFileCasePad, sizeof(s_LevelFileCasePad), "%s", marker.pad_ref);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove")) remove = i;
				ImGui::PopID();
			}
		}
	}
	ImGui::EndChild();
	if (remove >= 0) {
		char err[256] = {};
		forgeLevelSessionEditBegin();
		const s32 changed = forgeLevelDocRemoveCaseMarker(doc, remove, err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) {
			forgeLevelFileStatus(false, "Removed case marker.");
		} else {
			forgeLevelFileStatus(true, "Could not remove case marker: %s", err);
		}
	}
}

static void forgeDrawLevelFileTriggerPreview(const forge_level_pad_t &pad, const f32 halfSize[3])
{
	const s32 nativeWidth = videoGetNativeWidth();
	const s32 nativeHeight = videoGetNativeHeight();
	if (nativeWidth <= 0 || nativeHeight <= 0) return;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const ImVec2 scale(display.x / nativeWidth, display.y / nativeHeight);
	f32 low[3], high[3];
	for (int axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(halfSize[axis]) || halfSize[axis] < FORGE_LEVEL_TRIGGER_MIN_HALF_SIZE
				|| halfSize[axis] > FORGE_LEVEL_TRIGGER_MAX_HALF_SIZE) return;
		low[axis] = pad.position[axis] - halfSize[axis];
		high[axis] = pad.position[axis] + halfSize[axis];
		if (!std::isfinite(low[axis]) || !std::isfinite(high[axis]) || low[axis] >= high[axis]) return;
	}

	ImVec2 corners[8];
	bool projected[8] = {};
	for (int i = 0; i < 8; ++i) {
		const f32 world[3] = { i & 1 ? high[0] : low[0], i & 2 ? high[1] : low[1], i & 4 ? high[2] : low[2] };
		f32 x, y;
		if (forgeProjectWorldPos(world, &x, &y, nullptr)) {
			corners[i] = ImVec2(x * scale.x, y * scale.y);
			projected[i] = std::isfinite(corners[i].x) && std::isfinite(corners[i].y);
		}
	}
	ImDrawList *draw = ImGui::GetBackgroundDrawList();
	const float thickness = pdguiScale(1.5f);
	for (int i = 0; i < 8; ++i) {
		if (!projected[i]) continue;
		for (int axis = 0; axis < 3; ++axis) {
			const int end = i | (1 << axis);
			if (end != i && projected[end]) draw->AddLine(corners[i], corners[end], IM_COL32(120, 220, 255, 230), thickness);
		}
	}
}

static void forgeDrawLevelFileTriggers(forge_level_doc_t *doc, const forge_level_pad_t *start, bool opening)
{
	static float newHalfSize[3] = {100.0f, 100.0f, 100.0f};
	static forge_level_trigger_t selected = {};
	static char editedPad[32];
	static char editedTarget[32];
	static float editedHalfSize[3];
	static u32 selectionGeneration;
	const u32 generation = forgeLevelSessionGeneration();
	if (opening || selectionGeneration != generation) {
		selected = {};
		selectionGeneration = generation;
	}
	const bool editing = selected.volume_id[0] != '\0';
	static const char *const actions[] = {"Open Door", "Give Fixed Pickup"};
	if (editing) {
		ImGui::Text("Editing %s at %s", selected.action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP
			? actions[1] : actions[0], selected.pad_ref);
	} else if (ImGui::Combo("Action", &s_LevelFileTriggerAction, actions, IM_ARRAYSIZE(actions))) {
		s_LevelFileTriggerTarget[0] = '\0';
	}
	const s32 action = editing ? selected.action : s_LevelFileTriggerAction;
	const bool pickup = action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP;
	forge_level_pad_t pad = {};
	char (&padSelection)[32] = editing ? editedPad : s_LevelFileTriggerPad;
	const bool pickedPad = forgeLevelFilePickPad(doc, "Trigger pad", padSelection, start, pad);
	const bool hasBounds = pickedPad && pad.has_volume;
	const bool relocated = editing && hasBounds && strcmp(selected.pad_ref, pad.pad_ref) != 0;
	const bool validCenter = hasBounds && ((editing && !relocated) || forgeLevelLivePositionInRoom(pad.position));
	if (pickedPad && !hasBounds) ImGui::TextDisabled("Choose a pad with native bounds.");
	else if (hasBounds && !validCenter) ImGui::TextDisabled("The trigger center must be inside a loaded room.");
	char (&target)[32] = editing ? editedTarget : s_LevelFileTriggerTarget;
	const char *targetId = target[0] ? target : s_LevelFileObject;
	bool haveTarget = forgeLevelDocCanTriggerObject(doc, action, targetId) != 0;
	const char *placeholder = pickup ? "Select a placed fixed pickup" : "Select a placed door";
	if (ImGui::BeginCombo(pickup ? "Pickup to give" : "Door to open", haveTarget ? targetId : placeholder)) {
		for (s32 i = 0; i < forgeLevelDocObjectCount(doc); ++i) {
			forge_level_object_t object;
			if (!forgeLevelDocGetObject(doc, i, &object) || !forgeLevelDocCanTriggerObject(doc, action, object.record_id)) continue;
			char name[128];
			if (pickup) snprintf(name, sizeof(name), "%s  %s", object.record_id, object.weapon_catalog_id);
			else snprintf(name, sizeof(name), "%s", object.record_id);
			if (ImGui::Selectable(name, haveTarget && strcmp(object.record_id, targetId) == 0)) {
				snprintf(target, sizeof(target), "%s", object.record_id);
				targetId = target;
				haveTarget = true;
			}
		}
		ImGui::EndCombo();
	}
	float *halfSize = editing ? editedHalfSize : newHalfSize;
	ImGui::SliderFloat3("Half-size (X/Y/Z)", halfSize, FORGE_LEVEL_TRIGGER_MIN_HALF_SIZE,
		FORGE_LEVEL_TRIGGER_MAX_HALF_SIZE, "%.0f", ImGuiSliderFlags_AlwaysClamp);
	const bool unchanged = editing && !relocated && strcmp(selected.target_id, targetId) == 0
		&& memcmp(selected.half_size, halfSize, sizeof(selected.half_size)) == 0;
	ImGui::BeginDisabled(!validCenter || !haveTarget || unchanged);
	const bool apply = ImGui::Button(editing ? "Apply Trigger Edits"
		: (pickup ? "Give Fixed Pickup on Enter" : "Open Door on Enter"));
	ImGui::EndDisabled();
	if (editing) {
		ImGui::SameLine();
		if (ImGui::Button("Cancel Edit")) {
			selected = {};
			return;
		}
	}
	if (apply) {
		char err[256] = {};
		forgeLevelSessionEditBegin();
		const s32 changed = editing
			? forgeLevelDocUpdateTrigger(doc, selected.volume_id, pad.pad_ref, targetId, halfSize, err, sizeof(err))
			: forgeLevelDocAddTrigger(doc, action, pad.pad_ref, targetId, halfSize, err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) {
			forgeLevelFileStatus(false, "%s trigger at %s.", editing ? "Updated" : "Added", pad.pad_ref);
			if (editing) {
				selected = {};
				return;
			}
		} else {
			forgeLevelFileStatus(true, "Could not %s trigger: %s", editing ? "update" : "add", err);
		}
	}
	if (validCenter) forgeDrawLevelFileTriggerPreview(pad, halfSize);
	if (editing) return;
	for (s32 i = 0; i < forgeLevelDocTriggerCount(doc); ++i) {
		forge_level_trigger_t trigger;
		if (!forgeLevelDocGetTrigger(doc, i, &trigger)) continue;
		ImGui::PushID(trigger.volume_id);
		ImGui::Text("%s %s %s", trigger.pad_ref,
			trigger.action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP ? "gives" : "opens", trigger.target_id);
		if (trigger.editable) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Edit Trigger")) {
				selected = trigger;
				selectionGeneration = forgeLevelSessionGeneration();
				snprintf(editedPad, sizeof(editedPad), "%s", trigger.pad_ref);
				snprintf(editedTarget, sizeof(editedTarget), "%s", trigger.target_id);
				memcpy(editedHalfSize, trigger.half_size, sizeof(editedHalfSize));
			}
		}
		ImGui::SameLine();
		const bool remove = ImGui::SmallButton("Remove Trigger");
		ImGui::PopID();
		if (remove) {
			char err[256] = {};
			forgeLevelSessionEditBegin();
			const s32 changed = forgeLevelDocRemoveTrigger(doc, i, err, sizeof(err));
			forgeLevelSessionEditEnd(changed);
			if (changed) forgeLevelFileStatus(false, "Removed trigger at %s.", trigger.pad_ref);
			else forgeLevelFileStatus(true, "Could not remove trigger: %s", err);
			break;
		}
	}
}

static void forgeDrawLevelFileLogicZones(forge_level_doc_t *doc, const forge_level_pad_t *start, bool opening)
{
	static forge_level_logic_zone_t selected;
	static char selectedPad[32];
	static f32 halfSize[3] = {100.0f, 100.0f, 100.0f};
	if (opening) {
		selected = {};
		selectedPad[0] = '\0';
	}
	const bool editing = selected.volume_id[0] != '\0';
	if (editing) ImGui::Text("Editing %s", selected.volume_id);
	forge_level_pad_t pad = {};
	const bool pickedPad = forgeLevelFilePickPad(doc, "Zone pad", selectedPad, start, pad);
	const bool anchored = editing && pickedPad && strcmp(selected.pad_ref, pad.pad_ref) == 0;
	const bool validCenter = pickedPad && pad.has_volume && (anchored || forgeLevelLivePositionInRoom(pad.position));
	if (pickedPad && !validCenter) ImGui::TextDisabled("Choose a pad with bounds inside a loaded room.");
	ImGui::SliderFloat3("Half-size (X/Y/Z)", halfSize, FORGE_LEVEL_TRIGGER_MIN_HALF_SIZE,
		FORGE_LEVEL_TRIGGER_MAX_HALF_SIZE, "%.0f", ImGuiSliderFlags_AlwaysClamp);
	if (validCenter) forgeDrawLevelFileTriggerPreview(pad, halfSize);
	const bool unchanged = anchored && memcmp(selected.half_size, halfSize, sizeof(halfSize)) == 0;
	ImGui::BeginDisabled(!validCenter || unchanged);
	const bool apply = ImGui::Button(editing ? "Apply Zone Changes" : "Create Zone");
	ImGui::EndDisabled();
	if (editing) {
		ImGui::SameLine();
		if (ImGui::Button("Cancel Edit")) {
			selected = {};
			return;
		}
	}
	if (apply) {
		char id[32] = {}, error[256] = {};
		forgeLevelSessionEditBegin();
		const s32 changed = editing
			? forgeLevelDocUpdateLogicZone(doc, selected.volume_id, pad.pad_ref, halfSize, error, sizeof(error))
			: forgeLevelDocAddLogicZone(doc, pad.pad_ref, halfSize, id, sizeof(id), error, sizeof(error));
		forgeLevelSessionEditEnd(changed);
		if (changed) {
			forgeLevelFileStatus(false, "%s %s at %s.", editing ? "Updated" : "Created",
				editing ? selected.volume_id : id, pad.pad_ref);
			selected = {};
		} else {
			forgeLevelFileStatus(true, "%s", error);
		}
	}
	if (editing) return;

	for (s32 i = 0; i < forgeLevelDocLogicZoneCount(doc); ++i) {
		forge_level_logic_zone_t zone;
		if (!forgeLevelDocGetLogicZone(doc, i, &zone)) continue;
		ImGui::PushID(zone.volume_id);
		ImGui::Text("%s  %s", zone.volume_id, zone.pad_ref);
		ImGui::SameLine();
		if (ImGui::SmallButton("Edit")) {
			selected = zone;
			snprintf(selectedPad, sizeof(selectedPad), "%s", zone.pad_ref);
			memcpy(halfSize, zone.half_size, sizeof(halfSize));
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(zone.referenced != 0);
		const bool remove = ImGui::SmallButton("Remove");
		ImGui::EndDisabled();
		if (zone.referenced) {
			ImGui::SameLine();
			ImGui::TextDisabled("Used by level logic");
		}
		ImGui::PopID();
		if (remove) {
			char error[256] = {};
			forgeLevelSessionEditBegin();
			const s32 changed = forgeLevelDocRemoveLogicZone(doc, zone.volume_id, error, sizeof(error));
			forgeLevelSessionEditEnd(changed);
			if (changed) forgeLevelFileStatus(false, "Removed %s.", zone.volume_id);
			else forgeLevelFileStatus(true, "%s", error);
			break;
		}
	}
}

static void forgeDrawLevelFileRoomBrightness(forge_level_doc_t *doc)
{
	const s32 count = forgeLevelDocRoomBrightnessCount(doc);
	if (!count || !forgeLevelSessionCanEditRoomBrightness()) {
		ImGui::TextWrapped("This level has no editable native room brightness ranges.");
		return;
	}
	static s32 selection;
	static s32 minimum, maximum;
	static u32 generation = (u32)-1;
	const u32 current = forgeLevelSessionGeneration();
	if (selection >= count) selection = 0;
	forge_level_room_brightness_t room;
	if (!forgeLevelDocGetRoomBrightness(doc, selection, &room)) return;
	if (generation != current) {
		minimum = room.minimum;
		maximum = room.maximum;
		generation = current;
	}

	char label[32];
	snprintf(label, sizeof(label), "Room %d", room.roomnum);
	if (ImGui::BeginCombo("Room", label)) {
		ImGuiListClipper clipper;
		clipper.Begin(count);
		while (clipper.Step()) {
			for (s32 i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
				forge_level_room_brightness_t option;
				if (!forgeLevelDocGetRoomBrightness(doc, i, &option)) continue;
				snprintf(label, sizeof(label), "Room %d", option.roomnum);
				if (ImGui::Selectable(label, i == selection)) {
					selection = i;
					room = option;
					minimum = room.minimum;
					maximum = room.maximum;
				}
				if (i == selection) ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SliderInt("Minimum", &minimum, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderInt("Maximum", &maximum, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);
	if (minimum > maximum) ImGui::TextWrapped("Minimum must be at or below maximum.");
	if (room.has_base_override) {
		ImGui::TextDisabled("This room has an authored base brightness override.");
	} else {
		scenario_stage_atmosphere_t atmosphere = {};
		if (forgeLevelDocGetAtmosphere(doc, &atmosphere) && atmosphere.has_ambient) {
			ImGui::TextDisabled("Stage ambient sets the base brightness in this room.");
		}
	}
	ImGui::BeginDisabled(minimum > maximum || (minimum == room.minimum && maximum == room.maximum));
	const bool apply = ImGui::Button("Apply Room Brightness");
	ImGui::EndDisabled();
	ImGui::TextDisabled("Preview in Play Draft.");
	if (apply) {
		char err[256] = {};
		forgeLevelSessionEditBegin();
		const s32 changed = forgeLevelDocSetRoomBrightness(doc, room.roomnum, minimum, maximum, err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) forgeLevelFileStatus(false, "Updated room %d brightness.", room.roomnum);
		else forgeLevelFileStatus(true, "Could not update room brightness: %s", err);
	}
}

static std::string s_LevelFileLogicText;
static bool s_LevelFileLogicChanged;

static void forgeLoadLevelLogic(forge_level_doc_t *doc)
{
	size_t size = 0;
	const char *text = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH, &size);
	s_LevelFileLogicText.assign(text ? text : "", size);
	s_LevelFileLogicChanged = false;
}

static int forgeResizeLogicInput(ImGuiInputTextCallbackData *edit)
{
	auto &text = *static_cast<std::string *>(edit->UserData);
	text.resize(static_cast<size_t>(edit->BufTextLen));
	edit->Buf = text.data();
	return 0;
}

static void forgeDrawLevelFileLogic(forge_level_doc_t *doc, bool opening)
{
	static s32 selectedZone = 0;
	if (opening) {
		forgeLoadLevelLogic(doc);
		selectedZone = 0;
	}
	ImGui::TextWrapped("Edit the level's event, gate, and action nodes and their execution links. "
		"Apply the logic to the draft, then use Play Draft to try it.");
	forge_level_logic_zone_t zone;
	const s32 zones = forgeLevelDocLogicZoneCount(doc);
	if (zones && forgeLevelDocGetLogicZone(doc, selectedZone, &zone)) {
		ImGui::BeginDisabled(s_LevelFileLogicChanged);
		if (ImGui::BeginCombo("Logic Zone", zone.volume_id)) {
			for (s32 i = 0; i < zones; ++i) {
				forge_level_logic_zone_t option;
				if (!forgeLevelDocGetLogicZone(doc, i, &option)) continue;
				if (ImGui::Selectable(option.volume_id, i == selectedZone)) {
					selectedZone = i;
					zone = option;
				}
			}
			ImGui::EndCombo();
		}
		const bool enter = ImGui::Button("Add Enter Event");
		ImGui::SameLine();
		const bool leave = ImGui::Button("Add Leave Event");
		ImGui::EndDisabled();
		if (enter || leave) {
			char id[64] = {}, error[256] = {};
			forgeLevelSessionEditBegin();
			const s32 changed = forgeLevelDocAddLogicZoneEvent(doc, zone.volume_id,
				enter ? FORGE_LEVEL_LOGIC_ZONE_ENTER : FORGE_LEVEL_LOGIC_ZONE_LEAVE, id, sizeof(id), error, sizeof(error));
			forgeLevelSessionEditEnd(changed);
			if (changed) {
				forgeLoadLevelLogic(doc);
				forgeLevelFileStatus(false, "Added %s. Link its execution to a gate or action.", id);
			} else {
				forgeLevelFileStatus(true, "%s", error);
			}
		}
	}
	const float height = std::fmin(pdguiScale(312.0f), ImGui::GetIO().DisplaySize.y * 0.45f);
	if (ImGui::InputTextMultiline("##LevelLogicSource", s_LevelFileLogicText.data(),
			s_LevelFileLogicText.capacity() + 1, ImVec2(-FLT_MIN, height),
			ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput,
			forgeResizeLogicInput, &s_LevelFileLogicText)) {
		s_LevelFileLogicChanged = true;
	}
	ImGui::BeginDisabled(!s_LevelFileLogicChanged);
	const bool apply = ImGui::Button("Apply Logic to Draft");
	ImGui::SameLine();
	const bool revert = ImGui::Button("Revert Text");
	ImGui::EndDisabled();
	if (revert) forgeLoadLevelLogic(doc);
	if (!apply) return;

	char error[256] = {};
	forgeLevelSessionEditBegin();
	const s32 changed = forgeLevelDocReplaceLogic(doc, s_LevelFileLogicText.data(),
		s_LevelFileLogicText.size(), error, sizeof(error));
	forgeLevelSessionEditEnd(changed);
	if (!changed && error[0]) {
		forgeLevelFileStatus(true, "%s", error);
		return;
	}
	forgeLoadLevelLogic(doc);
	forgeLevelFileStatus(false, changed ? "Level logic updated." : "The executable logic matches the draft.");
}

static constexpr const char *kForgeDefaultHoverbikeModel = "base:model_hovbike";

static s32 forgePlaceLevelHoverbike(forge_level_doc_t *doc, const char *model,
	const f32 position[3], const f32 look[3])
{
	char id[32], err[256] = {};
	forgeLevelSessionEditBegin();
	const s32 changed = forgeLevelDocAddHoverbike(doc, model, position, look, id, sizeof(id), err, sizeof(err));
	forgeLevelSessionEditEnd(changed);
	if (changed) {
		forgeLevelFileStatus(false, "Added the hoverbike %s where the camera is, facing the way it looks.", id);
		snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", id);
		s_LevelFileObjectIndex = (int)forgeLevelDocObjectCount(doc) - 1;
	} else {
		forgeLevelFileStatus(true, "Could not add a hoverbike: %s", err);
	}
	return changed;
}

static bool forgeDrawLevelFileHoverbikePicker(forge_level_doc_t *doc, const f32 position[3],
	const f32 look[3], bool opening)
{
	static char vehicle[CATALOG_ID_LEN];
	if (opening) vehicle[0] = '\0';
	ImGui::SetNextItemWidth(310.0f * pdguiScale(1.0f));
	if (ImGui::BeginCombo("Hoverbike", vehicle[0] ? vehicle : "Default Hoverbike")) {
		if (ImGui::Selectable("Default Hoverbike", !vehicle[0])) vehicle[0] = '\0';
		if (!vehicle[0]) ImGui::SetItemDefaultFocus();
		/* The iterator owns the catalog lock. Resolve linked models afterward. */
		std::vector<std::string> ids;
		assetCatalogIterateByType(ASSET_VEHICLE, [](const asset_entry_t *entry, void *data) {
			if (!entry->bundled) static_cast<std::vector<std::string> *>(data)->emplace_back(entry->id);
		}, &ids);
		for (const std::string &id : ids) {
			if (!assetRuntimeVehiclePlacementModel(id.c_str())) continue;
			const bool selected = id == vehicle;
			if (ImGui::Selectable(id.c_str(), selected)) snprintf(vehicle, sizeof(vehicle), "%s", id.c_str());
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if (opening) ImGui::SetItemDefaultFocus();
	const asset_entry_t *model = vehicle[0] ? assetRuntimeVehiclePlacementModel(vehicle) : nullptr;
	ImGui::BeginDisabled(vehicle[0] && !model);
	const bool place = ImGui::Button("Place Here");
	ImGui::EndDisabled();
	if (vehicle[0] && !model) ImGui::TextDisabled("Choose an available hoverbike.");
	if (!place) return false;
	return forgePlaceLevelHoverbike(doc, model ? model->id : kForgeDefaultHoverbikeModel, position, look) != 0;
}

static void forgeDrawLevelFileTools(forge_level_doc_t *doc, const forge_level_pad_t *start, int request,
	const f32 position[3], const f32 look[3])
{
	static int active;
	static u32 generation;
	if (request) {
		active = request;
		generation = forgeLevelSessionGeneration();
	}
	if (!active) return;
	const char *title = "Bot Navigation###GridLevelTools";
	if (active == kForgeFileEditTriggers) title = "Triggers###GridLevelTools";
	if (active == kForgeFileEditRoomBrightness) title = "Room Brightness###GridLevelTools";
	if (active == kForgeFileEditLogic) title = "Level Logic###GridLevelTools";
	if (active == kForgeFileEditLogicZones) title = "Logic Zones###GridLevelTools";
	if (active == kForgeFileAddHoverbike) title = "Choose Hoverbike###GridLevelTools";
	if (request) ImGui::OpenPopup(title);
	const float scale = pdguiScale(1.0f);
	const float maxWidth = (active == kForgeFileEditLogic ? 760.0f : 560.0f) * scale;
	ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(maxWidth, ImGui::GetIO().DisplaySize.y * 0.75f));
	if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		active = 0;
		menupoolRelease(MENU_TYPE_GRID_LEVEL_TOOLS);
		return;
	}
	if (request) {
		menupoolAcquire(MENU_TYPE_GRID_LEVEL_TOOLS, nullptr, &g_CtxImGuiMenu);
		pdguiNavSuppressOpeningGesture();
	}
	bool close = !menupoolIsActive(MENU_TYPE_GRID_LEVEL_TOOLS) || !doc || netGetMode() != 0
		|| generation != forgeLevelSessionGeneration();
	if (!close) {
		if (active == kForgeFileEditTriggers) forgeDrawLevelFileTriggers(doc, start, request == kForgeFileEditTriggers);
		else if (active == kForgeFileEditRoomBrightness) forgeDrawLevelFileRoomBrightness(doc);
		else if (active == kForgeFileEditLogic) forgeDrawLevelFileLogic(doc, request == kForgeFileEditLogic);
		else if (active == kForgeFileEditLogicZones) forgeDrawLevelFileLogicZones(doc, start, request == kForgeFileEditLogicZones);
		else if (active == kForgeFileAddHoverbike) close = forgeDrawLevelFileHoverbikePicker(doc, position, look,
			request == kForgeFileAddHoverbike);
		else forgeDrawLevelFileWaypoints(doc, start);
		generation = forgeLevelSessionGeneration();
		forgeLevelFileDrawStatus();
		const bool editingLogic = active == kForgeFileEditLogic && s_LevelFileLogicChanged;
		close = ImGui::Button(editingLogic ? "Discard Text and Close"
			: active == kForgeFileAddHoverbike ? "Cancel" : "Close") || close;
		if (pdguiMenuCancelPressed() && !editingLogic) close = true;
	}
	if (close) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
	if (close) {
		active = 0;
		menupoolRelease(MENU_TYPE_GRID_LEVEL_TOOLS);
	}
}

static grid_match_sources_t s_LevelVariantReview;
static pdgui_grid_variant_picker_t s_LevelVariantPicker;
static u32 s_LevelVariantGeneration;

static void forgeLevelClearVariantReview(void)
{
	gridMatchSourcesFree(&s_LevelVariantReview);
	s_LevelVariantPicker = {};
	s_LevelVariantGeneration = 0;
	menupoolRelease(MENU_TYPE_GRID_VARIANT_REVIEW);
}

void pdguiBeginGridVariantPicker(pdgui_grid_variant_picker_t &picker,
	const forge_level_doc_t *doc, const grid_match_sources_t &sources)
{
	picker.keep.assign(sources.package_count, 1);
	picker.power_weapon.assign(forgeLevelDocObjectCount(doc), 0);
}

static bool gridVariantOmitted(const pdgui_grid_variant_picker_t &picker,
	const grid_match_sources_t &sources, const char *id, asset_type_e type)
{
	for (size_t i = 0; id[0] && i < sources.entry_count; i++) {
		const auto &source = sources.entries[i];
		if (source.asset.type == type && strcmp(source.asset.id, id) == 0) {
			return !picker.keep[source.package_index];
		}
	}
	return false;
}

bool pdguiDrawGridVariantPicker(pdgui_grid_variant_picker_t &picker,
	const forge_level_doc_t *doc, const grid_match_sources_t &sources,
	pdgui_grid_variant_selection_t &selection, char *err, size_t err_cap)
{
	size_t modCount = 0;
	for (size_t i = 0; i < sources.package_count; i++) {
		const auto &package = sources.packages[i];
		if (package.bundled || package.owner.type == ASSET_ARENA || strncmp(package.owner.id, "base:", 5) == 0) continue;
		ImGui::PushID(static_cast<s32>(i));
		bool keep = picker.keep[i] != 0;
		ImGui::Checkbox("Keep", &keep);
		ImGui::SameLine();
		ImGui::TextUnformatted(package.owner.id);
		picker.keep[i] = keep;
		if (!keep) selection.omitted.push_back(static_cast<u16>(i));
		ImGui::PopID();
		modCount++;
	}
	if (!modCount) ImGui::TextDisabled("No additional mod packages.");
	if (selection.omitted.empty()) return true;

	const s32 objects = forgeLevelDocObjectCount(doc);
	selection.records.resize(objects);
	selection.pickup_models.resize(objects);
	bool valid = true;
	for (s32 i = 0; i < objects; i++) {
		auto &object = selection.records[i];
		if (!forgeLevelDocGetObject(doc, i, &object) || object.removed) continue;
		forge_level_placement_fallback_t fallback = {};
		fallback.record_id = object.record_id;
		ImGui::PushID(object.record_id);
		if (object.weapon_settable && gridVariantOmitted(picker, sources, object.weapon_catalog_id, ASSET_WEAPON)) {
			const char *roles[] = { "Ordinary weapon (Falcon 2)", "Power weapon (Slayer)" };
			ImGui::TextUnformatted(object.weapon_catalog_id);
			ImGui::Combo("Replacement", &picker.power_weapon[i], roles, IM_ARRAYSIZE(roles));
			const char *weapon = picker.power_weapon[i] ? "base:slayer" : "base:falcon2";
			auto &model = selection.pickup_models[i];
			fallback.kind = FORGE_LEVEL_FALLBACK_PICKUP;
			fallback.expected_catalog_id = object.weapon_catalog_id;
			fallback.weapon_catalog_id = weapon;
			fallback.model_catalog_id = model.data();
			if (!forgeLevelPickupLook(weapon, model.data(), model.size(), &fallback.extra_scale,
					valid ? err : nullptr, valid ? err_cap : 0)) valid = false;
			selection.fallbacks.push_back(fallback);
		} else if (gridVariantOmitted(picker, sources, object.model_catalog_id, ASSET_MODEL)
				&& (strcmp(object.kind, "prop") == 0 || strcmp(object.kind, "hoverbike") == 0)) {
			fallback.kind = strcmp(object.kind, "prop") == 0 ? FORGE_LEVEL_FALLBACK_PROP : FORGE_LEVEL_FALLBACK_HOVERBIKE;
			fallback.expected_catalog_id = object.model_catalog_id;
			ImGui::Text("%s uses %s", object.record_id,
				fallback.kind == FORGE_LEVEL_FALLBACK_PROP ? "a base prop" : "the base hoverbike model");
			selection.fallbacks.push_back(fallback);
		}
		ImGui::PopID();
	}
	return valid;
}

static void forgeDrawLevelVariantReview(forge_level_doc_t *doc, bool opening)
{
	static const char popup[] = "Save Variant###GridVariantMods";
	char err[256] = {};
	if (opening) {
		forgeLevelClearVariantReview();
		if (!forgeLevelSessionReviewVariantSources(&s_LevelVariantReview, err, sizeof(err))) {
			forgeLevelFileStatus(true, "Could not review the variant: %s", err);
			return;
		}
		pdguiBeginGridVariantPicker(s_LevelVariantPicker, doc, s_LevelVariantReview);
		s_LevelVariantGeneration = forgeLevelSessionGeneration();
		ImGui::OpenPopup(popup);
	}
	const float scale = pdguiScale(1.0f);
	ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(690.0f * scale, ImGui::GetIO().DisplaySize.y * 0.8f));
	if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoSavedSettings)) {
		if (s_LevelVariantGeneration && !ImGui::IsPopupOpen(popup)) forgeLevelClearVariantReview();
		return;
	}
	if (opening) {
		menupoolAcquire(MENU_TYPE_GRID_VARIANT_REVIEW, nullptr, &g_CtxImGuiMenu);
		pdguiNavSuppressOpeningGesture();
	}
	bool close = !menupoolIsActive(MENU_TYPE_GRID_VARIANT_REVIEW)
		|| forgeLevelSessionGeneration() != s_LevelVariantGeneration;
	if (!close) {
		ImGui::Text("Variant %s", s_LevelFileName);
		pdgui_grid_variant_selection_t selection;
		const bool valid = pdguiDrawGridVariantPicker(s_LevelVariantPicker, doc, s_LevelVariantReview,
			selection, err, sizeof(err));
		if (!valid) ImGui::TextWrapped("%s", err);
		ImGui::BeginDisabled(!valid || !s_LevelFileName[0]);
		const bool save = ImGui::Button("Save Variant");
		ImGui::EndDisabled();
		ImGui::SameLine();
		close = ImGui::Button("Cancel") || pdguiMenuCancelPressed();
		ImGui::SetItemDefaultFocus();
		if (save && !close) {
			auto *result = static_cast<mod_author_result_t *>(calloc(1, sizeof(mod_author_result_t)));
			if (result && forgeLevelSessionSaveSelectedVariant(s_LevelFileName, &s_LevelVariantReview,
					selection.omitted.data(), selection.omitted.size(), selection.fallbacks.data(),
					selection.fallbacks.size(), result, err, sizeof(err))) {
				forgeLevelFileStatus(false, "Saved variant %s.", result->catalog_id);
				close = true;
			} else {
				forgeLevelFileStatus(true, "Could not save the variant: %s", result ? err : "out of memory");
			}
			free(result);
		}
		forgeLevelFileDrawStatus();
	}
	if (close) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
	if (close) forgeLevelClearVariantReview();
}

static void forgeLevelFileForget(void)
{
	forgeLevelClearVariantReview();
	s_LevelFileLogicText.clear();
	s_LevelFileLogicChanged = false;
	s_LevelFileStart[0] = '\0';
	s_LevelFileCasePad[0] = '\0';
	s_LevelFileCaseTeam = 0;
	s_LevelFileTriggerPad[0] = '\0';
	s_LevelFileTriggerTarget[0] = '\0';
	s_LevelFileTriggerAction = FORGE_LEVEL_TRIGGER_OPEN_DOOR;
	s_LevelFileWaypointPad[0] = '\0';
	s_LevelFileCover[0] = '\0';
	s_LevelFileRouteTarget[0] = '\0';
	s_LevelFileObject[0] = '\0';
	s_LevelFileObjectIndex = -1;
	s_LevelFilePosePreferObject = false;
	s_LevelFileCloseArmed = false;
}

/* Where the editor camera is and the way it faces, flat. The camera's yaw is
 * the player's turn angle (forgemode.c sets vv_theta from it), and a player
 * with turn angle t faces (-sin t, 0, cos t) (bondmove.c: bond2.unk00). A
 * player started on a pad gets t = 360 - atan2(look.x, look.z) (player.c),
 * which turns this look back into the same angle, so someone who starts here
 * sees what the camera saw. The X sign matters: with +sin the start faced
 * the mirror image of the camera for every yaw but 0 and 180. */
static void forgeLevelFileCamera(f32 position[3], f32 look[3])
{
	struct forge_editor_coord { f32 x, y, z; } cpos = { 0.0f, 0.0f, 0.0f };
	forgeGetCameraPos((struct coord *)&cpos);
	const f32 yaw = forgeGetCameraYawDeg() * 0.017453292f;
	position[0] = cpos.x;
	position[1] = cpos.y;
	position[2] = cpos.z;
	look[0] = -sinf(yaw);
	look[1] = 0.0f;
	look[2] = cosf(yaw);
}

struct ForgeLevelFilePose {
	u32 generation;
	char id[32];
	bool object;
	forge_level_pad_t before;
	f32 position[3];
	f32 originalYaw;
	f32 yaw;
	char error[256];
};

static void forgeLevelFilePoseSelect(ForgeLevelFilePose &pose, const char *id,
		const forge_level_pad_t &pad, s32 objectIndex)
{
	pose.generation = forgeLevelSessionGeneration();
	snprintf(pose.id, sizeof(pose.id), "%s", id);
	pose.object = objectIndex >= 0;
	pose.before = pad;
	memcpy(pose.position, pad.position, sizeof(pose.position));
	pose.yaw = pose.originalYaw = atan2f(-pad.look[0], pad.look[2]) * (180.0f / 3.14159265358979323846f);
	pose.error[0] = '\0';
	s_LevelFilePosePreferObject = pose.object;
	if (pose.object) {
		s_LevelFileObjectIndex = objectIndex;
		snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", pose.id);
	} else {
		snprintf(s_LevelFileStart, sizeof(s_LevelFileStart), "%s", pose.id);
	}
}

static void forgeLevelFilePosePicker(forge_level_doc_t *doc, ForgeLevelFilePose &pose)
{
	if (!ImGui::BeginCombo("Placement", pose.id[0] ? pose.id : "Select a start or object", ImGuiComboFlags_HeightLarge)) return;

	ImGui::SeparatorText("Player Starts");
	for (s32 i = 0; i < forgeLevelDocSpawnCount(doc); ++i) {
		forge_level_spawn_t start;
		forge_level_pad_t pad;
		if (!forgeLevelDocGetSpawn(doc, i, &start) || !forgeLevelDocFindPad(doc, start.pad_ref, &pad)) continue;
		const bool selected = !pose.object && strcmp(pose.id, start.spawn_id) == 0;
		if (ImGui::Selectable(start.spawn_id, selected) && !selected) {
			forgeLevelFilePoseSelect(pose, start.spawn_id, pad, -1);
		}
		if (selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::SeparatorText("Objects");
	for (s32 i = 0; i < forgeLevelDocObjectCount(doc); ++i) {
		forge_level_object_t object;
		forge_level_pad_t pad;
		if (!forgeLevelDocGetObject(doc, i, &object) || !object.movable
				|| !forgeLevelDocFindPad(doc, object.pad_ref, &pad)) continue;
		const bool selected = pose.object && strcmp(pose.id, object.record_id) == 0;
		char label[128];
		snprintf(label, sizeof(label), "%s (%s)###%s", object.record_id, object.kind, object.record_id);
		if (ImGui::Selectable(label, selected) && !selected) {
			forgeLevelFilePoseSelect(pose, object.record_id, pad, i);
		}
		if (selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
}

static void forgeDrawLevelFilePose(forge_level_doc_t *doc, bool request)
{
	static const char popup[] = "Edit Placement##LevelFilePose";
	static ForgeLevelFilePose pose;
	if (request) {
		pose = {};
		pose.generation = forgeLevelSessionGeneration();
		forge_level_pad_t startPad, objectPad;
		forge_level_object_t object;
		const bool haveStart = forgeLevelFileStartPad(doc, s_LevelFileStart, &startPad);
		const bool haveObject = s_LevelFileObjectIndex >= 0 && forgeLevelDocGetObject(doc, s_LevelFileObjectIndex, &object)
			&& object.movable && strcmp(object.record_id, s_LevelFileObject) == 0
			&& forgeLevelDocFindPad(doc, object.pad_ref, &objectPad);
		if (haveObject && (s_LevelFilePosePreferObject || !haveStart)) {
			forgeLevelFilePoseSelect(pose, object.record_id, objectPad, s_LevelFileObjectIndex);
		} else if (haveStart) {
			forgeLevelFilePoseSelect(pose, s_LevelFileStart, startPad, -1);
		}
		ImGui::OpenPopup(popup);
	}
	if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		menupoolRelease(MENU_TYPE_GRID_PLACEMENT);
		return;
	}
	if (request) {
		menupoolAcquire(MENU_TYPE_GRID_PLACEMENT, nullptr, &g_CtxImGuiMenu);
		pdguiNavSuppressOpeningGesture();
	}
	if (!menupoolIsActive(MENU_TYPE_GRID_PLACEMENT) || netGetMode() != 0
			|| pose.generation != forgeLevelSessionGeneration()) {
		forgeLevelFileStatus(true, "The Level File changed. Reopen Edit Placement to try again.");
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		menupoolRelease(MENU_TYPE_GRID_PLACEMENT);
		return;
	}

	ImGui::SetNextItemWidth(235.0f * pdguiScale(1.0f));
	forgeLevelFilePosePicker(doc, pose);
	ImGui::BeginDisabled(!pose.id[0]);
	ImGui::SetNextItemWidth(235.0f * pdguiScale(1.0f));
	if (ImGui::InputFloat3("World X, Y, Z", pose.position, "%.3f")) pose.error[0] = '\0';
	ImGui::SetNextItemWidth(235.0f * pdguiScale(1.0f));
	if (ImGui::InputFloat("Yaw (degrees)", &pose.yaw, 1.0f, 15.0f, "%.2f")) pose.error[0] = '\0';

	const f32 yaw = std::isfinite(pose.yaw) ? remainderf(pose.yaw, 360.0f) : pose.yaw;
	const bool yawChanged = !std::isfinite(yaw) || remainderf(yaw - pose.originalYaw, 360.0f) != 0.0f;
	const bool positionChanged = pose.position[0] != pose.before.position[0]
		|| pose.position[1] != pose.before.position[1] || pose.position[2] != pose.before.position[2];
	bool close = false;
	ImGui::BeginDisabled(!positionChanged && !yawChanged);
	if (ImGui::Button("Apply Placement")) {
		if (!std::isfinite(yaw) || !std::isfinite(pose.position[0])
				|| !std::isfinite(pose.position[1]) || !std::isfinite(pose.position[2])) {
			snprintf(pose.error, sizeof(pose.error), "Position and yaw must be finite numbers.");
		} else if (!forgeLevelLivePositionInRoom(pose.position)) {
			snprintf(pose.error, sizeof(pose.error), "Choose a position inside a room of the loaded level.");
		} else {
			f32 look[3];
			memcpy(look, pose.before.look, sizeof(look));
			if (yawChanged) {
				const f32 forward[3] = { 0.0f, 0.0f, 1.0f };
				forgeLevelTurnLook(forward, yaw, look);
			}
			forgeLevelSessionEditBegin();
			const s32 changed = pose.object
				? forgeLevelDocMoveObject(doc, pose.id, pose.position, look, pose.error, sizeof(pose.error))
				: forgeLevelDocMoveSpawn(doc, pose.id, pose.position, look, pose.error, sizeof(pose.error));
			forgeLevelSessionEditEnd(changed);
			if (changed) {
				forgeLevelFileStatus(false, "Updated the placement of %s.", pose.id);
				close = true;
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || pdguiMenuCancelPressed()) close = true;
	if (pose.error[0]) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.5f, 1.0f));
		ImGui::TextWrapped("%s", pose.error);
		ImGui::PopStyleColor();
	}
	if (close) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
	if (close) menupoolRelease(MENU_TYPE_GRID_PLACEMENT);
}

/* How far one press of a Nudge button moves a start or an object. */
static int s_LevelFileNudgeStep = 50;

/*
 * A row of Nudge buttons for something that faces `look` from where the
 * camera looks (`camera_look`, flat): left and right along the camera's
 * right, nearer and farther along its facing, up and down. 1 with the
 * offset to add when one was pressed. A camera at yaw t faces (-sin t, 0,
 * cos t) and its right is (-cos t, 0, -sin t): for a flat look (lx, 0, lz)
 * that is (-lz, 0, lx).
 */
static bool forgeLevelFileNudgeRow(const char *id, const f32 camera_look[3], f32 offset[3])
{
	static const char *const names[6] = { "Left", "Right", "Nearer", "Farther", "Up", "Down" };
	const f32 step = (f32)s_LevelFileNudgeStep;
	const f32 right[3] = { -camera_look[2], 0.0f, camera_look[0] };
	bool pressed = false;

	ImGui::PushID(id);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Nudge");
	for (int i = 0; i < 6; i++) {
		ImGui::SameLine();
		if (!ImGui::Button(names[i])) continue;
		pressed = true;
		for (int k = 0; k < 3; k++) offset[k] = 0.0f;
		switch (i) {
		case 0: for (int k = 0; k < 3; k++) offset[k] = -right[k] * step; break;
		case 1: for (int k = 0; k < 3; k++) offset[k] = right[k] * step; break;
		case 2: for (int k = 0; k < 3; k++) offset[k] = -camera_look[k] * step; break;
		case 3: for (int k = 0; k < 3; k++) offset[k] = camera_look[k] * step; break;
		case 4: offset[1] = step; break;
		default: offset[1] = -step; break;
		}
	}
	ImGui::PopID();
	return pressed;
}

static void forgeDrawLevelFileAtmosphere(forge_level_doc_t *doc)
{
	static scenario_stage_atmosphere_t draft = {};
	static u32 generation = (u32)-1;
	const u32 current = forgeLevelSessionGeneration();
	if (generation != current) {
		forgeLevelDocGetAtmosphere(doc, &draft);
		if (!draft.has_sky) {
			draft.sky_rgb[0] = 64;
			draft.sky_rgb[1] = 88;
			draft.sky_rgb[2] = 120;
		}
		if (!draft.has_fog) {
			draft.fog_start = 100;
			draft.fog_end = 650;
		}
		if (!draft.has_ambient) draft.ambient_light = 160;
		generation = current;
	}

	ImGui::SeparatorText("Atmosphere");
	bool sky = draft.has_sky != 0;
	bool fog = draft.has_fog != 0;
	bool ambient = draft.has_ambient != 0;
	ImGui::Checkbox("Set sky color", &sky);
	ImGui::BeginDisabled(!sky);
	float rgb[] = {draft.sky_rgb[0] / 255.0f, draft.sky_rgb[1] / 255.0f, draft.sky_rgb[2] / 255.0f};
	if (ImGui::ColorEdit3("Sky", rgb, ImGuiColorEditFlags_Uint8)) {
		for (s32 i = 0; i < 3; i++) draft.sky_rgb[i] = static_cast<u8>(rgb[i] * 255.0f + 0.5f);
	}
	ImGui::EndDisabled();
	ImGui::Checkbox("Set fog distance", &fog);
	ImGui::BeginDisabled(!fog);
	ImGui::SliderInt("Fog start", &draft.fog_start, 0, draft.fog_end - 1, "%d / 1000", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderInt("Fog end", &draft.fog_end, draft.fog_start + 1, 1000, "%d / 1000", ImGuiSliderFlags_AlwaysClamp);
	ImGui::EndDisabled();
	ImGui::Checkbox("Set ambient light", &ambient);
	ImGui::BeginDisabled(!ambient);
	ImGui::SliderInt("Default room brightness", &draft.ambient_light, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);
	ImGui::EndDisabled();

	const asset_entry_t *song = draft.primary_music[0] ? assetCatalogResolve(draft.primary_music) : nullptr;
	const char *name = song && song->type == ASSET_AUDIO && song->ext.audio.name[0]
		? song->ext.audio.name : draft.primary_music[0] ? draft.primary_music : "Stage default";
	if (ImGui::BeginCombo("Music", name)) {
		if (ImGui::Selectable("Stage default", !draft.primary_music[0])) draft.primary_music[0] = '\0';
		assetCatalogIterateUnlockedMusic([](const asset_entry_t *entry, void *context) {
			/* Stage music uses the game's sequencer. */
			if (entry->ext.audio.sound_id < 0 && entry->ext.audio.file_path[0]) return;
			auto *settings = static_cast<scenario_stage_atmosphere_t *>(context);
			ImGui::PushID(entry->id);
			const auto *label = entry->ext.audio.name[0] ? entry->ext.audio.name : entry->id;
			if (ImGui::Selectable(label, strcmp(entry->id, settings->primary_music) == 0)) {
				snprintf(settings->primary_music, sizeof(settings->primary_music), "%s", entry->id);
			}
			ImGui::PopID();
		}, &draft);
		ImGui::EndCombo();
	}
	draft.has_sky = sky ? 1 : 0;
	draft.has_fog = fog ? 1 : 0;
	draft.has_ambient = ambient ? 1 : 0;
	if (ImGui::Button("Apply Atmosphere")) {
		char err[256] = {};
		forgeLevelSessionEditBegin();
		const s32 changed = forgeLevelDocSetAtmosphere(doc, &draft, err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) {
			forgeLevelFileStatus(false, "Atmosphere updated. Save and reopen to play it.");
		} else {
			forgeLevelFileStatus(true, "Atmosphere update failed (%s).", err);
		}
	}
}

static void forgeDrawLevelFileWeather(forge_level_doc_t *doc)
{
	s32 kind, intensity;
	if (!forgeLevelDocGetWeatherStart(doc, &kind, &intensity)) return;

	const char *const options[] = { "Stage default", "Light rain", "Rain", "Heavy rain", "Snow" };
	int selection = 0;
	if (kind == SCENARIO_STAGE_WEATHER_START_RAIN) selection = intensity;
	else if (kind == SCENARIO_STAGE_WEATHER_START_SNOW) selection = 4;
	if (ImGui::Combo("Weather at stage start", &selection, options, IM_ARRAYSIZE(options))) {
		if (selection == 0) {
			kind = SCENARIO_STAGE_WEATHER_START_NONE;
			intensity = 0;
		} else if (selection == 4) {
			kind = SCENARIO_STAGE_WEATHER_START_SNOW;
			intensity = 1;
		} else {
			kind = SCENARIO_STAGE_WEATHER_START_RAIN;
			intensity = selection;
		}
		char err[256] = {};
		forgeLevelSessionEditBegin();
		const s32 changed = forgeLevelDocSetWeatherStart(doc, kind, intensity, err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) {
			forgeLevelFileStatus(false, "Weather updated. Playtest the draft or load the saved level.");
		} else if (err[0]) {
			forgeLevelFileStatus(true, "Weather update failed (%s).", err);
		}
	}
}

static void forgeLevelToolbarSameLine(const char *label)
{
	const ImGuiStyle &style = ImGui::GetStyle();
	const float available = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x
		- ImGui::GetItemRectMax().x;
	const float width = ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * style.FramePadding.x;
	if (available >= width + style.ItemSpacing.x) ImGui::SameLine();
}

/* A successful Play Draft queues a stage transition. Skip the body that frame. */
static bool forgeDrawLevelFileToolbar(void)
{
	const forge_level_source_t *source = forgeLevelSessionSource();
	const char *id = source ? source->arena_id : forgeLevelSessionArena();
	const asset_entry_t *arena = id && id[0] ? assetCatalogResolve(id) : nullptr;
	const char *title = mainGetStageNum() == GRID_BLANK_STAGE ? "Blank Map" : "Legacy map";
	if (arena && arena->type == ASSET_ARENA) {
		const char *name = arena->ext.arena.display_name[0] ? arena->ext.arena.display_name
			: langSafe(arena->ext.arena.name_langid);
		title = name && name[0] ? name : "Unnamed level";
	} else if (id && id[0]) {
		title = "Level unavailable";
	}
	ImGui::TextWrapped("%s", title);
	if (!source) {
		ImGui::TextDisabled("Level File closed");
	} else if (forgeLevelSessionHasUnsavedChanges()) {
		ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Unsaved changes");
	} else {
		ImGui::TextDisabled("No unsaved changes");
	}

	const bool canSave = source && netGetMode() == 0 && !mainStageChangePending();
	const bool directSave = canSave && forgeLevelSessionSaveTarget();
	const char *saveLabel = directSave ? "Save###GridNativeSave" : "Save As...###GridNativeSave";
	if (!source && forgeCanOpenLevelFileFromEditor()) {
		if (ImGui::Button("Open Level File###GridNativeOpen")) {
			s_forge_active_tab = FGT_FILE;
			s_forge_tab_set_request = FGT_FILE;
			s_LevelFileOpenRequested = true;
		}
		forgeLevelToolbarSameLine(saveLabel);
	}
	ImGui::BeginDisabled(!canSave || (directSave && !forgeLevelSessionHasUnsavedChanges()));
	if (ImGui::Button(saveLabel)) {
		s_forge_active_tab = FGT_FILE;
		s_forge_tab_set_request = FGT_FILE;
		s_SidebarScrollRequest[FGT_FILE] = 2; /* Save & Share */
		s_LevelFileSaveRequested = directSave;
	}
	ImGui::EndDisabled();

	forgeLevelToolbarSameLine("Undo");
	ImGui::BeginDisabled(forgeLevelSessionUndoCount() == 0);
	if (ImGui::Button("Undo") && forgeLevelSessionUndo()) {
		forgeLevelFileStatus(false, "Undid the last edit.");
	}
	ImGui::EndDisabled();

	forgeLevelToolbarSameLine("Redo");
	ImGui::BeginDisabled(forgeLevelSessionRedoCount() == 0);
	if (ImGui::Button("Redo") && forgeLevelSessionRedo()) {
		forgeLevelFileStatus(false, "Redid the edit.");
	}
	ImGui::EndDisabled();

	static char draft_error[256] = "";
	static u32 draftErrorGeneration;
	const bool canPlay = forgeCanPlayDraftFromEditor();
	forgeLevelToolbarSameLine("Play Draft");
	ImGui::BeginDisabled(!canPlay);
	const bool play = ImGui::Button("Play Draft") || s_LevelFilePlayDraftRequested;
	if (canPlay && (ImGui::IsItemHovered() || ImGui::IsItemFocused())) {
		char binding[32];
		if (pdguiGlyphGetActionLabel(ACTION_FORGE_TOGGLE, binding, sizeof(binding))) {
			ImGui::SetTooltip("Play the current draft, including unsaved edits. Return with %s.", binding);
		} else {
			ImGui::SetTooltip("Play the current draft, including unsaved edits. Use the Grid toggle to return.");
		}
	}
	ImGui::EndDisabled();
	s_LevelFilePlayDraftRequested = false;
	if (canPlay && play) {
		draft_error[0] = '\0';
		if (forgeLevelSessionPlayDraft(draft_error, sizeof(draft_error))) return true;
		draftErrorGeneration = forgeLevelSessionGeneration();
		if (!draft_error[0]) snprintf(draft_error, sizeof(draft_error), "Could not start the draft.");
	}
	if (source && draft_error[0] && draftErrorGeneration == forgeLevelSessionGeneration()) {
		ImGui::TextWrapped("%s", draft_error);
	}
	if (source) forgeLevelFileDrawStatus();
	return false;
}

struct ForgePropPaletteEntry {
	std::string id;
	std::string name;
};

static void forgeCollectPropPalette(const asset_entry_t *entry, void *data)
{
	if (!entry || !entry->enabled || !entry->id[0] || entry->temporary || entry->grid_offer_only) return;
	auto &rows = *static_cast<std::vector<ForgePropPaletteEntry> *>(data);
	if (strcmp(entry->id, "base:model_crate2_cq") == 0) {
		rows.push_back({entry->id, "Combat Crate"});
		return;
	}

	const char *slug = strchr(entry->id, ':');
	slug = slug ? slug + 1 : entry->id;
	if (strncmp(slug, "model_", 6) == 0 && slug[6]) slug += 6;
	std::string name;
	bool capitalize = true;
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(slug); *p; ++p) {
		if (*p == '_' || *p == '-' || *p == '.') {
			if (!name.empty() && name.back() != ' ') name += ' ';
			capitalize = true;
		} else {
			name += static_cast<char>(capitalize ? std::toupper(*p) : *p);
			capitalize = false;
		}
	}
	if (!name.empty() && name.back() == ' ') name.pop_back();
	rows.push_back({entry->id, name.empty() ? "Unnamed model" : name});
}

static const std::vector<ForgePropPaletteEntry> &forgePropPaletteEntries()
{
	static std::vector<ForgePropPaletteEntry> rows;
	static u32 recordedGeneration;
	static bool built;
	const u32 generation = assetCatalogGetGeneration();
	if (!built || generation != recordedGeneration) {
		rows.clear();
		assetCatalogIterateByType(ASSET_MODEL, forgeCollectPropPalette, &rows);
		std::sort(rows.begin(), rows.end(), [](const ForgePropPaletteEntry &a, const ForgePropPaletteEntry &b) {
			return a.name == b.name ? a.id < b.id : a.name < b.name;
		});
		recordedGeneration = generation;
		built = true;
	}
	return rows;
}

static void forgeBeginPropPlacement(const ForgePropPaletteEntry &row, bool dragging)
{
	snprintf(s_PropPaletteSelection, sizeof(s_PropPaletteSelection), "%s", row.id.c_str());
	s_PropPlacement = {true, dragging, !dragging, forgeEditorControlsActive() && !s_TransientEditorControls,
		forgeLevelSessionGeneration(), row.id, row.name};
	forgeEditorReleaseControls();
}

static void forgeDrawPropPalette()
{
	if (netGetMode() != 0 || mainStageChangePending()) return;
	ImGui::TextUnformatted("Place a prop");
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##PropPaletteSearch", "Search models", s_PropPaletteSearch, sizeof(s_PropPaletteSearch));
	const auto &rows = forgePropPaletteEntries();
	std::vector<const ForgePropPaletteEntry *> matching;
	int selection = -1;
	for (const auto &row : rows) {
		if (s_PropPaletteSearch[0] && !f_icontains(row.name.c_str(), s_PropPaletteSearch)
				&& !f_icontains(row.id.c_str(), s_PropPaletteSearch)) continue;
		if (row.id == s_PropPaletteSelection) selection = static_cast<int>(matching.size());
		matching.push_back(&row);
	}
	const bool returning = s_PropPaletteReturnRequested;
	if (matching.empty()) {
		s_PropPaletteReturnRequested = false;
		ImGui::TextDisabled("No models found.");
		return;
	}

	const float listHeight = ImGui::GetTextLineHeightWithSpacing() * 5.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
	if (ImGui::BeginChild("##PropPalette", ImVec2(0, listHeight),
			ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened)) {
		const bool restoreFocus = returning && forgeEditorControlsActive();
		const int returnRow = selection >= 0 ? selection : 0;
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(matching.size()));
		if (returning) clipper.IncludeItemByIndex(returnRow);
		while (clipper.Step()) {
			for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
				const auto &row = *matching[index];
				ImGui::PushID(row.id.c_str());
				if (restoreFocus && index == returnRow) ImGui::SetKeyboardFocusHere();
				char label[CATALOG_ID_LEN + 32];
				snprintf(label, sizeof(label), "%s###prop_palette_model", row.name.c_str());
				const bool selected = ImGui::Selectable(label, index == selection);
				if (ImGui::IsItemActive() && ImGui::GetCurrentContext()->ActiveIdSource == ImGuiInputSource_Mouse
						&& ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
					forgeBeginPropPlacement(row, true);
				} else if (selected) {
					forgeBeginPropPlacement(row, false);
				}
				if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) {
					ImGui::SetTooltip("Select to aim or drag into the level.\n%s\n%s", row.name.c_str(), row.id.c_str());
				}
				if (returning && index == returnRow) {
					ImGui::SetScrollHereY();
					s_PropPaletteReturnRequested = false;
				}
				ImGui::PopID();
			}
		}
	}
	ImGui::EndChild();
}

static void forgeDrawLevelFileSection(void)
{
	forgeScrollAnchorMaybe(FGT_FILE, 0);   /* sidebar "Starts" (the top) */
	ImGui::SeparatorText("Level File");

	const char *arena = forgeLevelSessionArena();
	forge_level_doc_t *doc = forgeLevelSessionDoc();
	const forge_level_source_t *source = forgeLevelSessionSource();
	const bool openLevelFile = s_LevelFileOpenRequested && forgeCanOpenLevelFileFromEditor();
	const bool editPlacement = s_LevelFilePoseRequested && netGetMode() == 0;
	const bool saveOpened = s_LevelFileSaveRequested && netGetMode() == 0;
	const int toolRequest = netGetMode() == 0 ? s_LevelFileToolRequested : 0;
	s_LevelFileOpenRequested = false;
	s_LevelFilePoseRequested = false;
	s_LevelFileSaveRequested = false;
	s_LevelFileToolRequested = 0;
	char err[256];
	err[0] = '\0';

	if (!doc || !source) {
		if (saveOpened) forgeLevelFileStatus(true, "Open an original Level File to save it.");
		if (!arena || !arena[0]) {
			ImGui::TextWrapped("Choose a level in %s to edit its level file.", forgeDisplayName());
		} else {
			ImGui::Text("Level: %s", arena);
			if (ImGui::Button("Open Level File") || openLevelFile || editPlacement || toolRequest) {
				forgeLevelFileForget();
				if (forgeLevelSessionOpen(arena, err, sizeof(err))) {
					forgeLevelFileStatus(false, "Opened %s.", arena);
					s_LevelFilePoseRequested = editPlacement;
					s_LevelFileToolRequested = toolRequest;
				} else {
					forgeLevelFileStatus(true, "Could not open %s: %s", arena, err);
				}
			}
			ImGui::TextWrapped("Edit player starts, objects, and level rules from this archive. "
				"Save your original in place or save a separate level.");
		}
		forgeLevelFileDrawStatus();
		return;
	}

	/* A marker clicked in the world selects its start or object here. */
	{
		char pickStart[32];
		char pickObject[32];
		s32 pickIndex = -1;
		if (pdguiForgeLevelMarkersTakePick(pickStart, sizeof(pickStart), pickObject, sizeof(pickObject),
				&pickIndex) && !menupoolIsActive(MENU_TYPE_GRID_PLACEMENT)
				&& !menupoolIsActive(MENU_TYPE_GRID_LEVEL_TOOLS)) {
			if (pickObject[0]) {
				snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", pickObject);
				s_LevelFileObjectIndex = (int)pickIndex;
				s_LevelFilePosePreferObject = true;
				forgeLevelFileStatus(false, "Selected %s.", pickObject);
			} else if (pickStart[0]) {
				snprintf(s_LevelFileStart, sizeof(s_LevelFileStart), "%s", pickStart);
				s_LevelFilePosePreferObject = false;
				forgeLevelFileStatus(false, "Selected %s.", pickStart);
			}
		}
	}

	ImGui::Text("%d player starts, %d objects, %d pads",
		(int)forgeLevelDocSpawnCount(doc), (int)forgeLevelDocObjectCount(doc),
		(int)forgeLevelDocPadCount(doc));
	forgeDrawLevelFilePose(doc, editPlacement);
	{
		bool markers = pdguiForgeLevelMarkersGet() != 0;
		if (ImGui::Checkbox("Show starts and objects in the world", &markers)) {
			pdguiForgeLevelMarkersSet(markers ? 1 : 0);
		}
		ImGui::TextDisabled("Diamonds are player starts, squares are objects. They show through walls; "
			"the one selected in a list below is yellow.");
		ImGui::SetNextItemWidth(180 * pdguiScale(1.0f));
		ImGui::SliderInt("Nudge step (units)##LevelFileNudgeStep", &s_LevelFileNudgeStep, 5, 500);
	}

	f32 position[3];
	f32 look[3];
	forgeLevelFileCamera(position, look);

	/* -------------------------------------------------------------
	 * Player starts
	 * ------------------------------------------------------------- */
	ImGui::SeparatorText("Player Starts");
	if (ImGui::Button("Add Start Here")) {
		char id[32];
		forgeLevelSessionEditBegin();
		const s32 levelEdit = forgeLevelDocAddSpawn(doc, position, look, nullptr, id, sizeof(id), err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		if (levelEdit) {
			snprintf(s_LevelFileStart, sizeof(s_LevelFileStart), "%s", id);
			forgeLevelFileStatus(false, "Added %s where the camera is.", id);
		} else {
			forgeLevelFileStatus(true, "Could not add a start: %s", err);
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(s_LevelFileStart[0] == '\0');
	if (ImGui::Button("Move Start Here")) {
		forgeLevelSessionEditBegin();
		const s32 levelEdit = forgeLevelDocMoveSpawn(doc, s_LevelFileStart, position, look, err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		if (levelEdit) {
			forgeLevelFileStatus(false, "Moved %s to where the camera is.", s_LevelFileStart);
		} else {
			forgeLevelFileStatus(true, "Could not move %s: %s", s_LevelFileStart, err);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Remove Start")) {
		forgeLevelSessionEditBegin();
		const s32 levelEdit = forgeLevelDocRemoveSpawn(doc, s_LevelFileStart, err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		if (levelEdit) {
			forgeLevelFileStatus(false, "Removed %s.", s_LevelFileStart);
			s_LevelFileStart[0] = '\0';
		} else {
			forgeLevelFileStatus(true, "Could not remove %s: %s", s_LevelFileStart, err);
		}
	}
	/* Turn the selected start where it stands, 45 degrees at a time. */
	for (int turn = 0; turn < 2; turn++) {
		ImGui::SameLine();
		if (ImGui::Button(turn == 0 ? "Turn Left 45###LevelFileStartLeft" : "Turn Right 45###LevelFileStartRight")) {
			forge_level_pad_t pad;
			f32 turned[3];
			if (!forgeLevelFileStartPad(doc, s_LevelFileStart, &pad)
					|| !forgeLevelTurnLook(pad.look, turn == 0 ? -45.0f : 45.0f, turned)) {
				forgeLevelFileStatus(true, "%s has no facing to turn.", s_LevelFileStart);
			} else {
				forgeLevelSessionEditBegin();
				const s32 levelEdit = forgeLevelDocMoveSpawn(doc, s_LevelFileStart, pad.position, turned, err,
					sizeof(err));
				forgeLevelSessionEditEnd(levelEdit);
				if (levelEdit) {
					forgeLevelFileStatus(false, "Turned %s 45 degrees %s.", s_LevelFileStart,
						turn == 0 ? "left" : "right");
				} else {
					forgeLevelFileStatus(true, "Could not turn %s: %s", s_LevelFileStart, err);
				}
			}
		}
	}
	/* Nudge the selected start a step, as seen from the camera. */
	{
		f32 offset[3];
		if (s_LevelFileStart[0] && forgeLevelFileNudgeRow("LevelFileStartNudge", look, offset)) {
			forge_level_pad_t pad;
			if (!forgeLevelFileStartPad(doc, s_LevelFileStart, &pad)) {
				forgeLevelFileStatus(true, "%s has no place to move from.", s_LevelFileStart);
			} else {
				const f32 to[3] = { pad.position[0] + offset[0], pad.position[1] + offset[1], pad.position[2] + offset[2] };
				forgeLevelSessionEditBegin();
				const s32 levelEdit = forgeLevelDocMoveSpawn(doc, s_LevelFileStart, to, pad.look, err, sizeof(err));
				forgeLevelSessionEditEnd(levelEdit);
				if (levelEdit) {
					forgeLevelFileStatus(false, "Nudged %s to (%.0f, %.0f, %.0f).", s_LevelFileStart, to[0], to[1], to[2]);
				} else {
					forgeLevelFileStatus(true, "Could not move %s: %s", s_LevelFileStart, err);
				}
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled("A start goes where the camera is, facing the way it faces. "
		"The game finds the floor under it.");

	ImGui::BeginChild("LevelFileStarts", ImVec2(-1, 130 * pdguiScale(1.0f)), true);
	{
		ImGuiListClipper clipper;
		clipper.Begin((int)forgeLevelDocSpawnCount(doc));
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
				forge_level_spawn_t spawn;
				forge_level_pad_t pad;
				char label[192];
				if (!forgeLevelDocGetSpawn(doc, i, &spawn)) continue;
				if (forgeLevelDocFindPad(doc, spawn.pad_ref, &pad)) {
					snprintf(label, sizeof(label), "%s   team %s   at (%.0f, %.0f, %.0f)%s",
						spawn.spawn_id, spawn.team, pad.position[0], pad.position[1],
						pad.position[2], pad.added ? "   new place" : "");
				} else {
					snprintf(label, sizeof(label), "%s   team %s", spawn.spawn_id, spawn.team);
				}
				ImGui::PushID(i);
				if (ImGui::Selectable(label, strcmp(s_LevelFileStart, spawn.spawn_id) == 0)) {
					snprintf(s_LevelFileStart, sizeof(s_LevelFileStart), "%s", spawn.spawn_id);
					s_LevelFilePosePreferObject = false;
				}
				ImGui::PopID();
			}
		}
	}
	ImGui::EndChild();

	ImGui::SeparatorText("King of the Hill");
	forge_level_pad_t selectedStart = {};
	const bool haveStart = s_LevelFileStart[0]
		&& forgeLevelFileStartPad(doc, s_LevelFileStart, &selectedStart);
	ImGui::BeginDisabled(!haveStart || forgeLevelDocHasHill(doc, selectedStart.pad_ref));
	if (ImGui::Button("Add Hill at Selected Start")) {
		forgeLevelSessionEditBegin();
		const s32 changed = forgeLevelDocSetHill(doc, selectedStart.pad_ref, 1, err, sizeof(err));
		forgeLevelSessionEditEnd(changed);
		if (changed) {
			forgeLevelFileStatus(false, "Added hill at %s.", selectedStart.pad_ref);
		} else {
			forgeLevelFileStatus(true, "Could not add hill: %s", err);
		}
	}
	ImGui::EndDisabled();
	for (s32 i = 0; i < forgeLevelDocHillCount(doc); i++) {
		char pad[32];
		if (!forgeLevelDocGetHillPad(doc, i, pad, sizeof(pad))) continue;
		ImGui::PushID(i);
		ImGui::Text("%s", pad);
		ImGui::SameLine();
		const bool remove = ImGui::SmallButton("Remove Hill");
		ImGui::PopID();
		if (remove) {
			forgeLevelSessionEditBegin();
			const s32 changed = forgeLevelDocSetHill(doc, pad, 0, err, sizeof(err));
			forgeLevelSessionEditEnd(changed);
			if (changed) {
				forgeLevelFileStatus(false, "Removed hill at %s.", pad);
			} else {
				forgeLevelFileStatus(true, "Could not remove hill: %s", err);
			}
			break;
		}
	}
	forgeDrawLevelFileCaseMarkers(doc, haveStart ? &selectedStart : nullptr);
	int requestedTool = toolRequest;
	if (netGetMode() == 0) {
		if (ImGui::Button("Triggers")) requestedTool = kForgeFileEditTriggers;
		ImGui::SameLine();
		if (ImGui::Button("Bot Navigation")) requestedTool = kForgeFileEditNavigation;
		ImGui::SameLine();
		if (ImGui::Button("Level Logic")) requestedTool = kForgeFileEditLogic;
		ImGui::SameLine();
		if (ImGui::Button("Logic Zones")) requestedTool = kForgeFileEditLogicZones;
	}
	/* -------------------------------------------------------------
	 * Objects
	 * ------------------------------------------------------------- */
	forgeScrollAnchorMaybe(FGT_FILE, 1);   /* sidebar "Objects" */
	ImGui::SeparatorText("Objects");
	forgeDrawPropPalette();
	forge_level_object_t selected = {};
	const bool haveSelected = s_LevelFileObjectIndex >= 0
		&& forgeLevelDocGetObject(doc, s_LevelFileObjectIndex, &selected)
		&& strcmp(selected.record_id, s_LevelFileObject) == 0;

	/* New objects, for a level that has none of the kind to copy. A new weapon
	 * place takes the lowest place number the level does not use yet. */
	if (ImGui::Button("New Weapon Place Here")) {
		bool used[16] = {};
		s32 slot = 0;
		char id[32];
		for (s32 i = 0; i < forgeLevelDocObjectCount(doc); i++) {
			forge_level_object_t object;
			if (forgeLevelDocGetObject(doc, i, &object) && object.weapon_slot >= 0 && object.weapon_slot < 16) {
				used[object.weapon_slot] = true;
			}
		}
		while (slot < 15 && used[slot]) slot++;
		if (used[slot]) slot = 0;
		forgeLevelSessionEditBegin();
		const s32 levelEdit = forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_WEAPON_PLACE, slot, position, look, id, sizeof(id),
				err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		if (levelEdit) {
			forgeLevelFileStatus(false, "Added %s where the camera is: weapon place %d of the match.", id,
				(int)slot + 1);
			snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", id);
			s_LevelFileObjectIndex = (int)forgeLevelDocObjectCount(doc) - 1;
		} else {
			forgeLevelFileStatus(true, "Could not add a weapon place: %s", err);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("New Ammo Crate Here")) {
		char id[32];
		forgeLevelSessionEditBegin();
		const s32 levelEdit = forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0, position, look, id, sizeof(id),
				err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		if (levelEdit) {
			forgeLevelFileStatus(false, "Added the ammo crate %s where the camera is.", id);
			snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", id);
			s_LevelFileObjectIndex = (int)forgeLevelDocObjectCount(doc) - 1;
		} else {
			forgeLevelFileStatus(true, "Could not add an ammo crate: %s", err);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("New Hoverbike Here")) {
		forgePlaceLevelHoverbike(doc, kForgeDefaultHoverbikeModel, position, look);
	}
	if (netGetMode() == 0) {
		if (ImGui::SmallButton("Choose Hoverbike")) requestedTool = kForgeFileAddHoverbike;
	}
	forgeDrawLevelFileTools(doc, haveStart ? &selectedStart : nullptr, requestedTool, position, look);

	/* A new plain prop, drawn with a model chosen from the catalog. The same
	 * choice redraws a selected plain prop (below). */
	ImGui::SetNextItemWidth(150 * pdguiScale(1.0f));
	ImGui::InputTextWithHint("##LevelFileModelFilter", "filter models", s_LevelFileModelFilter,
		sizeof(s_LevelFileModelFilter));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(250 * pdguiScale(1.0f));
	if (ImGui::BeginCombo("##LevelFileModel", s_LevelFileModel[0] ? s_LevelFileModel : "choose a model")) {
		assetCatalogIterateByType(ASSET_MODEL, forgeLevelFileModelRow, NULL);
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(s_LevelFileModel[0] == '\0');
	const bool addProp = ImGui::Button("New Prop Here");
	const bool addDecoration = ImGui::Button("New Decoration Here");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Players and projectiles pass through decorations.");
	if (addProp || addDecoration) {
		char id[32];
		forgeLevelSessionEditBegin();
		const auto add = addDecoration ? forgeLevelDocAddDecoration : forgeLevelDocAddProp;
		const s32 levelEdit = add(doc, s_LevelFileModel, position, look, id, sizeof(id), err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		const char *kind = addDecoration ? "decoration" : "prop";
		if (levelEdit) {
			forgeLevelFileStatus(false, "Added the %s %s (%s) where the camera is.", kind, id, s_LevelFileModel);
			snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", id);
			s_LevelFileObjectIndex = (int)forgeLevelDocObjectCount(doc) - 1;
		} else {
			forgeLevelFileStatus(true, "Could not add a %s: %s", kind, err);
		}
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!haveSelected || !selected.movable);
	if (ImGui::Button("Move Object Here")) {
		forgeLevelSessionEditBegin();
		const s32 levelEdit = forgeLevelDocMoveObject(doc, s_LevelFileObject, position, look, err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		if (levelEdit) {
			forgeLevelFileStatus(false, "Moved %s to where the camera is.", s_LevelFileObject);
		} else {
			forgeLevelFileStatus(true, "Could not move %s: %s", s_LevelFileObject, err);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Copy Object Here")) {
		char id[32];
		forgeLevelSessionEditBegin();
		const s32 levelEdit = forgeLevelDocDuplicateObject(doc, s_LevelFileObject, position, look, id, sizeof(id),
				err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		if (levelEdit) {
			forgeLevelFileStatus(false, "Copied %s to %s where the camera is.", s_LevelFileObject, id);
			snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", id);
			s_LevelFileObjectIndex = (int)forgeLevelDocObjectCount(doc) - 1;
		} else {
			forgeLevelFileStatus(true, "Could not copy %s: %s", s_LevelFileObject, err);
		}
	}
	/* Turn the selected object where it stands, 45 degrees at a time. */
	for (int turn = 0; turn < 2; turn++) {
		ImGui::SameLine();
		if (ImGui::Button(turn == 0 ? "Turn Left 45###LevelFileObjectLeft" : "Turn Right 45###LevelFileObjectRight")) {
			forge_level_pad_t pad;
			f32 turned[3];
			if (!haveSelected || !forgeLevelDocFindPad(doc, selected.pad_ref, &pad)
					|| !forgeLevelTurnLook(pad.look, turn == 0 ? -45.0f : 45.0f, turned)) {
				forgeLevelFileStatus(true, "%s has no facing to turn.", s_LevelFileObject);
			} else {
				forgeLevelSessionEditBegin();
				const s32 levelEdit = forgeLevelDocMoveObject(doc, s_LevelFileObject, pad.position, turned, err,
					sizeof(err));
				forgeLevelSessionEditEnd(levelEdit);
				if (levelEdit) {
					forgeLevelFileStatus(false, "Turned %s 45 degrees %s.", s_LevelFileObject,
						turn == 0 ? "left" : "right");
				} else {
					forgeLevelFileStatus(true, "Could not turn %s: %s", s_LevelFileObject, err);
				}
			}
		}
	}
	/* Nudge the selected object a step, as seen from the camera. */
	{
		f32 offset[3];
		if (forgeLevelFileNudgeRow("LevelFileObjectNudge", look, offset)) {
			forge_level_pad_t pad;
			if (!haveSelected || !forgeLevelDocFindPad(doc, selected.pad_ref, &pad)) {
				forgeLevelFileStatus(true, "%s has no place to move from.", s_LevelFileObject);
			} else {
				const f32 to[3] = { pad.position[0] + offset[0], pad.position[1] + offset[1], pad.position[2] + offset[2] };
				forgeLevelSessionEditBegin();
				const s32 levelEdit = forgeLevelDocMoveObject(doc, s_LevelFileObject, to, pad.look, err, sizeof(err));
				forgeLevelSessionEditEnd(levelEdit);
				if (levelEdit) {
					forgeLevelFileStatus(false, "Nudged %s to (%.0f, %.0f, %.0f).", s_LevelFileObject, to[0], to[1], to[2]);
				} else {
					forgeLevelFileStatus(true, "Could not move %s: %s", s_LevelFileObject, err);
				}
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!haveSelected || !selected.removable);
	const bool putBack = haveSelected && selected.removed;
	if (ImGui::Button(putBack ? "Put Object Back###LevelFileRemove" : "Remove Object###LevelFileRemove")) {
		forgeLevelSessionEditBegin();
		const s32 levelEdit = forgeLevelDocSetObjectRemoved(doc, s_LevelFileObject, putBack ? 0 : 1, err, sizeof(err));
		forgeLevelSessionEditEnd(levelEdit);
		if (!levelEdit) {
			forgeLevelFileStatus(true, "Could not change %s: %s", s_LevelFileObject, err);
		} else if (putBack) {
			forgeLevelFileStatus(false, "Put %s back in the level.", s_LevelFileObject);
		} else {
			forgeLevelFileStatus(false, "Removed %s. Put Object Back undoes it.", s_LevelFileObject);
		}
	}
	ImGui::EndDisabled();
	if (haveSelected && !selected.movable) {
		ImGui::TextDisabled("%s stands on no pad, so it cannot be moved or copied here.",
			selected.record_id);
	}
	if (haveSelected && !selected.removable) {
		ImGui::TextDisabled("%s is a %s, which cannot be removed here.", selected.record_id,
			selected.kind);
	}
	ImGui::TextWrapped(
		"A copy is a new object of the same kind with the same settings. In a "
		"multiplayer level a weapon is a slot of the match's weapon set, so a copy "
		"of a weapon is another spot for that slot. Removing an object leaves it out "
		"of the level and keeps its line in the list, so anything that refers to it "
		"still finds it. Lifts, escalator steps and characters cannot be removed yet.");

	/* What a selected plain prop is drawn with: the model chosen above. */
	if (haveSelected && strcmp(selected.kind, "prop") == 0) {
		ImGui::SeparatorText("What It Looks Like");
		ImGui::TextWrapped("%s is drawn with %s.", selected.record_id,
			selected.model_catalog_id[0] ? selected.model_catalog_id : "no model the level names");
		if (!selected.model_settable) {
			ImGui::TextDisabled("The level's two object files do not agree about its model, "
				"so it cannot be changed here.");
		}
		ImGui::BeginDisabled(!selected.model_settable || s_LevelFileModel[0] == '\0');
		if (ImGui::Button("Draw It With The Chosen Model")) {
			forgeLevelSessionEditBegin();
			const s32 levelEdit = forgeLevelDocSetObjectModel(doc, s_LevelFileObject, s_LevelFileModel, err, sizeof(err));
			forgeLevelSessionEditEnd(levelEdit);
			if (levelEdit) {
				forgeLevelFileStatus(false, "%s is now drawn with %s.", s_LevelFileObject, s_LevelFileModel);
			} else {
				forgeLevelFileStatus(true, "Could not change %s: %s", s_LevelFileObject, err);
			}
		}
		ImGui::EndDisabled();
	}

	/* The size a selected object is drawn at (`base.extra_scale`, 256 is life
	 * size). One of the match's weapon places is sized by the match's weapon
	 * set instead, so it has no size here. */
	if (haveSelected && selected.weapon_slot < 0) {
		const s32 size = forgeLevelDocObjectExtraScale(doc, s_LevelFileObjectIndex);
		ImGui::SeparatorText("Size");
		ImGui::Text("%s is drawn at %d%% of its model's size.", selected.record_id, (int)((size * 100 + 128) / 256));
		ImGui::SetNextItemWidth(180 * pdguiScale(1.0f));
		ImGui::SliderInt("##LevelFileSizePercent", &s_LevelFileSizePercent, 25, 400, "%d%%");
		ImGui::SameLine();
		if (ImGui::Button("Set Size###LevelFileSetSize")) {
			const s32 scale = (s32)((s_LevelFileSizePercent * 256 + 50) / 100);
			if (scale == size) {
				forgeLevelFileStatus(false, "%s is already that size.", s_LevelFileObject);
			} else {
				forgeLevelSessionEditBegin();
				const s32 levelEdit = forgeLevelDocSetObjectScale(doc, s_LevelFileObject, scale, err, sizeof(err));
				forgeLevelSessionEditEnd(levelEdit);
				if (levelEdit) {
					forgeLevelFileStatus(false, "%s is now drawn at %d%% of its model's size.", s_LevelFileObject,
						s_LevelFileSizePercent);
				} else {
					forgeLevelFileStatus(true, "Could not change %s: %s", s_LevelFileObject, err);
				}
			}
		}
	}

	/* What a selected weapon pickup gives: one of the match's weapon places,
	 * which the match's weapon set fills, or one weapon of the catalog. */
	if (haveSelected && strcmp(selected.kind, "weapon_pickup") == 0) {
		ImGui::SeparatorText("What It Gives");
		if (selected.weapon_slot >= 0) {
			ImGui::TextWrapped("%s is weapon place %d of the match: it gives whatever the match's "
				"weapon set has in that place.", selected.record_id, (int)selected.weapon_slot + 1);
		} else if (selected.weapon_catalog_id[0]) {
			ImGui::TextWrapped("%s always gives %s.", selected.record_id, selected.weapon_catalog_id);
		}
		if (!selected.weapon_settable) {
			ImGui::TextDisabled("The level's two object files do not agree about this pickup, "
				"so what it gives cannot be changed here.");
		}
		ImGui::BeginDisabled(!selected.weapon_settable);
		ImGui::SetNextItemWidth(260 * pdguiScale(1.0f));
		if (ImGui::BeginCombo("##LevelFileWeapon", s_LevelFileWeapon[0] ? s_LevelFileWeapon
				: "choose a weapon")) {
			assetCatalogIterateByType(ASSET_WEAPON, forgeLevelFileWeaponRow, NULL);
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(s_LevelFileWeapon[0] == '\0');
		if (ImGui::Button("Always Give This Weapon")) {
			forgeLevelSessionEditBegin();
			const s32 levelEdit = forgeLevelSessionGivePickup(s_LevelFileObject, s_LevelFileWeapon, err, sizeof(err));
			forgeLevelSessionEditEnd(levelEdit);
			if (levelEdit) {
				forgeLevelFileStatus(false, "%s now always gives %s.", s_LevelFileObject, s_LevelFileWeapon);
			} else {
				forgeLevelFileStatus(true, "Could not change %s: %s", s_LevelFileObject, err);
			}
		}
		ImGui::EndDisabled();
		ImGui::SetNextItemWidth(110 * pdguiScale(1.0f));
		ImGui::InputInt("##LevelFilePlace", &s_LevelFilePlace);
		if (s_LevelFilePlace < 1) s_LevelFilePlace = 1;
		if (s_LevelFilePlace > 16) s_LevelFilePlace = 16;
		ImGui::SameLine();
		if (ImGui::Button("Make It This Weapon Place")) {
			forgeLevelSessionEditBegin();
			const s32 levelEdit = forgeLevelDocSetPickupSlot(doc, s_LevelFileObject, (s32)s_LevelFilePlace - 1, err, sizeof(err));
			forgeLevelSessionEditEnd(levelEdit);
			if (levelEdit) {
				forgeLevelFileStatus(false, "%s is now weapon place %d of the match.", s_LevelFileObject,
					s_LevelFilePlace);
			} else {
				forgeLevelFileStatus(true, "Could not change %s: %s", s_LevelFileObject, err);
			}
		}
		ImGui::EndDisabled();
	}

	ImGui::BeginChild("LevelFileObjects", ImVec2(-1, 190 * pdguiScale(1.0f)), true);
	{
		ImGuiListClipper clipper;
		clipper.Begin((int)forgeLevelDocObjectCount(doc));
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
				forge_level_object_t object;
				char label[256];
				if (!forgeLevelDocGetObject(doc, i, &object)) continue;
				const char *what = object.weapon_catalog_id[0] ? object.weapon_catalog_id
					: object.model_catalog_id;
				snprintf(label, sizeof(label), "%s   %s   %s   %s%s", object.record_id, object.kind,
					what[0] ? what : "-", object.pad_ref[0] ? object.pad_ref : "no pad",
					object.removed ? "   REMOVED" : "");
				ImGui::PushID(i);
				if (ImGui::Selectable(label, i == s_LevelFileObjectIndex)) {
					s_LevelFileObjectIndex = i;
					snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", object.record_id);
					s_LevelFilePosePreferObject = true;
				}
				ImGui::PopID();
			}
		}
	}
	ImGui::EndChild();

	/* -------------------------------------------------------------
	 * Save
	 * ------------------------------------------------------------- */
	forgeScrollAnchorMaybe(FGT_FILE, 3);
	forgeDrawLevelFileAtmosphere(doc);
	if (netGetMode() == 0 && ImGui::Button("Room Brightness")) {
		s_LevelFileToolRequested = kForgeFileEditRoomBrightness;
	}
	forgeDrawLevelFileWeather(doc);
	const bool focusSave = forgeScrollAnchorMaybe(FGT_FILE, 2) && forgeEditorControlsActive();
	ImGui::SeparatorText("Save Level");
	const char *saveTarget = forgeLevelSessionSaveTarget();
	const bool canSaveOpened = saveTarget && forgeLevelSessionHasUnsavedChanges();
	bool clickSaveOpened = false;
	if (saveTarget) {
		ImGui::BeginDisabled(!canSaveOpened);
		clickSaveOpened = ImGui::Button("Save Opened Level");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextUnformatted(saveTarget);
	}
	if (clickSaveOpened || saveOpened) {
		if (!saveTarget) {
			forgeLevelFileStatus(true, "The opened level cannot be saved in place. Use Save As Level.");
		} else if (!canSaveOpened) {
			forgeLevelFileStatus(false, "The opened level has no unsaved changes.");
		} else if (forgeLevelSessionSaveOpened(err, sizeof(err)) == MOD_AUTHOR_OK) {
			forgeLevelFileStatus(false, "Saved the opened level.");
		} else {
			forgeLevelFileStatus(true, "Could not save the opened level: %s", err);
		}
	}
	static u32 checkedGeneration;
	static u32 checkedCatalogGeneration;
	static bool draftValid;
	static char draftCheck[320];
	if (focusSave) ImGui::SetKeyboardFocusHere();
	if (ImGui::Button("Validate Draft")) {
		char digest[SHA256_HEX_SIZE];
		draftValid = forgeLevelValidateOriginalDraft(doc, forgeLevelSessionSource(), digest, err, sizeof(err)) != 0;
		checkedGeneration = forgeLevelSessionGeneration();
		checkedCatalogGeneration = assetCatalogGetGeneration();
		if (draftValid) snprintf(draftCheck, sizeof(draftCheck), "Native draft source validated  %.12s", digest);
		else snprintf(draftCheck, sizeof(draftCheck), "Draft validation failed. %s", err);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Check the original arena's current draft before saving or playing it.");
	if (draftCheck[0] && checkedGeneration == forgeLevelSessionGeneration()
			&& checkedCatalogGeneration == assetCatalogGetGeneration()) {
		ImGui::PushStyleColor(ImGuiCol_Text, draftValid ? ImVec4(0.6f, 0.95f, 0.6f, 1.0f) : ImVec4(1.0f, 0.55f, 0.5f, 1.0f));
		ImGui::TextWrapped("%s", draftCheck);
		ImGui::PopStyleColor();
	}
	ImGui::InputText("Level Name", s_LevelFileName, sizeof(s_LevelFileName));
	ImGui::BeginDisabled(s_LevelFileName[0] == '\0');
	if (ImGui::Button("Save As Level")) {
		mod_author_result_t *result = (mod_author_result_t *)calloc(1, sizeof(*result));
		if (!result) {
			forgeLevelFileStatus(true, "Could not save: out of memory.");
		} else {
			if (forgeLevelSessionSaveAs(s_LevelFileName, result, err, sizeof(err)) == MOD_AUTHOR_OK) {
				if (err[0]) {
					forgeLevelFileStatus(true, "%s", err);
				} else {
					forgeLevelFileStatus(false,
						"%s the level \"%s\" (%s). Choose it in %s or in match setup to play it.",
						result->updated ? "Updated" : "Saved", s_LevelFileName, result->catalog_id, forgeDisplayName());
				}
			} else {
				forgeLevelFileStatus(true, "Could not save: %s", err);
			}
			free(result);
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!forgeLevelSessionCanReviewVariantSources());
	const bool reviewVariant = ImGui::Button("Review Variant Mods");
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	forgeDrawLevelVariantReview(doc, reviewVariant);
	ImGui::BeginDisabled(forgeLevelSessionHasUnsavedChanges() != 0);
	if (ImGui::Button("Pin Saved Revision")) {
		forge_revision_t revision;
		if (forgeLevelSessionPinSavedRevision(&revision, err, sizeof(err))) {
			forgeLevelFileStatus(false, "Pinned %.12s for %s in this profile.",
				revision.digest, revision.arena_id);
		} else {
			forgeLevelFileStatus(true, "Could not pin revision: %s", err);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Allow Temporary Sharing")) ImGui::OpenPopup("Share Pinned Revision");
	ImGui::EndDisabled();
	if (ImGui::BeginPopup("Share Pinned Revision")) {
		ImGui::TextUnformatted("Allow players joining your match to temporarily receive this pinned revision.\n"
			"Confirm that you have permission to share everything inside the arena.");
		if (ImGui::Button("Allow Sharing")) {
			if (forgeLevelSessionAllowMatchSharing(err, sizeof(err))) {
				const forge_revision_t *permission = forgeLevelMatchSharePermission();
				forgeLevelFileStatus(false, "Temporary sharing allowed for %s (%.12s).",
					permission->arena_id, permission->digest);
			} else {
				forgeLevelFileStatus(true, "Could not allow sharing: %s", err);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if (const forge_revision_t *permission = forgeLevelMatchSharePermission()) {
		ImGui::Text("Sharing allowed  %s (%.12s)", permission->arena_id, permission->digest);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", permission->digest);
		ImGui::SameLine();
		if (ImGui::Button("Revoke Sharing")) {
			forgeLevelRevokeMatchSharing();
			forgeLevelFileStatus(false, "Temporary sharing permission revoked.");
		}
	}
	ImGui::BeginDisabled(s_LevelFileName[0] == '\0');
	auto forkRevision = ImGui::Button("Fork Pinned Revision") ? &forgeLevelSessionForkPinnedRevision : nullptr;
	if (netGetMode() == 0) {
		ImGui::SameLine();
		if (ImGui::Button("Fork Previous Save")) forkRevision = &forgeLevelSessionForkPreviousRevision;
	}
	ImGui::EndDisabled();
	forge_revision_t parent = {};
	const forge_revision_parent_e parentState = forgeLevelSessionParentRevision(&parent, err, sizeof(err));
	if (parentState == FORGE_REVISION_PARENT_AVAILABLE) {
		ImGui::Text("Parent Revision  %s (%.12s)", parent.arena_id, parent.digest);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", parent.digest);
		ImGui::BeginDisabled(s_LevelFileName[0] == '\0');
		if (ImGui::Button("Fork Parent Revision")) forkRevision = &forgeLevelSessionForkParentRevision;
		ImGui::EndDisabled();
	} else if (parentState == FORGE_REVISION_PARENT_UNAVAILABLE) {
		ImGui::TextWrapped("Parent Revision unavailable. %s", err);
	}
	if (forkRevision) {
		mod_author_result_t *fork = (mod_author_result_t *)calloc(1, sizeof(*fork));
		if (!fork) {
			forgeLevelFileStatus(true, "Could not fork: out of memory.");
		} else {
			if (forkRevision(s_LevelFileName, fork, err, sizeof(err))) {
				forgeLevelFileStatus(false, "Forked %s. Choose it in %s to edit or play it.",
					fork->catalog_id, forgeDisplayName());
			} else {
				forgeLevelFileStatus(true, "Could not fork: %s", err);
			}
			free(fork);
		}
	}
	if (forgeLevelSessionCanSaveExtendedVariant() && ImGui::CollapsingHeader("Extend a Room")) {
		ImGui::PushID("offline-room-extension");
		static char variant[MOD_AUTHOR_NAME_MAX];
		static int room;
		static int metres = 3;
		ImGui::TextWrapped("Pin the saved original two-room arena before adding draft edits.");
		ImGui::InputText("Variant name", variant, sizeof(variant));
		const char *const rooms[] = {"Room 1", "Room 2"};
		ImGui::Combo("Room", &room, rooms, IM_ARRAYSIZE(rooms));
		ImGui::InputInt("Extra length (m)", &metres);
		constexpr int maxMetres = (ForgeRoomSize::MaxLength - ForgeRoomSize::MinLength) / 100;
		ImGui::BeginDisabled(!variant[0] || metres < 1 || metres > maxMetres);
		if (ImGui::Button("Save Extended Variant")) {
			mod_author_result_t *result = (mod_author_result_t *)calloc(1, sizeof(*result));
			if (!result) {
				forgeLevelFileStatus(true, "Could not save variant: out of memory.");
			} else {
				if (forgeLevelSessionSaveExtendedVariant(variant, room + 1, metres * 100, result, err, sizeof(err))) {
					forgeLevelFileStatus(false, "Saved %s. Restart the game, enable it in Mods, then open it from Levels.", variant);
					variant[0] = '\0';
				} else {
					forgeLevelFileStatus(true, "Could not save variant: %s", err);
				}
				free(result);
			}
		}
		ImGui::EndDisabled();
		ImGui::PopID();
	}
	if (ImGui::Button("Save Recovery Checkpoint")) {
		if (forgeLevelSessionWriteCheckpoint(err, sizeof(err))) {
			forgeLevelFileStatus(false, "Checkpoint saved to this profile.");
		} else {
			forgeLevelFileStatus(true, "Could not save checkpoint: %s", err);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Restore Recovery Checkpoint")) {
		if (forgeLevelSessionRestoreCheckpoint(err, sizeof(err))) {
			forgeLevelFileForget();
			forgeLevelFileStatus(false, "Checkpoint restored. Undo to return to the previous draft.");
		} else {
			forgeLevelFileStatus(true, "Could not restore checkpoint: %s", err);
		}
		/* A successful restore replaces the document. */
		return;
	}
	if (ImGui::Button(s_LevelFileCloseArmed ? "Discard Edits###LevelFileClose"
			: "Close Level File###LevelFileClose")) {
		if (forgeLevelSessionHasUnsavedChanges() && !s_LevelFileCloseArmed) {
			forgeLevelFileStatus(true, "Discard unsaved changes and close the level file?");
			s_LevelFileCloseArmed = true;
		} else {
			/* `doc` and `source` are gone after this: nothing below may use them. */
			forgeLevelSessionClose();
			forgeLevelFileForget();
			forgeLevelFileStatus(false, "Closed the level file.");
			forgeLevelFileDrawStatus();
			return;
		}
	}
	/* The HUD picks out what is selected here. */
	pdguiForgeLevelMarkersSelect(s_LevelFileStart, s_LevelFileObject);
	ImGui::TextWrapped(
		"Saving makes a level mod with this name from %s and your changes, and saving "
		"again under the same name updates it. A level made from one of the game's own "
		"levels uses your game files, so it stays on this machine. Changes that are not "
		"saved are lost when you leave the level.",
		source->arena_id);
}

/* The old Grid tools edit The Grid's own data, which a level does not hold
 * (forge-mode-scope.md: the placements come first; logic, zones and game
 * types become part of a level later). Each such tab says so at its top. */
static void forgeNotInLevelNote(const char *what)
{
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.45f, 1.0f), "Not saved in the level: %s", what);
	ImGui::PopTextWrapPos();
	ImGui::Separator();
}

static void forgeDrawActiveTab(int idx)
{
	if (forgeLevelSessionDoc() && forgeTabUsesLegacyMap(idx)) {
		ImGui::TextWrapped("Legacy maps are edited separately. Close the Level File from Save & Share to use these tools.");
		return;
	}

	switch (idx) {
	case FGT_FILE:
		/* The level itself: its starts and objects, drawn in the world while
		 * the camera flies, saved as a level mod. */
		forgeDrawLevelFileSection();
		break;
	case FGT_LEVEL:
		forgeNotInLevelNote("settings on this tab preview live and save in a legacy Grid map. "
			"Use Level File to save lighting, sky, fog, weather and music in the arena.");
		/* Level = Lighting + Mission + ambience (skybox / skylight / music).
		 * Sidebar anchors: 0 = Lighting sub-tab start, 1 = Mission sub-tab
		 * start. Anchors 2-4 (Skybox / Skylight / Background Music) live
		 * inside forgeDrawLevelExtras at their respective section headers. */
		forgeScrollAnchorMaybe(FGT_LEVEL, 0);
		forgeDrawLightingTab();
		ImGui::Separator();
		forgeScrollAnchorMaybe(FGT_LEVEL, 1);
		forgeDrawMissionTab();
		ImGui::Separator();
		forgeDrawLevelExtras();
		break;
	case FGT_OBJECTS:
		/* Objects = Catalog + Properties. The Objects sidebar's payload
		 * is the catalog category filter and is applied via
		 * forgeSidebarActivate, not a scroll-to anchor. */
		forgeNotInLevelNote("these objects are saved in legacy maps. "
			"Place a level's starts, weapons, crates, vehicles and props in Level File.");
		forgeDrawCatalogTab();
		ImGui::Separator();
		forgeDrawPropertiesTab();
		break;
	case FGT_CONTROL:
		/* Control = Logic + Zones. Anchors: 0 = Logic start, 1 = Wires,
		 * 2 = Zones start, 3 = Channels (inside Logic). */
		forgeNotInLevelNote("logic and zones run here and are kept only in an old Grid map. "
			"Use Level File > Level Logic for rules saved in a level.");
		forgeScrollAnchorMaybe(FGT_CONTROL, 0);
		forgeDrawLogicTab();
		ImGui::Separator();
		forgeScrollAnchorMaybe(FGT_CONTROL, 2);
		forgeDrawZonesTab();
		break;
	case FGT_GAMEPLAY:
		/* Playtest = Bots + Game Type. Anchor 0 = Game Type start,
		 * anchor 7 = Bots start. Other anchors (1-6) live inside
		 * forgeDrawGameTypeTab next to their section headers. */
		if (forgeLevelSessionDoc()) {
			static char match_sources[256];
			if (ImGui::Button("Review Saved Match Sources")) {
				grid_match_sources_t plan = {};
				char reason[192];
				if (forgeLevelSessionReviewMatchSources(&plan, reason, sizeof(reason))) {
					snprintf(match_sources, sizeof(match_sources),
						"Placements, stage rules and registered dependencies use %u assets in %u archives. ID %.12s.",
						(unsigned)plan.entry_count, (unsigned)plan.package_count, plan.digest);
				} else {
					snprintf(match_sources, sizeof(match_sources), "%s", reason);
				}
				gridMatchSourcesFree(&plan);
			}
			if (match_sources[0]) ImGui::TextWrapped("%s", match_sources);
		}
		forgeScrollAnchorMaybe(FGT_GAMEPLAY, 7);
		forgeDrawBotsTab();
		if (!forgeLevelSessionDoc()) {
			ImGui::Separator();
			forgeScrollAnchorMaybe(FGT_GAMEPLAY, 0);
			forgeDrawGameTypeTab();
		}
		break;
	case FGT_SETUP:
		/* Grid Map = an old Grid map's settings and save. Anchors live
		 * inside forgeDrawSettingsTab at the Map Metadata / Variant /
		 * Players section headers. */
		forgeNotInLevelNote("this is an old Grid map's settings and save (mods/Forge Maps), which only "
			"this editor reads and which loses data on every load. Save a level in Level File.");
		forgeDrawSettingsTab();
		break;
	default:
		break;
	}
}

/* Issue 8b (2026-04-24): sidebar + tab input handling.
 *
 * Runs unconditionally whenever the editor render fires (FREEFLY, forge
 * session active). Reads the six ACTION_FORGE_* actions from the
 * actionmap directly -- their bindings live in g_ImcForge which is
 * pushed in forgeTransitionToFreefly, so these reads only fire while
 * FREEFLY is active. Sidebar selection wraps via modulo so D-pad
 * navigation on a short list is seamless. */
static void forgeSidebarHandleInput(void)
{
	if (menupoolIsActive(MENU_TYPE_GRID_PLACEMENT)) return;
	if (menupoolIsActive(MENU_TYPE_GRID_LEVEL_TOOLS)) return;
	if (menupoolIsActive(MENU_TYPE_GRID_VARIANT_REVIEW)) return;

	/* Fix 8 (2026-05-01): the sidebar Toggle / Activate actions are
	 * context-sensitive when the user is holding an object (select-spawn-
	 * attach flow). Toggle on a held object opens its Properties context
	 * (Objects tab + selection set) instead of toggling editor visibility;
	 * Activate on a held object releases it (object stays where it last
	 * tracked the camera). Tab-cycle and D-pad navigation are unchanged. */
	const u32 held_uid = forgeHeldGetUid();

	/* Fix 2+3 (2026-05-01): the sidebar-toggle action now shows / hides
	 * the entire editor window when nothing is held. On hide, clear ImGui
	 * focus / active-id / nav-window so any focused widget releases the
	 * keyboard before the editor's Begin/End is skipped on the next frame.
	 * Without this clear the editor's child widget can keep
	 * WantCaptureKeyboard=true while the input-context stack thinks
	 * gameplay is on top, and B-195 fires on every subsequent keypress
	 * (verified in 2026-05-01 playtest log: sym=0x40000044 / 0x0065 /
	 * 0x0071 swallowed for ~2 minutes after a mid-session forge
	 * re-entry).
	 *
	 * Fix 8 (2026-05-01): when an object is held, Toggle instead opens
	 * the Properties context for that object. The user can then modify
	 * traits and pick the object back up via the catalog list -- or the
	 * release flow below -- without losing the in-flight placement. */
	if (actionPressed(0, ACTION_FORGE_SIDEBAR_TOGGLE)) {
		if (held_uid != 0) {
			/* Make sure the editor is visible so Properties is on screen. */
			if (!s_EditorVisible) {
				s_EditorVisible = true;
			}
			/* The Objects tab embeds the Properties pane (forgeDrawActiveTab
			 * dispatches FGT_OBJECTS to Catalog + Properties). Selection
			 * is what scopes Properties' fields. Use SelectOnly so the
			 * pane reflects exactly the held object regardless of any
			 * prior multi-select state. */
			s_forge_active_tab      = FGT_FILE;
			s_forge_tab_set_request = FGT_OBJECTS;
			forgeSelectionSelectOnly(held_uid);
		} else {
			bool was_visible = s_EditorVisible;
			s_EditorVisible = !s_EditorVisible;
			if (was_visible && !s_EditorVisible) {
				pdguiClearImGuiFocusAndNav();
			}
		}
	}

	/* Activate (A on pad / D-pad RIGHT / Right arrow) while holding a
	 * live object releases it -- object stays at its last camera-tracked
	 * pos. Catalog-list spawn-and-attach happens on the catalog Button
	 * click path in forgeDrawCatalogTab (not from this action), so this
	 * branch is dedicated to the release half of the held lifecycle. */
	if (held_uid != 0 && actionPressed(0, ACTION_FORGE_SIDEBAR_ACTIVATE)) {
		forgeHeldRelease();
		/* Skip the rest of sidebar handling this tick so the same press
		 * doesn't also fire the catalog-row activate. */
		return;
	}

	/* Tab-cycle and per-tab row navigation only run while the editor is
	 * on-screen. When the editor is hidden the only meaningful key is the
	 * toggle above; everything else is a no-op so a held button doesn't
	 * silently advance hidden state. */
	if (!s_EditorVisible) {
		return;
	}

	if (actionPressed(0, ACTION_FORGE_TAB_PREV)) {
		s_forge_active_tab = (s_forge_active_tab + FGT_COUNT - 1) % FGT_COUNT;
		s_forge_tab_set_request = s_forge_active_tab;
	}
	if (actionPressed(0, ACTION_FORGE_TAB_NEXT)) {
		s_forge_active_tab = (s_forge_active_tab + 1) % FGT_COUNT;
		s_forge_tab_set_request = s_forge_active_tab;
	}

	ForgeSidebarList lst = forgeSidebarForTab(s_forge_active_tab);
	if (lst.count > 0) {
		int &stored = s_SidebarSelection[s_forge_active_tab];
		int sel = stored - lst.first;
		if (sel < 0 || sel >= lst.count) sel = 0;
		const bool up = actionPressed(0, ACTION_FORGE_SIDEBAR_UP) != 0;
		const bool down = actionPressed(0, ACTION_FORGE_SIDEBAR_DOWN) != 0;
		if (up) {
			sel = (sel + lst.count - 1) % lst.count;
		}
		if (down) {
			sel = (sel + 1) % lst.count;
		}
		if (up || down) stored = sel + lst.first;
		if (actionPressed(0, ACTION_FORGE_SIDEBAR_ACTIVATE)) {
			forgeSidebarActivate(s_forge_active_tab, sel);
		}
	}
}

/* Draw the sidebar as a left-anchored BeginChild inside the editor
 * window. Uses a fixed width; the tab area fills the remaining space.
 *
 * Fix 2+3 (2026-05-01): the visibility gate moved up to the editor's
 * outer Begin in pdguiForgeEditorRender. This function only runs while
 * the editor is on-screen, so the sidebar is always shown alongside
 * the tab area. */
static void forgeSidebarDraw(float footerReserve, bool focusControls)
{
	ForgeSidebarList lst = forgeSidebarForTab(s_forge_active_tab);
	int sel = s_SidebarSelection[s_forge_active_tab] - lst.first;
	if (sel < 0 || sel >= lst.count) sel = 0;

	const float scale = pdguiScale(1.0f);
	const float sidebarW = 180.0f * scale;

	ImGui::BeginChild("##GridSidebar", ImVec2(sidebarW, -footerReserve),
			ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);

	ImGui::TextDisabled("%s", forgeTabLabel(s_forge_active_tab));
	ImGui::Separator();

	if (!lst.items || lst.count <= 0) {
		ImGui::TextDisabled("(no sections)");
	}
	for (int i = 0; i < lst.count; i++) {
		ImGui::PushID(i + lst.first);
		char lbl[128];
		snprintf(lbl, sizeof(lbl), "%s##sb", lst.items[i].label);
		if (focusControls && i == sel) ImGui::SetKeyboardFocusHere();
		if (ImGui::Selectable(lbl, i == sel)) {
			s_SidebarSelection[s_forge_active_tab] = i + lst.first;
			forgeSidebarActivate(s_forge_active_tab, i);
		}
		ImGui::PopID();
	}

	ImGui::EndChild();
	ImGui::SameLine();
}

/* Mouse look for the editor camera: hold the right mouse button over the
 * world and drag.
 *
 * The camera's own look input is the aim axis (forgemode.c), which is the
 * right stick or the aim keys; the mouse does not feed it (actionmap.cpp),
 * the game's own mouse deltas are zeroed while the editor's input context is
 * on top (input.c), and this editor binds the up, down and right arrow keys
 * to its sidebar. So with a keyboard and a mouse the camera could not be
 * turned. ImGui still sees the mouse, because the editor's window is used
 * with it, and its movement is taken from there. A drag that starts over a
 * window belongs to that window. The cursor stays visible: it is an editor. */
static void forgeEditorMouseLook(void)
{
	static bool s_looking = false;
	const float degPerPixel = 0.15f;
	ImGuiIO &io = ImGui::GetIO();

	if (inputCtxGetTop() != &g_CtxForgeEditor
			|| !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		s_looking = false;
		return;
	}
	if (!s_looking && io.WantCaptureMouse) {
		return;
	}
	s_looking = true;
	if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
		/* The picture's y grows downward; looking up is a positive pitch. */
		forgeAddCameraAngles(io.MouseDelta.x * degPerPixel, -io.MouseDelta.y * degPerPixel);
	}
}

static bool forgePropProject(const f32 world[3], float scaleX, float scaleY, ImVec2 &screen)
{
	float x, y;
	if (!forgeProjectWorldPos(world, &x, &y, nullptr)) return false;
	screen = ImVec2(x * scaleX, y * scaleY);
	return true;
}

static void forgeDrawAimedProp(const forge_level_prop_aim_t &aim, const ImVec2 &reticle,
		bool overWorld, s32 winW, s32 winH)
{
	const float scale = pdguiScale(1.0f);
	const ImU32 color = aim.valid ? IM_COL32(115, 245, 160, 240) : IM_COL32(255, 120, 110, 240);
	auto *draw = ImGui::GetForegroundDrawList();
	if (overWorld) draw->AddCircle(reticle, 9.0f * scale, color, 0, 2.0f * scale);

	const s32 nativeW = videoGetNativeWidth();
	const s32 nativeH = videoGetNativeHeight();
	if (aim.has_floor && nativeW > 0 && nativeH > 0) {
		const float half = FORGE_LEVEL_PROP_SNAP_UNITS * 0.5f;
		const auto *p = aim.position;
		const f32 corners[4][3] = {
			{p[0] - half, p[1], p[2] - half}, {p[0] + half, p[1], p[2] - half},
			{p[0] + half, p[1], p[2] + half}, {p[0] - half, p[1], p[2] + half},
		};
		ImVec2 points[4];
		bool projected = true;
		for (int i = 0; i < 4; i++) {
			if (!forgePropProject(corners[i], (float)winW / nativeW, (float)winH / nativeH, points[i])) {
				projected = false;
				break;
			}
		}
		if (projected) draw->AddPolyline(points, 4, color, ImDrawFlags_Closed, 2.0f * scale);
	}

	char confirm[32], cancel[32], editor[32];
	char controls[160];
	const bool haveConfirm = pdguiGlyphGetActionLabel(ACTION_FORGE_SIDEBAR_ACTIVATE, confirm, sizeof(confirm)) != 0;
	const bool haveCancel = pdguiGlyphGetActionLabel(ACTION_FORGE_PLACE_CANCEL, cancel, sizeof(cancel)) != 0;
	const bool haveEditor = pdguiGlyphGetActionLabel(ACTION_FORGE_EDITOR_CONTROLS, editor, sizeof(editor)) != 0;
	char gesture[64];
	if (s_PropPlacement.dragging) snprintf(gesture, sizeof(gesture), "Release to place");
	else if (haveConfirm) snprintf(gesture, sizeof(gesture), "Left click or %s to place", confirm);
	else snprintf(gesture, sizeof(gesture), "Left click to place");
	snprintf(controls, sizeof(controls), "%s%s%s%s%s", gesture,
		haveCancel || haveEditor ? "   Cancel: " : "",
		haveCancel ? cancel : "", haveCancel && haveEditor ? " / " : "", haveEditor ? editor : "");

	char status[CATALOG_ID_LEN + 32];
	snprintf(status, sizeof(status), "%s | %s", s_PropPlacement.name.c_str(),
		aim.valid ? "Ready to place" : !overWorld ? "Drag into the level" : "Aim at a placeable floor");
	const float wrap = ImMax(1.0f, winW - 40.0f * scale);
	const float statusHeight = ImGui::CalcTextSize(status, nullptr, false, wrap).y + ImGui::GetStyle().ItemSpacing.y;
	const float controlsHeight = ImGui::CalcTextSize(controls, nullptr, false, wrap).y;
	const ImVec2 position(20.0f * scale, ImMax(10.0f * scale, winH - statusHeight - controlsHeight - 20.0f * scale));
	draw->AddText(nullptr, 0.0f, position, color, status, nullptr, wrap);
	draw->AddText(nullptr, 0.0f, ImVec2(position.x, position.y + statusHeight),
		IM_COL32(240, 245, 255, 245), controls, nullptr, wrap);
}

static void forgeReturnFromPropAim()
{
	s_PropPaletteRestoreControls = s_PropPlacement.restoreControls;
	s_PropPaletteReturnRequested = true;
	s_forge_active_tab = FGT_FILE;
	s_forge_tab_set_request = FGT_FILE;
	s_SidebarScrollRequest[FGT_FILE] = 1; /* Objects and the prop palette. */
	s_PropPlacement = {};
}

static bool forgePropPlacementGestureHeld()
{
	return actionHeld(0, ACTION_FORGE_SIDEBAR_ACTIVATE)
		|| actionHeld(0, ACTION_FORGE_PLACE_CANCEL)
		|| actionHeld(0, ACTION_FORGE_EDITOR_CONTROLS)
		|| ImGui::IsMouseDown(ImGuiMouseButton_Left);
}

static void forgeAimedPropInput(s32 winW, s32 winH)
{
	if (inputCtxGetTop() != &g_CtxForgeEditor || ImGui::GetIO().AppFocusLost) {
		forgeLevelFileStatus(false, "Placement interrupted.");
		forgeReturnFromPropAim();
		return;
	}

	if (actionPressed(0, ACTION_FORGE_PLACE_CANCEL)
			|| actionPressed(0, ACTION_FORGE_EDITOR_CONTROLS)) {
		forgeLevelFileStatus(false, "Placement canceled.");
		forgeReturnFromPropAim();
		return;
	}

	const asset_entry_t *model = assetCatalogResolve(s_PropPlacement.modelId.c_str());
	if (!model || model->type != ASSET_MODEL || !model->enabled || model->temporary || model->grid_offer_only
			|| s_PropPlacement.modelId != model->id) {
		forgeLevelFileStatus(true, "The placement model is no longer available.");
		forgeReturnFromPropAim();
		return;
	}

	if (s_PropPlacement.generation != forgeLevelSessionGeneration()) {
		forgeLevelFileStatus(true, "The running level changed before the prop could be placed.");
		forgeReturnFromPropAim();
		return;
	}

	const bool dragging = s_PropPlacement.dragging;
	const ImVec2 reticle = dragging ? ImGui::GetMousePos() : ImVec2(winW * 0.5f, winH * 0.5f);
	const s32 nativeW = videoGetNativeWidth();
	const s32 nativeH = videoGetNativeHeight();
	const bool overWorld = !dragging
		|| (winW > 0 && winH > 0 && nativeW > 0 && nativeH > 0
			&& ImGui::IsMousePosValid(&reticle) && reticle.x >= 0 && reticle.y >= 0
			&& reticle.x < winW && reticle.y < winH
			&& !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
	const f32 pixel[2] = {
		overWorld && dragging ? reticle.x * nativeW / winW : 0.0f,
		overWorld && dragging ? reticle.y * nativeH / winH : 0.0f,
	};
	const f32 *screen = dragging ? pixel : nullptr;
	forge_level_prop_aim_t aim = {};
	if (overWorld && !forgeLevelSessionAimProp(screen, &aim)) {
		forgeLevelFileStatus(true, "The running level changed before the prop could be placed.");
		forgeReturnFromPropAim();
		return;
	}
	forgeDrawAimedProp(aim, reticle, overWorld, winW, winH);
	if (s_PropPlacement.awaitRelease) {
		if (!actionHeld(0, ACTION_FORGE_SIDEBAR_ACTIVATE) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_PropPlacement.awaitRelease = false;
		}
		return;
	}

	const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse;
	const bool drop = dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
	if (dragging && !drop && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		forgeLevelFileStatus(false, "Placement interrupted.");
		forgeReturnFromPropAim();
		return;
	}
	if (drop && !aim.valid) {
		forgeLevelFileStatus(true, "Drop the prop on a placeable floor.");
		forgeReturnFromPropAim();
		return;
	}
	if (drop || (!dragging && (actionPressed(0, ACTION_FORGE_SIDEBAR_ACTIVATE) || click))) {
		if (!aim.valid) return;
		char record[32], failure[256];
		if (forgeLevelSessionCommitAimedProp(s_PropPlacement.modelId.c_str(), s_PropPlacement.generation,
				screen, record, sizeof(record), failure, sizeof(failure))) {
			snprintf(s_LevelFileObject, sizeof(s_LevelFileObject), "%s", record);
			s_LevelFileObjectIndex = (int)forgeLevelDocObjectCount(forgeLevelSessionDoc()) - 1;
			s_LevelFilePosePreferObject = true;
			forgeLevelFileStatus(false, "Placed %s.", s_PropPlacement.name.c_str());
		} else {
			forgeLevelFileStatus(true, "Could not place %s: %s", s_PropPlacement.name.c_str(), failure);
		}
		forgeReturnFromPropAim();
	}
}

void pdguiForgeEditorRender(s32 winW, s32 winH)
{
	s_LevelFileOpenRequested = false;
	if (!forgeSessionIsActive() || !forgeIsFreefly()) {
		s_LevelFilePlayDraftRequested = false;
		s_PropPlacement = {};
		return;
	}

	forgeCoreInit();
	if (s_PropPlacement.active) {
		/* A pending placement owns the viewport until commit or cancel. */
		s_EditorPanelShown = false;
		forgeEditorMouseLook();
		forgeAimedPropInput(winW, winH);
		return;
	}
	if (s_PropPaletteRestoreControls) {
		/* The last confirm or cancel belonged to the viewport. Retake the
		 * previous menu lease only after the physical gesture releases. */
		if (forgePropPlacementGestureHeld()) return;
		if (inputCtxGetTop() != &g_CtxForgeEditor) return;
		s_PropPaletteRestoreControls = false;
		if (forgeEditorAcquireControls()) {
			pdguiNavSuppressOpeningGesture();
			ImGui::SetNavCursorVisible(true);
		}
	}

	const bool popupWasOpen = ImGui::IsPopupOpen(nullptr,
		ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	bool focusControls = false;
	if (!popupWasOpen && inputCtxGetTop() == &g_CtxForgeEditor) {
		if (pdguiMenuActionPressed(ACTION_FORGE_EDITOR_CONTROLS) && forgeEditorAcquireControls()) {
			pdguiNavSuppressOpeningGesture();
			s_EditorVisible = true;
			focusControls = true;
		} else {
			forgeSidebarHandleInput();
		}
	}

	/* Before the window: the look works with the editor hidden too. */
	forgeEditorMouseLook();

	s_EditorPanelShown = false;
	if (!s_EditorVisible) return;

	const bool controlsActive = forgeEditorControlsActive();
	if (controlsActive && !popupWasOpen) {
		const int direction = pdguiMenuTabNextPressed() - pdguiMenuTabPrevPressed();
		if (direction) {
			s_forge_active_tab = (s_forge_active_tab + direction + FGT_COUNT) % FGT_COUNT;
			s_forge_tab_set_request = s_forge_active_tab;
			focusControls = true;
		}
	}

	const float scale = pdguiScale(1.0f);
	ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 620.0f * scale), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2((float)winW - 640.0f * scale, 70.0f * scale), ImGuiCond_FirstUseEver);

	char editorTitle[FORGE_DISPLAY_NAME_CAP + sizeof(" -- Editor###The Grid -- Editor")];
	snprintf(editorTitle, sizeof(editorTitle), "%s -- Editor###The Grid -- Editor", forgeDisplayName());
	if (focusControls) {
		ImGui::SetNavCursorVisible(true);
		ImGui::SetNextWindowFocus();
	}
	ImGui::Begin(editorTitle, nullptr,
			ImGuiWindowFlags_NoCollapse);
	/* For the HUD, which draws before the panel: its labels stay out from
	 * under it (the panel is translucent). */
	s_EditorPanelMin = ImGui::GetWindowPos();
	s_EditorPanelMax = ImVec2(s_EditorPanelMin.x + ImGui::GetWindowWidth(),
		s_EditorPanelMin.y + ImGui::GetWindowHeight());
	s_EditorPanelShown = true;

	char controlsBinding[32];
	char controlsLabel[96];
	const InputAction modeAction = controlsActive ? ACTION_MENU_CANCEL : ACTION_FORGE_EDITOR_CONTROLS;
	const char *modeLabel = controlsActive ? "Return to camera" : "Use editor controls";
	if (pdguiGlyphGetActionLabel(modeAction, controlsBinding, sizeof(controlsBinding))) {
		snprintf(controlsLabel, sizeof(controlsLabel), "%s (%s)###GridEditorInputMode",
			modeLabel, controlsBinding);
	} else {
		snprintf(controlsLabel, sizeof(controlsLabel), "%s###GridEditorInputMode", modeLabel);
	}
	const bool switchMode = ImGui::Button(controlsLabel);
	if (switchMode && !controlsActive && forgeEditorAcquireControls()) {
		pdguiNavSuppressOpeningGesture();
	}

	if (forgeDrawLevelFileToolbar()) {
		ImGui::End();
		return;
	}

	if (!forgeLevelSessionDoc() && forgeTabUsesLegacyMap(s_forge_active_tab)) {
		if (ImGui::Button("Undo Legacy Map Edit")) forgeUndoApplyUndo();
		forgeLevelToolbarSameLine("Redo Legacy Map Edit");
		if (ImGui::Button("Redo Legacy Map Edit")) forgeUndoApplyRedo();
		ImGui::TextDisabled("Selected: %d   Legacy objects: %d / %d", forgeSelectionCount(),
			forgeObjectCount(), FORGE_MAX_OBJECTS);
	}

	ImGui::Separator();

	/* Issue 8b (2026-04-24): reserve space for the footer hint so the
	 * sidebar / tab-area BeginChild blocks below don't overlap it. */
	const float footerReserve = ImGui::GetTextLineHeightWithSpacing()
			+ ImGui::GetStyle().FramePadding.y * 2.0f
			+ ImGui::GetStyle().ItemSpacing.y + 4.0f;

	/* Issue 8b: sidebar + tab area sit side-by-side as sibling child
	 * regions. The sidebar takes a fixed width when visible; the tab
	 * area fills the rest. Both reserve footerReserve on the bottom
	 * so the footer stays pinned to the window frame. */
	forgeSidebarDraw(footerReserve, focusControls);

	ImGui::BeginChild("##GridTabArea", ImVec2(0, -footerReserve),
			ImGuiChildFlags_NavFlattened);

	/* S313 -- Use resizable tab-bar policy so labels fit on smaller
	 * editor windows without truncating; scroll if they overflow. */
	const ImGuiTabBarFlags tb_flags =
			ImGuiTabBarFlags_Reorderable |
			ImGuiTabBarFlags_FittingPolicyScroll;
	if (ImGui::BeginTabBar("GridTabs", tb_flags)) {
		for (int i = 0; i < FGT_COUNT; ++i) {
			ImGuiTabItemFlags flags = 0;
			if (s_forge_tab_set_request == i) {
				flags |= ImGuiTabItemFlags_SetSelected;
			}
			const char *label = i == FGT_GAMEPLAY ? "Playtest###Gameplay" : forgeTabLabel(i);
			if (ImGui::BeginTabItem(label, nullptr, flags)) {
				s_forge_active_tab = i;
				forgeDrawActiveTab(i);
				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}
	s_forge_tab_set_request = -1;

	ImGui::EndChild();

	/* Controller / keyboard hint footer so authors know which buttons
	 * drive the editor.  Always on so it's visible from any tab. */
	ImGui::Separator();
	if (controlsActive) {
		ImGui::TextDisabled("D-pad / arrows: controls   LB/RB / PgUp/PgDn: tabs");
	} else {
		ImGui::TextDisabled("Sticks: fly   LT/RT: height   D-pad: sidebar   X / Tab: hide");
		ImGui::TextDisabled("Hold right mouse over the world to look.");
	}

	const bool popupOpen = ImGui::IsPopupOpen(nullptr,
		ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	const ImGuiContext &gui = *ImGui::GetCurrentContext();
	const bool textActive = ImGui::GetInputTextState(gui.ActiveId) && gui.ActiveIdWindow &&
		ImGui::IsWindowChildOf(gui.ActiveIdWindow, ImGui::GetCurrentWindow(), true);
	if (s_PropPlacement.active) {
		/* The palette selection handed input back to the viewport. */
		ImGui::End();
		return;
	}
	/* A field or popup opened by mouse can still be operated by controller. */
	if (((popupOpen && !popupWasOpen) || textActive)
			&& !forgeEditorControlsActive() && forgeEditorAcquireControls()) {
		s_TransientEditorControls = true;
		pdguiNavSuppressOpeningGesture();
	}
	if (s_TransientEditorControls && !popupOpen && !textActive) {
		forgeEditorReleaseControls();
	} else if (controlsActive && !popupWasOpen && !popupOpen) {
		if (switchMode) {
			forgeEditorReleaseControls();
		} else if (pdguiMenuCancelPressed()) {
			const forge_placement_state_t *placement = forgeGetPlacement();
			if (placement && placement->ghost_active) forgePlaceCancel();
			else forgeEditorReleaseControls();
		}
	}

	ImGui::End();
}

void pdguiForgeEditorTick(void)
{
	/* S313 -- per-frame update of the ghost placement position from
	 * the freefly camera, so the HUD draws the ghost in the correct
	 * world-space spot (translated to screen-center crosshair).  The
	 * distance (400u) is the default placement reach; a later polish
	 * pass can add a scroll-wheel / D-pad adjuster bound to the
	 * editor's tool-mode.
	 *
	 * Fix 8 (2026-05-01): when an object is "held" (forgeHeldGetUid
	 * non-zero), its real pos[] is updated from the same camera-forward
	 * formula every tick. Holding and ghost-placing are mutually
	 * exclusive on the catalog flow, but kept independent here so a
	 * future tool-mode that re-engages ghost placement on top of a
	 * held object still works. */
	/* Forge F2: the boot flags that start a session on a named level and
	 * save from inside it. A no-op unless one was passed. Before the
	 * session checks, because the first of them is what starts a session. */
	(void)bootForgeLevelTick();
	if (!forgeIsFreefly()) forgeEditorReleaseControls();
	if (!forgeSessionIsActive() || !forgeIsFreefly() || netGetMode() != 0 || !forgeLevelSessionDoc()) {
		s_PropPlacement = {};
		s_PropPaletteReturnRequested = false;
		s_PropPaletteRestoreControls = false;
	}
	if (!forgeSessionIsActive() || !forgeIsFreefly() || netGetMode() != 0) {
		s_LevelFilePoseRequested = false;
		s_LevelFileSaveRequested = false;
		s_LevelFileToolRequested = 0;
		menupoolRelease(MENU_TYPE_GRID_PLACEMENT);
		menupoolRelease(MENU_TYPE_GRID_LEVEL_TOOLS);
		if (s_LevelVariantGeneration) forgeLevelClearVariantReview();
	} else if (!forgeLevelSessionDoc()) {
		menupoolRelease(MENU_TYPE_GRID_PLACEMENT);
		menupoolRelease(MENU_TYPE_GRID_LEVEL_TOOLS);
		if (s_LevelVariantGeneration) forgeLevelClearVariantReview();
	}

	/* Forge F3: while the camera flies, the world shows the open level file
	 * (moved and new objects drawn where the file puts them); otherwise the
	 * level as it was loaded. Every frame, so a stage change is noticed. */
	forgeLevelLiveTick(forgeSessionIsActive() && forgeIsFreefly() ? 1 : 0);

	if (!forgeSessionIsActive()) return;
	if (!forgeIsFreefly())       return;

	/* Reach into the forge freefly camera via its C-API (struct coord
	 * is forward-declared in forgemode.h to avoid including types.h
	 * in this C++ TU -- we shadow it here and pass a pointer). */
	struct forge_editor_coord { f32 x, y, z; } cpos = { 0.0f, 0.0f, 0.0f };
	forgeGetCameraPos((struct coord *)&cpos);
	f32 camera_pos[3] = { cpos.x, cpos.y, cpos.z };
	const f32 yaw_deg   = forgeGetCameraYawDeg();
	const f32 pitch_deg = forgeGetCameraPitchDeg();
	const f32 reach     = 400.0f;

	forge_placement_state_t *pp = forgeGetPlacement();
	if (pp && pp->ghost_active) {
		forgePlaceUpdate(camera_pos, yaw_deg, pitch_deg, reach);
	}

	if (forgeHeldGetUid() != 0) {
		forgeHeldUpdateFromCamera(camera_pos, yaw_deg, pitch_deg, reach);
	}
}
