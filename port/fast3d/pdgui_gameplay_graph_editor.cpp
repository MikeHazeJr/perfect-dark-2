/**
 * pdgui_gameplay_graph_editor.cpp -- shared gameplay graph editor primitives.
 */

#include "pdgui_gameplay_graph_editor.h"

#include <string.h>

static const PdGameplayGraphPinStyle s_PinStyles[] = {
	{ PDGAMEPLAY_GRAPH_PIN_EXEC,       "exec",       ImVec4(0.96f, 0.96f, 0.96f, 1.0f) },
	{ PDGAMEPLAY_GRAPH_PIN_NUMBER,     "number",     ImVec4(0.50f, 0.92f, 0.56f, 1.0f) },
	{ PDGAMEPLAY_GRAPH_PIN_INTEGER,    "integer",    ImVec4(0.38f, 0.86f, 0.92f, 1.0f) },
	{ PDGAMEPLAY_GRAPH_PIN_BOOLEAN,    "boolean",    ImVec4(0.96f, 0.36f, 0.36f, 1.0f) },
	{ PDGAMEPLAY_GRAPH_PIN_STRING,     "string",     ImVec4(0.92f, 0.78f, 0.30f, 1.0f) },
	{ PDGAMEPLAY_GRAPH_PIN_CATALOG_ID, "catalog_id", ImVec4(0.98f, 0.70f, 0.24f, 1.0f) },
	{ PDGAMEPLAY_GRAPH_PIN_CONTEXT_REF,"context",    ImVec4(0.92f, 0.74f, 0.28f, 1.0f) },
	{ PDGAMEPLAY_GRAPH_PIN_VECTOR3,    "vector3",    ImVec4(0.70f, 0.52f, 0.96f, 1.0f) },
	{ PDGAMEPLAY_GRAPH_PIN_ENTITY,     "entity",     ImVec4(0.42f, 0.64f, 0.96f, 1.0f) },
};

static const PdGameplayGraphPinStyle *findPinStyle(PdGameplayGraphPinType type)
{
	for (size_t i = 0; i < sizeof(s_PinStyles) / sizeof(s_PinStyles[0]); i++) {
		if (s_PinStyles[i].type == type) return &s_PinStyles[i];
	}
	return NULL;
}

static const char *categoryForKind(const char *kind)
{
	static char category[32];
	const char *dot = kind ? strchr(kind, '.') : NULL;
	if (!kind || !kind[0]) return "unknown";
	if (!dot) return kind;
	size_t len = (size_t)(dot - kind);
	if (len >= sizeof(category)) len = sizeof(category) - 1;
	memcpy(category, kind, len);
	category[len] = '\0';
	return category;
}

const char *pdguiGameplayGraphPinTypeName(PdGameplayGraphPinType type)
{
	const PdGameplayGraphPinStyle *style = findPinStyle(type);
	return style ? style->label : "invalid";
}

ImVec4 pdguiGameplayGraphPinColor(PdGameplayGraphPinType type)
{
	const PdGameplayGraphPinStyle *style = findPinStyle(type);
	return style ? style->color : ImVec4(0.65f, 0.80f, 0.95f, 1.0f);
}

ImVec4 pdguiGameplayGraphLinkColor(PdGameplayGraphPinType from_type,
		PdGameplayGraphPinType to_type)
{
	if (from_type == to_type) return pdguiGameplayGraphPinColor(from_type);
	if ((from_type == PDGAMEPLAY_GRAPH_PIN_CATALOG_ID &&
				to_type == PDGAMEPLAY_GRAPH_PIN_STRING) ||
			(from_type == PDGAMEPLAY_GRAPH_PIN_STRING &&
				to_type == PDGAMEPLAY_GRAPH_PIN_CATALOG_ID)) {
		return pdguiGameplayGraphPinColor(PDGAMEPLAY_GRAPH_PIN_CATALOG_ID);
	}
	return ImVec4(1.0f, 0.28f, 0.22f, 1.0f);
}

ImVec4 pdguiGameplayGraphCategoryColor(const char *node_kind)
{
	const char *cat = categoryForKind(node_kind);
	if (strcmp(cat, "event") == 0) return ImVec4(0.52f, 0.72f, 0.98f, 1.0f);
	if (strcmp(cat, "gate") == 0) return ImVec4(0.94f, 0.72f, 0.30f, 1.0f);
	if (strcmp(cat, "ammo") == 0) return ImVec4(0.46f, 0.84f, 0.62f, 1.0f);
	if (strcmp(cat, "fire") == 0) return ImVec4(0.98f, 0.44f, 0.32f, 1.0f);
	if (strcmp(cat, "spawn") == 0) return ImVec4(0.76f, 0.52f, 0.96f, 1.0f);
	if (strcmp(cat, "projectile") == 0) return ImVec4(0.42f, 0.80f, 0.88f, 1.0f);
	if (strcmp(cat, "entity") == 0) return ImVec4(0.62f, 0.78f, 0.42f, 1.0f);
	if (strcmp(cat, "presentation") == 0) return ImVec4(0.90f, 0.58f, 0.86f, 1.0f);
	if (strcmp(cat, "device") == 0) return ImVec4(0.50f, 0.68f, 0.95f, 1.0f);
	if (strcmp(cat, "special") == 0) return ImVec4(0.96f, 0.62f, 0.38f, 1.0f);
	if (strcmp(cat, "mission") == 0) return ImVec4(0.56f, 0.86f, 0.72f, 1.0f);
	if (strcmp(cat, "gamemode") == 0) return ImVec4(0.86f, 0.66f, 0.42f, 1.0f);
	return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
}

bool pdguiGameplayGraphPinsCompatible(PdGameplayGraphPinType from_type,
		PdGameplayGraphPinDirection from_dir,
		PdGameplayGraphPinType to_type,
		PdGameplayGraphPinDirection to_dir)
{
	if (from_dir != PDGAMEPLAY_GRAPH_PIN_OUTPUT ||
			to_dir != PDGAMEPLAY_GRAPH_PIN_INPUT) {
		return false;
	}
	if (from_type == PDGAMEPLAY_GRAPH_PIN_INVALID ||
			to_type == PDGAMEPLAY_GRAPH_PIN_INVALID) {
		return false;
	}
	if (from_type == to_type) return true;
	return (from_type == PDGAMEPLAY_GRAPH_PIN_CATALOG_ID &&
				to_type == PDGAMEPLAY_GRAPH_PIN_STRING) ||
		(from_type == PDGAMEPLAY_GRAPH_PIN_STRING &&
				to_type == PDGAMEPLAY_GRAPH_PIN_CATALOG_ID);
}
