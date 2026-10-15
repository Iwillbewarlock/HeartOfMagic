#include "SelfTestReload.h"

#include "BuffState.h"
#include "FormCopies.h"
#include "Forms.h"
#include "SelfTestLog.h"
#include "SelfTestProbe.h"
#include "SelfTestText.h"

#include <filesystem>
#include <format>

namespace BlackFlame::SelfTest::Reload
{
    namespace
    {
        using namespace SelfTestText;

        constexpr float kMarkerForward = 300.0f;
        constexpr auto kBuffWait = std::chrono::seconds(3);
        constexpr auto kRuneWait = std::chrono::seconds(3);
        constexpr auto kSaveWait = std::chrono::seconds(8);
        constexpr auto kReadyDelay = std::chrono::seconds(3);
        constexpr auto kArmedLimit = std::chrono::minutes(5);
        constexpr int kMaxRefScan = 4096;

        enum class State
        {
            kIdle,
            kLoadRequested,  // the run wrote the save; SelfTest::Tick loads it once the run has finished
            kLoading,        // Load called: the next kPreLoadGame / kPostLoadGame are ours
            kResume          // after the load: waiting for the player, then judging
        };

        enum Phase : int
        {
            kBuff = 0,
            kLaunch,
            kWaitRune,
            kSave,
            kWaitSave
        };

        // Game thread only; survives the load (plain statics, not the self-test context)
        struct ReloadState
        {
            State state = State::kIdle;
            RE::FormID rune = 0;       // the rune projectile reference
            RE::FormID runeBase = 0;   // its base: the copy
            Clock::time_point since{};
            std::optional<Clock::time_point> readySince;
        };
        ReloadState g_reload;

        // Step-local (reset at the step's first call)
        struct StepState
        {
            RE::SpellItem* rune = nullptr;
            const RE::BGSProjectile* copy = nullptr;
            RE::ObjectRefHandle marker;
            std::uint64_t mark = 0;
            Clock::time_point phaseStart{};
        };
        StepState g_step;

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        std::optional<std::filesystem::path> SaveFile()
        {
            const auto dir = logger::log_directory();  // ...\My Games\Skyrim Special Edition\SKSE
            if (!dir) {
                return std::nullopt;
            }
            return dir->parent_path() / "Saves" / (std::string(kSaveName) + ".ess");
        }

        void Next(int& a_phase, int a_to)
        {
            a_phase = a_to;
            g_step.phaseStart = Clock::now();
        }

        std::string DescribeRune(RE::Projectile* a_rune)
        {
            const auto* base = a_rune->GetBaseObject();
            const auto* explosion = a_rune->GetProjectileRuntimeData().explosion;
            return std::format("rune {:08X}: base {:08X} (copy {}), explosion {:08X} (copy {}), 3D {}, disabled {}, deleted {}",
                a_rune->GetFormID(), base ? base->GetFormID() : 0, IsCopy(base) ? "yes" : "NO",
                explosion ? explosion->GetFormID() : 0, IsCopy(explosion) ? "yes" : "NO", a_rune->Get3D() ? "yes" : "no",
                a_rune->IsDisabled(), a_rune->IsDeleted());
        }

        bool RuneOk(RE::Projectile* a_rune)
        {
            return a_rune && IsCopy(a_rune->GetBaseObject()) && IsCopy(a_rune->GetProjectileRuntimeData().explosion) &&
                   !a_rune->IsDeleted() && !a_rune->IsDisabled();
        }

