/**
 * pdgui_weapon_graph_node_editor.cpp -- Blueprint-style weapon graph canvas.
 */

#include "pdgui_weapon_graph_node_editor.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>

#include "imgui/imgui.h"
#include "imgui_node_editor.h"
#include "crude_json.h"

namespace ed = ax::NodeEditor;

static ed::EditorContext *s_WeaponGraphEditor = nullptr;
static const float kWeaponGraphNodeWidth = 228.0f;
static const float kWeaponGraphPinSocketSize = 18.0f;
static const float kWeaponGraphPinRowGap = 8.0f;
static int s_ContextNodeId = 0;
static int s_ContextLinkId = 0;
static int s_ContextPinId = 0;
static bool s_OpenAddMenuFromPin = false;
static int s_CopiedNode = -1;

static void editorSetStatus(PdWeaponGraphEditorResult *result,
		bool ok,
		const char *msg)
{
	if (!result || !msg) return;
	result->status_ok = ok;
	snprintf(result->status, sizeof(result->status), "%s", msg);
}

static const char *categoryForKind(const char *kind)
{
	static char category[32];
	const char *dot = kind ? strchr(kind, '.') : nullptr;
	if (!kind || !kind[0]) return "unknown";
	if (!dot) return kind;
	size_t len = (size_t)(dot - kind);
	if (len >= sizeof(category)) len = sizeof(category) - 1;
	memcpy(category, kind, len);
	category[len] = '\0';
	return category;
}

static ImVec4 categoryColor(const char *kind)
{
	const char *cat = categoryForKind(kind);
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
	return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
}

static ImVec4 pinColor(int pinKind)
{
	switch (pinKind) {
	case 1: return ImVec4(0.20f, 0.92f, 1.0f, 1.0f);
	case 2: return ImVec4(0.20f, 0.92f, 1.0f, 1.0f);
	case 3: return ImVec4(0.50f, 0.92f, 0.56f, 1.0f);
	case 4: return ImVec4(0.92f, 0.74f, 0.28f, 1.0f);
	default: return ImVec4(0.65f, 0.80f, 0.95f, 1.0f);
	}
}

static ImVec4 linkColorForPinKind(int pinKind)
{
	return pinColor(pinKind);
}

static int nextEditorId(PdWeaponGraphEditModel *model)
{
	if (!model) return 1;
	if (model->next_editor_id <= 0) model->next_editor_id = 1;
	return model->next_editor_id++;
}

static void ensureEditorIds(PdWeaponGraphEditModel *model)
{
	if (!model) return;
	if (model->selected_node >= model->node_count) model->selected_node = -1;
	if (model->selected_edge >= model->edge_count) model->selected_edge = -1;
	if (model->next_editor_id <= 0) model->next_editor_id = 1;
	for (int i = 0; i < model->node_count; i++) {
		if (model->nodes[i].editor_id <= 0) {
			model->nodes[i].editor_id = nextEditorId(model);
		}
	}
	for (int i = 0; i < model->edge_count; i++) {
		if (model->edges[i].editor_id <= 0) {
			model->edges[i].editor_id = nextEditorId(model);
		}
	}
}

static ed::NodeId nodeEditorId(const PdWeaponGraphNodeEdit &node)
{
	return ed::NodeId((uintptr_t)node.editor_id);
}

static ed::LinkId linkEditorId(const PdWeaponGraphEdgeEdit &edge)
{
	return ed::LinkId((uintptr_t)edge.editor_id);
}

static ed::PinId nodePinId(const PdWeaponGraphNodeEdit &node, int pinKind)
{
	return ed::PinId((uintptr_t)(node.editor_id * 16 + pinKind));
}

static int editorIdFromPin(ed::PinId pin)
{
	uintptr_t value = pin.Get();
	return (int)(value / 16);
}

static int pinKindFromPin(ed::PinId pin)
{
	uintptr_t value = pin.Get();
	return (int)(value % 16);
}

static int findNodeByEditorId(const PdWeaponGraphEditModel *model, int editorId)
{
	if (!model) return -1;
	for (int i = 0; i < model->node_count; i++) {
		if (model->nodes[i].editor_id == editorId) return i;
	}
	return -1;
}

static int findLinkByEditorId(const PdWeaponGraphEditModel *model, int editorId)
{
	if (!model) return -1;
	for (int i = 0; i < model->edge_count; i++) {
		if (model->edges[i].editor_id == editorId) return i;
	}
	return -1;
}

static bool edgeExists(const PdWeaponGraphEditModel *model, int from, int to)
{
	if (!model) return false;
	for (int i = 0; i < model->edge_count; i++) {
		if (model->edges[i].from == from && model->edges[i].to == to) return true;
	}
	return false;
}

static bool nodeVisibleForScope(const PdWeaponGraphEditorDesc *desc,
		const PdWeaponGraphEditModel *model,
		int index);

static bool edgeVisibleForScope(const PdWeaponGraphEditorDesc *desc,
		const PdWeaponGraphEditModel *model,
		const PdWeaponGraphEdgeEdit &edge)
{
	if (!model) return false;
	if (edge.from < 0 || edge.from >= model->node_count ||
			edge.to < 0 || edge.to >= model->node_count) {
		return false;
	}
	return nodeVisibleForScope(desc, model, edge.from) &&
		nodeVisibleForScope(desc, model, edge.to);
}

static int visibleEdgeCount(const PdWeaponGraphEditorDesc *desc,
		const PdWeaponGraphEditModel *model)
{
	if (!model) return 0;
	int count = 0;
	for (int i = 0; i < model->edge_count; i++) {
		if (edgeVisibleForScope(desc, model, model->edges[i])) count++;
	}
	return count;
}

