# Changelog

## Heart of Magic Librarian 1.2.1 (unreleased)

### New

- Tighter grouping by type when the tree is built (asked for on Nexus). Spells of one kind - bound weapons,
  runes, cloaks - now look for a parent of the same kind, rarer kinds and elements weigh more than common
  ones, and a spell made of one element is kept off a branch of another (fire, frost and shock stay apart).
  On a 3,500-spell load order three in four bound weapons now hang off another bound weapon (up from under
  three in five). The strength is a new setting, **Type grouping** in the Classic Growth Settings on the
  scan page (0 to 2, default 1); 0 builds the tree exactly as 1.2.0 did. Build the tree again to see it.
- Tiers by Magicka cost (asked for on Nexus): a new checkbox under Type grouping builds each school's five
  tiers by how much the spells cost, cheapest first, instead of Novice to Master. Spells with no cost stay at
  their level's tier, and a spell's level itself (XP needed, the spell card) does not change. Off by default.
  The cross-school links follow the same tiers. Scanner presets save both settings (an older preset sets the
  defaults).
- The in-game corner messages (spell learned, power increased, mastered, tome study, the weakened spell
  alert, the Immersive Spell Learning patch's study messages, BookXP) now follow the panel's language
  instead of always being English: they come from the language files' new `hud.*` keys (all 13 languages;
  a missing key shows English). Changing the language in Settings changes them at once. Skill names in
  "You lack the ... skill" are the game's own. Modders: `SpellLearning.NotificationText` (Papyrus) and
  `ShowNotification` (C++ API version 2) give the same for their own messages.
- Spell Research gets exactly the spells the tree builder takes: no more spells whose tomes cannot be found,
  and your spell blacklist and the plugins you turned off apply there too. Press Import Spells in Spell
  Research once to apply it to a save that already imported.
- Spells that cost nothing to cast stay out of the tree and of Spell Research: pet, mount and follower calls
  (Witcher Horses, the Creation Club pets, Inigo), a mod's utility spells, Equilibrium. A tree you already have
  drops them the next time it loads - no rebuild needed - and drops any node that is no longer a named spell.
- A spell gate picks the tree's spells more tightly, and Spell Research gets the same set. Left out on their
  own: summons that last over an hour (mounts and companions that stay), spells that only dismiss a mount, and
  an adult framework's detect and script spells (a rules file, `gate/rules.json`, by the plugin's masters).
  Spells it is unsure of - a horse summon, a summon of one named character, a cheap spell that only runs a
  script - stay in the tree and are listed under **Spells to look over** in the Blacklist window with the
  reason: Leave out puts one on the blacklist, Keep takes it off the list.
- After a load order change, a saved tree could show a nameless "Constant Effect" spell (or another mod's
  spell) where one of yours was: its saved ID now pointed at another plugin's record. The tree now always
  finds each spell by its plugin and record, and its prerequisite lists follow too.
- **XP Alerts** (Settings > Early Learning, on by default) turns the corner messages about XP and power off:
  casting a weakened spell, power steps, studying a tome, book XP. Learned, mastered, blocked tomes and the
  Spell Research line always show. Weakened Spell Alerts stays as an option under it and is greyed out while it
  is off. Reset to Defaults turns it back on; settings presets do not change it.
- A copy of a spell you learned before the copies were merged (another plugin's record of the same spell)
  now counts as that spell: its children open, and Heart of Magic does not give you the tree's record on top
  of it.

- **Skill XP from Studying** (Settings > Spell Tomes, on by default): with the Immersive Spell Learning
  patch, turn it off and studying a tome no longer raises the school's skill, so a skill mod (Static Skill
  Leveling and the like) handles skills alone. Needs the patch's updated script (optional/ISLPatch). A patch
  script installed with an older Heart of Magic script (the new functions missing) studies and shows its
  messages as before, in English, instead of losing the skill gain and the messages.
- SpellLearning.log shows where XP comes from: every tome, study, book and other-mod grant is one line (how
  much, to which spell, from where), and every cast that earned XP is one line (the spell, cast event or
  immediate cast hook, the total XP and how many learning targets got it). The per-target detail and
  "duplicate skipped" for a cast already counted the other way are in the debug log.
