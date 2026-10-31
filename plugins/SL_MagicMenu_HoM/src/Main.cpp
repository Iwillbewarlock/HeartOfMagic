// =============================================================================
// MagicMenuRewrite_HoM - the Heart of Magic blocks of MagicMenuRewrite's magic menu
// =============================================================================
// The spell tree window, the learning progress card and the node card, which the
// magic menu drew itself until HoM add-on plan Task 5, as three add-on blocks
// registered through MagicMenuRewrite's Block API v3 (BlockApi.h, the host's own
// header from plugins/SL_MagicMenu/src). They read Heart of Magic through its
// public API v2 (SpellLearningAPI.h) and the saved spell_tree.json, and their
// keys from MagicMenuRewrite.ini (HomSettings). Without Heart of Magic's API by
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
#include "LangGame.h"
#include "SpellLearningAPI.h"
#include "Spells.h"
#include "Strings.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    constexpr const char* kHomPluginName = "SpellLearning";

    // The blocks' final ids as MagicMenuRewrite accepted them (the payload of 'SLMD' / 'SLMH' / 'SLMV'); set during the
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

    // MagicMenuRewrite's answer to a registration (BlockApi::Result), during our Dispatch; matched to a block by its id
    void OnBlockMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg || a_msg->type != BlockApi::kMessageResult) return;
        if (!a_msg->data || a_msg->dataLen < sizeof(BlockApi::Result)) {
            logger::warn("MagicMenuRewrite_HoM: a registration answer of {} bytes - ignored", a_msg->dataLen);
            return;
        }
        const auto* result = static_cast<const BlockApi::Result*>(a_msg->data);
        const std::string id = result->id ? result->id : "";
        const auto it = std::ranges::find(HomAddon::kBlockIds, std::string_view(id));
        if (it == HomAddon::kBlockIds.end()) {
            logger::warn("MagicMenuRewrite_HoM: an answer for '{}', not one of our blocks - ignored", id);
            return;
        }
        const auto index = static_cast<std::size_t>(it - HomAddon::kBlockIds.begin());
        if (result->status != BlockApi::Status::kAccepted) {
            logger::error("MagicMenuRewrite_HoM: MagicMenuRewrite refused the block '{}' (API v{}): {}", id, result->apiVersion, result->reason ? result->reason : "");
            return;
        }
        {
            const std::scoped_lock lock(g_idLock);
            g_finalIds[index] = id;
        }
        g_accepted[index] = true;
        logger::info("MagicMenuRewrite_HoM: block '{}' accepted (API v{})", id, result->apiVersion);
    }

    // HoM broadcasts its API at kPostPostLoad
    void OnSpellLearningMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        if (a_msg->type != SpellLearning::kMessageType_APIReady || !a_msg->data) {
            logger::info("MagicMenuRewrite_HoM: SpellLearning message type 0x{:X} ignored", a_msg->type);
            return;
        }
        auto* api = static_cast<SpellLearning::ISpellLearningAPI*>(a_msg->data);
        Hom::SetApi(api);
        logger::info("MagicMenuRewrite_HoM: Heart of Magic API v{} received", api->GetAPIVersion());
    }

    void SendKeyHints(const SKSE::MessagingInterface* a_messaging);  // below, with the registrations

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
            if (a_event->opening) {
                // 2026-10-31 (the settings tab): [Display] sLanguage read again; a new language gets new texts from this menu on,
                // and the key hints MagicMenuRewrite keeps are sent again with the new labels (relabelled in place). The tree's title
                // strip keeps the start language: the host copied it at the registration (Block API v3 has no title update)
                if (LangGame::Refresh(HomStrings::g_catalog, HomStrings::kLangDir)) {
                    if (const auto* messaging = SKSE::GetMessagingInterface()) SendKeyHints(messaging);
                }
                HomAddon::OnMenuOpen();
            } else {
                HomAddon::OnMenuClose();  // SelfTest L2 mask clicks
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        MenuSink() = default;
    };

    // One registration (the struct lives for the Dispatch only; MagicMenuRewrite copies id, title and legacyId)
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
            // Task 7 draws the tree window (own mouse: its node clips' handlers); its key callback takes U and G for all three, and its
            // frame does their shared per-frame work while it shows (fix round 1 I1: a card's frame does it with the tree hidden)
            reg.title = g_treeTitle.c_str();
            reg.drawSelection = HomAddon::DrawTreeBlock;
            reg.frame = HomAddon::TreeFrame;
            reg.key = HomAddon::TreeKey;
            reg.flags = kFlagOwnMouse | kFlagKeepWhenEmpty;
            break;
        case BlockKind::kProgress:
            reg.drawSelection = HomAddon::DrawProgressBlock;
            reg.frame = HomAddon::ProgressFrame;  // fix round 1 I1: the shared per-frame work when the tree is hidden
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
            logger::error("MagicMenuRewrite_HoM: MagicMenuRewrite did not listen (not installed, or bEnabled=0) - no block {}", reg.id);
            return false;
        }
        if (!g_accepted[index]) logger::error("MagicMenuRewrite_HoM: MagicMenuRewrite sent no accepted answer for {} - that block is off", reg.id);
        return g_accepted[index].load();
    }

    // 2026-10-31 (user: "단축키 표시"): the keys the tree block's key callback takes (next spell, set the learning target) as hints in
    // SkyUI's key-hint strip ('SLMK'), sent at the registration and again after a language change (MenuSink): the host keeps them for
    // the session (the same block and key relabelled in place) and shows them while the tree block shows
    void SendKeyHints(const SKSE::MessagingInterface* a_messaging)
    {
        const auto id = FinalId(HomAddon::BlockKind::kTree);
        const auto& keys = HomSettings::Get();
        const std::array<std::pair<std::uint32_t, HomStrings::Str>, 2> hints{ { { keys.keyNext, HomStrings::Str::kHintNext },
            { keys.keySetTarget, HomStrings::Str::kHintSetTarget } } };
        for (const auto& [key, text] : hints) {
            if (key == 0 || key > BlockApi::kMaxHintScanCode) {
                logger::info("MagicMenuRewrite_HoM: \"{}\" key unbound - no hint", HomStrings::Text(text));
                continue;
            }
            BlockApi::KeyHint hint{ .id = id.c_str(), .scanCode = key, .label = HomStrings::Text(text) };  // Dispatch takes a non-const pointer
            if (!a_messaging->Dispatch(BlockApi::kMessageKeyHint, &hint, sizeof(hint), BlockApi::kPluginName)) {
                logger::warn("MagicMenuRewrite_HoM: the key hint {} was not delivered (MagicMenuRewrite did not listen)", key);
            }
        }
        logger::info("MagicMenuRewrite_HoM: key hints for {} done (keys {} and {}; an unbound one is logged above)", id, keys.keyNext, keys.keySetTarget);
    }

    // True when the tree block (the driver) was accepted
    bool Register()
    {
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) return false;
        const auto heights = HomSettings::BlockHeights(HomSettings::Get());
        g_heights = { heights.tree, heights.progress, heights.nodeCard };
        g_treeTitle = HomStrings::Text(HomStrings::Str::kTreeTitle);
        // the tree block carries the keys and their hints (and is the first to do the shared frame work): without it no card is registered
        if (!RegisterOne(messaging, HomAddon::BlockKind::kTree)) {
            logger::error("MagicMenuRewrite_HoM: the tree block was not accepted - the progress and node cards are not registered either");
            return false;
        }
        RegisterOne(messaging, HomAddon::BlockKind::kProgress);
        RegisterOne(messaging, HomAddon::BlockKind::kNodeCard);
        SendKeyHints(messaging);
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
            logger::info("MagicMenuRewrite_HoM: listening to MagicMenuRewrite {}, to {} {}", host ? "yes" : "NO (not loaded?)", kHomPluginName,
                hom ? "yes" : "no (not loaded?)");
            break;
        }
        case SKSE::MessagingInterface::kDataLoaded: {
            // user 2026-10-30 (design Q3): without Heart of Magic's API there is no block at all
            if (!Hom::Ready()) {
                logger::warn("MagicMenuRewrite_HoM: Heart of Magic's API did not arrive by kDataLoaded (SpellLearning.dll not loaded, or too old) - "
                             "no block registered");
                FlushLog();
                return;
            }
            logger::info("MagicMenuRewrite_HoM: kDataLoaded on thread {}, main thread {}", REX::W32::GetCurrentThreadId(), GameThread::Id());
            // the texts in [Display] sLanguage's language (refreshed at each menu open, MenuSink); before Register, which copies the tree title
            LangGame::Load(HomStrings::g_catalog, HomStrings::kLangDir);
            Spells::Load();
            if (Register()) {
                if (auto* ui = RE::UI::GetSingleton()) {
                    ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());
                } else {
                    logger::error("MagicMenuRewrite_HoM: no UI singleton - HoM's style is not read at menu opens (its defaults are used)");
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

    void RequestShowSpell(RE::FormID a_form, std::uint32_t a_requestId, bool a_nameShown)
    {
        if (!g_accepted[Index(BlockKind::kTree)]) return;
        const auto id = FinalId(BlockKind::kTree);
        BlockApi::ShowSpellRequest request{ .id = id.c_str(), .formId = a_form, .requestId = a_requestId, .nameShown = a_nameShown ? 1u : 0u };
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageShowSpell, &request, sizeof(request), BlockApi::kPluginName);
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
    SetupLog("Heart of Magic blocks for MagicMenuRewrite (tree window, progress card, node card)");
    // The main thread: HoM's API is used on it only. kDataLoaded ran on another thread in the game run of 2026-10-25, so it is
    // taken here, where SKSE loads plugins
    GameThread::Set();
    logger::info("MagicMenuRewrite_HoM: main thread {}", REX::W32::GetCurrentThreadId());
    HomSettings::Load();
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::error("MagicMenuRewrite_HoM: no SKSE messaging interface - nothing installed");
        return true;
    }
    messaging->RegisterListener(OnSKSEMessage);
    return true;
}
