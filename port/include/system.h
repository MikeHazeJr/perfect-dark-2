#ifndef _IN_SYSTEM_H
#define _IN_SYSTEM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <PR/ultratypes.h>

f32 sysGetSeconds(void);

enum LogLevel {
  LOG_VERBOSE,  /* Trace-level detail: function entry/exit, state dumps. Off by default. */
  LOG_NOTE,
  LOG_WARNING,
  LOG_ERROR,
  LOG_CHAT,
};

#define LOGFLAG_NOCON   0x10
#define LOGFLAG_SHOWMSG 0x20

/* -----------------------------------------------------------------------
 * Log channel filter system.
 *
 * Channels are bitmask flags. Each channel covers a group of log prefixes.
 * Filtering is based on the message text prefix (e.g., "NET:", "SAVE:").
 * Warnings and errors ALWAYS pass regardless of filter state.
 * The active filter mask is printed to the log header on startup and
 * whenever it changes, so you always know if output is being suppressed.
 * ----------------------------------------------------------------------- */

#define LOG_CH_NETWORK   0x0001  /* NET, UPNP, SERVER, LOBBY, MATCHSETUP  */
#define LOG_CH_GAME      0x0002  /* STAGE, INTRO, CATALOG, LOAD, PLAYER, SIMULANT, SETUP */
#define LOG_CH_COMBAT    0x0004  /* DAMAGE, WEAPON, AMMO, HEALTH, PICKUP  */
#define LOG_CH_AUDIO     0x0008  /* SND, AUDIO, MUSIC, SFX               */
#define LOG_CH_MENU      0x0010  /* MENU, HOTSWAP, DIALOG, FONT          */
#define LOG_CH_SAVE      0x0020  /* SAVE, SAVEMIGRATE, CONFIG             */
#define LOG_CH_MOD       0x0040  /* MOD, MODMGR, MODLOAD                 */
#define LOG_CH_SYSTEM    0x0080  /* SYS, MEM, MEMPC, CRASH, FS, UPDATER  */
#define LOG_CH_MATCH     0x0100  /* MATCH, CHRSLOTS, BOT_ALLOC, MATCHSETUP pipeline */
#define LOG_CH_ALL       0xFFFF
#define LOG_CH_NONE      0x0000

/* Get/set the active channel mask. Default is LOG_CH_ALL. */
u32  sysLogGetChannelMask(void);
void sysLogSetChannelMask(u32 mask);

/* Verbose logging: off by default, toggled via debug menu or --verbose flag. */
s32  sysLogGetVerbose(void);
void sysLogSetVerbose(s32 enabled);

/* Channel names/count for UI enumeration */
#define LOG_CH_COUNT 9
extern const char *sysLogChannelNames[LOG_CH_COUNT];
extern const u32   sysLogChannelBits[LOG_CH_COUNT];

/* Log ring buffer for live console */
s32  sysLogRingGetCount(void);
const char *sysLogRingGetLine(s32 idx);

void sysInitArgs(s32 argc, const char **argv);
void sysInit(void);

s32 sysArgCheck(const char *arg);
const char *sysArgGetString(const char *arg);
s32 sysArgGetInt(const char *arg, s32 defval);

u64 sysGetMicroseconds(void);

void sysFatalError(const char *fmt, ...) __attribute__((noreturn));

s32 sysLogIsOpen(void);
void sysLogPrintf(s32 level, const char *fmt, ...);

void sysGetExecutablePath(char *outPath, const u32 outLen);
void sysGetHomePath(char *outPath, const u32 outLen);

void *sysMemAlloc(const u32 size);
void *sysMemZeroAlloc(const u32 size);
void *sysMemRealloc(void *ptr, const u32 newSize);
void sysMemFree(void *ptr);

// hns is specified in 100ns units
void sysSleep(const s64 hns);

// yield CPU if supported (e.g. during a busy loop)
void sysCpuRelax(void);

void crashInit(void);
void crashShutdown(void);

/* Set to true in cleanup() before calling netDisconnect().
 * Subsystems that would block during orderly shutdown check this flag
 * and skip their blocking teardown paths (UPnP HTTP delete, stage
 * transitions, etc.) so the process exits within ~100 ms. */
extern bool g_AppQuitting;

#ifdef __cplusplus
}
#endif

#endif
