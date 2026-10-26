# SL_MagicMenu prototype, build 13 (2026-10-26)

Read `...-v12.md` (and its review sections) first; ledger `Desktop/Claude outputs/ACF-Phase0/M0/video7/LEDGER.md`,
section "Build 12 game run 2026-10-26 14:58".

Evidence (build 12 log, confirmed in the ledger):
- the over-pass target is RTV R10G10B10A2_UNORM 2560x1600, single-sample, NOT the swap chain's back buffer (C swap
  chain line) - it is composited later by its alpha over the world;
- over Inventory3DManager::Render() no pixel got darker (`P8 dark PASS` every time): H9 refuted;
- alpha went 0.667 -> 1.0 over the art's silhouette (`P8 alpha ALPHA_CHANGED`, ~18k-58k box pixels, up to 30786 with
  colour unchanged): the art covers the world behind the see-through panel there = the dark cloud (H6 in mechanism);
- the build 12 additive composite fell back at once: "Render() left another render target bound - it drew there, not
  into our target" (the engine re-binds its own target inside Render(); G5).
- User (build 12 video): no 3D on the Powers tab, like the Shouts tab.

## A Alpha restore (the fix)

A1 Formats. The restore handles a single-sample, non-array 2D target whose format P8 reads (`LayoutOf`: 8-bit
   RGBA/BGRA (+ _SRGB), R16G16B16A16_FLOAT, R10G10B10A2_UNORM). R11G11B10_FLOAT has no alpha: nothing to restore. Any
   other format, a multisampled or array target: skipped, logged once per menu ("A alpha restore skipped ..."), that
   over-pass draws as build 11. Pure table: `PreviewMath::AlphaRestoreFor` (kRestore / kNoAlphaStorage /
   kUnsupportedFormat).
