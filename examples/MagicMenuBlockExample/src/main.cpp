// =============================================================================
// MagicMenuBlockExample - a small add-on block for Magic Menu Rewrite (Block API v3)
// =============================================================================
// One block on the spell tabs that shows the common features of the Block API:
//   1. registration: listen for the host's Result at kPostLoad, register at kDataLoaded, keep the final id
//   2. drawSelection: the shown form's name and, for a spell, its magicka cost (AS2 text fields and a fill)
//   3. click: one button "Clicks: N"; a click counts when press and release both fall on it, then kMessageRedraw
//   4. key: H (DirectInput scan code 0x23) toggles a compact height (kMessageHeight), shown as a key hint (kMessageKeyHint)
// BlockApi.h is an unchanged copy of the host's header; its top comment is the contract. The rules this file follows:
//   - draw / click / key are called on the UI thread; they never throw (every body is wrapped in Guarded)
//   - the clip and the movie are lent for one call: nothing made from them is kept
//   - messages to the host may come from any thread, so state shared with other threads is atomic or under a mutex
// Guide: docs/magicmenu/BLOCK_API_GUIDE.md
// =============================================================================

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "BlockApi.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <format>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace logger = SKSE::log;

namespace
{
    // --- This block's settings ---------------------------------------------------------------------------------
    constexpr const char* kBlockId = "MagicMenuBlockExample.info";  // has a '.': the host keeps it as it is (the layout key)
    constexpr const char* kTitle = "EXAMPLE";                       // the title strip's text (copied by the host)
    constexpr float kHeight = 120.0f;                               // registered: the maximum, every menu opens at it
    constexpr float kCompactHeight = 64.0f;                         // the compact height, 0 < it <= kHeight
    // DirectInput scan code of H. Not J (0x24): the game's controlmap binds J to Journal. H is in no line of the
    // controlmap the host's key choice was checked against (the installed CoMAP copy; the vanilla file not checked
    // separately). The host and the HoM block use or avoid K 37, Backspace 14 (layout mode only), U 22, G 34, R 19 (Spell
    // Organizer), N 49 (Creature Framework), O 24 (Open Animation Replacer, OBody) and Home 199 (dMenu), per the shipped
    // MagicMenuRewrite.ini. Some mods bind H outside menus (Horse Whistle); the host never consumes a key, so a
    // mod that listens to H in menus would see it too.
    constexpr std::uint32_t kToggleKey = 0x23;
    constexpr const char* kToggleLabel = "Compact view";            // the key hint's label

    // --- Drawing constants (stage units; colours 0xRRGGBB) ------------------------------------------------------
    constexpr const char* kFont = "$EverywhereMediumFont";  // the game's font name; font mods (e.g. Korean) remap it
    constexpr double kAlphaFull = 100.0;                    // AS2 alphas are percents
    constexpr std::uint32_t kTextColor = 0xFFFFFF, kDimColor = 0xA0A0A0, kButtonFill = 0x303030, kButtonEdge = 0x909090;
    constexpr double kNameSize = 16.0, kDetailSize = 14.0, kLineHeight = 22.0;
    constexpr double kButtonW = 96.0, kButtonH = 22.0, kGap = 6.0;

    // --- State -------------------------------------------------------------------------------------------------
    // The final id is written by OnHostMessage (during our Dispatch at kDataLoaded, not the UI thread) and read when we
    // send messages (from the UI thread): a mutex. g_compact is changed by Key (UI thread) and reset by the menu-open
    // event (a thread BlockApi.h does not cover): atomic.
    std::mutex g_idLock;
    std::string g_finalId;
    std::atomic<bool> g_listening{ false };
    std::atomic<bool> g_accepted{ false };
    std::atomic<bool> g_compact{ false };

    struct Rect
    {
        double x = 0, y = 0, w = 0, h = 0;
        bool Contains(double a_x, double a_y) const { return w > 0 && h > 0 && a_x >= x && a_x <= x + w && a_y >= y && a_y <= y + h; }
    };
    // Only drawSelection and Click touch these, both on the UI thread: no guard
    Rect g_button;    // the button as the last draw placed it (content clip coordinates)
    int g_clicks = 0;

