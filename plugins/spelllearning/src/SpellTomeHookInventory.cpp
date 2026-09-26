#include "SpellTomeHook.h"

#include <mutex>

// =============================================================================
// TOME INVENTORY BOOST - does the player carry a tome for this spell?
// =============================================================================
//
// ProgressionManager::OnSpellCast asks once per learning target on every cast.
// Answering from the inventory each time walks everything the player carries,
// and the answer only changes when the inventory does. So it is kept per spell
// and thrown away when a container change involves the player, and on revert
// and load.

namespace
{
    constexpr float kPercentToFraction = 1.0f / 100.0f;

    // Whether a moved item could change the answer. Anything that is not
    // clearly "not a spell tome" counts, an unknown form included.
    bool CouldBeSpellTome(RE::FormID baseObjectId)
    {
        auto* form = RE::TESForm::LookupByID(baseObjectId);
        if (!form) return true;
        auto* book = form->As<RE::TESObjectBOOK>();
        return book && book->TeachesSpell();
    }

    // Listens for items moving into or out of the player's inventory. The
    // engine sends these from whichever thread moved the item, so the sink
    // touches nothing but an atomic counter.
    class TomeInventorySink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
    {
    public:
        static TomeInventorySink* GetSingleton()
        {
            static TomeInventorySink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESContainerChangedEvent* a_event,
            RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return RE::BSEventNotifyControl::kContinue;

            const RE::FormID playerId = player->GetFormID();
            if (a_event->oldContainer != playerId && a_event->newContainer != playerId) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (CouldBeSpellTome(a_event->baseObj)) {
                SpellTomeHook::GetSingleton()->InvalidateTomeInventoryCache();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        TomeInventorySink() = default;
        TomeInventorySink(const TomeInventorySink&) = delete;
        TomeInventorySink& operator=(const TomeInventorySink&) = delete;
    };
}

// =============================================================================
// Registration and invalidation
// =============================================================================

void SpellTomeHook::RegisterInventoryEvents()
{
    if (m_inventoryEventsRegistered) return;

    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (!eventSource) {
        logger::error("SpellTomeHook: no event source - the tome inventory check will not be cached");
        return;
    }
    eventSource->AddEventSink<RE::TESContainerChangedEvent>(TomeInventorySink::GetSingleton());
    m_inventoryEventsRegistered = true;
    logger::info("SpellTomeHook: Registered for container change events (tome inventory cache)");
}

void SpellTomeHook::InvalidateTomeInventoryCache()
{
    m_inventoryGeneration.fetch_add(1, std::memory_order_acq_rel);
}

// =============================================================================
// Helper: Check if player has a spell tome for a specific spell
// =============================================================================

bool SpellTomeHook::PlayerHasSpellTome(RE::FormID spellFormId)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;

    // Counting only the books that teach this spell skips copying every item
    // the player carries - the unfiltered GetInventory() duplicates each
    // entry's extra data just so it can be looked at and thrown away.
    const auto counts = player->GetInventoryCounts([spellFormId](RE::TESBoundObject& a_object) {
        auto* book = a_object.As<RE::TESObjectBOOK>();
        if (!book || !book->TeachesSpell()) return false;
        auto* taughtSpell = book->GetSpell();
        return taughtSpell && taughtSpell->GetFormID() == spellFormId;
    });
    for (const auto& [item, count] : counts) {
        if (count > 0) return true;
    }
    return false;
}

bool SpellTomeHook::PlayerHasSpellTomeCached(RE::FormID spellFormId) const
{
    // Read before the inventory is: a change that lands while it is being
    // walked bumps the number again, and the answer is not kept
    const std::uint64_t generation = m_inventoryGeneration.load(std::memory_order_acquire);

    {
        std::lock_guard<std::mutex> lock(m_tomeCacheMutex);
        if (m_tomeCacheGeneration != generation) {
            m_tomeInInventory.clear();
            m_tomeCacheGeneration = generation;
        } else if (auto it = m_tomeInInventory.find(spellFormId); it != m_tomeInInventory.end()) {
            return it->second;
        }
    }

    const bool hasTome = PlayerHasSpellTome(spellFormId);

    std::lock_guard<std::mutex> lock(m_tomeCacheMutex);
    if (m_tomeCacheGeneration == generation &&
        m_inventoryGeneration.load(std::memory_order_acquire) == generation) {
        m_tomeInInventory[spellFormId] = hasTome;
    }
    return hasTome;
}

// =============================================================================
// Helper: Get XP multiplier (includes tome inventory boost)
// =============================================================================

float SpellTomeHook::GetXPMultiplier(RE::FormID spellFormId) const
{
    float multiplier = 1.0f;

    // Check if tome inventory boost is enabled and player has the tome
    if (m_settings.tomeInventoryBoost && PlayerHasSpellTomeCached(spellFormId)) {
        multiplier += m_settings.tomeInventoryBoostPercent * kPercentToFraction;
        logger::trace("SpellTomeHook: Tome inventory boost active for {:08X}, multiplier = {:.2f}",
                     spellFormId, multiplier);
    }

    return multiplier;
}
