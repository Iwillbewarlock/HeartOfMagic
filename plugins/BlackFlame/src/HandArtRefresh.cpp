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
        // 1.6.1170 code (decrypted image, 2026-10-17): 38784 keeps the caster (GetMagicCaster's result) in rbx; its ready
        // branch is `mov rcx, rbx; mov rdx, [rax+0xE8]; call 34150` (+0x91, CopySwapArt.cpp), its unready branch
        // `mov dl, 1; mov rcx, rbx; call 34152` (+0xB1). Neither 38784, DeselectSpellImpl (34158) nor 34152 reads the
        // caster's state (+0x30): the engine unreadies a hand in whatever state it is in.
        constexpr std::uint64_t kDetachAe = 34152;
        constexpr std::uintptr_t kScanWindow = 0x200;  // the same window as the SetCastingArt site's scan
        constexpr std::array<std::uint8_t, 5> kDetachArgs{ 0xB2, 0x01, 0x48, 0x8B, 0xCB };   // mov dl, 1; mov rcx, rbx
        constexpr std::array<std::uint8_t, 3> kCasterInRcx{ 0x48, 0x8B, 0xCB };              // mov rcx, rbx
        constexpr std::size_t kSiteArgBytes = 10;  // mov rcx, rbx (3) + mov rdx, [rax+0xE8] (7) before the +0x91 call
        constexpr const char* kHookName = "hand art refresh (DetachCastingArt call in 38784)";
        constexpr std::array<int, 2> kHands{ RE::Actor::SlotTypes::kLeftHand, RE::Actor::SlotTypes::kRightHand };
        constexpr int kMaxRetryTicks = 50;       // ~5 s of unpaused ticks at the ticker's 100 ms: drawing, no 3D
        constexpr int kMaxCastWaitTicks = 600;   // ~60 s: a concentration spell held, a charge kept
        constexpr int kMaxLines = 60;

        using DetachFn = void (*)(RE::ActorMagicCaster*, bool);
        using SetArtFn = void (*)(RE::ActorMagicCaster*, RE::BGSArtObject*);
        DetachFn g_detach = nullptr;   // set once at kDataLoaded, before g_installed
        SetArtFn g_setArt = nullptr;
        std::atomic<bool> g_installed{ false };
        std::string g_note = "not installed";  // written once at kDataLoaded, before any reader

        std::atomic<std::uint32_t> g_pending{ 0 };  // bit (1 << slot) per hand
        std::array<std::atomic<int>, 2> g_retries{};    // drawing, no 3D
        std::array<std::atomic<int>, 2> g_castWaits{};  // charging, casting
        std::atomic<const char*> g_reason{ "" };
        std::atomic<std::uint64_t> g_refreshed{ 0 }, g_wasPending{ 0 }, g_gaveUp{ 0 };
        std::atomic<int> g_lines{ 0 };

        enum class Outcome
        {
            kDone,           // the art is the one wanted now (set here or already)
            kRetry,          // the hands are being drawn, or the player has no 3D: next tick, kMaxRetryTicks
            kCastWait,       // caster state not kNone (ready, charging, casting): next tick, kMaxCastWaitTicks
            kNotApplicable,  // sheathed, not a fire spell, another art: the hooked SetCastingArt decides at the next ready
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
            const auto weapon = actorState ? actorState->GetWeaponState() : RE::WEAPON_STATE::kSheathed;
            if (weapon == RE::WEAPON_STATE::kWantToDraw || weapon == RE::WEAPON_STATE::kDrawing) {
                a_line = std::format("hand {}, the hands are being drawn (weapon state {})", a_slot, static_cast<std::uint32_t>(weapon));
                return Outcome::kRetry;  // the ready branch may run with the old art before the hands are drawn
            }
            if (weapon != RE::WEAPON_STATE::kDrawn) {
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
            // HB1: kNone only, until a log shows which state an idle drawn hand is in; kReady waits like a charge or a cast
            // (the engine's unready path reads no state, so kReady is likely safe, but that is not shown)
            if (castState != RE::MagicCaster::State::kNone) {
                a_line = std::format("hand {}, spell {:08X}, not idle (caster state {}: ready, charging or casting)", a_slot, spell->GetFormID(),
                    static_cast<std::uint32_t>(castState));
                return Outcome::kCastWait;
            }
            if (!a_player->Get3D()) {
                a_line = std::format("hand {}, spell {:08X}, the player has no 3D", a_slot, spell->GetFormID());
                return Outcome::kRetry;
            }
            const bool attached = caster->flags.all(RE::ActorMagicCaster::Flags::kCastingArtAttached);
            // 38784's unready branch, whatever the flags: with the art attached it releases the light and detaches the art;
            // always (dl = 1) it clears castingArtData and the art's graph, releases the enchantment controller and cancels
            // and releases the clone task (34152, 1.6.1170)
            g_detach(caster, true);
            g_setArt(caster, wanted);  // not attached now: stores it with kStartCloneTask; Update's CheckAttachCastingArt attaches it
            g_refreshed.fetch_add(1);
            if (!attached) {
                g_wasPending.fetch_add(1);
            }
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

        bool BytesAre(std::uintptr_t a_at, const std::uint8_t* a_bytes, std::size_t a_size)
        {
            return Patch::IsExecutable(a_at, a_size) && std::memcmp(reinterpret_cast<const void*>(a_at), a_bytes, a_size) == 0;
        }

        void Refuse(std::string a_note, std::uintptr_t a_at)
        {
            g_note = std::move(a_note);
            logger::warn("BlackFlame: hand art refresh: {}", g_note);
            Patch::RecordHook(kHookName, false, a_at, a_at ? Patch::HexBytes(a_at, 16) : std::string(), g_note);
        }
    }

    void InstallHandArtRefresh(std::uintptr_t a_caller, std::uintptr_t a_site, std::uintptr_t a_setCastingArt)
    {
        if (!GetConfig().refreshCastingArt) {
            g_note = "refreshCastingArt false";
            return;
        }
        if (!REL::Module::IsAE() || !a_caller || !a_site || !a_setCastingArt) {
            g_note = "AE with the SetCastingArt site only";
            return;
        }
        if (!Patch::IsCodeReadBuild()) {
            Refuse("only on 1.6.1170 / 1.7.104 (the builds whose code was read)", 0);
            return;
        }
        if (!Patch::HasIds({ kDetachAe }, "hand art refresh")) {
            Refuse("Address Library id missing", 0);
            return;
        }
        if (!BytesAre(a_site - kSiteArgBytes, kCasterInRcx.data(), kCasterInRcx.size())) {
            Refuse(std::format("the SetCastingArt call at caller +0x{:X} is not preceded by mov rcx, rbx (bytes from -10: {}) - off",
                a_site - a_caller, Patch::HexBytes(a_site - kSiteArgBytes, kSiteArgBytes)), a_site);
            return;
        }
        const auto detach = Patch::AddressOf(kDetachAe);
        std::vector<std::uintptr_t> found;
        for (const auto at : CopySwapDetail::CallsInWindow(a_caller, detach, kScanWindow)) {
            if (BytesAre(at - kDetachArgs.size(), kDetachArgs.data(), kDetachArgs.size())) {
                found.push_back(at);
            }
        }
        if (found.size() != 1) {
            Refuse(std::format("{} call(s) to 34152 with mov dl,1; mov rcx,rbx before them in the caller's first 0x{:X} bytes "
                               "(one expected) - off", found.size(), kScanWindow), a_caller);
            return;
        }
        g_detach = reinterpret_cast<DetachFn>(detach);
        g_setArt = reinterpret_cast<SetArtFn>(a_setCastingArt);
        g_note = std::format("34152 called from caller +0x{:X} (bytes from -5: {}), SetCastingArt at +0x{:X} after mov rcx, rbx; "
                             "nothing written", found.front() - a_caller,
            Patch::HexBytes(found.front() - kDetachArgs.size(), kDetachArgs.size() + Patch::kRel32Size), a_site - a_caller);
        Patch::RecordHook(kHookName, true, found.front(), Patch::HexBytes(found.front(), Patch::kRel32Size), g_note);
        g_installed.store(true);
        logger::info("BlackFlame: hand art refresh on: {}", g_note);
    }

    void RequestHandArtRefresh(const char* a_reason)
    {
        if (!g_installed.load()) {
            return;
        }
        for (std::size_t i = 0; i < kHands.size(); ++i) {
            g_retries[i].store(0);
            g_castWaits[i].store(0);
        }
        g_reason.store(a_reason);
        g_pending.store(Bit(kHands[0]) | Bit(kHands[1]));  // the tick that follows (BuffState.cpp Tick) tries them
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
            const auto hand = static_cast<std::size_t>(slot);
            if (outcome == Outcome::kRetry || outcome == Outcome::kCastWait) {
                auto& count = outcome == Outcome::kRetry ? g_retries[hand] : g_castWaits[hand];
                const int limit = outcome == Outcome::kRetry ? kMaxRetryTicks : kMaxCastWaitTicks;
                if (count.fetch_add(1) + 1 < limit) {
                    continue;
                }
                g_gaveUp.fetch_add(1);
                LogLine(true, std::format("gave up after {} ticks: {} (the next ready gives the right art)", limit, line));
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
        for (std::size_t i = 0; i < kHands.size(); ++i) {
            g_retries[i].store(0);
            g_castWaits[i].store(0);
        }
    }

    HandArtRefreshStatus GetHandArtRefreshStatus()
    {
        return { g_installed.load(), g_note, g_refreshed.load(), g_wasPending.load(), g_gaveUp.load() };
    }
}
