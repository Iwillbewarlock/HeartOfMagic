// Draws every converted archetype twice on one page: SR Prisma's original SVG
// and the polylines read back from the generated ArchetypeIcons.h, so a person
// can compare them.
//
//   node tools/sr-icons/preview.mjs <copy of skald-icons.js> [ArchetypeIcons.h] [preview.html]
//
// The header is parsed (not the converter's in-memory result), so the page
// shows what the plugin will actually draw.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadIcons, iconSvg, CONVERTED, GRID, STROKE_WIDTH } from './convert.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const PX = 96;
const COLUMNS = 6;

const NAMES = ['Concentration Casting', 'Fire and Forget Casting', 'Acid', 'Air', 'Apparitions', 'Arcane Energy', 'Armor', 'Magical Constructs', 'Creatures', 'Daedra', 'Disease', 'Earth', 'Fire', 'Flesh', 'Force', 'Frost', 'Health', 'Humans', 'Life', 'Light', 'Magicka', 'Metal', 'Nature', 'Poison', 'Resistance', 'Shadow', 'Shields', 'Shock', 'Souls', 'Stamina', 'Sunlight', 'Time', 'Traps', 'Undead', 'Water', 'Weapons', 'Alteration Spells', 'Conjuration Spells', 'Destruction Spells', 'Illusion Spells', 'Restoration Spells', 'Actor Targeting', 'Area Targeting', 'Location Targeting', 'Self Targeting', 'Cloaking Techniques', 'Controlling Techniques', 'Courage Techniques', 'Curing Techniques', 'Cursing Techniques', 'Fear Techniques', 'Frenzy Techniques', 'Infusing Techniques', 'Pacifying Techniques', 'Sensing Techniques', 'Siphoning Techniques', 'Strengthening Techniques', 'Summoning Techniques', 'Telekinesis Techniques', 'Transforming Techniques'];

/** Reads kPoints / kSubpaths / kIcons back out of the generated header. */
function readHeader(text) {
  const block = (name) => {
    const m = text.match(new RegExp(`${name}\\{ \\{([\\s\\S]*?)\\} \\};`));
    if (!m) { throw new Error(`ArchetypeIcons.h: ${name} not found; regenerate it with convert.mjs`); }
    return m[1].replace(/\/\/[^\n]*/g, '');
  };
  const tuples = (s) => [...s.matchAll(/\{([^{}]*)\}/g)].map((m) => m[1].split(',').map((v) => v.trim()));
  const points = tuples(block('kPoints')).map(([x, y]) => [parseFloat(x), parseFloat(y)]);
  const subs = tuples(block('kSubpaths')).map(([f, c, cl, fi]) => ({ first: +f, count: +c, closed: cl === 'true', fill: fi === 'true' }));
  const icons = tuples(block('kIcons')).map(([f, c]) => ({ first: +f, count: +c }));
  return { points, subs, icons };
}

function convertedSvg(h, i) {
  const e = h.icons[i];
  const parts = [];
  for (let k = e.first; k < e.first + e.count; k++) {
    const s = h.subs[k];
    const pts = h.points.slice(s.first, s.first + s.count).map((p) => p.join(',')).join(' ');
    const tag = s.closed ? 'polygon' : 'polyline';
    parts.push(s.fill ? `<${tag} points="${pts}" fill="currentColor" stroke="none"/>` : `<${tag} points="${pts}"/>`);
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${GRID} ${GRID}" width="${PX}" height="${PX}" fill="none" stroke="currentColor" stroke-width="${STROKE_WIDTH}" stroke-linecap="round" stroke-linejoin="round">${parts.join('')}</svg>`;
}

function main([jsPath, hdrArg, outArg]) {
  if (!jsPath) { console.error('usage: node tools/sr-icons/preview.mjs <copy of skald-icons.js> [ArchetypeIcons.h] [preview.html]'); process.exit(2); }
  const icons = loadIcons(jsPath);
  const hdrPath = path.resolve(hdrArg || path.join(HERE, '../../plugins/SL_MagicMenu_SpellResearch/src/ArchetypeIcons.h'));
  const h = readHeader(fs.readFileSync(hdrPath, 'utf8'));
  const cells = CONVERTED.map((i) => {
    const e = h.icons[i];
    const n = h.subs.slice(e.first, e.first + e.count).reduce((a, s) => a + s.count, 0);
    return `<div class="cell"><div class="pair"><div>${iconSvg(icons, i, PX)}<span>SVG</span></div><div>${convertedSvg(h, i)}<span>polyline</span></div></div>` +
      `<div class="name">${i} ${NAMES[i]}<br>${e.count} subpaths, ${n} points</div></div>`;
  });
  const html = `<!doctype html>
<html><head><meta charset="utf-8"><title>SR archetype icons</title>
<style>
  html, body { margin: 0; background: #0b0b0c; color: #cfc9b8; font: 12px sans-serif; }
  body { padding: 12px; }
  h1 { font-size: 14px; font-weight: normal; margin: 0 0 10px; color: #8f8a7e; }
  .grid { display: grid; grid-template-columns: repeat(${COLUMNS}, ${2 * PX + 28}px); gap: 10px 14px; }
  .pair { display: flex; gap: 8px; }
  .pair div { display: flex; flex-direction: column; align-items: center; color: #6f6a60; font-size: 10px; }
  .name { color: #8f8a7e; font-size: 11px; margin-top: 2px; }
</style></head><body>
<h1>Left: SR Prisma SVG (skald-icons.js). Right: polylines read back from ArchetypeIcons.h. ${CONVERTED.length} icons, ${h.points.length} stored points.</h1>
<div class="grid">
${cells.join('\n')}
</div></body></html>
`;
  const out = path.resolve(outArg || path.join(HERE, 'preview.html'));
  fs.writeFileSync(out, html);
  console.log(`wrote ${out}`);
}

main(process.argv.slice(2));
