/**
 * release-zips-check.js - checks who ships the magic menu blocks, on the zips the build made in dist/
 * (docs/magicmenu/BLOCKS_MOVE.md):
 *   - Magic Menu Rewrite (MagicMenuRewrite_<mmr>.zip): the core only - MagicMenuRewrite.dll, its ini and lang files in
 *     "00 Core", no "10 Heart of Magic Block" folder, no MagicMenuRewrite_HoM or MagicMenuRewrite_SpellResearch file
 *     anywhere, no install step naming a block in its ModuleConfig.xml; its DebugSymbols zip without the blocks' .pdb.
 *   - Heart of Magic Librarian (HeartOfMagic-Librarian_<hom>.zip): MagicMenuRewrite_HoM.dll and its en/ko lang files only
 *     under optional/MagicMenuRewriteBlocks/SKSE/Plugins/ (never in the always-installed SKSE/), a ModuleConfig.xml
 *     option installing that folder (in a SelectAny group; Recommended with MagicMenuRewrite.dll, Optional otherwise,
 *     never another type; never in requiredInstallFiles or conditionalFileInstalls); its DebugSymbols zip with
 *     MagicMenuRewrite_HoM.pdb.
 *   - each block's and the core's DLL carries the expected SKSE plugin version (SKSEPlugin_Version read from the PE
 *     file): MagicMenuRewrite.dll the MMR version, MagicMenuRewrite_HoM.dll HoM's, MagicMenuRewrite_SpellResearch.dll
 *     the SR block's.
 *   - the Spell Research block's package (SpellResearchBlock_<sr>.zip): the DLL, its ini, en/ko lang files, the .pdb
 *     under DebugSymbols/, its notices, licence and the CommonLibSSE-NG licence texts; no HoM block.
 * The versions come from VERSION and the top-level CMakeLists.txt (MAGIC_MENU_VERSION, SPELL_RESEARCH_BLOCK_VERSION).
 *
 *   node tools/release-zips-check.js [--dist <dir>] [--hom <ver>] [--mmr <ver>] [--sr <ver>]
 *
 * One PASS/FAIL line per check and a total. Exit 0 when every check passes, 1 when one fails (a missing zip too),
 * 2 on bad arguments. Node only (fs, path, zlib): the zip's central directory is read directly.
 */
var fs = require('fs');
var path = require('path');
var zlib = require('zlib');

var ROOT = path.resolve(__dirname, '..');
var EOCD_SIGNATURE = 0x06054b50;
var CENTRAL_SIGNATURE = 0x02014b50;
var LOCAL_SIGNATURE = 0x04034b50;
var EOCD_MIN_SIZE = 22;
var EOCD_MAX_COMMENT = 0xffff;
var CENTRAL_HEADER_SIZE = 46;
var LOCAL_HEADER_SIZE = 30;
var METHOD_STORED = 0;
var METHOD_DEFLATE = 8;
var EXIT_OK = 0, EXIT_FAIL = 1, EXIT_USAGE = 2;
// PE layout (the offsets the SKSE plugin version reader needs)
var PE_HEADER_POINTER = 0x3c;
var PE_SIGNATURE = 0x00004550;          // "PE\0\0"
var COFF_HEADER_SIZE = 20;
var PE32_PLUS_MAGIC = 0x20b;
var PE32_PLUS_DATA_DIRECTORIES = 112;   // the export table is data directory 0
var SECTION_HEADER_SIZE = 40;
var SKSE_VERSION_EXPORT = 'SKSEPlugin_Version';

var passCount = 0, failCount = 0;

function expect(ok, text) {
    if (ok) passCount++; else failCount++;
    console.log((ok ? 'PASS ' : 'FAIL ') + text);
    return ok;
}

