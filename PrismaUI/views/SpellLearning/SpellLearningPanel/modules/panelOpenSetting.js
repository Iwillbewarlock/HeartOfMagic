/**
 * PanelOpenSetting - Settings > Hotkey: how the panel is opened.
 *
 * - The panel toggle key (Change / Reset / None). None is hotkeyCode 0 and
 *   hotkey "": no DirectInput key has scancode 0, so C++ InputHandler
 *   (Main.cpp, idCode != m_hotkeyCode) never matches it. None is decided by
 *   the code alone - a hand-edited {hotkeyCode: 0, hotkey: "F8"} is None too.
 * - A letter key (A-Z) gets a warning: typing that letter in a search box
 *   reaches the game too and closes the panel. It is allowed.
 *
 * Saved as config.hotkey and config.hotkeyCode. Personal settings: a settings
 * preset never carries them (settingsPresets.js lists what a preset holds, and
 * these are not in it).
 *
 * Depends on: settings, KEY_CODES (config.js), t / setI18n (i18n.js, optional),
 * autoSaveSettings (optional)
 */

var PanelOpenSetting = {

    DEFAULT_HOTKEY: 'F8',
    DEFAULT_HOTKEY_CODE: 66,   // DirectInput scancode of F8
    NONE_CODE: 0,

    isNone: function() { return settings.hotkeyCode === this.NONE_CODE; },
    isLetterKey: function(name) { return typeof name === 'string' && /^[A-Z]$/.test(name); },

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

    /** Set the hotkey (code 0 = None). */
    setHotkey: function(name, code) {
        if (code === this.NONE_CODE) name = '';
        settings.hotkey = name;
        settings.hotkeyCode = code;
        this.sync();
        this._save();
    },

    init: function() {
        var self = this;
        var input = document.getElementById('hotkeyInput');
        var change = document.getElementById('changeHotkeyBtn');
        var reset = document.getElementById('resetHotkeyBtn');
        var none = document.getElementById('noHotkeyBtn');

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
        this.sync();
    },

    /** Put the saved values on the controls: the key and the letter key warning. */
    sync: function() {
        var none = this.isNone();
        var input = document.getElementById('hotkeyInput');
        var noneBtn = document.getElementById('noHotkeyBtn');

        if (input && !(input.classList && input.classList.contains && input.classList.contains('listening'))) {
            // None shows as the placeholder, which a language switch writes again
            input.value = none ? '' : settings.hotkey;
            if (typeof setI18n === 'function') setI18n(input, 'settings.hotkey.none', null, 'None', 'placeholder');
        }
        // Pressed (disabled) while None is the setting
        if (noneBtn) noneBtn.disabled = none;
        this._show('hotkeyLetterWarning', !none && this.isLetterKey(settings.hotkey));
    },

    /** Read the saved hotkey from a config (a leftover config.magicMenu is ignored). */
    loadFrom: function(data) {
        var hk = this.readHotkey(data);
        settings.hotkey = hk.hotkey;
        settings.hotkeyCode = hk.hotkeyCode;
        this.sync();
    },

    /** Write the hotkey into a config. */
    saveTo: function(config) {
        config.hotkey = this.isNone() ? '' : settings.hotkey;
        config.hotkeyCode = settings.hotkeyCode;
        return config;
    },

    reset: function() {
        settings.hotkey = this.DEFAULT_HOTKEY;
        settings.hotkeyCode = this.DEFAULT_HOTKEY_CODE;
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
