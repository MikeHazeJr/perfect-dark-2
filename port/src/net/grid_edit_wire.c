#include <stdint.h>
#include <string.h>

#include "net/grid_edit_wire.h"
#include "net/netbuf.h"
#include "platform.h"

enum {
	EDIT_IDENTITY_SIZE = 1 + 8,
	EDIT_DELTA_METADATA_SIZE = 2 * 8,
	EDIT_COMMAND_HEADER_SIZE = 2 * 8 + 1,
	EDIT_SPAWN_ID_SIZE = sizeof("spawn_0000") - 1,
	EDIT_DECORATION_ID_SIZE = sizeof("setup_0000") - 1,
	EDIT_MOVE_PAYLOAD_SIZE = EDIT_SPAWN_ID_SIZE + 6 * 4,
	EDIT_DECORATION_PAYLOAD_SIZE = EDIT_DECORATION_ID_SIZE + 2 * 4,
	EDIT_AMBIENT_PAYLOAD_SIZE = 2,
	EDIT_FOCUS_HEADER_SIZE = EDIT_IDENTITY_SIZE + 1
};

_Static_assert(sizeof(f32) == sizeof(u32), "Grid move floats require a 32-bit representation");
_Static_assert(GRID_EDIT_SPAWN_ID_CAP > EDIT_SPAWN_ID_SIZE, "decoded spawn ID requires a terminator");
_Static_assert(GRID_EDIT_SPAWN_ID_CAP > EDIT_DECORATION_ID_SIZE, "decoded decoration ID requires a terminator");
_Static_assert(EDIT_DECORATION_PAYLOAD_SIZE <= EDIT_MOVE_PAYLOAD_SIZE, "decoration must fit the command wire bound");
_Static_assert(GRID_EDIT_COMMAND_WIRE_MAX == EDIT_IDENTITY_SIZE + EDIT_COMMAND_HEADER_SIZE
	+ EDIT_MOVE_PAYLOAD_SIZE, "command wire bound must fit the largest command");
_Static_assert(GRID_EDIT_DELTA_WIRE_MAX == GRID_EDIT_COMMAND_WIRE_MAX + EDIT_DELTA_METADATA_SIZE,
	"delta wire bound must include authority metadata");
_Static_assert(GRID_EDIT_FOCUS_WIRE_MAX == EDIT_FOCUS_HEADER_SIZE + EDIT_SPAWN_ID_SIZE
	&& EDIT_SPAWN_ID_SIZE == EDIT_DECORATION_ID_SIZE, "focus wire bound must cover both typed IDs");

static s32 validIdentity(const grid_edit_wire_identity_t *identity)
{
	return identity && identity->room_id != 0xff && identity->generation;
}

static u32 commandPayloadSize(grid_edit_operation_t operation)
{
	switch (operation) {
	case GRID_EDIT_MOVE_SPAWN: return EDIT_MOVE_PAYLOAD_SIZE;
	case GRID_EDIT_SET_AMBIENT_LIGHT: return EDIT_AMBIENT_PAYLOAD_SIZE;
	case GRID_EDIT_MOVE_DECORATION: return EDIT_DECORATION_PAYLOAD_SIZE;
	}
	return 0;
}

static s32 validMove(const grid_edit_move_spawn_t *move)
{
	const char *id = move->spawn_id;
	static const char prefix[] = "spawn_";
	if (memcmp(id, prefix, sizeof(prefix) - 1) != 0 || id[EDIT_SPAWN_ID_SIZE] != '\0') return 0;
	for (size_t i = sizeof(prefix) - 1; i < EDIT_SPAWN_ID_SIZE; i++) {
		if (id[i] < '0' || id[i] > '9') return 0;
	}

	/* Same minimum direction length used by the native document move. */
	double length_squared = 0;
	for (size_t i = 0; i < 3; i++) {
		if (!__builtin_isfinite(move->position[i]) || !__builtin_isfinite(move->look[i])) return 0;
		length_squared += (double)move->look[i] * move->look[i];
	}
	return __builtin_sqrt(length_squared) >= 1e-6;
}

static s32 validCommand(const grid_edit_command_t *command)
{
	if (!command || !command->sequence || !command->expected_revision) return 0;
	switch (command->operation) {
	case GRID_EDIT_MOVE_SPAWN:
		return validMove(&command->data.move_spawn);
	case GRID_EDIT_SET_AMBIENT_LIGHT:
		return (command->data.ambient_light.enabled == 0 || command->data.ambient_light.enabled == 1)
			&& command->data.ambient_light.brightness >= 0 && command->data.ambient_light.brightness <= UINT8_MAX;
	case GRID_EDIT_MOVE_DECORATION:
		return gridEditMoveDecorationValid(&command->data.move_decoration);
	}
	return 0;
}

