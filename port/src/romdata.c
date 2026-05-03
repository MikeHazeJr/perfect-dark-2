#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <SDL.h>
#include <PR/ultratypes.h>
#include "lib/rzip.h"
#include "versioninfo.h"
#include "romdata.h"
#include "romextract.h"
#include "sha256.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "fs.h"
#include "system.h"
#include "preprocess.h"
#include "platform.h"
#include "constants.h"
#include "data.h"

/**
 * asset files and ROM segments can be replaced by optional external files,
 * but asset filenames still have to be either pulled from the ROM or from an
 * external file, so stuff can't be completely custom
 * 
 * all data is assumed to be big endian, so it has to be byteswapped
 * at load time, which is fucking terrible
 */

#define ROMDATA_FILEDIR "files"
#define ROMDATA_SEGDIR "segs"

#define ROMDATA_ROM_NAME "pd." VERSION_ROMID ".z64"
#define ROMDATA_ROM_SIZE 33554432

#if VERSION == VERSION_NTSC_FINAL
#define ROMDATA_ROM_TITLE "Perfect Dark"
#define ROMDATA_ROM_ID "NPDE"
#define ROMDATA_ROM_DESC "NTSC v1.1"
#define ROMDATA_FILES_OFS 0x28080
#define ROMDATA_DATA_OFS 0x39850
#elif VERSION == VERSION_PAL_FINAL
#define ROMDATA_ROM_TITLE "Perfect Dark"
#define ROMDATA_ROM_ID "NPDP"
#define ROMDATA_ROM_DESC "PAL"
#define ROMDATA_FILES_OFS 0x28910
#define ROMDATA_DATA_OFS 0x39850
#elif VERSION == VERSION_JPN_FINAL
#define ROMDATA_ROM_TITLE "PERFECT DARK"
#define ROMDATA_ROM_ID "NPDJ"
#define ROMDATA_ROM_DESC "JPN"
#define ROMDATA_FILES_OFS 0x28800
#define ROMDATA_DATA_OFS 0x39850
#else
#error "This ROM version is unsupported."
#endif

#define ROMDATA_MAX_FILES 2048

#define GBC_ROM_NAME "pd.gbc"
#define GBC_ROM_SIZE 4194304

u8 *g_RomFile;
u32 g_RomFileSize;
const char *g_RomName = ROMDATA_ROM_NAME;

static u8 *romDataSeg;
static u32 romDataSegSize;

enum loadsource {
	SRC_UNLOADED = 0,
	SRC_ROM,
	SRC_EXTERNAL
};

struct romfilepatch {
	u32 ofs;
	u32 len;
	const char *src;
	const char *dst;
};

struct romfile {
	u8 **segstart;
	u8 **segend;
	const char *name;
	u8 *data;
	u32 size;
	preprocessfunc preprocess;
	s32 source; // enum loadsource
	s32 preprocessed;
	const struct romfilepatch *patches;
	u32 numpatches;
};

/* patches for individual files; applied on file load, before preprocFuncs, but */
/* after unzip; only applied when loading from a ROM file                       */
static const struct romfilepatch filePatches[] = {
	/* FILE_USETUPLUE: fixes Jon's double "if what" in Infiltration outro */
	{ 0x92a2, 1, "\x6c", "\x99" },
	{ 0x92b0, 1, "\x6c", "\x99" },
};

static struct romfile fileSlots[ROMDATA_MAX_FILES] = {
	[FILE_USETUPLUE] = { .patches = &filePatches[0], .numpatches = 2 },
};

#define ROMSEG_START(n) _ ## n ## SegmentRomStart
#define ROMSEG_END(n) _ ## n ## SegmentRomEnd

