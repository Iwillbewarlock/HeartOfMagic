# Magic Menu Rewrite: adding your own block (Block API guide)

This guide is for SKSE plugin authors who want to put their own panel ("block") into Magic Menu Rewrite's magic menu.
It walks through the example plugin in `examples/MagicMenuBlockExample/` and then covers each part of the API.

The contract is the top comment of `BlockApi.h` (`plugins/SL_MagicMenu/src/BlockApi.h`; a byte-identical copy is in
`examples/MagicMenuBlockExample/src/BlockApi.h`). When this guide and the header disagree, the header wins. Facts taken
from the host's source rather than the header are marked "(host code)". Things nobody has checked are marked
"Not verified".

## What a block is

Magic Menu Rewrite (the "host") lays out blocks in two columns beside SkyUI's spell list. The host's own blocks (the 3D
preview, the spell card) and add-on blocks share these columns. The player can rearrange them in the host's layout mode,
and the arrangement is saved.

All sizes are in stage units of the magic menu movie: a 1280 x 720 stage.

Each block has:

- a box, drawn by the host;
- a title strip at the top of the box, drawn by the host, showing your `title` (no strip when `title` is null or empty);
- below the strip and inside the padding, the content clip: a fresh, empty AS2 MovieClip the host lends you. You draw into
  it with the AS2 drawing API and text fields. Its origin (0,0) is its top-left corner. Your draw callback gets its
  drawable size as `a_width` and `a_height`.

The block's height is fixed: the one you register (finite, > 0, at most `kMaxHeight` = 4000), or a smaller one you ask
for later. The width is the column's, which the player can change, so always lay out from `a_width`.

`pad` (v3) sets the content clip's inset in the box (top - below the title strip when there is one -, left, right and
bottom), in stage units. Any value < 0 (the default, `kPadDefault` = -1) means the host's own pad. A pad that is not a
finite number, or more than 4000, is refused.

The header gives no number for the title strip's height or the host's default padding. Do not hard-code them: lay out
from `a_width` / `a_height`. (The Spell Research add-on in this repo uses a title strip of 22 and a pad of 8 in its own
code; those numbers come from that add-on, not from the API.)

## What you need

- An SKSE plugin built with CommonLibSSE-NG and MSVC for x64.
- A copy of `BlockApi.h`. It includes only `<cstddef>` and `<cstdint>`.

