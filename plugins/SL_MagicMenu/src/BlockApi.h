#pragma once

// =============================================================================
// BLOCK API v3 - how another SKSE plugin adds its own block to SL_MagicMenu's
// magic menu panels (blocks plan Task 6, design review D9; v2: Spell Research
// add-on design 2026-10-27, "Changes / 1"; v3: HoM add-on design 2026-10-30,
// "Block API v3"). Copy this header into your plugin;
// it needs nothing but <cstddef> and <cstdint> (the two RE classes are only
// declared here, CommonLibSSE-NG defines them).
//
// THIS IS A CommonLibSSE-NG C++ ABI INTERFACE, NOT A C ABI. The callbacks get
// RE::GFxMovieView* and RE::GFxValue& as CommonLibSSE-NG lays them out: build
// your plugin with CommonLibSSE-NG (any build whose GFxValue is the game's
// 0x18-byte value) and the MSVC x64 calling convention.
//
// Versions: an SL_MagicMenu with this header reads apiVersion 1 (RegistrationV1, frozen: the v1 header's Registration),
// 2 (RegistrationV2, frozen: the v2 header's Registration) and 3 (Registration). A v1 or v2 add-on built against its own
// header keeps working unchanged, its messages included ('SLMD'; 'SLMH' stays readable at apiVersion 2). Version 4 or
// later is refused with a reason ("update SL_MagicMenu").
//
// Registering:
//   1. At SKSE load, listen for SL_MagicMenu's answer:
//        SKSE::GetMessagingInterface()->RegisterListener(BlockApi::kPluginName, OnBlockResult);
//   2. At kPostPostLoad, kInputLoaded or kDataLoaded (SL_MagicMenu starts listening at kPostLoad; registration closes at
//      the first magic menu open after kDataLoaded - later ones are refused with a reason), send one message per block:
//        BlockApi::Registration reg{ .id = "MyMod.stats", .title = "STATS", .height = 120.0f, .drawSelection = DrawStats,
//                                    .tabs = BlockApi::kTabSpells };
//        SKSE::GetMessagingInterface()->Dispatch(BlockApi::kMessageRegister, &reg, sizeof(reg), BlockApi::kPluginName);
//      Dispatch returns false when no SL_MagicMenu listened (not installed, or bEnabled=0). The struct only needs to live
//      for the Dispatch call: SL_MagicMenu copies id, title and legacyId. Only the fields of the version's own layout that
//      structSize and the message size both cover are read: a struct cut short reads its missing fields as null / 0 (pad as
//      -1), and a v1 or v2 struct that grew past its layout is never read as later fields.
//   3. The answer comes back, during that Dispatch, as a kMessageResult message to the sender (BlockApi::Result: accepted
//      or refused, the block's final id and the reason; its apiVersion is the version your registration was read as, or
//      this SL_MagicMenu's kVersion when it was refused before that). Its strings live only for that call.
//
// The id: unique, 1..kMaxIdLength characters. An id with no '.' is made "<your plugin name>.<id>" (the result says the
// final id). It is the block's key in SL_MagicMenu_Layout.json: keep it the same across versions or the player's place
// for your block is lost. A block without a saved place goes to the end of the right column; when your plugin is not
// installed any more, its saved place is kept (the block is skipped while absent).
//
// The block: fixed height (stage units, 1280 x 720 stage; finite, > 0, <= kMaxHeight); the width is the column's, which
// the player can change. SL_MagicMenu draws the block's box and its title strip (no strip when title is null or empty)
// and lends you a fresh, empty clip below the title strip, inside the padding (the "content clip"): draw there with the
// AS2 drawing API (beginFill / lineTo / createTextField ...). a_width / a_height are that clip's drawable size.
//
// Threads and lifetimes: draw, drawSelection, click, frame and key are called on the UI thread, from the magic menu movie's
// frame handler, never from inside another of your callbacks. The clip is lent for the draw call only: do not keep the GFxValue,
// the movie or anything made in it past the return (the movie dies when the menu closes), and do not put AS2 handlers
// (onPress ...) on it - use click. Do not throw out of a callback (an exception is caught and logged, and the block is not
// drawn or clicked again in that menu). Messages to SL_MagicMenu (registration, kMessageRedraw, kMessageHeight,
// kMessageShowSpell, kMessageVisible) may come from any thread.
//
// When the block is drawn (draw, or drawSelection when it is set - then draw is never called): once after the menu opens,
// again when the block's size changes or it is moved to the other column, and on a kMessageRedraw for it - each time into a
// NEW empty clip. drawSelection also gets the form the menu's card shows (a_shown: the spell a tree click put on the card,
// else the FormID of the list's committed entry, else 0) and is called again whenever that form changes (in the same frame
// as the host's selection handling, or the next). Known: on the spell tabs a spell's or power's entry is its SpellItem's
// FormID. NOT verified in game: what a shout's entry gives (the host's code reads it as the TESShout) and what an Active
// Effects entry gives (it may be 0 or another form). Look a_shown up and check its form type yourself. There is no
// per-frame update.
//
// kMessageRedraw ('SLMD'): add-on -> SL_MagicMenu, data = the block's final id (NUL-terminated, as the result gave it),
// dataLen its size. The block is drawn again on the next UI frame - so do not send it from that draw (or any draw of your
// block), or the block is drawn again every frame. Ignored while the magic menu is closed (it is drawn at the next open
// anyway) and for an id that is not a registered block. Not answered.
//
// kMessageHeight ('SLMH'): add-on -> SL_MagicMenu, data = a HeightRequest (below), dataLen its size: the block's height for
// this menu, for content that needs less room than the block registered with. The registered height is the block's maximum
// and its size in the layout mode; every magic menu opens at it. On the next UI frame the columns are laid out again with the
// new height (the blocks below move up or down, the column's scroll follows) and the block is drawn again into its new size
// (and its click area follows), as for any size change - so do not send it again from that draw unless the height you need
// changed, or the block resizes every frame. Several requests for one block before that frame: the last one counts. The
// struct only needs to live for the Dispatch call (the id is copied). Ignored while the magic menu is closed and for an id
// that is not a registered block; refused (logged) when height is not finite, <= 0 or more than the registered height. The
// saved layout (SL_MagicMenu_Layout.json) is not changed. Not answered. 'SLMH' needs an SL_MagicMenu that knows it (the same
// kVersion 2 header with this message): an older one ignores the message, and the block keeps its registered height. v3
// leaves the HeightRequest as it was: SL_MagicMenu reads it at apiVersion 2 and 3, so a v2 add-on's requests keep working.
//
// click (optional): SL_MagicMenu lays one transparent hit area over the block's content clip. A mouse press there and its
// release are handed to click as two points in content clip coordinates (0,0 = the clip's top-left, the same space your
// draw used), once, on the frame after the release. No call (the press is cancelled) when the release is outside the
// content area or outside the column's visible part, when between press and release the column scrolled, the block was
// drawn again or moved, or the layout mode was entered or left; never while the layout mode is on (it owns the mouse).
// Keep the rects of your buttons from your last draw and act only when press and release fall in the same one. Mouse only
// (no gamepad or keyboard input in v2).
//
// tabs: the SkyUI magic menu category tabs the block shows on (kTab* bits; kTabsAll = 0 = every tab, the v1 behaviour).
// On the other tabs the block is hidden and the blocks below it in its column close up, as the tree, progress and node card
// blocks did on Powers / Shouts / Active Effects while SL_MagicMenu drew them. kTabSpells is every tab SL_MagicMenu drew its
// spell tree on: All, Favorites, the five school tabs (Alteration .. Restoration) and any category it does not know (a menu mod's
// own) - All and Favorites can still hold a power or a shout, which a_shown shows. kTabPowers, kTabShouts and
// kTabActiveEffects are those three tabs alone. A bit outside kTabsKnown is refused.
//
// --- v3: Registration's fields after tabs, all optional ---
//
// frame: called once per UI frame while the menu is open, for every shown v3 block that sets it, after the host's selection,
// tab handling and layout of that frame (the block's place is final for the frame) - in the layout mode too
// (Context::layoutMode true). It gets the block's present content clip (a_content), its overlay clip (a_overlay: a clip of
// the block's column above every panel in it, its origin on the content clip's origin - for a tooltip or a hover mark; null
// in the layout mode, whose covers must stay on top, and when the host has none this frame) and the Context below. Both
// clips are lent for the call only, like draw's clip: do not keep them, or anything made in them, past the return. The host
// clears the overlay when the block is hidden, moved, scrolled or drawn again, when the layout mode starts and when a panel is
// made above it, and gives a later frame a new, empty one with a new Context::overlayGeneration (every overlay clip has its
// own, the menu's first one too): a cached "my tooltip is up" is stale once the generation changed. contentGeneration is
// bumped each time the block is drawn into a new content clip. A block hidden by kMessageVisible gets no frame call.
//
// key: a keyboard key down (a_scanCode: its DirectInput scan code) while the menu is open, no text field takes keys and the
// layout mode is off, delivered on the next UI frame (before that frame's frame calls; none when the layout mode came on
// meanwhile) to every v3 block that sets key, on any tab. The return value is logged only: SL_MagicMenu never consumes a key
// (the menu and the game see it as before). No gamepad.
//
// pad: the content clip's inset in the block's box (stage units; top - below the title strip when there is one -, left,
// right and bottom). < 0 (the default, kPadDefault) is the host's own pad; a pad that is not a finite number or more than
// kMaxHeight is refused.
//
// flags (kFlag* bits; a bit outside kFlagsKnown is refused):
//   kFlagOwnMouse       the host lays no hit area over the block and calls no click (click is ignored with kFlagOwnMouse;
//                       the host logs it once at the registration): the add-on puts its own AS2 mouse
//                       handlers (GFxFunctionHandler: onRelease, onRollOver, onRollOut, hitArea) on clips it draws. Those
//                       handlers only record what happened; act on it in frame, and ignore events while the last
//                       Context::layoutMode seen was true (the layout mode owns the mouse; its covers lie above the panels).
//   kFlagKeepWhenEmpty  the block keeps its place and its box when the menu shows no spell.
//
// legacyId: one of SL_MagicMenu's retired built-in block ids ("tree", "progress", "nodecard") that this block takes over:
// a saved layout's place for that id becomes this block's place (the layout file keeps the retired id on disk, so an older
// SL_MagicMenu still reads it). Null or "" = none. Refused when the id is not a retired one or another block claimed it.
//
// Context (frame's a_context; it lives for the call only, tabSchool too): apiVersion / structSize first - read only the
// fields structSize covers (a later host may add fields at the end). shown: as drawSelection's a_shown; listForm: the list's
// committed entry's form (0 = none); selectionSeq: bumped by every change of the list selection (A -> C -> A between two
// frames still bumps it); source: what made the last selection (kList: the list, kAddon: a kMessageShowSpell the host
// served, its requestId in servedRequest); tabBits: the current tab's kTab* bit; tabSchool: "Alteration" .. "Restoration"
// on a school tab, else ""; layoutMode; overlayGeneration / contentGeneration (above); viewLeft .. viewBottom: the content
// clip's part inside its column's visible area, in content clip coordinates (all 0 when none of it shows); rowSize: the SkyUI
// list's row text size in stage units (0 until measured; every v3 block is drawn again once it is known); lineFirst /
// linePitch: the game font's first line height and its step per further line, per size unit (measured at the menu's
// open, else the host's fallback 1.25).
//
// kMessageShowSpell ('SLMS'): add-on -> SL_MagicMenu, data = a ShowSpellRequest, dataLen its size: show this spell as a
// click on it in the add-on's block would - selected in the list when it is there, else on the menu's card (and in the 3D
// preview when that setting is on; nameShown 0: the card hides the name). Queued, taken on the next UI frame: at most one
// request is served per frame - the newest sent (by any block); the others of that frame are dropped (one log line). It is
// dropped too when the list selection changed after it was queued (the player's newer choice wins) and while the layout
// mode is on. Not answered: the next Context shows the result (source kAddon, servedRequest = its requestId).
//
// kMessageVisible ('SLMV'): add-on -> SL_MagicMenu, data = a VisibleRequest, dataLen its size: hide the block (shown 0) but
// keep its place in the column, or show it again (shown 1). Queued, taken on the next UI frame. Every menu opens with the
// block shown; a hidden block is still drawn (draw / drawSelection) but gets no frame call and cannot be grabbed in the
// layout mode.
//
// Both v3 messages: the struct only needs to live for the Dispatch call (the id is copied). Ignored while the magic menu is
// closed and for an id that is not a registered block; refused (logged) when the message is shorter than the fields read,
// apiVersion is not 3, the id is empty or too long, or ('SLMS') formId is 0. Not answered.
// =============================================================================

