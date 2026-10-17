#include "HandArtRefresh.h"

#include "BuffState.h"
#include "Config.h"
#include "CopySwap.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "GameThread.h"
#include "Patch.h"
#include "SehGuard.h"

#include <array>
#include <atomic>
#include <cstring>
#include <format>

namespace BlackFlame
{
    namespace
    {
        // 1.6.1170 code (decrypted image, 2026-10-17): 38784's unready branch is `mov dl, 1; mov rcx, rbx; call 34152`
        // (the call at +0xB1); its ready branch calls SetCastingArt at +0x91 (CopySwapArt.cpp)
        constexpr std::uint64_t kDetachAe = 34152;
        constexpr std::uintptr_t kScanWindow = 0x200;  // the same window as the SetCastingArt site's scan
        constexpr std::array<std::uint8_t, 5> kDetachArgs{ 0xB2, 0x01, 0x48, 0x8B, 0xCB };
        constexpr const char* kHookName = "hand art refresh (DetachCastingArt call in 38784)";
        constexpr std::array<int, 2> kHands{ RE::Actor::SlotTypes::kLeftHand, RE::Actor::SlotTypes::kRightHand };
        constexpr int kMaxRetryTicks = 50;  // ~5 s of unpaused ticks at the ticker's 100 ms
        constexpr int kMaxLines = 60;

        using DetachFn = void (*)(RE::ActorMagicCaster*, bool);
        using SetArtFn = void (*)(RE::ActorMagicCaster*, RE::BGSArtObject*);
        DetachFn g_detach = nullptr;   // set once at kDataLoaded, before g_installed
        SetArtFn g_setArt = nullptr;
        std::atomic<bool> g_installed{ false };
        std::string g_note = "not installed";  // written once at kDataLoaded, before any reader

        std::atomic<std::uint32_t> g_pending{ 0 };  // bit (1 << slot) per hand
        std::array<std::atomic<int>, 2> g_retries{};
        std::atomic<const char*> g_reason{ "" };
        std::atomic<std::uint64_t> g_detached{ 0 }, g_setOnly{ 0 }, g_gaveUp{ 0 };
        std::atomic<int> g_lines{ 0 };

        enum class Outcome
        {
            kDone,           // the art is the one wanted now (set here or already)
            kRetry,          // charging, casting or no 3D: next tick
            kNotApplicable,  // not drawn, not a fire spell, another art: the hooked SetCastingArt decides at the next ready
        };

        constexpr std::uint32_t Bit(int a_slot) noexcept
        {
            return 1u << static_cast<std::uint32_t>(a_slot);
        }

        std::uint32_t FormIdOf(const RE::TESForm* a_form) noexcept
        {
            return a_form ? a_form->GetFormID() : 0;
        }

        Outcome RefreshHand(RE::PlayerCharacter* a_player, int a_slot, std::string& a_line)
        {
            const auto* actorState = a_player->AsActorState();
            if (!actorState || actorState->GetWeaponState() != RE::WEAPON_STATE::kDrawn) {
                return Outcome::kNotApplicable;
            }
            auto* caster = skyrim_cast<RE::ActorMagicCaster*>(a_player->GetMagicCaster(static_cast<RE::MagicSystem::CastingSource>(a_slot)));
            auto* spell = a_player->GetActorRuntimeData().selectedSpells[a_slot];
            if (!caster || !spell || !IsFireSpell(spell)) {
                return Outcome::kNotApplicable;
            }
            auto* engineArt = CostliestCastingArtOf(spell);
            auto* current = caster->castingArt;
            if (!engineArt || !current || (current != engineArt && SourceOf(current) != engineArt)) {
                return Outcome::kNotApplicable;
            }
            auto* wanted = PickCastingArtImpl(a_player, a_slot, spell, engineArt, IsStoked());
            if (current == wanted) {
                return Outcome::kDone;
            }
            const auto castState = caster->state.get();
            if ((castState != RE::MagicCaster::State::kNone && castState != RE::MagicCaster::State::kReady) || !a_player->Get3D()) {
                a_line = std::format("hand {}, spell {:08X}, caster state {}, 3D {}", a_slot, spell->GetFormID(),
                    static_cast<std::uint32_t>(castState), a_player->Get3D() ? "loaded" : "none");
                return Outcome::kRetry;
            }
            const bool attached = caster->flags.all(RE::ActorMagicCaster::Flags::kCastingArtAttached);
            if (attached) {
                g_detach(caster, true);  // 38784's unready branch: light, attached art, clone task, enchant controller
                g_detached.fetch_add(1);
            } else {
                g_setOnly.fetch_add(1);  // its clone is still pending: CheckAttachCastingArt restarts it for the new art
            }
            g_setArt(caster, wanted);  // stores it with kStartCloneTask; Update's CheckAttachCastingArt attaches it
            a_line = std::format("hand {}, spell {:08X}, art {:08X} -> {:08X}, was attached {}, caster state {}", a_slot,
                spell->GetFormID(), FormIdOf(current), FormIdOf(wanted), attached ? "yes" : "no", static_cast<std::uint32_t>(castState));
            return Outcome::kDone;
        }

