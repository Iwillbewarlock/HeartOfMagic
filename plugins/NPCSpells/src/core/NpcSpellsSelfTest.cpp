#include "core/NpcSpellsSelfTest.h"
#include <nlohmann/json.hpp>
#include <format>
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
        if (!v.is_string()) { AppendError(error, std::format("samples: not a FormID (XXXXXX:Plugin.ext): {}", v.dump())); continue; }
        std::string refError;
        const auto& text = v.get_ref<const std::string&>();
        if (auto ref = ParseFormRef(text, refError)) { out.samples.push_back(*ref); out.sampleTexts.push_back(text); }
        else AppendError(error, "samples: " + refError);
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
SelfTestVerdict JudgeSample(const std::vector<SelfTestListOutcome>& lists, std::size_t distinctEvidence, std::string& reason)
{
    if (lists.empty()) { reason = "no HoMNS list to check"; return SelfTestVerdict::kFail; }
    if (distinctEvidence > 0) { reason = std::format("HoMNS-only witnesses found: {}", distinctEvidence); return SelfTestVerdict::kPass; }
    std::size_t below = 0, noWitness = 0, open = 0;
    for (const auto& l : lists) {
        if (l.witnesses == 0) ++noWitness;
        else if (l.belowEvery) ++below;
        else ++open;
    }
    if (open == 0) {
        reason = std::format("no list could give a witness: {} with the skill below every entry, {} without witnesses", below, noWitness);
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
}
