// =============================================================================
// COPY SWAP - the casting (hand) art: SetCastingArt's call site (CopySwap.h, device 5)
// =============================================================================
// 1.7.104 (research 2026-10-16): ActorMagicCaster::SetCastingArt (AE 34150) has exactly one E8 caller in the whole
// executable, inside 38784: `selectedSpells[slot]` -> MagicItem::GetAVEffect (11302) -> castingArt (+0xE8) ->
// SetCastingArt(caster = actor->GetMagicCaster(slot), art). The site is found by a scan of a window from the caller's start
// (its pdata is split in three: the call sits past the first chunk) for E8s whose target is SetCastingArt; exactly one must
// be there, and (fix F4) the 7 bytes before it must be `48 8B 90 E8 00 00 00` (mov rdx, [rax+0xE8]: the castingArt read).
// Only on 1.6.1170 / 1.7.104 may the known offset +0x91 stand in for a call another plugin rewrote (an E8 into a trampoline
// stub). The thunk forwards rcx, rdx, r8, r9, checks the caster by the ActorMagicCaster vtable and the art by the
// BGSArtObject vtable before reading anything, then asks PickCastingArtImpl, which reads only FireSet's kDataLoaded cache of
// each fire spell's costliest casting art (fix F5: no GetAVEffect on a job thread). SE (fix F1): 33369 / 37830 are attempted
// AE->SE matches, never derived from SE code - not installed on SE.

#include "CopySwap.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "HandArtRefresh.h"
#include "LogLimit.h"
#include "MainThreadQueue.h"
#include "SehGuard.h"
#include "ThreadTrace.h"

#include <array>
#include <atomic>
#include <cstring>
#include <format>

namespace BlackFlame
{
    namespace
    {
        constexpr std::uint64_t kSetArtSeCandidate = 33369, kSetArtAe = 34150;  // SE: unverified matches (F1)
        constexpr std::uint64_t kCallerSeCandidate = 37830, kCallerAe = 38784;
        constexpr std::uintptr_t kScanWindow = 0x200;      // the caller is 0xDA bytes on 1.7.104, in three pdata chunks
        // F4: mov rdx, [rax+0xE8] right before the call - EffectSetting::data.castingArt (0x68 + 0x80; the same on SE and AE)
        constexpr std::array<std::uint8_t, 7> kArtLoad{ 0x48, 0x8B, 0x90, 0xE8, 0x00, 0x00, 0x00 };
        constexpr const char* kSiteName = "SetCastingArt call";
        constexpr std::uintptr_t kKnownOffsetAe = 0x91;    // 1.7.104 (1.6.1170 by unwind equality)
        constexpr int kMaxLines = 40;
        // Vtable ids (SE, AE; CommonLib Offsets_VTABLE.h), checked and resolved at install
        constexpr std::uint64_t kCasterVtableAe = 205828;  // VTABLE_ActorMagicCaster[0]
        constexpr std::uint64_t kArtVtableAe = 189017;     // VTABLE_BGSArtObject[0]
        std::uintptr_t g_casterVtable = 0;
        std::uintptr_t g_artVtable = 0;

        std::atomic<std::uint64_t> g_calls{ 0 }, g_swaps{ 0 }, g_rejected{ 0 };
        std::atomic<int> g_lines{ 0 };
        std::atomic<bool> g_failure{ false };

        template <class T>
        T ReadAt(const void* a_base, std::size_t a_offset)
        {
            T value{};
            std::memcpy(&value, static_cast<const std::uint8_t*>(a_base) + a_offset, sizeof(T));
            return value;
        }

        RE::BGSArtObject* Decide(RE::ActorMagicCaster* a_caster, RE::BGSArtObject* a_art, std::string& a_line, std::string& a_first)
        {
            if (!a_caster || !a_art || ReadAt<std::uintptr_t>(a_caster, 0) != g_casterVtable ||
                ReadAt<std::uintptr_t>(a_art, 0) != g_artVtable) {
                g_rejected.fetch_add(1);
                return a_art;
            }
            auto* actor = a_caster->GetCasterAsActor();
            const auto slot = static_cast<int>(a_caster->GetCastingSource());
            const RE::MagicItem* spell = nullptr;
            if (actor && slot >= RE::Actor::SlotTypes::kLeftHand && slot <= RE::Actor::SlotTypes::kRightHand) {
                spell = actor->GetActorRuntimeData().selectedSpells[slot];
            }
            auto* chosen = PickCastingArtImpl(actor, slot, spell, a_art, IsStoked());
            if (chosen != a_art) {
                g_swaps.fetch_add(1);
                // v2i: one line the first time each copy art is handed out (the player's lines above are capped)
                if (IsCopy(chosen) && LogLimit::FirstTime("copy casting art", chosen->GetFormID())) {
                    a_first = std::format("first copy casting art {:08X} for {:08X} (hand {}, spell {:08X}, caster {:08X})",
                        chosen->GetFormID(), a_art->GetFormID(), slot, spell ? spell->GetFormID() : 0,
                        actor ? actor->GetFormID() : 0);
                }
            }
            if (actor == RE::PlayerCharacter::GetSingleton() && g_lines.load() < kMaxLines && g_lines.fetch_add(1) < kMaxLines) {
                a_line = std::format("hand {}, spell {:08X}, art {:08X} -> {:08X}, stoked {}, thread {}", slot,
                    spell ? spell->GetFormID() : 0, a_art->GetFormID(), chosen->GetFormID(), IsStoked(), REX::W32::GetCurrentThreadId());
            }
            return chosen;
        }

