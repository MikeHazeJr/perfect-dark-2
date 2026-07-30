/* Perfect Dark 2 Workbench UI - vanilla JS, talks to server.js API. */
'use strict';

const STATUS_ORDER = [
  'missing', 'stub', 'partial', 'implemented', 'validated', 'blocked', 'cut',
  'open', 'mitigated', 'accepted', 'closed', 'decided', 'deferred',
];
const STATUS_COLOR = {
  missing: 'var(--missing)', stub: 'var(--stub)', partial: 'var(--partial)',
  implemented: 'var(--implemented)', validated: 'var(--validated)', blocked: 'var(--blocked)',
  cut: 'var(--cut)', open: 'var(--stub)', mitigated: 'var(--partial)',
  accepted: 'var(--implemented)', closed: 'var(--validated)',
  decided: 'var(--validated)', deferred: 'var(--cut)',
};
const NSTATE_COLOR = {
  new: 'var(--s-new)', acknowledged: 'var(--s-ack)', incorporated: 'var(--s-inc)',
  rejected_with_reason: 'var(--s-rej)', needs_clarification: 'var(--s-clar)',
  waiting_user_decision: 'var(--s-wait)', superseded: 'var(--s-sup)',
};
const OPEN_NOTE_STATES = ['new', 'needs_clarification', 'waiting_user_decision'];
const TABS = [
  'Board', 'Graph', 'Timeline', 'Decisions', 'Assets',
  'Validation', 'Performance', 'Notes', 'Activity', 'Hub',
];

const savedTab = localStorage.getItem('wb-tab');
const S = {
  items: [], notes: [], changelog: [], coord: null, meta: null,
  tab: TABS.includes(savedTab) ? savedTab : 'Board',
  author: localStorage.getItem('wb-author') || 'user',
};

