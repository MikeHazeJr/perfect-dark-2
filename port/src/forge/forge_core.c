/**
 * forge_core.c -- The Grid editor data model + runtime pools.
 *
 * Centralised storage + lifecycle for all editor state (F1-F8). Everything
 * besides the F0 mode toggle lives here. Each sub-system (undo, logic exec,
 * serialize, etc.) gets its own .c but reaches into this module for state.
 *
 * Pools are fixed-size and heap-resident via module statics -- simple, no
 * alloc churn, easy to diff against the design doc's budget caps.
 */

#include "forge/forge_core.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "system.h"

#ifndef countof
#define countof(a) ((s32)(sizeof(a) / sizeof((a)[0])))
#endif

#ifndef FORGE_UNUSED
#define FORGE_UNUSED(x) ((void)(x))
#endif

/* ============================================================
 * Storage
 * ============================================================ */

static forge_object_t       s_objects[FORGE_MAX_OBJECTS];
static forge_logic_node_t   s_logic_nodes[FORGE_MAX_LOGIC_NODES];
static forge_logic_wire_t   s_logic_wires[FORGE_MAX_LOGIC_WIRES];
static forge_channel_t      s_channels[FORGE_MAX_CHANNELS];
static forge_objective_t    s_objectives[FORGE_MAX_OBJECTIVES];
static forge_prefab_t       s_prefabs[FORGE_MAX_PREFABS];
static u32                  s_selection[FORGE_MAX_SELECTED];
static s32                  s_selection_count;

static forge_gametype_t     s_gametype;
static forge_boss_state_t   s_boss_state;
static forge_skylight_t     s_skylight;
static forge_atmosphere_t   s_atmosphere;
static forge_map_settings_t s_settings;
static forge_editor_state_t s_editor;
static forge_placement_state_t s_placement;
static forge_bot_settings_t s_bot_settings;

static u32 s_next_uid = 1;
static s32 s_initialized = 0;

/* ============================================================
 * Static catalog (F1)
 *
 * The full taxonomy from design §3.1 collapsed into a flat registry.
 * Subcategory strings are literal pointers into .rodata.
 * ============================================================ */

