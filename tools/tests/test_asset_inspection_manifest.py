"""Tiny stdlib tests; no native launch, asset install, or historical evidence."""
import hashlib
import gzip
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('inspection', Path(__file__).parents[1] / 'asset-inspection-manifest.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

def archive(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w') as z:
        for name, content in entries: z.writestr(name, content)
    return stream.getvalue()

class InspectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
        self.generation = dict(binary_sha256='b'*64, source_sha256='c'*64)
        self.asset = self.root / 'x.pdentity'
        self.asset.write_bytes(archive([('entity.ini','[asset]\ncatalog_id=base:x\nsound=base:beep\n')]))
    def tearDown(self): self.temp.cleanup()
    def ledger(self, **kw): return m.build([self.asset], self.generation, **kw)
    def test_identity_dependencies_pending(self):
        doc=self.ledger(); row=doc['assets'][0]
        self.assertTrue(doc['complete_inventory']); self.assertEqual(row['catalog_id'],'base:x')
        self.assertEqual(row['dependencies'],['base:beep']); self.assertEqual(row['load_verdict'],'pending')
    def test_duplicate_nested_occurrences(self):
        payload=self.asset.read_bytes(); self.asset=self.root/'base.pdbase'
        self.asset.write_bytes(archive([('a.pdentity',payload),('b.pdentity',payload)]))
        doc=self.ledger(); self.assertEqual(len(doc['assets']),2)
        self.assertEqual(doc['measured']['unique_archives'],2)
        self.assertNotEqual(doc['assets'][0]['occurrence'],doc['assets'][1]['occurrence'])
    def test_member_budget_fails_closed(self):
        doc=self.ledger(budget=m.Budget(max_member_bytes=1))
        self.assertFalse(doc['complete_inventory']); self.assertTrue(doc['errors'])
    def test_corrupt_archive_retained(self):
        self.asset.write_bytes(b'broken'); self.assertFalse(self.ledger()['complete_inventory'])
    def test_unsafe_member_rejected(self):
        self.asset.write_bytes(archive([('../entity.ini','x')]))
        self.assertFalse(self.ledger()['complete_inventory'])
    def test_missing_identity_defect(self):
        self.asset.write_bytes(archive([('entity.ini','[asset]\nname=x')]))
        self.assertTrue(self.ledger()['assets'][0]['defects'])
    def receipt(self, **changes):
        trace=self.root/'trace.log'
        receipt=dict(catalog_id='base:x',archive_sha256=m.file_hash(self.asset),generation=self.generation,
                     consumer='native_catalog_provider',rom_fallback=False,timestamp='2026-10-01T03:00:00Z',
                     load_verdict='pass',checks={'schema':'pass','execution_effects':'pass','dependency_sources':'pass'},
                     artifacts=[])
        receipt.update(changes)
        witness={k:v for k,v in receipt.items() if k != 'artifacts'}
        witness.update(schema='pd2.native-asset-consumer.v1',consumed_members={entry['path']:entry['sha256'] for entry in self.ledger()['assets'][0]['members']})
        trace.write_text(json.dumps(witness)); receipt['artifacts']=[dict(path='trace.log',kind='native_trace',sha256=m.file_hash(trace))]
        path=self.root/'receipt.json'; path.write_text(json.dumps({'receipts':[receipt]}))
        return path
    def test_matching_receipt_pass(self):
        self.assertEqual(self.ledger(receipts=self.receipt())['assets'][0]['fidelity_verdict'],'pass')
    def test_wrong_generation_pending(self):
        path=self.receipt(generation=dict(binary_sha256='a'*64,source_sha256='c'*64))
        self.assertEqual(self.ledger(receipts=path)['assets'][0]['load_verdict'],'pending')
    def test_screenshot_only_pending(self):
        path=self.receipt(); doc=json.loads(path.read_text()); doc['receipts'][0]['artifacts'][0]['kind']='screenshot'; path.write_text(json.dumps(doc))
        self.assertEqual(self.ledger(receipts=path)['assets'][0]['load_verdict'],'pending')
    def test_behavior_missing_pending(self):
        self.assertEqual(self.ledger(receipts=self.receipt(checks={'schema':'pass'}))['assets'][0]['pending'],['execution_effects','dependency_sources'])
    def test_behavior_and_schema_pass_cannot_cover_dependency_sources(self):
        row = self.ledger(receipts=self.receipt(checks={'schema':'pass','execution_effects':'pass'}))['assets'][0]
        self.assertEqual(row['load_verdict'], 'pass'); self.assertEqual(row['fidelity_verdict'], 'pending')
        self.assertEqual(row['pending'], ['dependency_sources'])
    def test_rom_fallback_pending(self):
        self.assertEqual(self.ledger(receipts=self.receipt(rom_fallback=True))['assets'][0]['load_verdict'],'pending')
    def test_tampered_artifact_pending(self):
        path=self.receipt(); (self.root/'trace.log').write_text('changed')
        self.assertEqual(self.ledger(receipts=path)['assets'][0]['load_verdict'],'pending')
    def test_failure_sticky(self):
        path=self.receipt(load_verdict='fail'); self.assertEqual(self.ledger(receipts=path)['assets'][0]['load_verdict'],'fail')
    def test_plain_log_claim_pending(self):
        path=self.receipt(); trace=self.root/'trace.log'; trace.write_text('load pass')
        doc=json.loads(path.read_text()); doc['receipts'][0]['artifacts'][0]['sha256']=m.file_hash(trace); path.write_text(json.dumps(doc))
        self.assertEqual(self.ledger(receipts=path)['assets'][0]['load_verdict'],'pending')
    def test_archive_hash_is_not_consumed_member_hash(self):
        path=self.receipt(); trace=self.root/'trace.log'; witness=json.loads(trace.read_text())
        # An archive admission/identity witness cannot attest its member reads.
        witness['consumed_members']={'entity.ini':m.file_hash(self.asset)}
        trace.write_text(json.dumps(witness))
        doc=json.loads(path.read_text()); doc['receipts'][0]['artifacts'][0]['sha256']=m.file_hash(trace)
        path.write_text(json.dumps(doc))
        row=self.ledger(receipts=path)['assets'][0]
        self.assertEqual(row['load_verdict'],'pending')
        self.assertEqual(row['consumer_evidence'],[])
    def test_expanded_occurrence_budget(self):
        payload=self.asset.read_bytes(); self.asset=self.root/'base.pdbase'
        self.asset.write_bytes(archive([('a.pdentity',payload),('b.pdentity',payload)]))
        doc=self.ledger(budget=m.Budget(max_members=3))
        self.assertTrue(doc['complete_inventory'])
        doc=self.ledger(budget=m.Budget(max_members=2))
        self.assertFalse(doc['complete_inventory'])
    def test_sectionless_descriptor(self):
        self.asset.write_bytes(archive([('entity.ini','catalog_id="base:x"\nsound=base:beep # comment')]))
        self.assertEqual(self.ledger()['assets'][0]['catalog_id'],'base:x')
    def test_optional_packaged_member_not_consumed(self):
        self.asset.write_bytes(archive([('entity.ini','catalog_id=base:x'),('_meta/provenance.json','{}')]))
        path=self.receipt(); doc=json.loads(path.read_text()); trace=self.root/'trace.log'; witness=json.loads(trace.read_text())
        del witness['consumed_members']['_meta/provenance.json']; trace.write_text(json.dumps(witness))
        doc['receipts'][0]['artifacts'][0]['sha256']=m.file_hash(trace); path.write_text(json.dumps(doc))
        self.assertEqual(self.ledger(receipts=path)['assets'][0]['fidelity_verdict'],'pass')
    def test_output_overrun_preserves_bounded_partial_and_error(self):
        output=self.root/'ledger.json'; doc=self.ledger()
        self.assertFalse(m.write_ledger(doc, output, 128))
        self.assertLessEqual(output.stat().st_size, 128)
        error=json.loads((self.root/'ledger.json.error.json').read_text())
        self.assertFalse(error['complete_inventory'])
        self.assertIn('byte budget exceeded',error['defect'])
    def test_output_exact_boundary(self):
        output=self.root/'ledger.json'; doc=self.ledger()
        size=len(json.dumps(doc,indent=2).encode('utf-8'))
        self.assertTrue(m.write_ledger(doc,output,size))
        self.assertEqual(json.loads(output.read_text()),doc)
        self.assertEqual(output.stat().st_size,size)
    def test_output_existing_preserved(self):
        output=self.root/'ledger.json'; output.write_text('historic')
        with self.assertRaises(FileExistsError): m.write_ledger(self.ledger(),output,4096)
        self.assertEqual(output.read_text(),'historic')
    def test_output_invalid_budget_no_file(self):
        output=self.root/'ledger.json'
        with self.assertRaises(ValueError): m.write_ledger(self.ledger(),output,0)
        self.assertFalse(output.exists())


class FrozenInspectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
        self.source = 'data/model.pdmesh'
        self.payload = archive([('mesh.ini', 'catalog_id=base:model\n'), ('model.obj', 'v 0 0 0\n')])
        digest = hashlib.sha256(self.payload).hexdigest()
        self.files = {self.source: dict(sha256=digest, bytes=len(self.payload), mtime_ns=1),
                      'PerfectDark.exe': dict(sha256='b'*64, bytes=12, mtime_ns=1)}
        self.blobs = self.root / 'blobs'; self.blob = self.blobs / digest[:2] / (digest + '.gz')
        self.blob.parent.mkdir(parents=True); self.blob.write_bytes(gzip.compress(self.payload))
        self.seed = self.root / 'seed.gz'; self.write_seed()
        self.generation = dict(binary_sha256='b'*64, source_sha256='')
    def tearDown(self): self.temp.cleanup()
    def write_seed(self):
        raw = json.dumps(self.files, sort_keys=True, separators=(',', ':')).encode()
        self.seed_id = hashlib.sha256(raw).hexdigest(); self.seed.write_bytes(gzip.compress(raw))
    def build(self, **changes):
        return m.build_frozen(self.seed, self.seed_id, self.blobs, [self.source], self.generation, **changes)
    def test_actual_members_and_identity_from_immutable_blob(self):
        before = self.blob.read_bytes(); doc = self.build(); row = doc['assets'][0]
        self.assertTrue(doc['complete_inventory']); self.assertEqual(row['catalog_id'], 'base:model')
        self.assertEqual(row['archive_origin'], self.source)
        self.assertEqual({x['path'] for x in row['members']}, {'mesh.ini', 'model.obj'})
        self.assertEqual(row['load_verdict'], 'pending'); self.assertEqual(row['fidelity_verdict'], 'pending')
        self.assertEqual(self.blob.read_bytes(), before); self.assertEqual(doc['install_copies'], 0)
    def test_corrupt_blob_retained_as_incomplete(self):
        self.blob.write_bytes(gzip.compress(b'corrupt'))
        doc = self.build(); self.assertFalse(doc['complete_inventory']); self.assertTrue(doc['errors'])
    def test_missing_blob_retained_as_incomplete(self):
        self.blob.unlink(); self.assertFalse(self.build()['complete_inventory'])
    def test_explicit_selection_no_directory_or_wildcard_scan(self):
        for names in (['data/'], ['data/*.pdmesh'], [self.source, self.source], [self.source]*9):
            with self.assertRaises(ValueError): m.build_frozen(self.seed, self.seed_id, self.blobs, names, self.generation)
    def test_blob_and_expanded_byte_budgets_enforced(self):
        doc = self.build(budget=m.Budget(max_bytes=1)); self.assertFalse(doc['complete_inventory'])
        doc = self.build(budget=m.Budget(max_member_bytes=1)); self.assertFalse(doc['complete_inventory'])
    def test_wrong_seed_or_source_set_rejected(self):
        with self.assertRaises(ValueError): m.build_frozen(self.seed, 'f'*64, self.blobs, [self.source], self.generation)
        self.generation['source_sha256'] = 'f'*64
        with self.assertRaises(ValueError): self.build()
    def test_native_manifest_requires_resolved_model_and_generation(self):
        doc = self.build(); text = m.native_manifest(doc, self.source, 'native1')
        self.assertIn('catalog_id=base:model\n', text); self.assertIn('archive_origin='+self.source+'\n', text)
        self.assertEqual(len(text.splitlines()), 8)
        with self.assertRaises(ValueError): m.native_manifest(doc, self.source, '../escape')
        with self.assertRaises(ValueError): m.native_manifest(doc, self.source+'::model.obj', 'native1')
        doc['generation']['binary_sha256'] = ''
        with self.assertRaises(ValueError): m.native_manifest(doc, self.source, 'native1')
    def test_native_receipt_matches_only_selected_provider_origin(self):
        doc = self.build(); row = doc['assets'][0]
        original = dict(schema='pd2.native-asset-consumer.v1', catalog_id=row['catalog_id'],
                        archive_sha256=row['archive_sha256'], archive_origin='data/other.pdmesh',
                        generation=doc['generation'], consumer='native_catalog_provider', rom_fallback=False,
                        timestamp='2026-10-01T12:00:00Z', load_verdict='pass', checks={},
                        consumed_members={'model.obj': next(x['sha256'] for x in row['members'] if x['path']=='model.obj')})
        trace = self.root / 'trace.json'; trace.write_text(json.dumps(original))
        receipt = {k: v for k,v in original.items() if k not in {'schema','consumed_members'}}
        receipt['artifacts'] = [dict(path='trace.json',kind='native_trace',sha256=m.file_hash(trace))]
        m.bind_receipts(doc['assets'], [receipt], doc['generation'], self.root)
        self.assertEqual(row['load_verdict'], 'pending')
        original['archive_origin'] = self.source; trace.write_text(json.dumps(original))
        receipt['archive_origin'] = self.source; receipt['artifacts'][0]['sha256'] = m.file_hash(trace)
        m.bind_receipts(doc['assets'], [receipt], doc['generation'], self.root)
        self.assertEqual(row['load_verdict'], 'pass'); self.assertEqual(row['fidelity_verdict'], 'pending')
    def test_nested_origin_preserves_archive_boundaries(self):
        nested = archive([('mesh.pdmesh', self.payload), ('body.ini','catalog_id=base:body\n')])
        digest = hashlib.sha256(nested).hexdigest(); source = 'data/body.pdbody'
        self.files[source] = dict(sha256=digest, bytes=len(nested), mtime_ns=1); self.write_seed()
        blob = self.blobs / digest[:2] / (digest+'.gz'); blob.parent.mkdir(exist_ok=True); blob.write_bytes(gzip.compress(nested))
        doc = m.build_frozen(self.seed,self.seed_id,self.blobs,[source],self.generation)
        self.assertEqual({r['archive_origin'] for r in doc['assets']}, {source, source+'::mesh.pdmesh'})
    def test_linked_blob_cannot_supply_inventory(self):
        link = self.blob.with_name('alias.gz')
        import os
        os.link(self.blob, link)
        self.assertFalse(self.build()['complete_inventory'])

    def test_material_and_gltf_references_are_inventory_dependencies(self):
        payload = archive([('mesh.ini', 'catalog_id=base:model\n'), ('model.obj', 'v 0 0 0\n'),
                           ('model.mtl', 'pd_texture_catalog = base:texture_a\n'),
                           ('model.gltf', json.dumps({'extras': {'texture_id': 'base:texture_b'}})),
                           ('_meta/provenance.json', json.dumps({'id': 'base:historical'}))])
        row = m.flatten(m.inspect_archive(io.BytesIO(payload), 'model.pdmesh', hashlib.sha256(payload).hexdigest(), m.Budget(), {}), 'model')[0]
        self.assertEqual(row['dependencies'], ['base:texture_a', 'base:texture_b'])
        self.assertIn('dependency_sources', row['pending'])

    def test_deep_public_json_keeps_bounded_incomplete_inventory(self):
        source = self.root/'deep.pdmesh'
        source.write_bytes(archive([('mesh.ini','catalog_id=base:deep\n'), ('model.json','['*1100+'0'+']'*1100)]))
        doc = m.build([source], self.generation)
        self.assertFalse(doc['complete_inventory']); self.assertTrue(doc['errors'])

    def test_standard_mtl_catalog_maps_without_equals_are_dependencies(self):
        self.assertEqual(m.material_dependencies('newmtl camera\nmap_Kd base:camera\nmap_d -clamp on base:alpha # base:ignored\n'),
                         {'base:camera','base:alpha'})
        self.assertEqual(m.material_dependencies('map_Kd images/camera.png\n# map_Kd base:ignored\n'),set())


if __name__ == '__main__': unittest.main()