/* segment table for ntsc-final                                                     */
/* size will get calculated automatically if it is 0                                */
/* if there are replacement files in the data dir, they will be loaded instead      */
/* offsets are specified for ntsc-final, pal-final and jpn-final in that order      */
#define ROMSEG_LIST() \
	ROMSEG_DECL_SEG(fontjpnsingle,      0x194b20,  0x180330,  0x0,       0x0,      preprocessJpnFont       ) \
	ROMSEG_DECL_SEG(fontjpnmulti,       0x19fb40,  0x18b340,  0x0,       0x0,      preprocessJpnFont       ) \
	ROMSEG_DECL_SEG(animations,         0x1a15c0,  0x18cdc0,  0x190c50,  0x0,      preprocessAnimations    ) \
	ROMSEG_DECL_SEG(mpconfigs,          0x7d0a40,  0x7bc240,  0x7c00d0,  0x11e0,   preprocessMpConfigs     ) \
	ROMSEG_DECL_SEG(mpstringsE,         0x7d1c20,  0x7bd420,  0x7c12b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsJ,         0x7d5320,  0x7c0b20,  0x7c49b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsP,         0x7d8a20,  0x7c4220,  0x7c80b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsG,         0x7dc120,  0x7c7920,  0x7cb7b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsF,         0x7df820,  0x7cb020,  0x7ceeb0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsS,         0x7e2f20,  0x7ce720,  0x7d25b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsI,         0x7e6620,  0x7d1e20,  0x7d5cb0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(firingrange,        0x7e9d20,  0x7d5520,  0x7d93b0,  0x1550,   NULL                    ) \
	ROMSEG_DECL_SEG(fonttahoma,         0x7f7860,  0x7e3060,  0x7e6ef0,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fontnumeric,        0x7f8b20,  0x7e4320,  0x7e81b0,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fonthandelgothicsm, 0x7f9d30,  0x7e5530,  0x7e93c0,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fonthandelgothicxs, 0x7fbfb0,  0x7e87b0,  0x7ec640,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fonthandelgothicmd, 0x7fdd80,  0x7eae20,  0x7eecb0,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fonthandelgothiclg, 0x8008e0,  0x7eee70,  0x7f2d00,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(sfxctl,             0x80a250,  0x7f87e0,  0x7fc670,  0x2fb80,  preprocessALBankFile    ) \
	ROMSEG_DECL_SEG(sfxtbl,             0x839dd0,  0x828360,  0x82c1f0,  0x4c2160, NULL                    ) \
	ROMSEG_DECL_SEG(seqctl,             0xcfbf30,  0xcea4c0,  0xcee350,  0xa060,   preprocessALBankFile    ) \
	ROMSEG_DECL_SEG(seqtbl,             0xd05f90,  0xcf4520,  0xcf83b0,  0x17c070, NULL                    ) \
	ROMSEG_DECL_SEG(sequences,          0xe82000,  0xe70590,  0xe74420,  0x563a0,  preprocessSequences     ) \
	ROMSEG_DECL_SEG(texturesdata,       0x1d65f40, 0x1d5ca20, 0x1d61f90, 0x0,      NULL                    ) \
	ROMSEG_DECL_SEG(textureslist,       0x1ff7ca0, 0x1fee780, 0x1ff68f0, 0x0,      preprocessTexturesList  ) \
	ROMSEG_DECL_SEG(copyright,          0x1ffea20, 0x1ff5500, 0x1ffd6b0, 0xb30,    NULL                    ) \
	ROMSEG_DECL_SEG(fontjpn,            0x0,       0x0,       0x178c40,  0x17920,  preprocessJpnFont       )

// declare the vars first

#undef ROMSEG_DECL_SEG
#define ROMSEG_DECL_SEG(name, ofs_ntsc, ofs_pal, ofs_jpn, size, preproc) u8 *ROMSEG_START(name), *ROMSEG_END(name);
ROMSEG_LIST()

// this is part of the animations seg and as such does not follow the naming convention
// these are set in preprocessAnimations
u8 *_animationsTableRomStart;
u8 *_animationsTableRomEnd;

// then build the table

#undef ROMSEG_DECL_SEG

#if VERSION == VERSION_NTSC_FINAL
#define ROMSEG_DECL_SEG(name, ofs_ntsc, ofs_pal, ofs_jpn, size, preproc) { &ROMSEG_START(name), &ROMSEG_END(name), #name, (u8 *)ofs_ntsc, size, preproc },
#elif VERSION == VERSION_PAL_FINAL
#define ROMSEG_DECL_SEG(name, ofs_ntsc, ofs_pal, ofs_jpn, size, preproc) { &ROMSEG_START(name), &ROMSEG_END(name), #name, (u8 *)ofs_pal, size, preproc },
#elif VERSION == VERSION_JPN_FINAL
#define ROMSEG_DECL_SEG(name, ofs_ntsc, ofs_pal, ofs_jpn, size, preproc) { &ROMSEG_START(name), &ROMSEG_END(name), #name, (u8 *)ofs_jpn, size, preproc },
#endif

static struct romfile romSegs[] = {
	ROMSEG_LIST()
	{ NULL, NULL, NULL, NULL, 0, NULL },
};

/* the game sets g_LoadType to the type of file it expects,              */
/* so we can hijack that in fileLoad and automatically byteswap the file */
static preprocessfunc filePreprocFuncs[] = {
	/* LOADTYPE_NONE  */ NULL,
	/* LOADTYPE_BG    */ NULL, // loaded in parts
	/* LOADTYPE_TILES */ preprocessTilesFile,
	/* LOADTYPE_LANG  */ preprocessLangFile,
	/* LOADTYPE_SETUP */ preprocessSetupFile,
	/* LOADTYPE_PADS  */ preprocessPadsFile,
	/* LOADTYPE_MODEL */ preprocessModelFile,
	/* LOADTYPE_GUN   */ preprocessGunFile,
};

static inline void romdataWrongRomError(const char *fmt, ...)
{
	char reason[1024];
	reason[0] = '\0';

	va_list args;
	va_start(args, fmt);
	vsnprintf(reason, sizeof(reason), fmt, args);
	va_end(args);

	sysFatalError("Wrong ROM file.\n%s\nEnsure that you have the correct " ROMDATA_ROM_DESC " ROM in z64 format.", reason);
}

/* ========================================================================
 * ROM SHA-256 known-good hash table
 *
 * Each entry is a 64-character lowercase hex string (SHA-256 of the z64
 * ROM file, big-endian byte order). The binary validates the ROM against
 * this compile-time table so no loose .sha256 files need to be shipped.
 *
 * To populate: run the game once, look for the log line:
 *   ROM: SHA-256 <64-char hex>
 * Copy that string into the appropriate array below and rebuild.
 *
 * Leaving the array empty (just NULL) disables validation — the ROM is
 * accepted as long as it passes the size + header check. Adding a wrong
 * hash only produces a WARNING; it never prevents the game from running.
 *
 * === ROM version compatibility ===
 * NTSC v1.0 (VERSION_NTSC_1_0 = 1): NOT compatible with NTSC_FINAL binaries.
 *   Game code uses `VERSION >= VERSION_NTSC_FINAL` guards throughout; struct
 *   sizes and file counts differ. Accept NTSC v1.1 (NTSC_FINAL) only.
 * PAL and JPN finals share DATA_OFS (0x39850) with NTSC but differ in
 *   FILES_OFS and game data — each requires its own binary build.
 * ======================================================================== */

/*
 * Phase 3 Pass A.3 (2026-05-02): SHA-256 hash table population.
 *
 * STATUS: data-side population is BYOR.  The arrays below stay
 * NULL-only in the public source tree; Mike (or any user with a
 * verified ROM) captures the SHA-256 from the "ROM: SHA-256 ..."
 * LOG_NOTE on first launch and pastes the value into the matching
 * region's array.  Once populated, romdataVerifyRomHash matches
 * against the known-good list and emits a quiet "hash verified"
 * line; mismatches surface as LOG_WARNING.
 *
 * The Phase 3 Pass A.4 self-heal path
 * (romextract.c::romExtractVerifyAll) operates per-extracted-file
 * and is independent of the ROM-level hash list -- per-file
 * sidecars catch corruption regardless of whether the parent ROM
 * hash is in this table.  Pass A.3 is purely a "did the user
 * provide the right ROM?" gate; Pass A.4 is "is the extracted disk
 * content intact?".
 *
 * Companion plan:
 *   context/designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md
 */
#if VERSION == VERSION_NTSC_FINAL
static const char *const s_KnownRomHashes[] = {
	/* Perfect Dark (U) (V1.1) NTSC — primary decompilation target.
	 * Run the game once and copy the "ROM: SHA-256" log line here.    */
	/* "e03b088b6ac9e0080412ef9b89b1e4c6f47e408d32bb3d406744b65168b75b4b", */
	NULL
};
#elif VERSION == VERSION_PAL_FINAL
static const char *const s_KnownRomHashes[] = {
	/* Perfect Dark (E) PAL final. Run once and paste "ROM: SHA-256" here. */
	NULL
};
#elif VERSION == VERSION_JPN_FINAL
static const char *const s_KnownRomHashes[] = {
	/* Perfect Dark (J) JPN final. Run once and paste "ROM: SHA-256" here. */
	NULL
};
#else
static const char *const s_KnownRomHashes[] = { NULL };
#endif

static void romdataVerifyRomHash(void)
{
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(g_RomFile, g_RomFileSize, digest);

	char hex[SHA256_HEX_SIZE];
	sha256ToHex(digest, hex);

	/* Always log the hash — developers can paste it into s_KnownRomHashes. */
	sysLogPrintf(LOG_NOTE, "ROM: SHA-256 %s", hex);

	s32 numKnown = 0;
	for (const char *const *h = s_KnownRomHashes; *h; h++) {
		numKnown++;
		if (strncmp(hex, *h, 64) == 0) {
			sysLogPrintf(LOG_NOTE, "ROM: hash verified (known-good)");
			return;
		}
	}

	if (numKnown > 0) {
		sysLogPrintf(LOG_WARNING,
		             "ROM: hash not in known-good list — wrong ROM version?");
	}
}

/**
 * Show a user-friendly ROM missing dialog with an "Open Folder" button.
 * Uses SDL_ShowMessageBox with custom buttons so users can quickly
 * navigate to the data directory where the ROM needs to be placed.
 */
static void romdataEnsureDataDir(const char *dataDir, const char *romName)
{
	/* Create the data directory if it doesn't exist */
#ifdef _WIN32
	_mkdir(dataDir);
#else
	mkdir(dataDir, 0755);
#endif

	/* Write a readme explaining what ROM file is needed */
	char readmePath[FS_MAXPATH];
	snprintf(readmePath, sizeof(readmePath), "%s/README.txt", dataDir);

	FILE *f = fopen(readmePath, "w");
	if (f) {
		fprintf(f, "Perfect Dark 2 - Data Directory\n");
		fprintf(f, "===============================\n\n");
		fprintf(f, "Place your Perfect Dark N64 ROM in this folder with the exact name:\n\n");
		fprintf(f, "    %s\n\n", romName);
		fprintf(f, "Requirements:\n");
		fprintf(f, "  - Must be the NTSC v1.0 (final) ROM\n");
		fprintf(f, "  - Must be in uncompressed z64 format\n");
		fprintf(f, "  - File size must be exactly 32 MB (33,554,432 bytes)\n");
		fprintf(f, "  - Do NOT use .v64 or .n64 format\n");
		fprintf(f, "  - Do NOT leave it in a zip/rar/7z archive\n");
		fclose(f);
	}
}

static void romdataShowMissingRomDialog(const char *romName, const char *dataDir)
{
	/* Ensure the data directory exists and has a readme */
	romdataEnsureDataDir(dataDir, romName);

	char msg[1024];
	snprintf(msg, sizeof(msg),
		"Perfect Dark N64 ROM not found.\n\n"
		"Please place your ROM file in the data folder with this exact name:\n\n"
		"    %s\n\n"
		"Data folder:\n"
		"    %s\n\n"
		"The ROM must be in uncompressed z64 format (32 MB).\n"
		"Do not rename the extension or use .v64 / .n64 format.",
		romName, dataDir);

	SDL_MessageBoxButtonData buttons[2] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Open Folder" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Exit" },
	};

	SDL_MessageBoxData boxData;
	memset(&boxData, 0, sizeof(boxData));
	boxData.flags = SDL_MESSAGEBOX_ERROR;
	boxData.window = NULL;
	boxData.title = "ROM File Missing";
	boxData.message = msg;
	boxData.numbuttons = 2;
	boxData.buttons = buttons;

	s32 buttonId = 1;
	SDL_ShowMessageBox(&boxData, &buttonId);

	if (buttonId == 0) {
		/* Open the data folder in the system file manager */
#ifdef _WIN32
		char winpath[1024];
		strncpy(winpath, dataDir, sizeof(winpath) - 1);
		winpath[sizeof(winpath) - 1] = '\0';
		/* Normalize forward slashes to backslashes for explorer */
		for (char *p = winpath; *p; p++) { if (*p == '/') *p = '\\'; }
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "explorer \"%s\"", winpath);
		system(cmd);
#elif defined(__APPLE__)
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "open \"%s\"", dataDir);
		system(cmd);
