/**
 * net_client_settings_wire.h -- Transactional v55 CLC_SETTINGS codec.
 *
 * Lobby settings exist before a match-scoped session catalog, so body/head
 * identity travels as exact public catalog IDs.  This boundary prepares the
 * complete snapshot before writing bytes and decodes into a local candidate
 * before publication.  A rejected packet therefore cannot leave a partially
 * updated client record behind.
 */

#ifndef PD_NET_CLIENT_SETTINGS_WIRE_H
#define PD_NET_CLIENT_SETTINGS_WIRE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <PR/ultratypes.h>
#include "player_identity.h"

/* Protocol v55 matches the live NET_MAX_NAME/MAX_PLAYERNAME capacity.  Keep
 * this header lightweight for the pure C++ test runner; the live C source pins
 * equality with a compile-time assertion after loading constants.h. */
#define NET_CLIENT_SETTINGS_NAME_CAPACITY 15

struct netbuf;

typedef enum net_client_settings_wire_status_e {
	NET_CLIENT_SETTINGS_WIRE_OK = 0,
	NET_CLIENT_SETTINGS_WIRE_INVALID_ARGUMENT,
	NET_CLIENT_SETTINGS_WIRE_INVALID_IDENTITY,
	NET_CLIENT_SETTINGS_WIRE_INVALID_TEAM,
	NET_CLIENT_SETTINGS_WIRE_INVALID_HANDICAP,
	NET_CLIENT_SETTINGS_WIRE_INVALID_FOV,
	NET_CLIENT_SETTINGS_WIRE_INVALID_NAME,
	NET_CLIENT_SETTINGS_WIRE_BUFFER_TOO_SMALL,
	NET_CLIENT_SETTINGS_WIRE_MALFORMED,
} net_client_settings_wire_status_e;

typedef struct net_client_settings_input_t {
	u16 options;
	const char *body_id;
	const char *head_id;
	u8 team;
	u8 handicap;
	f32 fovy;
	f32 fovzoommult;
	const char *name;
} net_client_settings_input_t;

typedef struct net_client_settings_plan_t {
	u16 options;
	player_identity_plan_t identity;
	u8 team;
	u8 handicap;
	f32 fovy;
	f32 fovzoommult;
	char name[NET_CLIENT_SETTINGS_NAME_CAPACITY];
} net_client_settings_plan_t;

/* Prepare a complete canonical snapshot without mutating out_plan on failure. */
net_client_settings_wire_status_e netClientSettingsPrepare(
	const net_client_settings_input_t *input,
	net_client_settings_plan_t *out_plan,
	player_identity_status_e *out_identity_status);

/* Write opcode + payload atomically with respect to the destination write cursor. */
net_client_settings_wire_status_e netClientSettingsWireWrite(
	struct netbuf *dst,
	u8 opcode,
	const net_client_settings_input_t *input,
	player_identity_status_e *out_identity_status);

/* Read the payload after its opcode; out_plan is unchanged on every rejection. */
net_client_settings_wire_status_e netClientSettingsWireRead(
	struct netbuf *src,
	net_client_settings_plan_t *out_plan,
	player_identity_status_e *out_identity_status);

/* Once a match starts, its committed handicap remains server-authoritative. */
u8 netClientSettingsEffectiveHandicap(u8 requested, s32 in_match,
	u8 committed);

const char *netClientSettingsWireStatusString(
	net_client_settings_wire_status_e status);

#ifdef __cplusplus
}
#endif

#endif /* PD_NET_CLIENT_SETTINGS_WIRE_H */
