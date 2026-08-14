#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "config.h"
#include "console.h"
#include "system.h"
#include "versioninfo.h"

#ifdef PLATFORM_WIN32

#include <windows.h>

// on win32 we use waitable timers instead of nanosleep
typedef HANDLE WINAPI (*CREATEWAITABLETIMEREXAFN)(LPSECURITY_ATTRIBUTES, LPCSTR, DWORD, DWORD);
static HANDLE timer;
static CREATEWAITABLETIMEREXAFN pfnCreateWaitableTimerExA;

// winapi also provides a yield macro
#define DO_YIELD() YieldProcessor()

// ask system for high performance GPU, if any
__attribute__((dllexport)) u32 NvOptimusEnablement = 1;
__attribute__((dllexport)) u32 AmdPowerXpressRequestHighPerformance = 1;

#else

#include <unistd.h>

// figure out how to yield
#if defined(PLATFORM_X86) || defined(PLATFORM_X86_64)
// this should work even if the code is not built with SSE enabled, at least on gcc and clang,
// but if it doesn't we'll have to use  __builtin_ia32_pause() or something
#include <immintrin.h>
#define DO_YIELD() _mm_pause()
#elif defined(PLATFORM_ARM) && (defined(PLATFORM_64BIT) || PLATFORM_ARM == 7 || PLATFORM_ARM == 8)
// same as YieldProcessor() on ARM Windows
#define DO_YIELD() __asm__ volatile("dmb ishst\n\tyield":::"memory")
#else
// fuck it
#define DO_YIELD() do { } while (0)
#endif

#endif

#define LOG_FNAME "pd.log"
#define CRASHLOG_FNAME "pd.crash.log"
#define LOG_ROOT_DIR "logs"
#define LOG_CLIENT_DIR "logs/game client"
#define USEC_IN_SEC 1000000ULL
#define WEAPON_DIAG_LOG_BUDGET 2048

static u64 startTick = 0;
static char logPath[2048];

static s32 sysArgc;
static const char **sysArgv;

/* -----------------------------------------------------------------------
 * Log channel filter
 * ----------------------------------------------------------------------- */

static u32 s_LogChannelMask = LOG_CH_ALL;
static s32 s_LogVerbose = 0;
static s32 s_WeaponDiagLogging = -1;
static SDL_atomic_t s_WeaponDiagLogCount;

const char *sysLogChannelNames[LOG_CH_COUNT] = {
	"Network", "Game", "Combat", "Audio", "Menu", "Save", "Mods", "System", "Match",
	"Catalog", "Distrib", "Render", "Benchmark", "TestScen"
};
const u32 sysLogChannelBits[LOG_CH_COUNT] = {
	LOG_CH_NETWORK, LOG_CH_GAME, LOG_CH_COMBAT, LOG_CH_AUDIO,
	LOG_CH_MENU, LOG_CH_SAVE, LOG_CH_MOD, LOG_CH_SYSTEM, LOG_CH_MATCH,
	LOG_CH_CATALOG, LOG_CH_DISTRIB, LOG_CH_RENDER,
	LOG_CH_BENCHMARK, LOG_CH_TESTSCEN
};

u32 sysLogGetChannelMask(void) { return s_LogChannelMask; }

void sysLogSetChannelMask(u32 mask)
{
	if (mask == s_LogChannelMask) return;
	u32 old = s_LogChannelMask;
	s_LogChannelMask = mask;

	/* Always log the filter change itself (bypasses filtering) */
	if (mask == LOG_CH_NONE) {
		sysLogPrintf(LOG_NOTE, "LOG: All log channels deactivated in settings");
	} else if (mask == LOG_CH_ALL) {
		sysLogPrintf(LOG_NOTE, "LOG: All log channels active");
	} else {
		char buf[256] = {0};
		for (s32 i = 0; i < LOG_CH_COUNT; i++) {
			if (mask & sysLogChannelBits[i]) {
				if (buf[0]) strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
				strncat(buf, sysLogChannelNames[i], sizeof(buf) - strlen(buf) - 1);
			}
		}
		sysLogPrintf(LOG_NOTE, "LOG: Active channels: %s", buf);
	}
}

s32 sysLogGetVerbose(void) { return s_LogVerbose; }

