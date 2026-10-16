#include "SelfTestReload.h"
#include "TrackedMutex.h"

#include "BuffState.h"
#include "Config.h"
#include "FormCopies.h"
#include "Forms.h"
#include "SelfTestLog.h"
#include "SelfTestProbe.h"
#include "SehGuard.h"
#include "SelfTestText.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>

namespace BlackFlame::SelfTest::Reload
{
    namespace
    {
        using namespace SelfTestText;

        constexpr float kMarkerForward = 300.0f;
        constexpr auto kBuffWait = std::chrono::seconds(3);
        constexpr auto kRuneWait = std::chrono::seconds(3);
        constexpr int kMaxRefScan = 4096;
        constexpr float kGameDaysSlack = 0.0001f;  // ~9 game seconds at the default timescale
        constexpr float kGameDaysWindow = 1.0f;     // W2: a save more than a game day after the request is not E3's
        constexpr auto kLoadWait = std::chrono::minutes(5);    // W3: a started load must finish by then
        constexpr auto kResumeWait = std::chrono::minutes(5);  // W3: the player must be ready by then
        constexpr const char* kMarkerFile = "HeartOfMagic_BlackFlame_e3.json";

        enum class State
        {
            kIdle,
            kWaiting,  // the player was asked to quicksave and quickload
            kResume    // a load E3 takes: judged once the player is ready
        };

        // Shared by the message handler and the ticker task: under g_mutex
        struct E3
        {
            State state = State::kIdle;
            RE::FormID rune = 0;      // the rune projectile reference
            RE::FormID runeBase = 0;  // its base: the copy
            RE::FormID cell = 0;
            float gameDays = 0.0f;
            std::uint64_t nonce = 0;
            Clock::time_point askedAt{};
            bool preloadSeen = false;
            Clock::time_point preloadAt{};
            Clock::time_point resumeAt{};
            Clock::duration readyPlay{};  // C1: unpaused play since the load, against config selfTestDelaySeconds
            std::optional<Clock::time_point> lastReadyCheck;
            RE::ObjectRefHandle runeHandle;  // valid only in the world E3 asked in
        };
        TrackedMutex g_mutex;
        E3 g_e3;

        // Step-local (game thread, reset at the step's first call)
        struct StepState
        {
            RE::SpellItem* rune = nullptr;
            RE::ObjectRefHandle marker;
            std::uint64_t mark = 0;
            Clock::time_point phaseStart{};
            bool active = false;  // W1: the step started and has not ended itself (timeout / exception ends it)
        };
        StepState g_step;

        enum Phase : int
        {
            kBuff = 0,
            kLaunch,
            kWaitRune
        };

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        std::optional<std::filesystem::path> MarkerPath()
        {
            const auto dir = logger::log_directory();
            if (!dir) {
                return std::nullopt;
            }
            return *dir / kMarkerFile;
        }

        bool WriteMarker(const E3& a_e3)
        {
            const auto path = MarkerPath();
            if (!path) {
                return false;
            }
            const nlohmann::json root{ { "rune", a_e3.rune }, { "runeBase", a_e3.runeBase }, { "cell", a_e3.cell },
                { "gameDays", a_e3.gameDays }, { "nonce", a_e3.nonce },
                { "written", std::chrono::system_clock::now().time_since_epoch().count() } };
            std::ofstream out(*path, std::ios::binary | std::ios::trunc);
            out << root.dump(2);
            out.close();
            return static_cast<bool>(out);
        }

        // The marker of this request (same nonce), or nothing
        bool MarkerMatches(std::uint64_t a_nonce)
        {
            const auto path = MarkerPath();
            std::ifstream in(path ? *path : std::filesystem::path{}, std::ios::binary);
            if (!in) {
                return false;
            }
            const auto root = nlohmann::json::parse(in, nullptr, false);
            return !root.is_discarded() && root.is_object() && root.contains("nonce") && root["nonce"].is_number_unsigned() &&
                   root["nonce"].get<std::uint64_t>() == a_nonce;
        }