static const forge_catalog_entry_t s_catalog[] = {
	/* ---- Structures / Geometry ---- */
	{ "base:floor_1x1", "Floor 1x1", FORGE_CAT_GEOMETRY, "Floors", "floor,1x1,small", 2, 0, 0 },
	{ "base:floor_2x2", "Floor 2x2", FORGE_CAT_GEOMETRY, "Floors", "floor,2x2", 2, 0, 0 },
	{ "base:floor_4x4", "Floor 4x4", FORGE_CAT_GEOMETRY, "Floors", "floor,4x4,large", 2, 0, 0 },
	{ "base:floor_8x8", "Floor 8x8", FORGE_CAT_GEOMETRY, "Floors", "floor,8x8,huge", 2, 0, 0 },
	{ "base:platform_thin", "Thin Platform", FORGE_CAT_GEOMETRY, "Platforms", "platform,thin", 4, 0, 0 },
	{ "base:platform_thick", "Thick Platform", FORGE_CAT_GEOMETRY, "Platforms", "platform,thick", 6, 0, 0 },
	{ "base:platform_glass", "Glass Platform", FORGE_CAT_GEOMETRY, "Platforms", "platform,glass,transparent", 4, 0, 0 },
	{ "base:ramp_15", "Ramp 15 deg",  FORGE_CAT_GEOMETRY, "Ramps", "ramp,15", 6, 0, 0 },
	{ "base:ramp_30", "Ramp 30 deg",  FORGE_CAT_GEOMETRY, "Ramps", "ramp,30", 6, 0, 0 },
	{ "base:ramp_45", "Ramp 45 deg",  FORGE_CAT_GEOMETRY, "Ramps", "ramp,45", 6, 0, 0 },
	{ "base:stairs_straight", "Stairs (Straight)", FORGE_CAT_GEOMETRY, "Stairs", "stairs,straight", 12, 0, 0 },
	{ "base:stairs_spiral",   "Stairs (Spiral)",   FORGE_CAT_GEOMETRY, "Stairs", "stairs,spiral", 24, 0, 0 },
	{ "base:stairs_lshape",   "Stairs (L)",        FORGE_CAT_GEOMETRY, "Stairs", "stairs,L", 18, 0, 0 },
	{ "base:bridge_narrow",   "Narrow Bridge",     FORGE_CAT_GEOMETRY, "Bridges", "bridge,narrow", 8, 0, 0 },
	{ "base:bridge_wide",     "Wide Bridge",       FORGE_CAT_GEOMETRY, "Bridges", "bridge,wide", 10, 0, 0 },
	{ "base:bridge_susp",     "Suspension Bridge", FORGE_CAT_GEOMETRY, "Bridges", "bridge,suspension", 32, 0, 0 },
	{ "base:wall_1x1",     "Wall 1x1",     FORGE_CAT_GEOMETRY, "Walls", "wall,1x1", 2, 0, 0 },
	{ "base:wall_2x1",     "Wall 2x1",     FORGE_CAT_GEOMETRY, "Walls", "wall,2x1", 2, 0, 0 },
	{ "base:wall_4x2",     "Wall 4x2",     FORGE_CAT_GEOMETRY, "Walls", "wall,4x2", 2, 0, 0 },
	{ "base:wall_window",  "Wall w/ Window", FORGE_CAT_GEOMETRY, "Walls", "wall,window", 8, 0, 0 },
	{ "base:wall_doorframe","Wall w/ Door Frame", FORGE_CAT_GEOMETRY, "Walls", "wall,doorframe", 8, 0, 0 },
	{ "base:wall_half",    "Half Wall",    FORGE_CAT_GEOMETRY, "Walls", "wall,half,cover", 4, 0, 0 },
	{ "base:wall_corner_90","Corner (90)", FORGE_CAT_GEOMETRY, "Walls", "wall,corner,90", 4, 0, 0 },
	{ "base:wall_corner_45","Corner (45)", FORGE_CAT_GEOMETRY, "Walls", "wall,corner,45", 4, 0, 0 },
	{ "base:column_round", "Round Column", FORGE_CAT_GEOMETRY, "Columns", "column,round", 8, 0, 0 },
	{ "base:column_square","Square Column",FORGE_CAT_GEOMETRY, "Columns", "column,square", 4, 0, 0 },
	{ "base:ibeam",        "I-Beam",       FORGE_CAT_GEOMETRY, "Columns", "ibeam,beam", 8, 0, 0 },
	{ "base:strut",        "Support Strut",FORGE_CAT_GEOMETRY, "Columns", "strut,beam", 8, 0, 0 },
	{ "base:ceiling_tile", "Ceiling Tile", FORGE_CAT_GEOMETRY, "Ceilings", "ceiling,tile", 2, 0, 0 },
	{ "base:skylight_panel","Skylight",    FORGE_CAT_GEOMETRY, "Ceilings", "ceiling,skylight", 2, 0, 0 },
	{ "base:roof_angled",  "Angled Roof",  FORGE_CAT_GEOMETRY, "Ceilings", "ceiling,roof,angled", 6, 0, 0 },

	/* ---- Props ---- */
	{ "base:prop_desk",     "Desk",         FORGE_CAT_PROP, "Furniture", "desk,office", 48, 0, 0 },
	{ "base:prop_table",    "Table",        FORGE_CAT_PROP, "Furniture", "table", 40, 0, 0 },
	{ "base:prop_chair",    "Chair",        FORGE_CAT_PROP, "Furniture", "chair,seat", 32, 0, 0 },
	{ "base:prop_sofa",     "Sofa",         FORGE_CAT_PROP, "Furniture", "sofa,couch,seat", 48, 0, 0 },
	{ "base:prop_filing_cabinet","Filing Cabinet", FORGE_CAT_PROP, "Furniture", "cabinet,office", 32, 0, 0 },
	{ "base:prop_locker",   "Locker",       FORGE_CAT_PROP, "Furniture", "locker,storage", 32, 0, 0 },
	{ "base:prop_terminal", "Terminal",     FORGE_CAT_PROP, "Furniture", "terminal,computer", 64, 0, 0 },
	{ "base:prop_monitor",  "Monitor",      FORGE_CAT_PROP, "Furniture", "monitor,screen", 32, 0, 0 },
	{ "base:prop_crate_small","Small Crate",FORGE_CAT_PROP, "Industrial", "crate,small", 16, 0, 0 },
	{ "base:prop_crate_med",  "Medium Crate",FORGE_CAT_PROP,"Industrial", "crate,medium", 16, 0, 0 },
	{ "base:prop_crate_large","Large Crate",FORGE_CAT_PROP, "Industrial", "crate,large", 16, 0, 0 },
	{ "base:prop_barrel",   "Barrel",       FORGE_CAT_PROP, "Industrial", "barrel", 16, 0, 0 },
	{ "base:prop_barrel_exp","Explosive Barrel",FORGE_CAT_PROP,"Industrial","barrel,explosive,destructible", 16, 0, 0 },
	{ "base:prop_pipe",     "Pipe Section", FORGE_CAT_PROP, "Industrial", "pipe", 12, 0, 0 },
	{ "base:prop_vent",     "Vent",         FORGE_CAT_PROP, "Industrial", "vent", 8, 0, 0 },
	{ "base:prop_generator","Generator",    FORGE_CAT_PROP, "Industrial", "generator,power,destructible", 48, 0, 0 },
	{ "base:prop_control_panel","Control Panel",FORGE_CAT_PROP,"Industrial","panel,control", 32, 0, 0 },
	{ "base:prop_rock_small","Small Rock",  FORGE_CAT_PROP, "Natural", "rock,natural,small", 16, 0, 0 },
	{ "base:prop_rock_med", "Medium Rock",  FORGE_CAT_PROP, "Natural", "rock,natural", 24, 0, 0 },
	{ "base:prop_rock_large","Large Rock",  FORGE_CAT_PROP, "Natural", "rock,natural,large,cover", 32, 0, 0 },
	{ "base:prop_tree",     "Tree",         FORGE_CAT_PROP, "Natural", "tree,foliage", 48, 0, 0 },
	{ "base:prop_bush",     "Bush",         FORGE_CAT_PROP, "Natural", "bush,foliage", 16, 0, 0 },
	{ "base:prop_grass",    "Grass Patch",  FORGE_CAT_PROP, "Natural", "grass,foliage", 8, 0, 0 },
	{ "base:prop_water_plane","Water Plane",FORGE_CAT_PROP, "Natural", "water,surface", 4, 0, 0 },
	{ "base:prop_poster",   "Poster",       FORGE_CAT_PROP, "Decorative", "poster,sign,decor", 2, 0, 0 },
	{ "base:prop_sign",     "Sign",         FORGE_CAT_PROP, "Decorative", "sign,decor", 4, 0, 0 },
	{ "base:prop_banner",   "Banner",       FORGE_CAT_PROP, "Decorative", "banner,decor", 4, 0, 0 },
	{ "base:prop_light_fix_ceiling","Ceiling Light Fixture",FORGE_CAT_PROP,"Decorative","fixture,ceiling", 8, 0, 0 },
	{ "base:prop_light_fix_wall","Wall Light Fixture",FORGE_CAT_PROP,"Decorative","fixture,wall", 8, 0, 0 },
	{ "base:prop_railing",  "Railing Section",FORGE_CAT_PROP,"Decorative","railing,decor", 8, 0, 0 },
	{ "base:prop_caution_tape","Caution Tape",FORGE_CAT_PROP,"Decorative","tape,decor", 4, 0, 0 },
	{ "base:prop_barrier",  "Barrier",      FORGE_CAT_PROP, "Decorative", "barrier,cover", 12, 0, 0 },

	/* ---- Weapons & Pickups ---- */
	{ "base:pad_pistol",    "Pistol Pad",   FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,pistol", 4, 0, 0 },
	{ "base:pad_magsec",    "MagSec Pad",   FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,magsec", 4, 0, 0 },
	{ "base:pad_cmp150",    "CMP150 Pad",   FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,cmp150", 4, 0, 0 },
	{ "base:pad_ar34",      "AR34 Pad",     FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,ar34,assault", 4, 0, 0 },
	{ "base:pad_sniper",    "Sniper Pad",   FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,sniper", 4, 0, 0 },
	{ "base:pad_shotgun",   "Shotgun Pad",  FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,shotgun", 4, 0, 0 },
	{ "base:pad_rocket",    "Rocket Pad",   FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,rocket,explosive", 4, 0, 0 },
	{ "base:pad_farsight",  "FarSight Pad", FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,farsight,alien", 4, 0, 0 },
	{ "base:pad_slayer",    "Slayer Pad",   FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,slayer,rocket", 4, 0, 0 },
	{ "base:pad_reaper",    "Reaper Pad",   FORGE_CAT_WEAPON_PAD, "Weapons", "weapon,pad,reaper,heavy", 4, 0, 0 },
	{ "base:pad_devastator","Devastator Pad",FORGE_CAT_WEAPON_PAD,"Weapons","weapon,pad,devastator", 4, 0, 0 },
	{ "base:pickup_ammo",   "Ammo Crate",   FORGE_CAT_PICKUP, "Ammo", "ammo,pickup", 8, 0, 0 },
	{ "base:pickup_shield", "Shield",       FORGE_CAT_PICKUP, "Equipment", "shield,equipment,defense", 8, 0, 0 },
	{ "base:pickup_cloak",  "Cloaking Device", FORGE_CAT_PICKUP, "Equipment", "cloak,equipment,stealth", 8, 0, 0 },
	{ "base:pickup_nvg",    "Night Vision", FORGE_CAT_PICKUP, "Equipment", "nightvision,equipment", 8, 0, 0 },
	{ "base:pickup_boost",  "Combat Boost", FORGE_CAT_PICKUP, "Equipment", "boost,equipment,buff", 8, 0, 0 },

	/* ---- Spawn points ---- */
	{ "base:spawn_player",  "Player Spawn", FORGE_CAT_SPAWN_POINT, "Spawn Points", "spawn,generic", 1, 0, 0 },
	{ "base:spawn_team",    "Team Spawn Zone", FORGE_CAT_SPAWN_POINT, "Spawn Points", "spawn,team,zone", 1, 0, 0 },
	{ "base:spawn_initial", "Initial Spawn",FORGE_CAT_SPAWN_POINT, "Spawn Points", "spawn,initial", 1, 0, FORGE_SPAWN_INITIAL },
	{ "base:spawn_respawn", "Respawn Point",FORGE_CAT_SPAWN_POINT, "Spawn Points", "spawn,respawn", 1, 0, FORGE_SPAWN_RESPAWN },
	{ "base:spawn_bot_patrol","Bot Patrol Point",FORGE_CAT_SPAWN_POINT,"Spawn Points","patrol,bot,waypoint", 1, 0, 0 },

	/* ---- Characters & AI ---- */
	{ "base:ai_guard_datadyne","dataDyne Guard",FORGE_CAT_AI,"Characters","ai,guard,datadyne", 64, 0, 0 },
	{ "base:ai_guard_carrington","Carrington Guard",FORGE_CAT_AI,"Characters","ai,guard,carrington", 64, 0, 0 },
	{ "base:ai_guard_skedar","Skedar Warrior",FORGE_CAT_AI,"Characters","ai,guard,skedar,alien", 80, 0, 0 },
	{ "base:ai_guard_maian","Maian",         FORGE_CAT_AI,"Characters","ai,guard,maian,alien", 64, 0, 0 },
	{ "base:ai_civilian",  "Civilian",       FORGE_CAT_AI,"Characters","ai,civilian", 48, 0, 0 },
	{ "base:ai_boss",      "Boss Character", FORGE_CAT_AI,"Characters","ai,boss,encounter", 128, 0, 0 },

	/* ---- Interactables ---- */
	{ "base:door_slide_single","Sliding Door (Single)",FORGE_CAT_INTERACTABLE,"Doors","door,slide,single", 16, 0, FORGE_DOOR_SLIDE_LEFT },
	{ "base:door_slide_double","Sliding Door (Double)",FORGE_CAT_INTERACTABLE,"Doors","door,slide,double", 32, 0, FORGE_DOOR_SLIDE_LEFT },
	{ "base:door_blast",      "Blast Door",             FORGE_CAT_INTERACTABLE,"Doors","door,blast,heavy", 48, 0, FORGE_DOOR_SLIDE_UP },
	{ "base:door_keycard",    "Keycard Door",           FORGE_CAT_INTERACTABLE,"Doors","door,keycard,locked", 16, 0, FORGE_DOOR_SLIDE_LEFT },
	{ "base:door_destruct",   "Destructible Door",      FORGE_CAT_INTERACTABLE,"Doors","door,destructible", 16, 0, FORGE_DOOR_SWING },
	{ "base:elevator_2stop",  "Elevator (2-Stop)",      FORGE_CAT_INTERACTABLE,"Elevators","elevator,2stop", 48, 0, 0 },
	{ "base:elevator_multi",  "Elevator (Multi-Stop)",  FORGE_CAT_INTERACTABLE,"Elevators","elevator,multi", 48, 0, 0 },
	{ "base:elevator_grav",   "Gravity Lift",           FORGE_CAT_INTERACTABLE,"Elevators","lift,gravity,oneway", 16, 0, 0 },
	{ "base:elevator_launch", "Launch Pad",             FORGE_CAT_INTERACTABLE,"Elevators","pad,launch,ballistic", 16, 0, 0 },
	{ "base:switch_wall",     "Wall Switch",            FORGE_CAT_INTERACTABLE,"Switches","switch,wall", 4, 0, FORGE_SWITCH_INTERACT },
	{ "base:switch_floor",    "Pressure Plate",         FORGE_CAT_INTERACTABLE,"Switches","switch,pressure,floor", 4, 0, FORGE_SWITCH_PROXIMITY },
	{ "base:switch_terminal", "Hackable Terminal",      FORGE_CAT_INTERACTABLE,"Switches","switch,terminal,hackable", 16, 0, FORGE_SWITCH_INTERACT },
	{ "base:switch_timed",    "Timed Button",           FORGE_CAT_INTERACTABLE,"Switches","switch,timed", 4, 0, FORGE_SWITCH_INTERACT },
	{ "base:terminal_hackable","Hackable Terminal (Objective)",FORGE_CAT_INTERACTABLE,"Terminals","terminal,hackable,objective", 16, 0, 0 },
	{ "base:terminal_camera", "Camera System",          FORGE_CAT_INTERACTABLE,"Terminals","terminal,camera,security", 16, 0, 0 },
	{ "base:terminal_alarm",  "Alarm Panel",            FORGE_CAT_INTERACTABLE,"Terminals","terminal,alarm", 16, 0, 0 },
	{ "base:display_screen",  "Display Screen",         FORGE_CAT_INTERACTABLE,"Terminals","display,screen,decor", 8, 0, 0 },
	{ "base:destruct_glass",  "Breakable Glass",        FORGE_CAT_INTERACTABLE,"Destructibles","glass,destructible", 4, 0, 0 },
	{ "base:destruct_container","Explosive Container",  FORGE_CAT_INTERACTABLE,"Destructibles","container,explosive,destructible", 16, 0, 0 },
	{ "base:destruct_support", "Structural Support",    FORGE_CAT_INTERACTABLE,"Destructibles","support,destructible", 24, 0, 0 },

	/* ---- Zones & Volumes ---- */
	{ "base:zone_trigger",   "Trigger Zone",    FORGE_CAT_ZONE, "Zones", "zone,trigger", 0, 0, FORGE_ZONE_TRIGGER },
	{ "base:zone_radiation", "Radiation Zone",  FORGE_CAT_ZONE, "Zones", "zone,radiation,damage", 0, 0, FORGE_ZONE_RADIATION },
	{ "base:zone_gravity_low","Low Gravity Zone",FORGE_CAT_ZONE,"Zones", "zone,gravity,low", 0, 0, FORGE_ZONE_GRAVITY },
	{ "base:zone_gravity_zero","Zero-G Zone",   FORGE_CAT_ZONE, "Zones", "zone,gravity,zero", 0, 0, FORGE_ZONE_GRAVITY },
	{ "base:zone_gravity_high","High Gravity Zone",FORGE_CAT_ZONE,"Zones","zone,gravity,high", 0, 0, FORGE_ZONE_GRAVITY },
	{ "base:zone_teleporter","Teleporter",     FORGE_CAT_ZONE, "Zones", "zone,teleporter", 0, 0, FORGE_ZONE_TELEPORTER },
	{ "base:zone_kill",      "Kill Zone",      FORGE_CAT_ZONE, "Zones", "zone,kill,oob", 0, 0, FORGE_ZONE_KILL },
	{ "base:zone_water",     "Water Volume",   FORGE_CAT_ZONE, "Zones", "zone,water,swim", 0, 0, FORGE_ZONE_WATER },
	{ "base:zone_sound",     "Sound Zone",     FORGE_CAT_ZONE, "Zones", "zone,sound,ambient", 0, 0, FORGE_ZONE_SOUND },
	{ "base:zone_fog",       "Fog Volume",     FORGE_CAT_ZONE, "Zones", "zone,fog,atmosphere", 0, 0, FORGE_ZONE_FOG },
	{ "base:zone_no_weapon", "No-Weapon Zone", FORGE_CAT_ZONE, "Zones", "zone,noweapon,holster", 0, 0, FORGE_ZONE_NO_WEAPON },
	{ "base:zone_soft_bound","Soft Bound Zone",FORGE_CAT_ZONE, "Zones", "zone,bounds,softkill", 0, 0, FORGE_ZONE_SOFT_BOUND },
	{ "base:zone_hill",      "Hill Zone (KotH)",FORGE_CAT_ZONE,"Zones", "zone,hill,koth,gamemode", 0, 0, FORGE_ZONE_HILL },
	{ "base:zone_territory", "Territory",      FORGE_CAT_ZONE, "Zones", "zone,territory,gamemode", 0, 0, FORGE_ZONE_TERRITORY },

	/* ---- Lighting ---- */
	{ "base:light_point",    "Point Light",    FORGE_CAT_LIGHT, "Lighting", "light,point", 0, 0, FORGE_LIGHT_POINT },
	{ "base:light_spot",     "Spotlight",      FORGE_CAT_LIGHT, "Lighting", "light,spot,directional", 0, 0, FORGE_LIGHT_SPOT },
	{ "base:light_area",     "Area Light",     FORGE_CAT_LIGHT, "Lighting", "light,area,fill", 0, 0, FORGE_LIGHT_AREA },
	{ "base:light_emissive", "Emissive Surface",FORGE_CAT_LIGHT, "Lighting", "light,emissive,glow", 0, 0, FORGE_LIGHT_EMISSIVE },

	/* ---- Effects ---- */
	{ "base:fx_fire",        "Fire Emitter",     FORGE_CAT_EFFECT, "Effects", "fx,fire,particle", 0, 0, 0 },
	{ "base:fx_smoke",       "Smoke Emitter",    FORGE_CAT_EFFECT, "Effects", "fx,smoke,particle", 0, 0, 0 },
	{ "base:fx_sparks",      "Spark Emitter",    FORGE_CAT_EFFECT, "Effects", "fx,sparks,particle", 0, 0, 0 },
	{ "base:fx_steam",       "Steam Emitter",    FORGE_CAT_EFFECT, "Effects", "fx,steam,particle", 0, 0, 0 },
	{ "base:fx_rain",        "Rain Emitter",     FORGE_CAT_EFFECT, "Effects", "fx,rain,weather,particle", 0, 0, 0 },
	{ "base:fx_snow",        "Snow Emitter",     FORGE_CAT_EFFECT, "Effects", "fx,snow,weather,particle", 0, 0, 0 },
	{ "base:fx_sound_amb",   "Ambient Sound",    FORGE_CAT_EFFECT, "Effects", "fx,sound,ambient", 0, 0, 1 },
	{ "base:fx_decal_blood", "Blood Decal",      FORGE_CAT_EFFECT, "Effects", "fx,decal,blood", 0, 0, 2 },
	{ "base:fx_decal_scorch","Scorch Decal",     FORGE_CAT_EFFECT, "Effects", "fx,decal,scorch", 0, 0, 2 },
	{ "base:fx_decal_graffiti","Graffiti Decal", FORGE_CAT_EFFECT, "Effects", "fx,decal,graffiti", 0, 0, 2 },
	{ "base:fx_decal_arrow", "Arrow Marking",    FORGE_CAT_EFFECT, "Effects", "fx,decal,arrow", 0, 0, 2 },
	{ "base:fx_shake_zone",  "Screen Shake Zone",FORGE_CAT_EFFECT, "Effects", "fx,shake", 0, 0, 3 },
	{ "base:fx_postprocess", "Post-Process Vol", FORGE_CAT_EFFECT, "Effects", "fx,post,colorgrade", 0, 0, 4 },
};

#define FORGE_CATALOG_STATIC_COUNT ((s32)(sizeof(s_catalog) / sizeof(s_catalog[0])))

/* ============================================================
 * Helpers
 * ============================================================ */

static s32 forgeStrStartsWith(const char *s, const char *prefix)
{
	while (*prefix) {
		if (!*s || *s != *prefix) return 0;
		++s; ++prefix;
	}
	return 1;
}

static s32 forgeStrCaseContains(const char *hay, const char *needle)
{
	if (!hay || !needle || !*needle) return 1;
	for (const char *p = hay; *p; ++p) {
		const char *a = p;
		const char *b = needle;
		while (*a && *b) {
			s32 ca = (*a >= 'A' && *a <= 'Z') ? *a + ('a' - 'A') : *a;
			s32 cb = (*b >= 'A' && *b <= 'Z') ? *b + ('a' - 'A') : *b;
			if (ca != cb) break;
			++a; ++b;
		}
		if (!*b) return 1;
	}
	return 0;
}

void forgeCopyStr(char *dst, const char *src, size_t n)
{
	if (n == 0) return;
	size_t i = 0;
	if (src) {
		for (; i + 1 < n && src[i]; ++i) dst[i] = src[i];
	}
	dst[i] = '\0';
}

u32 forgeObjectNextUid(void)
{
	u32 uid = s_next_uid;
	if (++s_next_uid == 0) s_next_uid = 1;
	return uid;
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

void forgeCoreInit(void)
{
	if (s_initialized) return;

	memset(s_objects, 0, sizeof(s_objects));
	memset(s_logic_nodes, 0, sizeof(s_logic_nodes));
	memset(s_logic_wires, 0, sizeof(s_logic_wires));
	memset(s_channels, 0, sizeof(s_channels));
	memset(s_objectives, 0, sizeof(s_objectives));
	memset(s_prefabs, 0, sizeof(s_prefabs));
	memset(s_selection, 0, sizeof(s_selection));
	s_selection_count = 0;

	memset(&s_gametype, 0, sizeof(s_gametype));
	forgeCopyStr(s_gametype.name, "Default Deathmatch", FORGE_NAME_LEN);
	s_gametype.structure = FORGE_GT_SINGLE;
	s_gametype.win_condition = FORGE_WIN_SCORE_TARGET;
	s_gametype.num_rounds = 1;
	s_gametype.score_limit = 20;
	s_gametype.time_limit_sec = 600;
	s_gametype.score_per_kill = 1;
	s_gametype.health_mult = 1.0f;
	forgeCopyStr(s_gametype.starting_weapon, "base:pad_pistol", FORGE_ID_LEN);
	s_gametype.show_wave_counter = 1;
	s_gametype.show_boss_bar = 1;

	memset(&s_boss_state, 0, sizeof(s_boss_state));

	memset(&s_skylight, 0, sizeof(s_skylight));
	s_skylight.direction_yaw_deg = 135.0f;
	s_skylight.direction_pitch_deg = -45.0f;
	s_skylight.color[0] = 1.0f; s_skylight.color[1] = 0.95f; s_skylight.color[2] = 0.85f;
	s_skylight.intensity = 1.0f;
	s_skylight.cast_shadow = 1;
	s_skylight.shadow_softness = 2;

	memset(&s_atmosphere, 0, sizeof(s_atmosphere));
	s_atmosphere.fog_color[0] = 0.5f; s_atmosphere.fog_color[1] = 0.55f; s_atmosphere.fog_color[2] = 0.65f;
	s_atmosphere.fog_near = 2000.0f;
	s_atmosphere.fog_far  = 8000.0f;
	s_atmosphere.ambient_color[0] = 0.25f; s_atmosphere.ambient_color[1] = 0.3f; s_atmosphere.ambient_color[2] = 0.4f;
	s_atmosphere.ambient_intensity = 0.5f;
	s_atmosphere.exposure = 1.0f;
	s_atmosphere.bloom_strength = 0.3f;
	forgeCopyStr(s_atmosphere.sky_id, "base:carrington_day", FORGE_ID_LEN);

	memset(&s_settings, 0, sizeof(s_settings));
	forgeCopyStr(s_settings.map_name, "Untitled Map", FORGE_NAME_LEN);
	forgeCopyStr(s_settings.author, "Player", FORGE_NAME_LEN);
	forgeCopyStr(s_settings.base_stage_id, "base:training", FORGE_ID_LEN);
	s_settings.max_players = 8;
	s_settings.recommended_players = 4;
	s_settings.map_size_tag = 1;
	s_settings.team_spawn_mode = 0;
	s_settings.min_spawn_points = 8;
	s_settings.respawn_delay_sec = 2.0f;
	s_settings.spawn_protection_sec = 1.5f;
	s_settings.default_time_limit_sec = 600;
	s_settings.default_score_limit = 20;
	s_settings.bounds_min[0] = -8192.0f; s_settings.bounds_min[1] = -4096.0f; s_settings.bounds_min[2] = -8192.0f;
	s_settings.bounds_max[0] =  8192.0f; s_settings.bounds_max[1] =  4096.0f; s_settings.bounds_max[2] =  8192.0f;
	s_settings.grid_size = 100.0f;
	s_settings.rotation_snap_deg = 15.0f;
	s_settings.surface_snap = 1;
	/* R2/R4 defaults: honour the map author's weapon choices unless
	 * the match explicitly opts out via "Use Map Defaults" UX. */
	s_settings.weapon_source = FORGE_WEAPONS_MAP_DEFAULTS;
	s_settings.allow_match_override = 1;
	/* S313 variant default -- new empty canvas. */
	s_settings.variant_mode = FORGE_VARIANT_NEW_EMPTY;
	s_settings.variant_source_slug[0] = '\0';

	/* S313 -- live bot testing defaults. */
	memset(&s_bot_settings, 0, sizeof(s_bot_settings));
	s_bot_settings.spawn_mode = FORGE_BOT_SPAWN_ANY;
	s_bot_settings.active_count = 0;
	s_bot_settings.frozen_count = 0;
	s_bot_settings.near_me_radius = 1500.0f;
	s_bot_settings.smart_aggression = 0.75f;
	forgeCopyStr(s_bot_settings.default_body_id, "base:body_bond", FORGE_ID_LEN);
	forgeCopyStr(s_bot_settings.default_difficulty, "normal", FORGE_NAME_LEN);

	memset(&s_editor, 0, sizeof(s_editor));
	s_editor.tool = FORGE_TOOL_SELECT;
	s_editor.target = FORGE_TARGET_OBJECTS;
	s_editor.snap_grid_enabled = 1;
	s_editor.snap_surface_enabled = 1;
	s_editor.zone_viz_enabled = 1;
	s_editor.logic_wires_viz_enabled = 1;
	s_editor.category_filter = FORGE_CAT_COUNT;
	s_editor.grid_size = 100.0f;
	s_editor.rotation_snap_deg = 15.0f;

	memset(&s_placement, 0, sizeof(s_placement));

	s_next_uid = 1;
	s_initialized = 1;
	sysLogPrintf(LOG_NOTE, "GRID: core init (catalog=%d entries, cap obj=%d logic=%d)",
			FORGE_CATALOG_STATIC_COUNT, FORGE_MAX_OBJECTS, FORGE_MAX_LOGIC_NODES);
}

void forgeCoreReset(void)
{
	/* Blow away all map state but preserve the init sentinel so caller
	 * doesn't have to re-init. */
	s_initialized = 0;
	forgeCoreInit();
}

/* Forward to undo for the per-tick logic advance. */
void forgeLogicTick(void);
void forgeLogicResetPerFrameFlags(void);

void forgeCoreTick(void)
{
	if (!s_initialized) forgeCoreInit();
	forgeLogicResetPerFrameFlags();
	/* Per-tick logic is forgeLogicTick but it lives in forge_logic.c; runtime
	 * execution remains a placeholder until real gameplay wiring lands. */
}

/* ============================================================
 * Catalog
 * ============================================================ */

s32 forgeCatalogCount(void)
{
	return FORGE_CATALOG_STATIC_COUNT;
}

const forge_catalog_entry_t *forgeCatalogGet(s32 index)
{
	if (index < 0 || index >= FORGE_CATALOG_STATIC_COUNT) return NULL;
	return &s_catalog[index];
}

const forge_catalog_entry_t *forgeCatalogFind(const char *id)
{
	if (!id) return NULL;
	for (s32 i = 0; i < FORGE_CATALOG_STATIC_COUNT; ++i) {
		if (strcmp(s_catalog[i].id, id) == 0) return &s_catalog[i];
	}
	return NULL;
}

s32 forgeCatalogSearch(const char *query, s32 *out_indices, s32 max)
{
	if (!out_indices || max <= 0) return 0;
	s32 n = 0;
	for (s32 i = 0; i < FORGE_CATALOG_STATIC_COUNT && n < max; ++i) {
		const forge_catalog_entry_t *e = &s_catalog[i];
		if (!query || !*query ||
				forgeStrCaseContains(e->name, query) ||
				forgeStrCaseContains(e->id, query) ||
				(e->tags && forgeStrCaseContains(e->tags, query))) {
			out_indices[n++] = i;
		}
	}
	return n;
}

const char *forgeCategoryName(forge_category_t c)
{
	switch (c) {
	case FORGE_CAT_GEOMETRY:     return "Geometry";
	case FORGE_CAT_PROP:         return "Props";
	case FORGE_CAT_WEAPON_PAD:   return "Weapons";
	case FORGE_CAT_SPAWN_POINT:  return "Spawn Points";
	case FORGE_CAT_PICKUP:       return "Pickups";
	case FORGE_CAT_LIGHT:        return "Lighting";
	case FORGE_CAT_EFFECT:       return "Effects";
	case FORGE_CAT_ZONE:         return "Zones";
	case FORGE_CAT_INTERACTABLE: return "Interactables";
	case FORGE_CAT_AI:           return "Characters";
	case FORGE_CAT_LOGIC:        return "Logic";
	case FORGE_CAT_PREFAB:       return "Prefabs";
	default:                     return "(All)";
	}
}

/* ============================================================
 * Object store
 * ============================================================ */

s32 forgeObjectCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		if (s_objects[i].in_use) ++n;
	}
	return n;
}

forge_object_t *forgeObjectGet(s32 index)
{
	if (index < 0 || index >= FORGE_MAX_OBJECTS) return NULL;
	return &s_objects[index];
}

forge_object_t *forgeObjectFindByUid(u32 uid)
{
	if (uid == 0) return NULL;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		if (s_objects[i].in_use && s_objects[i].uid == uid) return &s_objects[i];
	}
	return NULL;
}

static void forgeObjectInitDefaults(forge_object_t *o, const forge_catalog_entry_t *e)
{
	memset(o, 0, sizeof(*o));
	o->uid = forgeObjectNextUid();
	o->in_use = 1;
	o->visible = 1;
	o->enabled = 1;
	o->collision_mode = FORGE_COLLISION_SOLID;
	o->team = 0;
	o->scale[0] = o->scale[1] = o->scale[2] = 1.0f;
	o->tint[0] = o->tint[1] = o->tint[2] = 1.0f;
	o->cast_shadows = 1;
	if (e) {
		o->category = (u8)e->category;
		forgeCopyStr(o->catalog_id, e->id, FORGE_ID_LEN);
		/* Fill in sensible defaults per category. */
		switch (e->category) {
		case FORGE_CAT_WEAPON_PAD:
			o->props.weapon.ammo = -1;
			o->props.weapon.respawn_sec = 10.0f;
			o->props.weapon.respawn_effect = 1;
			forgeCopyStr(o->props.weapon.weapon_id, e->id, FORGE_ID_LEN);
			break;
		case FORGE_CAT_SPAWN_POINT:
			o->props.spawn.type = (u8)e->default_sub_type;
			o->props.spawn.priority = 100;
			o->props.spawn.radius = 256.0f;
			break;
		case FORGE_CAT_AI:
			forgeCopyStr(o->props.ai.body_id, "base:body_bond", FORGE_ID_LEN);
			forgeCopyStr(o->props.ai.head_id, "base:head_bond", FORGE_ID_LEN);
			forgeCopyStr(o->props.ai.weapon_id, "base:pad_pistol", FORGE_ID_LEN);
			o->props.ai.behavior = FORGE_AI_PATROL;
			o->props.ai.health_mult = 1.0f;
			o->props.ai.alert_radius = 1500.0f;
			o->props.ai.faction = 1;
			o->props.ai.boss_scale = 1.0f;
			break;
		case FORGE_CAT_LIGHT:
			o->props.light.type = (u8)e->default_sub_type;
			o->props.light.color[0] = 1.0f;
			o->props.light.color[1] = 1.0f;
			o->props.light.color[2] = 1.0f;
			o->props.light.intensity = 1.0f;
			o->props.light.range = 1000.0f;
			o->props.light.inner_cone_deg = 20.0f;
			o->props.light.outer_cone_deg = 35.0f;
			o->props.light.falloff = 1.0f;
			o->props.light.width = 200.0f;
			o->props.light.height = 200.0f;
			break;
		case FORGE_CAT_ZONE:
			o->props.zone.type = (u8)e->default_sub_type;
			o->props.zone.shape = FORGE_ZONE_SHAPE_BOX;
			o->props.zone.size[0] = 256.0f; o->props.zone.size[1] = 256.0f; o->props.zone.size[2] = 256.0f;
			o->props.zone.once_or_repeat = 1;
			o->props.zone.damage_per_sec = (e->default_sub_type == FORGE_ZONE_RADIATION) ? 5.0f : 0.0f;
			o->props.zone.gravity_mult = 1.0f;
			o->props.zone.gravity_dir[1] = -1.0f;
			o->collision_mode = FORGE_COLLISION_PASSTHROUGH;
			break;
		case FORGE_CAT_INTERACTABLE:
			if (forgeStrStartsWith(e->id, "base:door_")) {
				o->props.door.open_dir = (u8)e->default_sub_type;
				o->props.door.open_speed_sec = 1.0f;
				o->props.door.auto_close = (strcmp(e->id, "base:door_destruct") == 0) ? 0 : 1;
				o->props.door.auto_close_delay_sec = 3.0f;
				o->props.door.locked = (strcmp(e->id, "base:door_keycard") == 0) ? 1 : 0;
			} else if (forgeStrStartsWith(e->id, "base:elevator_")) {
				o->props.elev.num_stops = 2;
				o->props.elev.speed_units_per_sec = 200.0f;
				o->props.elev.wait_time_sec = 2.0f;
				o->props.elev.stops_y[0] = 0.0f;
				o->props.elev.stops_y[1] = 500.0f;
				o->props.elev.call_button = 1;
			} else if (forgeStrStartsWith(e->id, "base:switch_")) {
				o->props.sw.type = 0;
				o->props.sw.activation = (u8)e->default_sub_type;
				o->props.sw.cooldown_sec = 0.5f;
			}
			break;
		case FORGE_CAT_PICKUP:
			forgeCopyStr(o->props.pickup.item_id, e->id, FORGE_ID_LEN);
			o->props.pickup.quantity = 1;
			o->props.pickup.respawn_sec = 30.0f;
			break;
		case FORGE_CAT_EFFECT:
			o->props.effect.kind = (u8)e->default_sub_type;
			o->props.effect.intensity = 1.0f;
			o->props.effect.range = 500.0f;
			o->props.effect.looping = 1;
			forgeCopyStr(o->props.effect.asset_id, e->id, FORGE_ID_LEN);
			o->props.effect.color[0] = 1.0f; o->props.effect.color[1] = 1.0f; o->props.effect.color[2] = 1.0f;
			break;
		default:
			break;
		}
	}
}

forge_object_t *forgeObjectAllocate(forge_category_t cat, const char *catalog_id)
{
	if (!s_initialized) forgeCoreInit();
	const forge_catalog_entry_t *e = NULL;
	if (catalog_id) e = forgeCatalogFind(catalog_id);

	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		if (!s_objects[i].in_use) {
			forgeObjectInitDefaults(&s_objects[i], e);
			if (!e) s_objects[i].category = (u8)cat;
			return &s_objects[i];
		}
	}
	sysLogPrintf(LOG_WARNING, "GRID: object pool full (FORGE_MAX_OBJECTS=%d)", FORGE_MAX_OBJECTS);
	return NULL;
}

void forgeObjectRemove(u32 uid)
{
	if (uid == 0) return;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		if (s_objects[i].in_use && s_objects[i].uid == uid) {
			s_objects[i].in_use = 0;
			forgeSelectionRemove(uid);
			return;
		}
	}
}

/* ============================================================
 * Selection
 * ============================================================ */

s32 forgeSelectionCount(void)
{
	return s_selection_count;
}

u32 forgeSelectionGet(s32 index)
{
	if (index < 0 || index >= s_selection_count) return 0;
	return s_selection[index];
}

void forgeSelectionAdd(u32 uid)
{
	if (uid == 0 || s_selection_count >= FORGE_MAX_SELECTED) return;
	for (s32 i = 0; i < s_selection_count; ++i) {
		if (s_selection[i] == uid) return;
	}
	s_selection[s_selection_count++] = uid;
	forge_object_t *o = forgeObjectFindByUid(uid);
	if (o) o->selected = 1;
}

void forgeSelectionRemove(u32 uid)
{
	for (s32 i = 0; i < s_selection_count; ++i) {
		if (s_selection[i] == uid) {
			for (s32 j = i; j < s_selection_count - 1; ++j) s_selection[j] = s_selection[j + 1];
			--s_selection_count;
			forge_object_t *o = forgeObjectFindByUid(uid);
			if (o) o->selected = 0;
			return;
		}
	}
}

void forgeSelectionClear(void)
{
	for (s32 i = 0; i < s_selection_count; ++i) {
		forge_object_t *o = forgeObjectFindByUid(s_selection[i]);
		if (o) o->selected = 0;
	}
	s_selection_count = 0;
}

void forgeSelectionSelectOnly(u32 uid)
{
	forgeSelectionClear();
	forgeSelectionAdd(uid);
}

/* Forward to undo for delete */
void forgeUndoRecordDelete(u32 uid, const forge_object_t *snap);

void forgeSelectionDelete(void)
{
	if (s_selection_count == 0) return;
	forgeUndoPushBarrier("delete");
	u32 copy[FORGE_MAX_SELECTED];
	s32 n = s_selection_count;
	for (s32 i = 0; i < n; ++i) copy[i] = s_selection[i];
	s_selection_count = 0;
	for (s32 i = 0; i < n; ++i) {
		forge_object_t *o = forgeObjectFindByUid(copy[i]);
		if (o) {
			forge_object_t snap = *o;
			forgeUndoRecordDelete(o->uid, &snap);
			o->in_use = 0;
			o->selected = 0;
		}
	}
}

/* Forward to undo for place */
void forgeUndoRecordPlace(u32 uid, const forge_object_t *snap);

void forgeSelectionDuplicate(f32 ox, f32 oy, f32 oz)
{
	if (s_selection_count == 0) return;
	s32 n = s_selection_count;
	u32 copy[FORGE_MAX_SELECTED];
	for (s32 i = 0; i < n; ++i) copy[i] = s_selection[i];
	forgeSelectionClear();
	forgeUndoPushBarrier("duplicate");
	for (s32 i = 0; i < n; ++i) {
		forge_object_t *src = forgeObjectFindByUid(copy[i]);
		if (!src) continue;
		forge_object_t *dst = forgeObjectAllocate((forge_category_t)src->category, src->catalog_id);
		if (!dst) break;
		u32 new_uid = dst->uid;
		*dst = *src;
		dst->uid = new_uid;
		dst->in_use = 1;
		dst->selected = 1;
		dst->pos[0] += ox; dst->pos[1] += oy; dst->pos[2] += oz;
		forgeSelectionAdd(dst->uid);
		forgeUndoRecordPlace(dst->uid, dst);
	}
}

/* ============================================================
 * Editor state
 * ============================================================ */

forge_placement_state_t *forgeGetPlacement(void) { return &s_placement; }
forge_editor_state_t *forgeGetEditor(void) { return &s_editor; }

/* ============================================================
 * Placement
 * ============================================================ */

static f32 forgeSnap(f32 v, f32 step)
{
	if (step < 0.01f) return v;
	f32 q = v / step + (v >= 0.0f ? 0.5f : -0.5f);
	return (f32)((s32)q) * step;
}

void forgePlaceBegin(const char *catalog_id)
{
	if (!catalog_id) return;
	const forge_catalog_entry_t *e = forgeCatalogFind(catalog_id);
	if (!e) {
		sysLogPrintf(LOG_WARNING, "GRID: placeBegin unknown catalog id '%s'", catalog_id);
		return;
	}
	s_placement.ghost_active = 1;
	s_placement.ghost_valid = 1;
	forgeCopyStr(s_placement.pending_catalog_id, catalog_id, FORGE_ID_LEN);
	s_placement.ghost_scale[0] = s_placement.ghost_scale[1] = s_placement.ghost_scale[2] = 1.0f;
	s_editor.tool = FORGE_TOOL_PLACE;
	sysLogPrintf(LOG_NOTE, "GRID: place begin '%s' (%s)", e->id, e->name);
}

void forgePlaceUpdate(const f32 cpos[3], f32 yaw_deg, f32 pitch_deg, f32 distance)
{
	if (!s_placement.ghost_active) return;
	const f32 DEG2RAD = 0.017453292519943f;
	f32 yr = yaw_deg * DEG2RAD;
	f32 pr = pitch_deg * DEG2RAD;
	f32 fx = sinf(yr) * cosf(pr);
	f32 fy = sinf(pr);
	f32 fz = cosf(yr) * cosf(pr);

	f32 px = cpos[0] + fx * distance;
	f32 py = cpos[1] + fy * distance;
	f32 pz = cpos[2] + fz * distance;

	if (s_editor.snap_grid_enabled && s_editor.grid_size > 0.0f) {
		px = forgeSnap(px, s_editor.grid_size);
		py = forgeSnap(py, s_editor.grid_size);
		pz = forgeSnap(pz, s_editor.grid_size);
	}
	s_placement.ghost_pos[0] = px;
	s_placement.ghost_pos[1] = py;
	s_placement.ghost_pos[2] = pz;

	/* Validity check: within map bounds. Real collision check deferred. */
	u8 valid = 1;
	if (px < s_settings.bounds_min[0] || px > s_settings.bounds_max[0]) valid = 0;
	if (py < s_settings.bounds_min[1] || py > s_settings.bounds_max[1]) valid = 0;
	if (pz < s_settings.bounds_min[2] || pz > s_settings.bounds_max[2]) valid = 0;

	/* Budget check - hard cap stops placement. */
	forge_budget_stats_t b;
	forgeBudgetCompute(&b);
	if (forgeBudgetOverHard(&b)) valid = 0;

	s_placement.ghost_valid = valid;
}

s32 forgePlaceCommit(void)
{
	if (!s_placement.ghost_active || !s_placement.ghost_valid) return 0;
	forge_object_t *o = forgeObjectAllocate(FORGE_CAT_COUNT, s_placement.pending_catalog_id);
	if (!o) return 0;
	o->pos[0] = s_placement.ghost_pos[0];
	o->pos[1] = s_placement.ghost_pos[1];
	o->pos[2] = s_placement.ghost_pos[2];
	o->rot[0] = s_placement.ghost_rot[0];
	o->rot[1] = s_placement.ghost_rot[1];
	o->rot[2] = s_placement.ghost_rot[2];
	if (s_placement.ghost_scale[0] > 0.01f) {
		o->scale[0] = s_placement.ghost_scale[0];
		o->scale[1] = s_placement.ghost_scale[1];
		o->scale[2] = s_placement.ghost_scale[2];
	}
	sysLogPrintf(LOG_NOTE, "GRID: place commit '%s' uid=%u at (%.0f %.0f %.0f)",
			o->catalog_id, o->uid, o->pos[0], o->pos[1], o->pos[2]);
	forgeUndoRecordPlace(o->uid, o);
	return (s32)o->uid;
}

void forgePlaceCancel(void)
{
	if (s_placement.ghost_active) {
		sysLogPrintf(LOG_NOTE, "GRID: place cancel '%s'", s_placement.pending_catalog_id);
	}
	s_placement.ghost_active = 0;
	s_placement.pending_catalog_id[0] = '\0';
	if (s_editor.tool == FORGE_TOOL_PLACE) s_editor.tool = FORGE_TOOL_SELECT;
}

/* ============================================================
 * Logic node / wire pools
 * ============================================================ */

s32 forgeLogicNodeCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		if (s_logic_nodes[i].in_use) ++n;
	}
	return n;
}

forge_logic_node_t *forgeLogicNodeGet(s32 index)
{
	if (index < 0 || index >= FORGE_MAX_LOGIC_NODES) return NULL;
	return &s_logic_nodes[index];
}

forge_logic_node_t *forgeLogicNodeFindByUid(u32 uid)
{
	if (uid == 0) return NULL;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		if (s_logic_nodes[i].in_use && s_logic_nodes[i].uid == uid) return &s_logic_nodes[i];
	}
	return NULL;
}

forge_logic_node_t *forgeLogicNodeAllocate(forge_logic_kind_t kind, forge_logic_op_t op)
{
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		if (!s_logic_nodes[i].in_use) {
			memset(&s_logic_nodes[i], 0, sizeof(s_logic_nodes[i]));
			s_logic_nodes[i].uid = forgeObjectNextUid();
			s_logic_nodes[i].in_use = 1;
			s_logic_nodes[i].kind = (u8)kind;
			s_logic_nodes[i].op = (u8)op;
			return &s_logic_nodes[i];
		}
	}
	sysLogPrintf(LOG_WARNING, "GRID: logic node pool full (max=%d)", FORGE_MAX_LOGIC_NODES);
	return NULL;
}

void forgeLogicNodeRemove(u32 uid)
{
	if (uid == 0) return;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		if (s_logic_nodes[i].in_use && s_logic_nodes[i].uid == uid) {
			s_logic_nodes[i].in_use = 0;
			/* purge wires touching this node */
			for (s32 w = 0; w < FORGE_MAX_LOGIC_WIRES; ++w) {
				if (s_logic_wires[w].in_use &&
						(s_logic_wires[w].src_node_uid == uid ||
						 s_logic_wires[w].dst_node_uid == uid)) {
					s_logic_wires[w].in_use = 0;
				}
			}
			return;
		}
	}
}

