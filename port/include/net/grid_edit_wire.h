#ifndef PD_GRID_EDIT_WIRE_H
#define PD_GRID_EDIT_WIRE_H

#include "grid_edit_session.h"

struct netbuf;

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_EDIT_COMMAND_WIRE_MAX 60u
#define GRID_EDIT_DELTA_WIRE_MAX 76u
#define GRID_EDIT_FOCUS_WIRE_MAX 20u

typedef struct grid_edit_wire_identity {
	u8 room_id;
	u64 generation;
} grid_edit_wire_identity_t;

/* Payload only. The caller owns the surrounding message and authenticates
 * the room and author from its connection. Client commands carry no author.
 * Decoration moves carry a canonical setup ID and two horizontal coordinates.
 * The reader consumes exactly the bounded payload at the current cursor.
 * On failure the buffers, cursors and previous decoded values are preserved. */
s32 gridEditCommandWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_edit_command_t *command);
s32 gridEditCommandRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_edit_command_t *command);

/* Authoritative committed delta. The caller authenticates the host before
 * using it and verifies the room and generation against its current session. */
s32 gridEditDeltaWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_edit_delta_t *delta);
s32 gridEditDeltaRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_edit_delta_t *delta);

/* Focus payloads carry the edit identity and one optional resource key.
 * NULL writes a clear and a successful clear read sets has_resource to 0
 * and clears out. The caller authenticates the sender and room generation.
 * Failure preserves packet bytes, cursors and all previous outputs. */
s32 gridEditFocusResourceWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_edit_resource_t *resource_or_null);
s32 gridEditFocusResourceRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_edit_resource_t *out, s32 *has_resource);

#ifdef __cplusplus
}
#endif

#endif