static void outputAction(PdWeaponGraphEditorResult *result,
		PdWeaponGraphEditorAction action,
		int a,
		int b)
{
	if (!result) return;
	result->action = action;
	result->a = a;
	result->b = b;
}

static bool stringContainsNoCase(const char *haystack, const char *needle)
{
	if (!needle || !needle[0]) return true;
	if (!haystack) return false;
	size_t nlen = strlen(needle);
	for (const char *p = haystack; *p; p++) {
		size_t i = 0;
		while (i < nlen && p[i]) {
			char a = p[i];
			char b = needle[i];
			if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
			if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
			if (a != b) break;
			i++;
		}
		if (i == nlen) return true;
	}
	return false;
}

static const char *nodeLabel(const PdWeaponGraphEditModel *model, int index)
{
	if (!model || index < 0 || index >= model->node_count) return "(none)";
	return model->nodes[index].id[0] ? model->nodes[index].id
		: model->nodes[index].kind;
}

static int scopeIndexForName(const PdWeaponGraphEditorDesc *desc, const char *scope)
{
	if (!desc || !scope || !scope[0]) return -1;
	for (int i = 0; i < desc->scope_count; i++) {
		if (desc->scopes && desc->scopes[i] && strcmp(desc->scopes[i], scope) == 0) {
			return i;
		}
	}
	return -1;
}

static const char *currentAddScopeName(const PdWeaponGraphEditorDesc *desc,
		const PdWeaponGraphEditModel *model)
{
	if (desc && desc->scope_filter && desc->scope_filter[0]) {
		return desc->scope_filter;
	}
	if (desc && model && model->scope_pick >= 0 &&
			model->scope_pick < desc->scope_count &&
			desc->scopes && desc->scopes[model->scope_pick]) {
		return desc->scopes[model->scope_pick];
	}
	return "primary";
}

static bool nodeVisibleForScope(const PdWeaponGraphEditorDesc *desc,
		const PdWeaponGraphEditModel *model,
		int index)
{
	if (!desc || !model || index < 0 || index >= model->node_count) return false;
	if (!desc->scope_filter || !desc->scope_filter[0]) return true;
	const char *scope = model->nodes[index].subgraph[0]
		? model->nodes[index].subgraph : "primary";
	return strcmp(scope, desc->scope_filter) == 0 || strcmp(scope, "shared") == 0;
}

static bool paramsHasContext(const char *params, const char *name)
{
	if (!params || !name || !name[0]) return false;
	crude_json::value root = crude_json::value::parse(params);
	if (!root.is_object() || !root.contains("context_refs")) return false;
	const crude_json::value &refs = root["context_refs"];
	if (!refs.is_array()) return false;
	for (const auto &entry : refs.get<crude_json::array>()) {
		if (entry.is_string() && entry.get<crude_json::string>() == name) {
			return true;
		}
	}
	return false;
}

static bool paramsSetContext(char *params,
		size_t params_cap,
		const char *name,
		bool enabled)
{
	if (!params || !name || !name[0]) return false;
	crude_json::value root = crude_json::value::parse(params[0] ? params : "{}");
	if (!root.is_object()) return false;
	if (!root.contains("context_refs") || !root["context_refs"].is_array()) {
		root["context_refs"] = crude_json::array();
	}
	crude_json::array &refs = root["context_refs"].get<crude_json::array>();
	refs.erase(std::remove_if(refs.begin(), refs.end(),
		[name](const crude_json::value &entry) {
			return entry.is_string() && entry.get<crude_json::string>() == name;
		}), refs.end());
	if (enabled) refs.push_back(crude_json::value(name));
	std::string dumped = root.dump(0);
	if (dumped.size() >= params_cap) return false;
	snprintf(params, params_cap, "%s", dumped.c_str());
	return true;
}

static void seedMissingNodeLayout(PdWeaponGraphEditModel *model)
{
	if (!model) return;
	int shared = 0;
	int primary = 0;
	int secondary = 0;
	for (int i = 0; i < model->node_count; i++) {
		PdWeaponGraphNodeEdit &node = model->nodes[i];
		if (node.pos_valid) continue;
		const char *scope = node.subgraph[0] ? node.subgraph : "primary";
		int *ordinal = &primary;
		float baseY = 176.0f;
		if (strcmp(scope, "shared") == 0) {
			ordinal = &shared;
			baseY = 48.0f;
		} else if (strcmp(scope, "secondary") == 0) {
			ordinal = &secondary;
			baseY = 304.0f;
		}
		int lane = *ordinal / 2;
		(*ordinal)++;
		bool leftSide = strncmp(node.kind, "event.", 6) == 0 ||
			strncmp(node.kind, "gate.", 5) == 0;
		node.pos_x = leftSide ? 48.0f : 300.0f;
		node.pos_y = baseY + (float)lane * 128.0f;
		node.pos_valid = true;
	}
}

static bool paramsRewrite(char *params, size_t params_cap, const crude_json::value &root)
{
	if (!params || params_cap == 0) return false;
	std::string dumped = root.dump(0);
	if (dumped.size() >= params_cap) return false;
	snprintf(params, params_cap, "%s", dumped.c_str());
	return true;
}

static void renderPinSocket(const char *id, const char *tip, const ImVec4 &color)
{
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	float size = kWeaponGraphPinSocketSize;
	float r = 6.0f;
	ImGui::InvisibleButton(id, ImVec2(size, size));
	ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
	ImU32 fill = ImGui::ColorConvertFloat4ToU32(color);
	ImU32 border = ImGui::ColorConvertFloat4ToU32(ImVec4(0.06f, 0.07f, 0.08f, 1.0f));
	draw->AddCircleFilled(c, r, fill);
	draw->AddCircle(c, r + 2.0f, border, 16, 2.0f);
	if (ImGui::IsItemHovered() && tip && tip[0]) {
		ImGui::SetTooltip("%s", tip);
	}
}

