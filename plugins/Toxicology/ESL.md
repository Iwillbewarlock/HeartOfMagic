# HeartOfMagic_Toxicology.esp

The addon's plugin: an ESL-flagged `.esp` with four new records at fixed local FormIDs. The DLL looks them up
with `TESDataHandler::LookupForm(<local id>, "HeartOfMagic_Toxicology.esp")`, so the FormIDs below must not
change. The committed copy is `data/HeartOfMagic_Toxicology.esp`; `data/HeartOfMagic_Toxicology.json` holds the
default settings.

## Header

| Field | Value |
|---|---|
| Flags | `0x200` (ESL / light), nothing else |
| Masters | `Skyrim.esm` only |
| Author (CNAM) | Iwillbewarlock |
| Description (SNAM) | Heart of Magic Librarian - Toxicology |
| Next object ID | `0x804` |

## Records

| Local FormID | Type | EditorID | What it is |
|---|---|---|---|
| `0x800` | KYWD | `HoMTox_MimicPoison` | Keyword the DLL uses to mark its poison mimic (no fields besides the EditorID) |
| `0x801` | MGEF | `HoMTox_ToxicologyEffect` | Name "Toxicology"; archetype Script (no script attached), casting type Constant Effect, delivery Self, magic skill None, flags 0 (not Hostile, not Detrimental); description = the ability text below |
| `0x802` | SPEL | `HoMTox_ToxicologyAbility` | Name "Toxicology"; type Ability, Constant Effect, Self; one effect `0x801`, magnitude 0, area 0, duration 0 |
| `0x803` | BOOK | `HoMTox_ToxicologyTome` | Name "Spell Tome: Toxicology"; teaches spell `0x802` (flags `0x04`); value 350; other fields from the vanilla tome below |

### The tome's source

Fields copied from `SpellTomeOakflesh` (`09E2A8:Skyrim.esm`, the Skyrim.esm version, not the load-order
winner) into a new record - never an override of it:

| Field | Value |
|---|---|
| Model | `Clutter\Books\SpellTomeAlterationLowPoly.nif` with its MODT data |
| Object bounds | (-9,-11,-1) to (9,11,2) |
| Inventory art | `02FBB3:Skyrim.esm` `HighPolyAlterationBook` |
| Keywords | `VendorItemBook` `0937A2:Skyrim.esm`, `VendorItemSpellTome` `0937A5:Skyrim.esm` |
| Weight | 1 |
| Type | Book/Tome |
| Pick-up / put-down sound | none (the vanilla tome has none either) |

Set on the new record: name, value 350, the teaches-spell link to `0x802`, and the book text.

### Text

Effect description (`0x801`, DNAM):

> Poison spells inflict your first favorited poison; each poison can affect a creature only once. Every 100
> Magicka spent on poison spells uses one bottle, moving to the next when empty.

Book text (`0x803`): a centred handwritten title "Toxicology" in the vanilla tome markup
(`<font face='$HandwrittenFont'><font size='40'><p align='center'>`), then `</font><p align='left'>` and four
paragraphs separated by blank lines (`\r\n\r\n`). The second paragraph says each creature takes a given poison
only once (later strikes add no dose; a different poison can take hold once too). Both texts were drafted by
Gemini Argon from a fact sheet and checked line by line against it.

### Changes

- 2026-10-18: the effect description and the tome's second paragraph rewritten for the once-per-target-and-poison
  rule (they described renew-on-hit, which was removed). Done as a byte edit of just the `0x801` `DNAM` and the
  `0x803` `DESC` subrecords (node, no houseCARL write, so no `Update.esm` to remove), with the MGEF and BOOK record
  sizes and their group sizes recomputed; the other subrecords, the TES4 header (flags `0x200`, `Skyrim.esm`
  only, HEDR count 8 = four records and four groups, next object ID `0x804`) and FormIDs `0x800`-`0x803`
  byte-identical to the previous file. Read back with `housecarl_records` (form `everything`) from the dev mod
  folder: both texts equal the approved text.

## How it was made (houseCARL)

1. `housecarl_create_plugin` `patch="HeartOfMagic_Toxicology"`, `esl=true` - an empty ESL-flagged header. houseCARL
   put it in the mod folder `houseCARL - HeartOfMagic_Toxicology`; the folder was renamed on disk to
   `HeartOfMagic-Toxicology-Dev` (the dev mod folder) before any record was written. It is not enabled in MO2.
2. `housecarl_create` `into="HeartOfMagic_Toxicology.esp"` with the four records in one call, in the order KYWD, MGEF,
   SPEL, BOOK, so they took `0x800`-`0x803`. The spell's effect and the book's teaches link point at their
   siblings by `@<editorid>`. The MGEF archetype is a composed `MagicEffectArchetype` arm with type Script.
3. `housecarl_apply` `into="HeartOfMagic_Toxicology.esp"`, `op=CopyFrom`, `from="09E2A8:Skyrim.esm"`,
   `from_source="Skyrim.esm"` for `Model`, `ObjectBounds`, `InventoryArt`, `Keywords`, `Weight` and `Type` on `0x803`.
4. houseCARL always writes `Update.esm` as a second master next to `Skyrim.esm`, though no record references it.
   It was removed by hand (PowerShell, no other tool): the `MAST Update.esm` + `DATA` pair (31 bytes) cut from the
   TES4 header, the header size lowered by 31, and the six self FormIDs re-indexed from `0x02xxxxxx` to `0x01xxxxxx`
   (the four record headers, the spell's `EFID` and the book's teaches-spell field in `DATA`). Before the edit the
   file was checked for exactly those six self references and no other `0x01`-indexed FormID.
   **If the plugin is ever extended with houseCARL again, it will add `Update.esm` back; repeat this step.**
5. Read back with `housecarl_records` `source={"file":"HeartOfMagic_Toxicology.esp","mod":"HeartOfMagic-Toxicology-Dev"}`
   on the four FormIDs (every field above), and the header (flags `0x200`, one master) read from the file bytes.
6. Copied the file to `plugins/Toxicology/data/` (same SHA-256 as the dev mod folder's copy).
