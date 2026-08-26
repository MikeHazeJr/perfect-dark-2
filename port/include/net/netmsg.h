#ifndef _IN_NETMSG_H
#define _IN_NETMSG_H

#include <PR/ultratypes.h>
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmanifest.h"

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
#define SVC_BOT_AUTHORITY 0x4C // server→designated client: you are the bot AI authority (relay positions via CLC_BOT_MOVE)
#define SVC_STAGE_FLAG    0x50 // stage flags update (co-op only, u32 bitfield)
#define SVC_OBJ_STATUS    0x51 // objective status change (co-op only, index + status)
#define SVC_ALARM         0x52 // alarm state change (co-op only, active/inactive)
#define SVC_CUTSCENE      0x53 // v56: all-mode authority state {active, stable client mask, generation}
#define SVC_CUTSCENE_SKIP 0x54 // v56: authoritative skip acceptance {requester client ID, generation}
#define SVC_RECONNECT_COMMIT 0x55 // v57: ordered snapshot terminator {stable client ID}
#define SVC_RECONNECT_PROP_BEGIN 0x56 // v57: announced exact replicated-prop set
#define SVC_RECONNECT_PROP_STATE 0x57 // v57: one announced prop's live state
#define SVC_RECONNECT_PROP_END   0x58 // v57: exact replicated-prop set terminator
#define SVC_RECONNECT_INVENTORY  0x59 // v57: one stable client's exact inventory
#define NET_CUTSCENE_AUTHORITY_EVENT_CAPACITY 64u
#define NET_CUTSCENE_AUTHORITY_PACKET_CAPACITY 1024u
#define SVC_LOBBY_LEADER  0x60 // server announces lobby leader {clientId}
#define SVC_LOBBY_STATE   0x61 // server broadcasts lobby state (game mode, stage, etc.)

/* Phase A: Match Startup Pipeline (protocol v24) */
#define SVC_MATCH_MANIFEST  0x62 // server→room: full asset manifest for the upcoming match
#define SVC_MATCH_COUNTDOWN 0x63 // server→room: ready-gate progress (n/total ready, phase, countdown)
#define SVC_MATCH_CANCELLED 0x64 // server→room: countdown aborted; includes canceller name

/* SA-1: Session Catalog */
#define SVC_SESSION_CATALOG 0x67 // server→room: session ID mapping table

/* D3R-9: Network Distribution (protocol v20) */
#define SVC_CATALOG_INFO    0x70 // server→client: list of required component (id, category) — v27: no net_hash
#define SVC_DISTRIB_BEGIN   0x71 // server→client: start of a component archive transfer
#define SVC_DISTRIB_CHUNK   0x72 // server→client: one compressed chunk of the component archive
#define SVC_DISTRIB_END     0x73 // server→client: component transfer complete (success/fail)
#define SVC_LOBBY_KILL_FEED 0x74 // server→spectating clients: kill event with pre-resolved names

/* R-3: Room networking (protocol v29) */
#define SVC_ROOM_LIST     0x75 // server→all: full room list snapshot (on any room change)
#define SVC_ROOM_ASSIGN   0x76 // server→client: "you are now in room X" (0xFF = lounge)
#define SVC_MUSIC_ADVANCE 0x77 // server→room: advance playlist to next track (catalog ID string)

/* R-5: Room settings sync (protocol v35 additive) */
#define SVC_ROOM_SETTINGS  0x78 // server→room: match settings changed by leader (numBots, timelimit, etc.)
#define SVC_ROOM_PLAYLIST  0x79 // server→room: mod music playlist changed by leader (serialized string)

/* MASTER-C2b: Admin RCON reply (protocol v38+; ADMIN_RESP_RATE_LIMIT added v39). */
#define SVC_ADMIN          0x68 // server→operator-client: response to CLC_ADMIN

/* Phase 2 connectivity (protocol v41): authoritative host -> all match
 * clients. Carries an actor handle + a short achievement string. Clients
 * route into pdguiToastEnqueue with TOAST_CATEGORY_SOCIAL. */
#define SVC_ACHIEVEMENT_TOAST 0x69
#define ACHIEVEMENT_TOAST_MAX 96

