#include "librarian/LoadOrderSource.h"

// =============================================================================
// LibrarianLoadOrderSource - see LoadOrderSource.h
// =============================================================================

namespace Librarian
{
    LoadOrderSource::LoadOrderSource(const json& catalogSpells) :
        m_catalog(catalogSpells)
    {
        m_fields.editorId = false;
        m_fields.magickaCost = false;
        m_fields.castingType = true;
        m_fields.effects = true;
        m_fields.keywords = true;
        m_fields.effectDetails = true;

        // Resolved once: the planner walks the catalog once per line.
        for (const auto& [id, entry] : m_catalog.items()) {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(id);
            auto* spell = formId ? RE::TESForm::LookupByID<RE::SpellItem>(formId) : nullptr;
            if (spell) m_catalogSpells.push_back({ id, spell, &entry });
        }
    }

    void LoadOrderSource::ForEachCatalogSpell(const std::function<void(const std::string&, const json&, const json&)>& visit)
    {
        for (const auto& catalogSpell : m_catalogSpells) {
            visit(catalogSpell.id, SpellJson(catalogSpell.spell), *catalogSpell.entry);
        }
    }

    std::vector<Adapters::EffectUser> LoadOrderSource::UsersOf(const std::string& effectKey)
    {
        if (!m_usersIndexed) {
            IndexUsers();
            m_usersIndexed = true;
        }
        std::vector<Adapters::EffectUser> result;
        const auto found = m_users.find(EffectFormId(effectKey));
        if (found == m_users.end()) return result;

        // An item the planner cannot judge blocks the write whatever the
        // rest are, so hand it over alone rather than building the JSON
        // of every spell that shares the effect. By form type, not
        // As<SpellItem>(): a scroll derives from SpellItem and blocks.
        // A runtime spell (0xFF) has no persistent id and blocks too.
        std::vector<std::pair<RE::SpellItem*, std::string>> spells;
        for (auto* item : found->second) {
            std::string id = SpellScanner::GetPersistentFormId(item->GetFormID());
            if (item->GetFormType() != RE::FormType::Spell || id.empty()) {
                result.push_back({ std::move(id), nullptr, nullptr });
                return result;
            }
            spells.emplace_back(static_cast<RE::SpellItem*>(item), std::move(id));
        }
        for (auto& [spell, id] : spells) {
            Adapters::EffectUser user;
            user.spell = &SpellJson(spell);
            const auto entry = m_catalog.find(id);
            user.catalogEntry = entry == m_catalog.end() ? nullptr : &*entry;
            user.id = std::move(id);
            result.push_back(std::move(user));
        }
        return result;
    }

    RE::EffectSetting* LoadOrderSource::Effect(const std::string& effectKey)
    {
        const RE::FormID formId = EffectFormId(effectKey);
        return formId ? RE::TESForm::LookupByID<RE::EffectSetting>(formId) : nullptr;
    }

    // Every magic item by the effects it carries: spells, and the
    // enchantments, scrolls, potions and ingredients that block a write.
    void LoadOrderSource::IndexUsers()
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        const auto add = [&](RE::MagicItem* item) {
            if (!item) return;
            for (const auto* effect : item->effects) {
                if (!effect || !effect->baseEffect) continue;
                auto& list = m_users[effect->baseEffect->GetFormID()];
                if (list.empty() || list.back() != item) list.push_back(item);
            }
        };
        for (auto* item : dataHandler->GetFormArray<RE::SpellItem>()) add(item);
        for (auto* item : dataHandler->GetFormArray<RE::EnchantmentItem>()) add(item);
        for (auto* item : dataHandler->GetFormArray<RE::ScrollItem>()) add(item);
        for (auto* item : dataHandler->GetFormArray<RE::AlchemyItem>()) add(item);
        for (auto* item : dataHandler->GetFormArray<RE::IngredientItem>()) add(item);
    }

    // An effect key is the scan's "form", a persistent id. A runtime
    // form (0xFF) has none and resolves to 0: nothing is written to it.
    RE::FormID LoadOrderSource::EffectFormId(const std::string& effectKey)
    {
        const auto cached = m_effectIds.find(effectKey);
        if (cached != m_effectIds.end()) return cached->second;
        const RE::FormID formId = SpellScanner::ResolvePersistentFormId(effectKey);
        m_effectIds.emplace(effectKey, formId);
        return formId;
    }

    // Built once per spell, the scan's own shape, so the planner reads
    // exactly the fields it was measured on. The scanner leaves out the
    // keywords the perk adapters added, so a spell's JSON is as its plugins wrote it.
    const LoadOrderSource::json& LoadOrderSource::SpellJson(RE::SpellItem* spell)
    {
        auto& slot = m_spellJson[spell->GetFormID()];
        if (!slot) {
            slot = std::make_unique<json>(SpellScanner::BuildSpellJson(spell, spell->GetFormID(), m_fields));
        }
        return *slot;
    }
}