/** The entries of a zip: { name, method, compressedSize, localOffset } per file, '/' separators. Throws on a broken zip. */
function readZipEntries(buffer) {
    var lowest = Math.max(0, buffer.length - EOCD_MIN_SIZE - EOCD_MAX_COMMENT);
    var eocd = -1;
    for (var at = buffer.length - EOCD_MIN_SIZE; at >= lowest; at--) {
        if (buffer.readUInt32LE(at) === EOCD_SIGNATURE) { eocd = at; break; }
    }
    if (eocd < 0) throw new Error('no end of central directory record (not a zip, or zip64)');
    var count = buffer.readUInt16LE(eocd + 10);
    var offset = buffer.readUInt32LE(eocd + 16);
    var entries = [];
    for (var i = 0; i < count; i++) {
        if (offset + CENTRAL_HEADER_SIZE > buffer.length || buffer.readUInt32LE(offset) !== CENTRAL_SIGNATURE) {
            throw new Error('central directory entry ' + i + ' broken at offset ' + offset);
        }
        var nameLength = buffer.readUInt16LE(offset + 28);
        var extraLength = buffer.readUInt16LE(offset + 30);
        var commentLength = buffer.readUInt16LE(offset + 32);
        entries.push({
            name: buffer.toString('utf8', offset + CENTRAL_HEADER_SIZE, offset + CENTRAL_HEADER_SIZE + nameLength).replace(/\\/g, '/'),
            method: buffer.readUInt16LE(offset + 10),
            compressedSize: buffer.readUInt32LE(offset + 20),
            localOffset: buffer.readUInt32LE(offset + 42)
        });
        offset += CENTRAL_HEADER_SIZE + nameLength + extraLength + commentLength;
    }
    return entries;
}

/** One entry's bytes (stored or deflated). Throws on anything else. */
function readZipFile(buffer, entry) {
    var at = entry.localOffset;
    if (buffer.readUInt32LE(at) !== LOCAL_SIGNATURE) throw new Error('local header of ' + entry.name + ' broken');
    var start = at + LOCAL_HEADER_SIZE + buffer.readUInt16LE(at + 26) + buffer.readUInt16LE(at + 28);
    var data = buffer.subarray(start, start + entry.compressedSize);
    if (entry.method === METHOD_STORED) return data;
    if (entry.method === METHOD_DEFLATE) return zlib.inflateRawSync(data);
    throw new Error(entry.name + ': compression method ' + entry.method + ' not read');
}

/** { names: [file names, no directories], text(name) } of a zip, or null (a FAIL line written) when it cannot be read */
function openZip(file, label) {
    if (!expect(fs.existsSync(file), label + ': ' + path.basename(file) + ' exists')) return null;
    var buffer = fs.readFileSync(file);
    var entries;
    try {
        entries = readZipEntries(buffer);
    } catch (e) {
        expect(false, label + ': ' + path.basename(file) + ' reads as a zip (' + e.message + ')');
        return null;
    }
    var files = entries.filter(function (e) { return !/\/$/.test(e.name); });
    function bytes(name) {
        var entry = files.find(function (e) { return e.name === name; });
        return entry ? readZipFile(buffer, entry) : null;
    }
    return {
        names: files.map(function (e) { return e.name; }),
        bytes: bytes,
        text: function (name) {
            var data = bytes(name);
            return data ? data.toString('utf8') : null;
        }
    };
}

/**
 * The SKSE plugin version of a DLL: the export SKSEPlugin_Version (CommonLibSSE-NG's PluginDeclaration: a uint32 data
 * version, 1, then the version packed as major << 24 | minor << 16 | patch << 4 | build) read from the PE file, as
 * "major.minor.patch". Throws when the DLL has no such export.
 */
