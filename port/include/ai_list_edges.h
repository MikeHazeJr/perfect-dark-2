#ifndef PD_AI_LIST_EDGES_H
#define PD_AI_LIST_EDGES_H

/*
 * ai_list_edges.h -- the control flow of one AI list as edges between its
 * commands (context/designs/modding/ai-list-graphs.md, slice 1).
 *
 * Pure and game-independent: the caller passes one list's commands in order
 * (opcode and the bytes after it, cmd[2..]) and gets the edges the native
 * interpreter (src/game/chrai.c chraiExecute and chraiGoToLabel) would take:
 *
 *   next     to the following command (a command's normal successor; a
 *            branching command's "condition false" path; a yield's resume)
 *   branch   a branching command's label target: the first label with its
 *            number at or after the command itself (chraiGoToLabel scans
 *            forward from the command's own offset)
 *   goto     go_to_next (first matching label at or after it) and
 *            go_to_first (first matching label from the top of the list)
 *   restart  a label that is not found before the list's end: chraiGoToLabel
 *            returns offset 0, so the list restarts at its first command
 *
 * and per-command flags: yields (ends the tick, resumes at the next command),
 * ends (end: ends the tick without saving), returns (return: jumps to the
 * return list), switches_list (set_list on CHR_SELF) and reachable (some
 * path from the first command reaches it; a list is entered at its first
 * command only, and a yield's next command is reached through its edge).
 * end, return, set_list on CHR_SELF, go_to_next and go_to_first have no next
 * edge. Conformance (tools/asset_archive_conformance.py) carries the same
 * rules and a copy of the branching table; pd-tests keeps both equal to
 * the handlers in src/game/chraicommands.c and src/game/mplayer/mpaicommands.c.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The interpreter's own opcodes the rules name (g_CommandPointers). */
#define AI_LIST_OP_GO_TO_NEXT  0x0000
#define AI_LIST_OP_GO_TO_FIRST 0x0001
#define AI_LIST_OP_LABEL       0x0002
#define AI_LIST_OP_YIELD       0x0003
#define AI_LIST_OP_END         0x0004
#define AI_LIST_OP_SET_LIST    0x0005
#define AI_LIST_OP_RETURN      0x0008
#define AI_LIST_CHR_SELF       0xfd

typedef enum ai_list_edge_kind {
	AI_LIST_EDGE_NEXT = 0,
	AI_LIST_EDGE_BRANCH,
	AI_LIST_EDGE_GOTO,
	AI_LIST_EDGE_RESTART,
} ai_list_edge_kind_t;

/* Per-command flags (node_flags[i]). */
#define AI_LIST_NODE_YIELDS        0x01u
#define AI_LIST_NODE_ENDS          0x02u
#define AI_LIST_NODE_RETURNS       0x04u
#define AI_LIST_NODE_SWITCHES_LIST 0x08u
#define AI_LIST_NODE_REACHABLE     0x10u

/* A command leaves by at most two edges: next and one jump. */
#define AI_LIST_EDGES_PER_COMMAND 2u

typedef struct ai_list_command {
	uint16_t opcode;
	const uint8_t *operands; /* the command's bytes after the opcode, cmd[2..] */
	uint32_t operand_len;
} ai_list_command_t;

typedef struct ai_list_edge {
	uint32_t from; /* command index in the list */
	uint32_t to;
	ai_list_edge_kind_t kind;
} ai_list_edge_t;

typedef enum ai_list_edges_result {
	AI_LIST_EDGES_OK = 0,
	AI_LIST_EDGES_INVALID_ARGUMENT,
	AI_LIST_EDGES_NO_ROOM,
	AI_LIST_EDGES_MISSING_OPERAND,
	AI_LIST_EDGES_NO_MEMORY,
} ai_list_edges_result_t;

/* Computes the edges and flags of one list. edges must hold edge_capacity
 * entries (count * AI_LIST_EDGES_PER_COMMAND always suffices); they come out
 * grouped by command, in command order, next before the command's jump.
 * node_flags must hold count entries. On AI_LIST_EDGES_MISSING_OPERAND,
 * *out_bad_command (when given) names the command whose label (or, for
 * set_list, chr) operand is missing. */
ai_list_edges_result_t aiListEdgesCompute(const ai_list_command_t *commands,
	uint32_t count, ai_list_edge_t *edges, uint32_t edge_capacity,
	uint32_t *out_edge_count, uint8_t *node_flags, uint32_t *out_bad_command);

/* The branching commands: every handler whose native path jumps with
 * chraiGoToLabel(g_Vars.ailist, g_Vars.aioffset, label) when its condition
 * holds, other than go_to_next. Returns the index of the label byte in the
 * command (cmd[n], so n >= 2), or -1 when the opcode does not branch. */
int32_t aiListEdgesBranchLabelByte(uint16_t opcode);

/* The branching table in opcode order, for tests and tools. */
uint32_t aiListEdgesBranchOpcodeCount(void);
int aiListEdgesBranchOpcodeAt(uint32_t index, uint16_t *out_opcode,
	uint8_t *out_label_byte);

/* "next", "branch", "goto", "restart" (NULL for an unknown kind). */
const char *aiListEdgeKindName(ai_list_edge_kind_t kind);

const char *aiListEdgesResultName(ai_list_edges_result_t result);

#ifdef __cplusplus
}
#endif

#endif
