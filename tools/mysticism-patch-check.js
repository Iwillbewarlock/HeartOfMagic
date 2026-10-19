/**
 * mysticism-patch-check.js - checks the optional Mysticism patch, HeartOfMagic_Mysticism.esp (docs/ENVIRONMENT.md,
 * "The Mysticism patch"): light flag, HEDR 1.70, masters exactly PATCH_MASTERS, one record - an uncompressed MGEF override of
 * MysticismMagic.esp|0xBDE626 (MAG_ScreenShakeEffect) - whose VMAD runs magicPlaceActivatorScript with ExplosionRef
 * resolving, through the master list, to the twin twins.json gives the Blizzard explosion. With --mysticism it also
 * compares the record with MysticismMagic.esp's own: every subrecord byte for byte after mapping FormIDs from the
 * source's master list to the patch's (MGEF DATA, MDOB, KWDA, ESCE, SNDD and VMAD object properties), the only
 * allowed difference being ExplosionRef (source: the original explosion; patch: the twin). A missing or malformed
 * patch fails.
 *
 * Run by frostwalker-twin-check.js (its --patch and --mysticism options); not a command of its own.
 */
var fs = require('fs');
var path = require('path');
var reader = require('./plugin-reader');

var readPlugin = reader.readPlugin;
var parsePersistentId = reader.parsePersistentId;
var LIGHT_FLAG = reader.LIGHT_FLAG;
var HEDR_VERSION_HEX = reader.HEDR_VERSION_HEX;

var PATCH_MASTERS = ['Skyrim.esm', 'Update.esm', 'MysticismMagic.esp', 'HeartOfMagic.esp'];
var PATCH_TARGET = { plugin: 'MysticismMagic.esp', localId: 0xBDE626, editorId: 'MAG_ScreenShakeEffect' };
var PATCH_SCRIPT = 'magicPlaceActivatorScript';
var PATCH_PROPERTY = 'ExplosionRef';
var PATCH_ORIGINAL = 'Skyrim.esm|0x0B7A05';   // the twins.json original the property must name in Mysticism's record
// FormID fields of MGEF DATA (152 bytes) by byte offset: associated item, light, hit shader, enchant shader,
// projectile, explosion, casting art, hit effect art, impact data set, dual casting art, enchant art, hit visuals,
// enchant visuals, equip ability, image space modifier, perk to apply (checked against the patch: explosion at 76 is
// 0x000B7A05, casting art at 92 is MysticismMagic.esp's 0x482CE2)
var MGEF_DATA_FORMIDS = [8, 24, 32, 36, 72, 76, 92, 96, 100, 108, 116, 120, 124, 128, 132, 136];
var MGEF_DATA_SIZE = 152;
var MGEF_DATA_EXPLOSION = 76;

function sameName(a, b) {
    return String(a).toLowerCase() === String(b).toLowerCase();
}

function samePersistentId(a, b) {
    var x = parsePersistentId(a);
    var y = parsePersistentId(b);
    return sameName(x.plugin, y.plugin) && x.localId === y.localId;
}

/** The file a FormID's top byte names: a master, or the plugin itself (index = master count); null past that */
function fileOfIndex(index, masters, self) {
    if (index < masters.length) return masters[index];
    return index === masters.length ? self : null;
}

function persistentIdOf(formId, masters, self) {
    var file = fileOfIndex(formId >>> 24, masters, self);
    if (file === null) throw new Error('FormID 0x' + formId.toString(16) + ' names no master of ' + self);
    var local = (formId & 0xFFFFFF).toString(16).toUpperCase();
    return file + '|0x' + ('000000' + local).slice(-6);
}

/** A persistentId as a FormID under another file's master list (that file itself at index = master count) */
function formIdIn(persistentId, masters, self) {
    var id = parsePersistentId(persistentId);
    var index = -1;
    for (var i = 0; i < masters.length; i++) if (sameName(masters[i], id.plugin)) index = i;
    if (index === -1 && sameName(self, id.plugin)) index = masters.length;
    if (index === -1) throw new Error(persistentId + ' is not reachable from ' + self + ' (not one of its masters)');
    return ((index << 24) | id.localId) >>> 0;
}

