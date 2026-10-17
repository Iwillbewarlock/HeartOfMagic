// =============================================================================
// SELF-TEST v2 STEP - T0-decal: the decal texture set copies, judged by data (FormCopies.h, TXST)
// =============================================================================
// No live decal is read: BGSDecalManager's arrays (RE/B/BGSDecalManager.h) carry no thread contract, and a SEH guard is
// no lock (design review, 2026-10-17). What the player's decals take is decided by the forms, so those are judged:
//   - every filled PROJ copy whose source has a decal texture set holds that set's TXST copy when one is filled, else
//     the source's set (BGSProjectileData::decalData); every IPCT copy the same for decalTextureSet and decalTextureSet2;
//   - every TXST copy: its decal data (DODT) present exactly when the source's is, its slots the map's, and each non-empty
//     slot's textureFileIDs entry (BSResource::ID, RE/B/BGSTextureSet.h 0x0CC) non-zero wherever the source's is (the
//     loader builds them from the record's paths: the copy's must come from its own record);
//   - INFO: one line per decal-bearing copy (which set it holds), the closure's decal texture sets without a copy.
// The decal on screen (the rune's glyph, the scorch) is the player's eye check.

#include "SelfTestCtx.h"

#include "FireClosure.h"
#include "FormCopies.h"
#include "SelfTestLog.h"

