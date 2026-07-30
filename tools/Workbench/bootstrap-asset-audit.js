#!/usr/bin/env node
'use strict';

/*
 * Idempotently creates the umbrella Workbench program requested for the
 * all-family asset/archive/runtime and menu/input audit.
 */

const base = process.env.WORKBENCH_URL || 'http://127.0.0.1:8378';
const by = process.env.WORKBENCH_AUTHOR || 'codex-pd2-workbench-assets-20260730';

async function post(route, body) {
  const response = await fetch(base + route, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const payload = await response.json();
  if (!response.ok) throw new Error(`${route} ${response.status}: ${JSON.stringify(payload)}`);
  return payload;
}

async function run() {
  let roadmap = await (await fetch(base + '/api/roadmap')).json();
  const keyTag = key => `workbench-key:${key}`;
  const byKey = new Map();
  for (const item of roadmap.items) {
    for (const tag of item.tags || []) {
      if (tag.startsWith('workbench-key:')) byKey.set(tag.slice('workbench-key:'.length), item.id);
    }
  }

  async function ensure(key, item) {
    if (byKey.has(key)) return byKey.get(key);
    item.tags = [...new Set([...(item.tags || []), keyTag(key)])];
    const result = await post('/api/roadmap/item', {
      item, by, reason: `Bootstrap the requested asset/input audit lane (${key}).`,
    });
    byKey.set(key, result.id);
    return result.id;
  }

  const area = await ensure('asset-input-program', {
    type: 'area', title: 'Asset Source, Runtime, Menus, and Input Program',
    area: 'ASSET_INPUT', status: 'partial', phase: 1, lane: 'program',
    owner: by, deps: [], evidence: [],
    detail: 'Umbrella for lossless extraction, creator-facing typed archives, native runtime consumption, Modding Hub, menus, MKB/controller input, and glyph correctness.',
  });

  const workbench = await ensure('workbench-migration', {
    type: 'task', title: 'Replace Kanban with the repo-local Workbench',
    area: 'TOOLING', status: 'partial', phase: 1, lane: 'workbench',
    owner: by, deps: [], evidence: [],
    detail: 'Install typed durable truth, append-only notes and changelog, atomic validated API mutations, unified views, and a separate coordination hub. Archive the retired Kanban after migration.',
  });

  const audit = await ensure('asset-current-tree-audit', {
    type: 'task', title: 'Audit every asset family from extraction through production use',
    area: 'ASSETS', status: 'partial', phase: 1, lane: 'audit',
    owner: by, deps: [workbench], evidence: [],
    detail: 'Build a current-tree family matrix covering source extraction, public .pdxxx representation, self-containment, runtime provider/adapter consumption, generated caches, ROM fallback, tests, and creator workflows.',
  });

  const extraction = await ensure('asset-lossless-extraction', {
    type: 'task', title: 'Make extraction lossless and comprehensive for every asset family',
    area: 'EXTRACTION', status: 'missing', phase: 2, lane: 'implementation',
    owner: by, deps: [audit], evidence: [],
    detail: 'Fix every confirmed omission, stale conversion, raw dump, unsupported record, and identity/provenance gap found by the current-tree audit.',
  });

  const archives = await ensure('asset-public-archives', {
    type: 'task', title: 'Make every .pdxxx archive self-contained and creator-accessible',
    area: 'ARCHIVES', status: 'missing', phase: 2, lane: 'implementation',
    owner: by, deps: [audit], evidence: [],
    detail: 'Public typed archives are the editable source of truth. Schemas must be understandable, catalog-addressed, lossless, comprehensive, and free of opaque runtime payloads.',
  });

  const runtime = await ensure('asset-native-runtime', {
    type: 'task', title: 'Consume public asset source comprehensively in the production runtime',
    area: 'RUNTIME', status: 'missing', phase: 3, lane: 'implementation',
    owner: by, deps: [extraction, archives], evidence: [],
    detail: 'Catalog/provider loading must drive every production adapter. Engine-ready products are private source-hashed caches; any runtime ROM/RomProvider fallback is a chain failure.',
  });

  const modding = await ensure('asset-modding-hub', {
    type: 'task', title: 'Complete creator save, import, add-content, and validation flows',
    area: 'MODDING', status: 'missing', phase: 3, lane: 'implementation',
    owner: by, deps: [archives, runtime], evidence: [],
    detail: 'Ensure Modding Hub and documented workflows let creators edit existing content and add new catalog-addressed content without parallel authored runtime files.',
  });

  const menus = await ensure('menus-production-audit', {
    type: 'task', title: 'Audit and fix all production menu behavior',
    area: 'MENUS', status: 'partial', phase: 2, lane: 'implementation',
    owner: by, deps: [workbench], evidence: [],
    detail: 'Exercise the real ImGui menu graph and modal flows, including focus, back/cancel, mouse hit testing, scrolling, disabled actions, transitions, and load/import surfaces.',
  });

  const input = await ensure('input-controller-glyph-audit', {
    type: 'task', title: 'Preserve every input action across MKB and controller with correct glyphs',
    area: 'INPUT', status: 'partial', phase: 2, lane: 'implementation',
    owner: by, deps: [workbench], evidence: [],
    detail: 'Trace every action through InputAction authority, contexts, rebinding, persistence, prompts, glyph selection, controller navigation, and MKB parity. No action may be silently omitted or removed.',
  });

  await ensure('asset-family-matrix', {
    type: 'asset', title: 'All public typed archive families and their runtime products',
    area: 'ASSETS', status: 'partial', phase: 1, lane: 'audit',
    owner: by, deps: [audit], evidence: [],
    detail: 'One row per public family will record extractor, schema, dependencies, round-trip fidelity, runtime consumer, cache boundary, creator example, and durable proof.',
  });

  await ensure('risk-stale-parity', {
    type: 'risk', title: 'Historical parity closure may not match the current production tree',
    area: 'ASSETS', status: 'open', phase: 1, lane: 'audit',
    owner: by, deps: [audit], evidence: [],
    severity: 'high',
    detail: 'Prior Done claims are inputs, not present-tense proof. Current source, tests, produced archives, and default runtime paths must be rechecked before retaining implemented/validated truth.',
  });

  await ensure('validation-native-source-guard', {
    type: 'validation', title: 'Public asset source guard passes',
    area: 'ASSETS', status: 'missing', phase: 4, lane: 'verification',
    owner: by, deps: [runtime], evidence: [], verdict: 'not_run', artifacts: [],
    detail: 'Run tools/asset_native_source_guard.py and retain durable output after all source/runtime changes.',
  });

  await ensure('validation-all-family-roundtrip', {
    type: 'validation', title: 'All-family extraction and typed-archive round-trip passes',
    area: 'ARCHIVES', status: 'missing', phase: 4, lane: 'verification',
    owner: by, deps: [extraction, archives], evidence: [], verdict: 'not_run', artifacts: [],
    detail: 'Focused tests must prove complete source fields survive public archive conversion and feed native runtime use for every family.',
  });

  await ensure('validation-production-runtime', {
    type: 'validation', title: 'Catalog/provider production runtime uses every asset family',
    area: 'RUNTIME', status: 'missing', phase: 4, lane: 'verification',
    owner: by, deps: [runtime, modding], evidence: [], verdict: 'not_run', artifacts: [],
    detail: 'Durable production-path tests and runtime artifacts must prove no family is preview-only, disconnected, or falling back to ROM.',
  });

  await ensure('validation-menu-input', {
    type: 'validation', title: 'Menus, MKB, controller navigation, rebinding, and glyphs pass',
    area: 'INPUT', status: 'missing', phase: 4, lane: 'verification',
    owner: by, deps: [menus, input], evidence: [], verdict: 'not_run', artifacts: [],
    detail: 'Automated coverage plus ordinary game-client evidence must cover every action, focus path, pointer path, controller path, and dynamic glyph transition.',
  });

  await ensure('performance-asset-load', {
    type: 'performance', title: 'Asset extraction, validation, cache rebuild, and load remain responsive',
    area: 'RUNTIME', status: 'missing', phase: 4, lane: 'performance',
    owner: by, deps: [runtime], evidence: [], verdict: 'not_measured', artifacts: [],
    budget: 'Budgets will be derived from current baselines before optimization; no regression is acceptable.',
    measurements: [],
    metric: 'wall-clock extraction, validation, cache rebuild, and stage-load time',
    detail: 'Measure cold and warm creator/runtime paths separately and record asset count and cache state.',
  });

  roadmap = await (await fetch(base + '/api/roadmap')).json();
  console.log(JSON.stringify({
    programArea: area, workbench, audit, extraction, archives, runtime,
    modding, menus, input, totalItems: roadmap.items.length,
  }, null, 2));
}

run().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