/**
 * VMAD (scripts only, as on a magic effect): { version, objFormat, scripts: [{ name, props: [{ name, type,
 * objects: [{ at, formId }] }] }] }. Object properties (type 1, arrays type 11) give each FormID's byte offset.
 */
function parseVmad(data) {
    var pos = 0;
    function need(n) { if (pos + n > data.length) throw new Error('VMAD runs past its end at byte ' + pos); }
    function u8() { need(1); return data[pos++]; }
    function u16() { need(2); var v = data.readUInt16LE(pos); pos += 2; return v; }
    function u32() { need(4); var v = data.readUInt32LE(pos); pos += 4; return v; }
    function wstr() { var n = u16(); need(n); var s = data.toString('latin1', pos, pos + n); pos += n; return s; }
    var vmad = { version: u16(), objFormat: u16(), scripts: [] };
    if (vmad.objFormat !== 1 && vmad.objFormat !== 2) throw new Error('VMAD object format ' + vmad.objFormat + ' is not 1 or 2');
    function object(prop) {
        need(8);
        var at = vmad.objFormat === 1 ? pos : pos + 4;   // format 1: FormID, alias, unused; format 2: unused, alias, FormID
        prop.objects.push({ at: at, formId: data.readUInt32LE(at) });
        pos += 8;
    }
    var scriptCount = u16();
    for (var s = 0; s < scriptCount; s++) {
        var script = { name: wstr(), props: [] };
        if (vmad.version >= 4) u8();
        var propCount = u16();
        for (var p = 0; p < propCount; p++) {
            var prop = { name: wstr(), type: u8(), objects: [] };
            if (vmad.version >= 4) u8();
            var count;
            switch (prop.type) {
                case 1: object(prop); break;
                case 2: wstr(); break;
                case 3: case 4: u32(); break;
                case 5: u8(); break;
                case 11: count = u32(); for (var o = 0; o < count; o++) object(prop); break;
                case 12: count = u32(); for (var w = 0; w < count; w++) wstr(); break;
                case 13: case 14: count = u32(); need(4 * count); pos += 4 * count; break;
                case 15: count = u32(); need(count); pos += count; break;
                default: throw new Error('VMAD property ' + prop.name + ' has type ' + prop.type + ', which this check does not read');
            }
            script.props.push(prop);
        }
        vmad.scripts.push(script);
    }
    if (pos !== data.length) throw new Error('VMAD has ' + (data.length - pos) + ' bytes after its scripts (fragments are not expected on MGEF)');
    return vmad;
}

/** Byte offsets of the FormIDs in one MGEF subrecord */
function formIdOffsets(type, data) {
    var offsets = [];
    var i;
    if (type === 'DATA') {
        if (data.length !== MGEF_DATA_SIZE) throw new Error('MGEF DATA is ' + data.length + ' bytes, expected ' + MGEF_DATA_SIZE);
        return MGEF_DATA_FORMIDS.slice();
    }
    if (type === 'MDOB') return [0];
    if (type === 'KWDA' || type === 'ESCE') { for (i = 0; i + 4 <= data.length; i += 4) offsets.push(i); }
    if (type === 'SNDD') { for (i = 0; i + 8 <= data.length; i += 8) offsets.push(i + 4); }
    if (type === 'VMAD') {
        parseVmad(data).scripts.forEach(function (script) {
            script.props.forEach(function (prop) { prop.objects.forEach(function (o) { offsets.push(o.at); }); });
        });
    }
    return offsets;
}

/** The patch's script property: { script, prop, object } or a reason string */
function findExplosionRef(vmadData) {
    var vmad = parseVmad(vmadData);
    var script = vmad.scripts.filter(function (s) { return sameName(s.name, PATCH_SCRIPT); })[0];
    if (!script) return 'no ' + PATCH_SCRIPT + ' in VMAD (' + vmad.scripts.map(function (s) { return s.name; }).join(', ') + ')';
    var prop = script.props.filter(function (p) { return sameName(p.name, PATCH_PROPERTY); })[0];
    if (!prop) return PATCH_SCRIPT + ' has no ' + PATCH_PROPERTY + ' property';
    if (prop.type !== 1 || prop.objects.length !== 1) return PATCH_PROPERTY + ' is type ' + prop.type + ', not a single object';
    return { script: script, prop: prop, object: prop.objects[0] };
}

