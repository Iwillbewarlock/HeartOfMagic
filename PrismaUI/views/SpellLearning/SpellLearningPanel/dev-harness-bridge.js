/**
 * Dev Harness - Mock C++ Bridge
 * Replaces window.callCpp with a mock that logs calls and returns test data.
 * Must be loaded BEFORE all module scripts.
 */

window._BROWSER_TEST = true;
window._DEV_HARNESS = true;
window._cppLog = [];
window._mockSpellData = null;

// ============================================================================
// LOGGING
// ============================================================================

function logCppCall(direction, method, data) {
    var entry = {
        time: new Date().toLocaleTimeString(),
        direction: direction, // 'out' = JS->C++, 'in' = C++->JS
        method: method,
        data: typeof data === 'string' ? data.substring(0, 200) : ''
    };
    window._cppLog.push(entry);

    // Update status bar
    var statusEl = document.getElementById('harness-status');
    if (statusEl) {
        var last = statusEl.querySelector('.log-entry');
        if (!last) {
            last = document.createElement('span');
            last.className = 'log-entry';
            statusEl.appendChild(last);
        }
        var arrow = direction === 'out' ? '>' : '<';
        last.innerHTML = '<span class="method">' + arrow + ' ' + method + '</span> <span class="data">' + entry.data + '</span>';
    }

    // Update log panel
    var logBody = document.getElementById('cpp-log-body');
    if (logBody) {
        var item = document.createElement('div');
        item.className = 'log-item';
        item.innerHTML = '<span class="dir ' + direction + '">' + (direction === 'out' ? '>' : '<') + '</span>'
            + '<span class="method">' + method + '</span>'
            + '<span class="payload">' + entry.data + '</span>';
        logBody.appendChild(item);
        logBody.scrollTop = logBody.scrollHeight;
    }

    console.log('[Bridge ' + (direction === 'out' ? 'JS>C++' : 'C++>JS') + '] ' + method,
        data ? (typeof data === 'string' ? data.substring(0, 100) : data) : '');
}

/**
 * A stand-in tree for the harness when no dev server answers ProceduralTreeGenerate:
 * each school's spells sorted by tier and chained, at most three children a spell.
 * Not the Classic builder - just enough tree to exercise the panel.
 */
function mockTreeFromSpells(spells) {
    var schoolSpells = {};
    (spells || []).forEach(function(s) {
        var school = s.school || 'Unknown';
        if (!schoolSpells[school]) schoolSpells[school] = [];
        schoolSpells[school].push(s);
    });

    var mockTree = { version: '1.0', generator: 'DevHarness Mock', schools: {} };
    var tierOrder = ['Novice', 'Apprentice', 'Adept', 'Expert', 'Master'];
    for (var school in schoolSpells) {
        var list = schoolSpells[school];
        // Sort by tier
        list.sort(function(a, b) {
            return tierOrder.indexOf(a.skillLevel || 'Novice') - tierOrder.indexOf(b.skillLevel || 'Novice');
        });
        var rootId = list[0].formId;
        var nodes = [];
        var parentStack = [rootId];
        var childCount = {};
        childCount[rootId] = 0;

        for (var i = 0; i < list.length; i++) {
            var sp = list[i];
            var node = {
                formId: sp.formId,
                name: sp.name,
                children: [],
                prerequisites: [],
                tier: i === 0 ? 1 : Math.floor(i / 3) + 1,
                skillLevel: sp.skillLevel || 'Novice'
            };

            if (i > 0) {
                // Find a parent with < 3 children
                var parentId = parentStack[0];
                for (var pi = 0; pi < parentStack.length; pi++) {
                    if ((childCount[parentStack[pi]] || 0) < 3) {
                        parentId = parentStack[pi];
                        break;
                    }
                }
                node.prerequisites.push(parentId);
                // Add as child to parent
                for (var ni = 0; ni < nodes.length; ni++) {
                    if (nodes[ni].formId === parentId) {
                        nodes[ni].children.push(sp.formId);
                        break;
                    }
                }
                childCount[parentId] = (childCount[parentId] || 0) + 1;
            }

            nodes.push(node);
            parentStack.push(sp.formId);
            childCount[sp.formId] = 0;
        }

        mockTree.schools[school] = { root: rootId, layoutStyle: 'radial', nodes: nodes };
    }
    return mockTree;
}

