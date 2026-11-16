import io
import json
from pathlib import Path
import sys
import unittest
import zipfile

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
from asset_archive_conformance import (
    AI_COMMAND_SCHEMA,
    ARENA_SCENARIO_GRAPH_CACHE_KIND, FAMILIES_NOT_YET_REQUIRED,
    TYPED_DESCRIPTORS, ai_list_control_flow, ai_list_edge_errors,
    validate_archive_bytes,
)


def ai_row(list_id, index, opcode_name="end"):
    return {
        "ailist_ref": f"global_{list_id:04x}",
        "list_id": f"0x{list_id:04x}",
        "graph_node": f"scenario.ai.global_{list_id:04x}.command.{index:04d}",
        "command_index": index, "offset": index * 2, "opcode": "0x0004",
        "opcode_name": opcode_name, "operands": [],
        "model_catalog_id": "", "weapon_catalog_id": "",
        "body_catalog_id": "", "head_catalog_id": "",
    }


GOOD_INI = ("[ai_lists]\ncatalog_id = base:ai_global\n"
            "ai_lists_file = ai/global_ailists.json\n")


class GlobalAiListsConformanceTests(unittest.TestCase):
    """context/designs/modding/global-ai-lists.md: the .pdai family."""

    def bank(self, ini=GOOD_INI, rows=None):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            if ini is not None:
                archive.writestr("ai.ini", ini)
            archive.writestr("ai/global_ailists.json", json.dumps({
                "schema": "pd2.scenario.ai.lists.v1",
                "rows": rows if rows is not None else [ai_row(0x0000, 0),
                                                       ai_row(0x002d, 0)],
            }))
            archive.writestr("_meta/manifest.json", json.dumps({
                "pd_kind": "ai_lists", "pd_schema_version": 1,
                "id": "base:ai_global"}))
        return validate_archive_bytes(data.getvalue(), "fixture.pdai", ".pdai").errors

    def test_family_is_known_but_not_yet_required_of_every_root(self):
        self.assertEqual(TYPED_DESCRIPTORS[".pdai"], "ai.ini")
        self.assertIn(".pdai", FAMILIES_NOT_YET_REQUIRED)

    def test_global_bank_passes(self):
        self.assertEqual(self.bank(), [])

    def test_rows_hold_only_global_list_ids(self):
        for list_id in (0x0401, 0x1000):
            with self.subTest(list_id=list_id):
                self.assertTrue(self.bank(rows=[ai_row(list_id, 0)]))

    def test_descriptor_names_the_bank_and_its_rows(self):
        for ini in (None,
                    GOOD_INI.replace("[ai_lists]", "[lang]"),
                    GOOD_INI.replace("base:ai_global", "ai_global"),
                    GOOD_INI.replace("ai/global_ailists.json", "rows.json")):
            with self.subTest(ini=ini):
                self.assertTrue(self.bank(ini=ini))

    def test_rows_keep_named_commands(self):
        self.assertTrue(self.bank(rows=[ai_row(0, 0, opcode_name="command")]))


# context/designs/modding/ai-list-graphs.md, slice 1: the control-flow edges
# the conformance tool recomputes from the rows (port/src/ai_list_edges.c is
# the same in C; tests/test_ai_list_edges.cpp keeps the two tables equal).
GO_TO_NEXT, GO_TO_FIRST, LABEL, YIELD, END, SET_LIST, RETURN = (
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0008)
IF_IDLE = 0x000C            # cmd[2] = label
IF_TELEPORT_WHITE = 0x01C0  # cmd[2] = label, cmd[3] = chr
IF_TIMER_GREATER_THAN = 0x00BD

# Defection's list 0x0401 as its rows hold it.
DEFECTION_0401 = [
    (0x00B6, []),
    (0x000B, [0x00, 0x66, 0x00, 0x00, 0x00, 0x01, 0x04, 0x10, 0xFD, 0x02]),
    (LABEL, [0x08]),
    (YIELD, []),
    (IF_TIMER_GREATER_THAN, [0x00, 0x00, 0x00, 0x2C]),
    (GO_TO_FIRST, [0x08]),
    (LABEL, [0x2C]),
    (0x000B, [0x00, 0x66, 0x00, 0x01, 0xFF, 0xFF, 0x04, 0x10, 0xFD, 0x02]),
    (LABEL, [0x85]),
    (YIELD, []),
    (GO_TO_FIRST, [0x85]),
    (SET_LIST, [0xFD, 0x00, 0x00]),
    (END, []),
]