s32 forgeLogicWireCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_WIRES; ++i) {
		if (s_logic_wires[i].in_use) ++n;
	}
	return n;
}

forge_logic_wire_t *forgeLogicWireGet(s32 index)
{
	if (index < 0 || index >= FORGE_MAX_LOGIC_WIRES) return NULL;
	return &s_logic_wires[index];
}

s32 forgeLogicWireCreate(u32 src_uid, u32 dst_uid, u8 src_port, u8 dst_port)
{
	if (src_uid == 0 || dst_uid == 0 || src_uid == dst_uid) return -1;
	if (!forgeLogicNodeFindByUid(src_uid) || !forgeLogicNodeFindByUid(dst_uid)) return -1;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_WIRES; ++i) {
		if (!s_logic_wires[i].in_use) {
			s_logic_wires[i].src_node_uid = src_uid;
			s_logic_wires[i].dst_node_uid = dst_uid;
			s_logic_wires[i].src_port = src_port;
			s_logic_wires[i].dst_port = dst_port;
			s_logic_wires[i].in_use = 1;
			return i;
		}
	}
	return -1;
}

void forgeLogicWireRemove(s32 index)
{
	if (index < 0 || index >= FORGE_MAX_LOGIC_WIRES) return;
	s_logic_wires[index].in_use = 0;
}