        // The rune after the load: by its FormID first, else a projectile of the copy base in the player's cell
        RE::Projectile* FindRuneAfterLoad(std::string& a_how)
        {
            if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_reload.rune)) {
                if (auto* projectile = skyrim_cast<RE::Projectile*>(ref)) {
                    a_how = "by its FormID";
                    return projectile;
                }
            }
            auto* cell = Player() ? Player()->GetParentCell() : nullptr;
            RE::Projectile* found = nullptr;
            int scanned = 0;
            if (cell) {
                cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
                    if (++scanned > kMaxRefScan) {
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
                    if (base && base->GetFormID() == g_reload.runeBase) {
                        found = skyrim_cast<RE::Projectile*>(a_ref);
                        if (found) {
                            return RE::BSContainer::ForEachResult::kStop;
                        }
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            a_how = found ? "by its copy base in the player's cell (the FormID did not resolve)" : "";
            return found;
        }

        void Judge()
        {
            SelfTestLog::Open(true);
            SelfTestLog::Info("E3", std::format("resumed after loading {}", kSaveName));
            std::string how;
            auto* rune = FindRuneAfterLoad(how);
            if (!rune) {
                SelfTestLog::Fail("E3", std::format("after the reload: no rune {:08X} and no projectile of base {:08X} in the "
                    "player's cell", g_reload.rune, g_reload.runeBase));
            } else {
                SelfTestLog::Check("E3", RuneOk(rune), std::format("after the reload, found {}: {}", how, DescribeRune(rune)));
                const auto handle = rune->GetHandle();
                SelfTestLog::Info("E3", "rune: " + SelfTestWorld::Remove(handle));
            }
            SelfTestLog::Info("E3", std::format("dispelled {} Stoke effect(s)", SelfTestWorld::DispelStoke()));
            SelfTestLog::Info("E3-esl", "manual: with HeartOfMagic_BlackFlame_Copies.esp removed, a save holding a copy-based "
                "rune or hazard drops that reference on load (expected, as for any removed plugin) - not automated: it needs "
                "the load order changed between two game starts");
            SelfTestLog::Done("E3");
            g_reload = {};
        }
    }

    bool StepE3(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            g_step = {};
            g_step.phaseStart = Clock::now();
            std::string detail;
            const auto v2 = SelfTestWorld::LookupV2(detail);
            g_step.rune = v2.fireRune;
            const auto* effect = g_step.rune ? g_step.rune->GetAVEffect() : nullptr;
            g_step.copy = effect ? CopyOfAs(effect->data.projectileBase) : nullptr;
            if (!g_step.rune || !g_step.copy || !v2.marker) {
                SelfTestLog::Fail("E3", "not run: no Fire Rune, no copy of its projectile, or no marker: " + detail);
                return true;
            }
            if (!IsStoked()) {
                (void)SelfTestWorld::Cast(Player(), GetForms().stokeSpell, Player());
            }
            a_c.phase = kBuff;
        }
        const auto since = Clock::now() - g_step.phaseStart;
        switch (a_c.phase) {
        case kBuff:
            a_c.waitingFor = "the buff on for the rune";
            if (!IsStoked()) {
                if (since > kBuffWait) {
                    SelfTestLog::Fail("E3", "not run: the buff did not come on");
                    return true;
                }
                return false;
            }
            Next(a_c.phase, kLaunch);
            return false;
        case kLaunch:
            {
                std::string detail;
                const auto v2 = SelfTestWorld::LookupV2(detail);
                g_step.marker = SelfTestWorld::Spawn(v2.marker, kMarkerForward, 0.0f);
                auto* marker = RefOf(g_step.marker);
                g_step.mark = SelfTestProbe::Mark();
                if (!marker || !SelfTestWorld::LaunchAt(Player(), g_step.rune, marker, detail)) {
                    SelfTestLog::Fail("E3", "the rune could not be launched: " + detail);
                    (void)SelfTestWorld::Remove(g_step.marker);
                    return true;
                }
                SelfTestLog::Info("E3", detail);
                Next(a_c.phase, kWaitRune);
                return false;
            }
        case kWaitRune:
            {
                a_c.waitingFor = "the rune's launch record and its live projectile";
                RE::Projectile* rune = nullptr;
                for (const auto& r : SelfTestProbe::LaunchesSince(g_step.mark)) {
                    const auto ref = r.projectile.get();
                    if (r.spell == g_step.rune->GetFormID() && ref) {
                        rune = skyrim_cast<RE::Projectile*>(ref.get());
                    }
                }
                if (!rune || !rune->Get3D()) {
                    if (since > kRuneWait) {
                        SelfTestLog::Fail("E3", "no live rune projectile from the player's launch (0 observed)");
                        (void)SelfTestWorld::Remove(g_step.marker);
                        return true;
                    }
                    return false;
                }
                const bool ok = RuneOk(rune);
                SelfTestLog::Check("E3", ok, "before the save: " + DescribeRune(rune));
                g_reload.rune = rune->GetFormID();
                g_reload.runeBase = rune->GetBaseObject() ? rune->GetBaseObject()->GetFormID() : 0;
                (void)SelfTestWorld::Remove(g_step.marker);
                if (!ok) {
                    return true;  // nothing worth reloading
                }
                Next(a_c.phase, kSave);
                return false;
            }
        case kSave:
            {
                auto* manager = RE::BGSSaveLoadManager::GetSingleton();
                if (!manager) {
                    SelfTestLog::Fail("E3", "no BGSSaveLoadManager: no save, no reload");
                    return true;
                }
                std::error_code ec;
                if (const auto file = SaveFile()) {
                    std::filesystem::remove(*file, ec);  // the throwaway slot of an earlier run
                }
                manager->Save(kSaveName);
                SelfTestLog::Info("E3", std::format("saved to the throwaway slot {} (BGSSaveLoadManager::Save)", kSaveName));
                Next(a_c.phase, kWaitSave);
                return false;
            }
        default:
            {
                a_c.waitingFor = "the save file on disk";
                const auto file = SaveFile();
                std::error_code ec;
                const bool written = file && std::filesystem::exists(*file, ec) && std::filesystem::file_size(*file, ec) > 0;
                if (!written) {
                    if (since > kSaveWait) {
                        SelfTestLog::Fail("E3", std::format("the save {} did not appear within {} s - not reloaded",
                            file ? file->string() : std::string("(no SKSE log folder)"), kSaveWait.count()));
                        return true;
                    }
                    return false;
                }
                SelfTestLog::Info("E3", std::format("{} written; it is loaded once this run has finished, and the test resumes "
                    "after the load (results end with DONE E3)", file->string()));
                g_reload.state = State::kLoadRequested;
                g_reload.since = Clock::now();
                return true;
            }
        }
    }

