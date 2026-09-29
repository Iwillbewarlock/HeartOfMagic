# Changelog

## Heart of Magic Librarian 1.1.0

Skyrim AE 1.7 support, and the fixes that were ready as 1.0.2 (never released on its own).

### New

- Support for Skyrim AE 1.7.99 and 1.7.104. One set of DLLs now runs on SE 1.5.97, AE 1.6.x and AE 1.7.x;
  on 1.7 use the Address Library version that includes it. 1.7 support is built and checked offline only,
  not yet run in game.
- The spell tome hook (reading a tome gives XP and keeps the book) checks more of the game code before it
  patches. When a check fails, the hook stays out and tomes work the vanilla way (reading teaches the spell
  and uses up the book); the log says which check failed.
  - SE 1.5.97, AE 1.6.318 and 1.6.1170 (tested in game): as before. A mismatch in the new checks only logs a
    warning - unless another mod has already hooked the same call, in which case the hook stays out instead
    of removing that mod's hook.
  - AE 1.7.104: measured against the game files offline. AE 1.7.99: not measured; the hook goes in only if
    every check passes.
  - Other AE 1.6.x builds: the stricter checks may keep the hook out where 1.0.1 put it in.

### Fixed

The same kind of bug as the 1.0.1 scan fix: text Windows keeps in the old code page stopped a feature
without a word.

- Reading a spell tome now starts learning for spells whose name has letters outside plain English:
  "Heilende Hände", "Éclair", any Russian name. The tome set the learning target, then the rest was
  skipped - no XP, no "You begin to study..." message, no update in the panel, and with ISL/DEST the
  study never began.
- A load order with a plugin whose file name is in Korean, Chinese or Japanese can be scanned again, and
  spell cards fill in. Before, the whole scan failed and cards stayed empty. Unusual editor IDs no longer
  stop the scan either. Trees and tags of plugins with plain English file names are unaffected.
- Any other odd text that still reaches the panel shows as `�` instead of stopping what was being sent.
- A `config.json` that is not valid JSON (a typo from hand-editing, a cut-off write) is no longer replaced
  with the defaults and lost. It is kept as `config.json.broken` (then `.broken-2`, ...) in
  `SKSE/Plugins/SpellLearning/`, and your settings come back from `config.json.bak`, the last good save,
  which stays as it is. Only when that backup is missing or broken too is a fresh `config.json` made with the
  defaults, and saves in that session no longer replace `config.json.bak`. A `config.json` that cannot be
  opened at all (held by an antivirus or a sync tool) is left alone: the defaults apply for that session,
  settings changed in the panel are not saved until the file can be read again (the panel's status bar says
  so), your panel language is kept, and the file is read again the next time the settings load. A setting of
  the wrong type (a hotkey written as text) now falls back to its default instead of skipping every setting
  after it.
- The game no longer fails at startup because of Heart of Magic, SL_BookXP or the DEST shim when the
  Documents folder path has characters the Windows code page cannot hold (a user name in Korean on an
  English Windows, an emoji). Setting up the log file threw an error while SKSE loaded the plugin, and
  nothing caught it - most likely a crash on startup. The log file is now opened another way and is still
  written in `Documents/My Games/Skyrim Special Edition/SKSE/`; if even that fails, the plugin runs without
  one.
- For other SKSE plugins: the C++ API calls that change progress (`AddSourcedXP`, `AddRawXP`, `SetSpellXP`,
  `SetLearningTarget`, `ClearLearningTarget`, `RegisterXPSource`) catch an error inside Heart of Magic, log
  it and answer 0, false or nothing, instead of letting it into the calling plugin. The read-only calls are
  unchanged.

### License

- The DLLs now statically link CommonLibSSE-NG, which is GPL-3.0-or-later (with its Modding and Linking
  exceptions), so they are distributed as a GPL-3.0-or-later combined work.
- Heart of Magic's own source stays MIT.
- The license texts and `THIRD-PARTY-NOTICES.md` (every library in the DLLs and its license) are in the
  download.