static void renderDecorPinDot(const ImVec4 &color)
{
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	float r = 4.0f;
	draw->AddCircleFilled(ImVec2(p.x + r, p.y + r), r,
		ImGui::ColorConvertFloat4ToU32(color));
	ImGui::Dummy(ImVec2(r * 2.0f, r * 2.0f));
}

static void renderNode(PdWeaponGraphEditModel *model, int index)
{
	PdWeaponGraphNodeEdit &node = model->nodes[index];
	ImVec4 color = categoryColor(node.kind);
	ed::PushStyleColor(ed::StyleColor_NodeBg,
		ImVec4(color.x * 0.16f, color.y * 0.16f, color.z * 0.16f, 0.94f));
	ed::PushStyleColor(ed::StyleColor_NodeBorder, color);
	ed::BeginNode(nodeEditorId(node));
	ImGui::PushID(node.editor_id);
	ImGui::TextUnformatted(node.id[0] ? node.id : node.kind);
	ImGui::SameLine();
	ImGui::TextDisabled("[%s]", node.subgraph[0] ? node.subgraph : "primary");
	ImGui::TextColored(color, "%s", node.kind);
	float rowStartX = ImGui::GetCursorPosX();
	float inputLabelX = rowStartX + kWeaponGraphPinSocketSize + kWeaponGraphPinRowGap;
	float outputPinX = rowStartX + kWeaponGraphNodeWidth - kWeaponGraphPinSocketSize;
	float outputLabelX = outputPinX - 38.0f;
	ed::BeginPin(nodePinId(node, 1), ed::PinKind::Input);
	ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
	renderPinSocket("##exec_in_pin", "Exec input: drag from another node's output to connect",
		pinColor(1));
	ed::EndPin();
	ImGui::SameLine();
	ImGui::SetCursorPosX(inputLabelX);
	ImGui::TextColored(pinColor(1), "exec");
	ImGui::SameLine();
	ImGui::SetCursorPosX(outputLabelX);
	ImGui::TextColored(pinColor(2), "exec");
	ImGui::SameLine();
	ImGui::SetCursorPosX(outputPinX);
	ed::BeginPin(nodePinId(node, 2), ed::PinKind::Output);
	ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
	renderPinSocket("##exec_out_pin", "Exec output: drag to another node's input to connect",
		pinColor(2));
	ed::EndPin();
	if (strstr(node.params, "context_refs")) {
		ImGui::TextDisabled("context refs");
		ImGui::SameLine();
		renderDecorPinDot(pinColor(4));
	}
	ImGui::PopID();
	ed::EndNode();
	ed::PopStyleColor(2);
}

static void renderPalette(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result,
		float width,
		float height)
{
	PdWeaponGraphEditModel *model = desc->model;
	ImGui::BeginChild("##weapon_graph_node_palette", ImVec2(width, height), true);
	ImGui::TextUnformatted("Palette");
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputText("##node_filter", model->add_filter, sizeof(model->add_filter));
	ImGui::Separator();
	for (int i = 0; i < desc->module_count; i++) {
		const PdWeaponGraphModuleDef &module = desc->modules[i];
		if (!stringContainsNoCase(module.label, model->add_filter) &&
				!stringContainsNoCase(module.kind, model->add_filter)) {
			continue;
		}
		ImVec4 color = categoryColor(module.kind);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		bool selected = model->module_pick == i;
		if (ImGui::Selectable(module.label, selected)) {
			model->module_pick = i;
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", module.kind);
		}
	}
	ImGui::Separator();
	ImGui::SetNextItemWidth(-1.0f);
	const char *scopePreview = currentAddScopeName(desc, model);
	int scopeIndex = scopeIndexForName(desc, scopePreview);
	if (desc->scope_filter && desc->scope_filter[0]) {
		if (scopeIndex >= 0) model->scope_pick = scopeIndex;
		ImGui::Text("Mode: %s", scopePreview);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("This tab adds nodes to the %s graph.", scopePreview);
		}
	} else if (ImGui::BeginCombo("Mode", scopePreview)) {
		for (int i = 0; i < desc->scope_count; i++) {
			bool selected = model->scope_pick == i;
			if (ImGui::Selectable(desc->scopes[i], selected)) {
				model->scope_pick = i;
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("Add Node", ImVec2(-1.0f, 0.0f))) {
		result->module_index = model->module_pick;
		snprintf(result->scope, sizeof(result->scope), "%s", scopePreview);
		outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_ADD_MODULE,
			model->module_pick, scopeIndex);
		editorSetStatus(result, true, "Add node requested");
	}
	ImGui::EndChild();
}

static void renderSharedContext(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result)
{
	PdWeaponGraphEditModel *model = desc->model;
	if (ImGui::CollapsingHeader("Shared Context", ImGuiTreeNodeFlags_DefaultOpen)) {
		for (int i = 0; i < desc->context_count; i++) {
			bool enabled = model->context_enabled[i];
			ImGui::PushID(7000 + i);
			if (ImGui::Checkbox(desc->contexts[i].label, &enabled)) {
				model->context_enabled[i] = enabled;
				result->model_changed = true;
				editorSetStatus(result, true, "Shared context updated");
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s / %s", desc->contexts[i].scope,
					desc->contexts[i].type);
			}
			ImGui::PopID();
		}
	}
}

