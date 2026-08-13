/**
 * assetcatalog.h -- PC port asset catalog system
 *
 * String-keyed hash table for resolving game assets (maps, characters, skins,
 * weapons, etc.) by name. Replaces numeric array indexing with a flexible,
 * extensible catalog that supports both base game and mod content.
 *
 * Core features:
 * - FNV-1a hash table with linear probing (read-heavy, cache-friendly)
 * - CRC32 network identity for asset synchronization
 * - Registration API (catalogRegister*, convenience wrappers per type)
 * - Resolution API (catalogResolve*, catalogResolveByNetHash)
 * - Iteration API (by type, by category)
 * - Query API (has entry, is enabled, get skins for target)
 * - Dynamic growth (hash table, entry pool)
 * - Lifecycle (init, clear, clear mods)
 *
 * Usage:
 *   1. catalogInit() - once at startup, allocates hash table and entry pool
 *   2. catalogRegister*() - populate with assets
 *   3. catalogResolve() - lookup by ID
 *   4. catalogClear() - flush for full reload
 *   5. catalogClearMods() - remove only non-bundled assets
 *
 * Thread safety: Single-threaded. No locking. Call from main thread only.
 *
 * ADR-003 reference: context/ADR-003-asset-catalog-core.md
 */

#ifndef _IN_ASSETCATALOG_H
#define _IN_ASSETCATALOG_H

#include <PR/ultratypes.h>
#include "fs.h"
#include "assetprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Asset Source Descriptor (Direct File Access — Phase 2)
 * ========================================================================
 *
 * Every catalog entry carries a source descriptor that answers "where do
 * this asset's bytes come from?" The `primary` handle is the canonical
 * location (RomProvider for base ROM assets, FileProvider for mods loaded
 * from loose files). The `override` handle, if non-null, wins over
 * `primary` and is used to model mod overrides of base assets.
 *
 * Phase 2 populates `primary` at registration time and keeps
 * `override` unused for now. Future phases migrate callers to
 * `assetLoadToNew(entry->source.primary|override, ...)` and retire the
 * reverse-index intercept inside `romdataFileLoad`. */

#define ASSET_SRC_FLAG_NONE       0u
#define ASSET_SRC_FLAG_PINNED     (1u << 0) /* do not evict */
#define ASSET_SRC_FLAG_MOD_LOCKED (1u << 1) /* mod cannot be disabled */

typedef struct {
    asset_data_handle_t primary;   /* canonical source (never null for registered entries) */
    asset_data_handle_t override;  /* active override (null when none) */
    u32                 flags;     /* ASSET_SRC_FLAG_* bitmask */
} asset_source_t;

/* ========================================================================
 * Constants & Sizes
 * ======================================================================== */

#define CATALOG_ID_LEN       64     /* max length of asset ID string */
#define CATALOG_CATEGORY_LEN 64     /* max length of category string */

/* ========================================================================
 * Asset Type Enum
 * ======================================================================== */

typedef enum {
    ASSET_NONE = 0,
    ASSET_MAP,
    ASSET_CHARACTER,
    ASSET_SKIN,
    ASSET_BOT_VARIANT,
    ASSET_WEAPON,
    ASSET_TEXTURES,
    ASSET_SFX,
    ASSET_MUSIC,
    ASSET_PROP,
    ASSET_VEHICLE,
    ASSET_MISSION,
    ASSET_UI,
    ASSET_TOOL,
    ASSET_ARENA,
    ASSET_BODY,                /* MP body entry (base game g_MpBodies[] or mod) */
    ASSET_HEAD,                /* MP head entry (base game g_MpHeads[] or mod) */
    ASSET_ANIMATION,           /* animation set (body animation) */
    ASSET_TEXTURE,             /* individual texture entry (not a pack) */
    ASSET_GAMEMODE,            /* multiplayer or custom game rule set */
    ASSET_AUDIO,               /* audio entry: SFX, music, or voice */
    ASSET_HUD,                 /* HUD element (crosshair, ammo display, radar, etc.) */
    ASSET_EFFECT,              /* visual effect: shader tint, glow, particle, screen-space */
    ASSET_MODEL,               /* individual 3-D model (g_ModelStates[] entry, MODEL_* index) */
    ASSET_LANG,                /* language string bank (LANGBANK_* constant) */
    ASSET_BOT_PROFILE,         /* MP simulant profile entry (g_BotProfiles[] / mod) */
    ASSET_PROJECTILE,          /* graph-authored physical projectile behavior asset */
    ASSET_ENTITY,              /* graph-authored deployed/stuck behavior archetype asset */
    ASSET_MATERIAL,            /* reusable render/surface material asset */
    ASSET_FONT,                /* reusable UI/gameplay font face asset */
    ASSET_SCENARIO,            /* level/map content selected by arenas, missions, or modes */
    ASSET_THEME,               /* menu/UI theme bundle asset */
    ASSET_TYPE_COUNT
} asset_type_e;

/* ========================================================================
 * Asset Sub-type Constants
 * ======================================================================== */

/* Map mode flag constants for ext.map.mode (bitmask) */
#define MAP_MODE_MP   (1 << 0)   /* playable in multiplayer (Combat Simulator) */
#define MAP_MODE_SOLO (1 << 1)   /* playable in solo/campaign */
#define MAP_MODE_COOP (1 << 2)   /* playable in co-op */

/* Audio category constants for ext.audio.category */
#define AUDIO_CAT_SFX   0   /* sound effect */
#define AUDIO_CAT_MUSIC 1   /* music track */
#define AUDIO_CAT_VOICE 2   /* voice / dialogue */

/* HUD element type constants for ext.hud.element_type */
#define HUD_ELEM_CROSSHAIR 0   /* aiming reticle */
#define HUD_ELEM_AMMO      1   /* ammo counter */
#define HUD_ELEM_RADAR     2   /* proximity radar */
#define HUD_ELEM_HEALTH    3   /* health bar */

/* Effect type constants for ext.effect.effect_type */
#define EFFECT_TYPE_TINT        0   /* color tint applied to vertices */
#define EFFECT_TYPE_GLOW        1   /* emissive glow on object */
#define EFFECT_TYPE_SHIMMER     2   /* animated shimmer/sparkle */
#define EFFECT_TYPE_DARKEN      3   /* darken/shadow entire scene */
#define EFFECT_TYPE_SCREEN      4   /* full-screen post-process */
#define EFFECT_TYPE_PARTICLE    5   /* particle emitter attached to target */
#define EFFECT_TYPE_EXPLOSION   6   /* native explosion profile library */
#define EFFECT_TYPE_SPARK       7   /* native spark profile library */
#define EFFECT_TYPE_SMOKE       8   /* native smoke profile library */

/* Effect target constants for ext.effect.target */
#define EFFECT_TARGET_SCENE     0   /* applies to full rendered scene */
#define EFFECT_TARGET_PLAYER    1   /* applies to a specific player */
#define EFFECT_TARGET_CHR       2   /* applies to a character/bot */
#define EFFECT_TARGET_PROP      3   /* applies to a prop/object */
#define EFFECT_TARGET_WEAPON    4   /* applies to a weapon model */
#define EFFECT_TARGET_LEVEL     5   /* applies to all level geometry */
#define EFFECT_TARGET_CALLSITE  6   /* target/attachment are chosen by caller */
#define HUD_ELEM_TIMER     4   /* game timer */
#define HUD_ELEM_SCORE     5   /* score display */

/* ========================================================================
 * Asset Load State
 * ======================================================================== */

/**
 * Lifecycle state of an asset entry.
 *
 * REGISTERED  -- entry exists in catalog, not yet enabled or loaded
 * ENABLED     -- user/system has enabled the asset; eligible for loading
 * LOADED      -- asset data is resident in memory (loaded_data != NULL)
 * ACTIVE      -- asset is actively referenced by the running game
 *
 * Bundled (base game) assets are initialized at LOADED with
 * ref_count = ASSET_REF_BUNDLED and are never evicted.
 */
typedef enum {
    ASSET_STATE_REGISTERED = 0,
    ASSET_STATE_ENABLED,
    ASSET_STATE_LOADED,
    ASSET_STATE_ACTIVE
} asset_load_state_t;

typedef enum {
    ASSET_PAYLOAD_NONE = 0,
    ASSET_PAYLOAD_SYSMEM_BYTES,
    ASSET_PAYLOAD_STAGE_MODELDEF,
    ASSET_PAYLOAD_COLMESH,
    ASSET_PAYLOAD_ANIMATION_CLIP,
    ASSET_PAYLOAD_RUNTIME_ACTIVE
} asset_payload_kind_t;

/** Sentinel ref_count for bundled assets: never evicted from memory. */
#define ASSET_REF_BUNDLED 0x7FFFFFFF

/* B-254 (2026-04-25): Grid arena load mode.  Stored in
 * asset_entry.ext.arena.load_mode.  See the comment on that field. */
#define ARENA_LOADMODE_PLAYABLE 0
#define ARENA_LOADMODE_CANVAS   1

/* ========================================================================
 * Asset Entry Structure
 * ======================================================================== */