#else
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", dataDir);
		system(cmd);
#endif
	}

	exit(1);
}

static inline void romdataLoadRom(void)
{
	sysLogPrintf(LOG_NOTE, "ROM file: %s", g_RomName);

	g_RomFile = fsFileLoad(g_RomName, &g_RomFileSize);

	if (!g_RomFile) {
		/* Use the base directory that fsFileLoad actually searched so the
		 * dialog and Explorer point at the right folder even when --basedir
		 * or the home-directory fallback redirected the search away from
		 * the exe's data/ subfolder. */
		const char *dataDir = fsGetBaseDir();
		if (!dataDir || !dataDir[0]) {
			/* fsGetBaseDir can return NULL before fsInit completes; fall back
			 * to the exe directory so the dialog is never completely wrong. */
			static char exeDataDir[FS_MAXPATH];
			char exePath[FS_MAXPATH];
			sysGetExecutablePath(exePath, FS_MAXPATH);
			snprintf(exeDataDir, FS_MAXPATH, "%s/data", exePath);
			dataDir = exeDataDir;
		}
		sysLogPrintf(LOG_ERROR, "ROM: Could not open %s in %s", g_RomName, dataDir);
		romdataShowMissingRomDialog(g_RomName, dataDir);
	}

	// zips are not guaranteed to start with PK, but might as well at least try
	if (g_RomFileSize > 2 && (!memcmp(g_RomFile, "PK", 2) || !memcmp(g_RomFile, "Rar", 3) || !memcmp(g_RomFile, "7z", 2))) {
		romdataWrongRomError("Your ROM is in an archive file. Please extract it.");
	}

	if (g_RomFileSize != ROMDATA_ROM_SIZE) {
		romdataWrongRomError("ROM size does not match: expected: %u, got: %u.", ROMDATA_ROM_SIZE, g_RomFileSize);
	}

	if (memcmp(g_RomFile + 0x3b, ROMDATA_ROM_ID, 4) || memcmp(g_RomFile + 0x20, ROMDATA_ROM_TITLE, sizeof(ROMDATA_ROM_TITLE) - 1)) {
		romdataWrongRomError("ROM header does not match.");
	}

	/* SHA-256 validation against compile-time known-good list.
	 * Always logs the hash; warns if it's not in the list. */
	romdataVerifyRomHash();

	// inflate the compressed data segment since that's where some useful stuff is

	u8 *zipped = g_RomFile + ROMDATA_DATA_OFS;
	if (!rzipIs1173(zipped)) {
		romdataWrongRomError("Data segment is not 1173-compressed.");
	}

	const u32 dataSegLen = ((u32)zipped[2] << 16) | ((u32)zipped[3] << 8) | (u32)zipped[4];
	if (dataSegLen < ROMDATA_FILES_OFS) {
		romdataWrongRomError("Data segment too small (%u), need at least %u.", dataSegLen, ROMDATA_FILES_OFS);
	}

	u8 *dataSeg = sysMemAlloc(dataSegLen);
	if (!dataSeg) {
		sysFatalError("Could not allocate %u bytes for data segment.", dataSegLen);
	}

	u8 scratch[5 * 1024];
	if (rzipInflate(zipped, dataSeg, scratch) < 0) {
		free(dataSeg);
		sysFatalError("Could not inflate data segment.");
	}

	romDataSeg = dataSeg;
	romDataSegSize = dataSegLen;
}

static inline void romdataUpdateSegStartEnd(struct romfile* seg)
{
	if (seg->segstart) {
		*seg->segstart = seg->data;
	}

	if (seg->segend) {
		*seg->segend = seg->data + seg->size;
	}
}

