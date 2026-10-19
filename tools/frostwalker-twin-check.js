/**
 * frostwalker-twin-check.js - are the explosion twins in HeartOfMagic.esp neutral to Frostwalker?
 *
 *   node tools/frostwalker-twin-check.js
 *   node tools/frostwalker-twin-check.js --patterns "<Data>/SKSE/Plugins/Frostwalker"
 *   node tools/frostwalker-twin-check.js --esp <plugin> --twins <twins.json>
 *
 * Frostwalker (Nexus 184628) decides an explosion's element from its EditorID, then its name, then its
 * model path: lower-cased, each tested against NeutralSources, FrostSources, FireSources, WaterSources (in
 * that order, first hit wins) by substring, except "ice", which does not count right after "vo" (voice).
 * This is that matcher (frostwalker-src src/Utils.cpp MatchesAnyPattern / GetExplosionType, pattern files
 * read as Settings.cpp LoadPatternsFromFile: trimmed lines, blank and '#' lines skipped, lower-cased).
 *
 * For every twin in twins.json (docs/ENVIRONMENT.md) it reads the record from the plugin file itself - its
 * EditorID (EDID), name (FULL) and model (MODL) - and fails (exit 1) unless the record is an EXPL and each
 * string classifies Unknown. It also fails when the matcher's own cases go wrong (the original
 * MAG_BlizzardForceExplosion must read as cold, "voice" must not).
 *
 * The plugin itself: the TES4 header must carry the light flag (0x200), HEDR version 1.70 (the bytes
 * 9A 99 D9 3F: SE 1.5.97 and AE before 1.6.1130 refuse 1.71), and exactly one master, Skyrim.esm. Each twin's
 * FormID must name the plugin itself (top byte = the master count: a new record, not an override), and every
 * subrecord but its EDID must be the bytes listed in TWIN_FIELDS (the original's values, docs/ENVIRONMENT.md).
 *
 * Patterns: --patterns <dir> (or FROSTWALKER_DIR) is the folder holding NeutralSources/, FrostSources/,
 * FireSources/ and WaterSources/, read as Frostwalker 2.2 reads them (Settings.cpp LoadAllPatterns): every regular
 * file in each folder whatever its extension, a missing sub-folder an empty list; a --patterns path that is not a
 * folder stops the check (exit 2). Without it the lists of Frostwalker
 * 2.2's Default.ini files are used. The matcher's own cases always run on those built-in lists, so a user list
 * judges only the twins. The C++ copy (include/environment/EnvironmentMatch.h, EnvironmentTwins.cpp) reads the
 * same way. Plain Node, no packages.
 */
var fs = require('fs');
var path = require('path');

var ROOT = path.resolve(__dirname, '..');
var DEFAULT_ESP = path.join(ROOT, 'HeartOfMagic.esp');
var DEFAULT_TWINS = path.join(ROOT, 'SKSE', 'Plugins', 'SpellLearning', 'librarian', 'environment', 'twins.json');

// Frostwalker 2.2's Default.ini lists (SKSE/Plugins/Frostwalker/<folder>/Default.ini)
var BUILT_IN_PATTERNS = {
    neutral: ['practice'],
    cold: ['cold', 'frost', 'ice', 'blizzard', 'freeze', 'freezing'],
    fire: ['fire', 'flame', 'heat'],
    water: ['water', 'steam', 'river', 'ocaen', 'sea']
};
var PATTERN_FOLDERS = { neutral: 'NeutralSources', cold: 'FrostSources', fire: 'FireSources', water: 'WaterSources' };
var CLASS_ORDER = ['neutral', 'cold', 'fire', 'water'];
var UNKNOWN = 'unknown';

var LIGHT_FLAG = 0x200;
var HEDR_VERSION_HEX = '9a99d93f';   // 1.70 as a little-endian float
var EXPECTED_MASTERS = ['Skyrim.esm'];

