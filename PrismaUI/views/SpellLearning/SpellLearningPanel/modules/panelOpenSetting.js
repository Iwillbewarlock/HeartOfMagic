/**
 * PanelOpenSetting - Settings > Hotkey: how the panel is opened.
 *
 * - The panel toggle key (Change / Reset / None). None is hotkeyCode 0 and
 *   hotkey "": no DirectInput key has scancode 0, so C++ InputHandler
 *   (Main.cpp, idCode != m_hotkeyCode) never matches it. None is decided by
 *   the code alone - a hand-edited {hotkeyCode: 0, hotkey: "F8"} is None too.
 * - A letter key (A-Z) gets a warning: typing that letter in a search box
 *   reaches the game too and closes the panel. It is allowed.
 * - Magic Menu Bookmark (magicMenu.bookmark) and Tags and XP in the Magic Menu
 *   (magicMenu.spellInfo), both on by default. C++ reads them into
 *   UIManager::MagicMenuBookmark() / MagicMenuSpellInfo() (UIManagerConfig.cpp).
 * - Guard: the bookmark is the other way to open the panel, so with no hotkey
 *   it cannot be turned off, and with it off None cannot be chosen. A config
 *   with both off (hand-edited) loads with the bookmark on - here and in C++.
 *
 * Saved as config.hotkey, config.hotkeyCode and config.magicMenu = { bookmark,
 * spellInfo }. Personal settings: a settings preset never carries them
 * (settingsPresets.js lists what a preset holds, and these are not in it).
 *
 * Depends on: settings, KEY_CODES (config.js), t / setI18n (i18n.js, optional),
 * autoSaveSettings (optional)
 */

