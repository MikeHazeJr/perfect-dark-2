#ifndef _IN_ROMDATA_H
#define _IN_ROMDATA_H

#include <PR/ultratypes.h>

extern const char *g_RomName;
extern u8 *g_RomFile;
extern u32 g_RomFileSize;

s32 romdataInit(void);

u8 *romdataFileLoad(s32 fileNum, u32 *outSize);
void romdataFilePreprocess(s32 fileNum, s32 loadType, u8 *data, u32 size, u32 *outSize);
void romdataFileFree(s32 fileNum);
void romdataFileFreeForSolo(void); // All Solos in Multi Mod
const char *romdataFileGetName(s32 fileNum);

u8 *romdataFileGetData(s32 fileNum);
s32 romdataFileGetSize(s32 fileNum);

s32 romdataFileGetNumForName(const char *name);

u8 *romdataSegGetData(const char *segName);
u8 *romdataSegGetDataEnd(const char *segName);
u32 romdataSegGetSize(const char *segName);
u32 romdataFileGetEstimatedSize(const u32 size, const u32 loadtype);

/**
 * Phase 3 Pass B Slices 2/5/6/8/11 (2026-05-02): segment iterator API.
 *
 * Used by romextract.c::romExtractAllSegments to walk every loaded
 * ROM segment after romdataInit and dump the in-memory bytes to
 * data/<romid>/segs/<name>.bin so subsequent boots can read segments
 * from disk and skip the ROM mapping.
 *
 * Iterator semantics: indexes are dense in [0, romdataSegmentCount()).
 * Per-index getters return data/size/name for that slot, or NULL/0/""
 * if the slot is past the end or its data was never resolved (segment
 * absent on this ROM version).
 *
 * The underlying romSegs[] table stays static; these accessors are the
 * only public window onto it.
 */
s32 romdataSegmentCount(void);
const u8 *romdataSegmentGetData(s32 idx);
u32 romdataSegmentGetSize(s32 idx);
const char *romdataSegmentGetName(s32 idx);

s32 romdataCheckGbcRom(void);

#endif
