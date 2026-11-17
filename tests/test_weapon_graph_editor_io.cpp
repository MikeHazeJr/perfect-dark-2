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
#include <memory>
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
