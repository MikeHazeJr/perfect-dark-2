#include <ultra64.h>
#include <string.h>
#include "constants.h"
#include "bss.h"
#include "lib/dma.h"
#include "lib/memp.h"
#include "data.h"
#include "types.h"

void texInit(void)
{
	extern u8 EXT_SEG _textureslistSegmentRomStart;
	extern u8 EXT_SEG _textureslistSegmentRomEnd;

	u32 len = ((REF_SEG _textureslistSegmentRomEnd - REF_SEG _textureslistSegmentRomStart) + 15) & -16;

	/* c3849 Wave 2: grow the table by the custom-texture slot range. The base
	 * rows + terminator copy unchanged; the custom tail is zero-init, which
	 * makes texLoad's [n]/[n+1] dataoffset peek a defined "no data" for a
	 * custom slot whose bytes come from public image source instead. */
	g_Textures = mempAlloc(len + TEXTURE_CUSTOM_COUNT * sizeof(struct texture),
		MEMPOOL_PERMANENT);
	memset((u8 *)g_Textures + len, 0,
		TEXTURE_CUSTOM_COUNT * sizeof(struct texture));

	dmaExec(g_Textures, (romptr_t) REF_SEG _textureslistSegmentRomStart, len);
}
