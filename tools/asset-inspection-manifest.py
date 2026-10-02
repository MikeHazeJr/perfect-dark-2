#!/usr/bin/env python3
"""Bounded, occurrence-complete public asset inventory and native receipt ledger."""
from __future__ import annotations

import argparse
import configparser
import copy
import hashlib
import io
import gzip
import importlib.util
import json
import os
from pathlib import Path
import re
import tempfile
from datetime import datetime, timezone
import zipfile

FAMILIES = dict(zip(
    'weapon projectile entity material texture character head body arena scenario mesh anim sfx voice song ui font lang skin effect prop vehicle mission gamemode botprofile hud theme'.split(),
    'weapon projectile entity material texture character head body arena scenario mesh animation sound voice music ui font lang skin effect prop vehicle mission gamemode botprofile hud theme'.split()))
CONTAINERS = {'.pdbase', '.pdmod', '.zip'}
OBLIGATIONS = {
    'visual': ['views', 'materials', 'native_draw'],
    'animation': ['played_poses', 'interpolation', 'root_motion', 'loops'],
    'world': ['collision', 'portals', 'rooms', 'navigation', 'execution_effects'],
    'image': ['native_draw', 'dimensions', 'content'],
    'audio': ['duration', 'channels', 'loops', 'waveform', 'audible_playback'],
    'behavior': ['schema', 'execution_effects'],
}
SHA = re.compile(r'^[0-9a-fA-F]{64}$')
CATALOG = re.compile(r'^[A-Za-z][\w.-]*:[\w./-]+$')


def family(path):
    suffix = Path(path).suffix.lower()
    return suffix[3:] if suffix.startswith('.pd') and suffix[3:] in FAMILIES else None


def category(kind):
    if kind == 'anim': return 'animation'
    if kind in {'arena', 'scenario'}: return 'world'
    if kind in {'sfx', 'voice', 'song'}: return 'audio'
    if kind in {'texture', 'ui', 'font', 'lang', 'hud', 'theme'}: return 'image'
    if kind in {'mesh', 'body', 'head', 'character', 'material', 'skin'}: return 'visual'
    return 'behavior'


class Budget:
    def __init__(self, max_bytes=2 * 1024**3, max_member_bytes=256 * 1024**2,
                 max_members=200000, max_depth=12, spool_dir=None):
        self.max_bytes, self.max_member_bytes = max_bytes, max_member_bytes
        self.max_members, self.max_depth = max_members, max_depth
        self.bytes = self.members = 0
        self.spool_dir = spool_dir

    def read(self, stream, size):
        if size > self.max_member_bytes or self.bytes + size > self.max_bytes:
            raise ValueError('uncompressed byte budget exceeded')
        self.members += 1
        if self.members > self.max_members: raise ValueError('member budget exceeded')
        digest = hashlib.sha256()
        # Spool nested archives; no unbounded in-memory archive read.
        result = tempfile.SpooledTemporaryFile(max_size=1024**2, dir=self.spool_dir)
        count = 0
        try:
            while True:
                block = stream.read(1024**2)
                if not block: break
                count += len(block)
                if count > size: raise ValueError('member exceeds declared size')
                digest.update(block); result.write(block)
            if count != size: raise ValueError('member size mismatch')
        except BaseException:
            result.close()
            raise
        self.bytes += count
        result.seek(0)
        return digest.hexdigest(), result


def metadata(text):
    ids, refs = [], set()
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(('[', '#', ';', '//')) or '=' not in line: continue
        key, value = line.split('=', 1)
        value = value.split('#', 1)[0].split(';', 1)[0].strip().strip('"\'')
        key = re.sub(r'[^a-z0-9]', '', key.lower())
        if key in {'id', 'catalogid'} and CATALOG.fullmatch(value): ids.append(value)
        for candidate in re.findall(r'[A-Za-z][\w.-]*:[\w./-]+', value):
            if CATALOG.fullmatch(candidate): refs.add(candidate)
    unique = sorted(set(ids))
    return unique[0] if len(unique) == 1 else None, sorted(refs - set(ids)), unique


def material_dependencies(text):
    references = set(metadata(text)[1])
    for raw in text.splitlines():
        line = raw.split('#', 1)[0].strip()
        fields = line.split(None, 1)
        if len(fields) == 2 and fields[0].lower() in {'map_kd','map_ka','map_ks','map_d','map_bump','bump','norm','disp'}:
            for candidate in re.findall(r'[A-Za-z][\w.-]*:[\w./-]+', fields[1]):
                if CATALOG.fullmatch(candidate): references.add(candidate)
    return references