function readSksePluginVersion(dll) {
    var pe = dll.readUInt32LE(PE_HEADER_POINTER);
    if (dll.readUInt32LE(pe) !== PE_SIGNATURE) throw new Error('not a PE file');
    var coff = pe + 4;
    var sectionCount = dll.readUInt16LE(coff + 2);
    var optional = coff + COFF_HEADER_SIZE;
    if (dll.readUInt16LE(optional) !== PE32_PLUS_MAGIC) throw new Error('not a 64-bit PE file');
    var exportRva = dll.readUInt32LE(optional + PE32_PLUS_DATA_DIRECTORIES);
    var sections = optional + dll.readUInt16LE(coff + 16);
    function offsetOf(rva) {
        for (var i = 0; i < sectionCount; i++) {
            var at = sections + i * SECTION_HEADER_SIZE;
            var start = dll.readUInt32LE(at + 12), size = Math.max(dll.readUInt32LE(at + 8), dll.readUInt32LE(at + 16));
            if (rva >= start && rva < start + size) return dll.readUInt32LE(at + 20) + rva - start;
        }
        throw new Error('RVA 0x' + rva.toString(16) + ' in no section');
    }
    if (!exportRva) throw new Error('no export table');
    var exports = offsetOf(exportRva);
    var nameCount = dll.readUInt32LE(exports + 24);
    var functions = offsetOf(dll.readUInt32LE(exports + 28));
    var names = offsetOf(dll.readUInt32LE(exports + 32));
    var ordinals = offsetOf(dll.readUInt32LE(exports + 36));
    for (var n = 0; n < nameCount; n++) {
        var nameAt = offsetOf(dll.readUInt32LE(names + n * 4));
        var name = dll.toString('latin1', nameAt, dll.indexOf(0, nameAt));
        if (name !== SKSE_VERSION_EXPORT) continue;
        var data = offsetOf(dll.readUInt32LE(functions + dll.readUInt16LE(ordinals + n * 2) * 4));
        if (dll.readUInt32LE(data) !== 1) throw new Error(SKSE_VERSION_EXPORT + ' data version ' + dll.readUInt32LE(data) + ', not 1');
        var packed = dll.readUInt32LE(data + 4);
        return [packed >>> 24, (packed >>> 16) & 0xff, (packed >>> 4) & 0xfff].join('.');
    }
    throw new Error('no ' + SKSE_VERSION_EXPORT + ' export');
}

function expectDllVersion(zip, name, version, label) {
    var data = zip.bytes(name);
    var got;
    try {
        got = data ? readSksePluginVersion(data) : 'missing';
    } catch (e) {
        got = 'unreadable (' + e.message + ')';
    }
    expect(got === version, label + ': ' + path.basename(name) + ' says SKSE plugin version ' + got + ', expected ' + version);
}

/** The text of the FOMOD <group> that holds the <plugin> named like a_pattern, and that plugin's text ('' when none) */
function fomodOption(config, pattern) {
    var groups = config.match(/<group\b[^>]*>[\s\S]*?<\/group>/g) || [];
    for (var i = 0; i < groups.length; i++) {
        var plugin = (groups[i].match(new RegExp('<plugin name="[^"]*' + pattern + '[^"]*">[\\s\\S]*?</plugin>')) || [''])[0];
        if (plugin) return { group: groups[i], plugin: plugin };
    }
    return { group: '', plugin: '' };
}

function has(zip, name) {
    return zip.names.indexOf(name) >= 0;
}

function matching(zip, pattern) {
    return zip.names.filter(function (n) { return pattern.test(n); });
}

function list(names) {
    return names.length ? names.join(', ') : 'none';
}

