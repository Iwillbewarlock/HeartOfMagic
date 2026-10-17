// =============================================================================
// Toxicology - dev self test: step 7's reload check
// =============================================================================
// The main test ends with a test value as the count toward the next bottle and
// writes kSelfTestStatePath:
//   {"phase": "awaitSave", "expected": V, "original": O}
// The next save while the count is V turns it into "awaitLoad" with the save's
// name. The next load (kPostLoadGame, after the co-save Load callback): the
// count must be V again. When the loaded save is the one noted (or a name is
// unknown) the check runs, puts O back and removes the file; a load of another
// save keeps waiting. A load with no save taken drops the check (the loaded
// save's own count stands).

#include "game/ToxSelfTest.h"
#include "game/ToxSelfTestState.h"

#include "Common.h"
#include "core/ToxCore.h"
#include "game/ToxMagicka.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace SelfTest = ToxGame::SelfTest;

namespace
{
    constexpr std::string_view kAwaitSave = "awaitSave";
    constexpr std::string_view kAwaitLoad = "awaitLoad";

    struct ReloadState
    {
        std::string phase;
        float expected = 0.0f;
        float original = 0.0f;
        std::string saveName;  // normalized (SaveKey); empty = unknown
    };

    std::string g_loadingName;  // the save named by the last kPreLoadGame (main thread)

    // The name SKSE passes, without folders and without a trailing ".ess" / ".skse", lowercased: the save and the
    // load may spell it differently. Other dots stay (a save name may hold them).
    std::string SaveKey(std::string_view a_name)
    {
        if (const auto slash = a_name.find_last_of("/\\"); slash != std::string_view::npos) a_name.remove_prefix(slash + 1);
        std::string out = Toxicology::LowerAscii(a_name);
        for (const std::string_view ext : { std::string_view(".ess"), std::string_view(".skse") }) {
            if (out.size() > ext.size() && out.ends_with(ext)) {
                out.resize(out.size() - ext.size());
                break;
            }
        }
        return out;
    }

    std::optional<ReloadState> Read()
    {
        std::ifstream file(ToxGame::kSelfTestStatePath, std::ios::binary);
        if (!file) return std::nullopt;
        std::stringstream text;
        text << file.rdbuf();
        const auto json = nlohmann::json::parse(text.str(), nullptr, false);
        if (json.is_discarded() || !json.is_object()) {
            logger::warn("SELFTEST WARN {} is not a JSON object - reload check dropped", ToxGame::kSelfTestStatePath);
            return ReloadState{};  // an unknown phase: the check drops it
        }
        ReloadState out;
        try {
            out.phase = json.value("phase", std::string{});
            out.expected = json.value("expected", 0.0f);
            out.original = json.value("original", 0.0f);
            out.saveName = json.value("saveName", std::string{});
        } catch (const nlohmann::json::exception& e) {
            logger::warn("SELFTEST WARN {}: {} - reload check dropped", ToxGame::kSelfTestStatePath, e.what());
            return ReloadState{};
        }
        return out;
    }

    bool Write(const ReloadState& a_state)
    {
        const nlohmann::json json{ { "phase", a_state.phase }, { "expected", a_state.expected },
            { "original", a_state.original }, { "saveName", a_state.saveName } };
        std::ofstream file(ToxGame::kSelfTestStatePath, std::ios::binary | std::ios::trunc);
        file << json.dump(4) << '\n';
        if (!file) {
            logger::warn("SELFTEST WARN could not write {} - no reload check", ToxGame::kSelfTestStatePath);
            return false;
        }
        return true;
    }

    void Remove()
    {
        std::error_code ec;
        std::filesystem::remove(ToxGame::kSelfTestStatePath, ec);
        if (ec) logger::warn("SELFTEST WARN could not remove {}: {}", ToxGame::kSelfTestStatePath, ec.message());
    }

    void Done(std::size_t a_pass, std::size_t a_fail, std::string_view a_note)
    {
        logger::info("SELFTEST DONE pass={} fail={} (reload check: {})", a_pass, a_fail, a_note);
    }
}

// =============================================================================
// ToxSelfTestState.h
// =============================================================================

bool SelfTest::ArmReloadCheck(float a_expected, float a_original)
{
    return Write({ std::string(kAwaitSave), a_expected, a_original, {} });
}

bool SelfTest::ReloadCheckPending()
{
    std::error_code ec;
    return std::filesystem::exists(ToxGame::kSelfTestStatePath, ec);
}

void SelfTest::NoteReloadLoading(std::string_view a_saveName)
{
    g_loadingName = SaveKey(a_saveName);
}

void SelfTest::NoteReloadSave(std::string_view a_saveName)
{
    auto state = Read();
    if (!state || (state->phase != kAwaitSave && state->phase != kAwaitLoad)) return;
    const float now = ToxGame::Accumulated();
    if (std::fabs(now - state->expected) >= kMagickaTolerance) {
        Info("7 reload", std::format("save '{}' holds the count {:.2f}, not the test's {:.2f} - not the save the check waits for",
            a_saveName, now, state->expected));
        return;
    }
    state->phase = kAwaitLoad;
    state->saveName = SaveKey(a_saveName);
    if (Write(*state)) {
        Info("7 reload", std::format("save '{}' holds the test count {:.2f}: load it to finish the check", a_saveName, now));
    }
    FlushLog();
}

void SelfTest::RunReloadCheck()
{
    const auto state = Read();
    if (!state) return;
    logger::info("SELFTEST start reload check (phase '{}', expected {:.2f}, save '{}', loading '{}')", state->phase,
        state->expected, state->saveName, g_loadingName);
    if (state->phase != kAwaitLoad) {
        Info("7 reload", std::format("no save was taken while the count was the test's {:.2f} - check dropped; this save's own "
            "count {:.2f} stands", state->expected, ToxGame::Accumulated()));
        Remove();
        Done(0, 0, "dropped");
        return;
    }
    const bool namesKnown = !state->saveName.empty() && !g_loadingName.empty();
    if (namesKnown && state->saveName != g_loadingName) {
        Info("7 reload", std::format("loaded '{}', not the test's save '{}' - still waiting for that one", g_loadingName, state->saveName));
        return;
    }
    const float loaded = ToxGame::Accumulated();
    std::size_t pass = 0;
    std::size_t fail = 0;
    const auto detail = std::format("count toward the next bottle after the load {:.2f}, saved {:.2f} (save names {})", loaded,
        state->expected, namesKnown ? "match" : "unknown");
    if (std::fabs(loaded - state->expected) < kMagickaTolerance) {
        ++pass;
        logger::info("SELFTEST PASS 7 reload: {}", detail);
    } else {
        ++fail;
        logger::warn("SELFTEST FAIL 7 reload: {}", detail);
    }
    ToxGame::SetAccumulated(state->original);
    Info("7 reload", std::format("count put back to the pre-test {:.2f}", state->original));
    Remove();
    Done(pass, fail, "done");
}
