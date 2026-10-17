#pragma once

#include "Common.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// =============================================================================
// FORM COPIES (v2 spike S1) - the grey copies of fire PROJ / EXPL / IPCT / HAZD / ARTO / TXST
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
//   IPCT  data, decalTextureSet(2), sound1/2, hazard, DecalData dData (0x48-0xAC; its colour greyed)
//   TXST  boundData, flags (DNAM, 0x0C8), *decalData (DODT, 0x0C0, a pointer the record's stub allocated: assigned, colour
//         greyed); never textures[] (0x040) or textureFileIDs (0x0CC): the copy keeps its record's grey texture paths
//         (FormCopiesFillTexture.cpp)
//   HAZD  BGSHazardData data (0x80-0xB8), imageSpaceModifying (0x78), fullName, boundData
//   ARTO  data (DNAM art type, 0x68), boundData; never its TESModelTextureSwap (the copy keeps its grey
//         model). A source with alternate textures (MODS) is refused and left out (FormCopiesFillArt.cpp)
// then a second pass rewires only the links that have a copy (PROJ.explosionType and decalData,
// EXPL.impactPlacedObject and spawnProjectile, IPCT.hazard and both decal texture sets); every other
// link stays the original's (LIGH, SNDR, IMAD, ENCH, SPEL, IPDS: no IPDS copies). A copy
// that fails (form missing, wrong type, the copies plugin missing) is left out of the
// map and logged. The map is built once on the kDataLoaded thread and never changed:
// the hooks read it from any thread. A copy whose own model does not open through the game's
// file system is left out too (v2 fix J5: the engine keeps the original form). Right after the fill every copy is compared with its
// source (a difference there is a fill fault); at every kPostLoadGame / kNewGame
// RefreshCopies compares again and re-fills a copy that no longer matches (C5), logging the
// differing fields first.

namespace BlackFlame
{
    inline constexpr const char* kCopiesMapPath = "Data/SKSE/Plugins/HeartOfMagic_BlackFlame_copies.json";
    inline constexpr const char* kCopiesPluginName = "HeartOfMagic_BlackFlame_Copies.esp";

    /// kDataLoaded, once, after LoadForms: reads the copies map, fills every copy and builds the lookup
    void LoadAndFillCopies();

    /// kPostLoadGame / kNewGame (message thread): compare every copy with its live source; a copy that differs
    /// is logged field by field, filled again and its links rewired, then compared once more (C5)
    /// J1, W11: a_pointersOnly (in play): only the copy links are stored (one pointer each; CheckCopies reports the rest as
    /// deferred, W26); the structs and the BSFixedString fields (names, the muzzle
    /// model) wait for the next kPostLoadGame refresh
    void RefreshCopies(const char* a_reason, bool a_pointersOnly = false);

    /// What the last RefreshCopies of each reason found (one line per differing copy; J3: a refresh replaces only the notes of
    /// its own reason), for the self-test's T0-v2
    [[nodiscard]] std::vector<std::string> CopyRefreshNotes();

    /// The copy of a source form, or null (any thread after kDataLoaded); null too for a copy whose model failed to load
    /// in game (DisableCopiesForSession): that form stays original
    [[nodiscard]] RE::TESForm* CopyOf(const RE::TESForm* a_source) noexcept;

    struct FilledCopy
    {
        const RE::TESForm* source = nullptr;
        RE::TESForm* copy = nullptr;
    };

    /// Every copy that was filled (any thread after kDataLoaded)
    [[nodiscard]] std::vector<FilledCopy> FilledCopies();

    struct DisabledCopy
    {
        RE::FormID copy = 0;
        std::string text;  // what failed: the path and the error
    };

    /// v2i safety net, main thread, once (CopyModelProbe): a_copies' models did not load in game, so CopyOf stops handing
    /// them out for the session (the form stays original instead of invisible); a_muzzles' grey muzzle models did not load,
    /// so ExpectedMuzzleFor gives them the source's muzzle (the copy keeps its swaps; RefreshCopies then writes it).
    /// A second call is ignored.
    void DisableCopiesForSession(std::vector<DisabledCopy> a_copies, std::vector<RE::FormID> a_muzzles = {});