/* ============================================================
 * Channels
 * ============================================================ */

s32 forgeChannelCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_CHANNELS; ++i) {
		if (s_channels[i].in_use) ++n;
	}
	return n;
}

forge_channel_t *forgeChannelGet(s32 index)
{
	if (index < 0 || index >= FORGE_MAX_CHANNELS) return NULL;
	return &s_channels[index];
}

forge_channel_t *forgeChannelFind(const char *name)
{
	if (!name) return NULL;
	for (s32 i = 0; i < FORGE_MAX_CHANNELS; ++i) {
		if (s_channels[i].in_use && strcmp(s_channels[i].name, name) == 0) return &s_channels[i];
	}
	return NULL;
}

forge_channel_t *forgeChannelCreate(const char *name)
{
	if (!name) return NULL;
	forge_channel_t *existing = forgeChannelFind(name);
	if (existing) return existing;
	for (s32 i = 0; i < FORGE_MAX_CHANNELS; ++i) {
		if (!s_channels[i].in_use) {
			memset(&s_channels[i], 0, sizeof(s_channels[i]));
			forgeCopyStr(s_channels[i].name, name, FORGE_NAME_LEN);
			s_channels[i].in_use = 1;
			return &s_channels[i];
		}
	}
	return NULL;
}

void forgeChannelSet(const char *name, u8 state)
{
	forge_channel_t *c = forgeChannelFind(name);
	if (!c) c = forgeChannelCreate(name);
	if (c) {
		if (c->state != state) {
			sysLogPrintf(LOG_NOTE, "GRID.LOGIC: channel '%s' %s", name,
					state ? "ON" : "OFF");
			/* Fire ON_CHANNEL events matching this channel. */
			extern void forgeLogicFireChannelChange(const char *name, u8 state);
			forgeLogicFireChannelChange(name, state);
		}
		c->state = state ? 1 : 0;
	}
}

