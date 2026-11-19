#include <float.h>
#include <string.h>

#include "net/grid_edit_playtest_view.h"
#include "net/netbuf.h"
#include "platform.h"

enum {
	VIEW_IDENTITY_SIZE = 9,
	VIEW_HEADER_SIZE = VIEW_IDENTITY_SIZE + 2 * 4 + 2,
	VIEW_ROOM_SIZE = 4 * 2,
	VIEW_ACTOR_SIZE = 2 * 2 + 1,
	CURSOR_HIDDEN_SIZE = VIEW_IDENTITY_SIZE + 1,
	CURSOR_VISIBLE_SIZE = CURSOR_HIDDEN_SIZE + 2 * 4,
};

_Static_assert(sizeof(f32) == sizeof(u32), "Grid cursor positions require 32-bit floats");
_Static_assert(GRID_PLAYTEST_VIEW_WIRE_MAX == VIEW_HEADER_SIZE
	+ VIEW_ROOM_SIZE * GRID_PLAYTEST_VIEW_MAX_ROOMS + VIEW_ACTOR_SIZE * GRID_PLAYTEST_VIEW_MAX_ACTORS,
	"Grid playtest view wire bound must fit every room and actor");
_Static_assert(GRID_PLAYTEST_CURSOR_WIRE_MAX == CURSOR_VISIBLE_SIZE,
	"Grid playtest cursor wire bound must fit the visible payload");
_Static_assert(GRID_PLAYTEST_CURSOR_LAYOUT_PREFIX_SIZE == 1 + sizeof(u64),
	"Grid layout cursor includes a version and a 64-bit token");

_Static_assert(GRID_PLAYTEST_WORLD_VIEW_FOOTER_SIZE == 1 + sizeof(u64) + 4 * sizeof(f32),
	"Grid world views carry a version, layout generation and four bounds");
_Static_assert(GRID_PLAYTEST_TARGET_WIRE_PREFIX_SIZE == 2 + sizeof(u32) + sizeof(u64),
	"Grid placement targets carry a marker, version, epoch and layout generation");

static s32 validBuffer(const struct netbuf *buf)
{
	return buf && buf->data && !buf->error && buf->rp <= buf->wp && buf->wp <= buf->size;
}

static s32 validIdentity(const grid_edit_wire_identity_t *identity)
{
	return identity && identity->room_id != 0xff && identity->generation;
}

static s32 validCounts(u8 rooms, u8 actors)
{
	return rooms <= GRID_PLAYTEST_VIEW_MAX_ROOMS && actors <= GRID_PLAYTEST_VIEW_MAX_ACTORS
		&& (rooms == 0) == (actors == 0);
}

static u32 viewSize(const grid_playtest_view_t *view)
{
	return VIEW_HEADER_SIZE + view->room_count * VIEW_ROOM_SIZE + view->actor_count * VIEW_ACTOR_SIZE;
}

static s32 validView(const grid_playtest_view_t *view)
{
	if (!view || !view->epoch || !view->serial || !validCounts(view->room_count, view->actor_count)) return 0;
	for (u32 i = 0; i < view->room_count; i++) {
		const grid_playtest_view_room_t *room = &view->rooms[i];
		if (room->min_x >= room->max_x || room->min_z >= room->max_z) return 0;
	}
	for (u32 i = 0; i < view->actor_count; i++) {
		if (view->actors[i].bot > 1) return 0;
	}
	return 1;
}

static s32 validCursor(u8 visible, f32 x, f32 z)
{
	return !visible || (visible == 1 && __builtin_isfinite(x) && __builtin_isfinite(z));
}

static void writeU16(struct netbuf *wire, u16 value)
{
	const u16 le = PD_LE16(value);
	netbufWriteData(wire, &le, sizeof(le));
}

static void writeU32(struct netbuf *wire, u32 value)
{
	const u32 le = PD_LE32(value);
	netbufWriteData(wire, &le, sizeof(le));
}

static void writeF32(struct netbuf *wire, f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	writeU32(wire, bits);
}

static void writeU64(struct netbuf *wire, u64 value)
{
	const u64 le = PD_LE64(value);
	netbufWriteData(wire, &le, sizeof(le));
}