/* Phase 3 spectator wire (protocol v42): authoritative match host ->
 * spectator-subscribed clients. */
#define SVC_SPECTATE_ACK   0x6a
#define SVC_STATE_FRAME    0x6b
#define SPECTATE_FRAME_PARTICIPANTS_MAX 16
#define SPECTATE_FRAME_NAME_MAX         32

/* Track 2d GPU swarm state sync (protocol v48, 2026-05-16, c3807).
 * Authoritative listen-host -> all clients. Carries one chunk of the
 * GPU compute kernel's per-bot state, quantized to 20 bytes/bot. See
 * port/include/net/swarm_sync_quant.h for the per-bot layout. The
 * receiver dequantizes and uploads to its local GL state texture via
 * swarmGpuApplyRemoteState. Throttled to 10 Hz on the unreliable
 * channel; chunked so each ENet packet stays under ~32 KB. */
#define SVC_GPUSWARM_STATE 0x6c
#define SWARM_SYNC_CHUNK_BOTS_MAX 1024  /* 1024 * 20 = 20480 B payload + small header */
#define SWARM_SYNC_THROTTLE_FRAMES  6   /* 60 Hz / 6 = 10 Hz */

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
#define CLC_CATALOG_DIFF 0x09 // client→server: list of missing component catalog ID strings

/* Bot authority relay (protocol v28) */
#define CLC_BOT_MOVE      0x0A // bot-authority client→server: bot positions for server relay via SVC_CHR_MOVE

/* U-10: Stage-ready handshake */
#define CLC_STAGE_READY   0x0B // client→server: real post-load boundary; releases gameplay replication and optional bot delegation

/* R-3: Room networking (protocol v29) */
#define CLC_ROOM_CREATE  0x10 // client→server: create a new room
#define CLC_ROOM_JOIN    0x11 // client→server: join room by id
#define CLC_ROOM_LEAVE   0x12 // client→server: leave current room

/* R-5: Room settings sync (protocol v35 additive) */
#define CLC_ROOM_SETTINGS_UPDATE  0x13 // leader→server: push current match settings for broadcast
#define CLC_ROOM_PLAYLIST_UPDATE  0x14 // leader→server: push mod playlist string for broadcast

/* MASTER-C2b: Admin RCON request (protocol v38+). */
#define CLC_ADMIN                 0x15 // operator-client→server: admin auth / kick / ban / unban / list / status

/* Phase 3 spectator wire (protocol v42). */
#define CLC_SPECTATE_REQUEST      0x16 // client→host: promote me to CLFLAG_SPECTATOR; do not allocate a player slot

/* v46: cutscene skip authority. */
#define CLC_CUTSCENE_SKIP         0x17 // v56 request {playernum, authority generation}

/* v49 (2026-05-17): post-match lobby resync. Client asks server to
 * re-broadcast SVC_ROOM_ASSIGN + SVC_ROOM_SETTINGS + SVC_ROOM_PLAYLIST so
 * the client's room view is authoritative after the SVC_STAGE_END
 * roundtrip. No payload; the source netclient identifies which client to
 * resync. Server path: netmsgClcLobbyResyncRead -> emit SVC_ROOM_ASSIGN
 * with the client's existing room_id (or 0xFF for lounge), then replay the
 * current room settings and playlist payloads. */
#define CLC_LOBBY_RESYNC          0x18 // client->server: re-broadcast my room + settings

u32 netmsgClcLobbyResyncWrite(struct netbuf *dst);
u32 netmsgClcLobbyResyncRead(struct netbuf *src, struct netclient *srccl);
void netSendLobbyResync(void); /* Client helper: fire CLC_LOBBY_RESYNC. */

/* Phase A: Match Startup Pipeline (protocol v24) */
#define CLC_MANIFEST_STATUS 0x0E // client→server: manifest check result (READY / NEED_ASSETS / DECLINE)
#define CLC_LOBBY_CANCEL    0x0F // client→server: cancel countdown before match launch (any player)

/* v57 client reconnect credential lifecycle. The cookie never leaves memory
 * and is emitted only when the newly resolved endpoint exactly matches the
 * endpoint that issued it. The returned ENet connect datum is only a stable
 * slot hint; CLC_AUTH remains the authenticator. */
