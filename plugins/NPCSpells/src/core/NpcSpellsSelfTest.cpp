#include "core/NpcSpellsSelfTest.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <format>
#include <iterator>
namespace NpcSpells {
namespace {
    void AppendError(std::string& error, const std::string& part) { if (!error.empty()) error += "; "; error += part; }
    // A number outside 0..kSelfTestMaxMs, or not an integer, goes back to its default with an error
    void ReadMs(const nlohmann::json& j, const char* key, int& out, int fallback, std::string& error) {
        const auto it = j.find(key);
        if (it == j.end()) return;
        if (!it->is_number_integer() || it->get<long long>() < 0 || it->get<long long>() > kSelfTestMaxMs) {
            AppendError(error, std::format("{} {} is not a whole number of ms in 0..{}, the default {} is used", key, it->dump(), kSelfTestMaxMs, fallback));
            out = fallback;
            return;
        }
        out = static_cast<int>(it->get<long long>());
    }
}
bool ParseSelfTestConfig(const std::string& jsonText, SelfTestConfig& out, std::string& error)
{
    out = SelfTestConfig{};
    const auto j = nlohmann::json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { error = "self test JSON is not an object"; return false; }
    const auto samples = j.find("samples");
    if (samples == j.end() || !samples->is_array()) { error = "self test JSON has no \"samples\" array"; return false; }
    for (const auto& v : *samples) {
        // A string, or {"id": "...", "repeat": N}
        const nlohmann::json* idValue = &v;
        if (v.is_object()) { const auto id = v.find("id"); idValue = id != v.end() ? &*id : nullptr; }
        if (!idValue || !idValue->is_string()) { AppendError(error, std::format("samples: not a FormID (XXXXXX:Plugin.ext) or {{\"id\": ..., \"repeat\": N}}: {}", v.dump())); continue; }
        std::string refError;
        const auto& text = idValue->get_ref<const std::string&>();
        auto ref = ParseFormRef(text, refError);
        if (!ref) { AppendError(error, "samples: " + refError); continue; }
        int repeat = kSelfTestDefaultRepeat;
        if (const auto r = v.is_object() ? v.find("repeat") : v.end(); r != v.end()) {
            if (r->is_number_integer() && r->get<long long>() >= 1 && r->get<long long>() <= kSelfTestMaxRepeat) repeat = static_cast<int>(r->get<long long>());
            else AppendError(error, std::format("samples: {} repeat {} is not a whole number in 1..{}, {} is used", text, r->dump(), kSelfTestMaxRepeat, kSelfTestDefaultRepeat));
        }
        out.samples.push_back(*ref); out.sampleTexts.push_back(text); out.repeats.push_back(repeat);
    }
    ReadMs(j, "startDelayMs", out.startDelayMs, kSelfTestDefaultStartDelayMs, error);
    ReadMs(j, "maxWaitMs", out.maxWaitMs, kSelfTestDefaultMaxWaitMs, error);
    if (out.samples.empty()) { AppendError(error, "no usable sample"); return false; }
    return true;
}
std::vector<std::uint32_t> WitnessSpells(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& excluded)
{
    std::vector<std::uint32_t> out;
    std::unordered_set<std::uint32_t> seen;
    for (const auto id : listSpells)
        if (id != 0 && !excluded.contains(id) && seen.insert(id).second) out.push_back(id);
    return out;
}
std::size_t CountExcluded(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& excluded)
{
    std::unordered_set<std::uint32_t> seen;
    std::size_t n = 0;
    for (const auto id : listSpells)
        if (id != 0 && seen.insert(id).second && excluded.contains(id)) ++n;
    return n;
}
std::vector<std::uint32_t> PickedFrom(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& cache)
{
    std::vector<std::uint32_t> out;
    std::unordered_set<std::uint32_t> seen;
    for (const auto id : listSpells)
        if (id != 0 && cache.contains(id) && seen.insert(id).second) out.push_back(id);
    return out;
}
bool SelfTestWaitOver(std::size_t ticks, int waitMs)
{
    return static_cast<long long>(ticks) * kSelfTestPollMs >= static_cast<long long>(waitMs);
}
SelfTestReach WitnessReach(const std::vector<SelfTestEntry>& entries, bool allLevels, float skill, const std::unordered_set<std::uint32_t>& witnesses)
{
    SelfTestReach r;
    const auto atSkill = [skill](std::uint16_t level) { return static_cast<float>(level) <= skill; };
    for (const auto& e : entries) {
        r.minLevel = std::min(r.minLevel, e.level);
        if (witnesses.contains(e.spell)) r.witnessMinLevel = std::min(r.witnessMinLevel, e.level);
        if (atSkill(e.level) && (r.topLevel == kSelfTestNoLevel || e.level > r.topLevel)) r.topLevel = e.level;
    }
    if (allLevels) {
        r.witnessReachable = r.witnessMinLevel != kSelfTestNoLevel && atSkill(r.witnessMinLevel);
        return r;
    }
    for (const auto& e : entries)
        if (r.topLevel != kSelfTestNoLevel && e.level == r.topLevel && witnesses.contains(e.spell)) r.witnessReachable = true;
    return r;
}
SelfTestVerdict JudgeSample(const std::vector<SelfTestListOutcome>& lists, SelfTestSampleLists where, std::size_t distinctEvidence, std::string& reason)
{
    if (lists.empty()) { reason = "no HoMNS list to check"; return SelfTestVerdict::kFail; }
    if (distinctEvidence > 0) { reason = std::format("HoMNS-only witnesses found: {}", distinctEvidence); return SelfTestVerdict::kPass; }
    if (where.actorBase == 0 && where.holder > 0) {
        reason = std::format("holder has {} HoMNS lists, the spawned actor's base has none", where.holder);
        return SelfTestVerdict::kFail;
    }
    std::size_t unreachable = 0, noWitness = 0, rolledOther = 0, open = 0;
    for (const auto& l : lists) {
        if (l.witnesses == 0) ++noWitness;
        else if (!l.witnessReachable) ++unreachable;
        else if (l.picked > 0 && l.pickedWitnesses == 0) ++rolledOther;
        else ++open;
    }
    if (open == 0) {
        reason = std::format("no list could give a witness: {} with no witness reachable at the skill, {} without witnesses, {} whose roll landed on non-witness spells",
            unreachable, noWitness, rolledOther);
        return SelfTestVerdict::kInconclusive;
    }
    reason = std::format("no witness found on {} lists that could give one", open);
    return SelfTestVerdict::kFail;
}
const char* VerdictName(SelfTestVerdict v)
{
    switch (v) {
    case SelfTestVerdict::kPass: return "PASS";
    case SelfTestVerdict::kInconclusive: return "INCONCLUSIVE";
    default: return "FAIL";
    }
}
std::size_t CountShared(const std::vector<std::uint32_t>& witnesses, const std::unordered_set<std::uint32_t>& otherLists)
{
    std::size_t n = 0;
    for (const auto id : witnesses) n += otherLists.contains(id) ? 1 : 0;
    return n;
}
std::string SelfTestRunLabel(std::string_view sampleText, int run, int runs)
{
    if (runs <= 1) return std::string(sampleText);
    return std::format("{} run={}/{}", sampleText, run, runs);
}
void SelfTestRunTally::AddRun(const std::vector<std::pair<std::string, bool>>& listPicked)
{
    ++checkedRuns;
    for (const auto& [editorId, picked] : listPicked) {
        auto it = std::ranges::find_if(pickedRuns, [&](const auto& p) { return p.first == editorId; });
        if (it == pickedRuns.end()) { pickedRuns.emplace_back(editorId, 0); it = std::prev(pickedRuns.end()); }
        it->second += picked ? 1 : 0;
    }
}
std::string SelfTestListsLine(std::string_view sampleText, int runs, const SelfTestRunTally& tally)
{
    std::string out = std::format("SELFTEST lists {} runs={} checked={}", sampleText, runs, tally.checkedRuns);
    if (tally.pickedRuns.empty()) return out + " (no run reached the check)";
    bool first = true;
    for (const auto& [editorId, picked] : tally.pickedRuns) {
        out += std::format("{} {} pickedRuns={}/{}", first ? "" : ",", editorId, picked, tally.checkedRuns);
        first = false;
    }
    return out;
}
std::string SelfTestEndLine(bool spawned, std::string_view reason, std::size_t pass, std::size_t fail, std::size_t inconclusive)
{
    if (spawned) return std::format("SELFTEST DONE pass={} fail={} inconclusive={}", pass, fail, inconclusive);
    return std::format("SELFTEST RESTART {}: the next save load in this session starts it again", reason.empty() ? "ended before the spawn" : reason);
}
}