static void renderAttachedLinks(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result,
		int selected)
{
	PdWeaponGraphEditModel *model = desc->model;
	int outgoing = 0;
	int incoming = 0;
	ImGui::Separator();
	ImGui::TextUnformatted("Current Links");
	for (int i = 0; i < model->edge_count; i++) {
		const PdWeaponGraphEdgeEdit &edge = model->edges[i];
		if (!edgeVisibleForScope(desc, model, edge)) continue;
		if (edge.from != selected && edge.to != selected) continue;
		bool isOutgoing = edge.from == selected;
		if (isOutgoing) outgoing++; else incoming++;
		ImGui::PushID(72000 + i);
		ImGui::TextDisabled("%s %s",
			isOutgoing ? "Out to" : "In from",
			nodeLabel(model, isOutgoing ? edge.to : edge.from));
		ImGui::SameLine();
		if (ImGui::SmallButton("Remove Link")) {
			outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_EDGE,
				i, -1);
			editorSetStatus(result, true, "Graph link removed");
		}
		ImGui::PopID();
	}
	if (outgoing == 0 && incoming == 0) {
		ImGui::TextDisabled("This node has no links in the visible graph.");
	}
}

static int firstVisibleConnectTarget(const PdWeaponGraphEditorDesc *desc, int selected)
{
	PdWeaponGraphEditModel *model = desc->model;
	for (int i = 0; i < model->node_count; i++) {
		if (i != selected && nodeVisibleForScope(desc, model, i)) return i;
	}
	return -1;
}

static void renderInspectorConnectControls(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result,
		int selected)
{
	PdWeaponGraphEditModel *model = desc->model;
	int target = model->edge_to;
	if (target < 0 || target >= model->node_count || target == selected ||
			!nodeVisibleForScope(desc, model, target)) {
		target = firstVisibleConnectTarget(desc, selected);
	}
	model->edge_from = selected;
	model->edge_to = target;
	ImGui::Separator();
	ImGui::TextUnformatted("Outgoing Link");
	if (target < 0) {
		ImGui::TextDisabled("No compatible node in this graph tab.");
		return;
	}
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("Connect To", nodeLabel(model, target))) {
		for (int i = 0; i < model->node_count; i++) {
			if (i == selected || !nodeVisibleForScope(desc, model, i)) continue;
			bool isSelected = i == target;
			if (ImGui::Selectable(nodeLabel(model, i), isSelected)) {
				model->edge_to = i;
				target = i;
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("Connect Selected", ImVec2(-1.0f, 0.0f))) {
		if (target < 0 || target >= model->node_count) {
			editorSetStatus(result, false, "Choose a node to connect to");
		} else if (edgeExists(model, selected, target)) {
			editorSetStatus(result, false, "That graph link already exists");
		} else {
			outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_ADD_EDGE,
				selected, target);
			editorSetStatus(result, true, "Graph link added");
		}
	}
	renderAttachedLinks(desc, result, selected);
}

static void renderNodeParamControls(PdWeaponGraphNodeEdit &node,
		PdWeaponGraphEditorResult *result)
{
	crude_json::value root = crude_json::value::parse(node.params[0] ? node.params : "{}");
	if (!root.is_object()) {
		ImGui::TextDisabled("Parameter JSON is invalid; use Advanced JSON.");
		return;
	}
	crude_json::object &obj = root.get<crude_json::object>();
	bool changed = false;
	bool anyEditable = false;
	ImGui::TextUnformatted("Node Parameters");
	for (auto &entry : obj) {
		if (entry.first == "context_refs") continue;
		crude_json::value &value = entry.second;
		ImGui::PushID(entry.first.c_str());
		if (value.is_boolean()) {
			bool edited = value.get<crude_json::boolean>();
			if (ImGui::Checkbox(entry.first.c_str(), &edited)) {
				value = edited;
				changed = true;
			}
			anyEditable = true;
		} else if (value.is_number()) {
			float edited = (float)value.get<crude_json::number>();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputFloat(entry.first.c_str(), &edited, 0.0f, 0.0f, "%.3f")) {
				value = (crude_json::number)edited;
				changed = true;
			}
			anyEditable = true;
		} else if (value.is_string()) {
			char buf[192];
			snprintf(buf, sizeof(buf), "%s", value.get<crude_json::string>().c_str());
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText(entry.first.c_str(), buf, sizeof(buf))) {
				value = crude_json::string(buf);
				changed = true;
			}
			anyEditable = true;
		} else {
			ImGui::TextDisabled("%s: edit in Advanced JSON", entry.first.c_str());
		}
		ImGui::PopID();
	}
	if (!anyEditable) {
		ImGui::TextDisabled("No simple parameters on this node.");
	}
	if (changed) {
		if (paramsRewrite(node.params, sizeof(node.params), root)) {
			result->model_changed = true;
			editorSetStatus(result, true, "Node parameter updated");
		} else {
			editorSetStatus(result, false, "Node params are too large after edit");
		}
	}
}

