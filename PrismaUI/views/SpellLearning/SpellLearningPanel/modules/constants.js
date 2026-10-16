/**
 * SpellLearning Constants Module
 * 
 * Contains the default tree rules and the default colour palette. The hotkey
 * key codes (KEY_CODES) are in config.js.
 * This module has no dependencies and should be loaded early.
 */

// =============================================================================
// DEFAULT TEXT SIZES (2026-10-04: raised from 14 and 10 for readability)
// =============================================================================

// Panel body text in px at Font Size 1.0x (applyFontSizeMultiplier; the CSS
// body rule in styles-skyrim.css says the same before the settings load)
var PANEL_BASE_FONT_SIZE = 16;

// The tree's spell names in px (Settings > UI Display > Tree > Spell Name Size,
// settings.nodeFontSize; the slider runs 6..18)
var DEFAULT_NODE_FONT_SIZE = 12;

// The two sliders' ranges (index.html: fontSizeSlider, tree-node-font-size say
// the same); a saved value outside them is held to them when the config loads
var FONT_SIZE_MULTIPLIER_MIN = 0.7;
var FONT_SIZE_MULTIPLIER_MAX = 2.0;
var NODE_FONT_SIZE_MIN = 6;
var NODE_FONT_SIZE_MAX = 18;

// XP caps (Settings > Progression > XP Caps): percent of a spell's XP each source may
// give. The sliders' range (index.html xpCap*Slider) and the defaults C++ also uses
// (UIManagerConfig.cpp ReadXPSettingsFromConfig)
var XP_CAP_MIN = 0;
var XP_CAP_MAX = 100;
var XP_CAP_DEFAULTS = { xpCapAny: 5, xpCapSchool: 15, xpCapDirect: 50 };

/**
 * A saved number held to [min, max]; a missing or non-number value gives
 * `fallback`. `whole` rounds it (a slider with step 1).
 */
function clampSetting(value, min, max, fallback, whole) {
    var v = Number(value);
    if (value === undefined || value === null || value === '' || !isFinite(v)) return fallback;
    if (whole) v = Math.round(v);
    return v < min ? min : (v > max ? max : v);
}

/**
 * A plugin name to compare by: A-Z folded, every other letter as it is. The DLL folds
 * plugin names the same way (AsciiText, SpellScannerGate.cpp PluginKey), so a Korean,
 * Russian or accented name matches on both sides; toLowerCase would also fold Cyrillic
 * and accented capitals, which the DLL cannot. The spell gate's keys, the blacklist,
 * the plugin list and chosen roots all compare plugin names through this. Here, the
 * first script the page loads, so no module depends on the load order to have it.
 */
function foldPluginName(name) {
    return String(name).replace(/[A-Z]+/g, function(run) { return run.toLowerCase(); });
}

window.foldPluginName = foldPluginName;

// =============================================================================
// DEFAULT TREE RULES PROMPT
// =============================================================================

var DEFAULT_TREE_RULES = `You are a Skyrim spell tree architect. Create a logical learning progression tree for each school of magic.

## CRITICAL RULES (MUST FOLLOW)

### 1. SCHOOL SEPARATION (MOST IMPORTANT!)
- **NEVER mix spells between schools!**
- Each spell has a "school" field - ONLY place spells in their OWN school's tree
- Example: Clairvoyance has "school": "Illusion" → it goes in Illusion tree, NOT Alteration
- VALIDATE every spell belongs to the correct school before adding

### 2. INCLUDE ALL SPELLS
- Every spell from the input MUST appear in the output
- Count spells per school and verify your output matches

### 3. MAXIMUM 3 BRANCHES PER NODE
- Each spell can have AT MOST 3 children
- If more connections needed, create intermediate nodes or chains
- This keeps the tree visually clean and navigable

### 4. SAME-TIER BRANCHING ALLOWED
- Novice spells CAN branch to other Novice spells
- Progression does NOT require increasing skill level
- Group by THEME/EFFECT, not just skill tier
- Example: Flames (Novice) → Frostbite (Novice) → Sparks (Novice) is VALID if thematically grouped

### 5. PREFER VANILLA FORMIDS FOR ROOTS
- FormIDs starting with 0x00 are vanilla (preferred for roots)
- 0x02 = Dawnguard, 0x04 = Dragonborn, 0xFE+ = mods
- Recommended vanilla roots:
  - Destruction: Flames (0x00012FCD)
  - Restoration: Healing (0x00012FCC)
  - Alteration: Oakflesh (0x0005AD5C) or Candlelight (0x00043324)
  - Conjuration: Conjure Familiar (0x000640B6)
  - Illusion: Clairvoyance (0x00021143) or Courage (0x0004DEE8)

## Tree Structure Rules

1. **One Root Per School**: Each school has exactly ONE root spell - a Novice-level spell FROM THAT SCHOOL.

2. **Branching Logic** (max 3 children per node):
   - Destruction: Branch by element (Fire, Frost, Shock)
   - Conjuration: Branch by summon type (Atronachs, Undead, Daedra) and bound weapons
   - Illusion: Branch by effect type (Fear, Calm, Frenzy, Invisibility, Muffle)
   - Alteration: Branch by effect (Armor, Paralysis, Light, Transmute, Detect)
   - Restoration: Branch by effect (Healing, Turn Undead, Wards)

3. **Tier Progression**: 
   - Branching within same tier is encouraged (Novice→Novice for variety)
   - Higher tier spells should generally require SOME lower tier prereqs
   - Master spells need Expert prereqs, Expert needs Adept, etc.

4. **Prerequisites**: Only from same school, thematically sensible.

## BEFORE OUTPUT - VALIDATE:
- [ ] Every node's school matches the tree it's in
- [ ] All input spells appear in output
- [ ] Each school has exactly one root
- [ ] No node has more than 3 children
- [ ] No duplicate formIds across trees

## Custom Rules (add your own below)

- 
`;

// =============================================================================
// DEFAULT COLOR PALETTE
// =============================================================================

var DEFAULT_COLOR_PALETTE = [
    '#ef4444', // Red
    '#facc15', // Gold
    '#22c55e', // Green
    '#a855f7', // Purple
    '#38bdf8', // Cyan
    '#f97316', // Orange
    '#ec4899', // Pink
    '#14b8a6', // Teal
    '#8b5cf6', // Violet
    '#84cc16', // Lime
    '#06b6d4', // Sky
    '#f43f5e', // Rose
    '#6366f1', // Indigo
    '#eab308', // Yellow
    '#10b981', // Emerald
    '#d946ef', // Fuchsia
    '#0ea5e9', // Light Blue
    '#22d3ee', // Cyan Light
    '#fbbf24', // Amber
    '#a3e635'  // Lime Light
];
