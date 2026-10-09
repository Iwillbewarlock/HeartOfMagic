# Sorcerer / Staff Enchanting Plus vs. a Heart of Magic scroll+staff addon

## 1. How Sorcerer crafts scrolls/staves (evidence: Sorcerer.esp record walk)
Sorcerer.esp (masters incl. MysticismMagic.esp) has: BOOK 244, COBJ 1068, PERK 248, QUST 1 (MAG_SorcererControllerQuest, VMAD), no SCRL/SPEL-per-scroll records.
EDID counts: MAG_RecipeScroll* 243, MAG_ResearchNotes* 243 (BOOK), MAG_ResearchPerk* 243, MAG_RecipeStaff* 149, MAG_BreakdownRecipe* ~670.
- Scrolls: NOT runtime-created. The SCRL forms are Mysticism's (pre-made, one per spell). Sorcerer adds, per scroll: a COBJ recipe (MAG_RecipeScroll<Name>), a "research notes" BOOK, a PERK that gates the recipe, and a breakdown COBJ (deconstructing a found scroll gives the research item, which "fakes" learning). Gating = research via deconstruct, not "known spell".
- Staves: pre-made WEAP staff records (Mysticism/Praedystaves) + 149 recipe COBJs (COBJ CNAM=created staff, conditions on the research perk).
- Mod text itself: "Out of the box ... only Mysticism spells. Creating patches for spell mods is straightforward but somewhat tedious"; a Synthesis patcher generates the recipes/notes/perks per spell mod, plus hand-made pregenerated patches (Enai, Dareni) on Nexus 95212.
- Other pieces: CraftingCategories + COCKS menus (SKSE/Plugins/CraftingCategories/Sorcerer.json), InventoryInjector json.

## 2. Per-pack Praedystaves patch (Natura example)
"Sorcerer - Praedystaves - Natura.esp": masters Skyrim, Praedy's StavesAIO, Natura, Sorcerer; contains TES4 + 48 WEAP only (EDIDs NAT_RestorationStaffLocustPlague ...). Abyss: 5 KYWD + 14 WEAP. So yes: pre-made staff records per spell, hand/tool-authored per pack (just keyword/template edits on existing staves; the recipes still need the Synthesis patch from 95212, per the patch's own description).

