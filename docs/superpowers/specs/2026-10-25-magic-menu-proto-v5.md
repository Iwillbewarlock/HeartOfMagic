# SL_MagicMenu prototype, build 5 (2026-10-25)

Read `2026-10-25-magic-menu-proto-v4.md` first (and its review notes). Build 5 adds three things the user chose on
2026-10-25 ("다": all of them). Build 4 is deployed (Case 212) and not yet seen in game; build 5 is built and reviewed
now but deployed only after build 4's game run, so a build-4 problem is not hidden under build-5 changes.

Evidence and reviews: `C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3/` (`codex_overall_result.md` whole-screen
critique, `codex_clip_result.md` clipping study, `LEDGER.md`).

## Q1 Key conflict: our "next learnable" key is R, and R is taken

- Fact: TAKEALOOK has "Spell Organizer - Hide Remove Delete Erase Forget Spells". Its MCM key "Remove spell hotkey"
  (`Scripts/source/SpellOrganizerMCM.psc:86`, `OnKeyDown` :224) shows a confirmation box to remove the selected spell
  (ShowConfirmation default true, :24). The magic menu's bottom bar shows "R 제거" (build 3 frame). Our `iNext=19` is R too.
- Change: move our keys to keys nothing else in the magic menu uses. Proposed: next learnable = N (49), learn selected node
  next = G (34) stays unless taken. The implementer verifies against: SkyUI's installed `interface/skyui/config.txt` (search,
  sort, column keys), Spell Organizer's `RemoveHotkey` default in the .psc, SkyUI magic-menu bottom bar keys (E/M1/M2, F, LCtrl,
  Shift, Tab, R) and any other TAKEALOOK mod that registers keys in the magic menu (grep `RegisterForKey` / `MagicMenu` in
  installed .psc sources under `D:/TAKEALOOK/mods`, read only). Log at menu open which keys we use.
- Also read the actual key from Spell Organizer's MCM setting if it can be found (its saved value lives in the save; if not
  readable, assume R and say so).
- Every on-screen key hint uses the configured key's name, not a hard-coded letter.

## Q2 Empty states become the next action (whole-screen review #2)

- Progress card with no learning target: instead of "No learning target" + empty bar, one line that says what to do,
  e.g. "학습 대상 없음 · 배울 수 있는 주문 6개 · [N] 다음 후보". Distinguish: no target but candidates; no candidates in this
  school; every spell in the school mastered; HoM data not ready yet; spell not in the tree.
- Node card with nothing clicked: a short hint at the top of the card instead of a big centred line; when the user presses the
  next-learnable key the card shows that candidate at once with its requirements and "[G] 학습 대상으로 지정".
- Korean UI text for these strings (the user plays in Korean). Wording comes from Gemini Argon from a fact sheet (state, numbers,
  key placeholders); Claude checks it keeps every placeholder. Strings go in one table in the code (one place to translate later).
