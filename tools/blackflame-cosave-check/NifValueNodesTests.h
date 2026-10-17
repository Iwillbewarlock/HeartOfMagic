#pragma once

// =============================================================================
// NIF VALUE NODES TESTS - plugins/BlackFlame/src/NifValueNodes.h on NIF headers built per nif.xml (20.2.0.7, SSE bs 100)
// =============================================================================
// Included by main.cpp. A real-file check is --nif-values <file.nif>... (prints each NIF's values).

#include "../../plugins/BlackFlame/src/NifValueNodes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace NifValueNodesTests
{
    namespace NV = BlackFlame::NifValueNodes;
    using Bytes = std::vector<unsigned char>;

    template <class T>
    void Put(Bytes& a_out, T a_value)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&a_value);
        a_out.insert(a_out.end(), bytes, bytes + sizeof(T));
    }

    inline void PutSized(Bytes& a_out, const std::string& a_text)
    {
        Put(a_out, static_cast<std::uint32_t>(a_text.size()));
        a_out.insert(a_out.end(), a_text.begin(), a_text.end());
    }

    inline void PutExport(Bytes& a_out, const std::string& a_text)
    {
        a_out.push_back(static_cast<unsigned char>(a_text.size() + 1));
        a_out.insert(a_out.end(), a_text.begin(), a_text.end());
        a_out.push_back(0);
    }

    struct Block
    {
        std::uint16_t type;
        Bytes body;
    };

    // A 20.2.0.7 NIF: header, block types, the blocks, a footer
    inline Bytes Nif(std::uint32_t a_bsVersion, const std::vector<std::string>& a_types, const std::vector<Block>& a_blocks,
        std::uint32_t a_version = NV::kVersion20207)
    {
        Bytes out;
        const std::string line = "Gamebryo File Format, Version 20.2.0.7\n";
        out.insert(out.end(), line.begin(), line.end());
        Put(out, a_version);
        out.push_back(NV::kLittleEndian);
        Put(out, std::uint32_t{ 12 });
        Put(out, static_cast<std::uint32_t>(a_blocks.size()));
        Put(out, a_bsVersion);
        PutExport(out, "author");
        if (a_bsVersion > NV::kBsHasUnknownInt) {
            Put(out, std::uint32_t{ 0 });
        }
        if (a_bsVersion < NV::kBsHasProcessScript) {
            PutExport(out, "process");
        }
        PutExport(out, "export");
        if (a_bsVersion >= NV::kBsHasMaxFilepath) {
            PutExport(out, "max");
        }
        Put(out, static_cast<std::uint16_t>(a_types.size()));
        for (const auto& type : a_types) {
            PutSized(out, type);
        }
        for (const auto& block : a_blocks) {
            Put(out, block.type);
        }
        for (const auto& block : a_blocks) {
            Put(out, static_cast<std::uint32_t>(block.body.size()));
        }
        Put(out, std::uint32_t{ 2 });  // strings
        Put(out, std::uint32_t{ 12 });
        PutSized(out, "AddOnNode9");
        PutSized(out, "Scene Root");
        Put(out, std::uint32_t{ 0 });  // groups
        for (const auto& block : a_blocks) {
            out.insert(out.end(), block.body.begin(), block.body.end());
        }
        Put(out, std::uint32_t{ 1 });  // footer: one root
        Put(out, std::uint32_t{ 0 });
        return out;
    }

    // A BSValueNode body: some NiNode bytes, then the value and the flags
    inline Bytes ValueNode(std::int32_t a_value)
    {
        Bytes body(60, 0x11);
        Put(body, a_value);
        body.push_back(0x01);
        return body;
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        const std::vector<std::string> types{ "NiNode", "BSValueNode", "NiTriShape" };
        const std::vector<Block> blocks{ { 0, Bytes(40, 0) }, { 1, ValueNode(5) }, { 2, Bytes(9, 0) }, { 1 | 0x8000, ValueNode(8044) } };
        const auto sse = Nif(100, types, blocks);
        const auto read = NV::Read(sse.data(), sse.size());
        a_check(read.parsed && read.blocks == 4 && read.values == std::vector<std::int32_t>{ 5, 8044 },
            "nif values: an SSE NIF (bs 100) gives its two BSValueNode values in block order (a flagged type index too) " + read.why);
        const auto le = Nif(83, types, blocks);
        const auto fo = Nif(130, types, blocks);
        const auto readLe = NV::Read(le.data(), le.size());
        const auto readFo = NV::Read(fo.data(), fo.size());
        a_check(readLe.parsed && readLe.values.size() == 2 && readFo.parsed && readFo.values.size() == 2,
            "nif values: an LE header (bs 83, process script) and a bs 130 header (max filepath) read the same blocks");
        const auto none = Nif(100, { "NiNode" }, { { 0, Bytes(10, 0) } });
        const auto readNone = NV::Read(none.data(), none.size());
        a_check(readNone.parsed && readNone.values.empty() && readNone.valueNodes == 0, "nif values: a NIF without BSValueNode gives none");

        int cutRefused = 0;
        const std::size_t headerAndBlocks = sse.size() - 2 * sizeof(std::uint32_t);  // the footer is not read
        for (std::size_t n = 0; n < headerAndBlocks; ++n) {
            cutRefused += NV::Read(sse.data(), n).parsed ? 0 : 1;
        }
        a_check(cutRefused == static_cast<int>(headerAndBlocks),
            "nif values: cut at every byte before the footer (" + std::to_string(headerAndBlocks) + "): every read refused");
        const auto v1 = Nif(100, types, blocks, 0x14000005);
        auto badType = blocks;
        badType[0].type = 7;
        const auto bt = Nif(100, types, badType);
        auto tiny = blocks;
        tiny[1].body = Bytes(3, 0);
        const auto ti = Nif(100, types, tiny);
        const Bytes text{ 'n', 'o', 't', ' ', 'a', ' ', 'n', 'i', 'f' };
        a_check(!NV::Read(v1.data(), v1.size()).parsed && !NV::Read(bt.data(), bt.size()).parsed && !NV::Read(ti.data(), ti.size()).parsed &&
                    !NV::Read(text.data(), text.size()).parsed && !NV::Read(nullptr, 0).parsed,
            "nif values: refused: version 20.0.0.5, a type index past the list, a BSValueNode block of 3 bytes, not a NIF, nothing");
    }
}
