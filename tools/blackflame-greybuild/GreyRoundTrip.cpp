// =============================================================================
// GreyRoundTrip - does nifly write a NIF back as it read it?
// =============================================================================
// The grey copies are nifly re-saves, so this tells how far a copy can differ from
// its source apart from our edits. nifly rebuilds the header string table on save,
// which can reorder it; that alone is reported as StringOrder (same file size, same
// size per block, the same strings, and a second re-save byte-identical to the first).

#include "GreyBuild.h"

#include <NifFile.hpp>

#include <algorithm>
#include <fmt/format.h>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        constexpr std::size_t kContextBytes = 12;  // bytes shown each side of a difference

        [[nodiscard]] std::string ReadAll(const std::filesystem::path& a_file)
        {
            std::ifstream in(a_file, std::ios::binary);
            if (!in) {
                throw std::runtime_error(fmt::format("cannot open '{}' for reading", a_file.string()));
            }
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        void LoadBytes(nifly::NifFile& a_nif, const std::string& a_bytes, const std::string& a_what)
        {
            std::istringstream in(a_bytes, std::ios::binary);
            if (a_nif.Load(in) != 0 || !a_nif.IsValid()) {
                throw std::runtime_error(fmt::format("nifly could not load {}", a_what));
            }
        }

        [[nodiscard]] std::vector<std::string> SortedStrings(const nifly::NiHeader& a_hdr)
        {
            std::vector<std::string> strings;
            for (std::uint32_t i = 0; i < a_hdr.GetStringCount(); ++i) {
                strings.push_back(a_hdr.GetStringById(i));
            }
            std::sort(strings.begin(), strings.end());
            return strings;
        }

        [[nodiscard]] std::vector<std::uint32_t> BlockSizes(const nifly::NiHeader& a_hdr)
        {
            std::vector<std::uint32_t> sizes;
            for (std::uint32_t id = 0; id < a_hdr.GetNumBlocks(); ++id) {
                sizes.push_back(a_hdr.GetBlockSize(id));
            }
            return sizes;
        }

        [[nodiscard]] std::string Bytes(const std::string& a_data, std::size_t a_from, std::size_t a_to)
        {
            std::string text;
            for (std::size_t i = a_from; i < a_data.size() && i < a_to; ++i) {
                text += fmt::format(" {:02X}", static_cast<unsigned char>(a_data[i]));
            }
            return text;
        }
    }

    const char* RoundTripName(RoundTripKind a_kind) noexcept
    {
        switch (a_kind) {
        case RoundTripKind::Identical:
            return "identical";
        case RoundTripKind::StringOrder:
            return "string table reordered only";
        default:
            return "DIFFERENT";
        }
    }

    RoundTrip CheckRoundTrip(const std::filesystem::path& a_file)
    {
        const std::string original = ReadAll(a_file);
        nifly::NifFile first;
        LoadBytes(first, original, a_file.string());
        const auto stringsBefore = SortedStrings(first.GetHeader());
        const auto sizesBefore = BlockSizes(first.GetHeader());

        const std::string saved = SaveToBytes(first);
        if (saved == original) {
            return { RoundTripKind::Identical, fmt::format("{} bytes", original.size()) };
        }
        nifly::NifFile second;
        LoadBytes(second, saved, "its own re-save");
        const bool stable = SaveToBytes(second) == saved;
        const bool sameStrings = SortedStrings(second.GetHeader()) == stringsBefore;
        const bool sameSizes = saved.size() == original.size() && BlockSizes(second.GetHeader()) == sizesBefore;
        if (stable && sameStrings && sameSizes) {
            return { RoundTripKind::StringOrder,
                fmt::format("{} bytes, {} blocks of the same sizes, the same {} strings in another order; a second "
                            "re-save is byte-identical",
                    original.size(), sizesBefore.size(), stringsBefore.size()) };
        }
        std::size_t offset = 0;
        while (offset < original.size() && offset < saved.size() && original[offset] == saved[offset]) {
            ++offset;
        }
        const std::size_t from = offset >= kContextBytes ? offset - kContextBytes : 0;
        return { RoundTripKind::Different,
            fmt::format("bytes {} -> {}, block sizes {}, strings {}, second re-save {}; first difference at byte {}"
                        "\n    source :{}\n    re-save:{}",
                original.size(), saved.size(), sameSizes ? "same" : "changed", sameStrings ? "same" : "changed",
                stable ? "stable" : "unstable", offset, Bytes(original, from, offset + kContextBytes),
                Bytes(saved, from, offset + kContextBytes)) };
    }
}