static void renderInspector(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result,
		float width,
		float height)
{
	PdWeaponGraphEditModel *model = desc->model;
	ImGui::BeginChild("##weapon_graph_node_inspector", ImVec2(width, height), true);
	ImGui::TextUnformatted("Inspector");
	ImGui::Separator();
	renderSharedContext(desc, result);
	ImGui::Separator();
	int selected = model->selected_node;
	if (selected < 0 || selected >= model->node_count ||
			!nodeVisibleForScope(desc, model, selected)) {
		ImGui::TextDisabled("Select a node to edit params.");
		ImGui::EndChild();
		return;
	}
	PdWeaponGraphNodeEdit &node = model->nodes[selected];
	ImGui::Text("%s", node.kind);
	if (ImGui::InputText("Node ID", node.id, sizeof(node.id))) {
		result->model_changed = true;
	}
	const char *modePreview = node.subgraph[0] ? node.subgraph : "primary";
	if (ImGui::BeginCombo("Mode", modePreview)) {
		for (int i = 0; i < desc->scope_count; i++) {
			bool selectedScope = strcmp(modePreview, desc->scopes[i]) == 0;
			if (ImGui::Selectable(desc->scopes[i], selectedScope)) {
				snprintf(node.subgraph, sizeof(node.subgraph), "%s", desc->scopes[i]);
				result->model_changed = true;
			}
		}
		ImGui::EndCombo();
	}
	renderNodeParamControls(node, result);
	ImGui::Separator();
	ImGui::TextUnformatted("Advanced JSON");
	if (ImGui::InputTextMultiline("Params", node.params, sizeof(node.params),
			ImVec2(-1.0f, 112.0f * desc->scale),
			ImGuiInputTextFlags_AllowTabInput)) {
		result->model_changed = true;
	}
	if (ImGui::CollapsingHeader("Node Context Refs")) {
		for (int i = 0; i < desc->context_count; i++) {
			const char *name = desc->contexts[i].name;
			bool checked = paramsHasContext(node.params, name);
			ImGui::PushID(9000 + i);
			if (ImGui::Checkbox(desc->contexts[i].label, &checked)) {
				if (paramsSetContext(node.params, sizeof(node.params), name, checked)) {
					result->model_changed = true;
					editorSetStatus(result, true, "Node context refs updated");
				} else {
					editorSetStatus(result, false, "Could not rewrite node params JSON");
				}
			}
			ImGui::PopID();
		}
	}
	if (ImGui::Button("Set Primary", ImVec2(104.0f * desc->scale, 0.0f))) {
		outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_SET_PRIMARY, selected, -1);
	}
	ImGui::SameLine();
	if (ImGui::Button("Set Secondary", ImVec2(116.0f * desc->scale, 0.0f))) {
		outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_SET_SECONDARY, selected, -1);
	}
	if (ImGui::Button("Duplicate Node", ImVec2(130.0f * desc->scale, 0.0f))) {
		outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_DUPLICATE_NODE, selected, -1);
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete Node", ImVec2(108.0f * desc->scale, 0.0f))) {
		outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_NODE, selected, -1);
	}
	renderInspectorConnectControls(desc, result, selected);
	ImGui::Separator();
	ImGui::TextDisabled("Primary: %s", nodeLabel(model, model->primary_export));
	ImGui::TextDisabled("Secondary: %s", nodeLabel(model, model->secondary_export));
	ImGui::EndChild();
}

static void renderCanvasContextMenus(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result)
{
	PdWeaponGraphEditModel *model = desc->model;
	ed::NodeId contextNode;
	ed::LinkId contextLink;
	ed::PinId contextPin;
	if (ed::ShowNodeContextMenu(&contextNode)) {
		s_ContextNodeId = (int)contextNode.Get();
		ImGui::OpenPopup("Weapon Graph Node Context Menu");
	}
	if (ed::ShowLinkContextMenu(&contextLink)) {
		s_ContextLinkId = (int)contextLink.Get();
		ImGui::OpenPopup("Weapon Graph Link Context Menu");
	}
	if (ed::ShowPinContextMenu(&contextPin)) {
		s_ContextPinId = (int)contextPin.Get();
		ImGui::OpenPopup("Weapon Graph Pin Context Menu");
	}
	if (ed::ShowBackgroundContextMenu()) {
		ImGui::OpenPopup("Weapon Graph Canvas Context Menu");
	}

	ed::Suspend();
	if (s_OpenAddMenuFromPin) {
		ImGui::OpenPopup("Weapon Graph Canvas Context Menu");
		s_OpenAddMenuFromPin = false;
	}
	if (ImGui::BeginPopup("Weapon Graph Node Context Menu")) {
		int nodeIndex = findNodeByEditorId(model, s_ContextNodeId);
		if (nodeIndex >= 0) {
			if (ImGui::MenuItem("Set Primary Export")) {
				outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_SET_PRIMARY,
					nodeIndex, -1);
			}
			if (ImGui::MenuItem("Set Secondary Export")) {
				outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_SET_SECONDARY,
					nodeIndex, -1);
			}
			if (ImGui::MenuItem("Duplicate Node")) {
				outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_DUPLICATE_NODE,
					nodeIndex, -1);
			}
			if (ImGui::MenuItem("Delete Node")) {
				outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_NODE,
					nodeIndex, -1);
			}
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("Weapon Graph Link Context Menu")) {
		int linkIndex = findLinkByEditorId(model, s_ContextLinkId);
		if (linkIndex >= 0 && ImGui::MenuItem("Delete Link")) {
			outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_EDGE,
				linkIndex, -1);
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("Weapon Graph Pin Context Menu")) {
		ed::PinId pin((uintptr_t)s_ContextPinId);
		int nodeIndex = findNodeByEditorId(model, editorIdFromPin(pin));
		if (nodeIndex >= 0 && ImGui::MenuItem("Break Pin Links")) {
			outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_BREAK_PIN,
				nodeIndex, pinKindFromPin(pin));
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("Weapon Graph Canvas Context Menu")) {
		ImGui::TextUnformatted("Add Node");
		ImGui::SetNextItemWidth(220.0f * desc->scale);
		ImGui::InputText("Search", model->add_filter, sizeof(model->add_filter));
		ImGui::Separator();
		for (int i = 0; i < desc->module_count; i++) {
			const PdWeaponGraphModuleDef &module = desc->modules[i];
			if (!stringContainsNoCase(module.label, model->add_filter) &&
					!stringContainsNoCase(module.kind, model->add_filter)) {
				continue;
			}
			if (ImGui::MenuItem(module.label)) {
				result->module_index = i;
				const char *scopeName = currentAddScopeName(desc, model);
				int scopeIndex = scopeIndexForName(desc, scopeName);
				snprintf(result->scope, sizeof(result->scope), "%s", scopeName);
				outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_ADD_MODULE,
					i, scopeIndex);
				editorSetStatus(result, true, "Add node requested");
			}
		}
		ImGui::EndPopup();
	}
	ed::Resume();
}

