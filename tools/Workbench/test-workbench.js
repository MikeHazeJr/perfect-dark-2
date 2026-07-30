#!/usr/bin/env node
'use strict';

const assert = require('assert/strict');
const childProcess = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const root = __dirname;
const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'pd2-workbench-test-'));
const port = 18000 + (process.pid % 1000);
const base = `http://127.0.0.1:${port}`;

fs.writeFileSync(path.join(dataDir, 'roadmap.json'), JSON.stringify({
  schemaVersion: 1, project: 'Perfect Dark 2 Test', updated: null, items: [],
}, null, 2) + '\n');
fs.writeFileSync(path.join(dataDir, 'notes.jsonl'), '');
fs.writeFileSync(path.join(dataDir, 'changelog.jsonl'), '');

const child = childProcess.spawn(process.execPath, [path.join(root, 'server.js')], {
  cwd: root,
  env: { ...process.env, WORKBENCH_PORT: String(port), WORKBENCH_DATA_DIR: dataDir },
  stdio: ['ignore', 'pipe', 'pipe'],
});

let serverOutput = '';
child.stdout.on('data', chunk => { serverOutput += chunk; });
child.stderr.on('data', chunk => { serverOutput += chunk; });

async function request(route, body, expected) {
  const response = await fetch(base + route, body === undefined ? undefined : {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const payload = await response.json();
  assert.equal(response.status, expected, `${route}: ${JSON.stringify(payload)}`);
  return payload;
}

async function waitForServer() {
  for (let attempt = 0; attempt < 50; attempt++) {
    try {
      const response = await fetch(base + '/api/meta');
      if (response.ok) return;
    } catch (_) {
      await new Promise(resolve => setTimeout(resolve, 40));
    }
  }
  throw new Error(`server did not start\n${serverOutput}`);
}

async function run() {
  await waitForServer();

  const created = await request('/api/roadmap/item', {
    by: 'test', reason: 'create fixture',
    item: {
      id: 'T-TEST-001', type: 'task', title: 'Fixture task', area: 'TEST',
      status: 'stub', phase: 1, owner: 'test', deps: [], evidence: [],
      detail: 'Test the mutation contract.', tags: ['test'],
    },
  }, 201);
  assert.equal(created.id, 'T-TEST-001');

  await request('/api/roadmap/item', {
    by: 'test', item: {
      id: 'T-TEST-001', type: 'task', title: 'Duplicate', area: 'TEST',
      status: 'missing', deps: [], evidence: [],
    },
  }, 409);

  await request('/api/roadmap/item', {
    by: 'test', item: {
      type: 'task', title: 'Bad dependency', area: 'TEST',
      status: 'missing', deps: ['T-TEST-999'], evidence: [],
    },
  }, 400);

  await request('/api/roadmap/update', {
    id: 'T-TEST-001', by: 'test', reason: 'prove truth gate',
    patch: { status: 'implemented' },
  }, 400);

  await request('/api/roadmap/update', {
    id: 'T-TEST-001', by: 'test', reason: 'supply production evidence',
    patch: {
      status: 'implemented', model: 'workbench-test',
      evidence: ['Tools/Workbench/test-workbench.js exercised the production API.'],
    },
  }, 200);

  await request('/api/roadmap/item', {
    by: 'test', item: {
      type: 'decision', title: 'Invalid decision', area: 'TEST',
      status: 'open', deps: [], evidence: [],
      options: [
        { label: 'No stable key', tradeoff: 'Cannot be audited reliably.' },
        { label: 'Still no key', tradeoff: 'Cannot be audited reliably.' },
      ],
    },
  }, 400);

  const decision = await request('/api/roadmap/item', {
    by: 'test', reason: 'prove typed decision validation',
    item: {
      type: 'decision', title: 'Choose a fixture option', area: 'TEST',
      status: 'open', deps: ['T-TEST-001'], evidence: [],
      options: [
        { id: 'A', label: 'First', tradeoff: 'Exercises option A.' },
        { id: 'B', label: 'Second', tradeoff: 'Exercises option B.' },
      ],
    },
  }, 201);

  const note = await request('/api/notes', {
    target: decision.id, author: 'test', text: 'Process this fixture note.',
  }, 201);
  await request('/api/notes/state', {
    id: note.id, state: 'incorporated', by: 'test',
  }, 409);
  await request('/api/notes/state', {
    id: note.id, state: 'acknowledged', by: 'test',
  }, 200);
  await request('/api/notes/state', {
    id: note.id, state: 'incorporated', by: 'test',
    reason: 'Fixture note completed.',
  }, 200);

  const roadmap = await (await fetch(base + '/api/roadmap')).json();
  assert.equal(roadmap.items.length, 2);
  assert.equal(new Set(roadmap.items.map(item => item.id)).size, 2);
  assert.equal(roadmap.items.find(item => item.id === 'T-TEST-001').status, 'implemented');

  const changelog = await (await fetch(base + '/api/changelog')).json();
  assert.ok(changelog.some(event => event.kind === 'item_create'));
  assert.ok(changelog.some(event => event.kind === 'item_update'));
  assert.ok(changelog.some(event => event.kind === 'note_state'));
  assert.equal(fs.readdirSync(dataDir).some(name => name.endsWith('.tmp')), false);

  const page = await (await fetch(base + '/')).text();
  assert.match(page, /Perfect Dark 2 Workbench/);
  console.log('Workbench API tests passed.');
}

run().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
}).finally(() => {
  child.kill();
  fs.rmSync(dataDir, { recursive: true, force: true });
});
