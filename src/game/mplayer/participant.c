/**
 * participant.c — Dynamic Participant System (B-12)
 *
 * Heap-allocated pool of match participants, replacing the fixed u32 chrslots
 * bitmask. See participant.h for the full API and design rationale.
 */

#include <ultra64.h>
#include <string.h>
#include <stdlib.h>
#include "constants.h"
#include "game/mplayer/participant.h"

MpParticipantPool g_MpParticipants = { NULL, 0, 0 };

/* ========================================================================
 * Pool Lifecycle
 * ======================================================================== */

void mpParticipantPoolInit(s32 initial_capacity)
{
	if (g_MpParticipants.slots != NULL) {
		mpParticipantPoolFree();
	}

	if (initial_capacity <= 0) {
		initial_capacity = PARTICIPANT_DEFAULT_CAPACITY;
	}

	g_MpParticipants.slots = (MpParticipant *)malloc(initial_capacity * sizeof(MpParticipant));

	if (g_MpParticipants.slots == NULL) {
		g_MpParticipants.capacity = 0;
		g_MpParticipants.count = 0;
		return;
	}

	memset(g_MpParticipants.slots, 0, initial_capacity * sizeof(MpParticipant));
	g_MpParticipants.capacity = initial_capacity;
	g_MpParticipants.count = 0;

	/* All slots start as PARTICIPANT_NONE (0) via memset */
}

void mpParticipantPoolFree(void)
{
	if (g_MpParticipants.slots != NULL) {
		free(g_MpParticipants.slots);
		g_MpParticipants.slots = NULL;
	}

	g_MpParticipants.count = 0;
	g_MpParticipants.capacity = 0;
}

bool mpParticipantPoolResize(s32 new_capacity)
{
	if (new_capacity <= 0) {
		return false;
	}

	MpParticipant *newslots = (MpParticipant *)realloc(
		g_MpParticipants.slots,
		new_capacity * sizeof(MpParticipant)
	);

	if (newslots == NULL) {
		return false;
	}

	/* Zero-fill any newly added slots */
	if (new_capacity > g_MpParticipants.capacity) {
		memset(
			&newslots[g_MpParticipants.capacity],
			0,
			(new_capacity - g_MpParticipants.capacity) * sizeof(MpParticipant)
		);
	}

	g_MpParticipants.slots = newslots;

	/* Recount active participants if capacity shrank */
	if (new_capacity < g_MpParticipants.capacity) {
		s32 active = 0;
		for (s32 i = 0; i < new_capacity; i++) {
			if (newslots[i].type != PARTICIPANT_NONE) {
				active++;
			}
		}
		g_MpParticipants.count = active;
	}

	g_MpParticipants.capacity = new_capacity;
	return true;
}

/* ========================================================================
 * Slot Management
 * ======================================================================== */

s32 mpAddParticipant(ParticipantType type, u8 team, s8 client_id, u8 localslot)
{
	s32 index = mpGetFirstEmptySlot();

	if (index < 0) {
		return -1;
	}

	MpParticipant *p = &g_MpParticipants.slots[index];
	p->type = type;
	p->team = team;
	p->localslot = localslot;
	p->client_id = client_id;
	p->legacy_slot = -1;
	p->config = NULL;
	p->chr = NULL;

	g_MpParticipants.count++;

	return index;
}

void mpRemoveParticipant(s32 index)
{
	if (index < 0 || index >= g_MpParticipants.capacity) {
		return;
	}

	if (g_MpParticipants.slots[index].type != PARTICIPANT_NONE) {
		memset(&g_MpParticipants.slots[index], 0, sizeof(MpParticipant));
		g_MpParticipants.count--;
	}
}

void mpRemoveClientParticipants(s8 client_id)
{
	if (g_MpParticipants.slots == NULL) {
		return;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type != PARTICIPANT_NONE &&
			g_MpParticipants.slots[i].client_id == client_id) {
			memset(&g_MpParticipants.slots[i], 0, sizeof(MpParticipant));
			g_MpParticipants.count--;
		}
	}
}

void mpClearAllParticipants(void)
{
	if (g_MpParticipants.slots != NULL) {
		memset(g_MpParticipants.slots, 0,
			g_MpParticipants.capacity * sizeof(MpParticipant));
	}

	g_MpParticipants.count = 0;
}

/* ========================================================================
 * Queries
 * ======================================================================== */

bool mpIsParticipantActive(s32 index)
{
	if (index < 0 || index >= g_MpParticipants.capacity || g_MpParticipants.slots == NULL) {
		return false;
	}

	return g_MpParticipants.slots[index].type != PARTICIPANT_NONE;
}