static inline void romdataInitSegment(struct romfile *seg)
{
	if (!seg->data) {
		// unused in this ROM, skip it
		sysLogPrintf(LOG_NOTE, "skipping segment %s", seg->name);
		return;
	}

	if (!seg->size) {
		// size unknown
		if (seg[1].name) {
			// use next segment's base to calculate
			seg->size = seg[1].data - seg->data;
		} else {
			// this is the last segment, calculate based on rom size
			seg->size = (uintptr_t)g_RomFileSize - (uintptr_t)seg->data;
		}
	}

	// check if we have an external replacement and load it if so.
	// Phase 3 Pass B Slices 2/5/6/8/11 (2026-05-02): prefer the
	// per-romid extracted path data/<romid>/segs/<name>.bin first;
	// fall back to the legacy data/segs/<name> mod-override path so
	// existing mods that drop a replacement segment continue to work.
	char tmp[FS_MAXPATH];
	u8 *newData = NULL;

	// Per-romid path: data/<romid>/segs/<name>.bin
	snprintf(tmp, sizeof(tmp), "%s/segs/%s.bin", VERSION_ROMID, seg->name);
	{
		const s32 extSize = fsFileSize(tmp);
		if (extSize > 0) {
			newData = fsFileLoad(tmp, &seg->size);
		}
	}

	// Legacy mod-override path: <basedir>/segs/<name>
	if (newData == NULL) {
		snprintf(tmp, sizeof(tmp), ROMDATA_SEGDIR "/%s", seg->name);
		const s32 extFileSize = fsFileSize(tmp);
		if (extFileSize > 0) {
			newData = fsFileLoad(tmp, &seg->size);
		}
	}

	if (!newData) {
		// no external data, just make it point to the rom
		if (g_RomFile) {
			newData = g_RomFile + (uintptr_t)seg->data;
			seg->source = SRC_ROM;
			sysLogPrintf(LOG_NOTE, "loading segment %s from ROM (offset %08x pointer %p)", seg->name, (uintptr_t)seg->data, newData);
		} else {
			sysFatalError("No ROM or external file for segment:\n%s", seg->name);
		}
	} else {
		// loaded external data
		seg->source = SRC_EXTERNAL;
		sysLogPrintf(LOG_NOTE, "loading segment %s from file (pointer %p)", seg->name, newData);
	}

	seg->data = newData;

	romdataUpdateSegStartEnd(seg);

	// call the post load function if any
	if (seg->preprocess && !seg->preprocessed) {
		newData = seg->preprocess(seg->data, seg->size, &seg->size);

		if (newData) {
			if (seg->source == SRC_EXTERNAL)
				sysMemFree(seg->data);
			seg->data = newData;
			romdataUpdateSegStartEnd(seg);
		}
		
		seg->preprocessed = 1;
	}
}

static inline s32 romdataLoadExternalFileList(void)
{
	romDataSeg = fsFileLoad("filenames.lst", &romDataSegSize); // this null terminates the file by itself
	if (!romDataSeg || !romDataSegSize) {
		return 0;
	}

	s32 n = 1;
	char *p = (char *)romDataSeg;
	while (*p && n < ROMDATA_MAX_FILES) {
		// skip whitespace
		while (*p && isspace(*p)) ++p;
		if (*p) {
			const char *start = p;
			// skip to next whitespace or end of file
			while (*p && !isspace(*p)) ++p;
			// null terminate the name if needed
			if (*p) {
				*p++ = '\0';
			}
			fileSlots[n++].name = start;
		}
	}

	return n - 1;
}

static inline void romdataInitFiles(void)
{
	if (!g_RomFile) {
		// no ROM; try to load the file name list from disk
		if (!romdataLoadExternalFileList()) {
			sysFatalError("No ROM file or external filename table found.");
		}
		return;
	}

	// the file offset table is in the data seg
	const u32 *offsets = (u32 *)(romDataSeg + ROMDATA_FILES_OFS);
	u32 i;
	for (i = 1; offsets[i]; ++i) {
		if (offsets + i + 1 < (u32 *)(romDataSeg + romDataSegSize)) {
			const u32 nextofs = PD_BE32(offsets[i + 1]);
			const u32 ofs = PD_BE32(offsets[i]);
			fileSlots[i].data = g_RomFile + ofs;
			fileSlots[i].size = nextofs - ofs;
			fileSlots[i].source = SRC_UNLOADED;
			fileSlots[i].preprocessed = 0;
		}
	}

	// last offset is to the name table
	const u32 *nameOffsets = (u32 *)(g_RomFile + PD_BE32(offsets[i - 1]));
	for (i = 1; nameOffsets[i]; ++i) {
		const u32 ofs = PD_BE32(nameOffsets[i]);
		fileSlots[i].name = (const char *)nameOffsets + ofs;
	}

	// Model Slot Expansion (PD Plus Mod extra character slots)
	fileSlots[FILE_CDRCARROLL2].data = 0;
	fileSlots[FILE_CDRCARROLL2].size = 0;
	fileSlots[FILE_CDRCARROLL2].source = SRC_UNLOADED;
	fileSlots[FILE_CDRCARROLL2].preprocessed = 0;
	fileSlots[FILE_CDRCARROLL2].name = "Ccarroll2Z";

	fileSlots[FILE_CSKEDAR2].data = 0;
	fileSlots[FILE_CSKEDAR2].size = 0;
	fileSlots[FILE_CSKEDAR2].source = SRC_UNLOADED;
	fileSlots[FILE_CSKEDAR2].preprocessed = 0;
	fileSlots[FILE_CSKEDAR2].name = "Cskedar2Z";

	fileSlots[FILE_GHAND_DRCARROLL].data = 0;
	fileSlots[FILE_GHAND_DRCARROLL].size = 0;
	fileSlots[FILE_GHAND_DRCARROLL].source = SRC_UNLOADED;
	fileSlots[FILE_GHAND_DRCARROLL].preprocessed = 0;
	fileSlots[FILE_GHAND_DRCARROLL].name = "Ghand_carollZ";

	fileSlots[FILE_GHAND_SKEDAR].data = 0;
	fileSlots[FILE_GHAND_SKEDAR].size = 0;
	fileSlots[FILE_GHAND_SKEDAR].source = SRC_UNLOADED;
	fileSlots[FILE_GHAND_SKEDAR].preprocessed = 0;
	fileSlots[FILE_GHAND_SKEDAR].name = "Ghand_skedarZ";
}

static inline struct romfile *romdataGetSeg(const char *name)
{
	struct romfile *seg = romSegs;
	while (seg->name && strcmp(name, seg->name)) {
		++seg;
	}
	return seg;
}

s32 romdataInit(void)
{
	const char *altRomName = sysArgGetString("--rom-file");
	if (altRomName) {
		g_RomName = altRomName;
	}

	romdataLoadRom();

	// set segments to point to the rom or load them externally
	for (struct romfile *seg = romSegs; seg->name; ++seg) {
		romdataInitSegment(seg);
	}

	// load file table from the files segment
	romdataInitFiles();

	sysLogPrintf(LOG_NOTE, "romdataInit: loaded rom, size = %u", g_RomFileSize);

	return 0;
}