// ============================================================================
// MOCK callCpp
// ============================================================================

window.callCpp = function(method, data) {
    logCppCall('out', method, data);

    switch (method) {
        case 'LogMessage':
            try { var d = JSON.parse(data); console.log('[SKSE]', d.message || data); } catch(e) {}
            break;

        case 'ScanSpells':
            // Simulate async scan - load from test-data files
            setTimeout(function() { mockScanSpells(data); }, 300);
            break;

        case 'LoadSpellTree':
            // No saved tree in dev mode
            console.log('[Bridge] LoadSpellTree - no saved tree');
            break;

        case 'SaveSpellTree':
            window._lastSavedTree = data;
            // In dev mode, offer the JSON as a download so it can be
            // placed at SKSE/Plugins/SpellLearning/spell_tree.json
            try {
                var blob = new Blob([data], { type: 'application/json' });
                var url = URL.createObjectURL(blob);
                var a = document.createElement('a');
                a.href = url;
                a.download = 'spell_tree.json';
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                URL.revokeObjectURL(url);
                console.log('[Bridge] SaveSpellTree - downloaded spell_tree.json');
            } catch(e) {
                console.log('[Bridge] SaveSpellTree - stored in memory (download failed)');
            }
            if (typeof updateTreeStatus === 'function') {
                setTimeout(function() {
                    logCppCall('in', 'updateTreeStatus', 'Tree saved (dev mode)');
                }, 50);
            }
            break;

        case 'SaveOutput':
            console.log('[Bridge] SaveOutput - logged');
            if (typeof updateStatus === 'function') {
                setTimeout(function() { updateStatus('Output saved (dev mode)'); }, 50);
            }
            break;

        case 'HidePanel':
            console.log('[Bridge] HidePanel - panel stays visible in dev harness');
            break;

        case 'LoadUnifiedConfig':
            setTimeout(function() { mockLoadConfig(); }, 100);
            break;

        case 'SaveUnifiedConfig':
            console.log('[Bridge] Config saved (dev mode)');
            break;

        case 'GetProgress':
            setTimeout(function() { mockGetProgress(); }, 50);
            break;

        case 'GetPlayerKnownSpells':
            setTimeout(function() { mockGetKnownSpells(); }, 50);
            break;

        case 'SetLearningTarget':
            console.log('[Bridge] SetLearningTarget:', data);
            try {
                var target = JSON.parse(data);
                if (typeof window.onLearningTargetSet === 'function') {
                    setTimeout(function() {
                        logCppCall('in', 'onLearningTargetSet', JSON.stringify({ school: target.school, formId: target.formId, success: true }));
                        window.onLearningTargetSet(JSON.stringify({ school: target.school, formId: target.formId, spellName: 'Mock Spell', success: true }));
                    }, 100);
                }
            } catch(e) {}
            break;

        case 'CopyToClipboard':
            navigator.clipboard.writeText(data).then(function() {
                console.log('[Bridge] Copied to clipboard');
            }).catch(function() {
                console.log('[Bridge] Clipboard copy failed');
            });
            break;

        case 'GetClipboard':
            navigator.clipboard.readText().then(function(text) {
                if (typeof window.onClipboardContent === 'function') {
                    window.onClipboardContent(text);
                }
            }).catch(function() {
                console.log('[Bridge] Clipboard read failed');
            });
            break;

        case 'LoadPrompt':
            if (typeof window.updatePrompt === 'function') {
                setTimeout(function() {
                    window.updatePrompt(typeof DEFAULT_TREE_RULES !== 'undefined' ? DEFAULT_TREE_RULES : '');
                }, 50);
            }
            break;

        case 'CheckLLM':
            if (typeof window.onLLMStatus === 'function') {
                setTimeout(function() {
                    window.onLLMStatus(JSON.stringify({ available: false, version: 'dev-harness' }));
                }, 50);
            }
            break;

        case 'SetHotkey':
        case 'SetPauseGameOnFocus':
            console.log('[Bridge] Setting stored (dev mode)');
            break;

        case 'SetupPython':
            // Legacy — no-op, builder is always ready
            if (typeof window.onBuilderStatus === 'function') {
                setTimeout(function() {
                    window.onBuilderStatus(JSON.stringify({ installed: true, hasScript: true }));
                }, 200);
            }
            break;

        case 'DeclutterTree':
            // No plugin here: answer at once so LayoutDeclutter arranges the tree itself
            (function() {
                var id = null;
                try { id = JSON.parse(data).id; } catch (e) {}
                setTimeout(function() {
                    if (typeof window.onDeclutterResult === 'function') {
                        window.onDeclutterResult(JSON.stringify({ id: id, error: 'no plugin in the dev harness' }));
                    }
                }, 0);
            })();
            break;

        case 'ProceduralTreeGenerate':
            // Try dev server first (localhost:5556), fall back to a mock tree
            (function() {
                var request;
                try { request = JSON.parse(data); } catch(e) { request = { spells: [], config: {} }; }

                var spells = request.spells || [];
                var config = request.config || {};

                console.log('[Bridge] ProceduralTreeGenerate: ' + spells.length + ' spells');

                // Try dev server
                fetch('http://localhost:5556/build', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: data
                }).then(function(resp) {
                    if (!resp.ok) throw new Error('HTTP ' + resp.status);
                    return resp.json();
                }).then(function(result) {
                    console.log('[Bridge] Dev server returned result');
                    logCppCall('in', 'onProceduralTreeComplete', 'Dev server: ' + (result.success ? 'OK' : 'FAIL'));
                    if (typeof window.onProceduralTreeComplete === 'function') {
                        window.onProceduralTreeComplete(JSON.stringify(result));
                    }
                }).catch(function(err) {
                    console.log('[Bridge] Dev server not available (' + err.message + '), using JS fallback');
                    var treeData = mockTreeFromSpells(spells);
                    var result = {
                        success: true,
                        treeData: treeData,
                        elapsed: '0.1'
                    };
                    logCppCall('in', 'onProceduralTreeComplete', 'JS fallback: ' + Object.keys(treeData.schools || {}).length + ' schools');
                    if (typeof window.onProceduralTreeComplete === 'function') {
                        window.onProceduralTreeComplete(JSON.stringify(result));
                    }
                });
            })();
            break;

        case 'SavePreset':
            // Save preset to localStorage
            (function() {
                try {
                    var args = JSON.parse(data);
                    var storageKey = 'preset_' + args.type + '_' + args.name;
                    localStorage.setItem(storageKey, JSON.stringify(args.data));
                    console.log('[Bridge] SavePreset: saved ' + args.type + '/' + args.name + ' to localStorage');
                } catch(e) {
                    console.error('[Bridge] SavePreset failed:', e);
                }
            })();
            break;

        case 'DeletePreset':
            // Delete preset from localStorage
            (function() {
                try {
                    var args = JSON.parse(data);
                    var storageKey = 'preset_' + args.type + '_' + args.name;
                    localStorage.removeItem(storageKey);
                    console.log('[Bridge] DeletePreset: removed ' + args.type + '/' + args.name + ' from localStorage');
                } catch(e) {
                    console.error('[Bridge] DeletePreset failed:', e);
                }
            })();
            break;

        case 'LoadPresets':
            // Load all presets of a type from localStorage
            (function() {
                try {
                    var args = JSON.parse(data);
                    var type = args.type;
                    var prefix = 'preset_' + type + '_';
                    var presets = [];
                    for (var i = 0; i < localStorage.length; i++) {
                        var key = localStorage.key(i);
                        if (key.indexOf(prefix) === 0) {
                            var name = key.substring(prefix.length);
                            try {
                                var presetData = JSON.parse(localStorage.getItem(key));
                                presets.push({ key: name, data: presetData });
                            } catch(pe) {}
                        }
                    }
                    console.log('[Bridge] LoadPresets: found ' + presets.length + ' ' + type + ' presets in localStorage');
                    setTimeout(function() {
                        logCppCall('in', 'onPresetsLoaded', presets.length + ' ' + type + ' presets');
                        if (typeof window.onPresetsLoaded === 'function') {
                            window.onPresetsLoaded(JSON.stringify({ type: type, presets: presets }));
                        }
                    }, 50);
                } catch(e) {
                    console.error('[Bridge] LoadPresets failed:', e);
                }
            })();
            break;

        default:
            console.log('[Bridge] Unhandled method:', method);
    }
};