def inspect_archive(stream, name, digest, budget, cache, depth=0):
    if depth > budget.max_depth: raise ValueError('nested depth budget exceeded')
    cache_key = (digest, Path(name).suffix.lower())
    if cache_key in cache: return copy.deepcopy(cache[cache_key])
    kind = family(name)
    row = dict(family=kind, archive_sha256=digest, members=[], catalog_id=None,
               dependencies=[], identity_candidates=[], defects=[], children=[], dependency_scan_pending=[])
    references = set()
    with zipfile.ZipFile(stream) as archive:
        seen = set()
        for member in archive.infolist():
            if member.is_dir(): continue
            member_path = member.filename.replace('\\', '/')
            if member_path in seen or member_path.startswith('/') or '..' in member_path.split('/'):
                raise ValueError('duplicate or unsafe archive member: ' + member_path)
            seen.add(member_path)
            with archive.open(member) as source:
                member_hash, spool = budget.read(source, member.file_size)
            try:
                row['members'].append(dict(path=member_path, sha256=member_hash, bytes=member.file_size))
                if kind and member_path == FAMILIES[kind] + '.ini':
                    if member.file_size > 1024**2: raise ValueError('descriptor exceeds 1 MiB')
                    identity, deps, candidates = metadata(spool.read().decode('utf-8-sig'))
                    row.update(catalog_id=identity, dependencies=deps, identity_candidates=candidates)
                    references.update(deps)
                elif not member_path.startswith('_meta/') and Path(member_path).suffix.lower() in {'.ini', '.mtl', '.json', '.gltf'}:
                    if member.file_size > 1024**2:
                        row['dependency_scan_pending'].append(member_path)
                    else:
                        text = spool.read().decode('utf-8-sig')
                        if Path(member_path).suffix.lower() in {'.json', '.gltf'}:
                            def collect(value):
                                if isinstance(value, str) and CATALOG.fullmatch(value): references.add(value)
                                elif isinstance(value, list):
                                    for child in value: collect(child)
                                elif isinstance(value, dict):
                                    for child in value.values(): collect(child)
                            collect(json.loads(text))
                        elif Path(member_path).suffix.lower() == '.mtl': references.update(material_dependencies(text))
                        else: references.update(metadata(text)[1])
                elif family(member_path) or Path(member_path).suffix.lower() in CONTAINERS:
                    row['children'].append(dict(member=member_path, node=inspect_archive(
                        spool, member_path, member_hash, budget, cache, depth + 1)))
            finally: spool.close()
    if kind and not row['catalog_id']: row['defects'].append('missing or ambiguous catalog identity')
    row['dependencies'] = sorted(references - set(row['identity_candidates']))
    row['dependency_scan_scope'] = 'Catalog IDs in bounded public .ini/.mtl/.json/.gltf members; excludes _meta; no dependency load proof.'
    cache[cache_key] = copy.deepcopy(row)
    return row


def flatten(node, occurrence, parent=None, counter=None, limit=200000, origin=None):
    counter = counter if counter is not None else [0]
    counter[0] += 1
    if counter[0] > limit: raise ValueError('expanded occurrence budget exceeded')
    result = []
    if node['family']:
        row = {key: copy.deepcopy(value) for key, value in node.items() if key != 'children'}
        row.update(occurrence=occurrence, parent_occurrence=parent,
                   embedded_dependencies=[c['member'] for c in node['children']],
                   category=category(node['family']), load_verdict='pending', fidelity_verdict='pending',
                   consumer_evidence=[], pending=OBLIGATIONS[category(node['family'])].copy())
        if row['dependencies'] or row['dependency_scan_pending']: row['pending'].append('dependency_sources')
        result.append(row)
        if origin is not None: row['archive_origin'] = origin
    for child in node['children']:
        child_origin = origin + '::' + child['member'] if origin is not None else None
        result.extend(flatten(child['node'], occurrence + '!' + child['member'], occurrence, counter, limit, child_origin))
    return result


