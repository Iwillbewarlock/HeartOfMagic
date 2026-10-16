#include "Tester.h"

#include "Config.h"
#include "CopyFreshness.h"
#include "CopyRequest.h"
#include "CopySwap.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "MainThreadQueue.h"
#include "Patch.h"

#include <format>
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
            out += std::format("  map {} (version {}): {} entries; filled PROJ {}, EXPL {}, IPCT {}, HAZD {}, ARTO {}; left out {}\n",
                c.mapRead ? "read" : "NOT read", c.mapVersion, c.mapEntries, c.projectiles, c.explosions, c.impacts, c.hazards, c.arts, c.failed);
            out += std::format("  fire spells {} ({} cloak-associated), fire effects {}\n", fire.total, fire.cloakAssociated,
                fire.effects);
            out += std::format("  request: {} closure forms, {} excluded; fingerprint {}; request {}; dump {}; NIFs {} ({} failed) "
                               "in {} ms\n", request.forms, request.excluded, request.computed ? request.fingerprint : std::string("-"),
                request.requestWritten ? "written" : "not written", request.dumpState, request.nifs, request.nifsFailed,
                request.readMs);
            for (const auto& line : CheckCopies()) {
                if (!line.pass) {
                    out += "  copy problem: " + line.text + "\n";
                }
            }
            return out;
        }

        void Write()
        {
            const auto dir = SKSE::log::log_directory();
            if (!dir) {
                return;
            }
            const auto& cfg = GetConfig();
            std::string text = "Heart of Magic - Black Flame, tester file (send it back with HeartOfMagic_BlackFlame.log, "
                               "HeartOfMagic_BlackFlame_selftest.txt and a crash log if there is one)\n";
            text += std::format("runtime {}; testerMode {}, selfTest {}, writeRequest {}; main-thread queue {}\n", Runtime(),
                cfg.testerMode, cfg.selfTest, cfg.writeRequest, MainQueue::Installed() ? "installed" : "NOT installed");
            text += "hooks:\n" + Hooks();
            text += "copies:\n" + Copies();
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

    void WriteStartup()
    {
        try {
            Write();
        } catch (const std::exception& e) {
            logger::warn("BlackFlame: the tester file failed: {}", e.what());
        }
    }

    void NoteSelfTest(const std::string& a_doneLine)
    {
        {
            std::lock_guard lock(g_mutex);
            g_selfTest = a_doneLine;
        }
        WriteStartup();
    }
}