        struct SetCastingArtHook
        {
            // F4: all four argument registers forwarded and rax passed back (1.7.104: two arguments; the leaf tail-jumps)
            static std::uintptr_t thunk(RE::ActorMagicCaster* a_caster, RE::BGSArtObject* a_art, void* a_r8, void* a_r9)
            {
                const ThreadTrace::Scope trace("SetCastingArt (v2 copy)");
                MainQueue::NoteJobSideHook();
                g_calls.fetch_add(1);
                RE::BGSArtObject* art = a_art;
                std::string line;
                std::string first;
                try {
                    if (GetConfig().copyCastingArt) {
                        (void)Guarded("copy casting art", [&]() { art = Decide(a_caster, a_art, line, first); });
                    }
                } catch (...) {
                    art = a_art;
                    if (!g_failure.exchange(true)) {
                        logger::error("BlackFlame: the casting art thunk threw; the engine's art was used; not logged again");
                    }
                }
                const auto result = func(a_caster, art, a_r8, a_r9);
                try {  // I9: nothing may throw out of the thunk
                    if (!line.empty()) {
                        logger::info("BlackFlame: SetCastingArt (player): {}", line);
                    }
                    if (!first.empty()) {
                        logger::info("BlackFlame: {}", first);
                    }
                } catch (...) {
                    (void)g_failure.exchange(true);
                }
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        std::vector<std::uintptr_t> ArtCallsInWindow(std::uintptr_t a_function, std::uintptr_t a_callee)
        {
            return CopySwapDetail::CallsInWindow(a_function, a_callee, kScanWindow);
        }
    }

    std::vector<std::uintptr_t> CopySwapDetail::CallsInWindow(std::uintptr_t a_function, std::uintptr_t a_callee,
        std::uintptr_t a_window)
    {
        std::vector<std::uintptr_t> sites;
        if (!Patch::IsExecutable(a_function, a_window + Patch::kRel32Size)) {
            return sites;
        }
        for (auto at = a_function; at < a_function + a_window; ++at) {
            if (*reinterpret_cast<const std::uint8_t*>(at) == Patch::kCallOpcode && Patch::Rel32Target(at) == a_callee) {
                sites.push_back(at);
            }
        }
        return sites;
    }

    namespace
    {
        // G4: SE + testerMode - the scan and the byte check on the SE candidates, read only, into the hook list (no hook)
        void ProbeSeSite()
        {
            if (!GetConfig().testerMode || !Patch::HasIds({ kSetArtSeCandidate, kCallerSeCandidate }, "SE SetCastingArt probe")) {
                return;
            }
            const auto callee = Patch::AddressOf(kSetArtSeCandidate);
            const auto caller = Patch::AddressOf(kCallerSeCandidate);
            std::string found;
            for (const auto at : ArtCallsInWindow(caller, callee)) {
                const bool load = Patch::IsExecutable(at - kArtLoad.size(), kArtLoad.size()) &&
                                  std::memcmp(reinterpret_cast<const void*>(at - kArtLoad.size()), kArtLoad.data(), kArtLoad.size()) == 0;
                found += std::format("{}+0x{:X} (bytes from -7: {}; mov rdx, [rax+0xE8] {})", found.empty() ? "" : "; ", at - caller,
                    Patch::HexBytes(at - kArtLoad.size(), kArtLoad.size() + Patch::kRel32Size), load ? "yes" : "NO");
            }
            Patch::RecordHook("SetCastingArt call (SE probe, not hooked)", false, caller, Patch::HexBytes(caller, 16),
                std::format("E8 calls to the SE candidate {} in the first 0x{:X} bytes of {}: {}", kSetArtSeCandidate, kScanWindow,
                    kCallerSeCandidate, found.empty() ? std::string("none") : found));
        }
    }