typedef struct asset_entry {
    /* Identity */
    char id[CATALOG_ID_LEN];           /* "gf64_bond", "base:joanna_dark" */
    u32  id_hash;                      /* FNV-1a of id (hash table slot) */
    u32  net_hash;                     /* CRC32 of id; internal cache/dedup key only (not wire/save/API identity) */

    /* Classification */
    asset_type_e type;                 /* ASSET_MAP, ASSET_CHARACTER, etc. */
    char category[CATALOG_CATEGORY_LEN]; /* mod id or "base" for ROM assets */

    /* Filesystem */
    char dirpath[FS_MAXPATH];          /* absolute path to component folder */
    char descriptor_path[FS_MAXPATH];  /* canonical editable source descriptor;
                                        * may use archive::member chains */

    /* Common metadata */
    f32  model_scale;                  /* from .ini (default 1.0) */
    s32  enabled;                      /* bool: user toggle (s32, not stdbool) */
    s32  temporary;                    /* bool: session-only download */
    s32  bundled;                      /* bool: shipped with game */

    /* Runtime binding */
    s32  runtime_index;                /* index in relevant runtime array */
                                       /* (g_Stages, g_HeadsAndBodies, etc.) */
    s16  mp_index;                     /* position in g_MpBodies[]/g_MpHeads[] */
                                       /* -1 if not in the mp selection table */

    /* Type-specific extension (union keeps entry size bounded) */
    union {
        struct {
            s32 stagenum;              /* logical stage ID (e.g. 0x5e) */
            s32 mode;                  /* mp, solo, coop (flags or bitmask) */
            char music_file[FS_MAXPATH];
        } map;
        struct {
            char body_id[CATALOG_ID_LEN];
            char head_id[CATALOG_ID_LEN];
            char display_name[64];
            char bodyfile[FS_MAXPATH];
            char headfile[FS_MAXPATH];
            char portrait_file[FS_MAXPATH];
        } character;
        struct {
            char target_id[CATALOG_ID_LEN];  /* soft reference to target char */
            char skin_file[FS_MAXPATH];       /* editable skin/material binding source */
            char texture_file[FS_MAXPATH];    /* appearance payload for this skin */
            char swatches_file[FS_MAXPATH];   /* editable color swatch source */
            char material_archive[FS_MAXPATH]; /* typed material dependency archive */
            char texture_archive[FS_MAXPATH];  /* typed texture dependency archive */
        } skin;
        struct {
            char base_type[32];        /* "NormalSim", "DarkSim", etc. */
            f32 accuracy;
            f32 reaction_time;
            f32 aggression;
        } bot_variant;
        struct {
            s32 stagenum;              /* logical stage ID this arena loads */
            char scenario_id[CATALOG_ID_LEN]; /* catalog ID for playable scenario */
            char scenario_archive[FS_MAXPATH]; /* embedded playable scenario source */
            u8  requirefeature;        /* unlock check (0 = always available) */
            s32 name_langid;           /* language string ID for display name */
            /* B-254 (2026-04-25): how a Grid arena entry loads its stage.
             *   ARENA_LOADMODE_PLAYABLE (0) -- normal MP arena: scripts /
             *      AI / NPCs / cutscenes run as authored. Default.
             *   ARENA_LOADMODE_CANVAS (1) -- SP campaign mission used as
             *      a build canvas: geometry + lighting + physical doors
             *      load, but mission scripts / chr spawns / cutscene
             *      intros / objective markers are suppressed. Set on
             *      campaign-class stagenums when registered as Grid
             *      arenas so the user can fly around without anything
             *      triggering / dying / cutscenes playing. */
            u8  load_mode;
        } arena;
        struct {
            s16 bodynum;               /* global body ID in g_HeadsAndBodies[] */
            s16 name_langid;           /* language string ID for display name */
            s16 headnum;               /* default head ID for this body */
            u8  requirefeature;        /* unlock check (0 = always available) */
            /* B-226 (2026-04-23): human-readable display name. Populated from
             * s_BaseBodies[].desc for bundled base bodies, or from mod body
             * manifest display_name for mod bodies. Empty string means "fall
             * back to langbank via name_langid". Used by mpGetBodyName when
             * the langid resolves to junk (some legacy bodies share langids
             * or point to unrelated UI strings). */
            char display_name[64];
            char mesh_archive[FS_MAXPATH]; /* body mesh typed dependency/source */
            char hand_archive[FS_MAXPATH]; /* optional first-person hand mesh */
            /* Issue 10 (2026-04-24): rig_class is the authoritative physical
             * compatibility key for body <-> head pairing. Two entries with
             * the same rig_class share a neck socket geometry and can be
             * bolted together without visual gaps. Empty string means "no
             * rig_class set" -- treated as incompatible with every head so
             * mis-authored data surfaces loudly. Canonical strings:
             *   "human_male_neck_standard"    -- most DEFAULT male bodies
             *   "human_female_neck_standard"  -- FEMALE + FEMALEGUARD merged
             *   "maian_tall_neck"             -- MAIAN-type bodies
             *   "cass_neck"                   -- Cassandra-specific rig
             *   "mrblonde_neck"               -- Mr Blonde-specific rig
             * Future subdivisions (e.g. splitting DEFAULT into neck-variant
             * sub-buckets) land as data edits here; no code changes needed. */
            char rig_class[32];
        } body;
        struct {
            s16 headnum;               /* global head ID in g_HeadsAndBodies[] */
            u8  requirefeature;        /* unlock check (0 = always available) */
            char mesh_archive[FS_MAXPATH]; /* head mesh typed dependency/source */
            /* Issue 10 (2026-04-24): see body.rig_class above. Same semantics
             * -- equality match with a body's rig_class = physically
             * compatible pair. */
            char rig_class[32];
        } head;
        struct {
            s32 weapon_id;             /* MPWEAPON_* slot, not runtime WEAPON_* */
            char name[64];             /* human-readable display name */
            char model_file[FS_MAXPATH];
            char behavior_graph[FS_MAXPATH];
            char primary_graph[FS_MAXPATH];
            char secondary_graph[FS_MAXPATH];
            char shared_context[FS_MAXPATH];
            char settings_file[FS_MAXPATH];
            char variables_file[FS_MAXPATH];
            char presentation_file[FS_MAXPATH];
            s32  dual_wieldable;       /* bool: can be dual-wielded */
            u8   requirefeature;       /* unlock check (0 = always available) */
            /* S484 F9 / Mike I.2 (2026-04-27): the legacy headline
             * fields `damage`, `fire_rate`, `ammo_type` were dropped
             * here. They had zero readers in the live tree and were
             * shadow values without a single source of truth.
             * Selectors that need damage / fire_rate / ammo type now
             * route through the catalog manager
             * (catalogManagerGetWeaponByIndex(weapon_num)->...) which
             * is the single source of truth. */
        } weapon;
        struct {
            char name[64];             /* human-readable display name */
            char model_file[FS_MAXPATH];
            char behavior_graph[FS_MAXPATH];
            char entity_ref[CATALOG_ID_LEN]; /* optional transition target */
        } projectile;
        struct {
            char name[64];             /* human-readable display name */
            char archetype[64];        /* armed_mine, autogun, sensor, etc. */
            char model_file[FS_MAXPATH];
            char behavior_graph[FS_MAXPATH];
        } entity;
        struct {
            s32 anim_id;               /* animation table index */
            char name[64];             /* human-readable display name */
            s32 frame_count;           /* number of frames (0 = unknown) */
            s32 bytes_per_frame;       /* native character-animation frame stride */
            s32 header_len;            /* native character-animation header bytes */
            s32 framelen;              /* native animtableentry framelen */
            s32 flags;                 /* native animtableentry flags */
            char target_body[64];      /* body type this animation targets (empty = generic) */
        } anim;
        struct {
            s32 texture_id;            /* texture table index */
            s32 width;                 /* width in pixels (0 = unknown) */
            s32 height;                /* height in pixels (0 = unknown) */
            s32 format;                /* texture format constant (0 = unknown) */
            char file_path[FS_MAXPATH];
        } texture;
        struct {
            s32 prop_type;             /* PROPTYPE_* constant */
            char name[64];             /* human-readable display name */
            char prop_file[FS_MAXPATH];
            char model_file[FS_MAXPATH];
            char behavior_graph[FS_MAXPATH];
            u32  flags;                /* prop flags bitmask */
            f32  health;               /* base health value (0 = indestructible) */
        } prop;
        struct {
            s32 mode_id;               /* MPSCENARIO_* constant */
            char name[64];             /* human-readable display name */
            char description[256];     /* longer description for UI */
            s32 min_players;           /* minimum players required */
            s32 max_players;           /* maximum players supported */
            s32 team_based;            /* bool: requires teams */
            u8  requirefeature;        /* unlock check (0 = always available) */
            char rules_file[FS_MAXPATH];
        } gamemode;
        struct {
            s32 stagenum;              /* logical stage ID this scenario content backs */
            s32 mode;                  /* mp, solo, coop (flags or bitmask) */
            s32 source_room_count;     /* canonical room domain, including room 0 */
            char scene_file[FS_MAXPATH]; /* DCC-openable runtime scene source */
            char collision_file[FS_MAXPATH]; /* optional collision override */
            char rooms_file[FS_MAXPATH]; /* compatibility rooms/geometry export */
            char portals_file[FS_MAXPATH];
            char pads_file[FS_MAXPATH];
            char spawns_file[FS_MAXPATH];
            char volumes_file[FS_MAXPATH];
            char objects_file[FS_MAXPATH];
            char setup_fields_file[FS_MAXPATH];
            char ai_lists_file[FS_MAXPATH];
            char objectives_file[FS_MAXPATH];
            char navigation_file[FS_MAXPATH];
            char navigation_waypoints_file[FS_MAXPATH];
            char navigation_waygroups_file[FS_MAXPATH];
            char navigation_covers_file[FS_MAXPATH];
            char navigation_paths_file[FS_MAXPATH];
            char level_graph_file[FS_MAXPATH];
        } scenario;
        struct {
            s32 sound_id;              /* SFX enum value or music track index */
            char name[64];             /* human-readable display name */
            s32 category;              /* AUDIO_CAT_SFX / AUDIO_CAT_MUSIC / AUDIO_CAT_VOICE */
            s32 duration_ms;           /* duration in milliseconds (0 = unknown) */
            char file_path[FS_MAXPATH];
            s32 has_keymap;            /* SFX/voice source carries native ALKeyMap fields */
            s32 key_min;
            s32 key_max;
            s32 key_base;
            s32 key_detune;
            s32 velocity_min;
            s32 velocity_max;
            s32 sample_pan;
            s32 sample_volume;
            s32 has_loop;
            u32 loop_start_samples;
            u32 loop_end_samples;
            u32 loop_count;
            s32 has_envelope;
            u32 attack_time_us;
            u32 decay_time_us;
            u32 release_time_us;
            s32 attack_volume;
            s32 decay_volume;
            char voice_actor[64];
            char voice_transcript[512];
            char voice_language[16];
            char voice_context[128];
            char subtitle_file[FS_MAXPATH];
            char fallback_locale[16];
            char locale_audio_files[6][FS_MAXPATH];
            /* MUSIC tracks only: solo stage whose best-time gates this track.
             * Mirrors g_MpTracks[].unlockstage. -1 = always unlocked
             * (mod tracks default here). SFX / VOICE entries leave at 0. */
            s16  unlockstage;
        } audio;
        struct {
            s32 hud_id;                /* HUD element ID */
            char name[64];             /* human-readable display name */
            s32 element_type;          /* HUD_ELEM_* constant */
            char texture_file[FS_MAXPATH];
            char layout_file[FS_MAXPATH];
        } hud;
        struct {
            char texture_file[FS_MAXPATH];    /* public image source, usually texture.png/tga */
            char layout_file[FS_MAXPATH];     /* optional structured layout source */
            char nineslice_file[FS_MAXPATH];  /* optional nine-slice metadata source */
            char texture_name[64];     /* authored atlas/chrome slot name */
            s32 width;
            s32 height;
            s32 data_size;
            s32 nineslice_left;
            s32 nineslice_right;
            s32 nineslice_top;
            s32 nineslice_bottom;
            char nineslice_edge_mode[16];
            char nineslice_center_mode[16];
        } ui;
        struct {
            char name[64];             /* human-readable display name */
            s32 effect_type;           /* EFFECT_TYPE_* constant */
            s32 target;                /* EFFECT_TARGET_* constant */
            char effect_file[FS_MAXPATH];
            char timeline_file[FS_MAXPATH];
            char shader_id[64];        /* shader identifier for the renderer */
            f32 intensity;             /* effect strength 0.0-1.0 */
            f32 params[4];             /* generic effect parameters */
        } effect;
        struct {
            char material_file[FS_MAXPATH];
            char texture_archive[FS_MAXPATH]; /* embedded texture dependency archive */
            char effect_archive[FS_MAXPATH];  /* optional embedded effect archive */
        } material;
        struct {
            char model_file[FS_MAXPATH];
            char physics_file[FS_MAXPATH];
            char behavior_graph[FS_MAXPATH];
        } vehicle;
        struct {
            char scenario_archive[FS_MAXPATH]; /* required scenario dependency */
            char objectives_file[FS_MAXPATH];
            char mission_graph_file[FS_MAXPATH];
        } mission;
        struct {
            char theme_file[FS_MAXPATH];
            char ui_archive[FS_MAXPATH];   /* optional UI chrome dependency */
            char font_archive[FS_MAXPATH]; /* optional font dependency */
            char audio_archive[FS_MAXPATH]; /* optional theme SFX dependency */
            char music_archive[FS_MAXPATH]; /* optional theme music dependency */
            char effect_archive[FS_MAXPATH]; /* optional theme effect dependency */
        } theme;
        struct {
            s32 bank_id;               /* LANGBANK_* constant (0x01-0x44) */
            char locale[16];           /* locale tag for this language source */
            char lang_category[32];    /* stage / mp_ui / system */
            u32 string_count;          /* authored strings.json row count */
            char strings_file[FS_MAXPATH];
        } lang;
        struct {
            char font_file[FS_MAXPATH];    /* vector font or bitmap glyph atlas */
            char metrics_file[FS_MAXPATH]; /* bitmap glyph metrics/kerning source */
        } font;
        struct {
            s32  type;                 /* BOTTYPE_* constant (e.g. BOTTYPE_GENERAL) */
            s32  difficulty;           /* BOTDIFF_* constant */
            s16  body;                 /* default MP body index (g_MpBodies[] position) */
            s16  name_langid;          /* langbank string ID for display name */
            u8   requirefeature;       /* unlock check (0 = always available) */
            char target_body[CATALOG_ID_LEN]; /* catalog body ID for authored profiles */
            char profile_file[FS_MAXPATH];
        } bot_profile;
    } ext;

    /* Source numeric IDs for reverse-index (C-4 through C-7).
     * Set during registration. -1 means "not applicable to this asset type".
     * Base game bundled entries carry the ROM index they occupy.
     * Mod entries carry the ROM index they override (so the intercept can
     * redirect that filenum to the mod's file path). */
    s32 source_filenum;    /* ROM fileSlots[] index, or -1 */
    s32 source_texnum;     /* ROM textures table index, or -1 */
    s32 source_animnum;    /* ROM animations table index, or -1 */
    s32 source_soundnum;   /* ROM sounds table index, or -1 */

    /* Asset Provider source descriptor (Direct File Access — Phase 2).
     * `source.primary` binds this entry's bytes to a provider — RomProvider
     * for base-game assets, FileProvider for mod assets served from loose
     * files. `source.override`, if non-null, takes precedence (mod
     * overriding a base asset). Populated by registration helpers and by
     * `catalogSetPrimary` / `catalogSetOverride`. */
    asset_source_t source;

    /* Load state tracking (MEM-1) */
    asset_load_state_t load_state;     /* lifecycle state of this entry */
    void              *loaded_data;    /* pointer to loaded asset data (NULL if not loaded) */
    u32                data_size_bytes;/* size of loaded_data in bytes (0 if not loaded) */
    asset_payload_kind_t payload_kind; /* ownership/activation policy for loaded_data */
    s32                ref_count;      /* reference count; ASSET_REF_BUNDLED = never evict */
    s32                stage_ref_count;/* refs acquired by stage-category ownership (0 or 1) */

    /* Catalog internals */
    s32 occupied;                      /* bool: hash table slot in use */
} asset_entry_t;