def bind_receipts(rows, receipts, generation, base, budget=None):
    """Only native trace-backed same-generation receipts can promote a row."""
    bound_traces = []
    for row in rows:
        for receipt in receipts:
            if receipt.get('catalog_id') != row['catalog_id'] or not row['catalog_id']: continue
            if receipt.get('archive_sha256') != row['archive_sha256']: continue
            if row.get('archive_origin') is not None and receipt.get('archive_origin') != row['archive_origin']: continue
            if receipt.get('generation') != generation or not all(SHA.fullmatch(str(generation.get(k, ''))) for k in ('binary_sha256', 'source_sha256')): continue
            if receipt.get('consumer') != 'native_catalog_provider' or receipt.get('rom_fallback') is not False: continue
            try:
                datetime.fromisoformat(receipt['timestamp'].replace('Z', '+00:00'))
                artifacts = receipt['artifacts']
                if not artifacts: continue
                verified = []
                for artifact in artifacts:
                    path = (base / artifact['path']).resolve()
                    if not path.is_relative_to(base.resolve()): raise ValueError('artifact escapes receipt root')
                    if budget:
                        size = path.stat().st_size
                        if size > budget.max_member_bytes or budget.bytes + size > budget.max_bytes: raise ValueError('evidence hash budget exceeded')
                        budget.bytes += size
                    if file_hash(path) != artifact['sha256']: raise ValueError('artifact hash mismatch')
                    verified.append(artifact)
                native = [a for a in verified if a.get('kind') == 'native_trace']
                if not native: continue
                expected_members = {member['path']: member['sha256'] for member in row['members']}
                witnesses = []
                for artifact in native:
                    trace_path = base / artifact['path']
                    if trace_path.stat().st_size > 16 * 1024**2: raise ValueError('trace exceeds 16 MiB')
                    trace = json.loads(trace_path.read_text(encoding='utf-8-sig'))
                    consumed = trace.get('consumed_members')
                    consumed_valid = isinstance(consumed, dict) and bool(consumed) and all(
                        path in expected_members and expected_members[path] == sha
                        and not path.startswith('_meta/') for path, sha in consumed.items())
                    sizes = trace.get('consumed_member_sizes')
                    if sizes is not None:
                        expected_sizes = {m['path']: m['bytes'] for m in row['members']}
                        consumed_valid = consumed_valid and isinstance(sizes, dict) and set(sizes) == set(consumed) and all(
                            type(size) is int and expected_sizes.get(path) == size for path, size in sizes.items())
                    dependencies = trace.get('pending_dependencies', [])
                    dependencies_valid = isinstance(dependencies, list) and len(dependencies) <= 2048 and all(
                        isinstance(p, str) and CATALOG.fullmatch(p) for p in dependencies)
                    matched = (trace.get('schema') == 'pd2.native-asset-consumer.v1'
                        and trace.get('catalog_id') == row['catalog_id']
                        and trace.get('archive_sha256') == row['archive_sha256']
                        and (row.get('archive_origin') is None or trace.get('archive_origin') == row['archive_origin'])
                        and trace.get('generation') == generation
                        and trace.get('consumer') == 'native_catalog_provider'
                        and trace.get('rom_fallback') is False
                        and consumed_valid
                        and dependencies_valid
                        and dependencies == receipt.get('pending_dependencies', [])
                        and (not dependencies or trace.get('checks', {}).get('dependency_sources') != 'pass')
                        and trace.get('checks') == receipt.get('checks')
                        and trace.get('load_verdict') == receipt.get('load_verdict'))
                    witnesses.append(matched)
                    if matched: bound_traces.append((row, trace))
                if not any(witnesses): continue
            except (KeyError, ValueError, OSError, TypeError, AttributeError): continue
            row['consumer_evidence'].append(receipt)
            if receipt.get('load_verdict') == 'fail':
                row['load_verdict'] = 'fail'; row['defects'].extend(receipt.get('defects', ['native load failed']))
            elif row['load_verdict'] != 'fail' and receipt.get('load_verdict') == 'pass': row['load_verdict'] = 'pass'
            checks = receipt.get('checks', {})
            if receipt.get('pending_dependencies') and 'dependency_sources' not in row['pending']:
                row['pending'].append('dependency_sources')
            row['pending'] = [check for check in row['pending'] if checks.get(check) != 'pass']
            if any(value == 'fail' for value in checks.values()): row['fidelity_verdict'] = 'fail'
        if row['load_verdict'] == 'pass' and not row['pending'] and not row['defects'] and row['fidelity_verdict'] != 'fail': row['fidelity_verdict'] = 'pass'
    # Dependency coverage is a separate join over already member-verified rows.
    # Preserve the producer's original root receipt and its pending dependency list.
    helper = None
    for row, trace in bound_traces:
        dependencies = trace.get('pending_dependencies', [])
        if trace.get('operation') != 'modeldef_compile' or not dependencies: continue
        row['dependency_sources_verdict'] = 'pending'
        try:
            if row['dependency_scan_pending'] or set(dependencies) != set(row['dependencies']):
                raise ValueError('inventory dependencies are not completely represented by native bindings')
            if helper is None: helper = load_helper('native_dependency_contract', Path(__file__).with_name('asset-inspection-receipts.py'))
            related = [candidate for _, candidate in bound_traces if candidate is trace or (
                candidate.get('catalog_id') in dependencies and candidate.get('run_id') == trace.get('run_id')
                and candidate.get('event_stream_sha256') == trace.get('event_stream_sha256'))]
            coverage = helper.selected_coverage(related, trace['catalog_id'], trace['archive_origin'], trace['archive_sha256'])
            row['dependency_evidence'] = coverage
            if coverage['dependency_sources'] == 'pass':
                row['dependency_sources_verdict'] = 'pass'
                row['pending'] = [check for check in row['pending'] if check != 'dependency_sources']
        except (ValueError, KeyError, TypeError, AttributeError) as error:
            row['dependency_evidence'] = {'dependency_sources':'pending', 'reason':str(error)}


