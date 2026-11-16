/**
 * character_tool.h -- the rules of the Modding Hub's Characters tool.
 *
 * The tool makes a character, body or head mod from a template through the
 * mod author (mod_author.h). The window is port/fast3d/pdgui_menu_charactertool.cpp;
 * this module is what it decides, kept apart from the drawing so pd-tests can
 * prove it: which catalog entries the tool lists, how the gender and build a
 * player picks become a descriptor's `ismale` and `type`, and which descriptor
 * values a save sends to the mod author.
 *
 * A body or head descriptor has no gender key. It has `ismale` and `type`
 * (HEADBODYTYPE_*), and its `rig_class` (the neck it fits) follows from the
 * type unless the descriptor names one. The tool keeps the three in step: a
 * human is DEFAULT when male and FEMALE when female, a guard is FEMALEGUARD
 * and female, a Maian is MAIAN and either, and a changed type removes the
 * template's `rig_class` so the reader derives the new one.
 *
 * A save sends only what the player changed, so a template saved as it is
 * keeps every line of its descriptor.
 *
 * Pure: no catalog, no files, no logging.
 */
#ifndef _IN_CHARACTER_TOOL_H
#define _IN_CHARACTER_TOOL_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "mod_author.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What a template is. */
#define CHARACTER_TOOL_KIND_NONE      0
#define CHARACTER_TOOL_KIND_CHARACTER 1
#define CHARACTER_TOOL_KIND_BODY      2
#define CHARACTER_TOOL_KIND_HEAD      3

/* The build a player picks; with the gender it gives the descriptor's type. */
#define CHARACTER_TOOL_BUILD_HUMAN    0 /* DEFAULT when male, FEMALE when female */
#define CHARACTER_TOOL_BUILD_GUARD    1 /* FEMALEGUARD, female only */
#define CHARACTER_TOOL_BUILD_MAIAN    2 /* MAIAN, either gender */
#define CHARACTER_TOOL_BUILD_TEMPLATE 3 /* the template's own type, unchanged */
#define CHARACTER_TOOL_BUILD_COUNT    4

/* A descriptor's height is a u16; 0 is the reader's "not set", which a
 * template may keep but a player cannot choose. */
#define CHARACTER_TOOL_HEIGHT_MIN 1
#define CHARACTER_TOOL_HEIGHT_MAX 65535

/* What the template's descriptor says (a body's or a head's; a character's
 * gender lives in its embedded body and is not edited here yet). */
typedef struct character_tool_template {
	s32 kind;    /* CHARACTER_TOOL_KIND_* */
	s32 ismale;  /* 1 or 0 */
	s32 type;    /* HEADBODYTYPE_* */
	s32 height;
} character_tool_template_t;

/* What the player has set in the creator. */
typedef struct character_tool_form {
	s32 ismale;  /* 1 male, 0 female */
	s32 build;   /* CHARACTER_TOOL_BUILD_* */
	s32 height;
} character_tool_form_t;

/* The descriptor values of one save, with the storage their text needs. */
typedef struct character_tool_values {
	mod_author_descriptor_value_t values[4];
	u32 count;
	char height[8];
} character_tool_values_t;

/* The kind the tool lists a catalog entry under, or CHARACTER_TOOL_KIND_NONE
 * for an asset the tool does not make. */
s32 characterToolKindForAssetType(asset_type_e type);
/* "character", "body" or "head" (the mod author's kind names); "" for none. */
const char *characterToolKindName(s32 kind);
/* "Character", "Body" or "Head", for labels. */
const char *characterToolKindLabel(s32 kind);
/* Whether the creator edits gender, build and height for this kind. */
s32 characterToolKindHasBuild(s32 kind);

/* "Human", "Guard", "Maian" or "Template's own", for labels; "" out of range. */
const char *characterToolBuildLabel(s32 build);
/* The descriptor name of a type ("HEADBODYTYPE_MAIAN"), or NULL when unknown. */
const char *characterToolTypeName(s32 type);

/* The form a template opens with: its gender, its height, and the build its
 * type and gender make. A pair the builds cannot name (Cassandra's and
 * Mr Blonde's rigs, or a type that does not fit the gender) opens as the
 * template's own. */
void characterToolFormFromTemplate(const character_tool_template_t *tmpl,
	character_tool_form_t *form);

/* The type a form saves: the build's for the form's gender, or the
 * template's own. -1 for a build out of range. */
s32 characterToolTypeForForm(const character_tool_form_t *form,
	const character_tool_template_t *tmpl);

/* Whether the form can be saved. 0 writes the reason to why (optional): a
 * guard that is male, a changed height out of range, a build out of range.
 * A character has nothing to check. */
s32 characterToolFormValid(const character_tool_form_t *form,
	const character_tool_template_t *tmpl, char *why, size_t why_cap);

/*
 * The descriptor values a save sends: only what differs from the template.
 * A changed gender sends `ismale`; a changed type sends `type` and removes
 * `rig_class`; a changed height sends `height`. A character sends none.
 * Returns 1 and fills out, or 0 when the form is not valid (out->count 0).
 */
s32 characterToolDescriptorValues(const character_tool_form_t *form,
	const character_tool_template_t *tmpl, character_tool_values_t *out);

/* The words the --debug-character-tool-* flags take. 1 and the value, or 0. */
s32 characterToolParseGender(const char *text, s32 *ismale);   /* male, female */
s32 characterToolParseBuild(const char *text, s32 *build);     /* human, guard, maian, template */

#ifdef __cplusplus
}
#endif

#endif /* _IN_CHARACTER_TOOL_H */