void sysLogSetVerbose(s32 enabled)
{
	s_LogVerbose = enabled ? 1 : 0;
	sysLogPrintf(LOG_NOTE, "LOG: Verbose logging %s", enabled ? "enabled" : "disabled");
}

static s32 sysWeaponDiagLoggingEnabled(void)
{
	if (s_WeaponDiagLogging < 0) {
		s_WeaponDiagLogging =
			sysArgCheck("--debug-weapon-diag")
			|| sysArgCheck("--debug-generated-mesh-render-audit");
	}

	return s_WeaponDiagLogging;
}

/* Map a message prefix string to its channel bitmask.
 * Returns 0 if no known prefix is found (treated as untagged / always pass). */
static u32 sysLogClassifyMessage(const char *msg)
{
	/* Skip level prefix if present (e.g. "WARNING: NET: ...") */
	if (strncmp(msg, "WARNING: ", 9) == 0) msg += 9;
	else if (strncmp(msg, "ERROR: ", 7) == 0) msg += 7;

	/* Network */
	if (strncmp(msg, "NET:",   4) == 0) return LOG_CH_NETWORK;
	if (strncmp(msg, "UPNP:",  5) == 0) return LOG_CH_NETWORK;
	if (strncmp(msg, "SERVER:",7) == 0) return LOG_CH_NETWORK;
	if (strncmp(msg, "LOBBY:", 6) == 0) return LOG_CH_NETWORK;
	if (strncmp(msg, "MATCHSETUP:", 11) == 0) return LOG_CH_NETWORK;

	/* Game */
	if (strncmp(msg, "STAGE:",   6) == 0) return LOG_CH_GAME;
	if (strncmp(msg, "INTRO:",   6) == 0) return LOG_CH_GAME;
	if (strncmp(msg, "LOAD:",    5) == 0) return LOG_CH_GAME;
	if (strncmp(msg, "PLAYER:",  7) == 0) return LOG_CH_GAME;
	if (strncmp(msg, "SIMULANT:",9) == 0) return LOG_CH_GAME;
	if (strncmp(msg, "SETUP:",   6) == 0) return LOG_CH_GAME;
	if (strncmp(msg, "TICK:",    5) == 0) return LOG_CH_GAME;
	if (strncmp(msg, "JUMP:",    5) == 0) return LOG_CH_GAME;
	if (strncmp(msg, "CAPSULE:", 8) == 0) return LOG_CH_GAME;

	/* Match */
	if (strncmp(msg, "MATCH:",    6) == 0) return LOG_CH_MATCH;
	if (strncmp(msg, "MATCHSTART.", 11) == 0) return LOG_CH_MATCH;
	if (strncmp(msg, "MATCHSTART:", 11) == 0) return LOG_CH_MATCH;
	if (strncmp(msg, "SPAWN:",    6) == 0) return LOG_CH_MATCH;
	if (strncmp(msg, "SPAWN.",    6) == 0) return LOG_CH_MATCH;
	if (strncmp(msg, "CHRSLOTS:",  9) == 0) return LOG_CH_MATCH;
	if (strncmp(msg, "BOT_ALLOC:",10) == 0) return LOG_CH_MATCH;

	/* Combat */
	if (strncmp(msg, "COMBAT:", 7) == 0) return LOG_CH_COMBAT;
	if (strncmp(msg, "DAMAGE:", 7) == 0) return LOG_CH_COMBAT;
	if (strncmp(msg, "WEAPON:", 7) == 0) return LOG_CH_COMBAT;
	if (strncmp(msg, "AMMO:",   5) == 0) return LOG_CH_COMBAT;
	if (strncmp(msg, "HEALTH:", 7) == 0) return LOG_CH_COMBAT;
	if (strncmp(msg, "PICKUP:", 7) == 0) return LOG_CH_COMBAT;

	/* Audio */
	if (strncmp(msg, "SND:",   4) == 0) return LOG_CH_AUDIO;
	if (strncmp(msg, "AUDIO:", 6) == 0) return LOG_CH_AUDIO;
	if (strncmp(msg, "AUDIO.", 6) == 0) return LOG_CH_AUDIO;
	if (strncmp(msg, "MUSIC:", 6) == 0) return LOG_CH_AUDIO;
	if (strncmp(msg, "MUSIC.", 6) == 0) return LOG_CH_AUDIO;
	if (strncmp(msg, "SFX:",   4) == 0) return LOG_CH_AUDIO;

	/* Menu */
	if (strncmp(msg, "MENU:",    5) == 0) return LOG_CH_MENU;
	if (strncmp(msg, "HOTSWAP:", 8) == 0) return LOG_CH_MENU;
	if (strncmp(msg, "DIALOG:",  7) == 0) return LOG_CH_MENU;
	if (strncmp(msg, "FONT",     4) == 0) return LOG_CH_MENU;

	/* Save */
	if (strncmp(msg, "SAVE:",       5) == 0) return LOG_CH_SAVE;
	if (strncmp(msg, "SAVEMIGRATE:",12) == 0) return LOG_CH_SAVE;
	if (strncmp(msg, "CONFIG:",     7) == 0) return LOG_CH_SAVE;

	/* Mods */
	if (strncmp(msg, "MOD:",    4) == 0) return LOG_CH_MOD;
	if (strncmp(msg, "MODMGR:", 7) == 0) return LOG_CH_MOD;
	if (strncmp(msg, "MODLOAD:",8) == 0) return LOG_CH_MOD;

	/* Catalog / asset pipeline */
	if (strncmp(msg, "CATALOG:",  8) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "CATALOG.",  8) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "MANIFEST:", 9) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "MANIFEST-", 9) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "ASSET:",    6) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "ASSET.",    6) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "LOADER.",   7) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "romextract ", 11) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "SCENARIO.SOURCE:", 16) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "SCENARIO.GRAPH:",  15) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "MISSION.GRAPH:",   14) == 0) return LOG_CH_CATALOG;
	if (strncmp(msg, "MODASSET.COMPILER:", 18) == 0) return LOG_CH_CATALOG;

	/* Distrib */
	if (strncmp(msg, "DISTRIB:", 8) == 0) return LOG_CH_DISTRIB;

	/* Render */
	if (strncmp(msg, "RENDER:",  7) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "GFX:",     4) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "TEXTURE:", 8) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "FAST3D:",  7) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "SCENARIO.RENDER:", 16) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "MODASSET.RENDER:", 16) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "MODELDEF.SOURCE:", 16) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "BONDGUN.SOURCE:",  15) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "BGUN.ANIM.SOURCE:", 17) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "LOG.WPN.DIAG:",    13) == 0) return LOG_CH_RENDER;
	if (strncmp(msg, "MESHCOL:",          8) == 0) return LOG_CH_RENDER;

	/* System */
	if (strncmp(msg, "SYS:",     4) == 0) return LOG_CH_SYSTEM;
	if (strncmp(msg, "MEM:",     4) == 0) return LOG_CH_SYSTEM;
	if (strncmp(msg, "MEMPC:",   6) == 0) return LOG_CH_SYSTEM;
	if (strncmp(msg, "CRASH:",   6) == 0) return LOG_CH_SYSTEM;
	if (strncmp(msg, "FS:",      3) == 0) return LOG_CH_SYSTEM;
	if (strncmp(msg, "UPDATER:", 8) == 0) return LOG_CH_SYSTEM;

	/* Benchmark + test scenarios (S483). Prefix matches are dot-suffixed
	 * so any BENCHMARK.SWARM.CPU / .GPU / .SUMMARY / etc. all route here. */
	if (strncmp(msg, "BENCHMARK.", 10) == 0) return LOG_CH_BENCHMARK;
	if (strncmp(msg, "TESTSCEN.",   9) == 0) return LOG_CH_TESTSCEN;
	if (strncmp(msg, "TESTSCEN:",   9) == 0) return LOG_CH_TESTSCEN;

	/* No recognized prefix — treat as untagged (always passes filter) */
	return 0;
}

#define LOG_ROTATE_COUNT 5

static void sysLogRotate(const char *dir, const char *fname)
{
	char oldpath[2048], newpath[2048];
	const char *dot = strrchr(fname, '.');
	char stem[256], ext[64];

	if (dot && dot != fname) {
		int stemlen = (int)(dot - fname);
		if (stemlen >= (int)sizeof(stem)) stemlen = (int)sizeof(stem) - 1;
		memcpy(stem, fname, stemlen);
		stem[stemlen] = '\0';
		snprintf(ext, sizeof(ext), "%s", dot);
	} else {
		snprintf(stem, sizeof(stem), "%s", fname);
		ext[0] = '\0';
	}

	snprintf(oldpath, sizeof(oldpath), "%s/%s.%d%s", dir, stem, LOG_ROTATE_COUNT - 1, ext);
	remove(oldpath);

	for (int i = LOG_ROTATE_COUNT - 2; i >= 1; i--) {
		snprintf(oldpath, sizeof(oldpath), "%s/%s.%d%s", dir, stem, i, ext);
		snprintf(newpath, sizeof(newpath), "%s/%s.%d%s", dir, stem, i + 1, ext);
		rename(oldpath, newpath);
	}

	snprintf(oldpath, sizeof(oldpath), "%s/%s", dir, fname);
	snprintf(newpath, sizeof(newpath), "%s/%s.1%s", dir, stem, ext);
	rename(oldpath, newpath);
}

