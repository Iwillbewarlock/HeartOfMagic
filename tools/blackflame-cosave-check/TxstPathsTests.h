#pragma once

// =============================================================================
// TXST PATHS TESTS - plugins/BlackFlame/src/SlotTxstPaths.h: a txst object's deps mapped onto TX00 / TX03
// =============================================================================
// The deps are made the builder's way: the contract's TextureKeyString over the prefix, the normalised slot and its file
// hash, then CopyPath under textures\. Included by main.cpp.

#include "../../plugins/BlackFlame/src/SlotTxstPaths.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TxstPathsTests
{
    namespace CC = BlackFlame::CopyContract;
    namespace TP = BlackFlame::SlotTxstPaths;
    namespace LM = BlackFlame::SlotLedgerModel;
    namespace DP = BlackFlame::DataPathRule;

    inline CC::KeyPrefix Prefix()
    {
        CC::KeyPrefix prefix;
        prefix.builderExeFnv64 = 0x0123456789ABCDEFull;
        prefix.tint = 0.6;
        prefix.tintAdd = 0.9;
        prefix.gradBlackFlame = CC::FileHash::Hashed(0x1111111111111111ull);
        prefix.gradBlackFlameAdd = CC::FileHash::Hashed(0x2222222222222222ull);
        return prefix;
    }

    // The builder's dep for a_raw's copy in a_role
    inline LM::Dep DepOf(CC::TextureRole a_role, const std::string& a_raw, std::uint64_t a_fnv64)
    {
        const auto key16 = TP::CopyKey16(Prefix(), a_role, a_raw, CC::FileHash::Hashed(a_fnv64));
        LM::Dep dep;
        dep.path = key16 ? DP::CopyPath(DP::kTexturesRoot, *key16, std::string(DP::kTexturesRoot) + a_raw).value_or("") : "";
        dep.fnv64 = 0xABCDull;
        return dep;
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        TP::Strings raw{ "Effects\\RuneFire01.dds", "Effects\\RuneFire01_n.dds", "", "Effects\\RuneGlow.dds", "", "", "", "" };
        const auto tx00 = CC::FileHash::Hashed(0x1000);
        const auto tx03 = CC::FileHash::Hashed(0x3000);
        const auto d00 = DepOf(CC::TextureRole::kDiffuse, raw[0], 0x1000);
        const auto d03 = DepOf(CC::TextureRole::kGlow, raw[3], 0x3000);
        a_check(!d00.path.empty() && !d03.path.empty() && d00.path != d03.path, "txst paths: the test deps are copy paths ('" + d00.path + "')");
        const auto check = [&](const char* a_what, auto&& a_run) {  // the result first, then the message with its reason
            std::string why;
            const bool ok = a_run(why);
            a_check(ok, std::string("txst paths: ") + a_what + " (" + why + ")");
        };
        const auto copyOf = [](const LM::Dep& a_dep) { return a_dep.path.substr(DP::kTexturesRoot.size()); };
        check("both colour slots matched in any dep order, the textures root dropped, the other slots the source's", [&](std::string& a_why) {
            const auto out = TP::RecordStrings(Prefix(), raw, tx00, tx03, { d03, d00 }, a_why);
            return out && (*out)[0] == copyOf(d00) && (*out)[3] == copyOf(d03) && (*out)[1] == raw[1] && (*out)[2].empty();
        });
        check("only TX00 copied, TX03 keeps the source's", [&](std::string& a_why) {
            const auto out = TP::RecordStrings(Prefix(), raw, tx00, tx03, { d00 }, a_why);
            return out && (*out)[0] == copyOf(d00) && (*out)[3] == raw[3];
        });
        check("no deps (a fallback with both slots kept) publishes nothing", [&](std::string& a_why) {
            return !TP::RecordStrings(Prefix(), raw, tx00, tx03, {}, a_why) && !a_why.empty();
        });
        const auto stray = DepOf(CC::TextureRole::kDiffuse, "Effects\\Other.dds", 0x1000);
        check("a dep that is the copy of neither slot is refused", [&](std::string& a_why) {
            return !TP::RecordStrings(Prefix(), raw, tx00, tx03, { d00, stray }, a_why) && !a_why.empty();
        });
        LM::Dep addn;
        addn.isAddn = true;
        addn.addn = 40000;
        check("an ADDN dep is refused", [&](std::string& a_why) { return !TP::RecordStrings(Prefix(), raw, tx00, tx03, { d00, addn }, a_why); });
        check("TX00's texture changed since the build (another hash): its copy no longer matches, refused", [&](std::string& a_why) {
            return !TP::RecordStrings(Prefix(), raw, CC::FileHash::Hashed(0x9999), tx03, { d00, d03 }, a_why);
        });
        TP::Strings same = raw;
        same[3] = raw[0];  // one texture in both colour slots: two roles, two keys, two copies
        const auto s00 = DepOf(CC::TextureRole::kDiffuse, same[0], 0x1000);
        const auto s03 = DepOf(CC::TextureRole::kGlow, same[3], 0x1000);
        check("one texture in TX00 and TX03 gives two copies, each slot its own", [&](std::string& a_why) {
            const auto out = TP::RecordStrings(Prefix(), same, tx00, CC::FileHash::Hashed(0x1000), { s00, s03 }, a_why);
            return s00.path != s03.path && out && (*out)[0] == copyOf(s00) && (*out)[3] == copyOf(s03);
        });
        check("a TX00 whose texture is missing this session has no key: its dep is refused", [&](std::string& a_why) {
            return !TP::RecordStrings(Prefix(), raw, CC::FileHash::Missing(), tx03, { d00 }, a_why);
        });
    }
}
