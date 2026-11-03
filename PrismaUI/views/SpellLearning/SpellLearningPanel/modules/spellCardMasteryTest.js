/**
 * SpellCardMasteryTest - node tests for the mastery ranks on the panel (run-tests.js):
 *  - SpellCardMastery.view(): no row for a spell still being learned, ranks switched off or no data from C++ (an
 *    older DLL); a mastered spell with no mastery XP yet at rank 0 of the configured ranks, an empty bar; a rank and
 *    its share of the way to the next; a full bar at the last rank; a rank past the ranks held at the last;
 *  - draw(): the row shown with "Mastery rank 1/3" (keyed, so a language switch writes it again) and the bar's width,
 *    hidden for a spell being learned;
 *  - ProgressUpdates.takeMastery(): an update's mastery part stored; a masteryOnly update (a spell with no learning
 *    progress) taken whole there, so it never becomes a 0 XP learning entry;
 *  - MasterySetting: on by default, loaded from mastery.enabled (anything not a boolean is on), saved as
 *    mastery.enabled only (the hand-written numbers stay), Reset to Defaults on;
 *  - markup and languages: the card row and the setting's switch in index.html with their scripts, the keys in en
 *    and ko.
 *
 * Depends on: spellCardMastery.js, progressUpdates.js, masterySetting.js, each run in a sandbox of its own with a fake
 * document (PanelLayoutTest's helpers).
 */

var SpellCardMasteryTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _file: function(rel) {
        return require('fs').readFileSync(require('path').join(__dirname, rel), 'utf8');
    },

    _layout: function() {
        return global.PanelLayoutTest || (require('./panelLayoutTest.js'), global.PanelLayoutTest);
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var parts = [['view', this._view], ['draw', this._draw], ['updates', this._updates], ['setting', this._setting],
            ['markup', this._markup]];
        for (var i = 0; i < parts.length; i++) {
            try { parts[i][1].call(this); } catch (e) { this.check(false, parts[i][0] + ' threw: ' + (e && e.stack ? e.stack : e)); }
        }
        return { passed: this.passed, failed: this.failed };
    },

    _view: function() {
        var M = this._layout()._sandbox(['spellCardMastery.js']).SpellCardMastery;
        var info = { enabled: true, ranks: 3 };
        var entry = { xp: 150, rank: 1, ranks: 3, floor: 100, next: 200 };
        this.check(M.view('learning', entry, info) === null && M.view('available', null, info) === null,
            'View: no row for a spell still being learned');
        this.check(M.view('unlocked', entry, null) === null, 'View: no row without mastery data from C++ (an older DLL)');
        this.check(M.view('unlocked', entry, { enabled: false, ranks: 3 }) === null, 'View: no row while the ranks are off');
        var none = M.view('unlocked', null, info);
        this.check(none && none.rank === 0 && none.ranks === 3 && none.pct === 0,
            'View: a mastered spell with no mastery XP yet is rank 0 of 3, an empty bar');
        var half = M.view('unlocked', entry, info);
        this.check(half && half.rank === 1 && half.ranks === 3 && half.pct === 50, 'View: 150 between 100 and 200 is rank 1/3, half the bar');
        var last = M.view('unlocked', { xp: 300, rank: 3, ranks: 3, floor: 300, next: 300 }, info);
        this.check(last && last.rank === 3 && last.pct === 100, 'View: the last rank shows a full bar');
        var past = M.view('unlocked', { xp: 900, rank: 5, ranks: 3, floor: 300, next: 300 }, info);
        this.check(past && past.rank === 3 && past.pct === 100, 'View: a rank past the ranks is held at the last');
        var five = M.view('unlocked', { xp: 10, rank: 0, ranks: 5, floor: 0, next: 100 }, info);
        this.check(five && five.ranks === 5 && five.pct === 10, 'View: the entry\'s own ranks win over the reply\'s');
        var odd = M.view('unlocked', { xp: 'x', rank: 1, ranks: 3, floor: 100, next: 200 }, info);
        this.check(odd && odd.rank === 1 && odd.pct === 0, 'View: an XP that is not a number draws an empty bar, not NaN');
    },

    _draw: function() {
        var L = this._layout();
        var row = L.el('div', { id: 'spell-mastery', 'class': 'spell-mastery hidden' });
        var text = L.el('span', { id: 'spell-mastery-text' });
        var fill = L.el('div', { id: 'spell-mastery-fill' });
        var doc = L.doc([row, text, fill]);
        var box = L._sandbox(['spellCardMastery.js'], {
            state: { spellMastery: { '0x00012FCD': { xp: 150, rank: 1, ranks: 3, floor: 100, next: 200 } }, masteryInfo: { enabled: true, ranks: 3 } },
            getCanonicalFormId: function(n) { return n.formId; }
        });
        var M = box.SpellCardMastery;
        M._doc = function() { return doc; };
        M.draw({ formId: '0x00012FCD', state: 'unlocked' });
        this.check(!row.classList.contains('hidden') && text.textContent === 'Mastery rank 1/3' && fill.style.width === '50%',
            'Draw: a mastered spell shows "Mastery rank 1/3" and a half bar');
        this.check(text.getAttribute('data-i18n') === 'details.masteryRank' &&
            text.getAttribute('data-i18n-params') === JSON.stringify({ rank: 1, ranks: 3 }),
            'Draw: the line is keyed with its values (a language switch writes it again)');
        M.draw({ formId: '0x00012FCD', state: 'learning' });
        this.check(row.classList.contains('hidden'), 'Draw: hidden for a spell being learned');
        box.state.masteryInfo = null;
        M.draw({ formId: '0x00012FCD', state: 'unlocked' });
        this.check(row.classList.contains('hidden'), 'Draw: hidden when C++ sent no mastery data');
    },

    _updates: function() {
        var drawn = [];
        var box = this._layout()._sandbox(['progressUpdates.js'], {
            state: { spellProgress: {}, spellMastery: {}, selectedNode: { formId: '0xFE000801', state: 'unlocked' } },
            settings: {},
            getCanonicalFormId: function(n) { return n.formId; },
            SpellCardMastery: { draw: function(n) { drawn.push(n.formId); } }
        });
        var U = box.ProgressUpdates;
        var mastery = { xp: 40, rank: 0, ranks: 3, floor: 0, next: 100 };
        var only = U.takeMastery('0xFE000801', { formId: '0xFE000801', currentXP: 0, requiredXP: 100, masteryOnly: true, mastery: mastery });
        this.check(only === true && box.state.spellMastery['0xFE000801'] === mastery && !box.state.spellProgress['0xFE000801'],
            'Updates: a masteryOnly update stores the mastery part and makes no 0 XP learning entry');
        this.check(drawn.length === 1 && drawn[0] === '0xFE000801', 'Updates: the open card\'s mastery row is redrawn');
        var both = U.takeMastery('0x00012FCD', { formId: '0x00012FCD', currentXP: 100, requiredXP: 100, masteryOnly: false,
            mastery: { xp: 120, rank: 1, ranks: 3, floor: 100, next: 200 } });
        this.check(both === false && box.state.spellMastery['0x00012FCD'].rank === 1,
            'Updates: a spell with learning progress keeps its mastery part and goes on to the progress part');
        var older = U.takeMastery('0x00012FD0', { formId: '0x00012FD0', currentXP: 5, requiredXP: 100 });
        this.check(older === false && !box.state.spellMastery['0x00012FD0'], 'Updates: an update without mastery fields (an older DLL) is as before');

        // B3: a rank C++ cleared goes from the panel too
        box.state.spellMastery['0x00012FCD'] = { xp: 120, rank: 1, ranks: 3, floor: 100, next: 200 };
        U.takeMastery('0x00012FCD', { formId: '0x00012FCD', currentXP: 5, requiredXP: 100 });
        this.check(box.state.spellMastery['0x00012FCD'] && box.state.spellMastery['0x00012FCD'].rank === 1,
            'Updates: an update from an older DLL (no masteryOnly) leaves the rank held');
        U.takeMastery('0x00012FCD', { formId: '0x00012FCD', currentXP: 40, requiredXP: 100, masteryOnly: false });
        this.check(!box.state.spellMastery.hasOwnProperty('0x00012FCD'), 'Updates: an update from this DLL with no mastery part drops the rank');
        box.state.spellMastery['0xFE000801'] = { xp: 40, rank: 0, ranks: 3, floor: 0, next: 100 };
        drawn.length = 0;
        var cleared = U.takeMastery('0xFE000801', { formId: '0xFE000801', currentXP: 0, requiredXP: 0, masteryOnly: true });
        this.check(cleared === true && !box.state.spellMastery.hasOwnProperty('0xFE000801') && drawn.length === 1,
            'Updates: a mastery-only clear (ClearMastery) drops the rank and redraws the open card');
        box.state.spellMastery['0x00012FCD'] = { xp: 120, rank: 1, ranks: 3, floor: 100, next: 200 };
        box.state.learningTargets = {};
        box.window.onSpellRelocked(JSON.stringify({ success: true, formId: '0x00012FCD' }));
        this.check(!box.state.spellMastery.hasOwnProperty('0x00012FCD'), 'Updates: a relocked spell loses its rank on the panel');
    },

    _setting: function() {
        var L = this._layout();
        var toggle = L.el('input', { id: 'masteryRanksToggle' });
        var doc = L.doc([toggle]);
        var box = L._sandbox(['masterySetting.js'], { settings: {}, state: { masteryInfo: { enabled: true, ranks: 3 } }, document: doc });
        var S = box.MasterySetting;
        S.loadFrom({});
        this.check(S.enabled() === true && toggle.checked === true, 'Setting: on by default');
        S.loadFrom({ mastery: { enabled: false, ranks: 5 } });
        this.check(S.enabled() === false && toggle.checked === false, 'Setting: mastery.enabled false read and shown');
        S.loadFrom({ mastery: { enabled: 'no' } });
        this.check(S.enabled() === true, 'Setting: a value that is not true or false is on');
        S.set(false);
        var config = { mastery: { ranks: 5, tierXP: { novice: 50 } } };
        S.saveTo(config);
        this.check(config.mastery.enabled === false && config.mastery.ranks === 5 && config.mastery.tierXP.novice === 50,
            'Setting: saved as mastery.enabled only, the hand-written numbers stay');
        this.check(box.state.masteryInfo.enabled === false, 'Setting: the card follows the switch at once');
        var fresh = {};
        S.saveTo(fresh);
        this.check(fresh.mastery && fresh.mastery.enabled === false, 'Setting: a config without a mastery section gets one');
        S.reset();
        this.check(S.enabled() === true && toggle.checked === true, 'Setting: Reset to Defaults turns it on');
    },

    _markup: function() {
        var html = this._file('../index.html');
        this.check(/id="spell-mastery"[^>]*class="spell-mastery hidden"/.test(html) && html.indexOf('id="spell-mastery-text"') !== -1 &&
            html.indexOf('id="spell-mastery-fill"') !== -1, 'Markup: the card\'s mastery row, hidden until drawn');
        this.check(html.indexOf('<script src="modules/spellCardMastery.js"></script>') !== -1 &&
            html.indexOf('<script src="modules/spellCardMastery.js"></script>') < html.indexOf('<script src="modules/spellCardNeeds.js"></script>') &&
            html.indexOf('<script src="modules/masterySetting.js"></script>') !== -1,
            'Markup: both scripts load, the row\'s before the card part that draws it');
        this.check(/id="masteryRanksToggle"/.test(html) && html.indexOf('data-i18n="settings.progression.masteryEnable"') !== -1,
            'Markup: the switch in Settings with its label');
        var keys = ['details.masteryRank', 'hud.masteryRankUp', 'settings.progression.masteryRanks',
            'settings.progression.masteryEnable', 'settings.progression.masteryEnableDesc'];
        var self = this;
        ['en', 'ko'].forEach(function(loc) {
            var lang = JSON.parse(self._file('../lang/' + loc + '.json'));
            var missing = keys.filter(function(k) { return typeof lang[k] !== 'string' || !lang[k]; });
            var placeholders = lang['details.masteryRank'] && lang['details.masteryRank'].indexOf('{{rank}}') !== -1 &&
                lang['details.masteryRank'].indexOf('{{ranks}}') !== -1 && lang['hud.masteryRankUp'].indexOf('{{spell}}') !== -1;
            self.check(missing.length === 0 && placeholders, 'Languages: ' + loc + ' has the mastery keys with their placeholders' +
                (missing.length ? ' (missing ' + missing.join(', ') + ')' : ''));
        });
    }
};

global.SpellCardMasteryTest = SpellCardMasteryTest;
