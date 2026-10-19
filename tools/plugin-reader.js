/**
 * plugin-reader.js - reads an uncompressed Skyrim SE plugin file for the offline checks
 * (frostwalker-twin-check.js, mysticism-patch-check.js). Plain Node, no packages.
 *
 * readPlugin(file): the TES4 header ({ flags, hedr: hex of HEDR's version, masters }) and every record, by local
 * FormID and in file order (records.list), each { type, formId, flags, formVersion, compressed, strings: {EDID, FULL,
 * MODL}, subrecords: [[type, hex]...], raw: [[type, Buffer]...] }. Compressed records keep no subrecords. A file that
 * is not a plugin or whose sizes run past their ends throws.
 */
var fs = require('fs');

var LIGHT_FLAG = 0x200;
var HEDR_VERSION_HEX = '9a99d93f';   // 1.70 as a little-endian float

function cString(data) {
    var end = data.indexOf(0);
    return data.toString('latin1', 0, end === -1 ? data.length : end);
}

/**
 * The plugin's header ({ flags, hedr: hex of HEDR's version, masters }) and every record by local FormID:
 * { type, formId, strings: {EDID, FULL, MODL}, subrecords: [[type, hex]...] }
 */
function readPlugin(file) {
    var buf = fs.readFileSync(file);
    if (buf.length < 24 || buf.toString('latin1', 0, 4) !== 'TES4') throw new Error(file + ' is not a plugin (no TES4 header)');
    function need(end, limit, what) {
        if (end > limit) throw new Error(file + ' is malformed: ' + what + ' runs past its end (' + end + ' > ' + limit + ')');
    }
    var headerSize = buf.readUInt32LE(4);
    need(24 + headerSize, buf.length, 'the TES4 header');
    var header = { flags: buf.readUInt32LE(8), hedr: '', masters: [] };
    for (var h = 24; h < 24 + headerSize;) {
        need(h + 6, 24 + headerSize, 'a TES4 subrecord header');
        var hType = buf.toString('latin1', h, h + 4);
        var hLen = buf.readUInt16LE(h + 4);
        need(h + 6 + hLen, 24 + headerSize, 'TES4 ' + hType);
        if (hType === 'HEDR') header.hedr = buf.subarray(h + 6, h + 6 + Math.min(4, hLen)).toString('hex');
        if (hType === 'MAST') header.masters.push(cString(buf.subarray(h + 6, h + 6 + hLen)));
        h += 6 + hLen;
    }
    var records = {};
    var list = [];
    (function walk(start, end) {
        var p = start;
        while (p < end) {
            need(p + 24, end, 'a record or group header');
            var type = buf.toString('latin1', p, p + 4);
            var size = buf.readUInt32LE(p + 4);
            if (type === 'GRUP') {
                if (size < 24) throw new Error(file + ' is malformed: GRUP size ' + size);
                need(p + size, end, 'GRUP');
                walk(p + 24, p + size);
                p += size;
                continue;
            }
            need(p + 24 + size, end, type + ' record');
            var flags = buf.readUInt32LE(p + 8);
            var formId = buf.readUInt32LE(p + 12);
            var localId = formId & 0xFFFFFF;
            var entry = { type: type, formId: formId, flags: flags, formVersion: buf.readUInt16LE(p + 20),
                compressed: (flags & 0x40000) !== 0, strings: {}, subrecords: [], raw: [] };
            if (!entry.compressed) {
                for (var q = p + 24; q < p + 24 + size;) {
                    need(q + 6, p + 24 + size, type + ' subrecord header');
                    var sub = buf.toString('latin1', q, q + 4);
                    var len = buf.readUInt16LE(q + 4);
                    need(q + 6 + len, p + 24 + size, type + ' ' + sub);
                    var data = buf.subarray(q + 6, q + 6 + len);
                    if (sub === 'EDID' || sub === 'FULL' || sub === 'MODL') entry.strings[sub] = cString(data);
                    entry.subrecords.push([sub, data.toString('hex')]);
                    entry.raw.push([sub, Buffer.from(data)]);
                    q += 6 + len;
                }
            }
            records[localId] = entry;
            list.push(entry);
            p += 24 + size;
        }
    })(24 + headerSize, buf.length);
    records.header = header;
    records.list = list;
    return records;
}

function parsePersistentId(id) {
    var m = /^(.+)\|0x([0-9A-Fa-f]{1,8})$/.exec(id || '');
    if (!m) throw new Error('Bad persistentId "' + id + '" (expected "Plugin.esp|0xLOCALID")');
    return { plugin: m[1], localId: parseInt(m[2], 16) };
}

module.exports = {
    readPlugin: readPlugin,
    parsePersistentId: parsePersistentId,
    LIGHT_FLAG: LIGHT_FLAG,
    HEDR_VERSION_HEX: HEDR_VERSION_HEX
};
