#!/usr/bin/env node
'use strict';

const base = process.env.WORKBENCH_URL || 'http://127.0.0.1:8378';
const by = process.env.WORKBENCH_AUTHOR || 'codex-pd2-workbench-assets-20260730';

const families = [
  ['pdweapon', 'weapon.ini'],
  ['pdprojectile', 'projectile.ini'],
  ['pdentity', 'entity.ini'],
  ['pdmaterial', 'material.ini'],
  ['pdtexture', 'texture.ini'],
  ['pdcharacter', 'character.ini'],
  ['pdhead', 'head.ini'],
  ['pdbody', 'body.ini'],
  ['pdarena', 'arena.ini'],
  ['pdscenario', 'scenario.ini'],
  ['pdmesh', 'mesh.ini'],
  ['pdanim', 'animation.ini'],
  ['pdsfx', 'sound.ini'],
  ['pdvoice', 'voice.ini'],
  ['pdsong', 'music.ini'],
  ['pdui', 'ui.ini'],
  ['pdfont', 'font.ini'],
  ['pdlang', 'lang.ini'],
  ['pdskin', 'skin.ini'],
  ['pdeffect', 'effect.ini'],
  ['pdprop', 'prop.ini'],
  ['pdvehicle', 'vehicle.ini'],
  ['pdmission', 'mission.ini'],
  ['pdgamemode', 'gamemode.ini'],
  ['pdbotprofile', 'botprofile.ini'],
  ['pdhud', 'hud.ini'],
  ['pdtheme', 'theme.ini'],
];

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
  const roadmap = await (await fetch(base + '/api/roadmap')).json();
  const existing = new Map();
  for (const item of roadmap.items) {
    for (const tag of item.tags || []) {
      if (tag.startsWith('asset-family:')) existing.set(tag.slice(13), item.id);
    }
  }
  const ids = [];
  for (const [family, descriptor] of families) {
    if (existing.has(family)) {
      ids.push([family, existing.get(family), 'existing']);
      continue;
    }
    const created = await post('/api/roadmap/item', {
      by,
      reason: `Create the durable all-family audit row for .${family}.`,
      item: {
        type: 'asset',
        title: `.${family} public source and production runtime chain`,
        area: 'ASSETS',
        status: 'partial',
        phase: 1,
        lane: 'family-audit',
        owner: by,
        deps: ['T-ASSETS-001'],
        evidence: [],
        detail: [
          `Public descriptor: ${descriptor}`,
          'Audit fields: extractor completeness; public schema/readability; self-contained dependencies; catalog identity; provider load; production runtime adapter; private source-hashed cache boundary; retained/exemplar archives; creator save/import/add flow; automated and live proof.',
          'This row remains partial until current-tree evidence is attached and every confirmed gap is fixed.',
        ].join('\n\n'),
        tags: ['asset-family', `asset-family:${family}`, `descriptor:${descriptor}`],
      },
    });
    ids.push([family, created.id, 'created']);
  }
  console.log(JSON.stringify({ count: ids.length, ids }, null, 2));
}

run().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
