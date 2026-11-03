# SL_MagicMenu prototype, build 12 (2026-10-26)

Read `...-v11.md` (and its review R1-R12) first; ledger `Desktop/Claude outputs/ACF-Phase0/M0/video7/LEDGER.md`. Build 11
is deployed (Case 219); the user says the tree has no problem.

Build 11 game run 12:00 (log): the over-pass target is RTV R10G10B10A2_UNORM 2560x1600 single-sample (P8 cannot read
it: SKIP); the blend state at Render() entry is Scaleform's (SRC_ALPHA/INV_SRC_ALPHA), at Render() exit BlendEnable 0
(ONE/ZERO, write mask 0xF): the engine's last draw inside Render() had blending off. New first hypothesis H9: some art
shapes are drawn without blending (vanilla draws the item 3D on a black background, so their opaque dark parts never
showed); over our see-through panel the dark parts of glow meshes (IceGlow05, Sphere_Halo: Use_Falloff, faceted) show as
the dark polygon. H6 (alpha composite) drops: a screen-size 2-bit-alpha target is most likely the swap chain's buffer.

User 2026-10-26: "호버시에는 창 내용이 바뀌지 않도록하고 창내용은 선택한 마법에만 반응하게"; for the cloud they chose
"measure and fix together" (an additive composite, ini switch, with the measurement).

## C Cloud: additive composite from our own target (fix) + measurement

C1 New ini `[Preview] bAdditiveComposite=1`. When on, the over-pass draws the art into our own render target and adds it
   onto the menu instead of drawing it straight onto the screen:
   - our texture: the bound RTV's size and typed format family with alpha kept (R16G16B16A16_FLOAT; created once per
     size, released at menu close / size change), RTV + SRV;
   - before Render(): save OM state (RTVs, DSV), viewports, scissor; bind our RTV with the SAME DSV (Soft_Effect and the
     depth test keep working), ClearRenderTargetView to 0,0,0,0; Render() as today (scissor, dirty flags unchanged);
   - after Render(): bind the original RTV + DSV back, then draw one full-screen triangle limited to the box (scissor)
     with our own VS/PS: PS samples our SRV (point, clamp) and outputs its RGB; blend ONE/ONE ADD on RGB, alpha write
     off (RenderTargetWriteMask RGB) - black adds nothing, so an opaque dark part of the art cannot darken the menu;
   - every D3D11 state our composite touches is read with the context's Get* calls first and set back exactly after
     (IA layout/topology/VB/IB, VS, PS, PS SRV slot and sampler slot used, blend state+factor+mask, depth-stencil state,
     raster state, viewports, scissor), so the engine's shadow state still matches the context (do not touch its flags
     beyond today's);
   - shaders: compiled once at first use from embedded HLSL with D3DCompile loaded at run time from d3dcompiler_47.dll
     (LoadLibraryW + GetProcAddress; no new link dependency); a failure logs the reason and falls back to today's
     direct draw for the rest of the session.
   Effect: the art's light is added; parts meant to darken (the fire art's smoke particles) no longer darken.
C2 Measurement: P8 reads R10G10B10A2_UNORM (10/10/10 bits RGB /1023, 2-bit alpha /3) and our R16G16B16A16_FLOAT target.
   With the composite on, P8 compares the menu's pixels before and after the composite (`P8 dark`: must be PASS by
   construction - a FAIL means the composite is wrong) and logs what our own target holds over the box: pixels that are
   darker than black are impossible, so it logs `P8 art` with the count of pixels whose alpha is high (>= 0.5) but RGB
   low (max channel < 0.05) - opaque dark art pixels, the H9 evidence. With the composite off, P8 works as in build 11
   on the R10G10B10A2 target (RGB-darker count = H9 evidence).
C3 Log once per menu whether the bound RTV's resource is the swap chain's back buffer (RE::BSGraphics renderer's swap
   chain GetBuffer(0) compared by pointer; UNVERIFIED where the renderer keeps it - find it in CommonLibSSE-NG).
C4 bAdditiveComposite=0 keeps build 11's drawing exactly.

## S Selection: the windows follow the selected spell only, never a mouse hover

