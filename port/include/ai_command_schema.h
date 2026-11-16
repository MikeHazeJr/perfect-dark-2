#ifndef PD_AI_COMMAND_SCHEMA_H
#define PD_AI_COMMAND_SCHEMA_H

/*
 * ai_command_schema.h -- the typed operands of every AI command
 * (context/designs/modding/ai-typed-operands.md, slices 1-3b).
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

/* Slices 3 and 3b: catalog references. A field of kind model, weapon, body,
 * head, anim or sound is written as a catalog id ("weapon": "base:cmp150")
 * or as null, the field's "none" value with every bit set (a head of -1, a
 * cutscene weapon of 255). The writer asks namer for a value's id; with a
 * namer, a value it cannot name that is not "none" fails the writer (the
 * extractor writes no asset by number). The reader asks resolver for an
 * id's value (0 when it does not resolve); with a resolver, a plain number
 * for a catalog kind is refused. With NULL callbacks these are
 * aiCommandArgsToJson and aiCommandArgsEncode: numbers as they are, and a
 * catalog id is refused.
 *
 * A sound field's 16 bits are an asset and two playback values. With bit
 * 15 set it is a configured sound, and the whole value is the asset.
 * Otherwise bits 11-12 are the MP3 priority (not 0 for an MP3 voice line,
 * whose 11-bit id is then a file number, not a sample), bits 13-14 the MP3
 * response (1 whisper, 2 acknowledge) and bits 0-10 the id. The asset part
 * (what the namer names and the resolver returns) is the configured value,
 * a sample's id, or an MP3 voice line's file number with AI_SOUND_REF_MP3
 * set. Args give the playback values as their own keys after the sound's
 * id: "<field>_priority" (1 to 3; every MP3 voice line has one, nothing
 * else may) and "<field>_response" (1 to 3, written when not 0). */
#define AI_SOUND_REF_CONFIGURED 0x8000
#define AI_SOUND_REF_MP3        0x0800
#define AI_SOUND_REF_ID_MASK    0x07ff
void aiSoundRefSplit(int64_t value, int64_t *out_asset, int *out_priority,
	int *out_response);
int aiOperandKindIsCatalog(ai_operand_kind_t kind);
typedef const char *(*ai_command_ref_namer_fn)(ai_operand_kind_t kind, int64_t value,
	void *user);
typedef int (*ai_command_ref_resolver_fn)(ai_operand_kind_t kind, const char *id,
	int64_t *out_value, void *user);
int aiCommandArgsToJsonRefs(const ai_command_schema_t *schema, const uint8_t *operands,
	uint32_t operand_len, char *out, uint32_t out_size, ai_command_ref_namer_fn namer,
	void *user);
int aiCommandArgsEncodeRefs(const ai_command_schema_t *schema, const char *json,
	uint32_t json_len, uint8_t *out, uint32_t out_size, ai_command_ref_resolver_fn resolver,
	void *user, char *error, uint32_t error_size);

#ifdef __cplusplus
}
#endif

#endif
