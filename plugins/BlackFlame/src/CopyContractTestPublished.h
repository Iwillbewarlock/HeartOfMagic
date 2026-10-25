#pragma once

// =============================================================================
// COPY CONTRACT TEST PUBLISHED - staging\published.tsv cases of the shared test table (CopyContractTest.h)
// =============================================================================

#include "CopyContractPublished.h"
#include "CopyContractTestProgress.h"

#include <string>

namespace BlackFlame::CopyContractTest
{
    template <class Check>
    void CheckPublished(Check&& a_check)
    {
        namespace CC = CopyContract;
        constexpr std::uint64_t kToken = 0x0123456789abcdefull;
        const std::string nif = ProgressDetail::kNifPath;
        const std::string dds = ProgressDetail::kDdsPath;
        const CC::PublishedList list{ { nif, { 12345, 0x1111111111111111ull } }, { dds, { 7, 0x00000000000000ffull } } };
        const auto text = CC::WritePublished(list, kToken);
        const auto back = text ? CC::ParsePublished(*text) : CC::PublishedParse{};
        a_check(text && back.headerOk && back.token == kToken && back.refused == 0 && back.list == list,
            "published: the writer's file reads back as written (two copy paths, its token)");
        a_check(CC::PublishedHolds(back.list, nif, 0x1111111111111111ull) &&
                    CC::PublishedHolds(back.list, nif, 0x1111111111111111ull, 12345) &&
                    !CC::PublishedHolds(back.list, nif, 0x1111111111111111ull, 12346) &&
                    !CC::PublishedHolds(back.list, nif, 0x2222222222222222ull) && !CC::PublishedHolds(back.list, "meshes\\x.nif", 1),
            "published: holds a listed path only with its fnv64 (and its size when given)");
        const auto empty = CC::WritePublished({}, kToken);
        a_check(empty && *empty == std::string(CC::kPublishedHeader) + "0123456789abcdef\n" && CC::ParsePublished(*empty).headerOk &&
                    CC::ParsePublished(*empty).list.empty(),
            "published: an empty list is the header and token alone, and reads back empty");
        a_check(!CC::WritePublished({ { "meshes\\magic\\fx\\x.nif", { 1, 1 } } }, kToken) && !CC::WritePublished({ { nif, { 0, 1 } } }, kToken) &&
                    !CC::WritePublished({ { nif, { 1, 0 } } }, kToken) && !CC::WritePublished(list, 0),
            "published: the writer refuses a source path, bytes 0, fnv64 0 and token 0");
        const std::string header = std::string(CC::kPublishedHeader) + "0123456789abcdef\n";
        const std::string good = nif + "\t5\t0000000000000005\n";
        a_check(CC::ParsePublished("# blackflame published v2 0123456789abcdef\n" + good).list.empty() && !CC::ParsePublished(good).headerOk &&
                    CC::ParsePublished("").list.empty() && !CC::ParsePublished("# blackflame published v1\n" + good).headerOk &&
                    !CC::ParsePublished(std::string(CC::kPublishedHeader) + "0000000000000000\n" + good).headerOk &&
                    !CC::ParsePublished(std::string(CC::kPublishedHeader) + "0123456789ABCDEF\n" + good).headerOk,
            "published: another version, no header, an empty file, no token, token 0 or an upper-case token takes nothing");
        const auto bad = CC::ParsePublished(header + "meshes\\magic\\x.nif\t5\t0000000000000005\n" + nif + "\t0\t0000000000000005\n" + nif +
                                            "\t5\t0000000000000000\n" + nif + "\t5\t000000000000000G\n" + nif + "\t-5\t0000000000000005\n" +
                                            nif + "\t5x\t0000000000000005\n" + nif + "\t5\t0000000000000005\textra\n" + dds +
                                            "\t5\t0000000000000005");
        a_check(bad.headerOk && bad.list.empty() && bad.refused == 8,
            "published: a source path, bytes 0, fnv64 0, a bad hex, a sign, a size with letters, a fourth field and a cut last line "
            "are each refused");
        const auto twice = CC::ParsePublished(header + good + good + nif + "\t6\t0000000000000006\n" + good + dds + "\t9\t0000000000000009\r\n\n");
        a_check(twice.list.size() == 1 && twice.list.count(dds) != 0 && twice.list.at(dds).bytes == 9 && twice.list.count(nif) == 0,
            "published: a path listed again with the same values is one entry, with other values dropped for good; CRLF and a "
            "blank line are taken");
    }
}
