// =============================================================================
// SL_MagicMenu_HoM - the Heart of Magic blocks of SL_MagicMenu's magic menu
// =============================================================================
// The spell tree window, the learning progress card and the node card, which the
// magic menu drew itself until HoM add-on plan Task 5, as three add-on blocks
// registered through SL_MagicMenu's Block API v3 (BlockApi.h, the host's own
// header from plugins/SL_MagicMenu/src). They read Heart of Magic through its
// public API v2 (SpellLearningAPI.h) and the saved spell_tree.json, and their
// keys from SL_MagicMenu.ini (HomSettings). Without Heart of Magic's API by
// kDataLoaded no block is registered (user 2026-10-30, design Q3), one log line.
// Design: docs/superpowers/specs/2026-10-30-magic-menu-hom-addon-design.md
// =============================================================================

#include "Common.h"

#include "AddonState.h"
#include "BlockApi.h"
#include "Cards.h"
#include "GameThread.h"
#include "Hom.h"
#include "HomSettings.h"
#include "SpellLearningAPI.h"
#include "Spells.h"
#include "Strings.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>

namespace
{
    constexpr const char* kHomPluginName = "SpellLearning";

    // The blocks' final ids as SL_MagicMenu accepted them (the payload of 'SLMD' / 'SLMH' / 'SLMV'); set during the
    // registrations at kDataLoaded, read by the UI thread from the first menu open on (registration closes there)
    std::mutex g_idLock;
    std::array<std::string, HomAddon::kBlockCount> g_finalIds;
    std::array<std::atomic<bool>, HomAddon::kBlockCount> g_accepted{};
    std::array<double, HomAddon::kBlockCount> g_heights{};
    std::string g_treeTitle;

    std::string FinalId(HomAddon::BlockKind a_kind)
    {
        const std::scoped_lock lock(g_idLock);
        return g_finalIds[HomAddon::Index(a_kind)];
    }

