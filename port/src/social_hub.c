/*
 * social_hub.c -- cold-network lifecycle implementation.
 *
 * Mike directive 2026-05-17: online functionality stays cold (no socket
 * bound, no tick body running) until an agent is loaded, because the
 * published identity (handle, connect code) is per-agent. Implementation
 * detail per `port/include/social_hub.h`.
 *
 * Boot order pre-change:
 *   mainInit() calls socialInit + p2pInit + presenceInit +
 *   groupSessionInit + chatInit + voiceInit + ... ALL at boot,
 *   BEFORE any agent is selected.
 *
 * Boot order post-change:
 *   mainInit() calls only socialInit (local-only -- friends/blocks
 *   on disk). All online subsystems wait for prefsAgentLoad to fire
 *   socialHubBringOnline().
 *
 * Idempotence: BringOnline is safe to call multiple times. The
 * underlying init functions have their own internal guards; we only
 * actually run them once per session.
 *
 * --no-net: skipped entirely. Mirrors the pre-change behaviour at
 * port/src/main.c:1467-1471 (the previous --no-net gate on p2pInit).
 */

#include "social_hub.h"
#include "presence.h"
#include "chat.h"
#include "voice.h"
#include "net/p2p.h"
#include "net/group_session.h"
#include "file_transfer.h"
#include "pdgui_toast.h"
#include "spectator.h"
#include "theater.h"
#include "listening_room.h"
#include "social_share.h"
#include "system.h"

static s32 s_HubOnline = 0;

void socialHubBringOnline(void)
{
	if (s_HubOnline) return;

	/* main.c's g_BootNoNet is static-local; re-check the arg ourselves. */
	if (sysArgCheck("--no-net")) {
		sysLogPrintf(LOG_NOTE,
			"SOCIAL.HUB: --no-net set; socialHubBringOnline() skipped");
		return;
	}

	sysLogPrintf(LOG_NOTE,
		"SOCIAL.HUB: bring online -- agent loaded, opening sockets");

	/* Order mirrors the pre-change boot sequence at
	 * port/src/main.c:1467-1481 so behaviour is otherwise unchanged. */
	p2pInit();
	presenceInit();
	groupSessionInit();
	chatInit();
	fileTransferInit();
	pdguiToastInit();
	spectatorInit();
	theaterInit();
	listeningRoomInit();
	shareInit();
	voiceInit();

	s_HubOnline = 1;
	sysLogPrintf(LOG_NOTE,
		"SOCIAL.HUB: online -- presence/voice/p2p/groupSession/chat live");
}

void socialHubGoOffline(void)
{
	if (!s_HubOnline) return;

	sysLogPrintf(LOG_NOTE,
		"SOCIAL.HUB: go offline -- closing sockets");

	/* Reverse-order shutdown for symmetry with init. Only the
	 * subsystems that expose a public Shutdown are torn down; the
	 * others (fileTransferInit, pdguiToastInit, spectatorInit,
	 * theaterInit, listeningRoomInit, shareInit) currently have no
	 * teardown path -- they release at process exit. */
	voiceShutdown();
	chatShutdown();
	groupSessionShutdown();
	presenceShutdown();
	p2pShutdown();

	s_HubOnline = 0;
}

s32 socialHubIsOnline(void) { return s_HubOnline; }