static void sysLogEnsureDir(const char *dir)
{
#ifdef PLATFORM_WIN32
	CreateDirectoryA(dir, NULL);
#else
	(void)dir;
#endif
}

static inline void sysLogSetPath(const char *dir, const char *fname)
{
	sysLogEnsureDir(LOG_ROOT_DIR);
	sysLogEnsureDir(dir);

	snprintf(logPath, sizeof(logPath), "./%s/%s", dir, fname);
	sysLogRotate(dir, fname);
	FILE *f = fopen(logPath, "wb");
	if (!f) {
		char homeDir[2048];
		char homeLogRoot[2048];
		char homeLogDir[2048];
		sysGetHomePath(homeDir, sizeof(homeDir) - 1);
		snprintf(homeLogRoot, sizeof(homeLogRoot), "%s/%s", homeDir, LOG_ROOT_DIR);
		snprintf(homeLogDir, sizeof(homeLogDir), "%s/%s", homeDir, dir);
#ifdef PLATFORM_WIN32
		CreateDirectoryA(homeLogRoot, NULL);
		CreateDirectoryA(homeLogDir, NULL);
#endif
		sysGetHomePath(logPath, sizeof(logPath) - 1);
		strncat(logPath, "/", sizeof(logPath) - strlen(logPath) - 1);
		strncat(logPath, dir, sizeof(logPath) - strlen(logPath) - 1);
		strncat(logPath, "/", sizeof(logPath) - strlen(logPath) - 1);
		strncat(logPath, fname, sizeof(logPath) - strlen(logPath) - 1);
		sysLogRotate(homeLogDir, fname);
		f = fopen(logPath, "wb");
	}
	if (f) {
		fclose(f);
	}
}

static void sysLogSetDefaultPath(void)
{
	extern s32 g_NetDedicated;
	extern s32 g_NetHostLatch;

	if (g_NetDedicated || sysArgCheck("--dedicated")) {
		sysLogSetPath(LOG_CLIENT_DIR, "pd-server.log");
	} else if (g_NetHostLatch || sysArgCheck("--host")) {
		sysLogSetPath(LOG_CLIENT_DIR, "pd-host.log");
	} else {
		sysLogSetPath(LOG_CLIENT_DIR, "pd-client.log");
	}
}

s32 g_AppQuitting = 0;

void sysInitArgs(s32 argc, const char **argv)
{
	sysArgc = argc;
	sysArgv = argv;
}