# Repeated labels, a forward-only scan, a missing label, 0x01c0's label byte.
HAND_MADE = [
    (LABEL, [5]),                 # 0
    (IF_IDLE, [5]),               # 1 branch -> 3 (label 5 at or after itself)
    (GO_TO_NEXT, [5]),            # 2 goto -> 3
    (LABEL, [5]),                 # 3
    (YIELD, []),                  # 4
    (IF_TELEPORT_WHITE, [9, 5]),  # 5 branch -> 7 (cmd[2]; cmd[3] would restart)
    (IF_IDLE, [4]),               # 6 no label 4 after it: restart -> 0
    (LABEL, [9]),                 # 7
    (GO_TO_FIRST, [5]),           # 8 goto -> 0 (from the top)
    (RETURN, []),                 # 9 unreachable
    (END, []),                    # 10 unreachable
]


def list_rows(list_id, commands):
    rows, offset = [], 0
    for index, (opcode, operands) in enumerate(commands):
        # Real commands: conformance checks each row against the command
        # schema (ai-typed-operands.md), so the name is the schema's.
        row = ai_row(list_id, index, opcode_name=AI_COMMAND_SCHEMA[opcode][0])
        row.update(offset=offset, opcode=f"0x{opcode:04x}",
                   operands=[f"0x{value:02x}" for value in operands])
        rows.append(row)
        offset += 2 + len(operands)
    return rows


def list_graph(rows, commands):
    """The nodes and links the extractor writes for one list."""
    edges, flags = ai_list_control_flow(commands)
    nodes = []
    for row, node_flags in zip(rows, flags):
        node = {"id": row["graph_node"], "kind": "scenario.ai.command",
                "source": "ai/global_ailists.json",
                "semantic_kind": "scenario.ai.action.example"}
        node.update({key: row[key] for key in (
            "ailist_ref", "list_id", "command_index", "offset", "opcode",
            "opcode_name")})
        for flag in ("yields", "ends", "returns", "switches_list"):
            if flag in node_flags:
                node[flag] = True
        if "reachable" not in node_flags:
            node["reachable"] = False
        nodes.append(node)
    links = [{"from": rows[source]["graph_node"], "to": rows[target]["graph_node"],
              "kind": kind} for source, target, kind in edges]
    return nodes, links


class AiListEdgeRuleTests(unittest.TestCase):
    def test_defection_list(self):
        edges, flags = ai_list_control_flow(DEFECTION_0401)
        self.assertEqual(edges, [
            (0, 1, "next"), (1, 2, "next"), (2, 3, "next"), (3, 4, "next"),
            (4, 5, "next"), (4, 6, "branch"), (5, 2, "goto"), (6, 7, "next"),
            (7, 8, "next"), (8, 9, "next"), (9, 10, "next"), (10, 8, "goto"),
        ])
        self.assertEqual([i for i, f in enumerate(flags) if "yields" in f], [3, 9])
        self.assertEqual([i for i, f in enumerate(flags) if "reachable" not in f],
                         [11, 12])
        self.assertEqual(flags[11], {"switches_list"})
        self.assertEqual(flags[12], {"ends"})

    def test_hand_made_list(self):
        edges, flags = ai_list_control_flow(HAND_MADE)
        self.assertEqual(edges, [
            (0, 1, "next"), (1, 2, "next"), (1, 3, "branch"), (2, 3, "goto"),
            (3, 4, "next"), (4, 5, "next"), (5, 6, "next"), (5, 7, "branch"),
            (6, 7, "next"), (6, 0, "restart"), (7, 8, "next"), (8, 0, "goto"),
        ])
        self.assertEqual(flags[4], {"yields", "reachable"})
        self.assertEqual(flags[9], {"returns"})
        self.assertEqual(flags[10], {"ends"})
        self.assertTrue(all("reachable" in f for f in flags[:9]))

    def test_set_list_on_another_chr_continues(self):
        edges, flags = ai_list_control_flow(
            [(SET_LIST, [0x05, 0x04, 0x01]), (SET_LIST, [0xFD, 0x04, 0x02]), (END, [])])
        self.assertEqual(edges, [(0, 1, "next")])
        self.assertEqual(flags[1], {"switches_list", "reachable"})
        self.assertEqual(flags[2], {"ends"})

    def test_missing_operand_is_refused(self):
        with self.assertRaises(ValueError):
            ai_list_control_flow([(YIELD, []), (IF_IDLE, []), (END, [])])

    def test_graph_matches_rows(self):
        rows = list_rows(0x0010, HAND_MADE)
        nodes, links = list_graph(rows, HAND_MADE)
        self.assertEqual(ai_list_edge_errors(rows, nodes, links, require=True), [])

    def test_differences_name_the_list_and_the_command(self):
        rows = list_rows(0x0010, HAND_MADE)
        nodes, links = list_graph(rows, HAND_MADE)
        branch = next(link for link in links if link["kind"] == "branch")
        links.remove(branch)
        links.append({"from": rows[8]["graph_node"], "to": rows[9]["graph_node"],
                      "kind": "next"})
        nodes[4].pop("yields")
        nodes[10].pop("reachable")
        errors = ai_list_edge_errors(rows, nodes, links, require=False)
        where = "AI list global_0010 (0x0010) command"
        self.assertIn(f"{where} 1 (scenario.ai.global_0010.command.0001): "
                      "missing branch edge to command 3", errors)
        self.assertIn(f"{where} 8 (scenario.ai.global_0010.command.0008): "
                      "unexpected next edge to command 9", errors)
        self.assertIn(f"{where} 4 (scenario.ai.global_0010.command.0004): "
                      "yields must be true", errors)
        self.assertIn(f"{where} 10 (scenario.ai.global_0010.command.0010): "
                      "reachable must be false", errors)
        self.assertEqual(len(errors), 4)

    def test_a_graph_without_edges_is_checked_only_when_its_kind_has_them(self):
        rows = list_rows(0x0010, HAND_MADE)
        nodes = [{"id": row["graph_node"], "kind": "scenario.ai.command"}
                 for row in rows]
        self.assertEqual(ai_list_edge_errors(rows, nodes, [], require=False), [])
        self.assertTrue(ai_list_edge_errors(rows, nodes, [], require=True))