/* ============================================================
 * Game type / boss / objectives / atmosphere
 * ============================================================ */

forge_gametype_t *forgeGameType(void) { return &s_gametype; }

void forgeGameTypeReset(void)
{
	memset(&s_gametype, 0, sizeof(s_gametype));
	forgeCopyStr(s_gametype.name, "Default Deathmatch", FORGE_NAME_LEN);
	s_gametype.structure = FORGE_GT_SINGLE;
	s_gametype.win_condition = FORGE_WIN_SCORE_TARGET;
	s_gametype.num_rounds = 1;
	s_gametype.score_limit = 20;
	s_gametype.time_limit_sec = 600;
	s_gametype.health_mult = 1.0f;
	s_gametype.score_per_kill = 1;
}

forge_boss_state_t *forgeBossState(void) { return &s_boss_state; }

void forgeBossSetActive(u32 chr_uid, const char *name, f32 max_health, f32 *phases, s32 num_phases)
{
	memset(&s_boss_state, 0, sizeof(s_boss_state));
	s_boss_state.active = 1;
	s_boss_state.chr_uid = chr_uid;
	s_boss_state.max_health = max_health;
	s_boss_state.current_health = max_health;
	s_boss_state.num_phases = (u8)((num_phases < 4) ? num_phases : 4);
	forgeCopyStr(s_boss_state.name, name ? name : "Boss", FORGE_NAME_LEN);
	if (phases) {
		for (s32 i = 0; i < s_boss_state.num_phases; ++i) {
			/* phase thresholds stored as % -- not used in this shell */
			FORGE_UNUSED(phases[i]);
		}
	}
	sysLogPrintf(LOG_NOTE, "GRID.BOSS: active uid=%u name='%s' hp=%.1f phases=%d",
			chr_uid, s_boss_state.name, max_health, num_phases);
}

