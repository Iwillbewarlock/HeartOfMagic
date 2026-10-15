#pragma once

#include "Common.h"

#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// FORM COPIES (v2 spike S1) - the grey copies of fire PROJ / EXPL / IPCT / HAZD
// =============================================================================
// tools/blackflame-greybuild --forms writes HeartOfMagic_BlackFlame_Copies.esp: one
// masterless placeholder record per source form (EDID, OBND, MODL = a fully grey
// copy of the source's NIF, DATA scalars, every form link 0), and the copies map
// SKSE/Plugins/HeartOfMagic_BlackFlame_copies.json (source "plugin|localId" -> copy
// local id). At kDataLoaded (only) each copy is filled from the LIVE winning source
// form, per type, data members only (never the TESForm header, flags, the copy's own
// TESModel, or engine-internal pointers):
//   PROJ  BGSProjectileData data (0x80-0x108), soundLevel (0x130), fullName,
//         muzzleFlashModel through TESModel::SetModel, boundData
//   EXPL  BGSExplosionData data (0x98-0xE8), TESEnchantableForm members (0x70-0x7C),
//         TESImageSpaceModifiableForm imageSpaceModifying (0x90), fullName, boundData
//   IPCT  data, decalTextureSet(2), sound1/2, hazard, DecalData dData (0x48-0xAC)
//   HAZD  BGSHazardData data (0x80-0xB8), imageSpaceModifying (0x78), fullName, boundData
// then a second pass rewires only the links that have a copy (PROJ.explosionType,
// EXPL.impactPlacedObject and spawnProjectile, IPCT.hazard); every other link stays
// the original's (LIGH, SNDR, TXST, IMAD, ENCH, SPEL, IPDS: no IPDS copies). A copy
// that fails (form missing, wrong type, the copies plugin missing) is left out of the
// map and logged. The map is built once on the kDataLoaded thread and never changed:
// the hooks read it from any thread.

namespace BlackFlame
{
    inline constexpr const char* kCopiesMapPath = "Data/SKSE/Plugins/HeartOfMagic_BlackFlame_copies.json";
    inline constexpr const char* kCopiesPluginName = "HeartOfMagic_BlackFlame_Copies.esp";

    /// kDataLoaded, once, after LoadForms: reads the copies map, fills every copy and builds the lookup
    void LoadAndFillCopies();

    /// The copy of a source form, or null (any thread after kDataLoaded)
    [[nodiscard]] RE::TESForm* CopyOf(const RE::TESForm* a_source) noexcept;

    template <class T>
    [[nodiscard]] T* CopyOfAs(const T* a_source) noexcept
    {
        auto* copy = CopyOf(a_source);
        return copy ? copy->As<T>() : nullptr;
    }

    /// The source a copy was filled from, or null when a_form is not a copy
    [[nodiscard]] const RE::TESForm* SourceOf(const RE::TESForm* a_form) noexcept;

    [[nodiscard]] inline bool IsCopy(const RE::TESForm* a_form) noexcept
    {
        return SourceOf(a_form) != nullptr;
    }

    /// a_form, or its source when it is a copy (fire set checks of copies)
    [[nodiscard]] inline const RE::TESForm* SourceOrSelf(const RE::TESForm* a_form) noexcept
    {
        const auto* source = SourceOf(a_form);
        return source ? source : a_form;
    }

    struct CopyCounts
    {
        bool mapRead = false;
        std::size_t mapEntries = 0;
        std::size_t projectiles = 0;
        std::size_t explosions = 0;
        std::size_t impacts = 0;
        std::size_t hazards = 0;
        std::size_t failed = 0;      // left out of the map
        std::size_t linksToCopies = 0;  // links the second pass pointed at a copy
    };

    [[nodiscard]] CopyCounts GetCopyCounts() noexcept;

    struct CopyCheckLine
    {
        bool pass = false;
        std::string text;
    };

    /// Compares every copy with its live source now (game thread or kDataLoaded): scalars and uncopied links
    /// equal the source's, copied links point to the copies. Also the per-entry failures of the fill.
    [[nodiscard]] std::vector<CopyCheckLine> CheckCopies();

    // Fill helpers, FormCopiesFill.cpp (kDataLoaded only)
    namespace CopyFill
    {
        /// Data members from a_source into a_copy (same form type, checked by the caller); false for a type
        /// that is not copied
        bool FillData(RE::TESForm* a_copy, const RE::TESForm* a_source);

        /// The second pass: links that have a copy point at it; returns how many were rewired
        int RewireLinks(RE::TESForm* a_copy);

        /// Empty when a_copy equals what a fill from a_source now would give; else what differs
        [[nodiscard]] std::string Compare(const RE::TESForm* a_copy, const RE::TESForm* a_source);
    }
}
