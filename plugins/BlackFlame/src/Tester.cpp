#include "Tester.h"

#include "Config.h"
#include "CopyFreshness.h"
#include "CopyRequest.h"
#include "CopySwap.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "MainThreadQueue.h"
#include "PapyrusWrap.h"
#include "Patch.h"

#include <chrono>
#include <format>
#include <string_view>
#include <fstream>
#include <mutex>

namespace BlackFlame::Tester
{
    namespace
    {
        constexpr const char* kFileName = "HeartOfMagic_BlackFlame_tester.txt";

        std::mutex g_mutex;
        std::string g_selfTest = "not run in this session yet (selfTest or testerMode on, then load a save and wait)";

        std::string Runtime()
        {
            const auto version = REL::Module::get().version();
            return std::format("{} ({})", version.string(), REL::Module::IsAE() ? "AE" : "SE");
        }

        std::string Hooks()
        {
            const auto base = REL::Module::get().base();
            std::string out;
            for (const auto& hook : Patch::HookRecords()) {
                const auto where = hook.address == 0 ? std::string("-") :
                                   hook.address >= base ? std::format("+{:X}", hook.address - base) :
                                                          std::format("{:X} (outside the image)", hook.address);
                out += std::format("  {:8} {} at {}; {}{}\n", hook.installed ? "INSTALLED" : "SKIPPED", hook.name, where, hook.note,
                    hook.bytes.empty() ? std::string() : "; bytes " + hook.bytes);
            }
            return out.empty() ? std::string("  (none recorded)\n") : out;
        }

        std::string Copies()
        {
            const auto c = GetCopyCounts();
            const auto& request = GetCopyRequestState();
            const auto fire = GetFireSetCounts();
            const auto& fresh = GetFreshness();
            std::string out = std::format("  freshness {}: {}\n", FreshnessName(fresh.state), fresh.detail);
            out += std::format("  map {} (version {}): {} entries; filled PROJ {}, EXPL {}, IPCT {}, HAZD {}, ARTO {}; left out {} ({} source mesh absent)\n",
                c.mapRead ? "read" : "NOT read", c.mapVersion, c.mapEntries, c.projectiles, c.explosions, c.impacts, c.hazards, c.arts, c.failed,
                c.sourceAbsent);
            out += CopyModelProbeDone() ?  // v2i (CopyModelProbe; I12: a startup write comes before the probe)
                       std::format("  copy NIF failed to load in game: {}\n", CopiesFailedToLoad().size()) :
                       std::string("  copy NIF failed to load in game: not probed yet (before the probe, or copyModelProbe false)\n");
            out += std::format("  fire spells {} ({} cloak-associated), fire effects {}\n", fire.total, fire.cloakAssociated,
                fire.effects);
            out += std::format("  request: {} closure forms, {} excluded; fingerprint {}; request {}; dump {}; NIFs {} ({} failed) "
                               "in {} ms ({} form or muzzle NIFs, {} absent from the load order, {} ADDN-only)\n", request.forms, request.excluded, request.computed ? request.fingerprint : std::string("-"),
                request.requestWritten ? "written" : "not written", request.dumpState, request.nifs, request.nifsFailed,
                request.readMs, request.nifsFailedForms, request.nifsAbsentForms, request.nifsFailedAddons);
            for (const auto& line : CheckCopies()) {
                if (!line.pass) {
                    out += (line.info ? "  copy note: " : "  copy problem: ") + line.text + "\n";
                }
            }
            return out;
        }

        void WriteFile(const char* a_when)
        {
            const auto dir = SKSE::log::log_directory();
            if (!dir) {
                return;
            }
            const auto& cfg = GetConfig();
            std::string text = "Heart of Magic - Black Flame, tester file (send it back with HeartOfMagic_BlackFlame.log, "
                               "HeartOfMagic_BlackFlame_selftest.txt and a crash log if there is one)\n";
            text += std::format("written {} UTC at {}\n", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
                a_when);
            text += std::format("runtime {}; testerMode {}, selfTest {}, writeRequest {}; main-thread queue {}\n", Runtime(),
                cfg.testerMode, cfg.selfTest, cfg.writeRequest, MainQueue::Installed() ? "installed" : "NOT installed");
            text += "hooks:\n" + Hooks();
            text += "copies:\n" + Copies();
            text += "counters: " + CounterSummary() + "\n";
            if (std::string_view(a_when) == "kDataLoaded") {
                text += "  (native wrapper state at kDataLoaded; the wrappers may bind only at the first save load)\n";
            }
            {
                std::lock_guard lock(g_mutex);
                text += "self-test: " + g_selfTest + "\n";
            }
            std::ofstream out(*dir / kFileName, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out) {
                logger::warn("BlackFlame: the tester file could not be written");
            }
        }
    }

    std::string CounterSummary()
    {
        const auto c = GetCopySwapCounters();
        std::string wrappers;
        for (const auto& w : CheckNativeWrappers()) {
            wrappers += std::format("{}{}.{} {}{}", wrappers.empty() ? "" : ", ", w.className, w.functionName,
                w.bound ? "bound" : "NOT bound", w.bound && !w.matches ? " (signature differs)" : "");
        }
        return std::format("Launch {} calls / {} swaps / {} without a copy; explosion creation {} calls / {} already copies / {} "
                           "swapped (projectile) / {} swapped (self area) / {} rejected; hazard creation {} calls / {} swapped "
                           "(window) / {} swapped (spawn) / {} rejected; spawn window {} calls / {} not a SpawnHazardEffect; "
                           "SetCastingArt {} calls / {} copies / {} rejected; PlaceAtMe copies {}; native wrappers: {}",
            c.launchCalls, c.launchSwaps, c.launchNoCopy, c.createCalls, c.createAlreadyCopy, c.createSwapsProjectile,
            c.createSwapsSelfArea, c.createLayoutRejected, c.hazardCreateCalls, c.hazardSwapsWindow, c.hazardSwapsSpawn,
            c.hazardCreateRejected, c.spawnWindowCalls, c.spawnWindowOther, c.castingArtCalls, c.castingArtSwaps,
            c.castingArtRejected, PlacedCopySwaps(), wrappers.empty() ? std::string("none") : wrappers);
    }

    void Write(const char* a_when)
    {
        try {
            WriteFile(a_when);
        } catch (const std::exception& e) {
            logger::warn("BlackFlame: the tester file failed: {}", e.what());
        }
    }

    void WriteStartup()
    {
        Write("kDataLoaded");
    }

    void NoteSelfTest(const std::string& a_doneLine)
    {
        {
            std::lock_guard lock(g_mutex);
            // The main run and E3's reload part each end with a DONE line: both are kept
            g_selfTest = g_selfTest.starts_with("not run") ? a_doneLine : g_selfTest + "\n  " + a_doneLine;
        }
        Write("self-test end");
    }
}