/* ========================================================================
 * Phase 3 Pass C (2026-05-02): drop the in-memory ROM mapping.
 *
 * Boot sequence: romdataInit -> Pass A.2 extract files -> Pass A.4 verify
 * files -> Pass B segment extract -> Pass B segment verify -> Pass C
 * release.  After release the runtime never reads from g_RomFile again;
 * every byte that was resident in g_RomFile is already on disk under
 * data/<romid>/files/ and data/<romid>/segs/, with SHA-256 sidecars.
 *
 * Migration rules:
 *   - SRC_ROM segments with seg->data inside [g_RomFile, g_RomFile+size)
 *     are reloaded from data/<romid>/segs/<name>.bin into a heap buffer.
 *     Pointer adjusted, source flipped to SRC_EXTERNAL.  segstart/segend
 *     mirrors are refreshed via romdataUpdateSegStartEnd.
 *   - SRC_ROM segments whose preprocess function already produced a heap
 *     buffer (seg->data outside g_RomFile range) are left alone -- they
 *     don't dangle when g_RomFile is freed.  Source is normalised to
 *     SRC_EXTERNAL so post-release callers see a uniform state.
 *   - SRC_EXTERNAL segments are left alone.
 *   - Segments with NULL data (unused on this ROM version) are skipped.
 *   - fileSlots[i] in SRC_UNLOADED with .data inside g_RomFile range have
 *     .data NULLed.  romdataFileLoad's per-romid disk fallback (added in
 *     Pass C) takes over the next time that slot is requested.
 *   - fileSlots[i] in SRC_EXTERNAL keep their heap buffer.
 *
 * LOUD-FAIL (sysFatalError) before freeing g_RomFile if any segment can't
 * migrate -- we never half-release.
 * ======================================================================== */

static inline bool romdataPtrInRom(const u8 *p)
{
	if (g_RomFile == NULL || g_RomFileSize == 0 || p == NULL) {
		return false;
	}
	return (p >= g_RomFile) && (p < g_RomFile + g_RomFileSize);
}

s32 romdataReleaseRom(void)
{
	if (g_RomFile == NULL) {
		sysLogPrintf(LOG_NOTE, "ROMRELEASE: g_RomFile already NULL (server build); skipping.");
		return 0;
	}

	s32 segMigrated = 0;
	s32 segNormalised = 0;
	s32 segSkipped = 0;
	s32 fileSlotsCleared = 0;

	for (struct romfile *seg = romSegs; seg->name; ++seg) {
		if (seg->data == NULL) {
			segSkipped++;
			continue;
		}

		if (seg->source == SRC_EXTERNAL) {
			/* Already heap-backed via external file load. Nothing to do. */
			continue;
		}

		if (!romdataPtrInRom(seg->data)) {
			/* SRC_ROM segment with a preprocess() output that returned a
			 * heap buffer -- the pointer survives g_RomFile free.  Just
			 * normalise the source flag so callers see a uniform post-release
			 * state. */
			seg->source = SRC_EXTERNAL;
			segNormalised++;
			continue;
		}

		/* SRC_ROM segment whose data still points into g_RomFile.  Reload
		 * from disk -- the disk image was written by romExtractAllSegments
		 * and verified by romExtractVerifyAllSegments earlier in boot. */
		char segPath[FS_MAXPATH];
		if (romExtractSegmentRelPath(seg->name, segPath, (s32)sizeof(segPath)) <= 0) {
			sysFatalError("LOAD.PASSC: segment \"%s\" path build failed", seg->name);
			return -1;
		}

		/* Phase 3 Pass C close (S607, B-307): allocate the segment buffer
		 * with read-ahead padding so consumers that round their copy
		 * length up (dmaExecWithAutoAlign uses ALIGN16, which over-reads
		 * by up to 15 bytes; challengeLoadConfig in particular memcpys
		 * sizeof(struct mpconfig)=0x11f4 from a 0x11e0 segment) stay
		 * within the heap allocation.
		 *
		 * Pre-Pass-C the same code over-read by the same margin into
		 * g_RomFile, but g_RomFile was a contiguous 32 MB blob so the
		 * over-read landed inside the next segment's bytes -- harmless.
		 * Post-Pass-C each segment is its own heap allocation, and the
		 * over-read walked off the end into unmapped memory.
		 *
		 * fsFileLoad's `size + 1` null terminator is not enough.  We
		 * use sysMemAlloc with PASSC_SEG_PADDING bytes of slack and
		 * stat + raw fread to fill, instead of fsFileLoad. */
		const u32 PASSC_SEG_PADDING = 0x40;
		char segFullBuf[FS_MAXPATH + 1];
		const char *segFull = fsFullPath(segPath, segFullBuf, sizeof(segFullBuf));
		if (segFull == NULL || segFull[0] == '\0') {
			sysFatalError("LOAD.PASSC: segment \"%s\" path resolution failed", seg->name);
			return -1;
		}

		struct stat segSt;
		if (stat(segFull, &segSt) != 0 || segSt.st_size <= 0) {
			sysFatalError("LOAD.PASSC: segment \"%s\" cannot stat \"%s\" -- "
			              "first-launch extraction did not produce this file. "
			              "Reinstall data/%s/segs/.",
			              seg->name, segFull, VERSION_ROMID);
			return -1;
		}

		u32 diskSize = (u32)segSt.st_size;
		u8 *diskData = sysMemAlloc(diskSize + PASSC_SEG_PADDING);
		if (diskData == NULL) {
			sysFatalError("LOAD.PASSC: out of memory allocating %u bytes for segment \"%s\"",
			              diskSize + PASSC_SEG_PADDING, seg->name);
			return -1;
		}

		FILE *segF = fopen(segFull, "rb");
		if (segF == NULL) {
			sysFatalError("LOAD.PASSC: segment \"%s\" cannot open \"%s\"",
			              seg->name, segFull);
			return -1;
		}
		size_t segReadN = fread(diskData, 1, diskSize, segF);
		fclose(segF);
		if (segReadN != diskSize) {
			sysFatalError("LOAD.PASSC: segment \"%s\" short read (got=%zu, expected=%u)",
			              seg->name, segReadN, diskSize);
			return -1;
		}
		/* Zero the read-ahead padding so any over-read returns clean
		 * zero bytes rather than uninitialised heap. */
		memset(diskData + diskSize, 0, PASSC_SEG_PADDING);

		if (diskSize != seg->size) {
			/* Pre-Pass-C the segment may have been preprocess()'d to a
			 * different size; the disk-write captured the post-preprocess
			 * size.  Trust the disk size as the new authoritative size. */
			sysLogPrintf(LOG_NOTE,
			             "ROMRELEASE: segment \"%s\" disk size=%u differs from "
			             "in-memory size=%u; adopting disk size",
			             seg->name, diskSize, seg->size);
			seg->size = diskSize;
		}

		seg->data = diskData;
		seg->source = SRC_EXTERNAL;
		romdataUpdateSegStartEnd(seg);

		/* preprocessAnimations (port/src/preprocess/misc.c) sets the global
		 * _animationsTableRomStart / _animationsTableRomEnd pointers to
		 * (segData + size - 0x38a0) and (segData + size) at romdataInit
		 * time.  Both pointed into g_RomFile pre-Pass-C; without this remap
		 * they dangle and animsInit (called later in mainInit) would crash
		 * inside dmaExec (memcpy from freed memory).  No other preprocess
		 * publishes ROM-relative externs, so the special case is bounded
		 * to "animations" by name. */
		if (strcmp(seg->name, "animations") == 0) {
			extern u8 *_animationsTableRomStart;
			extern u8 *_animationsTableRomEnd;
			if (seg->size >= 0x38a0) {
				_animationsTableRomStart = diskData + (seg->size - 0x38a0);
			} else {
				_animationsTableRomStart = diskData;
			}
			_animationsTableRomEnd = diskData + seg->size;
			sysLogPrintf(LOG_VERBOSE,
			             "ROMRELEASE: animations table externs remapped "
			             "(start=%p, end=%p)",
			             _animationsTableRomStart, _animationsTableRomEnd);
		}

		segMigrated++;

		sysLogPrintf(LOG_VERBOSE,
		             "ROMRELEASE: segment \"%s\" migrated to disk (size=%u, ptr=%p)",
		             seg->name, diskSize, diskData);
	}

	/* Walk every fileSlot.  Two responsibilities:
	 *
	 *  (a) NULL the .data pointer of slots that reference g_RomFile + ofs.
	 *      romdataInitFiles set those up as lazy "ROM offset" pointers, and
	 *      romExtractAllFiles + romExtractVerifyAll converted some to
	 *      SRC_ROM as a side effect of walking the table.  Either way the
	 *      pointer would dangle after the free; romdataFileLoad's Pass C
	 *      disk fallback re-resolves to data/<romid>/files/<name>.bin on
	 *      the next request.
	 *
	 *  (b) Migrate the .name string when it points into g_RomFile.
	 *      romdataInitFiles set fileSlots[i].name to a pointer inside the
	 *      ROM-resident name table (`(const char *)nameOffsets + ofs`
	 *      where nameOffsets = g_RomFile + ...).  Many post-release
	 *      consumers still need the name -- catalogBindPrimaryFromDiskOrRom
	 *      via romExtractRelPathForFilenum + romdataFileGetName,
	 *      romdataFileGetNumForName, the romdataFileLoad Pass C disk
	 *      fallback that builds data/<romid>/files/<name>.bin, etc.
	 *      Without this migration, every such call dereferences freed
	 *      memory and crashes (observed: catalog base game registration
	 *      AV at romExtractBuildRelPath cmpb (%rax) on commit b15cc701).
	 *      We strdup-equivalent each ROM-resident name into a small heap
	 *      copy via sysMemAlloc; literal-string names (CDRCARROLL2 etc.
	 *      set up as compile-time string literals in romdataInitFiles)
	 *      stay as-is because they live in .rdata, not g_RomFile. */
	s32 namesMigrated = 0;
	for (s32 i = 1; i < ROMDATA_MAX_FILES; i++) {
		/* Data field: only SRC_EXTERNAL slots are guaranteed heap-backed.
		 * SRC_UNLOADED / SRC_ROM slots that point into g_RomFile must be
		 * NULLed so romdataFileLoad re-resolves through the per-romid
		 * disk path on the next request. */
		if (fileSlots[i].source != SRC_EXTERNAL) {
			if (fileSlots[i].data && romdataPtrInRom(fileSlots[i].data)) {
				fileSlots[i].data = NULL;
				fileSlots[i].source = SRC_UNLOADED;
				fileSlotsCleared++;
			} else if (fileSlots[i].source == SRC_ROM) {
				/* Defensive: SRC_ROM with data outside ROM range shouldn't
				 * happen at Pass C time (no game code has run yet), but if
				 * it does, normalise to SRC_UNLOADED so the next load
				 * takes the disk path. */
				fileSlots[i].data = NULL;
				fileSlots[i].source = SRC_UNLOADED;
				fileSlotsCleared++;
			}
		}

		/* Name field: unconditional check.  Even SRC_EXTERNAL slots whose
		 * .data was migrated to a heap buffer (via Pass A.2 mod load,
		 * legacy files/<name> mod-override, or the Pass C per-romid disk
		 * fallback that fires inside romdataFileLoad during
		 * romExtractAllFiles' initial walk) keep their .name pointing
		 * into the ROM-resident name table set up by romdataInitFiles --
		 * the load path replaces .data but never .name.  Migrate every
		 * ROM-resident name so post-release readers (catalogBindPrimary
		 * FromDiskOrRom -> romExtractRelPathForFilenum -> romdataFileGet
		 * Name) see heap-owned strings. */
		if (fileSlots[i].name != NULL
		        && romdataPtrInRom((const u8 *)fileSlots[i].name)) {
			const size_t len = strlen(fileSlots[i].name);
			char *copy = sysMemAlloc((u32)(len + 1));
			if (copy == NULL) {
				sysFatalError("ROMRELEASE: out of memory migrating name "
				              "for fileSlots[%d] (\"%.32s\")",
				              i, fileSlots[i].name);
				return -1;
			}
			memcpy(copy, fileSlots[i].name, len + 1);
			fileSlots[i].name = copy;
			namesMigrated++;
		}
	}

	sysMemFree(g_RomFile);
	g_RomFile = NULL;
	g_RomFileSize = 0;

	sysLogPrintf(LOG_NOTE,
	             "ROMRELEASE: g_RomFile released. segs migrated=%d normalised=%d skipped=%d, "
	             "fileSlots cleared=%d, names migrated=%d",
	             segMigrated, segNormalised, segSkipped, fileSlotsCleared, namesMigrated);

	return segMigrated;
}

