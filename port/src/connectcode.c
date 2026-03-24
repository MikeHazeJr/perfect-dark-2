/**
 * connectcode.c -- Encode/decode IP:port as Perfect Dark-themed word codes.
 *
 * 256-word vocabulary from Perfect Dark lore: characters, locations, weapons,
 * gadgets, vehicles, factions, and game concepts. Each word maps to one byte.
 * 6 words = 4 bytes IP + 2 bytes port.
 */

#include "connectcode.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ========================================================================
 * 256 unique Perfect Dark-themed words, all uppercase.
 * Index = byte value (0x00–0xFF).
 * ======================================================================== */

static const char *s_Words[256] = {
	/* Characters & People (0x00–0x1F) */
	"JOANNA",     "CARRINGTON", "GRIMSHAW",   "CASSANDRA",  "TRENT",      "ELVIS",
	"JONATHAN",   "FOSTER",     "BLONDE",     "VELVET",     "DANIEL",     "MAIAN",
	"SKEDAR",     "PELAGIC",    "CIPHER",     "SHADOW",
	"DOUBLE",     "PERFECT",    "DARK",       "AGENT",      "SPECIAL",    "GUARD",
	"SOLDIER",    "PILOT",      "SNIPER",     "TROOPER",    "OFFICER",    "LEADER",
	"HUNTER",     "STRIKER",    "WATCHER",    "PHANTOM",

	/* Locations (0x20–0x3F) */
	"CHICAGO",    "CRASHSITE",  "AREA",       "VILLA",      "AIRBASE",    "AIRFORCE",
	"ARCHIVE",    "RUINS",      "TEMPLE",     "FORTRESS",   "SANCTUM",    "OUTPOST",
	"HANGAR",     "BRIDGE",     "COMPLEX",    "INSTITUTE",
	"DATADYNE",   "TOWER",      "ROOFTOP",    "BASEMENT",   "TUNNEL",     "SEWER",
	"DOCK",       "PLATFORM",   "BUNKER",     "VAULT",      "CHAMBER",    "CORRIDOR",
	"SECTOR",     "DEPOT",      "TERMINAL",   "GATEWAY",

	/* Weapons (0x40–0x5F) */
	"FALCON",     "MAGNUM",     "SHOTGUN",    "LAPTOP",     "DRAGON",     "MAULER",
	"PHOENIX",    "CALLISTO",   "CROSSBOW",   "SLAYER",     "REAPER",     "FARSIGHT",
	"CYCLONE",    "LASER",      "ROCKET",     "GRENADE",
	"COMBAT",     "KNIFE",      "TRANQUIL",   "DEVASTATOR", "SUPERDRAGON","AVENGER",
	"MARQUIS",    "DEAGLE",     "RAPTOR",     "VENOM",      "COVERT",     "SILENCED",
	"PROXIMITY",  "REMOTE",     "TIMED",      "TRIGGER",

	/* Gadgets & Items (0x60–0x7F) */
	"CLOAKING",   "DISGUISE",   "ECMMINE",    "LOCKPICK",   "NIGHTVISION","SHIELD",
	"CAMSPY",     "BOMBSPY",    "DRUGSPY",    "SCANNER",    "UPLINK",     "RTRACKER",
	"XRAY",       "IRSCANNER",  "HORIZON",    "DECODER",
	"DATALINK",   "KEYCARD",    "FUSE",       "BRIEFCASE",  "EVIDENCE",   "DISK",
	"CANISTER",   "CRATE",      "AMMO",       "ARMOR",      "BOOST",      "PICKUP",
	"TOKEN",      "EMBLEM",     "BADGE",      "CROWN",

	/* Vehicles & Tech (0x80–0x9F) */
	"DROPSHIP",   "JUMPSHIP",   "INTERCEPTOR","SHUTTLE",    "HOVERBIKE",  "HOVERCRATE",
	"ELEVATOR",   "CONSOLE",    "MAINFRAME",  "SATELLITE",  "REACTOR",    "GENERATOR",
	"TURBINE",    "BEACON",     "ANTENNA",    "RADAR",
	"COMPUTER",   "SERVER",     "NETWORK",    "SIGNAL",     "RELAY",      "ORBITAL",
	"STATION",    "CAPSULE",    "WARHEAD",    "MISSILE",    "TORPEDO",    "CANNON",
	"TURRET",     "DRONE",      "PROBE",      "MECH",

	/* Mission Concepts (0xA0–0xBF) */
	"DEFECTION",  "EXTRACTION", "RESCUE",     "ESCAPE",     "INFILTRATE", "ASSAULT",
	"RECON",      "SABOTAGE",   "ATTACK",     "DEFENSE",    "DUEL",       "WAR",
	"BREACH",     "SIEGE",      "STORM",      "PURSUIT",
	"STEALTH",    "AMBUSH",     "COUNTER",    "STRIKE",     "DEPLOY",     "SECURE",
	"EXTRACT",    "NEUTRALIZE", "DETONATE",   "OVERRIDE",   "DECRYPT",    "TRANSMIT",
	"ACTIVATE",   "DISABLE",    "ELIMINATE",  "COMPLETE",

	/* Multiplayer / Game Concepts (0xC0–0xDF) */
	"ARENA",      "FELICITY",   "PIPES",      "GRID",       "WAREHOUSE",  "PINNACLE",
	"HELIX",      "TRENCH",     "RAVINE",     "CLIFF",      "MESA",       "CANYON",
	"SUMMIT",     "GLACIER",    "CRATER",     "RIDGE",
	"SIMULANT",   "MEATSIM",    "DARKSIM",    "VENGEANCE",  "CHALLENGE",  "PROTOCOL",
	"MISSION",    "BRIEFING",   "DEBRIEF",    "INTEL",      "CLASSIFIED", "CRITICAL",
	"PRIORITY",   "ALERT",      "OMEGA",      "ALPHA",

	/* Abstract / Sci-Fi (0xE0–0xFF) */
	"GALAXY",     "NEBULA",     "STELLAR",    "COSMIC",     "QUANTUM",    "VECTOR",
	"MATRIX",     "NEXUS",      "PRISM",      "APEX",       "ZENITH",     "NADIR",
	"ECLIPSE",    "AURORA",     "NOVA",       "PULSAR",
	"CRIMSON",    "COBALT",     "EMERALD",    "OBSIDIAN",   "TITANIUM",   "CARBON",
	"PLASMA",     "ION",        "FLUX",       "RIFT",       "VOID",       "CORE",
	"ECHO",       "DELTA",      "SIGMA",      "ZERO",
};