void sysInit(void)
{
	startTick = sysGetMicroseconds();

	/* Always log to file — essential for diagnostics in all builds.
	 * Name the log file based on mode for clarity.
	 * Check both the variable (set by server_main.c directly) AND the
	 * CLI flag (set by main.c from --dedicated/--host args) so that
 * both the standalone server and the client-launched dedicated mode
	 * get the correct log filename. */
	{
		if (!sysLogIsOpen()) {
			sysLogSetDefaultPath();
		}
	}

#ifdef VERSION_HASH
	sysLogPrintf(LOG_NOTE, "version: " VERSION_BRANCH " " VERSION_HASH " (" VERSION_TARGET ")");
#endif

	char timestr[256];
	const time_t curtime = time(NULL);
	strftime(timestr, sizeof(timestr), "%d %b %Y %H:%M:%S", localtime(&curtime));
	sysLogPrintf(LOG_NOTE, "startup date: %s", timestr);

	/* Register log settings with config system — values will be loaded
	 * from pd.ini when configInit() runs shortly after sysInit().
	 * Cast through (s32*) is safe because s_LogVerbose is s32
	 * and s_LogChannelMask is u32. */
	configRegisterInt("Debug.VerboseLogging", &s_LogVerbose, 0, 1);
	configRegisterUInt("Debug.LogChannelMask", &s_LogChannelMask, 0, 0xFFFF);

	/* Enable verbose logging from command line (overrides config) */
	if (sysArgCheck("--verbose")) {
		s_LogVerbose = 1;
	}

	/* Print log channel filter status at startup */
	sysLogPrintf(LOG_NOTE, "LOG: Channel filter mask: 0x%04X (%s), verbose: %s",
		s_LogChannelMask,
		s_LogChannelMask == LOG_CH_ALL ? "all channels active" :
		s_LogChannelMask == LOG_CH_NONE ? "all channels disabled" : "partial",
		s_LogVerbose ? "on" : "off");

#ifdef PLATFORM_WIN32
	// this function is only present on Vista+, so try to import it from kernel32 by hand
	pfnCreateWaitableTimerExA = (CREATEWAITABLETIMEREXAFN)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExA");
	if (pfnCreateWaitableTimerExA) {
		// function exists, try to create a hires timer
		timer = pfnCreateWaitableTimerExA(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	}
	if (!timer) {
		// no function or hires timers not supported, fallback to lower resolution timer
		sysLogPrintf(LOG_WARNING, "SYS: hires waitable timers not available");
		timer = CreateWaitableTimerA(NULL, FALSE, NULL);
	}
#endif
}

s32 sysArgCheck(const char *arg)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			return 1;
		}
	}
	return 0;
}

const char *sysArgGetString(const char *arg)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			if (i < sysArgc - 1) {
				return sysArgv[i + 1];
			}
		}
	}
	return NULL;
}

s32 sysArgGetInt(const char *arg, s32 defval)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			if (i < sysArgc - 1) {
				return strtol(sysArgv[i + 1], NULL, 0);
			}
		}
	}
	return defval;
}

u64 sysGetMicroseconds(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((u64)tv.tv_sec * USEC_IN_SEC + (u64)tv.tv_usec) - startTick;
}

float sysGetSeconds(void)
{
	u64 t = sysGetMicroseconds();
	return (f32)t / 1000000.f;
}

s32 sysLogIsOpen(void)
{
	return (logPath[0] != '\0');
}

const char *sysLogGetPath(void)
{
	return logPath;
}

/* Structured ring buffer for on-screen log display and log viewer */
#define SYSLOG_RING_SIZE 2048
static LogEntry s_LogRing[SYSLOG_RING_SIZE];
static s32 s_LogRingHead = 0;
static s32 s_LogRingCount = 0;
static u32 s_LogSequence = 0;

s32 sysLogRingGetCount(void) { return s_LogRingCount < SYSLOG_RING_SIZE ? s_LogRingCount : SYSLOG_RING_SIZE; }

const char *sysLogRingGetLine(s32 idx)
{
	s32 total = sysLogRingGetCount();
	if (idx < 0 || idx >= total) return "";
	s32 start = (s_LogRingHead - total + SYSLOG_RING_SIZE) % SYSLOG_RING_SIZE;
	return s_LogRing[(start + idx) % SYSLOG_RING_SIZE].text;
}

s32 sysLogEntryGetCount(void) { return sysLogRingGetCount(); }

const LogEntry *sysLogEntryGet(s32 idx)
{
	s32 total = sysLogRingGetCount();
	if (idx < 0 || idx >= total) return NULL;
	s32 start = (s_LogRingHead - total + SYSLOG_RING_SIZE) % SYSLOG_RING_SIZE;
	return &s_LogRing[(start + idx) % SYSLOG_RING_SIZE];
}

u32 sysLogEntryGetSequence(void) { return s_LogSequence; }

/* Engine Phase 2 (2026-05-03): mutex around the structured ring buffer
 * and stdout writes so the boot worker + main thread can log
 * concurrently without corrupting s_LogRing's head/count or interleaving
 * file output mid-line.  Lazy-init on first call so log lines emitted
 * before sysInit (e.g. updaterApplyPending) still work; SDL is
 * initialized via SDL_MAIN_HANDLED before main(). */