#include <cstddef>
#include <cstdint>

namespace RE
{
    class GFxMovieView;
    class GFxValue;
}

namespace BlockApi
{
    inline constexpr const char* kPluginName = "SL_MagicMenu";

    /// The message types, as four characters ('SLMB' ...; built from the characters: no multi-character literal)
    inline constexpr std::uint32_t FourCC(char a_a, char a_b, char a_c, char a_d)
    {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(a_a)) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(a_b)) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(a_c)) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(a_d));
    }
    inline constexpr std::uint32_t kMessageRegister = FourCC('S', 'L', 'M', 'B');  // add-on -> SL_MagicMenu: data = Registration*
    inline constexpr std::uint32_t kMessageResult = FourCC('S', 'L', 'M', 'R');    // SL_MagicMenu -> add-on: data = Result*
    inline constexpr std::uint32_t kMessageRedraw = FourCC('S', 'L', 'M', 'D');    // add-on -> SL_MagicMenu: data = the final id (v2)
    inline constexpr std::uint32_t kMessageHeight = FourCC('S', 'L', 'M', 'H');    // add-on -> SL_MagicMenu: data = HeightRequest* (v2)
    inline constexpr std::uint32_t kMessageShowSpell = FourCC('S', 'L', 'M', 'S'); // add-on -> SL_MagicMenu: data = ShowSpellRequest* (v3)
    inline constexpr std::uint32_t kMessageVisible = FourCC('S', 'L', 'M', 'V');   // add-on -> SL_MagicMenu: data = VisibleRequest* (v3)

    inline constexpr std::uint32_t kVersionV1 = 1;
    inline constexpr std::uint32_t kVersionV2 = 2;
    inline constexpr std::uint32_t kVersion = 3;
    inline constexpr std::uint32_t kMaxIdLength = 128;
    inline constexpr std::uint32_t kMaxTitleLength = 128;  // a longer title is cut
    inline constexpr float kMaxHeight = 4000.0f;

    /// The tabs mask (Registration::tabs, v2): see "tabs" above
    inline constexpr std::uint32_t kTabsAll = 0;
    inline constexpr std::uint32_t kTabSpells = 1u << 0;
    inline constexpr std::uint32_t kTabPowers = 1u << 1;
    inline constexpr std::uint32_t kTabShouts = 1u << 2;
    inline constexpr std::uint32_t kTabActiveEffects = 1u << 3;
    inline constexpr std::uint32_t kTabsKnown = kTabSpells | kTabPowers | kTabShouts | kTabActiveEffects;

    /// Registration::flags (v3): see "flags" above
    inline constexpr std::uint32_t kFlagOwnMouse = 1u << 0;
    inline constexpr std::uint32_t kFlagKeepWhenEmpty = 1u << 1;
    inline constexpr std::uint32_t kFlagsKnown = kFlagOwnMouse | kFlagKeepWhenEmpty;

    /// Registration::pad (v3): any value < 0 is the host's own pad
    inline constexpr float kPadDefault = -1.0f;

    /// What made the menu's last selection (Context::source, v3)
    enum class SelectionSource : std::uint32_t
    {
        kNone = 0,   // nothing chosen yet this menu
        kList = 1,   // the player's list (mouse, keys, gamepad)
        kAddon = 2,  // a kMessageShowSpell the host served (Context::servedRequest)
    };

    /// frame's view of the menu (v3): see "Context" above. Lent for the call only
    struct Context
    {
        std::uint32_t apiVersion = kVersion;
        std::uint32_t structSize = sizeof(Context);
        std::uint32_t shown = 0;          // as drawSelection's a_shown (RE::FormID)
        std::uint32_t listForm = 0;       // the list's committed entry's form, 0 = none
        std::uint32_t selectionSeq = 0;   // bumped by every change of the list selection
        SelectionSource source = SelectionSource::kNone;
        std::uint32_t servedRequest = 0;  // the requestId of the last kMessageShowSpell served (source kAddon)
        std::uint32_t tabBits = kTabSpells;
        const char* tabSchool = "";       // "Alteration" .. "Restoration" on a school tab, else ""
        bool layoutMode = false;
        std::uint32_t overlayGeneration = 0;  // bumped each time the host clears the overlay
        std::uint32_t contentGeneration = 0;  // bumped each time the block is drawn into a new content clip
        float viewLeft = 0.0f, viewTop = 0.0f, viewRight = 0.0f, viewBottom = 0.0f;  // the visible part, content clip coordinates
        float rowSize = 0.0f, lineFirst = 0.0f, linePitch = 0.0f;  // the SkyUI list's text metrics, 0 until measured
    };

    /// a_clip: the lent content clip; a_width / a_height: its drawable size in stage units; a_user: Registration::user
    using DrawFn = void (*)(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, void* a_user);
    /// v2: as DrawFn, plus a_shown: the form the menu's card shows (0 = none); std::uint32_t is RE::FormID, so a callback
    /// declared with RE::FormID matches
    using DrawSelectionFn = void (*)(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown,
        void* a_user);
    /// v2: a press and its release in the block, both in content clip coordinates (see "click" above)
    using ClickFn = void (*)(float a_pressX, float a_pressY, float a_releaseX, float a_releaseY, void* a_user);
    /// v3: once per UI frame for a shown block (see "frame" above); a_content and a_overlay are lent for the call only
    using FrameFn = void (*)(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const Context& a_context, void* a_user);
    /// v3: a keyboard key down (see "key" above); the return value is logged only, the host never consumes the key
    using KeyFn = bool (*)(std::uint32_t a_scanCode, void* a_user);

    /// API v1's Registration, FROZEN: the layout every v1 add-on sends. apiVersion and structSize come first in every version
    struct RegistrationV1
    {
        std::uint32_t apiVersion = kVersionV1;
        std::uint32_t structSize = sizeof(RegistrationV1);
        const char* id = nullptr;
        const char* title = nullptr;
        float height = 0.0f;
        DrawFn draw = nullptr;
        void* user = nullptr;
    };
    inline constexpr std::uint32_t kRegistrationV1Size = sizeof(RegistrationV1);

    /// API v2's Registration, FROZEN: the layout every v2 add-on sends (v1's fields in v1's places, then the v2 fields).
    /// v2 needs draw or drawSelection
    struct RegistrationV2
    {
        std::uint32_t apiVersion = kVersionV2;
        std::uint32_t structSize = sizeof(RegistrationV2);
        const char* id = nullptr;     // see "The id" above
        const char* title = nullptr;  // the title strip's text (null / "": no strip)
        float height = 0.0f;
        DrawFn draw = nullptr;        // not called when drawSelection is set
        void* user = nullptr;         // handed back to every callback
        DrawSelectionFn drawSelection = nullptr;  // v2: when set, used instead of draw
        ClickFn click = nullptr;                  // v2: optional
        std::uint32_t tabs = kTabsAll;            // v2: kTab* bits, kTabsAll = every tab
    };
    inline constexpr std::uint32_t kRegistrationV2Size = sizeof(RegistrationV2);

    // The ABI freeze: v2 starts with v1's fields at v1's offsets
    static_assert(offsetof(RegistrationV2, apiVersion) == offsetof(RegistrationV1, apiVersion));
    static_assert(offsetof(RegistrationV2, structSize) == offsetof(RegistrationV1, structSize));
    static_assert(offsetof(RegistrationV2, id) == offsetof(RegistrationV1, id));
    static_assert(offsetof(RegistrationV2, title) == offsetof(RegistrationV1, title));
    static_assert(offsetof(RegistrationV2, height) == offsetof(RegistrationV1, height));
    static_assert(offsetof(RegistrationV2, draw) == offsetof(RegistrationV1, draw));
    static_assert(offsetof(RegistrationV2, user) == offsetof(RegistrationV1, user));
    static_assert(offsetof(RegistrationV2, drawSelection) >= kRegistrationV1Size);
    static_assert(sizeof(void*) != 8 || kRegistrationV1Size == 48, "the v1 layout of x64 builds is 48 bytes");
    static_assert(sizeof(void*) != 8 || kRegistrationV2Size == 72, "the v2 layout of x64 builds is 72 bytes");

    /// API v3: v2's fields in v2's places, then the v3 fields (all optional). v3 needs draw or drawSelection, as v2
    struct Registration
    {
        std::uint32_t apiVersion = kVersion;
        std::uint32_t structSize = sizeof(Registration);
        const char* id = nullptr;     // see "The id" above
        const char* title = nullptr;  // the title strip's text (null / "": no strip)
        float height = 0.0f;
        DrawFn draw = nullptr;        // not called when drawSelection is set
        void* user = nullptr;         // handed back to every callback
        DrawSelectionFn drawSelection = nullptr;  // v2: when set, used instead of draw
        ClickFn click = nullptr;                  // v2: optional (never called with kFlagOwnMouse)
        std::uint32_t tabs = kTabsAll;            // v2: kTab* bits, kTabsAll = every tab
        FrameFn frame = nullptr;                  // v3: once per UI frame while shown
        KeyFn key = nullptr;                      // v3: keyboard keys down
        float pad = kPadDefault;                  // v3: the content clip's inset, < 0 = the host's
        std::uint32_t flags = 0;                  // v3: kFlag* bits
        const char* legacyId = nullptr;           // v3: a retired built-in id this block takes over (null = none)
    };

    // The ABI freeze: v3 starts with v2's fields at v2's offsets
    static_assert(offsetof(Registration, apiVersion) == offsetof(RegistrationV2, apiVersion));
    static_assert(offsetof(Registration, structSize) == offsetof(RegistrationV2, structSize));
    static_assert(offsetof(Registration, id) == offsetof(RegistrationV2, id));
    static_assert(offsetof(Registration, title) == offsetof(RegistrationV2, title));
    static_assert(offsetof(Registration, height) == offsetof(RegistrationV2, height));
    static_assert(offsetof(Registration, draw) == offsetof(RegistrationV2, draw));
    static_assert(offsetof(Registration, user) == offsetof(RegistrationV2, user));
    static_assert(offsetof(Registration, drawSelection) == offsetof(RegistrationV2, drawSelection));
    static_assert(offsetof(Registration, click) == offsetof(RegistrationV2, click));
    static_assert(offsetof(Registration, tabs) == offsetof(RegistrationV2, tabs));
    static_assert(offsetof(Registration, frame) >= kRegistrationV2Size);
    inline constexpr std::uint32_t kRegistrationV3Size = sizeof(Registration);
    static_assert(sizeof(void*) != 8 || kRegistrationV3Size == 104, "the v3 layout of x64 builds is 104 bytes");
    static_assert(sizeof(void*) != 8 || sizeof(Context) == 80, "the v3 Context of x64 builds is 80 bytes");

    enum class Status : std::uint32_t
    {
        kAccepted = 0,
        kRefused = 1,
    };

    /// The answer to one Registration (the v1 layout in every version). The strings live only for the message call
    struct Result
    {
        std::uint32_t apiVersion = kVersion;  // the version the registration was read as (kVersion when refused before that)
        std::uint32_t structSize = sizeof(Result);
        Status status = Status::kRefused;
        const char* id = "";      // the block's final id (prefixed), or the id as sent when refused
        const char* reason = "";  // why it was refused ("" when accepted)
    };

    /// kMessageHeight's data (see "kMessageHeight" above). apiVersion and structSize first, as in Registration
    struct HeightRequest
    {
        std::uint32_t apiVersion = kVersion;
        std::uint32_t structSize = sizeof(HeightRequest);
        const char* id = nullptr;  // the block's final id, as the Result gave it
        float height = 0.0f;       // stage units, 0 < height <= the registered height
    };

    /// kMessageShowSpell's data (v3; see "kMessageShowSpell" above)
    struct ShowSpellRequest
    {
        std::uint32_t apiVersion = kVersion;
        std::uint32_t structSize = sizeof(ShowSpellRequest);
        const char* id = nullptr;      // the block's final id, as the Result gave it
        std::uint32_t formId = 0;      // the spell (RE::FormID), not 0
        std::uint32_t requestId = 0;   // the add-on's own number, back in Context::servedRequest once served
        std::uint32_t nameShown = 1;   // 0: the card hides the spell's name
    };
    static_assert(sizeof(void*) != 8 || sizeof(ShowSpellRequest) == 32, "ShowSpellRequest of x64 builds is 32 bytes");

    /// kMessageVisible's data (v3; see "kMessageVisible" above)
    struct VisibleRequest
    {
        std::uint32_t apiVersion = kVersion;
        std::uint32_t structSize = sizeof(VisibleRequest);
        const char* id = nullptr;      // the block's final id, as the Result gave it
        std::uint32_t shown = 1;       // 0: hidden, its place kept; 1: shown
    };
    static_assert(sizeof(void*) != 8 || sizeof(VisibleRequest) == 24, "VisibleRequest of x64 builds is 24 bytes");
}