/* ========================================================================
 * Encode: IP (network order) + port (host order) → 6 words
 * ======================================================================== */

s32 connectCodeEncode(u32 ip, u16 port, char *buf, s32 bufsize)
{
	if (!buf || bufsize < 64) return -1;

	u8 bytes[6];
	/* IP is already in network byte order (big-endian) */
	bytes[0] = (ip >> 0)  & 0xFF;
	bytes[1] = (ip >> 8)  & 0xFF;
	bytes[2] = (ip >> 16) & 0xFF;
	bytes[3] = (ip >> 24) & 0xFF;
	/* Port in big-endian */
	bytes[4] = (port >> 8) & 0xFF;
	bytes[5] = (port >> 0) & 0xFF;

	return snprintf(buf, bufsize, "%s %s %s %s %s %s",
		s_Words[bytes[0]], s_Words[bytes[1]],
		s_Words[bytes[2]], s_Words[bytes[3]],
		s_Words[bytes[4]], s_Words[bytes[5]]);
}

/* ========================================================================
 * Decode: 6 words → IP + port
 * ======================================================================== */

static s32 wordToIndex(const char *word, s32 len)
{
	char upper[32];
	if (len <= 0 || len >= (s32)sizeof(upper)) return -1;

	for (s32 i = 0; i < len; i++) {
		upper[i] = (char)toupper((unsigned char)word[i]);
	}
	upper[len] = '\0';

	for (s32 i = 0; i < 256; i++) {
		if (strcmp(upper, s_Words[i]) == 0) {
			return i;
		}
	}
	return -1;
}

s32 connectCodeDecode(const char *code, u32 *outIp, u16 *outPort)
{
	if (!code || !outIp || !outPort) return -1;

	u8 bytes[6];
	s32 count = 0;
	const char *p = code;

	/* Skip leading whitespace */
	while (*p && (isspace((unsigned char)*p) || *p == '-' || *p == '.')) p++;

	while (*p && count < 6) {
		/* Find word start */
		const char *start = p;
		while (*p && !isspace((unsigned char)*p) && *p != '-' && *p != '.') p++;
		s32 len = (s32)(p - start);

		if (len > 0) {
			s32 idx = wordToIndex(start, len);
			if (idx < 0) return -1;
			bytes[count++] = (u8)idx;
		}

		/* Skip separators */
		while (*p && (isspace((unsigned char)*p) || *p == '-' || *p == '.')) p++;
	}

	if (count != 6) return -1;

	/* Reconstruct IP (network byte order) */
	*outIp = (u32)bytes[0] | ((u32)bytes[1] << 8) |
	          ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);

	/* Reconstruct port (host byte order from big-endian) */
	*outPort = ((u16)bytes[4] << 8) | (u16)bytes[5];

	return 0;
}
