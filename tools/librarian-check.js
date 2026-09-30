/**
 * librarian-check.js - one command that says whether a change made the
 * librarian worse, or anything slower
 *
 *   node tools/librarian-check.js            check against tools/librarian-baseline.json
 *   node tools/librarian-check.js --update   write the current figures as the new baseline
 *   node tools/librarian-check.js --log <SpellLearning.log>   also read the game's timings
 *
 * Accuracy (a drop fails the check, exit 1):
 *   - the Spell Research answer set, all rules and the vanilla-structure tier alone
 *     (librarian-test -a, -t mgef): precision, recall, F1
 *   - every AI-judged round in lab/librarian-judge/<round>/verdicts.json: precision
 *   - the share of spells with no tag at all (a rise fails)
 * Speed (over budget warns - timings depend on the machine):
 *   - classifying the pinned dump, a Classic tree build of it (treebuilder-test)
 *   - with --log: the perk adapters at game start and after a scan, from the game's log
 * Also reported: how many keyword writes the perk adapters plan.
 *
 * Inputs: the pinned dump (lab/librarian-judge/dumps/, not in git) and the answer set,
 * paths in tools/librarian-check.local.json (not in git):
 *   { "dump": "...", "answers": "..." }
 * Needs the build's tools (build/tools/Release). Plain Node, no packages.
 */
var fs = require('fs');
var os = require('os');
var path = require('path');
var childProcess = require('child_process');

var ROOT = path.resolve(__dirname, '..');
var TOOLS = path.join(ROOT, 'build', 'tools', 'Release');
var RULES = path.join(ROOT, 'SKSE', 'Plugins', 'SpellLearning', 'librarian');
var JUDGE_DIR = path.join(ROOT, 'lab', 'librarian-judge');
var BASELINE = path.join(__dirname, 'librarian-baseline.json');
var LOCAL = path.join(__dirname, 'librarian-check.local.json');

// A figure this far below its baseline counts as a drop (rounding of the tools' output)
var TOLERANCE = 0.05;

var judge = require(path.join(JUDGE_DIR, 'judge.js'));

function readJson(file) {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function run(exe, args) {
    var started = Date.now();
    var result = childProcess.spawnSync(path.join(TOOLS, exe), args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    if (result.error) throw new Error(exe + ': ' + result.error.message);
    return { out: (result.stdout || '') + (result.stderr || ''), ms: Date.now() - started, status: result.status };
}

// A tool run that failed, or printed no figure where one was expected, stops the
// check: a missing figure must not read as "nothing got worse"
function need(value, what, out) {
    if (value === null || value === undefined) throw new Error(what + ' not found in the tool output:\n' + out);
    return value;
}

function number(text, pattern) {
    var match = text.match(pattern);
    return match ? parseFloat(match[1]) : null;
}

function scoreLine(out, axis) {
    var match = out.match(new RegExp('^\\s+' + axis + '\\s+P\\s+([\\d.]+)\\s+R\\s+([\\d.]+)\\s+F1\\s+([\\d.]+)', 'm'));
    return match ? { p: parseFloat(match[1]), r: parseFloat(match[2]), f1: parseFloat(match[3]) } : null;
}

function localPaths() {
    var local = fs.existsSync(LOCAL) ? readJson(LOCAL) : {};
    return {
        dump: local.dump ? path.resolve(ROOT, local.dump) : path.join(JUDGE_DIR, 'dumps', 'takealook-2026-09-30.json'),
        answers: local.answers ? path.resolve(ROOT, local.answers) : null
    };
}

function measure(paths, logPath) {
    var tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'librarian-check-'));
    try {
        return measureIn(tmp, paths, logPath);
    } finally {
        fs.rmSync(tmp, { recursive: true, force: true });
    }
}

function measureIn(tmp, paths, logPath) {
    var catalog = path.join(tmp, 'catalog.json');
    var merged = path.join(tmp, 'merged.json');
    var figures = { accuracy: {}, judge: {}, speed: {}, info: {} };

    var full = run('librarian-test.exe', ['-i', paths.dump, '-r', RULES, '-o', catalog, '-m', merged]
        .concat(paths.answers ? ['-a', paths.answers] : []));
    if (full.status !== 0 || !fs.existsSync(catalog)) throw new Error('librarian-test failed:\n' + full.out);
    figures.speed.classifyMs = full.ms;
    figures.accuracy.noTagsPct = need(number(full.out, /no tags at all\s+\d+\s+([\d.]+)%/), 'untagged share', full.out);
    if (paths.answers) {
        figures.accuracy.all = need(scoreLine(full.out, 'both'), 'answer set score', full.out);
        var mgef = run('librarian-test.exe', ['-i', paths.dump, '-r', RULES, '-a', paths.answers, '-t', 'mgef']);
        if (mgef.status !== 0) throw new Error('librarian-test -t mgef failed:\n' + mgef.out);
        figures.accuracy.mgef = need(scoreLine(mgef.out, 'both'), 'mgef answer set score', mgef.out);
        figures.accuracy.mgefNoTagsPct = need(number(mgef.out, /no tags at all\s+\d+\s+([\d.]+)%/), 'mgef untagged share', mgef.out);
    }

    var spells = readJson(catalog).spells || {};
    fs.readdirSync(JUDGE_DIR).forEach(function (round) {
        var verdicts = path.join(JUDGE_DIR, round, 'verdicts.json');
        if (!fs.existsSync(verdicts)) return;
        var result = judge.scoreRound(readJson(verdicts), spells);
        figures.judge[round] = result.precision === null ? null : Math.round(result.precision * 10) / 10;
        figures.info['judgeUnjudged_' + round] = result.unjudged;
    });

    var adapters = run('librarian-test.exe', ['-i', paths.dump, '--catalog', catalog,
        '--adapters', path.join(RULES, 'adapters')]);
    if (adapters.status !== 0) throw new Error('librarian-test --adapters failed:\n' + adapters.out);
    figures.info.adapterWrites = need(number(adapters.out, /planned writes:\s*(\d+)/), 'planned adapter writes', adapters.out);

    // treebuilder-test exits 2 on a bad link or a school that never branches: that is a failure too
    var tree = run('treebuilder-test.exe', ['-i', merged, '-o', path.join(tmp, 'tree.json'), '-t', 'classic', '-s', '1']);
    if (tree.status !== 0) throw new Error('treebuilder-test failed (exit ' + tree.status + '):\n' + tree.out);
    figures.speed.treeBuildMs = need(number(tree.out, /\(([\d.]+) ms\)/), 'tree build time', tree.out);

    if (logPath) {
        var log = fs.readFileSync(logPath, 'utf8');
        var last = function (pattern) {
            var all = log.match(new RegExp(pattern, 'g'));
            return all ? number(all[all.length - 1], new RegExp(pattern)) : null;
        };
        figures.speed.adaptersAtStartMs = last('PerkAdapters: data loaded .*? in (\\d+) ms');
        figures.speed.adaptersAfterScanMs = last('PerkAdapters: scan .*? in (\\d+) ms');
    }

    return figures;
}