u32 netmsgClcAuthPrepareConnect(const ENetAddress *endpoint, u16 protocol);
s32 netmsgClcAuthReconnectAvailable(const ENetAddress *endpoint);
void netmsgClcAuthClearCookie(void);

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
/* Serialize the already-running authoritative match for one reconnecting
 * client without advancing playlists or recapturing match authority. */
u32 netmsgSvcStageReplayWrite(struct netbuf *dst);
/* Serialize exact authoritative state after the reconnecting client's real
 * stage-load boundary. This is the ordered companion to StageReplayWrite. */
typedef enum net_reconnect_snapshot_status_e {
	NET_RECONNECT_SNAPSHOT_OK = 0,
	NET_RECONNECT_SNAPSHOT_INVALID_ARGUMENT,
	NET_RECONNECT_SNAPSHOT_CUTSCENE,
	NET_RECONNECT_SNAPSHOT_ROSTER,
	NET_RECONNECT_SNAPSHOT_WORLD_ARGUMENT,
	NET_RECONNECT_SNAPSHOT_WORLD_DUPLICATE_SYNC_ID,
	NET_RECONNECT_SNAPSHOT_WORLD_UNSUPPORTED_DYNAMIC,
	NET_RECONNECT_SNAPSHOT_WORLD_SPAWN_WRITE,
	NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE,
	NET_RECONNECT_SNAPSHOT_WORLD_PROJECTILE_STATE,
	NET_RECONNECT_SNAPSHOT_WORLD_PROJECTILE_OWNER,
	NET_RECONNECT_SNAPSHOT_INVENTORY,
	NET_RECONNECT_SNAPSHOT_PLAYER_STATS,
	NET_RECONNECT_SNAPSHOT_PLAYER_MOVEMENT,
	NET_RECONNECT_SNAPSHOT_CHARACTER,
	NET_RECONNECT_SNAPSHOT_NPC,
	NET_RECONNECT_SNAPSHOT_STAGE_FLAGS,
	NET_RECONNECT_SNAPSHOT_OBJECTIVE,
	NET_RECONNECT_SNAPSHOT_SCORES,
	NET_RECONNECT_SNAPSHOT_COMMIT,
	NET_RECONNECT_SNAPSHOT_BUFFER,
} net_reconnect_snapshot_status_e;

typedef enum net_reconnect_prop_state_status_e {
	NET_RECONNECT_PROP_STATE_NONE = 0,
	NET_RECONNECT_PROP_STATE_INVALID_ARGUMENT,
	NET_RECONNECT_PROP_STATE_MODEL_SCALE_NONPOSITIVE,
	NET_RECONNECT_PROP_STATE_MODEL_SCALE_TOO_LARGE,
	NET_RECONNECT_PROP_STATE_MODEL_SCALE_NAN,
	NET_RECONNECT_PROP_STATE_ATTACHMENT_PAIR,
	NET_RECONNECT_PROP_STATE_ATTACHMENT_PARENT,
	NET_RECONNECT_PROP_STATE_ATTACHMENT_MATRIX_RANGE,
	NET_RECONNECT_PROP_STATE_ATTACHMENT_MATRIX_MISSING,
	NET_RECONNECT_PROP_STATE_EMBEDDED_ATTACHMENT,
	NET_RECONNECT_PROP_STATE_WEAPON_IDENTITY,
	NET_RECONNECT_PROP_STATE_WEAPON_DUAL_REFERENCE,
} net_reconnect_prop_state_status_e;

typedef struct net_reconnect_snapshot_result_s {
	net_reconnect_snapshot_status_e status;
	u8 client_id;       /* NET_NULL_CLIENT when no roster member owns failure */
	u32 prop_syncid;    /* NET_NULL_PROP when no world prop owns failure */
	s32 objective_index; /* -1 when no objective row owns failure */
	net_reconnect_prop_state_status_e prop_state_status;
	s32 prop_type;      /* -1 when no prop identity is available */
	s32 object_type;
	s32 model_num;
	f32 model_scale;
	s32 weapon_num;
	s32 dual_weapon_num;
	u32 dual_prop_syncid;
	u32 parent_syncid;
	u32 object_hidden;
	u32 object_hidden2;
	s32 attachment_mtx_index;
	u32 bytes_written;  /* attempted transaction bytes before atomic rollback */
	u32 capacity;
} net_reconnect_snapshot_result_t;