def file_hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024**2), b''): digest.update(chunk)
    return digest.hexdigest()


def enumerate_inputs(paths):
    found = set()
    for supplied in paths:
        if not supplied.exists(): raise ValueError('missing supplied input: ' + str(supplied))
        candidates = supplied.rglob('*') if supplied.is_dir() else [supplied]
        for path in candidates:
            if path.is_file() and (family(path.name) or path.suffix.lower() in CONTAINERS): found.add(path.resolve())
    return sorted(found)


def build(paths, generation, receipts=None, budget=None):
    budget = budget or Budget(); cache = {}; rows = []; errors = []; counter = [0]
    inputs = enumerate_inputs(paths)
    for path in inputs:
        try:
            if path.stat().st_size > budget.max_bytes - budget.bytes: raise ValueError('archive hash byte budget exceeded')
            digest = file_hash(path); budget.bytes += path.stat().st_size
            with path.open('rb') as stream: rows.extend(flatten(inspect_archive(stream, path.name, digest, budget, cache), str(path), counter=counter, limit=budget.max_members))
        except (ValueError, OSError, RuntimeError, zipfile.BadZipFile, UnicodeError, configparser.Error) as exc:
            errors.append(dict(input=str(path), defect=str(exc)))
    if receipts:
        if receipts.stat().st_size > 4 * 1024**2: raise ValueError('receipt envelope exceeds 4 MiB')
        document = json.loads(receipts.read_text(encoding='utf-8-sig'))
        bind_receipts(rows, document['receipts'], generation, receipts.parent, budget)
    return dict(schema='pd2.asset-inspection.v1', timestamp=datetime.now(timezone.utc).isoformat(),
                generation=generation, tool_sha256=file_hash(Path(__file__)), inputs=[str(p) for p in inputs],
                assets=rows, errors=errors, complete_inventory=not errors and bool(inputs),
                measured=dict(bytes_read=budget.bytes, members_read=budget.members, unique_archives=len(cache)),
                summary=dict(assets=len(rows), passed=sum(r['fidelity_verdict'] == 'pass' for r in rows),
                             pending=sum(r['fidelity_verdict'] == 'pending' for r in rows)))


