#pragma once

// =============================================================================
// TXST PATHS TESTS - plugins/BlackFlame/src/SlotTxstPaths.h: a txst object's deps mapped onto TX00 / TX03
// =============================================================================
// The deps are made the builder's way: the contract's TextureKeyString over the prefix, the normalised slot and its file
// hash, then CopyPath under textures\. Included by main.cpp.

#include "../../plugins/BlackFlame/src/CopyContractTest.h"
#include "../../plugins/BlackFlame/src/SlotTxstPaths.h"

#include <algorithm>
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
        prefix.outputIdentity = "1+dev0123456789abcdef";
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

    // review B2: a later session restores a published set from its stored strings, not from this session's key prefix
    template <class Check>
    void RunRestore(Check& a_check, const TP::Strings& a_raw, const CC::FileHash& a_tx00, const CC::FileHash& a_tx03, const LM::Dep& a_d00,
        const LM::Dep& a_d03)
    {
        const auto input = [&](const TP::Strings& a_slots, const CC::FileHash& a_hash00) {
            return CC::TxstKeyInput{ { a_slots.begin(), a_slots.end() }, a_hash00, a_tx03, true };
        };
        std::string why;
        const auto made = TP::RecordStrings(Prefix(), a_raw, a_tx00, a_tx03, { a_d00, a_d03 }, why);
        auto otherPrefix = Prefix();
        otherPrefix.tint = 0.7;          // another tint and builder since the publish: the key prefix differs
        otherPrefix.outputIdentity = "2";
        const auto keyText = CC::TxstKeyString(Prefix(), input(a_raw, a_tx00)).value_or("");
        const auto fields = CC::TxstSourceFields(input(a_raw, a_tx00));
        const std::vector<std::string> stored = made ? std::vector<std::string>(made->begin(), made->end()) : std::vector<std::string>{};
        a_check(made && !keyText.empty() && fields && keyText.size() > fields->size() && keyText.compare(keyText.size() - fields->size(), fields->size(), *fields) == 0 &&
                    *CC::TxstKeyString(otherPrefix, input(a_raw, a_tx00)) != keyText,
            "txst restore: the test's published set, key string and source fields (" + why + ")");
        const auto restore = [&](const char* a_what, bool a_want, const std::string& a_key, const std::optional<std::string>& a_fields,
                                 const std::vector<std::string>& a_stored, const std::vector<LM::Dep>& a_deps) {
            std::string reason;
            const auto out = TP::RestoredStrings(a_key, a_fields, a_raw, a_stored, a_deps, reason);
            const bool same = out && std::equal(out->begin(), out->end(), a_stored.begin(), a_stored.end());
            a_check(a_want ? same : !out && !reason.empty(), std::string("txst restore: ") + a_what + " (" + reason + ")");
        };
        restore("an intact set comes back as stored, whatever this session's prefix (no prefix is read)", true, keyText, fields, stored,
            { a_d03, a_d00 });
        // review round 1 D5: a set published under a BFK3 key (the exe hash in its prefix) restores the same way (README: the
        // key prefix is never compared, only the source's fields at the end of the key string)
        const std::string bfk3 = "format=BFK3|exe=0123456789abcdef|tint=0.6|tintadd=0.9" + fields.value_or("");
        restore("a set published under a BFK3 key string comes back as stored (the prefix is not compared)", true, bfk3, fields, stored,
            { a_d00, a_d03 });
        auto changed = a_raw;
        changed[0] = "Effects\\Other.dds";
        restore("the source's TX00 renamed since: refused", false, keyText, CC::TxstSourceFields(input(changed, a_tx00)), stored,
            { a_d00, a_d03 });
        restore("the source's TX00 bytes changed since (another hash): refused", false, keyText,
            CC::TxstSourceFields(input(a_raw, CC::FileHash::Hashed(0x9999))), stored, { a_d00, a_d03 });
        restore("a stored colour slot naming no dep: refused", false, keyText, fields, stored, { a_d00 });
        auto stray = a_d00;
        stray.path = "textures\\heartofmagic\\blackflame\\copies\\0000000000000000\\x.dds";
        restore("a dep no stored slot names: refused", false, keyText, fields, stored, { a_d00, a_d03, stray });
        auto otherSlot = stored;
        otherSlot[1] = "Effects\\Edited_n.dds";
        restore("a stored non-colour slot that is not the source's: refused", false, keyText, fields, otherSlot, { a_d00, a_d03 });
        restore("nothing stored (an older ledger): refused, the caller makes them again", false, keyText, fields, {}, { a_d00, a_d03 });
        restore("no key string stored: refused", false, "", fields, stored, { a_d00, a_d03 });
    }

    // review B2: the stored strings and the IDs rule through the ledger's JSON text
    template <class Check>
    void RunLedgerJson(Check& a_check)
    {
        LM::Ledger ledger;
        ledger.epoch = 1;
        ledger.version = 1;
        ledger.sessions = 1;
        LM::Entry entry;
        entry.slot = 0xEA0;
        entry.type = CC::SlotType::kTXST;
        entry.plugin = "Skyrim.esm";
        entry.localId = 0x1234;
        LM::Object object;
        object.role = LM::Role::kTxst;
        object.key = 0x55;
        object.keyText = "format=BFK3|kind=txst";
        object.deps = { { false, 0, "textures\\heartofmagic\\blackflame\\copies\\0000000000000055\\a.dds", 0x66 } };
        object.textures = { "heartofmagic\\a.dds", std::string("Effects\\\xB8\xB6_n.dds"), "", "g.dds", "", "", "", "" };
        object.idsRule = "prefixed";
        entry.published.push_back(object);
        LM::Insert(ledger, entry);
        std::string why;
        std::optional<LM::Ledger> back;
        std::string text;
        try {
            text = LM::ToJson(ledger).dump(1);
            back = LM::FromJson(LM::Json::parse(text), why);
        } catch (const LM::Json::exception& e) {
            why = e.what();
        }
        const auto* read = back ? LM::FindSlot(*back, 0xEA0) : nullptr;
        const auto* txst = read ? LM::PublishedOf(*read, LM::Role::kTxst) : nullptr;
        a_check(txst && txst->textures == object.textures && txst->idsRule == "prefixed" && text.find("\"hex\"") != std::string::npos,
            "txst ledger: the stored strings (one in the code page's bytes, written as hex) and the IDs rule read back (" + why + ")");
        const auto refused = [&](const char* a_what, auto&& a_spoil) {
            auto json = LM::ToJson(ledger);
            a_spoil(json["slots"][0]["published"][0]);
            std::string reason;
            a_check(!LM::FromJson(json, reason) && !reason.empty(), std::string("txst ledger: ") + a_what + " refuses the file (" + reason + ")");
        };
        refused("seven strings", [](LM::Json& a_p) { a_p["textures"].erase(a_p["textures"].size() - 1); });
        refused("a string that is a number", [](LM::Json& a_p) { a_p["textures"][0] = 7; });
        refused("strings with no idsRule", [](LM::Json& a_p) { a_p.erase("idsRule"); });
        auto old = LM::ToJson(ledger);
        old["slots"][0]["published"][0].erase("textures");
        old["slots"][0]["published"][0].erase("idsRule");
        std::string oldWhy;
        const auto oldBack = LM::FromJson(old, oldWhy);
        const auto* oldEntry = oldBack ? LM::FindSlot(*oldBack, 0xEA0) : nullptr;
        const auto* oldTxst = oldEntry ? LM::PublishedOf(*oldEntry, LM::Role::kTxst) : nullptr;
        a_check(oldTxst && oldTxst->textures.empty() && oldTxst->idsRule.empty(),
            "txst ledger: an older file without the strings is read (they stay empty: W1 makes them again) (" + oldWhy + ")");
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

        // The anchor the builder's self-test checks too (CopyContractTest.h): the DLL keys a texture copy as the builder does
        namespace CT = BlackFlame::CopyContractTest::Detail;
        const auto anchor = TP::CopyKey16(CT::Prefix(), CC::TextureRole::kDiffuse, "Effects\\FXFire01.dds",
            CC::FileHash::Hashed(CT::kAnchorTextureFnv64));
        a_check(anchor && *anchor == CT::kGoldenTextureKey16, "txst paths: CopyKey16 gives the contract's golden texture key16, now " +
                                                                  anchor.value_or("none"));

        RunRestore(a_check, raw, tx00, tx03, d00, d03);
        RunLedgerJson(a_check);
    }
}
