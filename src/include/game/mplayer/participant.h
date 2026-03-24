/**
 * participant.h — Dynamic Participant System (B-12)
 *
 * Replaces the fixed u32 chrslots bitmask with a dynamic, heap-allocated pool
 * of participant descriptors. Any slot can be a local player, remote player,
 * or bot. Default capacity 32, expandable at runtime via cheat.
 *
 * Phase 1: Runs parallel to chrslots. Both systems updated together.
 * Phase 2: Callsites migrated from chrslots to participant API.
 * Phase 3: chrslots removed.
 */

#ifndef _IN_GAME_MPLAYER_PARTICIPANT_H
#define _IN_GAME_MPLAYER_PARTICIPANT_H

#include <ultra64.h>
#include "types.h"

/* Default pool capacity — defined in constants.h as PARTICIPANT_DEFAULT_CAPACITY */

/* Maximum local splitscreen players per machine (hardware-bound) */
#define PARTICIPANT_MAX_LOCAL         MAX_LOCAL_PLAYERS

typedef enum {
	PARTICIPANT_NONE   = 0, /* Empty slot */
	PARTICIPANT_LOCAL  = 1, /* Local human (splitscreen slot 0-3 on this machine) */
	PARTICIPANT_REMOTE = 2, /* Remote human (connected via network) */
	PARTICIPANT_BOT    = 3, /* AI simulant */
} ParticipantType;

/**
 * Per-participant descriptor.
 *
 * This is a lightweight tracking struct — it does NOT replace mpchrconfig.
 * During Phase 1 it mirrors chrslots; during Phase 2+ it becomes the
 * authoritative source of match participant info.
 */
typedef struct mpparticipant {
	ParticipantType type;

	u8 team;

	/**
	 * Local player index (0-3) relative to the owning machine.
	 * For LOCAL: maps to this machine's splitscreen slot.
	 * For REMOTE: maps to the remote machine's splitscreen slot.
	 * For BOT: 0xFF (unused).
	 */
	u8 localslot;

	/**
	 * Network client ID that owns this participant.
	 * -1 = bot (server-owned AI)
	 *  0 = server/host machine
	 *  1+ = remote client index
	 */
	s8 client_id;

	/**
	 * Legacy slot index in the old chrslots bitmask.
	 * Players: 0-7 (maps to g_PlayerConfigsArray index)
	 * Bots: 0-23 (maps to g_BotConfigsArray index, with BOT_SLOT_OFFSET in bitmask)
	 *
	 * Used during Phase 1 for compatibility. Removed in Phase 3.
	 */
	s32 legacy_slot;

	/* Runtime pointers — set when the match starts, NULL in lobby */
	struct mpchrconfig *config;
	struct chrdata *chr;
} MpParticipant;

/**
 * The participant pool — dynamic array of participant descriptors.
 */
typedef struct mpparticipantpool {
	MpParticipant *slots;  /* Heap-allocated array, length = capacity */
	s32 count;             /* Number of active (non-NONE) participants */
	s32 capacity;          /* Current array length */
} MpParticipantPool;

/* Global participant pool instance */
extern MpParticipantPool g_MpParticipants;

/* ========================================================================
 * Pool Lifecycle
 * ======================================================================== */

/**
 * Allocate the pool with the given initial capacity.
 * Call once at startup or when entering multiplayer mode.
 */
void mpParticipantPoolInit(s32 initial_capacity);

/**
 * Free all pool memory. Call when leaving multiplayer mode.
 */
void mpParticipantPoolFree(void);

/**
 * Resize the pool to a new capacity.
 * Preserves existing participants up to min(old_capacity, new_capacity).
 * Returns true on success, false if allocation failed.
 */
bool mpParticipantPoolResize(s32 new_capacity);

/* ========================================================================
 * Slot Management
 * ======================================================================== */

/**
 * Add a participant to the first empty slot.
 * Returns the slot index, or -1 if pool is full.
 */
s32 mpAddParticipant(ParticipantType type, u8 team, s8 client_id, u8 localslot);

/**
 * Remove the participant at the given index (sets to PARTICIPANT_NONE).
 */
void mpRemoveParticipant(s32 index);

/**
 * Remove all participants belonging to a specific network client.
 * Used on client disconnect.
 */
void mpRemoveClientParticipants(s8 client_id);

/**
 * Clear all participants (reset pool to empty).
 */
void mpClearAllParticipants(void);

/* ========================================================================
 * Queries
 * ======================================================================== */

bool mpIsParticipantActive(s32 index);
s32  mpGetActiveParticipantCount(void);
s32  mpGetActiveBotCount(void);
s32  mpGetActivePlayerCount(void);    /* LOCAL + REMOTE humans */
s32  mpGetActiveLocalPlayerCount(void);
s32  mpGetActiveRemotePlayerCount(void);
s32  mpGetFirstEmptySlot(void);

/**
 * Get the participant at the given index.
 * Returns NULL if index is out of range or slot is NONE.
 */
MpParticipant *mpGetParticipant(s32 index);

/* ========================================================================
 * Iteration
 * ======================================================================== */

/**
 * Iterate over active participants:
 *   for (s32 i = mpParticipantFirst(); i >= 0; i = mpParticipantNext(i))
 */
s32 mpParticipantFirst(void);
s32 mpParticipantNext(s32 current);

/**
 * Iterate over active participants of a specific type:
 *   for (s32 i = mpParticipantFirstOfType(PARTICIPANT_BOT);
 *        i >= 0; i = mpParticipantNextOfType(i, PARTICIPANT_BOT))
 */
s32 mpParticipantFirstOfType(ParticipantType type);
s32 mpParticipantNextOfType(s32 current, ParticipantType type);

/* ========================================================================
 * Legacy Compatibility (Phase 1 only — removed in Phase 3)
 * ======================================================================== */

/**
 * Generate a u32 chrslots bitmask from the current participant pool.
 * Used by code not yet migrated to the participant API.
 * Players occupy bits 0-7, bots occupy bits 8-39 (u64).
 * Participants beyond the 40-slot limit are silently excluded.
 */
u64 mpParticipantsToLegacyChrslots(void);

/**
 * Populate the participant pool from a legacy chrslots bitmask.
 * Used when loading saved setups.
 */
void mpParticipantsFromLegacyChrslots(u64 chrslots);

#endif /* _IN_GAME_MPLAYER_PARTICIPANT_H */
