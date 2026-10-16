// Every Address Library id Black Flame uses (its own and the CommonLib functions it reaches), against every
// database in CommonLib tests/REL. SE column: the SE id; every AE column: the AE id.
// Usage: node table.js <tests/REL dir> [--md]
'use strict';
var load = require('./iddb.js'), path = require('path');
var dir = process.argv[2], md = process.argv.indexOf('--md') > 0;
var bins = [['1.5.97', 'version-1-5-97-0.bin', 'se'], ['1.6.353', 'versionlib-1-6-353-0.bin', 'ae'],
  ['1.6.629', 'versionlib-1-6-629-0.bin', 'ae'], ['1.6.1130', 'versionlib-1-6-1130-0.bin', 'ae'],
  ['1.6.1170', 'versionlib-1-6-1170-0.bin', 'ae'], ['1.6.1179', 'versionlib-1-6-1179-0.bin', 'ae'],
  ['1.7.99', 'versionlib-1-7-99-0.bin', 'ae']];
var dbs = bins.map(function (b) { return { name: b[0], kind: b[2], db: load(path.join(dir, b[1])) }; });
var own = [
  ['Projectile::Launch (copy swap detour)', 42928, 44108],
  ['explosion creation (copy swap detour)', 42657, 43829],
  ['hazard creation (C4 detour)', 42786, 43954],
  ['BGSImpactDataSet::GetImpactData (callee)', 20408, 20860],
  ['Projectile::SpawnCollisionEffects (GetImpactData site, impact window)', 43022, 44213],
  ['explosion impact function (GetImpactData site)', 42679, 43851],
  ['Projectile::ProcessImpacts (impact window site)', 42943, 44123],
  ['Projectile::HandleHits caller (impact window site)', 43014, 44205],
  ['ConeProjectile::UpdateImpl (impact window site)', 42638, 43806],
  ['Main::Update (main-thread queue call site)', 35565, 36564],
  ['ActorMagicCaster::SetCastingArt (C4 callee)', 33369, 34150],
  ['SetCastingArt caller (C4 site)', 37830, 38784],
  ['ActiveEffect spawn-hazard (R-H)', 33580, 34360],
  ['VTABLE_ActorMagicCaster[0]', 257613, 205828],
  ['VTABLE_Explosion[0]', 263870, 209940],
  ['VTABLE_Hazard[0]', 263922, 209971],
  ['VTABLE_BGSHazard[2] (TESModel)', 233489, 189083],
  ['VTABLE_BGSImpactData[1] (TESModel)', 236357, 190905],
  ['VTABLE_ActiveEffectReferenceEffectController[0]', 257577, 205809],
  ['VTABLE_BGSExplosion[0] (explosion creation base check)', 233467, 189059],
  ['VTABLE_BGSArtObject[0] (casting art check)', 233406, 189017],
  ['VTABLE_BGSHazard[0] (hazard creation base check)', 233487, 189079],
  ['VTABLE_TESObjectCELL[0] (hazard creation cell check)', 235197, 190143],
  ['VTABLE_SpawnHazardEffect[0] (spawn-hazard window check)', 259802, 207209]
];
var lib = require('./reached.json').filter(function (r) { return r[1] !== 46022; /* not called: SelfTestWorld.cpp says so */ }).map(function (r) { return [r[0], r[1], r[2]]; }).concat([
  ['MagicTarget::DispelEffect', 33721, 34505],
  ['BSResourceNiBinaryStream ctor', 69636, 71014],
  ['BSResourceNiBinaryStream dtor', 69638, 71016]
]);
function row(r, group) {
  var cells = dbs.map(function (d) { return d.db.map.has(BigInt(d.kind === 'se' ? r[1] : r[2])) ? 'yes' : 'MISSING'; });
  return { name: r[0], se: r[1], ae: r[2], group: group, cells: cells };
}
var rows = own.map(function (r) { return row(r, 'ours'); }).concat(lib.map(function (r) { return row(r, 'CommonLib'); }));
if (md) {
  console.log('| What | SE id | AE id | ' + dbs.map(function (d) { return d.name; }).join(' | ') + ' |');
  console.log('|---|---|---|' + dbs.map(function () { return '---'; }).join('|') + '|');
  rows.forEach(function (r) { console.log('| ' + r.name + (r.group === 'ours' ? '' : ' (CommonLib)') + ' | ' + r.se + ' | ' + r.ae + ' | ' + r.cells.join(' | ') + ' |'); });
} else {
  rows.forEach(function (r) { console.log([r.group, r.name, r.se, r.ae].concat(r.cells).join(' | ')); });
}
var missing = rows.filter(function (r) { return r.cells.indexOf('MISSING') >= 0; });
console.error(rows.length + ' ids, ' + missing.length + ' with a MISSING cell');
missing.forEach(function (r) { console.error('  ' + r.name + ' ' + r.se + '/' + r.ae + ': ' + r.cells.join(' ')); });