S1 SkyUI moves the list highlight on mouse roll-over and every highlight change asks for the item card and the 3D
   (ItemMenu.onItemHighlightChange). Build 12 tells a hover from a selection: a mouse roll-over is a hover; a mouse click
   on a row (itemPress - the game also equips it, as it always does) or a keyboard/gamepad move is a selection.
   The implementer verifies in SkyUI's source (github schlangster/skyui, BasicList.as / InventoryLists.as / ItemMenu.as)
   which event and field tell mouse from keyboard (e.g. selectionChange's keyboardOrMouse) and cites file:line.
S2 Our windows (card, 3D preview, tree centre, node card) follow only the selected spell. Preferred way: wrap the menu's
   highlight handler in AS2 from C++ (keep the original GFxValue, install a handler that calls the original only for a
   selection, not for a mouse hover), so the game neither refills the card nor loads another 3D on hover. If wrapping
   is not possible, re-assert the selected spell's card and 3D (the tree-click paths ShowTreeSpellCard /
   RequestUnlearnedPreview) when the highlight differs from the selection.
S3 The selected spell persists until the next selection; menu open starts with the list's current highlight as the
   selection. A tree click stays a selection (build 10 rules). The list's own highlight bar may still follow the mouse
   (SkyUI's), only our windows and the game's card/3D do not.
S4 Log: `selection <id> by click|key|tree|open` and `hover <id> ignored` (once per id per menu, capped).

## Tests

- preview-check: R10G10B10A2 decode (bits, alpha /3); `P8 art` opaque-dark counter.
- Offline where possible: the selection rule as a pure function (event kind -> selection or hover) with normal /
  boundary / failure cases.
- In game (log): `P8 dark` PASS with the composite, `P8 art` counts, swap-chain line, selection/hover lines.
  Eyes only: the cloud gone; the art's look (light only); hovering the list leaves the windows still; clicking selects.

User 2026-10-26 confirmed S1 as written: "클릭(게임이 원래 하는 장착도 같이 됨)이나 키보드·패드 이동만 선택으로 칩니다 이게 맞아".

## Design review round 1 (GPT gpt-6-astra, 2026-10-26) - taken; overrides the text above

G1 (S) InventoryLists.onItemsListSelectionChange re-dispatches itemHighlightChange WITHOUT keyboardOrMouse
   (InventoryLists.as:468-473; BasicList.as:343 has it). Capture the source at the list layer: wrap
   inventoryLists.onItemsListSelectionChange (it receives BasicList's selectionChange with keyboardOrMouse): keyboard
   (SELECT_KEYBOARD) -> a selection, pass on to the original; mouse (SELECT_MOUSE) not set by us -> a hover, swallowed
   (the original is not called, so ItemMenu asks for no card / 3D and its bottom bar stays). A click is itemPress /
   itemPressAux (right click) on the list: observe it, record the pressed entry as the selection, then call the saved
   original onItemsListSelectionChange for that index so the card / 3D / bottom bar follow; the game's own equip path
   (MagicMenu.onItemSelect / AttemptEquip) is left exactly as it is - never re-issue ItemSelect, never turn a mouse event
   into a keyboard one. Our own programmatic changes (tree click selectedIndex setter, menu open) mark themselves BEFORE
   the setter runs (selectedByUs today is set after it - too late). PageUp/PageDown come as SELECT_MOUSE with -1
   (ScrollingList.as:226-278): count a -1 as "no selection change" (keep the selection). Verify every file:line in the
   installed SkyUI if its SWF differs; log the wrapped functions at install.
G2 (S) One committed selection per menu (FormID, type, source: open|click|key|tree) is the only input of WatchSelection,
   the PreviewFrame selection handed to Preview, WatchChosen, CardSelection and CardSpell; the list highlight moves on
   its own. Category change, sort, search and index re-layout do not overwrite it (a selection whose entry is gone from
   the list keeps showing until the next selection).
   Review round 1 S3: a category/tab change keeps the school and centre of the selection (not the tab's) and, once the
   list is filled again, selects the committed entry in it once (marked as ours) so the game's card and 3D come back;
   not in the new list -> its 3D through the tree-click path (P3) when that is on, else logged that the 3D stays.
G3 (S) No re-assert fallback: if the wrap cannot be installed, log it and keep build 11's behaviour (windows follow the
   highlight) - the card request carries no FormID (ItemMenu.as:326-347), so a re-request cannot restore a selection.
G4 (S) The wrapper keeps the original's this/arguments/return (Invoke("call", this + args)), is installed per menu
   movie, releases the saved GFxValue before the movie goes, guards re-entry, and wraps whatever is there (another mod's
   handler included). Autodesk direct-access pattern: GetMember -> CreateFunction -> SetMember.
G5 (C) Render() may re-bind the engine's own render target inside (the shadow state has RT indices and a dirty flag;
   Nukem9's SE renderer re-binds dirty targets). So C measures whether the art reached our target: after Render(),
   count non-zero pixels of our target over the box and compare the screen before/after Render(); if our target stayed
   empty while the screen changed -> log "composite: Render() drew to the screen, not our target" and fall back to the
   direct draw for the session. The RTV is also read after Render() (as today) and logged.
G6 (C) P8 with the composite: two windows measured - the screen across the whole redirect (before binding our target
   -> after Render(), must be unchanged if Render() drew into ours) and across the composite (before -> after, RGB must
   not get darker); plus our target's content stats (non-zero, negative, > 1, NaN/Inf, "alpha >= 0.5 and RGB < 0.05"
   count reported as a count only, not as proof of H9). Boundary blend state stays labelled boundary.
G7 (C) Effect stated honestly: the additive composite stops the art from darkening the menu behind it; it does not undo
   what an opaque draw covers inside the art itself (glow lost under it, depth written). R10G10B10A2_UNORM has no sRGB
   view: the art's values are added as they are (same numbers a direct additive draw would add); > 1 saturates as
   before.
G8 (C) Composite pass state fully set by us: VS/PS ours, GS/HS/DS null, no predication, depth/stencil off, solid no-cull
   raster with ScissorEnable true + the box rect set again (ScissorScope's destructor restored the old one), viewport
   = the target, sample mask all, alpha-to-coverage off, blend ONE/ONE ADD RGB, write mask RGB. Order: unbind our RTV ->
   bind the original RTV/DSV (the state Render() left) -> our SRV -> draw -> unbind our SRV -> restore every saved
   input exactly (slots D3D nulled count). The redirect scope (before Render) and the composite scope (after Render)
   are separate: the composite restores the after-Render state, not the before-Render one.
G9 (C) Same DSV: single-sample only (the log shows 1 sample); MSAA target -> composite off for the session (logged).
   Our target is the RTV's size. The DSV/Soft_Effect depth situation is the same as today's direct over-pass.
G10 (C) Swap chain check: Renderer::GetCurrentRenderWindow()->swapChain GetBuffer(0), compared as IUnknown identity.

## Review round 1 (two reviewers, cross-checked, 2026-10-26) - taken; overrides the text above

S1 InventoryLists dispatches itemHighlightChange itself (InventoryLists.as:327 showItemsList with -1, :397
   InvalidateListData with the row under the mouse after a favourite / key equip), past the G1 wrap. Menu_mc's
   onItemHighlightChange is wrapped too (GetMember -> CreateFunction -> SetMember, original kept as an AS member):
   passed when our token is set (inside the saved selection-change original, or our own dispatch in RestoreListCard)
   or the event's row is the committed entry; a -1 with a selection (or a tree card) is swallowed (S3), a -1 with none
   is passed (vanilla's no-selection path); any other row and, while a tree card is the selection, the list row's are
   swallowed and logged ("highlight <i> not the selection, ignored", capped). Pure rule: SelectionRule::DecideHighlight.
S2 Commit() counts a commit as a change also when a tree card is shown (SelectionRule::CommitChanges), so coming back
   from it to the same row bumps selSeq and WatchSelection drops the tree card (not in build 11's follow-the-highlight).
S3 G2 above: WatchTab no longer sets the tab's school / default centre while a selection exists; WatchReselect selects
   the committed entry again after the deferred re-fill (BasicList.as:224 requestUpdate), up to 10 frames, as the same
   selection at its new row (no selSeq bump); a tree card stays as it is; WatchTreeCard's tab-change fade is off with
   the highlight wrap. WatchSelection sets the school from a school tab itself.
S4 A swallowed row change (index != -1) still plays the original's sound: GameDelegate.call("PlaySound",
   ["UIMenuFocus"]) (InventoryLists.as:502-503).
S5 A change of another row arriving during our own selectedIndex set (ScrollingList.as:190-193) is logged.
C1 The redirect window (G5/G6 window 1) compares by one stored step (R10: 1/1023, alpha 1/3; 8-bit 1/255; float exact),
   not P8's darker tolerance; the G5 decision is PreviewMath::JudgeRedirect (preview-check: target empty + 512 -> 460
   -> fallback).
C2 Every frame: something drawn and the RTV after Render() not ours -> no composite that frame, composite off for the
   session.
C3 After Render() the screen's RTV goes back into slot 0 only if slot 0 still holds ours; the DSV and slots 1-7 stay as
   Render() left them.
C4 The swap-chain line is logged once per menu whatever bAdditiveComposite is (not on VR).
C5 Menu close releases our target only when that menu used it last.
C6 An over-pass that is not scissored (clip off, model not fitted, no frame info, Q3 skip) while the composite is on
   is logged once per menu as "C composite skipped".