void forgeBossApplyDamage(u32 chr_uid, f32 amount)
{
	if (!s_boss_state.active || s_boss_state.chr_uid != chr_uid) return;
	s_boss_state.current_health -= amount;
	if (s_boss_state.current_health < 0.0f) s_boss_state.current_health = 0.0f;
	/* Phase transition logic would fire here in full runtime. */
	if (s_boss_state.current_health <= 0.0f) {
		s_boss_state.active = 0;
		sysLogPrintf(LOG_NOTE, "GRID.BOSS: defeated '%s'", s_boss_state.name);
	}
}

s32 forgeObjectiveCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_OBJECTIVES; ++i) {
		if (s_objectives[i].in_use) ++n;
	}
	return n;
}

forge_objective_t *forgeObjectiveGet(s32 index)
{
	if (index < 0 || index >= FORGE_MAX_OBJECTIVES) return NULL;
	return &s_objectives[index];
}

forge_objective_t *forgeObjectiveAllocate(forge_objective_kind_t kind)
{
	for (s32 i = 0; i < FORGE_MAX_OBJECTIVES; ++i) {
		if (!s_objectives[i].in_use) {
			memset(&s_objectives[i], 0, sizeof(s_objectives[i]));
			s_objectives[i].in_use = 1;
			s_objectives[i].kind = (u8)kind;
			s_objectives[i].status = FORGE_OBJ_PENDING;
			s_objectives[i].order = (u8)i;
			return &s_objectives[i];
		}
	}
	return NULL;
}