function checkMagicMenu(dist, version) {
    var folder = 'MagicMenuRewrite_' + version;
    console.log('Magic Menu Rewrite ' + version);
    var zip = openZip(path.join(dist, folder + '.zip'), 'MMR');
    if (zip) {
        var core = folder + '/00 Core/SKSE/Plugins/';
        ['MagicMenuRewrite.dll', 'MagicMenuRewrite.ini', 'MagicMenuRewrite/lang/en.json', 'MagicMenuRewrite/lang/ko.json',
            'InventoryReforged/MagicMenuRewrite.ini'].forEach(function (name) {
            expect(has(zip, core + name), 'MMR: 00 Core/SKSE/Plugins/' + name);
        });
        ['fomod/ModuleConfig.xml', 'fomod/info.xml', 'LICENSE', 'THIRD-PARTY-NOTICES.md'].forEach(function (name) {
            expect(has(zip, folder + '/' + name), 'MMR: ' + name);
        });
        var blocks = matching(zip, /MagicMenuRewrite_(HoM|SpellResearch)/i);
        expect(blocks.length === 0, 'MMR: no block file (MagicMenuRewrite_HoM*, MagicMenuRewrite_SpellResearch*) - found ' + list(blocks));
        var heartFolder = matching(zip, /\/10 Heart of Magic Block\//);
        expect(heartFolder.length === 0, 'MMR: no "10 Heart of Magic Block" folder - found ' + list(heartFolder));
        var tops = Array.from(new Set(zip.names.map(function (n) { return n.split('/')[1]; })));
        var extra = tops.filter(function (t) { return ['00 Core', 'fomod', 'licenses', 'LICENSE', 'THIRD-PARTY-NOTICES.md'].indexOf(t) < 0; });
        expect(extra.length === 0, 'MMR: nothing beside 00 Core, fomod, licenses, LICENSE, THIRD-PARTY-NOTICES.md - found ' + list(extra));
        var config = zip.text(folder + '/fomod/ModuleConfig.xml') || '';
        expect(!/<installSteps\b/.test(config) && !/MagicMenuRewrite_HoM|Heart of Magic Block/.test(config),
            'MMR: ModuleConfig.xml has no install step and names no block');
        var info = zip.text(folder + '/fomod/info.xml') || '';
        expect(info.indexOf('<Version>' + version + '</Version>') >= 0, 'MMR: fomod/info.xml says ' + version);
        expectDllVersion(zip, core + 'MagicMenuRewrite.dll', version, 'MMR');
        var notices = zip.text(folder + '/THIRD-PARTY-NOTICES.md') || '';
        expect(notices.indexOf('10 Heart of Magic Block') < 0 && notices.indexOf('plugins/SL_MagicMenu_HoM') < 0,
            'MMR: THIRD-PARTY-NOTICES.md no longer lists the Heart of Magic block');
    }
    var symbols = openZip(path.join(dist, folder + '_DebugSymbols.zip'), 'MMR symbols');
    if (symbols) {
        expect(has(symbols, folder + '_DebugSymbols/SKSE/Plugins/MagicMenuRewrite.pdb'), 'MMR symbols: MagicMenuRewrite.pdb');
        var blockPdbs = matching(symbols, /MagicMenuRewrite_(HoM|SpellResearch)/i);
        expect(blockPdbs.length === 0, 'MMR symbols: no block .pdb - found ' + list(blockPdbs));
    }
}

function checkHeartOfMagic(dist, version) {
    var folder = 'HeartOfMagic-Librarian_' + version;
    var optionDir = folder + '/optional/MagicMenuRewriteBlocks/SKSE/Plugins/';
    console.log('Heart of Magic Librarian ' + version);
    var zip = openZip(path.join(dist, folder + '.zip'), 'HoM');
    if (zip) {
        ['SpellLearning.dll', 'SL_BookXP.dll'].forEach(function (name) {
            expect(has(zip, folder + '/SKSE/Plugins/' + name), 'HoM: SKSE/Plugins/' + name + ' (the zip is HoM\'s)');
        });
        ['MagicMenuRewrite_HoM.dll', 'MagicMenuRewrite_HoM/lang/en.json', 'MagicMenuRewrite_HoM/lang/ko.json'].forEach(function (name) {
            expect(has(zip, optionDir + name), 'HoM: optional/MagicMenuRewriteBlocks/SKSE/Plugins/' + name);
        });
        expectDllVersion(zip, optionDir + 'MagicMenuRewrite_HoM.dll', version, 'HoM');
        var block = matching(zip, /MagicMenuRewrite_HoM/i);
        var outside = block.filter(function (n) { return n.indexOf(optionDir) !== 0; });
        expect(block.length === 3 && outside.length === 0,
            'HoM: the block\'s 3 files only under the optional folder (' + block.length + ' found, outside it: ' + list(outside) + ')');
        var others = matching(zip, /(^|\/)MagicMenuRewrite(\.|\/|_SpellResearch)/i);
        expect(others.length === 0, 'HoM: no other Magic Menu Rewrite file (the core, the Spell Research block) - found ' + list(others));
        var config = zip.text(folder + '/fomod/ModuleConfig.xml') || '';
        var required = (config.match(/<requiredInstallFiles>[\s\S]*?<\/requiredInstallFiles>/) || [''])[0];
        var conditional = (config.match(/<conditionalFileInstalls>[\s\S]*?<\/conditionalFileInstalls>/) || [''])[0];
        expect(required.length > 0 && required.indexOf('MagicMenuRewriteBlocks') < 0 && conditional.indexOf('MagicMenuRewriteBlocks') < 0,
            'HoM: ModuleConfig.xml installs the block neither always nor by condition');
        var option = fomodOption(config, 'Magic Menu Rewrite');
        var plugin = option.plugin;
        var groupType = (option.group.match(/^<group\b[^>]*\btype="([^"]*)"/) || [null, 'none'])[1];
        expect(groupType === 'SelectAny', 'HoM: the option\'s group is SelectAny (got ' + groupType + ')');
        var types = (plugin.match(/<(?:defaultType|type) name="([^"]*)"\/>/g) || []).map(function (t) { return t.match(/name="([^"]*)"/)[1]; });
        var badTypes = types.filter(function (t) { return t !== 'Optional' && t !== 'Recommended'; });
        expect(types.length > 0 && badTypes.length === 0,
            'HoM: the option is only ever Optional or Recommended (got ' + list(types) + ')');
        expect(/<folder source="optional\\MagicMenuRewriteBlocks\\SKSE" destination="SKSE"/.test(plugin),
            'HoM: a Magic Menu Rewrite option installs optional\\MagicMenuRewriteBlocks\\SKSE to SKSE');
        expect(/<defaultType name="Optional"\/>/.test(plugin) &&
                /<fileDependency file="SKSE\\Plugins\\MagicMenuRewrite\.dll" state="Active"\/>\s*<\/dependencies>\s*<type name="Recommended"\/>/.test(plugin),
            'HoM: the option is Recommended with MagicMenuRewrite.dll, Optional otherwise');
        var info = zip.text(folder + '/fomod/info.xml') || '';
        expect(info.indexOf('<Version>' + version + '</Version>') >= 0, 'HoM: fomod/info.xml says ' + version);
        var notices = zip.text(folder + '/THIRD-PARTY-NOTICES.md') || '';
        expect(notices.indexOf('optional/MagicMenuRewriteBlocks/SKSE/Plugins/MagicMenuRewrite_HoM.dll') >= 0 &&
                notices.indexOf('tag `v' + version + '`') >= 0,
            'HoM: THIRD-PARTY-NOTICES.md lists the block DLL, source tag v' + version);
    }
    var symbols = openZip(path.join(dist, folder + '_DebugSymbols.zip'), 'HoM symbols');
    if (symbols) {
        expect(has(symbols, folder + '_DebugSymbols/SKSE/Plugins/MagicMenuRewrite_HoM.pdb'), 'HoM symbols: MagicMenuRewrite_HoM.pdb');
        expect(has(symbols, folder + '_DebugSymbols/SKSE/Plugins/SpellLearning.pdb'), 'HoM symbols: SpellLearning.pdb');
    }
}