static s32 validDelta(const grid_edit_delta_t *delta)
{
	return delta->author_id && delta->revision > 1
		&& delta->revision - 1 == delta->command.expected_revision;
}

/* netbufWriteData takes care of the bounds while memcpy permits a message
 * prefix or a field to start at an unaligned offset. */
static void writeU64(struct netbuf *dst, u64 value)
{
	u64 encoded = PD_LE64(value);
	netbufWriteData(dst, &encoded, sizeof(encoded));
}

static void writeF32(struct netbuf *dst, f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	bits = PD_LE32(bits);
	netbufWriteData(dst, &bits, sizeof(bits));
}

static void writeCommand(struct netbuf *dst, const grid_edit_command_t *command)
{
	writeU64(dst, command->sequence);
	writeU64(dst, command->expected_revision);
	netbufWriteU8(dst, (u8)command->operation);
	switch (command->operation) {
	case GRID_EDIT_MOVE_SPAWN:
		netbufWriteData(dst, command->data.move_spawn.spawn_id, EDIT_SPAWN_ID_SIZE);
		for (size_t i = 0; i < 3; i++) writeF32(dst, command->data.move_spawn.position[i]);
		for (size_t i = 0; i < 3; i++) writeF32(dst, command->data.move_spawn.look[i]);
		break;
	case GRID_EDIT_SET_AMBIENT_LIGHT:
		netbufWriteU8(dst, (u8)command->data.ambient_light.enabled);
		netbufWriteU8(dst, (u8)command->data.ambient_light.brightness);
		break;
	case GRID_EDIT_MOVE_DECORATION:
		netbufWriteData(dst, command->data.move_decoration.record_id, EDIT_DECORATION_ID_SIZE);
		writeF32(dst, command->data.move_decoration.x);
		writeF32(dst, command->data.move_decoration.z);
		break;
	}
}

static s32 writePayload(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_edit_command_t *command, const grid_edit_delta_t *delta)
{
	if (!dst || !dst->data || dst->error || dst->rp > dst->wp || dst->wp > dst->size
			|| !validIdentity(identity) || !validCommand(command)) return 0;

	u32 size = EDIT_IDENTITY_SIZE + EDIT_COMMAND_HEADER_SIZE + commandPayloadSize(command->operation);
	if (delta) size += EDIT_DELTA_METADATA_SIZE;
	if (size > dst->size - dst->wp) return 0;

	u8 bytes[GRID_EDIT_DELTA_WIRE_MAX];
	struct netbuf wire = {bytes, sizeof(bytes), 0, 0, 0};
	netbufWriteU8(&wire, identity->room_id);
	writeU64(&wire, identity->generation);
	if (delta) {
		writeU64(&wire, delta->revision);
		writeU64(&wire, delta->author_id);
	}
	writeCommand(&wire, command);
	if (wire.error || wire.wp != size) return 0;
	return netbufWriteData(dst, bytes, size) == size;
}

static s32 readCommand(struct netbuf *src, grid_edit_command_t *command)
{
	command->sequence = netbufReadU64(src);
	command->expected_revision = netbufReadU64(src);
	command->operation = (grid_edit_operation_t)netbufReadU8(src);
	u32 size = commandPayloadSize(command->operation);
	if (src->error || !size || src->wp - src->rp != size) return 0;

	switch (command->operation) {
	case GRID_EDIT_MOVE_SPAWN:
		netbufReadData(src, command->data.move_spawn.spawn_id, EDIT_SPAWN_ID_SIZE);
		command->data.move_spawn.spawn_id[EDIT_SPAWN_ID_SIZE] = '\0';
		for (size_t i = 0; i < 3; i++) command->data.move_spawn.position[i] = netbufReadF32(src);
		for (size_t i = 0; i < 3; i++) command->data.move_spawn.look[i] = netbufReadF32(src);
		break;
	case GRID_EDIT_SET_AMBIENT_LIGHT:
		command->data.ambient_light.enabled = netbufReadU8(src);
		command->data.ambient_light.brightness = netbufReadU8(src);
		break;
	case GRID_EDIT_MOVE_DECORATION:
		netbufReadData(src, command->data.move_decoration.record_id, EDIT_DECORATION_ID_SIZE);
		command->data.move_decoration.record_id[EDIT_DECORATION_ID_SIZE] = '\0';
		command->data.move_decoration.x = netbufReadF32(src);
		command->data.move_decoration.z = netbufReadF32(src);
		break;
	}
	return !src->error && validCommand(command);
}

