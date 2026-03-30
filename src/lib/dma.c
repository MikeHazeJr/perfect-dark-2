#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "lib/crash.h"
#include "lib/dma.h"
#include "data.h"
#include "types.h"

volatile u32 g_DmaNumSlotsBusy;
u32 var80094ae4;
OSIoMesg g_DmaIoMsgs[32];
volatile u8 g_DmaSlotsBusy[32];
OSMesg g_DmaMesgs[32];
OSMesgQueue g_DmaMesgQueue;

u8 g_LoadType = 0;

void dmaInit(void)
{
	s32 i;

	for (i = 0; i < ARRAYCOUNT(g_DmaSlotsBusy); i++) {
		g_DmaSlotsBusy[i] = 0;
	}

	g_DmaNumSlotsBusy = 0;

	osCreateMesgQueue(&g_DmaMesgQueue, g_DmaMesgs, ARRAYCOUNT(g_DmaMesgs));
}

void dmaStart(void *memaddr, romptr_t romaddr, u32 len, bool priority)
{
	bcopy((const void *)romaddr, memaddr, len);
}

#if VERSION >= VERSION_NTSC_1_0
u32 xorDeadbeef(u32 value)
{
	return value ^ 0xdeadbeef;
}

u32 xorDeadbabe(u32 value)
{
	return value ^ 0xdeadbabe;
}

/**
 * This is executed after a DMA transfer. It xors the first 8 words with
 * 0x0330c820, then reads a value from the boot loader (0x340 in ROM) which
 * should be the same value, and xors the memory again with that value.
 */
void dmaCheckPiracy(void *memaddr, u32 len)
{
	if (g_LoadType != LOADTYPE_NONE && len > 128) {
#if PIRACYCHECKS
		u32 value = xorDeadbeef((PAL ? 0x0109082b : 0x0330c820) ^ 0xdeadbeef);
		u32 *ptr = (u32 *)memaddr;
		u32 data;
		u32 devaddr;
		s32 i;

		for (i = 0; i < 8; i++) {
			ptr[i] ^= value;
		}

		devaddr = xorDeadbabe((PAL ? 0xb0000454 : 0xb0000340) ^ 0xdeadbabe);

		osPiReadIo(devaddr, &data);

		for (i = 0; i < 8; i++) {
			ptr[i] ^= data;
		}
#endif
	}
}
#endif

void dmaWait(void)
{
}

void dmaExec(void *memaddr, romptr_t romaddr, u32 len)
{
	dmaStart(memaddr, romaddr, len, false);
	dmaWait();
#if VERSION >= VERSION_NTSC_1_0
	dmaCheckPiracy(memaddr, len);
#endif
}

void dmaExecHighPriority(void *memaddr, romptr_t romaddr, u32 len)
{
	dmaStart(memaddr, romaddr, len, true);
	dmaWait();
#if VERSION >= VERSION_NTSC_1_0
	dmaCheckPiracy(memaddr, len);
#endif
}

/**
 * DMA data from ROM to RAM with automatic alignment.
 *
 * The destination memory address is aligned to 0x10.
 *
 * The ROM address is aligned to 2 bytes (ie. subtract 1 if ROM address is odd).
 * If this is done then the returned memory pointer is bumped forwards by one
 * to compensate. The length of data to be transferred is also increased by one
 * to make it 2-byte aligned.
 *
 * It is assumed that the passed len is 2-byte aligned (ie. an even number).
 *
 * If a length of zero is passed, no DMA is done. This can be used to retrieve
 * the memory address that would have been returned.
 */
void *dmaExecWithAutoAlign(void *memaddr, romptr_t romaddr, u32 len)
{
	uintptr_t alignedrom = ALIGN2(romaddr);
	uintptr_t alignedmem = ALIGN16((uintptr_t) memaddr);
	u32 offset = romaddr - alignedrom; // 0 or 1
	u32 alignedlen = ALIGN16(offset + len);

	if (len == 0) {
		return (void *)(alignedmem + offset);
	}

	dmaExec((void *)alignedmem, alignedrom, alignedlen);

	return (void *)(alignedmem + offset);
}
