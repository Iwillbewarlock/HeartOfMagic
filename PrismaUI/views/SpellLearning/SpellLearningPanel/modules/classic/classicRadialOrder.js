/**
 * ClassicRadialOrder - after the Classic layout has placed a school, nearby
 * spells trade places so that a higher tier, and within a tier a spell that
 * costs more Magicka, sits farther from the heart.
 *
 * Why after, not in the slot scoring: the layout grows the tree outward from
 * the root, each spell next to its parent, so how far out a spell lands is
 * mostly how many links below the root it is. Aiming each spell's slot by its
 * cost (tried first, 2026-10-02) moved nothing measurable. Trading places
 * between neighbours does, and keeps the spells on the same grid points.
 *
 * The order: key = tier index + cost rank * COST_SPAN, where the cost rank is
 * the spell's place among the school's spells of its tier by Magicka cost,
 * 0..1 (rank, not the number: mods put costs ten times the game's; ties share
 * a rank; no cost ranks lowest; a tier of one is 0.5). COST_SPAN < 1, so tier
 * decides first and cost within a tier.
 *
 * A trade: two spells of the school within REACH tier spacings of each other,
 * whose keys and distances from the heart disagree, swap positions when the
 * lines at the two spells together get no more than ALLOW tier spacings
 * longer. Each spell takes its best trade (the largest gain: key gap times
 * distance gap, less the line growth), over at most PASSES passes. Roots stay.
 * Measured on a 1,381-spell tree, five schools (2026-10-02, docs/
 * TREE_BUILDING_SYSTEM.md "Radial order"): cost/distance rank correlation in a
 * tier ~0 -> 0.18-0.35, tier/distance 0.77-0.83 -> 0.83-0.89, the lines shorter and crossing
 * less; ~45 ms for all five, once per build.
 *
 * A tree built with tiers by Magicka cost (treeData.config.tier_mode "cost",
 * 2026-10-04) puts a spell in the tier of its cost rank, not its level. The
 * layout then reads each spell's level as the tree's tier (treeLevels,
 * withLevels), so its tier zones and the cost ranks here go by the tier the
 * spell is in; the cost rank within such a tier still orders it.
 *
 * Depends on: nothing (ClassicLayout.layoutAllSchools calls apply)
 */