- The complete source is on GitHub: github.com/Iwillbewarlock/HeartOfMagic, tag `v1.1.0`.
- The optional ISL patch credits its authors correctly: its script is based on Immersive Spell Learning -
  DESTified by Hackfield (who credits Ameisenfutter's original Immersive Spell Learning and Parapets' Don't Eat
  Spell Tomes), with the sit-down study animations from I Just Want to Sit Down and Read by GiraPomba. Its Gate
  to Sovngarde CE Bookworm trait support is compatibility code only; no GTS CE files are included.

## Heart of Magic Librarian 1.0.1

### Fixed

- A scan no longer stops on a file name the Windows code page cannot hold. The scan read every file in Data
  for SPID ini files and threw "No mapping for the Unicode character exists in the target multi-byte code page"
  on a name like that, and the Scan button stayed on "Scanning..." with nothing to say why. Names are read as
  UTF-8 now, and a scan that fails for any reason gives the button back and shows "Scan failed: <reason>" (a failed background tome scan only puts the scan message back and is logged; the tome list from an earlier scan stays in use).
- Presets (settings and scanner) with a non-English name (Korean, for example) are saved, listed and deleted
  under their own name. An English Windows usually garbled the file name, and a DBCS code page could make the
  save fail without a word. Deleting a preset with a non-English name also finds the file an older build saved
  under a garbled name (if that preset was saved again with this build first, delete it twice).
