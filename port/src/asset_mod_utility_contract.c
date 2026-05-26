/**
 * asset_mod_utility_contract.c -- source of truth for clean archive utilities.
 */

#include "asset_mod_utility_contract.h"

#include <string.h>

#define UTIL_CORE \
	(ASSET_MOD_UTIL_CREATE | ASSET_MOD_UTIL_IMPORT | \
	 ASSET_MOD_UTIL_CLONE | ASSET_MOD_UTIL_EDIT | \
	 ASSET_MOD_UTIL_VALIDATE | ASSET_MOD_UTIL_PACKAGE | \
	 ASSET_MOD_UTIL_HOT_ENABLE | ASSET_MOD_UTIL_EMBED_DEPS | \
	 ASSET_MOD_UTIL_TEMPLATE)

#define UTIL_WITH_PREVIEW (UTIL_CORE | ASSET_MOD_UTIL_PREVIEW)
#define UTIL_VALIDATE_ONLY (ASSET_MOD_UTIL_VALIDATE | ASSET_MOD_UTIL_PACKAGE)

static const asset_mod_utility_contract_t s_Contracts[] = {
	{ ASSET_WEAPON,      "weapon",      ".pdweapon",     "weapon.ini",      "Weapons",      "weapon",      UTIL_WITH_PREVIEW },
	{ ASSET_PROJECTILE,  "projectile",  ".pdprojectile", "projectile.ini",  "Projectiles",  "projectile",  UTIL_CORE },
	{ ASSET_ENTITY,      "entity",      ".pdentity",     "entity.ini",      "Entities",     "entity",      UTIL_CORE },
	{ ASSET_CHARACTER,   "character",   ".pdcharacter",  "character.ini",   "Characters",   "character",   UTIL_WITH_PREVIEW },
	{ ASSET_HEAD,        "head",        ".pdhead",       "head.ini",        "Characters",   "head",        UTIL_WITH_PREVIEW },
	{ ASSET_BODY,        "body",        ".pdbody",       "body.ini",        "Characters",   "body",        UTIL_WITH_PREVIEW },
	{ ASSET_ARENA,       "arena",       ".pdarena",      "arena.ini",       "Scenarios",    "arena",       UTIL_WITH_PREVIEW },
	{ ASSET_SCENARIO,    "scenario",    ".pdscenario",   "scenario.ini",    "Scenarios",    "scenario",    UTIL_WITH_PREVIEW },
	{ ASSET_MISSION,     "mission",     ".pdmission",    "mission.ini",     "Scenarios",    "mission",     UTIL_CORE },
	{ ASSET_MODEL,       "mesh",        ".pdmesh",       "mesh.ini",        "Geometry",     "mesh",        UTIL_WITH_PREVIEW },
	{ ASSET_ANIMATION,   "animation",   ".pdanim",       "animation.ini",   "Animation",    "animation",   UTIL_WITH_PREVIEW },
	{ ASSET_TEXTURE,     "texture",     ".pdtexture",    "texture.ini",     "Materials",    "texture",     UTIL_WITH_PREVIEW },
	{ ASSET_MATERIAL,    "material",    ".pdmaterial",   "material.ini",    "Materials",    "material",    UTIL_WITH_PREVIEW },
	{ ASSET_SKIN,        "skin",        ".pdskin",       "skin.ini",        "Skin Editor",  "skin",        UTIL_WITH_PREVIEW },
	{ ASSET_EFFECT,      "effect",      ".pdeffect",     "effect.ini",      "Effects",      "effect",      UTIL_WITH_PREVIEW },
	{ ASSET_PROP,        "prop",        ".pdprop",       "prop.ini",        "Props",        "prop",        UTIL_WITH_PREVIEW },
	{ ASSET_VEHICLE,     "vehicle",     ".pdvehicle",    "vehicle.ini",     "Props",        "vehicle",     UTIL_WITH_PREVIEW },
	{ ASSET_AUDIO,       "sfx",         ".pdsfx",        "sound.ini",       "Audio Mods",   "sfx",         UTIL_WITH_PREVIEW },
	{ ASSET_AUDIO,       "voice",       ".pdvoice",      "voice.ini",       "Audio Mods",   "voice",       UTIL_WITH_PREVIEW },
	{ ASSET_AUDIO,       "song",        ".pdsong",       "music.ini",       "Audio Mods",   "song",        UTIL_WITH_PREVIEW },
	{ ASSET_UI,          "ui",          ".pdui",         "ui.ini",          "UI",           "ui",          UTIL_WITH_PREVIEW },
	{ ASSET_FONT,        "font",        ".pdfont",       "font.ini",        "Font Mod",     "font",        UTIL_WITH_PREVIEW },
	{ ASSET_LANG,        "language",    ".pdlang",       "lang.ini",        "Language",     "language",    UTIL_CORE },
	{ ASSET_GAMEMODE,    "gamemode",    ".pdgamemode",   "gamemode.ini",    "Rules",        "gamemode",    UTIL_CORE },
	{ ASSET_BOT_PROFILE, "bot_profile", ".pdbotprofile", "botprofile.ini",  "Rules",        "bot-profile", UTIL_CORE },
	{ ASSET_HUD,         "hud",         ".pdhud",        "hud.ini",         "UI",           "hud",         UTIL_WITH_PREVIEW },
	{ ASSET_THEME,       "theme",       ".pdtheme",      "theme.ini",       "Menu Style",   "theme",       UTIL_WITH_PREVIEW },
	{ ASSET_TOOL,        "tool",        ".pdtool",       "tool.ini",        "Tools",        "tool",        UTIL_VALIDATE_ONLY | ASSET_MOD_UTIL_SECURE_TOOL },
};