function compare(now, base) {
    var failures = [], warnings = [];
    var drop = function (label, value, was) {
        if (value === null || value === undefined || was === null || was === undefined) return;
        if (value < was - TOLERANCE) failures.push(label + ' ' + was + ' -> ' + value);
    };
    ['all', 'mgef'].forEach(function (tier) {
        var a = now.accuracy[tier], b = (base.accuracy || {})[tier];
        if (!a || !b) return;
        drop('answer set (' + tier + ') precision', a.p, b.p);
        drop('answer set (' + tier + ') recall', a.r, b.r);
        drop('answer set (' + tier + ') F1', a.f1, b.f1);
    });
    Object.keys(base.judge || {}).forEach(function (round) {
        if (!(round in now.judge)) failures.push('judged round ' + round + ' is missing');
        else drop('judged round ' + round + ' precision', now.judge[round], base.judge[round]);
    });
    ['noTagsPct', 'mgefNoTagsPct'].forEach(function (key) {
        var value = now.accuracy[key], was = (base.accuracy || {})[key];
        if (value !== null && value !== undefined && was !== null && was !== undefined && value > was + TOLERANCE) {
            failures.push(key + ' ' + was + ' -> ' + value);
        }
    });
    var budgets = base.budgets || {};
    Object.keys(budgets).forEach(function (key) {
        var value = now.speed[key];
        if (value !== null && value !== undefined && value > budgets[key]) {
            warnings.push(key + ' ' + value + ' ms, budget ' + budgets[key] + ' ms');
        }
    });
    return { failures: failures, warnings: warnings };
}

function main() {
    var args = process.argv.slice(2);
    var update = args.indexOf('--update') >= 0;
    var logAt = args.indexOf('--log');
    if (logAt >= 0 && !args[logAt + 1]) {
        console.error('--log needs the path of SpellLearning.log');
        process.exit(2);
    }
    var paths = localPaths();
    if (!fs.existsSync(paths.dump)) {
        console.error('No pinned dump at ' + paths.dump + ' - copy a spell_scan_output.json there or set "dump" in ' + LOCAL);
        process.exit(2);
    }
    if (!paths.answers) console.warn('No answer set configured ("answers" in ' + LOCAL + '): answer set figures skipped');

    var now;
    try {
        now = measure(paths, logAt >= 0 ? args[logAt + 1] : null);
    } catch (e) {
        console.error(e.message);
        console.log('librarian-check: FAILED (a tool run failed)');
        process.exit(1);
    }
    console.log(JSON.stringify(now, null, 2));

    if (update) {
        var old = fs.existsSync(BASELINE) ? readJson(BASELINE) : {};
        var next = {
            _comment: 'Written by tools/librarian-check.js --update. Accuracy may not drop; budgets only warn.',
            dump: path.basename(paths.dump),
            updated: new Date().toISOString().slice(0, 10),
            accuracy: now.accuracy,
            judge: now.judge,
            budgets: old.budgets || { classifyMs: 15000, treeBuildMs: 4000, adaptersAtStartMs: 2000, adaptersAfterScanMs: 1500 }
        };
        fs.writeFileSync(BASELINE, JSON.stringify(next, null, 2) + '\n');
        console.log('Baseline written: ' + BASELINE);
        return;
    }
    if (!fs.existsSync(BASELINE)) {
        console.error('No baseline yet - run with --update first');
        process.exit(2);
    }
    var base = readJson(BASELINE);
    if (base.dump && base.dump !== path.basename(paths.dump)) {
        console.warn('The baseline was taken on ' + base.dump + ', this run uses ' + path.basename(paths.dump));
    }
    var result = compare(now, base);
    result.warnings.forEach(function (w) { console.warn('SLOW  ' + w); });
    result.failures.forEach(function (f) { console.error('WORSE ' + f); });
    console.log(result.failures.length ? 'librarian-check: FAILED' : 'librarian-check: OK');
    process.exit(result.failures.length ? 1 : 0);
}

main();