u32 netmsgSvcReconnectStateWrite(struct netbuf *dst,
	struct netclient *reconnecting_client,
	net_reconnect_snapshot_result_t *out_result);
const char *netmsgReconnectSnapshotStatusString(
	net_reconnect_snapshot_status_e status);
const char *netmsgReconnectPropStateStatusString(
	net_reconnect_prop_state_status_e status);
u32 netmsgSvcReconnectCommitWrite(struct netbuf *dst, u8 client_id);
u32 netmsgSvcReconnectCommitRead(struct netbuf *src,
	struct netclient *srccl);
u32 netmsgSvcReconnectPropBeginRead(struct netbuf *src,
	struct netclient *srccl);
u32 netmsgSvcReconnectPropStateRead(struct netbuf *src,
	struct netclient *srccl);
u32 netmsgSvcReconnectPropEndRead(struct netbuf *src,
	struct netclient *srccl);
u32 netmsgSvcReconnectInventoryRead(struct netbuf *src,
	struct netclient *srccl);
/* Serialize a new authoritative match from one immutable prepared roster and
 * commit that exact roster as the cutscene authority identity snapshot. */
u32 netmsgServerStageStartWrite(struct netbuf *dst, u8 room_id);
/* Side-effect-free typed/roster preflight used before irreversible host
 * stage-launch work. Returns zero only when a later write can be attempted. */
u32 netmsgSvcStageStartValidate(void);
u32 netmsgSvcStageStartRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcStageEndWrite(struct netbuf *dst, u8 room_id, u8 mode);
void netmsgSvcStageEndCommit(u8 room_id, u8 mode);
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
u32 netmsgSvcPropSyncRead(struct netbuf *src, struct netclient *srccl);
/* Mark a prop as dirty (event-driven). Called by each SvcProp*Write function. */
void netPropMarkDirty(u32 syncid);
/* Prop snapshot dirty-detection (event-driven replacement for CRC polling) */
int  netPropDirtyCheck(void);
void netPropSnapReset(void);
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
void netNpcReplicationReset(void);
bool netNpcIsReplicationReady(const struct chrdata *chr);
u32 netNpcCount(void);

u32 netmsgSvcStageFlagWrite(struct netbuf *dst);
u32 netmsgSvcStageFlagRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcObjStatusWrite(struct netbuf *dst, u8 index, u8 status);
u32 netmsgSvcObjStatusRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcAlarmWrite(struct netbuf *dst, u8 active);
u32 netmsgSvcAlarmRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcCutsceneRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcCutsceneSkipRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcCutsceneSkipWrite(struct netbuf *dst, u8 playernum,
		u32 generation);
u32 netmsgClcCutsceneSkipRead(struct netbuf *src, struct netclient *srccl);
s32 netmsgServerQueueLocalCutsceneSkip(u8 playernum, u32 generation);
s32 netmsgServerQueueCutsceneState(u8 active, u32 generation,
		u8 *out_player_mask);
u32 netmsgServerPrepareCutsceneAuthorityPacket(struct netbuf *dst,
		u8 *out_room_id,
		u32 *out_event_count);
s32 netmsgServerCommitCutsceneAuthorityPacket(u32 event_count);
bool netmsgCutsceneAuthorityIsActive(void);
bool netmsgCutsceneAuthorityHasMatch(void);
bool netmsgCutsceneAuthorityHasPendingEvents(void);
void netmsgCutsceneAuthorityRetireClient(u8 client_id);
void netmsgCutsceneAuthorityReset(void);

/* Lobby protocol messages (Phase 3) */
u32 netmsgClcLobbyStartWrite(struct netbuf *dst, u8 gamemode,
                            const char *stage_id, u8 difficulty,
                            u8 antiClientId, u8 numSims, u8 simType,
                            u8 timelimit, u32 options, u8 scenario,
                            u8 scorelimit, u16 teamscorelimit,
                            u8 weaponSetIndex);
u32 netmsgClcLobbyStartRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcLobbyLeaderWrite(struct netbuf *dst, u8 leaderClientId);
u32 netmsgSvcLobbyLeaderRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcLobbyStateWrite(struct netbuf *dst, u8 gamemode, const char *stage_id, u8 status);
u32 netmsgSvcLobbyStateRead(struct netbuf *src, struct netclient *srccl);

/* D3R-9: Network Distribution (protocol v20) */

/* SVC_CATALOG_INFO: server→client, list of required non-bundled enabled components */
u32 netmsgSvcCatalogInfoWrite(struct netbuf *dst);
u32 netmsgSvcCatalogInfoWriteChunk(struct netbuf *dst, u16 start_offset,
                                   u16 *next_offset, u16 *total_count);
u32 netmsgSvcCatalogInfoRead(struct netbuf *src, struct netclient *srccl);

/* CLC_CATALOG_DIFF: client→server, which components the client is missing.
 * v27: catalog ID strings replace u32 net_hash values. */
u32 netmsgClcCatalogDiffWrite(struct netbuf *dst, const char (*missing_ids)[CATALOG_ID_LEN], u16 count, u8 temporary);
u32 netmsgClcCatalogDiffRead(struct netbuf *src, struct netclient *srccl);

/* SVC_DISTRIB_BEGIN/CHUNK/END: server→client, component archive stream.
 * v27: catalog ID string replaces u32 net_hash as component identity on the wire. */
u32 netmsgSvcDistribBeginWrite(struct netbuf *dst, const char *catalog_id,
                                const char *category, u32 total_chunks,
                                u32 archive_bytes, const u8 expected_sha256[32]);
u32 netmsgSvcDistribBeginRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcDistribChunkWrite(struct netbuf *dst, const char *catalog_id, u16 chunk_idx,
                                u8 compression, const u8 *data, u16 data_len);
u32 netmsgSvcDistribChunkRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcDistribEndWrite(struct netbuf *dst, const char *catalog_id, u8 success);
u32 netmsgSvcDistribEndRead(struct netbuf *src, struct netclient *srccl);

/* SVC_LOBBY_KILL_FEED: server→spectating clients, pre-resolved kill event */
u32 netmsgSvcLobbyKillFeedWrite(struct netbuf *dst, const char *attacker, const char *victim,
                                 const char *weapon, u8 flags);
u32 netmsgSvcLobbyKillFeedRead(struct netbuf *src, struct netclient *srccl);

/* Phase A: Match Startup Pipeline (protocol v24) */

/* SVC_MATCH_MANIFEST: server→room, complete asset list for the upcoming match */
u32 netmsgSvcMatchManifestWrite(struct netbuf *dst, const match_manifest_t *manifest);
u32 netmsgSvcMatchManifestRead(struct netbuf *src, struct netclient *srccl);

/* CLC_MANIFEST_STATUS: client→server, catalog check result.
 * v27: missing list uses catalog ID strings, not net_hash u32 values. */
u32 netmsgClcManifestStatusWrite(struct netbuf *dst, u32 manifest_hash, u8 status,
                                  const char (*missing_ids)[CATALOG_ID_LEN], u16 num_missing);
u32 netmsgClcManifestStatusRead(struct netbuf *src, struct netclient *srccl);

/* SVC_MATCH_COUNTDOWN: server→room, ready-gate progress broadcast */
u32 netmsgSvcMatchCountdownWrite(struct netbuf *dst, u8 ready_count, u8 total_count,
                                  u8 phase, u8 countdown_secs);
u32 netmsgSvcMatchCountdownRead(struct netbuf *src, struct netclient *srccl);

/* CLC_LOBBY_CANCEL: any client→server, abort the countdown */
u32 netmsgClcLobbyCancelWrite(struct netbuf *dst);
u32 netmsgClcLobbyCancelRead(struct netbuf *src, struct netclient *srccl);