- `SelfTest: Q2 state <name>` logged when each state is first shown, so the log tells which states were reached.
- Strings (Gemini Argon draft 2026-10-25, two lines tightened by Claude; `{key}` already renders with its brackets, e.g. `[N]`):

  | key | text |
  |---|---|
  | progress_title | {school} 학습 |
  | progress_target | 학습 대상: {spell} ({pct}%) |
  | progress_none_candidates | 학습 대상 없음 · 배울 수 있는 주문 {n}개 · {key} 다음 후보 |
  | progress_none_nothing | 배울 수 있는 주문 없음 (선행 주문 미충족) |
  | progress_all_mastered | 모든 주문 숙달 완료 |
  | progress_not_ready | 주문 데이터 불러오는 중... |
  | progress_not_in_tree | 선택한 주문은 주문 트리에 없음 |
  | node_hint | 트리에서 주문 클릭 · {key} 다음 후보 |
  | node_set_target | 학습 대상 지정 {key} |
  | node_is_target | 현재 학습 대상 |
  | node_locked | 선행 주문 필요 |
  | node_mastered | 숙달 완료 |
  | legend_mastered | 숙달 |
  | legend_learning | 학습 중 |
  | legend_can_learn | 배울 수 있음 |
  | tree_not_in_tree | 주문 트리에 없는 주문 |
  | preview_title | 미리보기 |
  | tree_title | 주문 트리 |

  Argon final review (2026-10-25, 29 lines incl. build-5 additions): 10 flagged, 9 taken ("다음 후보" -> "다음 주문" x3,
  "주변 연결 없음" -> "연결된 주문 없음", "다른 배울 수 있는 주문 없음" -> "배울 수 있는 다른 주문 없음", req_all/req_some
  name 선행 주문, "경험치 충족 주문" -> "경험치 충족된 주문"); 1 rejected as factually wrong (no_xy candidates ARE in the
  tree, only without a position) and reworded by Claude to "트리 창에 표시 안 됨". The code table (Widget.cpp kStrings) is the
  current text.

  The game's Korean font must have these glyphs (the menu already shows Korean spell names, so it does); the "·" and "..."
  characters too (build 4's node card showed a missing glyph box for a bullet character - use a character the font has, check
  `kDot`).

## Q3 Clip the 3D preview to the box (clipping study, ranked option A+C)

Goal: nothing of the 3D preview is drawn outside the PREVIEW box. GPT's study says a plain scissor around our over-pass is not
enough: (C) the game's own pass before the movie also draws the models, outside the box too; (A) the engine may re-apply its
cached rasterizer/viewport state during the draw.

- C, hide the game's own pass: after our over-pass `Render()`, cull (SetAppCulled(true)) the loaded models we fitted, remembering
  which ones WE culled; right before our next over-pass, uncull only those (a model the game culled itself stays culled; a model
  that left loadedModels or was replaced is forgotten). The game's own pass then has nothing to draw. Method reference: IR
  Scene.cpp ~405-470 (DrawUnder keeps and restores cull flags around an extra Render; GPL-3, method only). Check that loading and
  animation still advance while culled (log the model's first-render and a frame counter); if culled models stop animating, log it.
- A, scissor in our over-pass: before our `Render()` set the scissor rectangle to the box in render-target pixels and enable
  scissor in the engine's own state (`BSGraphics::RendererShadowState` runtime data `rasterStateScissorMode` + `stateUpdateFlags`,
  then the D3D11 `RSSetScissorRects` on `BSGraphics::Renderer` runtime data `context`), restore both after. Use the actual output
  render target size (`OMGetRenderTargets`, Community Shaders' upscaling/frame generation can swap it; Grid Inventory does the
  same, `skypia0147-dev/grid-inventory` ItemPreview.cpp:1397-1436), not the window size. The engine scissor helper found by
  Community Shaders (`REL::RelocationID(75564, 77365)`, x/y/width/height) may be used only if its signature is confirmed.
- Both behind ini: `bHideGamePass=1`, `bClipPreview=1` (default on for this test build), each can be turned off alone.
- Self-test: log once per menu the scissor rect in pixels, the render-target size and whether the engine state still had scissor
  enabled after `Render()` (read `rasterStateScissorMode` back). Whether pixels really stay inside cannot be read from the log:
  that is the one eyes-only check (video), and the reason is stated in the checklist.
- If A cannot be made to hold (state reset), keep C and log it; D (offscreen render target + Scaleform image) is the fallback
  for a later build, not this one.

## Changes from the GPT design review (gpt-6-astra, `video3/codex_v5_design_result.md`)