// Every subrecord after EDID, in file order, as hex: the values of MysticismMagic.esp 2.4.2.0's
// MAG_BlizzardForceExplosion (docs/ENVIRONMENT.md, "field values"). FormIDs here are Skyrim.esm (index 00).
var TWIN_FIELDS = {
    'HeartOfMagic.esp|0x000800': [
        ['OBND', '000000000000000000000000'],
        ['MODL', '456666656374735c4658456d7074794f626a6563742e6e696600'],   // Effects\FXEmptyObject.nif
        ['MODT', '020000000000000000000000'],
        ['MNAM', '067a0b00'],   // Skyrim.esm|0x0B7A06 MAGBlizzardForceExplosionImod
        ['DATA', '00000000e9e50300000000000000000000000000000000000000c8420000000000007a440000dc440000003f5301000001000000']
    ]
};

// ============================================================================
// Frostwalker's matcher
// ============================================================================

function matchesAnyPattern(lower, patterns) {
    for (var i = 0; i < patterns.length; i++) {
        var pat = patterns[i];
        if (pat === 'ice') {
            var pos = lower.indexOf('ice');
            while (pos !== -1) {
                if (!(pos >= 2 && lower[pos - 2] === 'v' && lower[pos - 1] === 'o')) return true;
                pos = lower.indexOf('ice', pos + 1);
            }
        } else if (lower.indexOf(pat) !== -1) {
            return true;
        }
    }
    return false;
}

/** The element Frostwalker gives one string: 'neutral', 'cold', 'fire', 'water' or 'unknown' */
function classify(text, patterns) {
    var lower = text.toLowerCase();
    for (var i = 0; i < CLASS_ORDER.length; i++) {
        if (matchesAnyPattern(lower, patterns[CLASS_ORDER[i]])) return CLASS_ORDER[i];
    }
    return UNKNOWN;
}

function readPatternDir(root) {
    // A folder named on purpose must be there: a typo would give four empty lists and pass any twin.
    // (Frostwalker itself, without its folder, has no lists at all - then nothing reads the twin.)
    if (!fs.existsSync(root) || !fs.statSync(root).isDirectory()) {
        throw new Error('--patterns "' + root + '" is not a folder (expected the Frostwalker folder holding NeutralSources/ ...)');
    }
    var patterns = {};
    CLASS_ORDER.forEach(function (key) {
        var dir = path.join(root, PATTERN_FOLDERS[key]);
        patterns[key] = [];
        if (!fs.existsSync(dir)) {
            console.log('note: no ' + PATTERN_FOLDERS[key] + ' folder in ' + root + ' - an empty list, as Frostwalker reads it');
            return;
        }
        fs.readdirSync(dir).sort().forEach(function (name) {
            var file = path.join(dir, name);
            if (!fs.statSync(file).isFile()) return;
            fs.readFileSync(file, 'latin1').split('\n').forEach(function (line) {
                var trimmed = line.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, '');
                if (trimmed && trimmed[0] !== '#') patterns[key].push(trimmed.toLowerCase());
            });
        });
    });
    return patterns;
}