- The tree's tool bar (zoom, home, Find Spell, render settings, edit) can be made bigger: drag the small
  grip at its bottom-right corner (1x to 3x, double-click for 1x). The size is kept with the settings and
  Reset to Defaults puts it back. A bar wider than the tree wraps, and the school tabs move down with it.
- Spell Research compatibility: with Spell Research loaded, the spells your mods' tomes teach that Spell
  Research does not know yet are added to its research lists after its Import and at every save load, so
  they turn up in research, spell composing and tome copying. Like the tree, it leaves out voice slot spells
  and a second plugin's copy of the same spell (each spell is listed once). Switch: `spellResearch.enabled`
  in config.json (on by default).

### Changed

- Find Spell opens as a dropdown under the magnifier button instead of a window over the tree; the button
  shows and hides it, and Esc or choosing a spell hides it. The F key no longer opens it. The search ignores
  spaces ("헝거소환" finds "헝거 소환"), an exact name comes first and then names that start with the text,
  and "Found: ..." after the jump is in the panel's language. A school's name in the panel's language
  finds its spells too and is what the list shows; a formId is matched only for text that looks like one
  (0x... or four or more hex digits), after the name matches, so "fe" no longer lists every ESL spell.
  Switching tabs, closing the panel or loading a tree hides it, and the highlight ignores spaces like the
  search.
