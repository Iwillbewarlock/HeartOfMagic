# SL_MagicMenu prototype, build 10 (2026-10-26)

Read `2026-10-26-magic-menu-next.md` and `2026-10-25-magic-menu-proto-v7.md` first; ledger
`Desktop/Claude outputs/ACF-Phase0/M0/video7/LEDGER.md`. Build 9 is deployed (Case 217).

User 2026-10-26: "일단 지금 트리 방식에서 가시성 조작성 개선하고 이펙트 보완. 특히 빛무리 생기는거";
on the mockup (`.../M0/video9/mock/tree_compare.png`): "트리의 모습은 HoM에서 계산한 거 그대로 가져오면 될텐데" -
node positions are never moved.

## P8 Dark cloud behind the art (done in the working tree, not yet in game)

The over-pass marks the engine's blend / alpha-test / depth / raster states dirty before Render() (Scaleform changed
them on the context, the engine only re-sets dirty states). Self-test `P8 dark PASS|FAIL`: 30 frames after a selection
change, the box's pixels read back before and after Render(); additive art only brightens, so darker pixels = FAIL.
If P8 still FAILs in game: a soft black disc behind the fitted art (mockup `halo_options.png`, option 1) as fallback.

## T Tree window: a viewport on Heart of Magic's own layout

Measured: in `spell_tree.json` every school's nearest-neighbour distance is >= 37.9 tree units (p10 ~39.5, median
47-53); at the old fTreeScale 0.35 that is 13 stage units, less than a mark (ring reach 11.25) - the crowding seen in
game. GPT design review (`.../video9/codex_tree_design_result.md`) applied below.

T1 Positions are HoM's, never moved; the school-direction rotation stays (relative positions unchanged); the centre node
   at the window's middle.
T2 One fixed scale (not per centre: the layout should not jump between clicks): fTreeScale default 0.75 and marks
   1.15x (new ini fTreeMarkScale, default 1.15). Ring reach 11.25 x 1.15 = 12.9, diameter 25.9 < 37.9 x 0.75 = 28.4.
   Every mark/ring/selection bracket/hit radius scales with fTreeMarkScale. Log the applied values (an existing ini
   overrides the default: Settings log line must show them).
T3 Query by viewport: every node of the centre's school whose rotated, scaled position falls inside the window's content
   area (with the mark's reach as margin) is drawn - not only BFS-reached nodes. Cap ([Layout] iMaxNodes, 1..200, default
   60) by distance from the centre, but the centre, the selected node and the learning target are kept first (a cap below
   their count is raised to it). Edges (structural
   and lock) between drawn nodes. Nodes of other schools are not drawn.
T4 States/marks as now (StateLine and the existing state set, incl. Ready/Unknown). A node not linked to the centre and
   locked keeps today's "faint locked" look: drawn faint, not clickable (no node clip action) - only graph-reachable
   nodes and learnable/mastered ones get the click.
T5 Labels: centre, selected and target always tried first; then the centre's direct neighbours; then others nearest
   first - each only where it fits without overlapping marks or labels (no forced overlap any more: drop the label,
   the tooltip names it). Labels also keep off the drawn links (LayoutMath::LinkBoxes). Reading size never under the
   list's row size (existing rule).
T6 Hover tooltip: every drawn node clip (faint ones too, without onRelease or hand cursor) gets a hitArea on its mark
   only; onRollOver/onRollOut show/hide a tooltip in a separate layer of our widget (name, school · level, StateLine
   text), right or left of the mark, else above or below it, inside the window. Hover never sets the tree dirty; after a
   redraw the tooltip is re-checked (hidden if its node is gone).

Review round 1 (reviewer agent + GPT, 2026-10-26), applied: P8 compares per channel (a pixel is darker when any channel
falls by more than 24) and logs the darker pixels' bounds; P8 checks once per selection from 30 frames on (a frame with
nothing drawn postpones it) and SKIPs when Render() left another target bound; the input layout and topology are marked
dirty too; the cap keeps the centre/selected/target at any iMaxNodes; label order T5 without clickable-before-faint;
labels keep off links again; faint nodes get the tooltip; the tooltip goes above/below when neither side has room.
GPT second check: the selection brackets sit on the diamond's four tips (square corners reached r x 1.41 into a diagonal
neighbour's ring), r = ring reach + 1.5, arms 4.5 unscaled: 14.44 + 0.71 line corner + 12.94 = 28.09 < 28.4; labels keep
the target path's half width (1.0) off links.
T7 Off-window badges: the centre's linked neighbours that are outside the window are counted by direction (8 sectors)
   and shown as text badges at the window edge ("→ 3개 더"), replacing the +N / 잠김 N arrows. Count only "outside the
   window"; nodes dropped by the cap / without x/y / other school are not counted there (log them apart). No duplicates.
T8 Learning target path: when the target is in this school, the shortest structural-edge path centre -> target (BFS over
   the school's structural edges, ties broken by node index) is drawn brighter on its visible part. A display aid, not a
   claim about prerequisites.
T9 Edges a little thicker (kLineWidth x1.3), the legend in one smaller line.

Order (GPT): marks/hit/scale -> viewport query + cap -> states, labels, badges + tests -> hover -> T8/T9.

## Tests

- tools/magic-menu-tree-check: replace "nodes beyond steps are left out" with the viewport contract: an unlinked node
  inside the window is drawn; the target survives the cap; a node outside the window is not drawn; badge counts only
  outside-window linked neighbours; no two drawn marks closer than the mark diameter at the defaults.
- tools/magic-menu-preview-check label tests: no label overlap accepted (was: overlap area <= 50 ok).
- In game (log): "tree redraw" line with drawn / capped / outside counts, labels drawn / dropped, badges; hover and click
  are eyes-only checks.
