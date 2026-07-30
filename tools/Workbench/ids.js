'use strict';
/*
 * Roadmap id helpers — the single place that understands "<prefix>-<NNN>" indexing.
 *
 * Roadmap items are ided as a prefix + a zero-padded incrementing number:
 *   T-GPUSIM-006, T-NET-013, D-0004, R-0007, V-0006, P-0001, SHOP-011, A-ASSETS-002, ...
 * Some ids in a series carry a WORD suffix instead of a number (T-GPUSIM-FULL,
 * T-WORLD-CIVIC, D-ECON-EMPLOYER) — those are not part of the numeric sequence and are
 * ignored when picking the next index.
 *
 * Kept in its own zero-dependency module so both server.js and tests can use it.
 */

// Next free "<prefix>-<NNN>" id for a series, preserving the series' existing zero-pad width.
// nextIndexedId(items, 'T-GPUSIM') -> 'T-GPUSIM-007' when 006 is the highest numeric member.
// A brand-new series (no numeric members yet) defaults to 3-wide padding: '<prefix>-001'.
function nextIndexedId(items, prefix) {
  const re = new RegExp('^' + String(prefix).replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '-(\\d+)$');
  let max = 0, width = 0;
  for (const it of items || []) {
    const m = re.exec((it && it.id) || '');
    if (m) {
      max = Math.max(max, parseInt(m[1], 10));   // numeric compare, so 'X-10' > 'X-9'
      width = Math.max(width, m[1].length);       // keep the series' padding (006 -> 007, not 7)
    }
  }
  return String(prefix) + '-' + String(max + 1).padStart(width || 3, '0');
}

// Ids that appear on more than one item, with their occurrence count. [] when every id is unique.
function findDuplicateIds(items) {
  const counts = new Map();
  for (const it of items || []) {
    const id = it && it.id;
    if (id == null) continue;
    counts.set(id, (counts.get(id) || 0) + 1);
  }
  const dups = [];
  for (const [id, count] of counts) if (count > 1) dups.push({ id, count });
  return dups;
}

module.exports = { nextIndexedId, findDuplicateIds };
