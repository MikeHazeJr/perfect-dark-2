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

/**
 * Phase 3 Pass C (2026-05-02): drop the in-memory ROM mapping.
 *
 * Called once after romExtractAllSegments + romExtractVerifyAllSegments
 * complete (boot ordering in port/src/main.c).  Migrates every ROM
 * segment whose data still points into g_RomFile (`seg->source ==
 * SRC_ROM`) to a heap-backed copy loaded from
 * data/<romid>/segs/<name>.bin.  NULLs out the .data pointer for
 * every fileSlot still in SRC_UNLOADED (those were "lazy" pointers
 * into g_RomFile + ofs).  Then frees g_RomFile, sets it NULL, and
 * zeros g_RomFileSize.
 *
 * After this call:
 *   - g_RomFile == NULL, g_RomFileSize == 0
 *   - Every romfile segment has seg->source == SRC_EXTERNAL (or its
 *     data was NULL to start with and stays NULL).
 *   - Every fileSlot is SRC_UNLOADED with .data NULL OR SRC_EXTERNAL
 *     with .data on the heap.
 *   - romdataFileLoad routes SRC_UNLOADED slots through the per-romid
 *     extracted disk path (data/<romid>/files/<name>.bin) before the
 *     legacy SRC_ROM fallback fires; with g_RomFile NULL, that
 *     fallback LOUD-FAILs via sysFatalError.
 *
 * LOUD-FAIL: any segment with SRC_ROM whose disk-backed file is
 * missing/short triggers sysFatalError before g_RomFile is freed
 * (we never half-release).
 *
 * Server build (g_RomFile already NULL): early-returns 0.
 *
 * Returns: number of segments migrated to disk.  -1 on infrastructure
 * failure (LOUD-FAIL already emitted).
 */
s32 romdataReleaseRom(void);

#endif
