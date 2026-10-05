/**
 * The DLL's status messages (UIManagerScanner.cpp, UIManagerTree.cpp,
 * UIManagerConfig.cpp, UIManagerConfigSave.cpp) are English; these are their
 * language keys, so the player reads them in the panel's language. A message
 * not listed here (a changed DLL text) shows as the DLL wrote it.
 */
var CPP_STATUS_KEYS = {
    'Scanning spell tomes...': 'status.scanningTomes',
    'Scanning all spells...': 'status.scanningAllSpells',
    'Failed to save file': 'status.saveFileFailed',
    'Saved to spell_scan_output.json': 'status.savedScanOutput',
    'Error: config.json could not be read - settings are the defaults and changes are NOT written to it until the next game start (see SpellLearning.log)': 'status.configUnreadable',
    'Error: config.json could not be read - changes are NOT written to it (see SpellLearning.log)': 'status.configNotSaved',
    'No saved tree - import one': 'status.noSavedTreeImport',
    'Saved tree is damaged - see SpellLearning.log': 'status.savedTreeDamaged',
    'Tree saved': 'status.treeSaved',
    'Save failed': 'status.saveFailed'
};

/** The key for one of the DLL's messages, or null (an own key: no Object prototype names) */
function cppStatusKey(message) {
    return Object.prototype.hasOwnProperty.call(CPP_STATUS_KEYS, message) ? CPP_STATUS_KEYS[message] : null;
}
