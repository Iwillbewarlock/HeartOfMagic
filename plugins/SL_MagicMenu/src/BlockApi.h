#pragma once

// =============================================================================
// BLOCK API v2 - how another SKSE plugin adds its own block to SL_MagicMenu's
// magic menu panels (blocks plan Task 6, design review D9; v2: Spell Research
// add-on design 2026-10-27, "Changes / 1"). Copy this header into your plugin;
// it needs nothing but <cstddef> and <cstdint> (the two RE classes are only
// declared here, CommonLibSSE-NG defines them).
//
// THIS IS A CommonLibSSE-NG C++ ABI INTERFACE, NOT A C ABI. The callbacks get
// RE::GFxMovieView* and RE::GFxValue& as CommonLibSSE-NG lays them out: build
// your plugin with CommonLibSSE-NG (any build whose GFxValue is the game's
// 0x18-byte value) and the MSVC x64 calling convention.
//
// Versions: an SL_MagicMenu with this header reads apiVersion 1 (RegistrationV1, frozen: the v1 header's Registration) and
// 2 (Registration). A v1 add-on built against the v1 header keeps working unchanged. Version 3 or later is refused with a
// reason ("update SL_MagicMenu").
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
//      for the Dispatch call: SL_MagicMenu copies id and title. Only the fields structSize and the message size both cover
//      are read (an older v2 struct cut short reads its missing fields as null / 0).
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
// Threads and lifetimes: draw, drawSelection and click are called on the UI thread, from the magic menu movie's frame
// handler, never from inside another of your callbacks. The clip is lent for the draw call only: do not keep the GFxValue,
// the movie or anything made in it past the return (the movie dies when the menu closes), and do not put AS2 handlers
// (onPress ...) on it - use click. Do not throw out of a callback (an exception is caught and logged, and the block is not
// drawn or clicked again in that menu). Messages to SL_MagicMenu (registration, kMessageRedraw) may come from any thread.
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
// dataLen its size. The block is drawn again on the next UI frame. Ignored while the magic menu is closed (it is drawn at
// the next open anyway) and for an id that is not a registered block. Not answered.
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
// On the other tabs the block is hidden and the blocks below it in its column close up, as the built-in tree, progress
// and node card blocks do on Powers / Shouts / Active Effects. kTabSpells is every tab SL_MagicMenu draws its spell tree
// on: All, Favorites, the five school tabs (Alteration .. Restoration) and any category it does not know (a menu mod's
// own) - All and Favorites can still hold a power or a shout, which a_shown shows. kTabPowers, kTabShouts and
// kTabActiveEffects are those three tabs alone. A bit outside kTabsKnown is refused.
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

    inline constexpr std::uint32_t kVersionV1 = 1;
    inline constexpr std::uint32_t kVersion = 2;
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

    /// a_clip: the lent content clip; a_width / a_height: its drawable size in stage units; a_user: Registration::user
    using DrawFn = void (*)(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, void* a_user);
    /// v2: as DrawFn, plus a_shown: the form the menu's card shows (0 = none); std::uint32_t is RE::FormID, so a callback
    /// declared with RE::FormID matches
    using DrawSelectionFn = void (*)(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown,
        void* a_user);
    /// v2: a press and its release in the block, both in content clip coordinates (see "click" above)
    using ClickFn = void (*)(float a_pressX, float a_pressY, float a_releaseX, float a_releaseY, void* a_user);

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

    /// API v2: v1's fields in v1's places, then the v2 fields. v2 needs draw or drawSelection
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
        ClickFn click = nullptr;                  // v2: optional
        std::uint32_t tabs = kTabsAll;            // v2: kTab* bits, kTabsAll = every tab
    };

    // The ABI freeze: v2 starts with v1's fields at v1's offsets
    static_assert(offsetof(Registration, apiVersion) == offsetof(RegistrationV1, apiVersion));
    static_assert(offsetof(Registration, structSize) == offsetof(RegistrationV1, structSize));
    static_assert(offsetof(Registration, id) == offsetof(RegistrationV1, id));
    static_assert(offsetof(Registration, title) == offsetof(RegistrationV1, title));
    static_assert(offsetof(Registration, height) == offsetof(RegistrationV1, height));
    static_assert(offsetof(Registration, draw) == offsetof(RegistrationV1, draw));
    static_assert(offsetof(Registration, user) == offsetof(RegistrationV1, user));
    static_assert(offsetof(Registration, drawSelection) >= kRegistrationV1Size);
    static_assert(sizeof(void*) != 8 || kRegistrationV1Size == 48, "the v1 layout of x64 builds is 48 bytes");

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
}
