/**
 * net_interface.h — Clean protocol interface between networking and game logic.
 *
 * The networking layer (ENet, message encode/decode) calls these callbacks.
 * The server and client each provide their own implementations.
 * This is the ONLY boundary between networking and game code.
 */

#ifndef _IN_NET_INTERFACE_H
#define _IN_NET_INTERFACE_H

#include <PR/ultratypes.h>

/* Player connection lifecycle */
void netcb_OnPlayerJoin(u8 clientId, const char *name, u8 headnum, u8 bodynum);
void netcb_OnPlayerLeave(u8 clientId, const char *name, u32 reason);
void netcb_OnPlayerSettingsChanged(u8 clientId, const char *name, u8 headnum, u8 bodynum, u8 team);

/* Match lifecycle */
void netcb_OnMatchStart(u8 stagenum, u8 scenario, u32 rngSeed);
void netcb_OnMatchEnd(void);

/* Gameplay events (server-authoritative) */
void netcb_OnPlayerDeath(u8 victimId, u8 killerId, u8 weaponId);
void netcb_OnPlayerRespawn(u8 clientId);
void netcb_OnPlayerPosition(u8 clientId, f32 x, f32 y, f32 z, f32 angle);

/* Stage management */
void netcb_OnStageChange(u8 stagenum);
void netcb_OnStageEnd(void);

/* Chat */
void netcb_OnChat(u8 clientId, const char *message);

#endif /* _IN_NET_INTERFACE_H */