static inline bool romdataCheckGbcRomContents(const u8 *gbcRomFile, const u32 gbcRomSize)
{
	if (gbcRomSize != GBC_ROM_SIZE) {
		return false;
	}

	// ROM title
	if (memcmp(gbcRomFile + 0x134, "PerfDark   VPDE", 15) != 0) {
		return false;
	}

	// Licensee code
	if (memcmp(gbcRomFile + 0x144, "4Y", 2) != 0) {
		return false;
	}

	// Header and global checksums
	if (gbcRomFile[0x14D] != 0xA1 || gbcRomFile[0x14E] != 0xAD || gbcRomFile[0x14F] != 0x0F) {
		return false;
	}

	return true;
}

s32 romdataCheckGbcRom(void)
{
	if (fsFileSize(GBC_ROM_NAME) < 0) {
		// bail early if it doesn't exist to avoid generating error messages
		return false;
	}

	u32 gbcRomSize = 0;
	u8 *gbcRomFile = fsFileLoad(GBC_ROM_NAME, &gbcRomSize);
	if (!gbcRomFile) {
		return false;
	}

	const bool ret = romdataCheckGbcRomContents(gbcRomFile, gbcRomSize);
	sysMemFree(gbcRomFile);

	if (ret) {
		sysLogPrintf(LOG_NOTE, "romdataCheckGbcRom: valid GBC rom found");
	}

	return ret;
}

s32 romdataFileGetSize(s32 fileNum)
{
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "romdataFileGetSize: invalid file num %d", fileNum);
		return -1;
	}

	// ensure any external files are loaded and we use their size
	if (romdataFileLoad(fileNum, NULL)) {
		return fileSlots[fileNum].size;
	}

	sysLogPrintf(LOG_ERROR, "romdataFileGetSize: could not load file num %d", fileNum);
	return -1;
}

