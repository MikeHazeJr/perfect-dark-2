/**
 * test_weapon_graph_editor_io.cpp -- the Weapons creator's one graph document
 * to and from the clean `.pdweapon` behavior files.
 *
 * The files Split writes are checked with the compiler the game loads weapon
 * sources with (weaponGraphCompileWeaponSourceJson), and one case saves them
 * through modAuthorSave so the result is a complete mod on disk.
 *
 * When PD2_TEST_ARTIFACT_DIR is set, the saved archive is also copied to
 * <dir>/clones so tools/asset_archive_conformance.py can be run over it.
 */
#include "catch.hpp"
extern "C" {
#include "asset_archive_clone.h"
#include "assetcatalog.h"
#include "mod_asset_validate.h"
#include "mod_author.h"
#include "modarchive.h"
#include "modasset_json.h"
#include "weapon_graph_runtime.h"
#include "constants.h"
#include "mission_graph_runner.h"
#include "projectile_rules.h"
#include "weapon_rules.h"
}
#include "../port/external/imgui-node-editor/crude_json.h"
#include "modasset_gltf_document.h"
#include "weapon_graph_editor_io.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

const char *const kIoWeaponTemplate =
	"examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";

struct IoTempDir {
	fs::path path;

	IoTempDir() {
		static unsigned counter = 0;
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path()
			/ ("pd2-weapon-editor-io-" + std::to_string(stamp) + "-" + std::to_string(counter++));
		fs::create_directories(path);
	}

	~IoTempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::string ioReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string ioEntry(const std::string &archive, const std::string &entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry.c_str(), &size);
	REQUIRE(data != nullptr);
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

void ioKeepArtifact(const fs::path &archive)
{
	const char *root = std::getenv("PD2_TEST_ARTIFACT_DIR");
	if (!root || !root[0]) return;
	std::error_code ec;
	const fs::path dir = fs::path(root) / "clones";
	fs::create_directories(dir, ec);
	fs::copy_file(archive, dir / archive.filename(), fs::copy_options::overwrite_existing, ec);
}

using IoText = std::unique_ptr<char, decltype(&std::free)>;

struct IoSplit {
	weapon_graph_editor_split_t files{};
	char err[256] = "";
	s32 ok = 0;

	IoSplit(const char *editor, const char *assetId) {
		ok = weaponGraphEditorSplit(editor, assetId, &files, err, sizeof(err));
	}
	~IoSplit() { weaponGraphEditorSplitFree(&files); }
	IoSplit(const IoSplit &) = delete;
	IoSplit &operator=(const IoSplit &) = delete;
};

/* A compiled graph; the IR is too large for the stack of a test case. */
struct IoGraph {
	std::unique_ptr<weapon_graph_ir_t> ir = std::make_unique<weapon_graph_ir_t>();
	char err[256] = "";
	bool ok = false;

	~IoGraph() { weaponGraphIrFree(ir.get()); }

	void compileSource(const char *assetId, const std::string &primary,
		const std::string &secondary, const std::string &shared) {
		ok = weaponGraphCompileWeaponSourceJson(assetId,
			primary.data(), static_cast<u32>(primary.size()),
			secondary.data(), static_cast<u32>(secondary.size()),
			shared.empty() ? nullptr : shared.data(), static_cast<u32>(shared.size()),
			ir.get(), err, sizeof(err)) == 0;
	}

	void compileWhole(const std::string &graph) {
		ok = weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
			static_cast<u32>(graph.size()), ir.get(), err, sizeof(err)) == 0;
	}

	int node(const char *id) const {
		for (s32 i = 0; i < ir->node_count; i++) {
			if (std::strcmp(ir->nodes[i].id, id) == 0) return i;
		}
		return -1;
	}

	std::string kind(const char *id) const {
		const int index = node(id);
		return index < 0 ? std::string() : std::string(ir->nodes[index].kind);
	}

	std::string scope(const char *id) const {
		const int index = node(id);
		return index < 0 ? std::string("?") : std::string(ir->nodes[index].subgraph);
	}

	std::string exported(const char *name) const {
		for (s32 i = 0; i < ir->export_count; i++) {
			if (std::strcmp(ir->exports[i].name, name) == 0) {
				return ir->nodes[ir->exports[i].node].id;
			}
		}
		return std::string();
	}

	bool edge(const char *from, const char *to) const {
		const int a = node(from);
		const int b = node(to);
		for (s32 i = 0; i < ir->edge_count; i++) {
			if (ir->edges[i].from == a && ir->edges[i].to == b) return a >= 0 && b >= 0;
		}
		return false;
	}

	const weapon_graph_ir_param_t *param(const char *id, const char *key) const {
		const int index = node(id);
		if (index < 0) return nullptr;
		const weapon_graph_ir_node_t &n = ir->nodes[index];
		for (s32 i = 0; i < n.param_count; i++) {
			const weapon_graph_ir_param_t &p = ir->params[n.param_start + i];
			if (std::strcmp(p.key, key) == 0) return &p;
		}
		return nullptr;
	}
};

bool ioStrict(const char *json)
{
	return json && modAssetJsonValidateObject(json, std::strlen(json)) != 0;
}

size_t ioCount(const std::string &text, const std::string &needle)
{
	size_t count = 0;
	for (size_t at = text.find(needle); at != std::string::npos;
			at = text.find(needle, at + needle.size())) {
		count++;
	}
	return count;
}

} // namespace

