/**
 * Language setting - the picker in Settings > UI Display and what keeps its choice.
 *
 * Depends on: i18n.js (switchLocale, getLanguages, getLocale, t), state.js (settings),
 *             settingsPanel.js (autoSaveSettings, and onUnifiedConfigLoaded calls
 *             applySavedLanguage); main.js calls initializeLanguageSelect
 */

// =============================================================================
// LANGUAGE (Settings > UI Display)
// =============================================================================
//
// Three places know the language, and they are consulted in this order when the
// panel starts: the browser's own storage (read in index.html before anything
// draws), then lang/locale.js (the translation pack's default). The saved
// settings arrive from the game a moment later; if they name another language,
// it is switched to then and copied into the browser's storage for next time.

var LANGUAGE_STORAGE_KEY = 'hom_language';

function rememberLanguage(code) {
    try {
        if (!window.localStorage) return;
        if (code) localStorage.setItem(LANGUAGE_STORAGE_KEY, code);
        else localStorage.removeItem(LANGUAGE_STORAGE_KEY);
    } catch (e) { /* storage unavailable: the saved settings still carry it */ }
}

function showLanguageHint(key, fallback) {
    var hint = document.getElementById('uiLanguageHint');
    if (!hint) return;
    var text = (typeof t === 'function') ? t(key) : key;
    hint.textContent = (text && text !== key) ? text : fallback;
    hint.removeAttribute('data-i18n'); // or the next re-label would put the description back
}

/**
 * A live switch re-labels what is marked data-i18n; text a script built with
 * t() is built again here. Each on its own, so one that throws does not keep
 * the rest in the old language. Still left until the next start: the build
 * progress window, the spell card (redrawn on the next pick), passing status
 * lines, the hotkey field's "press a key".
 */
function refreshScriptTexts() {
    var steps = [
        function() { if (typeof DesignPresets !== 'undefined') DesignPresets._syncSelector(); },
        function() { if (typeof updateEasyPresetChips === 'function') updateEasyPresetChips(); },
        function() { if (typeof TreeGrowth !== 'undefined' && TreeGrowth.relabelStatus) TreeGrowth.relabelStatus(); },
        function() { if (typeof updateSpellTomeLearningUI === 'function') updateSpellTomeLearningUI(); },
        function() { if (typeof renderPowerSteps === 'function') renderPowerSteps(); },
        function() { if (typeof updateHowToContent === 'function') updateHowToContent(); }
    ];
    for (var i = 0; i < steps.length; i++) {
        try { steps[i](); } catch (e) { console.warn('[Language] Refresh after the switch failed:', e); }
    }
}

/** Called when the saved settings arrive. */
function applySavedLanguage() {
    var select = document.getElementById('uiLanguageSelect');
    var wanted = settings.language;
    if (!wanted || typeof switchLocale !== 'function' || wanted === getLocale()) {
        if (select && typeof getLocale === 'function') select.value = getLocale();
        return;
    }
    rememberLanguage(wanted);
    switchLocale(wanted, function(ok) {
        if (select) select.value = getLocale();
        if (ok) {
            refreshScriptTexts();
            showLanguageHint('settings.ui.languageRestart', 'Some text changes the next time the game starts.');
        }
    });
}

function initializeLanguageSelect() {
    var select = document.getElementById('uiLanguageSelect');
    if (!select || typeof getLanguages !== 'function') return;

    select.innerHTML = '';
    var languages = getLanguages();
    for (var i = 0; i < languages.length; i++) {
        var option = document.createElement('option');
        option.value = languages[i].code;
        option.textContent = languages[i].name;
        select.appendChild(option);
    }
    select.value = getLocale();

    select.addEventListener('change', function() {
        var code = this.value;
        settings.language = code;
        rememberLanguage(code);
        autoSaveSettings();
        switchLocale(code, function(ok) {
            if (ok) {
                refreshScriptTexts();
                showLanguageHint('settings.ui.languageRestart', 'Some text changes the next time the game starts.');
            } else {
                // Do not keep asking for a file that is not there
                select.value = getLocale();
                settings.language = getLocale();
                rememberLanguage(getLocale());
                autoSaveSettings();
                showLanguageHint('settings.ui.languageMissing', 'That translation could not be loaded.');
            }
        });
    });
}