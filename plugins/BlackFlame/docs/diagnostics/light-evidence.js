// Read-only evidence for aura-light.md (2026-10-23). No Skyrim, MO2, builder or game writes.
// node light-evidence.js records <MO2> <profile> <Skyrim local hex IDs...>
// node light-evidence.js nif <old.nif> <current.nif> <overwrite>
'use strict';
var fs = require('fs');
var path = require('path');
var zlib = require('zlib');

function subrecords(b) {
    var out = [], i = 0;
    while (i + 6 <= b.length) {
        var tag = b.toString('ascii', i, i + 4), n = b.readUInt16LE(i + 4);
        i += 6;
        if (tag === 'XXXX') {
            n = b.readUInt32LE(i);
            i += 4;
            tag = b.toString('ascii', i, i + 4);
            i += 6;
        }
        if (i + n > b.length) throw new Error('truncated ' + tag);
        out.push([tag, b.subarray(i, i + n)]);
        i += n;
    }
    return out;
}

function records(root, profile, ids) {
    var wanted = new Set(ids.map(function (s) { return parseInt(s, 16); }));
    var profilePath = path.join(root, 'profiles', profile);
    var mods = fs.readFileSync(path.join(profilePath, 'modlist.txt'), 'utf8').split(/\r?\n/)
        .filter(function (s) { return s[0] === '+'; }).map(function (s) { return s.slice(1); });
    var plugins = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm', 'HearthFires.esm', 'Dragonborn.esm'].concat(
        fs.readFileSync(path.join(profilePath, 'plugins.txt'), 'utf8').split(/\r?\n/)
            .filter(function (s) { return s[0] === '*'; }).map(function (s) { return s.slice(1); }));
    var winners = new Map();
    [path.join(root, 'overwrite')].concat(mods.map(function (m) { return path.join(root, 'mods', m); }),
        [path.join(root, 'Stock Game', 'Data')]).forEach(function (dir) {
        if (!fs.existsSync(dir)) return;
        fs.readdirSync(dir, { withFileTypes: true }).forEach(function (f) {
            var key = f.name.toLowerCase();
            if (f.isFile() && /\.(esp|esm|esl)$/i.test(f.name) && !winners.has(key)) {
                winners.set(key, path.join(dir, f.name));
            }
        });
    });
    plugins.forEach(function (plugin) {
        var file = winners.get(plugin.toLowerCase());
        if (!file) throw new Error('enabled plugin not found: ' + plugin);
        var b = fs.readFileSync(file);
        var masters = subrecords(b.subarray(24, 24 + b.readUInt32LE(4)))
            .filter(function (s) { return s[0] === 'MAST'; })
            .map(function (s) { return s[1].toString().replace(/\0+$/, '').toLowerCase(); });
        var sky = plugin === 'Skyrim.esm' ? 0 : masters.indexOf('skyrim.esm');
        if (sky < 0) return;
        function walk(start, end) {
            for (var i = start; i + 24 <= end;) {
                var kind = b.toString('ascii', i, i + 4), n = b.readUInt32LE(i + 4);
                if (kind === 'GRUP') {
                    if (n < 24 || i + n > end) throw new Error('bad GRUP in ' + file);
                    walk(i + 24, i + n);
                    i += n;
                } else {
                    var id = b.readUInt32LE(i + 12);
                    if ((id >>> 24) === sky && wanted.has(id & 0xffffff)) {
                        var data = b.subarray(i + 24, i + 24 + n);
                        if (b.readUInt32LE(i + 8) & 0x40000) data = zlib.inflateSync(data.subarray(4));
                        var subs = subrecords(data);
                        console.log(file + ' | ' + kind + ' ' + id.toString(16) + ' @0x' + i.toString(16));
                        console.log('  subrecords: ' + subs.map(function (s) { return s[0]; }).join(','));
                        subs.forEach(function (s) {
                            if (['EDID', 'MODL'].includes(s[0])) console.log(' ', s[0], s[1].toString().replace(/\0+$/, ''));
                            if (['DATA', 'EFID', 'KWDA', 'SPIT', 'VMAD', 'FNAM'].includes(s[0])) console.log(' ', s[0], s[1].length, s[1].toString('hex'));
                            if (kind === 'MGEF' && s[0] === 'DATA' && s[1].length === 152) {
                                var data = s[1];
                                console.log('  decoded MGEF', JSON.stringify({ light: data.readUInt32LE(0x18).toString(16),
                                    hitShader: data.readUInt32LE(0x20).toString(16), archetype: data.readUInt32LE(0x40),
                                    castingArt: data.readUInt32LE(0x5c).toString(16), hitArt: data.readUInt32LE(0x60).toString(16),
                                    equipAbility: data.readUInt32LE(0x80).toString(16) }));
                            }
                        });
                    }
                    i += 24 + n;
                }
            }
        }
        walk(0, b.length);
    });
}

