// =============================================================================
// SL_MagicMenu_SpellResearch - the first add-on block of SL_MagicMenu
// =============================================================================
// Shows the Spell Research archetypes (element, targeting, technique) of the spell
// on the magic menu's card, "?" with only the group's frame for the ones the
// player has no knowledge of at the spell's tier, and three links that ask Spell
// Research Prisma (0.2.1+) to open its journal, alembic and experience windows.
// Registered through SL_MagicMenu's Block API v2 (BlockApi.h, a copy of the
// host's). SRPrismaAPI.h is SR Prisma's own header, copied unchanged from
// SpellResearchPrismaUI HEAD dd084c3 plugin/src/SRPrismaAPI.h.
// Spec: docs/superpowers/specs/2026-10-27-spell-research-addon-design.md
// =============================================================================

#include "Common.h"

#include "Addon.h"
#include "BlockApi.h"
#include "BlockDraw.h"
#include "Lang.h"
#include "SRPrismaAPI.h"
#include "Settings.h"
#include "SrBlockRules.h"
#include "SrData.h"

#include <atomic>
#include <mutex>
#include <string>

namespace
{
    constexpr const char* kHomPluginName = "SpellLearning";
    constexpr REL::Version kMinSrPrisma{ 0, 2, 1 };  // the first SR Prisma that answers 'SRPO'

    const SKSE::LoadInterface* g_load = nullptr;
    std::atomic<bool> g_srPrismaUsable{ false };
    std::atomic<bool> g_homInstalled{ false };
    std::atomic<bool> g_registered{ false };
    std::mutex g_finalIdLock;
    std::string g_finalId;  // the block's id as SL_MagicMenu accepted it (the 'SLMD' payload)
    std::string g_title;    // the registration's title (lives past the Dispatch)