static SDL_mutex *s_LogMutex = NULL;

static SDL_mutex *sysLogGetMutex(void)
{
	if (s_LogMutex == NULL) {
		s_LogMutex = SDL_CreateMutex();
	}
	return s_LogMutex;
}

void sysLogPrintf(s32 level, const char *fmt, ...)
{
	static const char *prefix[] = {
		"VERBOSE: ", "", "WARNING: ", "ERROR: ", "CHAT: "
	};

	const s32 lvl = level & 0x0f;

	char logmsg[2048];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(logmsg, sizeof(logmsg), fmt, ap);
	va_end(ap);

	if (logPath[0] == '\0') {
		sysLogSetDefaultPath();
	}

	const char *pfx = (lvl < (s32)(sizeof(prefix) / sizeof(prefix[0]))) ? prefix[lvl] : "";

	if (strncmp(logmsg, "LOG.WPN.DIAG:", 13) == 0) {
		s32 prior_count;

		if (!sysWeaponDiagLoggingEnabled()) {
			return;
		}

		prior_count = SDL_AtomicAdd(&s_WeaponDiagLogCount, 1);
		if (prior_count > WEAPON_DIAG_LOG_BUDGET) {
			return;
		}
		if (prior_count == WEAPON_DIAG_LOG_BUDGET) {
			snprintf(logmsg, sizeof(logmsg),
				"LOG.WPN.DIAG: output budget exhausted after %d lines; further weapon diagnostics suppressed",
				WEAPON_DIAG_LOG_BUDGET);
		}
	}

	/* --- Verbose filter ---
	 * LOG_VERBOSE messages are dropped entirely unless verbose mode is on. */
	if (lvl == LOG_VERBOSE && !s_LogVerbose) {
		return;
	}

	/* --- Channel filter ---
	 * Warnings and errors always pass. LOG_NOTE and LOG_VERBOSE messages are
	 * filtered by channel. Untagged messages (no recognized prefix) always pass. */
	if ((lvl == LOG_NOTE || lvl == LOG_VERBOSE) && s_LogChannelMask != LOG_CH_ALL) {
		u32 ch = sysLogClassifyMessage(logmsg);
		if (ch != 0 && !(s_LogChannelMask & ch)) {
			return;  /* Filtered out */
		}
	}

	/* Generate timestamp for file logging */
	char timestamp[32] = {0};
	{
		float sec = sysGetSeconds();
		s32 mins = (s32)(sec / 60.0f);
		float secs = sec - (mins * 60.0f);
		snprintf(timestamp, sizeof(timestamp), "[%02d:%05.2f]", mins, secs);
	}

	SDL_mutex *m = sysLogGetMutex();
	if (m) SDL_LockMutex(m);

	/* Capture to structured ring buffer */
	{
		LogEntry *entry = &s_LogRing[s_LogRingHead];
		entry->channel   = sysLogClassifyMessage(logmsg);
		entry->level     = lvl;
		entry->timestamp = sysGetSeconds();
		entry->sequence  = ++s_LogSequence;
		/* text field stores the raw message (no level prefix) for easy filtering */
		strncpy(entry->text, logmsg, sizeof(entry->text) - 1);
		entry->text[sizeof(entry->text) - 1] = '\0';
		s_LogRingHead = (s_LogRingHead + 1) % SYSLOG_RING_SIZE;
		s_LogRingCount++;
	}

	/* File log: timestamped */
	if (logPath[0]) {
		FILE *f = fopen(logPath, "ab");
		if (f) {
			fprintf(f, "%s %s%s\n", timestamp, pfx, logmsg);
			fclose(f);
		}
	}

	/* Console: timestamped */
	FILE *fout = (lvl == LOG_VERBOSE || lvl == LOG_NOTE || lvl == LOG_CHAT) ? stdout : stderr;
	fprintf(fout, "%s %s%s\n", timestamp, pfx, logmsg);
	fflush(fout);

	if (m) SDL_UnlockMutex(m);

	/* conPrintLn dispatches to the in-game console renderer.  Run
	 * outside the lock to avoid lock inversion with the renderer's own
	 * locks, and because conPrintLn already serialises internally. */
	if ((level & LOGFLAG_NOCON) == 0) {
		conPrintLn((level & LOGFLAG_SHOWMSG) != 0, logmsg);
	}
}

