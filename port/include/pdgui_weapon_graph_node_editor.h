/**
 * pdgui_weapon_graph_node_editor.h -- in-game weapon graph canvas model.
 *
 * C++ only: this header is shared by the Modding Hub renderer and the
 * imgui-node-editor wrapper.
 */
#ifndef _IN_PDGUI_WEAPON_GRAPH_NODE_EDITOR_H
#define _IN_PDGUI_WEAPON_GRAPH_NODE_EDITOR_H

#include <stddef.h>

#include "pdgui_gameplay_graph_editor.h"
#include "weapon_graph_runtime.h"

#define PDGUI_WEAPON_GRAPH_MAX_NODES  48
#define PDGUI_WEAPON_GRAPH_MAX_EDGES  96
#define PDGUI_WEAPON_GRAPH_PARAM_LEN  512
#define PDGUI_WEAPON_GRAPH_SCOPE_LEN  16
#define PDGUI_WEAPON_GRAPH_FILTER_LEN 64
#define PDGUI_WEAPON_GRAPH_STATUS_LEN 192

struct PdWeaponGraphModuleDef {
	const char *label;
	const char *kind;
	const char *default_params;
};

struct PdWeaponGraphContextDef {
	const char *label;
	const char *name;
	const char *scope;
	const char *source;
	const char *type;
	const char *lifetime;
	bool default_enabled;
};

struct PdWeaponGraphNodeEdit {
	int editor_id;
	char id[WEAPON_GRAPH_IR_ID_LEN];
	char kind[WEAPON_GRAPH_IR_ID_LEN];
	char subgraph[PDGUI_WEAPON_GRAPH_SCOPE_LEN];
	char params[PDGUI_WEAPON_GRAPH_PARAM_LEN];
	float pos_x;
	float pos_y;
	bool pos_valid;
};

struct PdWeaponGraphEdgeEdit {
	int editor_id;
	int from;
	int to;
};

struct PdWeaponGraphEditModel {
	PdWeaponGraphNodeEdit nodes[PDGUI_WEAPON_GRAPH_MAX_NODES];
	PdWeaponGraphEdgeEdit edges[PDGUI_WEAPON_GRAPH_MAX_EDGES];
	int node_count;
	int edge_count;
	int module_pick;
	int scope_pick;
	int edge_from;
	int edge_to;
	int primary_export;
	int secondary_export;
	bool context_enabled[WEAPON_GRAPH_IR_MAX_CONTEXTS];
	int selected_node;
	int selected_edge;
	int next_editor_id;
	bool canvas_layout_seeded;
	char add_filter[PDGUI_WEAPON_GRAPH_FILTER_LEN];
};

enum PdWeaponGraphEditorAction {
	PD_WEAPON_GRAPH_EDITOR_ACTION_NONE = 0,
	PD_WEAPON_GRAPH_EDITOR_ACTION_ADD_MODULE,
	PD_WEAPON_GRAPH_EDITOR_ACTION_ADD_EDGE,
	PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_NODE,
	PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_EDGE,
	PD_WEAPON_GRAPH_EDITOR_ACTION_DUPLICATE_NODE,
	PD_WEAPON_GRAPH_EDITOR_ACTION_SET_PRIMARY,
	PD_WEAPON_GRAPH_EDITOR_ACTION_SET_SECONDARY,
	PD_WEAPON_GRAPH_EDITOR_ACTION_BREAK_PIN,
};

struct PdWeaponGraphEditorResult {
	PdWeaponGraphEditorAction action;
	int a;
	int b;
	int module_index;
	char scope[PDGUI_WEAPON_GRAPH_SCOPE_LEN];
	bool model_changed;
	bool status_ok;
	char status[PDGUI_WEAPON_GRAPH_STATUS_LEN];
};

struct PdWeaponGraphEditorDesc {
	PdWeaponGraphEditModel *model;
	const PdGameplayGraphEditorAdapter *adapter;
	const PdWeaponGraphModuleDef *modules;
	int module_count;
	const PdWeaponGraphContextDef *contexts;
	int context_count;
	const char *const *scopes;
	int scope_count;
	const char *scope_filter;
	const char *scope_label;
	float scale;
};

void pdguiWeaponGraphNodeEditorInit(void);
void pdguiWeaponGraphNodeEditorShutdown(void);
void pdguiWeaponGraphModelReset(PdWeaponGraphEditModel *model,
		const PdWeaponGraphContextDef *contexts,
		int context_count);
bool pdguiWeaponGraphModelLoadJson(PdWeaponGraphEditModel *model,
		const char *json,
		const PdWeaponGraphModuleDef *modules,
		int module_count,
		const PdWeaponGraphContextDef *contexts,
		int context_count,
		char *err,
		size_t err_cap);
bool pdguiWeaponGraphNodeEditorRender(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result);

#endif