// ============================================================================
// MOCK DATA RESPONSES
// ============================================================================

function mockScanSpells(configStr) {
    // Check if this is a tomes-only scan (for filtering)
    var isTomeScan = false;
    try {
        var cfg = JSON.parse(configStr);
        isTomeScan = cfg.scanMode === 'tomes';
    } catch (e) {}

    if (isTomeScan && window._mockSpellData) {
        // Return ~70% of spells as "tomed" with scanMode marker
        var tomed = window._mockSpellData.filter(function(s, i) { return i % 3 !== 0; });
        var tomeResult = {
            scanMode: 'spell_tomes',
            spellCount: tomed.length,
            spells: tomed.map(function(s) {
                return Object.assign({}, s, { tomeFormId: 'tome_' + s.formId, tomeName: s.name + ' Tome' });
            })
        };
        logCppCall('in', 'updateSpellData', tomed.length + ' tomed spells');
        if (typeof window.updateSpellData === 'function') {
            window.updateSpellData(JSON.stringify(tomeResult));
        }
        return;
    }

    if (window._mockSpellData) {
        var allResult = { scanMode: 'all_spells', spellCount: window._mockSpellData.length, spells: window._mockSpellData };
        logCppCall('in', 'updateSpellData', window._mockSpellData.length + ' spells');
        if (typeof window.updateSpellData === 'function') {
            window.updateSpellData(JSON.stringify(allResult));
        }
        updateHarnessStatus();
        return;
    }

    // Load from pre-scanned school data files (synced from MO2/overwrite)
    var schools = ['Alteration', 'Conjuration', 'Destruction', 'Illusion', 'Restoration'];
    var allSpells = [];
    var loaded = 0;

    schools.forEach(function(school) {
        fetch('test-data/' + school + '_spells.json')
            .then(function(r) { return r.json(); })
            .then(function(data) {
                var spells = data.spells || (Array.isArray(data) ? data : []);
                spells.forEach(function(s) {
                    s.school = s.school || school;
                    // Extract plugin name from persistentId (e.g. "Skyrim.esm|0x10F7EE")
                    if (!s.plugin && s.persistentId) {
                        var parts = s.persistentId.split('|');
                        if (parts.length >= 1) s.plugin = parts[0];
                    }
                    allSpells.push(s);
                });
                loaded++;
                if (loaded === schools.length) {
                    window._mockSpellData = allSpells;
                    var result = { scanMode: 'all_spells', spellCount: allSpells.length, spells: allSpells };
                    logCppCall('in', 'updateSpellData', allSpells.length + ' spells');
                    if (typeof window.updateSpellData === 'function') {
                        window.updateSpellData(JSON.stringify(result));
                    }
                    updateHarnessStatus();
                }
            })
            .catch(function(err) {
                console.error('[Bridge] Failed to load ' + school + ':', err);
                loaded++;
                if (loaded === schools.length && allSpells.length > 0) {
                    window._mockSpellData = allSpells;
                    var result = { scanMode: 'all_spells', spellCount: allSpells.length, spells: allSpells };
                    if (typeof window.updateSpellData === 'function') {
                        window.updateSpellData(JSON.stringify(result));
                    }
                    updateHarnessStatus();
                }
            });
    });
}

