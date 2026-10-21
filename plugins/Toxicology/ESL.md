# HeartOfMagic_Toxicology.esp

The addon's plugin: an ESL-flagged `.esp` with eight new records at fixed local FormIDs. The DLL looks them up
with `TESDataHandler::LookupForm(<local id>, "HeartOfMagic_Toxicology.esp")`, so the FormIDs below must not
change. The committed copy is `data/HeartOfMagic_Toxicology.esp`; `data/HeartOfMagic_Toxicology.json` holds the
default settings. The Korean copy is `data/ko/HeartOfMagic_Toxicology.esp` (section "Korean ESL").

## Header

| Field | Value |
|---|---|
| Flags | `0x200` (ESL / light), nothing else |
| Masters | `Skyrim.esm` only |
| Author (CNAM) | Iwillbewarlock |
| Description (SNAM) | Heart of Magic Librarian - Toxicology |
| Next object ID | `0x808` |
| HEDR record count | 13 (eight records and five groups; the count includes the groups) |

## Records

| Local FormID | Type | EditorID | What it is |
|---|---|---|---|
| `0x800` | KYWD | `HoMTox_MimicPoison` | Keyword the DLL uses to mark its poison mimic (no fields besides the EditorID) |
| `0x801` | MGEF | `HoMTox_ToxicologyEffect` | Name "Toxicology"; archetype Script (no script attached), casting type Constant Effect, delivery Self, magic skill None, flags 0 (not Hostile, not Detrimental); description = the ability text below |
| `0x802` | SPEL | `HoMTox_ToxicologyAbility` | Name "Toxicology"; type Ability, Constant Effect, Self; one effect `0x801`, magnitude 0, area 0, duration 0. Having it = Toxicology on |
| `0x803` | BOOK | `HoMTox_ToxicologyTome` | Name "Spell Tome: Toxicology"; teaches spell `0x805` (flags `0x04`; `0x802` until 1.0); value 350; other fields from the vanilla tome below |
| `0x804` | MGEF | `HoMTox_TogglePowerEffect` | Name "Toxicology"; archetype Script (no script attached), casting type Fire and Forget, delivery Self, magic skill None, flags No Duration, No Magnitude, No Area only (`0x0E00`; not Hostile, not Detrimental); description = the power text below. The DLL toggles on its apply event |
| `0x805` | SPEL | `HoMTox_ToxicologyPower` | Name "Toxicology"; type Lesser Power, Fire and Forget, Self; equip type `025BEE:Skyrim.esm` (VoiceEquip); base cost 0, flags Manual Cost Calc and Disallow Absorb/Reflect (`0x00200001`), charge time 0.5, cast duration 0, range 0, no half-cost perk; one effect `0x804`, magnitude 0, area 0, duration 0 |
| `0x806` | MESG | `HoMTox_MsgOn` | Notification shown when the power turns Toxicology on: name (FULL) = the text, which the DLL reads; description empty, not a message box (DNAM 0), display time 2 |
| `0x807` | MESG | `HoMTox_MsgOff` | The same, shown when the power turns it off |

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

Set on the new record: name, value 350, the teaches-spell link (`0x802` in 1.0, `0x805` since 1.1), and the book
text.

### Text

Effect description (`0x801`, DNAM):

> Poison spells inflict your first favorited poison; once it wears off, creatures resist it for a while. Every 100
> Magicka spent on poison spells uses one bottle, moving to the next when empty.

Power effect description (`0x804`, DNAM):

> Turns Toxicology on or off. While on, destructive poison spells carry the first favorited poison; while off, they
> carry no poison and no bottles are used.

Notifications (`0x806` / `0x807`, FULL): "Toxicology is now active." / "Toxicology is no longer active."

Book text (`0x803`): a centred handwritten title "Toxicology" in the vanilla tome markup
(`<font face='$HandwrittenFont'><font size='40'><p align='center'>`), then `</font><p align='left'>` and four
paragraphs separated by blank lines (`\r\n\r\n`). The first paragraph ends (since 1.1) with: "Toxicology, once
learned from this tome, is a power; the caster calls upon it to awaken the discipline, and calls upon it again to set
it aside. While it is awakened, it abides within the caster." The second paragraph says that when a poison wears off, the
creature resists that same poison for a while (ordinarily as long as the effect lasted, briefly for poisons that act
at once) and can then be poisoned again; during the effect and the resistance later strikes add no dose of it, and a
different poison can still take hold. Both texts were drafted by
Gemini Argon from a fact sheet and checked line by line against it.

