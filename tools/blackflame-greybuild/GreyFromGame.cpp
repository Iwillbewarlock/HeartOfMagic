// =============================================================================
// GreyFromGame - --from-game: the request and the NIF dump the DLL wrote, into one output mod
// =============================================================================
// The DLL writes <Documents>\My Games\<game folder>\SKSE\HeartOfMagic_BlackFlame\ at kDataLoaded
// (plugins/BlackFlame/src/CopyRequest.h): request.json (version 3) and data\meshes\... (the winning
// NIFs, read through the game's own file system). SE and AE share "Skyrim Special Edition"; the GOG
// build uses "Skyrim Special Edition GOG". When both hold a request, the newer one is taken. VR is
// not supported (the DLL has no VR build). Every refusal names the next step.

#include "GreyForms.h"

#include <fmt/format.h>
#include <iostream>
#include <optional>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

namespace GreyBuild
{
    namespace
    {
        constexpr const char* kRequestFile = "request.json";
        constexpr const char* kDumpFolder = "data";
        constexpr const wchar_t* kGameFolders[] = { L"Skyrim Special Edition", L"Skyrim Special Edition GOG" };

        std::optional<std::filesystem::path> DocumentsFolder()
        {
            PWSTR raw = nullptr;
            const HRESULT result = ::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw);
            std::optional<std::filesystem::path> folder;
            if (SUCCEEDED(result) && raw) {
                folder = std::filesystem::path(raw);
            }
            ::CoTaskMemFree(raw);  // freed whatever the result (null is allowed)
            return folder;
        }
    }

    std::optional<std::filesystem::path> FindRequestFolder(std::string& a_why)
    {
        const auto documents = DocumentsFolder();
        if (!documents) {
            a_why = "Windows did not give the Documents folder. Pass the folder yourself: --from-game "
                    "\"<Documents>\\My Games\\Skyrim Special Edition\\SKSE\\HeartOfMagic_BlackFlame\"";
            return std::nullopt;
        }
        std::optional<std::filesystem::path> best;
        std::filesystem::file_time_type bestTime{};
        std::string looked;
        for (const auto* game : kGameFolders) {
            const auto folder = *documents / L"My Games" / game / L"SKSE" / L"HeartOfMagic_BlackFlame";
            looked += fmt::format("\n  {}", PathText(folder / kRequestFile));
            std::error_code ec;
            const auto time = std::filesystem::last_write_time(folder / kRequestFile, ec);
            if (!ec && (!best || time > bestTime)) {
                best = folder;
                bestTime = time;
            }
        }
        if (!best) {
            a_why = fmt::format("no request.json found. Looked at:{}\nStart the game once with Heart of Magic - Black Flame "
                                "installed (config writeRequest true, the default) until the main menu shows, quit, then run "
                                "this again. Skyrim VR is not supported.", looked);
        }
        return best;
    }

    int RunFromGame(const std::filesystem::path& a_folder, const std::filesystem::path& a_out, float a_tint, bool a_reclaim,
        float a_tintAdd, bool a_textures)
    {
        constexpr int kOk = 0;
        constexpr int kFail = 1;
        constexpr int kUsage = 2;
        std::filesystem::path folder = a_folder;
        if (folder.empty()) {
            std::string why;
            const auto found = FindRequestFolder(why);
            if (!found) {
                std::cerr << why << "\n";
                return kFail;
            }
            folder = *found;
        }
        const auto request = folder / kRequestFile;
        const auto dump = folder / kDumpFolder;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(request, ec)) {
            std::cerr << fmt::format("{} is not there. Start the game once to the main menu with Black Flame installed, quit, "
                                     "then run this again.\n", PathText(request));
            return kFail;
        }
        if (!std::filesystem::is_directory(dump, ec)) {
            std::cerr << fmt::format("the NIF dump {} is missing (the game writes it next to request.json). Start the game "
                                     "once to the main menu, quit, then run this again.\n", PathText(dump));
            return kFail;
        }
        if (a_out.empty()) {
            std::cerr << "--from-game needs --out <the output mod folder>: an existing folder, the same one every time (in "
                         "MO2 make an empty mod, e.g. \"HeartOfMagic BlackFlame Copies\", and pass its folder).\n";
            return kUsage;
        }
        if (!std::filesystem::is_directory(a_out, ec)) {
            std::cerr << fmt::format("--out {} is not an existing folder. Create it first (in MO2: an empty mod placed after "
                                     "Heart of Magic - Black Flame) and run again.\n", PathText(a_out));
            return kUsage;
        }
        if (IsInside(a_out, folder) || IsInside(folder, a_out)) {
            std::cerr << fmt::format("--out {} and the request folder {} overlap: pick the output mod folder.\n",
                PathText(a_out), PathText(folder));
            return kUsage;
        }
        std::optional<StageTimer> readTime(std::in_place, "read request");
        const FormsRequest parsed = LoadFormsRequestFile(request);
        readTime.reset();
        if (parsed.version != kGameRequestVersion || !parsed.addons) {
            std::cerr << fmt::format("{} is a version {} request; --from-game reads the game's (version {}). Use --forms for a "
                                     "hand-made one.\n", PathText(request), parsed.version, kGameRequestVersion);
            return kUsage;
        }
        std::cout << fmt::format("request {} (game {}, fingerprint {}): {} forms, {} excluded, {} ADDN records, {} NIFs the game "
                                 "could not read\n", PathText(request), parsed.game, parsed.fingerprint, parsed.forms.size(),
            parsed.excluded.size(), parsed.addons->addons.size(), parsed.failedNifs.size());
        for (const auto& line : parsed.warnings) {
            std::cout << "WARN  " << line << "\n";
        }
        FormsRunOptions options;
        options.fingerprint = parsed.fingerprint;
        options.fingerprintSizes = parsed.fingerprintSizes;
        options.excluded = parsed.excluded;
        options.failedNifs = parsed.failedNifs;
        options.reclaimRetired = a_reclaim;
        options.warnAndContinue = true;
        options.tintAdd = a_tintAdd;
        // T1: textures need the dump of a DLL that writes them; an older request builds without them, saying so
        options.textures = a_textures && parsed.hasTextures;
        options.texturesNotDumped = parsed.texturesNotDumped;
        if (a_textures && !parsed.hasTextures) {
            std::cout << "NOTE  the request lists no textures (written by an older Black Flame DLL, or the game could not list "
                         "the textures - see HeartOfMagic_BlackFlame.log): no texture copies this run. Start the game once to "
                         "the main menu with this release, quit, then run this again.\n";
        }
        const auto totals = BuildForms(parsed.forms, *parsed.addons, { dump }, a_out, a_tint, options);
        std::cout << fmt::format("forms: {} copies, {} copy NIFs, {} warnings, {} failed\n", totals.copies, totals.nifsBuilt,
            totals.warnings + static_cast<int>(parsed.warnings.size()), totals.failed);
        if (totals.failed != 0 || totals.copies == 0) {
            return kFail;
        }
        std::cout << "next: enable the output mod (placed after Heart of Magic - Black Flame, so its two ESLs win over the empty "
                     "ones), then start the game again. If you use a loose-file cache mod (CRDW and the like), rebuild its cache "
                     "after running the builder: a cache made before this run does not know the new files.\n";
        return kOk;
    }
}