#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        constexpr std::size_t kMaxFailLines = 20;

        [[nodiscard]] RE::FormID IdOf(const RE::TESForm* a_form) noexcept
        {
            return a_form ? a_form->GetFormID() : 0;
        }

        [[nodiscard]] bool IdSet(const RE::BSResource::ID& a_id) noexcept
        {
            return a_id.file != 0 || a_id.dir != 0;
        }

        struct DecalTally
        {
            std::size_t links = 0;      // decal links of PROJ / IPCT copies whose source has a set
            std::size_t toCopies = 0;   // of those, held by a TXST copy
            std::size_t wrong = 0;      // holding neither the expected copy nor (without a copy) the source's set
            std::size_t sets = 0;       // TXST copies judged
            std::size_t setFaults = 0;  // of those, a DODT presence, slot or textureFileIDs fault
            std::size_t placedSets = 0; // AB2: copied EXPLs whose source places a texture set
            std::string lines;          // the first kMaxFailLines faults

            void Fault(const std::string& a_line)
            {
                if (++wrong + setFaults <= kMaxFailLines) {
                    lines += (lines.empty() ? "" : "; ") + a_line;
                }
            }
        };

        // a_held: the link the copy holds; a_source: the source's own set
        void JudgeLink(DecalTally& a_tally, const RE::TESForm* a_copy, const char* a_what, const RE::BGSTextureSet* a_held,
            const RE::BGSTextureSet* a_source)
        {
            if (!a_source) {
                return;
            }
            ++a_tally.links;
            const auto* expected = CopyOf(a_source);
            const RE::TESForm* want = expected ? expected : a_source;
            a_tally.toCopies += a_held == want && expected ? 1 : 0;
            if (a_held != want) {
                a_tally.Fault(std::format("{:08X} {}: holds {:08X}, expected {:08X}", IdOf(a_copy), a_what, IdOf(a_held), IdOf(want)));
            }
            const char* what = a_held != want ? "a wrong link" : expected ? "the TXST copy" : "the source's set: no TXST copy";
            SelfTestLog::Info("T0-decal", std::format("{:08X} {} -> {:08X} ({})", IdOf(a_copy), a_what, IdOf(a_held), what));
        }

        void JudgeSet(DecalTally& a_tally, const RE::BGSTextureSet& a_copy, const RE::BGSTextureSet& a_source)
        {
            ++a_tally.sets;
            std::string faults;
            if (!a_copy.decalData != !a_source.decalData) {
                faults += std::format(" DODT {} / source {}", a_copy.decalData ? "present" : "absent",
                    a_source.decalData ? "present" : "absent");
            }
            if (!CopyFill::ExpectedTexturesOf(&a_copy)) {
                faults += " no expected slots";
            }
            for (std::size_t i = 0; i < CopyFill::kTextureSlots; ++i) {
                const char* text = a_copy.textures[i].textureName.c_str();
                if (text && *text && IdSet(a_source.textureFileIDs[i]) && !IdSet(a_copy.textureFileIDs[i])) {
                    faults += std::format(" TX0{} textureFileID 0", i);
                }
            }
            const auto diff = CopyFill::Compare(&a_copy, &a_source);
            if (!diff.empty()) {
                faults += " " + diff;
            }
            if (!faults.empty()) {
                ++a_tally.setFaults;
                if (a_tally.wrong + a_tally.setFaults <= kMaxFailLines) {
                    a_tally.lines += std::format("{}TXST {:08X}:{}", a_tally.lines.empty() ? "" : "; ", a_copy.GetFormID(), faults);
                }
            }
        }
    }

    bool StepDecalCover(Ctx&, bool)
    {
        DecalTally tally;
        std::size_t idsSet = 0;
        std::size_t idSlots = 0;
        for (const auto& [source, copy] : FilledCopies()) {
            if (const auto* p = copy->As<RE::BGSProjectile>()) {
                JudgeLink(tally, copy, "PROJ decal", p->data.decalData, source->As<RE::BGSProjectile>()->data.decalData);
            } else if (const auto* i = copy->As<RE::BGSImpactData>()) {
                const auto& s = *source->As<RE::BGSImpactData>();
                JudgeLink(tally, copy, "IPCT texture set", i->decalTextureSet, s.decalTextureSet);
                JudgeLink(tally, copy, "IPCT texture set 2", i->decalTextureSet2, s.decalTextureSet2);
            } else if (const auto* e = copy->As<RE::BGSExplosion>()) {
                // AB2: an explosion that places a texture set keeps the source's set (decals only get TXST copies)
                const auto* placed = reinterpret_cast<const RE::TESForm*>(e->data.impactPlacedObject);
                const auto* own = reinterpret_cast<const RE::TESForm*>(source->As<RE::BGSExplosion>()->data.impactPlacedObject);
                if (own && own->GetFormType() == RE::FormType::TextureSet) {
                    ++tally.placedSets;
                    if (placed != own) {
                        tally.Fault(std::format("{:08X} EXPL placed texture set: holds {:08X}, expected the source's {:08X}",
                            IdOf(copy), IdOf(placed), IdOf(own)));
                    }
                }
            } else if (const auto* t = copy->As<RE::BGSTextureSet>()) {
                JudgeSet(tally, *t, *source->As<RE::BGSTextureSet>());
                for (std::size_t slot = 0; slot < CopyFill::kTextureSlots; ++slot) {
                    const char* text = t->textures[slot].textureName.c_str();
                    if (text && *text) {
                        ++idSlots;
                        idsSet += IdSet(t->textureFileIDs[slot]) ? 1 : 0;
                    }
                }
            }
        }
        std::size_t closureSets = 0;
        std::size_t closureWithout = 0;
        for (const auto& form : LastFireClosure().forms) {
            if (form.kind == CopyKind::kTextureSet) {
                ++closureSets;
                closureWithout += CopyOf(form.form) ? 0 : 1;
            }
        }
        SelfTestLog::Info("T0-decal", std::format("decal texture sets in the fire closure: {}, without a copy: {}; TXST copies' "
            "non-empty slots with a textureFileID: {} of {}; EXPL copies placing a texture set (kept the source's): {}", closureSets,
            closureWithout, idsSet, idSlots, tally.placedSets));
        const bool judged = tally.links > 0 || tally.sets > 0;
        SelfTestLog::Check("T0-decal", judged && tally.wrong == 0 && tally.setFaults == 0 && closureWithout == 0,
            std::format("{} decal links of PROJ / IPCT copies, {} to TXST copies, {} wrong; {} TXST copies, {} with a fault; {} "
                        "closure sets without a copy{}{}", tally.links, tally.toCopies, tally.wrong, tally.sets, tally.setFaults,
                closureWithout, judged ? "" : " (nothing to judge: no copy holds a decal)",
                tally.lines.empty() ? std::string() : ": " + tally.lines));
        return true;
    }
}
