/**
 * ClassicRadialOrderTest - node tests for the Classic layout's radial order
 * pass (classic/classicRadialOrder.js, run-tests.js).
 *
 * Checks: cost ranks per tier (ties share a rank, no cost ranks lowest, a tier
 * of one is 0.5, mod-sized costs only count by rank); the key puts tier before
 * cost; a cheap spell out past a dear one of its tier trades places with it,
 * and a lower tier out past a higher one too (a dear Expert inside a cheap
 * Master); a spell can trade with its own child; roots never move; a trade that
 * would stretch the lines too far is not made; spells keep distinct grid
 * points; a school already in order is left alone; a tree built by Magicka
 * cost lends its tiers as the spells' levels (treeLevels, withLevels).
 *
 * Depends on: classic/classicRadialOrder.js - under node this file runs it
 * into the global scope the way the page's script tag does.
 */

var ClassicRadialOrderTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function' || typeof g.ClassicRadialOrder !== 'undefined') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'classic', 'classicRadialOrder.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    _r: function(n) { return Math.sqrt(n.x * n.x + n.y * n.y); },

    run: function() {
        console.log('[ClassicRadialOrderTest] higher tier and dearer spells farther out');
        var g = typeof global !== 'undefined' ? global : window;
        this._load(g);
        var O = g.ClassicRadialOrder;
        var self = this;

        // ----- cost ranks -----
        var spells = {
            a: { skillLevel: 'Adept', magickaCost: 50 },
            b: { skillLevel: 'Adept', magickaCost: 5000 },
            c: { skillLevel: 'Adept', magickaCost: 50 },
            d: { skillLevel: 'Adept' },
            e: { skillLevel: 'Master', magickaCost: 900 }
        };
        var ranks = O.costRanks(['a', 'b', 'c', 'd', 'e'], function(id) { return spells[id]; });
        this.check(ranks.d === 0, 'no cost ranks lowest');
        this.check(ranks.a === ranks.c && ranks.a > 0 && ranks.a < 1, 'equal costs share a rank (' + ranks.a + ')');
        this.check(ranks.b === 1, 'the dearest of a tier ranks 1, however large the number');
        this.check(ranks.e === 0.5, 'a tier of one spell ranks 0.5');

        this.check(O.key('Apprentice', 1) < O.key('Adept', 0), 'tier before cost: the dearest Apprentice is under the cheapest Adept');
        this.check(O.key('Adept', 0.2) < O.key('Adept', 0.8), 'within a tier: dearer is higher');
        this.check(O.key('', 0.5) === null, 'unknown tier: no key');

        var S = 40;
        var spellOf = function(table) { return function(id) { return table[id] || null; }; };

        // ----- a cheap spell outside a dear one of its tier, side by side -----
        var t1 = { cheap: { skillLevel: 'Adept', magickaCost: 10 }, dear: { skillLevel: 'Adept', magickaCost: 300 },
                   root: { skillLevel: 'Novice', magickaCost: 5 } };
        var p1 = [
            { formId: 'root', x: 0, y: 100, isRoot: true, parentFormId: null, gridIdx: 0, skillLevel: 'Novice' },
            { formId: 'cheap', x: 0, y: 200, parentFormId: 'root', gridIdx: 1, skillLevel: 'Adept' },
            { formId: 'dear', x: 40, y: 160, parentFormId: 'root', gridIdx: 2, skillLevel: 'Adept' }
        ];
        var n1 = O.apply(p1, spellOf(t1), S);
        this.check(n1 >= 1 && this._r(p1[2]) > this._r(p1[1]), 'the dearer spell ends farther out (' + n1 + ' trade)');
        this.check(p1[0].x === 0 && p1[0].y === 100, 'the root stays');
        this.check(p1[1].gridIdx === 2 && p1[2].gridIdx === 1, 'grid points go with the positions');

        // ----- a lower tier outside a higher one -----
        var t2 = { root: { skillLevel: 'Novice' }, nov: { skillLevel: 'Novice', magickaCost: 400 },
                   exp: { skillLevel: 'Expert', magickaCost: 20 } };
        var p2 = [
            { formId: 'root', x: 0, y: 100, isRoot: true, parentFormId: null, gridIdx: 0, skillLevel: 'Novice' },
            { formId: 'nov', x: 0, y: 220, parentFormId: 'root', gridIdx: 1, skillLevel: 'Novice' },
            { formId: 'exp', x: 30, y: 170, parentFormId: 'root', gridIdx: 2, skillLevel: 'Expert' }
        ];
        O.apply(p2, spellOf(t2), S);
        this.check(this._r(p2[2]) > this._r(p2[1]), 'the Expert ends past the Novice, though the Novice costs more');

        // ----- a spell and its own child trade places -----
        var t5 = { root: { skillLevel: 'Novice' }, par: { skillLevel: 'Adept', magickaCost: 10 },
                   kid: { skillLevel: 'Adept', magickaCost: 300 } };
        var p5 = [
            { formId: 'root', x: 0, y: 100, isRoot: true, parentFormId: null, gridIdx: 0, skillLevel: 'Novice' },
            { formId: 'par', x: 0, y: 220, parentFormId: 'root', gridIdx: 1, skillLevel: 'Adept' },
            { formId: 'kid', x: 30, y: 170, parentFormId: 'par', gridIdx: 2, skillLevel: 'Adept' }
        ];
        O.apply(p5, spellOf(t5), S);
        this.check(this._r(p5[2]) > this._r(p5[1]), 'a dearer child trades with its cheaper parent (their own line does not count)');

        // ----- tier boundary: the dearest Expert stays inside the cheapest Master -----
        var t6 = { root: { skillLevel: 'Novice' }, exp: { skillLevel: 'Expert', magickaCost: 900 },
                   mas: { skillLevel: 'Master', magickaCost: 10 }, exp2: { skillLevel: 'Expert', magickaCost: 5 } };
        var p6 = [
            { formId: 'root', x: 0, y: 100, isRoot: true, parentFormId: null, gridIdx: 0, skillLevel: 'Novice' },
            { formId: 'exp', x: 0, y: 230, parentFormId: 'root', gridIdx: 1, skillLevel: 'Expert' },
            { formId: 'mas', x: 30, y: 180, parentFormId: 'root', gridIdx: 2, skillLevel: 'Master' },
            { formId: 'exp2', x: -300, y: 300, parentFormId: 'root', gridIdx: 3, skillLevel: 'Expert' }
        ];
        O.apply(p6, spellOf(t6), S);
        this.check(this._r(p6[2]) > this._r(p6[1]), 'a cheap Master ends past a dear Expert');

        // ----- a trade that would stretch the lines: not made -----
        var p3 = [
            { formId: 'root', x: 0, y: 100, isRoot: true, parentFormId: null, gridIdx: 0, skillLevel: 'Novice' },
            { formId: 'cheap', x: 0, y: 300, parentFormId: 'root', gridIdx: 1, skillLevel: 'Adept' },
            { formId: 'dear', x: 200, y: 200, parentFormId: 'far', gridIdx: 2, skillLevel: 'Adept' },
            { formId: 'far', x: 400, y: 400, parentFormId: 'root', gridIdx: 3, skillLevel: 'Novice' }
        ];
        var t3 = { cheap: t1.cheap, dear: t1.dear, root: t1.root, far: { skillLevel: 'Novice', magickaCost: 1 } };
        var before3 = JSON.stringify(p3.slice(1, 3));
        O.apply(p3, spellOf(t3), S);
        this.check(JSON.stringify(p3.slice(1, 3)) === before3, 'no trade when the lines would grow by more than ALLOW');

        // ----- already in order: untouched; positions stay distinct -----
        var p4 = [], t4 = {}, levels = ['Novice', 'Apprentice', 'Adept', 'Expert', 'Master'];
        p4.push({ formId: 'r', x: 0, y: 40, isRoot: true, parentFormId: null, gridIdx: 0, skillLevel: 'Novice' });
        t4.r = { skillLevel: 'Novice', magickaCost: 1 };
        for (var i = 1; i <= 10; i++) {
            var id = 's' + i, lvl = levels[Math.floor((i - 1) / 2)];
            p4.push({ formId: id, x: (i % 2) * 20, y: 40 + i * 40, parentFormId: i === 1 ? 'r' : 's' + (i - 1), gridIdx: i, skillLevel: lvl });
            t4[id] = { skillLevel: lvl, magickaCost: 10 * i };
        }
        var n4 = O.apply(p4, spellOf(t4), S);
        this.check(n4 === 0, 'a chain already in order is left alone');

        var mixed = p4.map(function(n) { return Object.assign({}, n); });
        mixed.reverse();
        mixed.forEach(function(n, k) { n.gridIdx = k; });
        O.apply(mixed, spellOf(t4), S);
        var spots = {};
        var distinct = mixed.every(function(n) { var k = n.x + ',' + n.y; if (spots[k]) return false; spots[k] = 1; return true; });
        this.check(distinct, 'every spell keeps its own grid point');

        // A tree by Magicka cost: the layout reads each spell's level as its tree tier
        var byLevel = { config: { tier_mode: 'level' }, schools: { D: { nodes: [{ formId: 'a', tier: 3 }] } } };
        this.check(O.treeLevels(byLevel) === null && O.treeLevels({ schools: {} }) === null,
            'a tree by level keeps the spells own levels');
        var byCost = { config: { tier_mode: 'cost' }, schools: {
            D: { nodes: [{ formId: 'm', tier: 1 }, { formId: 'n', tier: 5 }, { formId: 'o', tier: 9 }] } } };
        var lv = O.treeLevels(byCost);
        this.check(lv && lv.m === 'Novice' && lv.n === 'Master' && lv.o === 'Master',
            'a tree by cost: each spell at the level of its tree tier (past Master stays Master)');
        var scan = { m: { formId: 'm', skillLevel: 'Master', magickaCost: 5 }, n: { formId: 'n', skillLevel: 'Novice', magickaCost: 90 },
            x: { formId: 'x', skillLevel: 'Adept', magickaCost: 40 } };
        var map = O.withLevels(scan, lv);
        this.check(map.m.skillLevel === 'Novice' && map.n.skillLevel === 'Master' && map.m.magickaCost === 5 && map.x === scan.x,
            'withLevels: copies carry the tree level, other spells are the same records');
        this.check(scan.m.skillLevel === 'Master' && scan.n.skillLevel === 'Novice', 'withLevels leaves the scan records alone');
        this.check(O.withLevels(scan, null) === scan, 'withLevels with no levels is the map itself');
        this.check(O.key(map.m.skillLevel, 0.5) < O.key(map.n.skillLevel, 0.5),
            'the cheap Master in a cost tree keys inside the dear Novice');

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.ClassicRadialOrderTest = ClassicRadialOrderTest;
if (typeof global !== 'undefined') global.ClassicRadialOrderTest = ClassicRadialOrderTest;