function mockLoadConfig() {
    var config = {
        hotkey: 'F8',
        hotkeyCode: 66,
        pauseGameOnFocus: true,
        cheatMode: false,
        verboseLogging: false,
        heartAnimationEnabled: true,
        heartPulseSpeed: 0.06,
        heartBgOpacity: 1.0,
        heartBgColor: '#0a0a14',
        heartRingColor: '#b8a878',
        learningPathColor: '#00ffff',
        learningMode: 'perSchool',
        xpGlobalMultiplier: 1,
        xpMultiplierDirect: 100,
        xpMultiplierSchool: 50,
        xpMultiplierAny: 10,
        xpCapAny: 5,
        xpCapSchool: 15,
        xpCapDirect: 50,
        xpNovice: 100,
        xpApprentice: 200,
        xpAdept: 400,
        xpExpert: 800,
        xpMaster: 1500,
        revealName: 10,
        revealEffects: 25,
        revealDescription: 50,
        discoveryMode: false,
        nodeSizeScaling: true,
        earlySpellLearning: {
            enabled: true,
            unlockThreshold: 25.0,
            selfCastRequiredAt: 75.0,
            selfCastXPMultiplier: 150.0,
            binaryEffectThreshold: 80.0,
            modifyGameDisplay: true,
            powerSteps: [
                { xp: 25, power: 20, label: 'Budding' },
                { xp: 40, power: 35, label: 'Developing' },
                { xp: 55, power: 50, label: 'Practicing' },
                { xp: 70, power: 65, label: 'Advancing' },
                { xp: 85, power: 80, label: 'Refining' }
            ]
        },
        spellTomeLearning: {
            enabled: true,
            useProgressionSystem: true,
            grantXPOnRead: true,
            autoSetLearningTarget: true,
            showNotifications: true,
            xpPercentToGrant: 25.0,
            tomeInventoryBoost: true,
            tomeInventoryBoostPercent: 25.0,
            requirePrereqs: true,
            requireAllPrereqs: true,
            requireSkillLevel: false
        },
        notifications: { weakenedSpellNotifications: true, weakenedSpellInterval: 10.0 },
        llm: { apiKey: '', model: 'anthropic/claude-sonnet-4', maxTokens: 64000 },
        schoolColors: {}
    };

    logCppCall('in', 'onUnifiedConfigLoaded', 'config loaded');
    if (typeof window.onUnifiedConfigLoaded === 'function') {
        window.onUnifiedConfigLoaded(JSON.stringify(config));
    }
}

