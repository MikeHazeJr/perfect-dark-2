#ifndef PD_AI_EDGE_SHADOW_H
#define PD_AI_EDGE_SHADOW_H

/*
 * ai_edge_shadow.h -- AI lists as graphs, slice 2
 * (context/designs/modding/ai-list-graphs.md): the runtime's edge tables and
 * the shadow walk that proves the edges in play.
 *
 * When the runtime compiles an AI list (a stage's lists in
 * scenario_source_runtime.c, the AI list banks in scenario_global_ailists.c)
 * it builds the list's table from the same commands with aiListEdgesCompute
 * and registers it under the list's bytes, the pointer the interpreter's
 * cursor holds. On a stage built from scenario source the interpreter
 * (src/game/chrai.c) reports every command it dispatched; the walk finds the
 * command's node by (list, offset) and checks that the cursor moved as the
 * node's edges and flags say. Play does not change: the walk only counts and
 * logs (AI.EDGES lines).
 *
 * Pure and game-independent: no game globals. The tables are owned here and
 * go with the bytes they describe (aiEdgeShadowClearOwner).
 */

#include <stdint.h>

#include "ai_list_edges.h"

#ifdef __cplusplus
extern "C" {
#endif

/* set_camera_animation ends the tick without moving when its cutscene cannot
 * start; that is its documented stop. */
#define AI_EDGE_SHADOW_OP_SET_CAMERA_ANIMATION 0x0111
/* mp_init_simulants stays on its command while the simulant wave is
 * incomplete (B-1255): it stops where it stands, as above. */
#define AI_EDGE_SHADOW_OP_MP_INIT_SIMULANTS 0x0185

/* Log caps per counter period (aiEdgeShadowResetCounters). */
#define AI_EDGE_SHADOW_LOG_DIFFER_MAX    32
#define AI_EDGE_SHADOW_LOG_REFUSED_MAX   32
#define AI_EDGE_SHADOW_LOG_UNTRACKED_MAX 8

/* Who owns a registered table. Each owner's tables go together, with the
 * bytes they describe. */
typedef enum ai_edge_shadow_owner {
	AI_EDGE_SHADOW_OWNER_STAGE = 0, /* a stage's lists: its setup block in the stage pool */
	AI_EDGE_SHADOW_OWNER_BANK,      /* the AI list banks: base:ai_global and mod banks */
	AI_EDGE_SHADOW_OWNER_COUNT
} ai_edge_shadow_owner_t;

/* One compiled list: its bytes (the lookup key), list id, each command's
 * byte offset (ascending), opcode and flags, and its edges by command. */
typedef struct ai_edge_table ai_edge_table_t;

/* Builds the table of one compiled list from its commands in order (the
 * commands the bytes were written from: each takes 2 + operand_len bytes).
 * Returns NULL with a reason in error when the commands have no edges
 * (aiListEdgesCompute refused them) or memory runs out. */
ai_edge_table_t *aiEdgeTableBuild(int32_t list_id, const uint8_t *bytes,
	const ai_list_command_t *commands, uint32_t count, char *error,
	uint32_t error_size);
void aiEdgeTableFree(ai_edge_table_t *table);

int32_t aiEdgeTableListId(const ai_edge_table_t *table);
uint32_t aiEdgeTableCommandCount(const ai_edge_table_t *table);
uint32_t aiEdgeTableEdgeCount(const ai_edge_table_t *table);
/* The command that starts at offset (binary search), or -1. */
int32_t aiEdgeTableCommandAt(const ai_edge_table_t *table, uint32_t offset);
/* The byte offset of command index; index == count gives the list's size. */
uint32_t aiEdgeTableOffsetOf(const ai_edge_table_t *table, uint32_t index);

/* One node as the walk sees it: a command, its flags and its edges. */
typedef struct ai_edge_shadow_node {
	uint32_t index;          /* the command's index in its list */
	uint16_t opcode;
	uint8_t flags;           /* AI_LIST_NODE_* */
	uint32_t edge_count;     /* at most AI_LIST_EDGES_PER_COMMAND */
	const uint32_t *targets; /* edge_count command indexes */
	const uint8_t *kinds;    /* edge_count ai_list_edge_kind_t */
} ai_edge_shadow_node_t;

/* Fills node with command index of table; 0 when index is out of range. */
int aiEdgeTableNode(const ai_edge_table_t *table, uint32_t index,
	ai_edge_shadow_node_t *node);

/* How the command's handler returned. */
typedef enum ai_edge_shadow_move {
	AI_EDGE_SHADOW_MOVE_WENT_ON = 0, /* false, having moved the cursor itself */
	AI_EDGE_SHADOW_MOVE_SKIPPED,     /* false without moving: the B-949 guard
	                                  * advanced the cursor by the command's length */
	AI_EDGE_SHADOW_MOVE_STOPPED,     /* true: the entity's tick ended */
} ai_edge_shadow_move_t;

/* What one command did, as the walk observed it. */
typedef struct ai_edge_shadow_outcome {
	ai_edge_shadow_move_t move;
	int list_changed; /* the cursor left the command's list (for another list or none) */
	int32_t to;       /* the command at the cursor's offset in the same list, -1 for none */
	uint32_t offset;  /* the cursor's offset after the command */
} ai_edge_shadow_outcome_t;

typedef enum ai_edge_shadow_verdict {
	AI_EDGE_SHADOW_AGREE = 0,           /* moved along one of its edges, or stopped,
	                                     * switched or returned as its flags say */
	AI_EDGE_SHADOW_DIFFER,              /* moved where no edge or flag of its node says */
	AI_EDGE_SHADOW_REFUSED_FALLTHROUGH, /* refused (B-949 skip) and fell through to the
	                                     * next command although it has no next edge */
	AI_EDGE_SHADOW_UNTRACKED,           /* its list or offset has no table entry
	                                     * (aiEdgeShadowObserve only) */
} ai_edge_shadow_verdict_t;

/* The rules, with no game state:
 *   stopped  agrees when the cursor stayed in the list and the node yields
 *            and the cursor is at its next edge's target (the saved resume
 *            point), or the node ends (end) or is set_camera_animation and
 *            the cursor did not move;
 *   switched (the cursor left the list) agrees when the node switches list
 *            (set_list on CHR_SELF) or returns; so does such a node that
 *            went on to offset 0 of its own list (a switch to itself);
 *   skipped  (B-949) on a node with no next edge is a refused fall-through
 *            when the cursor is at the next command;
 *   else     the cursor must be at the target of one of the node's edges
 *            (next, branch, goto, restart), and a node that yields or ends
 *            never goes on.
 * Anything else differs. */
ai_edge_shadow_verdict_t aiEdgeShadowClassify(const ai_edge_shadow_node_t *node,
	const ai_edge_shadow_outcome_t *outcome);

/* Registers table under its bytes for owner and takes it. A table already
 * registered under the same bytes is freed and replaced. Returns 1, or 0
 * when out of memory (the table is freed). */
int aiEdgeShadowRegister(ai_edge_shadow_owner_t owner, ai_edge_table_t *table);
/* Drops (and frees) any table registered under bytes: a list whose table
 * could not be built must not be described by an older one. */
void aiEdgeShadowForget(const uint8_t *bytes);
/* Frees every table of owner and drops it from the lookup. */
void aiEdgeShadowClearOwner(ai_edge_shadow_owner_t owner);
/* The table registered under bytes, or NULL. */
const ai_edge_table_t *aiEdgeShadowFind(const uint8_t *bytes);
uint32_t aiEdgeShadowTableCount(ai_edge_shadow_owner_t owner);

/* One command the interpreter dispatched. */
typedef struct ai_edge_shadow_step {
	const uint8_t *list;       /* the command's list and offset, as dispatched */
	uint32_t offset;
	uint16_t opcode;           /* as read from the list's bytes */
	ai_edge_shadow_move_t move;
	const uint8_t *after_list; /* the cursor after the handler and the B-949 guard */
	uint32_t after_offset;
	/* Names a list that has no table in the log (its id, or -1). May be NULL. */
	int32_t (*list_id)(const uint8_t *list);
} ai_edge_shadow_step_t;

/* Checks one dispatched command against its node (aiEdgeShadowClassify),
 * counts it and logs a differ or a refused fall-through once per (list,
 * command) and the first untracked commands, within the log caps. No
 * allocation; O(1) list lookup, O(log n) command lookup. */
ai_edge_shadow_verdict_t aiEdgeShadowObserve(const ai_edge_shadow_step_t *step);

typedef struct ai_edge_shadow_counters {
	int32_t compared;            /* commands checked against their node */
	int32_t differ;              /* of those, moved where their node does not say */
	int32_t refused_fallthrough; /* of those, B-949 fall-throughs without a next edge */
	int32_t untracked;           /* commands whose list or offset has no table entry */
} ai_edge_shadow_counters_t;

void aiEdgeShadowGetCounters(ai_edge_shadow_counters_t *out);
/* Zeroes the counters and the log caps, and lets every command be logged
 * again (the campaign runner calls it as it starts each mission). */
void aiEdgeShadowResetCounters(void);

/* "agree", "differ", "refused_fallthrough", "untracked". */
const char *aiEdgeShadowVerdictName(ai_edge_shadow_verdict_t verdict);

#ifdef __cplusplus
}
#endif

#endif