static s32 readPayload(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_edit_command_t *command, grid_edit_delta_t *delta)
{
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size || !identity) return 0;

	const u32 prefix = EDIT_IDENTITY_SIZE + EDIT_COMMAND_HEADER_SIZE + (delta ? EDIT_DELTA_METADATA_SIZE : 0);
	const u32 remaining = src->wp - src->rp;
	if (remaining != prefix + EDIT_MOVE_PAYLOAD_SIZE && remaining != prefix + EDIT_AMBIENT_PAYLOAD_SIZE
			&& remaining != prefix + EDIT_DECORATION_PAYLOAD_SIZE) return 0;

	struct netbuf wire = *src;
	grid_edit_wire_identity_t received_identity = {0};
	grid_edit_delta_t received = {0};
	received_identity.room_id = netbufReadU8(&wire);
	received_identity.generation = netbufReadU64(&wire);
	if (delta) {
		received.revision = netbufReadU64(&wire);
		received.author_id = netbufReadU64(&wire);
	}
	if (!validIdentity(&received_identity) || !readCommand(&wire, &received.command)
			|| (delta && !validDelta(&received))) return 0;

	*identity = received_identity;
	if (delta) *delta = received;
	else *command = received.command;
	src->rp = wire.rp;
	return 1;
}

s32 gridEditCommandWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_edit_command_t *command)
{
	return writePayload(dst, identity, command, NULL);
}

s32 gridEditCommandRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_edit_command_t *command)
{
	return command && readPayload(src, identity, command, NULL);
}

s32 gridEditDeltaWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_edit_delta_t *delta)
{
	return delta && validDelta(delta) && writePayload(dst, identity, &delta->command, delta);
}

s32 gridEditDeltaRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_edit_delta_t *delta)
{
	return delta && readPayload(src, identity, NULL, delta);
}

static u32 focusIdSize(grid_edit_resource_kind_t kind)
{
	switch (kind) {
	case GRID_EDIT_RESOURCE_SPAWN: return EDIT_SPAWN_ID_SIZE;
	case GRID_EDIT_RESOURCE_DECORATION: return EDIT_DECORATION_ID_SIZE;
	default: return 0;
	}
}

s32 gridEditFocusResourceWrite(struct netbuf *dst, const grid_edit_wire_identity_t *identity,
	const grid_edit_resource_t *resource_or_null)
{
	if (!dst || !dst->data || dst->error || dst->rp > dst->wp || dst->wp > dst->size
			|| !validIdentity(identity) || (resource_or_null && !gridEditResourceValid(resource_or_null))) return 0;
	const grid_edit_resource_kind_t kind = resource_or_null ? resource_or_null->kind : 0;
	const u32 id_size = focusIdSize(kind);
	const u32 size = EDIT_FOCUS_HEADER_SIZE + id_size;
	if (size > dst->size - dst->wp) return 0;

	u8 bytes[GRID_EDIT_FOCUS_WIRE_MAX];
	struct netbuf wire = {bytes, sizeof(bytes), 0, 0, 0};
	netbufWriteU8(&wire, identity->room_id);
	writeU64(&wire, identity->generation);
	netbufWriteU8(&wire, (u8)kind);
	if (id_size) {
		const char *id = kind == GRID_EDIT_RESOURCE_SPAWN ? resource_or_null->spawn_id : resource_or_null->record_id;
		netbufWriteData(&wire, id, id_size);
	}
	return !wire.error && wire.wp == size && netbufWriteData(dst, bytes, size) == size;
}

s32 gridEditFocusResourceRead(struct netbuf *src, grid_edit_wire_identity_t *identity,
	grid_edit_resource_t *out, s32 *has_resource)
{
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size
			|| !identity || !out || !has_resource) return 0;
	const u32 size = src->wp - src->rp;
	if (size != EDIT_FOCUS_HEADER_SIZE && size != GRID_EDIT_FOCUS_WIRE_MAX) return 0;
	struct netbuf wire = *src;
	grid_edit_wire_identity_t received = {0};
	received.room_id = netbufReadU8(&wire);
	received.generation = netbufReadU64(&wire);
	grid_edit_resource_t candidate = {0};
	candidate.kind = (grid_edit_resource_kind_t)netbufReadU8(&wire);
	const u32 id_size = focusIdSize(candidate.kind);
	if (!validIdentity(&received) || size != EDIT_FOCUS_HEADER_SIZE + id_size) return 0;
	if (id_size) {
		char *id = candidate.kind == GRID_EDIT_RESOURCE_SPAWN ? candidate.spawn_id : candidate.record_id;
		netbufReadData(&wire, id, id_size);
		id[id_size] = '\0';
	}
	if (wire.error || (candidate.kind && !gridEditResourceValid(&candidate))) return 0;
	*identity = received;
	*out = candidate;
	*has_resource = candidate.kind != 0;
	src->rp = wire.rp;
	return 1;
}