/* SVC_MATCH_CANCELLED: server→room, countdown aborted; includes canceller name */
u32 netmsgSvcMatchCancelledWrite(struct netbuf *dst, const char *canceller_name);
u32 netmsgSvcMatchCancelledRead(struct netbuf *src, struct netclient *srccl);

/* SA-1: SVC_SESSION_CATALOG */
u32 netmsgSvcSessionCatalogRead(struct netbuf *src, struct netclient *srccl);

/* Bot authority relay (protocol v28) */
u32 netmsgSvcBotAuthorityWrite(struct netbuf *dst);
u32 netmsgSvcBotAuthorityRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcBotMoveWrite(struct netbuf *dst);
u32 netmsgClcBotMoveRead(struct netbuf *src, struct netclient *srccl);

/* U-10: Stage-ready handshake */
u32 netmsgClcStageReadyWrite(struct netbuf *dst);
u32 netmsgClcStageReadyRead(struct netbuf *src, struct netclient *srccl);

/* Phase F: client-side countdown display state — updated by SVC_MATCH_COUNTDOWN handler. */
struct match_countdown_state {
    u8  ready_count;     /* clients that have responded READY */
    u8  total_count;     /* total expected clients */
    u8  phase;           /* MANIFEST_PHASE_* */
    u8  countdown_secs;  /* seconds remaining (meaningful when phase == MANIFEST_PHASE_LOADING) */
    s32 active;          /* 1 once at least one countdown has been received */
};
extern struct match_countdown_state g_MatchCountdownState;

/* Max length for the canceller name in SVC_MATCH_CANCELLED (matches lobby name limit). */
#define MATCH_CANCEL_NAME_LEN 32

/* Client-side cancellation state — updated by SVC_MATCH_CANCELLED handler.
 * UI reads this to display "[Name] cancelled the match start". */
struct match_cancelled_state {
    s32  active;                      /* 1 while the message is pending display */
    char name[MATCH_CANCEL_NAME_LEN]; /* name of the player who cancelled */
};
extern struct match_cancelled_state g_MatchCancelledState;

/* R-3: Room networking (protocol v29) */
u32 netmsgSvcRoomListWrite(struct netbuf *dst);
u32 netmsgSvcRoomListRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcRoomAssignWrite(struct netbuf *dst, u8 room_id);
u32 netmsgSvcRoomAssignRead(struct netbuf *src, struct netclient *srccl);

/* v34: Mid-match music playlist advance.
 * v40 (Issue 4b): wire gains u32 match_clock_offset_ms after track_id;
 * authoritative host broadcasts elapsed-since-track-start in ms so
 * clients can lerp playback rate to converge or hard-seek if drift
 * exceeds 5000 ms. */
u32 netmsgSvcMusicAdvanceWrite(struct netbuf *dst, const char *track_id, u32 match_clock_offset_ms);
u32 netmsgSvcMusicAdvanceRead(struct netbuf *src, struct netclient *srccl);
void netMusicBroadcastAdvance(const char *track_id, u8 room_id, u32 match_clock_offset_ms);

/* R-5: Room settings sync (v35 additive) */
u32 netmsgSvcRoomSettingsWrite(struct netbuf *dst, u8 numBots, u8 timelimit,
                                u8 scorelimit, u16 teamscorelimit, u32 options,
                                u8 scenario, u8 weaponSetIndex, const char *stage_id);
u32 netmsgSvcRoomSettingsRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgSvcRoomPlaylistWrite(struct netbuf *dst, const char *playlist_str);
u32 netmsgSvcRoomPlaylistRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcRoomSettingsUpdateWrite(struct netbuf *dst, u8 numBots, u8 timelimit,
                                      u8 scorelimit, u16 teamscorelimit, u32 options,
                                      u8 scenario, u8 weaponSetIndex, const char *stage_id);
u32 netmsgClcRoomSettingsUpdateRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcRoomPlaylistUpdateWrite(struct netbuf *dst, const char *playlist_str);
u32 netmsgClcRoomPlaylistUpdateRead(struct netbuf *src, struct netclient *srccl);
/* Convenience: pack current g_MatchConfig into CLC_ROOM_SETTINGS_UPDATE and send.
 * Call from the room screen whenever the leader changes settings. */