void forgeObjectiveRemove(s32 index)
{
	if (index < 0 || index >= FORGE_MAX_OBJECTIVES) return;
	s_objectives[index].in_use = 0;
}

void forgeObjectiveSetStatus(s32 index, forge_objective_status_t status)
{
	if (index < 0 || index >= FORGE_MAX_OBJECTIVES) return;
	if (!s_objectives[index].in_use) return;
	s_objectives[index].status = (u8)status;
	sysLogPrintf(LOG_NOTE, "GRID.MISSION: objective %d -> %s",
			index,
			status == FORGE_OBJ_COMPLETE ? "COMPLETE" :
			status == FORGE_OBJ_FAILED   ? "FAILED"   : "PENDING");
}

forge_skylight_t   *forgeSkylight(void)   { return &s_skylight; }
forge_atmosphere_t *forgeAtmosphere(void) { return &s_atmosphere; }
forge_map_settings_t *forgeMapSettings(void) { return &s_settings; }

/* ============================================================
 * S313 -- Live bot testing
 *
 * The forge-side data captures what the author has asked for.  Engine
 * integration (botmgrAllocateBot / botmgrRemoveAll) is a follow-up
 * polish pass -- today these entry points log intent and bump pending
 * counters that can be consumed by a runtime tick once wired up.
 * ============================================================ */

forge_bot_settings_t *forgeBotSettings(void) { return &s_bot_settings; }

void forgeBotAddRequest(s32 active)
{
	if (!s_initialized) forgeCoreInit();
	if (active) {
		s_bot_settings.active_count++;
		s_bot_settings.pending_add_active++;
		sysLogPrintf(LOG_NOTE, "GRID.BOT: add active (mode=%d active=%d frozen=%d body='%s' diff='%s')",
				s_bot_settings.spawn_mode,
				(s32)s_bot_settings.active_count,
				(s32)s_bot_settings.frozen_count,
				s_bot_settings.default_body_id,
				s_bot_settings.default_difficulty);
	} else {
		s_bot_settings.frozen_count++;
		s_bot_settings.pending_add_frozen++;
		sysLogPrintf(LOG_NOTE, "GRID.BOT: add frozen (active=%d frozen=%d)",
				(s32)s_bot_settings.active_count,
				(s32)s_bot_settings.frozen_count);
	}
}

void forgeBotRemoveAll(void)
{
	if (!s_initialized) forgeCoreInit();
	s_bot_settings.active_count = 0;
	s_bot_settings.frozen_count = 0;
	s_bot_settings.pending_remove_all++;
	sysLogPrintf(LOG_NOTE, "GRID.BOT: remove all (pending_engine_sync=%d)",
			s_bot_settings.pending_remove_all);
}

void forgeBotFreezeAll(s32 frozen)
{
	if (!s_initialized) forgeCoreInit();
	s_bot_settings.all_frozen = frozen ? 1 : 0;
	sysLogPrintf(LOG_NOTE, "GRID.BOT: freeze-all = %d", (s32)s_bot_settings.all_frozen);
}

/* ============================================================
 * S313 -- Map variant helpers
 *
 * Base stage imports are lightweight today: the editor tags every
 * currently-placed object with `from_base=1` on import, so the save
 * path can elide them from the delta.  Full engine-side import of the
 * base stage's intro-commands / pads / props into forge objects is a
 * follow-up polish pass.
 * ============================================================ */

void forgeImportBaseStageObjects(void)
{
	if (!s_initialized) forgeCoreInit();
	s32 marked = 0;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		if (s_objects[i].in_use && !s_objects[i].from_base) {
			s_objects[i].from_base = 1;
			++marked;
		}
	}
	sysLogPrintf(LOG_NOTE, "GRID.VARIANT: marked %d objects as from_base", marked);
}

s32 forgeObjectResetToBase(u32 uid)
{
	forge_object_t *o = forgeObjectFindByUid(uid);
	if (!o) return 0;
	if (!o->from_base) return 0;
	/* Reset the modified-from-base flag back to pristine.  The author's
	 * transform/property edits remain in the object for now; a richer
	 * implementation would snapshot the base-state at import and
	 * diff/restore it here. */
	o->from_base = 1;
	sysLogPrintf(LOG_NOTE, "GRID.VARIANT: reset uid=%u to base pristine state", uid);
	return 1;
}

s32 forgeObjectRemoveFromBase(u32 uid)
{
	forge_object_t *o = forgeObjectFindByUid(uid);
	if (!o) return 0;
	if (!o->from_base) return 0;
	/* Variant-side "delete the base object" means in_use=0; the save
	 * path uses the absence of this UID in the saved list to signal
	 * the loader to elide that object during base re-import. */
	o->in_use = 0;
	forgeSelectionRemove(uid);
	sysLogPrintf(LOG_NOTE, "GRID.VARIANT: removed base object uid=%u", uid);
	return 1;
}

s32 forgeCountBaseObjects(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		if (s_objects[i].in_use && s_objects[i].from_base) ++n;
	}
	return n;
}

s32 forgeCountDeltaObjects(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		if (s_objects[i].in_use && !s_objects[i].from_base) ++n;
	}
	return n;
}

/* ============================================================
 * Budget
 * ============================================================ */

