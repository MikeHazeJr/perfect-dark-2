#ifndef _IN_GAME_GAME_166E40_H
#define _IN_GAME_GAME_166E40_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

#include "romdata.h"

romptr_t fileGetRomAddress(s32 filenum);
s32 fileGetRomSize(s32 filenum);
void filesInit(void);
u32 fileGetInflatedSize(s32 filenum, u32 loadtype);
/* Internal ROM-load entry points used by the Asset Provider dispatcher
 * (port/src/assetload.c). Game code calls assetLoadRomToNew() /
 * assetLoadRomToAddr() in port/include/assetload.h instead — these workers
 * exist so the dispatcher's RomProvider fast-path doesn't recurse through
 * its own public wrappers. */
void *fileLoadRomToNew(s32 filenum, u32 method, u32 loadtype);
void *fileLoadRomToAddr(s32 filenum, u32 method, u8 *ptr, u32 size);
u32 fileGetLoadedSize(s32 filenum);
u32 fileGetAllocationSize(s32 filenum);
void fileSetSize(s32 filenum, void *ptr, u32 size, bool reallocate);
void filesStop(u8 arg0);
void func0f167330(void);

#endif
