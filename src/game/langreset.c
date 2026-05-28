#include <ultra64.h>
#include "constants.h"
#include "game/file.h"
#include "game/lang.h"
#include "bss.h"
#include "lib/memp.h"
#include "data.h"
#include "types.h"
#include "platform.h"
#include "langmanifest.h"

extern u8 *g_LangBuffer;
extern s32 g_LangBufferSize;

void langReset(s32 stagenum)
{
	s32 i;
	s32 size;

	for (i = 0; i < ARRAYCOUNT(g_LangBanks); i++) {
		g_LangBanks[i] = NULL;
	}

	/* Reset manifest tracking: all banks just cleared above */
	langManifestReset();

#if VERSION >= VERSION_PAL_BETA
	// PAL and newer have to support switching languages mid-stage. To do this,
	// langReload iterates the bank pointers and reloads them if they're
	// non-zero. Here it's using langReload to do the initial load by setting
	// the desired banks to dummy (non-zero) values so langReload will load them.
	g_LangBanks[LANGBANK_GUN] = (void *)1;
	g_LangBanks[LANGBANK_MPMENU] = (void *)1;
	g_LangBanks[LANGBANK_PROPOBJ] = (void *)1;
	g_LangBanks[LANGBANK_MPWEAPONS] = (void *)1;
	g_LangBanks[LANGBANK_OPTIONS] = (void *)1;
	g_LangBanks[LANGBANK_MISC] = (void *)1;

	if (stagenum == STAGE_CREDITS) {
		g_LangBanks[LANGBANK_TITLE] = (void *)1;
	}

	if (stagenum == STAGE_CITRAINING) {
		size = 108000;
	} else {
		size = 68000;
	}

#ifdef PLATFORM_64BIT
	size *= 2;
#endif

	g_LangBuffer = mempAlloc(ALIGN16(size), MEMPOOL_STAGE);
	g_LangBufferSize = size;

	langReload();

	/* Record the banks that langReload() just loaded (those we pre-marked above) */
	langManifestRecordBank(LANGBANK_GUN);
	langManifestRecordBank(LANGBANK_MPMENU);
	langManifestRecordBank(LANGBANK_PROPOBJ);
	langManifestRecordBank(LANGBANK_MPWEAPONS);
	langManifestRecordBank(LANGBANK_OPTIONS);
	langManifestRecordBank(LANGBANK_MISC);
	if (stagenum == STAGE_CREDITS) {
		langManifestRecordBank(LANGBANK_TITLE);
	}
#else
	// Versions prior to PAL load the language directly
	langLoad(LANGBANK_GUN);
	langManifestRecordBank(LANGBANK_GUN);

	langLoad(LANGBANK_MPMENU);
	langManifestRecordBank(LANGBANK_MPMENU);

	langLoad(LANGBANK_PROPOBJ);
	langManifestRecordBank(LANGBANK_PROPOBJ);

	langLoad(LANGBANK_MPWEAPONS);
	langManifestRecordBank(LANGBANK_MPWEAPONS);

	langLoad(LANGBANK_OPTIONS);
	langManifestRecordBank(LANGBANK_OPTIONS);

	langLoad(LANGBANK_MISC);
	langManifestRecordBank(LANGBANK_MISC);

	if (stagenum == STAGE_CREDITS) {
		langLoad(LANGBANK_TITLE);
		langManifestRecordBank(LANGBANK_TITLE);
	}
#endif
}