A2 Around Render() in the scissored over-pass (`Preview.cpp RenderClipped`), every frame:
   - before Render() (`SaveBoxAlpha`): the box (`ScissorBox`, clamped to the target, checked again against the view's
     mip so the copy source lies inside the texture) of the bound slot-0 target is copied with CopySubresourceRegion into
     a scratch texture of the same typed format and the target's size, at the same pixels (so the pixel shader Loads at
     its own pixel: no constant buffer). Scratch: SHADER_RESOURCE only, made per size/format, released at the close of
     the menu that used it last (as build 12's target);
   - after Render(), when Render() left the same target bound (`sameTarget`): `DrawAlphaRestore` binds that RTV alone
     in slot 0 (no DSV; depth off), draws one full-screen triangle (SV_VertexID) scissored to the box, PS
     `float4(0,0,0, saved.Load(pos).a)`, blend off, RenderTargetWriteMask ALPHA: the alpha goes back, RGB stays what
     Render() drew. Render() left another target: not restored that frame, logged (once per menu) and counted;
   - reused from build 12: run-time D3DCompile from d3dcompiler_47.dll (vs_4_0 / ps_4_0), the G8 full save/restore
     scope (`PassScope`, formerly CompositeScope; now also the 8 RTVs + DSV), the session fallback with one warn line
     (`AlphaPassOff`: shaders, a D3D object or the device fail).
A3 Measurement (P8, a few checks per menu as before): `P8 dark` / `P8 alpha` over Render() are read after Render() and
   before the restore (unchanged meaning). New `P8 alpha restored PASS|FAIL|INCONCLUSIVE|SKIP`: the box's alpha after
   the restore against its value before Render(); PASS when every pixel's alpha is within half a stored step of its
   before value (R10G10B10A2: step 1/3; 8-bit 1/255; float formats exact), FAIL otherwise, INCONCLUSIVE on NaN/Inf
   (`PreviewMath::CountAlphaMoved`, `AlphaRestoredVerdict`).
A4 ini `[Preview] bKeepPanelAlpha=1` replaces `bAdditiveComposite`; 0 = build 11's drawing exactly. Close line per menu:
   "A alpha restore - menu N: X restored, Y skipped".

Deleted (dead with A): build 12's redirect into our own target (`RedirectScope`, the R16G16B16A16_FLOAT target and its
RTV, `CompositeTarget`, `DrawComposite`'s additive blend, `ReportComposite`), the `P8 redirect` and `P8 art` lines,
`PreviewMath::JudgeRedirect` / `RedirectFailure` / `RedirectVerdict` / `CountChanged` / `ChangeCount` / `CountTarget` /
`TargetStats` / `kOpaqueAlpha` / `kDarkColour` and their preview-check tests. Kept: the C swap-chain line, P8 dark/alpha.

Effect stated honestly: the art's RGB is still written into the panel; the later alpha composite now shows it at the
panel's alpha (0.667 here) over the world instead of covering the world. How that looks is the user's eyes-only check.

## B Powers tab: no 3D

B1 `Widget.cpp`: `g_hide3D` (atomic) is set in WatchTab when the category flag is kFlagPowers (128; Shouts 64 is
   unchanged), reset at attach. The PostDisplay hook passes Powers to Preview as a tab without the box
   (`active = g_previewShown && !g_hide3D`, `noBox = !g_previewShown || g_hide3D`): Preview hides every model up
   (HideForTab, as on Active Effects), draws no over-pass and gives the models back when another tab shows. The box and
   layout stay the Powers/Shouts ones (g_previewShown and Tab::kPowers untouched). Logged once per menu:
   "Widget: Powers tab: 3D hidden (user 2026-10-26)".

## Tests

- preview-check: A1 format table (5 cases); A3 alpha-restored compare (normal 2->2 with RGB changed PASS, failure 2->3
  FAIL, boundary under half a step, float exact, 8-bit step, 2x1 bounds, NaN INCONCLUSIVE, size/format/rows nullopt).
- In game (log): `P8 alpha restored PASS` (review round 1 #4: the evidence; the save line is not), no "A alpha restore off/skipped" line;
  `P8 alpha ALPHA_CHANGED` over Render() stays (it is measured before the restore). Powers tab line once.
- Eyes only: the dark cloud gone behind the art; the art's look over the see-through panel; Powers tab shows no 3D.

Unverified: CopySubresourceRegion from a texture whose RTV is bound (precedent: P8's ReadViewPixels does it on the same
target and build 12 read real values); whether the engine leaves the same target bound after Render() on every frame
(build 12 direct draw: yes, P8 dark was judged); the look of the art at the panel's alpha.

## Review round 1

Findings checked against the code; comments in the code cite "review v13 #n".

1. Unclipped over-passes (clip off, model not fitted / zooming, no frame info, no box in the target) called Render()
   with no restore. Now `RenderUnclipped` saves and restores the **whole** bound target ({0,0,w,h} from BoundTarget),
   not the box: without the scissor the model may stand away from the box (zooming), so a box-only restore would miss
   it. The scratch is the target's size either way; the same-target check after Render() applies.
2. Those frames no longer call LogRestoreSkipped. Restores of the whole target are counted (`restoresWhole`, one info
   line per menu); a frame with no target to save (no context, none bound) is `LogRestoreNoTarget` with its own counter
   and reason. Close line: restored (N whole target), skipped, no target to save, scratch made.
3. Powers hides its 3D regardless of bMovePreview: `OnPostDisplay` takes `a_hide3D` (Widget passes g_hide3D); Active
   Effects keeps the bMovePreview=0 "changes nothing" promise.
4. The save-time line now says "A saving the alpha ... for the restore"; the in-game evidence is `P8 alpha restored`,
   logged right after that frame's `P8 alpha`, now naming its verdict. `PreviewMath::AlphaRestoredVerdict(count,
   renderChangedAlpha)`: NOTHING_TO_RESTORE (not PASS) when Render() did not change the alpha; SKIP when P8 dark
   skipped. No copy or draw when no model is drawn.
5. EnsureScratch counts (re)creations per menu; past 8, AlphaPassOff with the reason.
6. Stale comments fixed (close-time release, ReadViewPixels, DrawAlphaRestore's scissor rect).
7. SaveBoxAlpha turns predication off around CopySubresourceRegion and sets it back.
8. PassScope saves the OM UAVs (OMGetRenderTargetsAndUnorderedAccessViews, 8 RTVs + 8 UAVs) and sets them back with
   the real RTV count as UAV start slot, initial counts -1; all references released. Limit: UAV slots 8+ of an 11.1
   context are not read.

Tests: preview-check +4 (PASS / NOTHING_TO_RESTORE / FAIL / INCONCLUSIVE). In game: `P8 alpha restored PASS` on a
frame whose `P8 alpha` is ALPHA_CHANGED; no "A alpha restore skipped" line; the close line's counts.

## Review round 2

Second check; each finding confirmed against the code before the fix. Comments cite "review v13 round 2 #n".

1. With bMovePreview=0 and bDrawPreviewOver=0 the PostDisplay hook was not installed (Install) and the thunk returned
   early, so the Powers hide never ran. The hook is now installed always. With neither job (also IR or VR, where
   `g_powersHideCalls` is off) the thunk calls Preview only while the Powers tab shows and once more after it for our
   movie (`g_powersHidePending`); every other frame returns at once as before. Preview skips HideArt on such a call
   (`a_fit || a_overPass` gate), and the give-back on a tab with the box gives every model back (`a_all`) since no Q3
   follows. Nothing else changes for those settings.
2. Powers -> Active Effects with bMovePreview=0: both calls are not active, so GiveBackTab was never reached. Preview
   keeps `clip.powersHid` (a_hide3D at the last call for our movie on a shown tab); when it ends in the inactive branch
   and no tab hide follows (bMovePreview=0), GiveBackTab(all). With bMovePreview=1 Active Effects keeps hiding as before.
3. PassScope restore: with 8 RTVs (or no UAV saved in the slots after the RTVs) OMSetRenderTargets(count, views,
   depth); otherwise OMSetRenderTargetsAndUnorderedAccessViews as before (avoids D3D11 error 3146060, StartSlot 8 /
   0 UAVs).
4. "Render() changed the alpha" for `P8 alpha restored` is `PreviewMath::RenderChangedAlpha` (CountAlphaMoved, stored
   step, before vs after Render()), not P8's tolerance (8-bit 2.5/255). The line also says "changed by a stored step:
   yes/no". SKIP when those two reads cannot be compared.
5. The scratch count is of successful creations (`scratchCreated`, incremented after the SRV is made; the cap refuses
   at 8 made); the close line says "scratch created N time(s)".

Tests: preview-check +5 (8-bit 100 -> 101: P8 alpha NO_SIGNIFICANT_ALPHA_DELTA but RenderChangedAlpha true, restored
PASS; unchanged NOTHING_TO_RESTORE; size mismatch nullopt), 159 PASS. In game (log): with both settings 0, Powers tab
then another tab shows "P7 tab hide ends" with all given back; Powers -> Active Effects (bMovePreview=0) logs
"the Powers tab hide ends on a tab without the box". Unverified in game: the D3D11 debug layer run (#3) is GPT's WARP
reproduction only; whether OMSetRenderTargets clears a UAV that Render() bound in the slots after the RTVs (the old call nulled them) is not checked.