/* ========================================================================
 * Lifecycle API
 * ======================================================================== */

/**
 * Initialize the asset catalog.
 * Allocates hash table (2048 slots) and entry pool (512 entries).
 * Call once during startup, before any registration.
 * Safe to call multiple times (clears state, reallocs).
 */
void assetCatalogInit(void);

/**
 * Transactionally retire every typed payload/runtime adapter/dependency,
 * then clear all entries and reset the hash table. Catalog table allocations
 * are reused for the next population; family payload allocations are freed.
 * Call before a full catalog reload.
 */
void assetCatalogClear(void);

/**
 * Transactionally retire and remove all entries where bundled == false.
 * Bundled rows, dependency edges, and runtime adapters remain active.
 * Rehashes remaining entries. Increments generation counter.
 * Call when disabling/toggling mods (partial reload).
 */
void assetCatalogClearMods(void);

/**
 * Get the catalog generation counter.
 * Incremented on every catalog rebuild (clear, clearMods, refreshMods).
 * Consumers can cache a local generation and compare to detect stale data.
 */
u32 assetCatalogGetGeneration(void);

/**
 * Hot-reload mod catalog entries.
 * Clears all non-bundled entries, re-scans the mods directory,
 * rebuilds the hash table, and increments the generation counter.
 * If modsdir is NULL, only the clear + generation bump is performed.
 */
void catalogRefreshMods(const char *modsdir);

/**
 * Get total number of registered entries (base + mods).
 */
s32 assetCatalogGetCount(void);

/**
 * Get number of entries of a specific type.
 * Returns 0 if type is invalid or has no entries.
 */
s32 assetCatalogGetCountByType(asset_type_e type);

/**
 * Direct pool access by pool index.
 * Used by assetcatalog_load.c for reverse-index iteration.
 * Returns NULL if index is out of range or entry is not occupied.
 */
const asset_entry_t *assetCatalogGetByIndex(s32 index);

/**
 * Get the current high-water mark of the entry pool.
 * Callers should iterate [0, assetCatalogGetPoolSize()) with assetCatalogGetByIndex()
 * to visit all entries (skipping NULL returns for holes / unoccupied slots).
 */
s32 assetCatalogGetPoolSize(void);

/**
 * Mutable resolve by string ID.
 * Used exclusively by the lifecycle layer (assetcatalog_load.c) to update
 * loaded_data, data_size_bytes, and ref_count.  Callers must not modify
 * identity or classification fields (id, type, bundled, etc.).
 * Returns NULL if the entry is not found or the catalog is not initialised.
 */
asset_entry_t *assetCatalogGetMutable(const char *id);

/* Remove one catalog row by exact ID and rebuild the lookup table. This is
 * intentionally narrow: transactional compound-archive registration uses it
 * to roll back rows created earlier in the same failed commit. Existing rows
 * must never be passed here as part of rollback. Returns 1 when removed. */
s32 assetCatalogUnregister(const char *id);

/* Scanner-transaction rollback only. Removes a row that has not acquired a
 * payload or lifecycle reference without dependency preflight; the enclosing
 * transaction restores the exact dependency snapshot separately. */
s32 assetCatalogRollbackUnactivatedRegistration(const char *id);

/* ========================================================================
 * Registration API
 * ======================================================================== */

