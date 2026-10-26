#pragma once

// =============================================================================
// DUMP SWEEP TESTS - the dump files no manifest stamp names (plugins/BlackFlame/src/DumpSweep.h), on real folders
// =============================================================================
// A stamped file stays (any ASCII case), an unstamped one goes with the folders it leaves empty, the root stays, a file that
// will not delete is counted as failed, an empty stamp set clears the folder, a missing root removes nothing. Included by
// main.cpp; a_dir is a fresh temporary folder.

#include "../../plugins/BlackFlame/src/DumpSweep.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

namespace DumpSweepTests
{
    namespace Sweep = BlackFlame::DumpSweep;
    namespace fs = std::filesystem;

    inline void Put(const fs::path& a_file, const std::string& a_text)
    {
        fs::create_directories(a_file.parent_path());
        std::ofstream(a_file, std::ios::binary) << a_text;
    }

    template <class Check>
    void Run(Check&& a_check, const fs::path& a_dir)
    {
        const auto root = a_dir / "dump-sweep" / "data";
        fs::remove_all(a_dir / "dump-sweep");
        Put(root / "meshes" / "magic" / "kept.nif", "NIF");
        Put(root / "Textures" / "Effects" / "Kept.DDS", "DDS");
        Put(root / "meshes" / "old" / "mod" / "gone.nif", "OLD!");
        Put(root / "textures" / "old.dds", "OLDER");
        const std::set<std::string> stamped{ "meshes\\magic\\kept.nif", "textures\\effects\\kept.dds", "textures\\never-dumped.dds" };
        const auto swept = Sweep::RemoveUnstamped(root, stamped);
        a_check(swept.files == 2 && swept.bytes == 9 && swept.failed == 0 && fs::exists(root / "meshes" / "magic" / "kept.nif") &&
                    fs::exists(root / "Textures" / "Effects" / "Kept.DDS") && !fs::exists(root / "meshes" / "old") &&
                    !fs::exists(root / "textures" / "old.dds") && fs::exists(root),
            "dump sweep: stamped files stay (ASCII case ignored), the others go with the folders they leave empty, the root stays "
            "(removed " + std::to_string(swept.files) + ", " + std::to_string(swept.bytes) + " bytes)");

        const auto locked = root / "textures" / "locked.dds";
        Put(locked, "L");
        const HANDLE handle = ::CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto held = Sweep::RemoveUnstamped(root, stamped);
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
        a_check(handle != INVALID_HANDLE_VALUE && held.files == 0 && held.failed == 1 && fs::exists(locked) &&
                    fs::exists(root / "meshes" / "magic" / "kept.nif"),
            "dump sweep: a file another handle holds without delete sharing is counted as failed and stays (tried again next start)");

        const auto all = Sweep::RemoveUnstamped(root, {});
        a_check(all.files == 3 && all.failed == 0 && fs::exists(root) && fs::is_empty(root),
            "dump sweep: an empty stamp set (nothing is needed) clears the dump, the root stays");
        const auto none = Sweep::RemoveUnstamped(a_dir / "dump-sweep" / "missing", stamped);
        a_check(none.files == 0 && none.failed == 0 && !none.refused, "dump sweep: a missing dump folder removes nothing");

        // review Codex: Windows folds non-ASCII case too ("\xC3\x89" is U+00C9, "\xC3\xA9" U+00E9)
        Put(root / fs::path(L"textures\\Éffects\\a.dds"), "A");
        const auto accented = Sweep::RemoveUnstamped(root, { "textures\\\xC3\xA9" "ffects\\a.dds" });
        a_check(accented.files == 0 && fs::exists(root / fs::path(L"textures\\Éffects\\a.dds")),
            "dump sweep: a stamp naming a folder in another non-ASCII case is the same file (kept)");

        // review Codex: nothing under a junction is looked at - the root itself or one inside it
        const auto outside = a_dir / "dump-sweep" / "outside";
        Put(outside / "precious.txt", "keep me");
        const auto junctionRoot = a_dir / "dump-sweep" / "junction-root";
        const auto inner = root / "linked";
        const auto mklink = [](const fs::path& a_link, const fs::path& a_target) {
            const std::wstring command = L"cmd /c mklink /J \"" + a_link.wstring() + L"\" \"" + a_target.wstring() + L"\" >nul";
            return _wsystem(command.c_str()) == 0;
        };
        const bool linked = mklink(junctionRoot, outside) && mklink(inner, outside);
        const auto viaRoot = Sweep::RemoveUnstamped(junctionRoot, {});
        const auto viaInner = Sweep::RemoveUnstamped(root, {});
        a_check(linked && viaRoot.refused && viaRoot.files == 0 && viaInner.failed == 0 && fs::exists(outside / "precious.txt"),
            "dump sweep: a dump folder that is a junction is refused, and a junction inside it is left alone (its target's files stay)");
        std::error_code ec;
        fs::remove(junctionRoot, ec);  // the junctions themselves, never their target's files
        fs::remove(inner, ec);
        fs::remove_all(a_dir / "dump-sweep");
    }
}
