# SL_MagicMenu prototype, build 11 (2026-10-26)

Read `2026-10-26-magic-menu-proto-v10.md` first; ledger `Desktop/Claude outputs/ACF-Phase0/M0/video7/LEDGER.md`. Build 10
is deployed (Case 218).

Build 10 game run 06:12: the dark faceted cloud behind the art is unchanged (user screenshot); P8 logged SKIP 8 times (the
bound target is not 8-bit RGBA/BGRA, so nothing was measured). User, same day: "트리모양을 출력할때 HoM 설정을 따라가도록해",
"직접 표시되는건 선택된 마법과 직접적으로 연결된 노드만, 나머지는 반투명으로"; asked about HoM's two differing rules, they
chose: full opacity = direct links only (not HoM's whole ancestor/descendant path), and every node in the window drawn
(not HoM's discovery-mode hiding).

Two independent parts, different files: H (Preview.cpp, PreviewMath.h, preview-check) and T (Widget.cpp, a new HomStyle
reader, Settings, TreeData, tree-check).

## H Dark cloud: build 11 measures, the rendering is not changed

Evidence so far: the healing art showed the cloud while every shape drawn was additive (its only normal-blend shape,
Cylinder08:1, is hidden); additive draws cannot darken their own target, so the darkening is a later composite of the
target's alpha (H6) or the draws were not additive. The fire art adds one normal-blend smoke particle system
(pFireballCore07, H7), which cannot explain the healing art. The dirty-flag fix (H1) did not remove the cloud. GPT
diagnosis round 2: `video9/../` scratchpad `codex_halo2_result.md` (H6 first, UNVERIFIED).

H1 At the P8 check, log once per menu what the bound target is: RTV view format, resource format, sample count, mip,
   size; and the blend state bound at Render() entry and after it (OMGetBlendState: BlendEnable, Src/Dest/Op for RGB
   and for alpha, RenderTargetWriteMask, IndependentBlendEnable; a null state logged as D3D's default).
H2 ReadBoxPixels keeps 4 channels and reads R8G8B8A8 / B8G8R8A8 (UNORM, SRGB, TYPELESS by the view's typed format),
   R16G16B16A16_FLOAT (half -> float) and R11G11B10_FLOAT (XMLoadFloat3PK; alpha = none). Values as float, channel
   order normalised to RGBA. Other formats: SKIP naming the format.
H3 CountDarker -> float with a per-format epsilon (8-bit: 24/255; float formats: 0.05 absolute and 10 % relative of
   the larger value), reporting separately: RGB-darker pixels (+bounds), alpha-increased and alpha-decreased pixels
   (+bounds) when the format has alpha, RGB-unchanged-but-alpha-increased pixels, min/max, NaN/Inf count.
   Verdict line `P8 dark` stays (PASS = no RGB-darker pixel); a new line `P8 alpha` says NO_ALPHA_STORAGE /
   NO_ALPHA_CHANGE / ALPHA_CHANGED (counts) - the first two rule H6 out for this target, the third keeps it.
H4 The MarkDrawStatesDirty comment says the cause is unproven (tried in build 10, cloud unchanged). The flags stay.
H5 No other change to drawing. The black-disc fallback is not built until a measurement says what the cloud is.

Tests: preview-check DarkCompare on the float counter: 8-bit, half-float and R11G11B10 inputs; alpha-only change;
NaN; epsilon edges.

## T Tree: Heart of Magic's look and settings

Source of truth (investigation 2026-10-26, file:line in the session report): HoM's canvas renderer
(`PrismaUI/views/SpellLearning/SpellLearningPanel/modules/canvasRenderer*.js`, `treeStyle.js`, `nodeBatch.js`). Values
come from the live `Data/SKSE/Plugins/SpellLearning/config.json` and the active design preset
`Data/SKSE/Plugins/SpellLearning/presets/design/<designPreset>.json` (`tree` block; user: Skald), with `treeStyle.js`
DEFAULTS for keys the preset lacks. Both files read with nlohmann_json at menu open (read-only, missing/invalid file or
key -> the defaults, logged once). Reader in its own file (HomStyle.cpp/.h) so the tools can test it.

T10 Layout: node x/y as HoM draws them - no per-school rotation (HoM rotation 0, focusRotate off); build 10's T1
    rotation is dropped. One scale (fTreeScale), the centre node at the window's middle.
T11 Opacity: full alpha for the centre (the spell chosen in the list / clicked) and its direct structural neighbours
    (one link, either direction); every other node, its label and its edges get alpha x 0.6 (HoM DIM_SAME_SCHOOL,
    multiplied with the state's own alpha). Clickability does not change (build 10 rule).
T12 Node shape by school (nodeBatch SHAPES: Destruction diamond, Alteration hexagon, Conjuration pentagon, Illusion
    triangle, Restoration/other circle), turned toward the tree origin (atan2(y, x) + ROTATION) as HoM does.
    Size by state in HoM world units x fTreeScale x fTreeMarkScale: unlocked/learning 12, available 9, locked 7,
    selected +1.5; available gets the extra ring (size + 4, alpha 0.6, width 1.2) when the preset says availableRing.
    fTreeMarkScale default becomes 1.0 (a multiplier on HoM's sizes). The implementer checks in nodeBatch.js /
    canvasRendererNodes.js whether "size" is a radius and states it; the spacing test then uses the largest reach
    (available ring) - must stay under half the closest pair (28.4 at 0.75) or the tree-check FAILs.
T13 State styles (Skald values, preset keys): unlocked fill = school colour inked (schoolInk toward schoolInkTone),
    stroke school colour 1.5, inner core 0.5x; learning fill learningPathColor, stroke school 1.5, white dot 0.4x;
    available fill nodeFill, stroke school 1; locked: discoveryMode on -> HoM's mystery look (dashed outline school
    colour x0.4, alpha 0.6, mysteryFill, "?" inside), off -> nodeFill, lockedStroke dashed, alpha 0.4. Selected/hovered
    stroke focusStroke. Our states map: kMastered -> unlocked; kLearning, kTarget -> learning; kReady, kAvailable ->
    available; kLocked, kUnknown -> locked. The learning percent stays in the tooltip.
T14 Edges: locked lockedEdgeColor w1 a lockedEdgeAlpha; frontier (learned -> available/learning) school colour w1.25 a
    frontierEdgeAlpha; both learned school colour w unlockedEdgeWidth a unlockedEdgeAlpha. With showSelectionPath: the
    centre's links to its direct neighbours in selectedPathColor w selectedPathWidth a selectedPathAlpha, all other
    edges dimEdgeColor w1 a0.08 (HoM's dim batch). The learning target path (build 10 T8) on top in learningPathColor
    w2 a0.7. showBaseConnections off -> only both-learned and frontier edges (and the two paths). Lock (hard
    prerequisite) chains only for the centre's hard prerequisites, as HoM.
T15 Labels: showNodeNames off -> none. Locked nodes no label. Available with progress < revealName % (and not
    cheatMode) -> "???" in labelHidden; available labelAvailable; unlocked labelUnlocked; learning learningPathColor.
    Max 18 characters (labelMaxChars, then "…"). Our reading-size rule stays (never under the list's row size); the
    overlap rule of build 10 stays (centre, selected, target first).
T16 Tooltip follows the same reveal rule: a locked or hidden node shows "???" as its name (no name HoM hides), its
    school and state still shown.
T17 schoolColors from config (fallback HoM's defaults); schoolVisibility[school] == false -> that school not drawn.
T18 Legend: the new state looks in one line (unlocked, learning, available, locked).
T19 Settings log line adds the HoM style read: preset name, file found or default, discoveryMode, showSelectionPath,
    showNodeNames, showBaseConnections, revealName, learningPathColor.

Not ported (no effect on our window or not asked): focusZoom/focusOnClick camera moves, school dividers and chapter
titles, page grain/ornaments, label halo, LOD levels, hover path.

## Tests

- New offline checks for HomStyle: preset parse with a full Skald block, a preset missing keys (defaults), a broken
  file (defaults + reason), colour parsing ("#rrggbb", "rgba(r,g,b,a)"), ink mix value, state mapping.
- tree-check: no rotation in the window query; opacity set = centre + direct neighbours; spacing at the defaults with
  the largest reach.
- preview-check: label reveal rule ("???" under revealName, none for locked).
- In game (log): Settings + HomStyle line, `tree redraw` with full/dim counts, P8 dark / P8 alpha with the format.
  Eyes only: does the tree look like HoM's panel; the cloud (unchanged in this build by design).

## Design review round 1 (GPT gpt-6-astra, 2026-10-26) - all 12 points checked and taken; these override the text above

R1 (T13) Display state is not the behaviour enum alone: the learning target (any progress, incl. 100 % not yet learned)
   -> learning; kReady not the target -> available; kLearning that is not the target (learned early, HoM still
   counting) -> available (HoM cppCallbacks.js:1033 keeps it available); kMastered -> unlocked; kLocked/kUnknown ->
   locked. The reveal rule (T15/T16) uses this display state. Tests: 100 % un-learned target, non-target early-learned.
R2 (H3) No verdict that rules H6 out: `P8 alpha` reports NO_ALPHA_STORAGE (format without alpha) / NO_SIGNIFICANT_ALPHA_DELTA /
   ALPHA_CHANGED with counts and the largest alpha change; alpha epsilon is its own, finer (8-bit 2/255, float 0.005).
   H6 stays open until the later composite of that target is checked.
R3 (H3) Any NaN/Inf in before or after -> `P8 dark INCONCLUSIVE` and `P8 alpha INCONCLUSIVE` (never PASS); tests for NaN
   and +/-Inf on each side and an all-invalid box.
R4 (T13) Every school colour is inked (schoolInk toward schoolInkTone) before use in fill, stroke, ring and edge; the
   learning node's outer ring is hubRing (Skald #cfc9b8, canvasRendererSpell.js:108 _heartRing), not the school
   colour; a node's own themeColor (if spell_tree.json has it) wins, as canvasRendererSpell.js:71.
R5 (T12) "size" is a radius (canvasRendererNodes.js:54). Locked nodes in the mystery look take HoM's tier sizes 7..11
   (canvasRendererNodes.js:66-75). The hard-prerequisite shell/ring decorations (canvasRendererSpell.js:207-237) are
   NOT ported (the centre's lock chains stay). The spacing test uses the largest reach of what is drawn (mystery 11 +
   1.5 selected, available ring 9 + 4, + half line) x fTreeScale x fTreeMarkScale against half the closest pair
   (37.9 x 0.75 / 2 = 14.21). The learning path edge into the target is width 3 (canvasRendererEdges.js:343).
R6 (T13) Hover: no redraw. Each node clip gets a hidden focus-outline child (focusStroke) drawn with it; onRollOver /
   onRollOut toggle its _visible. The +1.5 size step is for the selected node only (hover keeps its size: a hover
   resize would need a redraw).
R7 (T) Preset lookup as HoM: scan presets/design/*.json and pick the file whose "id" equals designPreset (case-
   insensitive); none found -> treeStyle.js DEFAULTS, logged ("preset <id> not found, HoM's built-in presets are not
   read by the prototype"). Tests: file missing, id not matching, key missing.
R8 (H2) Readback failures logged by reason (RTV dimension / array / sample count / format / API + HRESULT where there is
   one). MSAA: resolve the whole subresource to a single-sample USAGE_DEFAULT texture of the typed RTV format, then copy
   the box to STAGING. Typeless resources are read by the RTV's typed format (R8 and R16 families).
R9 (H1) The blend log also has BlendFactor[4], SampleMask, AlphaToCoverageEnable, and is labelled "at Render() entry/exit
   (boundary state, not per draw)"; logged per selection change with the P8 check, not once per menu.
R10 (T) Scaleform: polygons from vertex lists; dashed closed outlines keep the dash phase around the whole outline (a
   new helper beside Draw.h's straight dash); fill and dashed stroke drawn separately. Alpha is applied once: the dim
   factor x the state alpha goes into each drawing call's alpha (0..100), never also into the clip's _alpha.
R11 (T11) The full-opacity set is the centre plus nodes joined to it by one STRUCTURAL edge (either direction), built
   apart from the clickability steps (TreeData's BFS also walks lock edges). Tests: lock-only neighbour (dim), reverse
   structural neighbour (full), 2 hops (dim).
R12 (T10) Wording: HoM draws the saved x/y with its wheel rotation, which is 0 by default and unchanged by
   focusRotate=false; the prototype draws the saved x/y unrotated. Widget.cpp's rotation argument and tree-check's
   coordinate / badge-direction expectations change with it; add a non-square window case (raw +x/+y, window in/out,
   edge ends, badge sectors).