    // SL_MagicMenu's answer to the registration (BlockApi::Result), during our Dispatch
    void OnBlockMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg || a_msg->type != BlockApi::kMessageResult) return;
        if (!a_msg->data || a_msg->dataLen < sizeof(BlockApi::Result)) {
            logger::warn("SL_MagicMenu_SpellResearch: a registration answer of {} bytes - ignored", a_msg->dataLen);
            return;
        }
        const auto* result = static_cast<const BlockApi::Result*>(a_msg->data);
        const std::string id = result->id ? result->id : "";
        if (result->status != BlockApi::Status::kAccepted) {
            logger::error("SL_MagicMenu_SpellResearch: SL_MagicMenu refused the block '{}' (API v{}): {}", id, result->apiVersion,
                result->reason ? result->reason : "");
            return;
        }
        {
            const std::scoped_lock lock(g_finalIdLock);
            g_finalId = id;
        }
        g_registered = true;
        logger::info("SL_MagicMenu_SpellResearch: block '{}' accepted (API v{})", id, result->apiVersion);
    }

    void OnBlockBuilt()
    {
        Addon::RequestRedraw();
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
            if (a_event->opening) {
                Settings::ResolveLayout();
                BlockDraw::OnMenuOpen();
                SrData::RequestBuild(OnBlockBuilt);
            } else {
                BlockDraw::OnMenuClose();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        MenuSink() = default;
    };

    void CheckPlugins()
    {
        const auto* srPrisma = g_load ? g_load->GetPluginInfo(SRPrismaAPI::kPluginName) : nullptr;
        if (!srPrisma) {
            logger::info("SL_MagicMenu_SpellResearch: {} is not loaded - no links", SRPrismaAPI::kPluginName);
        } else {
            const auto version = REL::Version::unpack(srPrisma->version);
            g_srPrismaUsable = srPrisma->version >= kMinSrPrisma.pack();
            logger::info("SL_MagicMenu_SpellResearch: {} {} loaded - links {}", SRPrismaAPI::kPluginName, version.string("."),
                g_srPrismaUsable.load() ? "on" : std::format("off (needs {} or later: older builds never answer 'SRPO')", kMinSrPrisma.string(".")));
        }
        g_homInstalled = g_load && g_load->GetPluginInfo(kHomPluginName) != nullptr;
    }

    void Register()
    {
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) return;
        g_title = Lang::Get("title");
        BlockApi::Registration reg{ .id = Addon::kBlockId,
            .title = g_title.c_str(),
            .height = static_cast<float>(SrBlock::kBlockHeight),
            .drawSelection = BlockDraw::Draw,
            .click = BlockDraw::Click,
            .tabs = BlockApi::kTabSpells };
        const bool listened = messaging->Dispatch(BlockApi::kMessageRegister, &reg, sizeof(reg), BlockApi::kPluginName);
        if (!listened) {
            logger::error("SL_MagicMenu_SpellResearch: SL_MagicMenu did not listen (not installed, or bEnabled=0) - no block");
            return;
        }
        if (!g_registered) logger::error("SL_MagicMenu_SpellResearch: SL_MagicMenu sent no accepted answer - no block");
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostLoad: {
            auto* messaging = SKSE::GetMessagingInterface();
            const bool host = messaging->RegisterListener(BlockApi::kPluginName, OnBlockMessage);
            const bool srPrisma = messaging->RegisterListener(SRPrismaAPI::kPluginName, BlockDraw::OnSrPrismaMessage);
            logger::info("SL_MagicMenu_SpellResearch: listening to SL_MagicMenu {}, to {} {}", host ? "yes" : "NO (not loaded?)",
                SRPrismaAPI::kPluginName, srPrisma ? "yes" : "no (not loaded?)");
            break;
        }
        case SKSE::MessagingInterface::kPostPostLoad: CheckPlugins(); break;
        case SKSE::MessagingInterface::kPreLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            // another character: its snapshot and a held notification must not show (built again at the next menu open)
            SrData::Clear();
            BlockDraw::ForgetHeld();
            break;
        case SKSE::MessagingInterface::kDataLoaded: {
            Lang::Load();
            if (Settings::SelfTest()) Lang::SelfTest();
            std::string why;
            const auto init = SrData::Init(why);
            if (init != SrData::InitResult::kReady) {
                if (Settings::SelfTest()) {
                    // spec "Self-test": the lists' check has its line also when the block is off
                    const bool broken = init == SrData::InitResult::kBroken;
                    logger::info("[selftest-sr] lists {} {} - the block is off", broken ? "FAIL" : "SKIP", why);
                    logger::info("[selftest-sr] DONE pass=0 fail={} skip={}", broken ? 1 : 0, broken ? 0 : 1);
                }
                FlushLog();
                return;
            }
            if (auto* ui = RE::UI::GetSingleton()) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());
            } else {
                logger::error("SL_MagicMenu_SpellResearch: no UI singleton - no snapshot is ever built (the block says getting ready)");
            }
            Register();
            FlushLog();
            break;
        }
        default: break;
        }
    }
}

namespace Addon
{
    bool SrPrismaUsable()
    {
        return g_srPrismaUsable.load();
    }

    bool HomInstalled()
    {
        return g_homInstalled.load();
    }

    void RequestRedraw()
    {
        if (!g_registered) return;
        std::string id;
        {
            const std::scoped_lock lock(g_finalIdLock);
            id = g_finalId;
        }
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageRedraw, id.data(), static_cast<std::uint32_t>(id.size() + 1), BlockApi::kPluginName);
        }
    }

    void RequestHeight(double a_height)
    {
        if (!g_registered) return;
        std::string id;
        {
            const std::scoped_lock lock(g_finalIdLock);
            id = g_finalId;
        }
        BlockApi::HeightRequest request{ .id = id.c_str(), .height = static_cast<float>(a_height) };
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(BlockApi::kMessageHeight, &request, sizeof(request), BlockApi::kPluginName);
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, SKSE::InitInfo{ .log = false });
    SetupLog("Spell Research archetypes block for SL_MagicMenu");
    g_load = a_skse;
    Settings::Load();
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::error("SL_MagicMenu_SpellResearch: no SKSE messaging interface - nothing installed");
        return true;
    }
    messaging->RegisterListener(OnSKSEMessage);
    return true;
}
