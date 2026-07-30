#!/usr/bin/env node
'use strict';

/*
 * Perfect Dark 2 Workbench.
 *
 * Durable truth:
 *   data/roadmap.json    mutable, atomically replaced
 *   data/notes.jsonl     append-only note lifecycle events
 *   data/changelog.jsonl append-only roadmap/activity audit
 *
 * Live session and exclusive-resource state remains separate in
 * .codex-coordination/state.json and is read here only for the Hub view.
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { nextIndexedId, findDuplicateIds } = require('./ids');

const PORT = Number.parseInt(process.env.WORKBENCH_PORT || '8378', 10);
const HOST = process.env.WORKBENCH_HOST || '127.0.0.1';
const ROOT = __dirname;
const REPO_ROOT = path.resolve(ROOT, '..', '..');
const PUBLIC_DIR = path.join(ROOT, 'public');
const DATA_DIR = path.resolve(process.env.WORKBENCH_DATA_DIR || path.join(ROOT, 'data'));
const EXPORTS_DIR = path.join(ROOT, 'exports');
const ROADMAP_PATH = path.join(DATA_DIR, 'roadmap.json');
const NOTES_PATH = path.join(DATA_DIR, 'notes.jsonl');
const CHANGELOG_PATH = path.join(DATA_DIR, 'changelog.jsonl');

function coordinationStatePath() {
  const candidates = [];
  if (process.env.PD2_CODEX_COORDINATION_ROOT) {
    candidates.push(path.resolve(process.env.PD2_CODEX_COORDINATION_ROOT));
  }
  candidates.push(REPO_ROOT);
  const dotGit = path.join(REPO_ROOT, '.git');
  if (fs.existsSync(dotGit) && fs.statSync(dotGit).isFile()) {
    const match = /^gitdir:\s*(.+)\s*$/im.exec(fs.readFileSync(dotGit, 'utf8'));
    if (match) {
      let gitDirText = match[1].trim();
      const homePrefix = /^\/home\/[^/]+\/(.*)$/.exec(gitDirText);
      const drivePrefix = /^\/([A-Za-z])\/(.*)$/.exec(gitDirText);
      if (homePrefix) gitDirText = path.join(os.homedir(), homePrefix[1]);
      else if (drivePrefix) gitDirText = `${drivePrefix[1]}:\\${drivePrefix[2]}`;
      const worktreeGitDir = path.resolve(REPO_ROOT, gitDirText);
      candidates.push(path.resolve(worktreeGitDir, '..', '..', '..'));
    }
  }
  for (const candidate of candidates) {
    const state = path.join(candidate, '.codex-coordination', 'state.json');
    if (fs.existsSync(state)) return state;
  }
  return path.join(candidates[0], '.codex-coordination', 'state.json');
}

const COORD_STATE = coordinationStatePath();

const ITEM_TYPES = new Set([
  'task', 'decision', 'risk', 'asset', 'validation', 'performance', 'area',
]);
const TRUTH_STATUSES = new Set([
  'missing', 'stub', 'partial', 'implemented', 'validated', 'blocked', 'cut',
]);
const DECISION_STATUSES = new Set(['open', 'decided', 'deferred']);
const RISK_STATUSES = new Set(['open', 'mitigated', 'accepted', 'closed', 'blocked']);
const VERDICTS = new Set(['not_run', 'not_measured', 'pass', 'fail', 'blocked']);
const NOTE_STATES = new Set([
  'new', 'acknowledged', 'incorporated', 'rejected_with_reason',
  'needs_clarification', 'waiting_user_decision', 'superseded',
]);
const TERMINAL_NOTE_STATES = new Set([
  'incorporated', 'rejected_with_reason', 'needs_clarification',
  'waiting_user_decision', 'superseded',
]);
const MUTABLE_FIELDS = new Set([
  'title', 'area', 'status', 'phase', 'lane', 'owner', 'deps', 'evidence',
  'detail', 'tags', 'verdict', 'artifacts', 'budget', 'measurements', 'metric',
  'severity', 'resolution', 'options', 'model', 'legacyIds',
]);

for (const dir of [DATA_DIR, EXPORTS_DIR]) {
  fs.mkdirSync(dir, { recursive: true });
}

function isoNow() {
  return new Date().toISOString();
}

function readText(file) {
  return fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '');
}

function readRoadmap() {
  if (!fs.existsSync(ROADMAP_PATH)) {
    return { schemaVersion: 1, project: 'Perfect Dark 2', updated: null, items: [] };
  }
  const doc = JSON.parse(readText(ROADMAP_PATH));
  if (!doc || !Array.isArray(doc.items)) throw new Error('roadmap.json requires items[]');
  return doc;
}

function writeRoadmapAtomic(doc) {
  doc.updated = isoNow();
  const tmp = `${ROADMAP_PATH}.${process.pid}.${Date.now()}.tmp`;
  const fd = fs.openSync(tmp, 'wx');
  try {
    fs.writeFileSync(fd, JSON.stringify(doc, null, 2) + '\n', 'utf8');
    fs.fsyncSync(fd);
  } finally {
    fs.closeSync(fd);
  }
  fs.renameSync(tmp, ROADMAP_PATH);
}

function readJsonl(file) {
  if (!fs.existsSync(file)) return [];
  return readText(file).split(/\r?\n/).filter(Boolean).map((line, index) => {
    try { return JSON.parse(line); }
    catch (error) { return { malformed: true, line: index + 1, error: String(error) }; }
  });
}

function appendJsonl(file, value) {
  const fd = fs.openSync(file, 'a');
  try {
    fs.writeSync(fd, JSON.stringify(value) + '\n', null, 'utf8');
    fs.fsyncSync(fd);
  } finally {
    fs.closeSync(fd);
  }
}

let changeSeq = null;
function audit(kind, by, payload) {
  if (changeSeq === null) {
    changeSeq = readJsonl(CHANGELOG_PATH).reduce((max, row) => Math.max(max, row.seq || 0), 0);
  }
  const event = { seq: ++changeSeq, ts: isoNow(), kind, by: by || 'unknown', ...payload };
  appendJsonl(CHANGELOG_PATH, event);
  return event;
}

function statusSet(type) {
  if (type === 'decision') return DECISION_STATUSES;
  if (type === 'risk') return RISK_STATUSES;
  return TRUTH_STATUSES;
}

function arrayOfStrings(value, name, errors) {
  if (value === undefined) return;
  if (!Array.isArray(value) || value.some(x => typeof x !== 'string' || !x.trim())) {
    errors.push(`${name} must be an array of non-empty strings`);
  }
}

function validateItem(item, allItems, priorId = null) {
  const errors = [];
  if (!item || typeof item !== 'object') return ['item must be an object'];
  if (!item.id || typeof item.id !== 'string') errors.push('id is required');
  if (!ITEM_TYPES.has(item.type)) errors.push(`type must be one of ${[...ITEM_TYPES].join(', ')}`);
  if (!item.title || typeof item.title !== 'string') errors.push('title is required');
  if (!statusSet(item.type).has(item.status)) errors.push(`invalid ${item.type || 'item'} status`);
  if (item.phase !== undefined && item.phase !== null &&
      (!Number.isInteger(item.phase) || item.phase < 0)) errors.push('phase must be a non-negative integer');
  arrayOfStrings(item.deps, 'deps', errors);
  arrayOfStrings(item.evidence, 'evidence', errors);
  arrayOfStrings(item.tags, 'tags', errors);
  arrayOfStrings(item.artifacts, 'artifacts', errors);
  arrayOfStrings(item.legacyIds, 'legacyIds', errors);
  if (item.deps && item.deps.includes(item.id)) errors.push('an item cannot depend on itself');
  const ids = new Set(allItems.filter(x => x.id !== priorId).map(x => x.id));
  for (const dep of item.deps || []) if (!ids.has(dep)) errors.push(`unknown dependency: ${dep}`);
  if (item.type === 'decision') {
    if (!Array.isArray(item.options) || item.options.length < 2 || item.options.length > 4) {
      errors.push('decision items require 2-4 options');
    } else if (item.options.some(o => !o || typeof o.id !== 'string' ||
        typeof o.label !== 'string' || typeof o.tradeoff !== 'string')) {
      errors.push('each decision option requires id, label, and tradeoff strings');
    }
    if (item.status === 'decided' && !item.resolution) errors.push('decided requires resolution');
  }
  if (item.type === 'validation' || item.type === 'performance') {
    if (!VERDICTS.has(item.verdict)) errors.push(`${item.type} requires a valid verdict`);
    if (item.type === 'performance' && !item.budget) errors.push('performance requires budget');
  }
  if (item.status === 'implemented') {
    if (!(item.evidence || []).length) errors.push('implemented requires production-path evidence');
    if (!item.model) errors.push('implemented requires model attribution');
  }
  if (item.status === 'validated') {
    if (!(item.evidence || []).length) errors.push('validated requires evidence');
    if (!item.model) errors.push('validated requires model attribution');
    if ((item.type === 'validation' || item.type === 'performance') &&
        (item.verdict !== 'pass' || !(item.artifacts || []).length)) {
      errors.push('validated validation/performance items require pass verdict and durable artifacts');
    }
  }
  return errors;
}

function prefixFor(item) {
  const area = String(item.area || 'GENERAL').toUpperCase().replace(/[^A-Z0-9]+/g, '');
  return {
    task: `T-${area}`, decision: 'D', risk: 'R', asset: `A-${area}`,
    validation: 'V', performance: 'P', area: `RM-${area}`,
  }[item.type];
}

function foldNotes() {
  const notes = new Map();
  for (const ev of readJsonl(NOTES_PATH)) {
    if (ev.event === 'note_add') {
      notes.set(ev.id, {
        id: ev.id, ts: ev.ts, target: ev.target, author: ev.author, text: ev.text,
        state: 'new', supersededBy: null,
        history: [{ ts: ev.ts, state: 'new', by: ev.author, reason: null }],
      });
    } else if (ev.event === 'note_state' && notes.has(ev.id)) {
      const note = notes.get(ev.id);
      note.state = ev.state;
      note.supersededBy = ev.supersededBy || note.supersededBy;
      note.history.push({ ts: ev.ts, state: ev.state, by: ev.by, reason: ev.reason || null });
    }
  }
  return [...notes.values()];
}

function nextNoteId() {
  const max = readJsonl(NOTES_PATH).reduce((n, ev) => {
    const match = /^N-(\d+)$/.exec(ev.id || '');
    return match ? Math.max(n, Number.parseInt(match[1], 10)) : n;
  }, 0);
  return `N-${String(max + 1).padStart(4, '0')}`;
}

function send(res, code, body, type = 'application/json; charset=utf-8') {
  const data = Buffer.isBuffer(body) ? body : typeof body === 'string' ? body : JSON.stringify(body);
  res.writeHead(code, {
    'Content-Type': type,
    'Content-Length': Buffer.byteLength(data),
    'Cache-Control': 'no-store',
  });
  res.end(data);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let data = '';
    req.on('data', chunk => {
      data += chunk;
      if (data.length > 2_000_000) reject(new Error('request body too large'));
    });
    req.on('end', () => {
      try { resolve(data ? JSON.parse(data) : {}); } catch (error) { reject(error); }
    });
    req.on('error', reject);
  });
}

const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8',
};

function serveStatic(res, relative) {
  const safe = path.normalize(relative || 'index.html').replace(/^([/\\.])+/, '');
  const file = path.join(PUBLIC_DIR, safe);
  if (!file.startsWith(PUBLIC_DIR) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    return send(res, 404, { error: 'not found' });
  }
  send(res, 200, fs.readFileSync(file), MIME[path.extname(file)] || 'application/octet-stream');
}

function exportMarkdown(by) {
  const doc = readRoadmap();
  const notes = foldNotes();
  const stamp = isoNow();
  let roadmap = `# Perfect Dark 2 Workbench Roadmap\n\nGenerated: ${stamp}\n\n`;
  roadmap += '| ID | Type | Area | Status | Title |\n|---|---|---|---|---|\n';
  for (const item of doc.items) {
    roadmap += `| ${item.id} | ${item.type} | ${item.area || ''} | ${item.status} | ${item.title.replace(/\|/g, '/')} |\n`;
  }
  let noteText = `# Perfect Dark 2 Workbench Notes\n\nGenerated: ${stamp}\n\n`;
  for (const note of notes) noteText += `## ${note.id} [${note.state}] -> ${note.target}\n\n${note.text}\n\n`;
  fs.writeFileSync(path.join(EXPORTS_DIR, 'roadmap-export.md'), roadmap, 'utf8');
  fs.writeFileSync(path.join(EXPORTS_DIR, 'notes-export.md'), noteText, 'utf8');
  audit('export', by, { files: ['exports/roadmap-export.md', 'exports/notes-export.md'] });
  return { roadmap: 'exports/roadmap-export.md', notes: 'exports/notes-export.md' };
}

const routes = {
  'GET /api/roadmap': async (req, res) => send(res, 200, readRoadmap()),
  'GET /api/roadmap/next-id': async (req, res, url) => {
    const prefix = String(url.searchParams.get('prefix') || '').trim();
    if (!prefix) return send(res, 400, { error: 'prefix required' });
    send(res, 200, { prefix, id: nextIndexedId(readRoadmap().items, prefix) });
  },
  'POST /api/roadmap/item': async (req, res) => {
    const body = await readBody(req);
    const doc = readRoadmap();
    const item = { ...(body.item || {}) };
    if (!item.id) item.id = nextIndexedId(doc.items, body.prefix || prefixFor(item));
    if (doc.items.some(x => x.id === item.id)) return send(res, 409, { error: `duplicate id: ${item.id}` });
    item.created = item.updated = isoNow();
    const errors = validateItem(item, doc.items);
    if (errors.length) return send(res, 400, { error: 'invalid item', errors });
    doc.items.push(item);
    writeRoadmapAtomic(doc);
    audit('item_create', body.by, { id: item.id, item, reason: body.reason || null });
    send(res, 201, { ok: true, id: item.id });
  },
  'POST /api/roadmap/update': async (req, res) => {
    const body = await readBody(req);
    const doc = readRoadmap();
    const index = doc.items.findIndex(x => x.id === body.id);
    if (index < 0) return send(res, 404, { error: `item not found: ${body.id}` });
    const patch = body.patch || {};
    const unknown = Object.keys(patch).filter(k => !MUTABLE_FIELDS.has(k));
    if (unknown.length) return send(res, 400, { error: 'immutable or unknown fields', fields: unknown });
    const next = { ...doc.items[index], ...patch, updated: isoNow() };
    const errors = validateItem(next, doc.items, body.id);
    if (errors.length) return send(res, 400, { error: 'invalid update', errors });
    const changes = {};
    for (const key of Object.keys(patch)) changes[key] = { from: doc.items[index][key] ?? null, to: patch[key] };
    doc.items[index] = next;
    writeRoadmapAtomic(doc);
    audit('item_update', body.by, { id: body.id, changes, reason: body.reason || null });
    send(res, 200, { ok: true, id: body.id, changes });
  },
  'GET /api/notes': async (req, res) => send(res, 200, foldNotes()),
  'POST /api/notes': async (req, res) => {
    const body = await readBody(req);
    const ids = new Set(readRoadmap().items.map(x => x.id));
    if (!body.target || (body.target !== 'GENERAL' && !ids.has(body.target))) {
      return send(res, 400, { error: 'target must be GENERAL or an existing item id' });
    }
    if (!body.author || !body.text) return send(res, 400, { error: 'author and text required' });
    const id = nextNoteId();
    appendJsonl(NOTES_PATH, {
      event: 'note_add', id, ts: isoNow(), target: body.target,
      author: body.author, text: body.text, state: 'new',
    });
    audit('note_add', body.author, { note: id, target: body.target, preview: String(body.text).slice(0, 160) });
    send(res, 201, { ok: true, id });
  },
  'POST /api/notes/state': async (req, res) => {
    const body = await readBody(req);
    if (!NOTE_STATES.has(body.state)) return send(res, 400, { error: 'invalid note state' });
    const note = foldNotes().find(x => x.id === body.id);
    if (!note) return send(res, 404, { error: `note not found: ${body.id}` });
    const validTransition = note.state === 'new' ? body.state === 'acknowledged'
      : note.state === 'acknowledged' ? TERMINAL_NOTE_STATES.has(body.state)
        : ['needs_clarification', 'waiting_user_decision'].includes(note.state)
          ? ['acknowledged', 'superseded'].includes(body.state)
          : false;
    if (!validTransition) return send(res, 409, { error: `invalid transition ${note.state} -> ${body.state}` });
    if (body.state === 'rejected_with_reason' && !body.reason) return send(res, 400, { error: 'reason required' });
    if (body.state === 'superseded' && !body.supersededBy) return send(res, 400, { error: 'supersededBy required' });
    const event = {
      event: 'note_state', id: body.id, ts: isoNow(), state: body.state,
      by: body.by || 'unknown', reason: body.reason || null,
    };
    if (body.supersededBy) event.supersededBy = body.supersededBy;
    appendJsonl(NOTES_PATH, event);
    audit('note_state', body.by, { note: body.id, state: body.state, reason: body.reason || null });
    send(res, 200, { ok: true });
  },
  'GET /api/changelog': async (req, res) => send(res, 200, readJsonl(CHANGELOG_PATH).slice(-500).reverse()),
  'GET /api/coordination': async (req, res) => {
    if (!fs.existsSync(COORD_STATE)) return send(res, 200, { available: false });
    try {
      send(res, 200, {
        available: true, mtime: fs.statSync(COORD_STATE).mtime.toISOString(),
        state: JSON.parse(readText(COORD_STATE)),
      });
    } catch (error) { send(res, 200, { available: false, error: String(error) }); }
  },
  'GET /api/meta': async (req, res) => {
    const doc = readRoadmap();
    const notes = foldNotes();
    const counts = { byType: {}, byStatus: {}, byArea: {} };
    for (const item of doc.items) {
      counts.byType[item.type] = (counts.byType[item.type] || 0) + 1;
      counts.byStatus[item.status] = (counts.byStatus[item.status] || 0) + 1;
      counts.byArea[item.area || 'none'] = (counts.byArea[item.area || 'none'] || 0) + 1;
    }
    send(res, 200, {
      port: PORT, host: HOST, hostname: os.hostname(), items: doc.items.length,
      notes: notes.length, openNotes: notes.filter(x => ['new', 'needs_clarification', 'waiting_user_decision'].includes(x.state)).length,
      updated: doc.updated, counts, duplicateIds: findDuplicateIds(doc.items),
    });
  },
  'POST /api/export': async (req, res) => {
    const body = await readBody(req);
    send(res, 200, { ok: true, files: exportMarkdown(body.by || 'workbench-ui') });
  },
};

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${HOST}:${PORT}`);
  const key = `${req.method} ${url.pathname}`;
  try {
    if (routes[key]) return await routes[key](req, res, url);
    if (url.pathname === '/' || url.pathname === '/index.html') return serveStatic(res, 'index.html');
    return serveStatic(res, url.pathname.slice(1));
  } catch (error) {
    send(res, 500, { error: String(error && error.message || error) });
  }
});

server.listen(PORT, HOST, () => {
  console.log(`Perfect Dark 2 Workbench: http://${HOST === '0.0.0.0' ? 'localhost' : HOST}:${PORT}`);
  console.log(`Data: ${DATA_DIR}`);
  console.log(`Coordination state (read-only): ${COORD_STATE}`);
});