def load_helper(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def build_frozen(seed_manifest, seed_id, blob_root, selections, generation, receipts=None, budget=None):
    """Inspect explicit immutable source blobs only; never restore an install or prune storage."""
    budget = budget or Budget()
    planner = load_helper('frozen_seed_metadata', Path(__file__).with_name('asset-inspection-plan.py'))
    files = planner.seed_files(seed_manifest, seed_id)
    source_hash = planner.source_set_hash(files)
    if generation.get('source_sha256') and generation['source_sha256'].lower() != source_hash:
        raise ValueError('frozen source-set hash mismatch')
    generation = dict(binary_sha256=generation.get('binary_sha256', '').lower(), source_sha256=source_hash)
    names = [Path(path).as_posix() for path in selections]
    if not names or len(names) > 8 or len(set(names)) != len(names):
        raise ValueError('select one to eight unique exact frozen archive paths')
    if any(name not in files or not (family(name) or Path(name).suffix.lower() in CONTAINERS) for name in names):
        raise ValueError('selection must name an exact public source archive in the frozen seed')
    readonly = load_helper('frozen_blob_io', Path(__file__).parents[1] / 'devtools/smoke-storage-manifest.py')
    root = Path(blob_root).absolute()
    for parent in [*reversed(root.parents), root]: readonly.check_plain(parent)
    rows, errors, cache, counter, blob_bytes = [], [], {}, [0], 0
    for name in names:
        entry = files[name]; digest = entry['sha256']
        blob = root / digest[:2] / (digest + '.gz')
        spool = None
        try:
            readonly.check_plain(blob.parent); before = readonly.check_plain(blob)
            if before.st_nlink != 1 or before.st_size > budget.max_bytes - budget.bytes:
                raise ValueError('linked/compressed blob exceeds read budget')
            # Charge compressed bytes and then decompressed archive/member work.
            budget.bytes += before.st_size; blob_bytes += before.st_size
            with readonly.sequential_open(blob) as source:
                readonly.verify_opened_path(source, blob)
                opened = os.fstat(source.fileno())
                if readonly.identity(before) != readonly.identity(opened): raise ValueError('immutable blob changed before read')
                with gzip.GzipFile(fileobj=source, mode='rb') as decoded:
                    actual, spool = budget.read(decoded, entry['bytes'])
                if readonly.identity(opened) != readonly.identity(os.fstat(source.fileno())):
                    raise ValueError('immutable blob changed during read')
            if readonly.identity(before) != readonly.identity(readonly.check_plain(blob)) or actual != digest:
                raise ValueError('immutable source blob identity mismatch')
            rows.extend(flatten(inspect_archive(spool, Path(name).name, digest, budget, cache),
                                name, counter=counter, limit=budget.max_members, origin=name))
        except (ValueError, OSError, RuntimeError, EOFError, zipfile.BadZipFile, UnicodeError, configparser.Error) as error:
            errors.append(dict(input=name, defect=str(error)))
        finally:
            if spool is not None: spool.close()
    if receipts:
        if receipts.stat().st_size > 4 * 1024**2: raise ValueError('receipt envelope exceeds 4 MiB')
        document = json.loads(receipts.read_text(encoding='utf-8-sig'))
        bind_receipts(rows, document['receipts'], generation, receipts.parent, budget)
    return dict(schema='pd2.asset-inspection.v1', timestamp=datetime.now(timezone.utc).isoformat(),
                generation=generation, source_seed=seed_id, seed_binary_sha256=files.get('PerfectDark.exe', {}).get('sha256'),
                tool_sha256=file_hash(Path(__file__)), inputs=names, assets=rows, errors=errors,
                complete_inventory=not errors, inventory_scope='Explicit selected archives and their nested occurrences only; not whole corpus.',
                install_copies=0, immutable_inputs_modified=False,
                measured=dict(bytes_read=budget.bytes, compressed_blob_bytes=blob_bytes, members_read=budget.members,
                              unique_archives=len(cache)),
                summary=dict(assets=len(rows), passed=sum(r['fidelity_verdict'] == 'pass' for r in rows),
                             pending=sum(r['fidelity_verdict'] == 'pending' for r in rows)))


def native_manifest(document, origin, run_id):
    """Operational one-model evidence controls, not authored asset source."""
    generation = document['generation']
    if not all(re.fullmatch(r'[0-9a-f]{64}', generation.get(k, '')) for k in ('binary_sha256', 'source_sha256')):
        raise ValueError('native manifest requires the emitting binary and frozen source identity')
    if not re.fullmatch(r'[A-Za-z0-9_.-]{1,80}', run_id): raise ValueError('invalid native run identity')
    matching = [row for row in document['assets'] if row.get('archive_origin') == origin]
    if len(matching) != 1 or matching[0]['family'] != 'mesh' or not matching[0]['catalog_id'] or matching[0]['defects']:
        raise ValueError('native manifest requires one unambiguous inventoried public model')
    row = matching[0]
    if len(origin) > 1024 or any(ord(c) < 32 or c == '\\' for c in origin) or any(
            ':' in part or part.startswith('/') or any(p in {'', '.', '..'} for p in part.split('/')) for part in origin.split('::')):
        raise ValueError('unsafe native archive origin')
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]*:[A-Za-z0-9_./-]+', row['catalog_id']) or len(row['catalog_id']) >= 128:
        raise ValueError('unsafe native catalog identity')
    if not re.fullmatch(r'[0-9a-f]{64}', row['archive_sha256']): raise ValueError('native archive hash required')
    values = dict(schema='pd2.native-asset-consumer-manifest.v1', binary_sha256=generation['binary_sha256'],
                  source_sha256=generation['source_sha256'], catalog_id=row['catalog_id'], archive_origin=origin,
                  archive_sha256=row['archive_sha256'], run_id=run_id, max_output_bytes=16 * 1024**2)
    return ''.join(str(key) + '=' + str(value) + '\n' for key, value in values.items())