// ============================================================================
// Plugin reader (uncompressed records; the twins are plain)
// ============================================================================

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
    if (buf.toString('latin1', 0, 4) !== 'TES4') throw new Error(file + ' is not a plugin (no TES4 header)');
    var headerSize = buf.readUInt32LE(4);
    var header = { flags: buf.readUInt32LE(8), hedr: '', masters: [] };
    for (var h = 24; h < 24 + headerSize;) {
        var hType = buf.toString('latin1', h, h + 4);
        var hLen = buf.readUInt16LE(h + 4);
        if (hType === 'HEDR') header.hedr = buf.subarray(h + 6, h + 6 + Math.min(4, hLen)).toString('hex');
        if (hType === 'MAST') header.masters.push(cString(buf.subarray(h + 6, h + 6 + hLen)));
        h += 6 + hLen;
    }
    var records = {};
    (function walk(start, end) {
        var p = start;
        while (p < end) {
            var type = buf.toString('latin1', p, p + 4);
            var size = buf.readUInt32LE(p + 4);
            if (type === 'GRUP') { walk(p + 24, p + size); p += size; continue; }
            var flags = buf.readUInt32LE(p + 8);
            var formId = buf.readUInt32LE(p + 12);
            var localId = formId & 0xFFFFFF;
            var entry = { type: type, formId: formId, compressed: (flags & 0x40000) !== 0, strings: {}, subrecords: [] };
            if (!entry.compressed) {
                for (var q = p + 24; q < p + 24 + size;) {
                    var sub = buf.toString('latin1', q, q + 4);
                    var len = buf.readUInt16LE(q + 4);
                    if (sub === 'EDID' || sub === 'FULL' || sub === 'MODL') {
                        entry.strings[sub] = cString(buf.subarray(q + 6, q + 6 + len));
                    }
                    entry.subrecords.push([sub, buf.subarray(q + 6, q + 6 + len).toString('hex')]);
                    q += 6 + len;
                }
            }
            records[localId] = entry;
            p += 24 + size;
        }
    })(24 + headerSize, buf.length);
    records.header = header;
    return records;
}

function parsePersistentId(id) {
    var m = /^(.+)\|0x([0-9A-Fa-f]{1,8})$/.exec(id || '');
    if (!m) throw new Error('Bad persistentId "' + id + '" (expected "Plugin.esp|0xLOCALID")');
    return { plugin: m[1], localId: parseInt(m[2], 16) };
}

// ============================================================================
// Checks
// ============================================================================

function selfTest(patterns, fail) {
    var cases = [
        ['MAG_BlizzardForceExplosion', 'cold'],   // the original's EditorID (Mysticism)
        ['BlizzardForceExplosion', 'cold'],       // the original's EditorID (Skyrim.esm)
        ['VoiceDragonFrost01', 'cold'],           // "frost" still counts in a shout
        ['VoiceUnrelentingForce', UNKNOWN],       // "ice" right after "vo" does not
        ['voiceice', 'cold'],                     // a second "ice" that is not after "vo" does
        ['PracticeIceBlast', 'neutral'],          // neutral is tested first
        ['Effects\\FXEmptyObject.nif', UNKNOWN]
    ];
    cases.forEach(function (c) {
        var got = classify(c[0], patterns);
        if (got !== c[1]) fail('matcher case "' + c[0] + '": ' + got + ', expected ' + c[1]);
    });
    return cases.length;
}

/** A new record of this plugin (not an override), its fields after EDID exactly TWIN_FIELDS' bytes */
function checkTwinRecord(id, record, masterCount, fail) {
    var top = record.formId >>> 24;
    if (top !== masterCount) {
        fail(id + ': FormID 0x' + record.formId.toString(16) + ' has index ' + top + ', expected ' + masterCount +
            ' (a record of the plugin itself)');
    }
    var expected = TWIN_FIELDS[id];
    if (!expected) { fail(id + ': no fixed field values in TWIN_FIELDS - add the original bytes'); return; }
    var got = record.subrecords.filter(function (s) { return s[0] !== 'EDID'; });
    if (record.subrecords.length === 0 || record.subrecords[0][0] !== 'EDID') fail(id + ': EDID is not the first subrecord');
    var gotText = got.map(function (s) { return s[0] + ' ' + s[1]; });
    var wantText = expected.map(function (s) { return s[0] + ' ' + s[1]; });
    var max = Math.max(gotText.length, wantText.length);
    for (var i = 0; i < max; i++) {
        if (gotText[i] !== wantText[i]) fail(id + ': field ' + (i + 1) + ' is "' + (gotText[i] || '(none)') + '", expected "' + (wantText[i] || '(none)') + '"');
    }
    console.log('  ' + id + ' fields: ' + got.map(function (s) { return s[0]; }).join(' ') + (gotText.join() === wantText.join() ? ' (as the original)' : ''));
}