void sysFatalError(const char *fmt, ...)
{
	static s32 alreadyCrashed = 0;

	if (alreadyCrashed) {
		abort();
	}

	char errmsg[2048] = { 0 };

	alreadyCrashed = 1;

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(errmsg, sizeof(errmsg), fmt, ap);
	va_end(ap);

	sysLogPrintf(LOG_ERROR, "FATAL: %s", errmsg);

	fflush(stdout);
	fflush(stderr);

	/* Smoke verify harness: skip the modal dialog so the runner can
	 * collect the FATAL: log line and exit promptly instead of blocking
	 * the whole CI run on a click-to-dismiss popup. */
	{
		extern int smokeHarnessIsActive(void);
		if (!smokeHarnessIsActive()) {
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", errmsg, NULL);
		}
	}

	exit(1);
}

void sysGetExecutablePath(char *outPath, const u32 outLen)
{
	// try asking SDL
	char *sdlPath = SDL_GetBasePath();

	if (sdlPath && *sdlPath) {
		// -1 to trim trailing slash
		const u32 len = strlen(sdlPath) - 1;
		if (len < outLen) {
			memcpy(outPath, sdlPath, len);
			outPath[len] = '\0';
		}
	} else if (sysArgc && sysArgv[0] && sysArgv[0][0]) {
		// get exe path from argv[0]
		strncpy(outPath, sysArgv[0], outLen - 1);
		outPath[outLen - 1] = '\0';
	} else if (outLen > 1) {
		// give up, use working directory instead
		outPath[0] = '.';
		outPath[1] = '\0';
	}

#ifdef PLATFORM_WIN32
	// replace all backslashes with forward slashes, windows supports both
	for (u32 i = 0; i < outLen && outPath[i]; ++i) {
		if (outPath[i] == '\\') {
			outPath[i] = '/';
		}
	}
#endif

	SDL_free(sdlPath);
}

void sysGetHomePath(char *outPath, const u32 outLen)
{
	/* Multi-process production smokes need independent identity/social homes.
	 * Keep the override unavailable to ordinary launches: the runner must pass
	 * both --smoke and an explicit per-process path. */
	if (sysArgCheck("--smoke")) {
		const char *debugHome = sysArgGetString("--debug-home-path");
		if (debugHome && debugHome[0] && outLen > 0) {
			strncpy(outPath, debugHome, outLen - 1);
			outPath[outLen - 1] = '\0';
#ifdef PLATFORM_WIN32
			for (u32 i = 0; i < outLen && outPath[i]; ++i) {
				if (outPath[i] == '\\') {
					outPath[i] = '/';
				}
			}
#endif
			return;
		}
	}

	// try asking SDL
	char *sdlPath = SDL_GetPrefPath("", "perfectdark");

	if (sdlPath && *sdlPath) {
		// -1 to trim trailing slash
		const u32 len = strlen(sdlPath) - 1;
		if (len < outLen) {
			memcpy(outPath, sdlPath, len);
			outPath[len] = '\0';
		}
	} else if (outLen > 1) {
		// give up, use working directory instead
		outPath[0] = '.';
		outPath[1] = '\0';
	}

#ifdef PLATFORM_WIN32
	// replace all backslashes with forward slashes, windows supports both
	for (u32 i = 0; i < outLen && outPath[i]; ++i) {
		if (outPath[i] == '\\') {
			outPath[i] = '/';
		}
	}
#endif

	SDL_free(sdlPath);
}

void *sysMemAlloc(const u32 size)
{
	return malloc(size);
}

void *sysMemZeroAlloc(const u32 size)
{
	return calloc(1, size);
}

void *sysMemRealloc(void *ptr, const u32 newSize)
{
	return realloc(ptr, newSize);
}

void sysMemFree(void *ptr)
{
	free(ptr);
}

void sysSleep(const s64 hns)
{
#ifdef PLATFORM_WIN32
	static LARGE_INTEGER li;
	li.QuadPart = -hns;
	SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE);
	WaitForSingleObject(timer, INFINITE);
#else
	const struct timespec spec = { 0, hns * 100 };
	nanosleep(&spec, NULL);
#endif
}

void sysCpuRelax(void)
{
	DO_YIELD();
}
