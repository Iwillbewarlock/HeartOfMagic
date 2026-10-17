// =============================================================================
// SkseFile - the DLL's readers on real files: .skse co-saves (--cosave) and NIF headers (--nif-values)
// =============================================================================

#include "SkseFile.h"

#include "../../plugins/BlackFlame/src/CoSaveFile.h"
#include "../../plugins/BlackFlame/src/NifValueNodes.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace BlackFlame::SkseFile
{
    namespace
    {
        using Bytes = std::vector<unsigned char>;

        bool ReadWhole(const char* a_path, Bytes& a_out)
        {
            std::ifstream in(a_path, std::ios::binary);
            if (!in) {
                return false;
            }
            a_out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        std::string UidText(std::uint32_t a_uid)
        {
            std::string text;
            for (int shift = 24; shift >= 0; shift -= 8) {
                const auto c = static_cast<unsigned char>((a_uid >> shift) & 0xFF);
                text.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '.');
            }
            return text;
        }
    }

    int WalkFiles(int a_argc, char** a_argv)
    {
        int failed = 0;
        for (int i = 2; i < a_argc; ++i) {
            Bytes bytes;
            if (!ReadWhole(a_argv[i], bytes)) {
                std::cout << a_argv[i] << ": does not open\n";
                ++failed;
                continue;
            }
            const auto walk = CoSaveFile::WalkCoSave(bytes.data(), bytes.size());
            const auto bfsl = CoSaveFile::ReadBfsl(bytes.data(), bytes.size());
            std::cout << a_argv[i] << ": " << bytes.size() << " bytes, walk " << CoSaveFile::Describe(walk.result);
            if (walk.result != CoSaveFile::Result::Ok) {
                std::cout << " at offset " << walk.failAt;
            }
            std::cout << ", runtime 0x" << std::hex << walk.runtimeVersion << std::dec << ", " << walk.plugins.size()
                      << " blocks; Black Flame: " << CoSaveFile::Describe(bfsl.state);
            if (bfsl.state == CoSaveFile::BfslState::kRead) {
                std::cout << " (" << bfsl.snapshot.slots.size() << " slots, epoch " << CopyContract::Hex16(bfsl.snapshot.epoch) << ")";
            } else if (!bfsl.why.empty()) {
                std::cout << " (" << bfsl.why << ")";
            }
            std::cout << "\n";
            for (const auto& plugin : walk.plugins) {
                std::cout << "  block '" << UidText(plugin.uniqueId) << "' (0x" << std::hex << plugin.uniqueId << std::dec << ") at "
                          << plugin.offset << ": " << plugin.records.size() << " records\n";
            }
            failed += walk.result != CoSaveFile::Result::Ok ? 1 : 0;
        }
        return failed == 0 ? 0 : 1;
    }

    int NifValues(int a_argc, char** a_argv)
    {
        int failed = 0;
        for (int i = 2; i < a_argc; ++i) {
            Bytes bytes;
            const bool opened = ReadWhole(a_argv[i], bytes);
            const auto read = NifValueNodes::Read(bytes.data(), bytes.size());
            std::cout << a_argv[i] << ":";
            if (!opened || !read.parsed) {
                std::cout << " NOT PARSED (" << (opened ? read.why : std::string("does not open")) << ")";
                ++failed;
            }
            for (const auto value : read.values) {
                std::cout << " " << value;
            }
            std::cout << "\n";
        }
        return failed == 0 ? 0 : 1;
    }
}