/** Mysticism's own record against the patch's: equal byte for byte after mapping FormIDs, but for ExplosionRef */
function compareWithSource(record, patchMasters, patchName, twinId, sourceFile, fail) {
    var label = 'compare with ' + path.basename(sourceFile);
    var source;
    try {
        source = readPlugin(sourceFile);
    } catch (e) {
        fail(label + ': ' + e.message);
        return;
    }
    var srcName = path.basename(sourceFile);
    if (!sameName(srcName, PATCH_TARGET.plugin)) fail(label + ': the file is not named ' + PATCH_TARGET.plugin);
    var srcMasters = source.header.masters;
    var srcFormId = ((srcMasters.length << 24) | PATCH_TARGET.localId) >>> 0;
    var srcRecord = source.list.filter(function (r) { return r.formId === srcFormId && r.type === 'MGEF'; })[0];
    if (!srcRecord) { fail(label + ': no MGEF 0x' + srcFormId.toString(16) + ' in the source'); return; }
    if (srcRecord.compressed) { fail(label + ': the source record is compressed, not compared'); return; }
    if (srcRecord.flags !== record.flags) fail(label + ': record flags 0x' + record.flags.toString(16) + ', source 0x' + srcRecord.flags.toString(16));
    if (srcRecord.formVersion !== record.formVersion) fail(label + ': form version ' + record.formVersion + ', source ' + srcRecord.formVersion);

    var srcTypes = srcRecord.raw.map(function (s) { return s[0]; }).join(' ');
    var patchTypes = record.raw.map(function (s) { return s[0]; }).join(' ');
    if (srcTypes !== patchTypes) { fail(label + ': subrecords "' + patchTypes + '", source "' + srcTypes + '"'); return; }

    var twinFormId = formIdIn(twinId, patchMasters, patchName);
    var mapped = 0;
    var differing = 0;
    var explosionRefSeen = false;
    srcRecord.raw.forEach(function (sub, i) {
        var type = sub[0];
        var want = Buffer.from(sub[1]);
        var got = record.raw[i][1];
        try {
            formIdOffsets(type, want).forEach(function (at) {
                var fid = want.readUInt32LE(at);
                if (fid === 0 || fid === 0xFFFFFFFF) return;
                want.writeUInt32LE(formIdIn(persistentIdOf(fid, srcMasters, srcName), patchMasters, patchName), at);
                mapped++;
            });
            if (type === 'VMAD') {
                var ref = findExplosionRef(sub[1]);
                if (typeof ref === 'string') throw new Error('source ' + ref);
                var srcRef = persistentIdOf(ref.object.formId, srcMasters, srcName);
                if (!samePersistentId(srcRef, PATCH_ORIGINAL)) {
                    throw new Error('source ' + PATCH_PROPERTY + ' is ' + srcRef + ', expected the original ' + PATCH_ORIGINAL);
                }
                want.writeUInt32LE(twinFormId, ref.object.at);   // the one allowed difference
                explosionRefSeen = true;
            }
        } catch (e) {
            fail(label + ': ' + type + ': ' + e.message);
            differing++;
            return;
        }
        if (!want.equals(got)) {
            var at = 0;
            while (at < Math.min(want.length, got.length) && want[at] === got[at]) at++;
            fail(label + ': ' + type + ' (subrecord ' + (i + 1) + ') differs at byte ' + at + ': patch ' +
                got.subarray(at, at + 8).toString('hex') + ', source mapped ' + want.subarray(at, at + 8).toString('hex') +
                ' (lengths ' + got.length + '/' + want.length + ')');
            differing++;
        }
    });
    if (!explosionRefSeen) fail(label + ': no VMAD in the source record');
    console.log('  ' + label + ': ' + srcRecord.raw.length + ' subrecords, ' + mapped + ' FormIDs mapped from ' +
        JSON.stringify(srcMasters) + ' + itself, ' + differing + ' differing' +
        (differing === 0 ? ' (equal but ' + PATCH_PROPERTY + ': ' + PATCH_ORIGINAL + ' -> ' + twinId + ')' : ''));
}

