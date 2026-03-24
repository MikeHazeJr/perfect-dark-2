#ifndef _IN_NETMSG_H
#define _IN_NETMSG_H

#include <PR/ultratypes.h>
#include "net/net.h"
#include "net/netbuf.h"

#define SVC_BAD           0x00 // trash
#define SVC_NOP           0x01 // does nothing
#define SVC_AUTH          0x02 // auth response, sent in response to CLC_AUTH
#define SVC_CHAT          0x03 // chat message
#define SVC_STAGE_START   0x10 // start level
#define SVC_STAGE_END     0x11 // end level
#define SVC_PLAYER_MOVE   0x20 // player movement and inputs
#define SVC_PLAYER_GUNS   0x21 // player gun state
#define SVC_PLAYER_STATS  0x22 // player stats (health etc)
#define SVC_PLAYER_SCORES 0x23 // player match scores (kills, deaths, points)
#define SVC_PROP_MOVE     0x30 // prop movement
#define SVC_PROP_SPAWN    0x31 // new prop spawned
#define SVC_PROP_DAMAGE   0x32 // prop was damaged
#define SVC_PROP_PICKUP   0x33 // prop was picked up
#define SVC_PROP_USE      0x34 // door/lift/etc was used
#define SVC_PROP_DOOR     0x35 // door state changed
#define SVC_PROP_LIFT     0x36 // lift state changed
#define SVC_PROP_SYNC     0x37 // prop sync checksum for desync detection
#define SVC_PROP_RESYNC   0x38 // full prop state correction (sent on desync)
#define SVC_CHR_DAMAGE    0x42 // chr was damaged
#define SVC_CHR_DISARM    0x43 // chr's weapons were dropped
#define SVC_CHR_MOVE      0x44 // chr (bot/simulant) position update from server
#define SVC_CHR_STATE     0x45 // chr (bot/simulant) state update (weapon, health, action)
#define SVC_CHR_SYNC      0x46 // chr sync checksum for desync detection
#define SVC_CHR_RESYNC    0x47 // full bot state correction (sent on desync)
#define SVC_NPC_MOVE      0x48 // NPC position update (co-op only, server-authoritative)
#define SVC_NPC_STATE     0x49 // NPC state update (co-op only: health, flags, alertness)
#define SVC_NPC_SYNC      0x4A // NPC sync checksum for desync detection (co-op only)
#define SVC_NPC_RESYNC    0x4B // full NPC state correction (co-op only)
#define SVC_STAGE_FLAG    0x50 // stage flags update (co-op only, u32 bitfield)
#define SVC_OBJ_STATUS    0x51 // objective status change (co-op only, index + status)
#define SVC_ALARM         0x52 // alarm state change (co-op only, active/inactive)
#define SVC_CUTSCENE      0x53 // cutscene state change (co-op only, start/end)
#define SVC_LOBBY_LEADER  0x60 // server announces lobby leader {clientId}
#define SVC_LOBBY_STATE   0x61 // server broadcasts lobby state (game mode, stage, etc.)

/* D3R-9: Network Distribution (protocol v20) */
#define SVC_CATALOG_INFO    0x70 // server→client: list of required component (net_hash, id, category)
#define SVC_DISTRIB_BEGIN   0x71 // server→client: start of a component archive transfer
#define SVC_DISTRIB_CHUNK   0x72 // server→client: one compressed chunk of the component archive
#define SVC_DISTRIB_END     0x73 // server→client: component transfer complete (success/fail)
#define SVC_LOBBY_KILL_FEED 0x74 // server→spectating clients: kill event with pre-resolved names

#define CLC_BAD      0x00 // trash
#define CLC_NOP      0x01 // does nothing
#define CLC_AUTH     0x02 // auth request, sent immediately after connecting
#define CLC_CHAT     0x03 // chat message
#define CLC_MOVE     0x04 // player input
#define CLC_SETTINGS    0x05 // player settings changed
#define CLC_RESYNC_REQ  0x06 // client requests full state resync from server
#define CLC_COOP_READY  0x07 // client signals ready for co-op mission start
#define CLC_LOBBY_START 0x08 // lobby leader requests match start {gamemode, stagenum, difficulty}

/* D3R-9: Network Distribution (protocol v20) */
#define CLC_CATALOG_DIFF 0x09 // client→server: list of missing component net_hashes

u32 netmsgClcAuthWrite(struct netbuf *dst);
u32 netmsgClcAuthRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcChatWrite(struct netbuf *dst, const char *str);
u32 netmsgClcChatRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcMoveWrite(struct netbuf *dst);
u32 netmsgClcMoveRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcSettingsWrite(struct netbuf *dst);
u32 netmsgClcSettingsRead(struct netbuf *src, struct netclient *srccl);