- **Q1 key: N is taken.** Creature Framework (installed) uses `PupTargetKey=49` (N) and casts a spell on key-up with no menu check
  (`Creature Framework/scripts/source/CFConfigMenu.psc:40`, `CreatureFramework.psc:218`); our key events also reach other mods.
  So: scan every installed mod under `D:/TAKEALOOK/mods` (read only) for magic-menu and global keys - Papyrus sources
  (`RegisterForKey`, key properties and their defaults), MCM Helper settings/json (`TAKEALOOK - MCM and INI/MCM/Settings/*.json`,
  `*/MCM/Config/*/settings.ini`), SkyUI config.txt files - and pick the first free key from: Y(21), U(22), H(35), J(36), K(37),
  B(48), V(47). Same check for G (34); OSL ChildFix registers G but its handler is commented out (`OSLAroused_Main.psc:271`) -
  keep G only if nothing else live uses it, else pick the next free one. Write the scan result (key -> mods) to the log once at
  load and to `docs/superpowers/specs/v5-key-scan.md`. **Change the shipped ini** (`iNext`, `iSetTarget`): the installed ini's
  `iNext=19` overrides the C++ default. Spell Organizer's installed json says R=19, but its value in the save is unknown.
- **Q2 states (corrected).** "No candidates" is not always "prerequisites missing": candidates count only `kAvailable`, so also
  separate: a spell at 100% but not yet learned (`kReady`), learned early but not mastered (`kLearning`), state unknown
  (`kUnknown`), no tree loaded, no school / none selected, and candidates that have no tree position (cannot be centred).
  The shown count and the next-candidate key use the same candidate set (today only the key skips nodes without x/y).
  "불러오는 중" only while the snapshot is really pending. "Selected spell not in the tree" stays a separate note next to normal
  progress. The implementer writes the final Korean line for any new state in the same style as the table (short label style)
  and lists them in the report for the lead to check.