s32 mpGetActiveParticipantCount(void)
{
	return g_MpParticipants.count;
}

s32 mpGetActiveBotCount(void)
{
	s32 count = 0;

	if (g_MpParticipants.slots == NULL) {
		return 0;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type == PARTICIPANT_BOT) {
			count++;
		}
	}

	return count;
}

s32 mpGetActivePlayerCount(void)
{
	s32 count = 0;

	if (g_MpParticipants.slots == NULL) {
		return 0;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		ParticipantType type = g_MpParticipants.slots[i].type;
		if (type == PARTICIPANT_LOCAL || type == PARTICIPANT_REMOTE) {
			count++;
		}
	}

	return count;
}

s32 mpGetActiveLocalPlayerCount(void)
{
	s32 count = 0;

	if (g_MpParticipants.slots == NULL) {
		return 0;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type == PARTICIPANT_LOCAL) {
			count++;
		}
	}

	return count;
}

s32 mpGetActiveRemotePlayerCount(void)
{
	s32 count = 0;

	if (g_MpParticipants.slots == NULL) {
		return 0;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type == PARTICIPANT_REMOTE) {
			count++;
		}
	}

	return count;
}

s32 mpGetFirstEmptySlot(void)
{
	if (g_MpParticipants.slots == NULL) {
		return -1;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type == PARTICIPANT_NONE) {
			return i;
		}
	}

	return -1;
}

MpParticipant *mpGetParticipant(s32 index)
{
	if (index < 0 || index >= g_MpParticipants.capacity || g_MpParticipants.slots == NULL) {
		return NULL;
	}

	if (g_MpParticipants.slots[index].type == PARTICIPANT_NONE) {
		return NULL;
	}

	return &g_MpParticipants.slots[index];
}

/* ========================================================================
 * Iteration
 * ======================================================================== */

s32 mpParticipantFirst(void)
{
	if (g_MpParticipants.slots == NULL) {
		return -1;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type != PARTICIPANT_NONE) {
			return i;
		}
	}

	return -1;
}

s32 mpParticipantNext(s32 current)
{
	if (g_MpParticipants.slots == NULL) {
		return -1;
	}

	for (s32 i = current + 1; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type != PARTICIPANT_NONE) {
			return i;
		}
	}

	return -1;
}

s32 mpParticipantFirstOfType(ParticipantType type)
{
	if (g_MpParticipants.slots == NULL) {
		return -1;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type == type) {
			return i;
		}
	}

	return -1;
}

s32 mpParticipantNextOfType(s32 current, ParticipantType type)
{
	if (g_MpParticipants.slots == NULL) {
		return -1;
	}

	for (s32 i = current + 1; i < g_MpParticipants.capacity; i++) {
		if (g_MpParticipants.slots[i].type == type) {
			return i;
		}
	}

	return -1;
}

/* ========================================================================
 * Legacy Compatibility (Phase 1 only — removed in Phase 3)
 * ======================================================================== */

u64 mpParticipantsToLegacyChrslots(void)
{
	u64 chrslots = 0;

	if (g_MpParticipants.slots == NULL) {
		return 0;
	}

	for (s32 i = 0; i < g_MpParticipants.capacity; i++) {
		MpParticipant *p = &g_MpParticipants.slots[i];

		if (p->type == PARTICIPANT_NONE) {
			continue;
		}

		s32 bit = p->legacy_slot;

		if (bit < 0) {
			continue;
		}

		if (p->type == PARTICIPANT_BOT) {
			bit += BOT_SLOT_OFFSET;
		}

		if (bit >= 0 && bit < 64) {
			chrslots |= (1ull << bit);
		}
	}

	return chrslots;
}

void mpParticipantsFromLegacyChrslots(u64 chrslots)
{
	mpClearAllParticipants();

	/* Reconstruct player slots (bits 0-7) */
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (chrslots & (1ull << i)) {
			s32 idx = mpAddParticipant(PARTICIPANT_LOCAL, 0, 0, (u8)i);
			if (idx >= 0) {
				g_MpParticipants.slots[idx].legacy_slot = i;
			}
		}
	}

	/* Reconstruct bot slots (bits 8-39) */
	for (s32 i = 0; i < MAX_BOTS; i++) {
		if (chrslots & (1ull << (i + BOT_SLOT_OFFSET))) {
			s32 idx = mpAddParticipant(PARTICIPANT_BOT, 0, -1, 0xFF);
			if (idx >= 0) {
				g_MpParticipants.slots[idx].legacy_slot = i;
			}
		}
	}
}
