#include "FireClosure.h"

#include "FireSet.h"

#include "EncodingUtils.h"

#include <deque>
#include <format>
#include <string_view>
#include <unordered_map>

namespace BlackFlame
{
    namespace
    {
        constexpr std::string_view kOwnPluginPrefix = "heartofmagic_blackflame";
        constexpr std::uint32_t kLightLocalMask = 0xFFF;
        constexpr std::uint32_t kFullLocalMask = 0xFFFFFF;

        struct Visit
        {
            RE::TESForm* form = nullptr;
            RE::FormID via = 0;
            bool costliest = false;
        };

        // 0 not seen, 1 seen, 2 seen from a costliest seed: a form is walked again only when its state rises
        enum : std::uint8_t
        {
            kUnseen,
            kSeen,
            kSeenCostliest
        };

    }

    bool IsOwnPluginForm(const RE::TESForm* a_form)
    {
            const auto* file = a_form ? a_form->GetFile(0) : nullptr;
            if (!file) {
                return false;
            }
            const std::string_view name = file->GetFilename();
            if (name.size() < kOwnPluginPrefix.size()) {
                return false;
            }
            for (std::size_t i = 0; i < kOwnPluginPrefix.size(); ++i) {
                const auto c = static_cast<unsigned char>(name[i]);
                if (static_cast<char>(c < 0x80 ? std::tolower(c) : c) != kOwnPluginPrefix[i]) {
                    return false;
                }
            }
            return true;
    }

    namespace
    {
        std::optional<CopyKind> KindOf(RE::FormType a_type)
        {
            switch (a_type) {
            case RE::FormType::Projectile:
                return CopyKind::kProjectile;
            case RE::FormType::Explosion:
                return CopyKind::kExplosion;
            case RE::FormType::Impact:
                return CopyKind::kImpact;
            case RE::FormType::Hazard:
                return CopyKind::kHazard;
            case RE::FormType::ArtObject:
                return CopyKind::kArt;
            default:
                return std::nullopt;
            }
        }

        class Walker
        {
        public:
            FireClosure Run()
            {
                for (auto* spell : FireSpellList()) {
                    ++m_out.spells;
                    for (const auto* item : spell->effects) {
                        auto* base = item ? item->baseEffect : nullptr;
                        if (base && IsBurnable(base, spell)) {
                            Push(base, spell->GetFormID(), false);  // (a)
                        }
                    }
                    if (auto* costliest = spell->GetAVEffect()) {
                        Push(costliest, spell->GetFormID(), true);  // (b)
                    }
                }
                while (!m_queue.empty()) {
                    const Visit visit = m_queue.front();
                    m_queue.pop_front();
                    Expand(visit);
                }
                return std::move(m_out);
            }

        private:
            void Push(RE::TESForm* a_form, RE::FormID a_via, bool a_costliest)
            {
                if (!a_form) {
                    return;
                }
                auto& state = m_state[a_form->GetFormID()];
                const std::uint8_t next = a_costliest ? kSeenCostliest : kSeen;
                if (state >= next) {
                    return;
                }
                const bool first = state == kUnseen;
                state = next;
                if (first && IsOwnPluginForm(a_form)) {
                    m_out.excluded.push_back({ a_form, "defined in one of our own plugins", a_via });
                    return;
                }
                if (const auto kind = KindOf(a_form->GetFormType())) {
                    if (first) {
                        if (const auto* art = a_form->As<RE::BGSArtObject>(); art && art->numAlternateTextures != 0) {
                            m_out.excluded.push_back({ a_form, std::format("alternate textures not copied ({})",
                                                                   art->numAlternateTextures), a_via });
                            return;
                        }
                        m_index.emplace(a_form->GetFormID(), m_out.forms.size());
                        m_out.forms.push_back({ a_form, *kind, a_via, a_costliest });
                    } else if (const auto it = m_index.find(a_form->GetFormID()); it != m_index.end()) {
                        m_out.forms[it->second].reachedByCostliest = true;
                    }
                } else if (first) {
                    ++m_out.passedThrough;
                }
                m_queue.push_back({ a_form, a_via, a_costliest });
            }

            // EXPL.impactPlacedObject: a placed BASE object declared TESObjectREFR* (see FormCopiesFill.cpp PlacedLinkFor)
            void PushPlaced(RE::TESObjectREFR* a_placed, const Visit& a_from)
            {
                auto* form = reinterpret_cast<RE::TESForm*>(a_placed);
                if (!form) {
                    return;
                }
                if (KindOf(form->GetFormType())) {
                    Push(form, a_from.via, a_from.costliest);
                } else if (m_state[form->GetFormID()] == kUnseen) {
                    m_state[form->GetFormID()] = kSeen;
                    m_out.excluded.push_back({ form, "placed object is not a copy type", a_from.via });
                }
            }

            void PushSpellEffects(const RE::MagicItem* a_spell, const Visit& a_from)
            {
                if (!a_spell) {
                    return;
                }
                for (const auto* item : a_spell->effects) {
                    Push(item ? item->baseEffect : nullptr, a_from.via, a_from.costliest);
                }
            }

