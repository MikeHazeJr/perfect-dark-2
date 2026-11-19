#ifndef PD_GRID_EDIT_PLAYTEST_VIEW_H
#define PD_GRID_EDIT_PLAYTEST_VIEW_H

#include "net/grid_edit_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_PLAYTEST_VIEW_MAX_ROOMS 32u
#define GRID_PLAYTEST_VIEW_MAX_ACTORS 16u
#define GRID_PLAYTEST_VIEW_WIRE_MAX \
	(19u + 8u * GRID_PLAYTEST_VIEW_MAX_ROOMS + 5u * GRID_PLAYTEST_VIEW_MAX_ACTORS)
#define GRID_PLAYTEST_WORLD_VIEW_VERSION 1u
#define GRID_PLAYTEST_WORLD_VIEW_FOOTER_SIZE 25u
#define GRID_PLAYTEST_WORLD_VIEW_WIRE_MAX (GRID_PLAYTEST_VIEW_WIRE_MAX + GRID_PLAYTEST_WORLD_VIEW_FOOTER_SIZE)
#define GRID_PLAYTEST_TARGET_WIRE_PREFIX_SIZE 14u
#define GRID_PLAYTEST_TARGET_COMMAND_WIRE_MAX (GRID_EDIT_COMMAND_WIRE_MAX + GRID_PLAYTEST_TARGET_WIRE_PREFIX_SIZE)
#define GRID_PLAYTEST_TARGET_COMMAND_MARKER 0xffu
#define GRID_PLAYTEST_CURSOR_WIRE_MAX 18u
#define GRID_PLAYTEST_CURSOR_LAYOUT_VERSION 1u
#define GRID_PLAYTEST_CURSOR_LAYOUT_PREFIX_SIZE 9u
#define GRID_PLAYTEST_CURSOR_LAYOUT_WIRE_MAX (GRID_PLAYTEST_CURSOR_WIRE_MAX + GRID_PLAYTEST_CURSOR_LAYOUT_PREFIX_SIZE)

typedef struct grid_playtest_view_room {
	u16 min_x, min_z, max_x, max_z;
} grid_playtest_view_room_t;

typedef struct grid_playtest_view_actor {
	u16 x, z;
	u8 bot;
} grid_playtest_view_actor_t;

typedef struct grid_playtest_target {
	u32 epoch;
	u64 layout_generation;
} grid_playtest_target_t;

typedef struct grid_playtest_view {
	u32 epoch, serial;
	u8 room_count, actor_count;
	grid_playtest_view_room_t rooms[GRID_PLAYTEST_VIEW_MAX_ROOMS];
	grid_playtest_view_actor_t actors[GRID_PLAYTEST_VIEW_MAX_ACTORS];
	/* Optional world projection. Legacy packets leave these fields zero. */
	u8 has_world_bounds;
	u64 layout_generation;
	f32 min_x, min_z, max_x, max_z;
} grid_playtest_view_t;

/* Private presentation payloads. The transport authenticates the sender,
 * validates room and epoch membership and applies disclosure policy before
 * any send. Readers consume one exact payload. Failures preserve the cursor,
 * packet bytes and previous outputs. A view with both counts zero clears it. */
s32 gridEditPlaytestViewWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_playtest_view_t *view);
s32 gridEditPlaytestViewRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_playtest_view_t *view);

/* Negotiated world projection carries the same bounded frame with exact
 * X/Z conversion and layout generation. Clears keep the legacy shape. */
s32 gridEditPlaytestWorldViewWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_playtest_view_t *view);
s32 gridEditPlaytestWorldViewRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_playtest_view_t *view);

s32 gridEditPlaytestTargetMatchesView(const grid_playtest_target_t *target, const grid_playtest_view_t *view);

/* A map target wraps an existing placement command in its displayed layout
 * stamp. The transport authenticates the sender and checks the current layout. */
s32 gridEditPlaytestTargetCommandWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_playtest_target_t *target, const grid_edit_command_t *command);
s32 gridEditPlaytestTargetCommandRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_playtest_target_t *target, grid_edit_command_t *command);

/* Hidden cursors carry no position. A successful hidden read clears x and z. */
s32 gridEditPlaytestCursorWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	u8 visible, f32 x, f32 z);
s32 gridEditPlaytestCursorRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	u8 *visible, f32 *x, f32 *z);
/* Negotiated cursor format binds coordinates to a host-issued layout token. */
s32 gridEditPlaytestLayoutCursorWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	u64 token, u8 visible, f32 x, f32 z);
s32 gridEditPlaytestLayoutCursorRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	u64 *token, u8 *visible, f32 *x, f32 *z);

#ifdef __cplusplus
}
#endif

#endif
