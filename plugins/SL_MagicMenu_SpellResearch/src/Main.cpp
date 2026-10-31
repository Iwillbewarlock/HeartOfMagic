// =============================================================================
// MagicMenuRewrite_SpellResearch - the first add-on block of MagicMenuRewrite
// =============================================================================
// Shows the Spell Research archetypes (element, targeting, technique) of the spell
// on the magic menu's card, "?" with only the group's frame for the ones the
// player has no knowledge of at the spell's tier, and three links that ask Spell
// Research Prisma (0.2.1+) to open its journal, alembic and experience windows.
// Registered through MagicMenuRewrite's Block API v2 (BlockApi.h, a copy of the
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

    // SR Prisma and Heart of Magic are judged by SKSE's RegisterListener (a sender that is not loaded is refused), not by
    // LoadInterface::GetPluginInfo: that call goes through a function slot of the SKSE interface that older SKSE builds
    // (and SKSE VR - not verified) may not have, and the SKSE release that added it could not be established here (final
    // review R1). So the 0.2.1 version gate is gone: an SR Prisma older than 0.2.1 that never answers 'SRPO' ends in the
    // "does not respond" reason line (Dispatch false) or the 5 s drop of the pending request
    std::atomic<bool> g_srPrismaListening{ false };
    std::atomic<bool> g_srPrismaUsable{ false };
    std::atomic<bool> g_homInstalled{ false };
    std::atomic<bool> g_registered{ false };
    std::mutex g_finalIdLock;
    std::string g_finalId;  // the block's id as MagicMenuRewrite accepted it (the 'SLMD' payload)
    std::string g_title;    // the registration's title (lives past the Dispatch)

    // MagicMenuRewrite's answer to the registration (BlockApi::Result), during our Dispatch
    void OnBlockMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg || a_msg->type != BlockApi::kMessageResult) return;
        if (!a_msg->data || a_msg->dataLen < sizeof(BlockApi::Result)) {
            logger::warn("MagicMenuRewrite_SpellResearch: a registration answer of {} bytes - ignored", a_msg->dataLen);
            return;
        }
        const auto* result = static_cast<const BlockApi::Result*>(a_msg->data);
        const std::string id = result->id ? result->id : "";
        if (result->status != BlockApi::Status::kAccepted) {
            logger::error("MagicMenuRewrite_SpellResearch: MagicMenuRewrite refused the block '{}' (API v{}): {}", id, result->apiVersion,
                result->reason ? result->reason : "");
            return;
        }
        {
            const std::scoped_lock lock(g_finalIdLock);
            g_finalId = id;
        }
        g_registered = true;
        logger::info("MagicMenuRewrite_SpellResearch: block '{}' accepted (API v{})", id, result->apiVersion);
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
                Lang::Refresh();  // SR Prisma's settings tab can change the language in game
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
        g_srPrismaUsable = g_srPrismaListening.load();
        logger::info("MagicMenuRewrite_SpellResearch: {} {} (decided by RegisterListener at kPostLoad; its version is not checked - "
                     "an SR Prisma before 0.2.1 never answers, which shows as 'does not respond')",
            SRPrismaAPI::kPluginName, g_srPrismaUsable.load() ? "loaded - links on" : "not loaded - no links");
        logger::info("MagicMenuRewrite_SpellResearch: Heart of Magic {} (RegisterListener)", g_homInstalled.load() ? "loaded" : "not loaded");
    }

    void IgnoreHom(SKSE::MessagingInterface::Message*) {}  // only to learn whether Heart of Magic is loaded (self-test)

    // True when MagicMenuRewrite accepted the block
    bool Register()
    {
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) return false;
        g_title = Lang::Get("title");
        BlockApi::Registration reg{ .id = Addon::kBlockId,
            .title = g_title.c_str(),
            .height = static_cast<float>(SrBlock::kBlockHeight),
            .drawSelection = BlockDraw::Draw,
            .click = BlockDraw::Click,
            .tabs = BlockApi::kTabSpells };
        const bool listened = messaging->Dispatch(BlockApi::kMessageRegister, &reg, sizeof(reg), BlockApi::kPluginName);
        if (!listened) {
            logger::error("MagicMenuRewrite_SpellResearch: MagicMenuRewrite did not listen (not installed, or bEnabled=0) - no block");
            return false;
        }
        if (!g_registered) logger::error("MagicMenuRewrite_SpellResearch: MagicMenuRewrite sent no accepted answer - no block");
        return g_registered.load();
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostLoad: {
            auto* messaging = SKSE::GetMessagingInterface();
            const bool host = messaging->RegisterListener(BlockApi::kPluginName, OnBlockMessage);
            const bool srPrisma = messaging->RegisterListener(SRPrismaAPI::kPluginName, BlockDraw::OnSrPrismaMessage);
            g_srPrismaListening = srPrisma;
            g_homInstalled = messaging->RegisterListener(kHomPluginName, IgnoreHom);
            logger::info("MagicMenuRewrite_SpellResearch: listening to MagicMenuRewrite {}, to {} {}", host ? "yes" : "NO (not loaded?)",
                SRPrismaAPI::kPluginName, srPrisma ? "yes" : "no (not loaded?)");
            break;
        }
        case SKSE::MessagingInterface::kPostPostLoad: CheckPlugins(); break;
        case SKSE::MessagingInterface::kPreLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            // another character: its snapshot, a held notification and a waiting open request must not carry over (SR Prisma
            // answers a waiting request with Busy on a load; the snapshot is built again at the next menu open)
            SrData::Clear();
            BlockDraw::ForgetHeld();
            BlockDraw::ForgetPending();
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
            // the menu sink only once the block is accepted: without it no menu open walks SR's lists or reads the ini
            if (Register()) {
                if (auto* ui = RE::UI::GetSingleton()) {
                    ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());
                } else {
                    logger::error("MagicMenuRewrite_SpellResearch: no UI singleton - no snapshot is ever built (the block says getting ready)");
                }
            }
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
    SetupLog("Spell Research archetypes block for MagicMenuRewrite");
    Settings::Load();
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::error("MagicMenuRewrite_SpellResearch: no SKSE messaging interface - nothing installed");
        return true;
    }
    messaging->RegisterListener(OnSKSEMessage);
    return true;
}