// ---------- data ----------
async function api(path, opts) {
  const r = await fetch(path, opts);
  const j = await r.json();
  if (!r.ok) throw new Error(j.error || r.status);
  return j;
}
async function post(path, body) {
  return api(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
}
async function loadAll() {
  const [roadmap, notes, changelog, meta, coord] = await Promise.all([
    api('/api/roadmap'), api('/api/notes'), api('/api/changelog'), api('/api/meta'), api('/api/coordination'),
  ]);
  S.items = roadmap.items; S.notes = notes; S.changelog = changelog; S.meta = meta; S.coord = coord;
  if (!savedTab && !window._mobileLandingChosen && window.matchMedia && window.matchMedia('(max-width: 760px)').matches) {
    const needsInput = S.items.some(i => i.type === 'decision' && i.status !== 'decided' && i.status !== 'deferred') ||
      S.notes.some(n => ['waiting_user_decision', 'needs_clarification'].includes(n.state));
    if (needsInput) S.tab = 'Decisions';
    window._mobileLandingChosen = true;
  }
  const dupIds = meta.duplicateIds || [];
  const dupWarn = dupIds.length ? ` · ⚠ ${dupIds.length} duplicate id${dupIds.length > 1 ? 's' : ''} (${dupIds.map(d => d.id).join(', ')})` : '';
  document.getElementById('meta-counts').textContent =
    `${meta.items} items · ${meta.notes} notes (${meta.openNotes} open)${dupWarn} · updated ${fmtTs(roadmap.updated)}`;
  const phoneButton = document.getElementById('btn-phone');
  if (phoneButton) {
    phoneButton.disabled = !(meta.lanUrls || []).length;
    phoneButton.title = meta.phoneUrl ? `Open or share ${meta.phoneUrl}` : 'No LAN address detected';
  }
  buildFilterOptions();
  render();
}

// ---------- helpers ----------
function esc(s) { return String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
// Escape, then turn bare URLs into tappable links (display trimmed, full href preserved).
function linkifyEsc(s) {
  return esc(s).replace(/(https?:\/\/[^\s<)]+)/g, m => {
    const disp = m.replace(/^https?:\/\//, '');
    return `<a href="${m}" target="_blank" rel="noopener">${disp.length > 64 ? disp.slice(0, 62) + '…' : disp}</a>`;
  });
}
function fmtTs(ts) { return ts ? ts.replace('T', ' ').slice(0, 16) + 'Z' : '—'; }
function statusChip(s) { const c = STATUS_COLOR[s] || 'var(--dim)'; return `<span class="status" style="color:${c};border-color:${c}">${esc(s || '—')}</span>`; }
function nstateChip(s) { const c = NSTATE_COLOR[s] || 'var(--dim)'; return `<span class="nstate" style="color:${c};border-color:${c}">${esc(s)}</span>`; }
function notesFor(id) { return S.notes.filter(n => n.target === id); }
function openNotesFor(id) { return notesFor(id).filter(n => OPEN_NOTE_STATES.includes(n.state)); }
let _toastTimer = null;
function toast(msg, kind) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.className = kind || '';
  t.style.display = 'block';
  clearTimeout(_toastTimer);
  _toastTimer = setTimeout(() => { t.style.display = 'none'; }, kind === 'err' ? 6000 : 4200);
}
let BANNER = null;
function setBanner(html) { BANNER = html; }
function dismissBanner() { BANNER = null; render(); }
// Disable a button while its action runs — instant feedback + double-tap guard.
function busy(btn, label) {
  if (!btn) return () => {};
  const orig = btn.textContent;
  btn.disabled = true; btn.textContent = label;
  return () => { btn.disabled = false; btn.textContent = orig; };
}
function whoami() {
  const v = prompt('Acting as (author id):', S.author);
  if (v) { S.author = v; localStorage.setItem('wb-author', v); }
  return v;
}

// ---------- filters ----------
function buildFilterOptions() {
  const sets = { 'f-type': new Set(), 'f-area': new Set(), 'f-status': new Set(), 'f-phase': new Set(), 'f-lane': new Set() };
  for (const i of S.items) {
    sets['f-type'].add(i.type); if (i.area) sets['f-area'].add(i.area);
    if (i.status) sets['f-status'].add(i.status); if (i.phase) sets['f-phase'].add(String(i.phase));
    if (i.lane) sets['f-lane'].add(i.lane);
  }
  for (const [id, set] of Object.entries(sets)) {
    const sel = document.getElementById(id);
    const cur = sel.value;
    const label = sel.options[0].textContent;
    sel.innerHTML = `<option value="">${label}</option>` +
      Array.from(set).sort().map(v => `<option value="${esc(v)}">${esc(v)}</option>`).join('');
    sel.value = cur;
  }
}
function filteredItems() {
  const q = document.getElementById('f-search').value.trim().toLowerCase();
  const ft = document.getElementById('f-type').value, fa = document.getElementById('f-area').value;
  const fs = document.getElementById('f-status').value, fp = document.getElementById('f-phase').value;
  const fl = document.getElementById('f-lane').value;
  return S.items.filter(i => {
    if (i.type === 'area') return false;
    if (ft && i.type !== ft) return false;
    if (fa && i.area !== fa) return false;
    if (fs && i.status !== fs) return false;
    if (fp && String(i.phase) !== fp) return false;
    if (fl && i.lane !== fl) return false;
    if (q) {
      const hay = (i.id + ' ' + i.title + ' ' + (i.detail || '') + ' ' + (i.evidence || []).join(' ') + ' ' + (i.tags || []).join(' ')).toLowerCase();
      if (!hay.includes(q)) return false;
    }
    return true;
  });
}

// ---------- rendering ----------
function render() {
  const nav = document.getElementById('tabs');
  const openDecisions = S.items.filter(i => i.type === 'decision' && i.status !== 'decided').length +
    S.notes.filter(n => ['waiting_user_decision', 'needs_clarification'].includes(n.state)).length;
  const openNotes = S.notes.filter(n => n.state === 'new').length;
  nav.innerHTML = TABS.map(t => {
    let badge = '';
    if (t === 'Decisions' && openDecisions) badge = `<span class="badge">${openDecisions}</span>`;
    if (t === 'Notes' && openNotes) badge = `<span class="badge">${openNotes}</span>`;
    return `<button class="${S.tab === t ? 'active' : ''}" onclick="setTab('${t}')">${t}${badge}</button>`;
  }).join('');
  const v = document.getElementById('view');
  const items = filteredItems();
  if (S.tab === 'Board') v.innerHTML = renderBoard(items);
  else if (S.tab === 'Graph') v.innerHTML = renderGraph(items);
  else if (S.tab === 'Timeline') v.innerHTML = renderTimeline(items);
  else if (S.tab === 'Assets') v.innerHTML = renderMatrix(items.filter(i => i.type === 'asset'), 'asset');
  else if (S.tab === 'Validation') v.innerHTML = renderMatrix(items.filter(i => i.type === 'validation'), 'validation');
  else if (S.tab === 'Performance') v.innerHTML = renderMatrix(items.filter(i => i.type === 'performance'), 'performance');
  else if (S.tab === 'Decisions') v.innerHTML = renderDecisions();
  else if (S.tab === 'Notes') v.innerHTML = renderNotes();
  else if (S.tab === 'Activity') v.innerHTML = renderActivity();
  else if (S.tab === 'Hub') v.innerHTML = renderHub();
}
function setTab(t) {
  S.tab = t;
  try { localStorage.setItem('wb-tab', t); } catch (_) {}
  render();
}

function cardHtml(i) {
  const open = openNotesFor(i.id).length;
  const nAll = notesFor(i.id).length;
  return `<div class="card" style="border-left-color:${STATUS_COLOR[i.status] || 'var(--dim)'}" onclick="openDrawer('${i.id}')">
    <div class="id">${esc(i.id)}</div>
    <div class="title">${esc(i.title)}</div>
    <div class="chips">
      <span class="chip">${esc(i.type)}</span>
      ${i.area ? `<span class="chip">${esc(i.area)}</span>` : ''}
      ${i.phase ? `<span class="chip">P${esc(i.phase)}</span>` : ''}
      ${i.lane ? `<span class="chip">lane ${esc(i.lane)}</span>` : ''}
      ${(i.deps || []).length ? `<span class="chip">${i.deps.length} deps</span>` : ''}
      ${nAll ? `<span class="chip ${open ? 'note-open' : ''}">✎ ${nAll}${open ? ' (' + open + ' open)' : ''}</span>` : ''}
      ${i.model ? `<span class="chip" title="model that completed this">🤖 ${esc(i.model.replace('claude-', ''))}</span>` : ''}
    </div>
  </div>`;
}

function renderBoard(items) {
  const cols = STATUS_ORDER.filter(s => items.some(i => i.status === s));
  const other = items.filter(i => !STATUS_ORDER.includes(i.status));
  let html = '<div class="columns">';
  for (const s of cols) {
    const list = items.filter(i => i.status === s);
    html += `<div class="col"><h3 style="color:${STATUS_COLOR[s]}">${s} (${list.length})</h3><div class="cards">${list.map(cardHtml).join('')}</div></div>`;
  }
  if (other.length) html += `<div class="col"><h3>other (${other.length})</h3><div class="cards">${other.map(cardHtml).join('')}</div></div>`;
  html += '</div>';
  return items.length ? html : '<div class="empty">No items match the current filters.</div>';
}

function renderGraph(items) {
  const shown = items.slice(0, 180);
  const byId = new Map(shown.map(i => [i.id, i]));
  const colX = { 2: 120, 3: 420, 4: 720, 5: 1020, 6: 1320 };
  const yCount = {};
  const pos = new Map();
  for (const i of shown) {
    const p = colX[i.phase] ? i.phase : 3;
    yCount[p] = (yCount[p] || 0) + 1;
    pos.set(i.id, { x: colX[p], y: 40 + yCount[p] * 34 });
  }
  const H = Math.max(400, 60 + Math.max(0, ...Object.values(yCount)) * 34);
  let edges = '', nodes = '';
  for (const i of shown) {
    for (const d of (i.deps || [])) {
      if (!byId.has(d)) continue;
      const a = pos.get(d), b = pos.get(i.id);
      edges += `<path d="M ${a.x + 150} ${a.y + 10} C ${(a.x + b.x + 150) / 2} ${a.y + 10}, ${(a.x + b.x - 50) / 2} ${b.y + 10}, ${b.x} ${b.y + 10}" fill="none" stroke="var(--line)" stroke-width="1.2" marker-end="url(#arr)"/>`;
    }
  }
  for (const i of shown) {
    const p = pos.get(i.id);
    const c = STATUS_COLOR[i.status] || 'var(--dim)';
    nodes += `<g style="cursor:pointer" onclick="openDrawer('${i.id}')">
      <rect x="${p.x}" y="${p.y}" width="150" height="22" rx="4" fill="var(--panel2)" stroke="${c}"/>
      <text x="${p.x + 6}" y="${p.y + 15}">${esc(i.id.length > 22 ? i.id.slice(0, 21) + '…' : i.id)}</text>
      <title>${esc(i.title)} [${esc(i.status)}]</title></g>`;
  }
  const labels = Object.entries(colX).map(([p, x]) => `<text x="${x}" y="20" style="font-size:13px;fill:var(--dim)">Phase ${p}</text>`).join('');
  return `<div class="panel"><h3>Dependency graph <span class="muted">(${shown.length} shown${items.length > shown.length ? ' of ' + items.length : ''}; columns = phase; hover for title)</span></h3>
    <div style="overflow:auto"><svg width="1520" height="${H}" xmlns="http://www.w3.org/2000/svg">
    <defs><marker id="arr" markerWidth="8" markerHeight="8" refX="7" refY="3" orient="auto"><path d="M0,0 L7,3 L0,6" fill="none" stroke="var(--dim)"/></marker></defs>
    ${labels}${edges}${nodes}</svg></div></div>`;
}

function renderTimeline(items) {
  let html = '';
  for (const phase of [2, 3, 4, 5, 6]) {
    const list = items.filter(i => i.phase === phase).sort((a, b) => (a.area || '').localeCompare(b.area || '') || a.id.localeCompare(b.id));
    if (!list.length) continue;
    html += `<div class="timeline-phase panel"><h4>Phase ${phase} — ${({2:'Architecture contracts',3:'Parallel implementation',4:'Serial integration',5:'Validation & proof',6:'Release hardening'})[phase]} (${list.length})</h4>
      <div class="timeline-items">${list.map(i =>
        `<span class="titem" style="border-left-color:${STATUS_COLOR[i.status] || 'var(--dim)'}" onclick="openDrawer('${i.id}')" title="${esc(i.title)}">${esc(i.id)} ${esc(i.title.length > 46 ? i.title.slice(0, 45) + '…' : i.title)}</span>`).join('')}
      </div></div>`;
  }
  return html || '<div class="empty">No items match.</div>';
}

function renderMatrix(list, kind) {
  if (!list.length) return '<div class="empty">No matching items.</div>';
  const cols = kind === 'performance'
    ? ['ID', 'Metric / gate', 'Budget', 'Measured', 'Verdict', 'Status', 'Notes']
    : kind === 'validation'
      ? ['ID', 'Proof', 'Lane', 'Verdict', 'Artifact', 'Status', 'Notes']
      : ['ID', 'Asset', 'Category', 'Source/evidence', 'Status', 'Notes'];
  let html = `<div class="table-note">Swipe sideways to scroll the table →</div><table><tr>${cols.map(c => `<th>${c}</th>`).join('')}</tr>`;
  for (const i of list.sort((a, b) => a.id.localeCompare(b.id))) {
    const notes = notesFor(i.id).length;
    if (kind === 'performance') {
      html += `<tr class="rowlink" onclick="openDrawer('${i.id}')"><td>${esc(i.id)}</td><td>${esc(i.title)}${i.metric ? `<div class="muted">${esc(i.metric)}</div>` : ''}</td><td>${esc(i.budget || '—')}</td><td>${esc(JSON.stringify(i.measurements || []))}</td><td>${esc(i.verdict || 'not_measured')}</td><td>${statusChip(i.status)}</td><td>${notes || ''}</td></tr>`;
    } else if (kind === 'validation') {
      html += `<tr class="rowlink" onclick="openDrawer('${i.id}')"><td>${esc(i.id)}</td><td>${esc(i.title)}</td><td>${esc(i.lane || '—')}</td><td>${esc(i.verdict || 'not_run')}</td><td class="muted">${esc((i.artifacts || []).join('; ') || '—')}</td><td>${statusChip(i.status)}</td><td>${notes || ''}</td></tr>`;
    } else {
      html += `<tr class="rowlink" onclick="openDrawer('${i.id}')"><td>${esc(i.id)}</td><td>${esc(i.title)}</td><td>${esc((i.tags || [])[0] || '—')}</td><td class="muted">${esc((i.evidence || []).join('; ') || '—')}</td><td>${statusChip(i.status)}</td><td>${notes || ''}</td></tr>`;
    }
  }
  return html + '</table>';
}

function capAgeDays(iso) { const t = new Date(iso).getTime(); return isFinite(t) ? (Date.now() - t) / 86400000 : 1e9; }
function capShortDate(iso) { try { const d = new Date(iso); return isFinite(d.getTime()) ? d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' }) : ''; } catch (_) { return ''; } }
function capDateBuckets(folders) {
  const defs = [
    { label: 'Today', max: 1 },
    { label: 'This week', max: 7 },
    { label: 'This month', max: 31 },
    { label: 'Older', max: Infinity },
  ];
  const out = defs.map(d => ({ label: d.label, folders: [] }));
  for (const f of folders) {
    const age = capAgeDays(f.mtime);
    for (let i = 0; i < defs.length; i++) { if (age < defs[i].max) { out[i].folders.push(f); break; } }
  }
  return out.filter(b => b.folders.length);
}

async function showCaptures(dir) {
  window._capDir = dir;
  const v = document.getElementById('view');
  let data;
  try {
    data = dir === '__recent__' ? await api('/api/captures?mode=recent&limit=60') : await api('/api/captures?dir=' + encodeURIComponent(dir || ''));
  } catch (e) { v.innerHTML = `<div class="empty">Captures failed: ${esc(e.message)}</div>`; return; }
  if (S.tab !== 'Captures') return;
  const crumbs = [];
  crumbs.push(`<span class="opt ${dir === '__recent__' ? 'sel' : ''}" onclick="showCaptures('__recent__')">🕒 Recent</span>`);
  crumbs.push(`<span class="opt ${dir === '' ? 'sel' : ''}" onclick="showCaptures('')">📁 All folders</span>`);
  if (dir && dir !== '__recent__') {
    const parts = dir.split('/');
    for (let i = 0; i < parts.length; i++) {
      crumbs.push(`<span class="opt" onclick="showCaptures('${esc(parts.slice(0, i + 1).join('/'))}')">${esc(parts[i])}</span>`);
    }
  }
  let html = `<div class="panel"><h3>📸 Captures <span class="muted">— proof screenshots and review renders from the repo (read-only). Tap a thumbnail for full size.</span></h3>
    <div class="cap-crumbs">${crumbs.join(' / ')}</div>`;
  if (data.folders.length) {
    // Group capture sets by when they were last updated (newest first); each folder IS a
    // capture/test type. Old sets sink to the bottom and get a "stale" style so out-of-date
    // captures are obvious at a glance.
    for (const b of capDateBuckets(data.folders)) {
      html += `<div class="cap-bucket">${esc(b.label)} <span class="muted">· ${b.folders.length}</span></div>`;
      html += `<div class="cap-grid">${b.folders.map(f => {
        const stale = capAgeDays(f.mtime) > 30;
        const bits = [];
        if (f.imgCount) bits.push(f.imgCount + ' img');
        if (f.subCount) bits.push(f.subCount + ' sub');
        const meta = [bits.join(' · '), capShortDate(f.mtime)].filter(Boolean).join(' · ');
        return `<div class="cap-folder${stale ? ' stale' : ''}" onclick="showCaptures('${esc(f.path)}')" title="last updated ${esc(fmtTs(f.mtime))}">📁<br>${esc(f.name)}${meta ? `<br><span class="cap-fmeta">${esc(meta)}</span>` : ''}</div>`;
      }).join('')}</div>`;
    }
    if (data.images.length) html += '<div style="height:10px"></div>';
  }
  if (data.images.length) {
    html += `<div class="cap-grid">${data.images.map(im => `
      <div class="cap-tile" onclick="openLightbox('${esc(im.path)}','${esc(im.name)}')">
        <img loading="lazy" src="/captures/${encodeURI(im.path)}" alt="${esc(im.name)}">
        <div class="cap-name">${esc(dir === '__recent__' ? im.path : im.name)}<br><span>${fmtTs(im.mtime)}</span></div>
      </div>`).join('')}</div>`;
  } else if (!data.folders.length) {
    html += '<div class="muted">No images here.</div>';
  }
  v.innerHTML = html + '</div>';
  window._capRendered = dir;
}
function openLightbox(relPath, name) {
  document.getElementById('lightbox-img').src = '/captures/' + encodeURI(relPath);
  document.getElementById('lightbox-name').textContent = relPath;
  document.getElementById('lightbox').style.display = 'flex';
}

function proofState(c) {
  if (!c.present) return { cls: 'pending', label: c.required ? 'Required · pending' : 'Optional · pending' };
  if (c.status === 'approved') return { cls: 'approved', label: 'Approved' };
  if (c.status === 'rejected') return { cls: 'rejected', label: 'Needs replacement' };
  return { cls: 'captured', label: 'Captured · review' };
}

function proofList(label, values, cls) {
  if (!values || !values.length) return '';
  return `<div class="${cls || ''}"><b>${esc(label)}:</b> ${values.map(esc).join(' · ')}</div>`;
}

function proofCard(c) {
  const state = proofState(c);
  const image = c.present
    ? `<button class="proof-image" onclick="openLightbox('${esc(c.path)}','${esc(c.label)}')" aria-label="Open ${esc(c.label)} full size">
         <img loading="lazy" src="/captures/${encodeURI(c.path)}" alt="${esc(c.caption || c.label)}">
       </button>`
    : `<div class="proof-placeholder"><span>Capture pending</span><small>${esc(c.view || c.camera || 'required view')}</small></div>`;
  const capturedWithoutProvenance = c.present && !c.authoredAssets.length && !c.provenance.length;
  return `<article class="proof-card ${state.cls}">
    ${image}
    <div class="proof-body">
      <div class="proof-head"><h4>${esc(c.label)}</h4><span class="proof-state ${state.cls}">${esc(state.label)}</span></div>
      <div class="chips">
        ${c.view ? `<span class="chip">${esc(c.view)}</span>` : ''}
        ${c.roomLabels ? '<span class="chip proof-good">room labels</span>' : ''}
        ${c.authoredAssets.length ? `<span class="chip proof-good">${c.authoredAssets.length} authored asset${c.authoredAssets.length === 1 ? '' : 's'}</span>` : ''}
      </div>
      ${c.caption ? `<p>${esc(c.caption)}</p>` : ''}
      ${c.camera ? `<div class="proof-meta"><b>Camera:</b> ${esc(c.camera)}</div>` : ''}
      ${c.proceduralFallbacks.length ? `<div class="proof-warning"><b>Procedural fallback disclosed:</b> ${c.proceduralFallbacks.map(esc).join(' · ')}</div>` : ''}
      ${capturedWithoutProvenance ? '<div class="proof-warning">Captured image is missing authored-asset provenance.</div>' : ''}
      ${(c.provenance.length || c.authoredAssets.length || c.notes) ? `<details>
        <summary>Capture metadata</summary>
        ${proofList('Provenance', c.provenance)}
        ${proofList('Authored assets', c.authoredAssets, 'proof-good-text')}
        ${c.notes ? `<div><b>Notes:</b> ${esc(c.notes)}</div>` : ''}
        ${c.stat ? `<div class="muted">${Math.max(1, Math.round(c.stat.size / 1024))} KiB · ${fmtTs(c.stat.mtime)}</div>` : ''}
      </details>` : ''}
    </div>
  </article>`;
}

async function showVisualProofs(force) {
  if (!force && window._proofLoading) return;
  window._proofLoading = true;
  const v = document.getElementById('view');
  let data;
  try {
    data = await api('/api/visual-proofs');
  } catch (e) {
    if (S.tab === 'Visual Proof') v.innerHTML = `<div class="empty">Visual proof failed: ${esc(e.message)}</div>`;
    window._proofLoading = false;
    return;
  }
  window._proofLoading = false;
  window._proofLoadedAt = Date.now();
  if (S.tab !== 'Visual Proof') return;
  if (!data.ok) {
    v.innerHTML = `<div class="panel proof-error"><h3>Visual proof manifest needs attention</h3>
      <div>${esc(data.error || 'Unknown manifest error')}</div>
      <div class="muted">${esc(data.manifestPath || '')}</div>
      <button onclick="showVisualProofs(true)">Retry</button></div>`;
    return;
  }
  const s = data.summary;
  const groups = [];
  for (const c of data.captures) {
    let group = groups.find(g => g.id === c.group);
    if (!group) { group = { id: c.group, captures: [] }; groups.push(group); }
    group.captures.push(c);
  }
  const summaryClass = s.complete ? 'approved' : 'pending';
  let html = `<div class="proof-gallery">
    <div class="panel proof-summary">
      <div>
        <h3>🖼 ${esc(data.title)}</h3>
        ${data.description ? `<div class="muted">${esc(data.description)}</div>` : ''}
      </div>
      <div class="proof-summary-actions">
        <span class="proof-state ${summaryClass}">${s.present}/${s.required} required views present</span>
        <button onclick="window._proofLoadedAt=0;showVisualProofs(true)">Refresh proof</button>
      </div>
      <div class="proof-local-note">Images remain local in <code>${esc(data.captureRoot)}</code> and are excluded from GitHub. The tracked manifest is <code>${esc(data.manifestPath)}</code>.</div>
    </div>`;
  if (!groups.length) html += '<div class="empty">No required proof views are defined.</div>';
  for (const group of groups) {
    const title = group.id.replace(/[-_]+/g, ' ').replace(/\b\w/g, c => c.toUpperCase());
    const present = group.captures.filter(c => c.present).length;
    html += `<section class="proof-group"><h3>${esc(title)} <span class="muted">${present}/${group.captures.length}</span></h3>
      <div class="proof-grid">${group.captures.map(proofCard).join('')}</div></section>`;
  }
  v.innerHTML = html + '</div>';
}

function renderShopping() {
  const list = S.items.filter(i => i.type === 'shopping').sort((a, b) => a.id.localeCompare(b.id));
  const open = list.filter(i => i.status !== 'decided' && i.status !== 'deferred');
  const done = list.filter(i => i.status === 'decided' || i.status === 'deferred');
  let html = '';
  if (BANNER) html += `<div class="banner"><div>${BANNER}</div><span class="x" onclick="dismissBanner()">✕</span></div>`;
  html += `<div class="panel"><h3>🛒 Asset shopping list <span class="muted">— candidate purchases with why + what they unblock. Tap an option + Submit like a decision; add store links or alternatives in the comment. Prices are approximate and worth re-checking at the store page.</span></h3>`;
  html += open.length ? open.map(decisionCard).join('') : '<div class="muted">Nothing pending purchase review.</div>';
  html += '</div>';
  if (done.length) html += `<div class="panel"><h3>Reviewed (${done.length})</h3>${done.map(decidedCard).join('')}</div>`;
  return html;
}

function renderDecisions() {
  const decisions = S.items.filter(i => i.type === 'decision').sort((a, b) => a.id.localeCompare(b.id));
  const open = decisions.filter(d => d.status !== 'decided' && d.status !== 'deferred');
  const done = decisions.filter(d => d.status === 'decided' || d.status === 'deferred');
  const waiting = S.notes.filter(n => ['waiting_user_decision', 'needs_clarification'].includes(n.state));
  let html = '';
  if (BANNER) html += `<div class="banner"><div>${BANNER}</div><span class="x" onclick="dismissBanner()">✕</span></div>`;
  if (waiting.length) {
    html += `<div class="panel"><h3>Questions waiting on you (${waiting.length})</h3>${waiting.map(inboxCard).join('')}</div>`;
  }
  html += `<div class="panel"><h3>Open decisions (${open.length}) <span class="muted">— tap an option, add a comment if you want, then Submit. Tap the grey text to expand context.</span></h3>`;
  html += open.length ? open.map(decisionCard).join('') : '<div class="muted">Nothing open. 🎉</div>';
  html += '</div>';
  if (done.length) {
    html += `<div class="panel"><h3>Resolved (${done.length})</h3>${done.map(decidedCard).join('')}</div>`;
  }
  return html;
}

function inboxCard(n) {
  return `<div class="dcard">
    <h4><span class="id">${esc(n.id)}</span> about <a href="#" onclick="event.preventDefault();openDrawer('${esc(n.target)}')">${esc(n.target)}</a></h4>
    <div class="detail full">${linkifyEsc(n.text)}<span class="muted"> — ${esc(n.author)}, ${fmtTs(n.ts)}</span></div>
    <textarea id="ans-${n.id}" placeholder="Your answer…"></textarea>
    <div class="row"><button class="primary" onclick="answerInbox('${n.id}','${esc(n.target)}',this)">Send answer</button></div>
  </div>`;
}

function decisionCard(d) {
  const sel = (window._decSel || {})[d.id];
  const opts = (d.options || []).map((o, i) =>
    `<span class="opt ${sel === i ? 'sel' : ''}" data-opt="${d.id}" onclick="pickOpt('${d.id}',${i})" title="${esc(o.tradeoff)}">${esc(o.label)}</span>`).join('');
  return `<div class="dcard">
    <h4><span class="id" style="cursor:pointer" onclick="openDrawer('${d.id}')">${esc(d.id)}</span>${esc(d.title)}</h4>
    ${d.detail ? `<div class="detail" onclick="if(event.target.tagName!=='A')this.classList.toggle('full')">${linkifyEsc(d.detail)}</div>` : ''}
    ${opts ? `<div>${opts}</div>` : ''}
    <textarea id="dc-${d.id}" placeholder="Optional comment / condition / your own answer…"></textarea>
    <div class="row">
      <button class="primary" onclick="decideDecision('${d.id}',this)">Submit decision</button>
      <button onclick="commentDecision('${d.id}',this)">Comment only</button>
      <span class="muted">${notesFor(d.id).length ? notesFor(d.id).length + ' note(s)' : ''}</span>
    </div>
  </div>`;
}

function decidedCard(d) {
  return `<div class="dcard decided">
    <h4><span class="id" style="cursor:pointer" onclick="openDrawer('${d.id}')">${esc(d.id)}</span>${esc(d.title)}</h4>
    <div class="detail full">✓ ${esc(d.resolution || d.decision || 'decided')}</div>
    <div class="row"><button onclick="reopenDecision('${d.id}',this)">Reopen</button><span class="muted">${notesFor(d.id).length ? notesFor(d.id).length + ' note(s)' : ''}</span></div>
  </div>`;
}

function pickOpt(id, i) {
  window._decSel = window._decSel || {};
  window._decSel[id] = window._decSel[id] === i ? null : i;
  document.querySelectorAll(`[data-opt="${id}"]`).forEach((el, idx) => el.classList.toggle('sel', window._decSel[id] === idx));
}

async function answerInbox(noteId, target, btn) {
  if (btn && btn.disabled) return;
  const ta = document.getElementById('ans-' + noteId);
  const text = (ta && ta.value || '').trim();
  if (!text) return toast('Write an answer first', 'err');
  const done = busy(btn, 'Sending…');
  try {
    const r = await post('/api/notes', { target, author: S.author, text: `ANSWER to ${noteId}: ${text}` });
    await post('/api/notes/state', { id: noteId, state: 'superseded', by: S.author, reason: 'answered by user in workbench', supersededBy: r.id });
    setBanner(`✓ Answer sent as <b>${esc(r.id)}</b> — the question is retired and linked to your reply.`);
    toast(`Answer sent (${r.id}) ✓`, 'ok');
    await loadAll();
  } catch (e) {
    toast('Failed to send answer: ' + e.message, 'err');
  } finally { done(); }
}

async function decideDecision(id, btn) {
  if (btn && btn.disabled) return;
  const d = S.items.find(x => x.id === id);
  const selIdx = (window._decSel || {})[id];
  const comment = (document.getElementById('dc-' + id) || { value: '' }).value.trim();
  const choice = (selIdx === null || selIdx === undefined) ? null : (d.options || [])[selIdx];
  if (!choice && !comment) return toast('Tap an option or write a comment first', 'err');
  const choiceText = choice ? `${choice.id}: ${choice.label}` : '';
  const resolution = [choiceText, comment].filter(Boolean).join(' — ');
  const done = busy(btn, 'Submitting…');
  try {
    await post('/api/roadmap/update', { id, patch: { status: 'decided', resolution }, by: S.author, reason: 'decided by user via workbench Decisions tab' });
    await post('/api/notes', { target: id, author: S.author, text: `DECISION: ${resolution}` });
    setBanner(`✓ <b>${esc(id)}</b> decided: ${esc(resolution)}<br><span class="muted">Recorded — the next AI session will read it. Changed your mind? Reopen it in the Resolved list below.</span>`);
    toast(`${id} decided ✓`, 'ok');
    await loadAll();
  } catch (e) {
    toast(`Failed to submit ${id}: ` + e.message, 'err');
  } finally { done(); }
}

async function commentDecision(id, btn) {
  if (btn && btn.disabled) return;
  const comment = (document.getElementById('dc-' + id) || { value: '' }).value.trim();
  if (!comment) return toast('Write a comment first', 'err');
  const done = busy(btn, 'Adding…');
  try {
    const r = await post('/api/notes', { target: id, author: S.author, text: comment });
    setBanner(`✓ Comment added to <b>${esc(id)}</b> as note <b>${esc(r.id)}</b> — it stays open, and the next AI session will read your comment.`);
    toast(`Comment added to ${id} ✓`, 'ok');
    await loadAll();
  } catch (e) {
    toast('Failed to add comment: ' + e.message, 'err');
  } finally { done(); }
}

async function reopenDecision(id, btn) {
  if (btn && btn.disabled) return;
  const done = busy(btn, 'Reopening…');
  try {
    await post('/api/roadmap/update', { id, patch: { status: 'open' }, by: S.author, reason: 'reopened by user via workbench' });
    setBanner(`↩ <b>${esc(id)}</b> reopened — it's back in the Open list for a new answer.`);
    toast(`${id} reopened`, 'ok');
    await loadAll();
  } catch (e) {
    toast('Failed to reopen: ' + e.message, 'err');
  } finally { done(); }
}

function noteHtml(n) {
  const transitions = n.state === 'new'
    ? [['acknowledged', 'ack']]
    : n.state === 'acknowledged'
      ? [
        ['incorporated', 'incorporate'], ['rejected_with_reason', 'reject'],
        ['needs_clarification', 'needs-clarification'],
        ['waiting_user_decision', 'wait-user'], ['superseded', 'supersede'],
      ]
      : ['needs_clarification', 'waiting_user_decision'].includes(n.state)
        ? [['acknowledged', 'ack'], ['superseded', 'supersede']]
        : [];
  const actions = transitions.map(([st, label]) =>
    `<button onclick="event.stopPropagation();setNoteState('${n.id}','${st}')">${label}</button>`).join('');
  return `<div class="note">
    <div class="head"><b>${esc(n.id)}</b> ${nstateChip(n.state)} <span>→ <a href="#" onclick="event.preventDefault();openDrawer('${esc(n.target)}')">${esc(n.target)}</a></span>
      <span>by ${esc(n.author)}</span><span>${fmtTs(n.ts)}</span>${n.supersededBy ? `<span>superseded by ${esc(n.supersededBy)}</span>` : ''}</div>
    <div class="text">${linkifyEsc(n.text)}</div>
    ${actions ? `<div class="actions">${actions}</div>` : ''}
    ${n.history.length > 1 ? `<div class="history">${n.history.map(h => `${fmtTs(h.ts)} → <b>${esc(h.state)}</b> by ${esc(h.by)}${h.reason ? ' — ' + esc(h.reason) : ''}`).join('<br>')}</div>` : ''}
  </div>`;
}

function renderNotes() {
  const stateSel = window._noteFilter || '';
  const list = S.notes.filter(n => !stateSel || n.state === stateSel).sort((a, b) => b.id.localeCompare(a.id));
  const options = ['', ...Object.keys(NSTATE_COLOR)].map(s => `<option value="${s}" ${s === stateSel ? 'selected' : ''}>${s || 'state: all'}</option>`).join('');
  return `<div>
      <div class="panel"><h3>Add note</h3>
        <div style="display:flex;gap:6px;flex-wrap:wrap">
          <input id="note-target" list="all-ids" placeholder="target id (e.g. T-ASSETS-001, GENERAL)" size="24">
          <datalist id="all-ids">${S.items.map(i => `<option value="${esc(i.id)}">${esc(i.title)}</option>`).join('')}</datalist>
          <input id="note-author" placeholder="author" value="${esc(S.author)}" size="12">
          <input id="note-text" placeholder="note text…" style="flex:1;min-width:220px">
          <button class="primary" onclick="addNote()">Add</button>
        </div>
      </div>
      <div class="panel"><h3>Notes <select onchange="window._noteFilter=this.value;render()">${options}</select> <span class="muted">(${list.length})</span></h3>
        ${list.map(noteHtml).join('') || '<div class="muted">No notes.</div>'}
      </div>
  </div>`;
}

function renderActivity() {
  return `<div class="panel"><h3>Activity / changelog <span class="muted">(${S.changelog.length})</span></h3><div class="feed">
    ${S.changelog.map(ev => `<div class="ev"><span class="ts">#${ev.seq} ${fmtTs(ev.ts)}</span> <b>${esc(ev.kind)}</b> by ${esc(ev.by)}<br>${esc(changeSummary(ev))}</div>`).join('') || '<div class="muted">No activity yet.</div>'}
  </div></div>`;
}

function changeSummary(ev) {
  if (ev.kind === 'item_update') return `${ev.id}: ` + Object.entries(ev.changes || {}).map(([k, v]) => `${k}: ${JSON.stringify(v.from)} → ${JSON.stringify(v.to)}`).join('; ') + (ev.reason ? ` (${ev.reason})` : '');
  if (ev.kind === 'item_create') return `${ev.id}: ${(ev.item && ev.item.title) || ev.title || ''}${ev.reason ? ' (' + ev.reason + ')' : ''}`;
  if (ev.kind === 'note_add') return `${ev.note} on ${ev.target}: ${ev.preview || ''}`;
  if (ev.kind === 'note_state') return `${ev.note} → ${ev.state}${ev.reason ? ' (' + ev.reason + ')' : ''}`;
  if (ev.kind === 'import') return `${ev.created} created, ${ev.updated} updated${ev.reason ? ' (' + ev.reason + ')' : ''}`;
  if (ev.kind === 'export') return (ev.files || []).join(', ');
  return JSON.stringify(Object.assign({}, ev, { seq: undefined, ts: undefined, kind: undefined, by: undefined }));
}

function phonePanel() {
  const urls = (S.meta && S.meta.lanUrls) || [];
  if (!urls.length) return '';
  const preferred = urls.find(u => u.recommended) || urls[0];
  return `<div class="panel"><h3>📱 Open on your phone (same Wi-Fi)</h3>
    <a class="phone-link" href="${esc(preferred.url)}" target="_blank" rel="noopener">${esc(preferred.url)} <span class="muted" style="font-size:12px">(${esc(preferred.iface)})</span></a>
    <div class="phone-actions">
      <button class="primary" onclick="copyPhoneUrl('${esc(preferred.url)}',this)">Copy link</button>
      <button onclick="sharePhoneUrl('${esc(preferred.url)}')">Share…</button>
      <button onclick="openPhoneSheet()">All addresses</button>
    </div>
    <div class="muted">Connect the phone to the same Wi-Fi, then type, copy, or share the highlighted address. Decisions are touch-sized and the last-open tab is remembered.</div>
  </div>`;
}

function phoneSheetHtml() {
  const urls = (S.meta && S.meta.lanUrls) || [];
  if (!urls.length) return '<div class="empty">No LAN address was detected. Check that Wi-Fi or Ethernet is connected, then refresh.</div>';
  const preferred = urls.find(u => u.recommended) || urls[0];
  const alternatives = urls.filter(u => u !== preferred);
  return `<button class="phone-sheet-close" onclick="closePhoneSheet()" aria-label="Close">✕</button>
    <h2>Open Workbench on your phone</h2>
    <p>On the same Wi-Fi, open this address:</p>
    <a class="phone-link recommended" href="${esc(preferred.url)}" target="_blank" rel="noopener">${esc(preferred.url)}</a>
    <div class="phone-actions">
      <button class="primary" onclick="copyPhoneUrl('${esc(preferred.url)}',this)">Copy link</button>
      <button onclick="sharePhoneUrl('${esc(preferred.url)}')">Share to device…</button>
    </div>
    <ol class="phone-steps">
      <li>Keep this Workbench window/server running.</li>
      <li>Connect the phone to the same Wi-Fi as this PC.</li>
      <li>Open the address above in Safari or Chrome.</li>
      <li>Optionally use <b>Add to Home Screen</b> for one-tap return.</li>
    </ol>
    ${alternatives.length ? `<details><summary>Other network addresses</summary>${alternatives.map(u =>
      `<a class="phone-link alternate" href="${esc(u.url)}" target="_blank" rel="noopener">${esc(u.url)} <span>(${esc(u.iface)})</span></a>`).join('')}</details>` : ''}
    <p class="muted">VPN/virtual adapters may only work for their peers. If the preferred address does not load, try the address labeled Wi-Fi or Ethernet.</p>`;
}

function openPhoneSheet() {
  document.getElementById('phone-sheet-body').innerHTML = phoneSheetHtml();
  document.getElementById('phone-sheet').classList.add('open');
}
function closePhoneSheet() { document.getElementById('phone-sheet').classList.remove('open'); }
async function copyPhoneUrl(url, btn) {
  try {
    if (navigator.clipboard && window.isSecureContext) await navigator.clipboard.writeText(url);
    else {
      const ta = document.createElement('textarea');
      ta.value = url; ta.setAttribute('readonly', ''); ta.style.position = 'fixed'; ta.style.opacity = '0';
      document.body.appendChild(ta); ta.select();
      if (!document.execCommand('copy')) throw new Error('copy unavailable');
      ta.remove();
    }
    toast('Phone link copied ✓', 'ok');
    if (btn) { const old = btn.textContent; btn.textContent = 'Copied ✓'; setTimeout(() => { btn.textContent = old; }, 1800); }
  } catch (_) { prompt('Copy this Workbench address:', url); }
}
async function sharePhoneUrl(url) {
  if (navigator.share) {
    try { await navigator.share({ title: 'Perfect Dark 2 Workbench', text: 'Open the Workbench', url }); return; }
    catch (e) { if (e && e.name === 'AbortError') return; }
  }
  copyPhoneUrl(url);
}

function renderHub() {
  const c = S.coord;
  if (!c || !c.available) return phonePanel() + '<div class="empty">Coordination state not found (.codex-coordination/state.json). The CodexCoordination hub owns sessions/queue — see AGENTS.md.</div>';
  const st = c.state || {};
  const sessions = st.sessions || [];
  const queue = st.queue || [];
  let html = phonePanel();
  html += `<div class="panel"><h3>Coordination hub <span class="muted">(read-only mirror of .codex-coordination/state.json, mtime ${fmtTs(c.mtime)}; writes go through Tools/CodexCoordination/CodexCoordination.ps1)</span></h3>`;
  html += '<h3>Sessions</h3><table><tr><th>ID</th><th>Status</th><th>Goal</th><th>Current task</th><th>ETA</th><th>Last update</th></tr>';
  for (const s of sessions) {
    html += `<tr><td>${esc(s.id || s.sessionId)}</td><td>${esc(s.status)}</td><td>${esc(s.goal || '')}</td><td>${esc(s.currentTask || '')}</td><td>${esc(s.eta || '')}</td><td class="muted">${fmtTs(s.updatedUtc || s.lastUpdateUtc || s.lastSeenUtc)}</td></tr>`;
  }
  html += '</table><h3 style="margin-top:12px">Queue</h3>';
  if (!queue.length) html += '<div class="muted">Queue empty.</div>';
  else {
    html += '<table><tr><th>Queue ID</th><th>Session</th><th>Type</th><th>Resource</th><th>Title</th><th>State</th></tr>';
    for (const q of queue) {
      html += `<tr><td>${esc(q.id)}</td><td>${esc(q.sessionId)}</td><td>${esc(q.type)}</td><td>${esc(q.resource)}</td><td>${esc(q.title)}</td><td>${esc(q.state || (q.startedUtc ? 'running' : 'pending'))}</td></tr>`;
    }
    html += '</table>';
  }
  return html + '</div>';
}

// ---------- drawer ----------
function openDrawer(id) {
  const i = S.items.find(x => x.id === id);
  const body = document.getElementById('drawer-body');
  if (!i) { body.innerHTML = `<h2>${esc(id)}</h2><div class="muted">Free-form target (not a roadmap item).</div>` + drawerNotes(id); }
  else {
    const statusOptions = STATUS_ORDER.map(s => `<option ${i.status === s ? 'selected' : ''}>${s}</option>`).join('');
    body.innerHTML = `
      <div class="id muted">${esc(i.id)} · ${esc(i.type)}</div>
      <h2>${esc(i.title)}</h2>
      <div>${statusChip(i.status)} ${i.verdict ? '· verdict: ' + esc(i.verdict) : ''}</div>
      <div class="kv">
        <span class="k">area</span><span>${esc(i.area || '—')}</span>
        <span class="k">phase / lane</span><span>P${esc(i.phase || '—')} / ${esc(i.lane || '—')}</span>
        <span class="k">owner</span><span>${esc(i.owner || '—')}</span>
        <span class="k">model</span><span>${esc(i.model || '—')}</span>
        ${i.metric ? `<span class="k">metric</span><span>${esc(i.metric)}</span>` : ''}
        ${i.budget ? `<span class="k">budget</span><span>${esc(i.budget)}</span>` : ''}
        ${(i.measurements || []).length ? `<span class="k">measurements</span><span>${esc(JSON.stringify(i.measurements))}</span>` : ''}
        ${(i.artifacts || []).length ? `<span class="k">artifacts</span><span>${esc(i.artifacts.join('; '))}</span>` : ''}
        ${i.resolution ? `<span class="k">resolution</span><span>${esc(i.resolution)}</span>` : ''}
        <span class="k">updated</span><span>${fmtTs(i.updated)}</span>
      </div>
      ${i.detail ? `<div class="panel" style="white-space:pre-line">${linkifyEsc(i.detail)}</div>` : ''}
      ${(i.evidence || []).length ? `<div class="panel"><h3>Evidence</h3>${i.evidence.map(e => `<div class="muted" style="font-family:Consolas,monospace;font-size:11px">${esc(e)}</div>`).join('')}</div>` : ''}
      ${(i.deps || []).length ? `<div class="panel"><h3>Depends on</h3>${i.deps.map(d => `<a href="#" onclick="event.preventDefault();openDrawer('${esc(d)}')">${esc(d)}</a> `).join('')}</div>` : ''}
      <div class="panel"><h3>Change status</h3>
        <select id="drawer-status">${statusOptions}</select>
        <input id="drawer-reason" placeholder="reason" size="24">
        <button class="primary" onclick="updateStatus('${i.id}')">Apply</button>
      </div>` + drawerNotes(id);
  }
  document.getElementById('drawer').classList.add('open');
}
function drawerNotes(id) {
  const list = notesFor(id);
  return `<div class="panel"><h3>Notes on ${esc(id)} (${list.length})</h3>
    ${list.map(noteHtml).join('') || '<div class="muted">None yet.</div>'}
    <div style="display:flex;gap:6px;margin-top:8px">
      <input id="drawer-note-text" placeholder="add note…" style="flex:1">
      <button class="primary" onclick="addNoteFromDrawer('${esc(id)}')">Add</button>
    </div></div>`;
}
function closeDrawer() { document.getElementById('drawer').classList.remove('open'); }

// ---------- actions ----------
async function addNote() {
  const target = document.getElementById('note-target').value.trim();
  const author = document.getElementById('note-author').value.trim() || S.author;
  const text = document.getElementById('note-text').value.trim();
  if (!target || !text) return toast('target and text required', 'err');
  S.author = author; localStorage.setItem('wb-author', author);
  try {
    const r = await post('/api/notes', { target, author, text });
    toast(`Added ${r.id} ✓`, 'ok');
    await loadAll();
  } catch (e) { toast('Failed to add note: ' + e.message, 'err'); }
}
async function addNoteFromDrawer(target) {
  const text = document.getElementById('drawer-note-text').value.trim();
  if (!text) return toast('note text required', 'err');
  try {
    const r = await post('/api/notes', { target, author: S.author, text });
    toast(`Added ${r.id} ✓`, 'ok');
    await loadAll(); openDrawer(target);
  } catch (e) { toast('Failed to add note: ' + e.message, 'err'); }
}
async function setNoteState(id, state) {
  let reason = null, supersededBy = null;
  if (state === 'rejected_with_reason') { reason = prompt('Rejection reason (required):'); if (!reason) return; }
  else if (state === 'superseded') { supersededBy = prompt('Superseding note id (required):'); if (!supersededBy) return; }
  else { reason = prompt('Optional reason (Enter to skip):') || null; }
  try {
    await post('/api/notes/state', { id, state, by: S.author, reason, supersededBy });
    toast(`${id} → ${state} ✓`, 'ok');
    await loadAll();
  } catch (e) { toast('Error: ' + e.message, 'err'); }
}
async function updateStatus(id) {
  const status = document.getElementById('drawer-status').value;
  const reason = document.getElementById('drawer-reason').value.trim() || null;
  try {
    await post('/api/roadmap/update', { id, patch: { status }, by: S.author, reason });
    toast(`${id} status → ${status} ✓`, 'ok');
    await loadAll(); openDrawer(id);
  } catch (e) { toast('Failed to update status: ' + e.message, 'err'); }
}

// ---------- wiring ----------
for (const id of ['f-search', 'f-type', 'f-area', 'f-status', 'f-phase', 'f-lane']) {
  document.getElementById(id).addEventListener('input', render);
}
document.getElementById('btn-clear').onclick = () => {
  for (const id of ['f-search', 'f-type', 'f-area', 'f-status', 'f-phase', 'f-lane']) document.getElementById(id).value = '';
  render();
};
document.getElementById('btn-refresh').onclick = loadAll;
document.getElementById('btn-export').onclick = async () => {
  try {
    const r = await post('/api/export', { by: S.author });
    toast('Exported: ' + Object.values(r.files).join(', '), 'ok');
  } catch (e) { toast('Export failed: ' + e.message, 'err'); }
};
document.getElementById('btn-phone').onclick = openPhoneSheet;
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') { closeDrawer(); closePhoneSheet(); }
});

loadAll().catch(e => { document.getElementById('view').innerHTML = `<div class="empty">Failed to load: ${esc(e.message)}</div>`; });
// Auto-refresh, but never while the user is typing — a re-render would wipe their input.
setInterval(() => {
  const el = document.activeElement;
  const typing = el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT');
  if (document.visibilityState === 'visible' && !typing) loadAll().catch(() => {});
}, 30000);
