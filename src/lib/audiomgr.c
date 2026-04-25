#include <ultra64.h>
#include "lib/boot.h"
#include "lib/sched.h"
#include "naudio/n_synthInternals.h"
#include "constants.h"
#include "bss.h"
#include "lib/audiodma.h"
#include "lib/lib_2fc60.h"
#include "lib/profile.h"
#include "lib/libc/ll.h"
#include "data.h"
#include "types.h"

u32 var80091560;
u32 var80091564;
u64 var80091568;
u64 var80091570;
u64 var80091578;
u64 var80091580;
u64 var80091588;
u64 var80091590;
u32 var80091598;
u32 var8009159c;
u32 var800915a0;
u32 var800915a4;
u32 var800915a8;
u32 var800915ac;
u32 var800915b0;
u32 var800915b4;
u32 var800915b8;
u32 var800915bc;
u32 var800915c0;
u32 var800915c4;
AMAudioMgr g_AudioManager;
OSScClient g_AudioSchedClient;
u32 var800918dc;
u32 g_AmgrFreqPerTick;
u32 var800918e4;
s32 var800918e8;
s32 var800918ec;
void *g_AudioSp;

u32 var8005cf90 = 0x00000000;
u8 var8005cf94 = 1;

void amgrHandleDoneMsg(AudioInfo *info);
void amgrHandleFrameMsg(AudioInfo *info, AudioInfo *previnfo);
void amgrMain(void *arg);

void amgrInit(void)
{
	g_AudioSp = bootAllocateStack(THREAD_AUDIO, STACKSIZE_AUDIO);
}

