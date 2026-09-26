# Presets: Creating and Sharing

Heart of Magic has three preset systems that let you save, share, and load configurations.

## Preset Types

| Type | What It Saves | Use Case |
|------|---------------|----------|
| **Settings Presets** | XP rates, tier requirements, progression tuning, early spell learning, tome learning | "Easy mode", "Hardcore", custom difficulty |
| **Scanner Presets** | Root layout, growth algorithm settings, grid config, spell matching mode, PreReq Master locks | "Wide radial tree", "Tight clustered", custom visual style |
| **Design Presets** | How the panel looks: how the spell tree is drawn, the UI theme, extra CSS | "Arcane" spellbook, "Modern Dark", "Classic", looks shipped by add-ons |

Design presets are picked from a list (*Settings > UI Display > Design*), not saved from the
panel; they come from the mod and from files. See [Design Presets](#design-presets) below.

## In-Game Operations

### Save a Preset

1. Configure your settings the way you want them
2. Click the **[+] Save** button next to the preset chips
3. Enter a name in the prompt
4. Your preset appears as a new chip

### Apply a Preset

Click on any preset chip. All settings for that type update immediately.

### Update a Preset

1. Apply a preset (it highlights as active)
2. Adjust settings
3. Click the refresh button on the active chip
4. Current settings overwrite the saved preset

### Rename a Preset

Double-click the preset chip name. An inline text field appears — type the new name and press Enter.

### Delete a Preset

1. Click the X button on a preset chip
2. The button turns red (armed)
3. Click again within 2 seconds to confirm deletion

The Default preset cannot be deleted.

## Built-In Settings Presets

Three settings presets ship with the mod:

### Default (Normal)

Balanced progression. 100 XP for Novice spells up to 1500 XP for Master. Standard casting multipliers.

### Easy

2x faster progression. Half the XP requirements. Higher casting multipliers and caps. Wider early learning effectiveness range. More XP from spell tomes.

### Hard

75% XP rate. 50-67% higher tier requirements. Lower casting multipliers and caps. Narrower early learning range. Higher reveal thresholds (more discovery before seeing spell details).

## What Settings Presets Save

- XP global multiplier and per-source multipliers (Direct/School/Any)
- XP caps per source
- Tier XP requirements (Novice through Master)
- Learning mode and reveal thresholds
- Early spell learning (enabled, unlock threshold, effectiveness range, power steps)
- Spell tome learning (enabled, XP grant, inventory boost, prerequisite requirements)
- Discovery mode and notification settings

## What Scanner Presets Save

- Tree generation parameters (all procedural settings)
- Root base settings (Sun mode: ring tier, grid density, grid type; Flat mode: line points, direction)
- Active root mode (Sun/Flat)
- Classic growth settings (spread, radial bias, center mask, spell matching mode)
- Tree growth settings (trunk thickness, branch/trunk/root allocation)
- Active growth mode (Classic/Tree)
- PreReq Master settings (lock percentages, tier constraints, distribution mode)

## File Locations

Presets are stored as individual JSON files:

```
Skyrim SE/Data/SKSE/Plugins/SpellLearning/
  presets/
    settings/
      Default.json
      Easy.json
      Hard.json
      MyCustomDifficulty.json
    scanner/
      WideRadial.json
      TightClustered.json
```

Under MO2, these may be in the **Overwrite** folder:
```
MO2/overwrite/SKSE/Plugins/SpellLearning/presets/...
```

## Sharing Presets

### Exporting

1. Navigate to the preset folder on disk (see File Locations above)
2. Copy the `.json` file for the preset you want to share
3. Share the file (Nexus, Discord, etc.)

### Importing

1. Download the preset `.json` file
2. Drop it into the correct folder:
   - Settings presets go in `presets/settings/`
   - Scanner presets go in `presets/scanner/`
3. Restart the game (or reopen the Heart of Magic panel)
4. The preset appears as a chip in the UI

### Preset File Format

Settings preset example:

```json
{
  "name": "Relaxed Explorer",
  "created": 1707521234567,
  "builtIn": false,
  "settings": {
    "xpGlobalMultiplier": 1.5,
    "xpMultiplierDirect": 120,
    "xpMultiplierSchool": 60,
    "xpMultiplierAny": 15,
    "xpCapDirect": 55,
    "xpCapSchool": 20,
    "xpCapAny": 8,
    "xpNovice": 75,
    "xpApprentice": 150,
    "xpAdept": 300,
    "xpExpert": 600,
    "xpMaster": 1200,
    "learningMode": "perSchool",
    "revealName": 8,
    "revealEffects": 20,
    "revealDescription": 40,
    "earlySpellLearning": {
      "enabled": true,
      "unlockThreshold": 20,
      "minEffectiveness": 25,
      "maxEffectiveness": 75
    },
    "spellTomeLearning": {
      "enabled": true,
      "xpPercentToGrant": 30,
      "tomeInventoryBoost": true,
      "tomeInventoryBoostPercent": 30
    }
  }
}
```

Scanner preset example:

```json
{
  "name": "Wide Radial",
  "created": 1707521234567,
  "settings": {
    "treeGeneration": { },
    "sunSettings": {
      "ringTier": 5,
      "nodeSize": 25,
      "rootsPerSchool": 4,
      "gridDensity": 0.8,
      "gridType": "EqualArea",
      "invertGrowth": false
    },
    "activeMode": "sun",
    "classicSettings": {
      "spread": 100,
      "radialBias": 0.5,
      "spellMatching": "smart"
    },
    "treeGrowthActiveMode": "classic",
    "prmEnabled": true,
    "prmSettings": {
      "globalLockPercent": 30
    }
  }
}
```

## Design Presets

A design preset decides how the panel looks. Three are built in: **Arcane** (the default, an open
spellbook on parchment), **Modern Dark** (slate-blue glass, amber accent, rounded) and **Classic** (the
tree as it looked before presets). Two more ship with the mod as preset files, **Night Grimoire** and
**Candlelit Tome** (`SKSE/Plugins/SpellLearning/presets/design/` in the repository) - copy one to start
your own. Add-ons and players add more with
one `.json` file each in:

```
Skyrim SE/Data/SKSE/Plugins/SpellLearning/presets/design/
```

Each file is its own design, so add-ons never have to share or overwrite a list. The file's `name` is
what the list shows; `id` (optional, else the name) is what the player's choice is saved as. A built-in
id (`arcane`, `modern`, `classic`) cannot be replaced.

A player can turn off a design's page, selection sigil, learning glow and heart runes in the
render popup (its chips), or still everything at once; a design does not need
to offer its own switches for them.

### The `render` block

The heart, globe and starfield are the design's to shape: a preset's optional `render` object sets any
of the panel's renderer settings by their config names, and the value counts over the player's saved
one. Keys: `globeSize`, `globeParticleRadius`, `globeDensity`, `globeDotMin`, `globeDotMax`,
`globeBgFill`, `particleCoreEnabled`, `heartPulseSpeed`, `heartPulseDelay`, `heartBgColor`,
`heartRingColor`, `learningPathColor`, `globeColor`, `magicTextColor`, `globeText`, `globeTextSize`,
`starfieldColor`, `starfieldDensity`, `starfieldMaxSize`, `starfieldSeed`, `starfieldBgColor`. A key
the design leaves out keeps the shipped value. `globeSize` alone sizes the particle globe too. A value
of the wrong type is converted when it can be ("60" -> 60) and skipped otherwise.

```json
"render": { "globeSize": 60, "globeText": "ARCANUM", "starfieldDensity": 120 }
```

Several looks can share one stylesheet: `cssFile` points at the sheet and each preset's inline `css`
sets the variables it reads - the inline CSS is always applied after the sheet. The shipped Night
Grimoire and Candlelit Tome do this with `themes/design-darkbook.css` and a `:root { --book-... }`
palette; a new dark look only needs its own palette.

The heart, globe and learning-path colours can be set as tree tokens (`hubRing`, `hubFill`, `hubText`,
`globeColor`, `learningColor`) or in the `render` block below. The render popup no longer offers them;
a player's old value from before is put back to the shipped default once, so a design's colours show
(`CanvasRenderer._designOrPlayer` still lets a non-default value through, for configs edited by hand).

```json
{
  "id": "frost-codex",
  "name": "Frost Codex",
  "author": "SomeModder",
  "description": "Pale blue vellum, silver ink.",
  "names": { "ko": "서리 사본", "de": "Frost-Kodex" },
  "descriptions": { "ko": "옅은 푸른 양피지, 은빛 잉크." },
  "uiTheme": "skyrim",
  "cssFile": "themes/frost-codex.css",
  "css": ["#tooltip .tooltip-name { font-family: Georgia, serif; }"],
  "tree": {
    "pageColor": "#dfe8ee",
    "schoolInk": 0.3,
    "schoolInkTone": "#1a2a3a",
    "labelFont": "Georgia, serif",
    "chapterTitles": true
  }
}
```

| Field | Meaning |
|-------|---------|
| `tree` | Tokens for the spell tree (below). Anything left out keeps the Classic value. |
| `names`, `descriptions` | Optional. The name and description per language code (`ko`, `de`, `zh-cn`, ...), since an add-on cannot add lines to the mod's `lang/` files. Languages not listed show `name` / `description`. |
| `uiTheme` | The UI theme the design is built on: an id from `themes/manifest.json` (`skyrim` = Skyrim Edge, the only one shipped; Modern Dark is Skyrim Edge with `themes/design-modern.css` over it). Left out, `skyrim`. The UI theme has no selector of its own any more - the design sets it. A design's CSS can also set `--preview-bg`, the background of the scan screen's tree previews. |
| `cssFile` | Optional. A stylesheet laid over the theme, path relative to the panel folder (`PrismaUI/views/SpellLearning/SpellLearningPanel/`). Ship it under your own name in `themes/`. |
| `css` | Optional. The same, written inline: a string or an array of lines. |

Colours in `tree` are `#rrggbb` or `rgba(r, g, b, a)`. A token with the wrong type (text where a number
belongs) is ignored and keeps its Classic value; unknown tokens are ignored, so a preset written for a
newer version still loads.

### Tree tokens

| Group | Tokens |
|-------|--------|
| Page | `pageColor` (`""` keeps the starfield), `pageGrain` 0-1, `pageGrainColor`, `pageGlow`, `pageGlowAlpha`, `pageGlowRadius`, `pageEdge`, `pageEdgeAlpha` |
| Ink | `schoolInk` 0-1 and `schoolInkTone`: school colours mixed toward the tone, so the player's own school colours still read on the page. `learningColor` (`""` = the player's setting) |
| Labels | `labelFont`, `labelMaxChars`, `labelHalo` (outline colour, `""` = none), `labelHaloWidth`, `labelUnlocked`, `labelAvailable`, `labelHidden` |
| Spells | `nodeFill`, `unlockedFill`, `unlockedRim`, `unlockedCore` (`""` = school colour or its dark shade), `lockedStroke`, `mysteryFill`, `focusStroke`, `ringTrack`, `availableAlpha`, `availableRing`, `nodeGlow`, `learningGlow` |
| Heart | `hubFill`, `hubRing`, `hubText`, `globeColor` (`""` = the player's heart settings), `hubRunes` |
| Lines | `dimEdgeColor`, `unlockedEdgeColor`, `unlockedEdgeAlpha`, `unlockedEdgeWidth`, `edgeGlow`, `lockedEdgeColor`, `lockedEdgeAlpha`, `frontierEdgeAlpha` (edges into learnable spells; 0 = drawn as locked), `selectedPathColor`, `selectedPathAlpha`, `selectedPathWidth`, `hoverPathAlpha` |
| Book | `accent` (sigil, runes, chapter titles, dividers), `selectionSigil`, `chapterTitles`, `chapterSize`, `dividerOrnament` (`reveal`, `revealMs` and `inkColor` - the ink reveal on opening - were removed and are ignored) |

`TreeStyle.DEFAULTS` in `modules/treeStyle.js` is the authoritative list with the Classic values, and
the Arcane preset in `modules/designPresets.js` is a full worked example. `nodeGlow` and `edgeGlow` are
the costly ones (a sprite per learned spell, a wide stroke per learned edge); the rest cost about the
same as Classic. See [DESIGN.md](DESIGN.md) for measurements.

Restart the game after adding a file: presets are read once, when the panel first loads its settings.

## Tips

- Start from a built-in preset and tweak from there
- Scanner presets pair with specific root modes — a Sun-based scanner preset won't look right if applied with Flat mode active
- Share your presets on the mod's Nexus page for others to try
- Back up your presets folder before updating the mod