static void renderCanvas(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result,
		float width,
		float height)
{
	PdWeaponGraphEditModel *model = desc->model;
	ImGui::BeginChild("##weapon_graph_canvas_host", ImVec2(width, height), true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ed::SetCurrentEditor(s_WeaponGraphEditor);
	ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0.03f, 0.035f, 0.04f, 1.0f));
	ed::PushStyleColor(ed::StyleColor_Grid, ImVec4(0.32f, 0.46f, 0.48f, 0.18f));
	ed::Begin("Weapon Behavior Graph Canvas", ImVec2(0, 0));
	for (int i = 0; i < model->node_count; i++) {
		if (!nodeVisibleForScope(desc, model, i)) continue;
		renderNode(model, i);
	}
	for (int i = 0; i < model->edge_count; i++) {
		const PdWeaponGraphEdgeEdit &edge = model->edges[i];
		if (!edgeVisibleForScope(desc, model, edge)) continue;
		ed::Link(linkEditorId(edge),
			nodePinId(model->nodes[edge.from], 2),
			nodePinId(model->nodes[edge.to], 1),
			linkColorForPinKind(2), 4.0f);
	}
	if (!model->canvas_layout_seeded) {
		for (int i = 0; i < model->node_count; i++) {
			if (model->nodes[i].pos_valid) {
				ed::SetNodePosition(nodeEditorId(model->nodes[i]),
					ImVec2(model->nodes[i].pos_x, model->nodes[i].pos_y));
			}
		}
		model->canvas_layout_seeded = true;
	}
	ed::BeginCreate(ImVec4(0.70f, 0.85f, 1.0f, 1.0f), 2.0f);
	ed::PinId startPin, endPin;
	if (ed::QueryNewLink(&startPin, &endPin)) {
		if (startPin && endPin) {
			int startKind = pinKindFromPin(startPin);
			int endKind = pinKindFromPin(endPin);
			int fromNode = findNodeByEditorId(model, editorIdFromPin(startPin));
			int toNode = findNodeByEditorId(model, editorIdFromPin(endPin));
			if (startKind == 1 && endKind == 2) {
				std::swap(startKind, endKind);
				std::swap(fromNode, toNode);
			}
			if (fromNode < 0 || toNode < 0 || startKind != 2 || endKind != 1) {
				ed::RejectNewItem(ImVec4(1.0f, 0.28f, 0.22f, 1.0f), 2.0f);
				editorSetStatus(result, false, "Links must connect exec output to exec input");
			} else if (!nodeVisibleForScope(desc, model, fromNode) ||
					!nodeVisibleForScope(desc, model, toNode)) {
				ed::RejectNewItem(ImVec4(1.0f, 0.28f, 0.22f, 1.0f), 2.0f);
				editorSetStatus(result, false, "Links must stay inside the visible graph tab");
			} else if (fromNode == toNode) {
				ed::RejectNewItem(ImVec4(1.0f, 0.28f, 0.22f, 1.0f), 2.0f);
				editorSetStatus(result, false, "A node cannot link to itself");
			} else if (edgeExists(model, fromNode, toNode)) {
				ed::RejectNewItem(ImVec4(1.0f, 0.28f, 0.22f, 1.0f), 2.0f);
				editorSetStatus(result, false, "That graph link already exists");
			} else if (ed::AcceptNewItem(ImVec4(0.46f, 0.92f, 0.50f, 1.0f), 2.0f)) {
				outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_ADD_EDGE,
					fromNode, toNode);
				editorSetStatus(result, true, "Graph link added");
			}
		}
	}
	ed::PinId newNodePin;
	if (ed::QueryNewNode(&newNodePin)) {
		if (newNodePin) {
			s_ContextPinId = (int)newNodePin.Get();
			s_OpenAddMenuFromPin = true;
		}
		ed::RejectNewItem(ImVec4(0.92f, 0.72f, 0.24f, 1.0f), 2.0f);
		editorSetStatus(result, true, "Choose a compatible node from the add-node menu");
	}
	ed::EndCreate();

	ed::BeginDelete();
	ed::LinkId deletedLink;
	ed::PinId deletedStart, deletedEnd;
	if (ed::QueryDeletedLink(&deletedLink, &deletedStart, &deletedEnd)) {
		int linkIndex = findLinkByEditorId(model, (int)deletedLink.Get());
		if (linkIndex >= 0 && ed::AcceptDeletedItem()) {
			outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_EDGE,
				linkIndex, -1);
			editorSetStatus(result, true, "Graph link removed");
		}
	}
	ed::NodeId deletedNode;
	if (ed::QueryDeletedNode(&deletedNode)) {
		int nodeIndex = findNodeByEditorId(model, (int)deletedNode.Get());
		if (nodeIndex >= 0 && ed::AcceptDeletedItem()) {
			outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_REMOVE_NODE,
				nodeIndex, -1);
			editorSetStatus(result, true, "Graph node removed");
		}
	}
	ed::EndDelete();

	renderCanvasContextMenus(desc, result);
	ed::End();
	ed::PopStyleColor(2);

	ed::NodeId selectedNodes[1];
	if (ed::GetSelectedNodes(selectedNodes, 1) > 0) {
		model->selected_node = findNodeByEditorId(model, (int)selectedNodes[0].Get());
		if (!nodeVisibleForScope(desc, model, model->selected_node)) {
			model->selected_node = -1;
		}
	}
	ed::LinkId selectedLinks[1];
	if (ed::GetSelectedLinks(selectedLinks, 1) > 0) {
		model->selected_edge = findLinkByEditorId(model, (int)selectedLinks[0].Get());
	}
	ed::PinId hoveredPin = ed::GetHoveredPin();
	if (hoveredPin && ImGui::GetIO().KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		int nodeIndex = findNodeByEditorId(model, editorIdFromPin(hoveredPin));
		if (nodeIndex >= 0) {
			outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_BREAK_PIN,
				nodeIndex, pinKindFromPin(hoveredPin));
			editorSetStatus(result, true, "Pin links removed");
		}
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
		ed::NavigateToContent(0.20f);
		editorSetStatus(result, true, "Framed graph nodes");
	}
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
		s_CopiedNode = model->selected_node;
		if (s_CopiedNode >= 0) editorSetStatus(result, true, "Copied graph node");
	}
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
		if (s_CopiedNode >= 0 && s_CopiedNode < model->node_count) {
			outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_DUPLICATE_NODE,
				s_CopiedNode, -1);
			editorSetStatus(result, true, "Duplicated graph node");
		}
	}
	for (int i = 0; i < model->node_count; i++) {
		if (!nodeVisibleForScope(desc, model, i)) continue;
		ImVec2 pos = ed::GetNodePosition(nodeEditorId(model->nodes[i]));
		if (!model->nodes[i].pos_valid ||
				model->nodes[i].pos_x != pos.x ||
				model->nodes[i].pos_y != pos.y) {
			model->nodes[i].pos_x = pos.x;
			model->nodes[i].pos_y = pos.y;
			model->nodes[i].pos_valid = true;
			result->model_changed = true;
		}
	}
	ImGui::EndChild();
}