#if VERSION >= VERSION_PAL_BETA
void amgrCreate(ALSynConfig *config, u32 *settings)
#else
void amgrCreate(ALSynConfig *config)
#endif
{
	f32 freqpertick;
	s32 i;

	config->outputRate = osAiSetFrequency(22020);
	config->dmaproc = admaNew;

#if VERSION >= VERSION_JPN_FINAL
	freqpertick = settings[1] * (f32)config->outputRate / 30.0f;

#elif VERSION >= VERSION_PAL_BETA
	freqpertick = settings[1] * (f32)config->outputRate / 25.0f;
#else
	freqpertick = config->outputRate / 30.0f;

#endif

	g_AmgrFreqPerTick = (s32)freqpertick;

	if ((f32)g_AmgrFreqPerTick < freqpertick) {
		g_AmgrFreqPerTick++;
	}

	g_AmgrFreqPerTick = g_AmgrFreqPerTick / SAMPLES * SAMPLES + SAMPLES;
	var800918dc = g_AmgrFreqPerTick - SAMPLES;
	var800918e4 = g_AmgrFreqPerTick + 80;
	var8005cf94 = 0;

	admaInit();

	osCreateMesgQueue(&g_AudioManager.audioReplyMsgQ, g_AudioManager.audioReplyMsgBuf, ARRAYCOUNT(g_AudioManager.audioFrameMsgBuf));
	osCreateMesgQueue(&g_AudioManager.audioFrameMsgQ, g_AudioManager.audioFrameMsgBuf, ARRAYCOUNT(g_AudioManager.audioFrameMsgBuf));

	var800918ec = 2000;

	for (i = 0; i < ARRAYCOUNT(g_AudioManager.ACMDList); i++) {
		g_AudioManager.ACMDList[i] = alHeapAlloc(&g_SndHeap, 1, var800918ec * sizeof(Acmd));
	}

	for (i = 0; i < ARRAYCOUNT(g_AudioManager.audioInfo); i++) {
		g_AudioManager.audioInfo[i] = alHeapAlloc(&g_SndHeap, 1, sizeof(AudioInfo));
		g_AudioManager.audioInfo[i]->frameSamples = 0;
		g_AudioManager.audioInfo[i]->data = alHeapAlloc(&g_SndHeap, 1, PAL ? 3688 : 1024 * 3);
	}

#ifndef AVOID_UB // these will be used after this scope ends, triggering a big boom
	{
#endif
		s32 sp590[] = { 0x00000001, 0x000014a0, 0x00000000, 0x00001358, 0x00004d0c, 0x00000000, 0x000053ff, 0x00000000, 0x00000000, 0x00000000 };
		s32 sp568[] = { 0x00000001, 0x000001b8, 0x00000000, 0x00000068, 0x00004000, 0x00000000, 0x00007fff, 0x00001db0, 0x00001b58, 0x00000000 };
		s32 sp540[] = { 0x00000001, 0x000001b8, 0x00000000, 0x00000068, 0x00000000, 0x00005fff, 0x00007fff, 0x0000017c, 0x000001f4, 0x00000000 };
		s32 sp478[] = { 0x00000006, 0x00001868, 0x00000000, 0x00000160, 0x00002666, 0xffffd99a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000188, 0x00000640, 0x0000235e, 0xffffd99a, 0x0000750c, 0x00000000, 0x00000000, 0x00000bca, 0x00000318, 0x000009f8, 0x00004000, 0xffffc000, 0x00006d78, 0x00000000, 0x00000000, 0x00001286, 0x00000c78, 0x000015d8, 0x0000521a, 0xffffc000, 0x0000724f, 0x00000000, 0x00000000, 0x00001650, 0x00000d28, 0x000012c0, 0x00002143, 0xffffe000, 0x00005de4, 0x00000000, 0x00000000, 0x00002286, 0x00000000, 0x00001720, 0x000032c8, 0xffffcd38, 0x00000000, 0x00000000, 0x00000000, 0x00004500 };
		s32 sp430[] = { 0x00000002, 0x000008b0, 0x00000600, 0x00000760, 0x00007142, 0x00000000, 0x00005bff, 0x00000000, 0x00000000, 0x00007bc9, 0x00000000, 0x00000528, 0x00005f27, 0xffffb288, 0x00007ef1, 0x00000000, 0x00000001, 0x000066bb };
		s32 sp3c8[] = { 0x00000003, 0x00000b40, 0x00000000, 0x00000160, 0x00002666, 0xffffd99a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000188, 0x00000640, 0x0000235e, 0xffffd99a, 0x000016f2, 0x00000000, 0x00000000, 0x00000bca, 0x00000318, 0x000009f8, 0x00004000, 0xffffc000, 0x0000186b, 0x00000000, 0x00000000, 0x00001286 };
		s32 sp360[] = { 0x00000003, 0x00000b40, 0x00000000, 0x00000160, 0x00002666, 0xffffd99a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000188, 0x00000640, 0x0000235e, 0xffffd99a, 0x000016f2, 0x00000000, 0x00000000, 0x00000bca, 0x00000318, 0x000009f8, 0x00004000, 0xffffc000, 0x0000186b, 0x00000000, 0x00000000, 0x00001286 };
		s32 sp2f8[] = { 0x00000003, 0x00000898, 0x00000000, 0x000004a0, 0x00002666, 0xffffd99a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000001a0, 0x00000340, 0x00000ccc, 0xfffff334, 0x00003fff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000528, 0x00001388, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00005000 };
		s32 sp270[] = { 0x00000004, 0x00000898, 0x00000000, 0x000005a8, 0x00002666, 0xffffd99a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000001e0, 0x000004a0, 0x00000ccc, 0xfffff334, 0x00003fff, 0x00000000, 0x00000000, 0x00000000, 0x000005a8, 0x000007d0, 0x00000ccc, 0xfffff334, 0x00003fff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000810, 0x00001f40, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00005000 };
		s32 sp248[] = { 0x00000001, 0x00001130, 0x00000000, 0x00000f60, 0x00002ee0, 0x00000000, 0x00007fff, 0x00000000, 0x00000000, 0x00000000 };
		s32 sp1c0[] = { 0x00000004, 0x00000e98, 0x000000c0, 0x00000188, 0x00002666, 0xffffd99a, 0x00003484, 0x00000000, 0x00000000, 0x00000000, 0x000001b8, 0x00000580, 0x00004000, 0xffffc000, 0x000019eb, 0x00000000, 0x00000000, 0x00000000, 0x00000a50, 0x00000b98, 0x00002000, 0xffffe000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000cb8, 0x00004650, 0xffffb9b0, 0x00000000, 0x0000017c, 0x0000000a, 0x00000000 };
		s32 sp198[] = { 0x00000001, 0x00000528, 0x00000000, 0x00000448, 0x00003334, 0x00000000, 0x00007335, 0x00000000, 0x00000000, 0x00000000 };
		s32 sp090[] = { 0x00000008, 0x00001b80, 0x00000000, 0x000000c0, 0x00002666, 0xffffd99a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000000c0, 0x00000188, 0x00002666, 0xffffd99a, 0x00002b84, 0x00000000, 0x00000000, 0x00000000, 0x00000370, 0x00000b00, 0x00004000, 0xffffc000, 0x000011eb, 0x00000000, 0x00000000, 0x00000000, 0x00000420, 0x00000840, 0x00002000, 0xffffe000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000dc0, 0x00001810, 0x00004000, 0xffffc000, 0x000011eb, 0x00000000, 0x00000000, 0x00000000, 0x00000e70, 0x000014a0, 0x00002000, 0xffffe000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000014a0, 0x00001738, 0x00002000, 0xffffe000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00001970, 0x000032c8, 0xffffcd38, 0x00000000, 0x0000017c, 0x0000000a, 0x00000000 };
		s32 sp068[] = { 0x00000001, 0x00000a50, 0x00000000, 0x00000898, 0x00003334, 0x00000000, 0x00007335, 0x00000000, 0x00000000, 0x00000000 };
		s32 sp040[] = { 0x00000001, 0x00000148, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 };

		config->params[0] = (s32 *) sp090;

		if (g_SndMaxFxBusses >= 2) {
			for (i = 1; i < g_SndMaxFxBusses; i++) {
				config->params[i] = (s32 *) sp068;
			}
		}
#ifndef AVOID_UB
	}
#endif

	n_alInit(&g_AudioManager.g, config);
	func00030bfc(0, 60);
	osCreateThread(&g_AudioManager.thread, THREAD_AUDIO, &amgrMain, 0, g_AudioSp, THREADPRI_AUDIO);
}

