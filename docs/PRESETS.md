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

- No `treeGeneration` block since 2026-09-28: its fields (themes, routing, tier rules, scoring,
  convergence, LLM features) belonged to the removed builders, and its last flag,
  `bidirectionalSoftPrereqs`, was the inert Alternate Pathways toggle, removed the same day. An older
  preset's or config's `treeGeneration` is ignored
- Root base settings (Sun mode: ring tier, grid density, grid type; Flat mode: line points, direction)
- Active root mode (Sun/Flat)
- Classic growth settings (spread, radial bias, center mask, spell matching mode)
- Active growth mode (Classic; an old preset naming another mode falls back to Classic, and its
  `treeGrowthTreeSettings` - the removed Tree mode's settings - are ignored and no longer saved)
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

A file is named after its preset. Only characters Windows forbids in file names (`/ \ : * ? " < > |`) become
`_`; other letters stay, Korean or Chinese included (the name goes to the file system as UTF-8,
`PathText::FromUtf8`). Builds before that wrote a non-ASCII name through the Windows code page, so such a preset
may sit under a garbled file name. It still lists by the `name` inside it. Deleting or renaming it (delete, then
save) finds it the same way, a fallback for non-ASCII names from older builds only: when no file carries the
name and the name has a non-ASCII letter, every file in the folder whose `name` inside is that name is removed.
An ASCII name was never garbled, so a file that only carries it inside (an add-on's `AddonFire.json` named
"Fire") is never touched. Saving writes the preset's own file and removes nothing. A folder listing skips a file
it cannot read or name, it does not stop there.

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
**Chalkboard** (`SKSE/Plugins/SpellLearning/presets/design/` in the repository; Candlelit Tome was removed on 2026-09-27 - a saved choice of it falls back to Arcane) - copy one to start
your own. Add-ons and players add more with
one `.json` file each in:

```
Skyrim SE/Data/SKSE/Plugins/SpellLearning/presets/design/
```

Each file is its own design, so add-ons never have to share or overwrite a list. The file's `name` is
what the list shows; `id` (optional, else the name) is what the player's choice is saved as. A built-in
id (`arcane`, `modern`, `classic`) cannot be replaced.

A player can turn off a design's page, selection sigil, learning glow, heart runes and drawn lines
(all the Drawn lines tokens at once) in the render popup (its chips), or still everything at once; a design does not need
to offer its own switches for them.

### The `render` block

The heart, globe and starfield are the design's to shape: a preset's optional `render` object sets any
of the panel's renderer settings by their config names, and the value counts over the player's saved
one. Keys: `globeSize`, `globeParticleRadius`, `globeDensity`, `globeDotMin`, `globeDotMax`,
`globeBgFill`, `particleCoreEnabled`, `heartPulseSpeed`, `heartPulseDelay`, `heartBgColor`,
`heartRingColor`, `learningPathColor`, `globeColor`, `magicTextColor`, `globeText`, `globeTextSize`,
`starfieldColor`, `starfieldDensity`, `starfieldMaxSize`, `starfieldSeed`, `starfieldBgColor`,
`starfieldFixed` (true: the stars stay put on the screen instead of moving with the tree). A key
the design leaves out keeps the shipped value. `globeSize` alone sizes the particle globe too. A value
of the wrong type is converted when it can be ("60" -> 60) and skipped otherwise.

```json
"render": { "globeSize": 60, "globeText": "ARCANUM", "starfieldDensity": 120 }
```

Several looks can share one stylesheet: `cssFile` points at the sheet and each preset's inline `css`
sets the variables it reads - the inline CSS is always applied after the sheet. The shipped Night
Grimoire and Chalkboard do this with `themes/design-darkbook.css` and a `:root { --book-... }`
palette; a new dark look only needs its own palette.
(`--book-cover` is the leather's one colour; without it the sheet uses `--book-cover-top`.)

**Keep a design's CSS cheap.** In game PrismaUI paints the panel on the CPU and repaints whatever lies
under or over anything that changes, several times a second. Measured there: box shadows (blurred ones
worst, but plain rings too) and gradients on large surfaces - the panel, its header, the cards, tools,
tooltip, modals - tripled the stalls (223 vs 69 ms with a design's shadows and gradients off). Use one
colour and `border`/`outline` for those (an inner rule: `outline` with a negative `outline-offset`); keep
gradients and shadows to small things such as buttons.

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
| `descriptions` | Optional. The description per language code (`ko`, `de`, `zh-cn`, ...), since an add-on cannot add lines to the mod's `lang/` files. Languages not listed show `description`. Names are not translated: the selector shows `name` in every language (the shipped designs' names are English; a `names` map, which older add-ons may carry, is ignored). |
| `uiTheme` | The UI theme the design is built on: an id from `themes/manifest.json` (`skyrim` = Skyrim Edge, the only one shipped; Modern Dark is Skyrim Edge with `themes/design-modern.css` over it). Left out, `skyrim`. The UI theme has no selector of its own any more - the design sets it. A design's CSS can also set `--preview-bg`, the background of the scan screen's tree previews, and `--status-idle`, `--status-working`, `--status-done`, `--status-error`, the tree builder's status line (kept apart from each other). |
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
| Labels | `labelFont` (a font list, or `var(--name)` to use a CSS variable the design sets per language - the canvas reads it once per design and language), `labelMaxChars`, `labelHalo` (outline colour, `""` = none), `labelHaloWidth`, `labelUnlocked`, `labelAvailable`, `labelHidden` |
| Spells | `nodeFill`, `unlockedFill`, `unlockedRim`, `unlockedCore` (`""` = school colour or its dark shade), `lockedStroke`, `mysteryFill`, `focusStroke`, `ringTrack`, `availableAlpha`, `availableRing`, `nodeGlow`, `learningGlow` |
| Heart | `hubFill`, `hubRing`, `hubText`, `globeColor` (`""` = the player's heart settings), `hubRunes`, `hubEmblem` (an image path drawn in the heart instead of its text; `""` = the text) |
| Illustrations | `pageOrnament` (an image path for the page's top-left corner, mirrored into all four; needs `pageColor`; `""` = none). Paths are relative to the panel's `index.html`; an image that fails to load is simply not drawn |
| Lines | `dimEdgeColor`, `unlockedEdgeColor`, `unlockedEdgeAlpha`, `unlockedEdgeWidth`, `edgeGlow`, `lockedEdgeColor`, `lockedEdgeAlpha`, `frontierEdgeAlpha` (edges into learnable spells; 0 = drawn as locked), `selectedPathColor`, `selectedPathAlpha`, `selectedPathWidth`, `hoverPathAlpha` |
| Drawn lines | How shapes and lines are drawn (`modules/treeStyleInk.js`), all off by default and only at the full level of detail: `handDrawn` 0-0.15 (spell shapes wobble by this share of their size, straight lines bow a little; four pre-made variants per school, no extra paint calls), `lockedEdgeStipple` (locked lines as a dot pattern from zoom 1: up to 5 a dot every 5 world units, above that every 7; 0 = solid), `edgeBreaks` (known and frontier lines broken by a dash, `"on off ..."` in screen px; `""` = none), `edgeCut` 0-1 (engraved: a stripe of page this share of `unlockedEdgeWidth` down known lines, drawn once the line is 2.2 px wide on screen), `innerLine` 0-1 (an inset outline inside known spells at this share of their size) and `innerLineColor` (`""` = the page) |
| Book | `accent` (sigil, runes, chapter titles, dividers), `selectionSigil`, `chapterTitles`, `chapterSize`, `dividerOrnament` (`reveal`, `revealMs` and `inkColor` - the ink reveal on opening - were removed and are ignored) |

`TreeStyle.DEFAULTS` in `modules/treeStyle.js` is the authoritative list with the Classic values, and
the Arcane preset in `modules/designPresets.js` is a full worked example. `nodeGlow` and `edgeGlow` are
the costly ones (a sprite per learned spell, a wide stroke per learned edge); the rest cost about the
same as Classic. Of the drawn lines, a dot or dash per few pixels of the ~1,400 locked lines would be
the costly one, which is why locked lines are stippled with a pattern and not a dash (`edgeBreaks`
applies only to the few known and frontier lines, with long dashes). See [DESIGN.md](DESIGN.md) for measurements.

Restart the game after adding a file: presets are read once, when the panel first loads its settings.

## Tips

- Start from a built-in preset and tweak from there
- Scanner presets pair with specific root modes — a Sun-based scanner preset won't look right if applied with Flat mode active
- Share your presets on the mod's Nexus page for others to try
- Back up your presets folder before updating the mod
