#pragma once

#include <cstdint>
#include <vector>

namespace logger
{
    template <class... T> void info(T&&...) {}
    template <class... T> void error(T&&...) {}
}

namespace RE
{
    using FormID = std::uint32_t;
    namespace MagicSystem
    {
        enum class SpellType { kSpell, kAbility };
    }

    struct TESForm
    {
        explicit TESForm(FormID a_id) : id(a_id) {}
        virtual ~TESForm() = default;
        FormID GetFormID() const { return id; }
        template <class T> T* As() { return dynamic_cast<T*>(this); }
        FormID id;
    };

    struct SpellItem;
    struct BGSArtObject : TESForm { using TESForm::TESForm; };

    struct EffectSetting : TESForm
    {
        using TESForm::TESForm;
        enum class Archetype { kOther, kCloak };
        struct Data
        {
            SpellItem* equipAbility = nullptr;
            TESForm* associatedForm = nullptr;
            BGSArtObject* castingArt = nullptr;
        } data;
        Archetype type = Archetype::kOther;
        bool HasArchetype(Archetype a_type) const { return type == a_type; }
    };

    struct Effect { EffectSetting* baseEffect; };

    struct MagicItem : TESForm
    {
        using TESForm::TESForm;
        std::vector<Effect*> effects;
        MagicSystem::SpellType type = MagicSystem::SpellType::kSpell;
        auto GetSpellType() const { return type; }
        EffectSetting* GetAVEffect() const { return effects.empty() ? nullptr : effects.front()->baseEffect; }
    };

    struct SpellItem : MagicItem { using MagicItem::MagicItem; };

    struct TESDataHandler
    {
        inline static TESDataHandler* singleton = nullptr;
        std::vector<SpellItem*> spells;
        static TESDataHandler* GetSingleton() { return singleton; }
        template <class T> std::vector<T*>& GetFormArray() { return spells; }
    };
}
