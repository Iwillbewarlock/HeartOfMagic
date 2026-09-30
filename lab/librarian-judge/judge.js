/**
 * judge.js - the AI judging loop for the spell tag librarian
 *
 * The Spell Research answer set is vanilla-heavy, so mod spells are measured
 * by an AI reading each spell's effect descriptions (LIBRARIAN.md 5절). This
 * script does the mechanical halves of that loop:
 *
 *   node judge.js sample <scan dump> <catalog> <out sample.json> [count] [seed]
 *       Draws <count> (default 120) mod spells - not the game or its DLC - that
 *       have a visible effect with a description, seeded (default 20260930),
 *       with their visible effects and the catalog's tags. An AI judges the
 *       sample against TAGS.md and LIBRARIAN.md 3절 and writes verdicts.json:
 *       [{ id, name, correct: [tag], wrong: [{tag, why}], missing: [{tag, kind, why}] }]
 *       (kind: "structural" - a record field shows it - or "narrative").
 *
 *   node judge.js score <verdicts.json> <catalog> [--rejudge <out.json>]
 *       Scores any catalog against a round's verdicts: a tag the verdicts call
 *       correct or missing counts right, one they call wrong counts wrong, one
 *       they never saw is "unjudged" (a tag the rules gave after the round).
 *       Precision is right / (right + wrong). --rejudge writes the spells with
 *       unjudged tags, for the next round to judge only what changed.
 *
 * Plain Node, no packages. Not shipped (lab/).
 */
var fs = require('fs');

var BASE_GAME = ['skyrim.esm', 'update.esm', 'dawnguard.esm', 'hearthfires.esm', 'dragonborn.esm'];

function readJson(path) {
    return JSON.parse(fs.readFileSync(path, 'utf8'));
}

// mulberry32: a small seeded generator, so a round can be drawn again
function seeded(seed) {
    var state = seed >>> 0;
    return function () {
        state = (state + 0x6D2B79F5) >>> 0;
        var t = state;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

function isHidden(effect) {
    return !!(effect.flags && effect.flags.hideInUI);
}

function sample(dumpPath, catalogPath, outPath, count, seed) {
    var spells = readJson(dumpPath).spells || [];
    var catalog = readJson(catalogPath).spells || {};
    var pool = spells.filter(function (s) {
        var plugin = String(s.plugin || '').toLowerCase();
        if (BASE_GAME.indexOf(plugin) >= 0 || !catalog[s.persistentId]) return false;
        return (s.effects || []).some(function (e) { return !isHidden(e) && String(e.description || '').trim(); });
    });
    var random = seeded(seed);
    // Fisher-Yates over the pool, first <count> taken
    for (var i = pool.length - 1; i > 0; i--) {
        var j = Math.floor(random() * (i + 1));
        var swap = pool[i]; pool[i] = pool[j]; pool[j] = swap;
    }
    var out = pool.slice(0, count).map(function (s) {
        var tags = catalog[s.persistentId];
        return {
            id: s.persistentId,
            name: s.name,
            school: s.school,
            delivery: s.delivery,
            effects: (s.effects || []).filter(function (e) { return !isHidden(e); }).map(function (e) {
                return {
                    name: e.name, archetype: e.archetype, av: e.primaryAV,
                    description: String(e.description || '').slice(0, 300)
                };
            }),
            elements: tags.elements || [],
            techniques: tags.techniques || []
        };
    });
    fs.writeFileSync(outPath, JSON.stringify(out, null, 1));
    console.log('pool ' + pool.length + ', sampled ' + out.length + ' -> ' + outPath);
}

function score(verdictPath, catalogPath, rejudgePath) {
    var verdicts = readJson(verdictPath);
    var catalog = readJson(catalogPath).spells || {};
    var right = 0, wrong = 0, unjudged = 0;
    var wrongTags = {}, rejudge = [];
    verdicts.forEach(function (v) {
        var entry = catalog[v.id] || {};
        var tags = (entry.elements || []).concat(entry.techniques || []);
        var good = {}, bad = {};
        (v.correct || []).forEach(function (t) { good[t] = true; });
        (v.missing || []).forEach(function (m) { good[m.tag] = true; });
        (v.wrong || []).forEach(function (w) { bad[w.tag] = true; });
        var fresh = [];
        tags.forEach(function (t) {
            if (good[t]) right++;
            else if (bad[t]) { wrong++; wrongTags[t] = (wrongTags[t] || 0) + 1; }
            else { unjudged++; fresh.push(t); }
        });
        if (fresh.length) rejudge.push({ id: v.id, name: v.name, unjudged: fresh, tags: tags });
    });
    var precision = right + wrong ? (100 * right / (right + wrong)).toFixed(1) : '-';
    console.log('right ' + right + '  wrong ' + wrong + '  unjudged ' + unjudged + '  precision ' + precision + '%');
    var ranked = Object.keys(wrongTags).sort(function (a, b) { return wrongTags[b] - wrongTags[a]; });
    console.log('still wrong: ' + ranked.map(function (t) { return t + ' ' + wrongTags[t]; }).join(', '));
    if (rejudgePath) {
        fs.writeFileSync(rejudgePath, JSON.stringify(rejudge, null, 1));
        console.log(rejudge.length + ' spells with unjudged tags -> ' + rejudgePath);
    }
}

var args = process.argv.slice(2);
if (args[0] === 'sample' && args.length >= 4) {
    sample(args[1], args[2], args[3], parseInt(args[4] || '120', 10), parseInt(args[5] || '20260930', 10));
} else if (args[0] === 'score' && args.length >= 3) {
    var at = args.indexOf('--rejudge');
    score(args[1], args[2], at > 0 ? args[at + 1] : null);
} else {
    console.log('usage: node judge.js sample <dump> <catalog> <out.json> [count] [seed]');
    console.log('       node judge.js score <verdicts.json> <catalog> [--rejudge <out.json>]');
    process.exit(1);
}
