#include "game/ToxSelection.h"

#include "game/ToxData.h"

#include <chrono>
#include <optional>
#include <thread>
#include <vector>

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    constexpr const wchar_t* kI5ModuleName = L"InventoryInjectorImproved.dll";

    // =========================================================================
    // State (game main thread only, ToxSelection.h)
    // =========================================================================

    using Clock = std::chrono::steady_clock;

    RE::FormID g_cachedPoisonId = 0;               // 0 = no poison to copy
    std::optional<Clock::time_point> g_cachedAt;   // nullopt = recompute on the next call
    RE::FormID g_keywordedId = 0;                  // the poison carrying the keyword, 0 = none
    RE::FormID g_loggedChoiceId = 0;               // the last choice written to the log
    std::thread::id g_mainThread;                  // set by RegisterMenuSink (kDataLoaded)

    // =========================================================================
    // Choosing (spec 5.2)
    // =========================================================================

    // The player's poisons with count > 0, and the core's pick among them; 0 when none qualifies
    RE::FormID PickFromInventory()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;
        // RE/T/TESObjectREFR.h:114,406: object -> (count, entry); the filter sees the base object
        auto inventory = player->GetInventory([](RE::TESBoundObject& a_object) {
            const auto* item = a_object.As<RE::AlchemyItem>();
            return item && item->IsPoison();  // RE/A/AlchemyItem.h:72
        });
        std::vector<Toxicology::PoisonCandidate> candidates;
        candidates.reserve(inventory.size());
        for (auto& [object, data] : inventory) {
            auto& [count, entry] = data;
            if (count <= 0 || !object || !entry) continue;
            const char* name = entry->GetDisplayName();  // RE/I/InventoryEntryData.h:39
            candidates.push_back({ name ? name : "", object->GetFormID(), entry->IsFavorited(), count });
        }
        const auto top = Toxicology::PickTopPoison(candidates);  // core/ToxCore.h, checked by tools/toxicology-test
        return top ? candidates[*top].formId : 0;
    }

    void LogChoice(RE::FormID a_id)
    {
        if (a_id == g_loggedChoiceId) return;
        g_loggedChoiceId = a_id;
        if (!a_id) {
            logger::info("Toxicology: no poison to copy (no favorited poison, no ability or disabled)");
            return;
        }
        const auto* poison = RE::TESForm::LookupByID<RE::AlchemyItem>(a_id);
        logger::info("Toxicology: poison to copy: {} {:08X}", poison ? poison->GetName() : "?", a_id);
    }

    // =========================================================================
    // Icon keyword (spec 5.5, 12)
    // =========================================================================

    // Takes the keyword off the poison carrying it, found again by FormID, and forgets it
    void RemoveKeywordFromHolder()
    {
        if (!g_keywordedId) return;
        auto* keyword = ToxGame::MimicKeyword();
        // RE/T/TESForm.h:225 (As<T> after the form map lookup); a gone 0xFF form is null
        auto* holder = RE::TESForm::LookupByID<RE::AlchemyItem>(g_keywordedId);
        if (keyword && holder) {
            const bool removed = holder->RemoveKeyword(keyword);  // RE/B/BGSKeywordForm.h:39
            logger::debug("Toxicology: icon keyword {} {:08X}", removed ? "removed from" : "was not on", g_keywordedId);
        } else {
            logger::debug("Toxicology: poison {:08X} that carried the icon keyword is gone, forgotten", g_keywordedId);
        }
        g_keywordedId = 0;
    }

    void AddKeywordTo(RE::FormID a_id)
    {
        auto* keyword = ToxGame::MimicKeyword();
        auto* poison = RE::TESForm::LookupByID<RE::AlchemyItem>(a_id);
        if (!keyword || !poison) return;
        const bool added = poison->AddKeyword(keyword);  // RE/B/BGSKeywordForm.h:29, false when already there
        g_keywordedId = a_id;
        logger::debug("Toxicology: icon keyword {} {:08X}", added ? "added to" : "already on", a_id);
    }

    // =========================================================================
    // I5 icon cache (spec 5.5)
    // =========================================================================
    // I5 (InventoryInjectorImproved) caches I4's icon per FormID, so a moved keyword can leave
    // the old icon in the inventory and barter menus until its cache is purged. Its "i5" console
    // commands are not reachable from a plugin: I5 0.3.1 / 0.4.0 patch one call instruction
    // inside the console's own command function (ConsoleHook.cpp: write_call at
    // RelocationID(52065, 52952) + 0xE2 / 0x52), while RE::Script::CompileAndRun
    // (RE/S/Script.h:49, src/RE/S/Script.cpp:30-62) calls the compiler directly and never passes
    // that call. So nothing is run; the limit is logged once when I5 is loaded.

    void NoteI5Cache()
    {
        static bool noted = false;
        if (noted) return;
        noted = true;
        if (!REX::W32::GetModuleHandleW(kI5ModuleName)) {  // REX/W32/KERNEL32.h:462
            logger::info("Toxicology: InventoryInjectorImproved (I5) not loaded - no icon cache to purge");
            return;
        }
        logger::warn("Toxicology: InventoryInjectorImproved (I5) is loaded - Toxicology does not purge its icon cache, "
            "so the inventory and barter menus may show the copy icon on the old poison until it is purged in the console "
            "('i5 purge'; 'i5 cache purge' on newer I5). The favorites menu is not cached and is right");
    }

    // =========================================================================
    // Menu sink
    // =========================================================================

    class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static MenuSink* GetSingleton()
        {
            static MenuSink singleton;
            return &singleton;
        }

        MenuSink(const MenuSink&) = delete;
        MenuSink& operator=(const MenuSink&) = delete;

        // RE/B/BSTEvent.h:188; RE/M/MenuOpenCloseEvent.h; menu names RE/I/InventoryMenu.h:25, RE/F/FavoritesMenu.h:28
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!a_event || !a_event->opening) return RE::BSEventNotifyControl::kContinue;
            if (a_event->menuName != RE::InventoryMenu::MENU_NAME && a_event->menuName != RE::FavoritesMenu::MENU_NAME) {
                return RE::BSEventNotifyControl::kContinue;
            }
            // Same frame on the main thread (the icon is drawn from this opening on); from any
            // other thread the work goes to an SKSE task, which runs on the main thread
            const bool onMain = std::this_thread::get_id() == g_mainThread;
            static bool threadLogged = false;
            if (!threadLogged) {
                threadLogged = true;
                logger::info("Toxicology: menu events arrive on the {} thread", onMain ? "main" : "a non-main");
            }
            if (onMain) {
                ToxGame::OnSelectionMaybeChanged();
            } else if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([]() { ToxGame::OnSelectionMaybeChanged(); });  // SKSE/Interfaces.h:394
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        MenuSink() = default;
        ~MenuSink() override = default;
    };
}

