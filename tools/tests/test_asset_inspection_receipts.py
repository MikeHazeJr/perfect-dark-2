"""Receipt contract tests with synthetic events only; no game or source corpus."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("receipts", Path(__file__).parents[1] / "asset-inspection-receipts.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)


class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.generation = dict(binary_sha256="b" * 64, source_sha256="c" * 64)
        self.common = dict(schema="pd2.native-asset-event.v1", run_id="run1", attempt_id="model1",
                           catalog_id="base:mesh", archive_origin="data/mesh.pdmesh", archive_sha256="a" * 64,
                           generation=self.generation, operation="modeldef_compile")
        self.events = [self.common | dict(event="begin", timestamp="2026-10-01T10:00:00Z", provider="FileProvider"),
                       self.common | dict(event="member", timestamp="2026-10-01T10:00:01Z", provider="FileProvider",
                                          member="model.obj", sha256="d" * 64, bytes=12, source_role="mesh_source"),
                       self.common | dict(event="end", timestamp="2026-10-01T10:00:02Z", native_completed=True,
                                          load_verdict="pass", rom_fallback=False, checks={"native_draw": "pending"})]

    def convert(self, events=None, **limits):
        return m.convert(self.events if events is None else events, self.generation, "e" * 64, **limits)

    def test_member_contract_keeps_visual_checks_pending(self):
        traces, pending = self.convert()
        self.assertFalse(pending)
        self.assertEqual(traces[0]["consumed_members"], {"model.obj": "d" * 64})
        self.assertEqual(traces[0]["checks"], {"native_draw": "pending"})
        self.assertEqual(traces[0]["event_lines"], [1, 2, 3])

    def test_hydration_log_cannot_be_converted(self):
        with self.assertRaises(ValueError):
            self.convert([{"message": "ASSET.SOURCE.CHARACTER: result=PASS"}])

    def test_native_closure_and_member_sizes_preserved(self):
        self.events[-1]['native_source_closure_sha256'] = 'f' * 64
        trace = self.convert()[0][0]
        self.assertEqual(trace['native_source_closure_sha256'], 'f' * 64)
        self.assertEqual(trace['consumed_member_sizes'], {'model.obj': 12})
        self.events[-1]['native_source_closure_sha256'] = 'wrong'
        with self.assertRaises(ValueError): self.convert()

    def test_cross_archive_dependency_proof_stays_pending(self):
        self.events[-1]['pending_dependencies'] = ['base:texture_a']
        self.assertEqual(self.convert()[0][0]['pending_dependencies'], ['base:texture_a'])
        self.events[-1]['checks']['dependency_sources'] = 'pass'
        with self.assertRaises(ValueError): self.convert()

    def test_invalid_dependency_identity_rejected(self):
        for dependencies in (None, ['base:a', 'base:a'], ['bad'], [12]):
            self.events[-1]['pending_dependencies'] = dependencies
            with self.assertRaises(ValueError): self.convert()

    def test_descriptor_only_model_pending(self):
        self.events[1].update(member="mesh.ini", source_role="descriptor")
        traces, pending = self.convert()
        self.assertFalse(traces)
        self.assertIn("activating source", pending[0]["reason"])

    def test_metadata_only_pending(self):
        self.events[1].update(member="_meta/model.gltf")
        self.assertFalse(self.convert()[0])

    def test_role_cannot_make_ini_a_mesh(self):
        self.events[1]["member"] = "mesh.ini"
        self.assertFalse(self.convert()[0])

    def test_missing_terminal_retained_pending(self):
        traces, pending = self.convert(self.events[:-1])
        self.assertFalse(traces)
        self.assertEqual(pending[0]["reason"], "missing native terminal event")

    def test_native_completion_required(self):
        self.events[-1]["native_completed"] = False
        with self.assertRaises(ValueError): self.convert()

    def test_rom_fallback_rejected(self):
        self.events[-1]["rom_fallback"] = True
        with self.assertRaises(ValueError): self.convert()

    def test_provider_failure_rejected_on_begin_and_member(self):
        for index in (0, 1):
            with self.subTest(index=index):
                events = copy.deepcopy(self.events); events[index]["provider"] = "RomProvider"
                with self.assertRaises(ValueError): self.convert(events)

    def test_cross_asset_or_generation_rejected(self):
        for changes in ({"catalog_id": "base:other"}, {"archive_sha256": "f" * 64},
                        {"archive_origin": "data/other.pdmesh"},
                        {"generation": dict(binary_sha256="f" * 64, source_sha256="c" * 64)}):
            with self.subTest(changes=changes):
                events = copy.deepcopy(self.events); events[1].update(changes)
                with self.assertRaises(ValueError): self.convert(events)

    def test_changed_member_rejected(self):
        for changes in ({"sha256": "f" * 64}, {"bytes": 13}, {"source_role": "image_source"}):
            events = copy.deepcopy(self.events)
            events.insert(2, events[1] | changes)
            with self.assertRaises(ValueError): self.convert(events)

    def test_repeated_same_read_allowed(self):
        self.events.insert(2, self.events[1].copy())
        self.assertEqual(len(self.convert()[0][0]["consumed_members"]), 1)

    def test_unsafe_member_rejected(self):
        for path in ("../model.obj", "/model.obj", "a//model.obj", "a\\model.obj", "C:stream", "a/./model.obj"):
            with self.subTest(path=path):
                events = copy.deepcopy(self.events); events[1]["member"] = path
                with self.assertRaises(ValueError): self.convert(events)

    def test_unsafe_or_missing_archive_origin_rejected(self):
        for origin in (None, "../mesh.pdmesh", "data/archive.pdbody::../mesh.pdmesh", "data/model.obj",
                       "data/private.pdcache", "data/base.pdbase", "data/mesh.pdmesh::model.obj"):
            events = [event | {"archive_origin": origin} for event in self.events]
            with self.assertRaises(ValueError): self.convert(events)

    def test_nested_typed_archive_origin_retained(self):
        origin = "data/body.pdbody::mesh.pdmesh"
        events = [event | {"archive_origin": origin} for event in self.events]
        self.assertEqual(self.convert(events)[0][0]["archive_origin"], origin)

    def test_time_order_and_timezone_required(self):
        for timestamp in ("2026-10-01T09:59:00Z", "2026-10-01T10:00:01"):
            self.events[1]["timestamp"] = timestamp
            with self.assertRaises(ValueError): self.convert()

    def test_duplicate_begin_and_event_after_end_rejected(self):
        for events in ([self.events[0]] + self.events, self.events + [self.events[1]]):
            with self.assertRaises(ValueError): self.convert(events)

    def test_unbegun_read_rejected(self):
        with self.assertRaises(ValueError): self.convert(self.events[1:])

    def test_duplicate_json_keys_rejected(self):
        with self.assertRaises(ValueError): m.loads('{"sha256":"a","sha256":"b"}')

    def test_member_and_attempt_bounds(self):
        with self.assertRaises(ValueError): self.convert(max_members=0)
        with self.assertRaises(ValueError): self.convert(max_attempts=0)

    def test_failed_native_terminal_stays_failed(self):
        self.events[-1]["load_verdict"] = "fail"
        self.assertEqual(self.convert()[0][0]["load_verdict"], "fail")

    def test_artifacts_hashed_and_original_stream_preserved(self):
        raw = ("\n".join(json.dumps(event) for event in self.events) + "\n").encode()
        traces, pending = m.convert(self.events, self.generation, hashlib.sha256(raw).hexdigest())
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "proof"
            envelope = m.write_bundle(output, raw, traces, pending)
            self.assertEqual((output / "native-events.jsonl").read_bytes(), raw)
            for artifact in envelope["receipts"][0]["artifacts"]:
                self.assertEqual(hashlib.sha256((output / artifact["path"]).read_bytes()).hexdigest(), artifact["sha256"])

    def test_output_existing_and_over_budget_preserved(self):
        traces, pending = m.convert(self.events, self.generation, hashlib.sha256(b"events").hexdigest())
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "proof"
            with self.assertRaises(ValueError): m.write_bundle(output, b"events", traces, pending, max_bytes=1)
            self.assertFalse(output.exists())
            output.mkdir(); (output / "historical").write_text("keep")
            with self.assertRaises(FileExistsError): m.write_bundle(output, b"events", traces, pending)
            self.assertEqual((output / "historical").read_text(), "keep")

    def test_stream_binding_cannot_be_replaced_at_export(self):
        traces, pending = self.convert()
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "proof"
            with self.assertRaises(ValueError): m.write_bundle(output, b"different", traces, pending)
            self.assertFalse(output.exists())

    def test_converted_members_bind_inventory_without_visual_promotion(self):
        spec = importlib.util.spec_from_file_location("inventory", Path(__file__).parents[1] / "asset-inspection-manifest.py")
        inventory = importlib.util.module_from_spec(spec); spec.loader.exec_module(inventory)
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "mesh.pdmesh"
            mesh = b"v 0 0 0\n"
            with zipfile.ZipFile(archive, "w") as target:
                target.writestr("mesh.ini", "catalog_id=base:mesh\n")
                target.writestr("model.obj", mesh)
            events = copy.deepcopy(self.events)
            for event in events: event["archive_sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
            events[1].update(sha256=hashlib.sha256(mesh).hexdigest(), bytes=len(mesh))
            raw = ("\n".join(json.dumps(event) for event in events) + "\n").encode()
            traces, pending = m.convert(events, self.generation, hashlib.sha256(raw).hexdigest())
            proof = Path(temp) / "proof"
            m.write_bundle(proof, raw, traces, pending)
            ledger = inventory.build([archive], self.generation, receipts=proof / "receipts.json")
            row = ledger["assets"][0]
            self.assertEqual(row["load_verdict"], "pass")
            self.assertEqual(row["fidelity_verdict"], "pending")
            self.assertIn("native_draw", row["pending"])

    def test_stream_count_and_byte_limits(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "events"
            path.write_text("{}\n{}\n")
            with self.assertRaises(ValueError): m.read_events(path, max_events=1)
            with self.assertRaises(ValueError): m.read_events(path, max_bytes=1)

    def test_independent_attempts_can_interleave(self):
        other = [event | {"attempt_id": "model2", "catalog_id": "base:other"} for event in self.events]
        events = [self.events[0], other[0], self.events[1], other[1], self.events[2], other[2]]
        self.assertEqual(len(self.convert(events)[0]), 2)

    def dependency_events(self):
        root = copy.deepcopy(self.events)
        root[-1].update(pending_dependencies=['base:texture_a'], dependency_bindings=[dict(
            catalog_id='base:texture_a', native_source_closure_sha256='f'*64, native_slot=65534)])
        leaf = [event | dict(attempt_id='texture1', catalog_id='base:texture_a',
            archive_origin='data/texture.pdtexture', archive_sha256='9'*64, operation='texture_load') for event in copy.deepcopy(self.events)]
        leaf[1].update(member='texture.png', source_role='image_source')
        leaf[-1].update(native_source_closure_sha256='f'*64, native_texture=dict(
            native_slot=65534, width=1, height=1, rgba_bytes=4, rgba_sha256='8'*64))
        return leaf + root

    def coverage(self, traces):
        return m.selected_coverage(traces, 'base:mesh', 'data/mesh.pdmesh', 'a'*64,
            {'data/mesh.pdmesh':{'sha256':'a'*64}, 'data/texture.pdtexture':{'sha256':'9'*64}})

    def test_actual_parent_binding_and_leaf_join_keeps_draw_pending(self):
        traces = self.convert(self.dependency_events())[0]
        coverage = self.coverage(traces)
        self.assertEqual(coverage['dependency_sources'], 'pass')
        self.assertEqual(coverage['native_draw'], 'pending')
        self.assertEqual(coverage['fidelity'], 'pending')
        self.assertEqual(traces[1]['pending_dependencies'], ['base:texture_a'])

    def test_missing_mismatched_or_cross_run_leaf_cannot_close_dependency(self):
        traces = self.convert(self.dependency_events())[0]
        with self.assertRaises(ValueError): self.coverage(traces[1:])
        for changes in ({'native_source_closure_sha256':'7'*64}, {'native_texture':dict(traces[0]['native_texture'],native_slot=65533)},
                        {'run_id':'other'}, {'generation':dict(self.generation,source_sha256='7'*64)},
                        {'native_completed':False}):
            changed = copy.deepcopy(traces); changed[0].update(changes)
            with self.assertRaises(ValueError): self.coverage(changed)
        changed = copy.deepcopy(traces); changed[1]['native_completed'] = False
        with self.assertRaises(ValueError): self.coverage(changed)

    def test_unselected_or_wrong_frozen_archive_cannot_close_dependency(self):
        traces = self.convert(self.dependency_events())[0]
        for changes in ({'catalog_id':'base:other'}, {'archive_sha256':'7'*64}):
            changed = copy.deepcopy(traces); changed[0].update(changes)
            with self.assertRaises(ValueError): self.coverage(changed)

    def test_bad_native_rgba_or_parent_binding_witness_rejected(self):
        for changes in ({'rgba_bytes':3}, {'width':0}, {'native_slot':65535}, {'native_slot':True}, {'rgba_sha256':'bad'}):
            events = self.dependency_events(); events[2]['native_texture'].update(changes)
            with self.assertRaises(ValueError): self.convert(events)
        events = self.dependency_events(); events[-1]['dependency_bindings'] *= 2
        with self.assertRaises(ValueError): self.convert(events)

    def test_source_read_event_cannot_claim_draw_pass(self):
        self.events[-1]['checks']['native_draw'] = 'pass'
        with self.assertRaises(ValueError): self.convert()

    def test_missing_parent_binding_retains_dependency_pending(self):
        events = self.dependency_events(); del events[-1]['dependency_bindings']
        self.assertEqual(self.coverage(self.convert(events)[0])['dependency_sources'], 'pending')

    def test_member_verified_ledger_join_closes_only_source_dependency(self):
        spec = importlib.util.spec_from_file_location('joined_inventory', Path(__file__).parents[1]/'asset-inspection-manifest.py')
        inventory = importlib.util.module_from_spec(spec); spec.loader.exec_module(inventory)
        with tempfile.TemporaryDirectory() as temp:
            mesh, image = b'v 0 0 0\n', b'synthetic png source'
            model, texture = Path(temp)/'mesh.pdmesh', Path(temp)/'texture.pdtexture'
            with zipfile.ZipFile(model, 'w') as z:
                z.writestr('mesh.ini', 'catalog_id=base:mesh\n'); z.writestr('model.obj', mesh)
                z.writestr('model.mtl', 'map_Kd base:texture_a\n')
            with zipfile.ZipFile(texture, 'w') as z:
                z.writestr('texture.ini', 'catalog_id=base:texture_a\n'); z.writestr('texture.png', image)
            events = self.dependency_events()
            for event in events:
                path = texture if event['operation'] == 'texture_load' else model
                event['archive_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
                if event['event'] == 'member':
                    data = image if event['operation'] == 'texture_load' else mesh
                    event.update(sha256=hashlib.sha256(data).hexdigest(),bytes=len(data))
            raw = ('\n'.join(json.dumps(e) for e in events)+'\n').encode()
            traces, pending = m.convert(events,self.generation,hashlib.sha256(raw).hexdigest())
            proof = Path(temp)/'proof'; m.write_bundle(proof,raw,traces,pending)
            row = next(r for r in inventory.build([model,texture],self.generation,receipts=proof/'receipts.json')['assets'] if r['catalog_id']=='base:mesh')
            self.assertEqual(row['dependency_sources_verdict'],'pass',row.get('dependency_evidence'))
            self.assertNotIn('dependency_sources',row['pending'])
            self.assertEqual(row['fidelity_verdict'],'pending')
            self.assertIn('native_draw',row['pending'])


if __name__ == "__main__": unittest.main()
