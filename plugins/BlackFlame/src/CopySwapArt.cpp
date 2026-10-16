// =============================================================================
// COPY SWAP - the casting (hand) art: SetCastingArt's call site (CopySwap.h, device 5)
// =============================================================================
// 1.7.104 (research 2026-10-16): ActorMagicCaster::SetCastingArt (AE 34150, SE 33369) has exactly one E8 caller in the
// whole executable, inside 38784 (SE 37830): `selectedSpells[slot]` -> MagicItem::GetAVEffect (11302) -> castingArt
// (+0xE8) -> SetCastingArt(caster = actor->GetMagicCaster(slot), art). The site is found on every runtime by a scan of a
// window from the caller's start (its pdata is split: the call sits past the first chunk) for E8s whose target is
// SetCastingArt; exactly one must be there. Only on 1.6.1170 / 1.7.104 may the known offset +0x91 stand in for a call
// another plugin rewrote (an E8 into a trampoline stub). The thunk checks the caster by the ActorMagicCaster vtable and
// the art by the BGSArtObject vtable before reading anything, then asks PickCastingArtImpl.

#include "CopySwap.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "MainThreadQueue.h"
#include "SehGuard.h"
#include "ThreadTrace.h"

#include <atomic>
#include <cstring>
#include <format>

namespace BlackFlame
{
    namespace
    {
        constexpr std::uint64_t kSetArtSe = 33369, kSetArtAe = 34150;
        constexpr std::uint64_t kCallerSe = 37830, kCallerAe = 38784;
        constexpr std::uintptr_t kScanWindow = 0x200;      // the caller is 0xD0 bytes on 1.7.104, in two pdata chunks
        constexpr std::uintptr_t kKnownOffsetAe = 0x91;    // 1.7.104 (1.6.1170 by unwind equality)
        constexpr int kMaxLines = 40;
        // Vtable ids (SE, AE; CommonLib Offsets_VTABLE.h), checked and resolved at install
        constexpr std::uint64_t kCasterVtableSe = 257613, kCasterVtableAe = 205828;  // VTABLE_ActorMagicCaster[0]
        constexpr std::uint64_t kArtVtableSe = 233406, kArtVtableAe = 189017;        // VTABLE_BGSArtObject[0]
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

        RE::BGSArtObject* Decide(RE::ActorMagicCaster* a_caster, RE::BGSArtObject* a_art, std::string& a_line)
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
            }
            if (actor == RE::PlayerCharacter::GetSingleton() && g_lines.load() < kMaxLines && g_lines.fetch_add(1) < kMaxLines) {
                a_line = std::format("hand {}, spell {:08X}, art {:08X} -> {:08X}, stoked {}, thread {}", slot,
                    spell ? spell->GetFormID() : 0, a_art->GetFormID(), chosen->GetFormID(), IsStoked(), REX::W32::GetCurrentThreadId());
            }
            return chosen;
        }

        struct SetCastingArtHook
        {
            // rax is passed back (the leaf tail-jumps on 1.7.104; a caller elsewhere may read it)
            static std::uintptr_t thunk(RE::ActorMagicCaster* a_caster, RE::BGSArtObject* a_art)
            {
                const ThreadTrace::Scope trace("SetCastingArt (v2 copy)");
                MainQueue::NoteJobSideHook();
                g_calls.fetch_add(1);
                RE::BGSArtObject* art = a_art;
                std::string line;
                try {
                    if (GetConfig().copyCastingArt) {
                        (void)Guarded("copy casting art", [&]() { art = Decide(a_caster, a_art, line); });
                    }
                } catch (...) {
                    art = a_art;
                    if (!g_failure.exchange(true)) {
                        logger::error("BlackFlame: the casting art thunk threw; the engine's art was used; not logged again");
                    }
                }
                const auto result = func(a_caster, art);
                if (!line.empty()) {
                    logger::info("BlackFlame: SetCastingArt (player): {}", line);
                }
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // Every E8 to a_callee in [a_function, a_function + kScanWindow)
        std::vector<std::uintptr_t> CallsInWindow(std::uintptr_t a_function, std::uintptr_t a_callee)
        {
            std::vector<std::uintptr_t> sites;
            if (!Patch::IsExecutable(a_function, kScanWindow + Patch::kRel32Size)) {
                return sites;
            }
            for (auto at = a_function; at < a_function + kScanWindow; ++at) {
                if (*reinterpret_cast<const std::uint8_t*>(at) == Patch::kCallOpcode && Patch::Rel32Target(at) == a_callee) {
                    sites.push_back(at);
                }
            }
            return sites;
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
        const auto* costliest = a_spell->GetAVEffect();
        if (!costliest || costliest->data.castingArt != a_art) {
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
            if (GetCopyCounts().arts == 0) {
                site.note = "no art copies";
                return;
            }
            const auto calleeId = Patch::IdFor(kSetArtSe, kSetArtAe);
            const auto callerId = Patch::IdFor(kCallerSe, kCallerAe);
            const auto casterVtableId = Patch::IdFor(kCasterVtableSe, kCasterVtableAe);
            const auto artVtableId = Patch::IdFor(kArtVtableSe, kArtVtableAe);
            if (!Patch::HasIds({ calleeId, callerId, casterVtableId, artVtableId }, "copy casting art")) {
                site.note = "an Address Library id is missing";
                Patch::RecordHook("SetCastingArt call", false, 0, {}, site.note);
                return;
            }
            g_casterVtable = Patch::AddressOf(casterVtableId);
            g_artVtable = Patch::AddressOf(artVtableId);
            const auto callee = Patch::AddressOf(calleeId);
            const auto caller = Patch::AddressOf(callerId);
            const auto sites = CallsInWindow(caller, callee);
            std::uintptr_t at = sites.size() == 1 ? sites.front() : 0;
            bool chained = false;
            if (sites.empty() && REL::Module::IsAE() && Patch::IsCodeReadBuild()) {
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
                Patch::RecordHook("SetCastingArt call", false, caller, Patch::HexBytes(caller, 16), site.note);
                return;
            }
            const auto before = Patch::HexBytes(at >= 7 ? at - 7 : at, 12);
            SetCastingArtHook::func = Patch::WriteCall("SetCastingArt call", at, SetCastingArtHook::thunk,
                std::format("caller +0x{:X}{}; bytes from -7: {}", at - caller, chained ? ", chained" : "", before));
            site.hooked = true;
            site.chained = chained;
            site.offset = at - caller;
            site.note = std::format("caller +0x{:X}{} (bytes from -7: {})", at - caller, chained ? ", chained onto another plugin's "
                "redirect" : "", before);
            logger::info("BlackFlame: copy casting art: SetCastingArt call hooked at {}", site.note);
        }

        void AddArtCounters(CopySwapCounters& a_out) noexcept
        {
            a_out.castingArtCalls = g_calls.load();
            a_out.castingArtSwaps = g_swaps.load();
            a_out.castingArtRejected = g_rejected.load();
        }
    }
}
