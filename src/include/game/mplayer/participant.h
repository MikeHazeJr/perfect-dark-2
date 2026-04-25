/**
 * participant.h — Dynamic Participant System (B-12)
 *
 * Authoritative pool of match participants. Each slot is either empty,
 * a local human, a remote human, or a bot. Slot indices 0..MAX_PLAYERS-1
 * are player slots; MAX_PLAYERS..MAX_MPCHRS-1 are bot slots. Default
 * capacity MAX_MPCHRS; expandable at runtime via cheat.
 *
 * Phase 3 (2026-04-17) removed the legacy u64 chrslots bitmask from
 * struct mpsetup and the wire format. This pool is now the sole source
 * of participant identity.
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
 * Lightweight tracking struct; does NOT replace mpchrconfig. Authoritative
 * source of match participant identity (Phase 3).
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
 * Place a participant at a specific slot index.
 * If the slot already has an active participant, overwrites it.
 * If the slot was NONE, increments the count.
 * Returns the slot index, or -1 if out of range.
 *
 * With a MAX_MPCHRS-capacity pool the slot index has fixed semantics:
 *   players  at 0 .. MAX_PLAYERS-1
 *   bots     at MAX_PLAYERS .. MAX_MPCHRS-1
 */
s32 mpAddParticipantAt(s32 slot, ParticipantType type, u8 team, s8 client_id, u8 localslot);

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
 * Wire Serialization Helpers (B-12 Phase 3)
 * ======================================================================== */

/**
 * Encode the pool as a 64-bit active-slot bitmap, where bit i is set iff
 * slot i has a non-NONE participant. Used by SVC_STAGE_START serialization.
 * Slots beyond 63 (none exist today) are silently clipped.
 */
u64 mpParticipantsEncodeActiveMask(void);

/**
 * Rebuild the participant pool from an active-slot bitmap.
 * Slots with bit set below MAX_PLAYERS are added as PARTICIPANT_REMOTE;
 * slots at MAX_PLAYERS..MAX_MPCHRS-1 are added as PARTICIPANT_BOT.
 * Called by the client when decoding SVC_STAGE_START.
 */
void mpParticipantsDecodeActiveMask(u64 mask);

#endif /* _IN_GAME_MPLAYER_PARTICIPANT_H */