This is a C++ ABI, not a C ABI. The callbacks receive `RE::GFxMovieView*` and `RE::GFxValue&` exactly as
CommonLibSSE-NG lays them out (the header requires a build whose `GFxValue` is the game's 0x18-byte value) and use the
MSVC x64 calling convention. A plugin built with another library or compiler ABI cannot use these callbacks safely.

## Your first block: the example plugin

`examples/MagicMenuBlockExample/src/main.cpp` registers one block, `MagicMenuBlockExample.info`, on the spell tabs. It
shows the name of the spell on the menu's card and its magicka cost, a button that counts clicks, and the key H, which
switches between a full and a compact height.

### Step 1: listen for the host's answer at kPostLoad

```cpp
case SKSE::MessagingInterface::kPostLoad:
    g_listening = SKSE::GetMessagingInterface()->RegisterListener(BlockApi::kPluginName, OnHostMessage);
```

`BlockApi::kPluginName` is `"MagicMenuRewrite"`. SKSE's `RegisterListener` returns false when no plugin of that name is
loaded, so this also tells you whether the host is installed. That is why it is done at `kPostLoad`, when every plugin
is loaded (the header's step 1, line 22).

### Step 2: register at kDataLoaded

```cpp
BlockApi::Registration reg{ .id = kBlockId, .title = kTitle, .height = kHeight,
    .drawSelection = DrawSelection, .click = Click, .tabs = BlockApi::kTabSpells, .key = Key };
messaging->Dispatch(BlockApi::kMessageRegister, &reg, sizeof(reg), BlockApi::kPluginName);
```

- The designated initializers must follow the field order of `Registration`.
- `Dispatch` returns false when the host did not listen (not installed, or `bEnabled=0` in `MagicMenuRewrite.ini`).
- The struct only has to live for the `Dispatch` call. The host copies `id`, `title` and `legacyId`.
- Leave `apiVersion` and `structSize` at their defaults (3 and `sizeof(Registration)`).

### Step 3: read the Result

The host answers during that same `Dispatch`, with a `kMessageResult` message to your plugin. `BlockApi::Result` holds
the status, the block's final id and, when refused, the reason. Its strings live only for the call, so copy them:

```cpp
const auto* result = static_cast<const BlockApi::Result*>(a_msg->data);
const std::string id = result->id ? result->id : "";
if (result->status != BlockApi::Status::kAccepted) { /* log result->reason */ return; }
{ const std::scoped_lock lock(g_idLock); g_finalId = id; }
```

Keep the final id: every later message names your block by it. After `Dispatch` returns, the example checks that an
accepted Result arrived, then sends its key hint and adds a menu open/close event sink.

### Step 4: draw

The example sets `drawSelection`, so the host calls it with the form the card shows. See "Drawing" and "The shown form".

### Step 5: build and test

See "Building" and "Testing" at the end of this guide.

## Drawing

Set either `draw` or `drawSelection` (v2 and v3 refuse a registration with neither). When `drawSelection` is set, `draw`
is never called.

Your draw callback is called, each time into a new, empty clip:

- once after the menu opens;
- when the block's size changes or it moves to the other column;
- on the next UI frame after you send `kMessageRedraw` for it;
- v3 only: once `Context::rowSize` is first measured (header line 140);
- for `drawSelection` only: whenever the shown form changes.

There is no per-frame draw. For per-frame work, see "The frame callback".

Use the AS2 drawing API on the clip through `GFxValue::Invoke`: `lineStyle`, `beginFill`, `moveTo`, `lineTo`, `endFill`,
`createEmptyMovieClip` and `createTextField`. The example's `FillRect` draws a filled rectangle with a border this way.

Text fields: `createTextField(name, depth, x, y, width, height)`, then set a text format with `getNewTextFormat` /
`setNewTextFormat`, using the game's font name `"$EverywhereMediumFont"`. Font mods (for example Korean ones) remap that
name, so text in other languages works. Set `embedFonts` to true. Create the text value with
`a_movie->CreateString(&value, text)`: a `GFxValue` built from a plain `const char*` only points at your string and does
not copy it. The example's `Text` helper does all of this.

Rules for the clip (header lines 47-52):

- It is lent for that call only. Do not keep the `GFxValue`, the movie, or anything you created in the clip after the
  callback returns. The movie dies when the menu closes.
- Do not put AS2 event handlers (`onPress` and so on) on it. Use `click`, or see `kFlagOwnMouse` under "Clicks".

## The shown form

`drawSelection`'s `a_shown` is a FormID:

- the spell a tree click put on the card, otherwise
- the FormID of the list's committed entry, otherwise
- 0 (nothing selected).

Known: on the spell tabs, a spell's or a power's list entry gives its `SpellItem`'s FormID. Not verified in game: what a
shout's entry gives (the host's code reads it as the `TESShout`) and what an Active Effects entry gives (it may be 0 or
another form). So always look the form up and check its type, as the example does:

```cpp
const auto* form = RE::TESForm::LookupByID(a_shown);
const auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
```

The All and Favorites tabs can hold a power or a shout too, even on `kTabSpells`.

## Clicks

Without `kFlagOwnMouse`, the host lays one transparent hit area over your content clip. When the player presses inside
it and releases, your `click` is called once, on the frame after the release, with both points in content clip
coordinates (the same space as your draw).

There is no call (the press is cancelled) when the release is outside the content area or outside the column's visible
part, when between press and release the column scrolled, your block was drawn again or moved, or the layout mode was
entered or left. `click` is never called while the layout mode is on.

The pattern: keep your buttons' rects from your last draw, and act only when the press and the release fall in the same
one. The example keeps `g_button` and counts a click only when both points are inside it, then sends `kMessageRedraw` so
the new count is drawn. Sending `kMessageRedraw` from `click` is fine; sending it from a draw is not.

`click` is mouse only: keyboard keys come through `key` (v3, see "Keys and key hints"). There is no gamepad input for
blocks.

With `kFlagOwnMouse` (v3) the host lays no hit area over your block and never calls `click` (it logs once when both are
set). You put your own AS2 mouse handlers (`GFxFunctionHandler`: `onRelease`, `onRollOver`, `onRollOut`, `hitArea`) on
clips you draw. Those handlers should only record what happened. Act on it in your `frame` callback, and ignore events
while the last `Context::layoutMode` you saw was true.

## Keys and key hints

Set `key` (v3) to receive keyboard key downs. Your callback gets the DirectInput scan code when all of these hold: the
menu is open, no text field takes keys, the layout mode is off, and your block is shown in that frame (it has a place on
the current tab and is not hidden by `kMessageVisible`). The key is delivered on the next UI frame, right before your
block's `frame` call.

- A key pressed while your block is not shown is lost. It is not delivered later.
- The host never consumes a key: the menu and the game still see it. The return value is only logged.
- No gamepad.

Pick a key the magic menu and other blocks do not use. By default the host uses K (37) to turn the layout mode on and
off and Backspace (14) to reset the layout, but Backspace only while the layout mode is on; the Heart of Magic block uses
U (22) and G (34). The key notes in the shipped `MagicMenuRewrite.ini` also avoid R (19, Spell Organizer's remove-spell
key in this menu), N (49, Creature Framework), O (24, Open Animation Replacer and OBody) and Home (199, dMenu). Do not take J (0x24): the game's
controlmap binds it to Journal. The example uses H (0x23), which is in no line of the controlmap the host's keys were
checked against (the installed CoMAP copy; the vanilla controlmap was not checked separately). Some mods bind H outside
menus (Horse Whistle, for example); since the host never consumes a key, any mod that listens to H in menus sees it too.

The example's `Key` toggles the compact height:

```cpp
bool Key(std::uint32_t a_scanCode, void*) noexcept
{
    bool used = false;
    Guarded("key", [&] {
        if (a_scanCode != kToggleKey) return;
        const bool compact = !g_compact.load();
        g_compact = compact;
        SendHeight(compact ? kCompactHeight : kHeight);
        used = true;
    });
    return used;
}
```

`Guarded` runs the body in a `try` / `catch` and logs what it caught, so nothing is thrown out of the callback.

Key hints (`kMessageKeyHint`, v3): a line in SkyUI's bottom key-hint strip with the key's icon and your label. Send a
`BlockApi::KeyHint` once the Result gave you the final id, for example right after registering at `kDataLoaded`:

```cpp
BlockApi::KeyHint hint{ .id = id.c_str(), .scanCode = kToggleKey, .label = kToggleLabel };
messaging->Dispatch(BlockApi::kMessageKeyHint, &hint, sizeof(hint), BlockApi::kPluginName);
```

- It is a standing declaration for the game session. Send it once, not at every menu open.
- Sending the same block and scan code again replaces the label. A null or empty label removes that hint.
- It shows while your block is shown on the current tab and the layout mode is off. It never shows while SkyUI shows
  gamepad buttons.
- The label is UTF-8, copied, cut to 64 bytes (`kMaxHintLabelBytes`) at a character boundary.
- At most 4 hints per block (`kMaxKeyHints`). Scan codes are 1 to 255.
- SkyUI's panel holds 6 buttons. A hint with no free button is dropped and logged.
- An older host ignores the message.

## Changing the height

`kMessageHeight` (v2) sets your block's height for this menu, for content that needs less room:

```cpp
BlockApi::HeightRequest request{ .id = id.c_str(), .height = a_height };
messaging->Dispatch(BlockApi::kMessageHeight, &request, sizeof(request), BlockApi::kPluginName);
```

- The registered height is the maximum. A request that is not finite, <= 0, or more than the registered height is
  refused (logged).
- Every magic menu opens at the registered height. The example resets its compact flag in a `MenuOpenCloseEvent` sink
  for that reason.
- A larger height, or the height the block has now, applies on the next UI frame and drops a smaller one that is still
  waiting. A smaller height waits about 150 ms first (the host's shrink delay, `kShrinkDelayMs` in `BlockRules.h`), so a
  block that grows back at once does not make the columns jump. In the layout mode every height applies at once. The
  example's compact toggle is that shrink case: after H, the block gets smaller about 150 ms later. Going back to the
  full height is at once.
- When the height applies, the columns are laid out again and your block is drawn again at the new size. Do not send the
  same height again from that draw, or the block resizes every frame.
- Several requests before that frame: the last one counts.
- The saved layout file is not changed.
- Ignored while the magic menu is closed.

The height is the whole block's, including the title strip and the padding. Your next draw gets the smaller `a_height`.
The example draws its second line only when `a_height` leaves room for it.

## Showing a spell on the menu's card

`kMessageShowSpell` (v3) asks the host to show a spell as if the player clicked it in your block: selected in the list
when it is there, otherwise on the menu's card (and in the 3D preview when that setting is on).

```cpp
BlockApi::ShowSpellRequest request{ .id = id.c_str(), .formId = spellFormId, .requestId = ++g_requestId };
```

- `formId` must not be 0. `nameShown = 0` makes the card hide the spell's name.
- Queued and taken on the next UI frame. At most one request is served per frame: the newest sent by any block; the
  others are dropped (one log line).
- Dropped when the list selection changed after it was queued (the player's newer choice wins) and while the layout mode
  is on.
- Not answered. The next `Context` shows the result: `source == SelectionSource::kAddon` and `servedRequest` equals your
  `requestId`.

## Hiding and showing the block

`kMessageVisible` (v3) with a `VisibleRequest`: `shown = 0` hides your block but keeps its place in the column;
`shown = 1` shows it again.

- Queued and taken on the next UI frame, and taken again after that frame's draws, so a request sent from a draw applies
  before the frame is shown. A block can hide itself from its first draw.
- Requests are applied in order. Only a change does anything.
- Every menu opens with the block shown.
- A hidden block is still drawn, but gets no `frame` call and no keys, and cannot be grabbed in the layout mode.

Separately, `tabs` decides on which SkyUI tabs the block appears at all. `kTabsAll` (0) means every tab.
`kTabSpells` is every tab the host drew its spell tree on: All, Favorites, the five school tabs and any category it does
not know (a menu mod's own). `kTabPowers`, `kTabShouts` and `kTabActiveEffects` are those tabs alone. On other tabs the
block is hidden and the blocks below it close up. A bit outside `kTabsKnown` is refused.

`kFlagKeepWhenEmpty` (v3): the block keeps its place and box when the menu shows no spell. Without it, a block that sets
`drawSelection` closes up (is not drawn, the blocks below move up) while no spell is selected - for example right after
the menu opens - and shows again once one is (host code: BlockRulesV3.h `ClosesWhenEmpty`). In the layout mode every block
shows, so it can be moved. If your block looks missing until you open the layout mode, this is why: set the flag when your
block has something to show without a spell (the example sets it for its "Select a spell" line and its button).

## The frame callback and Context

`frame` (v3) is called once per UI frame while the menu is open, for every shown block that sets it, after the host's
selection, tab and layout work of that frame. It is called in the layout mode too.

```cpp
void Frame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, void* a_user);
```

- `a_content`: your block's present content clip.
- `a_overlay`: a clip of your column above every panel in it, with its origin on your content clip's origin, for a
  tooltip or a hover mark. It is null in the layout mode and when the host has none this frame. The host clears it when
  the block is hidden, moved, scrolled or drawn again, when the layout mode starts, and when a panel is made above it.
- Both clips are lent for the call only, like the draw clip.
- No `frame` call for a block hidden by `kMessageVisible`, and none while it has no content clip yet.

`Context` lives for the call only (its `tabSchool` string too). Read only the fields `structSize` covers; a later host
may add fields at the end.

| Field | Meaning |
|---|---|
| `shown` | as `drawSelection`'s `a_shown` |
| `listForm` | the list's committed entry's form, 0 = none |
| `selectionSeq` | bumped by every change of the list selection (A to C to A between two frames still bumps it) |
| `source`, `servedRequest` | what made the last selection: `kList` or `kAddon` (a served `kMessageShowSpell`, its `requestId`) |
| `tabBits` | the current tab's `kTab*` bit |
| `tabSchool` | "Alteration" .. "Restoration" on a school tab, else "" |
| `layoutMode` | true while the layout mode is on |
| `overlayGeneration` | changes whenever the overlay was cleared: a cached "my tooltip is up" is stale once it changed |
| `contentGeneration` | bumped each time the block is drawn into a new content clip |
| `viewLeft` .. `viewBottom` | the part of your content clip inside the column's visible area (all 0 when none shows) |
| `rowSize` | the SkyUI list's row text size (0 until measured; every v3 block is drawn again once it is known) |
| `lineFirst`, `linePitch` | the game font's first line height and per-line step, per size unit (fallback 1.25) |
| `frameSeq` | the host's UI frame number this menu, from 1; read it only when `structSize >= kContextFrameSeqNeeded`; 0 = a host that does not send it |

`frameSeq` is the same for every `frame` call of one frame, so a plugin with several blocks can do its shared work once
per frame.

## The id and the saved layout

- 1 to 128 characters (`kMaxIdLength`). An id without a `.` becomes `"<your SKSE plugin name>.<id>"`; the Result gives
  the final id. The example uses `MagicMenuBlockExample.info`, which already has a `.`.
- The id is the block's key in `MagicMenuRewrite_Layout.json` (host code: `Data/SKSE/Plugins/MagicMenuRewrite_Layout.json`;
  under MO2 it lands in Overwrite). Keep it the same across versions of your plugin, or the player's place for your
  block is lost.
- A block without a saved place goes to the end of the right column.
- When your plugin is uninstalled, its saved place is kept, and the block is skipped while absent.
- Players can leave a block out of the menu with `[Blocks] sHidden` in `MagicMenuRewrite.ini` (comma-separated ids).

`legacyId` (v3) is only for taking over one of the host's retired built-in blocks ("tree", "progress", "nodecard"). Leave
it null.

## Threads and lifetimes

- `draw`, `drawSelection`, `click`, `frame` and `key` are called on the UI thread, from the magic menu movie's frame
  handler, and never from inside another of your callbacks.
- Messages to the host (registration, `kMessageRedraw`, `kMessageHeight`, `kMessageShowSpell`, `kMessageVisible`,
  `kMessageKeyHint`) may be sent from any thread.
- So data your callbacks share with other threads must be guarded. In the example, the final id is written by the Result
  handler during registration and read when messages are sent, so it is under a mutex. The compact flag is changed by
  `key` and reset by the menu event sink (whose thread the API does not cover), so it is atomic. The button rect and the
  click count are touched only by `drawSelection` and `click`, both on the UI thread, so they need no guard.

Do not:

- keep a lent clip, the movie, or anything made in them after the callback returns;
- put AS2 handlers on the content clip (only on your own clips, with `kFlagOwnMouse`);
- throw out of a callback. The host catches it and logs it, and your block is not drawn or clicked again in that menu.
  The example wraps every callback body in a `try` / `catch`;
- send `kMessageRedraw` from any draw of your block, or the block is drawn every frame;
- send `kMessageHeight` again from the resulting draw unless the height you need changed.

## Registration timing

1. At `kPostLoad`: register your listener for `"MagicMenuRewrite"`. The host starts listening for registrations at
   `kPostLoad` too.
2. At `kPostPostLoad`, `kInputLoaded` or `kDataLoaded`: send one `kMessageRegister` per block.
3. Registration closes at the first magic menu open after `kDataLoaded`. Later registrations are refused.

The host sends the Result during your `Dispatch`, so when `Dispatch` returns you already know the outcome.

## The Result and common refusal reasons

`Result::apiVersion` is the version your registration was read as, or the host's `kVersion` when it was refused before
that. On refusal, `Result::reason` says why. The reasons (host code, `BlockRegistry.cpp`):

| Reason | What to do |
|---|---|
| registration is closed: the magic menu was opened already | register by `kDataLoaded` |
| no Registration in the message | pass `&reg` and `sizeof(reg)` |
| apiVersion N is not one this MagicMenuRewrite reads (...): update MagicMenuRewrite | the player's host is older than your header |
| structSize / message size is smaller than the v1 Registration | do not change `structSize`; pass `sizeof(reg)` |
| the id is longer than 128 characters / the id is empty | fix the id |
| the height ... is not a finite number in 0 < height <= 4000 | fix the height |
| no draw callback | set `draw` or `drawSelection` |
| tabs / flags 0x... has bits this MagicMenuRewrite does not know: update MagicMenuRewrite | use only known bits, or the host is older |
| the pad ... is not a finite number <= 4000 | leave `pad` at `kPadDefault` (-1) |
| the id is a built-in block's / the id is registered already (by ...) | choose another id |
| the legacyId ... | leave `legacyId` null |

For refusals before the id is read (closed, no message, bad version or size, id too long or empty), `Result::id` is
`""` (header line 366). For later refusals it is the id as you sent it.

## Building

### Inside this repository

In the HeartOfMagic super-build, the root `CMakeLists.txt` adds the example with
`add_subdirectory(examples/MagicMenuBlockExample)`. With the configured `build` folder:

```
cmake --build build --config Release --target MagicMenuBlockExample
```

The DLL is `build/examples/MagicMenuBlockExample/Release/MagicMenuBlockExample.dll`. The example is not part of any
release package, and in the super-build it is left out of the ALL build (the `EXCLUDE_FROM_ALL` target property), so
only `--target MagicMenuBlockExample` builds it.

`add_commonlibsse_plugin` generates a source file that declares the plugin with `"..."sv` literals and does not bring in
`std::literals` itself. The example's precompiled header `src/PCH.h` has `using namespace std::literals;` for it; without
that line the build fails with C3688 on the generated `__MagicMenuBlockExamplePlugin.cpp`.

### On its own

Copy `examples/MagicMenuBlockExample/` to a folder of your own. The standalone build compiles CommonLibSSE-NG from a
source tree, the way this repository does (`plugins/cmake/commonlibsse.cmake`). Its packages come from vcpkg in
manifest mode.

You need Visual Studio with the C++ workload, CMake 3.25 or later, and vcpkg. Visual Studio's bundled vcpkg works
(`<Visual Studio>/VC/vcpkg`).

Before you build it as your own block, rename it. Two plugins that register the same block id are not both shown: the
second is refused with "the id is registered already". Change:

- `project(MagicMenuBlockExample ...)` in `CMakeLists.txt`: the target and the DLL name;
- `NAME` in `add_commonlibsse_plugin`: the SKSE plugin name, which the host sends the Result to and which names your log
  file;
- `AUTHOR` there;
- `kBlockId` in `src/main.cpp`: the block id, also the key of the block's place in the saved layout;
- `kTitle` there: the text in the block's title strip.

The log lines in `main.cpp` start with "MagicMenuBlockExample:"; change them too, so your log is not mixed up with the
example's.

1. Get CommonLibSSE-NG 10.0.0, the version this example was built with. By default `CMakeLists.txt` looks for it in
   `external/CommonLibSSE-NG`:

   ```
   git clone --branch v10.0.0 --recursive https://github.com/alandtse/CommonLibVR.git external/CommonLibSSE-NG
   ```

   The tag `v10.0.0` on that repository is commit d61bca4, the commit this repository's submodule uses (checked with
   `git ls-remote --tags`). It is the URL in this repository's `.gitmodules`; the `ng` branch there is CommonLibSSE-NG.

   As a git submodule instead, the folder has to be a git repository first:

   ```
   git init
   git submodule add https://github.com/alandtse/CommonLibVR.git external/CommonLibSSE-NG
   git -C external/CommonLibSSE-NG checkout v10.0.0
   git submodule update --init --recursive
   ```

   Do not take the `ng` branch's newest commit (`-b ng`) unless you also update `vcpkg.json` (see below): it is a later
   CommonLibSSE-NG than 10.0.0.

   Or point to a checkout you already have with `-DCOMMONLIBSSE_PATH=<path>`.

2. Configure. The triplet must be `x64-windows-static`, because the plugin links the static MSVC runtime. It is the
   default when this folder is the top-level project, so `-DVCPKG_TARGET_TRIPLET` can be left out:

   ```
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
     "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake"
   ```

   Keep the quotes: Visual Studio's own vcpkg sits under `C:/Program Files/...`, and the space would split the argument.

   With Visual Studio 2026, use `-G "Visual Studio 18 2026"`. That generator needs CMake 4.2 or later (CMake's own
   documentation: "versionadded 4.2").

3. Build:

   ```
   cmake --build build --config Release --target MagicMenuBlockExample
   ```

   The DLL is `build/Release/MagicMenuBlockExample.dll`. After you renamed the project (the list above), use your own name
   in both places: `--target <your project name>` and `build/Release/<your NAME>.dll`.

What the files do:

- `vcpkg.json` lists the packages CommonLibSSE-NG 10.0.0's own `vcpkg.json` asks for, with the same minimum versions:
  directxmath, directxtk, fmt, nlohmann-json, rapidcsv, simpleini, spdlog, toml11 and xbyak (not its `tests` feature).
  When you update CommonLibSSE-NG, compare this list with its `vcpkg.json` again.
- `vcpkg-configuration.json` has only the Microsoft registry, at the baseline from CommonLibSSE-NG 10.0.0's own
  `vcpkg-configuration.json` (00c5775211f45cd08b37fce0484b4cb940e422ab).
- `CMakeLists.txt` sets the options this repository sets for CommonLibSSE-NG: SE and AE on, VR off, Xbyak on, tests off,
  and `SKSE_SUPPORT_PATCH_SAFETY` off (when it is on, every fresh configure downloads MinHook's hde64 from GitHub). It
  adds the library with `add_subdirectory(... EXCLUDE_FROM_ALL)` and includes its `cmake/CommonLibSSE.cmake`, which
  defines `add_commonlibsse_plugin`. Before `project()` it sets the triplet to `x64-windows-static` when none is given.

Long paths: if a vcpkg package fails with `fatal error C1083: Cannot open compiler generated file: '': Invalid argument`,
a path inside vcpkg's build tree is too long for the compiler. It was 245 characters when this happened here. Put the
build folder at a short path, or move vcpkg's build tree with
`-DVCPKG_INSTALL_OPTIONS=--x-buildtrees-root=<short path>`.

How this was checked (2026-11-01):

- CommonLibSSE-NG 10.0.0 (commit d61bca4) from this repository's submodule, given as `COMMONLIBSSE_PATH`.
- Visual Studio 18 2026 generator (CMake 4.2.3) and Visual Studio's bundled vcpkg, with `-DVCPKG_TARGET_TRIPLET` given
  and `--x-buildtrees-root` set to a short folder.
- vcpkg installed 11 packages: the 10 in `vcpkg.json` and vcpkg-cmake, which they need. 9 were built from source and 2
  came from vcpkg's local binary cache. Configure took about 2 minutes and the build about 9 minutes, most of it
  CommonLibSSE-NG.
- No warnings. The DLL exports `SKSEPlugin_Load`, `SKSEPlugin_Query` and `SKSEPlugin_Version`.
- The triplet default was checked later with a fresh configure without `-DVCPKG_TARGET_TRIPLET`: the cache holds
  `x64-windows-static` and vcpkg installed for it (configure only, not built).
- Not tried: the clone and submodule commands in step 1, the default `external/CommonLibSSE-NG` path and the Visual
  Studio 2022 generator.

## Testing

1. Install the DLL in `Data/SKSE/Plugins/` (as an MO2 mod), next to Magic Menu Rewrite.
2. Start the game, load a save and open the magic menu.
3. Read the logs in `Documents/My Games/Skyrim Special Edition/SKSE/` (the folder for SE/AE from Steam; other editions
   use another "My Games" folder name - check yours):
   - `MagicMenuRewrite.log`: "MagicMenuRewrite: add-on block registrations open" at load, then your block's line:
     `Widget: add-on block MagicMenuBlockExample.info registered by MagicMenuBlockExample (120 high)`, or
     `Widget: add-on block <id> from <plugin> refused - <reason>` (host code). Height requests, clicks and used keys
     are logged there too.
   - `MagicMenuBlockExample.log`: the example's own lines ("listening to MagicMenuRewrite: yes", "block ... accepted",
     "button clicked"). CommonLibSSE-NG 10's `SKSE::log::init()` only writes a file in builds with AE support enabled
     (`ENABLE_SKYRIM_AE`, on in this repository); a build without it has no example log file.
4. In the menu: the EXAMPLE block at the end of the right column (without a saved place), the spell's name and cost, the
   button counting clicks, H switching the height, and "Compact view" in the bottom key-hint strip.

Not verified: the example has not been run in game. Only its two builds (inside this repository and on its own) were
checked.

## Versions

| Header | apiVersion | Adds |
|---|---|---|
| v1 | 1 | `draw`, `title`, `height`, `user` (`RegistrationV1`, frozen) |
| v2 | 2 | `drawSelection`, `click`, `tabs`; `kMessageRedraw`, `kMessageHeight` (`RegistrationV2`, frozen) |
| v3 | 3 | `frame`, `key`, `pad`, `flags`, `legacyId`; `kMessageShowSpell`, `kMessageVisible`, `kMessageKeyHint` |

- A host with the v3 header reads apiVersion 1, 2 and 3. A v1 or v2 add-on built against its own header keeps working
  unchanged, its messages included.
- Version 4 or later is refused with "update MagicMenuRewrite".
- The host reads only the fields that both `structSize` and the message size cover, for that version's own layout.
  Missing fields read as null / 0 (`pad` as -1).
- `kMessageHeight` (`'SLMH'`) needs a host that knows it. An older host ignores it, and the block keeps its registered
  height. `HeightRequest` is read at apiVersion 2 and 3.
- `kMessageKeyHint` came later than the rest of v3 (2026-10-31). An older host ignores it.
- `Context::frameSeq` was added at the end of `Context` (2026-10-31): read it only when
  `structSize >= kContextFrameSeqNeeded`.
- The v3 messages (`kMessageShowSpell`, `kMessageVisible`, `kMessageKeyHint`) must carry apiVersion 3.
