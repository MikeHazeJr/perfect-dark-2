#ifndef PD2_PDTHEME_SOURCE_H
#define PD2_PDTHEME_SOURCE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PDTHEME_SOURCE_SCHEMA "pd2.theme.v1"
#define PDTHEME_SOURCE_MAX_TEXTURES 16
#define PDTHEME_SOURCE_MAX_NINESLICES 16
#define PDTHEME_SOURCE_MAX_EFFECTS 8

typedef struct pdtheme_source_info {
	char catalog_id[64];
	char name[128];
	char author[64];
	char version[32];
	s32 palette_fields;
	s32 texture_roles;
	s32 nineslices;
	s32 caustics;
	s32 border_effects;
} pdtheme_source_info_t;

/* Strict parser for the authoritative public theme.json source. It rejects
 * unknown or duplicate fields, wrong JSON types, truncated strings, nonfinite
 * or out-of-range numbers, raw authored paths, invalid catalog references,
 * malformed colors, and fixed-capacity overflow. expected_catalog_id may be
 * NULL for standalone validation; the source still must declare an ID. */
s32 pdthemeSourceParse(const char *json, size_t json_size,
	const char *expected_catalog_id, pdtheme_source_info_t *out,
	char *error, size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif
