#include "HitEffectHook.h"
#include "Perf.h"
#include "Patch.h"
#include "TrackedMutex.h"

#include "BuffState.h"
#include "CloneWatch.h"
#include "Config.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "Forms.h"
#include "HitArt.h"
#include "HitPathAttribution.h"
#include "HitPathObserve.h"
#include "LogLimit.h"
#include "SehGuard.h"
#include "SelfTestProbe.h"

#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace BlackFlame
{
    namespace
    {
        using Controller = RE::ActiveEffectReferenceEffectController;

        constexpr std::size_t kHitEffectArtIndex = 0x0C;     // RE/A/ActiveEffectReferenceEffectController.h
        constexpr std::size_t kHitEffectShaderIndex = 0x0D;  // same header
        enum Slot : std::size_t
        {
            kShaderSlot,
            kArtSlot,
            kSlotCount
        };

        enum Kind : std::size_t
        {
            kAnyEffect,
            kPlayerEffect,
            kKindCount
        };

        constexpr std::array<const char*, kSlotCount> kSlotNames{ "GetHitEffectShader", "GetHitEffectArt" };

        // S3 (hit shader research, 2026-10-16): slot 0x0D (AE 34846 / SE 34049) reads MGEF data.effectShader (or
        // hitVisuals +0x28, dual cast +0x40); it returns null when the active effect's flags (+0x7C) have bit 1 set
        // (written only by the cloak callback). Decrypted 1.6.1170 disassembly (2026-10-19): it is reached only through
        // the vtable, twice per ShaderReferenceEffect - ReferenceEffectController::Start (34761) and the
        // ShaderReferenceEffect constructor (34910) both `call [rax+68h]` (no direct call to 34846). So every counter
        // and line here is per call, not per hit: one shader hit is normally two 0x0D calls. The hook stays where it is.
        constexpr std::uint32_t kNoHitShaderBit = 1u << 1;
        constexpr int kMaxAnyLines = 20;     // lines per slot for casters other than the player
        constexpr int kMaxPlayerLines = 60;  // lines per slot for the player's effects, a cap of their own
        constexpr int kWindowStatsTicks = 300;  // periodic stats line while stoked: every 300 ticks of 100 ms (30 s)

        enum class Outcome
        {
            kSwapped,        // ours returned (shader) / remembered for the recolour (art)
            kCopied,         // art: the art's copy returned
            kNotPlayer,      // someone else cast it
            kEngineNull,     // the engine's answer was null
            kEngineNullFlag, // null with flags bit 1 set (the engine's "no hit shader" bit)
            kNotStoked,
            kNotFire,        // not a fire effect of a fire spell (IsBurnable)
            kNoBurnShader,
            kAlreadyOurs,
            kConfigOff,      // burnShader false (shader slot) / hitArt false (art slot: no later recolour)
            kOutcomeCount
        };

        const char* OutcomeText(Outcome a_outcome)
        {
            switch (a_outcome) {
            case Outcome::kSwapped:
                return "SWAPPED (ours)";
            case Outcome::kCopied:
                return "COPY returned";
            case Outcome::kNotPlayer:
                return "kept: not the player's";
            case Outcome::kEngineNull:
                return "kept: the engine answered null";
            case Outcome::kEngineNullFlag:
                return "kept: the engine answered null (flags bit 1)";
            case Outcome::kNotStoked:
                return "kept: not stoked";
            case Outcome::kNotFire:
                return "kept: not a fire effect of a fire spell";
            case Outcome::kNoBurnShader:
                return "kept: no burn shader form";
            case Outcome::kAlreadyOurs:
                return "kept: already ours";
            default:
                return "kept: the config key is off";
            }
        }

        struct SlotStats
        {
            std::atomic<std::uint64_t> playerCalls{ 0 }, swapped{ 0 }, copied{ 0 }, engineNull{ 0 }, engineNullFlag{ 0 },
                notStoked{ 0 }, notFire{ 0 }, otherRejected{ 0 };
        };

        std::array<std::array<std::atomic<int>, kKindCount>, kSlotCount> g_diagnostics{};
        std::array<SlotStats, kSlotCount> g_stats;  // since the game started (the self-test reads deltas of these)
        // Phase 0: every call by outcome (the other casters' included), zeroed when a stoke window starts; separate from
        // g_stats so the self-test's before/after reads never see a reset
        std::array<std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Outcome::kOutcomeCount)>, kSlotCount> g_window{};
        std::atomic<std::int64_t> g_windowStart{ 0 };  // steady_clock ticks at the window's start, 0: no window
        std::atomic<int> g_windowTicks{ 0 };           // ticks since the last stats line (TickHitEffectStats)
        std::atomic<bool> g_failureReported{ false };

        void ReportFailure()
        {
            if (!g_failureReported.exchange(true)) {
                logger::error("BlackFlame: hit effect hook threw; further failures are not logged");
            }
        }

        std::atomic<bool> g_noteFailureReported{ false };

        void ReportNoteFailure()
        {
            if (!g_noteFailureReported.exchange(true)) {
                logger::warn("BlackFlame: hit effect hook: the fire hit note threw; the shader was not changed by it; "
                             "further failures are not logged");
            }
        }

        // The player's reference handle, read once per loaded world (cleared by ResetHitEffectState)
        std::atomic<std::uint32_t> g_playerHandle{ 0 };
        std::atomic<bool> g_hitShaderProbePending{ true };  // log only: the next stoke window logs the probe (each load's first)

        std::uint32_t PlayerHandleValue()
        {
            auto value = g_playerHandle.load();
            if (value == 0) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    value = player->GetHandle().native_handle();
                    g_playerHandle.store(value);
                }
            }
            return value;
        }


        // How often the engine asks one controller: counted while the diagnostic lines last (bounded by them, and
        // cleared with their caps), so the log shows whether a call comes once per effect or again and again
        struct CallCounts
        {
            TrackedMutex mutex;
            std::unordered_map<const Controller*, int> counts;
        };

        CallCounts& GetCallCounts()
        {
            static auto* calls = std::make_unique<CallCounts>().release();  // never destroyed: hooks may run at exit
            return *calls;
        }

        int CallNumber(const Controller* a_this)
        {
            auto& calls = GetCallCounts();
            std::lock_guard lock(calls.mutex);
            return ++calls.counts[a_this];
        }

        // The diagnostic lines start counting again (their caps and the per-controller call numbers)
        void ResetDiagnosticLines() noexcept
        {
            for (auto& slot : g_diagnostics) {
                for (auto& counter : slot) {
                    counter.store(0);
                }
            }
            try {
                auto& calls = GetCallCounts();
                std::lock_guard lock(calls.mutex);
                calls.counts.clear();
            } catch (const std::exception& e) {
                // only the call numbers count on; the caps above are reset
                logger::warn("BlackFlame: hit diagnostics: the per-controller call numbers were not cleared ({})", e.what());
            }
        }

        // The first kMaxAnyLines calls of a slot whose caster is not the player and, with a cap of their own, the
        // first kMaxPlayerLines calls whose caster is the player - both only with something to show (the engine's
        // answer or the MGEF's own field not null): an always-on ability with no shader (FE2D6812 in the live log of
        // 2026-10-18) used the whole player cap at load. The calls skipped here are still counted (g_stats, g_window).
        void Diagnose(Slot a_slot, Controller* a_this, const RE::TESForm* a_original, Outcome a_outcome)
        {
            auto* effect = a_this->effect;
            const auto* base = effect ? effect->GetBaseObject() : nullptr;
            const RE::TESForm* field = !base ? nullptr :
                                       a_slot == kShaderSlot ? static_cast<const RE::TESForm*>(base->data.effectShader) :
                                                               static_cast<const RE::TESForm*>(base->data.hitEffectArt);
            if (!a_original && !field) {
                return;
            }
            const auto playerHandle = PlayerHandleValue();
            const bool player = effect && playerHandle != 0 && effect->caster.native_handle() == playerHandle;
            auto& counter = g_diagnostics[a_slot][player ? kPlayerEffect : kAnyEffect];
            const int cap = player ? kMaxPlayerLines : kMaxAnyLines;
            if (counter.load() >= cap) {
                return;
            }
            const int line = counter.fetch_add(1) + 1;
            if (line > cap) {
                return;
            }
            const auto target = a_this->target.get();
            const auto* ours = GetForms().burnShader;
            const auto flags = effect ? effect->flags.underlying() : 0u;
            logger::info("BlackFlame: {} [{} {}]: controller 0x{:X} (call {} for it), effect at 0x{:X} flags 0x{:08X} (bit1 {}), "
                         "MGEF {:08X} (its field {:08X}) spell {:08X}, target {:08X}, caster {}, stoked {}, burnShader {:08X} "
                         "at 0x{:X}, engine answer {:08X} -> {}, callerThread={}",
                kSlotNames[a_slot], player ? "player" : "any", line, reinterpret_cast<std::uintptr_t>(a_this),
                CallNumber(a_this), reinterpret_cast<std::uintptr_t>(effect), flags, (flags & kNoHitShaderBit) ? "set" : "clear",
                base ? base->GetFormID() : 0, field ? field->GetFormID() : 0,
                effect && effect->spell ? effect->spell->GetFormID() : 0, target ? target->GetFormID() : 0,
                player ? "player" : "other", IsStoked() ? "yes" : "no", ours ? ours->GetFormID() : 0,
                reinterpret_cast<std::uintptr_t>(ours), a_original ? a_original->GetFormID() : 0, OutcomeText(a_outcome),
                REX::W32::GetCurrentThreadId());
        }

        // The controller's target, or else the effect's own target (its MagicTarget's reference)
        RE::FormID TargetIdOf(Controller* a_this)
        {
            if (const auto target = a_this->target.get()) {
                return target->GetFormID();
            }
            auto* magicTarget = a_this->effect ? a_this->effect->target : nullptr;
            auto* ref = magicTarget ? magicTarget->GetTargetStatsObject() : nullptr;
            return ref ? ref->GetFormID() : 0;
        }

        // While the self-test records: every swap, and every fire effect left to the engine because
        // someone else cast it (the direct evidence for T5-npc)
        void Record(Controller* a_this, const RE::TESEffectShader* a_original, bool a_swapped)
        {
            auto* effect = a_this->effect;
            SelfTestProbe::RecordBurn({ 0, TargetIdOf(a_this), effect && effect->spell ? effect->spell->GetFormID() : 0,
                a_original->GetFormID(), REX::W32::GetCurrentThreadId(), reinterpret_cast<std::uintptr_t>(a_this),
                reinterpret_cast<std::uintptr_t>(effect), a_swapped });
        }

        void LogSwap(Controller* a_this, const RE::TESEffectShader* a_original)
        {
            auto* effect = a_this->effect;
            const auto spellId = effect && effect->spell ? effect->spell->GetFormID() : 0;
            const auto targetId = TargetIdOf(a_this);
            if (SelfTestProbe::Recording()) {
                Record(a_this, a_original, true);
            }
            int suppressed = 0;
            if (LogLimit::Allow("hit shader swap", spellId, suppressed)) {
                logger::info("BlackFlame: hit shader: spell {:08X} on {:08X}: engine shader {:08X} swapped for ours{}",
                    spellId, targetId, a_original->GetFormID(),
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }

        // Why a call was or was not swapped (cheap checks first: every hit shader and art of every actor passes here)
        Outcome Decide(Slot a_slot, Controller* a_this, const RE::TESForm* a_original)
        {
            auto* effect = a_this->effect;
            const auto playerHandle = PlayerHandleValue();
            // No player handle yet (or none for the effect): never the player's - a caster-less effect would match 0
            if (!effect || playerHandle == 0 || effect->caster.native_handle() != playerHandle) {
                return Outcome::kNotPlayer;
            }
            if (!a_original) {
                return a_slot == kShaderSlot && (effect->flags.underlying() & kNoHitShaderBit) ? Outcome::kEngineNullFlag :
                                                                                               Outcome::kEngineNull;
            }
            if (!IsStoked()) {
                return Outcome::kNotStoked;
            }
            if (!IsBurnable(effect->GetBaseObject(), effect->spell)) {
                return Outcome::kNotFire;
            }
            if (a_slot == kArtSlot) {
                // v2: the copy when one exists (its NIF is grey); the runtime recolour only for an art without one
                if (IsCopy(a_original)) {
                    return Outcome::kAlreadyOurs;
                }
                if (GetConfig().copyHitArt && CopyOf(a_original)) {
                    return Outcome::kCopied;
                }
                return GetConfig().hitArt ? Outcome::kSwapped : Outcome::kConfigOff;
            }
            const auto* ours = GetForms().burnShader;
            if (!ours) {
                return Outcome::kNoBurnShader;
            }
            if (a_original == ours) {
                return Outcome::kAlreadyOurs;
            }
            return GetConfig().burnShader ? Outcome::kSwapped : Outcome::kConfigOff;
        }

        void Count(Slot a_slot, Outcome a_outcome)
        {
            g_window[a_slot][static_cast<std::size_t>(a_outcome)].fetch_add(1, std::memory_order_relaxed);
            if (a_outcome == Outcome::kNotPlayer) {
                return;
            }
            auto& stats = g_stats[a_slot];
            stats.playerCalls.fetch_add(1);
            switch (a_outcome) {
            case Outcome::kSwapped:
                stats.swapped.fetch_add(1);
                break;
            case Outcome::kCopied:
                stats.copied.fetch_add(1);
                break;
            case Outcome::kEngineNullFlag:
                stats.engineNullFlag.fetch_add(1);
                stats.engineNull.fetch_add(1);
                break;
            case Outcome::kEngineNull:
                stats.engineNull.fetch_add(1);
                break;
            case Outcome::kNotStoked:
                stats.notStoked.fetch_add(1);
                break;
            case Outcome::kNotFire:
                stats.notFire.fetch_add(1);
                break;
            default:
                stats.otherRejected.fetch_add(1);
                break;
            }
        }

        // Phase 0: one line per slot with the window's calls by outcome (per call: 0x0D is asked twice per
        // ShaderReferenceEffect, see the S3 note at the top)
        void LogWindowStats(const char* a_when, const char* a_reason)
        {
            const auto start = g_windowStart.load();
            const auto elapsed = start == 0 ? std::chrono::steady_clock::duration::zero() :
                                              std::chrono::steady_clock::now().time_since_epoch() -
                                                  std::chrono::steady_clock::duration(start);
            const double seconds = std::chrono::duration<double>(elapsed).count();
            for (const Slot slot : { kShaderSlot, kArtSlot }) {
                // one load per counter, so the total and its parts agree while the hook keeps counting (review)
                std::uint64_t snapshot[std::size(g_window[0])]{};
                for (std::size_t i = 0; i < std::size(snapshot); ++i) {
                    snapshot[i] = g_window[slot][i].load();
                }
                const auto n = [&](Outcome a_outcome) { return snapshot[static_cast<std::size_t>(a_outcome)]; };
                const std::uint64_t engineNull = n(Outcome::kEngineNull) + n(Outcome::kEngineNullFlag);
                const std::uint64_t playerCalls = n(Outcome::kSwapped) + n(Outcome::kCopied) + engineNull +
                                                  n(Outcome::kNotStoked) + n(Outcome::kNotFire) + n(Outcome::kNoBurnShader) +
                                                  n(Outcome::kAlreadyOurs) + n(Outcome::kConfigOff);
                const bool shader = slot == kShaderSlot;
                logger::info("BlackFlame: hit {} stats (stoke window {} {:.1f} s, {}; per call{}): player calls {}: {} {}, "
                             "copied {}, engine null {} (flags bit 1: {}), not stoked {}, not fire {}, config off {}, "
                             "already ours {}, no burn shader {}; not the player's {}",
                    shader ? "shader 0x0D" : "art 0x0C", a_when, seconds, a_reason,
                    shader ? ", two per shader effect" : "", playerCalls, shader ? "swapped" : "kept for the recolour",
                    n(Outcome::kSwapped), n(Outcome::kCopied), engineNull, n(Outcome::kEngineNullFlag),
                    n(Outcome::kNotStoked), n(Outcome::kNotFire), n(Outcome::kConfigOff), n(Outcome::kAlreadyOurs),
                    n(Outcome::kNoBurnShader), n(Outcome::kNotPlayer));
            }
            HitPath::LogStats(a_when, seconds, a_reason);  // the 34094 / ApplyEffectShader observation (HitPathObserve.h)
        }

        RE::TESEffectShader* ChooseShader(Controller* a_this, RE::TESEffectShader* a_original)
        {
            const Outcome outcome = Decide(kShaderSlot, a_this, a_original);
            Count(kShaderSlot, outcome);
            Diagnose(kShaderSlot, a_this, a_original, outcome);
            if (outcome != Outcome::kSwapped) {
                if (a_original && SelfTestProbe::Recording() && a_this->effect &&
                    IsBurnable(a_this->effect->GetBaseObject(), a_this->effect->spell)) {
                    Record(a_this, a_original, false);
                }
                return a_original;
            }
            LogSwap(a_this, a_original);
            return GetForms().burnShader;
        }

        // The art to hand back: the copy (copyHitArt), else the engine's (remembered for the recolour under hitArt)
        RE::BGSArtObject* ChooseArt(Controller* a_this, RE::BGSArtObject* a_art)
        {
            const Outcome outcome = Decide(kArtSlot, a_this, a_art);
            Count(kArtSlot, outcome);
            Diagnose(kArtSlot, a_this, a_art, outcome);
            if (outcome == Outcome::kCopied) {
                auto* copy = CopyOfAs<RE::BGSArtObject>(a_art);
                // v2i: one line the first time each copy hit art is handed out
                if (copy && LogLimit::FirstTime("copy hit art", copy->GetFormID())) {
                    const auto* spell = a_this->effect ? a_this->effect->spell : nullptr;
                    logger::info("BlackFlame: first copy hit art {:08X} for {:08X} (spell {:08X})", copy->GetFormID(),
                        a_art->GetFormID(), spell ? spell->GetFormID() : 0);
                }
                // Cause B diagnostics (CloneWatch.h), I1: only for an effect on the player (its ActiveEffect's MagicTarget; the
                // controller's own target is not set yet at the first call), so an NPC hit by the player's spell takes no slot
                const auto* player = RE::PlayerCharacter::GetSingleton();
                if (player && a_this->effect && a_this->effect->target == player->AsMagicTarget()) {
                    ArmCloneWatch(copy);
                }
                return copy ? copy : a_art;  // CopyOf was not null in Decide; a copy dropped by the model probe is never returned
            }
            if (outcome == Outcome::kSwapped) {
                RememberHitArt(a_this, a_art);  // config hitArt: recolour once its 3D exists (HitArt.h)
            }
            return a_art;
        }

        struct HitEffectShaderHook
        {
            static RE::TESEffectShader* thunk(Controller* a_this)
            {
                auto* original = func(a_this);
                const Perf::Scope perf(Perf::kHitShader);  // perf audit 2: our part, after the original
                auto* result = original;
                // Entered from the engine's vtable: nothing may throw out of here, and a fault in our
                // part returns the engine's shader
                try {
                    if (a_this) {
                        (void)Guarded("hit effect shader", [&]() { result = ChooseShader(a_this, original); });
                    }
                } catch (...) {
                    result = original;
                    ReportFailure();
                }
                // log only, after the result is decided and in a guard of its own: a fault here skips this note only and
                // can never change the shader handed back or turn the swap step off; nothing may throw out either. Every
                // caster's fire effect, whatever the engine answered (a null answer counts too; HitPath::NoteFireEffect)
                try {
                    if (a_this) {
                        (void)GuardedKeep("hit effect shader (fire hit note)", [&]() { HitPath::NoteFireEffect(a_this->effect); });
                    }
                } catch (...) {
                    ReportNoteFailure();
                }
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct HitEffectArtHook
        {
            static RE::BGSArtObject* thunk(Controller* a_this)
            {
                auto* art = func(a_this);
                const Perf::Scope perf(Perf::kHitArt);  // perf audit 2: our part, after the original
                auto* result = art;
                // As the shader thunk: nothing may throw out of here, and a fault in our part returns the engine's art
                try {
                    if (a_this) {
                        (void)Guarded("hit effect art", [&]() { result = ChooseArt(a_this, art); });
                    }
                } catch (...) {
                    result = art;
                    ReportFailure();
                }
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    bool IsPlayerFireEffect(RE::ActiveEffect* a_effect)
    {
        const auto player = PlayerHandleValue();
        if (!a_effect || player == 0 || a_effect->caster.native_handle() != player) {
            return false;
        }
        return IsBurnable(a_effect->GetBaseObject(), a_effect->spell);
    }

    std::uint32_t PlayerHandleNative()
    {
        return PlayerHandleValue();
    }

    void ResetHitEffectState()
    {
        g_playerHandle.store(0);
        HitPath::ClearFireHits();  // log only: FF refs are reused across loads
        HitPath::Detail::ResetIdleResolves();  // log only: the corpse lines' resolve budget, each load and new game
        g_hitShaderProbePending.store(true);   // log only: the probe runs again at this load's first stoke window
    }

    void ResetHitDiagnostics() noexcept
    {
        ResetDiagnosticLines();
    }

    void OnStokeWindowChanged(bool a_on, const char* a_reason)
    {
        if (a_on) {
            // the line caps come back for the first windows of a session only: the lines are written synchronously
            // in the hook's threads, and a session of many stokes would otherwise add 160 lines a window (review)
            constexpr int kWindowCapResets = 3;
            static std::atomic<int> s_capResets{ 0 };
            const int window = s_capResets.fetch_add(1);
            const bool capsReset = window < kWindowCapResets;
            // log only: the player handle the corpse lines compare killers with, read here on the game thread
            (void)PlayerHandleValue();
            if (g_hitShaderProbePending.exchange(false)) {
                HitPath::LogHitShaderProbe("first stoke window of this load");
            }
            if (capsReset) {
                ResetDiagnosticLines();
            }
            for (auto& slot : g_window) {
                for (auto& counter : slot) {
                    counter.store(0);
                }
            }
            HitPath::ResetWindow(capsReset);
            g_windowTicks.store(0);
            g_windowStart.store(std::chrono::steady_clock::now().time_since_epoch().count());
            logger::info("BlackFlame: hit shader/art diagnostics: stoke window started ({}): window counts reset{}", a_reason,
                capsReset ? ", line caps reset" : ", line caps kept (only the first 3 windows of a session reset them)");
            return;
        }
        if (g_windowStart.load() == 0) {
            return;  // no window was open (a load before any buff)
        }
        LogWindowStats("ended after", a_reason);
        g_windowStart.store(0);
    }

    void TickHitEffectStats()
    {
        if (g_windowStart.load() == 0 || g_windowTicks.fetch_add(1) + 1 < kWindowStatsTicks) {
            return;
        }
        g_windowTicks.store(0);
        LogWindowStats("running for", "periodic");
    }

    HitSlotStats GetHitSlotStats(bool a_shader) noexcept
    {
        const auto& stats = g_stats[a_shader ? kShaderSlot : kArtSlot];
        HitSlotStats out;
        out.playerCalls = stats.playerCalls.load();
        out.swapped = stats.swapped.load();
        out.copied = stats.copied.load();
        out.engineNull = stats.engineNull.load();
        out.engineNullFlag = stats.engineNullFlag.load();
        out.notStoked = stats.notStoked.load();
        out.notFire = stats.notFire.load();
        out.otherRejected = stats.otherRejected.load();
        return out;
    }

    void InstallHitEffectHooks()
    {
        // Both slots are hooked whatever the config says (S3: the diagnostics tell whether the engine asks, with
        // what, and why a call was kept); burnShader decides whether the shader call is swapped, hitArt only whether the
        // returned art's 3D is recoloured later (the art call always returns the engine's art)
        const auto& config = GetConfig();
        REL::Relocation<std::uintptr_t> vtbl{ Controller::VTABLE[0] };
        HitEffectShaderHook::func = Patch::WriteVfunc("ActiveEffectReferenceEffectController HitEffectShaderHook", vtbl, kHitEffectShaderIndex, HitEffectShaderHook::thunk);
        HitEffectArtHook::func = Patch::WriteVfunc("ActiveEffectReferenceEffectController HitEffectArtHook", vtbl, kHitEffectArtIndex, HitEffectArtHook::thunk);
        logger::info("BlackFlame: hooked the ActiveEffectReferenceEffectController vtable: GetHitEffectShader (0x{:X}) swap {}, "
                     "GetHitEffectArt (0x{:X}) copy {} ({} art copies), recolour {}", kHitEffectShaderIndex,
            config.burnShader ? "on" : "off (burnShader false)", kHitEffectArtIndex, config.copyHitArt ? "on" : "off (copyHitArt false)",
            GetCopyCounts().arts, config.hitArt ? "on" : "off (hitArt false)");
    }
}