var ClassicRadialOrder = {

    TIERS: ['Novice', 'Apprentice', 'Adept', 'Expert', 'Master'],
    COST_SPAN: 0.9,     // how much of a tier step the cost rank may add
    REACH: 6,           // trade partners within this many tier spacings
    ALLOW: 1,           // a trade may lengthen the lines by this many tier spacings
    PASSES: 16,

    /**
     * The levels the layout places a tree's spells by: null for a tree by level
     * (each spell's own skillLevel), else formId -> the level name of its tree
     * tier (node.tier, 1-based) for a tree by Magicka cost.
     * @param {Object} treeData - the C++ build result (config, schools)
     * @returns {Object|null}
     */
    treeLevels: function(treeData) {
        if (!treeData || !treeData.config || treeData.config.tier_mode !== 'cost' || !treeData.schools) return null;
        var levels = {};
        for (var school in treeData.schools) {
            if (!treeData.schools.hasOwnProperty(school)) continue;
            var nodes = treeData.schools[school].nodes || [];
            for (var i = 0; i < nodes.length; i++) {
                var t = Math.max(1, Math.min(this.TIERS.length, Number(nodes[i].tier) || 1));
                levels[nodes[i].formId] = this.TIERS[t - 1];
            }
        }
        return levels;
    },

    /**
     * A spell map whose spells carry the given levels: each spell in levels is
     * a copy with skillLevel replaced (the scan's own records stay as they are).
     * @param {Object} spellMap - formId -> scan spell
     * @param {Object|null} levels - from treeLevels
     * @returns {Object} spellMap itself when levels is null
     */
    withLevels: function(spellMap, levels) {
        if (!levels) return spellMap;
        var out = {};
        for (var id in spellMap) {
            if (!spellMap.hasOwnProperty(id)) continue;
            var sp = spellMap[id];
            if (!levels.hasOwnProperty(id) || !sp) { out[id] = sp; continue; }
            var copy = {};
            for (var k in sp) { if (sp.hasOwnProperty(k)) copy[k] = sp[k]; }
            copy.skillLevel = levels[id];
            out[id] = copy;
        }
        return out;
    },

    /**
     * Magicka cost rank of every spell within its tier.
     * @param {Array<string>} formIds
     * @param {function(string): Object|null} spellOf - formId -> scan spell (skillLevel, magickaCost)
     * @returns {Object} formId -> rank 0..1
     */
    costRanks: function(formIds, spellOf) {
        var byTier = {};
        for (var i = 0; i < formIds.length; i++) {
            var sp = spellOf(formIds[i]);
            var level = (sp && sp.skillLevel) || '';
            var cost = sp ? Number(sp.magickaCost) : 0;
            if (!(cost > 0)) cost = 0;
            (byTier[level] = byTier[level] || []).push({ id: formIds[i], cost: cost });
        }
        var out = {};
        for (var lv in byTier) {
            if (!byTier.hasOwnProperty(lv)) continue;
            var list = byTier[lv];
            if (list.length === 1) { out[list[0].id] = 0.5; continue; }
            list.sort(function(a, b) { return a.cost - b.cost; });
            for (var s = 0; s < list.length; ) {
                var e = s;
                while (e + 1 < list.length && list[e + 1].cost === list[s].cost) e++;
                var r = ((s + e) / 2) / (list.length - 1);   // ties: the middle of their ranks
                for (var k = s; k <= e; k++) out[list[k].id] = r;
                s = e + 1;
            }
        }
        return out;
    },

    /** The order key of a spell, or null when its tier is unknown (it does not trade). */
    key: function(skillLevel, rank) {
        var t = this.TIERS.indexOf(skillLevel);
        if (t < 0) return null;
        return t + (typeof rank === 'number' ? rank : 0.5) * this.COST_SPAN;
    },

    /**
     * Trade places in one school's layout.
     * @param {Array<Object>} placed - layout nodes {formId, x, y, gridIdx, isRoot, skillLevel,
     *        parentFormId}; changed in place. The lines are parentFormId's, the ones the saved
     *        tree gets (ClassicTreeOutput)
     * @param {function(string): Object|null} spellOf - formId -> scan spell
     * @param {number} spacing - tier spacing (world units)
     * @returns {number} trades made
     */
    apply: function(placed, spellOf, spacing) {
        if (!placed || placed.length < 2 || !(spacing > 0)) return 0;
        var self = this;
        var byId = {}, i, j;
        for (i = 0; i < placed.length; i++) byId[placed[i].formId] = placed[i];

        // Lines at each spell (both ends placed)
        var links = {};
        for (i = 0; i < placed.length; i++) links[placed[i].formId] = [];
        for (i = 0; i < placed.length; i++) {
            var up = byId[placed[i].parentFormId];
            if (!up || up === placed[i]) continue;
            links[placed[i].formId].push(up);
            links[up.formId].push(placed[i]);
        }

        // One tier for both the rank and the key: the scan's, else the layout's
        var info = {};
        for (i = 0; i < placed.length; i++) {
            var sp = spellOf(placed[i].formId) || {};
            info[placed[i].formId] = { skillLevel: sp.skillLevel || placed[i].skillLevel || '', magickaCost: sp.magickaCost };
        }
        var ranks = this.costRanks(placed.map(function(n) { return n.formId; }), function(id) { return info[id]; });
        var keys = {};
        for (i = 0; i < placed.length; i++) {
            keys[placed[i].formId] = this.key(info[placed[i].formId].skillLevel, ranks[placed[i].formId]);
        }

        // Length of a spell's lines if it stood at (x, y), the line to `other` left out
        var lineLen = function(n, x, y, other) {
            var list = links[n.formId], sum = 0;
            for (var q = 0; q < list.length; q++) {
                if (list[q] === other) continue;
                var dx = list[q].x - x, dy = list[q].y - y;
                sum += Math.sqrt(dx * dx + dy * dy);
            }
            return sum;
        };

        var reach = this.REACH * spacing, allow = this.ALLOW * spacing;
        var total = 0;
        for (var pass = 0; pass < this.PASSES; pass++) {
            var cells = {};
            for (i = 0; i < placed.length; i++) {
                var cn = placed[i];
                var ck = Math.floor(cn.x / reach) + ',' + Math.floor(cn.y / reach);
                (cells[ck] = cells[ck] || []).push(cn);
            }
            var trades = 0;
            for (i = 0; i < placed.length; i++) {
                var a = placed[i], ka = keys[a.formId];
                if (a.isRoot || ka === null) continue;
                var ra = Math.sqrt(a.x * a.x + a.y * a.y);
                var cx = Math.floor(a.x / reach), cy = Math.floor(a.y / reach);
                var best = null, bestGain = 0;
                for (var dx = -1; dx <= 1; dx++) {
                    for (var dy = -1; dy <= 1; dy++) {
                        var cell = cells[(cx + dx) + ',' + (cy + dy)];
                        if (!cell) continue;
                        for (j = 0; j < cell.length; j++) {
                            var b = cell[j], kb = keys[b.formId];
                            if (b === a || b.isRoot || kb === null || kb === ka) continue;
                            var ddx = b.x - a.x, ddy = b.y - a.y;
                            if (ddx * ddx + ddy * ddy > reach * reach) continue;
                            var rb = Math.sqrt(b.x * b.x + b.y * b.y);
                            if ((ka - kb) * (ra - rb) >= 0) continue;   // already in order
                            var delta = lineLen(a, b.x, b.y, b) + lineLen(b, a.x, a.y, a) -
                                        lineLen(a, a.x, a.y, b) - lineLen(b, b.x, b.y, a);
                            if (delta > allow) continue;
                            var gain = (Math.abs(ka - kb) * Math.abs(ra - rb) - delta) / spacing;
                            if (gain > bestGain) { bestGain = gain; best = b; }
                        }
                    }
                }
                if (best) {
                    self._swap(a, best);
                    trades++;
                }
            }
            total += trades;
            if (!trades) break;
        }
        return total;
    },

    _swap: function(a, b) {
        var x = a.x, y = a.y, g = a.gridIdx;
        a.x = b.x; a.y = b.y; a.gridIdx = b.gridIdx;
        b.x = x; b.y = y; b.gridIdx = g;
    }
};

if (typeof window !== 'undefined') window.ClassicRadialOrder = ClassicRadialOrder;
