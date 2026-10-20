// =============================================================================
// OldCopiesVfs - the old-copies cleanup's guard on Data under MO2's VFS (OldCopiesVfs.h)
// =============================================================================

#include "OldCopiesVfs.h"

#include "BuildProgress.h"
#include "SlotLedgerFiles.h"

namespace BlackFlame::OldCopies::Vfs
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace Files = OldCopiesFiles;

        constexpr const wchar_t* kUsvfsModule = L"usvfs_x64.dll";

        std::string Text(const fs::path& a_path) { return SlotLedgerFiles::PathText(a_path); }
    }

    Guard MakeGuard(const fs::path& a_data, const OldCopiesRule::KeySet& a_keep)
    {
        Guard guard;
        guard.vfs = ::GetModuleHandleW(kUsvfsModule) != nullptr;
        if (!guard.vfs) {
            return guard;
        }
        std::optional<std::string> sample;
        for (const auto key : a_keep) {
            const auto object = BuildProgress::Find(key);
            if (object && object->copiedThisSession && !object->replacedThisSession && DataPathRule::ParseCopyPath(object->line.path)) {
                sample = object->line.path;
                break;
            }
        }
        if (!sample) {
            guard.why = "no file was copied into Data this session to find MO2's overwrite folder from";
            return guard;
        }
        const auto relative = fs::path(*sample);
        std::string why;
        const auto real = Files::FinalPath(a_data / relative, why);
        const auto root = real ? Files::RootFromSample(*real, relative) : std::nullopt;
        if (!real || !root) {
            guard.why = real ? "the real path '" + Text(*real) + "' of '" + *sample + "' does not end in its Data path" :
                               "the real path of '" + *sample + "' could not be read: " + why;
        } else if (const auto dataReal = Files::FinalPath(a_data, why); !dataReal) {
            guard.why = "the real path of the Data folder '" + Text(a_data) + "' could not be read: " + why;
        } else if (Files::Under(*dataReal, *root, false) || Files::Under(a_data, *root, false)) {
            // the real spelling of Data too (case, 8.3 names), so a virtual path given back is never taken for the overwrite folder
            guard.why = "GetFinalPathNameByHandleW gave the virtual path '" + Text(*real) + "' (Data is '" + Text(*dataReal) +
                        "'; MO2's overwrite folder cannot be told)";
        } else {
            guard.overwrite = root;
        }
        logger::info("BlackFlame: old copies: MO2 VFS ({} loaded): '{}' resolves to '{}': {}", Text(fs::path(kUsvfsModule)),
            Text(a_data / relative), real ? Text(*real) : "(none: " + why + ")",
            guard.overwrite ? "overwrite folder '" + Text(*guard.overwrite) + "'" : "Data is counted only: " + guard.why);
        return guard;
    }

    // Every file of a_folder (a Data key folder) and the folder itself resolve under the overwrite folder
    bool InOverwrite(Guard& a_guard, const fs::path& a_folder, const Files::Listing& a_listing)
    {
        bool all = true;
        for (std::size_t i = 0; all && i <= a_listing.files.size(); ++i) {
            const auto& path = i < a_listing.files.size() ? a_listing.files[i].first : a_folder;
            std::string why;
            const auto real = Files::FinalPath(path, why);
            all = real && Files::Under(*a_guard.overwrite, *real);
            if (!a_guard.sampleLogged) {
                a_guard.sampleLogged = true;
                logger::info("BlackFlame: old copies: MO2 VFS: first Data candidate '{}' resolves to '{}' ({})", Text(path),
                    real ? Text(*real) : "(none: " + why + ")", all ? "under the overwrite folder" : "not under it: kept");
            }
        }
        return all;
    }
}