    void LoadIfRequested()
    {
        if (g_reload.state != State::kLoadRequested) {
            return;
        }
        auto* manager = RE::BGSSaveLoadManager::GetSingleton();
        if (!manager) {
            logger::warn("BlackFlame: self-test E3: no BGSSaveLoadManager - the reload is not done");
            g_reload = {};
            return;
        }
        g_reload.state = State::kLoading;
        logger::info("BlackFlame: self-test E3: loading {}", kSaveName);
        FlushLog();
        manager->Load(kSaveName);
    }

    bool OnGameLoaded()
    {
        if (g_reload.state != State::kLoading) {
            return false;
        }
        g_reload.state = State::kResume;
        g_reload.since = Clock::now();
        g_reload.readySince.reset();
        logger::info("BlackFlame: self-test E3: the reload is ours - resuming when the player is ready");
        return true;
    }

    void Tick()
    {
        if (g_reload.state != State::kResume) {
            return;
        }
        const auto now = Clock::now();
        if (now - g_reload.since > kArmedLimit) {
            logger::warn("BlackFlame: self-test E3: the player was not ready within {} minutes - not judged",
                kArmedLimit.count());
            g_reload = {};
            return;
        }
        auto* player = Player();
        auto* ui = RE::UI::GetSingleton();
        if (!player || !ui || !player->Is3DLoaded() || !player->GetParentCell() || ui->GameIsPaused()) {
            g_reload.readySince.reset();
            return;
        }
        if (!g_reload.readySince) {
            g_reload.readySince = now;
        }
        if (now - *g_reload.readySince >= kReadyDelay) {
            Judge();
        }
    }

    bool Active() noexcept
    {
        return g_reload.state != State::kIdle;
    }

    void Cancel(const char* a_reason)
    {
        if (g_reload.state == State::kLoading) {
            return;  // our own load
        }
        if (g_reload.state != State::kIdle) {
            logger::info("BlackFlame: self-test E3 dropped ({})", a_reason);
        }
        g_reload = {};
    }
}