/**
 * Register a single asset with minimal fields.
 * Computes id_hash (FNV-1a) and net_hash (CRC32); net_hash is for internal
 * catalog use only — never treat it as wire/save/public API identity.
 * If ID already exists (by string match), overwrites it (last-write-wins).
 * Returns pointer to entry, or NULL on allocation failure.
 * Caller should set type-specific union fields via the entry pointer.
 */
asset_entry_t *assetCatalogRegister(const char *id, asset_type_e type);

/**
 * Register a map asset.
 * Convenience wrapper that calls catalogRegister() and sets ext.map fields.
 * Returns entry pointer or NULL.
 */
asset_entry_t *assetCatalogRegisterMap(const char *id, s32 stagenum,
                                        const char *dirpath);

/**
 * Register a character asset.
 * Convenience wrapper that sets ext.character fields.
 */
asset_entry_t *assetCatalogRegisterCharacter(const char *id,
                                             const char *bodyfile,
                                             const char *headfile);

/** Resolve the enabled top-level character assembler for an exact body/head
 * catalog-ID pair. Returns NULL when no authoritative .pdcharacter binding
 * exists; callers must not infer a character from numeric body/head slots. */
const asset_entry_t *assetCatalogFindCharacterByBodyHead(
    const char *body_id, const char *head_id);

/**
 * Register a skin asset.
 * Convenience wrapper that sets ext.skin.target_id.
 */
asset_entry_t *assetCatalogRegisterSkin(const char *id,
                                        const char *target_id);

/**
 * Register a bot variant asset.
 * Convenience wrapper that sets ext.bot_variant fields.
 */
asset_entry_t *assetCatalogRegisterBotVariant(const char *id,
                                              const char *base_type,
                                              f32 accuracy,
                                              f32 reaction_time,
                                              f32 aggression);

/**
 * Register an arena asset.
 * Convenience wrapper that sets ext.arena fields.
 * An arena is a stage reference used in the MP arena selection menu,
 * with an unlock requirement and a display name (language string ID).
 */
asset_entry_t *assetCatalogRegisterArena(const char *id, s32 stagenum,
                                          u8 requirefeature, s32 name_langid);

/**
 * Register a body asset.
 * Convenience wrapper that sets ext.body fields.
 * A body is an MP-selectable character body with a display name,
 * a default head, and an optional unlock requirement.
 */
asset_entry_t *assetCatalogRegisterBody(const char *id, s16 bodynum,
                                         s16 name_langid, s16 headnum,
                                         u8 requirefeature);

/**
 * B-226: set a catalog-backed display name for a body entry. Overrides the
 * langbank-backed name from mpGetBodyName() when the langid resolves to
 * junk (shared or unrelated UI strings). Empty string clears the override.
 */
void catalogSetBodyDisplayName(asset_entry_t *entry, const char *display_name);

/**
 * Issue 10 (2026-04-24): set the rig_class for a body entry. rig_class is
 * the authoritative physical compatibility key used by
 * catalogGetBodyValidHeadIds -- a head and body are compatible iff their
 * rig_class strings match exactly. Empty string clears it (body will have
 * no compatible heads). See ext.body.rig_class for canonical values.
 */
void catalogSetBodyRigClass(asset_entry_t *entry, const char *rig_class);

/**
 * Register a head asset.
 * Convenience wrapper that sets ext.head fields.
 * A head is an MP-selectable character head with an optional unlock requirement.
 */
asset_entry_t *assetCatalogRegisterHead(const char *id, s16 headnum,
                                         u8 requirefeature);

/**
 * Issue 10 (2026-04-24): set the rig_class for a head entry. See
 * catalogSetBodyRigClass above for semantics.
 */
void catalogSetHeadRigClass(asset_entry_t *entry, const char *rig_class);

/**
 * Register a weapon asset.
 * Convenience wrapper that sets ext.weapon fields.
 * weapon_id should be an MPWEAPON_* slot, not a runtime WEAPON_* enum.
 * model_file may be NULL/"" for base game entries.
 *
 * S484 F9 / Mike I.2 (2026-04-27): the legacy `damage` / `fire_rate` /
 * `ammo_type` shadow parameters were dropped. The catalog row no longer
 * carries headline gameplay numbers; selectors route through the
 * catalog manager (catalogManagerGetWeaponByIndex(weapon_num)->...)
 * for the single source of truth.
 */
asset_entry_t *assetCatalogRegisterWeapon(const char *id, s32 weapon_id,
                                           const char *name,
                                           const char *model_file,
                                           s32 dual_wieldable);

/**
 * Register an MP bot profile asset.
 * Convenience wrapper that sets ext.bot_profile fields.
 * `type` and `difficulty` are BOTTYPE_* / BOTDIFF_* constants.
 * `body` is the default MP body index (g_MpBodies[] position).
 * `name_langid` is the langbank string ID for the display name.
 * `requirefeature` is the MPFEATURE_* unlock gate (0 = always available).
 */
asset_entry_t *assetCatalogRegisterBotProfile(const char *id, s32 type,
                                              s32 difficulty, s16 body,
                                              s16 name_langid,
                                              u8 requirefeature);

/**
 * Register an animation asset.
 * Convenience wrapper that sets ext.anim fields.
 * anim_id is the index in the animation table (matches animations.json order).
 * target_body may be NULL or "" for generic animations.
 */
asset_entry_t *assetCatalogRegisterAnimation(const char *id, s32 anim_id,
                                              const char *name,
                                              s32 frame_count,
                                              const char *target_body);

/**
 * Register an individual texture asset.
 * Convenience wrapper that sets ext.texture fields.
 * Distinct from ASSET_TEXTURES (texture pack): this is one named texture.
 * width, height, format may be 0 for base game entries (loaded from ROM).
 */
asset_entry_t *assetCatalogRegisterTexture(const char *id, s32 texture_id,
                                            s32 width, s32 height, s32 format,
                                            const char *file_path);

/**
 * Register a prop asset.
 * Convenience wrapper that sets ext.prop fields.
 * prop_type should be a PROPTYPE_* constant.
 * model_file may be NULL/"" for base game entries.
 */
asset_entry_t *assetCatalogRegisterProp(const char *id, s32 prop_type,
                                         const char *name,
                                         const char *model_file,
                                         u32 flags, f32 health);

/**
 * Register a textures asset (texture pack / replacement set).
 * No type-specific fields — dirpath and category are sufficient.
 */
asset_entry_t *assetCatalogRegisterTextures(const char *id);

/**
 * Register an SFX asset (sound effect pack).
 * No type-specific fields — dirpath and category are sufficient.
 */
asset_entry_t *assetCatalogRegisterSfx(const char *id);

/**
 * Register a game mode asset.
 * Convenience wrapper that sets ext.gamemode fields.
 * mode_id should be an MPSCENARIO_* constant.
 * description may be NULL/"" for terse entries.
 */
asset_entry_t *assetCatalogRegisterGameMode(const char *id, s32 mode_id,
                                             const char *name,
                                             const char *description,
                                             s32 min_players, s32 max_players,
                                             s32 team_based);

/**
 * Register an audio asset.
 * Convenience wrapper that sets ext.audio fields.
 * category: AUDIO_CAT_SFX, AUDIO_CAT_MUSIC, or AUDIO_CAT_VOICE.
 * file_path may be NULL/"" for ROM-embedded sounds.
 */
asset_entry_t *assetCatalogRegisterAudio(const char *id, s32 sound_id,
                                          const char *name, s32 category,
                                          s32 duration_ms,
                                          const char *file_path);

/**
 * Register a HUD element asset.
 * Convenience wrapper that sets ext.hud fields.
 * element_type: HUD_ELEM_CROSSHAIR, HUD_ELEM_AMMO, etc.
 * texture_file may be NULL/"" for elements using the default renderer.
 * Layout-aware scanners/base registration assign the primary source.
 */
asset_entry_t *assetCatalogRegisterHud(const char *id, s32 hud_id,
                                        const char *name, s32 element_type,
                                        const char *texture_file);

/* ========================================================================
 * Resolution API
 * ======================================================================== */

/**
 * Resolve an asset by string ID.
 * Computes FNV-1a hash, probes hash table, verifies full string match.
 * Returns const pointer to entry, or NULL if not found or not enabled.
 * Pointer valid until next catalogClear() or pool realloc.
 */
const asset_entry_t *assetCatalogResolve(const char *id);

/**
 * Resolve and return the runtime_index for a character asset.
 * Returns runtime_index if found, or -1 if not found / not enabled.
 */
s32 assetCatalogResolveBodyIndex(const char *id);

/**
 * Resolve and return the runtime_index for a map asset.
 * Returns runtime_index if found, or -1 if not found / not enabled.
 */
s32 assetCatalogResolveStageIndex(const char *id);

/**
 * Resolve an asset by internal CRC32 net_hash (catalog cache key).
 * Linear scan of entry pool (infrequent, connection-time / manifest checks only).
 * Returns const pointer to first entry with matching net_hash, or NULL.
 */
const asset_entry_t *assetCatalogResolveByNetHash(u32 net_hash);

/* ========================================================================
 * Iteration API
 * ======================================================================== */

/**
 * Callback signature for iteration functions.
 * Called once per matching entry. entry pointer valid during callback only.
 */
typedef void (*asset_iter_fn)(const asset_entry_t *entry, void *userdata);

/**
 * Iterate ENABLED entries of a specific asset type.
 * Calls fn for each entry where occupied, enabled, and type matches.
 * Disabled entries (entry->enabled == 0, set via assetCatalogSetEnabled)
 * are skipped per B-303 (catalog universality sweep, 2026-05-01) so that
 * mod toggles surface in selectors immediately.  This mirrors the
 * resolve-by-ID semantic which has always returned NULL for disabled
 * entries.
 *
 * Modder UIs that need to LIST disabled entries (Mod Manager, Modding
 * Hub, Audio Mod author) must call assetCatalogIterateByTypeIncludingDisabled
 * below.
 */