function mockGetProgress() {
    logCppCall('in', 'onProgressData', '{}');
    if (typeof window.onProgressData === 'function') {
        window.onProgressData(JSON.stringify({}));
    }
}

function mockGetKnownSpells() {
    logCppCall('in', 'onPlayerKnownSpells', '{"knownSpells":[],"weakenedSpells":[],"count":0}');
    if (typeof window.onPlayerKnownSpells === 'function') {
        window.onPlayerKnownSpells(JSON.stringify({ knownSpells: [], weakenedSpells: [], count: 0 }));
    }
}

// ============================================================================
// HARNESS HELPERS
// ============================================================================

function updateHarnessStatus() {
    var spellCount = document.getElementById('status-spell-count');
    var schoolCount = document.getElementById('status-school-count');
    if (window._mockSpellData && spellCount) {
        spellCount.textContent = window._mockSpellData.length;
        var schools = {};
        window._mockSpellData.forEach(function(s) { if (s.school) schools[s.school] = true; });
        if (schoolCount) schoolCount.textContent = Object.keys(schools).length;
    }
}

// Fire onPanelShowing after init
window.addEventListener('load', function() {
    setTimeout(function() {
        if (typeof window.onPanelShowing === 'function') {
            logCppCall('in', 'onPanelShowing', '');
            window.onPanelShowing();
        }

        // Probe dev server -- report real status to UI
        window._devServerAvailable = false;
        fetch('http://localhost:5556/build', { method: 'OPTIONS' })
            .then(function(r) {
                window._devServerAvailable = true;
                console.log('[DevHarness] Dev server ONLINE (port 5556)');
                if (typeof window.onBuilderStatus === 'function') {
                    window.onBuilderStatus(JSON.stringify({
                        installed: true, hasScript: true
                    }));
                }
                // Show in toolbar
                var statusEl = document.getElementById('harness-status');
                if (statusEl) {
                    var tag = document.createElement('span');
                    tag.style.cssText = 'color:#3fb950;font-weight:600;';
                    tag.textContent = 'Server: LIVE';
                    statusEl.appendChild(tag);
                }
            })
            .catch(function() {
                console.log('[DevHarness] Dev server offline -- JS fallback active');
                if (typeof window.onBuilderStatus === 'function') {
                    window.onBuilderStatus(JSON.stringify({
                        installed: true, hasScript: true
                    }));
                }
                var statusEl = document.getElementById('harness-status');
                if (statusEl) {
                    var tag = document.createElement('span');
                    tag.style.cssText = 'color:#d29922;';
                    tag.textContent = 'Server: offline (JS fallback)';
                    statusEl.appendChild(tag);
                }
            });
    }, 500);
});

console.log('[DevHarness] Mock C++ bridge loaded');
