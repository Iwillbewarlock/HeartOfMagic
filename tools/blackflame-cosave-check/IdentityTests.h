#pragma once

// =============================================================================
// IdentityTests - the DLL's read of the builder's output identity (CopyContractIdentity.h), offline
// =============================================================================
// BuilderIdentityFault on texts (this version's release and dev forms, another version, malformed and binary bytes) and
// ReadIdentityResource on files: no file, a file that is no PE image, this exe (no identity resource: a builder from
// before BFK4 looks like this) and, when it is built beside this exe, blackflame-greybuild.exe itself (the read the DLL
// makes at kDataLoaded, its text taken and a valid identity of this contract's version).

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
    }

    template <class Check, class Skip>
    void Run(Check& a_check, Skip& a_skip)
    {
        RunTexts(a_check);
        std::error_code ec;
        const auto temp = std::filesystem::temp_directory_path(ec) /
                          ("blackflame-cosave-check-identity-" + std::to_string(::GetCurrentProcessId()));
        std::filesystem::create_directories(temp, ec);
        RunFiles(a_check, a_skip, temp);
        std::filesystem::remove_all(temp, ec);
    }
}