static s32 strEqNoCase(const char *a, const char *b)
{
	if (!a || !b) return 0;
	while (*a && *b) {
		char ca = *a++;
		char cb = *b++;
		if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
		if (ca != cb) return 0;
	}
	return *a == '\0' && *b == '\0';
}

size_t assetModUtilityContractCount(void)
{
	return sizeof(s_Contracts) / sizeof(s_Contracts[0]);
}

const asset_mod_utility_contract_t *assetModUtilityContractAt(size_t index)
{
	if (index >= assetModUtilityContractCount()) return NULL;
	return &s_Contracts[index];
}

const asset_mod_utility_contract_t *assetModUtilityContractForType(asset_type_e type)
{
	for (size_t i = 0; i < assetModUtilityContractCount(); i++) {
		if (s_Contracts[i].type == type) return &s_Contracts[i];
	}
	return NULL;
}

const asset_mod_utility_contract_t *assetModUtilityContractForExtension(const char *extension)
{
	for (size_t i = 0; i < assetModUtilityContractCount(); i++) {
		if (strEqNoCase(s_Contracts[i].extension, extension)) return &s_Contracts[i];
	}
	return NULL;
}

s32 assetModUtilitySupports(const asset_mod_utility_contract_t *contract, u32 op)
{
	if (!contract) return 0;
	return (contract->ops & op) == op;
}

const char *assetModUtilityOperationName(u32 op)
{
	switch (op) {
	case ASSET_MOD_UTIL_CREATE: return "create";
	case ASSET_MOD_UTIL_IMPORT: return "import";
	case ASSET_MOD_UTIL_CLONE: return "clone";
	case ASSET_MOD_UTIL_EDIT: return "edit";
	case ASSET_MOD_UTIL_VALIDATE: return "validate";
	case ASSET_MOD_UTIL_PACKAGE: return "package";
	case ASSET_MOD_UTIL_PREVIEW: return "preview";
	case ASSET_MOD_UTIL_HOT_ENABLE: return "hot-enable";
	case ASSET_MOD_UTIL_EMBED_DEPS: return "embed-dependencies";
	case ASSET_MOD_UTIL_TEMPLATE: return "template";
	case ASSET_MOD_UTIL_SECURE_TOOL: return "secure-tool";
	default: return "unknown";
	}
}