void pdguiWeaponGraphNodeEditorInit(void)
{
	if (s_WeaponGraphEditor) return;
	ed::Config config;
	config.SettingsFile = nullptr;
	s_WeaponGraphEditor = ed::CreateEditor(&config);
}

void pdguiWeaponGraphNodeEditorShutdown(void)
{
	if (!s_WeaponGraphEditor) return;
	ed::DestroyEditor(s_WeaponGraphEditor);
	s_WeaponGraphEditor = nullptr;
}

void pdguiWeaponGraphModelReset(PdWeaponGraphEditModel *model,
		const PdWeaponGraphContextDef *contexts,
		int context_count)
{
	if (!model) return;
	memset(model, 0, sizeof(*model));
	model->primary_export = -1;
	model->secondary_export = -1;
	model->selected_node = -1;
	model->selected_edge = -1;
	model->next_editor_id = 1;
	for (int i = 0; i < context_count && i < WEAPON_GRAPH_IR_MAX_CONTEXTS; i++) {
		model->context_enabled[i] = contexts ? contexts[i].default_enabled : false;
	}
}

static bool copyString(char *dst, size_t cap, const crude_json::value &src)
{
	if (!dst || cap == 0 || !src.is_string()) return false;
	snprintf(dst, cap, "%s", src.get<crude_json::string>().c_str());
	return true;
}

