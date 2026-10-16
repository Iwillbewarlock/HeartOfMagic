#include "CopyFreshness.h"

#include "CopyRequest.h"
#include "FormCopies.h"
#include "MainThreadQueue.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>

namespace BlackFlame
{
    namespace
    {
        constexpr const char* kStaleText = "Black Flame: your fire spells changed. Run the Black Flame builder, then restart the game.";
        constexpr const char* kNoneText = "Black Flame: the grey copies are not built yet. Run the Black Flame builder, then restart the game.";
        constexpr const char* kEmptyText = "Black Flame: the copies output holds no fire forms. Check the builder's messages.";
        constexpr const char* kMissingText = "Black Flame: the generated copies are not enabled. Enable HeartOfMagic_BlackFlame_Copies.esp "
                                             "and put the copies output mod after Black Flame.";
        // The player-facing line when a loose-file index (CRDW and the like) does not know the copies; {} the count, {} the cache
        constexpr const char* kFileIndexText = "Black Flame: {} grey copies are not visible to the game's file index ({}) - "
                                               "rebuild that cache.";
        constexpr const char* kFileIndexNamed = "the CRDW loose-file cache is out of date";
        constexpr const char* kFileIndexGeneric = "a loose-file cache such as CRDW is out of date";
        constexpr int kOldMapVersion = 1;

        TrackedMutex g_fileIndexMutex;
        std::string g_fileIndexText;               // under g_fileIndexMutex
        std::atomic<bool> g_fileIndexNotified{ false };
        std::atomic<bool> g_fileIndexCrdw{ false };  // W13: CRDW.dll was loaded when the stale index was reported

        FreshnessState g_state;          // written once at kDataLoaded
        bool g_checked = false;          // F10: CheckFreshness ran (kDataLoaded finished its copy request part)
        std::string g_uncheckedWhy;      // why not, when it did not
        std::atomic<bool> g_notified{ false };