TEST_CASE("weapon editor graph round trips through the example weapon's clean files",
          "[modding][weapon-editor]") {
	const std::string archive = ioReadFile(kIoWeaponTemplate);
	REQUIRE_FALSE(archive.empty());
	const std::string primary = ioEntry(archive, "behavior/primary.graph.json");
	const std::string secondary = ioEntry(archive, "behavior/secondary.graph.json");
	const std::string shared = ioEntry(archive, "behavior/shared-context.json");

	char err[256] = "";
	IoText editor(weaponGraphEditorCompose(primary.c_str(), secondary.c_str(),
		shared.c_str(), "example:tri_weapon", err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(editor.get() != nullptr);
	REQUIRE(ioStrict(editor.get()));

	/* The editor document is one graph the compiler takes as it is. */
	IoGraph whole;
	whole.compileWhole(editor.get());
	INFO(whole.err);
	REQUIRE(whole.ok);
	CHECK(whole.ir->node_count == 4);
	CHECK(whole.scope("trigger_primary") == "primary");
	CHECK(whole.scope("spawn_projectile") == "primary");
	CHECK(whole.scope("trigger_secondary") == "secondary");
	CHECK(whole.scope("deploy_entity") == "secondary");
	CHECK(whole.exported("primary") == "spawn_projectile");
	CHECK(whole.exported("secondary") == "deploy_entity");
	CHECK(whole.edge("trigger_primary", "spawn_projectile"));
	CHECK(whole.edge("trigger_secondary", "deploy_entity"));
	CHECK(whole.ir->edge_count == 2);
	CHECK(whole.ir->context_count == 4);

	IoSplit split(editor.get(), "example:tri_weapon");
	INFO(split.err);
	REQUIRE(split.ok == 1);
	REQUIRE(split.files.primary != nullptr);
	REQUIRE(split.files.secondary != nullptr);
	REQUIRE(split.files.shared_context != nullptr);
	CHECK(ioStrict(split.files.primary));
	CHECK(ioStrict(split.files.secondary));
	CHECK(ioStrict(split.files.shared_context));
	CHECK(std::string(split.files.primary).find("\"graph_id\": \"primary\"") != std::string::npos);
	CHECK(std::string(split.files.secondary).find("\"graph_id\": \"secondary\"") != std::string::npos);
	CHECK(std::string(split.files.shared_context).find("\"schema\": \"pd.weapon_shared_context.v1\"")
		!= std::string::npos);

	/* What the game compiles from the split files is what it compiles from
	 * the template's own files. */
	IoGraph fromSplit;
	fromSplit.compileSource("example:tri_weapon", split.files.primary, split.files.secondary,
		split.files.shared_context);
	INFO(fromSplit.err);
	REQUIRE(fromSplit.ok);
	IoGraph fromTemplate;
	fromTemplate.compileSource("example:tri_weapon", primary, secondary, shared);
	INFO(fromTemplate.err);
	REQUIRE(fromTemplate.ok);

	REQUIRE(fromSplit.ir->node_count == fromTemplate.ir->node_count);
	for (s32 i = 0; i < fromTemplate.ir->node_count; i++) {
		const weapon_graph_ir_node_t &want = fromTemplate.ir->nodes[i];
		const weapon_graph_ir_node_t &got = fromSplit.ir->nodes[i];
		INFO(want.id);
		CHECK(std::string(got.id) == want.id);
		CHECK(std::string(got.kind) == want.kind);
		REQUIRE(got.param_count == want.param_count);
		for (s32 p = 0; p < want.param_count; p++) {
			const weapon_graph_ir_param_t &a = fromTemplate.ir->params[want.param_start + p];
			const weapon_graph_ir_param_t *b = fromSplit.param(want.id, a.key);
			INFO(a.key);
			REQUIRE(b != nullptr);
			CHECK(b->type == a.type);
			CHECK(std::string(b->value) == a.value);
			CHECK(b->i_value == a.i_value);
			CHECK(b->f_value == a.f_value);
			CHECK(b->b_value == a.b_value);
		}
	}
	REQUIRE(fromSplit.ir->edge_count == fromTemplate.ir->edge_count);
	for (s32 i = 0; i < fromTemplate.ir->edge_count; i++) {
		CHECK(fromSplit.ir->edges[i].from == fromTemplate.ir->edges[i].from);
		CHECK(fromSplit.ir->edges[i].to == fromTemplate.ir->edges[i].to);
	}
	REQUIRE(fromSplit.ir->export_count == fromTemplate.ir->export_count);
	for (s32 i = 0; i < fromTemplate.ir->export_count; i++) {
		CHECK(std::string(fromSplit.ir->exports[i].name) == fromTemplate.ir->exports[i].name);
		CHECK(fromSplit.ir->exports[i].node == fromTemplate.ir->exports[i].node);
	}
	REQUIRE(fromSplit.ir->context_count == fromTemplate.ir->context_count);
	for (s32 i = 0; i < fromTemplate.ir->context_count; i++) {
		const weapon_graph_ir_context_t &want = fromTemplate.ir->contexts[i];
		const weapon_graph_ir_context_t &got = fromSplit.ir->contexts[i];
		CHECK(std::string(got.name) == want.name);
		CHECK(std::string(got.scope) == want.scope);
		CHECK(std::string(got.source) == want.source);
		CHECK(std::string(got.type) == want.type);
		CHECK(std::string(got.lifetime) == want.lifetime);
	}

	/* Loading what was saved and saving it again changes nothing. */
	IoText again(weaponGraphEditorCompose(split.files.primary, split.files.secondary,
		split.files.shared_context, "example:tri_weapon", err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(again.get() != nullptr);
	CHECK(std::string(again.get()) == std::string(editor.get()));
	IoSplit second(again.get(), "example:tri_weapon");
	REQUIRE(second.ok == 1);
	CHECK(std::string(second.files.primary) == split.files.primary);
	CHECK(std::string(second.files.secondary) == split.files.secondary);
	CHECK(std::string(second.files.shared_context) == split.files.shared_context);
}

TEST_CASE("weapon editor split writes an empty mode the way the base game does",
          "[modding][weapon-editor]") {
	SECTION("only the primary mode has nodes") {
		const char *const editor = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "mod:weapon_one_mode",
  "graph_id": "custom_modular",
  "nodes": [
    { "id": "pull", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary" } },
    { "id": "shot", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_single", "ammo_slot": -1 } }
  ],
  "edges": [ { "from": "pull", "to": "shot" } ],
  "exports": [ { "name": "primary", "node": "shot" } ]
})";
		IoSplit split(editor, "mod:weapon_one_mode");
		INFO(split.err);
		REQUIRE(split.ok == 1);
		/* No context list in the document: the template's file is kept. */
		CHECK(split.files.shared_context == nullptr);

		const std::string secondary = split.files.secondary;
		CHECK(secondary.find("\"id\": \"secondary_empty\"") != std::string::npos);
		CHECK(secondary.find("\"kind\": \"function.empty\"") != std::string::npos);
		CHECK(secondary.find("\"function_type\": \"none\"") != std::string::npos);
		CHECK(secondary.find("\"source\": \"empty_slot\"") != std::string::npos);
		CHECK(secondary.find("\"edges\": []") != std::string::npos);

		IoGraph graph;
		graph.compileSource("mod:weapon_one_mode", split.files.primary, split.files.secondary, "");
		INFO(graph.err);
		REQUIRE(graph.ok);
		CHECK(graph.ir->node_count == 3);
		CHECK(graph.exported("primary") == "shot");
		CHECK(graph.exported("secondary") == "secondary_empty");
		CHECK(graph.kind("secondary_empty") == "function.empty");
		CHECK(graph.scope("secondary_empty") == "secondary");

		/* Saved, loaded and saved again: the empty function is a node like
		 * any other and no second one appears. */
		char err[256] = "";
		IoText again(weaponGraphEditorCompose(split.files.primary, split.files.secondary,
			nullptr, "mod:weapon_one_mode", err, sizeof(err)), &std::free);
		INFO(err);
		REQUIRE(again.get() != nullptr);
		IoSplit second(again.get(), "mod:weapon_one_mode");
		REQUIRE(second.ok == 1);
		CHECK(std::string(second.files.secondary) == secondary);
		CHECK(ioCount(second.files.secondary, "function.empty") == 1);
	}

	SECTION("a document with no nodes at all") {
		IoSplit split(R"({ "schema": "pd.weapon_graph.v1", "nodes": [], "shared_context": [] })",
			"mod:weapon_nothing");
		INFO(split.err);
		REQUIRE(split.ok == 1);
		/* An empty list is a decision: the weapon shares no contexts. */
		REQUIRE(split.files.shared_context != nullptr);
		CHECK(std::string(split.files.shared_context).find("\"contexts\": []") != std::string::npos);
		CHECK(ioStrict(split.files.shared_context));

		IoGraph graph;
		graph.compileSource("mod:weapon_nothing", split.files.primary, split.files.secondary,
			split.files.shared_context);
		INFO(graph.err);
		REQUIRE(graph.ok);
		CHECK(graph.ir->node_count == 2);
		CHECK(graph.exported("primary") == "primary_empty");
		CHECK(graph.exported("secondary") == "secondary_empty");
		CHECK(graph.ir->context_count == 0);
	}

	SECTION("the id of the added node never collides") {
		const char *const editor = R"({
  "nodes": [
    { "id": "secondary_empty", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary" } },
    { "id": "secondary_empty_2", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary" } }
  ]
})";
		IoSplit split(editor, "mod:weapon_names");
		INFO(split.err);
		REQUIRE(split.ok == 1);
		IoGraph graph;
		graph.compileSource("mod:weapon_names", split.files.primary, split.files.secondary, "");
		INFO(graph.err);
		REQUIRE(graph.ok);
		CHECK(graph.exported("primary") == "secondary_empty");
		CHECK(graph.exported("secondary") == "secondary_empty_3");
	}
}

TEST_CASE("weapon editor split gives every mode an export",
          "[modding][weapon-editor]") {
	SECTION("a mode with a function and no export is exported at the function") {
		const char *const editor = R"({
  "nodes": [
    { "id": "pull", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary" } },
    { "id": "gate", "kind": "gate.cooldown_ready", "subgraph": "primary", "params": { "mode": "primary" } },
    { "id": "shot", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_single" } },
    { "id": "hold", "kind": "event.trigger_held", "subgraph": "secondary", "params": { "mode": "secondary" } },
    { "id": "swing", "kind": "melee.strike", "subgraph": "secondary", "params": { "mode": "secondary", "function_type": "melee" } }
  ],
  "edges": [ { "from": "pull", "to": "gate" }, { "from": "gate", "to": "shot" }, { "from": "hold", "to": "swing" } ]
})";
		IoSplit split(editor, "mod:weapon_no_exports");
		INFO(split.err);
		REQUIRE(split.ok == 1);
		IoGraph graph;
		graph.compileSource("mod:weapon_no_exports", split.files.primary, split.files.secondary, "");
		INFO(graph.err);
		REQUIRE(graph.ok);
		CHECK(graph.ir->node_count == 5);
		CHECK(graph.exported("primary") == "shot");
		CHECK(graph.exported("secondary") == "swing");
		CHECK(graph.ir->edge_count == 3);
	}

	SECTION("a mode with nodes but no function gets the empty function") {
		const char *const editor = R"({
  "nodes": [
    { "id": "shot", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary" } },
    { "id": "hold", "kind": "event.trigger_held", "subgraph": "secondary", "params": { "mode": "secondary" } }
  ],
  "exports": [ { "name": "primary", "node": "shot" } ]
})";
		IoSplit split(editor, "mod:weapon_trigger_only");
		INFO(split.err);
		REQUIRE(split.ok == 1);
		IoGraph graph;
		graph.compileSource("mod:weapon_trigger_only", split.files.primary, split.files.secondary, "");
		INFO(graph.err);
		REQUIRE(graph.ok);
		CHECK(graph.node("hold") >= 0);
		CHECK(graph.exported("secondary") == "secondary_empty");
		CHECK(graph.kind("secondary_empty") == "function.empty");
	}
}

TEST_CASE("weapon editor split keeps shared nodes and links between the modes",
          "[modding][weapon-editor]") {
	const char *const editor = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "mod:weapon_shared",
  "graph_id": "custom_modular",
  "nodes": [
    { "id": "lock", "kind": "gate.target_lock", "subgraph": "shared", "params": { "mode": "shared" } },
    { "id": "shot", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_single" } },
    { "id": "swing", "kind": "melee.strike", "subgraph": "secondary", "params": { "mode": "secondary", "function_type": "melee" } }
  ],
  "edges": [
    { "from": "lock", "to": "shot" },
    { "from": "lock", "to": "swing" }
  ],
  "exports": [
    { "name": "primary", "node": "shot" },
    { "name": "secondary", "node": "swing" }
  ]
})";
	IoSplit split(editor, "mod:weapon_shared");
	INFO(split.err);
	REQUIRE(split.ok == 1);

	const std::string primary = split.files.primary;
	const std::string secondary = split.files.secondary;
	/* The shared node lives in the primary file and stays shared. */
	CHECK(primary.find("\"id\": \"lock\", \"kind\": \"gate.target_lock\", \"subgraph\": \"shared\"")
		!= std::string::npos);
	CHECK(secondary.find("\"id\": \"lock\"") == std::string::npos);
	/* Each link is stored once, with the mode it belongs to. */
	CHECK(primary.find("{ \"from\": \"lock\", \"to\": \"shot\" }") != std::string::npos);
	CHECK(primary.find("\"to\": \"swing\"") == std::string::npos);
	CHECK(secondary.find("{ \"from\": \"lock\", \"to\": \"swing\" }") != std::string::npos);

	IoGraph graph;
	graph.compileSource("mod:weapon_shared", primary, secondary, "");
	INFO(graph.err);
	REQUIRE(graph.ok);
	CHECK(graph.ir->node_count == 3);
	CHECK(graph.scope("lock") == "shared");
	CHECK(graph.edge("lock", "shot"));
	CHECK(graph.edge("lock", "swing"));
	CHECK(graph.ir->edge_count == 2);

	char err[256] = "";
	IoText again(weaponGraphEditorCompose(primary.c_str(), secondary.c_str(), nullptr,
		"mod:weapon_shared", err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(again.get() != nullptr);
	const std::string composed = again.get();
	CHECK(composed.find("\"id\": \"lock\", \"kind\": \"gate.target_lock\", \"subgraph\": \"shared\"")
		!= std::string::npos);
	CHECK(ioCount(composed, "\"from\": \"lock\"") == 2);
	IoSplit second(composed.c_str(), "mod:weapon_shared");
	REQUIRE(second.ok == 1);
	CHECK(std::string(second.files.primary) == primary);
	CHECK(std::string(second.files.secondary) == secondary);

	SECTION("a mode whose function is a shared node still gets a file the loader takes") {
		const char *const both = R"({
  "nodes": [
    { "id": "shot", "kind": "fire.hitscan", "subgraph": "shared", "params": { "function_type": "shoot_single" } }
  ],
  "exports": [
    { "name": "primary", "node": "shot" },
    { "name": "secondary", "node": "shot" }
  ]
})";
		IoSplit shared(both, "mod:weapon_both");
		INFO(shared.err);
		REQUIRE(shared.ok == 1);
		IoGraph compiled;
		compiled.compileSource("mod:weapon_both", shared.files.primary, shared.files.secondary, "");
		INFO(compiled.err);
		REQUIRE(compiled.ok);
		CHECK(compiled.exported("primary") == "shot");
		CHECK(compiled.exported("secondary") == "shot");
		CHECK(compiled.kind("secondary_empty") == "function.empty");
		CHECK(compiled.ir->node_count == 2);
	}
}

TEST_CASE("weapon editor split reads the document the Weapons creator writes",
          "[modding][weapon-editor]") {
	/* The shape weaponGraphBuilderSyncJson emits: contexts as rows, the
	 * subgraph table, nodes with their mode, and the editor's layout. */
	const char *const editor = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "mod:weapon_hub",
  "graph_id": "custom_modular",
  "shared_context": [
    { "name": "owner_player", "scope": "player", "source": "equipped_player", "type": "player_ref", "lifetime": "weapon_instance" },
    { "name": "weapon_instance", "scope": "weapon", "source": "equipped_weapon", "type": "weapon_instance_ref", "lifetime": "weapon_instance" }
  ],
  "subgraphs": [
    { "id": "primary", "entry": "node_2", "shared_context": ["owner_player", "weapon_instance"] },
    { "id": "secondary", "entry": "", "shared_context": ["owner_player", "weapon_instance"] }
  ],
  "nodes": [
    { "id": "node_1", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary" } },
    { "id": "node_2", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_single", "ammo_slot": -1, "damage": 1.1, "spread": 0.1, "flags": 268435456, "recoildist": 2.0, "impactforce": 1e-3, "label": "a \"loud\" one \\ two" } }
  ],
  "edges": [
    { "from": "node_1", "to": "node_2" }
  ],
  "exports": [
    { "name": "primary", "node": "node_2" }
  ],
  "editor": {
    "layout": {
      "nodes": [
        { "id": "node_1", "x": 48.0, "y": 48.0 },
        { "id": "node_2", "x": 278.0, "y": 48.0 }
      ]
    }
  }
})";
	IoSplit split(editor, "mod:weapon_hub");
	INFO(split.err);
	REQUIRE(split.ok == 1);
	const std::string primary = split.files.primary;
	const std::string shared = split.files.shared_context ? split.files.shared_context : "";
	REQUIRE(ioStrict(primary.c_str()));
	REQUIRE(ioStrict(shared.c_str()));

	/* Numbers come out as an author would write them, not as 18 digits. */
	CHECK(primary.find("\"damage\": 1.1,") != std::string::npos);
	CHECK(primary.find("\"spread\": 0.1 ") != std::string::npos);
	CHECK(primary.find("\"flags\": 268435456,") != std::string::npos);
	CHECK(primary.find("\"ammo_slot\": -1,") != std::string::npos);
	CHECK(primary.find("\"recoildist\": 2,") != std::string::npos);
	CHECK(primary.find("\"impactforce\": 0.001,") != std::string::npos);
	CHECK(primary.find("\"label\": \"a \\\"loud\\\" one \\\\ two\"") != std::string::npos);
	/* The editor's own bookkeeping stays out of the clean files. */
	CHECK(primary.find("layout") == std::string::npos);
	CHECK(primary.find("subgraphs") == std::string::npos);
	CHECK(primary.find("shared_context") == std::string::npos);

	CHECK(shared.find("{ \"name\": \"owner_player\", \"scope\": \"player\", "
		"\"source\": \"equipped_player\", \"type\": \"player_ref\", "
		"\"lifetime\": \"weapon_instance\" }") != std::string::npos);
	CHECK(ioCount(shared, "\"name\":") == 2);

	IoGraph graph;
	graph.compileSource("mod:weapon_hub", primary, split.files.secondary, shared);
	INFO(graph.err);
	REQUIRE(graph.ok);
	CHECK(graph.ir->context_count == 2);
	CHECK(graph.exported("primary") == "node_2");
	CHECK(graph.exported("secondary") == "secondary_empty");
	const weapon_graph_ir_param_t *damage = graph.param("node_2", "damage");
	REQUIRE(damage != nullptr);
	CHECK(damage->f_value == Approx(1.1f));
	const weapon_graph_ir_param_t *label = graph.param("node_2", "label");
	REQUIRE(label != nullptr);
	CHECK(std::string(label->value) == "a \"loud\" one \\ two");
}

