// =============================================================================
// COPY SWAP - the Launch and explosion creation detours (CopySwap.h, devices 1 and 2)
// =============================================================================

#include "CopySwap.h"
#include "Perf.h"
#include "TrackedMutex.h"

#include "Attribution.h"
#include "BuffState.h"
#include "Config.h"
#include "CopyFreshness.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "LogLimit.h"
#include "MainThreadQueue.h"
#include "PlayerArt.h"
#include "SehGuard.h"
#include "SelfTestProbe.h"
#include "SlotAssign.h"
#include "ThreadTrace.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace BlackFlame
{
    namespace
    {
        // Address Library ids (SE, AE): Projectile::Launch (CommonLib Projectile.cpp RELOCATION_ID(42928, 44108)) and the
        // explosion creation function that the projectile explode function (SE 43025 / AE 44216) and the self-area cast
        // (SE 33635 / AE 34413) call
        constexpr std::uint64_t kLaunchSe = 42928, kLaunchAe = 44108;
        constexpr std::uint64_t kCreateSeCandidate = 42657, kCreateAe = 43829;  // SE: an unverified match (F1)
        constexpr const char* kCreateName = "explosion creation (v2 copy swap)";
        constexpr std::size_t kSeCandidateBytes = 32;

        // The explosion create data (1.7.104 code: built at 44216 +0x2a6..+0x940 and 34413 +0x130..+0x195; 44216, 34413
        // and 43829 have the same pdata size and unwind codes on 1.6.1170; other builds and SE: every pointer checked)
        constexpr std::size_t kCreateBase = 0x00;        // BGSExplosion*
        constexpr std::size_t kCreateProjectile = 0x20;  // the source Projectile* (44216), null for a self-area cast
        constexpr std::size_t kCreateSpell = 0x28;       // MagicItem*
        constexpr std::size_t kCreateCaster = 0x38;      // MagicCaster* (34413), null from 44216

        using LaunchFn = RE::ProjectileHandle*(RE::ProjectileHandle*, RE::Projectile::LaunchData&);
        // All four argument registers are forwarded and rax returned: only the first argument (the create data) is read,
        // and an unverified signature (SE) cannot lose rdx, r8 or r9 on the way to the original
        using CreateFn = std::uintptr_t(void*, void*, void*, void*);

        // Vtable ids (SE, AE; CommonLib Offsets_VTABLE.h), checked and resolved at install, never in a thunk (a missing id
        // there would end the game at the first call instead of leaving the device off)
        constexpr std::uint64_t kExplosionVtableSe = 233467, kExplosionVtableAe = 189059;      // VTABLE_BGSExplosion[0]
        constexpr std::uint64_t kActorCasterVtableSe = 257613, kActorCasterVtableAe = 205828;  // VTABLE_ActorMagicCaster[0]
        std::uintptr_t g_explosionVtable = 0;
        std::uintptr_t g_actorCasterVtable = 0;

        std::uintptr_t g_launchOriginal = 0;  // written before the detour's jump (Patch::PrologueDetour)
        std::uintptr_t g_createOriginal = 0;

        std::atomic<std::uint64_t> g_launchCalls{ 0 }, g_launchPlayerFire{ 0 }, g_launchSwaps{ 0 }, g_launchNoCopy{ 0 },
            g_launchPostWrites{ 0 };
        std::atomic<std::uint64_t> g_createCalls{ 0 }, g_createAlreadyCopy{ 0 }, g_createSwapsProjectile{ 0 },
            g_createSwapsSelfArea{ 0 }, g_createRejected{ 0 }, g_createNotSpell{ 0 };
        std::atomic<bool> g_launchFailure{ false }, g_createFailure{ false };

        void ReportOnce(std::atomic<bool>& a_flag, const char* a_what)
        {
            if (!a_flag.exchange(true)) {
                logger::error("BlackFlame: {} threw; the original went on; further failures are not logged", a_what);
            }
        }

        // The fallback the user chose: a player's fire form without a copy stays orange, said once per form
        void LogNoCopyOnce(const RE::TESForm* a_form, const char* a_what)
        {
            static TrackedMutex mutex;
            static auto* logged = std::make_unique<std::unordered_set<RE::FormID>>().release();
            {
                std::lock_guard lock(mutex);
                if (!logged->insert(a_form->GetFormID()).second) {
                    return;
                }
            }
            const auto& fresh = GetFreshness();
            logger::info("BlackFlame: {} {:08X} of a player's fire launch has no grey copy - it stays orange (copies {}: {}; run "
                         "the Black Flame builder to add it)", a_what, a_form->GetFormID(), FreshnessName(fresh.state), fresh.detail);
        }

        // Trace (Fire Rune glyph): the player's fire launches while stoked, swapped or not, the first ones per source PROJ
        // (LogLimit::PerKey: a Flames concentration cast cannot use up the lines before a rune's launch shows)
        std::atomic<bool> g_launchTraceFull{ false }, g_launchTraceFailure{ false };

        // What PostLaunch read of the launched projectile, for the trace line (log only)
        struct LaunchExplosion
        {
            bool read = false;    // PostLaunch's guarded body got that far
            bool inHand = false;  // the result handle gave a Projectile
            RE::FormID before = 0, after = 0;
        };

        RE::FormID IdOf(const RE::TESForm* a_form) noexcept
        {
            return a_form ? a_form->GetFormID() : 0;
        }

        // Why the launch keeps its base (null: it launches a copy); LaunchThunk's checks in its order. a_stepRan: the
        // "copy launch" Guarded body ran to its end (false: the step is off after a fault, or faulted or threw now)
        const char* KeptWhy(bool a_stepRan, bool a_playerFire, const RE::BGSProjectile* a_asked, const RE::BGSProjectile* a_copy)
        {
            if (a_copy || (a_asked && IsCopy(a_asked))) {
                return nullptr;
            }
            if (!a_stepRan) {
                return "the copy launch step did not run (off after a fault, or it faulted or threw on this launch)";
            }
            if (!a_playerFire) {
                return "a fire launch not linked to the player (LinkToPlayer said no)";
            }
            if (!a_asked) {
                return "no projectile base";
            }
            if (!GetConfig().copyLaunch) {
                return "copyLaunch off";
            }
            if (IsDroppedByProbe(a_asked)) {
                return "its copy was dropped by the copy model probe";
            }
            return PoolCopyOf(a_asked) ? "its slot's gate is closed" : "no copy and no slot";
        }

        // Its own guard (a fault skips the line only) and no throw out: nothing of the launch depends on it. The player's
        // fire launches only (IsPlayerFireLaunch), and a stoked player's fire spell launch that LinkToPlayer refused
        void TraceLaunch(const RE::Projectile::LaunchData& a_data, bool a_stepRan, bool a_playerFire, const RE::BGSProjectile* a_asked,
            const RE::BGSProjectile* a_copy, bool a_made, const LaunchExplosion& a_runtime) noexcept
        {
            if (g_launchTraceFull.load()) {
                return;
            }
            try {
                (void)GuardedKeep("copy launch (trace)", [&]() {
                    if (!a_playerFire && !(a_data.spell && IsStoked() && a_data.shooter == RE::PlayerCharacter::GetSingleton() &&
                                             IsFireLaunch(a_data.spell, a_data.enchantItem, a_data.weaponSource))) {
                        return;
                    }
                    int line = 0, keyLine = 0;
                    if (!LogLimit::PerKey("launch trace", IdOf(a_asked), line, keyLine)) {
                        return;
                    }
                    if (line >= LogLimit::kTraceLines) {
                        // the kind's total cap reached: later launches return before PerKey's lock (one stopped by the
                        // per-key or key-count cap still takes it)
                        g_launchTraceFull.store(true);
                    }
                    const auto* chosen = a_copy ? a_copy : a_asked;
                    const char* kept = KeptWhy(a_stepRan, a_playerFire, a_asked, a_copy);
                    const auto* slot = kept && a_asked ? PoolCopyOf(a_asked) : nullptr;
                    const auto* explosion = chosen ? chosen->data.explosionType : nullptr;
                    // a base object in a TESObjectREFR* field (CopyGroupsPlan LinksOf): a TXST when the explosion places a decal
                    const auto* placed = explosion ? reinterpret_cast<const RE::TESForm*>(explosion->data.impactPlacedObject) : nullptr;
                    const std::string runtime = !a_runtime.read  ? std::string("not read (the after step faulted or is off)") :
                                                !a_runtime.inHand ? std::string("no projectile in hand") :
                                                                    std::format("{:08X} -> {:08X}", a_runtime.before, a_runtime.after);
                    logger::info("BlackFlame: launch trace {}/{} ({} of this source PROJ): spell {:08X}, source PROJ {:08X} (decal TXST "
                                 "{:08X}) -> {} PROJ {:08X}{}{}{}: decal TXST {}, explosion {:08X} (placed object {}), model '{}'; "
                                 "projectile made {}, its runtime explosion after the launch {}",
                        line, LogLimit::kTraceLines, keyLine, IdOf(a_data.spell), IdOf(a_asked),
                        a_asked ? IdOf(a_asked->data.decalData) : 0, chosen && IsCopy(chosen) ? "copy" : "SOURCE", IdOf(chosen),
                        kept ? " - not swapped: " : "", kept ? kept : "",
                        slot ? std::format(" (slot {:08X})", slot->GetFormID()) : std::string(),
                        CopySwapDetail::TxstText(chosen ? chosen->data.decalData : nullptr), IdOf(explosion),
                        CopySwapDetail::TxstText(placed), chosen ? chosen->model.c_str() : "", a_made ? "yes" : "NO", runtime);
                });
            } catch (...) {  // (...) as LaunchThunk's: noexcept, and SehGuard passes every non-fault throw on to here
                if (!g_launchTraceFailure.exchange(true)) {
                    logger::warn("BlackFlame: the launch trace threw; the line was dropped (the launch is unchanged); further "
                                 "failures are not logged");
                }
            }
        }

        bool IsPlayerFireLaunch(const RE::Projectile::LaunchData& a_data)
        {
            if (!IsStoked() || !IsFireLaunch(a_data.spell, a_data.enchantItem, a_data.weaponSource)) {
                return false;
            }
            auto* shooter = a_data.shooter;
            return LinkToPlayer("launch", nullptr, shooter, NonActorCasterOf(shooter)) != PlayerLink::kNone;
        }

        // After Launch: the new projectile's explosion to its copy (the MGEF explosion case, e.g. Fireball)
        void PostLaunch(RE::ProjectileHandle* a_result, const RE::Projectile::LaunchData& a_data, bool a_playerFire,
            RE::BGSProjectile* a_asked, RE::BGSProjectile* a_copy, LaunchExplosion& a_trace)
        {
            const auto projectile = a_result ? a_result->get() : RE::NiPointer<RE::Projectile>{};
            RE::BGSExplosion* before = nullptr;
            RE::BGSExplosion* after = nullptr;
            if (projectile) {
                auto& runtime = projectile->GetProjectileRuntimeData();
                before = runtime.explosion;
                if (a_playerFire && GetConfig().copyLaunchExplosion && before && !IsCopy(before)) {
                    if (auto* copy = CopyOfAs(before)) {
                        runtime.explosion = copy;
                        g_launchPostWrites.fetch_add(1);
                    } else {
                        LogNoCopyOnce(before, "explosion");
                    }
                }
                after = runtime.explosion;
            }
            a_trace = { true, projectile.get() != nullptr, before ? before->GetFormID() : 0, after ? after->GetFormID() : 0 };  // copies only
            if (!SelfTestProbe::Recording()) {
                return;
            }
            SelfTestProbe::LaunchRecord record;
            record.shooter = a_data.shooter ? a_data.shooter->GetFormID() : 0;
            record.spell = a_data.spell ? a_data.spell->GetFormID() : 0;
            record.base = a_asked ? a_asked->GetFormID() : 0;
            record.copyBase = a_copy ? a_copy->GetFormID() : 0;
            record.explosionBefore = before ? before->GetFormID() : 0;
            record.explosionAfter = after ? after->GetFormID() : 0;
            record.projectile = projectile ? projectile->GetHandle() : RE::ObjectRefHandle{};
            record.playerFire = a_playerFire;
            record.thread = REX::W32::GetCurrentThreadId();
            SelfTestProbe::RecordLaunch(std::move(record));
        }

        RE::ProjectileHandle* LaunchThunk(RE::ProjectileHandle* a_result, RE::Projectile::LaunchData& a_data)
        {
            auto perfStart = Perf::Now();  // perf audit 2: our parts, apart from the original Launch
            const ThreadTrace::Scope trace("Launch (v2 detour)");
            MainQueue::NoteJobSideHook();  // G1
            g_launchCalls.fetch_add(1);
            RE::BGSProjectile* asked = a_data.projectileBase;
            RE::BGSProjectile* copy = nullptr;
            bool playerFire = false;
            bool stepRan = false;  // the trace line only: false when the step is off after a fault, faulted or threw
            try {
                stepRan = Guarded("copy launch", [&]() {
                    playerFire = IsPlayerFireLaunch(a_data);
                    if (!playerFire || !asked || IsCopy(asked)) {
                        return;
                    }
                    g_launchPlayerFire.fetch_add(1);
                    if (!GetConfig().copyLaunch) {
                        return;  // the projectile swap is off: nothing is missing
                    }
                    copy = CopyOfAs(asked);
                    if (!copy) {
                        g_launchNoCopy.fetch_add(1);
                        LogNoCopyOnce(asked, "projectile");
                    }
                });
            } catch (...) {
                copy = nullptr;
                stepRan = false;
                ReportOnce(g_launchFailure, "the Launch detour");
            }
            if (copy) {
                a_data.projectileBase = copy;
                g_launchSwaps.fetch_add(1);
            }
            const auto perfBefore = Perf::Now() - perfStart;
            auto* result = reinterpret_cast<LaunchFn*>(g_launchOriginal)(a_result, a_data);
            perfStart = Perf::Now();
            if (copy) {
                a_data.projectileBase = asked;  // the caller's struct as it was
                // v2i: one line the first time each copy is launched (the swap itself is not logged per call); I9: nothing
                // may throw out of the detour
                try {
                    if (LogLimit::FirstTime("copy launch", copy->GetFormID())) {
                        logger::info("BlackFlame: first launch of copy projectile {:08X} for {:08X} (spell {:08X}, shooter {:08X}): "
                                     "projectile made {}", copy->GetFormID(), asked->GetFormID(),
                            a_data.spell ? a_data.spell->GetFormID() : 0, a_data.shooter ? a_data.shooter->GetFormID() : 0,
                            result && *result ? "yes" : "NO");
                    }
                } catch (...) {
                    ReportOnce(g_launchFailure, "the Launch detour (first-launch line)");
                }
            }
            LaunchExplosion runtime;  // log only: what PostLaunch read, for the trace line after it
            try {
                (void)Guarded("copy launch (after)", [&]() { PostLaunch(result, a_data, playerFire, asked, copy, runtime); });
            } catch (...) {
                ReportOnce(g_launchFailure, "the Launch detour (after)");
            }
            TraceLaunch(a_data, stepRan, playerFire, asked, copy, result && *result, runtime);
            Perf::Add(Perf::kLaunch, perfBefore + (Perf::Now() - perfStart));
            return result;
        }

        template <class T>
        T Read(const void* a_base, std::size_t a_offset)
        {
            T value{};
            std::memcpy(&value, static_cast<const std::uint8_t*>(a_base) + a_offset, sizeof(T));
            return value;
        }

        bool IsProjectileRef(const RE::TESForm* a_form)
        {
            const auto type = a_form->GetFormType();
            return type >= RE::FormType::ProjectileMissile && type <= RE::FormType::ProjectileBarrier;
        }

        // What the creation should get instead of its base, or null. Every pointer is checked before use.
        RE::BGSExplosion* DecideCreate(void* a_data, RE::BGSExplosion*& a_before, bool& a_selfArea, RE::FormID& a_projBase)
        {
            // The base must be a BGSExplosion by its vtable before anything else is read from it (another layout: rejected)
            auto* base = Read<RE::TESForm*>(a_data, kCreateBase);
            if (!base || Read<std::uintptr_t>(base, 0) != g_explosionVtable || base->GetFormType() != RE::FormType::Explosion) {
                g_createRejected.fetch_add(1);
                return nullptr;
            }
            a_before = static_cast<RE::BGSExplosion*>(base);
            if (IsCopy(base)) {
                g_createAlreadyCopy.fetch_add(1);
                return nullptr;
            }
            auto* copy = GetConfig().copyExplosionCreate ? CopyOfAs(a_before) : nullptr;
            auto* projectile = Read<RE::TESForm*>(a_data, kCreateProjectile);
            if (projectile) {
                if (!IsProjectileRef(projectile)) {
                    g_createRejected.fetch_add(1);
                    return nullptr;
                }
                const auto* projectileBase = static_cast<RE::TESObjectREFR*>(projectile)->GetBaseObject();
                a_projBase = projectileBase ? projectileBase->GetFormID() : 0;
                if (copy && IsCopy(projectileBase)) {
                    g_createSwapsProjectile.fetch_add(1);
                    return copy;
                }
                return nullptr;
            }
            // Self area (34413): the caster must be an ActorMagicCaster (vtable), the player's, while stoked
            auto* caster = Read<void*>(a_data, kCreateCaster);
            if (!copy || !caster || !IsStoked()) {
                return nullptr;
            }
            if (Read<std::uintptr_t>(caster, 0) != g_actorCasterVtable) {
                return nullptr;  // a NonActorMagicCaster or something else: not the self-area cast
            }
            auto* spell = Read<RE::TESForm*>(a_data, kCreateSpell);
            if (!spell) {
                g_createRejected.fetch_add(1);
                return nullptr;
            }
            if (spell->GetFormType() != RE::FormType::Spell) {
                g_createNotSpell.fetch_add(1);  // a scroll, enchantment, ingredient...: a valid layout, not a fire spell
                return nullptr;
            }
            const auto* actor = static_cast<RE::ActorMagicCaster*>(caster)->GetCasterAsActor();
            if (actor != RE::PlayerCharacter::GetSingleton() || !IsFireSpell(spell->As<RE::SpellItem>())) {
                return nullptr;
            }
            a_selfArea = true;
            g_createSwapsSelfArea.fetch_add(1);
            return copy;
        }

        std::uintptr_t CreateThunk(void* a_data, void* a_rdx, void* a_r8, void* a_r9)
        {
            const ThreadTrace::Scope trace("explosion create (v2 detour)");
            MainQueue::NoteJobSideHook();  // G1
            g_createCalls.fetch_add(1);
            RE::BGSExplosion* before = nullptr;
            RE::BGSExplosion* swapTo = nullptr;
            bool selfArea = false;
            RE::FormID projectileBase = 0;
            try {
                if (a_data) {
                    (void)Guarded("copy explosion create",
                        [&]() { swapTo = DecideCreate(a_data, before, selfArea, projectileBase); });
                }
            } catch (...) {
                swapTo = nullptr;
                before = nullptr;  // G10: nothing half read goes into the record
                ReportOnce(g_createFailure, "the explosion creation detour");
            }
            std::uintptr_t result = 0;
            {
                // F3: the caller's create data is put back however the original returns
                const CopySwapDetail::SlotSwap<RE::BGSExplosion> swap(static_cast<RE::BGSExplosion**>(a_data), swapTo);
                result = reinterpret_cast<CreateFn*>(g_createOriginal)(a_data, a_rdx, a_r8, a_r9);
            }
            try {
                if (before && SelfTestProbe::Recording()) {
                    (void)Guarded("copy explosion create (record)", [&]() {
                        SelfTestProbe::RecordExplosionCreate({ 0, before->GetFormID(),
                            swapTo ? swapTo->GetFormID() : before->GetFormID(), projectileBase, selfArea,
                            REX::W32::GetCurrentThreadId() });
                    });
                }
            } catch (...) {
                ReportOnce(g_createFailure, "the explosion creation record");
            }
            return result;
        }
    }

    bool IsFireLaunch(const RE::MagicItem* a_spell, const RE::EnchantmentItem*, const RE::TESObjectWEAP* a_weapon)
    {
        if (a_spell) {
            return IsFireSpell(a_spell);  // a spell decides alone: a non-fire spell sharing a copied PROJ is never swapped
        }
        return a_weapon && IsBoundFireWeapon(a_weapon);
    }

    namespace CopySwapDetail
    {
        std::string TxstText(const RE::TESForm* a_form)
        {
            if (!a_form) {
                return "00000000";
            }
            const auto slot = SlotAssign::SlotOf(a_form);
            const char* gate = !slot ? "not a slot form" : SlotAssign::GateOpen(*slot) ? "slot gate open" : "slot gate CLOSED";
            const auto* set = a_form->As<RE::BGSTextureSet>();
            if (!set) {
                return std::format("{:08X} (a {}, not a TXST; {})", a_form->GetFormID(), RE::FormTypeToString(a_form->GetFormType()),
                    gate);
            }
            const char* tx00 = set->textures[0].textureName.c_str();
            return std::format("{:08X} (TX00 '{}', {})", a_form->GetFormID(), tx00 ? tx00 : "", gate);
        }

        void InstallLaunchDetours(CopySwapStatus& a_status)
        {
            const auto& config = GetConfig();
            if (config.copyLaunch || config.copyLaunchExplosion) {
                const auto id = Patch::IdFor(kLaunchSe, kLaunchAe);
                if (Patch::HasIds({ id }, "copy swap (Projectile::Launch)")) {
                    a_status.launch = Patch::PrologueDetour("Projectile::Launch (v2 copy swap)", Patch::AddressOf(id),
                        reinterpret_cast<std::uintptr_t>(&LaunchThunk), g_launchOriginal);
                } else {
                    a_status.launch.note = "Address Library id missing";  // F7
                    Patch::RecordHook("Projectile::Launch (v2 copy swap)", false, 0, {}, a_status.launch.note);
                }
                if (!a_status.launch.hooked) {
                    logger::warn("BlackFlame: copy swap: Projectile::Launch not detoured ({}); bytes {}", a_status.launch.note,
                        a_status.launch.bytes);
                }
            }
            // Multi-runtime (2026-10-16): on every runtime whose prologue the replay decoder takes. The create data layout
            // was read on 1.7.104 (1.6.1170 unwind-equal); elsewhere it is checked per pointer at runtime (the base by its
            // BGSExplosion vtable, the projectile by its form type, the caster by the ActorMagicCaster vtable, the spell by its
            // form type), the thunk forwards all four argument registers, and a fault trips the SEH guard once and stops the
            // swap decision for the session (the detour stays and calls the original). V14: no explosion copy, no detour.
            const auto explosionVtableId = Patch::IdFor(kExplosionVtableSe, kExplosionVtableAe);
            const auto casterVtableId = Patch::IdFor(kActorCasterVtableSe, kActorCasterVtableAe);
            if (config.copyExplosionCreate && !REL::Module::IsAE()) {
                // F1: 42657 is an attempted match, not derived from SE code: off on SE (Launch's post-write still gives MGEF
                // explosions their copies there)
                a_status.explosionCreate.note = CopySwapDetail::kSeNotVerified;
                CopySwapDetail::RecordSeCandidate(kCreateName, kCreateSeCandidate);
            } else if (config.copyExplosionCreate && GetCopyCounts().explosions == 0) {
                a_status.explosionCreate.note = "no explosion copies";
            } else if (config.copyExplosionCreate && !Patch::HasIds({ explosionVtableId, casterVtableId }, "copy swap (explosion creation vtables)")) {
                a_status.explosionCreate.note = "a vtable id is missing";
                Patch::RecordHook(kCreateName, false, 0, {}, a_status.explosionCreate.note);
            } else if (config.copyExplosionCreate) {
                g_explosionVtable = Patch::AddressOf(explosionVtableId);
                g_actorCasterVtable = Patch::AddressOf(casterVtableId);
                if (Patch::HasIds({ kCreateAe }, "copy swap (explosion creation)")) {
                    a_status.explosionCreate = Patch::PrologueDetour(kCreateName, Patch::AddressOf(kCreateAe),
                        reinterpret_cast<std::uintptr_t>(&CreateThunk), g_createOriginal);
                } else {
                    a_status.explosionCreate.note = "Address Library id missing";  // F7
                    Patch::RecordHook(kCreateName, false, 0, {}, a_status.explosionCreate.note);
                }
                if (!a_status.explosionCreate.hooked) {
                    logger::warn("BlackFlame: copy swap: explosion creation not detoured ({}); bytes {}",
                        a_status.explosionCreate.note, a_status.explosionCreate.bytes);
                }
            }
        }

        void RecordSeCandidate(const char* a_name, std::uint64_t a_seId)
        {
            std::string bytes;
            std::uintptr_t address = 0;
            if (GetConfig().testerMode && Patch::HasIds({ a_seId }, "SE candidate (read only)")) {
                address = Patch::AddressOf(a_seId);
                bytes = Patch::HexBytes(address, kSeCandidateBytes);
            }
            Patch::RecordHook(a_name, false, address, std::move(bytes), std::format("{} (SE candidate id {}{})", kSeNotVerified, a_seId,
                address ? "; its first bytes recorded for an offline check" : ""));
            logger::info("BlackFlame: {}: off - {}", a_name, kSeNotVerified);
        }

        void AddLaunchCounters(CopySwapCounters& a_out) noexcept
        {
            a_out.launchCalls = g_launchCalls.load();
            a_out.launchPlayerFire = g_launchPlayerFire.load();
            a_out.launchSwaps = g_launchSwaps.load();
            a_out.launchNoCopy = g_launchNoCopy.load();
            a_out.launchPostWrites = g_launchPostWrites.load();
            a_out.createCalls = g_createCalls.load();
            a_out.createAlreadyCopy = g_createAlreadyCopy.load();
            a_out.createSwapsProjectile = g_createSwapsProjectile.load();
            a_out.createSwapsSelfArea = g_createSwapsSelfArea.load();
            a_out.createLayoutRejected = g_createRejected.load();
            a_out.createNotSpell = g_createNotSpell.load();
        }
    }
}