void netSendRoomSettingsUpdate(void);
/* Convenience: pack current mod playlist into CLC_ROOM_PLAYLIST_UPDATE and send. */
void netSendRoomPlaylistUpdate(void);

/* SEC-14: CLC_ROOM_CREATE now carries access mode + password + max_players.
 *   access      — 0=OPEN, 1=PASSWORD, 2=INVITE (see room_access_t)
 *   password    — plaintext (server hashes), empty string for non-password rooms
 *   maxPlayers  — 1..HUB_MAX_CLIENTS; clamped on server side */
u32 netmsgClcRoomCreateWrite(struct netbuf *dst, const char *name, u8 access,
                              const char *password, u8 maxPlayers);
u32 netmsgClcRoomCreateRead(struct netbuf *src, struct netclient *srccl);
/* SEC-14: CLC_ROOM_JOIN now carries an optional password (empty for open rooms). */
u32 netmsgClcRoomJoinWrite(struct netbuf *dst, u8 room_id, const char *password);
u32 netmsgClcRoomJoinRead(struct netbuf *src, struct netclient *srccl);
u32 netmsgClcRoomLeaveWrite(struct netbuf *dst);
u32 netmsgClcRoomLeaveRead(struct netbuf *src, struct netclient *srccl);
/* In-client listen host: leave room without sending CLC (no ENet peer to self). */
void netListenHostRoomLeave(void);

/* MASTER-C2b: Admin RCON messages. */
u32 netmsgClcAdminRead(struct netbuf *src, struct netclient *srccl);

/* Phase 2: SVC_ACHIEVEMENT_TOAST (protocol v41). */
u32 netmsgSvcAchievementToastWrite(struct netbuf *dst, u32 actor_handle,
                                    const char *achievement_text);
u32 netmsgSvcAchievementToastRead(struct netbuf *src, struct netclient *srccl);
/* Convenience: server / authoritative host enqueues a broadcast for the
 * current match room. */
void netSendAchievementToast(u32 actor_handle, const char *achievement_text);

/* Phase 3 (protocol v42): spectator wire.
 *
 * CLC_SPECTATE_REQUEST: client requests spectator role on the host
 *   server. The server sets CLFLAG_SPECTATOR on the netclient; the
 *   slot does NOT count as a match participant. Server replies with
 *   SVC_SPECTATE_ACK. Subsequent SVC_STATE_FRAME packets stream the
 *   match participant snapshot at SPECTATOR_FANOUT_HZ.
 *
 * SVC_SPECTATE_ACK: u8 accepted (1) or rejected (0); on accept, a
 *   u32 stream_token the client mirrors back in any future requests.
 *
 * SVC_STATE_FRAME: u32 host_handle, u32 frame_seq, u8 participant_count,
 *   followed by `participant_count` blocks of:
 *     u8 in_use, u8 team, u8 is_bot, u8 _pad,
 *     s16 score, s16 deaths,
 *     f32 pos_x, f32 pos_y, f32 pos_z,
 *     f32 angle_theta, f32 angle_verta,
 *     u32 weapon_runtime_idx,
 *     char name[SPECTATE_FRAME_NAME_MAX]
 *   Total per-participant: 64 bytes.
 */
u32 netmsgClcSpectateRequestWrite(struct netbuf *dst, u32 my_handle);
u32 netmsgClcSpectateRequestRead (struct netbuf *src, struct netclient *srccl);

u32 netmsgSvcSpectateAckWrite(struct netbuf *dst, u8 accepted, u32 stream_token);
u32 netmsgSvcSpectateAckRead (struct netbuf *src, struct netclient *srccl);

u32 netmsgSvcStateFrameWrite(struct netbuf *dst, u32 host_handle, u32 frame_seq,
                              const void *participants_blob, u32 participants_count);
u32 netmsgSvcStateFrameRead (struct netbuf *src, struct netclient *srccl);

/* Server-side host: build + broadcast a state frame to every netclient
 * with CLFLAG_SPECTATOR set. Called on the spectator fan-out tick
 * boundary. No-op if not in NETMODE_SERVER. */