u32 netmsgSvcAuthWrite(struct netbuf *dst, struct netclient *authcl);
u32 netmsgSvcAuthRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcChatWrite(struct netbuf *dst, const char *str);
u32 netmsgSvcChatRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcStageStartWrite(struct netbuf *dst);
u32 netmsgSvcStageStartRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcStageEndWrite(struct netbuf *dst);
u32 netmsgSvcStageEndRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPlayerMoveWrite(struct netbuf *dst, struct netclient *movecl);
u32 netmsgSvcPlayerMoveRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPlayerStatsWrite(struct netbuf *dst, struct netclient *actcl);
u32 netmsgSvcPlayerStatsRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropSpawnWrite(struct netbuf *dst, struct prop *prop);
u32 netmsgSvcPropSpawnRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropMoveWrite(struct netbuf *dst, struct prop *prop, struct coord *initrot);
u32 netmsgSvcPropMoveRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropDamageWrite(struct netbuf *dst, struct prop *prop, f32 damage, struct coord *pos, s32 weaponnum, s32 playernum);
u32 netmsgSvcPropDamageRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropPickupWrite(struct netbuf *dst, struct netclient *actcl, struct prop *prop, const s32 tickop);
u32 netmsgSvcPropPickupRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropUseWrite(struct netbuf *dst, struct prop *prop, struct netclient *usercl, const s32 tickop);
u32 netmsgSvcPropUseRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropDoorWrite(struct netbuf *dst, struct prop *prop, struct netclient *usercl);
u32 netmsgSvcPropDoorRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropLiftWrite(struct netbuf *dst, struct prop *prop);
u32 netmsgSvcPropLiftRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcChrDamageWrite(struct netbuf *dst, struct chrdata *chr, f32 damage, struct coord *vector, struct gset *gset, struct prop *aprop, s32 hitpart, bool damageshield, struct prop *prop2, s32 side, s16 *arg11, bool explosion, struct coord *explosionpos);
u32 netmsgSvcChrDamageRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcChrDisarmWrite(struct netbuf *dst, struct chrdata *chr, struct prop *attacker, u8 weaponnum, f32 wpndamage, struct coord *wpnpos);
u32 netmsgSvcChrDisarmRead(struct netbuf *src, struct netclient *srccl);

u32 netmsgSvcPlayerScoresWrite(struct netbuf *dst);
u32 netmsgSvcPlayerScoresRead(struct netbuf *src, struct netclient *srccl);

u32 netmsgSvcChrMoveWrite(struct netbuf *dst, struct chrdata *chr);
u32 netmsgSvcChrMoveRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcChrStateWrite(struct netbuf *dst, struct chrdata *chr);
u32 netmsgSvcChrStateRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcChrSyncWrite(struct netbuf *dst);
u32 netmsgSvcChrSyncRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropSyncWrite(struct netbuf *dst);
u32 netmsgSvcPropSyncRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcChrResyncWrite(struct netbuf *dst);
u32 netmsgSvcChrResyncRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcPropResyncWrite(struct netbuf *dst);
u32 netmsgSvcPropResyncRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcResyncReqWrite(struct netbuf *dst, u8 flags);
u32 netmsgClcResyncReqRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcCoopReadyWrite(struct netbuf *dst);
u32 netmsgClcCoopReadyRead(struct netbuf *src, struct netclient *srccl);

u32 netmsgSvcNpcMoveWrite(struct netbuf *dst, struct chrdata *chr);
u32 netmsgSvcNpcMoveRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcNpcStateWrite(struct netbuf *dst, struct chrdata *chr);
u32 netmsgSvcNpcStateRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcNpcSyncWrite(struct netbuf *dst);
u32 netmsgSvcNpcSyncRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcNpcResyncWrite(struct netbuf *dst);
u32 netmsgSvcNpcResyncRead(struct netbuf *src, struct netclient *srccl);
u32 netNpcCount(void);

u32 netmsgSvcStageFlagWrite(struct netbuf *dst);
u32 netmsgSvcStageFlagRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcObjStatusWrite(struct netbuf *dst, u8 index, u8 status);
u32 netmsgSvcObjStatusRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcAlarmWrite(struct netbuf *dst, u8 active);
u32 netmsgSvcAlarmRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcCutsceneWrite(struct netbuf *dst, u8 active);
u32 netmsgSvcCutsceneRead(struct netbuf *src, struct netclient *srccl);

/* Lobby protocol messages (Phase 3) */
u32 netmsgClcLobbyStartWrite(struct netbuf *dst, u8 gamemode, u8 stagenum, u8 difficulty);
u32 netmsgClcLobbyStartRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcLobbyLeaderWrite(struct netbuf *dst, u8 leaderClientId);
u32 netmsgSvcLobbyLeaderRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcLobbyStateWrite(struct netbuf *dst, u8 gamemode, u8 stagenum, u8 status);
u32 netmsgSvcLobbyStateRead(struct netbuf *src, struct netclient *srccl);

/* D3R-9: Network Distribution (protocol v20) */

/* SVC_CATALOG_INFO: server→client, list of required non-bundled enabled components */
u32 netmsgSvcCatalogInfoWrite(struct netbuf *dst);
u32 netmsgSvcCatalogInfoRead(struct netbuf *src, struct netclient *srccl);

/* CLC_CATALOG_DIFF: client→server, which components the client is missing */
u32 netmsgClcCatalogDiffWrite(struct netbuf *dst, const u32 *missing_hashes, u16 count, u8 temporary);
u32 netmsgClcCatalogDiffRead(struct netbuf *src, struct netclient *srccl);

/* SVC_DISTRIB_BEGIN/CHUNK/END: server→client, component archive stream */
u32 netmsgSvcDistribBeginWrite(struct netbuf *dst, u32 net_hash, const char *id,
                                const char *category, u32 total_chunks, u32 archive_bytes);
u32 netmsgSvcDistribBeginRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcDistribChunkWrite(struct netbuf *dst, u32 net_hash, u16 chunk_idx,
                                u8 compression, const u8 *data, u16 data_len);
u32 netmsgSvcDistribChunkRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcDistribEndWrite(struct netbuf *dst, u32 net_hash, u8 success);
u32 netmsgSvcDistribEndRead(struct netbuf *src, struct netclient *srccl);

/* SVC_LOBBY_KILL_FEED: server→spectating clients, pre-resolved kill event */
u32 netmsgSvcLobbyKillFeedWrite(struct netbuf *dst, const char *attacker, const char *victim,
                                 const char *weapon, u8 flags);
u32 netmsgSvcLobbyKillFeedRead(struct netbuf *src, struct netclient *srccl);

#endif