static void writeIdentity(struct netbuf *wire, const grid_edit_wire_identity_t *identity)
{
	netbufWriteU8(wire, identity->room_id);
	writeU64(wire, identity->generation);
}

static grid_edit_wire_identity_t readIdentity(struct netbuf *wire)
{
	grid_edit_wire_identity_t identity = {0};
	identity.room_id = netbufReadU8(wire);
	identity.generation = netbufReadU64(wire);
	return identity;
}

s32 gridEditPlaytestViewWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_playtest_view_t *view)
{
	if (!validBuffer(dst) || !validIdentity(identity) || !validView(view)) return 0;
	const u32 size = viewSize(view);
	if (size > dst->size - dst->wp) return 0;

	u8 bytes[GRID_PLAYTEST_VIEW_WIRE_MAX];
	struct netbuf wire = { .data = bytes, .size = sizeof(bytes) };
	writeIdentity(&wire, identity);
	writeU32(&wire, view->epoch);
	writeU32(&wire, view->serial);
	netbufWriteU8(&wire, view->room_count);
	netbufWriteU8(&wire, view->actor_count);
	for (u32 i = 0; i < view->room_count; i++) {
		const grid_playtest_view_room_t *room = &view->rooms[i];
		writeU16(&wire, room->min_x);
		writeU16(&wire, room->min_z);
		writeU16(&wire, room->max_x);
		writeU16(&wire, room->max_z);
	}
	for (u32 i = 0; i < view->actor_count; i++) {
		const grid_playtest_view_actor_t *actor = &view->actors[i];
		writeU16(&wire, actor->x);
		writeU16(&wire, actor->z);
		netbufWriteU8(&wire, actor->bot);
	}
	return !wire.error && wire.wp == size && netbufWriteData(dst, bytes, size) == size;
}

s32 gridEditPlaytestViewRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_playtest_view_t *view)
{
	if (!validBuffer(src) || !identity || !view) return 0;
	const u32 size = src->wp - src->rp;
	if (size < VIEW_HEADER_SIZE || size > GRID_PLAYTEST_VIEW_WIRE_MAX) return 0;

	struct netbuf wire = *src;
	const grid_edit_wire_identity_t received = readIdentity(&wire);
	grid_playtest_view_t candidate = {0};
	candidate.epoch = netbufReadU32(&wire);
	candidate.serial = netbufReadU32(&wire);
	candidate.room_count = netbufReadU8(&wire);
	candidate.actor_count = netbufReadU8(&wire);
	if (!validIdentity(&received) || !validCounts(candidate.room_count, candidate.actor_count)
			|| size != viewSize(&candidate)) return 0;
	for (u32 i = 0; i < candidate.room_count; i++) {
		grid_playtest_view_room_t *room = &candidate.rooms[i];
		room->min_x = netbufReadU16(&wire);
		room->min_z = netbufReadU16(&wire);
		room->max_x = netbufReadU16(&wire);
		room->max_z = netbufReadU16(&wire);
	}
	for (u32 i = 0; i < candidate.actor_count; i++) {
		grid_playtest_view_actor_t *actor = &candidate.actors[i];
		actor->x = netbufReadU16(&wire);
		actor->z = netbufReadU16(&wire);
		actor->bot = netbufReadU8(&wire);
	}
	if (wire.error || !validView(&candidate)) return 0;
	*identity = received;
	*view = candidate;
	src->rp = wire.rp;
	return 1;
}

static s32 validWorldView(const grid_playtest_view_t *view)
{
	return view && view->has_world_bounds == 1 && view->room_count
		&& __builtin_isfinite(view->min_x) && __builtin_isfinite(view->min_z)
		&& __builtin_isfinite(view->max_x) && __builtin_isfinite(view->max_z)
		&& view->max_x > view->min_x && view->max_z > view->min_z
		&& view->min_x >= -FLT_MAX / 2 && view->min_z >= -FLT_MAX / 2
		&& view->max_x <= FLT_MAX / 2 && view->max_z <= FLT_MAX / 2;
}

s32 gridEditPlaytestWorldViewWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_playtest_view_t *view)
{
	if (!validBuffer(dst) || !validWorldView(view)) return 0;
	const u32 size = viewSize(view) + GRID_PLAYTEST_WORLD_VIEW_FOOTER_SIZE;
	if (size > dst->size - dst->wp) return 0;

	u8 bytes[GRID_PLAYTEST_WORLD_VIEW_WIRE_MAX];
	struct netbuf wire = { .data = bytes, .size = sizeof(bytes) };
	if (!gridEditPlaytestViewWrite(&wire, identity, view)) return 0;
	netbufWriteU8(&wire, GRID_PLAYTEST_WORLD_VIEW_VERSION);
	writeU64(&wire, view->layout_generation);
	writeF32(&wire, view->min_x);
	writeF32(&wire, view->min_z);
	writeF32(&wire, view->max_x);
	writeF32(&wire, view->max_z);
	return !wire.error && wire.wp == size && netbufWriteData(dst, bytes, size) == size;
}

s32 gridEditPlaytestWorldViewRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_playtest_view_t *view)
{
	if (!validBuffer(src) || !identity || !view) return 0;
	const u32 size = src->wp - src->rp;
	if (size < VIEW_HEADER_SIZE + GRID_PLAYTEST_WORLD_VIEW_FOOTER_SIZE
			|| size > GRID_PLAYTEST_WORLD_VIEW_WIRE_MAX) return 0;

	struct netbuf wire = *src;
	wire.wp -= GRID_PLAYTEST_WORLD_VIEW_FOOTER_SIZE;
	grid_edit_wire_identity_t received;
	grid_playtest_view_t candidate;
	if (!gridEditPlaytestViewRead(&wire, &received, &candidate)) return 0;
	wire.wp = src->wp;
	if (netbufReadU8(&wire) != GRID_PLAYTEST_WORLD_VIEW_VERSION) return 0;
	candidate.has_world_bounds = 1;
	candidate.layout_generation = netbufReadU64(&wire);
	candidate.min_x = netbufReadF32(&wire);
	candidate.min_z = netbufReadF32(&wire);
	candidate.max_x = netbufReadF32(&wire);
	candidate.max_z = netbufReadF32(&wire);
	if (wire.error || !validWorldView(&candidate)) return 0;
	*identity = received;
	*view = candidate;
	src->rp = wire.rp;
	return 1;
}

s32 gridEditPlaytestTargetMatchesView(const grid_playtest_target_t *target, const grid_playtest_view_t *view)
{
	return target && target->epoch && validWorldView(view) && view->actor_count
		&& target->epoch == view->epoch && target->layout_generation == view->layout_generation;
}

static s32 validTargetCommand(const grid_playtest_target_t *target, const grid_edit_command_t *command)
{
	return target && target->epoch && command
		&& (command->operation == GRID_EDIT_MOVE_SPAWN || command->operation == GRID_EDIT_MOVE_DECORATION);
}

s32 gridEditPlaytestTargetCommandWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_playtest_target_t *target, const grid_edit_command_t *command)
{
	if (!validBuffer(dst) || !validTargetCommand(target, command)) return 0;
	u8 bytes[GRID_PLAYTEST_TARGET_COMMAND_WIRE_MAX];
	struct netbuf wire = { .data = bytes, .size = sizeof(bytes) };
	netbufWriteU8(&wire, GRID_PLAYTEST_TARGET_COMMAND_MARKER);
	netbufWriteU8(&wire, GRID_PLAYTEST_WORLD_VIEW_VERSION);
	writeU32(&wire, target->epoch);
	writeU64(&wire, target->layout_generation);
	return gridEditCommandWrite(&wire, identity, command) && wire.wp <= dst->size - dst->wp
		&& netbufWriteData(dst, bytes, wire.wp) == wire.wp;
}

s32 gridEditPlaytestTargetCommandRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_playtest_target_t *target, grid_edit_command_t *command)
{
	if (!validBuffer(src) || !identity || !target || !command
			|| src->wp - src->rp <= GRID_PLAYTEST_TARGET_WIRE_PREFIX_SIZE
			|| src->wp - src->rp > GRID_PLAYTEST_TARGET_COMMAND_WIRE_MAX) return 0;
	struct netbuf wire = *src;
	if (netbufReadU8(&wire) != GRID_PLAYTEST_TARGET_COMMAND_MARKER
			|| netbufReadU8(&wire) != GRID_PLAYTEST_WORLD_VIEW_VERSION) return 0;
	grid_playtest_target_t received = {0};
	received.epoch = netbufReadU32(&wire);
	received.layout_generation = netbufReadU64(&wire);
	grid_edit_wire_identity_t received_identity;
	grid_edit_command_t received_command;
	if (!gridEditCommandRead(&wire, &received_identity, &received_command)
			|| !validTargetCommand(&received, &received_command)) return 0;
	*identity = received_identity;
	*target = received;
	*command = received_command;
	src->rp = wire.rp;
	return 1;
}

s32 gridEditPlaytestCursorWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	u8 visible, f32 x, f32 z)
{
	if (!validBuffer(dst) || !validIdentity(identity) || !validCursor(visible, x, z)) return 0;
	const u32 size = visible ? CURSOR_VISIBLE_SIZE : CURSOR_HIDDEN_SIZE;
	if (size > dst->size - dst->wp) return 0;
	u8 bytes[GRID_PLAYTEST_CURSOR_WIRE_MAX];
	struct netbuf wire = { .data = bytes, .size = sizeof(bytes) };
	writeIdentity(&wire, identity);
	netbufWriteU8(&wire, visible);
	if (visible) {
		writeF32(&wire, x);
		writeF32(&wire, z);
	}
	return !wire.error && wire.wp == size && netbufWriteData(dst, bytes, size) == size;
}

s32 gridEditPlaytestCursorRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	u8 *visible, f32 *x, f32 *z)
{
	if (!validBuffer(src) || !identity || !visible || !x || !z) return 0;
	const u32 size = src->wp - src->rp;
	if (size != CURSOR_HIDDEN_SIZE && size != CURSOR_VISIBLE_SIZE) return 0;
	struct netbuf wire = *src;
	const grid_edit_wire_identity_t received = readIdentity(&wire);
	const u8 show = netbufReadU8(&wire);
	if (!validIdentity(&received) || show > 1
			|| size != (show ? CURSOR_VISIBLE_SIZE : CURSOR_HIDDEN_SIZE)) return 0;
	const f32 cursor_x = show ? netbufReadF32(&wire) : 0.0f;
	const f32 cursor_z = show ? netbufReadF32(&wire) : 0.0f;
	if (wire.error || !validCursor(show, cursor_x, cursor_z)) return 0;
	*identity = received;
	*visible = show;
	*x = cursor_x;
	*z = cursor_z;
	src->rp = wire.rp;
	return 1;
}

s32 gridEditPlaytestLayoutCursorWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	u64 token, u8 visible, f32 x, f32 z)
{
	if (!validBuffer(dst) || !token) return 0;
	u8 bytes[GRID_PLAYTEST_CURSOR_LAYOUT_WIRE_MAX];
	struct netbuf wire = { .data = bytes, .size = sizeof(bytes) };
	netbufWriteU8(&wire, GRID_PLAYTEST_CURSOR_LAYOUT_VERSION);
	netbufWriteU64(&wire, token);
	return gridEditPlaytestCursorWrite(&wire, identity, visible, x, z)
		&& wire.wp <= dst->size - dst->wp && netbufWriteData(dst, bytes, wire.wp) == wire.wp;
}

s32 gridEditPlaytestLayoutCursorRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	u64 *token, u8 *visible, f32 *x, f32 *z)
{
	if (!validBuffer(src) || !token || src->wp - src->rp < GRID_PLAYTEST_CURSOR_LAYOUT_PREFIX_SIZE
			|| src->wp - src->rp > GRID_PLAYTEST_CURSOR_LAYOUT_WIRE_MAX) return 0;
	struct netbuf wire = *src;
	const u8 version = netbufReadU8(&wire);
	const u64 received = netbufReadU64(&wire);
	if (version != GRID_PLAYTEST_CURSOR_LAYOUT_VERSION || !received
			|| !gridEditPlaytestCursorRead(&wire, identity, visible, x, z)) return 0;
	*token = received;
	src->rp = wire.rp;
	return 1;
}