void netSendSpectateStateFrame(void);

/* Track 2d GPU swarm sync (protocol v48, 2026-05-16, c3807).
 *
 * netmsgSvcGpuSwarmStateWrite -- build ONE chunk of the broadcast. The
 * chunk's bot byte payload must already be packed (see
 * swarmSyncQuantEncode). The caller is responsible for invoking this
 * once per chunk and netSend-ing each result on the unreliable channel.
 *
 * netmsgSvcGpuSwarmStateRead -- decode one chunk off the wire. On the
 * client side, this dequantizes the chunk into a row-major RGBA32F
 * buffer and forwards to the GL state texture via swarmGpuApplyRemote-
 * State (port/fast3d/swarm_gpu.cpp). On pd-server the decode runs but
 * the apply call is compiled out via PD_SERVER guards (no GL on
 * pd-server). Either way the wire bytes are consumed so the dispatcher
 * stays in sync. */
u32 netmsgSvcGpuSwarmStateWrite(struct netbuf *dst, u32 frame_idx,
                                 u16 total_count, u16 chunk_start,
                                 u16 chunk_count,
                                 const u8 *packed_chunk_bytes);
u32 netmsgSvcGpuSwarmStateRead(struct netbuf *src, struct netclient *srccl);

/* Listen-host helper: read the GPU state texture, encode in chunks, and
 * broadcast each chunk on the unreliable channel. Throttled internally
 * via a static frame counter (SWARM_SYNC_THROTTLE_FRAMES). No-op when
 * not in NETMODE_SERVER, when running headless (g_NetDedicated == 1),
 * or when no peer is connected. */
void netSendGpuSwarmState(void);
/* SVC_ADMIN is written by the server helper below; no client-side Write wrapper
 * is exposed because the client never sends it. */

void netBroadcastRoomList(void);
/* Rebuild and publish lobby leader plus room-list topology after an
 * authenticated join or reconnect commit. Server mode only. */
void netmsgServerPublishAuthenticatedTopology(void);

/* SEC-C4: Reset per-client chat rate limiter state on disconnect. */
void netmsgChatRateReset(u32 idx);
void netmsgAdminAuthRateReset(u32 idx);

/* SEC-13: Reset per-client room-mutation rate limiter on disconnect. */
void netmsgRoomMutationRateReset(u32 idx);

/* SEC-13: Mark the SVC_ROOM_LIST broadcast dirty; flush once at end of frame.
 * Coalesces bursty CLC_ROOM_CREATE/JOIN/LEAVE traffic into a single broadcast. */
void netRoomListMarkDirty(void);
void netRoomListFlushIfDirty(void);

/* Phase F: Drive the server-side launch countdown.
 * Called each server tick from netEndFrame().  No-op until readyGateCheck() arms it. */
void readyGateTickCountdown(void);

/* Bug B fix: abort the pre-match countdown when the room it targets is torn
 * down or when a preparing client leaves.  Both routes broadcast
 * SVC_MATCH_CANCELLED via the existing readyGateAbort() pipeline, so all
 * clients clear their 3-2-1 overlay. */
void netReadyGateAbortForRoom(u8 room_id, const char *reason);
void netReadyGateOnClientLeft(u8 clientId);
/* Local host/listen-server cancel path used by UI Back/Escape during countdown.
 * Returns 0 on success, <0 when not cancellable in current state. */
s32 netReadyGateCancelByLocalClient(struct netclient *srccl);
/* Dedicated-server cancel path used when no local client exists. */
s32 netReadyGateCancelByServer(void);

/* Prop syncid → prop* lookup map.
 * Replaces the O(n) linear scan in netbufReadPropPtr with a direct-indexed O(1) lookup.
 * Must be kept in sync whenever prop syncids change:
 *   netSyncIdMapRebuild() — call after netSyncIdsAllocate() (full stage sync)
 *   netSyncIdMapSet()     — call after any individual prop->syncid assignment
 *   netSyncIdMapClear()   — call on stage teardown / connection init
 */
void netSyncIdMapClear(void);
void netSyncIdMapSet(u32 syncid, struct prop *prop);
void netSyncIdMapRebuild(void);

#endif