function main() {
    var args = process.argv.slice(2);
    var opt = { esp: DEFAULT_ESP, twins: DEFAULT_TWINS, patterns: process.env.FROSTWALKER_DIR || '' };
    for (var i = 0; i < args.length; i++) {
        var key = args[i].replace(/^--/, '');
        if (!(key in opt) || i + 1 >= args.length) {
            console.error('Usage: node tools/frostwalker-twin-check.js [--esp <plugin>] [--twins <twins.json>] [--patterns <Frostwalker dir>]');
            process.exit(2);
        }
        opt[key] = args[++i];
    }

    var failures = 0;
    function fail(message) { failures++; console.log('FAIL ' + message); }

    var patterns;
    try {
        patterns = opt.patterns ? readPatternDir(opt.patterns) : BUILT_IN_PATTERNS;
    } catch (e) {
        console.error('ERROR ' + e.message);
        process.exit(2);
    }
    console.log('patterns: ' + (opt.patterns || 'built-in (Frostwalker 2.2 Default.ini)') + ' - ' +
        CLASS_ORDER.map(function (k) { return k + ' ' + patterns[k].length; }).join(', '));

    var cases = selfTest(BUILT_IN_PATTERNS, fail);   // fixed fixture: the matcher's own rules, not the user's lists
    console.log('matcher: ' + cases + ' cases on the built-in lists');

    var espName = path.basename(opt.esp);
    var records = readPlugin(opt.esp);
    var header = records.header;
    console.log('header: flags 0x' + header.flags.toString(16) + ', HEDR ' + header.hedr + ', masters ' + JSON.stringify(header.masters));
    if ((header.flags & LIGHT_FLAG) === 0) fail(espName + ': TES4 flags 0x' + header.flags.toString(16) + ' lack the light flag 0x200');
    if (header.hedr !== HEDR_VERSION_HEX) fail(espName + ': HEDR version bytes ' + header.hedr + ', expected ' + HEDR_VERSION_HEX + ' (1.70)');
    if (JSON.stringify(header.masters) !== JSON.stringify(EXPECTED_MASTERS)) {
        fail(espName + ': masters ' + JSON.stringify(header.masters) + ', expected ' + JSON.stringify(EXPECTED_MASTERS));
    }
    var twins = JSON.parse(fs.readFileSync(opt.twins, 'utf8')).explosions || [];
    if (twins.length === 0) fail('no explosions in ' + opt.twins);

    twins.forEach(function (entry) {
        var twin = parsePersistentId(entry.twin);
        parsePersistentId(entry.original);
        if (twin.plugin.toLowerCase() !== espName.toLowerCase()) {
            fail(entry.twin + ': twin is not in ' + espName);
            return;
        }
        var record = records[twin.localId];
        if (!record) { fail(entry.twin + ': no record 0x' + twin.localId.toString(16) + ' in ' + espName); return; }
        if (record.type !== 'EXPL') { fail(entry.twin + ': record is ' + record.type + ', not EXPL'); return; }
        if (record.compressed) { fail(entry.twin + ': compressed record, strings not read'); return; }
        ['EDID', 'FULL', 'MODL'].forEach(function (sub) {
            var text = record.strings[sub];
            if (text === undefined) { console.log('  ' + entry.twin + ' ' + sub + ': (none)'); return; }
            var got = classify(text, patterns);
            console.log('  ' + entry.twin + ' ' + sub + ': "' + text + '" -> ' + got);
            if (got !== UNKNOWN) fail(entry.twin + ' ' + sub + ' "' + text + '" reads as ' + got);
        });
        if (record.strings.EDID === undefined) fail(entry.twin + ': no EditorID');
        checkTwinRecord(entry.twin, record, header.masters.length, fail);
    });

    console.log(failures === 0 ? 'PASS ' + twins.length + ' twin(s)' : failures + ' failure(s)');
    process.exit(failures === 0 ? 0 : 1);
}

main();
