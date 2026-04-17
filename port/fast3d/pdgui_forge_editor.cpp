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
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "imgui/imgui.h"
#include "pdgui_forge.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"

extern "C" {
#include "game/forgemode.h"
#include "forge/forge_core.h"
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
	ImGui::BeginChild("CatalogList", ImVec2(0, 0), true);

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
				if (!strstr(e->name, ed->search_filter) &&
				    !strstr(e->id,   ed->search_filter) &&
				    !(e->tags && strstr(e->tags, ed->search_filter))) {
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
				if (!strstr(e->name, ed->search_filter) &&
				    !strstr(e->id,   ed->search_filter) &&
				    !(e->tags && strstr(e->tags, ed->search_filter))) {
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
				/* Place at current freefly camera forward ~400 units (F1 ghost
				 * reticle will take over once editor-tick drives placement).
				 * NOTE: struct coord is forward-declared in forgemode.h to
				 * avoid pulling types.h into C++; we shadow it here as three
				 * contiguous f32 which is the same layout. */
				struct forge_editor_coord { f32 x, y, z; } p = { 0.0f, 0.0f, 0.0f };
				forgeGetCameraPos((struct coord *)&p);
				f32 cpos[3] = { p.x, p.y, p.z };
				forgePlaceBegin(e->id);
				forgePlaceUpdate(cpos, forgeGetCameraYawDeg(), forgeGetCameraPitchDeg(), 400.0f);
				s32 uid = forgePlaceCommit();
				forgePlaceCancel();
				if (uid > 0) {
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
	ImGui::Separator();

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
		ImGui::InputText("Weapon ID", p->weapon_id, FORGE_ID_LEN);
		ImGui::InputInt("Ammo (-1 default)", &p->ammo);
		bool dual = p->dual_wield; if (ImGui::Checkbox("Dual Wield", &dual)) p->dual_wield = dual;
		ImGui::SliderFloat("Respawn (sec)", &p->respawn_sec, 0.0f, 60.0f);
		int team_lock = p->team_lock;
		if (ImGui::SliderInt("Team Lock", &team_lock, 0, 8)) p->team_lock = (u8)team_lock;
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

	ImGui::SeparatorText("Scoring");
	ImGui::InputInt("Per Kill",      &gt->score_per_kill);
	ImGui::InputInt("Per Headshot",  &gt->score_per_headshot);
	ImGui::InputInt("Per Objective", &gt->score_per_objective);
	ImGui::InputInt("Per Survive Sec",&gt->score_per_survive_sec);

	ImGui::SeparatorText("Player Setup");
	ImGui::InputText("Starting Weapon", gt->starting_weapon, FORGE_ID_LEN);
	ImGui::SliderFloat("Health x", &gt->health_mult, 0.25f, 8.0f);
	const char *roles[] = { "None","Infection","VIP","Juggernaut","Horde Defender","Gun-Game Hunter" };
	int rm = gt->role_mode;
	if (ImGui::Combo("Role Mode", &rm, roles, 6)) gt->role_mode = (u8)rm;

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

	ImGui::SeparatorText("HUD");
	bool hw = gt->show_wave_counter; if (ImGui::Checkbox("Wave Counter",  &hw)) gt->show_wave_counter = hw;
	ImGui::SameLine();
	bool hb = gt->show_boss_bar;     if (ImGui::Checkbox("Boss Bar",       &hb)) gt->show_boss_bar = hb;
	bool hr = gt->show_role_indicator;if (ImGui::Checkbox("Role Indicator",&hr)) gt->show_role_indicator = hr;
	ImGui::SameLine();
	bool hs = gt->show_survival_timer;if (ImGui::Checkbox("Survival Timer",&hs)) gt->show_survival_timer = hs;

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

static void forgeDrawSettingsTab(void)
{
	forge_map_settings_t *s = forgeMapSettings();
	forge_editor_state_t *ed = forgeGetEditor();
	forge_budget_stats_t b; forgeBudgetCompute(&b);

	ImGui::SeparatorText("Map Metadata");
	ImGui::InputText("Map Name",    s->map_name, FORGE_NAME_LEN);
	ImGui::InputText("Author",      s->author,   FORGE_NAME_LEN);
	ImGui::InputTextMultiline("Description", s->description, FORGE_DESC_LEN, ImVec2(-1, 60));
	ImGui::InputText("Base Stage ID", s->base_stage_id, FORGE_ID_LEN);

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
	if (ImGui::Checkbox("Pause Simulation in Forge", &ps)) ed->paused_sim = ps ? 1 : 0;
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
	static char save_slug[FORGE_NAME_LEN] = "";
	if (save_slug[0] == '\0') f_slug_from_name(s->map_name, save_slug, sizeof(save_slug));
	ImGui::InputText("Mod Slug", save_slug, FORGE_NAME_LEN);
	if (ImGui::Button("Save As Mod")) {
		if (save_slug[0]) forgeSerializeSaveToMod(save_slug);
	}
	ImGui::SameLine();
	if (ImGui::Button("Quick Re-Derive Slug")) {
		f_slug_from_name(s->map_name, save_slug, sizeof(save_slug));
	}
	ImGui::SameLine();
	if (ImGui::Button("Load From Mod")) {
		if (save_slug[0]) forgeSerializeLoadFromMod(save_slug);
	}
	ImGui::TextDisabled("Saves to mods/Forge Maps/<slug>/ with mod.json + map.json.");

	ImGui::SeparatorText("Reset");
	if (ImGui::Button("Reset Map (destructive)")) {
		forgeCoreReset();
	}
}

/* ============================================================
 * Main entry
 * ============================================================ */

void pdguiForgeEditorRender(s32 winW, s32 winH)
{
	if (!forgeSessionIsActive()) return;
	if (!forgeIsFreefly())       return;     /* editor hides in NORMAL play */

	forgeCoreInit();

	const float scale = pdguiScale(1.0f);
	ImGui::SetNextWindowSize(ImVec2(560.0f * scale, 560.0f * scale), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2((float)winW - 580.0f * scale, 70.0f * scale), ImGuiCond_FirstUseEver);

	ImGui::Begin("The Grid -- Editor", nullptr,
			ImGuiWindowFlags_NoCollapse);

	/* Undo / redo bar always visible. */
	if (ImGui::Button("Undo")) forgeUndoApplyUndo();
	ImGui::SameLine();
	if (ImGui::Button("Redo")) forgeUndoApplyRedo();
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	s32 sel = forgeSelectionCount();
	ImGui::Text("Selected: %d", sel);
	ImGui::SameLine();
	ImGui::TextDisabled(" | Objects: %d / %d", forgeObjectCount(), FORGE_MAX_OBJECTS);

	ImGui::Separator();

	if (ImGui::BeginTabBar("GridTabs")) {
		if (ImGui::BeginTabItem("Catalog"))    { forgeDrawCatalogTab();    ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Properties")) { forgeDrawPropertiesTab(); ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Zones"))      { forgeDrawZonesTab();      ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Lighting"))   { forgeDrawLightingTab();   ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Logic"))      { forgeDrawLogicTab();      ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Game Type"))  { forgeDrawGameTypeTab();   ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Mission"))    { forgeDrawMissionTab();    ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Settings"))   { forgeDrawSettingsTab();   ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}

	ImGui::End();
}

void pdguiForgeEditorTick(void)
{
	/* Reserved -- would drive per-frame placement reticle + editor hotkeys.
	 * F0-F8 data-model and UI are callable now; hotkey integration comes in
	 * a follow-up polish pass once the freefly camera exposes a raycast. */
}