        bool CopiesPluginLoaded()
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            return dh && dh->LookupModByName(kCopiesPluginName) != nullptr;
        }
    }

    const char* FreshnessName(CopyFreshness a_state) noexcept
    {
        switch (a_state) {
        case CopyFreshness::kFresh:
            return "fresh";
        case CopyFreshness::kStale:
            return "stale";
        case CopyFreshness::kPluginMissing:
            return "pluginMissing";
        default:
            return "none";
        }
    }

    void CheckFreshness(const FireClosure& a_closure)
    {
        const auto counts = GetCopyCounts();
        const auto& request = GetCopyRequestState();
        FreshnessState state;
        for (const auto& form : a_closure.forms) {
            state.formsWithoutCopy += CopyOf(form.form) ? 0 : 1;
        }
        state.copiesLostSource = counts.sourceMissing;
        if (!counts.mapRead || counts.mapEntries == 0) {
            state.state = CopyFreshness::kNone;
            state.detail = counts.mapRead ? "the copies map has no entry" : "no copies map (the builder has not been run)";
        } else if (!CopiesPluginLoaded() || (counts.copyMissing > 0 && counts.copyMissing * 2 >= counts.mapEntries)) {
            state.state = CopyFreshness::kPluginMissing;
            state.detail = std::format("the map was read but {} ({} of {} entries without their copy form)",
                CopiesPluginLoaded() ? std::string("most copies are missing") : std::string(kCopiesPluginName) + " is not loaded",
                counts.copyMissing, counts.mapEntries);
        } else {
            // The byte form when this start computed it (writeRequest on), else the size form
            const bool bytes = request.computed && !counts.requestFingerprint.empty();
            const auto& now = bytes ? request.fingerprint : request.sizeFingerprint;
            const auto& built = bytes ? counts.requestFingerprint : counts.requestFingerprintSizes;
            if (counts.mapVersion == kOldMapVersion) {
                state.state = CopyFreshness::kStale;
                state.detail = "a version 1 map (built before the game wrote requests)";
            } else if (built.empty() || now.empty() || built != now) {
                state.state = CopyFreshness::kStale;
                state.detail = std::format("the {} fingerprint differs: built {}, now {}", bytes ? "byte" : "size",
                    built.empty() ? std::string("(none in the map)") : built, now.empty() ? std::string("(not computed)") : now);
            } else {
                state.state = CopyFreshness::kFresh;
                state.detail = std::format("the {} fingerprint matches ({})", bytes ? "byte" : "size", now);
            }
        }
        state.detail += std::format("; {} closure forms without a copy, {} map entries whose source is gone", state.formsWithoutCopy,
            state.copiesLostSource);
        g_state = std::move(state);
        g_checked = true;
        logger::info("BlackFlame: copies freshness: {} - {}", FreshnessName(g_state.state), g_state.detail);
    }

    void MarkFreshnessUnchecked(const std::string& a_why)
    {
        g_checked = false;
        g_uncheckedWhy = a_why;
        g_state.detail = "not checked: " + a_why;
        logger::warn("BlackFlame: copies freshness not checked ({}): no notice will be shown this session", a_why);
    }

    const FreshnessState& GetFreshness() noexcept
    {
        return g_state;
    }

    void ReportStaleFileIndex(std::size_t a_count)
    {
        const bool crdw = REX::W32::GetModuleHandleW(L"CRDW.dll") != nullptr;  // CRDW AE's SKSE plugin (mod "CRDW AE")
        g_fileIndexCrdw.store(crdw);
        const auto text = std::vformat(kFileIndexText, std::make_format_args(a_count, crdw ? kFileIndexNamed : kFileIndexGeneric));
        {
            std::lock_guard lock(g_fileIndexMutex);
            g_fileIndexText = text;
        }
        logger::warn("BlackFlame: {} copy NIF(s) exist on disk but the game's loose-file index does not know them ({}): \"{}\" - "
                     "rebuild the loose-file cache after running the builder", a_count, crdw ? "CRDW.dll is loaded" :
                     "no CRDW.dll; another index layer", text);
        const auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->GetParentCell()) {
            NotifyFileIndexOnce();  // in the world already: now
        }
    }

    std::string FileIndexState(const std::vector<std::string>& a_copyModelPaths)
    {
        constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;
        constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;
        std::uint64_t hash = kFnvOffset;
        const auto mix = [&](std::string_view a_text) {
            for (const char c : a_text) {
                hash = (hash ^ static_cast<unsigned char>(c)) * kFnvPrime;
            }
        };
        const auto stamp = [](const std::filesystem::path& a_file) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(a_file, ec);
            const auto ok = !ec;
            const auto time = std::filesystem::last_write_time(a_file, ec);
            return ok && !ec ? std::format("{}:{}", size, time.time_since_epoch().count()) : std::string("absent");
        };
        for (const auto& model : a_copyModelPaths) {
            std::string lower = model;
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const std::string relative = lower.starts_with("meshes\\") ? model : "meshes\\" + model;
            mix(model);
            mix(stamp(std::filesystem::path("Data") / relative));
        }
        std::string crdw = "no CRDW.dll";
        if (const auto module = REX::W32::GetModuleHandleW(L"CRDW.dll")) {
            constexpr std::size_t kModulePathChars = 1024;  // a longer module path reads as absent
            std::wstring file(kModulePathChars, L'\0');
            const auto length = REX::W32::GetModuleFileNameW(module, file.data(), static_cast<std::uint32_t>(file.size()));
            file.resize(length);
            crdw = "CRDW.dll " + stamp(std::filesystem::path(file));
        }
        return std::format("index {}, copy NIFs {} fnv64:{:016x}", crdw, a_copyModelPaths.size(), hash);
    }

    std::string StaleFileIndexText()
    {
        std::lock_guard lock(g_fileIndexMutex);
        return g_fileIndexText;
    }

    void NotifyFileIndexOnce()
    {
        const auto text = StaleFileIndexText();
        if (text.empty() || g_fileIndexNotified.exchange(true)) {  // W13: claimed first, so two callers cannot both post
            return;
        }
        if (!g_fileIndexCrdw.load()) {
            logger::info("BlackFlame: file index notice \"{}\" (no CRDW.dll: logged only, not shown)", text);
            return;
        }
        if (!MainQueue::Installed()) {
            logger::info("BlackFlame: file index notice \"{}\" (no main-thread queue: not shown)", text);
            return;
        }
        if (MainQueue::Post("file index notice", [text]() { RE::SendHUDMessage::ShowHUDMessage(text.c_str()); })) {
            logger::info("BlackFlame: file index notice: telling the player \"{}\"", text);
        } else {
            g_fileIndexNotified.store(false);  // not posted: the next caller tries again
        }
    }

    void NotifyFreshnessOnce()
    {
        const auto state = g_state.state;
        if (state == CopyFreshness::kFresh || g_notified.load()) {
            return;
        }
        if (!g_checked) {
            // F10: kDataLoaded did not get as far as the check (LoadForms or the closure failed): say so, show nothing
            g_notified.store(true);
            logger::warn("BlackFlame: copies freshness was not checked this session ({}): no notice",
                g_uncheckedWhy.empty() ? std::string("kDataLoaded did not finish") : g_uncheckedWhy);
            return;
        }
        const auto counts = GetCopyCounts();
        const char* text = state == CopyFreshness::kPluginMissing      ? kMissingText :
                           state == CopyFreshness::kNone && counts.mapRead ? kEmptyText :  // G9: a map with no entry
                           state == CopyFreshness::kNone                 ? kNoneText :
                                                                           kStaleText;
        if (!MainQueue::Installed()) {
            g_notified.store(true);  // no AddTask fallback for a HUD line: the log says it, once
            logger::info("BlackFlame: copies freshness {}: \"{}\" (no main-thread queue: not shown)", FreshnessName(state), text);
            return;
        }
        // F10: marked only once the notice is really queued (a stopped task kind refuses it)
        if (MainQueue::Post("copies freshness notice", [text]() { RE::SendHUDMessage::ShowHUDMessage(text); })) {
            g_notified.store(true);
            logger::info("BlackFlame: copies freshness {}: telling the player \"{}\"", FreshnessName(state), text);
        }
    }
}