TEST_CASE("weapon editor shows a node's settings one per line with plain numbers",
          "[modding][weapon-editor]") {
	const std::string json = R"({ "spread": 0.6, "ammo_slot": -1,
  "recoilsettings": { "xrange": 0.6, "unk10": 1.0 }, "tags": [ "a", 2 ],
  "on": true, "none": null, "big": 4294967295 })";
	crude_json::value value;
	REQUIRE(modAssetJsonReadValue(json.data(), json.size(), value));
	const std::string text = weaponGraphEditorValueText(value);
	CHECK(text ==
		"{\n"
		"  \"ammo_slot\": -1,\n"
		"  \"big\": 4294967295,\n"
		"  \"none\": null,\n"
		"  \"on\": true,\n"
		"  \"recoilsettings\": { \"unk10\": 1, \"xrange\": 0.6 },\n"
		"  \"spread\": 0.6,\n"
		"  \"tags\": [ \"a\", 2 ]\n"
		"}");

	/* A very small number is written with an exponent and still reads back
	 * to exactly the same value. */
	const std::string tiny = "[" + weaponGraphEditorValueText(crude_json::value(1.5e-7)) + "]";
	crude_json::value tinyBack;
	REQUIRE(modAssetJsonReadValue(tiny.data(), tiny.size(), tinyBack));
	REQUIRE(tinyBack.is_array());
	CHECK(tinyBack[0].get<crude_json::number>() == 1.5e-7);

	/* The text is strict JSON and showing it again changes nothing. */
	crude_json::value again;
	REQUIRE(modAssetJsonReadValue(text.data(), text.size(), again));
	CHECK(weaponGraphEditorValueText(again) == text);

	crude_json::value empty;
	REQUIRE(modAssetJsonReadValue("{}", 2, empty));
	CHECK(weaponGraphEditorValueText(empty) == "{}");
	CHECK(weaponGraphEditorValueText(crude_json::value(0.1)) == "0.1");
	CHECK(weaponGraphEditorValueText(crude_json::value(std::string("a\tb"))) == "\"a\\tb\"");
}

