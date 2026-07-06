/*
 * Hardware write-watchpoint harness (Windows DR0-DR3 debug registers).
 * See port/include/memwatch.h. Enabled by --memp-watch. No-op otherwise.
 * Originally built to hunt the B-952 skedarruins crash; kept as general-purpose
 * heap-corruption infra (which chr to arm is now selected with --memp-watch-chrnum=N
 * rather than a hardcoded heuristic).
 *
 * A write that corrupts a watched address can occur on any thread (game logic OR
 * an async asset-loader thread), and debug registers are PER-THREAD, so each arm
 * spawns a helper thread that enumerates every thread in the process and sets DRn
 * on all of them (suspend -> set -> resume). A process-wide vectored exception
 * handler catches the single-step trap and logs the writing RIP + which thread.
 * memWatchSelfTest() proves the mechanism works in this environment.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <string.h>
#include <stdlib.h>

#include "types.h"
#include "system.h"
#include "memwatch.h"

static void *s_watchAddrs[4];
static s32 s_watchCount = 0;
static PVOID s_veh = NULL;
static s8 s_enabled = -1;

int memWatchEnabled(void)
{
	if (s_enabled < 0) {
		s_enabled = sysArgCheck("--memp-watch") ? 1 : 0;
	}
	return s_enabled;
}

s32 memWatchChrnum(void)
{
	/* Which chr to arm the watchpoint on, from --memp-watch-chrnum=N. Default -1
	 * (arm on no chr) so the harness is inert until a target is named, even with
	 * --memp-watch set. Parsed once. */
	static s32 s_chrnum = -2;
	if (s_chrnum == -2) {
		s_chrnum = sysArgGetInt("--memp-watch-chrnum", -1);
	}
	return s_chrnum;
}

static LONG CALLBACK memWatchVeh(PEXCEPTION_POINTERS ep)
{
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
		DWORD64 dr6 = ep->ContextRecord->Dr6;
		s32 i;
		s32 any = 0;

		for (i = 0; i < 4; i++) {
			if (dr6 & (1ull << i)) {
				any = 1;
				sysLogPrintf(LOG_ERROR,
					"MEMWATCH.HIT: slot%d addr=%p WRITTEN by RIP=%p RSP=%p tid=%lu -- B-952 culprit; symbolize RIP (ImageBase-relative)",
					(int)i, s_watchAddrs[i],
					(void *)ep->ContextRecord->Rip,
					(void *)ep->ContextRecord->Rsp,
					(unsigned long)GetCurrentThreadId());
			}
		}

		if (any) {
			ep->ContextRecord->Dr6 = 0;
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

/* Set (or clear DR7 for) debug slot `slot` = `addr`, 4-byte write watch, on every
 * thread in this process except the calling (helper) thread. Runs on a helper so
 * it can safely suspend the game thread(s). */
static DWORD WINAPI memWatchArmThread(LPVOID param)
{
	void **pack = (void **)param;
	void *addr = pack[0];
	s32 slot = (s32)(intptr_t)pack[1];
	DWORD myid = GetCurrentThreadId();
	DWORD pid = GetCurrentProcessId();
	HANDLE snap;
	THREADENTRY32 te;
	s32 nset = 0;

	free(pack);

	snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		sysLogPrintf(LOG_WARNING, "MEMWATCH: thread snapshot failed for slot%d", (int)slot);
		return 0;
	}

	te.dwSize = sizeof(te);
	if (Thread32First(snap, &te)) {
		do {
			HANDLE h;
			CONTEXT ctx;

			if (te.th32OwnerProcessID != pid || te.th32ThreadID == myid) {
				continue;
			}

			h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
				FALSE, te.th32ThreadID);
			if (!h) {
				continue;
			}

			memset(&ctx, 0, sizeof(ctx));
			ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
			SuspendThread(h);

			if (GetThreadContext(h, &ctx)) {
				switch (slot) {
				case 0: ctx.Dr0 = (DWORD64)addr; break;
				case 1: ctx.Dr1 = (DWORD64)addr; break;
				case 2: ctx.Dr2 = (DWORD64)addr; break;
				case 3: ctx.Dr3 = (DWORD64)addr; break;
				}
				ctx.Dr7 |= (1ull << (slot * 2));            /* local enable Ln */
				ctx.Dr7 &= ~(0xfull << (16 + slot * 4));
				ctx.Dr7 |= (0xdull << (16 + slot * 4));      /* RW=01 write, LEN=11 (4 bytes) */
				ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
				if (SetThreadContext(h, &ctx)) {
					nset++;
				}
			}

			ResumeThread(h);
			CloseHandle(h);
		} while (Thread32Next(snap, &te));
	}

	CloseHandle(snap);
	sysLogPrintf(LOG_NOTE, "MEMWATCH: slot%d addr=%p armed on %d thread(s)", (int)slot, addr, (int)nset);
	return 0;
}

void memWatchAddr(void *addr, s32 chrnum, s32 bodynum)
{
	void **pack;
	s32 slot;
	s32 i;

	if (!memWatchEnabled() || addr == NULL || s_watchCount >= 4) {
		return;
	}

	for (i = 0; i < s_watchCount; i++) {
		if (s_watchAddrs[i] == addr) {
			return;
		}
	}

	if (!s_veh) {
		s_veh = AddVectoredExceptionHandler(1, memWatchVeh);
	}

	slot = s_watchCount;
	s_watchAddrs[slot] = addr;
	s_watchCount++;

	pack = (void **)malloc(2 * sizeof(void *));
	pack[0] = addr;
	pack[1] = (void *)(intptr_t)slot;
	CreateThread(NULL, 0, memWatchArmThread, pack, 0, NULL);

	sysLogPrintf(LOG_NOTE,
		"MEMWATCH.ARM: slot%d addr=%p (chrnum=%d bodynum=%d) -- 4-byte write watch (all threads)",
		(int)slot, addr, (int)chrnum, (int)bodynum);
}

/* Re-arm all recorded watchpoints on every CURRENT thread. The corrupting write
 * appears to be on a thread spawned AFTER the initial arm (async asset loader),
 * which therefore never had the DRs set; calling this periodically re-covers new
 * threads so their write to a watched address traps. */
void memWatchRefresh(void)
{
	s32 i;

	if (!memWatchEnabled() || s_watchCount == 0) {
		return;
	}

	for (i = 0; i < s_watchCount; i++) {
		void **pack = (void **)malloc(2 * sizeof(void *));
		pack[0] = s_watchAddrs[i];
		pack[1] = (void *)(intptr_t)i;
		CreateThread(NULL, 0, memWatchArmThread, pack, 0, NULL);
	}
}

/* Prove the DR mechanism works in this environment: arm on a probe var, wait for
 * the helper to set the DRs, then write it -- a MEMWATCH.HIT must follow. */
void memWatchSelfTest(void)
{
	static volatile u32 s_probe = 0;

	if (!memWatchEnabled()) {
		return;
	}

	sysLogPrintf(LOG_NOTE, "MEMWATCH.SELFTEST: arming probe %p, then writing it", (void *)&s_probe);
	memWatchAddr((void *)&s_probe, -99, -99);
	Sleep(80); /* let the helper set DRs on all threads */
	s_probe = 0xdeadbeefu; /* -> should raise MEMWATCH.HIT if the mechanism works */
	Sleep(20);
	sysLogPrintf(LOG_NOTE, "MEMWATCH.SELFTEST: wrote probe=0x%08x (a MEMWATCH.HIT above means DRs work here)", (unsigned)s_probe);
}
