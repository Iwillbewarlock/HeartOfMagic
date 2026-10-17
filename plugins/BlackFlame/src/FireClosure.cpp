#include "FireClosure.h"

#include "FireSet.h"

#include "EncodingUtils.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <format>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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
            return CopyKindOf(a_type);
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
                PushBoundArrows();
                while (!m_queue.empty()) {
                    const Visit visit = m_queue.front();
                    m_queue.pop_front();
                    Expand(visit);
                }
                return std::move(m_out);
            }

        private:
            // Bound-weapon stage 1: the arrows of the bound fire weapons (FireClosure.h)
            void PushBoundArrows()
            {
                std::vector<RE::BGSProjectile*> arrows;
                for (auto* ammo : BoundArrowAmmo()) {
                    ++m_out.boundArrowAmmo;
                    auto* projectile = ammo->GetRuntimeData().data.projectile;
                    if (std::ranges::find(arrows, projectile) == arrows.end()) {
                        arrows.push_back(projectile);
                    }
                }
                for (const auto& seed : BoundFireSeeds()) {
                    ++m_out.boundSeeds;
                    const auto via = seed.effect->GetFormID();
                    Push(seed.effect->data.projectileBase, via, true, kEdgeBoundArrow);
                    if (seed.weapon->IsBow() || seed.weapon->IsCrossbow()) {
                        for (auto* arrow : arrows) {
                            Push(arrow, via, true, kEdgeBoundArrow);
                        }
                    }
                }
            }

            void Push(RE::TESForm* a_form, RE::FormID a_via, bool a_costliest, std::uint32_t a_edge = kEdgeOther)
            {
                // J1: an excluded form is never walked, whichever seed reaches it again
                if (!a_form || m_excluded.contains(a_form->GetFormID())) {
                    return;
                }
                if (const auto it = m_index.find(a_form->GetFormID()); it != m_index.end()) {
                    m_out.forms[it->second].edges |= a_edge;  // C5: every edge that reaches it, walked again or not
                }
                auto& state = m_state[a_form->GetFormID()];
                const std::uint8_t next = a_costliest ? kSeenCostliest : kSeen;
                if (state >= next) {
                    return;
                }
                const bool first = state == kUnseen;
                state = next;
                const auto kind = KindOf(a_form->GetFormType());
                if (first) {
                    if (auto reason = Exclusion(a_form, kind.has_value()); !reason.empty()) {
                        Exclude(a_form, std::move(reason), a_via);
                        return;
                    }
                }
                if (kind) {
                    if (first) {
                        m_index.emplace(a_form->GetFormID(), m_out.forms.size());
                        m_out.forms.push_back({ a_form, *kind, a_via, a_costliest, a_edge });
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
                const auto kind = KindOf(form->GetFormType());
                // AB2: a texture set placed by an explosion is neither walked nor excluded (excluding it would also take away
                // the decal copy a PROJ or IPCT reaches it for); the copy EXPL keeps the source's placed set (PlacedLinkFor)
                if (kind == CopyKind::kTextureSet) {
                    if (m_placedSets.insert(form->GetFormID()).second) {
                        logger::info("BlackFlame: fire closure: explosion placed object {:08X} is a texture set: kept as the "
                                     "source's (texture sets are copied as decals only)", form->GetFormID());
                    }
                    return;
                }
                if (kind) {
                    Push(form, a_from.via, a_from.costliest);
                } else if (!m_excluded.contains(form->GetFormID())) {
                    Exclude(form, "placed object is not a form type the copies hold (PROJ, EXPL, IPCT, HAZD, ARTO)", a_from.via);
                }
            }

            // J8: a hazard's spell as a seed of plan section 1 (a)/(b): its fire effects and its costliest effect, not every
            // effect (the spell is not made a fire spell)
            void PushSpellEffects(RE::MagicItem* a_spell, const Visit& a_from)
            {
                if (!a_spell) {
                    return;
                }
                for (const auto* item : a_spell->effects) {
                    auto* base = item ? item->baseEffect : nullptr;
                    if (base && (IsFireEffect(base) || IsFireMgefAny(base))) {
                        Push(base, a_from.via, a_from.costliest);
                    }
                }
                Push(a_spell->GetAVEffect(), a_from.via, true);
            }

            // Why a_form is not walked or copied, or empty: our own plugins, a copy-type form with no defining file (made at
            // runtime: the builder needs a source), an art with alternate textures
            static std::string Exclusion(const RE::TESForm* a_form, bool a_copyType)
            {
                if (IsOwnPluginForm(a_form)) {
                    return "defined in one of our own plugins";
                }
                if (a_copyType && !a_form->GetFile(0)) {
                    return "no defining file (created at runtime)";
                }
                if (const auto* art = a_form->As<RE::BGSArtObject>(); art && art->numAlternateTextures != 0) {
                    return std::format("alternate textures not copied ({})", art->numAlternateTextures);
                }
                return {};
            }

            void Exclude(RE::TESForm* a_form, std::string a_reason, RE::FormID a_via)
            {
                m_excluded.insert(a_form->GetFormID());
                m_state[a_form->GetFormID()] = kSeenCostliest;
                m_out.excluded.push_back({ a_form, std::move(a_reason), a_via });
            }

            void ExpandEffect(const RE::EffectSetting& a_mgef, const Visit& a_v)
            {
                const auto& d = a_mgef.data;
                Push(d.projectileBase, a_v.via, a_v.costliest);
                Push(d.explosion, a_v.via, a_v.costliest);
                Push(d.impactDataSet, a_v.via, a_v.costliest);
                Push(d.castingArt, a_v.via, a_v.costliest, a_v.costliest ? kEdgeCastingArtCostliest : kEdgeCastingArt);
                Push(d.dualCastData, a_v.via, a_v.costliest);
                if (a_mgef.HasArchetype(RE::EffectSetting::Archetype::kSpawnHazard)) {
                    Push(d.associatedForm ? d.associatedForm->As<RE::BGSHazard>() : nullptr, a_v.via, a_v.costliest,
                        IsFireEffect(&a_mgef) ? kEdgeSpawnHazardFire : kEdgeSpawnHazard);
                }
                // The hit art goes on a target only through IsBurnable effects (the 0x0C swap): fire MGEFs only
                if (IsFireEffect(&a_mgef)) {
                    Push(d.hitEffectArt, a_v.via, a_v.costliest, kEdgeHitArt);
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
                    Push(dual->data.pHitEffectArt, a_v.via, a_v.costliest, kEdgeHitArt);
                } else if (const auto* rfct = form->As<RE::BGSReferenceEffect>()) {
                    Push(rfct->data.artObject, a_v.via, a_v.costliest, kEdgeHitArt);
                } else if (const auto* proj = form->As<RE::BGSProjectile>()) {
                    Push(proj->data.explosionType, a_v.via, a_v.costliest);
                    Push(proj->data.decalData, a_v.via, a_v.costliest, kEdgeDecal);  // RE/B/BGSProjectile.h decalData (0x78)
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
                    Push(impact->decalTextureSet, a_v.via, a_v.costliest, kEdgeDecal);  // RE/B/BGSImpactData.h DNAM, ENAM
                    Push(impact->decalTextureSet2, a_v.via, a_v.costliest, kEdgeDecal);
                } else if (const auto* hazard = form->As<RE::BGSHazard>()) {
                    Push(hazard->data.impactDataSet, a_v.via, a_v.costliest);
                    PushSpellEffects(hazard->data.spell, a_v);
                }
            }

            FireClosure m_out;
            std::deque<Visit> m_queue;
            std::unordered_map<RE::FormID, std::uint8_t> m_state;
            std::unordered_map<RE::FormID, std::size_t> m_index;
            std::unordered_set<RE::FormID> m_excluded;
            std::unordered_set<RE::FormID> m_placedSets;  // AB2: texture sets an explosion places (logged once)
        };
    }

    const char* CopyKindTag(CopyKind a_kind) noexcept
    {
        const auto index = static_cast<std::size_t>(a_kind);
        return index < kCopyKindCount ? kCopyKindTraits[index].tag : "?";
    }

    std::optional<CopyKind> CopyKindOf(RE::FormType a_type) noexcept
    {
        for (const auto& traits : kCopyKindTraits) {
            if (traits.formType == a_type) {
                return traits.kind;
            }
        }
        return std::nullopt;
    }

    RE::FormType CopyFormTypeOf(std::string_view a_tag) noexcept
    {
        for (const auto& traits : kCopyKindTraits) {
            if (a_tag == traits.tag) {
                return traits.formType;
            }
        }
        return RE::FormType::None;
    }

    namespace
    {
        FireClosure& Kept()
        {
            static auto* kept = std::make_unique<FireClosure>().release();  // never freed (read at exit-free times only)
            return *kept;
        }
    }

    FireClosure BuildFireClosure()
    {
        Kept() = Walker().Run();
        return Kept();
    }

    const FireClosure& LastFireClosure() noexcept
    {
        return Kept();
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