- A spell with effects of two schools now earns cast XP at the rate of its tree school (its first effect's),
  the school it is listed under, instead of its costliest effect's. So for those spells the School, Direct and
  Any cast XP rates can come out differently from 1.2.0 (a cast now counts as "same school" for the targets of
  its first effect's school).
- Modders: `SpellLearning_SpellMastered` and `SpellLearning_SpellEarlyGranted` name the school the spell is the
  learning target of (the one `SpellLearning_TargetChanged` named), not its costliest effect's.
  `SpellLearning_TargetChanged` also fires (0) for a school that loses the spell because it was set under
  another school, and for each school changed when a save from before is put right on load.

### Fixed

- Spells no longer earn double XP with mods that cast them a second way (SpellHotbar 2, Papyrus Spell.Cast):
  each cast is counted once however the game reports it. Before, two quick casts reported both ways could
  count three times, and a busy frame could let one cast count twice.
- A spell set as the learning target from the panel and again from its tome no longer gets XP twice per
  cast: a spell is the target of one school only, and a save that already has it under two is put right
  when it loads.
- A spell with effects of two schools that was set as the learning target from its tome kept getting XP
  but the panel neither showed it as the target nor could stop it: the tome filed it under its costliest
  effect's school, the panel under the tree's (the first effect's). Both use the tree's school now, the
  panel's Stop Learning clears the spell whatever school holds it, and a save that has it under the other
  school is moved over when it loads.
- Two casts of one spell, one by hand and one through SpellHotbar 2 or Spell.Cast, less than 0.3 s apart
  could count once: one cast's two reports are now paired only within 0.1 s.
- In discovery mode, Find Spell used before the tree was drawn could list locked spells; it lists nothing
  until the tree is there.
- Dragging the tool bar's size grip no longer sticks to the mouse when the button is let go outside the
  panel.
- Weakened Spell Alerts and its interval were never saved: the game always used on / 10 s whatever the panel
  showed, and Reset to Defaults and settings presets left the controls as they were. They are saved now,
  reset, and follow a preset.

- Bigger default text: the panel's text is 16px at Font Size 1.0x (was 14px) and the tree's spell names 12
  (was 10). Saved settings keep their values; Reset to Defaults gives the new sizes, and its sliders and
  the text change at once.
- Settings > Font Size goes up to 2.0x (it stopped at 1.5x), and it now also enlarges the spell names on
  the tree (on top of Spell Name Size). The spell card grows with the text: the side panel gets wider and
  the bottom bar taller, so nothing is cut off at the larger sizes.
- Editing prerequisites in edit mode works as shown again. A new link showed up as both Required and
  Optional, deleting one deleted both, and switching one between Required and Optional left a second copy.
  A built tree used one list for two things, so every change to one went into the other. A link is now
  Required only, delete removes just that one, H/S moves it, and undo puts it back the way it was. A tree
  saved with the old mix-up is put right when it loads: a link listed as both stays Required, copies go,
  and the fix is saved with the tree. A PreReq Master lock on a spell's parent is not taken for that
  mix-up: the spell's other optional links (a bridge) stay optional. Switching a Required link to Optional
  when the spell had no Optional ones now needs that one, instead of needing none.
- Deleting a spell in edit mode takes it out of every spell that needed it - its children, a PreReq Master
  lock on it, a cross-school link from it - so none of them waits on a spell that is gone after a reload.
  Ctrl+Z after it no longer links a spell back to the deleted one, and a school's root spell is not
  deleted (the status line says why).
  A spell whose Optional links need none of them says so on the card instead of "need 0 of 2".
- One spell is no longer in the tree twice (or three times) when two mods carry it, such as Saints and
  Seducers' Conjure Hunger and ECSS Summonable Creatures' version of it. Records with the same editor ID in
  the same school count as one spell: the tree keeps the one from the earlier plugin in the load order (or
  the one whose tome can be found), and reading another one's tome counts as reading the tome of the one in your tree -
  same learning target, tome XP and early learning, and the other copy is not added to the spellbook. A
  spell that is in your tree is always taught by its own tome, even when a mod added later carries the same
  spell. Spells
  that only share a name (in some translations different spells do) stay apart. Build the tree again (Scan,
  then build) to take the copies out of it. Copies already in a save's spellbook stay there; the mod does
  not remove spells. Needs editor IDs (po3's Tweaks); without them nothing is merged. In vanilla mode (without
  ISL/DEST) a tome still teaches the spell it names.

## Heart of Magic Librarian 1.2.0

### New

- Perk overhauls recognise modded spells. Perk mods find spells by keywords on their effects (Augmented
  Flames asks for MagicDamageFire), and a spell mod that left them out got no perk bonuses without a patch
  for each pair of mods. Heart of Magic now adds those keywords itself, in memory, to the spells the
  librarian has tagged, following one adapter file per perk mod: vanilla keywords (read by every perk
  overhaul), Adamant/Mysticism, Path of Sorcery; Vokrii and Ordinator need nothing beyond the vanilla ones.
  A keyword goes on only when the librarian's tag and the effect's own record agree, never on an effect
  another item shares unless that item qualifies too, never on a perk-bonus effect, and never into a scan.
  It runs at game start and after each full scan; `perk_adapters_report.json` lists what it added, and
  `"perkAdapters": { "enabled": false }` in config.json turns it off from the next game start or scan.
  Nothing is written to plugins or saves. Where a perk asks a spell list rather than a keyword, the
  modded spells that fit go into that list too (Adamant's concentration fire/shock lists, Vokrii's Grand
  Conjurer list; lists are filled at game start, a scan in play updates them at the next start). Summons are sorted by what they summon: an undead creature counts as an undead summon,
  an animal as a familiar, before the librarian's tags are asked.
  Ascension (1, 2 and the Adamant 6 patch) now recognises unaspected Destruction damage from any spell mod:
  arcane, shadow, blood, necrotic or nature damage that is not fire, frost, shock, poison, sun or a
  vampire drain. The adapter names Ascension's keyword by FormID, as Apostasy Framework gives the same
  keyword another name.
  Paragon (with its Magic Addon) is covered by the same files: it reads no keyword of its own for spells, so
  the Adamant/Mysticism and Ascension files now also turn on when ParagonPerks.esp is loaded.

- Spells from the same mod now sit together in the tree: the builder gives a parent from the child's own
  mod a bonus (below the one for a shared element, so a mod's fire spell still goes to fire first). On a
  3,500-spell load order two in three mod spells now hang off a spell of their own mod, up from under half.
  Rebuild the tree to see it.

- The spell card's level can be pressed like a keyword: it lights every spell of that level in the tree,
  in every school. Press it again, or the pill above the tree, to clear it.

- The debug symbols download also switches on a scan dump: with it installed, the panel's Scan writes
  `spell_scan_output.json` next to the catalog (the file bug reports and the offline tools use), as the
  Papyrus `RunScan` already did. The main download does not write it.

### Fixed
- Escape closes the panel again in game (with a spell selected, the first press drops the selection).
  The panel's page did not always receive the key; the plugin now passes Escape on from the game's input.

- The scan tab's Easy page no longer shows "No presets yet" when there are no tree builder presets; the
  preset section is simply left out and the build uses the current settings.
- Hidden helper effects no longer give spells wrong element tags. A mod's screen shake or stagger push that
  rides inside a spell (Mysticism's screen shake carries frost resistance, so Bane of the Undead and Mass
  Fear were tagged frost) now counts only when it deals real harm. What a cloak, a hazard or an explosion
  puts on others counts instead: the scan records it (`applies` on the effect), and a cloak's element comes
  from the spell it casts. Rescan to see the new tags.
- Tags follow what the game's and the mods' own keywords were made to say. Summons are no longer tagged
  creature (Bethesda's familiar keyword is on every summon that is not an atronach, the Dremora Lord
  included), and magic damage with no element is tagged arcane instead of resistance. Vampiric magic is
  blood magic (not undead) whoever casts it, while a blood or vampire mod's plain lightning bolt is no
  longer blood just because of the mod's name. Blue flames are fire, not frost; Soul Trap is not a trap;
  keywords that tell NPCs whom to cast at no longer tag spells undead.
- Walls, storms and other hazards a spell leaves standing are tagged construct, not trap (a trap is set to
  go off later, like a rune); speed buffs no longer count as stamina; wards carry force. A librarian rule
  with a condition the rules cannot read is now skipped with a warning instead of matching every spell.
- Summons are tagged by what they call up (the summoned actor's race: daedra, undead, ghost, dwarven
  machine), and spells that only take hold on the undead, daedra, people, animals or machines are tagged
  by that target. Rescan to see the new tags.
- Turn Undead spells are no longer tagged as aimed at creatures and people, Fury and Paralysis no longer
  carry courage and calm tags from the helper effects inside them, and raised corpses count people too.
- The scan page's "Tomes only" switch is gone: the tree is always built from spells a tome teaches, as it
  was with the switch on (its default). If you had turned it off, quest, perk and script spells no tome
  teaches leave the tree at the next build; they still get the perk overhaul keywords.
- Spells equipped in the voice slot are left out of the tree even when a tome teaches them, and their tome
  teaches them the vanilla way: they are mods' script and animation spells (Smooth Animation's ChargeEffect),
  and one learned would take the shout's place. Rescan and rebuild.
- Spells whose tomes nothing in your load order hands out - no copy placed, sold, dropped, crafted or given by
  a quest, script or distribution config - are left out of the tree: no tome of theirs can be found, so the
  tree could not offer them (a quest or perk that grants such a spell still does). The plugin reads your
  plugin files once in the background after the game starts to find them (a panel scan in those seconds
  waits for it); a damaged plugin only keeps its own tomes and its masters' in the tree.
- A spell's tier now follows its half-cost perk (the Novice to Master perk the game discounts it with), as
  it was meant to. It never did: the perk step read a name the engine does not keep, so every spell was
  tiered by its first effect's minimum skill, and a Master spell with a 0 there counted as Novice. On the
  author's load order 276 spells change tier (30 Master spells were Novice). The tier sets the spell's
  place in the tree and the XP it needs, so scan and rebuild the tree after updating: until then the panel
  still shows the old tier and sends that tier's XP for a learning target. After the rebuild, a spell
  already being learned keeps its percentage (a save stores progress as a percentage) and the XP left
  follows its new tier, unless you set that spell's XP yourself. Passive learning's per-tier caps follow
  the new tier at once. A mod's own tier perk is now read only from the end of its name (a tier word,
  or a school followed by 00/25/50/75/100) and needs po3 Tweaks.

## Heart of Magic Librarian 1.1.0

Skyrim AE 1.7 support, and the fixes that were ready as 1.0.2 (never released on its own).

### New

- Support for Skyrim AE 1.7.99 and 1.7.104. One set of DLLs now runs on SE 1.5.97, AE 1.6.x and AE 1.7.x;
  on 1.7 use the Address Library version that includes it. 1.7 support is built and checked offline only,
  not yet run in game.
- The spell tome hook (reading a tome gives XP and keeps the book) checks more of the game code before it
  patches. When a check fails, the hook stays out and tomes work the vanilla way (reading teaches the spell
  and uses up the book); the log says which check failed.
  - SE 1.5.97, AE 1.6.318 and 1.6.1170 (tested in game): as before (on AE a mismatch in the new checks only
    logs a warning) - unless another mod has already hooked the same call, in which case the hook stays out
    instead of removing that mod's hook.
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
  settings changed in the panel apply but are not written to the file (the panel's status bar says so), your
  panel language is kept, and the file is read again at the next game start. A setting of
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
