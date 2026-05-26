/**
 * pdgui_gameplay_graph_editor.h -- shared gameplay graph editor primitives.
 *
 * C++ only. Weapon graphs, mission graphs, gamemode graphs, and later gameplay
 * script editors share the same pin typing, compatibility, and color contract.
 */
#ifndef _IN_PDGUI_GAMEPLAY_GRAPH_EDITOR_H
#define _IN_PDGUI_GAMEPLAY_GRAPH_EDITOR_H

#include "imgui.h"

enum PdGameplayGraphPinType {
	PDGAMEPLAY_GRAPH_PIN_INVALID = 0,
	PDGAMEPLAY_GRAPH_PIN_EXEC,
	PDGAMEPLAY_GRAPH_PIN_NUMBER,
	PDGAMEPLAY_GRAPH_PIN_INTEGER,
	PDGAMEPLAY_GRAPH_PIN_BOOLEAN,
	PDGAMEPLAY_GRAPH_PIN_STRING,
	PDGAMEPLAY_GRAPH_PIN_CATALOG_ID,
	PDGAMEPLAY_GRAPH_PIN_CONTEXT_REF,
	PDGAMEPLAY_GRAPH_PIN_VECTOR3,
	PDGAMEPLAY_GRAPH_PIN_ENTITY,
};

enum PdGameplayGraphPinDirection {
	PDGAMEPLAY_GRAPH_PIN_INPUT = 0,
	PDGAMEPLAY_GRAPH_PIN_OUTPUT,
};

struct PdGameplayGraphPinStyle {
	PdGameplayGraphPinType type;
	const char *label;
	ImVec4 color;
};

struct PdGameplayGraphEditorAdapter {
	const char *graph_kind;
	const char *asset_extension;
	const char *descriptor_file;
	const char *primary_graph_file;
	const char *secondary_graph_file;
	bool supports_shared_context;
	bool supports_saved_layout;
};

const char *pdguiGameplayGraphPinTypeName(PdGameplayGraphPinType type);
ImVec4 pdguiGameplayGraphPinColor(PdGameplayGraphPinType type);
ImVec4 pdguiGameplayGraphLinkColor(PdGameplayGraphPinType from_type,
		PdGameplayGraphPinType to_type);
ImVec4 pdguiGameplayGraphCategoryColor(const char *node_kind);
bool pdguiGameplayGraphPinsCompatible(PdGameplayGraphPinType from_type,
		PdGameplayGraphPinDirection from_dir,
		PdGameplayGraphPinType to_type,
		PdGameplayGraphPinDirection to_dir);

#endif