bool pdguiWeaponGraphModelLoadJson(PdWeaponGraphEditModel *model,
		const char *json,
		const PdWeaponGraphModuleDef *modules,
		int module_count,
		const PdWeaponGraphContextDef *contexts,
		int context_count,
		char *err,
		size_t err_cap)
{
	(void)modules;
	(void)module_count;
	if (err && err_cap) err[0] = '\0';
	if (!model || !json || !json[0]) return false;
	crude_json::value root = crude_json::value::parse(json);
	if (!root.is_object()) {
		if (err && err_cap) snprintf(err, err_cap, "graph JSON could not be parsed");
		return false;
	}
	pdguiWeaponGraphModelReset(model, contexts, context_count);
	for (int i = 0; i < WEAPON_GRAPH_IR_MAX_CONTEXTS; i++) {
		model->context_enabled[i] = false;
	}
	if (root.contains("shared_context") && root["shared_context"].is_array()) {
		const auto &items = root["shared_context"].get<crude_json::array>();
		for (const auto &item : items) {
			if (!item.is_object() || !item.contains("name") ||
					!item["name"].is_string()) {
				continue;
			}
			const std::string &name = item["name"].get<crude_json::string>();
			for (int i = 0; i < context_count; i++) {
				if (contexts && name == contexts[i].name) {
					model->context_enabled[i] = true;
				}
			}
		}
	}
	if (root.contains("nodes") && root["nodes"].is_array()) {
		const auto &items = root["nodes"].get<crude_json::array>();
		for (const auto &item : items) {
			if (!item.is_object() || model->node_count >= PDGUI_WEAPON_GRAPH_MAX_NODES) {
				continue;
			}
			PdWeaponGraphNodeEdit &node = model->nodes[model->node_count++];
			memset(&node, 0, sizeof(node));
			node.editor_id = nextEditorId(model);
			if (item.contains("id")) copyString(node.id, sizeof(node.id), item["id"]);
			if (item.contains("kind")) copyString(node.kind, sizeof(node.kind), item["kind"]);
			if (item.contains("subgraph")) {
				copyString(node.subgraph, sizeof(node.subgraph), item["subgraph"]);
			}
			if (!node.subgraph[0]) snprintf(node.subgraph, sizeof(node.subgraph), "primary");
			if (item.contains("params") && item["params"].is_object()) {
				std::string dumped = item["params"].dump(0);
				snprintf(node.params, sizeof(node.params), "%s", dumped.c_str());
			} else {
				snprintf(node.params, sizeof(node.params), "{}");
			}
		}
	}
	if (root.contains("edges") && root["edges"].is_array()) {
		const auto &items = root["edges"].get<crude_json::array>();
		for (const auto &item : items) {
			if (!item.is_object() || model->edge_count >= PDGUI_WEAPON_GRAPH_MAX_EDGES) {
				continue;
			}
			if (!item.contains("from") || !item.contains("to") ||
					!item["from"].is_string() || !item["to"].is_string()) {
				continue;
			}
			const std::string &from = item["from"].get<crude_json::string>();
			const std::string &to = item["to"].get<crude_json::string>();
			int from_index = -1;
			int to_index = -1;
			for (int i = 0; i < model->node_count; i++) {
				if (from == model->nodes[i].id) from_index = i;
				if (to == model->nodes[i].id) to_index = i;
			}
			if (from_index >= 0 && to_index >= 0) {
				PdWeaponGraphEdgeEdit &edge = model->edges[model->edge_count++];
				edge.editor_id = nextEditorId(model);
				edge.from = from_index;
				edge.to = to_index;
			}
		}
	}
	if (root.contains("exports") && root["exports"].is_array()) {
		const auto &items = root["exports"].get<crude_json::array>();
		for (const auto &item : items) {
			if (!item.is_object() || !item.contains("name") ||
					!item.contains("node") || !item["name"].is_string() ||
					!item["node"].is_string()) {
				continue;
			}
			const std::string &name = item["name"].get<crude_json::string>();
			const std::string &node_id = item["node"].get<crude_json::string>();
			for (int i = 0; i < model->node_count; i++) {
				if (node_id != model->nodes[i].id) continue;
				if (name == "primary") model->primary_export = i;
				else if (name == "secondary") model->secondary_export = i;
			}
		}
	}
	if (root.contains("editor") && root["editor"].is_object() &&
			root["editor"].contains("layout") &&
			root["editor"]["layout"].is_object() &&
			root["editor"]["layout"].contains("nodes") &&
			root["editor"]["layout"]["nodes"].is_array()) {
		const auto &items = root["editor"]["layout"]["nodes"].get<crude_json::array>();
		for (const auto &item : items) {
			if (!item.is_object() || !item.contains("id") ||
					!item["id"].is_string()) {
				continue;
			}
			const std::string &node_id = item["id"].get<crude_json::string>();
			for (int i = 0; i < model->node_count; i++) {
				if (node_id != model->nodes[i].id) continue;
				if (item.contains("x") && item["x"].is_number()) {
					model->nodes[i].pos_x = (float)item["x"].get<crude_json::number>();
					model->nodes[i].pos_valid = true;
				}
				if (item.contains("y") && item["y"].is_number()) {
					model->nodes[i].pos_y = (float)item["y"].get<crude_json::number>();
					model->nodes[i].pos_valid = true;
				}
			}
		}
	}
	if (model->node_count == 0) {
		if (err && err_cap) snprintf(err, err_cap, "graph has no editable nodes");
		return false;
	}
	seedMissingNodeLayout(model);
	return true;
}

bool pdguiWeaponGraphNodeEditorRender(const PdWeaponGraphEditorDesc *desc,
		PdWeaponGraphEditorResult *result)
{
	if (!desc || !desc->model || !result) return false;
	memset(result, 0, sizeof(*result));
	result->action = PD_WEAPON_GRAPH_EDITOR_ACTION_NONE;
	result->status_ok = true;
	pdguiWeaponGraphNodeEditorInit();
	ensureEditorIds(desc->model);

	const float scale = desc->scale > 0.0f ? desc->scale : 1.0f;
	float avail = ImGui::GetContentRegionAvail().x;
	float paletteW = 176.0f * scale;
	float inspectorW = 254.0f * scale;
	float canvasW = avail - paletteW - inspectorW - 16.0f * scale;
	if (canvasW < 260.0f * scale) canvasW = 260.0f * scale;
	if (desc->scope_label && desc->scope_label[0]) {
		ImGui::TextDisabled("Editing %s", desc->scope_label);
	}
	float rowH = ImGui::GetContentRegionAvail().y - 30.0f * scale;
	if (rowH < 280.0f * scale) rowH = 280.0f * scale;

	renderPalette(desc, result, paletteW, rowH);
	ImGui::SameLine();
	renderCanvas(desc, result, canvasW, rowH);
	ImGui::SameLine();
	renderInspector(desc, result, inspectorW, rowH);

	ImGui::Separator();
	ImGui::TextDisabled(
		"Canvas: drag nodes, drag exec pins to link, right-click canvas/node/link/pin, Alt-click pin to break, Delete removes selection, F frames visible nodes. Visible links: %d/%d.",
		visibleEdgeCount(desc, desc->model), desc->model->edge_count);
	if (desc->model->selected_node >= 0) {
		ImGui::SameLine();
		ImGui::TextDisabled("Selected: %s", nodeLabel(desc->model, desc->model->selected_node));
	}
	return result->model_changed ||
		result->action != PD_WEAPON_GRAPH_EDITOR_ACTION_NONE ||
		result->status[0] != '\0';
}