class GlobalAiListsGraphTests(unittest.TestCase):
    NEW_INI = (GOOD_INI + "ai_lists_graph_file = ai/global_ailists.graph.json\n"
               "extract_version = global_ailists_rows_v2_aiedges\n")
    TYPED_INI = (GOOD_INI + "ai_lists_graph_file = ai/global_ailists.graph.json\n"
                 "extract_version = global_ailists_rows_v3_typedargs\n")

    def bank(self, ini, rows, graph):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr("ai.ini", ini)
            archive.writestr("ai/global_ailists.json", json.dumps({
                "schema": "pd2.scenario.ai.lists.v1", "rows": rows}))
            if graph is not None:
                archive.writestr("ai/global_ailists.graph.json", json.dumps(graph))
            archive.writestr("_meta/manifest.json", json.dumps({
                "pd_kind": "ai_lists", "pd_schema_version": 1,
                "id": "base:ai_global"}))
        return validate_archive_bytes(data.getvalue(), "fixture.pdai", ".pdai").errors

    def graph(self, rows, commands):
        nodes, links = list_graph(rows, commands)
        return {"schema": "pd2.scenario.ai.graph.v1", "nodes": nodes, "links": links}

    def test_bank_with_its_graph_passes(self):
        rows = list_rows(0x0010, HAND_MADE) + list_rows(0x0011, [(END, [])])
        graph = self.graph(rows[:len(HAND_MADE)], HAND_MADE)
        tail = self.graph(rows[len(HAND_MADE):], [(END, [])])
        graph["nodes"] += tail["nodes"]
        self.assertEqual(self.bank(self.NEW_INI, rows, graph), [])

    def test_bank_graph_must_match_its_rows(self):
        rows = list_rows(0x0010, HAND_MADE)
        graph = self.graph(rows, HAND_MADE)
        graph["links"] = [link for link in graph["links"] if link["kind"] != "goto"]
        errors = self.bank(self.NEW_INI, rows, graph)
        self.assertTrue(any("missing goto edge to command 3" in error for error in errors))
        graph = self.graph(rows, HAND_MADE)
        graph["nodes"].pop()
        errors = self.bank(self.NEW_INI, rows, graph)
        self.assertTrue(any("has no node for 1 row(s)" in error for error in errors))

    def test_rows_and_nodes_carry_matching_args(self):
        """ai-typed-operands.md slice 2: args decode the operands, nodes copy them."""
        commands = [(0x0033, [0x24, 0x06]), (LABEL, [6]), (END, [])]  # if_chr_dead
        rows = list_rows(0x0010, commands)
        args = [{"chr": 36, "label": 6}, {"label": 6}, {}]
        for row, arg in zip(rows, args):
            row["args"] = arg
        graph = self.graph(rows, commands)
        args_by_node = {row["graph_node"]: row["args"] for row in rows}
        for node in graph["nodes"]:
            if node["id"] in args_by_node:
                node["args"] = args_by_node[node["id"]]
        self.assertEqual(self.bank(self.TYPED_INI, rows, graph), [])
        # A row whose args do not decode its operands.
        bad_rows = [dict(row) for row in rows]
        bad_rows[0]["args"] = {"chr": 37, "label": 6}
        errors = self.bank(self.TYPED_INI, bad_rows, graph)
        self.assertTrue(any("do not match its operands" in error for error in errors))
        # A node whose args are not its row's.
        bad_graph = json.loads(json.dumps(graph))
        bad_graph["nodes"][1]["args"] = {"chr": 36, "label": 7}
        errors = self.bank(self.TYPED_INI, rows, bad_graph)
        self.assertTrue(any("args differ from its row's" in error for error in errors))
        # The typed extract version requires them on every row.
        bare = [{k: v for k, v in row.items() if k != "args"} for row in rows]
        errors = self.bank(self.TYPED_INI, bare, self.graph(bare, commands))
        self.assertTrue(any("3 rows without args" in error for error in errors))

    def test_args_alone_give_the_bytes(self):
        """A row may drop its operands: its args are encoded, and its edges
        come from those bytes (a branch to label 6 here)."""
        commands = [(0x0033, [0x24, 0x06]), (LABEL, [6]), (END, [])]  # if_chr_dead
        rows = list_rows(0x0010, commands)
        graph = self.graph(rows, commands)
        args = [{"chr": 36, "label": 6}, {"label": 6}, {}]
        lean = []
        for row, arg in zip(rows, args):
            row = {k: v for k, v in row.items() if not k.endswith("_catalog_id")}
            del row["operands"]
            row["args"] = arg
            lean.append(row)
        for node in graph["nodes"]:
            node["args"] = next(r["args"] for r in lean if r["graph_node"] == node["id"])
        self.assertEqual(self.bank(self.TYPED_INI, lean, graph), [])
        # Args the schema refuses, and a row with neither form.
        bad = [dict(row) for row in lean]
        bad[0]["args"] = {"chr": 36}
        errors = self.bank(self.TYPED_INI, bad, graph)
        self.assertTrue(any("missing argument 'label'" in error for error in errors))
        bad = [dict(row) for row in lean]
        del bad[1]["args"]
        errors = self.bank(self.TYPED_INI, bad, graph)
        self.assertTrue(any("must be a list of bytes (or the row must have args)" in error
                            for error in errors))

    def test_new_extract_version_requires_the_graph(self):
        rows = list_rows(0x0010, [(END, [])])
        self.assertTrue(self.bank(self.NEW_INI, rows, None))
        self.assertTrue(self.bank(
            GOOD_INI + "extract_version = global_ailists_rows_v2_aiedges\n",
            rows, self.graph(rows, [(END, [])])))
        # A bank from before the edges keeps passing without a graph.
        self.assertEqual(self.bank(
            GOOD_INI + "extract_version = global_ailists_rows_v1\n", rows, None), [])