    // Runs a callback body; an exception is logged here and never leaves the callback (BlockApi.h: do not throw).
    // spdlog does not throw from a log call (it reports its own errors), so logging inside catch is safe.
    template <class Body>
    void Guarded(const char* a_what, Body&& a_body) noexcept
    {
        try {
            a_body();
        } catch (const std::exception& e) {
            logger::error("MagicMenuBlockExample: {} failed: {}", a_what, e.what());
        } catch (...) {
            logger::error("MagicMenuBlockExample: {} failed: unknown exception", a_what);
        }
    }

    std::string FinalId()
    {
        const std::scoped_lock lock(g_idLock);
        return g_finalId;
    }

    // --- Messages to the host (any thread) ----------------------------------------------------------------------

    // Draw the block again on the next UI frame. Never call this from a draw: the block would be drawn every frame.
    void SendRedraw()
    {
        if (!g_accepted) return;
        std::string id = FinalId();
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageRedraw, id.data(), static_cast<std::uint32_t>(id.size() + 1), BlockApi::kPluginName);
        }
    }

    // The block's height for this menu: 0 < a_height <= the registered height. The host lays out and redraws next frame.
    void SendHeight(float a_height)
    {
        if (!g_accepted) return;
        const std::string id = FinalId();
        BlockApi::HeightRequest request{ .id = id.c_str(), .height = a_height };  // only needs to live for the Dispatch
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageHeight, &request, sizeof(request), BlockApi::kPluginName);
        }
    }

    // --- AS2 drawing helpers (a minimal copy of the pattern the repo's add-ons use) ------------------------------

    void Call(RE::GFxValue& a_clip, const char* a_method, std::initializer_list<RE::GFxValue> a_args)
    {
        std::vector<RE::GFxValue> args(a_args);
        a_clip.Invoke(a_method, nullptr, args.data(), args.size());
    }

    // A filled rectangle with a 1-unit border (the drawing API: lineStyle, beginFill, moveTo / lineTo, endFill)
    void FillRect(RE::GFxValue& a_clip, const Rect& a_rect, std::uint32_t a_fill, std::uint32_t a_edge)
    {
        Call(a_clip, "lineStyle", { 1.0, static_cast<double>(a_edge), kAlphaFull });
        Call(a_clip, "beginFill", { static_cast<double>(a_fill), kAlphaFull });
        Call(a_clip, "moveTo", { a_rect.x, a_rect.y });
        Call(a_clip, "lineTo", { a_rect.x + a_rect.w, a_rect.y });
        Call(a_clip, "lineTo", { a_rect.x + a_rect.w, a_rect.y + a_rect.h });
        Call(a_clip, "lineTo", { a_rect.x, a_rect.y + a_rect.h });
        Call(a_clip, "lineTo", { a_rect.x, a_rect.y });
        Call(a_clip, "endFill", {});
    }

    // A one-line, not selectable text field in the game's font; a_align is "left", "center" or "right"
    void Text(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const char* a_name, const Rect& a_rect, const std::string& a_text,
        std::uint32_t a_color, double a_size, const char* a_align = "left")
    {
        // No number (not a clip we can add to): make nothing. A fallback depth of 0 would replace the child already at
        // depth 0 (the host's Draw.h NextDepth does the same)
        RE::GFxValue depth;
        a_clip.Invoke("getNextHighestDepth", &depth);
        if (!depth.IsNumber()) return;
        Call(a_clip, "createTextField", { a_name, depth.GetNumber(), a_rect.x, a_rect.y, a_rect.w, a_rect.h });
        RE::GFxValue field;
        if (!a_clip.GetMember(a_name, &field) || !field.IsDisplayObject()) return;
        RE::GFxValue format;
        field.Invoke("getNewTextFormat", &format);
        if (format.IsObject()) {
            format.SetMember("font", kFont);
            format.SetMember("color", static_cast<double>(a_color));
            format.SetMember("size", a_size);
            format.SetMember("align", a_align);
            field.Invoke("setNewTextFormat", nullptr, &format, 1);
        }
        field.SetMember("embedFonts", true);
        field.SetMember("selectable", false);
        RE::GFxValue text;
        a_movie->CreateString(&text, a_text.c_str());  // a copy the movie owns: a GFxValue made from a char* only points at it
        field.SetMember("text", text);
    }

    // --- The block's callbacks (UI thread) ----------------------------------------------------------------------

    struct Shown
    {
        std::string name;
        std::string detail;  // "" = no second line
    };

    // What to write for the form the menu's card shows. BlockApi.h: look it up and check its type yourself (a shout or an
    // Active Effects entry may give another form or 0 - not verified in game).
    Shown Describe(std::uint32_t a_shown)
    {
        if (a_shown == 0) return { "Select a spell", "" };
        const auto* form = RE::TESForm::LookupByID(a_shown);
        if (!form) return { std::format("Unknown form {:08X}", a_shown), "" };
        const char* name = form->GetName();
        std::string title = name && *name ? std::string(name) : std::format("Form {:08X}", a_shown);
        const auto* spell = form->As<RE::SpellItem>();
        if (!spell) return { std::move(title), "Not a spell" };
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return { std::move(title), "" };
        return { std::move(title), std::format("Magicka cost: {:.0f}", spell->CalculateMagickaCost(player)) };
    }

    // Called once after the menu opens, after a size change or a move to the other column, after our kMessageRedraw, once
    // rowSize is first measured, and whenever a_shown changes - each time into a NEW empty clip. a_width / a_height: the clip's drawable size.
    void DrawSelection(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void*) noexcept
    {
        Guarded("drawSelection", [&] {
            if (!a_movie) return;
            const double width = a_width, height = a_height;
            const double lineH = (std::min)(kLineHeight, height);
            const Shown shown = Describe(a_shown);

            // the button, top right; its rect is kept for Click (the host gives clicks in this same coordinate space)
            g_button = { (std::max)(0.0, width - kButtonW), 0.0, (std::min)(kButtonW, width), (std::min)(kButtonH, height) };
            FillRect(a_clip, g_button, kButtonFill, kButtonEdge);
            Text(a_movie, a_clip, "clicks", g_button, std::format("Clicks: {}", g_clicks), kTextColor, kDetailSize, "center");

            // line 1: the name, left of the button. Line 2 only when the height leaves room (not in the compact height).
            Text(a_movie, a_clip, "name", { 0.0, 0.0, (std::max)(0.0, g_button.x - kGap), lineH }, shown.name, kTextColor, kNameSize);
            if (!shown.detail.empty() && height >= 2 * kLineHeight) {
                Text(a_movie, a_clip, "detail", { 0.0, kLineHeight, width, kLineHeight }, shown.detail, kDimColor, kDetailSize);
            }
        });
    }

    // A press and its release in the content clip (the frame after the release). Count it only when both are on the button.
    void Click(float a_pressX, float a_pressY, float a_releaseX, float a_releaseY, void*) noexcept
    {
        Guarded("click", [&] {
            if (!g_button.Contains(a_pressX, a_pressY) || !g_button.Contains(a_releaseX, a_releaseY)) return;
            ++g_clicks;
            logger::info("MagicMenuBlockExample: button clicked ({} so far)", g_clicks);
            SendRedraw();  // allowed here: a click is not a draw
        });
    }

    // A keyboard key down while the block is shown. The host never consumes the key; the return value is only logged.
    bool Key(std::uint32_t a_scanCode, void*) noexcept
    {
        bool used = false;
        Guarded("key", [&] {
            if (a_scanCode != kToggleKey) return;
            const bool compact = !g_compact.load();
            g_compact = compact;
            SendHeight(compact ? kCompactHeight : kHeight);  // the host redraws us at the new size next frame
            used = true;
        });
        return used;
    }

    // The block never asks for a height in its first draw, so the host opens every magic menu with it at the registered height
    // (BlockApi.h "kMessageHeight") and the compact toggle starts over at each open
    class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static MenuSink* Get()
        {
            static MenuSink sink;
            return &sink;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (a_event && a_event->opening && a_event->menuName == RE::MagicMenu::MENU_NAME) g_compact = false;
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // --- Registration -------------------------------------------------------------------------------------------

    // The host's answer (BlockApi::Result), sent during our kMessageRegister Dispatch. Its strings live only for this call.
    void OnHostMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        Guarded("the Result handler", [&] {
            if (!a_msg || a_msg->type != BlockApi::kMessageResult) return;
            if (!a_msg->data || a_msg->dataLen < sizeof(BlockApi::Result)) {
                logger::warn("MagicMenuBlockExample: a Result of {} bytes - ignored", a_msg->dataLen);
                return;
            }
            const auto* result = static_cast<const BlockApi::Result*>(a_msg->data);
            const std::string id = result->id ? result->id : "";
            if (result->status != BlockApi::Status::kAccepted) {
                logger::error("MagicMenuBlockExample: block '{}' refused (API v{}): {}", id, result->apiVersion, result->reason ? result->reason : "");
                return;
            }
            {
                const std::scoped_lock lock(g_idLock);
                g_finalId = id;
            }
            g_accepted = true;
            logger::info("MagicMenuBlockExample: block '{}' accepted (API v{})", id, result->apiVersion);
        });
    }

    void Register()
    {
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) return;
        if (!g_listening) {
            logger::error("MagicMenuBlockExample: no listener for MagicMenuRewrite (not installed?) - not registering");
            return;
        }
        // The struct only needs to live for the Dispatch: the host copies id and title. Designated fields in declaration order.
        BlockApi::Registration reg{ .id = kBlockId,
            .title = kTitle,
            .height = kHeight,
            .drawSelection = DrawSelection,
            .click = Click,
            .tabs = BlockApi::kTabSpells,
            .key = Key,
            // A drawSelection block closes up while the menu shows no spell, unless it keeps its box with this flag. This block
            // has something to show without a spell ("Select a spell" and its button), so it keeps it. Leave the flag out
            // for a block that only means something for a selected spell
            .flags = BlockApi::kFlagKeepWhenEmpty };
        if (!messaging->Dispatch(BlockApi::kMessageRegister, &reg, sizeof(reg), BlockApi::kPluginName)) {
            logger::error("MagicMenuBlockExample: MagicMenuRewrite did not listen (not installed, or bEnabled=0) - no block");
            return;
        }
        if (!g_accepted) {
            logger::error("MagicMenuBlockExample: no accepted Result (the refusal reason, if any, is logged above) - no block");
            return;
        }

        // The key hint: a standing declaration for the game session, sent once the Result gave the final id
        const std::string id = FinalId();
        BlockApi::KeyHint hint{ .id = id.c_str(), .scanCode = kToggleKey, .label = kToggleLabel };
        if (!messaging->Dispatch(BlockApi::kMessageKeyHint, &hint, sizeof(hint), BlockApi::kPluginName)) {
            logger::warn("MagicMenuBlockExample: the key hint was not delivered");
        }

        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            // every plugin is loaded now: listen for the host's Result (false when MagicMenuRewrite is not loaded)
            g_listening = SKSE::GetMessagingInterface()->RegisterListener(BlockApi::kPluginName, OnHostMessage);
            logger::info("MagicMenuBlockExample: listening to MagicMenuRewrite: {}", g_listening.load() ? "yes" : "no (not loaded?)");
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            // the last moment to register: registration closes at the first magic menu open
            Register();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    // .log = false and our own log::init() in a try: SKSE::Init is noexcept, and CommonLibSSE-NG's log setup throws when the
    // Documents path holds characters the system's ANSI code page cannot (that would end the game at load, not just the log)
    SKSE::Init(a_skse, SKSE::InitInfo{ .log = false });
    try {
        logger::init();  // Documents/My Games/<game>/SKSE/MagicMenuBlockExample.log
    } catch (...) {
        // no log file this session; the plugin still loads
    }
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::error("MagicMenuBlockExample: no SKSE messaging - nothing registered");
    }
    return true;
}