void assetCatalogIterateByType(asset_type_e type, asset_iter_fn fn,
                                void *userdata);

/**
 * Variant of assetCatalogIterateByType that includes disabled entries.
 * Use ONLY in modder UIs (Mod Manager, Modding Hub, Audio Mod author)
 * that need to render the disabled state for re-enable.  All gameplay
 * selectors call assetCatalogIterateByType.
 */
void assetCatalogIterateByTypeIncludingDisabled(asset_type_e type,
                                                 asset_iter_fn fn,
                                                 void *userdata);

/**
 * Iterate all ENABLED entries with a specific category string.
 * Same enabled-filter discipline as assetCatalogIterateByType (B-303).
 * Category matching is exact (case-sensitive).
 */
void assetCatalogIterateByCategory(const char *category, asset_iter_fn fn,
                                    void *userdata);

/**
 * Iterate ENABLED entries of a specific asset type that are AVAILABLE to
 * the local player right now: catalog membership intersected with both
 * the enabled flag and the unlock-state.  The unlock filter consults
 * `challengeIsFeatureUnlocked` against whichever `requirefeature` field
 * the type carries on its `ext` payload:
 *   ASSET_ARENA       -> ext.arena.requirefeature
 *   ASSET_BODY        -> ext.body.requirefeature
 *   ASSET_HEAD        -> ext.head.requirefeature
 *   ASSET_WEAPON      -> ext.weapon.requirefeature
 *   ASSET_GAMEMODE    -> ext.gamemode.requirefeature
 *   ASSET_BOT_PROFILE -> ext.bot_profile.requirefeature
 *   any other type    -> no unlock gate (iterates identically to
 *                        assetCatalogIterateByType modulo the unlock
 *                        check; the enabled filter still applies)
 *
 * This is the canonical helper for selector pools. Per Mike's directive
 * "selector pool = catalog INTERSECT unlock-state": every UI that builds
 * a pickable list of arenas / bodies / heads / weapons / gamemodes /
 * bot profiles should call this rather than iterating the full catalog
 * and filtering inline.  After B-303 the helper also respects
 * `entry->enabled`, so a mod-toggled-off entry never reaches selectors.
 *
 * Server build: returns immediately because `assetCatalogRegisterBaseGame`
 * is not called server-side, so no entries exist to iterate.
 */
void assetCatalogIterateUnlockedByType(asset_type_e type, asset_iter_fn fn,
                                        void *userdata);

/**
 * Count entries of a specific asset type that pass the unlock filter (same
 * predicate as assetCatalogIterateUnlockedByType).  O(N) over the full
 * catalog pool; use sparingly (cache the result for per-frame UI sizing).
 */
s32 assetCatalogGetUnlockedCountByType(asset_type_e type);

/**
 * Iterate ASSET_AUDIO entries with category == AUDIO_CAT_MUSIC that are
 * unlocked for the current player.  The unlock semantic for music tracks
 * is best-time-based, NOT challenge-feature-based: a track is unlocked
 * iff `ext.audio.unlockstage < 0`, out of campaign range, or the player
 * has any best-time on `g_GameFile.besttimes[unlockstage]`.  Mirrors the
 * legacy `mpIsTrackUnlocked` predicate (src/game/mplayer/mplayer.c).
 *
 * Mod tracks register with `unlockstage = -1` and are always emitted.
 *
 * Server build: zero entries (no `assetCatalogRegisterBaseGame` and no
 * `g_GameFile`); the iterator still compiles and is a no-op.
 */
void assetCatalogIterateUnlockedMusic(asset_iter_fn fn, void *userdata);

/**
 * Count of unlocked music tracks (same predicate as
 * assetCatalogIterateUnlockedMusic).
 */
s32 assetCatalogGetUnlockedMusicCount(void);

/* ========================================================================
 * Query API
 * ======================================================================== */

/**
 * Check if an asset is registered (enabled or not).
 */
s32 assetCatalogHasEntry(const char *id);

/**
 * Check if an asset is registered and enabled.
 */
s32 assetCatalogIsEnabled(const char *id);

/**
 * Get all skin assets that target a specific character.
 * Iterates catalog looking for ASSET_SKIN entries with ext.skin.target_id
 * matching the given target_id.
 * Fills out[] with up to maxout entry pointers.
 * Returns number of skins found (may be 0).
 */
s32 assetCatalogGetSkinsForTarget(const char *target_id,
                                   const asset_entry_t **out, s32 maxout);

/* ========================================================================
 * Write API (D3R-6)
 * ======================================================================== */

/**
 * Set the enabled state of an asset entry by string ID. Disable preflights and
 * retires the complete typed closure outside the catalog mutex; preflight or
 * teardown failure leaves/restores the prior enabled state.
 * Does nothing if the ID is not found or the catalog is not initialized.
 * This is the only write operation exposed outside the catalog internals.
 * Note: base game (bundled) entries can be disabled via this call for
 * temporary UI purposes, but they re-enable on catalog reset/reload.
 */
void assetCatalogSetEnabled(const char *id, s32 enabled);

/* Engine Phase 4 (2026-05-03): set entry->category by ID under the
 * catalog mutex.  Walker callbacks (loader_walker_anim.c, _font.c,
 * _lang.c) need to fill the category field after registration; doing
 * so via the entry pointer they just received races with concurrent
 * pool reallocs from other workers, so they re-resolve under-lock via
 * this helper instead.  No-op if ID is unknown or category is NULL. */
void assetCatalogSetCategoryById(const char *id, const char *category);

/* ========================================================================
 * Asset Source API (Direct File Access — Phase 2)
 * ========================================================================
 *
 * These helpers bind an `asset_source_t` to a catalog entry. `primary` is
 * the canonical bytes (RomProvider or FileProvider); `override` replaces
 * `primary` at load time when non-null (mod overriding a base asset).
 *
 * All functions are no-ops if `entry` is NULL. Passing a null handle to
 * `catalogSetPrimary` is a logic error (primary must always resolve); a
 * null handle to `catalogSetOverride` is equivalent to `catalogClearOverride`.
 */

void catalogSetPrimary(asset_entry_t *entry, asset_data_handle_t handle);
void catalogSetPrimaryFile(asset_entry_t *entry, const char *path);
asset_data_handle_t catalogHandleForSourceFile(const char *path);
void catalogSetPrimaryRomFilenum(asset_entry_t *entry, s32 filenum);
void catalogSetOverride(asset_entry_t *entry, asset_data_handle_t handle);
void catalogClearOverride(asset_entry_t *entry);

/* Phase 3 Pass B (2026-05-02): convenience binder for every Phase 3
 * slice that migrates a base-game ASSET_* entry from RomProvider to
 * FileProvider.
 *
 * Behaviour:
 *   1. Compute the canonical extracted-file relative path for the
 *      given ROM filenum via romExtractRelPathForFilenum.
 *   2. Probe whether the file actually exists on disk now.
 *   3. If yes: bind FileProvider via catalogSetPrimaryFile.
 *   4. If no:  fall back to RomProvider via catalogSetPrimaryRomFilenum
 *      (defensive -- pre-A.2 boot or server build with NULL g_RomFile).
 *
 * Pass A.4 self-heal guarantees the file exists with valid SHA-256
 * by the time gameplay loads run, so the FileProvider branch wins
 * on every steady-state boot.  RomProvider fallback is only used
 * during the transient pre-A.2 cohort or in server builds.
 */
void catalogBindPrimaryFromDiskOrRom(asset_entry_t *entry, s32 filenum);

/**
 * Effective load source for an entry — `override` if non-null, otherwise
 * `primary`. Returns a null handle if `entry` is NULL or both fields
 * are null.
 */
asset_data_handle_t catalogEffectiveHandle(const asset_entry_t *entry);

/**
 * Enumerate unique category strings across all registered entries.
 * Fills out[][CATALOG_CATEGORY_LEN] with up to maxout distinct strings.
 * Skips the "base" category and empty categories (not user-manageable).
 * Returns number of unique categories found (may be 0 if no mod entries).
 *
 * Typical usage: build the "By Mod" tree in the Mod Manager UI.
 */
s32 assetCatalogGetUniqueCategories(char out[][CATALOG_CATEGORY_LEN], s32 maxout);

/* ========================================================================
 * Load State API (MEM-1)
 * ======================================================================== */

/**
 * Get the current load state of an asset entry by string ID.
 * Returns ASSET_STATE_REGISTERED if the ID is not found.
 */
asset_load_state_t assetCatalogGetLoadState(const char *id);

/**
 * Set the load state of an asset entry by string ID.
 * Does nothing if the ID is not found or the catalog is not initialized.
 * Callers should use this to advance an entry through the lifecycle
 * (ENABLED → LOADED → ACTIVE) as asset data is managed.
 * Note: bundled entries have ref_count = ASSET_REF_BUNDLED; callers must
 * not decrement below that sentinel or force eviction of bundled data.
 */
void assetCatalogSetLoadState(const char *id, asset_load_state_t state);

/* ========================================================================
 * SA-2: Modular Catalog API Layer
 * ======================================================================== */

/* Forward declaration for wire helper signatures (defined in net/netbuf.h). */
struct netbuf;

/**
 * Result struct for body asset resolution.
 * filenum is populated from source_filenum (set at registration from
 * g_HeadsAndBodies[bodynum].filenum).
 * display_name points into the catalog entry id[] -- stable for catalog lifetime.
 * session_id is 0 if the session catalog is not active or entry is absent.
 */
