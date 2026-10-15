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
std::size_t CountShared(const std::vector<std::uint32_t>& witnesses, const std::unordered_set<std::uint32_t>& otherLists)
{
    std::size_t n = 0;
    for (const auto id : witnesses) n += otherLists.contains(id) ? 1 : 0;
    return n;
}
}
