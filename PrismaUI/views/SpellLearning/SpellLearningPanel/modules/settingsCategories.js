/**
 * SettingsCategories - the Settings page one category at a time (2.0 review, item 6)
 *
 * Display, Learning, Spell Tomes and Advanced (developer and debug options: never mixed
 * with learning). A list at the page's left picks one (a row on top when the page is
 * narrower than NARROW_PX; the side-by-side blocks one under another below ONE_COLUMN_PX); only that category's page (.settings-cat-page) shows. Every
 * category keeps its own scroll position for the panel session: picked again, or the
 * Settings tab opened again, it comes back where it was left. Not saved between game
 * sessions (no config key): a new session opens on DEFAULT at the top.
 *
 * The Presets row (#settingsPresetsRow) drives the Learning and Spell Tomes settings, so it
 * moves to the top of whichever of those two is shown (PRESET_CATEGORIES).
 *
 * #contentSettings stays the one scroller (the sticky Save/Reset bar at its foot); the
 * list sticks to its top. Which page holds which setting: docs/DESIGN.md, "Settings
 * categories".
 *
 * Depends on: index.html (#contentSettings, .settings-cat-btn, .settings-cat-page,
 * #settingsPresetsRow). script.js switchTab calls remember() before leaving Settings and
 * onShow() when it opens; onResizeEnd calls layout().
 */
var SettingsCategories = {
    CATEGORIES: ['display', 'learning', 'tomes', 'advanced'],
    DEFAULT: 'display',
    PRESET_CATEGORIES: ['learning', 'tomes'],
    /** Narrower than this (px), the Settings page puts its category list on top as a row */
    NARROW_PX: 720,
    /** Narrower than this (px), the side-by-side settings blocks and pairs go one under another
     *  (the 900 px default panel: two columns of long German or Russian labels cut their rows) */
    ONE_COLUMN_PX: 1100,

    current: 'display',
    _scroll: {},
    _initialized: false,

    /** The page's document (a stand-in under node tests) */
    _doc: function() { return document; },

    _content: function() { return this._doc().getElementById('contentSettings'); },

    _each: function(selector, fn) {
        var list = this._doc().querySelectorAll(selector);
        for (var i = 0; i < list.length; i++) fn(list[i]);
    },

    /** @returns {boolean} a known category name */
    isCategory: function(cat) {
        return this.CATEGORIES.indexOf(cat) !== -1;
    },

    init: function() {
        if (this._initialized) return;
        this._initialized = true;
        var self = this;
        this._each('.settings-cat-btn', function(btn) {
            btn.addEventListener('click', function() {
                self.select(btn.getAttribute('data-settings-cat'));
            });
        });
        this.show(this.current);
        this.layout();
    },

    /**
     * Show one category's page and mark its button. No scroll change: select() and
     * onShow() put the remembered position back.
     * @param {string} cat
     * @returns {boolean} false for an unknown category (nothing changes)
     */
    show: function(cat) {
        if (!this.isCategory(cat)) return false;
        this.current = cat;
        this._each('.settings-cat-btn', function(btn) {
            var on = btn.getAttribute('data-settings-cat') === cat;
            btn.classList.toggle('active', on);
            btn.setAttribute('aria-selected', on ? 'true' : 'false');
        });
        this._each('.settings-cat-page', function(page) {
            page.classList.toggle('active', page.getAttribute('data-settings-cat') === cat);
        });
        this._placePresets(cat);
        return true;
    },

    /** A category picked from the list: the old one's scroll kept, the new one's put back */
    select: function(cat) {
        if (!this.isCategory(cat)) return;
        if (cat === this.current) return;
        this.remember();
        this.show(cat);
        this._restore();
        // An armed Reset to Defaults does not stay armed behind another page (resetConfirm.js)
        if (typeof ResetConfirm !== 'undefined') ResetConfirm.disarm();
    },

    /** Keep the shown category's scroll position (the page is still showing) */
    remember: function() {
        var content = this._content();
        if (!content || !content.classList.contains('active')) return;
        this._scroll[this.current] = content.scrollTop || 0;
    },

    /** The Settings tab opened: the list's layout for its width, the category where it was left */
    onShow: function() {
        this.layout();
        this._restore();
    },

    /** The list as a column at the left, or as a row on top when the page is narrow; the
     *  blocks in one column below ONE_COLUMN_PX */
    layout: function() {
        var content = this._content();
        if (!content || !content.classList.contains('active')) return;
        var width = content.clientWidth || 0;
        content.classList.toggle('settings-narrow', width > 0 && width < this.NARROW_PX);
        content.classList.toggle('settings-onecol', width > 0 && width < this.ONE_COLUMN_PX);
    },

    _restore: function() {
        var content = this._content();
        if (!content) return;
        content.scrollTop = this._scroll[this.current] || 0;
    },

    /** The Presets row at the top of the Learning or Spell Tomes page, whichever shows */
    _placePresets: function(cat) {
        if (this.PRESET_CATEGORIES.indexOf(cat) === -1) return;
        var doc = this._doc();
        var row = doc.getElementById('settingsPresetsRow');
        var page = doc.querySelector('.settings-cat-page[data-settings-cat="' + cat + '"]');
        if (!row || !page || row.parentNode === page) return;
        page.insertBefore(row, page.firstChild);
    }
};

window.SettingsCategories = SettingsCategories;