// =============================================================================
// Public
// =============================================================================

RE::AlchemyItem* ToxGame::CurrentPoison(bool a_forceRecompute)
{
    if (!GetSettings().enabled || !PlayerHasAbility()) return nullptr;
    const auto now = Clock::now();
    if (a_forceRecompute || !g_cachedAt ||
        std::chrono::duration<double>(now - *g_cachedAt).count() >= kSelectionCacheSeconds) {
        g_cachedPoisonId = PickFromInventory();
        g_cachedAt = now;
    }
    // By FormID, not a kept pointer: a player-made poison's form can go away between calls
    return g_cachedPoisonId ? RE::TESForm::LookupByID<RE::AlchemyItem>(g_cachedPoisonId) : nullptr;
}

void ToxGame::OnSelectionMaybeChanged()
{
    const auto* poison = CurrentPoison(true);
    const RE::FormID choice = poison ? poison->GetFormID() : 0;
    LogChoice(choice);
    const RE::FormID wanted = (choice && GetSettings().showIcon && MimicKeyword()) ? choice : 0;
    if (wanted == g_keywordedId) return;
    RemoveKeywordFromHolder();
    if (wanted) AddKeywordTo(wanted);
    NoteI5Cache();
}

void ToxGame::ResetSelection()
{
    RemoveKeywordFromHolder();
    g_cachedPoisonId = 0;
    g_cachedAt.reset();
    g_loggedChoiceId = 0;
}

void ToxGame::RegisterMenuSink()
{
    g_mainThread = std::this_thread::get_id();
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        logger::error("Toxicology: no UI singleton at kDataLoaded - the copied poison is chosen only on hits and loads");
        return;
    }
    ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());  // RE/U/UI.h:83,166
    logger::info("Toxicology: menu sink registered (InventoryMenu, FavoritesMenu)");
}
