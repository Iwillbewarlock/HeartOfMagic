#include "GreyCheck.h"

#include "Config.h"
#include "GreyMap.h"
#include "HazardSwap.h"
#include "ImpactSwap.h"

#include <charconv>
#include <format>
#include <memory>
#include <string_view>

namespace BlackFlame
{
    namespace
    {
        constexpr int kHexBase = 16;

        std::vector<GreyCheckLine>& Results()
        {
            static auto* results = std::make_unique<std::vector<GreyCheckLine>>().release();
            return *results;
        }

        void Add(bool a_pass, std::string a_text)
        {
            logger::info("BlackFlame: grey check {}: {}", a_pass ? "PASS" : "FAIL", a_text);
            Results().push_back({ a_pass, std::move(a_text) });
        }

        // "Plugin.esp|000804" -> the ADDN record, or null
        RE::BGSAddonNode* LookupGreyForm(const std::string& a_form)
        {
            const auto bar = a_form.find('|');
            if (bar == std::string::npos) {
                return nullptr;
            }
            const std::string plugin = a_form.substr(0, bar);
            const std::string_view hex = std::string_view(a_form).substr(bar + 1);
            std::uint32_t local = 0;
            const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), local, kHexBase);
            if (ec != std::errc{} || end != hex.data() + hex.size()) {
                return nullptr;
            }
            auto* dh = RE::TESDataHandler::GetSingleton();
            return dh ? dh->LookupForm<RE::BGSAddonNode>(local, plugin) : nullptr;
        }

        void CheckAddons()
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            for (const auto& addon : GreyAddons()) {
                auto* form = LookupGreyForm(addon.greyForm);
                const auto& nodes = dh->addonNodes;
                auto* registered = addon.grey < nodes.free_idx() ? nodes[static_cast<std::uint16_t>(addon.grey)] : nullptr;
                const bool pass = form && form->index == addon.grey && registered == form;
                Add(pass, std::format("grey addon {} (for {}): record {} {}, index {}, addonNodes[{}] {}", addon.grey,
                    addon.index, addon.greyForm, form ? "found" : "NOT found", form ? form->index : 0, addon.grey,
                    registered == form && form ? "is it" : "is NOT it"));
            }
        }

        void CheckFiles()
        {
            int missing = 0;
            const auto files = GreyModelFiles();
            std::vector<std::string> paths = files;
            for (const auto& addon : GreyAddons()) {
                paths.push_back(addon.greyModel);
            }
            for (const auto& path : paths) {
                RE::BSResourceNiBinaryStream stream(path.c_str());
                if (!stream.good()) {
                    ++missing;
                    Add(false, std::format("grey model {} does not open", path));
                }
            }
            if (missing == 0) {
                Add(true, std::format("all {} grey model files open", paths.size()));
            }
        }
    }

    void RunGreyCheck()
    {
        if (!Results().empty()) {
            return;
        }
        // Always one line with the counts; no models while a swap is on is a FAIL (nothing could be swapped)
        const bool swapsOn = GetConfig().impactSwap || GetConfig().hazardSwap;
        const auto models = GreyModelCount();
        Add(!swapsOn || models > 0, std::format("grey map: {} models, {} addons{}", models, GreyAddons().size(),
            swapsOn && models == 0 ? " - impactSwap/hazardSwap are on but nothing can be swapped" : ""));
        if (models == 0) {
            return;
        }
        CheckAddons();
        CheckFiles();
        const auto impact = GetImpactSwapStatus();
        if (GetConfig().impactSwap) {
            Add(impact.idsPresent && impact.sitesHooked == 3 && impact.modelHooked,
                std::format("impact swap: ids {}, {} of 3 call sites hooked ({} chained), GetModel {}",
                    impact.idsPresent ? "present" : "MISSING", impact.sitesHooked, impact.sitesChained,
                    impact.modelHooked ? "hooked" : "NOT hooked"));
        }
        if (GetConfig().hazardSwap) {
            Add(HazardSwapHooked(), std::format("hazard swap: Load3D and GetModel {}", HazardSwapHooked() ? "hooked" : "NOT hooked"));
        }
    }

    const std::vector<GreyCheckLine>& GreyCheckResults()
    {
        return Results();
    }
}