TEST_CASE("weapon editor split refuses a document it cannot place and returns nothing",
          "[modding][weapon-editor]") {
	struct Bad { const char *doc; const char *reason; };
	const Bad cases[] = {
		{ "", "is empty" },
		{ "[1, 2]", "not one strict JSON object" },
		{ R"({ "nodes": [ ] } trailing)", "not one strict JSON object" },
		{ R"({ "nodes": { } })", "nodes entry that is not a list" },
		{ R"({ "nodes": [ 7 ] })", "no id or no kind" },
		{ R"({ "nodes": [ { "id": "", "kind": "fire.hitscan" } ] })", "no id or no kind" },
		{ R"({ "nodes": [ { "id": "a" } ] })", "no id or no kind" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan" }, { "id": "a", "kind": "melee.strike" } ] })",
			"node id a is used twice" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan", "subgraph": "tertiary" } ] })",
			"not primary, secondary or shared" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan", "subgraph": 2 } ] })",
			"not primary, secondary or shared" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan", "params": [ ] } ] })",
			"params that are not an object" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan" } ], "edges": [ { "from": "a", "to": "b" } ] })",
			"link to a node that does not exist (a to b)" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan" } ], "edges": [ { "from": "a" } ] })",
			"link to a node that does not exist" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan" } ], "edges": 4 })",
			"edges entry that is not a list" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan" } ], "exports": [ { "name": "primary", "node": "z" } ] })",
			"primary function is set to node z, which does not exist" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan" } ], "exports": [ { "node": "a" } ] })",
			"export with no name or no node" },
		{ R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan" } ], "exports": [ { "name": "primary", "node": "a" }, { "name": "primary", "node": "a" } ] })",
			"primary function is set twice" },
		{ R"({ "nodes": [ ], "shared_context": 3 })", "shared_context entry that is not a list" },
		{ R"({ "nodes": [ ], "shared_context": [ { "scope": "player" } ] })",
			"shared context has no name" },
	};
	for (const Bad &bad : cases) {
		INFO(bad.doc);
		weapon_graph_editor_split_t out;
		std::memset(&out, 0x5a, sizeof(out));
		char err[256] = "untouched";
		CHECK(weaponGraphEditorSplit(bad.doc, "mod:weapon_bad", &out, err, sizeof(err)) == 0);
		INFO(err);
		CHECK(std::string(err).find(bad.reason) != std::string::npos);
		CHECK(out.primary == nullptr);
		CHECK(out.secondary == nullptr);
		CHECK(out.shared_context == nullptr);
	}

	weapon_graph_editor_split_t out;
	char err[256] = "";
	CHECK(weaponGraphEditorSplit(R"({ "nodes": [ ] })", "", &out, err, sizeof(err)) == 0);
	CHECK(std::string(err).find("no asset id") != std::string::npos);
	CHECK(weaponGraphEditorSplit(R"({ "nodes": [ ] })", nullptr, &out, err, sizeof(err)) == 0);
	CHECK(weaponGraphEditorSplit(nullptr, "mod:weapon_bad", &out, err, sizeof(err)) == 0);
	CHECK(weaponGraphEditorSplit(R"({ "nodes": [ ] })", "mod:weapon_bad", nullptr, err, sizeof(err)) == 0);
	/* No place for a reason is not an error of its own. */
	CHECK(weaponGraphEditorSplit("[]", "mod:weapon_bad", &out, nullptr, 0) == 0);
	CHECK(weaponGraphEditorSplit(R"({ "nodes": [ ] })", "mod:weapon_ok", &out, nullptr, 0) == 1);
	weaponGraphEditorSplitFree(&out);
	CHECK(out.primary == nullptr);
	weaponGraphEditorSplitFree(&out);
	weaponGraphEditorSplitFree(nullptr);
}