function checkMysticismPatch(opt, twins, fail) {
    var patchName = path.basename(opt.patch);
    if (!fs.existsSync(opt.patch)) { fail(patchName + ': not found at ' + opt.patch); return; }
    var records;
    try {
        records = readPlugin(opt.patch);
    } catch (e) {
        fail(patchName + ': ' + e.message);
        return;
    }
    var header = records.header;
    console.log('patch header: flags 0x' + header.flags.toString(16) + ', HEDR ' + header.hedr + ', masters ' + JSON.stringify(header.masters));
    if ((header.flags & LIGHT_FLAG) === 0) fail(patchName + ': TES4 flags 0x' + header.flags.toString(16) + ' lack the light flag 0x200');
    if (header.hedr !== HEDR_VERSION_HEX) fail(patchName + ': HEDR version bytes ' + header.hedr + ', expected ' + HEDR_VERSION_HEX + ' (1.70)');
    if (JSON.stringify(header.masters) !== JSON.stringify(PATCH_MASTERS)) {
        fail(patchName + ': masters ' + JSON.stringify(header.masters) + ', expected ' + JSON.stringify(PATCH_MASTERS));
        return;   // every FormID below is read through this list
    }
    if (records.list.length !== 1) {
        fail(patchName + ': ' + records.list.length + ' records, expected exactly one (the ' + PATCH_TARGET.editorId + ' override)');
        if (records.list.length === 0) return;
    }
    var record = records.list[0];
    var targetId = PATCH_TARGET.plugin + '|0x' + PATCH_TARGET.localId.toString(16).toUpperCase();
    var expectedFormId = formIdIn(targetId, header.masters, patchName);
    if (record.type !== 'MGEF' || record.formId !== expectedFormId) {
        fail(patchName + ': the record is ' + record.type + ' 0x' + record.formId.toString(16) + ', expected MGEF 0x' +
            expectedFormId.toString(16) + ' (' + targetId + ', an override)');
        return;
    }
    if (record.compressed) { fail(patchName + ': the record is compressed, not read'); return; }
    if (record.strings.EDID !== PATCH_TARGET.editorId) fail(patchName + ': EditorID "' + record.strings.EDID + '", expected ' + PATCH_TARGET.editorId);

    var twinEntry = twins.filter(function (e) { return samePersistentId(e.original, PATCH_ORIGINAL); })[0];
    if (!twinEntry) { fail('twins.json has no twin for ' + PATCH_ORIGINAL); return; }
    var vmad = record.raw.filter(function (s) { return s[0] === 'VMAD'; });
    if (vmad.length !== 1) { fail(patchName + ': ' + vmad.length + ' VMAD subrecords, expected one'); return; }
    var ref;
    try {
        ref = findExplosionRef(vmad[0][1]);
    } catch (e) {
        ref = e.message;
    }
    if (typeof ref === 'string') { fail(patchName + ': ' + ref); return; }
    var resolved = persistentIdOf(ref.object.formId, header.masters, patchName);
    var data = record.raw.filter(function (s) { return s[0] === 'DATA'; })[0];
    var fieldExplosion = data && data[1].length === MGEF_DATA_SIZE ?
        persistentIdOf(data[1].readUInt32LE(MGEF_DATA_EXPLOSION), header.masters, patchName) : '(no DATA)';
    console.log('  ' + targetId + ' (' + record.strings.EDID + ') override: ' + PATCH_SCRIPT + '.' + PATCH_PROPERTY +
        ' -> ' + resolved + '; Explosion field ' + fieldExplosion + ' (the DLL repoints it at runtime)');
    if (!samePersistentId(resolved, twinEntry.twin)) {
        fail(patchName + ': ' + PATCH_PROPERTY + ' is ' + resolved + ', expected the twin ' + twinEntry.twin);
    }
    if (opt.mysticism) compareWithSource(record, header.masters, patchName, twinEntry.twin, opt.mysticism, fail);
}

module.exports = { checkMysticismPatch: checkMysticismPatch };
