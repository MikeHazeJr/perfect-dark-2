/**
 * discord.h — Discord Rich Presence thin IPC client.
 *
 * No external library.  Talks to Discord via Windows named-pipe IPC
 * (\\.\pipe\discord-ipc-N).  Fails silently if Discord is not running.
 *
 * SETUP: register a Discord application at
 *   https://discord.com/developers/applications
 * Copy the "Application ID" (numeric string) and replace the default below.
 * Then upload art assets named pd2_logo / icon_solo / icon_combat /
 * icon_coop / icon_counterop / icon_forge in the Rich Presence → Art Assets
 * tab of your application.  Until you do that, asset keys are silently ignored
 * by Discord and only the text fields appear.
 */

#pragma once

/* Discord application "Client ID".  Replace "0" with the numeric ID from
 * https://discord.com/developers/applications → General Information. */
#ifndef DISCORD_APP_ID
#define DISCORD_APP_ID "0"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** One-shot init.  Attempts to connect to the Discord IPC pipe; fails
 *  silently if Discord is not running.  Safe to call multiple times. */
void discordInit(void);

/** Release the IPC pipe.  Called from cleanup(). */
void discordShutdown(void);

/**
 * Per-frame poll.  Call from mainTick.  Rate-limits updates internally
 * (DISCORD_TICK_SECS seconds between sends) and also fires immediately
 * whenever the displayed state changes.  Reconnects silently if the pipe
 * was lost (e.g. Discord restarted).
 */
void discordTick(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