typedef struct {
    const asset_entry_t *entry;        /**< full catalog entry (NULL on failure) */
    s32                  filenum;      /**< runtime filenum for model load calls */
    asset_data_handle_t  handle;       /**< provider-ready source handle */
    f32                  model_scale;  /**< from catalog entry (default 1.0) */
    const char          *display_name; /**< points to entry->id */
    u32                  net_hash;     /**< internal CRC32; not wire/save identity */
    u16                  session_id;   /**< session wire ID (0 = not in session) */
} catalog_body_result_t;

/** Heads share the same result layout as bodies. */
typedef catalog_body_result_t catalog_head_result_t;

/**
 * Result struct for stage (map) asset resolution.
 * bgfileid/padsfileid/setupfileid/mpsetupfileid/tilefileid come from
 * g_Stages[runtime_index] when the stage table is loaded (client);
 * all -1 on server or unloaded stages.
 *
 * Phase 4 — handle fields: each *_handle field is the provider-ready
 * load source for the corresponding file.  Populated internally from the
 * fileid using romProviderHandle().  On the dedicated server all handles
 * are null (server never loads stage geometry/pads/tiles).
 * Callers must use assetLoadToNew(stage.<x>_handle, ...) instead of
 * assetLoadToNew(romProviderHandle((s32)stage.<x>fileid), ...).
 */
typedef struct {
    const asset_entry_t *entry;
    s32                  bgfileid;
    s32                  padsfileid;
    s32                  setupfileid;
    s32                  mpsetupfileid; /**< multiplayer setup file id (-1 if not applicable) */
    s32                  tilefileid;    /**< tile file id (-1 if not applicable) */
    s32                  stagenum;    /**< logical stage ID (e.g. 0x5e) */
    u32                  net_hash;      /**< internal CRC32; not wire/save identity */
    u16                  session_id;
    /* Phase 4: provider handles — use these instead of romProviderHandle(fileid). */
    asset_data_handle_t  bg_handle;
    asset_data_handle_t  pads_handle;
    asset_data_handle_t  setup_handle;
    asset_data_handle_t  mpsetup_handle;
    asset_data_handle_t  tile_handle;
} catalog_stage_result_t;

/** Result struct for weapon asset resolution. */
typedef struct {
    const asset_entry_t *entry;
    s32                  filenum;     /**< weapon model file (source_filenum, -1 for base) */
    asset_data_handle_t  handle;      /**< provider-ready source handle */
    s32                  weapon_num;  /**< runtime WEAPON_* enum value */
    s32                  mp_weapon_id;/**< MPWEAPON_* slot used by g_MpWeapons[] */
    u32                  net_hash;    /**< internal CRC32; not wire/save identity */
    u16                  session_id;
} catalog_weapon_result_t;

/** Result struct for modelnum asset resolution. */
typedef struct {
    const asset_entry_t *entry;
    s32                  filenum;    /**< model file (source_filenum, -1 for base) */
    asset_data_handle_t  handle;     /**< provider-ready source handle */
    s32                  modelnum;   /**< runtime MODEL_* / g_ModelStates[] index */
    u32                  net_hash;   /**< internal CRC32; not wire/save identity */
    u16                  session_id;
} catalog_model_result_t;

/** Result struct for prop asset resolution. */
typedef struct {
    const asset_entry_t *entry;
    s32                  filenum;    /**< prop model file (source_filenum, -1 for base) */
    asset_data_handle_t  handle;     /**< provider-ready source handle */
    s32                  prop_type;  /**< runtime PROPTYPE_* value */
    u32                  net_hash;   /**< internal CRC32; not wire/save identity */
    u16                  session_id;
} catalog_prop_result_t;

/** Result struct for audio asset resolution (Batch A-1). */
typedef struct {
    const asset_entry_t *entry;
    s32                  sound_id;   /**< MUSIC_* enum for music, SFX index for SFX */
    s32                  category;   /**< AUDIO_CAT_SFX / AUDIO_CAT_MUSIC / AUDIO_CAT_VOICE */
    const char          *file_path;  /**< selected public source, empty for ROM */
    const char          *voice_actor;
    const char          *voice_transcript;
    const char          *voice_language;
    const char          *voice_context;
    char                 file_path_storage[FS_MAXPATH];
    char                 voice_actor_storage[64];
    char                 voice_transcript_storage[512];
    char                 voice_language_storage[16];
    char                 voice_context_storage[128];
} catalog_audio_result_t;

/* ── SA-2: Resolution by catalog string ID ─────────────────────────────── */

/** Resolve a body asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveBody(const char *id, catalog_body_result_t *out);

/** Resolve a head asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveHead(const char *id, catalog_head_result_t *out);

/** Resolve a stage (map) asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveStage(const char *id, catalog_stage_result_t *out);

/** Resolve a weapon asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveWeapon(const char *id, catalog_weapon_result_t *out);

/** Resolve a model asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveModel(const char *id, catalog_model_result_t *out);

/** Resolve a prop asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveProp(const char *id, catalog_prop_result_t *out);

/** Resolve an audio asset by catalog string ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveAudio(const char *id, catalog_audio_result_t *out);

/* ── SA-2: Resolution by session wire ID ───────────────────────────────── */

/** Resolve a body asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveBodyBySession(u16 session_id, catalog_body_result_t *out);

/** Resolve a head asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveHeadBySession(u16 session_id, catalog_head_result_t *out);

/** Resolve a stage (map) asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveStageBySession(u16 session_id, catalog_stage_result_t *out);

/** Resolve a weapon asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolveWeaponBySession(u16 session_id, catalog_weapon_result_t *out);

/** Resolve a prop asset by session wire ID. Returns 1 on success, 0 on failure. */
s32 catalogResolvePropBySession(u16 session_id, catalog_prop_result_t *out);

/* ── SA-2: Resolution by internal CRC32 net_hash (cache key, not public ID) ─ */

/**
 * Resolve an asset entry by internal net_hash (CRC32 of catalog id string).
 * Thin wrapper around assetCatalogResolveByNetHash() -- O(n) linear scan.
 * Use sparingly (manifest checks, connection-time only). Do not expose net_hash
 * as wire/save/public API identity; catalog IDs belong at those boundaries.
 */
const asset_entry_t *catalogResolveByNetHash(u32 net_hash);

/* ── SA-4: Reverse-index lookup (migration only) ───────────────────────── */

/* ── Phase 8: cached runtime lookups ────────────────────────────────────
 * Built once by catalogBuildRuntimeCaches() after catalog population.
 * Integer-to-string resolution is cached for normal runtime use; body/head
 * selector helpers may scan mp_index as a late-rebuild fallback. */

/**
 * Build all runtime↔catalog-ID caches (mp body/head, stage, weapon, model).
 * Call once after assetCatalogRegisterBaseGame() + component scanning.
 * Also populates entry->mp_index on each body/head asset_entry_t.
 */
void catalogBuildRuntimeCaches(void);

/**
 * Cached mp body table position → catalog ID string.
 * Returns NULL if mp_idx is out of range or has no registered catalog entry.
 */
const char *catalogMpBodyId(s32 mp_idx);

/**
 * Cached mp head table position → catalog ID string.
 * Returns NULL if mp_idx is out of range or has no registered catalog entry.
 */
const char *catalogMpHeadId(s32 mp_idx);

/**
 * O(1) cached lookup: (asset_type, runtime_index) → catalog ID string.
 * Covers all asset types (MAP, ARENA, WEAPON, MODEL, BODY, HEAD, etc.).
 * Returns NULL if not found or runtime_index is out of cache range.
 */
const char *catalogIdByRuntime(asset_type_e type, s32 runtime_index);

/**
 * Explicit stage identity helpers. Stages have three legacy integer spaces:
 * stage table index (g_Stages[] / catalog runtime_index), solo stage index
 * (g_SoloStages[] / besttimes[]), and stagenum (logical STAGE_* id passed to
 * mainChangeToStage). Use these instead of catalogIdByRuntime(ASSET_MAP,...)
 * where the integer space matters.
 */
const char *catalogStageIdByStageTableIndex(s32 stage_table_index);
const char *catalogStageIdBySoloStageIndex(s32 solo_stage_index);
const char *catalogStageIdByStagenum(s32 stagenum);

/**
 * Explicit game-mode identity helper. The scenario index is an MPSCENARIO_*
 * value / ext.gamemode.mode_id, not a generic catalog pool index.
 */
const char *catalogGameModeIdByScenarioIndex(s32 scenario_index);

/**
 * Explicit weapon identity helpers. Weapons have two legacy integer spaces:
 * MPWEAPON_* slots for multiplayer setup and WEAPON_* runtime enums for
 * gameplay/inventory. Use these instead of catalogIdByRuntime(ASSET_WEAPON,...)
 * where the integer space matters.
 */
const char *catalogWeaponIdByRuntimeWeaponNum(s32 weapon_num);
const char *catalogWeaponIdByMpWeaponId(s32 mp_weapon_id);

/**
 * Explicit model/body/head identity helpers. These are runtime engine indices,
 * not MP selector positions. MP body/head selector positions still use
 * catalogMpBodyId() / catalogMpHeadId().
 */
const char *catalogModelIdByModelnum(s32 modelnum);
const char *catalogBodyIdByBodynum(s32 bodynum);
const char *catalogHeadIdByHeadnum(s32 headnum);

/**
 * Source identity helpers. These are only for catalog/provider internals and
 * migration bridges that need to resolve a source back to the owning catalog
 * entry. Public boundaries should use catalog ID strings directly.
 */
const char *catalogIdBySourceFilenum(asset_type_e type, s32 source_filenum);
const char *catalogIdBySourceHandle(asset_type_e type, asset_data_handle_t handle);
asset_data_handle_t catalogHandleBySourceFilenum(asset_type_e type, s32 source_filenum);