    /// The copies DisableCopiesForSession took out (empty before it ran)
    [[nodiscard]] std::vector<DisabledCopy> CopiesFailedToLoad();

    /// DisableCopiesForSession ran (the copy model probe finished)
    [[nodiscard]] bool CopyModelProbeDone() noexcept;

    /// J2: a_source has a copy that the copy model probe took out of the swaps (CopyOf returns null for it)
    [[nodiscard]] bool IsDroppedByProbe(const RE::TESForm* a_source) noexcept;

    template <class T>
    [[nodiscard]] T* CopyOfAs(const T* a_source) noexcept
    {
        auto* copy = CopyOf(a_source);
        return copy ? copy->As<T>() : nullptr;
    }

    /// C3: the muzzle flash model a PROJ copy should hold - the map's greyMuzzleModel (record form, without meshes\),
    /// or null for the source's own (FormCopiesFill.cpp); any thread after kDataLoaded
    [[nodiscard]] const char* ExpectedMuzzleFor(const RE::TESForm* a_copy) noexcept;

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
        std::size_t arts = 0;
        std::size_t textureSets = 0;          // TXST (decal texture sets)
        std::size_t textureSetsRejected = 0;  // of failed: TXST copies out of step with the load order (rebuild; freshness)
        std::size_t failed = 0;      // left out of the map
        std::size_t linksToCopies = 0;  // links the second pass pointed at a copy
        int mapVersion = 0;             // 1 (spike builds) or 2 (C2 builder)
        std::size_t greyless = 0;       // filled copies whose map greyModel is empty (the builder kept the source's model)
        std::size_t sourceMissing = 0;  // entries whose source form is not in the load order
        std::size_t copyMissing = 0;    // entries whose copy form is not there (the copies plugin not enabled?)
        std::size_t modelsOpened = 0;   // H1: filled copies with a model that opened through the game's file system
        std::size_t sourceAbsent = 0;   // H1: of failed, left out because the source's own mesh is in no mod or archive
        std::string requestFingerprint;       // map v2: the request's fingerprint
        std::string requestFingerprintSizes;  // map v2: its size-only form
    };

    [[nodiscard]] CopyCounts GetCopyCounts() noexcept;

    struct CopyCheckLine
    {
        bool pass = false;
        std::string text;
        bool info = false;  // H1: an INFO line (source mesh absent in this load order), not judged
    };

    /// H1: a source left out because its own mesh is in no mod or archive (not a base-game form, and other copy
    /// models opened); such a form keeps its original and is no failure of ours
    [[nodiscard]] bool IsSourceMeshAbsent(const RE::TESForm* a_source) noexcept;

    /// Whether a_form is defined by Skyrim.esm, Update.esm or one of the three DLC masters
    [[nodiscard]] bool IsBaseGameForm(const RE::TESForm* a_form);

    /// Compares every copy with its live source now (game thread or kDataLoaded): scalars and uncopied links
    /// equal the source's, copied links point to the copies. Also the per-entry failures of the fill.
    [[nodiscard]] std::vector<CopyCheckLine> CheckCopies();

    // Fill helpers, FormCopiesFill.cpp (kDataLoaded only)
    namespace CopyFill
    {
        // TXST (FormCopiesFillTexture.cpp): RE/B/BSTextureSet.h Textures::kUsedTotal (TX00..TX07)
        inline constexpr std::size_t kTextureSlots = RE::BSTextureSet::Textures::kUsedTotal;

        /// a_decal with its colour's red, green and blue set to its Rec. 709 luminance (Tint.h kLuma*), alpha kept
        [[nodiscard]] RE::DecalData ExpectedDecal(const RE::DecalData& a_decal) noexcept;

        void FillTextureSet(RE::BGSTextureSet& a_copy, const RE::BGSTextureSet& a_source);
        bool RefillTextureSet(RE::BGSTextureSet& a_copy, const RE::BGSTextureSet& a_source);
        [[nodiscard]] std::string CompareTextureSet(const RE::BGSTextureSet& a_copy, const RE::BGSTextureSet& a_source);

        /// Why a TXST copy cannot be used (empty: it can): its DODT present unlike the live source's, a live source slot
        /// other than the map's sourceTextures (the build is stale), a copy slot other than the map says (ESL and map out of
        /// step), or a grey texture that does not open through the game's file system. a_expected: the copy's slots as the
        /// map gives them (UTF-8)
        [[nodiscard]] std::string TextureSetProblem(const RE::TESForm* a_copy, const RE::TESForm* a_source,
            const nlohmann::json& a_item, std::array<std::string, kTextureSlots>& a_expected);

        /// The TXST copy's slots as the map gave them (null when a_copy is no filled TXST copy)
        [[nodiscard]] const std::array<std::string, kTextureSlots>* ExpectedTexturesOf(const RE::TESForm* a_copy) noexcept;

        /// Why a_source cannot be copied although its type can (an ARTO with alternate textures), or empty
        [[nodiscard]] std::string Refusal(const RE::TESForm* a_source);

        // ARTO (FormCopiesFillArt.cpp): the art type and the bounds, never the copy's own model
        void FillArt(RE::BGSArtObject& a_copy, const RE::BGSArtObject& a_source);
        bool RefillArt(RE::BGSArtObject& a_copy, const RE::BGSArtObject& a_source);
        [[nodiscard]] std::string CompareArt(const RE::BGSArtObject& a_copy, const RE::BGSArtObject& a_source);

        /// Data members from a_source into a_copy (same form type, checked by the caller); false for a type
        /// that is not copied
        bool FillData(RE::TESForm* a_copy, const RE::TESForm* a_source);

        /// D3 (refresh): writes into a_copy only what differs from a fill now, each data struct as one write with its links
        /// already pointing at copies; whether anything changed
        bool Refill(RE::TESForm* a_copy, const RE::TESForm* a_source, bool a_pointersOnly = false);

        /// Perf: the copies' own sources and copies by pointer (FormCopies, once after the fill resolved them; read-only after),
        /// so a diff text names a link to one of them without walking the game's form map
        void SetKnownForms(std::unordered_map<const RE::TESForm*, RE::FormID> a_known);

        /// W12, X16: a form pointer the known forms do not name: the first one by a walk of the game's form map that stops
        /// at it, from the second on in one reverse map of that map (the pairs copied under its read lock, the map built
        /// after unlocking), shared while any PointerNameScope lives and freed when the last one ends (it holds every
        /// form of the game). D3 either way: a pointer is compared, never dereferenced.
        /// Times this thread's pointer naming while it lives (the refresh summary line)
        class PointerNameScope
        {
        public:
            PointerNameScope();
            ~PointerNameScope();
            PointerNameScope(const PointerNameScope&) = delete;
            PointerNameScope& operator=(const PointerNameScope&) = delete;

            [[nodiscard]] long long LookupMicros() const noexcept { return m_micros; }  // time spent naming pointers
            [[nodiscard]] bool Walked() const noexcept { return m_built; }               // the reverse map was built under it

        private:
            friend std::string NamePointer(const RE::TESForm* a_form);
            bool m_built = false;
            long long m_micros = 0;
            PointerNameScope* m_outer = nullptr;
        };

        /// The pointer's FormID text ("XXXXXXXX"), "null", or that it is not in the form map (FormCopiesFill.cpp)
        [[nodiscard]] std::string NamePointer(const RE::TESForm* a_form);

        /// The second pass: links that have a copy point at it; returns how many were rewired
        int RewireLinks(RE::TESForm* a_copy);

        /// Empty when a_copy equals what a fill from a_source now would give; else what differs
        [[nodiscard]] std::string Compare(const RE::TESForm* a_copy, const RE::TESForm* a_source);
    }
}
