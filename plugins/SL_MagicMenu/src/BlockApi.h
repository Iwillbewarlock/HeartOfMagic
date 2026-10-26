#pragma once

// =============================================================================
// BLOCK API - how another SKSE plugin adds its own block to SL_MagicMenu's
// magic menu panels (blocks plan Task 6, design review D9). Copy this header
// into your plugin; it needs nothing but <cstdint> (the two RE classes are only
// declared here, CommonLibSSE-NG defines them).
//
// THIS IS A CommonLibSSE-NG C++ ABI INTERFACE, NOT A C ABI. The draw callback
// gets RE::GFxMovieView* and RE::GFxValue& as CommonLibSSE-NG lays them out:
// build your plugin with CommonLibSSE-NG (any build whose GFxValue is the
// game's 0x18-byte value) and the MSVC x64 calling convention.
//
// Registering:
//   1. At SKSE load, listen for SL_MagicMenu's answer:
//        SKSE::GetMessagingInterface()->RegisterListener(BlockApi::kPluginName, OnBlockResult);
//   2. At kPostPostLoad, kInputLoaded or kDataLoaded (SL_MagicMenu starts listening at kPostLoad; registration closes at
//      the first magic menu open after kDataLoaded - later ones are refused with a reason), send one message per block:
//        BlockApi::Registration reg{ .id = "MyMod.stats", .title = "STATS", .height = 120.0f, .draw = DrawStats };
//        SKSE::GetMessagingInterface()->Dispatch(BlockApi::kMessageRegister, &reg, sizeof(reg), BlockApi::kPluginName);
//      Dispatch returns false when no SL_MagicMenu listened (not installed, or bEnabled=0). The struct only needs to live
//      for the Dispatch call: SL_MagicMenu copies id and title.
//   3. The answer comes back, during that Dispatch, as a kMessageResult message to the sender (BlockApi::Result: accepted
//      or refused, the block's final id and the reason). Its strings live only for that call.
//
// The id: unique, 1..kMaxIdLength characters. An id with no '.' is made "<your plugin name>.<id>" (the result says the
// final id). It is the block's key in SL_MagicMenu_Layout.json: keep it the same across versions or the player's place
// for your block is lost. A block without a saved place goes to the end of the right column; when your plugin is not
// installed any more, its saved place is kept (the block is skipped while absent).
//
// The block: fixed height (stage units, 1280 x 720 stage; finite, > 0, <= kMaxHeight); the width is the column's, which
// the player can change. SL_MagicMenu draws the block's box and its title strip (no strip when title is null or empty)
// and lends you a fresh, empty clip below the title strip, inside the padding: draw there with the AS2 drawing API
// (beginFill / lineTo / createTextField ...). a_width / a_height are that clip's drawable size.
//
// When draw is called: on the UI thread, from the magic menu movie's frame handler - once after the menu opens, and
// again when the block's size changes or it is moved to the other column (each time into a NEW empty clip). The clip is
// lent for the call only: do not keep the GFxValue, the movie or anything made in it past the return (the movie dies
// when the menu closes). Do not throw out of draw (an exception is caught and logged, and the block is not drawn again
// in that menu). There is no per-frame update in API v1.
// =============================================================================

#include <cstdint>

namespace RE
{
    class GFxMovieView;
    class GFxValue;
}

namespace BlockApi
{
    inline constexpr const char* kPluginName = "SL_MagicMenu";

    /// The message types, as the four characters 'SLMB' / 'SLMR' (built from the characters: no multi-character literal)
    inline constexpr std::uint32_t FourCC(char a_a, char a_b, char a_c, char a_d)
    {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(a_a)) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(a_b)) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(a_c)) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(a_d));
    }
    inline constexpr std::uint32_t kMessageRegister = FourCC('S', 'L', 'M', 'B');  // add-on -> SL_MagicMenu: data = Registration*
    inline constexpr std::uint32_t kMessageResult = FourCC('S', 'L', 'M', 'R');    // SL_MagicMenu -> add-on: data = Result*

    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::uint32_t kMaxIdLength = 128;
    inline constexpr std::uint32_t kMaxTitleLength = 128;  // a longer title is cut
    inline constexpr float kMaxHeight = 4000.0f;

    /// a_clip: the lent content clip; a_width / a_height: its drawable size in stage units; a_user: Registration::user
    using DrawFn = void (*)(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, void* a_user);

    /// API v1. apiVersion and structSize come first and stay first in every later version
    struct Registration
    {
        std::uint32_t apiVersion = kVersion;
        std::uint32_t structSize = sizeof(Registration);
        const char* id = nullptr;     // see "The id" above
        const char* title = nullptr;  // the title strip's text (null / "": no strip)
        float height = 0.0f;
        DrawFn draw = nullptr;
        void* user = nullptr;         // handed back to draw
    };
    inline constexpr std::uint32_t kRegistrationV1Size = sizeof(Registration);

    enum class Status : std::uint32_t
    {
        kAccepted = 0,
        kRefused = 1,
    };

    /// The answer to one Registration. The strings live only for the message call
    struct Result
    {
        std::uint32_t apiVersion = kVersion;
        std::uint32_t structSize = sizeof(Result);
        Status status = Status::kRefused;
        const char* id = "";      // the block's final id (prefixed), or the id as sent when refused
        const char* reason = "";  // why it was refused ("" when accepted)
    };
}