void forgeBudgetCompute(forge_budget_stats_t *out)
{
	if (!out) return;
	memset(out, 0, sizeof(*out));
	s32 tri = 0;
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		if (!s_objects[i].in_use) continue;
		out->objects++;
		const forge_catalog_entry_t *e = forgeCatalogFind(s_objects[i].catalog_id);
		if (e) tri += e->tri_cost;
		if (s_objects[i].category == FORGE_CAT_LIGHT) out->lights++;
		if (s_objects[i].category == FORGE_CAT_EFFECT) {
			if (s_objects[i].props.effect.kind == 0) out->effects++;
			if (s_objects[i].props.effect.kind == 1) out->audio_emitters++;
		}
	}
	out->triangles = tri;
	out->logic_nodes = forgeLogicNodeCount();

	out->objects_soft = 500;   out->objects_hard = FORGE_MAX_OBJECTS;
	out->triangles_soft = 500000; out->triangles_hard = 1000000;
	out->lights_soft = 32;     out->lights_hard = FORGE_MAX_LIGHTS;
	out->logic_nodes_soft = 128; out->logic_nodes_hard = FORGE_MAX_LOGIC_NODES;
	out->effects_soft = 16;    out->effects_hard = 32;
	out->audio_emitters_soft = 16; out->audio_emitters_hard = 32;
}

s32 forgeBudgetOverSoft(const forge_budget_stats_t *s)
{
	if (!s) return 0;
	if (s->objects      > s->objects_soft)      return 1;
	if (s->triangles    > s->triangles_soft)    return 1;
	if (s->lights       > s->lights_soft)       return 1;
	if (s->logic_nodes  > s->logic_nodes_soft)  return 1;
	if (s->effects      > s->effects_soft)      return 1;
	if (s->audio_emitters > s->audio_emitters_soft) return 1;
	return 0;
}

s32 forgeBudgetOverHard(const forge_budget_stats_t *s)
{
	if (!s) return 0;
	if (s->objects      >= s->objects_hard)      return 1;
	if (s->triangles    >= s->triangles_hard)    return 1;
	if (s->lights       >= s->lights_hard)       return 1;
	if (s->logic_nodes  >= s->logic_nodes_hard)  return 1;
	if (s->effects      >= s->effects_hard)      return 1;
	if (s->audio_emitters >= s->audio_emitters_hard) return 1;
	return 0;
}

/* ============================================================
 * Prefabs
 * ============================================================ */

s32 forgePrefabCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < FORGE_MAX_PREFABS; ++i) {
		if (s_prefabs[i].in_use) ++n;
	}
	return n;
}

forge_prefab_t *forgePrefabGet(s32 index)
{
	if (index < 0 || index >= FORGE_MAX_PREFABS) return NULL;
	return &s_prefabs[index];
}

forge_prefab_t *forgePrefabSaveFromSelection(const char *name)
{
	if (s_selection_count == 0) return NULL;
	for (s32 i = 0; i < FORGE_MAX_PREFABS; ++i) {
		if (!s_prefabs[i].in_use) {
			memset(&s_prefabs[i], 0, sizeof(s_prefabs[i]));
			forgeCopyStr(s_prefabs[i].name, name ? name : "Unnamed Prefab", FORGE_NAME_LEN);
			forgeCopyStr(s_prefabs[i].author, s_settings.author, FORGE_NAME_LEN);
			s_prefabs[i].in_use = 1;
			s_prefabs[i].num_objects = 0;
			/* snapshot centroid so instantiate can translate */
			f32 cx = 0, cy = 0, cz = 0;
			s32 count = 0;
			for (s32 j = 0; j < s_selection_count && count < 32; ++j) {
				forge_object_t *o = forgeObjectFindByUid(s_selection[j]);
				if (!o) continue;
				cx += o->pos[0]; cy += o->pos[1]; cz += o->pos[2];
				++count;
			}
			if (count > 0) { cx /= count; cy /= count; cz /= count; }
			s32 k = 0;
			for (s32 j = 0; j < s_selection_count && k < 32; ++j) {
				forge_object_t *o = forgeObjectFindByUid(s_selection[j]);
				if (!o) continue;
				s_prefabs[i].objects[k] = *o;
				s_prefabs[i].objects[k].pos[0] -= cx;
				s_prefabs[i].objects[k].pos[1] -= cy;
				s_prefabs[i].objects[k].pos[2] -= cz;
				s_prefabs[i].objects[k].uid = 0;  /* reassigned on instantiate */
				++k;
			}
			s_prefabs[i].num_objects = (u8)k;
			sysLogPrintf(LOG_NOTE, "GRID.PREFAB: saved '%s' with %d objects",
					s_prefabs[i].name, (s32)k);
			return &s_prefabs[i];
		}
	}
	sysLogPrintf(LOG_WARNING, "GRID: prefab pool full");
	return NULL;
}

/* ============================================================
 * S310 R3 -- dependency collection
 * ============================================================ */

static s32 forgeIdIsBase(const char *id)
{
	/* "base:" prefix => shipped with the base game, no dependency needed */
	return (id && id[0] == 'b' && id[1] == 'a' && id[2] == 's' && id[3] == 'e' && id[4] == ':') ? 1 : 0;
}

static s32 forgeIdIsUnique(const char *id, char out[][FORGE_ID_LEN], s32 n)
{
	for (s32 i = 0; i < n; ++i) {
		if (strcmp(out[i], id) == 0) return 0;
	}
	return 1;
}

static void forgeMaybeAddDep(const char *id, char out[][FORGE_ID_LEN], s32 *n, s32 max)
{
	if (!id || !*id) return;
	if (forgeIdIsBase(id)) return;
	if (*n >= max) return;
	if (!forgeIdIsUnique(id, out, *n)) return;
	forgeCopyStr(out[*n], id, FORGE_ID_LEN);
	++(*n);
}

s32 forgeCollectDependencies(char out[][FORGE_ID_LEN], s32 max)
{
	if (!out || max <= 0) return 0;
	s32 n = 0;

	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		forge_object_t *o = forgeObjectGet(i);
		if (!o || !o->in_use) continue;
		forgeMaybeAddDep(o->catalog_id,  out, &n, max);
		forgeMaybeAddDep(o->material_id, out, &n, max);
		switch (o->category) {
		case FORGE_CAT_WEAPON_PAD:
			forgeMaybeAddDep(o->props.weapon.weapon_id, out, &n, max);
			break;
		case FORGE_CAT_AI:
			forgeMaybeAddDep(o->props.ai.body_id,   out, &n, max);
			forgeMaybeAddDep(o->props.ai.head_id,   out, &n, max);
			forgeMaybeAddDep(o->props.ai.weapon_id, out, &n, max);
			break;
		case FORGE_CAT_EFFECT:
			forgeMaybeAddDep(o->props.effect.asset_id, out, &n, max);
			break;
		case FORGE_CAT_PICKUP:
			forgeMaybeAddDep(o->props.pickup.item_id, out, &n, max);
			break;
		case FORGE_CAT_ZONE:
			forgeMaybeAddDep(o->props.zone.sound_loop_id,  out, &n, max);
			forgeMaybeAddDep(o->props.zone.enter_sound_id, out, &n, max);
			break;
		case FORGE_CAT_INTERACTABLE:
			if (strncmp(o->catalog_id, "base:door_", 10) == 0) {
				forgeMaybeAddDep(o->props.door.key_id,     out, &n, max);
				forgeMaybeAddDep(o->props.door.open_sound, out, &n, max);
				forgeMaybeAddDep(o->props.door.close_sound,out, &n, max);
			}
			break;
		default:
			break;
		}
	}

	/* Atmosphere */
	forgeMaybeAddDep(s_atmosphere.sky_id, out, &n, max);

	/* Game type */
	forgeMaybeAddDep(s_gametype.starting_weapon, out, &n, max);
	for (s32 w = 0; w < s_gametype.num_waves && w < FORGE_MAX_WAVES; ++w) {
		if (!s_gametype.waves[w].in_use) continue;
		forgeMaybeAddDep(s_gametype.waves[w].enemy_catalog_id, out, &n, max);
	}

	/* Base stage is a dependency too if it's a mod stage. */
	forgeMaybeAddDep(s_settings.base_stage_id, out, &n, max);

	return n;
}

s32 forgePrefabInstantiate(s32 prefab_index, const f32 origin[3])
{
	if (prefab_index < 0 || prefab_index >= FORGE_MAX_PREFABS) return 0;
	if (!s_prefabs[prefab_index].in_use) return 0;
	forgeUndoPushBarrier("instantiate prefab");
	s32 placed = 0;
	forgeSelectionClear();
	for (s32 i = 0; i < s_prefabs[prefab_index].num_objects; ++i) {
		const forge_object_t *src = &s_prefabs[prefab_index].objects[i];
		forge_object_t *dst = forgeObjectAllocate((forge_category_t)src->category, src->catalog_id);
		if (!dst) break;
		u32 new_uid = dst->uid;
		*dst = *src;
		dst->uid = new_uid;
		dst->in_use = 1;
		dst->pos[0] += origin[0]; dst->pos[1] += origin[1]; dst->pos[2] += origin[2];
		forgeSelectionAdd(dst->uid);
		forgeUndoRecordPlace(dst->uid, dst);
		++placed;
	}
	return placed;
}