u8 *romdataFileGetData(s32 fileNum)
{
	return romdataFileLoad(fileNum, NULL);
}

u8 *romdataFileLoad(s32 fileNum, u32 *outSize)
{
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "romdataFileLoad: invalid file num %d", fileNum);
		return NULL;
	}

	u8 *out = NULL;

	// try to load external file
	if (fileSlots[fileNum].source == SRC_UNLOADED) {

		/* C-4: catalog is primary asset router — resolve every file through it.
		 * catalogResolveFile() returns a CatalogResolveResult with the routing
		 * decision: mod override (load from path), base-game ROM (catalog_id >= 0),
		 * or unknown to catalog (catalog_id < 0).  Only the mod-override branch
		 * changes behavior; the other two fall through to the legacy files/ + ROM path. */
		{
			CatalogResolveResult r = catalogResolveFile(fileNum);
			if (r.is_mod_override && r.path) {
				u32 size = 0;
				u8 *modOut = fsFileLoad(r.path, &size);
				if (modOut && size) {
					fileSlots[fileNum].data = modOut;
					fileSlots[fileNum].size = size;
					fileSlots[fileNum].source = SRC_EXTERNAL;
					fileSlots[fileNum].numpatches = 0; /* mod file — no ROM patches */
					sysLogPrintf(LOG_NOTE, "CATALOG: file %d → mod override \"%s\" (entry %d)",
					             fileNum, r.path, r.catalog_id);
					if (outSize) {
						*outSize = size;
					}
					return modOut;
				}
				sysLogPrintf(LOG_WARNING, "C-4: catalog override for file %d (%s) failed to load: %s",
				             fileNum, fileSlots[fileNum].name, r.path);
			} else if (r.catalog_id >= 0) {
				{
					const asset_entry_t *ce = assetCatalogGetByIndex(r.catalog_id);
					sysLogPrintf(LOG_NOTE, "CATALOG: %s (%d) → base",
					             ce ? ce->id : "?", fileNum);
				}
			} else {
				sysLogPrintf(LOG_VERBOSE, "CATALOG: file %d → base (not cataloged)", fileNum);
			}
		}

		char tmp[FS_MAXPATH] = { 0 };
		snprintf(tmp, sizeof(tmp), ROMDATA_FILEDIR "/%s", fileSlots[fileNum].name);

		// All Solos in Multi Mod: do not load in solo, coop, counter-op (excluding playable skedar model)
		if (fsFileSize(tmp) > 0 && (!g_NotLoadMod || fileNum == FILE_CSKEDAR2 || fileNum == FILE_GHAND_SKEDAR)) {
			u32 size = 0;
			out = fsFileLoad(tmp, &size);
			if (out && size) {
				sysLogPrintf(LOG_NOTE, "file %d (%s) loaded externally", fileNum, fileSlots[fileNum].name);
				fileSlots[fileNum].data = out;
				fileSlots[fileNum].size = size;
				fileSlots[fileNum].source = SRC_EXTERNAL;
				// external file; do not apply patches to this
				fileSlots[fileNum].numpatches = 0;
			}
		}

		/* Phase 3 Pass C (2026-05-02): with g_RomFile released after
		 * extraction, fall through to the per-romid extracted disk
		 * file before declaring SRC_ROM.  Pass A.2 + A.4 guarantee
		 * data/<romid>/files/<name>.bin exists with verified bytes
		 * by the time gameplay loads run.  Patches are still applied
		 * at romdataFilePreprocess time because the extracted bytes
		 * are pre-patch (raw ROM); numpatches is left untouched. */
		if (fileSlots[fileNum].source == SRC_UNLOADED) {
			char passcPath[FS_MAXPATH];
			if (romExtractRelPathForFilenum(fileNum, passcPath, (s32)sizeof(passcPath)) > 0) {
				const s32 extSize = fsFileSize(passcPath);
				if (extSize > 0) {
					u32 size = 0;
					u8 *passcOut = fsFileLoad(passcPath, &size);
					if (passcOut && size) {
						sysLogPrintf(LOG_NOTE,
						             "file %d (%s) loaded from per-romid disk \"%s\"",
						             fileNum, fileSlots[fileNum].name, passcPath);
						fileSlots[fileNum].data = passcOut;
						fileSlots[fileNum].size = size;
						fileSlots[fileNum].source = SRC_EXTERNAL;
						/* numpatches kept as-is: pre-patch ROM bytes. */
						out = passcOut;
					}
				}
			}
		}

		if (fileSlots[fileNum].source == SRC_UNLOADED) {
			/* Phase 3 Pass C: g_RomFile may have been released; if so,
			 * neither the catalog override, the legacy mod path, nor
			 * the per-romid extracted file produced bytes.  LOUD-FAIL
			 * because returning NULL would just defer the crash. */
			if (g_RomFile == NULL) {
				sysFatalError("LOAD.PASSC: file %d (%s) has no source -- "
				              "g_RomFile released, no override, no per-romid "
				              "disk file.  Reinstall data/%s/files/.",
				              fileNum,
				              fileSlots[fileNum].name ? fileSlots[fileNum].name : "?",
				              VERSION_ROMID);
				return NULL;
			}
			// tried and failed, fall back to ROM
			fileSlots[fileNum].source = SRC_ROM;
		}
	}

	if (!out) {
		out = fileSlots[fileNum].data;
	}

	if (out && outSize) {
		*outSize = fileSlots[fileNum].size;
	}

	return out;
}

void romdataFilePreprocess(s32 fileNum, s32 loadType, u8 *data, u32 size, u32 *outSize)
{
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "romdataFilePreprocess: invalid file num %d", fileNum);
		return;
	}

	if (data && size) {
		if (loadType && loadType < (u32)ARRAYCOUNT(filePreprocFuncs) && filePreprocFuncs[loadType]) {
			// apply patches
			for (u32 i = 0; i < fileSlots[fileNum].numpatches; ++i) {
				const struct romfilepatch *p = &fileSlots[fileNum].patches[i];
				if (!memcmp(data + p->ofs, p->src, p->len)) {
					memcpy(data + p->ofs, p->dst, p->len);
					sysLogPrintf(LOG_NOTE, "file %d (%s) patched at offset 0x%x", fileNum, fileSlots[fileNum].name, p->ofs);
				}
			}
			// then preprocess
			filePreprocFuncs[loadType](data, size, outSize);
		}
	}
}

void romdataFileFree(s32 fileNum)
{
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "fsFileFree: invalid file num %d", fileNum);
		return;
	}

	if (fileSlots[fileNum].source == SRC_EXTERNAL) {
		sysMemFree(fileSlots[fileNum].data);
		fileSlots[fileNum].data = NULL;
	}

	fileSlots[fileNum].source = SRC_UNLOADED;
}

