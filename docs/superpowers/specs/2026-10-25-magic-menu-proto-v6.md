# SL_MagicMenu prototype, build 6 (2026-10-25)

Read `2026-10-25-magic-menu-proto-v5.md` first (with its review notes). Build 6 is the layout the user chose after seeing
offline renders (2026-10-25): layout **E2** = C (bigger item card) + D (node card filled, faint locked neighbours), aligned to
a two-column grid ("오와열 맞춰줘", then "1로"). Builds 4 and 5 have not been seen in game yet (the user cannot play now and
asked for everything doable outside the game).

Reference renders (the target look): `C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3/render/layouts/`
`layout_E2@2x.png`, `layout_E2_guides@2x.png` (cyan guide lines), `compare_E_E2.png`. Renderer and every value it used:
`tools/menu-render/` (`layouts.py`, `README.md` "E2"), `layouts_log.txt`.

## L1 Grid (stage units, 1280x720 movie)

One gap everywhere = `fPanelGap` 9. Left column x 648..969, right column x 978..1238.

| box | rect (left, top - right, bottom) |
|---|---|
| preview | 648,67 - 969,297 |
| tree | 978,67 - 1238,297 |
| progress card | 648,306 - 969,408 |
| item card slot | 648,417 - 969,623 (card fits at 0.686, width-limited: 321/468) |
| node card | 978,306 - 1238,623 |

- Change the C++ defaults in Settings and the shipped `dist/SL_MagicMenu.ini` [Layout] values to these. The Powers/Shouts tab
  rule (item card moves under the preview, `fPanelGap` below it) still holds with the new preview bottom.
- The preview content area (Settings::PanelContent: box minus title band and padding) follows automatically; check the 3D fit
  and the scissor use it (they do in build 5).
- Tree box is 230 high now (was 263): keep `fTreeScale` (no auto-shrink rule exists); nodes that fall outside get the build-5
  "+N" exits; the legend keeps its code rule (centre 13 above the box bottom).

## L2 Inside alignment (from the E2 render, see the guide image)

- Box titles at the same inset and baseline in each row (already true in code).
- Body text left edges on one line per column (already true: column left + padding 8 + 2).
- Row 2 first lines: the progress card's first line (size 14) and the node card's spell name (size 19) start at the same
  top, so their baselines differ by about 4.5 units; move the progress card's first line down so the two baselines meet
  (derive the offset from the measured font metrics of build 5, `MeasureTextMetrics`, not a hard 4.5; log the offset).
  Everything in the progress card must still fit its 102 height (build-5 fit rules).
- Node card bottom: the button's bottom = card bottom - padding 8 (= 615 with the new rects), aligned with the item card slot
  bottom minus padding; move the bottom divider, XP line and button 2 units DOWN, toward the card bottom (render used
  76->74, 70->68, 38->36 measured from the card bottom; build 6 derives them from the button: kPad + kButtonH = 36, +32, +38).

## L3 Node card shows the learning target when nothing is clicked (from D)

- When no tree node is selected and the school has a learning target, the node card draws that target exactly as if it had
  been clicked (name, school · level, state ring "현재 학습 대상 · {pct}%", requirements, XP, the disabled "현재 학습 대상"
  button). The tree does NOT recentre and shows no selection bracket for it (it is a default view, not a click).
  The state follows `Spells::LookOf` as for a click: a target at 100% XP not learned yet shows "경험치 충족 · 습득 대기"
  (Ready comes before Target, by design), its button that same text, disabled.
- When there is no learning target, keep build 5's short hint ("트리에서 주문 클릭 · [U] 다음 주문").
- A tree click or the next-candidate key replaces it as before; clearing the selection returns to the target view.
- `SelfTest: Q2 state node_default_target` the first time per menu.

## L4 Faint locked neighbours when the tree would show one node (from D)

- Only when the drawn tree has exactly one node (the centre), draw its directly linked locked neighbours (not learned, not
  learnable) as grey outline diamonds with their name and a second line "잠김", and a faint link line; at most the same
  max-nodes rule; labels placed on the far side from the centre so they do not cross the link (as the render did).
- They are not clickable, never selected, never counted in "+N" exits or the candidate count.
- When the centre has no links at all, keep "연결된 주문 없음".
- Korean: "잠김" (new string `tree_locked`), fits the existing short-label style.

## Out of scope

Item card larger than 0.686 (would overlap the node card), progress card to the top (B), gamepad, combat-module slot.