/**
 * Legacy model-source bridge: resolve a provider handle from a ROM model
 * source filenum when the caller has not yet been migrated to a typed catalog
 * identity. If preferred_type is not ASSET_NONE, that type is tried first;
 * the catalog then owns the remaining model-source fallback order.
 */
asset_data_handle_t catalogHandleByModelSourceFilenum(asset_type_e preferred_type,
		s32 source_filenum);

/**
 * Body → default head catalog ID string.
 * Reads ext.body.headnum from the body catalog entry and resolves it to the
 * matching ASSET_HEAD catalog ID.  Used by character pickers to auto-select the
 * correct head when a body is chosen.
 * Returns NULL if body_id is unknown, wrong type, or headnum < 0.
 */
const char *catalogGetBodyDefaultHead(const char *body_id);

/**
 * Body -> default head mpheadnum (g_MpHeads[] position).
 * Convenience wrapper for UI carousels that work in mpheadnum space.
 * Returns -1 if the body is not found, has no default head, or the sentinel
 * value 1000 (random-gender head) is stored - caller should keep existing head.
 */
s32 catalogGetBodyDefaultMpHeadIdx(s32 mpbodynum);

/**
 * B-226: Body mp_index -> catalog-stored display name.
 * Returns a pointer into the catalog entry's internal buffer (stable until
 * catalog reload). Returns NULL if no display_name was set at registration
 * (caller should fall back to langGet(body->name)). Use this to override
 * stale / junk langbank entries for e.g. the 4 Bond-actor bodies that all
 * share L_OPTIONS_070 ("Dinner Jacket") and the SKEDAR/DRCAROLL bodies
 * whose langids point to unrelated UI strings.
 */
const char *catalogGetBodyDisplayName(s32 mpbodynum);

/* ── SA-5 failure state ─────────────────────────────────────────────────── */

/**
 * Set to 1 by any catalogGet*ByIndex helper when the required asset is absent
 * from the catalog.  Callers on the load path should check this after loading
 * a stage or character to detect pipeline initialization failures.
 * Reset by the caller (or catalog reload) before the next load sequence.
 */
extern s32  g_CatalogFailure;

/**
 * Human-readable description of the first catalog miss.
 * Valid only when g_CatalogFailure == 1.  Populated by snprintf.
 */
extern char g_CatalogFailureMsg[256];

/**
 * Gate 5 (c3844) catalog health checkpoint consumer for g_CatalogFailure.
 * Call at a safe phase boundary (stage load entry). Emits a single
 * checkpoint-tagged CATALOG.HEALTH report if a miss is pending, hard-fails
 * under source-only enforcement, and always clears the flag. Returns 1 if
 * healthy, 0 if a miss was pending and consumed.
 */
s32 catalogAssertHealthy(const char *checkpoint);

/** Clear g_CatalogFailure + g_CatalogFailureMsg without reporting. */
void catalogClearHealth(void);

/* ── SA-5a: Load-site helpers ───────────────────────────────────────────── */

/* ── Phase 4: Handle-based load accessors ───────────────────────────────────
 *
 * These replace the pattern:
 *   assetLoadToNew(romProviderHandle(catalogGetBodyFilenumByIndex(n)), ...)
 * with:
 *   assetLoadToNew(catalogGetBodyHandle(n), ...)
 *
 * Returns the effective provider handle for the asset (override wins over
 * primary; null handle on catalog miss or server build).  Safe to call with
 * a null out from assetLoadToNew — it returns NULL on a null handle.
 * O(1) after cache build.
 */

/** Phase 4: Effective provider handle for a body by runtime body index. */
asset_data_handle_t catalogGetBodyHandle(s32 bodynum);

/** Phase 4: Effective provider handle for a head by runtime head index. */
asset_data_handle_t catalogGetHeadHandle(s32 headnum);

/** Phase 4: Effective provider handle for a prop model by MODEL_* index. */
asset_data_handle_t catalogGetPropHandle(s32 propnum);

/* ── SA-5a: [MIGRATION BRIDGE] filenum accessors ────────────────────────────
 *
 * DEPRECATED — Phase 4.  These functions return a raw ROM filenum, which is
 * still required by legacy game APIs (fileGetInflatedSize, fileGetLoadedSize,
 * modeldefLoad) that have not yet been migrated to handle-based equivalents.
 * Do NOT use for new code.  Migrate callers to catalogGetBody/Head/PropHandle
 * once the legacy APIs are updated.
 */

/**
 * [DEPRECATED] Resolve a body model filenum by runtime body index.
 * Mod-override-aware.  O(n) scan.  On miss: logs CATALOG-FATAL, sets
 * g_CatalogFailure, returns 0.  Prefer catalogGetBodyHandle().
 */
s32 catalogGetBodyFilenumByIndex(s32 bodynum);

/**
 * [DEPRECATED] Resolve a head model filenum by runtime head index.
 * Same contract as catalogGetBodyFilenumByIndex.  Prefer catalogGetHeadHandle().
 */
s32 catalogGetHeadFilenumByIndex(s32 headnum);

/**
 * SA-5a: Resolve a body model scale by runtime body index.
 * Mod-override-aware drop-in for g_HeadsAndBodies[bodynum].scale.
 * Returns catalog entry model_scale (default 1.0 for base game).
 * Mods that ship custom model scales will have their value respected here.
 * On catalog miss: logs [CATALOG-FATAL] and falls back to legacy scale.
 */
f32 catalogGetBodyScaleByIndex(s32 bodynum);

/**
 * SA-5b: Resolve all stage file IDs by runtime stage array index.
 * Mod-override-aware drop-in for g_Stages[stageindex].bgfileid / padsfileid /
 * setupfileid / mpsetupfileid / tilefileid at file load call sites.
 * Performs an O(n) catalog scan -- acceptable at load time (called once per
 * stage transition, not per frame).
 * Populates all file ID fields in *out from the catalog entry.
 * On catalog miss: logs [CATALOG-FATAL], sets g_CatalogFailure, returns 0
 * with *out zeroed.  No silent fallback to g_Stages[].
 * Returns 1 on success (catalog hit), 0 on catalog miss.
 */
s32 catalogGetStageResultByIndex(s32 stageindex, catalog_stage_result_t *out);

/** Phase 4: Resolve an ASSET_MODEL result by runtime MODEL_* / g_ModelStates[] index. */
s32 catalogResolveModelByModelnum(s32 modelnum, catalog_model_result_t *out);

/** Phase 4: Effective provider handle for a model by runtime MODEL_* index. */
asset_data_handle_t catalogGetModelHandle(s32 modelnum);

/**
 * B-936 A/B control (--debug-rom-modeldef): catalog-owned bridge that hands
 * out the ORIGINAL ROM modeldef handle for a fileid, only while the debug
 * flag is active (null handle otherwise). Exists so game code never calls
 * romProviderHandle() directly; not a normal-play load path.
 */
asset_data_handle_t catalogDebugRomModeldefHandle(s32 filenum);

/** [DEPRECATED] Prefer catalogResolveModelByModelnum() when the filenum is required. */
s32 catalogGetModelFilenumByModelnum(s32 modelnum);

/**
 * [DEPRECATED] SA-5c: Resolve a prop model filenum by runtime model array
 * index (MODEL_* enum).  Prefer catalogGetPropHandle().
 * O(n) scan.  On miss: logs CATALOG-FATAL, sets g_CatalogFailure, returns 0.
 */
s32 catalogGetPropFilenumByIndex(s32 propnum);

/* Stage/arena/weapon numeric-ID resolvers DELETED (Phase 7).
 * Callers use stage_id/weapon catalog IDs directly or inline
 * assetCatalogGetByIndex scans for the few remaining conversion sites. */

/* ── SA-5d: Body / head property accessors (M0.1e) ─────────────────────────
 * Thin wrappers over g_HeadsAndBodies[] that make the catalog the public
 * API for body/head property reads.  The ROM array is the current internal
 * implementation; future mod overrides will intercept here.
 * O(1).  All bounds-checked — return 0 / 1.0f on out-of-range index.
 *
 * bodynum / headnum are g_HeadsAndBodies[] indices (runtime_index on the
 * catalog entry).  They are NOT mp_index (g_MpBodies[]/g_MpHeads[] position).
 */

/** Boolean: is this body (g_HeadsAndBodies[] index) male? */
s32 catalogGetBodyIsMale(s32 bodynum);

/** Integer: character type constant (HEADBODYTYPE_*) for a body. */
s32 catalogGetBodyType(s32 bodynum);

/** Integer: height field used for bot speed calculation. */
s32 catalogGetBodyHeight(s32 bodynum);

/** Float: per-body animation scale factor. */
f32 catalogGetBodyAnimScale(s32 bodynum);

/** Boolean: can this body's height vary (human / Skedar variability)? */
s32 catalogGetBodyCanVaryHeight(s32 bodynum);

/**
 * Boolean: body model is self-contained (unk00_01 == 1 means no separate head
 * attachment slot).  Returns 1 for complete bodies, 0 for bodies that attach
 * a separate head model.
 */
s32 catalogGetBodyIsComplete(s32 bodynum);

/** Integer: hand model file number for first-person hand rendering. */
s32 catalogGetBodyHandFilenum(s32 bodynum);

/** Boolean: is this head (g_HeadsAndBodies[] index) male? */
s32 catalogGetHeadIsMale(s32 headnum);

/** Integer: character type constant (HEADBODYTYPE_*) for a head. */
s32 catalogGetHeadType(s32 headnum);