static inline void romdataResetFile(s32 fileNum)
{
	// the file offset table is in the data seg
	const u32 *offsets = (u32 *)(romDataSeg + ROMDATA_FILES_OFS);
	if (offsets + fileNum + 1 < (u32 *)(romDataSeg + romDataSegSize)) {
		const u32 nextofs = PD_BE32(offsets[fileNum + 1]);
		const u32 ofs = PD_BE32(offsets[fileNum]);
		/* Phase 3 Pass C: when g_RomFile has been released, NULL the
		 * data pointer so the next romdataFileLoad takes the
		 * per-romid extracted disk path.  Pre-Pass-C path repoints
		 * into g_RomFile + ofs as before. */
		fileSlots[fileNum].data = g_RomFile ? (g_RomFile + ofs) : NULL;
		fileSlots[fileNum].size = nextofs - ofs;
		fileSlots[fileNum].source = SRC_UNLOADED;
		fileSlots[fileNum].preprocessed = 0;
	}
}

// All Solos in Multi Mod: reset mod files for solo (bg, clipping, pads)
void romdataFileFreeForSolo(void)
{
	romdataResetFile(0x009); // bgdata/bg_azt.seg
	romdataResetFile(0x00a); // bgdata/bg_pete.seg
	romdataResetFile(0x00b); // bgdata/bg_depo.seg
	romdataResetFile(0x00e); // bgdata/bg_dam.seg
	romdataResetFile(0x014); // bgdata/bg_cave.seg
	romdataResetFile(0x017); // bgdata/bg_sho.seg
	romdataResetFile(0x018); // bgdata/bg_eld.seg
	romdataResetFile(0x019); // bgdata/bg_imp.seg
	romdataResetFile(0x01b); // bgdata/bg_lue.seg
	romdataResetFile(0x01c); // bgdata/bg_ame.seg
	romdataResetFile(0x01d); // bgdata/bg_rit.seg
	romdataResetFile(0x01f); // bgdata/bg_ear.seg
	romdataResetFile(0x020); // bgdata/bg_lee.seg
	romdataResetFile(0x024); // bgdata/bg_pam.seg
	romdataResetFile(0x14b); // bgdata/bg_ame_padsZ
	romdataResetFile(0x14c); // bgdata/bg_ame_tilesZ
	romdataResetFile(0x155); // bgdata/bg_azt_padsZ
	romdataResetFile(0x156); // bgdata/bg_azt_tilesZ
	romdataResetFile(0x159); // bgdata/bg_cave_padsZ
	romdataResetFile(0x15a); // bgdata/bg_cave_tilesZ
	romdataResetFile(0x15f); // bgdata/bg_dam_padsZ
	romdataResetFile(0x160); // bgdata/bg_dam_tilesZ
	romdataResetFile(0x161); // bgdata/bg_depo_padsZ
	romdataResetFile(0x162); // bgdata/bg_depo_tilesZ
	romdataResetFile(0x167); // bgdata/bg_ear_padsZ
	romdataResetFile(0x168); // bgdata/bg_ear_tilesZ
	romdataResetFile(0x169); // bgdata/bg_eld_padsZ
	romdataResetFile(0x16a); // bgdata/bg_eld_tilesZ
	romdataResetFile(0x16b); // bgdata/bg_imp_padsZ
	romdataResetFile(0x16c); // bgdata/bg_imp_tilesZ
	romdataResetFile(0x16f); // bgdata/bg_lee_padsZ
	romdataResetFile(0x170); // bgdata/bg_lee_tilesZ
	romdataResetFile(0x175); // bgdata/bg_lue_padsZ
	romdataResetFile(0x176); // bgdata/bg_lue_tilesZ
	romdataResetFile(0x179); // bgdata/bg_pam_padsZ
	romdataResetFile(0x17a); // bgdata/bg_pam_tilesZ
	romdataResetFile(0x17b); // bgdata/bg_pete_padsZ
	romdataResetFile(0x17c); // bgdata/bg_pete_tilesZ
	romdataResetFile(0x17f); // bgdata/bg_rit_padsZ
	romdataResetFile(0x180); // bgdata/bg_rit_tilesZ
	romdataResetFile(0x189); // bgdata/bg_sho_padsZ
	romdataResetFile(0x18a); // bgdata/bg_sho_tilesZ
}

const char *romdataFileGetName(s32 fileNum)
{
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		return NULL;
	}
	return fileSlots[fileNum].name;
}

s32 romdataFileGetNumForName(const char *name)
{
	if (!name || !name[0]) {
		return -1;
	}

	for (s32 i = 0; i < ROMDATA_MAX_FILES; ++i) {
		if (fileSlots[i].name && !strcmp(fileSlots[i].name, name)) {
			return i;
		}
	}

	return -1;
}

u8 *romdataSegGetData(const char *segName)
{
	return romdataGetSeg(segName)->data;
}

u8 *romdataSegGetDataEnd(const char *segName)
{
	struct romfile *seg = romdataGetSeg(segName);
	return seg->data + seg->size;
}

u32 romdataSegGetSize(const char *segName)
{
	return romdataGetSeg(segName)->size;
}

/* ========================================================================
 * Phase 3 Pass B Slices 2/5/6/8/11 (2026-05-02): segment iterator API.
 * romextract.c walks every loaded segment to dump bytes to disk so
 * subsequent boots can skip the ROM mapping for segment loads.
 * ======================================================================== */

s32 romdataSegmentCount(void)
{
	/* romSegs[] is NULL-terminated by the trailing { NULL, NULL, NULL,
	 * NULL, 0, NULL } sentinel.  Count the live entries. */
	s32 n = 0;
	for (struct romfile *seg = romSegs; seg->name; ++seg) {
		n++;
	}
	return n;
}

const u8 *romdataSegmentGetData(s32 idx)
{
	if (idx < 0) return NULL;
	s32 i = 0;
	for (struct romfile *seg = romSegs; seg->name; ++seg, ++i) {
		if (i == idx) return seg->data;
	}
	return NULL;
}

u32 romdataSegmentGetSize(s32 idx)
{
	if (idx < 0) return 0;
	s32 i = 0;
	for (struct romfile *seg = romSegs; seg->name; ++seg, ++i) {
		if (i == idx) return seg->size;
	}
	return 0;
}

const char *romdataSegmentGetName(s32 idx)
{
	if (idx < 0) return "";
	s32 i = 0;
	for (struct romfile *seg = romSegs; seg->name; ++seg, ++i) {
		if (i == idx) return seg->name;
	}
	return "";
}

u32 romdataFileGetEstimatedSize(const u32 size, const u32 loadtype)
{
#ifdef PLATFORM_64BIT
	switch (loadtype) {
	case LOADTYPE_BG:	   return (u32)(size * 1.1f);
	case LOADTYPE_TILES: return (u32)(size * 1.1f);
	case LOADTYPE_LANG:  return (u32)(size * 1.3f);
	case LOADTYPE_SETUP: return (u32)(size * 1.5f);
	case LOADTYPE_PADS:  return (u32)(size * 1.7f);
	case LOADTYPE_MODEL: return (u32)(size * 1.7f);
	case LOADTYPE_GUN: return (u32)(size * 1.7f);
	default:
		sysLogPrintf(LOG_WARNING, "romdataFileGetEstimatedSize: wrong loadtype %d", loadtype);
	}
#endif
	return size;
}
