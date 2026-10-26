# menu-render: offline picture of SL_MagicMenu build 5

Draws what build 5 of `plugins/SL_MagicMenu` puts on the SkyUI magic menu, over a real game frame, without the game.
Reads the plugin source's constants (copied below, nothing is linked) and the real `spell_tree.json`. Writes nothing
outside `--out`.

## Run

```
cd tools/menu-render
python render_menu.py            # defaults below; Python 3.10+, Pillow, numpy
python render_menu.py --row-size 13.3   # the list row size measured on the frame instead of the code's fallback 14
```

| option | default |
|---|---|
| `--tree` | `D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_tree.json` (the only copy in the modlist) |
| `--video3` | `C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3` (uses `expected/base_v3_poison.png`, `frames/f_014.jpg`) |
| `--out` | `<video3>/render` |
| `--row-size` | 14 (`kListRowFallback`) |
| `--ss` | 4 px per stage unit while drawing; saved downsampled to 2 (`@2x`, the game's own 2560x1600) and 1 (1280x800) |

Outputs per scenario: `<name>.png` (1280x800) and `<name>@2x.png` (2560x1600, drawn at 2x, the left half upscaled from
the frame), plus `render_log.txt` (tree window, tallies, every text line chosen).

Files: `menu_model.py` (tree and state rules), `textengine.py` (font), `render_menu.py` (layout, composition).

## Scenarios

| file | list selection | tree click | HoM target | preview content | item card |
|---|---|---|---|---|---|
| `scenario1_all_poison_no_target` | 치명적인 독 분사 (모두 보기 tab) | none | none | stand-in (see below) | real card crop, 레벨 fixed |
| `scenario2_heal_click_rejuvenating` | 치유 | 회춘의 손길 | none | 치유's orb (bPreviewUnlearned=0) | P2 card for 회춘의 손길, redrawn |
| `scenario3_all_poison_target40` | 치명적인 독 분사 | none | 회춘의 손길 40% | stand-in | real card crop, 레벨 fixed |

Code paths these follow:
- Scenario 2: `OnNodeClicked` (Widget.cpp:2284-2300) recentres on the clicked node, so the tree is centred on 회춘의 손길
  with the selection brackets there (build 3's f_014 still shows the old centre). `SelectInList` finds no entry for the
  unlearned spell, so the list stays on 치유. The shipped ini has `bPreviewUnlearned=0`, so no P3 model load: the
  preview keeps 치유's model. The item card is the P2 card (`ShowTreeSpellCard`, Widget.cpp:1294).
- Scenario 3: with 회춘의 손길 as target `LookOf` gives kTarget, so `Learnable` drops it: 5 candidates, not 6.
- Scenarios 1/3: the only linked node of 치명적인 독 분사 is 독 볼트 (locked): not drawn, not an exit (`FindExits`
  filters by `Shown`), and `noLinks` is false (adjacent not empty): a single gold diamond, no "+N", no note.

## Constants taken from the source

`plugins/SL_MagicMenu/src` unless noted.

| what | value | where |
|---|---|---|
| panel rects (stage 1280x720) | preview 648,67-957,330; tree 971,67-1238,330; progress 648,339-969,441; item card 648,450-969,623; node card 979,339-1238,623 | Settings.h:44-48, dist/SL_MagicMenu.ini [Layout] |
| tree scale, steps, max nodes | 0.35, 1, 40 | Settings.h:50-52, ini |
| keys | iNext=22 -> `[U]`, iSetTarget=34 -> `[G]` | ini [Keys]; Widget.cpp:453-468 KeyName/KeyLabel |
| title strip, padding, legend strip | 22, 8, 18 | Settings.h:57-58 |
| tree content area, panel content area | TreeContent, PanelContent | Settings.h:67, 76 |
| edge width, title size, colours (fill 000000, edge 8A7A5A, title C8C0B0) | 1, 12 | Widget.cpp:77-78 |
| alphas | preview 100 (over-pass on, so not see-through: Widget.cpp:2625-2627), panels 85 | Widget.cpp:79 |
| reading size | `Size(x)=max(x, rowSize or 14)`, clamp 10..20 | Widget.cpp:83, 471-474 |
| line factor, gutter | 1.25, 4 | Widget.cpp:85-86, 476-485 |
| node shapes | node r 7, hit r 10, ring gap 3, arc width 2.5, select r 15 arm 4.5 | Widget.cpp:90-93 |
| labels | width 104, size 10, gap 1, 2 lines | Widget.cpp:94-95, DrawLabel 1721-1746 |
| dashes, line width | 4/3, 1 | Widget.cpp:96 |
| note, legend | 10, 10, icon r 5, text gap 4 | Widget.cpp:97, DrawLegend 1584-1621 |
| exits | arrow 5, inset 7, text w 36 | Widget.cpp:98, FindExits 1811, DrawExits 1843 |
| colours | gold D6B56A, blue 4A90D9, white F0F0F0, locked edge 6A6A6A, padlock A0A0A0, edge 9A9A9A, lock edge 7A7A7A, label D8D8D8, dim 8A8A8A | Widget.cpp:99-101 |
| progress card | top 4, school 11, target 19 (raw), bar gap 2, bar h 9, xp w 120, xp 12, hint gap 4, hint 13, 2 message lines, bar back 202830 | Widget.cpp:104-107, RedrawProgress 1972-2030, NextStepLine 1954-1970 |
| node card | name 19 (raw, 2 lines), sub 12, state 14, state icon r 6, state text x 22, rule gap 4, row 13, mark share 0.8, mark gap 4, bottom rule 76, xp 70, button 38/28/14; ok 6CC070, not yet E05050, button fill 1E3A5A, off 262626 | Widget.cpp:110-114, StateLine 2037, RequirementRows 2070, DrawRows 2097, DrawButton 2125, RedrawNodeCard 2140 |
| strings (Korean) | kStrings table | Widget.cpp:189-241 |
| school words | 회복마법 etc. | Widget.cpp:236-240, SchoolText 275 |
| shapes | Diamond, Arc (from the top, clockwise, 32 segments), Padlock, Check, Cross (stroke max(1.5, size/6)) | Draw.h:117-202 |
| text fit | FitText bisection on code points with "...", FitLine, FitLines | Widget.cpp:519-567, Draw.h:84 Cut |
| panel drawing order | preview, tree, progress, node card, legend | Widget.cpp:2627-2631 |
| tree rules | parse, prerequisite normalising, rotation -90 - spokeAngle, Query (BFS, rotate, scale, inside window, nearest first, cap) | TreeData.cpp:256-287, 329-392, 442-448, 450-527 |
| state rules | LookOf, PrereqOf, Learnable (tier, name bytes, formId) | Spells.cpp:431-475 |
| drawn nodes | centre + linked mastered/ready/target/learning/available | Widget.cpp:638-642, 1783-1790 |
| item card fit | top-aligned, centred across, `min(w, h)` scale | Widget.cpp:955-984 |
| preview fill | 0.92 of the content area | PreviewMath.h:23 |

Log lines used (`Documents/My Games/Skyrim Special Edition/SKSE/SL_MagicMenu.log`, build-4 game run 2026-10-25 11:56):
- `SelfTest: item card PASS - background in _root 673.5,450.0..943.4,623.0`: the card's stage rect, used for all three
  scenarios (frame-to-stage scale 269.9 / 424 = 0.6366).
- `Spells: tree ... 1410 by persistent id, 0 unresolved`: every node resolves, so saved FormIDs stand in for game ids.
- `Restoration - 208 nodes, 6 available to learn now`, `Destruction - 332 nodes, 13 available`, and the tree redraw
  lines (치유 linked 6 -> drawn 6, 치명적인 독 분사 2 -> 1, 소각 2 -> 1, 불씨조각 6 -> 6, 불꽃의 룬 4 -> 4, 불꽃 망토 3 -> 3,
  불꽃의 벽 2 -> 1, 드래곤파이어 2 -> 1, 독조각 4 -> 2): the offline model reproduces every one of these with the known
  set below (check: `render_log.txt` and the snippet in the session report).

## Approximations and assumptions

1. **Player state (assumed).** Known = the list rows of `base_v3_poison.png` that are tree nodes: 느린 화염 고리, 독조각,
   드래곤파이어, 불꽃 망토, 불꽃의 룬, 불꽃의 벽, 불씨조각, 소각, 치명적인 독 분사, 치유, 화염 망령, 화염 폭풍 (the vanilla
   0x0007A82B; the other 화염 폭풍 node 0x49445FD0 would make 작열 learnable and give Destruction 14, against the log's 13),
   화염 화살, 화염구 (the half-hidden last row, read as 화염구). Known spells count as mastered (HoM's IsSpellMastered for a
   known spell outside early-learned tracking); nothing is learned early; unknown spells have 0% except the scenario-3
   target. With this, Restoration has 6 learnable spells (독의 씨앗, 섬뜩한 촉수, 성적 충동 상태 (Sexmad) 감지, 천체 가루,
   천체의 복구, 회춘의 손길), matching the log and build 3's "6 spells can be learned".
2. **Required XP.** Novice = 100, from f_014 (build 3's node card: "XP 0 / 100" for 회춘의 손길, "XP 100 / 100" for 치유).
   Other tiers are not needed by these scenarios and are not set.
3. **Reading size.** `--row-size 14` = the code's fallback before `MeasureListRow` reads the list (no measured line in
   any log yet). Measured on the frame: the list's "치명적인 독 분사" is 65 px wide where our size-10 label is 49 px, so the
   list rows are about 13.3; build 5 would use that after its measurement. The difference is about 5% in text size.
4. **Font.** The menu font is "Futura Book BT" inside `Interface/fonts_futura.swf` (TAKEALOOK - Font Edit
   `Interface/fontconfig.txt`, `$EverywhereMediumFont`); no TTF of it exists in the modlist (only ImGui fonts). Drawn with
   `C:/Windows/Fonts/malgun.ttf`, scaled per character class to the game font's ink measured on `base_v3_poison.png`:
   Hangul em 0.842 x size with horizontal squeeze 0.755 (tree label "치명적인 독 분사" at size 10: 49x8 px vs Malgun 77x9.5),
   other characters em 0.72, squeeze 1.057 ("No learning target" at 19: 122x14; "6 spells can be learned [R] Next" at 13:
   147 px; "Mastered" at 10: 32 px; "PREVIEW" at 12: 38 px). Check after calibration: 47.9 / 120.0 / 149.6 / 31.1 / 36.2
   px against 49 / 122 / 147 / 32 / 38. Text widths decide wrapping, "..." cuts and label drops exactly as the code's
   rules do, so these depend on this calibration. Baseline 2 + 0.9 x size below the field top, drawn line pitch 1.2 x size
   (the code lays out with 1.25 and reads textHeight back; the real textHeight is not known).
5. **3D preview.** No frame of the clip shows 치명적인 독 분사's model (the build-4 log has no G1 PASS for it), so
   scenarios 1/3 show 치유's orb (MAGINVHealSpellArt) hue-shifted to green, labelled in the render note. Scenario 2 shows
   it unchanged (correct content: 치유 is the list selection). Source: the old PREVIEW box of f_014 (upscaled to
   1280x800), title text blanked, edges feathered 14 px where the old box cut the halo. The extent is the bound of all
   non-black pixels made symmetric about the bright core (standing in for the model's bound centre), then scaled to fill
   0.92 of the content area 656,97-949,322 and centred on it (width-limited here: 269.6 x 201.2 units) and clipped to it
   (the build-5 scissor). The dim halo is pushed toward black (`(lum-22)/45` ramp) because build 5 draws the model over
   an opaque black box, where build 3's see-through box showed the room through it. Additive composite over black.
6. **Item card.** Real card from `base_v3_poison.png`: its black background 687,506-1110,759 (424 x 254 px); right of
   x 975 the old node card panel lay over it (border, hint text), blanked to black. Placed at the log's background rect
   (673.5, 450, width 269.9; its black part ends at y 611.7). Build 5 fits the visible union of background and text
   fields; with the background alone the left edge could move by a few units (the build-4 "whole card" union reached
   954.8). 레벨: `FixLevelLabel` turns wordWrap off and autoSize right, so "레벨" is drawn whole with the old "레" glyph's
   right edge kept, in the card's label style (measured on "매지카"). Card text styles are copied with
   `malgunbd.ttf` fitted to the measured ink of the real card's title, description, level value, numbers and labels.
7. **Scenario-2 card (P2).** From `spell_scan_output.json` (HoM scan, Natura.esp|0x000F8F): name 회춘의 손길, the one
   visible effect's description "가까운 생명체 대상의 체력을 <mag> 회복합니다. 이후 소량의 체력을 다시 회복합니다." with
   magnitude 5 (the `<mag>` -> number rule matches the real poison card, which shows its two effects at 20), level
   초보자 (minimum skill 0 -> sNoviceDisplayName). Magicka **24 is an estimate**: the record cost is 30 (cost override);
   치유 (same casting perk 0x0F2CAA, also Novice) shows 10 for a record cost of 12.59, ratio 0.794 -> 23.8. The magnitude
   is the record's, without perk or skill scaling. Description wrap width 225 card px: with it the real poison card's three lines come
   out with the same breaks (line 1 is 219.8 wide here and fits; line 2 + " 목표는" is 230.3 and does not).
8. **Background.** Left of x 588 (the SkyUI list panel's edge) the frame as is. Right of it: the old panels and the old
   card cut out and filled by normalised Gaussian interpolation (sigma 60 then 25) from the surroundings, blurred
   (radius 12) and darkened by 15% (ramped in over 40 px). Panel fills (85% black) are composited over that.
9. **Frames.** Scenario 2 uses `frames/f_014.jpg` (1152x720, 0.9 scale) upscaled to 1280x800, so its left half is softer
   than scenarios 1/3 (`base_v3_poison.png` native). The mouse cursor of base_v3 stays where it was in the list.
10. **Not drawn by the plugin:** the yellow-bordered render note at the bottom right of each picture.

## Layout options (layouts.py)

`python layouts.py` renders scenario 3 (치명적인 독 분사 in the list, target 회춘의 손길 40%) in four layouts into
`<video3>/render/layouts/`: `layout_A..D.png` (+`@2x`), `compare_layouts.png` (the right halves, crop x 640..1250,
y 60..680 of each picture, taken from the @2x files: tiles 1220 px wide), `layouts_log.txt`. The card text is redrawn
in the real card's measured styles in all four (kind "redraw"), so the card reads the same in each.

| | preview | tree | progress | item card slot | node card | item card holder scale |
|---|---|---|---|---|---|---|
| A build 5 | 648,67-957,330 | 971,67-1238,330 | 648,339-969,441 | 648,450-969,623 | 979,339-1238,623 | 0.577 (height-limited) |
| B progress on top | 648,146-957,330 | 971,146-1238,330 | 648,67-1238,137 (one row) | 648,339-969,623 | 979,339-1238,623 | 0.686 (width-limited) |
| C bigger card | 648,67-957,260 | 971,67-1238,330 | 648,269-969,371 | 648,380-969,623 | 979,339-1238,623 | 0.686 (width-limited) |
| D fuller cards | as A | as A | as A | as A | as A | 0.577 |
| E2 = E on a grid | 648,67-969,297 | 978,67-1238,297 | 648,306-969,408 | 648,417-969,623 | 978,306-1238,623 | 0.686 |
| E = C + D | 648,67-957,297 | 971,67-1238,330 | 648,306-969,408 | 648,417-969,623 | 979,339-1238,623 | 0.686 (width-limited; slot height 206 = the fitted height) |

Proposals, not plugin behaviour: B's one-row progress strip (school + target on the left half, bar + XP + next step on
the right, build 5's sizes and strings); C's rects (a holder scale of 0.8 would need a 374-unit-wide slot, which runs
into the node card at x 979, so the width caps it at 0.686); D's node card showing the learning target when nothing is
clicked (build 5's RedrawNodeCard with the target as the selection: state "현재 학습 대상 · 40%", disabled button
"현재 학습 대상"), and D's faint locked neighbours (only when the window would draw just its centre: grey outline
diamond 6A6A6A, line 4A4A4A, name + "잠김" in 6A6A6A on the side away from the centre). The card fit is
`ComputeCardFit` (Widget.cpp:955-984) on the logged background bounds 468x300.

E (`compare_CD.png`: A, C, E side by side from the @2x files): C's column with D's two behaviours. C left 37 units
empty under the card (fitted height 205.8 in a 243-unit slot), so E's card slot is the fitted height
(min(321/468, 206/300) = 0.686, the same scale as C) and the freed 37 units go to the preview (193 -> 230 high);
gaps stay 9.

E2 (`layout_E2*.png`, `layout_E2_guides*.png`, `compare_E_E2.png`): E on one two-column grid, gap 9 everywhere
(left 648..969, right 978..1238; row 1 67..297, row 2 306..623). Proposal changes inside the boxes: the progress
card's first line starts 4.5 units lower (`BASELINE x (19 - 14)`) so its baseline sits on the node card name's
baseline (329.1); the node card's bottom block (rule 76 -> 74, XP 70 -> 68, button 38 -> 36 from the bottom) moves
2 down so the button ends at 615 = the card slot bottom minus kPad. Tree scale stays 0.35 (an ini value; the code
has no automatic rescale): the window's half height drops from 103.5 to 87, which still holds 독 볼트 (sy -45.7).
The legend follows its code rule (centre 13 units above the tree box bottom). Guides: cyan solid = column/row
edges and the 8-unit padding insets; dashed = titles baseline 83.8, row-2 first baseline 329.1, legend centre 284,
button bottom 615.

## Build-6 item card options (layouts_v6.py)

`python layouts_v6.py` -> `layouts/layout_E2now|F|G|H(.png, @2x)`, `compare_FGH.png`, `layouts_v6_log.txt`. Scenario 3.
Build-6 parts ported from the current source: no panel border (Widget.cpp Panel), PlanProgress (LayoutMath.h:211: bar
row max(9, FieldH(12)) = 19, XP at size 12, L2 offset 4.5), the node card's bottom block hanging from the button
(kButtonFromBottom = kPad + 28, XP +32, rule +38), the default target in the node card, faint locked neighbours
(kFaintLink 4A4A4A, kFaintText 6A6A6A), the centre label on the left when the right side meets a faint link, and a
faint "잠김 N" arrow toward locked links outside the window (here 천체의 핵심, up) - all three as in the build-6 frame
video6/f_029. Text metrics stay at their 1.25 defaults (the in-game MeasureTextMetrics values are not known).
Preview: build 6's real poison model cropped from f_029's preview content area (1152x720 upscaled); F/G/H drop the
Cylinder08 disc by a luminance mask (below 28 black, full from 58); its size scales with min(width, height) of the
content area (a bound-sphere fit), so G's taller preview shows it 1.59x larger (and softer: upscaled). Required XP from
HoM config.json (Apprentice 200, Adept 400, Expert 800, Master 1500; Novice 100 from f_014).
Proposals: F's own card (rect 648,417-969,623, panel fill, name 19, school · level, description at the reading size up
to 4 lines, 매지카 cost and cast type 집중); G's rects (preview 648,67-969,512, progress 648,521-969,623, no card) and
its node card (the list selection with a 주문 정보 section); H's slot search (ComputeCardFit over every E2 box: current
slot 0.686, preview 0.686, tree 0.556, node card 0.556, the merged left row 2 0.686 - the 321-wide left column caps it,
so the current slot is already the best).

## Build 7 mockups (build7.py)

`python build7.py` -> `<video3>/render/build7/build7_M1..M4(.png, @2x)`, `compare_build7.png`, `build7_log.txt`.
Spec v7 C1/C2/P7. Strings: Widget.cpp kStrings after the terminology pass (학습 목표, 선행 조건, 경험치). Our card ports
RedrawOwnCard / ViewOf / OwnCardRect as they stood on 2026-10-25 (provisional): name 19 (2 lines), sub line Size(12),
shout words line, rule gap 5, description at Size(13) with as many lines as fit, footer rule + cost in 62% of the width
+ cast type right; Active Effects cuts the panel to its content. Rects: spell tabs 648,417-969,623; Powers/Shouts
648,306-969,623 (preview 648,67-969,297, right column empty); Active Effects 648,67-969,623 cut to content.
Data: M1 the real card text; M2 독물학 from video6 f_063 (its spell type is not readable offline: 하급 특수능력 assumed);
M3 거침없는 힘 from Skyrim.esm SHOU 0x00013E07 (recovery 15/20/45 s, vanilla record, overrides not checked) and its
WOOP words with the Korean patch strings (FUS 힘, RO 균형, "D4" 밀기 - the patch's own string for Dah), known words
assumed (the first); M4 평판: 알 수 없음 from f_105 (no time left). Preview: build 6's real models (f_029 poison,
f_063 독물학) with the disc masked by luminance; size estimated as the visible effect's extent filling kFill of
min(width, height), capped at 3x build 6's on-screen size (both hit the cap; upscaled from 1152x720 frames, so soft).
M3 has no shout 3D (no capture). M1's left half is the build-3 All-tab frame (no build-6 All-tab frame has 치명적인 독
분사 selected); M3's left half is the Powers-tab frame f_063 (no Shouts-tab capture), marked on the picture.
The old panels in a frame are now filled by three normalised-Gaussian passes (90/45/20) where a narrower pass only
replaces the wider estimate with at least 15% known weight around it (large holes no longer turn dark).
