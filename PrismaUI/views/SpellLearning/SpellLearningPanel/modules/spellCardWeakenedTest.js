/**
 * SpellCardWeakenedTest - node tests for the spell card's "Weakened (N%)" badge
 * (spellWeakenedPower in spellCardView.js, run-tests.js).
 *
 * Checks: a spell is weakened only when the known spells list it as early-learned
 * (state.weakenedSpells), by its own or its canonical id, never because the spell
 * info read once a session said so (a spell mastered since keeps no badge); the
 * power follows its XP by the power steps (calculateCurrentEffectiveness), the
 * spell info's figure only without XP; no badge for a spell mastered since (node
 * unlocked, progress unlocked - the cheat Unlock's shape too - or full power), and
 * the mastery paths drop it from the list; the badge shows for the 'available' state
 * the known spells keep a weakened spell in; and TreeParser.updateNodeFromCache
 * carries the spell info's effectiveness and scaled effects onto the node.
 *
 * Depends on: spellCardView.js, progressionUI.js, treeParser.js, progressUpdates.js - run in their
 * own sandbox with stand-in `state`, `settings` and `SpellCache`.
 */

var SpellCardWeakenedTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _sandbox: function() {
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var quiet = function() {};
        var box = {
            window: {},
            console: { log: quiet, warn: quiet, error: quiet },
            state: { weakenedSpells: new Set(), spellProgress: {} },
            settings: { earlySpellLearning: { enabled: true, powerSteps: [
                { xp: 25, power: 20 }, { xp: 50, power: 40 }, { xp: 75, power: 60 }
            ] } },
            SpellCache: { data: {}, get: function(id) { return this.data[id]; } },
            getCanonicalFormId: function(node) {
                var i = node.formId.indexOf('_dup_');
                return i === -1 ? node.formId : node.formId.substring(0, i);
            }
        };
        vm.createContext(box);
        ['progressionUI.js', 'spellCardView.js', 'treeParser.js', 'progressUpdates.js'].forEach(function(name) {
            var file = path.join(__dirname, name);
            vm.runInContext(fs.readFileSync(file, 'utf8'), box, { filename: file });
        });
        return box;
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var box;
        try { box = this._sandbox(); } catch (e) {
            this.check(false, 'spellCardView.js, progressionUI.js and treeParser.js loaded: ' + e.message);
            return { passed: this.passed, failed: this.failed };
        }
        var power = box.spellWeakenedPower;
        var flames = { formId: '0x00012FCD', state: 'unlocked', effectiveness: 65 };

        this.check(power(flames) === null, 'not weakened while the known spells list none, a stale effectiveness 65 aside');
        box.state.weakenedSpells = new Set(['0x0002B96B']);
        this.check(power(flames) === null, 'not weakened when the known spells list another spell');

        box.state.weakenedSpells = new Set(['0x00012FCD']);
        flames.state = 'available';
        this.check(power(flames) === 65, 'weakened, no XP yet: the spell info\'s power');
        box.state.spellProgress['0x00012FCD'] = { xp: 300, required: 500, progress: 0.6 };
        this.check(power(flames) === 40, 'weakened at 60% XP: the power step reached (40%)');
        box.state.spellProgress['0x00012FCD'] = { xp: 400, required: 500 };
        this.check(power(flames) === 60, 'XP without a progress share: xp / required (80% -> 60%)');
        this.check(power({ formId: '0x00012FCD_dup_1', state: 'available' }) === 60, 'a duplicate node by its canonical id');
        delete box.state.spellProgress['0x00012FCD'];
        this.check(power({ formId: '0x00012FCD', state: 'available' }) === null, 'no XP and no spell info: full power, no badge (not NaN)');

        // Mastered since the list came: no badge, though the spell is still listed until the next open
        box.state.spellProgress['0x00012FCD'] = { xp: 500, required: 500, progress: 1, unlocked: true };
        this.check(power(flames) === null, 'still listed but mastered (unlocked, progress 1): no badge');
        box.state.spellProgress['0x00012FCD'] = { xp: 100, required: 100, unlocked: true, ready: true };
        this.check(power(flames) === null, 'still listed after the cheat Unlock ({xp:100, required:100, unlocked:true}): no badge');
        box.state.spellProgress['0x00012FCD'] = { xp: 300, required: 500, progress: 0.6, unlocked: true };
        this.check(power(flames) === null, 'mastered with a stale progress share (onSpellUnlocked): no badge, not 40%');
        box.state.spellProgress['0x00012FCD'] = { xp: 300, required: 500, progress: 0.6 };
        this.check(power({ formId: '0x00012FCD', state: 'unlocked' }) === null, 'a node shown unlocked: no badge');
        box.state.spellProgress['0x00012FCD'] = { xp: 500, required: 500, progress: 1 };
        this.check(power(flames) === null, 'XP full before C++ marks it mastered: 100% is no weakening');
        box.state.spellProgress['0x00012FCD'] = { xp: 100, required: 500, progress: 0.2 };
        this.check(power(flames) === 20, 'at the first step: its power (20%)');

        // The mastery paths take the spell out of the list (ProgressUpdates.forgetWeakened)
        box.isSpellMastered = function() { return true; };
        box.ProgressUpdates.handle({ formId: '0x00012FCD', currentXP: 500, requiredXP: 500, unlocked: true });
        this.check(!box.state.weakenedSpells.has('0x00012FCD'), 'a progress update that masters the spell drops it from the list');
        box.state.weakenedSpells = new Set(['0x00012FCD', '0x00012FCD_dup_1']);
        box.ProgressUpdates.forgetWeakened('0x00012FCD', '0x00012FCD_dup_1');
        this.check(box.state.weakenedSpells.size === 0, 'forgetWeakened drops the canonical id and the formId');
        box.state.weakenedSpells = undefined;
        var threw = false;
        try { box.ProgressUpdates.forgetWeakened('0x00012FCD'); } catch (e) { threw = true; }
        this.check(!threw, 'forgetWeakened with no list yet: nothing to do');
        delete box.state.spellProgress['0x00012FCD'];
        this.check(power(flames) === null, 'no known spells sent yet: not weakened');

        // The badge: weakened spells sit in 'available' (onPlayerKnownSpells never leaves them 'unlocked')
        var src = require('fs').readFileSync(require('path').join(__dirname, 'spellCardView.js'), 'utf8');
        this.check(/if \(isWeakened\) \{\s*stateText = tOr\('details\.stateWeakened'/.test(src),
            'the Weakened badge does not wait for an \'unlocked\' state');
        this.check(!/node\.isWeakened|node\.effectiveness\s*</.test(src),
            'the card takes weakened from the known spells, not from the spell info');
        this.check(!/\.style\.color\s*=/.test(src), 'no inline colours on the card (the design sets --weakened-color)');
        this.check(/Math\.floor\(full \* effectiveness \/ 100\)/.test(src),
            'weakened magnitudes worked out at the badge power, not the session-old scaledMagnitude');
        var ui = require('fs').readFileSync(require('path').join(__dirname, 'progressionUI.js'), 'utf8');
        this.check((ui.match(/ProgressUpdates\.forgetWeakened\(/g) || []).length === 2,
            'the cheat Unlock and onSpellUnlocked drop the spell from the list');

        // The spell info's figures land on the node
        box.SpellCache.data['0x00012FCD'] = { name: 'Flames', effectiveness: 40,
            scaledEffects: [{ name: 'Fire Damage', originalMagnitude: 8, scaledMagnitude: 3, duration: 0 }] };
        var node = { formId: '0x00012FCD', state: 'available' };
        box.TreeParser.updateNodeFromCache(node);
        this.check(node.effectiveness === 40 && node.scaledEffects && node.scaledEffects[0].scaledMagnitude === 3,
            'updateNodeFromCache carries effectiveness and scaledEffects');
        this.check(node.isWeakened === undefined, 'updateNodeFromCache sets no weakened flag');
        box.SpellCache.data['0x00012FCD'] = { name: 'Flames' };
        box.TreeParser.updateNodeFromCache(node);
        this.check(node.effectiveness === 100 && node.scaledEffects === null, 'info without them: 100 and no scaled effects');
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.SpellCardWeakenedTest = SpellCardWeakenedTest;
if (typeof global !== 'undefined') global.SpellCardWeakenedTest = SpellCardWeakenedTest;
