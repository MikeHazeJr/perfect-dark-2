#!/usr/bin/env node
'use strict';

/*
 * One-way migration from the retired Kanban tracker into Workbench.
 *
 * The default import deliberately excludes Kanban's Done column: those 127
 * historical records remain intact in context/_old/kanban-legacy/state.json,
 * while current/deferred work and unresolved user decisions become live,
 * typed Workbench records. Pass --include-done only for forensic imports.
 */

const fs = require('fs');
const path = require('path');

const args = new Set(process.argv.slice(2));
const includeDone = args.has('--include-done');
const dryRun = args.has('--dry-run');
const base = process.env.WORKBENCH_URL || 'http://127.0.0.1:8378';
const source = process.env.KANBAN_STATE ||
  path.resolve(__dirname, '..', '..', 'context', '_old', 'kanban-legacy', 'state.json');
const parkedSource = process.env.KANBAN_PARKED ||
  path.resolve(path.dirname(source), 'parked.json');

function array(value) {
  return Array.isArray(value) ? value : [];
}

function concise(value, max = 1800) {
  const text = String(value || '').trim();
  return text.length > max ? text.slice(0, max - 1) + '…' : text;
}

function statusFor(card) {
  if (card.column === 'blocked') return 'blocked';
  const completion = card.pending_completion || {};
  const hasEvidence = Boolean(
    completion.verify_notes || array(completion.files).length ||
    /\b(verified|passed|built|tested)\b/i.test(card.notes || '')
  );
  if (card.column === 'done') return hasEvidence ? 'implemented' : 'partial';
  const hasPartialWork = array(card.subtasks).some(subtask => subtask.status === 'done') || hasEvidence;
  return hasPartialWork ? 'partial' : 'missing';
}

function evidenceFor(card) {
  const completion = card.pending_completion || {};
  const evidence = [];
  if (completion.verify_notes) evidence.push(concise(completion.verify_notes, 1200));
  if (array(completion.files).length) {
    evidence.push(`Legacy changed files: ${completion.files.join(', ')}`);
  }
  if (!evidence.length && /\b(verified|passed|built|tested)\b/i.test(card.notes || '')) {
    evidence.push(`Legacy tracker statement: ${concise(card.notes, 1000)}`);
  }
  return evidence;
}

function detailFor(card) {
  const parts = [
    concise(card.description),
    card.notes ? `Legacy notes: ${concise(card.notes)}` : '',
    array(card.subtasks).length
      ? `Legacy subtasks:\n${card.subtasks.map(s => `- [${s.status || 'unknown'}] ${s.id}: ${s.title}`).join('\n')}`
      : '',
    `Imported from retired Kanban ${card.id}, column ${card.column}.`,
  ];
  return parts.filter(Boolean).join('\n\n');
}

async function call(route, body) {
  if (dryRun) return { ok: true, id: `DRY-${Math.random().toString(16).slice(2, 8)}` };
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
  if (!fs.existsSync(source)) throw new Error(`Kanban state not found: ${source}`);
  const legacy = JSON.parse(fs.readFileSync(source, 'utf8'));
  const roadmap = dryRun ? { items: [] } : await (await fetch(base + '/api/roadmap')).json();
  const imported = new Map();
  for (const item of roadmap.items || []) {
    for (const legacyId of array(item.legacyIds)) imported.set(legacyId, item.id);
  }

  const cards = array(legacy.cards).filter(card => includeDone || card.column !== 'done');
  let cardCount = 0;
  let decisionCount = 0;
  let parkedCount = 0;
  let skipped = 0;

  for (const card of cards) {
    if (imported.has(card.id)) {
      skipped++;
      continue;
    }
    const status = statusFor(card);
    const evidence = evidenceFor(card);
    const item = {
      type: 'task',
      title: card.title,
      area: String(card.pillar || 'legacy').toUpperCase().replace(/[^A-Z0-9]+/g, '_'),
      status,
      phase: 0,
      lane: 'historical-import',
      owner: 'unassigned',
      deps: [],
      evidence,
      detail: detailFor(card),
      tags: ['legacy-kanban', `legacy-column:${card.column}`],
      legacyIds: [card.id],
    };
    if (status === 'implemented') item.model = 'legacy-kanban-record';
    const result = await call('/api/roadmap/item', {
      item, by: 'kanban-migration',
      reason: 'Import current/deferred work from retired Kanban without reusing its id.',
    });
    imported.set(card.id, result.id);
    cardCount++;
  }

  for (const card of array(legacy.cards)) {
    for (const question of array(card.open_questions)) {
      const isResolved = question.resolved || question.resolved_at || question.answer;
      if (isResolved || imported.has(question.id)) continue;
      const deps = imported.has(card.id) ? [imported.get(card.id)] : [];
      const options = array(question.choices).slice(0, 4).map((choice, index) => ({
        id: String(choice.id || String.fromCharCode(65 + index)).toUpperCase(),
        label: concise(choice.label, 120),
        tradeoff: concise([choice.rationale, choice.implication].filter(Boolean).join(' '), 600),
      }));
      if (options.length < 2) continue;
      const result = await call('/api/roadmap/item', {
        item: {
          type: 'decision',
          title: `Legacy decision for ${card.id}: ${concise(question.question, 140)}`,
          area: String(card.pillar || 'legacy').toUpperCase().replace(/[^A-Z0-9]+/g, '_'),
          status: 'open',
          phase: 0,
          lane: 'user-decision',
          owner: 'user',
          deps,
          evidence: [],
          detail: question.question,
          tags: ['legacy-kanban', 'user-decision'],
          legacyIds: [question.id],
          options,
          resolution: null,
        },
        by: 'kanban-migration',
        reason: 'Preserve an unresolved Kanban question in the Workbench Decisions view.',
      });
      imported.set(question.id, result.id);
      decisionCount++;
    }
  }

  if (fs.existsSync(parkedSource)) {
    const parked = JSON.parse(fs.readFileSync(parkedSource, 'utf8'));
    for (const entry of array(parked.parked)) {
      if (imported.has(entry.id)) {
        skipped++;
        continue;
      }
      const result = await call('/api/roadmap/item', {
        item: {
          type: 'task',
          title: entry.title,
          area: String(entry.pillar || 'legacy').toUpperCase().replace(/[^A-Z0-9]+/g, '_'),
          status: 'missing',
          phase: 0,
          lane: 'manual-resume',
          owner: 'unassigned',
          deps: [],
          evidence: array(entry.context_refs),
          detail: [
            concise(entry.last_state_snapshot),
            `Resume condition: ${JSON.stringify(entry.resume_condition || {})}`,
            `Imported from retired parked thread ${entry.id}.`,
          ].join('\n\n'),
          tags: ['legacy-kanban', 'manual-resume', ...array(entry.tags)],
          legacyIds: [entry.id],
        },
        by: 'kanban-migration',
        reason: 'Preserve a manually parked legacy thread as a durable Workbench item.',
      });
      imported.set(entry.id, result.id);
      parkedCount++;
    }
  }

  console.log(JSON.stringify({
    source, includeDone, dryRun, importedCards: cardCount,
    importedDecisions: decisionCount, importedParked: parkedCount, skipped,
  }, null, 2));
}

run().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