s8 g_AudioIsThreadRunning = false;

void amgrStartThread(void)
{
	osStartThread(&g_AudioManager.thread);
	g_AudioIsThreadRunning = true;
}

OSMesgQueue *amgrGetFrameMesgQueue(void)
{
	return &g_AudioManager.audioFrameMsgQ;
}

/**
 * This doesn't set g_AudioIsThreadRunning to false, but that's okay because
 * this is only called when resetting the console, and when that happens the
 * variable is likely reset too.
 */
void amgrStopThread(void)
{
	if (g_AudioIsThreadRunning) {
		osStopThread(&g_AudioManager.thread);
	}
}

extern u32 g_AdmaCurFrame;

void amgrMain(void *arg)
{
	s32 count = 0;
	bool done = false;
	s16 *msg = NULL;
	AudioInfo *info = NULL;

	static u32 var8005d514 = 1;

#if PAL
	// Receive retrace events every second retrace
	osScAddClient(&g_Sched, &g_AudioSchedClient, &g_AudioManager.audioFrameMsgQ, true);
#else
	// 8MB - Receive retrace events every second retrace
	// 4MB - Receive retrace events every retrace due to smaller command buffer
	osScAddClient(&g_Sched, &g_AudioSchedClient, &g_AudioManager.audioFrameMsgQ, true);
#endif

	while (!done) {
		osRecvMesg(&g_AudioManager.audioFrameMsgQ, (OSMesg *) &msg, OS_MESG_BLOCK);

		switch (*msg) {
		case OS_SC_RSP_MSG:
			var80091588 = osGetTime();
			profileSetMarker(PROFILE_AUDIOFRAME_START);
			amgrHandleFrameMsg(g_AudioManager.audioInfo[g_AdmaCurFrame % 3], info);
			admaReceiveAll();

			count++;
			profileSetMarker(PROFILE_AUDIOFRAME_END);

			var80091590 = osGetTime();
			var80091570 = var80091590 - var80091588;

			if (count % 240 == 0) {
				var80091578 = var80091580 / 240;
				var80091580 = 0; var80091568 = 0;
			} else {
				var80091580 = (var80091580 + var80091590) - var80091588;
			}

			if (var80091568 < var80091590 - var80091588) {
				var80091568 = var80091590 - var80091588;
			}

			if (var8005d514 == 0) {
				osRecvMesg(&g_AudioManager.audioReplyMsgQ, (OSMesg *) &info, OS_MESG_BLOCK);
			}

			var8005d514 = 0;
			amgrHandleDoneMsg(info);
			break;
		case OS_SC_PRE_NMI_MSG:
			done = true;
			break;
		case OS_SC_QUIT_MSG:
			done = true;
			break;
		}
	}

	n_alClose(&g_AudioManager.g);
}

void amgrHandleFrameMsg(AudioInfo *info, AudioInfo *previnfo)
{
	u32 somevalue;
	s16 *outbuffer;
	Acmd *datastart;
	Acmd *cmd;

	static OSScTask *g_AmgrCurrentCmdList;

	extern u8 aspTextStart;
	extern u8 aspDataStart;

	if (g_AmgrCurrentCmdList) {
		schedSubmitTask(&g_Sched, g_AmgrCurrentCmdList);
	}

	admaBeginFrame();

	somevalue = osAiGetLength() / 4;
	// HACK: only allow small frames if really needed
	if (somevalue < 1100) {
		somevalue = 248;
	}
	datastart = g_AudioManager.ACMDList[var8005cf90];
	outbuffer = (s16 *) osVirtualToPhysical(info->data);

	if (previnfo) {
		osAiSetNextBuffer(previnfo->data, previnfo->frameSamples * 4);
	}

	if (somevalue > 248 && var8005cf94 == 0) {
		info->frameSamples = var800918dc;
		var8005cf94 = 2;
	} else {
		info->frameSamples = g_AmgrFreqPerTick;

		if (var8005cf94 != 0) {
			var8005cf94--;
		}
	}

	cmd = n_alAudioFrame(datastart, &var800918e8, outbuffer, info->frameSamples);

	g_AmgrCurrentCmdList = &info->task;

	g_AmgrCurrentCmdList->next = NULL;
	g_AmgrCurrentCmdList->msgQ = &g_AudioManager.audioReplyMsgQ;
	g_AmgrCurrentCmdList->msg = info;
	g_AmgrCurrentCmdList->flags = OS_SC_NEEDS_RSP;
	g_AmgrCurrentCmdList->list.t.type = M_AUDTASK;
	g_AmgrCurrentCmdList->list.t.flags = 0;
	g_AmgrCurrentCmdList->list.t.data_ptr = (u64 *) datastart;
	g_AmgrCurrentCmdList->list.t.data_size = (cmd - datastart) * sizeof(Acmd);
	g_AmgrCurrentCmdList->list.t.yield_data_ptr = NULL;
	g_AmgrCurrentCmdList->list.t.yield_data_size = 0;

	var8005cf90 ^= 1;
}

