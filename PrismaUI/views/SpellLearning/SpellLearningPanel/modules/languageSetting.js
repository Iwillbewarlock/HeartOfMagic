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
        if (ok) showLanguageHint('settings.ui.languageRestart', 'Some text changes the next time the game starts.');
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