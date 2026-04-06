#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "system.h"
#include "platform.h"

#define CRASH_LOG_FNAME "pd.crash.log"
#define CRASH_MAX_MSG 8192
#define CRASH_MAX_SYM 256
#define CRASH_MAX_FRAMES 32
#define CRASH_MSG(...) \
	if (msglen < CRASH_MAX_MSG) msglen += snprintf(msg + msglen, CRASH_MAX_MSG - msglen, __VA_ARGS__)

#if defined(PLATFORM_WIN32)

#include <windows.h>
#include <dbghelp.h>
#include <inttypes.h>
#include <excpt.h>
#include <signal.h>

// NOTE: game builds with gcc, which means we have no PDBs for the windows version
// this means that you generally won't get any symbol names in the main executable

static LPTOP_LEVEL_EXCEPTION_FILTER prevExFilter;

static void *crashGetModuleBase(const void *addr)
{
	HMODULE h = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, addr, &h);
	return (void *)h;
}

static void crashStackTrace(char *msg, PEXCEPTION_POINTERS exinfo)
{
	CONTEXT context = *exinfo->ContextRecord;
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	SymSetOptions(SymGetOptions() | SYMOPT_DEBUG | SYMOPT_LOAD_LINES);
	SymInitialize(process, NULL, TRUE);

	STACKFRAME64 stackframe;
	memset(&stackframe, 0, sizeof(stackframe));

	DWORD image;
#ifdef PLATFORM_X86
	image = IMAGE_FILE_MACHINE_I386;
	stackframe.AddrPC.Offset = context.Eip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Ebp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Esp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#elif defined(PLATFORM_X86_64)
	image = IMAGE_FILE_MACHINE_AMD64;
	stackframe.AddrPC.Offset = context.Rip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Rsp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Rsp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#else
	snprintf(msg, CRASH_MAX_MSG, "no stack trace available on this arch\n");
	return;
#endif

	DWORD disp = 0;
	DWORD64 disp64 = 0;
	IMAGEHLP_LINE64 line;
	DWORD msglen = 0;

	CRASH_MSG("EXCEPTION: 0x%08lx\n", exinfo->ExceptionRecord->ExceptionCode);
	CRASH_MSG("PC: %p", exinfo->ExceptionRecord->ExceptionAddress);
	if (SymGetLineFromAddr64(process, (uintptr_t)exinfo->ExceptionRecord->ExceptionAddress, &disp, &line)) {
		CRASH_MSG(": %s:%lu+%lu", line.FileName, line.LineNumber, disp);
	}
	CRASH_MSG("\nMODULE: [%p]\n", crashGetModuleBase(exinfo->ExceptionRecord->ExceptionAddress));
	CRASH_MSG("MAIN MODULE: [%p]\n", crashGetModuleBase(crashInit));
	CRASH_MSG("\nBACKTRACE:\n");

	char symbuf[sizeof(SYMBOL_INFO) + CRASH_MAX_SYM * sizeof(TCHAR)];
	PSYMBOL_INFO sym = (PSYMBOL_INFO)symbuf;
	sym->SizeOfStruct = sizeof(*sym);
	sym->MaxNameLen = CRASH_MAX_SYM;

	s32 i;
	for (i = 0; i < CRASH_MAX_FRAMES; ++i) {
		const BOOL res = StackWalk64(image, process, thread, &stackframe, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
		if (!res) {
			break;
		}

		CRASH_MSG("#%02d: %p", i, (void *)(uintptr_t)stackframe.AddrPC.Offset);

		if (SymFromAddr(process, stackframe.AddrPC.Offset, &disp64, sym)) {
			CRASH_MSG(": %s+%llu", sym->Name, disp64);
		} else if (process, stackframe.AddrPC.Offset) {
			const uintptr_t modbase = (uintptr_t)crashGetModuleBase((void *)(uintptr_t)stackframe.AddrPC.Offset);
			const uintptr_t modofs = (uintptr_t)stackframe.AddrPC.Offset - modbase;
			CRASH_MSG(": [%p]+%p", (void *)modbase, (void *)modofs);
		}

		if(SymGetLineFromAddr64(process, stackframe.AddrPC.Offset, &disp, &line)) {
			CRASH_MSG(" (%s:%lu+%lu)", line.FileName, line.LineNumber, disp);
		}

		CRASH_MSG("\n");
	}

	if (i <= 1) {
		CRASH_MSG("no information\n");
	} else if (i == CRASH_MAX_FRAMES) {
		CRASH_MSG("...\n");
	}

	SymCleanup(process);
}

/**
 * Lightweight first-chance handler via AddVectoredExceptionHandler.
 * Uses STATIC buffers only — no stack allocations beyond a few locals.
 * This catches stack overflows that the UEF (crashHandler) would miss
 * because the UEF's own 8 KB stack allocation overflows the guard page.
 */
static char s_VehMsg[512];
static volatile long s_VehFired = 0;

static long __stdcall crashVectoredHandler(PEXCEPTION_POINTERS exinfo)
{
	const DWORD code = exinfo->ExceptionRecord->ExceptionCode;

	/* Only handle fatal exceptions (not breakpoints, single-step, etc.) */
	if (code != EXCEPTION_ACCESS_VIOLATION
			&& code != EXCEPTION_STACK_OVERFLOW
			&& code != EXCEPTION_ILLEGAL_INSTRUCTION
			&& code != EXCEPTION_INT_DIVIDE_BY_ZERO
			&& code != EXCEPTION_PRIV_INSTRUCTION) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	/* Prevent re-entry */
	if (InterlockedCompareExchange(&s_VehFired, 1, 0) != 0) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	/* For stack overflow: reset the guard page so we can use minimal stack */
	if (code == EXCEPTION_STACK_OVERFLOW) {
		_resetstkoflw();
	}

	/* Write a minimal crash line directly to the log file using static buffer.
	 * This avoids the 2 KB stack allocation in sysLogPrintf. */
	{
		const void *pc = exinfo->ExceptionRecord->ExceptionAddress;
		const void *modbase = crashGetModuleBase(pc);
		const uintptr_t offset = (uintptr_t)pc - (uintptr_t)modbase;
		const char *codename = "EXCEPTION";
		if (code == EXCEPTION_STACK_OVERFLOW) codename = "STACK_OVERFLOW";
		else if (code == EXCEPTION_ACCESS_VIOLATION) codename = "ACCESS_VIOLATION";

		snprintf(s_VehMsg, sizeof(s_VehMsg),
			"FATAL: %s PC=%p (+0x%llx) CODE=0x%08lx MODULE=%p RSP=%p\n",
			codename, pc, (unsigned long long)offset, code, modbase,
#ifdef PLATFORM_X86_64
			(void *)(uintptr_t)exinfo->ContextRecord->Rsp
#else
			(void *)(uintptr_t)exinfo->ContextRecord->Esp
#endif
		);

		/* Direct file write — sysLogPrintf uses too much stack */
		const char *logpath = sysLogGetPath();
		if (logpath && logpath[0]) {
			FILE *f = fopen(logpath, "ab");
			if (f) {
				fprintf(f, "%s", s_VehMsg);
				fclose(f);
			}
		}

		/* Also write to stderr */
		fputs(s_VehMsg, stderr);
		fflush(stderr);
	}

	/* Let the normal UEF run next for full stack trace (if there's stack space) */
	return EXCEPTION_CONTINUE_SEARCH;
}

static long __stdcall crashHandler(PEXCEPTION_POINTERS exinfo)
{
	/* Use STATIC buffer instead of stack-allocated 8 KB.
	 * The old stack allocation caused the crash handler itself to
	 * overflow when handling STATUS_STACK_OVERFLOW. */
	static char msg[CRASH_MAX_MSG + 1];
	memset(msg, 0, sizeof(msg));

	if (IsDebuggerPresent()) {
		if (prevExFilter) {
			return prevExFilter(exinfo);
		}
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	sysLogPrintf(LOG_ERROR, "FATAL: Crashed: PC=%p CODE=0x%08lx", exinfo->ExceptionRecord->ExceptionAddress, exinfo->ExceptionRecord->ExceptionCode);

	fflush(stderr);
	fflush(stdout);

	crashStackTrace(msg, exinfo);

	// open log file for the crash dump if one hasn't been opened yet
	if (!sysLogIsOpen()) {
		FILE *f = fopen(CRASH_LOG_FNAME, "wb");
		if (f) {
			fprintf(f, "Crash!\n\n%s", msg);
			fclose(f);
		}
	}

	sysFatalError("Crash!\n\n%s", msg);

	return EXCEPTION_CONTINUE_EXECUTION;
}

#elif defined(PLATFORM_LINUX)

#include <ucontext.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <ctype.h>
#include <dlfcn.h>
#include <sys/fcntl.h>

static struct sigaction prevSigAction;

static s32 crashIsDebuggerPresent(void)
{
	static s32 result = -1;

	if (result >= 0) {
		return result;
	}

	char buf[4096] = { 0 };

	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0) {
		result = 0;
		return 0;
	}

	int rx = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (rx <= 0) {
		result = 0;
		return 0;
	}

	buf[rx] = 0;

	char *str = strstr(buf, "TracerPid:");
	if (!str) {
		result = 0;
		return 0;
	}

	str += 10;

	while (*str && !isdigit(*str)) {
		if (*str == '\n') {
			result = 0;
			return 0;
		}
		++str;
	}

	result = (atoi(str) != 0);
	return result;
}

static void *crashGetModuleBase(const void *addr)
{
	Dl_info info;
	if (dladdr(addr, &info)) {
		return info.dli_fbase;
	}
	return NULL;
}

static void crashStackTrace(char *msg, s32 sig, void *pc)
{
	u32 msglen = 0;
	void *frames[CRASH_MAX_FRAMES] = { NULL };

	const s32 nframes = backtrace(frames, CRASH_MAX_FRAMES);
	if (nframes <= 0) {
		CRASH_MSG("no information\n");
		return;
	}

	char **strings = backtrace_symbols(frames, nframes);

	CRASH_MSG("SIGNAL: %d\n", sig);
	CRASH_MSG("PC: ");
	if (pc) {
		CRASH_MSG("%p\n", pc);
	} else if (strings) {
		CRASH_MSG("%s\n", strings[0]);
	} else {
		CRASH_MSG("%p\n", frames[0]);
	}

	CRASH_MSG("MODULE: %p\n", crashGetModuleBase(frames[0]));
	CRASH_MSG("MAIN MODULE: %p\n", crashGetModuleBase(crashInit));
	CRASH_MSG("\nBACKTRACE:\n");

	s32 i;
	for (i = 0; i < nframes; ++i) {
		CRASH_MSG("#%02d: ", i);
		if (strings && strings[i]) {
			CRASH_MSG("%s\n", strings[i]);
		} else {
			CRASH_MSG("%p\n", frames[i]);
		}
	}

	if (i == CRASH_MAX_FRAMES) {
		CRASH_MSG("...\n");
	} else if (i <= 1) {
		CRASH_MSG("no information\n");
	}

	free(strings);
}

static void crashHandler(s32 sig, siginfo_t *siginfo, void *ctx)
{
	char msg[CRASH_MAX_MSG + 1] = { 0 };

	if (crashIsDebuggerPresent()) {
		return;
	}

	void *pc = NULL;
	if (ctx) {
		ucontext_t *ucontext = (ucontext_t *)ctx;
#ifdef PLATFORM_X86
		pc = (void *)ucontext->uc_mcontext.gregs[REG_EIP];
#elif defined(PLATFORM_X86_64)
		pc = (void *)ucontext->uc_mcontext.gregs[REG_RIP];
#elif defined(PLATFORM_ARM) && defined(PLATFORM_64BIT)
		pc = (void *)ucontext->uc_mcontext.pc;
#elif defined(PLATFORM_ARM)
		pc = (void *)ucontext->uc_mcontext.arm_pc;
#endif
	}

	sysLogPrintf(LOG_ERROR, "FATAL: Crashed: PC=%p SIGNAL=%d", pc, sig);

	fflush(stderr);
	fflush(stdout);

	crashStackTrace(msg, sig, pc);

	sysFatalError("Crash!\n\n%s", msg);
}

#endif

s32 g_CrashEnabled = 0;

static char crashMsg[1024];

#ifdef PLATFORM_WIN32
/**
 * SIGABRT handler for GCC's -fstack-protector-strong.
 * When __stack_chk_fail detects a smashed canary it calls abort(),
 * which raises SIGABRT through the CRT. The VEH/UEF never see it,
 * so we catch it here with signal().
 */
static void crashSigabrtHandler(int sig)
{
	static volatile long s_AbrtFired = 0;
	if (InterlockedCompareExchange(&s_AbrtFired, 1, 0) != 0) {
		_exit(3);
	}

	const char *logpath = sysLogGetPath();
	if (logpath && logpath[0]) {
		FILE *f = fopen(logpath, "ab");
		if (f) {
			fprintf(f, "FATAL: SIGABRT caught — likely __stack_chk_fail (stack buffer overflow detected by -fstack-protector-strong)\n");
			fclose(f);
		}
	}
	fputs("FATAL: SIGABRT caught — likely __stack_chk_fail (stack buffer overflow detected)\n", stderr);
	fflush(stderr);

	sysLogPrintf(LOG_ERROR, "CRASH: SIGABRT — stack-protector canary smashed or explicit abort()");

	sysFatalError("SIGABRT: Stack buffer overflow detected by -fstack-protector-strong.\n"
		"Check the log — the corrupted function's canary was smashed.\n"
		"This confirms a buffer overrun inside a game tick function.");
}
#endif

void crashInit(void)
{
#ifdef PLATFORM_WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS);
	/* First-chance vectored handler: runs before the UEF, uses minimal stack.
	 * Critical for catching stack overflow where the UEF can't run. */
	AddVectoredExceptionHandler(1, crashVectoredHandler);
	prevExFilter = SetUnhandledExceptionFilter(crashHandler);
	/* Catch SIGABRT from GCC's __stack_chk_fail (stack protector) */
	signal(SIGABRT, crashSigabrtHandler);
	g_CrashEnabled = 1;
#elif defined(PLATFORM_LINUX)
	struct sigaction sigact = { 0 };
	sigact.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigact.sa_sigaction = crashHandler;
	sigaction(SIGSEGV, &sigact, &prevSigAction);
	sigaction(SIGABRT, &sigact, &prevSigAction);
	sigaction(SIGBUS,  &sigact, &prevSigAction);
	sigaction(SIGILL,  &sigact, &prevSigAction);
	g_CrashEnabled = 1;
#endif
}

void crashShutdown(void)
{
	if (!g_CrashEnabled) {
		return;
	}
#ifdef PLATFORM_WIN32
	if (prevExFilter) {
		SetUnhandledExceptionFilter(prevExFilter);
	}
#elif defined(PLATFORM_LINUX)
	sigaction(SIGSEGV, &prevSigAction, NULL);
	sigaction(SIGABRT, &prevSigAction, NULL);
	sigaction(SIGBUS,  &prevSigAction, NULL);
	sigaction(SIGILL,  &prevSigAction, NULL);
#endif
	g_CrashEnabled = 0;
}

void crashCreateThread(void)
{

}

void crashSetMessage(char *string)
{

}

void crashReset(void)
{

}

void crashAppendChar(char c)
{

}
