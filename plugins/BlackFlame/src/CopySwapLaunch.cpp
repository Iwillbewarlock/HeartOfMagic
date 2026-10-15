// =============================================================================
// COPY SWAP - the Launch and explosion creation detours (CopySwap.h, devices 1 and 2)
// =============================================================================

#include "CopySwap.h"

#include "Attribution.h"
#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "PlayerArt.h"
#include "SehGuard.h"
#include "SelfTestProbe.h"
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
        constexpr std::uint64_t kCreateSe = 42657, kCreateAe = 43829;

        // The explosion create data (1.7.104 code: built at 44216 +0x2a6..+0x940 and 34413 +0x130..+0x195; 44216, 34413
        // and 43829 have the same pdata size and unwind codes on 1.6.1170; SE unverified)
        constexpr std::size_t kCreateBase = 0x00;        // BGSExplosion*
        constexpr std::size_t kCreateProjectile = 0x20;  // the source Projectile* (44216), null for a self-area cast
        constexpr std::size_t kCreateSpell = 0x28;       // MagicItem*
        constexpr std::size_t kCreateCaster = 0x38;      // MagicCaster* (34413), null from 44216

        using LaunchFn = RE::ProjectileHandle*(RE::ProjectileHandle*, RE::Projectile::LaunchData&);
        using CreateFn = std::uintptr_t(void*);

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
            static std::mutex mutex;
            static auto* logged = std::make_unique<std::unordered_set<RE::FormID>>().release();
            {
                std::lock_guard lock(mutex);
                if (!logged->insert(a_form->GetFormID()).second) {
                    return;
                }
            }
            logger::info("BlackFlame: {} {:08X} of a player's fire launch has no grey copy - it stays orange (re-run "
                         "blackflame-greybuild --forms to add it)", a_what, a_form->GetFormID());
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
            RE::BGSProjectile* a_asked, RE::BGSProjectile* a_copy)
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
            const ThreadTrace::Scope trace("Launch (v2 detour)");
            g_launchCalls.fetch_add(1);
            RE::BGSProjectile* asked = a_data.projectileBase;
            RE::BGSProjectile* copy = nullptr;
            bool playerFire = false;
            try {
                (void)Guarded("copy launch", [&]() {
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
                ReportOnce(g_launchFailure, "the Launch detour");
            }
            if (copy) {
                a_data.projectileBase = copy;
                g_launchSwaps.fetch_add(1);
            }
            auto* result = reinterpret_cast<LaunchFn*>(g_launchOriginal)(a_result, a_data);
            if (copy) {
                a_data.projectileBase = asked;  // the caller's struct as it was
            }
            try {
                (void)Guarded("copy launch (after)", [&]() { PostLaunch(result, a_data, playerFire, asked, copy); });
            } catch (...) {
                ReportOnce(g_launchFailure, "the Launch detour (after)");
            }
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
            auto* base = Read<RE::TESForm*>(a_data, kCreateBase);
            if (!base || base->GetFormType() != RE::FormType::Explosion) {
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
            static const std::uintptr_t actorCasterVtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_ActorMagicCaster[0] }.address();
            if (Read<std::uintptr_t>(caster, 0) != actorCasterVtable) {
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

        std::uintptr_t CreateThunk(void* a_data)
        {
            const ThreadTrace::Scope trace("explosion create (v2 detour)");
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
                ReportOnce(g_createFailure, "the explosion creation detour");
            }
            auto** slot = static_cast<RE::BGSExplosion**>(a_data);
            if (swapTo) {
                *slot = swapTo;
            }
            const auto result = reinterpret_cast<CreateFn*>(g_createOriginal)(a_data);
            if (swapTo) {
                *slot = before;  // the caller's create data as it was
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
        void InstallLaunchDetours(CopySwapStatus& a_status)
        {
            const auto& config = GetConfig();
            if (config.copyLaunch || config.copyLaunchExplosion) {
                const auto id = Patch::IdFor(kLaunchSe, kLaunchAe);
                if (Patch::HasIds({ id }, "copy swap (Projectile::Launch)")) {
                    a_status.launch = Patch::PrologueDetour("Projectile::Launch (v2 copy swap)", Patch::AddressOf(id),
                        reinterpret_cast<std::uintptr_t>(&LaunchThunk), g_launchOriginal);
                }
                if (!a_status.launch.hooked) {
                    logger::warn("BlackFlame: copy swap: Projectile::Launch not detoured ({}); bytes {}", a_status.launch.note,
                        a_status.launch.bytes);
                }
            }
            // V6: SE off - the creation's signature and create data layout were read on 1.7.104 code and match
            // 1.6.1170 only by pdata/unwind; nothing of it is verified on 1.5.97. V14: no explosion copy, no detour.
            if (config.copyExplosionCreate && !REL::Module::IsAE()) {
                a_status.explosionCreate.note = "off on SE (signature and create data layout unverified there)";
                logger::info("BlackFlame: copy swap: explosion creation detour {}", a_status.explosionCreate.note);
            } else if (config.copyExplosionCreate && GetCopyCounts().explosions == 0) {
                a_status.explosionCreate.note = "no explosion copies";
            } else if (config.copyExplosionCreate) {
                const auto id = Patch::IdFor(kCreateSe, kCreateAe);
                if (Patch::HasIds({ id }, "copy swap (explosion creation)")) {
                    a_status.explosionCreate = Patch::PrologueDetour("explosion creation (v2 copy swap)",
                        Patch::AddressOf(id), reinterpret_cast<std::uintptr_t>(&CreateThunk), g_createOriginal);
                }
                if (!a_status.explosionCreate.hooked) {
                    logger::warn("BlackFlame: copy swap: explosion creation not detoured ({}); bytes {}",
                        a_status.explosionCreate.note, a_status.explosionCreate.bytes);
                }
            }
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
