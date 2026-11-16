/*
 * ai_edge_shadow.c -- AI lists as graphs, slice 2: the runtime's edge tables
 * and the shadow walk (see ai_edge_shadow.h;
 * context/designs/modding/ai-list-graphs.md).
 */

#include "ai_edge_shadow.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>
#include "system.h"

struct ai_edge_table {
	const uint8_t *bytes;
	int32_t list_id;
	uint32_t count;
	uint32_t edge_count;
	ai_edge_shadow_owner_t owner;
	uint32_t *offsets;    /* count + 1: [count] is the list's size */
	uint32_t *first_edge; /* count + 1: command i's edges are [first_edge[i], first_edge[i + 1]) */
	uint32_t *targets;    /* edge_count */
	uint16_t *opcodes;    /* count */
	uint8_t *kinds;       /* edge_count */
	uint8_t *flags;       /* count */
	uint8_t *logged;      /* count: this command's differ or fall-through was logged */
};

/* The lookup: open addressing with linear probing, keyed by the list's bytes,
 * at most half full. */
static struct {
	ai_edge_table_t **slots;
	uint32_t capacity; /* 0 or a power of two */
	uint32_t used;
	ai_edge_table_t *last; /* the table the last lookup found */
} s_Lookup;

static ai_edge_shadow_counters_t s_Counters;
static int32_t s_LoggedDiffer;
static int32_t s_LoggedRefused;
static int32_t s_LoggedUntracked;