function checkSpellResearchBlock(dist, version) {
    var name = 'MagicMenuRewrite_SpellResearch';
    console.log('Spell Research block ' + version);
    var zip = openZip(path.join(dist, 'SpellResearchBlock_' + version + '.zip'), 'SR block');
    if (!zip) return;
    var wanted = [
        'SKSE/Plugins/' + name + '.dll', 'SKSE/Plugins/' + name + '.ini',
        'SKSE/Plugins/' + name + '/lang/en.json', 'SKSE/Plugins/' + name + '/lang/ko.json',
        'DebugSymbols/SKSE/Plugins/' + name + '.pdb',
        name + '-THIRD-PARTY-NOTICES.md', name + '-LICENSE.txt',
        'licenses/CommonLibSSE-NG/COPYING.txt', 'licenses/CommonLibSSE-NG/EXCEPTIONS.md', 'licenses/CommonLibSSE-NG/LICENSE-MIT.txt'
    ];
    wanted.forEach(function (file) { expect(has(zip, file), 'SR block: ' + file); });
    var hom = matching(zip, /MagicMenuRewrite_HoM|MagicMenuRewrite\.dll/i);
    expect(hom.length === 0, 'SR block: no HoM block or core file - found ' + list(hom));
    expectDllVersion(zip, 'SKSE/Plugins/' + name + '.dll', version, 'SR block');
    var notices = zip.text(name + '-THIRD-PARTY-NOTICES.md') || '';
    var missingTexts = (notices.match(/`licenses\/[^`]+`/g) || []).map(function (t) { return t.slice(1, -1); })
        .filter(function (t) { return !has(zip, t); });
    expect(notices.length > 0 && missingTexts.length === 0, 'SR block: every licence text the notices name is in the zip - missing ' + list(missingTexts));
    expect(notices.indexOf('spell-research-block-v' + version) >= 0, 'SR block: notices name the source tag spell-research-block-v' + version);
}

function readVersions() {
    var cmake = fs.readFileSync(path.join(ROOT, 'CMakeLists.txt'), 'utf8');
    function cmakeVar(key) {
        var m = cmake.match(new RegExp('set\\(' + key + ' "([^"]+)"\\)'));
        return m ? m[1] : null;
    }
    return {
        hom: fs.readFileSync(path.join(ROOT, 'VERSION'), 'utf8').trim(),
        mmr: cmakeVar('MAGIC_MENU_VERSION'),
        sr: cmakeVar('SPELL_RESEARCH_BLOCK_VERSION')
    };
}

function main(argv) {
    var options = { dist: path.join(ROOT, 'dist') };
    var versions = readVersions();
    for (var i = 0; i < argv.length; i++) {
        var key = argv[i].replace(/^--/, '');
        if (['dist', 'hom', 'mmr', 'sr'].indexOf(key) < 0 || i + 1 >= argv.length) {
            console.error('usage: node tools/release-zips-check.js [--dist <dir>] [--hom <ver>] [--mmr <ver>] [--sr <ver>] (bad argument: ' + argv[i] + ')');
            return EXIT_USAGE;
        }
        if (key === 'dist') options.dist = path.resolve(argv[++i]); else versions[key] = argv[++i];
    }
    if (!versions.mmr || !versions.sr) {
        console.error('MAGIC_MENU_VERSION or SPELL_RESEARCH_BLOCK_VERSION not found in CMakeLists.txt: pass --mmr and --sr');
        return EXIT_USAGE;
    }
    checkMagicMenu(options.dist, versions.mmr);
    checkHeartOfMagic(options.dist, versions.hom);
    checkSpellResearchBlock(options.dist, versions.sr);
    console.log('release-zips-check: ' + passCount + ' PASS, ' + failCount + ' FAIL');
    return failCount === 0 ? EXIT_OK : EXIT_FAIL;
}

process.exitCode = main(process.argv.slice(2));