- A reason text with `$&` or `$1` in it is shown as written (the panel's language strings expanded them).

## Heart of Magic Librarian 1.0.0

A rebuild of Heart of Magic by Dinkel Zombie. Compared with Heart of Magic v2.5 ("depythoned", Nexus,
March 2026). Requirements are unchanged: SKSE64, Address Library, PrismaUI, Skyrim SE 1.5.97 or AE.
Still no ESP, no Python, no API key.

### New

- **Known Higher Spells** (Settings > Progression, on by default): knowing a higher spell opens the
  spells directly below it for a share of their XP (Novice 30%, Apprentice 40%, Adept 50%, Expert 70%,
  Master 80%). Optional: open the whole chain down to the root, and give these spells their own XP rates.
  Works in the tree, with tomes and with ISL/DEST.
- **Spell tags**: every scan tags each spell with its element (fire, frost, blood, water, holy,
  shadow...) and technique (summoning, siphon, curse...), from rule files in
  `SKSE/Plugins/SpellLearning/librarian/`. Spells of the same element now grow on the same branch.
  Extra rules switch on by themselves when keyword frameworks (ADAR, KIT, NSV, OCF) are installed.
- **Grouping that works in any language**: branches come from scan data and editor IDs first, then from
  names, so Korean, Chinese or other translated games get the same tree as English.
- **Paths to other schools**: related spells in different schools (a fire spell and a fire summon) are
  linked. A link is an extra, optional way to unlock a spell, never an added requirement. Linked spells
  wear a ring; hover or select one to see its links, and click a link to travel there.
- **Tidy trees**: spells are moved off each other, off the lines and off the heart when a tree is built.
- **Spell card redesign**: icon (from an installed icon set, else the school emblem), keyword chips,
  a description with the real numbers filled in, and the figures, revealed as progress grows. Click a
  keyword chip to light every spell with that keyword.
- **Designs**: one Design setting (Settings > UI Display) sets the whole look - Classic (the old look),
  Modern Dark, Arcane (a parchment spellbook), Night Grimoire (indigo vellum, gold engraving) and
  Chalkboard (slate and pastel chalk), each with its own drawn lines and book or chalk fonts per language.
  Mods can add a design with one JSON file in `presets/design/`.
- **Camera and navigation**: clicking a spell glides to it (Center on Click, Zoom on Click, Focus Zoom,
  Rotate Wheel, Dim Others), school tabs, Home and search buttons.
- **Preview on Hover**: resting the cursor on a spell shows its full card.
- **Language picker** in Settings > UI Display (no more editing `lang/locale.js`); switching rewrites the
  panel's texts and status lines in the new language right away.
- **Same-name spells**: when two spells in the tree share a name (a mod's copy of a vanilla spell), the
  card's lists (Unlocks, prerequisites, locks, paths to other schools) show each one's plugin.
- **Render popup** rebuilt: a "Still everything" switch and one chip per moving part (heart pulse,
  particles, stars, sigil, glow, runes, page, drawn lines...).
- For modders: Papyrus `SpellLearning.RunScan()` writes a tagged scan to `spell_scan_output.json`.

### Changed

- **Smoother panel**: the tree is drawn once and reused - dragging draws only the newly uncovered
  edge, big repaints are spread over frames, hovering and XP gains no longer repaint the tree, and
  opening the panel repaints only if something changed. Animations run at about 12 fps and stop when
  the panel is left alone.
- **XP**: casting a spell that leads to your current learning target always counts as direct XP,
  whoever set the target (a tome, Papyrus, another mod). Passive learning uses the spell's real tier.
  Required XP is right straight after loading a save.
- Spell details sit in a bar under the tree by default (the side panel is still a setting).
- **Esc** is two steps: first it drops the selected spell, then it closes the panel.
- The scanner's **Tomes only** filter is on by default.
- The design now decides colours and sizes; the render popup's fine controls are gone.
- Every one of the 13 languages has the full text.
- Faster casting: the "tome in inventory" bonus check is cached.

### Removed

- The Tree, Graph, Oracle and Thematic build modes. Classic is the only builder.
- LLM (OpenRouter) tree generation and its API key setting (its connection did not check certificates).
- The UI Theme dropdown (replaced by Design).
- The SkyrimNet bridge scripts. They never worked in v2.5: nothing ever sent them a request.

### Fixed

- Tree prerequisites were forgotten after every save load, so for the rest of that session any spell
  could be learned from a tome or through ISL/DEST, prerequisites or not.
- Early-learned (weakened) spells went back to full power on every load.
- "Spell (Learning - 20%) (Learning - 20%)" after loading another save.
- A mastered spell sharing an effect with one still being learned showed the weakened description.
- Spells dropped out of the tree after a load-order change (trees must be rebuilt once - see below).
- Cheat-mode Relock did not last; learning targets were sometimes not cleared on mastery.
- A possible crash when a cast finished learning a spell.
- The spell tome hook could patch the wrong code when another mod had patched the same spot. It now
  patches only where it expects to and otherwise stays out (tomes then work the vanilla way).
- Casting, Papyrus calls and ISL study no longer change progress off the game thread.
- `spell_tree.json` and `config.json` are written safely with a `.bak` copy; saving settings no longer
  stalls the game; a damaged tree file is reported instead of silently not loading.
- A click no longer nudges the tree; Esc in Find Spell no longer closes the whole panel; Retry after a
  failed build keeps your blacklist and filters; the Build button is disabled while building.
- Untranslated buttons and status text.
- The spell card's Magicka cost is a whole number (it showed the game's raw decimal).
- A spell listed as its own prerequisite in a saved or hand-edited tree no longer locks it for good.

### Upgrading from v2.5

- **Replace, do not merge**: the DLL names are the same, so disable or remove Heart of Magic v2.5 before
  installing. Mod managers will show a lower version number (1.0.0) because this is a new mod.
- Saves, co-saves, `config.json` and `spell_tree.json` carry over.
- **Rescan and rebuild your tree once** to get tags, links and load-order-safe spell IDs. Presets that
  used a removed mode build with Classic.
- Known Higher Spells is on: a character who already knows higher spells will find lower ones open.
- Early-learned spells show as weakened again (v2.5 had been losing that).
- You open to the Arcane design (or Modern Dark if you used it); Classic is the old look. Custom render
  colours and star settings are reset once.
- If you had set an OpenRouter key, delete `SKSE/Plugins/SpellLearning/openrouter_config.json` and the
  `"llm"` block in `config.json`; nothing reads them any more.
- The tome hook is verified on SE 1.5.97 and AE 1.6.318 / 1.6.1170. If `SpellLearning.log` says
  "SpellTomeHook failed to install", tomes work the vanilla way.
- The card's icons need an icon set (for example Kome's Inventory Tweaks).