var PanelOpenSetting = {

    DEFAULT_HOTKEY: 'F8',
    DEFAULT_HOTKEY_CODE: 66,   // DirectInput scancode of F8
    NONE_CODE: 0,
    MAGIC_MENU_DEFAULTS: { bookmark: true, spellInfo: true },

    /** settings.magicMenu, made with the defaults when missing. */
    _mm: function() {
        if (!settings.magicMenu || typeof settings.magicMenu !== 'object') settings.magicMenu = {};
        var m = settings.magicMenu;
        for (var k in this.MAGIC_MENU_DEFAULTS) {
            if (this.MAGIC_MENU_DEFAULTS.hasOwnProperty(k) && typeof m[k] !== 'boolean') m[k] = this.MAGIC_MENU_DEFAULTS[k];
        }
        return m;
    },

    isNone: function() { return settings.hotkeyCode === this.NONE_CODE; },
    isLetterKey: function(name) { return typeof name === 'string' && /^[A-Z]$/.test(name); },
    canChooseNone: function() { return this._mm().bookmark !== false; },
    canTurnBookmarkOff: function() { return !this.isNone(); },

    /** The hotkey of a config: {hotkey, hotkeyCode}; 0 stays 0 (None), anything not a key code is F8. */
    readHotkey: function(data) {
        var code = data ? data.hotkeyCode : undefined;
        if (typeof code !== 'number' || !isFinite(code) || code < 0 || Math.floor(code) !== code) {
            return { hotkey: this.DEFAULT_HOTKEY, hotkeyCode: this.DEFAULT_HOTKEY_CODE };
        }
        if (code === this.NONE_CODE) return { hotkey: '', hotkeyCode: this.NONE_CODE };
        var name = (data && typeof data.hotkey === 'string' && data.hotkey) ? data.hotkey : this.DEFAULT_HOTKEY;
        return { hotkey: name, hotkeyCode: code };
    },

    /** Set the hotkey (code 0 = None, refused while the bookmark is off). Returns whether it changed. */
    setHotkey: function(name, code) {
        if (code === this.NONE_CODE) {
            if (!this.canChooseNone()) return false;
            name = '';
        }
        settings.hotkey = name;
        settings.hotkeyCode = code;
        this.sync();
        this._save();
        return true;
    },

    /** Turn a magic menu switch on or off; the bookmark stays on while there is no hotkey. */
    setMagicMenu: function(key, on) {
        var m = this._mm();
        if (key === 'bookmark' && !on && !this.canTurnBookmarkOff()) {
            this.sync();
            return false;
        }
        m[key] = !!on;
        this.sync();
        this._save();
        return true;
    },

    init: function() {
        var self = this;
        var input = document.getElementById('hotkeyInput');
        var change = document.getElementById('changeHotkeyBtn');
        var reset = document.getElementById('resetHotkeyBtn');
        var none = document.getElementById('noHotkeyBtn');
        var bookmark = document.getElementById('magicMenuBookmarkToggle');
        var spellInfo = document.getElementById('magicMenuSpellInfoToggle');

        if (input && change) {
            change.addEventListener('click', function() {
                input.classList.add('listening');
                input.value = typeof t === 'function' ? t('settingsPanel.pressAKey') : 'Press a key...';

                function onKeyDown(e) {
                    e.preventDefault();
                    var keyName = e.key.toUpperCase();
                    var code = KEY_CODES[keyName] || KEY_CODES[e.key];
                    input.classList.remove('listening');
                    document.removeEventListener('keydown', onKeyDown);
                    if (code) {
                        self.setHotkey(keyName, code);
                        console.log('[SpellLearning] Hotkey changed to:', keyName, '(code:', code, ')');
                    } else {
                        console.log('[SpellLearning] Unsupported key:', e.key);
                        self.sync();
                    }
                }
                document.addEventListener('keydown', onKeyDown);
            });
        }
        if (reset) {
            reset.addEventListener('click', function() {
                if (input) input.classList.remove('listening');
                self.setHotkey(self.DEFAULT_HOTKEY, self.DEFAULT_HOTKEY_CODE);
            });
        }
        if (none) {
            none.addEventListener('click', function() {
                if (input) input.classList.remove('listening');
                self.setHotkey('', self.NONE_CODE);
            });
        }
        if (bookmark) bookmark.addEventListener('change', function() { self.setMagicMenu('bookmark', this.checked); });
        if (spellInfo) spellInfo.addEventListener('change', function() { self.setMagicMenu('spellInfo', this.checked); });
        this.sync();
    },

    /** Put the saved values on the controls: the key, the warning, what is locked and why. */
    sync: function() {
        var m = this._mm();
        var none = this.isNone();
        var input = document.getElementById('hotkeyInput');
        var noneBtn = document.getElementById('noHotkeyBtn');
        var bookmark = document.getElementById('magicMenuBookmarkToggle');
        var spellInfo = document.getElementById('magicMenuSpellInfoToggle');
        var bookmarkRow = document.getElementById('magicMenuBookmarkRow');

        if (input && !(input.classList && input.classList.contains && input.classList.contains('listening'))) {
            // None shows as the placeholder, which a language switch writes again
            input.value = none ? '' : settings.hotkey;
            if (typeof setI18n === 'function') setI18n(input, 'settings.hotkey.none', null, 'None', 'placeholder');
        }
        if (noneBtn) noneBtn.disabled = none || !this.canChooseNone();
        if (bookmark) bookmark.checked = m.bookmark !== false;
        if (spellInfo) spellInfo.checked = m.spellInfo !== false;
        // The switch stays clickable-looking only when it can be turned off
        if (bookmarkRow && bookmarkRow.classList) bookmarkRow.classList.toggle('disabled-controls', !this.canTurnBookmarkOff());
        if (bookmark) bookmark.disabled = !this.canTurnBookmarkOff();

        this._show('hotkeyLetterWarning', !none && this.isLetterKey(settings.hotkey));
        this._show('noHotkeyLockedReason', !none && !this.canChooseNone());
        this._show('magicMenuBookmarkLockedReason', !this.canTurnBookmarkOff());
    },

    /** Read the saved settings from a config; both ways to open the panel off turns the bookmark on. */
    loadFrom: function(data) {
        var hk = this.readHotkey(data);
        settings.hotkey = hk.hotkey;
        settings.hotkeyCode = hk.hotkeyCode;
        var src = (data && data.magicMenu && typeof data.magicMenu === 'object') ? data.magicMenu : {};
        settings.magicMenu = {
            bookmark: src.bookmark !== false,
            spellInfo: src.spellInfo !== false
        };
        if (this.isNone() && !settings.magicMenu.bookmark) {
            settings.magicMenu.bookmark = true;
            console.warn('[SpellLearning] No hotkey and the magic menu bookmark off: the bookmark is turned back on');
        }
        this.sync();
    },

    /** Write the settings into a config. */
    saveTo: function(config) {
        var m = this._mm();
        config.hotkey = this.isNone() ? '' : settings.hotkey;
        config.hotkeyCode = settings.hotkeyCode;
        config.magicMenu = { bookmark: m.bookmark !== false, spellInfo: m.spellInfo !== false };
        return config;
    },

    reset: function() {
        settings.hotkey = this.DEFAULT_HOTKEY;
        settings.hotkeyCode = this.DEFAULT_HOTKEY_CODE;
        settings.magicMenu = JSON.parse(JSON.stringify(this.MAGIC_MENU_DEFAULTS));
        var input = document.getElementById('hotkeyInput');
        if (input && input.classList) input.classList.remove('listening');
        this.sync();
    },

    _show: function(id, on) {
        var el = document.getElementById(id);
        if (el && el.classList) el.classList.toggle('hidden', !on);
    },

    _save: function() {
        if (typeof autoSaveSettings === 'function') autoSaveSettings();
    }
};

window.PanelOpenSetting = PanelOpenSetting;