### Changes

- 2026-10-18: the effect description and the tome's second paragraph rewritten for the once-per-target-and-poison
  rule (they described renew-on-hit, which was removed). Done as a byte edit of just the `0x801` `DNAM` and the
  `0x803` `DESC` subrecords (node, no houseCARL write, so no `Update.esm` to remove), with the MGEF and BOOK record
  sizes and their group sizes recomputed; the other subrecords, the TES4 header (flags `0x200`, `Skyrim.esm`
  only, HEDR count 8 = four records and four groups, next object ID `0x804`) and FormIDs `0x800`-`0x803`
  byte-identical to the previous file. Read back with `housecarl_records` (form `everything`) from the dev mod
  folder: both texts equal the approved text.
- 2026-10-18 (later): the effect description and the tome's second paragraph rewritten again for the immunity window
  after the poison wears off (spec 5.4, which replaced once per target and poison). Same method and checks as the
  entry above: only the `0x801` `DNAM` and the `0x803` `DESC` changed (with the MGEF and BOOK record and group
  sizes), header and FormIDs byte-identical; read back with `housecarl_records` from the dev mod folder.
- 2026-10-20 (1.1, the power toggle, `docs/superpowers/specs/2026-10-20-toxicology-power-toggle.md`): four records
  added and the tome changed, as a node byte edit of the 1.0 file (no houseCARL write, so no `Update.esm`). Templates:
  `0x804` is a copy of `0x801` with EditorID, name, description, DATA flags (`0x0E00`) and casting type (Fire and
  Forget) changed; `0x805` is a copy of `0x802` with EditorID, name, an `ETYP` inserted before `DESC` (the xEdit SPEL
  order), a new `SPIT` and `EFID` `0x804`, modelled on the vanilla lesser power `PowerKhajiitNightEye`
  (`0AA01D:Skyrim.esm`: VoiceEquip, charge time 0.5; the vanilla SPIT flags are 0, this one sets Manual Cost Calc and
  Disallow Absorb/Reflect, and it has no menu display object); the two MESG records are new in a new top-level MESG
  group placed after the BOOK group (xEdit's TES5 group order puts MESG after BOOK), with the record and group header
  bytes copied from the existing KYWD record and BOOK group. Field offsets and flag bits are from xEdit's
  `wbDefinitionsTES5.pas` (dev-4.1.6: MGEF DATA, SPEL SPIT, MESG). `0x803`: the `DATA` teaches-spell FormID `0x802` ->
  `0x805` and the last sentence of the `DESC`'s first paragraph replaced (the rest byte-identical). The touched
  record sizes, the MGEF, SPEL and BOOK group sizes and the HEDR count (8 -> 13) and next object ID (`0x804` ->
  `0x808`) recomputed; the TES4 header otherwise, FormIDs `0x800`-`0x803`, the KYWD record and every other subrecord
  of `0x801`-`0x803` byte-identical to 1.0 (checked by a dump diff). Read back with `housecarl_records` (form
  `everything`) from the file's path: every field above as stated, `ETYP` resolves to `Voice`, the spell's effect is
  `0x804` and the book teaches `0x805`; header flags `0x200` and one master from a byte dump.

## Korean ESL

`data/ko/HeartOfMagic_Toxicology.esp` is the English file with ten text subrecords in Korean, stored as UTF-8
(as most Korean translation plugins in the TAKEALOOK load order store their text; many Korean translations there
use DSD JSON or STRINGS files instead). It has the same file name, because the
DLL looks the records up by it; the release puts it in `HeartOfMagic-Librarian-Toxicology_<version>_KO.zip`
(`plugins/cmake/DistributionToxicology.cmake`) and `.\DeployDev.ps1 -ToxicologyOnly -ToxicologyKorean` deploys it.

| Record | Subrecord | Korean |
|---|---|---|
| `0x801` MGEF | `FULL` | 독물학 |
| `0x801` MGEF | `DNAM` | the effect description below |
| `0x802` SPEL | `FULL` | 독물학 |
| `0x803` BOOK | `FULL` | 마법책: 독물학 (the glossary's "마법책:" tome form) |
| `0x803` BOOK | `DESC` | the same markup with the title 독물학 and the four paragraphs in Korean |
| `0x804` MGEF | `FULL` | 독물학 |
| `0x804` MGEF | `DNAM` | the power description below |
| `0x805` SPEL | `FULL` | 독물학 |
| `0x806` MESG | `FULL` | 독물학이 활성화되었습니다. |
| `0x807` MESG | `FULL` | 독물학이 비활성화되었습니다. |

Effect description:

> 독 주문에 즐겨찾기 맨 위에 있는 독의 효과가 함께 실린다. 독이 풀리면 대상은 한동안 그 독에 내성이 생긴다. 독 주문에
> 매지카를 100 소모할 때마다 독이 한 병씩 줄고, 병이 비면 다음 독으로 넘어간다.

Power description:

> 독물학을 켜거나 끈다. 켜 두면 독 주문에 즐겨찾기 맨 위에 있는 독의 효과가 함께 실리고, 꺼 두면 독이 실리지 않으며
> 병도 줄지 않는다.

The tome's first paragraph ends (since 1.1) with: "이 책으로 익힌 독물학은 권능이 된다. 시전자는 이 권능을 펼쳐 기예를
일깨우고, 다시 펼쳐 거두어들인다. 한번 일깨운 기예는 거둘 때까지 시전자에게 깃든다."

The text was translated from the English text above and given the translation final review by Gemini Argon and GPT
(Codex) in two rounds (first round: Argon 10 lines, Codex 10; second round: Argon 3, Codex 3, two of them the same
line). The effect description follows the plain "~한다" style of the vanilla Korean effect descriptions; the tome stays
in-world and names no menu. The 1.1 texts (the power description, the two notifications, the tome's new sentences) had
the same final review: Argon flagged 1 line, GPT flagged 1 line (the tome sentence), 1 line changed.

Made with a byte edit like the changes above: only those subrecords differ from the English file, with the MGEF,
SPEL, BOOK and MESG record sizes and their group sizes recomputed; the TES4 header and every FormID are
byte-identical. 1.0 was read back with `housecarl_records` from the dev mod folder after a `-ToxicologyKorean` deploy.
1.1 (2026-10-20) was made again from the new English file (the 1.0 Korean `0x801` DNAM and `0x803` FULL and DESC
taken as bytes from the 1.0 Korean file, the DESC with only the last sentences of its first paragraph replaced) and
read back with `housecarl_records` from the file's path: the names, the descriptions, the book text and the two
notifications equal the reviewed text, value 350, the book teaches `0x805`. **When the English file changes, the
Korean file has to be made again from the new English file.**

`tools/toxicology-esl-check` (`toxicology-esl-check <english.esp> <korean.esp>`) holds the pair to this: the TES4
header, every subrecord and the order of records, groups and subrecords must be byte-identical, and so must every
record and GRUP header apart from its size field, which instead must add up in both files. The exceptions are the
ten subrecords in the table above (`0x801` FULL and DNAM, `0x802` FULL, `0x803` FULL and DESC, `0x804` FULL and DNAM,
`0x805` FULL, `0x806` FULL, `0x807` FULL): each must be in both files exactly once, the Korean ones UTF-8 ending in
exactly one NUL with no other NUL, and not all ten may equal the English ones (that would be the English file copied
over the Korean one). The MESG records' empty `DESC` is not one of them and must stay byte-identical. The Toxicology distribution step
(`plugins/cmake/DistributionToxicology.cmake`) runs it before the zips are made, so a Korean file left behind by a
change to the English one (a value, a flag, a new record) stops the build. Each FAIL line names the record and
subrecord, with the byte that differs for a subrecord or the offsets for a header; a change in the structure (a record
added or removed) is reported at its first point only. `toxicology-test --self-test` runs the same check on the
committed pair and on broken copies (`tools/toxicology-test-esl.cpp`, including a broken `0x806` FULL and a changed
`0x805` SPIT byte). It cannot see a change to the English text itself, since those ten subrecords may differ: after editing one, translate it again and remake the Korean file.

## How it was made (houseCARL)

1. `housecarl_create_plugin` `patch="HeartOfMagic_Toxicology"`, `esl=true` - an empty ESL-flagged header. houseCARL
   put it in the mod folder `houseCARL - HeartOfMagic_Toxicology`; the folder was renamed on disk to
   `HeartOfMagic-Toxicology-Dev` (the dev mod folder) before any record was written. Since 2026-10-17 the mod
   and the plugin are enabled in the MO2 profile `TKL - MUNG ADDON`.
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