## 3. Staff Enchanting Plus (Nexus 132680 v1.1.3; source github.com/Exit-9B/StaffEnchanting, read directly)
NOT installed in TAKEALOOK (no folder). It is an SKSE DLL that replaces the Dragonborn staff-enchanter menu (hooks TESObjectREFR::ActivateCraftingWorkbench, furniture keyword DLC2StaffEnchanter 0x17738).
- Spell list (StaffCraftingMenu.cpp ~l.224-240): player's known spells = actorEffects->spells + playerRef->addedSpells, filtered by IsSpellValid: not self-targeted (INI bAllowSelfTargeted=0), cost >= 1, equip slot either-hand, no ritual effects, JSON exclusions/whitelist; optional bUseSpellTomes=0 lists inventory tomes' spells.
- Form creation: runtime, via the ENGINE's created-object system: copies the spell's Effects into BSTArray<Effect>, then BGSCreatedObjectManager::AddWeaponEnchantment(effects) (same path as vanilla arcane-enchanter), Create.cpp hooks MagicItemCreationHelpers::CreateNewEnchantment to set staff enchantment type/casting/delivery from the first effect. The result is a normal player-enchanted weapon (unenchanted staff + created EnchantmentItem), saved natively by the engine in the save file's created-object tables. No per-spell patches. Charge from morpholith count (spell level+1) or soul value; name "Staff of %s".
- Config surface: Data/SKSE/Plugins/StaffEnchanting/*.json {"whitelist":[...],"exclusions":["Plugin.esp|0xID"]}, read once at startup (JSONSettings.cpp). Official patch for Sorcerer ships in its FOMOD (data/Patches/Sorcerer).
- Scrolls: NOT supported at all (staff only).

## 4. Design sketch: HoM mastery-gated craft of scrolls/staves for any spell
Staves: easiest and proven. Either (a) don't build; feed Staff Enchanting Plus a mastery filter (see 5), or (b) own bench/menu reusing the same engine calls: AddWeaponEnchantment (RE/B/BGSCreatedObjectManager.h; DestroyEnchantment(ench,true) for cancel) + SetEnchantment on the staff's ExtraDataList. Persistence: engine handles it. Gate: ISpellLearningAPI::IsSpellMastered(formID) (plugins/SpellLearningAPI.h l.85). Note HoM: a known copy counts as mastered (docs/PLAN-PUBLIC-MODDER-API.md l.113).
Scrolls: harder. RE::ScrollItem is a TESBoundObject/MagicItem; IFormFactory::Create<T>() (RE/I/IFormFactory.h l.54) gives a dynamic form (ID >= 0xFF000000, TESForm::IsDynamicForm). Pitfalls (UNVERIFIED by test, from engine knowledge):
- The engine only serializes created objects via BGSCreatedObjectManager (weapon/armor enchantments, potions, poisons). A bare dynamic ScrollItem is not saved: after load the FormID in inventories dangles/vanishes. You'd need your own co-save record (spell FormID + scroll params), recreate on load, and re-map inventory entries; or use a library that does this (powerofthree's dynamic-form handling; I did not check it).
- Dynamic forms also need InitItemImpl/ cost/ chargeTime/ effects copy (ScrollItem::CopyMagicItemData), keywords (VendorItemScroll), model/ICON/ name, and the skill-usage XP path; mod conflicts with Mysticism's scroll perk overhaul.
- Safer alternative: pre-generate SCRL records offline from spell_catalog.json (as Sorcerer's Synthesis does) into a generated ESP; mastery gate by COBJ condition (a Papyrus/GLOB or a custom condition function from our DLL) - no runtime form risk but a rebuild per load order (HoM already scans the load order, but writing an ESP at runtime is not possible; needs an external tool step).

## 5. Verdict
Not already covered as a whole: Sorcerer = per-spell pre-made recipes (Mysticism-centric, patch tool per mod) and no mastery gate; Staff Enchanting Plus = any spell, but staves only and gate = "player knows the spell" (known != mastered in HoM terms; in HoM a spell can be known as a weakened copy).
Recommendation: do NOT write a full crafting addon first. Cheapest high-value route: an INTEGRATION that gives Staff Enchanting Plus a mastery filter. Its only hooks are static JSON exclusions at startup and the known-spell set; HoM can (1) generate "exclusions" JSON for every non-mastered spell on game start (stale during play: spells mastered later stay excluded until restart - limit), or (2) propose a small upstream PR/our own fork adding a runtime predicate/API. For scrolls, an addon is only worthwhile if someone commits to the dynamic-form persistence work; Sorcerer's gate is research perk+COBJ conditions, so a HoM-provided condition function (e.g. "HoM_IsMastered(spell)") on those COBJ/perks would be the Sorcerer-side integration, but Sorcerer has no per-spell hook for arbitrary spells without its Synthesis patch.

## Not verified
- No in-game test, no runtime form creation tested; the co-save/dangling-ID behaviour of dynamic ScrollItem is from knowledge, not from code I read.
- Did not decompile MAG_SorcererControllerQuest scripts or read COBJ condition function semantics (only saw CTDA bytes; 0x2F=GetItemCount in breakdown recipes).
- Staff Enchanting Plus not installed locally; analysis is from its public GitHub source at main (may differ slightly from v1.1.3).
- Did not open Mysticism's esp to confirm scroll records (inferred: Sorcerer has zero SCRL and lists MysticismMagic.esp as master).
- Did not check HoM Papyrus API for a mastered check (C++ API only seen).