            void ExpandEffect(const RE::EffectSetting& a_mgef, const Visit& a_v)
            {
                const auto& d = a_mgef.data;
                Push(d.projectileBase, a_v.via, a_v.costliest);
                Push(d.explosion, a_v.via, a_v.costliest);
                Push(d.impactDataSet, a_v.via, a_v.costliest);
                Push(d.castingArt, a_v.via, a_v.costliest);
                Push(d.dualCastData, a_v.via, a_v.costliest);
                if (a_mgef.HasArchetype(RE::EffectSetting::Archetype::kSpawnHazard)) {
                    Push(d.associatedForm ? d.associatedForm->As<RE::BGSHazard>() : nullptr, a_v.via, a_v.costliest);
                }
                // The hit art goes on a target only through IsBurnable effects (the 0x0C swap): fire MGEFs only
                if (IsFireEffect(&a_mgef)) {
                    Push(d.hitEffectArt, a_v.via, a_v.costliest);
                    if (d.hitVisuals) {
                        Push(d.hitVisuals, a_v.via, a_v.costliest);
                    }
                }
            }

            void Expand(const Visit& a_v)
            {
                auto* form = a_v.form;
                if (const auto* mgef = form->As<RE::EffectSetting>()) {
                    ExpandEffect(*mgef, a_v);
                } else if (const auto* dual = form->As<RE::BGSDualCastData>()) {
                    Push(dual->data.pProjectile, a_v.via, a_v.costliest);
                    Push(dual->data.pExplosion, a_v.via, a_v.costliest);
                    Push(dual->data.pImpactDataSet, a_v.via, a_v.costliest);
                    Push(dual->data.pHitEffectArt, a_v.via, a_v.costliest);
                } else if (const auto* rfct = form->As<RE::BGSReferenceEffect>()) {
                    Push(rfct->data.artObject, a_v.via, a_v.costliest);
                } else if (const auto* proj = form->As<RE::BGSProjectile>()) {
                    Push(proj->data.explosionType, a_v.via, a_v.costliest);
                } else if (const auto* expl = form->As<RE::BGSExplosion>()) {
                    Push(expl->data.impactDataSet, a_v.via, a_v.costliest);
                    PushPlaced(expl->data.impactPlacedObject, a_v);
                    Push(expl->data.spawnProjectile, a_v.via, a_v.costliest);
                } else if (const auto* set = form->As<RE::BGSImpactDataSet>()) {
                    for (const auto& [material, impact] : set->impactMap) {
                        Push(impact, a_v.via, a_v.costliest);
                    }
                } else if (const auto* impact = form->As<RE::BGSImpactData>()) {
                    Push(impact->hazard, a_v.via, a_v.costliest);
                } else if (const auto* hazard = form->As<RE::BGSHazard>()) {
                    Push(hazard->data.impactDataSet, a_v.via, a_v.costliest);
                    PushSpellEffects(hazard->data.spell, a_v);
                }
            }

            FireClosure m_out;
            std::deque<Visit> m_queue;
            std::unordered_map<RE::FormID, std::uint8_t> m_state;
            std::unordered_map<RE::FormID, std::size_t> m_index;
        };
    }

    const char* CopyKindTag(CopyKind a_kind) noexcept
    {
        switch (a_kind) {
        case CopyKind::kProjectile:
            return "PROJ";
        case CopyKind::kExplosion:
            return "EXPL";
        case CopyKind::kImpact:
            return "IPCT";
        case CopyKind::kHazard:
            return "HAZD";
        default:
            return "ARTO";
        }
    }

    FireClosure BuildFireClosure()
    {
        return Walker().Run();
    }

    std::string ModelPathOf(const RE::TESForm* a_form)
    {
        const RE::TESModel* model = nullptr;
        if (!a_form) {
            return {};
        }
        switch (a_form->GetFormType()) {
        case RE::FormType::Projectile:
            model = a_form->As<RE::BGSProjectile>();
            break;
        case RE::FormType::Explosion:
            model = a_form->As<RE::BGSExplosion>();
            break;
        case RE::FormType::Impact:
            model = a_form->As<RE::BGSImpactData>();
            break;
        case RE::FormType::Hazard:
            model = a_form->As<RE::BGSHazard>();
            break;
        case RE::FormType::ArtObject:
            model = a_form->As<RE::BGSArtObject>();
            break;
        case RE::FormType::AddonNode:
            model = a_form->As<RE::BGSAddonNode>();
            break;
        default:
            break;
        }
        const char* text = model ? model->model.c_str() : nullptr;
        return text ? std::string(text) : std::string();
    }

    std::string SourceText(const RE::TESForm* a_form)
    {
        const auto* file = a_form ? a_form->GetFile(0) : nullptr;
        if (!file) {
            return {};
        }
        const auto local = a_form->GetFormID() & (file->IsLight() ? kLightLocalMask : kFullLocalMask);
        return std::format("{}|{:06X}", EncodingUtils::SanitizeToUTF8(std::string(file->GetFilename())), local);
    }
}