def write_ledger(document, output, max_bytes):
    """Exclusive bounded output; retain partial bytes and a small failure receipt."""
    if max_bytes <= 0: raise ValueError('output budget must be positive')
    written = 0
    with output.open('xb') as stream:
        for chunk in json.JSONEncoder(indent=2).iterencode(document):
            encoded = chunk.encode('utf-8')
            if written + len(encoded) > max_bytes:
                # Never exceed the reservation or replace earlier evidence.
                error = dict(schema='pd2.asset-inspection-output-error.v1',
                             complete_inventory=False, defect='ledger output byte budget exceeded',
                             partial_ledger=str(output), max_output_bytes=max_bytes,
                             bytes_written=written, summary=document.get('summary', {}))
                with output.with_name(output.name + '.error.json').open('x', encoding='utf-8') as receipt:
                    json.dump(error, receipt, indent=2)
                return False
            stream.write(encoded); written += len(encoded)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--receipts', type=Path)
    parser.add_argument('--binary-sha256', default='')
    parser.add_argument('--source-sha256', default='')
    parser.add_argument('--frozen-seed', type=Path)
    parser.add_argument('--seed-id', default='')
    parser.add_argument('--blob-root', type=Path)
    parser.add_argument('--native-manifest', type=Path)
    parser.add_argument('--native-origin', default='')
    parser.add_argument('--native-run-id', default='')
    parser.add_argument('--max-bytes', type=int, default=2 * 1024**3)
    parser.add_argument('--max-member-bytes', type=int, default=256 * 1024**2)
    parser.add_argument('--max-members', type=int, default=200000)
    parser.add_argument('--max-depth', type=int, default=12)
    parser.add_argument('--max-output-bytes', type=int, default=64 * 1024**2)
    args = parser.parse_args()
    if min(args.max_bytes, args.max_member_bytes, args.max_members, args.max_output_bytes) <= 0 or args.max_depth < 0: parser.error('budgets must be positive')
    if args.output.exists() or args.output.with_name(args.output.name + '.error.json').exists(): parser.error('output or failure receipt already exists; preserve evidence')
    if args.native_manifest and (not args.frozen_seed or args.native_manifest.exists()): parser.error('native manifest needs frozen inventory and a new output file')
    if args.frozen_seed and (not args.seed_id or not args.blob_root): parser.error('frozen inventory requires seed identity and explicit immutable blob root')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    generation = dict(binary_sha256=args.binary_sha256, source_sha256=args.source_sha256)
    budget = Budget(args.max_bytes, args.max_member_bytes, args.max_members, args.max_depth, args.output.parent)
    if args.frozen_seed:
        document = build_frozen(args.frozen_seed, args.seed_id, args.blob_root, args.paths, generation, args.receipts, budget)
    else:
        document = build(args.paths, generation, args.receipts, budget)
    manifest = native_manifest(document, args.native_origin, args.native_run_id) if args.native_manifest and document['complete_inventory'] else None
    if not write_ledger(document, args.output, args.max_output_bytes):
        print('Ledger budget exceeded; partial bytes and .error.json retained')
        return 1
    if manifest is not None:
        if len(manifest.encode()) > 8192: raise ValueError('native manifest exceeds8KiB')
        with args.native_manifest.open('x', encoding='utf-8', newline='\n') as target: target.write(manifest)
    print(json.dumps(document['summary']))
    return 0 if document['complete_inventory'] else 1


if __name__ == '__main__': raise SystemExit(main())