/**
 * P3 (2026-04-24): return every catalog head ID that is valid for the given
 * body_id.  Valid-pair rules follow modelcatalog.c::catalogIsHeadBodyCompatible:
 *
 *   - same HEADBODYTYPE_*                           -> valid
 *   - DEFAULT head with DEFAULT body                -> valid
 *   - FEMALE / FEMALEGUARD head with FEMALE /
 *     FEMALEGUARD body                              -> valid
 *
 * Gender filtering is baked into the HEADBODYTYPE_* values (FEMALE heads are
 * separate from DEFAULT heads), so the type match is sufficient to avoid
 * cross-gender pairs.
 *
 * Returns a pointer to an internal static array of const char* catalog IDs.
 * *out_count is set to the number of valid head IDs.  Returns NULL / 0 if the
 * body_id is unknown, wrong type, or the valid set is empty.  The returned
 * array's pointers are stable (catalog IDs never move); the array itself is
 * re-populated on every call, so callers must copy or iterate immediately.
 *
 * Deterministic-pair bodies (e.g. a unique character) will return a
 * single-element array.  Mod-authored bodies that declare the same
 * HEADBODYTYPE_* as an existing pool inherit the full pool automatically.
 *
 * AUDIT-24-M3 (2026-04-25) -- REENTRANCY CONTRACT:
 *   The returned pointer aliases the module-static `s_ValidHeadBuf` (256
 *   slots).  The next call to this function INVALIDATES the previous
 *   return -- both arrays alias the same buffer.  Callers iterating two
 *   bodies and remembering the first result will silently observe the
 *   second body's heads.  Pattern that breaks:
 *
 *     const char *const *a = catalogGetBodyValidHeadIds(body_a, &na);
 *     const char *const *b = catalogGetBodyValidHeadIds(body_b, &nb);
 *     // a and b now both point at body_b's heads -- bug.
 *
 *   Safe pattern: iterate and copy strings on each call before invoking
 *   again.  The single-threaded contract is also a non-thread contract
 *   (no concurrent calls).
 */
const char *const *catalogGetBodyValidHeadIds(const char *body_id,
                                              int *out_count);

/**
 * Convenience: pick a random head ID from the valid set for body_id.
 * Uses rngRandom() so repeated calls yield different heads for a body that
 * has more than one valid head.  Deterministic-pair bodies always return the
 * same head.  Returns NULL if the body has no valid heads.
 *
 * AUDIT-24-M2 (2026-04-25) -- DETERMINISM CONTRACT:
 *   Each client picks INDEPENDENTLY -- there is no seed coordination.
 *   Two clients invoking this function for the same bot slot will pick
 *   different heads.  This is OK for the existing matchsetup flow because
 *   the LEADER picks and then broadcasts the resolved `head_id` string
 *   to peers via `g_MatchConfig` and SVC_LOBBY_STATE; followers receive
 *   the picked ID rather than re-rolling.
 *
 *   DESYNC RISK: any future call site that picks then *uses* the head
 *   locally without a paired wire broadcast will desync (player A sees
 *   bot with face X; player B sees bot with face Y; HUD / killfeed /
 *   stats names diverge).  Lobby preview rendering and server-side
 *   simulant config resolution are the most likely future violators --
 *   audit before merging.  Server-side code MUST resolve via the
 *   leader-broadcast value, not by calling this helper. */
const char *catalogPickRandomHeadIdForBody(const char *body_id);

/** Integer: height field for a head (matches catalogGetBodyHeight contract). */
s32 catalogGetHeadHeight(s32 headnum);

/* ── SA-5f: Body / head modeldef lazy-load and reset (M0.1f) ────────────────
 * Centralise all g_HeadsAndBodies[].modeldef read/write in catalog code.
 * catalogGetBodyModeldef / catalogGetHeadModeldef: lazy-load on first call,
 * cached thereafter.  Guarded by #if !defined(PD_SERVER) — server has no ROM
 * model data and must never call these.
 * catalogReset*Modeldef: set one entry's cached pointer to NULL (call before
 * stage unload to allow fresh load next time).
 * catalogResetAllModeldefs: reset every entry in the array (used by bodiesReset).
 */

/* Forward declaration needed for SA-5f signatures. */
struct modeldef;

#if !defined(PD_SERVER)
/**
 * SA-5f: Lazy-load the body modeldef by runtime body index.
 * If g_HeadsAndBodies[bodynum].modeldef is NULL, loads it via modeldefLoadToNew
 * and caches the result.  Returns the (possibly newly loaded) pointer, or NULL
 * if the load failed.  O(1) after first call.
 * NOT available on the dedicated server (no ROM model data).
 */
struct modeldef *catalogGetBodyModeldef(s32 bodynum);

/**
 * SA-5f: Lazy-load the head modeldef by runtime head index.
 * Same contract as catalogGetBodyModeldef.  Returns NULL for HEAD_RANDOM_GENDER
 * or if the load failed.
 * NOT available on the dedicated server.
 */
struct modeldef *catalogGetHeadModeldef(s32 headnum);
#endif /* !PD_SERVER */

/**
 * SA-5f: Clear the cached modeldef pointer for one body index.
 * Sets g_HeadsAndBodies[bodynum].modeldef = NULL so the next call to
 * catalogGetBodyModeldef() triggers a fresh load.
 */
void catalogResetBodyModeldef(s32 bodynum);

/**
 * SA-5f: Clear the cached modeldef pointer for one head index.
 */
void catalogResetHeadModeldef(s32 headnum);

/**
 * SA-5f: Clear ALL cached modeldef pointers in g_HeadsAndBodies[].
 * Iterates until the sentinel (filenum == 0).  Called by bodiesReset()
 * at stage start to force a fresh model load for the new stage.
 */
void catalogResetAllModeldefs(void);

/* ── SA-5e: MP weapon table accessors (M0.1e) ──────────────────────────────
 * Thin wrappers over g_MpWeapons[] that make the catalog the public API for
 * MP weapon property reads.  ROM array is the internal implementation.
 * O(1).  Bounds-checked — return 0 on out-of-range index.
 *
 * mpweapon_idx is the g_MpWeapons[] array index (MPWEAPON_* constant range,
 * 0..NUM_MPWEAPONS-1).
 *
 * Note: priammotype / priammoqty / secammotype / secammoqty from
 * g_MpWeapons[] are accessed via mpGetMpWeaponByLocation() callers pending
 * a future catalogGetMpWeaponAmmoInfo() accessor.
 */

/** Integer: runtime WEAPON_* enum value for this MP weapon slot. */
s32 catalogGetMpWeaponNum(s32 mpweapon_idx);

/** Integer: unlock feature ID required to use this weapon (0 = always available). */
s32 catalogGetMpWeaponUnlockFeature(s32 mpweapon_idx);

/** Integer: primary ammo type (AMMOTYPE_*) for this MP weapon slot. 0 = none. */
s32 catalogGetMpWeaponPriAmmoType(s32 mpweapon_idx);

/** Integer: primary ammo quantity granted on pickup for this MP weapon slot. */
s32 catalogGetMpWeaponPriAmmoQty(s32 mpweapon_idx);

/** Integer: secondary ammo type (AMMOTYPE_*) for this MP weapon slot. 0 = none. */
s32 catalogGetMpWeaponSecAmmoType(s32 mpweapon_idx);

/** Integer: secondary ammo quantity granted on pickup for this MP weapon slot. */
s32 catalogGetMpWeaponSecAmmoQty(s32 mpweapon_idx);

/* ── INV-1 (player-init-architectural-fixes 2026-04-26): _Checked variants ──
 *
 * Loud-fail wrappers around the spawn-critical accessors above. Each writes
 * the value to *out + returns 1 on success; on miss, writes a safe default
 * to *out + returns 0 + emits one CATALOG.MISS WARNING line tagged with
 * the accessor name + index + reason.
 *
 * Spawn-critical paths (playerSpawn, botSpawn, body0f02ce8c,
 * bgunTickMasterLoad) MUST use these variants. Non-critical readers may
 * continue to call the legacy accessors above for back-compat.
 *
 * Returns are s32 (1 success, 0 miss) to match the existing
 * catalogResolveX() / catalogGetStageResultByIndex() return convention
 * and avoid pulling stdbool.h into src/game/ TUs (per the CLAUDE.md
 * "bool is s32" rule).
 *
 * Reference: context/designs/player-init-architectural-fixes-2026-04-26.md.
 */

s32 catalogGetMpWeaponNumChecked(s32 mpweapon_idx, s32 *out_value);
s32 catalogGetMpWeaponPriAmmoTypeChecked(s32 mpweapon_idx, s32 *out_value);
s32 catalogGetMpWeaponPriAmmoQtyChecked(s32 mpweapon_idx, s32 *out_value);

s32 catalogGetBodyScaleChecked(s32 bodynum, f32 *out_value);
s32 catalogGetBodyAnimScaleChecked(s32 bodynum, f32 *out_value);
s32 catalogGetBodyHandFilenumChecked(s32 bodynum, s32 *out_value);

#if !defined(PD_SERVER)
s32 catalogGetBodyModeldefChecked(s32 bodynum, struct modeldef **out_md);
s32 catalogGetHeadModeldefChecked(s32 headnum, struct modeldef **out_md);
#endif

s32 catalogGetStageResultByIndexChecked(s32 stageindex, catalog_stage_result_t *out);

/* ── SA-2: Wire helpers ─────────────────────────────────────────────────── */

/**
 * Write a 2-byte session asset reference to a network buffer.
 * The ONLY function that may serialize asset references onto the wire.
 */
void catalogWriteAssetRef(struct netbuf *buf, u16 session_id);

/**
 * Read a 2-byte session asset reference from a network buffer.
 * The ONLY function that may deserialize asset references from the wire.
 * Returns the session wire ID (0 = no asset / not assigned).
 */
u16 catalogReadAssetRef(struct netbuf *buf);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_H */
