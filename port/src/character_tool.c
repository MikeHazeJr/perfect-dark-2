/**
 * character_tool.c -- the rules of the Modding Hub's Characters tool: see
 * character_tool.h.
 */
#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "character_tool.h"
#include "constants.h"

s32 characterToolKindForAssetType(asset_type_e type)
{
	switch (type) {
	case ASSET_CHARACTER: return CHARACTER_TOOL_KIND_CHARACTER;
	case ASSET_BODY: return CHARACTER_TOOL_KIND_BODY;
	case ASSET_HEAD: return CHARACTER_TOOL_KIND_HEAD;
	default: return CHARACTER_TOOL_KIND_NONE;
	}
}

const char *characterToolKindName(s32 kind)
{
	switch (kind) {
	case CHARACTER_TOOL_KIND_CHARACTER: return "character";
	case CHARACTER_TOOL_KIND_BODY: return "body";
	case CHARACTER_TOOL_KIND_HEAD: return "head";
	default: return "";
	}
}

const char *characterToolKindLabel(s32 kind)
{
	switch (kind) {
	case CHARACTER_TOOL_KIND_CHARACTER: return "Character";
	case CHARACTER_TOOL_KIND_BODY: return "Body";
	case CHARACTER_TOOL_KIND_HEAD: return "Head";
	default: return "";
	}
}

s32 characterToolKindHasBuild(s32 kind)
{
	return kind == CHARACTER_TOOL_KIND_BODY || kind == CHARACTER_TOOL_KIND_HEAD;
}

const char *characterToolBuildLabel(s32 build)
{
	switch (build) {
	case CHARACTER_TOOL_BUILD_HUMAN: return "Human";
	case CHARACTER_TOOL_BUILD_GUARD: return "Guard";
	case CHARACTER_TOOL_BUILD_MAIAN: return "Maian";
	case CHARACTER_TOOL_BUILD_TEMPLATE: return "Template's own";
	default: return "";
	}
}

const char *characterToolTypeName(s32 type)
{
	switch (type) {
	case HEADBODYTYPE_DEFAULT: return "HEADBODYTYPE_DEFAULT";
	case HEADBODYTYPE_FEMALE: return "HEADBODYTYPE_FEMALE";
	case HEADBODYTYPE_FEMALEGUARD: return "HEADBODYTYPE_FEMALEGUARD";
	case HEADBODYTYPE_MAIAN: return "HEADBODYTYPE_MAIAN";
	case HEADBODYTYPE_CASS: return "HEADBODYTYPE_CASS";
	case HEADBODYTYPE_MRBLONDE: return "HEADBODYTYPE_MRBLONDE";
	default: return NULL;
	}
}

void characterToolFormFromTemplate(const character_tool_template_t *tmpl,
	character_tool_form_t *form)
{
	if (!form) {
		return;
	}
	memset(form, 0, sizeof(*form));
	form->ismale = 1;
	form->build = CHARACTER_TOOL_BUILD_TEMPLATE;
	if (!tmpl) {
		return;
	}
	form->ismale = tmpl->ismale ? 1 : 0;
	form->height = tmpl->height;
	if (tmpl->type == HEADBODYTYPE_MAIAN) {
		form->build = CHARACTER_TOOL_BUILD_MAIAN;
	} else if (tmpl->type == HEADBODYTYPE_DEFAULT && form->ismale) {
		form->build = CHARACTER_TOOL_BUILD_HUMAN;
	} else if (tmpl->type == HEADBODYTYPE_FEMALE && !form->ismale) {
		form->build = CHARACTER_TOOL_BUILD_HUMAN;
	} else if (tmpl->type == HEADBODYTYPE_FEMALEGUARD && !form->ismale) {
		form->build = CHARACTER_TOOL_BUILD_GUARD;
	}
}

s32 characterToolTypeForForm(const character_tool_form_t *form,
	const character_tool_template_t *tmpl)
{
	if (!form) {
		return -1;
	}
	switch (form->build) {
	case CHARACTER_TOOL_BUILD_HUMAN:
		return form->ismale ? HEADBODYTYPE_DEFAULT : HEADBODYTYPE_FEMALE;
	case CHARACTER_TOOL_BUILD_GUARD:
		return HEADBODYTYPE_FEMALEGUARD;
	case CHARACTER_TOOL_BUILD_MAIAN:
		return HEADBODYTYPE_MAIAN;
	case CHARACTER_TOOL_BUILD_TEMPLATE:
		return tmpl ? tmpl->type : -1;
	default:
		return -1;
	}
}

/* Writes a refusal's reason and returns 0. */
static s32 characterToolRefuse(char *why, size_t why_cap, const char *text)
{
	if (why && why_cap) {
		snprintf(why, why_cap, "%s", text);
	}
	return 0;
}

s32 characterToolFormValid(const character_tool_form_t *form,
	const character_tool_template_t *tmpl, char *why, size_t why_cap)
{
	if (why && why_cap) {
		why[0] = '\0';
	}
	if (!form || !tmpl) {
		return characterToolRefuse(why, why_cap, "no template is open");
	}
	if (tmpl->kind == CHARACTER_TOOL_KIND_CHARACTER) {
		return 1;
	}
	if (!characterToolKindHasBuild(tmpl->kind)) {
		return characterToolRefuse(why, why_cap, "the template is not a character, body or head");
	}
	if (form->build < 0 || form->build >= CHARACTER_TOOL_BUILD_COUNT) {
		return characterToolRefuse(why, why_cap, "pick a build");
	}
	if (form->build == CHARACTER_TOOL_BUILD_GUARD && form->ismale) {
		return characterToolRefuse(why, why_cap, "the guard build is female");
	}
	if (!characterToolTypeName(characterToolTypeForForm(form, tmpl))) {
		return characterToolRefuse(why, why_cap, "the template's own type is not one the game knows");
	}
	/* A template with no height (0, the reader's default) may keep it. */
	if (form->height != tmpl->height
			&& (form->height < CHARACTER_TOOL_HEIGHT_MIN || form->height > CHARACTER_TOOL_HEIGHT_MAX)) {
		return characterToolRefuse(why, why_cap, "the height must be from 1 to 65535");
	}
	return 1;
}

s32 characterToolDescriptorValues(const character_tool_form_t *form,
	const character_tool_template_t *tmpl, character_tool_values_t *out)
{
	s32 type;

	if (!out) {
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if (!characterToolFormValid(form, tmpl, NULL, 0)) {
		return 0;
	}
	if (tmpl->kind == CHARACTER_TOOL_KIND_CHARACTER) {
		return 1; /* a character's own descriptor has nothing the tool edits yet */
	}
	if ((form->ismale ? 1 : 0) != (tmpl->ismale ? 1 : 0)) {
		out->values[out->count].key = "ismale";
		out->values[out->count++].value = form->ismale ? "1" : "0";
	}
	type = characterToolTypeForForm(form, tmpl);
	if (type != tmpl->type) {
		out->values[out->count].key = "type";
		out->values[out->count++].value = characterToolTypeName(type);
		/* The neck follows the type: without a rig_class of its own the
		 * reader derives it (head_body_rig.h). */
		out->values[out->count].key = "rig_class";
		out->values[out->count++].value = NULL;
	}
	if (form->height != tmpl->height) {
		snprintf(out->height, sizeof(out->height), "%d", (int)form->height);
		out->values[out->count].key = "height";
		out->values[out->count++].value = out->height;
	}
	return 1;
}

s32 characterToolParseGender(const char *text, s32 *ismale)
{
	if (!text || !ismale) {
		return 0;
	}
	if (strcmp(text, "male") == 0) {
		*ismale = 1;
		return 1;
	}
	if (strcmp(text, "female") == 0) {
		*ismale = 0;
		return 1;
	}
	return 0;
}

s32 characterToolParseBuild(const char *text, s32 *build)
{
	if (!text || !build) {
		return 0;
	}
	if (strcmp(text, "human") == 0) {
		*build = CHARACTER_TOOL_BUILD_HUMAN;
	} else if (strcmp(text, "guard") == 0) {
		*build = CHARACTER_TOOL_BUILD_GUARD;
	} else if (strcmp(text, "maian") == 0) {
		*build = CHARACTER_TOOL_BUILD_MAIAN;
	} else if (strcmp(text, "template") == 0) {
		*build = CHARACTER_TOOL_BUILD_TEMPLATE;
	} else {
		return 0;
	}
	return 1;
}