TEST_CASE("weapon editor compose refuses template files it cannot read",
          "[modding][weapon-editor]") {
	const char *const primary = R"({ "nodes": [ { "id": "a", "kind": "fire.hitscan", "params": { "mode": "primary" } } ],
  "exports": [ { "name": "primary", "node": "a" } ] })";
	const char *const secondary = R"({ "nodes": [ { "id": "b", "kind": "melee.strike", "params": { "mode": "secondary" } } ],
  "exports": [ { "name": "secondary", "node": "b" } ] })";
	const char *const shared = R"({ "schema": "pd.weapon_shared_context.v1", "contexts": [ ] })";
	char err[256] = "";

	IoText good(weaponGraphEditorCompose(primary, secondary, shared, "mod:weapon_c", err, sizeof(err)),
		&std::free);
	INFO(err);
	REQUIRE(good.get() != nullptr);
	/* A node with no mode of its own takes the mode of the file it is in. */
	CHECK(std::string(good.get()).find("\"id\": \"a\", \"kind\": \"fire.hitscan\", \"subgraph\": \"primary\"")
		!= std::string::npos);
	CHECK(std::string(good.get()).find("\"id\": \"b\", \"kind\": \"melee.strike\", \"subgraph\": \"secondary\"")
		!= std::string::npos);
	CHECK(std::string(good.get()).find("\"shared_context\": []") != std::string::npos);

	struct Bad { const char *primary; const char *secondary; const char *shared; const char *reason; };
	const Bad cases[] = {
		{ nullptr, secondary, shared, "the primary graph is empty" },
		{ primary, "", shared, "the secondary graph is empty" },
		{ "{ nodes }", secondary, shared, "the primary graph is not one strict JSON object" },
		{ primary, primary, shared, "node id a is used twice" },
		{ primary, secondary, "nope", "the shared context is not one strict JSON object" },
		{ primary, secondary, R"({ "contexts": 5 })", "contexts entry that is not a list" },
		{ primary, R"({ "nodes": [ { "id": "b", "kind": "melee.strike" } ], "edges": [ { "from": "b", "to": "gone" } ] })",
			shared, "the secondary graph has a link to a node that does not exist" },
		{ primary, R"({ "nodes": [ { "id": "b", "kind": "melee.strike" } ], "exports": [ { "name": "primary", "node": "b" } ] })",
			shared, "primary function is set twice" },
	};
	for (const Bad &bad : cases) {
		INFO(bad.reason);
		char reason[256] = "untouched";
		char *text = weaponGraphEditorCompose(bad.primary, bad.secondary, bad.shared,
			"mod:weapon_c", reason, sizeof(reason));
		INFO(reason);
		CHECK(text == nullptr);
		CHECK(std::string(reason).find(bad.reason) != std::string::npos);
		std::free(text);
	}
	CHECK(weaponGraphEditorCompose(primary, secondary, shared, "", err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("no asset id") != std::string::npos);
	CHECK(weaponGraphEditorCompose(primary, secondary, shared, nullptr, nullptr, 0) == nullptr);

	/* A link and an export may name a node of the other file. */
	const char *const crossSecondary = R"({ "nodes": [ { "id": "b", "kind": "melee.strike" } ],
  "edges": [ { "from": "a", "to": "b" } ], "exports": [ { "name": "secondary", "node": "a" } ] })";
	IoText cross(weaponGraphEditorCompose(primary, crossSecondary, nullptr, "mod:weapon_c",
		err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(cross.get() != nullptr);
	CHECK(std::string(cross.get()).find("{ \"from\": \"a\", \"to\": \"b\" }") != std::string::npos);
	CHECK(std::string(cross.get()).find("{ \"name\": \"secondary\", \"node\": \"a\" }") != std::string::npos);
}

TEST_CASE("a weapon saved from the editor's graph is a complete mod with the edited behavior",
          "[modding][weapon-editor][author]") {
	IoTempDir tmp;
	const fs::path mods = tmp.path / "mods";
	fs::create_directories(mods);

	/* The template's primary function with one more setting, no secondary
	 * function, and one shared context instead of four. The ids are the
	 * template's: the save renames them. */
	const char *const editor = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "example:tri_weapon",
  "graph_id": "custom_modular",
  "shared_context": [
    { "name": "owner_player", "scope": "player", "source": "equipped_player", "type": "player_ref", "lifetime": "weapon_instance" }
  ],
  "nodes": [
    { "id": "trigger_primary", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary" } },
    { "id": "spawn_projectile", "kind": "spawn.fired_projectile", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_projectile", "projectile_ref": "example:tri_projectile", "ammo_slot": -1 } }
  ],
  "edges": [ { "from": "trigger_primary", "to": "spawn_projectile" } ],
  "exports": [ { "name": "primary", "node": "spawn_projectile" } ]
})";
	IoSplit split(editor, "example:tri_weapon");
	INFO(split.err);
	REQUIRE(split.ok == 1);
	REQUIRE(split.files.shared_context != nullptr);

	const asset_archive_clone_override_t overrides[] = {
		{ "behavior/primary.graph.json", split.files.primary,
			static_cast<u32>(std::strlen(split.files.primary)), 0 },
		{ "behavior/secondary.graph.json", split.files.secondary,
			static_cast<u32>(std::strlen(split.files.secondary)), 0 },
		{ "behavior/shared-context.json", split.files.shared_context,
			static_cast<u32>(std::strlen(split.files.shared_context)), 0 },
	};
	const std::string root = mods.string();
	mod_author_request_t request{};
	request.template_path = kIoWeaponTemplate;
	request.mods_root = root.c_str();
	request.display_name = "Edited Tri";
	request.tool = "pd-tests";
	request.overrides = overrides;
	request.override_count = 3;

	auto result = std::make_unique<mod_author_result_t>();
	char err[512] = "";
	const s32 saved = modAuthorSave(&request, result.get(), err, sizeof(err));
	INFO(err);
	REQUIRE(saved == MOD_AUTHOR_OK);
	CHECK(std::string(result->catalog_id) == "mod:weapon_edited_tri");
	ioKeepArtifact(result->archive_path);

	char reason[256] = "";
	CHECK(modAssetValidateArchiveFile(result->archive_path, reason, sizeof(reason))
		== MOD_ASSET_VALIDATE_OK);
	INFO(reason);

	/* The saved files name the mod, not the template. */
	const std::string archive = ioReadFile(result->archive_path);
	const std::string primary = ioEntry(archive, "behavior/primary.graph.json");
	const std::string secondary = ioEntry(archive, "behavior/secondary.graph.json");
	const std::string shared = ioEntry(archive, "behavior/shared-context.json");
	CHECK(primary.find("\"asset_id\": \"mod:weapon_edited_tri\"") != std::string::npos);
	CHECK(secondary.find("\"asset_id\": \"mod:weapon_edited_tri\"") != std::string::npos);
	CHECK(shared.find("\"asset_id\": \"mod:weapon_edited_tri\"") != std::string::npos);
	CHECK(primary.find("example:") == std::string::npos);

	std::string projectile;
	for (u32 i = 0; i < result->ids.id_count; i++) {
		if (std::strcmp(result->ids.ids[i].old_id, "example:tri_projectile") == 0) {
			projectile = result->ids.ids[i].new_id;
		}
	}
	REQUIRE_FALSE(projectile.empty());
	CHECK(projectile.compare(0, 4, "mod:") == 0);

	/* What the game compiles from the saved archive. */
	IoGraph graph;
	graph.ok = weaponGraphCompileArchiveFile(result->archive_path, ASSET_WEAPON,
		graph.ir.get(), graph.err, sizeof(graph.err)) == 0;
	INFO(graph.err);
	REQUIRE(graph.ok);
	CHECK(std::string(graph.ir->asset_id) == "mod:weapon_edited_tri");
	CHECK(graph.ir->node_count == 3);
	CHECK(graph.exported("primary") == "spawn_projectile");
	CHECK(graph.exported("secondary") == "secondary_empty");
	CHECK(graph.kind("secondary_empty") == "function.empty");
	CHECK(graph.node("deploy_entity") < 0);
	CHECK(graph.ir->context_count == 1);
	const weapon_graph_ir_param_t *ref = graph.param("spawn_projectile", "projectile_ref");
	REQUIRE(ref != nullptr);
	CHECK(std::string(ref->value) == projectile);
	const weapon_graph_ir_param_t *ammo = graph.param("spawn_projectile", "ammo_slot");
	REQUIRE(ammo != nullptr);
	CHECK(ammo->i_value == -1);

	/* Opening the saved mod in the editor shows the same graph. */
	IoText reopened(weaponGraphEditorCompose(primary.c_str(), secondary.c_str(), shared.c_str(),
		result->catalog_id, err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(reopened.get() != nullptr);
	IoGraph again;
	again.compileWhole(reopened.get());
	INFO(again.err);
	REQUIRE(again.ok);
	CHECK(again.ir->node_count == 3);
	CHECK(again.exported("secondary") == "secondary_empty");
	CHECK(again.scope("secondary_empty") == "secondary");
}

/* held-weapon-events.md slice 2: a base weapon's function graphs are
 * pd.weapon_graph.v2, the record plus the function's program. A weapon made
 * from one keeps its program through the Weapons creator: compose keeps each
 * program node's settings and each execution edge's ports, the joined
 * document validates with its programs, and split writes the program back
 * into its own mode file as v2. */
TEST_CASE("weapon editor keeps a function's program through compose and split",
          "[modding][weapon-editor][held-weapon]") {
	const char *const primary = R"({
  "schema": "pd.weapon_graph.v2",
  "asset_id": "mod:weapon_program",
  "graph_id": "primary",
  "nodes": [
    { "id": "primary_trigger", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary", "trigger": "pressed" } },
    { "id": "primary_action", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_single", "ammo_slot": 0 } },
    { "id": "primary_pressed", "kind": "event.weapon.trigger_pressed" },
    { "id": "primary_has_ammo", "kind": "gate.weapon.has_ammo", "rounds": 2 },
    { "id": "primary_attack", "kind": "og.weapon.burst", "count": 3, "gap": 4 },
    { "id": "primary_use_ammo", "kind": "og.weapon.use_ammo", "rounds": 1 }
  ],
  "edges": [
    { "from": "primary_trigger", "to": "primary_action" },
    { "from": "primary_pressed", "output": "out", "to": "primary_has_ammo", "input": "exec" },
    { "from": "primary_has_ammo", "output": "pass", "to": "primary_attack", "input": "exec" },
    { "from": "primary_attack", "output": "out", "to": "primary_use_ammo", "input": "exec" }
  ],
  "exports": [ { "name": "primary", "node": "primary_action" } ]
})";
	const char *const secondary = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "mod:weapon_program",
  "graph_id": "secondary",
  "nodes": [
    { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary", "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1 } }
  ],
  "edges": [],
  "exports": [ { "name": "secondary", "node": "secondary_action" } ]
})";
	char err[256] = "";
	IoText editor(weaponGraphEditorCompose(primary, secondary, nullptr, "mod:weapon_program",
		err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(editor.get() != nullptr);
	const std::string joined = editor.get();
	CHECK(ioStrict(editor.get()));
	CHECK(joined.find("\"schema\": \"pd.weapon_graph.v2\"") != std::string::npos);
	CHECK(joined.find("\"rounds\": 2") != std::string::npos);
	CHECK(joined.find("\"gap\": 4") != std::string::npos);
	CHECK(joined.find("\"output\": \"pass\"") != std::string::npos);
	CHECK(joined.find("\"input\": \"exec\"") != std::string::npos);
	REQUIRE(weaponGraphValidateJson(ASSET_WEAPON, editor.get(), static_cast<u32>(joined.size()),
		err, sizeof(err)) == 0);

	IoSplit split(editor.get(), "mod:weapon_program");
	INFO(split.err);
	REQUIRE(split.ok == 1);
	const std::string writtenPrimary = split.files.primary;
	const std::string writtenSecondary = split.files.secondary;
	CHECK(writtenPrimary.find("\"schema\": \"pd.weapon_graph.v2\"") != std::string::npos);
	CHECK(writtenPrimary.find("\"rounds\": 2") != std::string::npos);
	CHECK(writtenPrimary.find("\"count\": 3") != std::string::npos);
	CHECK(writtenPrimary.find("\"output\": \"pass\"") != std::string::npos);
	CHECK(writtenSecondary.find("\"schema\": \"pd.weapon_graph.v1\"") != std::string::npos);
	CHECK(ioStrict(split.files.primary));

	IoGraph graph;
	graph.compileSource("mod:weapon_program", writtenPrimary, writtenSecondary, "");
	INFO(graph.err);
	REQUIRE(graph.ok);
	CHECK(graph.exported("primary") == "primary_action");
	REQUIRE(graph.ir->held_program_json[0] != nullptr);
	CHECK(graph.ir->held_program_json[1] == nullptr);
	CHECK(std::string(graph.ir->held_program_json[0]).find("og.weapon.burst") != std::string::npos);
	CHECK(std::string(graph.ir->held_program_json[0]).find("og.weapon.use_ammo") != std::string::npos);

	/* The joined document's programs are checked as registration checks
	 * them: a module wired where it does nothing fails validation. */
	std::string broken = joined;
	const std::string kind = "\"kind\": \"event.weapon.trigger_pressed\"";
	REQUIRE(broken.find(kind) != std::string::npos);
	broken.replace(broken.find(kind), kind.size(), "\"kind\": \"event.weapon.trigger_released\"");
	CHECK(weaponGraphValidateJson(ASSET_WEAPON, broken.data(), static_cast<u32>(broken.size()),
		err, sizeof(err)) != 0);
	CHECK(std::string(err).find("og.weapon.burst has no effect on event.weapon.trigger_released") != std::string::npos);

	/* held-weapon-events.md slice 3: a trigger's settings are checked too. */
	std::string slow = joined;
	const std::string gap = "\"gap\": 4";
	REQUIRE(slow.find(gap) != std::string::npos);
	slow.replace(slow.find(gap), gap.size(), "\"gap\": 300");
	CHECK(weaponGraphValidateJson(ASSET_WEAPON, slow.data(), static_cast<u32>(slow.size()),
		err, sizeof(err)) != 0);
	CHECK(std::string(err).find("og.weapon.burst needs gap from 0 to 255, got 300") != std::string::npos);

	/* slice 5 (PR 31 review): only the charges the runtime runs validate. A
	 * custom weapon's charge may start on a press (it then goes when full); a
	 * base weapon's may not, nor may a pressed charge wait for a press. */
	const auto charged = [&](const std::string &fields, const std::string &asset) {
		std::string doc = joined;
		const std::string burst = "\"og.weapon.burst\"";
		const std::string gap = "\"gap\": 4";
		REQUIRE(doc.find(burst) != std::string::npos);
		doc.replace(doc.find(burst), burst.size(), "\"og.weapon.charge\"");
		doc.replace(doc.find(gap), gap.size(), gap + fields);
		const std::string mod = "mod:weapon_program";
		for (size_t at = doc.find(mod); at != std::string::npos; at = doc.find(mod, at + asset.size())) {
			doc.replace(at, mod.size(), asset);
		}
		return doc;
	};
	std::string doc = charged(", \"start\": \"on_press\"", "mod:weapon_program");
	CHECK(weaponGraphValidateJson(ASSET_WEAPON, doc.data(), static_cast<u32>(doc.size()), err, sizeof(err)) == 0);
	doc = charged(", \"start\": \"on_press\", \"release\": \"on_press\"", "mod:weapon_program");
	CHECK(weaponGraphValidateJson(ASSET_WEAPON, doc.data(), static_cast<u32>(doc.size()), err, sizeof(err)) != 0);
	CHECK(std::string(err).find("og.weapon.charge: a charge that a press starts goes by itself when full")
		!= std::string::npos);
	doc = charged(", \"start\": \"on_press\"", "base:weapon_program");
	CHECK(weaponGraphValidateJson(ASSET_WEAPON, doc.data(), static_cast<u32>(doc.size()), err, sizeof(err)) != 0);
	CHECK(std::string(err).find("og.weapon.charge: start on_press is a custom weapon's committed launch")
		!= std::string::npos);
	doc = charged("", "base:weapon_program"); // builds while selected, fired by the trigger
	CHECK(weaponGraphValidateJson(ASSET_WEAPON, doc.data(), static_cast<u32>(doc.size()), err, sizeof(err)) == 0);
}

// held-weapon-events.md slice 8a: the creator's designer nodes are exactly
// the program nodes the runtime runs, each with a designer name and group,
// and each node has its own execution ports.
TEST_CASE("the creator's designer nodes are the runtime's program nodes, with their ports",
	"[weapon-graph][editor][held-weapon]")
{
	std::set<std::string> runtime;
	for (s32 m = 0; m < WEAPON_MODULE_COUNT; m++) runtime.insert(weaponRulesModuleKind(1u << m));
	for (s32 g = 0; g < WEAPON_GATE_COUNT; g++) runtime.insert(weaponRulesGateKind(WEAPON_RULE_GATE_HAS_AMMO << g));
	for (s32 e = 0; weaponRulesEventAt(e) >= 0; e++) runtime.insert(weaponRulesEventName(weaponRulesEventAt(e)));
	const std::set<std::string> groups = { "Events", "Checks", "Trigger", "Ammo", "Shot", "Aim", "Feel", "State" };
	std::set<std::string> designer;
	for (s32 i = 0; i < weaponGraphDesignerNodeCount(); i++) {
		const weapon_graph_designer_node_t *node = weaponGraphDesignerNodeAt(i);
		REQUIRE(node != nullptr);
		INFO(node->kind);
		CHECK(designer.insert(node->kind).second);
		CHECK(node->label[0] != '\0');
		CHECK(groups.count(node->group) == 1);
		CHECK(weaponGraphDesignerNodeFor(node->kind) == node);
		CHECK(weaponGraphNodeIsProgram(node->kind) == 1);
	}
	CHECK(designer == runtime);
	CHECK(weaponGraphDesignerNodeAt(weaponGraphDesignerNodeCount()) == nullptr);
	CHECK(weaponGraphDesignerNodeAt(-1) == nullptr);
	CHECK(weaponGraphDesignerNodeFor("fire.hitscan") == nullptr);
	CHECK(weaponGraphDesignerNodeFor(nullptr) == nullptr);
	CHECK(std::string(weaponGraphDesignerNodeFor("og.weapon.automatic")->label) == "Automatic");
	CHECK(std::string(weaponGraphDesignerNodeFor("og.weapon.automatic")->group) == "Trigger");

	// An event has no input; a gate leaves by pass or fail; a module by out;
	// a record node keeps its one unnamed link.
	CHECK(weaponGraphNodeTakesExec("event.weapon.trigger_pressed") == 0);
	CHECK(weaponGraphNodeOutputCount("event.weapon.trigger_pressed") == 1);
	CHECK(std::string(weaponGraphNodeOutputName("event.weapon.trigger_pressed", 0)) == "out");
	CHECK(weaponGraphNodeTakesExec("gate.weapon.has_ammo") == 1);
	CHECK(weaponGraphNodeOutputCount("gate.weapon.has_ammo") == 2);
	CHECK(std::string(weaponGraphNodeOutputName("gate.weapon.has_ammo", 0)) == "pass");
	CHECK(std::string(weaponGraphNodeOutputName("gate.weapon.has_ammo", 1)) == "fail");
	CHECK(weaponGraphNodeOutputName("gate.weapon.has_ammo", 2) == nullptr);
	CHECK(weaponGraphNodeTakesExec("og.weapon.automatic") == 1);
	CHECK(weaponGraphNodeOutputCount("og.weapon.automatic") == 1);
	CHECK(std::string(weaponGraphNodeOutputName("og.weapon.automatic", 0)) == "out");
	CHECK(weaponGraphNodeIsProgram("fire.hitscan") == 0);
	CHECK(weaponGraphNodeTakesExec("fire.hitscan") == 1);
	CHECK(weaponGraphNodeOutputCount("fire.hitscan") == 1);
	CHECK(std::string(weaponGraphNodeOutputName("fire.hitscan", 0)).empty());
	CHECK(weaponGraphNodeOutputName("fire.hitscan", -1) == nullptr);

	// The canvas draws each node's own ports and links a gate's branches
	// apart; the hub lists the designer nodes first and keeps the port of a
	// link it adds.
	const std::string canvas = ioReadFile(fs::path(PD_SOURCE_DIR) / "port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	REQUIRE(canvas.find("ed::BeginPin(nodePinId(node, row == 0 ? 2 : 5), ed::PinKind::Output);") != std::string::npos);
	REQUIRE(canvas.find("if (row == 0 && takesExec) {") != std::string::npos);
	REQUIRE(canvas.find("nodePinId(model->nodes[edge.from], edgeStartPinKind(edge)),") != std::string::npos);
	REQUIRE(canvas.find("edgePortFor(model->nodes[fromNode].kind, startKind, result->port, sizeof(result->port));")
		!= std::string::npos);
	REQUIRE(canvas.find("\"Program nodes link only to program nodes\"") != std::string::npos);
	const std::string hub = ioReadFile(fs::path(PD_SOURCE_DIR) / "port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(hub.find("desc.modules = weaponGraphPalette().data();") != std::string::npos);
	REQUIRE(hub.find("weaponGraphBuilderAddEdge(result.a, result.b, false, result.port);") != std::string::npos);
	REQUIRE(hub.find("row.group = \"Function record\";") != std::string::npos);
	CHECK(hub.find("desc.modules = s_WeaponGraphModules;") == std::string::npos);
}

// held-weapon-events.md slice 8a: the inspector's named inputs for a
// program node are the runtime's own settings, and each choice it offers is
// one the runtime takes.
TEST_CASE("the creator's named inputs are the rule tables' settings", "[weapon-graph][editor][held-weapon]")
{
	weapon_graph_designer_setting_t settings[32];
	for (s32 m = 0; m < WEAPON_MODULE_COUNT; m++) {
		const u32 bit = 1u << m;
		const char *kind = weaponRulesModuleKind(bit);
		INFO(kind);
		const s32 count = weaponGraphDesignerSettings(kind, settings, 32);
		REQUIRE(count <= 32);
		CHECK(weaponGraphDesignerSettings(kind, nullptr, 0) == count);
		std::set<std::string> listed;
		for (s32 i = 0; i < count; i++) {
			CHECK(listed.insert(settings[i].key).second);
			CHECK(settings[i].label[0] != '\0');
		}
		for (s32 i = 0; weaponRulesSettingAt(i); i++) {
			const weapon_rule_setting_t *rule = weaponRulesSettingAt(i);
			if (rule->module != bit) continue;
			INFO(rule->key);
			CHECK(listed.count(rule->key) == 1);
			for (s32 j = 0; j < count; j++) {
				if (std::string(settings[j].key) != rule->key) continue;
				const weapon_graph_designer_setting_t &s = settings[j];
				if (rule->type == WEAPON_RULE_SETTING_WHOLE) {
					CHECK(s.input == WEAPON_GRAPH_INPUT_WHOLE);
					CHECK(weaponRulesSettingValid(rule, std::to_string((int)s.lo).c_str()) == 1);
					CHECK(weaponRulesSettingValid(rule, std::to_string((int)s.hi).c_str()) == 1);
				} else if (rule->type == WEAPON_RULE_SETTING_BOOL) {
					CHECK(s.input == WEAPON_GRAPH_INPUT_SWITCH);
				} else if (s.input == WEAPON_GRAPH_INPUT_NUMBER) {
					char hi[32];
					std::snprintf(hi, sizeof(hi), "%g", (double)s.hi);
					CHECK(weaponRulesSettingValid(rule, hi) == 1);
					CHECK(weaponRulesSettingValid(rule, "0") == (s.lo_included ? 1 : 0));
				} else {
					CHECK((s.input == WEAPON_GRAPH_INPUT_CHOICE || s.input == WEAPON_GRAPH_INPUT_ID));
					REQUIRE(s.choices != nullptr);
					std::string word;
					for (const char *p = s.choices; ; p++) {
						if (*p == '|' || *p == '\0') {
							INFO(word);
							CHECK(weaponRulesSettingValid(rule, word.c_str()) == 1);
							word.clear();
							if (*p == '\0') break;
						} else {
							word += *p;
						}
					}
					if (s.input == WEAPON_GRAPH_INPUT_ID) {
						CHECK(weaponRulesSettingValid(rule, "base:test_id") == 1);
					}
				}
			}
		}
	}
	// A gate's, set_state's and the lock's own.
	REQUIRE(weaponGraphDesignerSettings("gate.weapon.has_ammo", settings, 32) == 1);
	CHECK(std::string(settings[0].key) == "rounds");
	CHECK(settings[0].lo == 1.0f);
	REQUIRE(weaponGraphDesignerSettings("gate.weapon.state_is", settings, 32) == 2);
	CHECK(std::string(settings[0].key) == "key");
	CHECK(std::string(settings[1].key) == "equals");
	REQUIRE(weaponGraphDesignerSettings("og.weapon.set_state", settings, 32) == 2);
	CHECK(std::string(settings[1].key) == "value");
	const s32 lock = weaponGraphDesignerSettings("og.weapon.lock_on", settings, 32);
	std::set<std::string> lockKeys;
	for (s32 i = 0; i < lock; i++) lockKeys.insert(settings[i].key);
	CHECK(lockKeys == std::set<std::string>{ "locks", "aim_follows", "kinds", "sight_targets_only", "vehicles",
		"relation", "alive", "cloaked" });
	CHECK(weaponGraphDesignerSettings("event.weapon.trigger_pressed", settings, 32) == 0);
	CHECK(weaponGraphDesignerSettings("gate.weapon.ready_to_fire", settings, 32) == 0);
	CHECK(weaponGraphDesignerSettings("fire.hitscan", settings, 32) == 0);
	CHECK(weaponGraphDesignerSettings(nullptr, settings, 32) == 0);

	// The lock's filter choices are the ones the lock's settings parse.
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	for (s32 i = 0; i < lock; i++) {
		if (settings[i].input != WEAPON_GRAPH_INPUT_CHOICE && settings[i].input != WEAPON_GRAPH_INPUT_KINDS) continue;
		std::string word;
		for (const char *p = settings[i].choices; ; p++) {
			if (*p == '|' || *p == '\0') {
				Texts texts;
				texts.values = { { settings[i].key, word } };
				weapon_rule_lock_t parsed;
				INFO(settings[i].key << "=" << word);
				CHECK(weaponRulesApplyLockSettings(Texts::get, &texts, &parsed) == 1);
				word.clear();
				if (*p == '\0') break;
			} else {
				word += *p;
			}
		}
	}

	// The inspector edits a program node's settings by these inputs and
	// leaves the record's params editor to record nodes.
	const std::string canvas = ioReadFile(fs::path(PD_SOURCE_DIR) / "port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	REQUIRE(canvas.find("const s32 count = weaponGraphDesignerSettings(node.kind, settings, 32);") != std::string::npos);
	REQUIRE(canvas.find("renderProgramSettings(node, result);") != std::string::npos);
	REQUIRE(canvas.find("ImGui::TextDisabled(\"%s: the function's own\", setting.label);") != std::string::npos);
}

// held-weapon-events.md slice 8a: Mike's first example as the creator
// builds it: the Falcon 2's primary program, its Fire once replaced by an
// Automatic set to 600 rounds a minute with no spin-up (the node the hub
// writes: its id from the new kind, empty params, the two settings), is a
// weapon the game takes.
TEST_CASE("the creator's machine-gun pistol: Fire once replaced by Automatic", "[weapon-graph][editor][held-weapon]")
{
	const std::string primary = R"({
  "schema": "pd.weapon_graph.v2",
  "asset_id": "mod:machine_pistol",
  "graph_id": "primary",
  "nodes": [
    { "id": "primary_trigger", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary", "trigger": "pressed" } },
    { "id": "primary_action", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_single", "ammo_slot": 0, "damage": 1, "spread": 2 } },
    { "id": "primary_pressed", "kind": "event.weapon.trigger_pressed" },
    { "id": "primary_held", "kind": "event.weapon.ready_while_held" },
    { "id": "primary_has_ammo", "kind": "gate.weapon.has_ammo", "rounds": 1 },
    { "id": "primary_has_reserve", "kind": "gate.weapon.has_reserve" },
    { "id": "primary_attack", "kind": "og.weapon.fire_once" },
    { "id": "primary_dry_fire", "kind": "og.weapon.dry_fire" },
    { "id": "primary_use_ammo", "kind": "og.weapon.use_ammo", "rounds": 1 },
    { "id": "primary_shot", "kind": "og.weapon.hitscan" }
  ],
  "edges": [
    { "from": "primary_trigger", "to": "primary_action" },
    { "from": "primary_pressed", "output": "out", "to": "primary_has_ammo", "input": "exec" },
    { "from": "primary_held", "output": "out", "to": "primary_has_ammo", "input": "exec" },
    { "from": "primary_has_ammo", "output": "pass", "to": "primary_attack", "input": "exec" },
    { "from": "primary_attack", "output": "out", "to": "primary_use_ammo", "input": "exec" },
    { "from": "primary_use_ammo", "output": "out", "to": "primary_shot", "input": "exec" },
    { "from": "primary_has_ammo", "output": "fail", "to": "primary_has_reserve", "input": "exec" },
    { "from": "primary_has_reserve", "output": "pass", "to": "primary_dry_fire", "input": "exec" }
  ],
  "exports": [ { "name": "primary", "node": "primary_action" } ]
})";
	const std::string secondary = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "mod:machine_pistol",
  "graph_id": "secondary",
  "nodes": [
    { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary", "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1 } }
  ],
  "edges": [],
  "exports": [ { "name": "secondary", "node": "secondary_action" } ]
})";
	char err[256] = "";
	IoText editor(weaponGraphEditorCompose(primary.c_str(), secondary.c_str(), nullptr, "mod:machine_pistol",
		err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(editor.get() != nullptr);
	std::string joined = editor.get();
	REQUIRE(weaponGraphValidateJson(ASSET_WEAPON, joined.data(), static_cast<u32>(joined.size()), err, sizeof(err)) == 0);
	// Fire once and Automatic are of one group, so Replace offers it.
	REQUIRE(weaponGraphDesignerNodeFor("og.weapon.fire_once") != nullptr);
	CHECK(std::string(weaponGraphDesignerNodeFor("og.weapon.fire_once")->group)
		== weaponGraphDesignerNodeFor("og.weapon.automatic")->group);
	const std::string attack = "\"id\": \"primary_attack\"";
	const size_t at = joined.find(attack);
	REQUIRE(at != std::string::npos);
	// The composed node ends with its empty params: "params": {} }.
	const std::string tail = "\"params\": {} }";
	const size_t start = joined.rfind('{', at);
	const size_t end = joined.find(tail, at);
	REQUIRE(start != std::string::npos);
	REQUIRE(end != std::string::npos);
	joined.replace(start, end + tail.size() - start,"{ \"id\": \"og_weapon_automatic_11\", \"kind\": \"og.weapon.automatic\", "
		"\"subgraph\": \"primary\", \"params\": {}, \"rate\": 600, \"spin_up\": 0 }");
	for (const char *ref : { "\"from\": \"primary_attack\"", "\"to\": \"primary_attack\"" }) {
		const std::string from = ref;
		const size_t edge = joined.find(from);
		REQUIRE(edge != std::string::npos);
		std::string renamed = from;
		renamed.replace(renamed.find("primary_attack"), std::strlen("primary_attack"), "og_weapon_automatic_11");
		joined.replace(edge, from.size(), renamed);
	}
	INFO(joined);
	REQUIRE(weaponGraphValidateJson(ASSET_WEAPON, joined.data(), static_cast<u32>(joined.size()), err, sizeof(err)) == 0);
	IoSplit split(joined.c_str(), "mod:machine_pistol");
	INFO(split.err);
	REQUIRE(split.ok == 1);
	const std::string written = split.files.primary;
	CHECK(written.find("\"kind\": \"og.weapon.automatic\"") != std::string::npos);
	CHECK(written.find("\"rate\": 600") != std::string::npos);
	CHECK(written.find("og.weapon.fire_once") == std::string::npos);
	IoGraph graph;
	graph.compileSource("mod:machine_pistol", written, split.files.secondary, "");
	INFO(graph.err);
	REQUIRE(graph.ok);
	REQUIRE(graph.ir->held_program_json[0] != nullptr);
	CHECK(std::string(graph.ir->held_program_json[0]).find("og.weapon.automatic") != std::string::npos);

	// The hub keeps a replaced node's links and place, drops its settings,
	// and makes the new id apart from the node's own; a duplicate keeps its
	// settings.
	const std::string hub = ioReadFile(fs::path(PD_SOURCE_DIR) / "port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(hub.find("weaponGraphBuilderMakeNodeId(module.kind, id, sizeof(id));") != std::string::npos);
	REQUIRE(hub.find("weaponGraphBuilderReplaceNode(result.a, result.b);") != std::string::npos);
	REQUIRE(hub.find("strncpy(node.extra, src.extra, sizeof(node.extra) - 1);") != std::string::npos);
	const std::string canvas = ioReadFile(fs::path(PD_SOURCE_DIR) / "port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	REQUIRE(canvas.find("outputAction(result, PD_WEAPON_GRAPH_EDITOR_ACTION_REPLACE_NODE, nodeIndex, i);")
		!= std::string::npos);
}

// held-weapon-events.md slice 8b: a projectile's designer nodes are exactly
// the program nodes its runtime runs, with their ports, and each setting the
// inspector offers parses as the projectile's own settings do.
TEST_CASE("the creator's projectile designer nodes are the runtime's, with settings it parses",
	"[weapon-graph][editor][held-weapon]")
{
	std::set<std::string> runtime;
	for (s32 m = 0; m < PROJECTILE_MODULE_COUNT; m++) runtime.insert(projectileRulesModuleKind(1u << m));
	for (s32 g = 0; g < PROJECTILE_GATE_COUNT; g++) runtime.insert(projectileRulesGateKind(PROJECTILE_RULE_GATE_FUSE_ARMED << g));
	for (s32 e = 0; projectileRulesEventAt(e) >= 0; e++) runtime.insert(projectileRulesEventName(projectileRulesEventAt(e)));
	const std::set<std::string> groups = { "Events", "Checks", "Arm", "Flight", "Impact" };
	std::set<std::string> designer;
	for (s32 i = 0; i < weaponGraphProjectileDesignerNodeCount(); i++) {
		const weapon_graph_designer_node_t *node = weaponGraphProjectileDesignerNodeAt(i);
		REQUIRE(node != nullptr);
		INFO(node->kind);
		CHECK(designer.insert(node->kind).second);
		CHECK(node->label[0] != '\0');
		CHECK(groups.count(node->group) == 1);
		CHECK(weaponGraphDesignerNodeFor(node->kind) == node);
		CHECK(weaponGraphNodeIsProgram(node->kind) == 1);
	}
	CHECK(designer == runtime);
	CHECK(weaponGraphProjectileDesignerNodeAt(weaponGraphProjectileDesignerNodeCount()) == nullptr);
	// Ports: a projectile event has no input, a gate leaves by pass or fail.
	CHECK(weaponGraphNodeTakesExec("event.projectile.in_flight") == 0);
	CHECK(std::string(weaponGraphNodeOutputName("event.projectile.in_flight", 0)) == "out");
	CHECK(weaponGraphNodeOutputCount("gate.projectile.damage_not_blast") == 2);
	CHECK(std::string(weaponGraphNodeOutputName("gate.projectile.damage_not_blast", 1)) == "fail");
	CHECK(weaponGraphNodeTakesExec("og.projectile.home") == 1);
	CHECK(weaponGraphNodeIsProgram("projectile.homing") == 0);

	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	weapon_graph_designer_setting_t settings[32];
	for (s32 m = 0; m < PROJECTILE_MODULE_COUNT; m++) {
		const u32 bit = 1u << m;
		const char *kind = projectileRulesModuleKind(bit);
		const s32 count = weaponGraphDesignerSettings(kind, settings, 32);
		REQUIRE(count <= 32);
		for (s32 i = 0; i < count; i++) {
			const weapon_graph_designer_setting_t &s = settings[i];
			INFO(kind << " " << s.key);
			std::vector<std::string> values;
			if (s.input == WEAPON_GRAPH_INPUT_CHOICE || s.input == WEAPON_GRAPH_INPUT_KINDS) {
				std::string word;
				for (const char *p = s.choices; ; p++) {
					if (*p == '|' || *p == '\0') {
						values.push_back(word);
						word.clear();
						if (*p == '\0') break;
					} else {
						word += *p;
					}
				}
			} else if (s.input == WEAPON_GRAPH_INPUT_SWITCH) {
				values = { "true", "false" };
			} else if (s.input == WEAPON_GRAPH_INPUT_WHOLE) {
				values = { std::to_string((int)s.lo), std::to_string((int)s.hi) };
			} else if (s.input == WEAPON_GRAPH_INPUT_NUMBER) {
				char hi[32];
				std::snprintf(hi, sizeof(hi), "%g", (double)s.hi);
				values = { hi };
			}
			REQUIRE_FALSE(values.empty());
			for (const std::string &value : values) {
				INFO(value);
				Texts texts;
				texts.values = { { s.key, value } };
				if (bit == PROJECTILE_MODULE_EXPLODE) {
					const s32 type = projectileRulesExplosionFromText(value.c_str());
					CHECK(type != PROJECTILE_EXPLOSION_OWN);
					CHECK(type != PROJECTILE_EXPLOSION_REFUSED);
				} else if (bit == PROJECTILE_MODULE_ARM_PROXIMITY) {
					projectile_arm_t arm;
					std::memset(&arm, 0, sizeof(arm));
					CHECK(projectileRulesArmSettings(bit, Texts::get, &texts, &arm, nullptr) == 1);
				} else {
					projectile_flight_t flight;
					projectileRulesNativeFlight(WEAPON_HOMINGROCKET, FUNC_PRIMARY, &flight);
					CHECK(projectileRulesFlightSettings(bit, Texts::get, &texts, &flight, nullptr) == 1);
				}
			}
		}
	}
	CHECK(weaponGraphDesignerSettings("og.projectile.thrust", settings, 32) == 2);
	CHECK(weaponGraphDesignerSettings("og.projectile.home", settings, 32) == 7);
	CHECK(weaponGraphDesignerSettings("og.projectile.arm_proximity", settings, 32) == 6);
	CHECK(weaponGraphDesignerSettings("og.projectile.woosh", settings, 32) == 0);
	CHECK(weaponGraphDesignerSettings("projectile.homing", settings, 32) == 0);
}

// held-weapon-events.md slice 8b: Mike's second example's rocket half, as
// the creator edits it: the homing rocket's home node gets the "Enemy
// vehicles" filter through its settings, everything else in its program
// stays, and the program still compiles with settings the home parses.
TEST_CASE("the creator sets a projectile node's settings in its program's text", "[weapon-graph][editor][held-weapon]")
{
	const std::string graph = R"({
  "schema": "pd.projectile_graph.v2",
  "nodes": [
    { "id": "motion", "kind": "projectile.motion", "params": {} },
    { "id": "in_flight", "kind": "event.projectile.in_flight" },
    { "id": "in_flight_home", "kind": "og.projectile.home" },
    { "id": "in_flight_trail", "kind": "og.projectile.trail", "smoke": "homing", "while": "flying" }
  ],
  "edges": [
    { "from": "in_flight", "output": "out", "to": "in_flight_home", "input": "exec" },
    { "from": "in_flight_home", "output": "out", "to": "in_flight_trail", "input": "exec" }
  ]
})";
	char err[256] = "";
	IoText home(weaponGraphEditorNodeSettings(graph.c_str(), "in_flight_home", err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(home.get() != nullptr);
	CHECK(std::string(home.get()).empty());
	IoText trail(weaponGraphEditorNodeSettings(graph.c_str(), "in_flight_trail", err, sizeof(err)), &std::free);
	REQUIRE(trail.get() != nullptr);
	CHECK(std::string(trail.get()) == "\"smoke\": \"homing\", \"while\": \"flying\"");
	CHECK(weaponGraphEditorNodeSettings(graph.c_str(), "nothing", err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("the graph has no node nothing") != std::string::npos);

	IoText edited(weaponGraphEditorSetNodeSettings(graph.c_str(), "in_flight_home",
		"\"kinds\": \"vehicle\", \"relation\": \"enemy\", \"vehicles\": \"any\"", err, sizeof(err)), &std::free);
	INFO(err);
	REQUIRE(edited.get() != nullptr);
	CHECK(ioStrict(edited.get()));
	IoText after(weaponGraphEditorNodeSettings(edited.get(), "in_flight_home", err, sizeof(err)), &std::free);
	REQUIRE(after.get() != nullptr);
	CHECK(std::string(after.get()) == "\"kinds\": \"vehicle\", \"relation\": \"enemy\", \"vehicles\": \"any\"");
	IoText trailAfter(weaponGraphEditorNodeSettings(edited.get(), "in_flight_trail", err, sizeof(err)), &std::free);
	REQUIRE(trailAfter.get() != nullptr);
	CHECK(std::string(trailAfter.get()) == std::string(trail.get()));
	CHECK(std::string(edited.get()).find("\"kind\": \"projectile.motion\"") != std::string::npos);
	// The home parses them as the Enemy vehicles filter.
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	Texts texts;
	texts.values = { { "kinds", "vehicle" }, { "relation", "enemy" }, { "vehicles", "any" } };
	projectile_flight_t flight;
	projectileRulesNativeFlight(WEAPON_HOMINGROCKET, FUNC_PRIMARY, &flight);
	REQUIRE(projectileRulesFlightSettings(PROJECTILE_MODULE_HOME, Texts::get, &texts, &flight, nullptr) == 1);
	CHECK(flight.home_filtered == 1);
	// The edited program still compiles.
	const auto known = [](const char *kind) -> s32 {
		return projectileRulesModuleBit(kind) != 0 || projectileRulesGateBit(kind) != 0;
	};
	mission_graph_program_t *program = missionGraphProgramCompileProjectile(edited.get(), std::strlen(edited.get()),
		known, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	missionGraphProgramFree(program);
	// Settings cleared, and settings that would rename the node refused.
	IoText cleared(weaponGraphEditorSetNodeSettings(edited.get(), "in_flight_home", "", err, sizeof(err)), &std::free);
	REQUIRE(cleared.get() != nullptr);
	IoText none(weaponGraphEditorNodeSettings(cleared.get(), "in_flight_home", err, sizeof(err)), &std::free);
	REQUIRE(none.get() != nullptr);
	CHECK(std::string(none.get()).empty());
	CHECK(weaponGraphEditorSetNodeSettings(graph.c_str(), "in_flight_home", "\"id\": \"other\"", err, sizeof(err))
		== nullptr);
	CHECK(std::string(err).find("a setting cannot be named id") != std::string::npos);
	CHECK(weaponGraphEditorSetNodeSettings(graph.c_str(), "in_flight_home", "\"kinds\": ", err, sizeof(err))
		== nullptr);

	// The creator shows these settings with the inspector's inputs and saves
	// each edited program over its projectile's behavior graph, once it
	// checks out.
	const std::string hub = ioReadFile(fs::path(PD_SOURCE_DIR) / "port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(hub.find("projectileEntries.push_back(program.entry + \"::behavior.graph.json\");") != std::string::npos);
	REQUIRE(hub.find("if (pdguiWeaponGraphRenderNodeSettings(kind.c_str(), extra, sizeof(extra))) {") != std::string::npos);
	REQUIRE(hub.find("return weaponSaveFail(\"PROJECTILE_FAIL\", msg);") != std::string::npos);
	REQUIRE(hub.find("weaponLoadProjectilePrograms(s_WeaponEditTemplateArchive); /* slice 8b */") != std::string::npos);
}

// PR 49 review: a target filter's kinds group checks the kinds it names, so
// an edit in the inspector keeps them.
TEST_CASE("the creator's kinds checkboxes expand a filter's kind groups", "[weapon-graph][editor][held-weapon]")
{
	const char *const choices = "player|simulant|guard|vehicle|sentry|object";
	CHECK(weaponGraphDesignerKindsMask("characters", choices) == 0x07u);
	CHECK(weaponGraphDesignerKindsMask("things", choices) == 0x38u);
	CHECK(weaponGraphDesignerKindsMask("all", choices) == 0x3fu);
	CHECK(weaponGraphDesignerKindsMask("player,vehicle", choices) == 0x09u);
	CHECK(weaponGraphDesignerKindsMask("characters, vehicle", choices) == 0x0fu);
	CHECK(weaponGraphDesignerKindsMask("bogus", choices) == 0u);
	CHECK(weaponGraphDesignerKindsMask(nullptr, choices) == 0u);
	// The lock's and the home's kinds setting offers these choices.
	weapon_graph_designer_setting_t settings[32];
	const s32 count = weaponGraphDesignerSettings("og.weapon.lock_on", settings, 32);
	bool found = false;
	for (s32 i = 0; i < count; i++) {
		if (std::string(settings[i].key) == "kinds") {
			CHECK(std::string(settings[i].choices) == choices);
			found = true;
		}
	}
	CHECK(found);
	const std::string canvas = ioReadFile(fs::path(PD_SOURCE_DIR) / "port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	REQUIRE(canvas.find("const u32 checked = weaponGraphDesignerKindsMask(current.c_str(), setting.choices);")
		!= std::string::npos);
	CHECK(canvas.find("const bool all = current == \"all\";") == std::string::npos);
}