    // SL_MagicMenu's answer to a registration (BlockApi::Result), during our Dispatch; matched to a block by its id
    void OnBlockMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg || a_msg->type != BlockApi::kMessageResult) return;
        if (!a_msg->data || a_msg->dataLen < sizeof(BlockApi::Result)) {
            logger::warn("SL_MagicMenu_HoM: a registration answer of {} bytes - ignored", a_msg->dataLen);
            return;
        }
        const auto* result = static_cast<const BlockApi::Result*>(a_msg->data);
        const std::string id = result->id ? result->id : "";
        const auto it = std::ranges::find(HomAddon::kBlockIds, std::string_view(id));
        if (it == HomAddon::kBlockIds.end()) {
            logger::warn("SL_MagicMenu_HoM: an answer for '{}', not one of our blocks - ignored", id);
            return;
        }
        const auto index = static_cast<std::size_t>(it - HomAddon::kBlockIds.begin());
        if (result->status != BlockApi::Status::kAccepted) {
            logger::error("SL_MagicMenu_HoM: SL_MagicMenu refused the block '{}' (API v{}): {}", id, result->apiVersion, result->reason ? result->reason : "");
            return;
        }
        {
            const std::scoped_lock lock(g_idLock);
            g_finalIds[index] = id;
        }
        g_accepted[index] = true;
        logger::info("SL_MagicMenu_HoM: block '{}' accepted (API v{})", id, result->apiVersion);
    }

    // HoM broadcasts its API at kPostPostLoad
    void OnSpellLearningMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        if (a_msg->type != SpellLearning::kMessageType_APIReady || !a_msg->data) {
            logger::info("SL_MagicMenu_HoM: SpellLearning message type 0x{:X} ignored", a_msg->type);
            return;
        }
        auto* api = static_cast<SpellLearning::ISpellLearningAPI*>(a_msg->data);
        Hom::SetApi(api);
        logger::info("SL_MagicMenu_HoM: Heart of Magic API v{} received", api->GetAPIVersion());
    }

    class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static MenuSink* Get()
        {
            static MenuSink sink;
            return &sink;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!a_event || a_event->menuName != RE::MagicMenu::MENU_NAME) return RE::BSEventNotifyControl::kContinue;
            // the per-menu state is reset by the next callback (EnsureMenu); the style is read for this open
            if (a_event->opening) HomAddon::OnMenuOpen();
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        MenuSink() = default;
    };

    // One registration (the struct lives for the Dispatch only; SL_MagicMenu copies id, title and legacyId)
    bool RegisterOne(const SKSE::MessagingInterface* a_messaging, HomAddon::BlockKind a_kind)
    {
        using BlockApi::kFlagKeepWhenEmpty;
        using BlockApi::kFlagOwnMouse;
        using HomAddon::BlockKind;
        const auto index = HomAddon::Index(a_kind);
        BlockApi::Registration reg{ .id = HomAddon::kBlockIds[index], .height = static_cast<float>(g_heights[index]), .tabs = BlockApi::kTabSpells,
            .legacyId = HomAddon::kLegacyIds[index] };
        switch (a_kind) {
        case BlockKind::kTree:
            // Task 7 draws the tree window (own mouse: its node clips' handlers); its frame and key drive all three blocks
            reg.title = g_treeTitle.c_str();
            reg.drawSelection = HomAddon::DrawTreeBlock;
            reg.frame = HomAddon::TreeFrame;
            reg.key = HomAddon::TreeKey;
            reg.flags = kFlagOwnMouse | kFlagKeepWhenEmpty;
            break;
        case BlockKind::kProgress:
            reg.drawSelection = HomAddon::DrawProgressBlock;
            reg.pad = static_cast<float>(HomAddon::kCardPad);
            reg.flags = kFlagKeepWhenEmpty;
            break;
        case BlockKind::kNodeCard:
            reg.drawSelection = HomAddon::DrawNodeCardBlock;
            reg.frame = HomAddon::NodeCardFrame;
            reg.pad = static_cast<float>(HomAddon::kCardPad);
            reg.flags = kFlagOwnMouse | kFlagKeepWhenEmpty;  // the set-target button's own handler
            break;
        }
        if (!a_messaging->Dispatch(BlockApi::kMessageRegister, &reg, sizeof(reg), BlockApi::kPluginName)) {
            logger::error("SL_MagicMenu_HoM: SL_MagicMenu did not listen (not installed, or bEnabled=0) - no block {}", reg.id);
            return false;
        }
        if (!g_accepted[index]) logger::error("SL_MagicMenu_HoM: SL_MagicMenu sent no accepted answer for {} - that block is off", reg.id);
        return g_accepted[index].load();
    }

    // True when the tree block (the driver) was accepted
    bool Register()
    {
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) return false;
        const auto heights = HomSettings::BlockHeights(HomSettings::Get());
        g_heights = { heights.tree, heights.progress, heights.nodeCard };
        g_treeTitle = HomStrings::Text(HomStrings::Str::kTreeTitle);
        // the tree block's frame and key drive all three: without it the cards would never update, so none is registered
        if (!RegisterOne(messaging, HomAddon::BlockKind::kTree)) {
            logger::error("SL_MagicMenu_HoM: the tree block was not accepted - the progress and node cards are not registered either");
            return false;
        }
        RegisterOne(messaging, HomAddon::BlockKind::kProgress);
        RegisterOne(messaging, HomAddon::BlockKind::kNodeCard);
        return true;
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostLoad: {
            auto* messaging = SKSE::GetMessagingInterface();
            const bool host = messaging->RegisterListener(BlockApi::kPluginName, OnBlockMessage);
            const bool hom = messaging->RegisterListener(kHomPluginName, OnSpellLearningMessage);
            logger::info("SL_MagicMenu_HoM: listening to SL_MagicMenu {}, to {} {}", host ? "yes" : "NO (not loaded?)", kHomPluginName,
                hom ? "yes" : "no (not loaded?)");
            break;
        }
        case SKSE::MessagingInterface::kDataLoaded: {
            // user 2026-10-30 (design Q3): without Heart of Magic's API there is no block at all
            if (!Hom::Ready()) {
                logger::warn("SL_MagicMenu_HoM: Heart of Magic's API did not arrive by kDataLoaded (SpellLearning.dll not loaded, or too old) - "
                             "no block registered");
                FlushLog();
                return;
            }
            logger::info("SL_MagicMenu_HoM: kDataLoaded on thread {}, main thread {}", REX::W32::GetCurrentThreadId(), GameThread::Id());
            Spells::Load();
            if (Register()) {
                if (auto* ui = RE::UI::GetSingleton()) {
                    ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());
                } else {
                    logger::error("SL_MagicMenu_HoM: no UI singleton - HoM's style is not read at menu opens (its defaults are used)");
                }
            }
            FlushLog();
            break;
        }
        default: break;
        }
    }
}

namespace HomAddon
{
    double RegisteredHeight(BlockKind a_kind)
    {
        return g_heights[Index(a_kind)];
    }

    void RequestRedraw(BlockKind a_kind)
    {
        if (!g_accepted[Index(a_kind)]) return;
        const auto id = FinalId(a_kind);
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageRedraw, const_cast<char*>(id.c_str()), static_cast<std::uint32_t>(id.size() + 1), BlockApi::kPluginName);
        }
    }

    void RequestHeight(BlockKind a_kind, double a_height)
    {
        if (!g_accepted[Index(a_kind)]) return;
        const auto id = FinalId(a_kind);
        BlockApi::HeightRequest request{ .id = id.c_str(), .height = static_cast<float>(a_height) };
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageHeight, &request, sizeof(request), BlockApi::kPluginName);
        }
    }

    void RequestVisible(BlockKind a_kind, bool a_shown)
    {
        if (!g_accepted[Index(a_kind)]) return;
        const auto id = FinalId(a_kind);
        BlockApi::VisibleRequest request{ .id = id.c_str(), .shown = a_shown ? 1u : 0u };
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageVisible, &request, sizeof(request), BlockApi::kPluginName);
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, SKSE::InitInfo{ .log = false });
    SetupLog("Heart of Magic blocks for SL_MagicMenu (tree window, progress card, node card)");
    // The main thread: HoM's API is used on it only. kDataLoaded ran on another thread in the game run of 2026-10-25, so it is
    // taken here, where SKSE loads plugins
    GameThread::Set();
    logger::info("SL_MagicMenu_HoM: main thread {}", REX::W32::GetCurrentThreadId());
    HomSettings::Load();
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::error("SL_MagicMenu_HoM: no SKSE messaging interface - nothing installed");
        return true;
    }
    messaging->RegisterListener(OnSKSEMessage);
    return true;
}