- **Q3 C redesigned (cull ownership).** `SetAppCulled` is one bit with no owner: the game also hides earlier selections' models.
  Rule (Inventory Reforged does the same, `Scene.cpp:578` KeepPreview / `:450`): we only ever touch the model whose `itemBase` is
  the current list selection (or our P3 spell). After our over-pass `Render()` we cull that model if it is shown and remember it;
  before the next over-pass we uncull it only if it is still in loadedModels, still the current selection's model, and still
  culled. Any other model is never culled or unculled by us. On selection change, tab change to a tab without the preview,
  menu close, or the over-pass not running, uncull what we culled (if still the selection's) and forget it. Order in PostDisplay:
  uncull -> fit -> scissor -> Render -> restore scissor -> cull.
- **Q3 completion condition lowered and stated.** The game's pass runs before our hook in the same frame, so a model's first
  frame (load) can still show outside the box once; build 5 promises "nothing outside the box after a model's first frame",
  not "never". Log per model whether its first game-pass frame was culled.
- **Q3 animation while culled is unverified.** Log for the selection's model: `loadTask`/loaded state, and one number that
  moves with animation (e.g. the model's or its first particle system's world bound radius, or a controller's last time if
  CommonLib exposes it) at 1 s intervals while culled; if it never changes, say so (that would mean culling froze it). The user's
  video is the final check; the ini switch `bHideGamePass=0` is the way back.
- **Q3 scissor contract.** Use exact types: `RendererShadowState::GetRuntimeData().rasterStateScissorMode` (uint32),
  `stateUpdateFlags` (`REX::EnumSet<ShaderFlags,uint32>`), `Renderer::GetRuntimeData().context`
  (`REX::W32::ID3D11DeviceContext*`), `REX::W32::D3D11_RECT` (fields x1,y1,x2,y2). Save and restore: the D3D11 raster state
  (`RSGetState`, release the COM reference), the scissor rect array and count (`RSGetScissorRects`), and the shadow scissor mode;
  mark raster dirty after setting and after restoring; never overwrite the whole `stateUpdateFlags` with an old value (OR in the
  bit). Pixel rect: map the box through the movie viewport origin/size (as the frame handler already does) and scale from the
  viewport's buffer size to the actual bound render target size (`OMGetRenderTargets` -> texture desc; release references); do
  not multiply by Community Shaders' world upscale ratio (it is 1 at the UI stage). Log once per menu: RT size, viewport,
  pixel rect, whether `Render()` changed the scissor rect or mode (read both back right after `Render()`).
- **A failing means clipping failed.** If the read-back shows the engine dropped the scissor, log `SelfTest: Q3 clip FAIL` - C
  alone is not a clipping success.

### Q3 changes from the build 5 code reviews (GPT + reviewer agent, rounds 1 and 2, 2026-10-25)

- **Tab without the box: the hide is kept, not undone (departs from the rule above).** The rule above says to uncull on a tab
  change to a tab without the preview. The code keeps our hide while the menu stays open on such a tab (Active Effects): the
  game's own pass has no box to draw it in there, and giving back by the last frame's selection showed a model that was no
  longer selected (round 1, #5). The model comes back when the box is shown again for the same selection. The menu's close and
  the next menu's reset give back every model we hid that is still up and hidden, whatever the selection, so our hide never
  outlives the menu (round 2, #4). Crossing such a tab ends the observation history: back on a tab with the box, the
  selection's model counts as a change again, and a model the game showed meanwhile is not counted as a lost hide (round 2, #3).
- **One selection form.** While P3 asked for the tree card's spell's 3D, only that spell's model is hidden and given back; the
  list item's model is left as it is (round 2, #1).
- **A -> B -> A.** Every model hidden in this menu and still up is remembered; selected again and still hidden, it is given
  back, unless the game already shows another model of that form (round 1, #6/#8; round 2, #5). Frames that draw two models of
  one form are counted. The flash of the frame a model becomes the selection's is counted at every such change.
- **Hide and clip follow place/fit**: off with `bMovePreview=0` or Inventory Reforged active; the scissor is off on a frame that
  was not fitted (zooming, not placed yet) (round 1, #2).
- **Clip verdict.** A frame whose render target changed during `Render()`, or that drew nothing, is not judged. The close
  summary judges hide and clip apart: `Q3 summary hide PASS|FAIL|-|off, clip PASS|FAIL|NOT JUDGED|off`; clip PASS needs the
  scissor to have held at least once and never been dropped or left unset (round 1, #3; round 2, #2).
- **Animation samples** count only while our fit has settled and keeps its scale; the bound is compared at the model's own
  scale (round 1, #4/#7). The assumption that the game hides an earlier selection's model itself is stated as an assumption
  (round 2, #6).

## Q4 Readability (whole-screen review #7, #5)

The user cannot play right now and asked for everything doable outside the game (2026-10-25), so build 5 also takes the
low-risk items that do not change the user's chosen layout (mockup v8):
- Right-side text sizes: node names, legend, requirement lines and hints at least the size of the left SkyUI list rows (measure
  the list's row font size from the movie, log it, scale ours to match; keep within the boxes - long names wrap or ellipsize,
  never overflow; the box titles become the Korean strings above).
- Tree exits: when the selected node links to nodes that are not drawn (outside the window or over iMaxNodes), draw a small
  arrow at the box edge in that direction with "+N" (count of hidden linked nodes). A single node with no links shows
  "주변 연결 없음".
- The selection frame in the tree is drawn apart from the state marks (it covered the gold diamond in build 3).

## Q5 Offline check for the preview math

`tools/magic-menu-preview-check.cpp` (built like `tools/magic-menu-tree-check.cpp`, see tools/CMakeLists.txt): runs the place/fit
math (the same functions, moved into a header both the plugin and the tool include, no game types needed beyond plain structs) on
synthetic cases and prints PASS/FAIL per case: the review counter-examples (deep model 10/15 depth -> projected width <= 0.92 of
the room; off-centre box; tiny model hits grow cap 3; sphere-only model; parent with rotation and scale -> local move lands the
world point; X2 re-created model at the same address; zero/negative content box rejected). The lead runs it; its PASS count goes
into the case.

## Out of scope for build 5

Moving the progress card to the top (changes the user's mockup layout - ask first), card-role rework, lighter panels, gamepad
focus, the combat-module slot, and the library picks (4-part node card, side-by-side compare, ring gauge, staggered tree).