    RE::BGSArtObject* PickCastingArtImpl(const RE::Actor* a_actor, int a_slot, const RE::MagicItem* a_spell, RE::BGSArtObject* a_art,
        bool a_stoked)
    {
        if (!a_art || !a_stoked || !a_actor || a_actor != RE::PlayerCharacter::GetSingleton() ||
            (a_slot != RE::Actor::SlotTypes::kLeftHand && a_slot != RE::Actor::SlotTypes::kRightHand) || !a_spell ||
            !IsFireSpell(a_spell)) {
            return a_art;
        }
        // F5: the kDataLoaded cache, never GetAVEffect here (the thunk runs on job threads)
        if (CostliestCastingArtOf(a_spell) != a_art) {
            return a_art;  // only the art the engine takes from that spell's costliest effect
        }
        auto* copy = CopyOfAs<RE::BGSArtObject>(a_art);
        return copy ? copy : a_art;
    }

    namespace CopySwapDetail
    {
        void InstallArtSite(CopySwapStatus& a_status)
        {
            auto& site = a_status.castingArt;
            if (!GetConfig().copyCastingArt) {
                site.note = "copyCastingArt false";
                return;
            }
            if (!REL::Module::IsAE()) {
                site.note = kSeNotVerified;  // F1
                RecordSeCandidate(kSiteName, kCallerSeCandidate);
                RecordSeCandidate("SetCastingArt (callee)", kSetArtSeCandidate);
                ProbeSeSite();  // G4: read only, in testerMode
                return;
            }
            if (GetCopyCounts().arts == 0) {
                site.note = "no art copies";
                return;
            }
            if (!Patch::HasIds({ kSetArtAe, kCallerAe, kCasterVtableAe, kArtVtableAe }, "copy casting art")) {
                site.note = "Address Library id missing";
                Patch::RecordHook(kSiteName, false, 0, {}, site.note);
                return;
            }
            g_casterVtable = Patch::AddressOf(kCasterVtableAe);
            g_artVtable = Patch::AddressOf(kArtVtableAe);
            const auto callee = Patch::AddressOf(kSetArtAe);
            const auto caller = Patch::AddressOf(kCallerAe);
            const auto sites = ArtCallsInWindow(caller, callee);
            std::uintptr_t at = sites.size() == 1 ? sites.front() : 0;
            bool chained = false;
            if (sites.empty() && Patch::IsCodeReadBuild()) {
                const auto known = caller + kKnownOffsetAe;
                if (*reinterpret_cast<const std::uint8_t*>(known) == Patch::kCallOpcode && Patch::IsTrampolineStub(Patch::Rel32Target(known))) {
                    at = known;
                    chained = true;
                }
            }
            if (!at) {
                site.note = std::format("{} E8 call(s) to SetCastingArt in the caller's first 0x{:X} bytes (one expected) - left alone",
                    sites.size(), kScanWindow);
                logger::warn("BlackFlame: copy casting art: {}", site.note);
                Patch::RecordHook(kSiteName, false, caller, Patch::HexBytes(caller, 16), site.note);
                return;
            }
            const auto before = Patch::HexBytes(at - kArtLoad.size(), kArtLoad.size() + Patch::kRel32Size);
            if (!Patch::IsExecutable(at - kArtLoad.size(), kArtLoad.size()) ||
                std::memcmp(reinterpret_cast<const void*>(at - kArtLoad.size()), kArtLoad.data(), kArtLoad.size()) != 0) {
                site.note = std::format("the call at caller +0x{:X} is not preceded by mov rdx, [rax+0xE8] (bytes from -7: {}) - left "
                                        "alone", at - caller, before);
                logger::warn("BlackFlame: copy casting art: {}", site.note);
                Patch::RecordHook(kSiteName, false, at, before, site.note);
                return;
            }
            SetCastingArtHook::func = Patch::WriteCall(kSiteName, at, SetCastingArtHook::thunk,
                std::format("caller +0x{:X}{}; bytes from -7: {}", at - caller, chained ? ", chained" : "", before));
            site.hooked = true;
            site.chained = chained;
            site.offset = at - caller;
            site.note = std::format("caller +0x{:X}{} (bytes from -7: {})", at - caller, chained ? ", chained onto another plugin's "
                "redirect" : "", before);
            logger::info("BlackFlame: copy casting art: SetCastingArt call hooked at {}", site.note);
            InstallHandArtRefresh(caller, at, callee);  // the drawn hands follow the buff at the next tick (HandArtRefresh.h)
        }

        void AddArtCounters(CopySwapCounters& a_out) noexcept
        {
            a_out.castingArtCalls = g_calls.load();
            a_out.castingArtSwaps = g_swaps.load();
            a_out.castingArtRejected = g_rejected.load();
        }
    }
}