function nif(file) {
    var b = fs.readFileSync(file), i = b.indexOf(10) + 1;
    function u32() { var v = b.readUInt32LE(i); i += 4; return v; }
    function str() { var n = u32(), s = b.toString('utf8', i, i + n); i += n; return s; }
    if (u32() !== 0x14020007 || b[i++] !== 1 || u32() !== 12) throw new Error('expected SSE NIF');
    var count = u32();
    if (u32() !== 100) throw new Error('expected BS version 100');
    for (var k = 0; k < 3; k++) { var n = b[i++]; i += n; }
    var typeCount = b.readUInt16LE(i); i += 2;
    var types = Array.from({ length: typeCount }, str);
    var indexes = Array.from({ length: count }, function () { var v = b.readUInt16LE(i); i += 2; return v; });
    var sizes = Array.from({ length: count }, u32), stringCount = u32();
    u32();
    var strings = Array.from({ length: stringCount }, str), groups = u32();
    i += groups * 4;
    var blocks = sizes.map(function (size, id) {
        var block = { id: id, type: types[indexes[id]], offset: i, data: b.subarray(i, i + size) };
        i += size;
        return block;
    });
    return { bytes: b, blocks: blocks, strings: strings };
}

function colours(oldPath, currentPath, dataRoot) {
    var old = nif(oldPath), now = nif(currentPath), differences = 0, keys = 0, chromaticKeys = 0;
    var chromaticTangents = 0, chromaticInterpolators = 0;
    function chromatic(b, i) { return b.readFloatLE(i) !== b.readFloatLE(i + 4) || b.readFloatLE(i + 4) !== b.readFloatLE(i + 8); }
    now.bytes.forEach(function (v, i) { differences += v !== old.bytes[i] ? 1 : 0; });
    now.blocks.forEach(function (b) {
        var previous = old.blocks[b.id];
        if (!b.data.equals(previous.data)) console.log('changed block', b.id, b.type, '@0x' + b.offset.toString(16), previous.data.toString('hex'), '->', b.data.toString('hex'));
        if (b.type === 'NiPoint3Interpolator') chromaticInterpolators += chromatic(b.data, 0) ? 1 : 0;
        if (b.type !== 'NiPosData') return;
        var count = b.data.readUInt32LE(0), type = b.data.readUInt32LE(4), stride = { 1: 16, 2: 40, 3: 28 }[type];
        if (!stride) throw new Error('unsupported point key type ' + type);
        for (var k = 0; k < count; k++) {
            var offset = 12 + k * stride;
            keys++;
            chromaticKeys += chromatic(b.data, offset) ? 1 : 0;
            if (type === 2) chromaticTangents += (chromatic(b.data, offset + 12) ? 1 : 0) + (chromatic(b.data, offset + 24) ? 1 : 0);
        }
    });
    console.log(JSON.stringify({ blocks: now.blocks.length, differences: differences, keys: keys, chromaticKeys: chromaticKeys,
        chromaticTangents: chromaticTangents, chromaticInterpolators: chromaticInterpolators,
        lights: now.blocks.filter(function (b) { return /^Ni(?:Point|Spot|Directional|Ambient)?Light$/.test(b.type); }).length }));
    // SSE texture paths are inline SizedStrings, not entries in the header's string table.
    var palettes = new Set();
    now.blocks.forEach(function (b) {
        if (b.type !== 'BSEffectShaderProperty') return;
        console.log('effect texture strings', b.id, b.data.toString('latin1').match(/textures\\[A-Za-z0-9_\\. -]+\.dds/g) || []);
        var matches = b.data.toString('latin1').match(/textures\\heartofmagic\\blackflame\\copies\\[a-f0-9]{16}\\palette\.dds/g) || [];
        matches.forEach(function (p) { palettes.add(p); });
    });
    palettes.forEach(function (p) {
        var file = path.join(dataRoot, p), b = fs.readFileSync(file);
        if (b.toString('ascii', 0, 4) !== 'DDS ' || b.readUInt32LE(84) !== 0 || b.readUInt32LE(88) !== 32) throw new Error('expected RGBA8 DDS: ' + file);
        var count = b.readUInt32LE(12) * b.readUInt32LE(16), chroma = 0;
        for (var i = 128; i < 128 + count * 4; i += 4) chroma += b[i] !== b[i + 1] || b[i + 1] !== b[i + 2] ? 1 : 0;
        console.log(p, 'top mip texels', count, 'chromatic', chroma);
    });
}

if (process.argv[2] === 'records') records(process.argv[3], process.argv[4], process.argv.slice(5));
else if (process.argv[2] === 'nif') colours(process.argv[3], process.argv[4], process.argv[5]);
else throw new Error('usage: light-evidence.js records <MO2> <profile> <Skyrim hex IDs...> | nif <old> <current> <overwrite>');
