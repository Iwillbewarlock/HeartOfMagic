# SL_MagicMenu prototype, build 7 (2026-10-25)

Read `2026-10-25-magic-menu-proto-v6.md` (and v5, v4 with their review notes) first. Build 6 ran in game on 2026-10-25 22:07
without crashing (after the TranslateToScreen crash fix and the panel-border removal, Case 214): P1/Q3/P2/L2 self-tests PASS,
P4 FAIL (SkyUI card's hidden parts outside the slot; on screen its black panel overflows to the right, under the node card).
Evidence: video `C:/Users/qtuna/Videos/The Elder Scrolls V  Skyrim Special Edition/The Elder Scrolls V  Skyrim Special Edition 2026.10.25 - 22.07.21.02.mp4`,
frames/analysis `C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video6/` (`argon_result.md`), log
`Documents/My Games/Skyrim Special Edition/SKSE/SL_MagicMenu.log` (22:05-22:08).

User decisions (2026-10-25): remove the glow disc behind the preview effect; replace SkyUI's spell card with our own card
(layout F, render `.../video3/render/layouts/layout_F@2x.png`); "함성이랑 특성쪽도 우리식으로 나오게 바꿔. 트리창 경험치창만
안나오게 하면 되는 부분이니" - the Powers and Shouts tabs use the same look, without the tree and the progress (XP) card.

## C1 Our own info card (replaces SkyUI's item card on every tab where we draw)

- Hide SkyUI's card (`_root.Menu_mc.itemCard` / its fade holder: `_visible = false` or `_alpha = 0` every frame we own the tab;
  never touch its data). Stop the build-4 card fit/move (P4) while our card is on; keep the old path behind
  `bOwnCard=0` (ini, default 1) as the way back.
- Data: what SkyUI would show. Read the card's info object (`itemCard.itemInfo` getter = LastUpdateObj, ItemCard.as:172-175)
  after each list highlight change (it is filled by the game through RequestItemCardInfo, so other mods' description changes,
  e.g. Description Framework which hooks the magic menu, are included). For a tree click on an unlearned spell use build 4's P2
  object (ItemCard::SetForm -> obj); P2 still writes SkyUI's card so the info object is one source either way.
- Fields by card type (SkyUI Inventory.as ICT_* and ItemCard.as setter cases; verify in the AS2 copy):
  spell (ICT_SPELL 7): name, school + level (`magicSchoolName` / `castLevel`), `effects` (HTML), `spellCost`, castTime (0 = per
  second -> show "/초" with the cost), cast type; power (ICT_SPELL_DEFAULT 13 or whatever SkyUI uses): name, effects, cast
  frequency if present; shout (ICT_SHOUT): name, the three words (learned/unlocked state if the object has it), effects,
  recharge time; active effect (ICT_ACTIVE_EFFECT or the tab's own type): name, effects, time left. Unknown type: name +
  effects only, logged once.
- Look (render F): panel at the item card slot, black fill, no border; name size like the node card name (19, fit/ellipsize);
  sub line (school · level, or "함성" / "특수능력" / time left) in kTitleColor; a rule; the description as htmlText (keep the
  game's colour/magnitude markup, but force our font size = reading size and our font face if the HTML sets another; wrap,
  at most the lines that fit, ellipsis); a rule; bottom row: left "매지카 {cost}" (or the shout recharge), right the cast
  type. Korean labels for new strings come from Gemini Argon (fact sheet), checked by the lead.
- Self-test: `SelfTest: C1 card PASS|FAIL` - our name field text equals the info object's name after each change; log the
  card type and which fields were present once per type.

## C2 Powers / Shouts (and Active Effects) tabs in our look

- Powers and Shouts tabs: show the preview box (3D as on spell tabs) and our info card; hide the tree box, the progress card and
  the node card. Layout: left column as on spell tabs - preview 648,67-969,297, our card 648,306-969,623 (the progress card's and
  the card slot's space together); right column empty.
- Active Effects tab: no preview box and no 3D in the box (see P7), our info card at the left column 648,67-969,(fit to content
  or 623). Hide SkyUI's card there too.
- `SelfTest: C2 tab <kind> layout` once per tab per menu with the visible boxes.

## P7 Preview fixes

1. Backdrop disc: when a preview model is first seen, walk its geometry and cull (`SetAppCulled(true)`) every non-particle
   BSTriShape whose NiAlphaProperty blends normally (src SRC_ALPHA, dst INV_SRC_ALPHA; additive shapes have dst ONE) and whose
   name starts with "Cylinder" (vanilla MAGINV art: `Cylinder08:1` in MAGINVHealSpellArt.nif and FireballInvArt.nif, verified with
   houseCARL). Exclude culled shapes from the fit measure (the disc made the fit small: heal grow 0.55). Log per model which
   shapes were culled. ini `bHideBackdrop=1`. A second ini `bHideGlow=0` (off) culls additive halo shapes named `Sphere_Halo`,
   `glow01`, `lightRays*` for users who want no glow at all.
2. Active Effects tab: the box is not drawn but our place/fit kept moving the model there, so the 3D floated over the game view
   (video 48.5-58.5 s). While that tab is shown: no place/fit, the game's own place restored (itemPos/itemScale back to the saved
   values, models moved back by the parent-space rule), hide-from-game-pass given back; re-place when a preview tab returns.
3. Over-pass draws only the current selection's model: around our over-pass `Render()`, cull every loaded model that is not the
   current selection's (and not P3's tree spell) and restore their flags right after in the same call (Inventory Reforged
   DrawUnder pattern, `Scene.cpp` ~405-465). Fixes the previous spell's effect staying in the box when the new selection has no 3D
   (video 36.0-36.5 s: Matchmaker selected, Dragonfire still shown). With no model for the selection the box is empty.