        void LogLine(bool a_warn, const std::string& a_text)
        {
            if (g_lines.load() >= kMaxLines || g_lines.fetch_add(1) >= kMaxLines) {
                return;
            }
            if (a_warn) {
                logger::warn("BlackFlame: hand art refresh ({}): {}", g_reason.load(), a_text);
            } else {
                logger::info("BlackFlame: hand art refresh ({}): {}", g_reason.load(), a_text);
            }
        }
    }

    void InstallHandArtRefresh(std::uintptr_t a_caller, std::uintptr_t a_setCastingArt)
    {
        if (!GetConfig().refreshCastingArt) {
            g_note = "refreshCastingArt false";
            return;
        }
        if (!REL::Module::IsAE() || !a_caller || !a_setCastingArt) {
            g_note = "AE with the SetCastingArt site only";
            return;
        }
        if (!Patch::HasIds({ kDetachAe }, "hand art refresh")) {
            g_note = "Address Library id missing";
            Patch::RecordHook(kHookName, false, 0, {}, g_note);
            return;
        }
        const auto detach = Patch::AddressOf(kDetachAe);
        std::vector<std::uintptr_t> found;
        for (const auto at : CopySwapDetail::CallsInWindow(a_caller, detach, kScanWindow)) {
            if (Patch::IsExecutable(at - kDetachArgs.size(), kDetachArgs.size()) &&
                std::memcmp(reinterpret_cast<const void*>(at - kDetachArgs.size()), kDetachArgs.data(), kDetachArgs.size()) == 0) {
                found.push_back(at);
            }
        }
        if (found.size() != 1) {
            g_note = std::format("{} call(s) to 34152 with mov dl,1; mov rcx,rbx before them in the caller's first 0x{:X} bytes "
                                 "(one expected) - off", found.size(), kScanWindow);
            logger::warn("BlackFlame: hand art refresh: {}", g_note);
            Patch::RecordHook(kHookName, false, a_caller, Patch::HexBytes(a_caller, 16), g_note);
            return;
        }
        g_detach = reinterpret_cast<DetachFn>(detach);
        g_setArt = reinterpret_cast<SetArtFn>(a_setCastingArt);
        g_note = std::format("34152 called from caller +0x{:X} (bytes from -5: {}); nothing written", found.front() - a_caller,
            Patch::HexBytes(found.front() - kDetachArgs.size(), kDetachArgs.size() + Patch::kRel32Size));
        Patch::RecordHook(kHookName, true, found.front(), Patch::HexBytes(found.front(), Patch::kRel32Size), g_note);
        g_installed.store(true);
        logger::info("BlackFlame: hand art refresh on: {}", g_note);
    }

    void RequestHandArtRefresh(const char* a_reason)
    {
        if (!g_installed.load()) {
            return;
        }
        for (auto& retries : g_retries) {
            retries.store(0);
        }
        g_reason.store(a_reason);
        g_pending.store(Bit(kHands[0]) | Bit(kHands[1]));
        TickHandArtRefresh();  // at once when on the main thread; else the next tick
    }

    void TickHandArtRefresh()
    {
        if (g_pending.load() == 0 || !g_installed.load() || !IsOnGameThread()) {
            return;  // only from the main-thread queue (after the frame's job wait)
        }
        auto* ui = RE::UI::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!ui || !player || ui->GameIsPaused()) {
            return;  // a paused game: Update does not attach anything; tried again, not counted
        }
        for (const auto slot : kHands) {
            if ((g_pending.load() & Bit(slot)) == 0) {
                continue;
            }
            std::string line;
            auto outcome = Outcome::kNotApplicable;
            if (!Guarded("hand art refresh", [&]() { outcome = RefreshHand(player, slot, line); })) {
                outcome = Outcome::kNotApplicable;  // the guard logged the fault and turned the step off
            }
            if (outcome == Outcome::kRetry) {
                if (g_retries[static_cast<std::size_t>(slot)].fetch_add(1) + 1 < kMaxRetryTicks) {
                    continue;
                }
                g_gaveUp.fetch_add(1);
                LogLine(true, std::format("gave up after {} ticks: {} (the next ready gives the right art)", kMaxRetryTicks, line));
            } else if (!line.empty()) {
                LogLine(false, line);
            }
            g_pending.fetch_and(~Bit(slot));
        }
    }

    bool HasPendingHandArtRefresh() noexcept
    {
        return g_pending.load() != 0;
    }

    void ResetHandArtRefresh()
    {
        g_pending.store(0);
        for (auto& retries : g_retries) {
            retries.store(0);
        }
    }

    HandArtRefreshStatus GetHandArtRefreshStatus()
    {
        return { g_installed.load(), g_note, g_detached.load(), g_setOnly.load(), g_gaveUp.load() };
    }
}
