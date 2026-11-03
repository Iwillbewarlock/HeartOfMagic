# SL_MagicMenu - after build 9: what works, what to add, what to fix (2026-10-26)

Read with `2026-10-25-magic-menu-proto-v7.md` and the ledger `Desktop/Claude outputs/ACF-Phase0/M0/video7/LEDGER.md`.
User 2026-10-26 01:55, after the build 9 video: "아직 이펙트쪽은 좀 부자연스럽지만 기본틀은 완성된거 같네".

## Build 9 game run (2026-10-26 01:53, video 113 s, frames `.../M0/video9/`)

Works (video + log): right side empty at open with nothing chosen ("nothing chosen" line); moreHUD's black box gone
(made transparent, alpha was 100); panels at 70% like the left list; our info card with readable descriptions; fire,
heal and illusion art shown for shared-art spells; Active Effects tab shows no 3D (tab hide 1 model, 2 handed back to
Q3 on return). Self-tests: P1 PASS 16 (+summary), C1 card PASS 12, C1 readable PASS 3, P2 PASS 5, Q3 anim PASS 5.
Two FAIL lines, both understood:
- Q3 hide FAIL once (FireballInvArt at the first shared-art selection: one frame drawn by the game's pass unclipped).
- C1 card FAIL once: 갈망 selected again with an identical card - the test, not the card (it shows "갈망").

## What looks unnatural in the preview (fix plan, effects first)

E1 Size by the visible part, not the mesh outline. The fit measures every non-particle shape's vertices; art whose
   visible glow is small inside large faint shapes comes out as a dot (카이단 옵션: outline 42.8 x 45.3, grow 0.54,
   a small red dot on screen; 투여 small too). Plan: (a) leave out shapes that draw almost nothing - alpha-blended
   shapes with a near-zero material alpha or an empty/black texture, and normal-blend "Cylinder"/"Plane" helpers
   beyond the backdrop; (b) better and engine-true: read the box back after the over-pass (D3D11 copy of the scissor
   rect, small staging texture, a few frames after a selection), take the bounds of pixels brighter than the panel,
   and correct the grow once (screen-space fit; no per-model tables). Offline: a harness for the bounds math.
E2 Dark smoke over the 70% panel. Normal-blend smoke particles were invisible on the black box; on the see-through
   box they read as a muddy halo (fire spells). Plan: an ini choice - hide normal-blend particle systems in menu art
   (like the backdrop, by blend mode, not by name), or draw a darker backing only behind the 3D (preview box alpha
   85-90 while the rest stays 70). User to pick from a render of both.
E3 Off-centre rising effects. Fire art's flame and smoke rise; centring on the whole outline puts the core low.
   Plan: centre on the brightest part (from E1b) or on the core shape's vertices only.
E4 Same visual size across spells. After E1, aim every art at one share of the box (now 92% of the outline) and
   cap the grow so small art is not blown up into a blur (the disc-free estimate showed blur at 3x).
E5 Shared-art switch: the one unclipped frame (Q3 hide FAIL) - hide the model the art match picks before the game's
   pass of the frame it becomes the selection's, not after.
E6 Tests: C1 treats "same form, same card" as identity even when the list index changed; P1 says "particles left
   out" already - add a pixel-based check once E1b exists.
E7 Unverified: does SkyUI drop the selection when the mouse leaves the list (then the right side hides)? Log shows
   "nothing chosen" only at open. Check in the next run; if not, hide on mouse-out of the list area.

## Features still to add (from earlier sessions and the plan)

F1 Progress card at the top (mockup change - ask the user first).
F2 Card roles: node card vs our info card overlap (tree click shows both) - one clear role each.
F3 Gamepad input (tree navigation, U/G keys on the pad).
F4 Combat-module slot (ACF Phase 0: HoM -> menu -> integration -> combat).
F5 Bookmark-library elements: 4-fact node card, side-by-side compare, round progress gauge.
F6 Shouts: the third word shows "D4" (the Korean patch's own string); show words learned/unlocked state.
F7 Active-effect time text "2$m 56$s" (SkyUI's own translation strings) in our words.
F8 Translations other than Korean (strings table is ready; Argon drafts, glossary first).
F9 Settings in MCM (alpha, hide glow/backdrop, keys) instead of ini only.

## Before any public release

Credit and ask: Inventory Reforged (GPL-3; placement and the card-frame / moreHUD hide ideas), Compare Equipment NG
(GPL-3), Item Preview Customizer (GPL-3), Inventory Interface Information Injector (MIT) - ideas only so far, no code
copied; our plugin links CommonLibSSE-NG. User rule: check sources, credit, ask before release.

## Next session, first steps

1. Read this file and the ledger.
2. E1b prototype (box read-back bounds) behind an ini switch, logged only (no fit change) for one game run.
3. Render E2's two options for the user.