## T7 Tree fixes

1. A list selection that is not in the tree keeps the previous centre (design), but draw that previous tree dimmed (alpha ~35%)
   with the "주문 트리에 없는 주문" note on top, so it is not mistaken for the selection's tree.
2. Labels placed to the LEFT of a node must not overlap the node mark: in video frames f_009/f_086 ("불씨조각") and f_024-f_040
   ("치유") the empty diamond covers the first character. Check PlaceLabel's left-side x (gap from the node centre must clear the
   mark radius + ring) and fix; add an offline case in LayoutMath (left label box vs mark box: no overlap).
   Found in the build-6 log and frames (GPT design review #1 agrees): the label in those frames is the CENTRE's, on its right,
   and a neighbour 15-19 units away covers it (log "치유 ... centre label right (overlap right 297.8, left 365.5)"): the kept
   centre/selected label skipped the overlap check. Build 7 places kept labels at the first clear place of: default side, other
   side, below, above, both sides further out (LayoutMath::KeptLabelPlaces / ChoosePlace), and every label starts past the
   mark's ring (MarkReach 11.25; build 6 started at 11).

## Strings (build 7, terminology pass 2026-10-25)

The glossary wins (user rule): game GMST terms from the Skyrim Korean glossary, Heart of Magic's own panel terms from
`dist/HeartOfMagic_2.4.0/PrismaUI/.../lang/ko.json`. Changed in kStrings (Widget.cpp):

| key | text | source |
|---|---|---|
| progress_target | 학습 목표: {spell} ({pct}%) | HoM panel 학습 목표 |
| progress_none_candidates | 학습 목표 없음 · 배울 수 있는 주문 {n}개 · {key} 다음 주문 | HoM panel |
| progress_none_nothing | 배울 수 있는 주문 없음 (선행 조건 미충족) | HoM panel 선행 조건 |
| node_set_target | 학습 목표 지정 {key} | HoM panel |
| node_is_target | 현재 학습 목표 | HoM panel |
| node_target_pct | 현재 학습 목표 · {pct}% | HoM panel |
| node_locked | 선행 조건 필요 | HoM panel |
| req_all / req_some / req_none | 선행 조건 모두 숙달 필요 / 선행 조건 중 {n}개 숙달 필요 / 선행 조건 없음 | HoM panel |
| xp | {cur} / {need} 경험치 | HoM panel |
| card_cast_concentration | 집중 | GMST Concentration |
| card_cast_fire_forget | 단발 | no glossary entry; pairs with 집중 |
| card_cast_constant | 지속 효과 | GMST Constant Effect |
| card_cast_scroll | 마법 주문서 | GMST Scroll |
| card_lesser_power | 하급 특수능력 | GMST Lesser Power |
| card_ability | 능력 | GMST Ability |
| card_active_effect | 적용중인 효과 | GMST ACTIVE EFFECTS (no space) |
| card_shout_recharge | 충전 {t} ({t} = the words' values, "5 / 10 / 15초") | GMST Recharge |

Still drafts for Argon's review: card_magicka "매지카 {n}", card_magicka_per_sec "매지카 {n}/초", card_shout "함성",
card_power "특수능력 · 하루 1회", card_power_any "특수능력", card_time_left "남은 시간 {t}", card_hours/minutes/seconds
"{n}시간"/"{n}분"/"{n}초".

## Out of scope

Gamepad, combat-module slot, translations other than Korean, the "2$m 56$s" time text (SkyUI's own translation strings).