static void s_setError(char *error, uint32_t error_size, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
static void s_setError(char *error, uint32_t error_size, const char *fmt, ...)
{
	va_list args;

	if (!error || !error_size) {
		return;
	}
	va_start(args, fmt);
	vsnprintf(error, error_size, fmt, args);
	va_end(args);
}

ai_edge_table_t *aiEdgeTableBuild(int32_t list_id, const uint8_t *bytes,
	const ai_list_command_t *commands, uint32_t count, char *error,
	uint32_t error_size)
{
	ai_list_edge_t *edges;
	uint8_t *flags;
	uint32_t edge_count = 0;
	uint32_t bad_command = 0;
	ai_list_edges_result_t result;
	ai_edge_table_t *table;
	uint64_t offset = 0;
	size_t size;
	uint8_t *cursor;

	s_setError(error, error_size, "%s", "");
	if (!bytes || !commands || count == 0) {
		s_setError(error, error_size, "%s", "no commands");
		return NULL;
	}
	if (count > 0x7fffffffu / AI_LIST_EDGES_PER_COMMAND) {
		s_setError(error, error_size, "%u commands is too many", (unsigned)count);
		return NULL;
	}

	edges = (ai_list_edge_t *)malloc((size_t)count * AI_LIST_EDGES_PER_COMMAND
		* sizeof(*edges) + count);
	if (!edges) {
		s_setError(error, error_size, "%s", "out of memory");
		return NULL;
	}
	flags = (uint8_t *)(edges + (size_t)count * AI_LIST_EDGES_PER_COMMAND);
	result = aiListEdgesCompute(commands, count, edges,
		count * AI_LIST_EDGES_PER_COMMAND, &edge_count, flags, &bad_command);
	if (result != AI_LIST_EDGES_OK) {
		if (result == AI_LIST_EDGES_MISSING_OPERAND) {
			s_setError(error, error_size, "command %u (opcode 0x%04x): %s",
				(unsigned)bad_command, (unsigned)commands[bad_command].opcode,
				aiListEdgesResultName(result));
		} else {
			s_setError(error, error_size, "%s", aiListEdgesResultName(result));
		}
		free(edges);
		return NULL;
	}

	size = sizeof(*table)
		+ ((size_t)count + 1) * sizeof(uint32_t) * 2 /* offsets, first_edge */
		+ (size_t)edge_count * sizeof(uint32_t)      /* targets */
		+ (size_t)count * sizeof(uint16_t)           /* opcodes */
		+ (size_t)edge_count                         /* kinds */
		+ (size_t)count * 2;                         /* flags, logged */
	table = (ai_edge_table_t *)calloc(1, size);
	if (!table) {
		free(edges);
		s_setError(error, error_size, "%s", "out of memory");
		return NULL;
	}
	cursor = (uint8_t *)(table + 1);
	table->offsets = (uint32_t *)cursor;
	cursor += ((size_t)count + 1) * sizeof(uint32_t);
	table->first_edge = (uint32_t *)cursor;
	cursor += ((size_t)count + 1) * sizeof(uint32_t);
	table->targets = (uint32_t *)cursor;
	cursor += (size_t)edge_count * sizeof(uint32_t);
	table->opcodes = (uint16_t *)cursor;
	cursor += (size_t)count * sizeof(uint16_t);
	table->kinds = cursor;
	cursor += edge_count;
	table->flags = cursor;
	cursor += count;
	table->logged = cursor;

	table->bytes = bytes;
	table->list_id = list_id;
	table->count = count;
	table->edge_count = edge_count;
	table->owner = AI_EDGE_SHADOW_OWNER_STAGE;

	/* Each command takes its opcode's two bytes and its operands, as the
	 * compilers lay the list out. */
	for (uint32_t i = 0; i < count; i++) {
		table->offsets[i] = (uint32_t)offset;
		table->opcodes[i] = commands[i].opcode;
		table->flags[i] = flags[i];
		offset += 2u + (uint64_t)commands[i].operand_len;
		if (offset > 0xffffffffu) {
			free(table);
			free(edges);
			s_setError(error, error_size, "%s", "list is larger than 4 GB");
			return NULL;
		}
	}
	table->offsets[count] = (uint32_t)offset;

	/* By command: aiListEdgesCompute gives them grouped by command, next
	 * first; a counting pass keeps that order whatever the grouping. */
	for (uint32_t e = 0; e < edge_count; e++) {
		if (edges[e].from >= count || edges[e].to >= count) {
			free(table);
			free(edges);
			s_setError(error, error_size, "edge %u leaves the list", (unsigned)e);
			return NULL;
		}
		table->first_edge[edges[e].from + 1]++;
	}
	for (uint32_t i = 0; i < count; i++) {
		table->first_edge[i + 1] += table->first_edge[i];
	}
	{
		uint32_t *fill = (uint32_t *)calloc((size_t)count, sizeof(*fill));

		if (!fill) {
			free(table);
			free(edges);
			s_setError(error, error_size, "%s", "out of memory");
			return NULL;
		}
		for (uint32_t e = 0; e < edge_count; e++) {
			const uint32_t from = edges[e].from;
			const uint32_t at = table->first_edge[from] + fill[from]++;

			table->targets[at] = edges[e].to;
			table->kinds[at] = (uint8_t)edges[e].kind;
		}
		free(fill);
	}
	free(edges);
	return table;
}

void aiEdgeTableFree(ai_edge_table_t *table)
{
	free(table);
}

int32_t aiEdgeTableListId(const ai_edge_table_t *table)
{
	return table ? table->list_id : -1;
}

uint32_t aiEdgeTableCommandCount(const ai_edge_table_t *table)
{
	return table ? table->count : 0;
}

uint32_t aiEdgeTableEdgeCount(const ai_edge_table_t *table)
{
	return table ? table->edge_count : 0;
}

int32_t aiEdgeTableCommandAt(const ai_edge_table_t *table, uint32_t offset)
{
	uint32_t lo = 0;
	uint32_t hi;

	if (!table) {
		return -1;
	}
	hi = table->count;
	while (lo < hi) {
		const uint32_t mid = lo + (hi - lo) / 2;

		if (table->offsets[mid] == offset) {
			return (int32_t)mid;
		}
		if (table->offsets[mid] < offset) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return -1;
}

uint32_t aiEdgeTableOffsetOf(const ai_edge_table_t *table, uint32_t index)
{
	if (!table || index > table->count) {
		return 0;
	}
	return table->offsets[index];
}

int aiEdgeTableNode(const ai_edge_table_t *table, uint32_t index,
	ai_edge_shadow_node_t *node)
{
	if (!table || !node || index >= table->count) {
		return 0;
	}
	node->index = index;
	node->opcode = table->opcodes[index];
	node->flags = table->flags[index];
	node->edge_count = table->first_edge[index + 1] - table->first_edge[index];
	node->targets = table->targets + table->first_edge[index];
	node->kinds = table->kinds + table->first_edge[index];
	return 1;
}

ai_edge_shadow_verdict_t aiEdgeShadowClassify(const ai_edge_shadow_node_t *node,
	const ai_edge_shadow_outcome_t *outcome)
{
	const uint8_t leaves = AI_LIST_NODE_SWITCHES_LIST | AI_LIST_NODE_RETURNS;
	int64_t next = -1;

	if (!node || !outcome || (node->edge_count > 0 && (!node->targets || !node->kinds))) {
		return AI_EDGE_SHADOW_DIFFER;
	}
	for (uint32_t e = 0; e < node->edge_count; e++) {
		if (node->kinds[e] == AI_LIST_EDGE_NEXT) {
			next = node->targets[e];
			break;
		}
	}

	if (outcome->move == AI_EDGE_SHADOW_MOVE_STOPPED) {
		if (outcome->list_changed) {
			return AI_EDGE_SHADOW_DIFFER;
		}
		/* yield saves the next command as the resume point. */
		if (node->flags & AI_LIST_NODE_YIELDS) {
			return next >= 0 && outcome->to == next
				? AI_EDGE_SHADOW_AGREE : AI_EDGE_SHADOW_DIFFER;
		}
		/* end, and set_camera_animation whose cutscene cannot start, end
		 * the tick where they stand. */
		if ((node->flags & AI_LIST_NODE_ENDS)
				|| node->opcode == AI_EDGE_SHADOW_OP_SET_CAMERA_ANIMATION) {
			return outcome->to >= 0 && (uint32_t)outcome->to == node->index
				? AI_EDGE_SHADOW_AGREE : AI_EDGE_SHADOW_DIFFER;
		}
		return AI_EDGE_SHADOW_DIFFER;
	}

	/* A yield or an end always ends the tick. */
	if (node->flags & (AI_LIST_NODE_YIELDS | AI_LIST_NODE_ENDS)) {
		return AI_EDGE_SHADOW_DIFFER;
	}
	if (outcome->list_changed) {
		return (node->flags & leaves) ? AI_EDGE_SHADOW_AGREE : AI_EDGE_SHADOW_DIFFER;
	}
	/* set_list or return to the list that runs: it starts again at offset 0. */
	if ((node->flags & leaves) && outcome->move == AI_EDGE_SHADOW_MOVE_WENT_ON
			&& outcome->offset == 0 && outcome->to == 0) {
		return AI_EDGE_SHADOW_AGREE;
	}
	/* B-949: a command that went on without moving is skipped to the next
	 * command, which a node without a next edge does not describe. */
	if (outcome->move == AI_EDGE_SHADOW_MOVE_SKIPPED && next < 0) {
		return outcome->to >= 0 && (uint32_t)outcome->to == node->index + 1
			? AI_EDGE_SHADOW_REFUSED_FALLTHROUGH : AI_EDGE_SHADOW_DIFFER;
	}
	if (outcome->to < 0) {
		return AI_EDGE_SHADOW_DIFFER;
	}
	for (uint32_t e = 0; e < node->edge_count; e++) {
		if (node->targets[e] == (uint32_t)outcome->to) {
			return AI_EDGE_SHADOW_AGREE;
		}
	}
	return AI_EDGE_SHADOW_DIFFER;
}

const char *aiEdgeShadowVerdictName(ai_edge_shadow_verdict_t verdict)
{
	switch (verdict) {
	case AI_EDGE_SHADOW_AGREE: return "agree";
	case AI_EDGE_SHADOW_DIFFER: return "differ";
	case AI_EDGE_SHADOW_REFUSED_FALLTHROUGH: return "refused_fallthrough";
	case AI_EDGE_SHADOW_UNTRACKED: return "untracked";
	}
	return "unknown";
}

/* ---- the lookup ---- */

static uint32_t s_hash(const uint8_t *bytes, uint32_t mask)
{
	const uint64_t key = (uint64_t)(uintptr_t)bytes;

	return (uint32_t)((key * 0x9e3779b97f4a7c15ull) >> 32) & mask;
}

static int64_t s_slotOf(const uint8_t *bytes)
{
	uint32_t mask;
	uint32_t i;

	if (!s_Lookup.capacity || !bytes) {
		return -1;
	}
	mask = s_Lookup.capacity - 1;
	i = s_hash(bytes, mask);
	/* At most half full, so an empty slot ends every probe. */
	while (s_Lookup.slots[i]) {
		if (s_Lookup.slots[i]->bytes == bytes) {
			return i;
		}
		i = (i + 1) & mask;
	}
	return -1;
}

static void s_place(ai_edge_table_t **slots, uint32_t capacity,
	ai_edge_table_t *table)
{
	const uint32_t mask = capacity - 1;
	uint32_t i = s_hash(table->bytes, mask);

	while (slots[i]) {
		i = (i + 1) & mask;
	}
	slots[i] = table;
}

/* Room for `want` tables with the lookup at most half full. */
static int s_reserve(uint32_t want)
{
	ai_edge_table_t **slots;
	uint32_t capacity = s_Lookup.capacity ? s_Lookup.capacity : 256u;

	while (want > capacity / 2) {
		if (capacity > 0x40000000u) {
			return 0;
		}
		capacity *= 2;
	}
	if (capacity == s_Lookup.capacity) {
		return 1;
	}
	slots = (ai_edge_table_t **)calloc(capacity, sizeof(*slots));
	if (!slots) {
		return 0;
	}
	for (uint32_t i = 0; i < s_Lookup.capacity; i++) {
		if (s_Lookup.slots[i]) {
			s_place(slots, capacity, s_Lookup.slots[i]);
		}
	}
	free(s_Lookup.slots);
	s_Lookup.slots = slots;
	s_Lookup.capacity = capacity;
	return 1;
}

/* Frees the table in slot i and closes the gap (backward shift), so every
 * probe still reaches its table. */
static void s_removeAt(uint32_t i)
{
	const uint32_t mask = s_Lookup.capacity - 1;
	uint32_t j = i;

	if (s_Lookup.last == s_Lookup.slots[i]) {
		s_Lookup.last = NULL;
	}
	aiEdgeTableFree(s_Lookup.slots[i]);
	s_Lookup.slots[i] = NULL;
	s_Lookup.used--;
	for (;;) {
		uint32_t home;

		j = (j + 1) & mask;
		if (!s_Lookup.slots[j]) {
			break;
		}
		home = s_hash(s_Lookup.slots[j]->bytes, mask);
		/* Stays when its home lies cyclically in (i, j]. */
		if (i <= j ? (i < home && home <= j) : (i < home || home <= j)) {
			continue;
		}
		s_Lookup.slots[i] = s_Lookup.slots[j];
		s_Lookup.slots[j] = NULL;
		i = j;
	}
}

int aiEdgeShadowRegister(ai_edge_shadow_owner_t owner, ai_edge_table_t *table)
{
	int64_t slot;

	if (!table || (unsigned)owner >= AI_EDGE_SHADOW_OWNER_COUNT || !table->bytes) {
		aiEdgeTableFree(table);
		return 0;
	}
	table->owner = owner;
	slot = s_slotOf(table->bytes);
	if (slot >= 0) {
		ai_edge_table_t *old = s_Lookup.slots[slot];

		if (s_Lookup.last == old) {
			s_Lookup.last = NULL;
		}
		s_Lookup.slots[slot] = table;
		aiEdgeTableFree(old);
		return 1;
	}
	if (!s_reserve(s_Lookup.used + 1)) {
		aiEdgeTableFree(table);
		return 0;
	}
	s_place(s_Lookup.slots, s_Lookup.capacity, table);
	s_Lookup.used++;
	return 1;
}

void aiEdgeShadowForget(const uint8_t *bytes)
{
	const int64_t slot = s_slotOf(bytes);

	if (slot >= 0) {
		s_removeAt((uint32_t)slot);
	}
}

void aiEdgeShadowClearOwner(ai_edge_shadow_owner_t owner)
{
	uint32_t i = 0;

	while (i < s_Lookup.capacity) {
		/* A removal can shift a later table into slot i: look again. Slots
		 * before i hold no table of owner, and a shift never moves a later
		 * table before i. */
		if (s_Lookup.slots[i] && s_Lookup.slots[i]->owner == owner) {
			s_removeAt(i);
		} else {
			i++;
		}
	}
	if (s_Lookup.used == 0) {
		free(s_Lookup.slots);
		s_Lookup.slots = NULL;
		s_Lookup.capacity = 0;
		s_Lookup.last = NULL;
	}
}

static ai_edge_table_t *s_find(const uint8_t *bytes)
{
	int64_t slot;

	if (s_Lookup.last && s_Lookup.last->bytes == bytes) {
		return s_Lookup.last;
	}
	slot = s_slotOf(bytes);
	if (slot < 0) {
		return NULL;
	}
	s_Lookup.last = s_Lookup.slots[slot];
	return s_Lookup.last;
}

const ai_edge_table_t *aiEdgeShadowFind(const uint8_t *bytes)
{
	return s_find(bytes);
}

uint32_t aiEdgeShadowTableCount(ai_edge_shadow_owner_t owner)
{
	uint32_t count = 0;

	for (uint32_t i = 0; i < s_Lookup.capacity; i++) {
		if (s_Lookup.slots[i] && s_Lookup.slots[i]->owner == owner) {
			count++;
		}
	}
	return count;
}

/* ---- the walk ---- */

static void s_count(int32_t *counter)
{
	if (*counter < 0x7fffffff) {
		(*counter)++;
	}
}

/* Takes a line of a capped log; logs the cap once when it is reached. */
static int s_takeLogLine(int32_t *logged, int32_t max, const char *what)
{
	if (*logged < max) {
		(*logged)++;
		return 1;
	}
	if (*logged == max) {
		(*logged)++;
		sysLogPrintf(LOG_NOTE,
			"AI.EDGES: further %s lines are not logged until the counters reset",
			what);
	}
	return 0;
}

static void s_describeNode(const ai_edge_shadow_node_t *node, char *out,
	size_t out_size)
{
	static const struct {
		uint8_t flag;
		const char *name;
	} flag_names[] = {
		{ AI_LIST_NODE_YIELDS, "yields" },
		{ AI_LIST_NODE_ENDS, "ends" },
		{ AI_LIST_NODE_RETURNS, "returns" },
		{ AI_LIST_NODE_SWITCHES_LIST, "switches_list" },
	};
	size_t used = 0;
	int any = 0;

	out[0] = '\0';
	used += (size_t)snprintf(out + used, out_size - used, "edges");
	for (uint32_t e = 0; e < node->edge_count && used < out_size; e++) {
		const char *kind = aiListEdgeKindName((ai_list_edge_kind_t)node->kinds[e]);

		used += (size_t)snprintf(out + used, out_size - used, " %s->%u",
			kind ? kind : "?", (unsigned)node->targets[e]);
		any = 1;
	}
	if (!any && used < out_size) {
		used += (size_t)snprintf(out + used, out_size - used, " none");
	}
	if (used < out_size) {
		used += (size_t)snprintf(out + used, out_size - used, ", flags");
	}
	any = 0;
	for (size_t f = 0; f < sizeof(flag_names) / sizeof(flag_names[0]) && used < out_size; f++) {
		if (node->flags & flag_names[f].flag) {
			used += (size_t)snprintf(out + used, out_size - used, "%s%s",
				any ? "|" : " ", flag_names[f].name);
			any = 1;
		}
	}
	if (!any && used < out_size) {
		used += (size_t)snprintf(out + used, out_size - used, " none");
	}
	if (used < out_size && !(node->flags & AI_LIST_NODE_REACHABLE)) {
		snprintf(out + used, out_size - used, " (unreachable)");
	}
}

static void s_describeOutcome(const ai_edge_shadow_outcome_t *outcome,
	char *out, size_t out_size)
{
	const char *how = outcome->move == AI_EDGE_SHADOW_MOVE_STOPPED ? "stopped at"
		: outcome->move == AI_EDGE_SHADOW_MOVE_SKIPPED ? "skipped (B-949) to"
		: "went on to";

	if (outcome->list_changed) {
		snprintf(out, out_size, "%s another list",
			outcome->move == AI_EDGE_SHADOW_MOVE_STOPPED ? "stopped in" : how);
	} else if (outcome->to >= 0) {
		snprintf(out, out_size, "%s command %d (+%u)", how, (int)outcome->to,
			(unsigned)outcome->offset);
	} else {
		snprintf(out, out_size, "%s +%u, where no command starts", how,
			(unsigned)outcome->offset);
	}
}

static void s_logUntracked(const ai_edge_shadow_step_t *step,
	const ai_edge_table_t *table, int32_t index)
{
	if (!s_takeLogLine(&s_LoggedUntracked, AI_EDGE_SHADOW_LOG_UNTRACKED_MAX,
			"untracked")) {
		return;
	}
	if (!table) {
		const int32_t id = step->list_id ? step->list_id(step->list) : -1;

		sysLogPrintf(LOG_WARNING,
			"AI.EDGES: untracked: list 0x%04x has no edge table (opcode 0x%04x at +%u)",
			(unsigned)(id & 0xffff), (unsigned)step->opcode,
			(unsigned)step->offset);
	} else if (index < 0) {
		sysLogPrintf(LOG_WARNING,
			"AI.EDGES: untracked: list 0x%04x has no command at +%u (opcode 0x%04x there)",
			(unsigned)(table->list_id & 0xffff), (unsigned)step->offset,
			(unsigned)step->opcode);
	} else {
		sysLogPrintf(LOG_WARNING,
			"AI.EDGES: untracked: list 0x%04x command %d at +%u is opcode 0x%04x in its table and 0x%04x in its bytes",
			(unsigned)(table->list_id & 0xffff), (int)index,
			(unsigned)step->offset, (unsigned)table->opcodes[index],
			(unsigned)step->opcode);
	}
}

ai_edge_shadow_verdict_t aiEdgeShadowObserve(const ai_edge_shadow_step_t *step)
{
	ai_edge_table_t *table;
	ai_edge_shadow_node_t node;
	ai_edge_shadow_outcome_t outcome;
	ai_edge_shadow_verdict_t verdict;
	int32_t index;
	char expected[160];
	char actual[96];

	if (!step || !step->list) {
		s_count(&s_Counters.untracked);
		return AI_EDGE_SHADOW_UNTRACKED;
	}
	table = s_find(step->list);
	index = table ? aiEdgeTableCommandAt(table, step->offset) : -1;
	if (index < 0 || table->opcodes[index] != step->opcode) {
		s_count(&s_Counters.untracked);
		s_logUntracked(step, table, index);
		return AI_EDGE_SHADOW_UNTRACKED;
	}

	aiEdgeTableNode(table, (uint32_t)index, &node);
	outcome.move = step->move;
	outcome.list_changed = step->after_list != step->list;
	outcome.offset = step->after_offset;
	outcome.to = outcome.list_changed ? -1
		: aiEdgeTableCommandAt(table, step->after_offset);
	verdict = aiEdgeShadowClassify(&node, &outcome);
	s_count(&s_Counters.compared);

	if (verdict == AI_EDGE_SHADOW_DIFFER) {
		s_count(&s_Counters.differ);
		if (!table->logged[index] && s_takeLogLine(&s_LoggedDiffer,
				AI_EDGE_SHADOW_LOG_DIFFER_MAX, "differ")) {
			table->logged[index] = 1;
			s_describeNode(&node, expected, sizeof(expected));
			s_describeOutcome(&outcome, actual, sizeof(actual));
			sysLogPrintf(LOG_WARNING,
				"AI.EDGES: differ list 0x%04x command %d (opcode 0x%04x at +%u; %s): %s",
				(unsigned)(table->list_id & 0xffff), (int)index,
				(unsigned)node.opcode, (unsigned)step->offset, expected, actual);
		}
	} else if (verdict == AI_EDGE_SHADOW_REFUSED_FALLTHROUGH) {
		s_count(&s_Counters.refused_fallthrough);
		if (!table->logged[index] && s_takeLogLine(&s_LoggedRefused,
				AI_EDGE_SHADOW_LOG_REFUSED_MAX, "refused_fallthrough")) {
			table->logged[index] = 1;
			sysLogPrintf(LOG_NOTE,
				"AI.EDGES: refused_fallthrough list 0x%04x command %d (opcode 0x%04x at +%u): refused without a next edge; the B-949 guard went on to command %d (+%u)",
				(unsigned)(table->list_id & 0xffff), (int)index,
				(unsigned)node.opcode, (unsigned)step->offset, (int)outcome.to,
				(unsigned)outcome.offset);
		}
	}
	return verdict;
}

void aiEdgeShadowGetCounters(ai_edge_shadow_counters_t *out)
{
	if (out) {
		*out = s_Counters;
	}
}

void aiEdgeShadowResetCounters(void)
{
	memset(&s_Counters, 0, sizeof(s_Counters));
	s_LoggedDiffer = 0;
	s_LoggedRefused = 0;
	s_LoggedUntracked = 0;
	for (uint32_t i = 0; i < s_Lookup.capacity; i++) {
		if (s_Lookup.slots[i]) {
			memset(s_Lookup.slots[i]->logged, 0, s_Lookup.slots[i]->count);
		}
	}
}