        void DeleteMarker()
        {
            const auto path = MarkerPath();
            std::error_code ec;
            if (path && std::filesystem::exists(*path, ec) && !std::filesystem::remove(*path, ec)) {
                logger::warn("BlackFlame: self-test E3: could not delete {} ({}) - it is never reused (the nonce differs)",
                    path->string(), ec.message());
            }
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

        // By its FormID (it must still have the copy base), else a projectile of the copy base in the player's cell
        RE::Projectile* FindRune(RE::FormID a_rune, RE::FormID a_base, std::string& a_how)
        {
            if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_rune)) {
                auto* projectile = skyrim_cast<RE::Projectile*>(ref);
                if (projectile && projectile->GetBaseObject() && projectile->GetBaseObject()->GetFormID() == a_base) {
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
                    if (base && base->GetFormID() == a_base && (found = skyrim_cast<RE::Projectile*>(a_ref)) != nullptr) {
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            a_how = found ? "by its copy base in the player's cell" : "";
            return found;
        }

        // The cleanup, only in the world E3 ran in or in a save shown to be its own; the Stoke buff only when a_dispel
        // (E3 cast it here, or its rune was found by FormID in its own save)
        void CleanUp(RE::Projectile* a_rune, RE::ObjectRefHandle a_marker, bool a_dispel)
        {
            SelfTestLog::Info("E3", std::format("cleanup: {}{}{}",
                a_dispel ? std::format("dispelled {} Stoke effect(s)", SelfTestWorld::DispelStoke()) : std::string("buff left"),
                a_rune ? "; rune: " + SelfTestWorld::Remove(a_rune->GetHandle()) : std::string(),
                a_marker.get() ? "; marker: " + SelfTestWorld::Remove(a_marker) : std::string()));
        }

        // Appends a closing block to the self-test file (the run itself already wrote DONE)
        void Close(const std::string& a_infoOrEmpty, bool a_alreadyOpen = false)
        {
            if (!a_alreadyOpen) {
                SelfTestLog::Open(true);
            }
            if (!a_infoOrEmpty.empty()) {
                SelfTestLog::Info("E3", a_infoOrEmpty);
            }
            SelfTestLog::Info("E3-esl", "manual: with HeartOfMagic_BlackFlame_Copies.esp removed, a save holding a copy-based "
                "rune or hazard drops that reference on load (expected, as for any removed plugin); not automated (it needs "
                "the load order changed between two game starts)");
            SelfTestLog::Done("E3");
            DeleteMarker();
        }

        void Judge(const E3& a_e3)
        {
            SelfTestLog::Open(true);
            SelfTestLog::Info("E3", "resumed after the player's load");
            auto* cell = Player() ? Player()->GetParentCell() : nullptr;
            const float days = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f;
            const bool sameSave = MarkerMatches(a_e3.nonce) && cell && cell->GetFormID() == a_e3.cell &&
                                  days + kGameDaysSlack >= a_e3.gameDays && days <= a_e3.gameDays + kGameDaysWindow;
            if (!sameSave) {
                Close(std::format("E3 not run (manual step skipped): the loaded save is not one made in E3's cell within a "
                                  "game day after its request (marker {}, cell {:08X} vs {:08X}, game days {:.5f} vs {:.5f}); nothing "
                                  "was cleaned up here - a save made after the request still holds the Stoke buff and the copy rune",

                          MarkerMatches(a_e3.nonce) ? "ok" : "missing or of another request", cell ? cell->GetFormID() : 0,
                          a_e3.cell, days, a_e3.gameDays),
                    true);
                return;
            }
            std::string how;
            auto* rune = FindRune(a_e3.rune, a_e3.runeBase, how);
            if (!rune) {
                SelfTestLog::Fail("E3", std::format("after the reload: no rune {:08X} and no projectile of base {:08X} in the "
                    "player's cell (the save is E3's: same cell, later game time)", a_e3.rune, a_e3.runeBase));
            } else {
                SelfTestLog::Check("E3", RuneOk(rune), std::format("after the reload, found {}: {}", how, DescribeRune(rune)));
            }
            CleanUp(rune, {}, rune && how == "by its FormID");
            Close({}, true);
        }

        void Disarm()
        {
            std::lock_guard lock(g_mutex);
            g_e3 = {};
        }

        // A step path that ends E3 before the request: the cleanup in this world and a FAIL
        bool FailStep(const std::string& a_why)
        {
            SelfTestLog::Fail("E3", a_why);
            RE::Projectile* rune = nullptr;
            for (const auto& r : SelfTestProbe::LaunchesSince(g_step.mark)) {
                if (g_step.rune && r.spell == g_step.rune->GetFormID()) {
                    const auto ref = r.projectile.get();
                    rune = ref ? skyrim_cast<RE::Projectile*>(ref.get()) : rune;
                }
            }
            CleanUp(rune, g_step.marker, true);  // the world E3 ran in: it cast the buff
            g_step = {};
            return true;
        }
    }

    namespace
    {
        bool StepE3Body(Ctx& a_c, bool a_first);
    }

    bool StepE3(Ctx& a_c, bool a_first)
    {
        try {
            const bool done = StepE3Body(a_c, a_first);
            g_step.active = g_step.active && !done;
            return done;
        } catch (const std::exception& e) {
            return FailStep(std::format("the step threw: {}", e.what()));
        }
    }

    void OnRunFinished(bool a_forLoad)
    {
        // W1: E3 ended by its step timeout or an exception before it armed: clean up in this world
        if (!g_step.active || a_forLoad) {
            g_step = {};
            return;
        }
        (void)FailStep("ended before the request (timeout or exception): cleaned up");
    }

    namespace
    {
    bool StepE3Body(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            g_step = {};
            g_step.active = true;
            g_step.phaseStart = Clock::now();
            g_step.mark = SelfTestProbe::Mark();
            Disarm();
            std::string detail;
            const auto v2 = SelfTestWorld::LookupV2(detail);
            g_step.rune = v2.fireRune;
            const auto* effect = g_step.rune ? g_step.rune->GetAVEffect() : nullptr;
            if (!g_step.rune || !effect || !CopyOf(effect->data.projectileBase) || !v2.marker) {
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
                return since > kBuffWait ? FailStep("not run: the buff did not come on") : false;
            }
            a_c.phase = kLaunch;
            g_step.phaseStart = Clock::now();
            return false;
        case kLaunch:
            {
                std::string detail;
                const auto v2 = SelfTestWorld::LookupV2(detail);
                g_step.marker = SelfTestWorld::Spawn(v2.marker, kMarkerForward, 0.0f);
                auto* marker = RefOf(g_step.marker);
                g_step.mark = SelfTestProbe::Mark();
                if (!marker || !SelfTestWorld::LaunchAt(Player(), g_step.rune, marker, detail)) {
                    return FailStep("the rune could not be launched: " + detail);
                }
                SelfTestLog::Info("E3", detail);
                a_c.lastCast = Clock::now();
                a_c.phase = kWaitRune;
                g_step.phaseStart = Clock::now();
                return false;
            }
        default:
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
                    return since > kRuneWait ? FailStep("no live rune projectile from the player's launch (0 observed)") : false;
                }
                const bool ok = RuneOk(rune);
                SelfTestLog::Check("E3", ok, "before the save: " + DescribeRune(rune));
                (void)SelfTestWorld::Remove(g_step.marker);
                g_step.marker = {};
                if (!ok) {
                    CleanUp(rune, {}, true);
                    g_step = {};
                    return true;  // nothing worth reloading
                }
                auto* cell = Player()->GetParentCell();
                E3 armed;
                armed.state = State::kWaiting;
                armed.rune = rune->GetFormID();
                armed.runeBase = rune->GetBaseObject()->GetFormID();
                armed.cell = cell ? cell->GetFormID() : 0;
                armed.gameDays = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f;
                armed.nonce = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
                armed.askedAt = Clock::now();
                armed.runeHandle = rune->GetHandle();
                if (!WriteMarker(armed)) {
                    return FailStep("the E3 marker file could not be written next to the self-test file");
                }
                {
                    std::lock_guard lock(g_mutex);
                    g_e3 = armed;
                }
                SelfTestLog::Info("E3", std::format("E3: quicksave now, then quickload that save (within {} minutes, in this "
                    "cell; the test resumes after the load and ends with DONE E3; do not keep playing from that quicksave: it "
                    "holds the test rune and buff)", kE3Window.count()));
                g_step = {};
                return true;
            }
        }
    }
    }

    void OnPreLoadGame()
    {
        bool ended = false;
        {
            std::lock_guard lock(g_mutex);
            if (g_e3.state == State::kWaiting) {
                g_e3.preloadSeen = true;
                g_e3.preloadAt = Clock::now();
            } else if (g_e3.state == State::kResume) {
                g_e3 = {};  // W2: a newer load before the judgement: that load is not judged
                ended = true;
            }
        }
        if (ended) {
            Close("E3 not run (manual step skipped): another load started before the judgement");
        }
    }

    void OnNewGame()
    {
        bool was = false;
        {
            std::lock_guard lock(g_mutex);
            was = g_e3.state != State::kIdle;
            g_e3 = {};
        }
        if (was) {
            Close("E3 not run (manual step skipped): a new game started instead of the reload");
        }
    }

    bool OnPostLoadGame(bool a_success)
    {
        std::string skipped;
        {
            std::lock_guard lock(g_mutex);
            if (g_e3.state != State::kWaiting) {
                return false;
            }
            const bool inTime = Clock::now() - g_e3.askedAt <= kE3Window;
            if (a_success && g_e3.preloadSeen && inTime) {
                g_e3.state = State::kResume;
                g_e3.readyPlay = {};
                g_e3.lastReadyCheck.reset();
                g_e3.resumeAt = Clock::now();
                return true;
            }
            skipped = std::format("E3 not run (manual step skipped): load {}, its preload {}, {} the {} minute window",
                a_success ? "succeeded" : "FAILED", g_e3.preloadSeen ? "seen" : "not seen", inTime ? "within" : "after",
                kE3Window.count());
            g_e3 = {};
        }
        Close(skipped);  // the world E3 asked in is gone: nothing of it to clean
        return false;
    }

    void Tick()
    {
        E3 e3;
        {
            std::lock_guard lock(g_mutex);
            e3 = g_e3;
        }
        const auto now = Clock::now();
        if (e3.state == State::kWaiting) {
            if (e3.preloadSeen) {
                if (now - e3.preloadAt > kLoadWait) {  // W3: a load that never finished
                    Disarm();
                    Close(std::format("E3 not run (manual step skipped): a load started but did not finish within {} minutes",
                        kLoadWait.count()));
                }
                return;
            }
            if (now - e3.askedAt <= kE3Window) {
                return;
            }
            // The player never reloaded: cleaned up only while E3's world is the live one (player in its cell, the rune
            // still there); otherwise nothing is touched
            Disarm();
            auto* player = Player();
            auto* cell = player ? player->GetParentCell() : nullptr;
            const auto ref = e3.runeHandle.get();
            auto* rune = ref ? skyrim_cast<RE::Projectile*>(ref.get()) : nullptr;
            SelfTestLog::Open(true);
            if (rune && cell && cell->GetFormID() == e3.cell) {
                CleanUp(rune, {}, true);
            } else {
                SelfTestLog::Info("E3", "no cleanup: E3's cell or rune is not loaded any more (a Stoke buff the test cast may "
                    "still be on the player)");
            }
            Close(std::format("E3 not run (manual step skipped): no load within {} minutes", kE3Window.count()), true);
            return;
        }
        if (e3.state != State::kResume) {
            return;
        }
        const auto delay = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<float>(GetConfig().selfTestDelaySeconds));
        if (now - e3.resumeAt > kResumeWait + delay) {  // W3: the player never became ready after the load
            Disarm();
            Close(std::format("E3 not run (manual step skipped): the player was not ready within {} minutes of the load",
                kResumeWait.count()));
            return;
        }
        auto* player = Player();
        auto* ui = RE::UI::GetSingleton();
        constexpr auto kMaxGap = std::chrono::milliseconds(500);  // a longer gap between ticks is paused time
        const bool ready = player && ui && player->Is3DLoaded() && player->GetParentCell() && !ui->GameIsPaused();
        {
            std::lock_guard lock(g_mutex);
            const auto gap = g_e3.lastReadyCheck ? std::min<Clock::duration>(now - *g_e3.lastReadyCheck, kMaxGap) :
                                                   Clock::duration{};
            g_e3.lastReadyCheck = now;
            if (ready) {
                g_e3.readyPlay += gap;
            }
            if (!ready || g_e3.readyPlay < delay) {
                return;  // C1: the same unpaused-play delay as the run's start
            }
        }
        Disarm();
        try {
            Judge(e3);
        } catch (const std::exception& e) {
            SelfTestLog::Fail("E3", std::format("the judgement threw: {}", e.what()));
            // W1: the rune looked for once more (guarded) so the cleanup can remove it
            std::string how;
            RE::Projectile* rune = nullptr;
            (void)Guarded("E3 rune lookup after a throw", [&]() { rune = FindRune(e3.rune, e3.runeBase, how); });
            CleanUp(rune, {}, rune && how == "by its FormID");
            Close({}, true);
        }
    }

    bool Active()
    {
        std::lock_guard lock(g_mutex);
        return g_e3.state != State::kIdle;
    }

    void ShiftClocks(Clock::duration a_paused)
    {
        g_step.phaseStart += a_paused;
    }
}
