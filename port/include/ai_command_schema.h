#ifndef PD_AI_COMMAND_SCHEMA_H
#define PD_AI_COMMAND_SCHEMA_H

/*
 * ai_command_schema.h -- the typed operands of every AI command
 * (context/designs/modding/ai-typed-operands.md, slice 1).
 *
 * Pure and game-independent. For each opcode the schema gives the command's
 * name, its length (opcode included; 0 for print, whose text ends at a NUL)
 * and its fields in byte order. Every operand byte, cmd[2] to
 * cmd[length - 1], belongs to exactly one field. Multi-byte values are
 * big-endian, as the handlers combine them; signed fields are two's
 * complement. Bytes no code path reads are fields of kind unused.
 *
 * The table was derived from the native handlers (src/game/chraicommands.c,
 * src/game/chrai.c, src/game/mplayer/mpaicommands.c) and their graph twins'
 * parameter names, then checked against every base AI row. Conformance
 * (tools/asset_archive_conformance.py) carries a copy; pd-tests keeps the
 * copy equal to this table, the lengths equal to g_CommandLengths, and the
 * fields consistent with the bytes each handler reads.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ai_operand_kind {
	AI_OPERAND_UNUSED = 0,
	AI_OPERAND_NUMBER,
	AI_OPERAND_BOOL,
	AI_OPERAND_FLAGS,
	AI_OPERAND_LABEL,
	AI_OPERAND_CHR,
	AI_OPERAND_PAD,
	AI_OPERAND_OBJECT,
	AI_OPERAND_ROOM,
	AI_OPERAND_AI_LIST,
	AI_OPERAND_ANIM,
	AI_OPERAND_SOUND,
	AI_OPERAND_MUSIC,
	AI_OPERAND_TEXT,
	AI_OPERAND_TEAM,
	AI_OPERAND_SQUADRON,
	AI_OPERAND_OBJECTIVE,
	AI_OPERAND_PATH,
	AI_OPERAND_PORTAL,
	AI_OPERAND_MODEL,
	AI_OPERAND_WEAPON,
	AI_OPERAND_BODY,
	AI_OPERAND_HEAD,
	AI_OPERAND_STRING,
	AI_OPERAND_KIND_COUNT
} ai_operand_kind_t;

typedef struct ai_command_field {
	const char *name;
	uint8_t offset;    /* from the start of the command (the opcode is 0-1) */
	uint8_t width;     /* 1 to 4 bytes; 0 for the print text */
	uint8_t is_signed;
	uint8_t kind;      /* ai_operand_kind_t */
} ai_command_field_t;

typedef struct ai_command_schema {
	uint16_t opcode;
	const char *name;
	uint8_t length;    /* bytes with the opcode; 0 = print (variable) */
	uint8_t field_count;
	const ai_command_field_t *fields;
} ai_command_schema_t;

#define AI_COMMAND_OP_PRINT 0x00b5

/* The command with this opcode, or NULL when no handler has one. */
const ai_command_schema_t *aiCommandSchemaFind(uint16_t opcode);
/* Every command in opcode order. */
uint32_t aiCommandSchemaCount(void);
const ai_command_schema_t *aiCommandSchemaAt(uint32_t index);
/* The field of this name, or NULL. */
const ai_command_field_t *aiCommandSchemaField(const ai_command_schema_t *schema,
	const char *name);

/* A kind's public name ("chr", "label", ...) and back; -1 for an unknown name. */
const char *aiOperandKindName(ai_operand_kind_t kind);
int aiOperandKindFromName(const char *name);

/* Reads a field's value from a command's operand bytes (cmd[2..],
 * operand_len of them). Returns 1, or 0 when the field does not fit. */
int aiCommandFieldRead(const ai_command_field_t *field, const uint8_t *operands,
	uint32_t operand_len, int64_t *out_value);
/* Writes a value into a command's operand bytes. Returns 1, or 0 when the
 * field does not fit or the value is outside the field's range. */
int aiCommandFieldWrite(const ai_command_field_t *field, uint8_t *operands,
	uint32_t operand_len, int64_t value);
/* The smallest and largest value a field holds. */
void aiCommandFieldRange(const ai_command_field_t *field, int64_t *out_min,
	int64_t *out_max);

/* Slice 2: a command's arguments as a JSON object, e.g. {"chr": 36,
 * "label": 6}, in field order. An unused field is written only when its
 * bytes are not zero (it keeps a base row byte-exact; it has no effect);
 * print's text is a JSON string. Writes a NUL-terminated object into out
 * and returns its length, or -1 when the operands do not fit the command
 * (print: no NUL at the end) or out is too small. */
int aiCommandArgsToJson(const ai_command_schema_t *schema, const uint8_t *operands,
	uint32_t operand_len, char *out, uint32_t out_size);

/* Encodes a JSON arguments object (json points at its '{', json_len covers
 * it through its '}') into the command's operand bytes. Every field but an
 * unused one is required; an unused field defaults to zero; a key that is
 * no field, a value outside its field's range or a malformed object is
 * refused. Returns the operand count written to out, or -1 with the reason
 * in error. */
int aiCommandArgsEncode(const ai_command_schema_t *schema, const char *json,
	uint32_t json_len, uint8_t *out, uint32_t out_size, char *error,
	uint32_t error_size);

#ifdef __cplusplus
}
#endif

#endif