void amgrHandleDoneMsg(AudioInfo *info)
{
	static bool firsttime = true;

	if ((s32)osAiGetLength() >> 2 == 0 && firsttime == false) {
		firsttime = false;
	}
}

void amgrFrame(void)
{
	static AudioInfo *previnfo = NULL;
	static s32 count = 0;

	var80091588 = osGetTime();

	AudioInfo *info = g_AudioManager.audioInfo[g_AdmaCurFrame % 3];

	admaBeginFrame();

	const s32 somevalue = osAiGetLength() / 4;
	Acmd *datastart = g_AudioManager.ACMDList[var8005cf90];
	s16 *outbuffer = (s16 *) osVirtualToPhysical(info->data);

	if (previnfo) {
		osAiSetNextBuffer(previnfo->data, previnfo->frameSamples * 4);
	}

	/* B-141 fix: three-tier audio production pacing for the PC port so the SDL
	 * audio queue can absorb main-thread stalls without draining to zero.
	 *
	 * 22050 Hz stereo, 60 Hz game tick.  Consumer = 367.5 stereo samples/frame.
	 * info->data is sized to 3072 bytes = 768 stereo samples, so a fast-fill
	 * push of 736 is safely in-bounds.
	 *
	 *   tier 1 (queue > 3000):   push 184  — brake, drains 183.5/frame
	 *   tier 2 (queue 2500-3000): push 368 — steady, drifts +0.5/frame
	 *   tier 3 (queue < 2500):   push 736 — fast-fill, fills +368/frame
	 *
	 * Why the change?  Original code targets ~1100 (50ms) with 368 everywhere
	 * below the threshold.  Two problems: (1) 50ms cushion is smaller than
	 * typical main-thread hitches (50-100ms), causing underruns.  (2) From a
	 * cold queue (stage load / startup) the +0.5/frame drift needs 36s to
	 * reach 1100 and 100s to reach 3000 — unplayable.  With tier 3, a cold
	 * queue fills to 2500 in 2500/368 ~= 7 frames = 115ms.  Steady state
	 * oscillates between 2800 and 3000 (~128-136ms cushion), so a 100ms
	 * hitch drains to ~700 stereo samples, well above the 128-sample
	 * underrun threshold.  PAL systems retain the original 368+184=552/frame
	 * (already enough for 50Hz). */
	if (somevalue > 3000 && var8005cf94 == 0) {
		// already a lot queued, render 1 naudio frame (184 samples) this frame
		info->frameSamples = 184;
		var8005cf94 = 2;
	} else {
		// have space in audio queue, render 2 naudio frames this frame (and 1 extra on PAL)
		info->frameSamples = 368 + PAL * 184;

		/* B-141 fast-fill (NTSC only): cushion below steady-state target,
		 * push 4 naudio chunks (736 stereo samples) instead of 2.  This
		 * rebuilds cushion from a cold queue in ~100ms and recovers from
		 * hitches in ~1 sec instead of tens of seconds. */
		if (somevalue < 2500 && var8005cf94 == 0 && !PAL) {
			info->frameSamples = 736;
		}

		if (var8005cf94 != 0) {
			var8005cf94--;
		}
	}

	Acmd *cmd = n_alAudioFrame(datastart, &var800918e8, outbuffer, info->frameSamples);

	var8005cf90 ^= 1;

	admaReceiveAll();

	previnfo = info;

	count++;

	var80091590 = osGetTime();
	var80091570 = var80091590 - var80091588;

	if (count % 240 == 0) {
		var80091578 = var80091580 / 240;
		var80091580 = 0; var80091568 = 0;
	} else {
		var80091580 = (var80091580 + var80091590) - var80091588;
	}

	if (var80091568 < var80091590 - var80091588) {
		var80091568 = var80091590 - var80091588;
	}
}