class ScenarioAiEdgesKindTests(unittest.TestCase):
    """The example arena embeds a scenario whose one list is a single end."""

    ARENA = REPO / "examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena"
    MEMBER = "dependencies/assets/scenarios/tri_scenario.pdscenario"

    def arena_errors(self, strip_flags, kind):
        with zipfile.ZipFile(self.ARENA) as arena:
            entries = {name: arena.read(name) for name in arena.namelist()}
        with zipfile.ZipFile(io.BytesIO(entries[self.MEMBER])) as scenario:
            members = {name: scenario.read(name) for name in scenario.namelist()}
        if strip_flags:
            members["level.graph.json"] = members["level.graph.json"].replace(
                b', "ends": true', b"")
        rebuilt = io.BytesIO()
        with zipfile.ZipFile(rebuilt, "w") as scenario:
            for name, data in members.items():
                scenario.writestr(name, data)
        entries[self.MEMBER] = rebuilt.getvalue()
        entries["arena.ini"] = entries["arena.ini"].replace(
            ARENA_SCENARIO_GRAPH_CACHE_KIND.encode(), kind.encode())
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as arena:
            for name, payload in entries.items():
                arena.writestr(name, payload)
        errors = validate_archive_bytes(data.getvalue(), "tri_arena.pdarena",
                                        ".pdarena").errors
        return [error for error in errors if "AI list" in error]

    def test_the_example_carries_its_edges(self):
        self.assertEqual(self.arena_errors(False, ARENA_SCENARIO_GRAPH_CACHE_KIND), [])

    def test_the_new_kind_requires_them(self):
        errors = self.arena_errors(True, ARENA_SCENARIO_GRAPH_CACHE_KIND)
        self.assertEqual(len(errors), 1)
        self.assertIn("AI list ailist_0000 (0x0000) command 0 "
                      "(scenario.ai.ailist_0000.command.0000): ends must be true",
                      errors[0])

    def test_an_older_kind_does_not(self):
        # The kind before the edges: everything ahead of "_aiedges".
        older = ARENA_SCENARIO_GRAPH_CACHE_KIND.split("_aiedges")[0]
        self.assertNotEqual(older, ARENA_SCENARIO_GRAPH_CACHE_KIND)
        self.assertEqual(self.arena_errors(True, older), [])


if __name__ == "__main__":
    unittest.main()
