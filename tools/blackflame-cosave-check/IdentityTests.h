#pragma once

// =============================================================================
// IdentityTests - the DLL's read of the builder's output identity (CopyContractIdentity.h), offline
// =============================================================================
// BuilderIdentityFault on texts (this version's release and dev forms, another version, malformed and binary bytes) and
// ReadIdentityResource on files: no file, a file that is no PE image, this exe (no identity resource: a builder from
// before BFK4 looks like this) and, when it is built beside this exe, blackflame-greybuild.exe itself (the read the DLL
// makes at kDataLoaded, its text taken and a valid identity of this contract's version). Review E2: the path LoadLibraryExW
// gets (DataFileLoadPath: absolute, back slashes), the builder read through a forward-slash spelling and a relative one,
// and the kinds the player's line tells apart (BuilderControl.h ClassifyIdentity: missing, unreadable, another version,
// dev, release).

#include "../../plugins/BlackFlame/src/BuilderControl.h"
#include "../../plugins/BlackFlame/src/CopyContractIdentity.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace IdentityTests
{
    namespace CC = BlackFlame::CopyContract;

    inline constexpr const wchar_t* kBuilderExeName = L"blackflame-greybuild.exe";
    inline constexpr std::size_t kSelfPathChars = 32768;

    inline std::filesystem::path ThisExe()
    {
        std::wstring buffer(kSelfPathChars, L'\0');
        const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        buffer.resize(length < buffer.size() ? length : 0);
        return buffer;
    }

    inline bool Contains(const std::optional<std::string>& a_text, const char* a_needle)
    {
        return a_text && a_text->find(a_needle) != std::string::npos;
    }

    template <class Check>
    void RunTexts(Check& a_check)
    {
        const std::string own = std::to_string(CC::kBuilderOutputVersion);
        const std::string other = std::to_string(CC::kBuilderOutputVersion + 1);
        a_check(!CC::BuilderIdentityFault(own) && !CC::BuilderIdentityFault(own + "+dev0123456789abcdef"),
            "identity read: this DLL's version, release or dev form, is taken (" + own + ")");
        const auto newer = CC::BuilderIdentityFault(other);
        const auto newerDev = CC::BuilderIdentityFault(other + "+devffffffffffffffff");
        a_check(Contains(newer, ("is of output version " + other + " and this DLL of " + own).c_str()) &&
                    Contains(newerDev, "two releases") && CC::BuilderIdentityFault(own, CC::kBuilderOutputVersion + 1),
            "identity read: another output version is refused, its numbers named (" + newer.value_or("taken") + ")");
        const auto malformed = CC::BuilderIdentityFault("01");
        const auto binary = CC::BuilderIdentityFault(std::string("1\0", 2));
        const auto empty = CC::BuilderIdentityFault("");
        a_check(Contains(malformed, "malformed output identity '01'") && Contains(binary, "'hex 3100'") && Contains(empty, "''"),
            "identity read: a malformed identity is refused, shown as text or as hex when it holds other bytes (" +
                binary.value_or("taken") + ")");
    }

    template <class Check>
    void RunKinds(Check& a_check)
    {
        namespace BC = BlackFlame::BuilderControl;
        using Kind = BC::BuilderIdentityKind;
        const std::string own = std::to_string(CC::kBuilderOutputVersion);
        const std::string other = std::to_string(CC::kBuilderOutputVersion + 1);
        struct Case
        {
            const char* what;
            bool found;
            std::optional<std::string> text;
            Kind expected;
        };
        const Case cases[] = {
            { "the exe not found", false, own, Kind::kMissing },
            { "the exe not loaded or no resource", true, std::nullopt, Kind::kUnreadable },
            { "a malformed identity", true, std::string("01"), Kind::kUnreadable },
            { "a binary identity", true, std::string("1\0", 2), Kind::kUnreadable },
            { "another version", true, other, Kind::kOtherVersion },
            { "another version, dev", true, other + "+devffffffffffffffff", Kind::kOtherVersion },
            { "this version, dev", true, own + "+dev0123456789abcdef", Kind::kDev },
            { "this version, release", true, own, Kind::kRelease },
        };
        for (const auto& c : cases) {
            const auto kind = BC::ClassifyIdentity(c.found, c.text);
            const bool faultFree = c.text && !CC::BuilderIdentityFault(*c.text);
            a_check(kind == c.expected && (!c.found || BC::IdentityTaken(kind) == faultFree),
                std::string("identity kind: ") + c.what + " (taken exactly when BuilderIdentityFault finds nothing)");
        }
        const auto missing = BC::BuilderNotice(Kind::kMissing);
        const auto unreadable = BC::BuilderNotice(Kind::kUnreadable);
        const auto otherLine = BC::BuilderNotice(Kind::kOtherVersion);
        a_check(missing.find("was not found") != std::string::npos && unreadable.find("could not be read") != std::string::npos &&
                    otherLine.find("from another version") != std::string::npos && missing != unreadable && unreadable != otherLine &&
                    BC::BuilderNotice(Kind::kDev).empty() && BC::BuilderNotice(Kind::kRelease).empty(),
            "identity kind: three distinct player lines (not found / could not be read / another version), none for a taken "
            "builder ('" + unreadable + "')");
        a_check(BC::BuilderRouteOffClause(Kind::kUnreadable) == "; the grey copy builder could not be read either" &&
                    BC::BuilderRouteOffClause(Kind::kOtherVersion) == "; the grey copy builder is from another version too" &&
                    BC::BuilderRouteOffClause(Kind::kMissing) == "; the grey copy builder was not found either" &&
                    BC::BuilderRouteOffClause(Kind::kDev).empty() && BC::BuilderRouteOffClause(Kind::kRelease).empty(),
            "identity kind: the route-off line's clause per kind");
    }

    inline bool HasForwardSlash(const std::filesystem::path& a_path)
    {
        return a_path.native().find(L'/') != std::wstring::npos;
    }

    template <class Check>
    void RunPaths(Check& a_check)
    {
        const auto relative = CC::DataFileLoadPath("sub/dir/builder.exe");
        const auto native = relative.native();
        const std::wstring tail = L"sub\\dir\\builder.exe";
        a_check(relative.is_absolute() && !HasForwardSlash(relative) && native.size() > tail.size() &&
                    native.compare(native.size() - tail.size(), tail.size(), tail) == 0,
            "load path: a relative forward-slash path becomes absolute with back slashes (never the module search)");
        a_check(CC::DataFileLoadPath("C:/Games/Skyrim/Data/SKSE/Plugins/x.exe").native() == L"C:\\Games\\Skyrim\\Data\\SKSE\\Plugins\\x.exe",
            "load path: an absolute forward-slash path gets back slashes");
        const auto empty = CC::ReadIdentityResource("");
        a_check(CC::DataFileLoadPath("").empty() && !empty.text && empty.why.find("does not resolve") != std::string::npos,
            "load path: an empty path is refused before LoadLibraryExW (" + empty.why + ")");
    }

    template <class Check, class Skip>
    void RunFiles(Check& a_check, Skip& a_skip, const std::filesystem::path& a_temp)
    {
        const auto missing = CC::ReadIdentityResource(a_temp / "no such builder.exe");
        a_check(!missing.text && missing.why.find("does not load as a data file") != std::string::npos,
            "identity read: no file - not loaded (" + missing.why + ")");
        const auto text = a_temp / "not-an-exe.exe";
        std::ofstream(text, std::ios::binary) << "this is no PE image";
        const auto notPe = CC::ReadIdentityResource(text);
        a_check(!notPe.text && notPe.why.find("does not load as a data file") != std::string::npos,
            "identity read: a file that is no PE image - not loaded (" + notPe.why + ")");
        const auto self = ThisExe();
        const auto none = CC::ReadIdentityResource(self);
        a_check(!self.empty() && !none.text && none.why.find("has no output identity resource") != std::string::npos,
            "identity read: an exe without the resource (this one; a builder from before BFK4) - refused (" + none.why + ")");
        const auto builder = self.parent_path() / kBuilderExeName;
        std::error_code ec;
        if (!std::filesystem::exists(builder, ec)) {
            a_skip("identity read: no blackflame-greybuild.exe beside this exe (the real builder's resource was not read)");
            return;
        }
        const auto read = CC::ReadIdentityResource(builder);
        a_check(read.text && !CC::BuilderIdentityFault(*read.text),
            "identity read: the builder beside this exe carries a valid identity of this version (" +
                (read.text ? *read.text : read.why) + ")");
        const std::filesystem::path slashedPath = builder.generic_wstring();
        const auto slashed = CC::ReadIdentityResource(slashedPath);
        a_check(HasForwardSlash(slashedPath) && slashed.text && read.text && *slashed.text == *read.text,
            "identity read: the same builder named with forward slashes reads the same identity");
        const auto relative = std::filesystem::relative(builder, std::filesystem::current_path(ec), ec);
        if (ec || relative.empty() || relative.is_absolute()) {
            a_skip("identity read: the builder has no relative path from the working folder (another drive?)");
            return;
        }
        const auto fromRelative = CC::ReadIdentityResource(relative);
        a_check(fromRelative.text && read.text && *fromRelative.text == *read.text,
            "identity read: the same builder named by a relative path (" + relative.string() + ") reads the same identity");
    }

    template <class Check, class Skip>
    void Run(Check& a_check, Skip& a_skip)
    {
        RunTexts(a_check);
        RunKinds(a_check);
        RunPaths(a_check);
        std::error_code ec;
        const auto temp = std::filesystem::temp_directory_path(ec) /
                          ("blackflame-cosave-check-identity-" + std::to_string(::GetCurrentProcessId()));
        std::filesystem::create_directories(temp, ec);
        RunFiles(a_check, a_skip, temp);
        std::filesystem::remove_all(temp, ec);
    }
}
