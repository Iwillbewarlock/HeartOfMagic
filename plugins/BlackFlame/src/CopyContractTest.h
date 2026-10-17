#pragma once

// =============================================================================
// COPY CONTRACT TEST - the shared test table (keys, pool, copy paths; the progress line in CopyContractTestProgress.h)
// =============================================================================
// Run by tools/blackflame-cosave-check --self-test and the builder's --self-test, so both sides check the same frozen
// contract against the same code (included, not copied). Not compiled into the DLL. a_check is any callable taking
// (bool ok, const std::string& what). The golden strings below are the contract: a change to them is a contract change
// (bump kContractVersion and say so in both sides' notes).

#include "CopyContract.h"
#include "CopyContractTestProgress.h"

#include <clocale>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BlackFlame::CopyContractTest
{
    namespace Detail
    {
        namespace CC = CopyContract;

        inline constexpr const char* kGoldenNif =
            "format=BFK2|exe=0123456789abcdef|tint=0.600000|tintadd=0.900000|palette=derived|grad=1111111111111111|"
            "gradadd=missing|kind=muzzle|nif=meshes\\magic\\fx\\fireboltmuzzle.nif|nifhash=aaaaaaaaaaaaaaaa|"
            "tex=textures\\effects\\fxfire01.dds=2222222222222222|tex=textures\\effects\\gradientfire.dds=notdumped|addn=none";
        inline constexpr const char* kGoldenNifKey16 = "c6a8faaade4c4789";
        inline constexpr const char* kGoldenTxst =
            "format=BFK2|exe=0123456789abcdef|tint=0.600000|tintadd=0.900000|palette=derived|grad=1111111111111111|"
            "gradadd=missing|kind=txst|slot0=415c622e646473|slot1=|slot2=|slot3=672e646473|slot4=|slot5=|slot6=|slot7=|"
            "tx00=3333333333333333|tx03=missing|dodt=1";
        inline constexpr const char* kGoldenTxstKey16 = "29c0c5300f827de9";
        inline constexpr const char* kGoldenAddn =
            "format=BFA1|addn=3|src=dawnguard.esm:1abc|modelraw=613c622e6e6966|modelhash=missing|addn=9|src=skyrim.esm:2f|"
            "model=meshes\\mps\\fxfiremps.nif|modelhash=4444444444444444|slot=f00|src=embers xd.esp:812|slot=f01|"
            "src=skyrim.esm:2f";
        inline constexpr const char* kGoldenAddnHash = "fa83c888def48188";
        inline constexpr const char* kKey16 = "0123456789abcdef";

        [[nodiscard]] inline CC::KeyPrefix Prefix()
        {
            CC::KeyPrefix prefix;
            prefix.builderExeFnv64 = 0x0123456789ABCDEFull;
            prefix.tint = 0.6;
            prefix.tintAdd = 0.9;
            prefix.paletteMode = CC::PaletteMode::kDerived;
            prefix.gradBlackFlame = CC::FileHash::Hashed(0x1111111111111111ull);
            prefix.gradBlackFlameAdd = CC::FileHash::Missing();
            return prefix;
        }

        [[nodiscard]] inline CC::NifKeyInput Muzzle()
        {
            CC::NifKeyInput input;
            input.kind = CC::NifKind::kMuzzle;
            input.path = "Magic/FX/FireBoltMuzzle.NIF";
            input.nif = CC::FileHash::Hashed(0xAAAAAAAAAAAAAAAAull);
            input.textures = { { "Textures\\Effects\\FXFire01.dds", CC::FileHash::Hashed(0x2222222222222222ull) },
                { "effects\\gradientfire.dds", CC::FileHash::NotDumped() },
                { "textures/effects/FXFire01.DDS", CC::FileHash::Hashed(0x2222222222222222ull) } };
            return input;
        }

        [[nodiscard]] inline CC::TxstKeyInput Txst()
        {
            CC::TxstKeyInput input;
            input.slotsRaw = { "A\\b.dds", "", "", "g.dds", "", "", "", "" };
            input.tx00 = CC::FileHash::Hashed(0x3333333333333333ull);
            input.tx03 = CC::FileHash::Missing();
            input.dodt = true;
            return input;
        }

        [[nodiscard]] inline std::vector<CC::AddnRecord> Addns()
        {
            return { { 9, "Skyrim.esm", 0x2F, "MPS/FXFireMPS.nif", CC::FileHash::Hashed(0x4444444444444444ull) },
                { 3, "Dawnguard.esm", 0x1ABC, "a<b.nif", CC::FileHash::Missing() } };
        }

        [[nodiscard]] inline std::vector<CC::LedgerPair> Pairs()
        {
            return { { 0xF01, "Skyrim.esm", 0x2F }, { 0xF00, "Embers XD.esp", 0x812 } };
        }

        [[nodiscard]] inline std::string KeyOr(const std::optional<std::string>& a_text)
        {
            return a_text ? CC::Key16Of(*a_text) : std::string("refused");
        }

        template <class Check>
        void CheckBasics(Check& a_check)
        {
            a_check(CC::Hex16(CC::Fnv64("foobar")) == "85944171f73967e8" && CC::Hex16(CC::Fnv64("")) == "cbf29ce484222325",
                "fnv64: published vectors ('', 'foobar') as 16 lower-case hex digits");
            a_check(CC::ParseHex16("0123456789abcdef") == 0x0123456789ABCDEFull && !CC::ParseHex16("0123456789ABCDEF") &&
                        !CC::ParseHex16("0123456789abcde") && !CC::ParseHex16("0123456789abcdeg"),
                "hex16: lower case 16 digits only");
            a_check(CC::PoolOf(0x800)->type == CC::SlotType::kPROJ && CC::PoolOf(0x9DF)->type == CC::SlotType::kPROJ &&
                        CC::PoolOf(0x9E0)->type == CC::SlotType::kIPCT && CC::PoolOf(0xBC0)->type == CC::SlotType::kEXPL &&
                        CC::PoolOf(0xD00)->type == CC::SlotType::kARTO && CC::PoolOf(0xDE0)->type == CC::SlotType::kHAZD &&
                        CC::PoolOf(0xEA0)->type == CC::SlotType::kTXST && CC::PoolOf(0xF00)->type == CC::SlotType::kADDN &&
                        CC::PoolOf(0xFCF)->type == CC::SlotType::kADDN && !CC::PoolOf(0xFD0) && !CC::PoolOf(0x7FF),
                "pool: every edge in its type, 0x7FF and 0xFD0 in none");
            a_check(CC::TxstHasDodt(0xEA0) && CC::TxstHasDodt(0xEE7) && !CC::TxstHasDodt(0xEE8) && !CC::TxstHasDodt(0xEFF),
                "pool: TXST 0xEA0-0xEE7 with DODT, 0xEE8-0xEFF without");
            a_check(CC::AddnIndexOfSlot(0xF00) == 40000u && CC::AddnIndexOfSlot(0xFCF) == 40207u && !CC::AddnIndexOfSlot(0xEFF) &&
                        CC::SlotOfAddnIndex(40207) == 0xFCF && !CC::SlotOfAddnIndex(40208) && !CC::SlotOfAddnIndex(39999),
                "pool: ADDN index = 40000 + place, 40000-40207");
        }

        template <class Check>
        void CheckKeys(Check& a_check)
        {
            const auto prefix = Prefix();
            const auto golden = CC::NifKeyString(prefix, Muzzle());
            a_check(golden && *golden == kGoldenNif && CC::Key16Of(*golden) == kGoldenNifKey16,
                "key: the golden muzzle string and key16 (" + (golden ? *golden : std::string("refused")) + ")");
            const auto txst = CC::TxstKeyString(prefix, Txst());
            a_check(txst && *txst == kGoldenTxst && CC::Key16Of(*txst) == kGoldenTxstKey16, "key: the golden TXST string and key16");
            a_check(CC::AddnTableString(Addns(), Pairs()) == kGoldenAddn && CC::Hex16(CC::AddnTableHash(Addns(), Pairs())) == kGoldenAddnHash,
                "key: the golden ADDN table string (sorted, refused model as modelraw) and its hash");

            auto shuffled = Muzzle();
            std::swap(shuffled.textures.front(), shuffled.textures.back());
            shuffled.textures.push_back(shuffled.textures.front());
            a_check(KeyOr(CC::NifKeyString(prefix, Muzzle())) == KeyOr(CC::NifKeyString(prefix, shuffled)) &&
                        KeyOr(CC::NifKeyString(prefix, Muzzle())) == kGoldenNifKey16,
                "key: same input same key; texture order and a texture named twice do not change it");

            auto spelled = Muzzle();
            bool sameSpellings = true;
            for (const auto* path : { "data\\meshes\\magic\\fx\\fireboltmuzzle.nif", "magic\\fx\\fireboltmuzzle.nif",
                     "\\Meshes//Magic\\FX\\FireBoltMuzzle.nif" }) {
                spelled.path = path;
                sameSpellings = sameSpellings && KeyOr(CC::NifKeyString(prefix, spelled)) == kGoldenNifKey16;
            }
            a_check(sameSpellings, "key: path normalisation (data\\, meshes\\, case, '/' and repeated '\\' fold to one key)");

            auto refused = Muzzle();
            refused.path = "..\\x.nif";
            auto rawTexture = Muzzle();
            rawTexture.textures.push_back({ "c:\\x.dds", CC::FileHash::Missing() });
            const auto raw = CC::NifKeyString(prefix, rawTexture);
            a_check(!CC::NifKeyString(prefix, refused) && raw && raw->find("|texraw=633a5c782e646473=missing|") != std::string::npos,
                "key: a refused NIF path gives no key; a refused texture path goes in as texraw=<hex>");

            CC::NifKeyInput model;
            model.kind = CC::NifKind::kModel;
            model.path = "magic\\firebolt.nif";
            model.nif = CC::FileHash::Hashed(0x5555555555555555ull);
            auto muzzle = Muzzle();
            const auto modelBefore = KeyOr(CC::NifKeyString(prefix, model));
            const auto muzzleBefore = KeyOr(CC::NifKeyString(prefix, muzzle));
            muzzle.nif = CC::FileHash::Hashed(0x6666666666666666ull);
            a_check(KeyOr(CC::NifKeyString(prefix, model)) == modelBefore && KeyOr(CC::NifKeyString(prefix, muzzle)) != muzzleBefore,
                "key: a muzzle-only replacement changes the muzzle key, not the PROJ's model key");
            auto asModel = Muzzle();
            asModel.kind = CC::NifKind::kModel;
            auto asMps = Muzzle();
            asMps.kind = CC::NifKind::kMps;
            a_check(KeyOr(CC::NifKeyString(prefix, asModel)) != kGoldenNifKey16 &&
                        KeyOr(CC::NifKeyString(prefix, asMps)) != KeyOr(CC::NifKeyString(prefix, asModel)),
                "key: one NIF as model, muzzle and MPS gives three keys (the kind is in the string)");

            auto txstChanged = Txst();
            txstChanged.tx00 = CC::FileHash::Hashed(0x3333333333333334ull);
            auto txstNoDodt = Txst();
            txstNoDodt.dodt = false;
            auto txstEmpty = Txst();
            txstEmpty.slotsRaw[0].clear();
            auto txstEmptyOther = txstEmpty;
            txstEmptyOther.tx00 = CC::FileHash::Missing();
            a_check(KeyOr(CC::TxstKeyString(prefix, txstChanged)) != kGoldenTxstKey16 &&
                        KeyOr(CC::NifKeyString(prefix, Muzzle())) == kGoldenNifKey16 &&
                        KeyOr(CC::TxstKeyString(prefix, txstNoDodt)) != kGoldenTxstKey16,
                "key: a TXST-only texture replacement (and a DODT change) changes the TXST key and no NIF key");
            a_check(KeyOr(CC::TxstKeyString(prefix, txstEmpty)) == KeyOr(CC::TxstKeyString(prefix, txstEmptyOther)) &&
                        CC::TxstKeyString(prefix, txstEmpty)->find("|tx00=none|") != std::string::npos &&
                        !CC::TxstKeyString(prefix, CC::TxstKeyInput{}),
                "key: an empty TXST slot keys as none whatever its hash; slotsRaw must hold 8 slots");

            auto notDumped = Muzzle();
            notDumped.textures = { { "effects\\x.dds", CC::FileHash::NotDumped() } };
            auto missing = notDumped;
            missing.textures.front().hash = CC::FileHash::Missing();
            const auto notDumpedText = CC::NifKeyString(prefix, notDumped);
            const auto missingText = CC::NifKeyString(prefix, missing);
            auto txstNotDumped = Txst();
            txstNotDumped.tx03 = CC::FileHash::NotDumped();
            a_check(notDumpedText && missingText && *notDumpedText != *missingText &&
                        KeyOr(notDumpedText) != KeyOr(missingText) &&
                        notDumpedText->find("=notdumped|") != std::string::npos && missingText->find("=missing|") != std::string::npos &&
                        KeyOr(CC::TxstKeyString(prefix, txstNotDumped)) != kGoldenTxstKey16,
                "key: a not-dumped texture and a missing one give different strings and keys (NIF and TXST)");

            auto addns = Addns();
            const auto before = CC::AddnTableHash(addns, Pairs());
            addns.front().modelHash = CC::FileHash::Hashed(0x4444444444444445ull);
            const auto after = CC::AddnTableHash(addns, Pairs());
            auto pairs = Pairs();
            pairs.front().localId = 0x30;
            auto plain = Muzzle();
            auto valueNode = Muzzle();
            valueNode.addnTable = before;
            const auto valueBefore = KeyOr(CC::NifKeyString(prefix, valueNode));
            valueNode.addnTable = after;
            a_check(before != after && CC::AddnTableHash(Addns(), pairs) != before &&
                        KeyOr(CC::NifKeyString(prefix, plain)) == kGoldenNifKey16 &&
                        KeyOr(CC::NifKeyString(prefix, valueNode)) != valueBefore,
                "key: an ADDN or ledger pair change re-keys a BSValueNode NIF only (addn=none is unaffected)");
            a_check(CC::HasValueNodeBlock(std::string_view("\x0b\0\0\0BSValueNode", 15)) && !CC::HasValueNodeBlock("NiNode BSValue"),
                "key: the BSValueNode byte scan");

            auto godskin = prefix;
            godskin.paletteMode = CC::PaletteMode::kGodskin;
            auto tinted = prefix;
            tinted.tint = 0.6000004;
            auto rounded = prefix;
            rounded.tint = 0.6000004999;
            a_check(KeyOr(CC::NifKeyString(godskin, Muzzle())) != kGoldenNifKey16 &&
                        KeyOr(CC::NifKeyString(tinted, Muzzle())) == kGoldenNifKey16 &&
                        CC::KeyPrefixString(rounded)->find("|tint=0.600000|") != std::string::npos,
                "key: palette mode in the prefix; tint rounded to 6 decimals (0.6000004 keys as 0.6)");
            auto nan = prefix;
            nan.tintAdd = std::numeric_limits<double>::quiet_NaN();
            a_check(!CC::KeyPrefixString(nan) && !CC::NifKeyString(nan, Muzzle()), "key: a tint that is not finite gives no key");

            const CC::ChildKeyInput source{ "Effects/FXFire01.dds", 0x7777777777777777ull };
            auto otherSource = source;
            otherSource.sourceFnv64 = 0x7777777777777778ull;
            const auto diffuse = KeyOr(CC::TextureKeyString(prefix, CC::TextureRole::kDiffuse, source));
            a_check(diffuse != KeyOr(CC::TextureKeyString(prefix, CC::TextureRole::kGlow, source)) &&
                        diffuse != KeyOr(CC::TextureKeyString(prefix, CC::TextureRole::kDiffuse, otherSource)) &&
                        diffuse != KeyOr(CC::PaletteKeyString(prefix, CC::PaletteBlend::kAdd, source)) &&
                        KeyOr(CC::PaletteKeyString(prefix, CC::PaletteBlend::kAdd, source)) !=
                            KeyOr(CC::PaletteKeyString(prefix, CC::PaletteBlend::kNormal, source)) &&
                        CC::TextureKeyString(prefix, CC::TextureRole::kDiffuse, source)->find(
                            "|kind=texture|role=diffuse|src=textures\\effects\\fxfire01.dds|srchash=7777777777777777") != std::string::npos,
                "key: texture and palette child keys (role, blend and source bytes each change the key)");
        }

        template <class Check>
        void CheckLocale(Check& a_check)
        {
            const char* current = std::setlocale(LC_ALL, nullptr);
            const std::string saved = current ? current : "C";
            bool exercised = false;
            for (const auto* name : { "de-DE", "German_Germany.1252", "de_DE.UTF-8" }) {
                if (std::setlocale(LC_ALL, name)) {
                    char control[32];
                    std::snprintf(control, sizeof(control), "%.6f", 0.6);
                    exercised = std::string(control) == "0,600000";
                    if (exercised) {
                        break;
                    }
                }
            }
            const auto text = CC::KeyPrefixString(Prefix());
            const bool ok = text && text->find("|tint=0.600000|tintadd=0.900000|") != std::string::npos;
            std::setlocale(LC_ALL, saved.c_str());
            a_check(ok, exercised ? "key: floats locale-free (to_chars gives 0.600000 under a comma locale; printf gave 0,600000)" :
                                    "key: floats as 0.600000 (no comma locale on this system: the locale case was not exercised)");
        }

        template <class Check>
        void CheckCopyPaths(Check& a_check)
        {
            namespace Rule = DataPathRule;
            const auto mesh = Rule::CopyPath(Rule::kMeshesRoot, kKey16, "Magic/FX/FireBoltMuzzle.NIF");
            const auto texture = Rule::CopyPath(Rule::kTexturesRoot, kKey16, "data\\Textures\\Effects\\FXFire01.dds");
            a_check(mesh && *mesh == "meshes\\heartofmagic\\blackflame\\copies\\0123456789abcdef\\fireboltmuzzle.nif" && texture &&
                        *texture == "textures\\heartofmagic\\blackflame\\copies\\0123456789abcdef\\fxfire01.dds",
                "path: copies\\<key16>\\<basename> under meshes\\ and textures\\, normalised");
            a_check(!Rule::CopyPath(Rule::kMeshesRoot, "0123456789ABCDEF", "a.nif") &&
                        !Rule::CopyPath(Rule::kMeshesRoot, "0123456789abcde", "a.nif") &&
                        !Rule::CopyPath("sound\\", kKey16, "a.nif") && !Rule::CopyPath(Rule::kMeshesRoot, kKey16, "..\\a.nif") &&
                        !Rule::CopyPath(Rule::kMeshesRoot, kKey16, "magic\\"),
                "path: refused for an upper-case or short key16, another root, a path Normalize refuses, a folder");
            const auto parts = mesh ? Rule::ParseCopyPath(*mesh) : std::nullopt;
            a_check(parts && parts->root == Rule::kMeshesRoot && parts->key16 == kKey16 && parts->basename == "fireboltmuzzle.nif" &&
                        Rule::StagingRelative(*mesh) == *mesh,
                "path: ParseCopyPath reads CopyPath back; the staging-relative path is the Data-relative one");
            const std::string base = "meshes\\heartofmagic\\blackflame\\copies\\";
            a_check(!Rule::ParseCopyPath(base + kKey16 + "\\sub\\a.nif") && !Rule::ParseCopyPath(base + kKey16 + "\\") &&
                        !Rule::ParseCopyPath(base + kKey16 + "\\A.nif") && !Rule::ParseCopyPath(base + "palettes\\add\\a.dds") &&
                        !Rule::ParseCopyPath(base + kKey16 + "\\..\\a.nif") && !Rule::ParseCopyPath("meshes\\magic\\a.nif") &&
                        !Rule::ParseCopyPath(base + "0123456789ABCDEF\\a.nif") && !Rule::StagingRelative("meshes\\a.nif"),
                "path: ParseCopyPath refuses a deeper folder, no basename, upper case, the old palette folder, '..', other "
                "folders, an upper-case key");
            char lead = 0;
            for (int byte = 0x81; byte <= 0xFE && !lead; ++byte) {
                if (Rule::IsLeadByte(static_cast<unsigned char>(byte))) {
                    lead = static_cast<char>(byte);
                }
            }
            if (lead) {
                const std::string source = std::string("magic\\x") + lead + "\\y.nif";  // a double-byte character ending in 0x5C
                const auto dbcs = Rule::CopyPath(Rule::kMeshesRoot, kKey16, source);
                const auto back = dbcs ? Rule::ParseCopyPath(*dbcs) : std::nullopt;
                a_check(back && back->basename == std::string("x") + lead + "\\y.nif",
                    "path: a double-byte character's trail byte 0x5C stays inside the basename");
            } else {
                a_check(true, "path: no double-byte code page here (the trail-byte case was not exercised)");
            }
        }
    }

    /// Every contract case through a_check(bool ok, const std::string& what)
    template <class Check>
    void Run(Check&& a_check)
    {
        Detail::CheckBasics(a_check);
        Detail::CheckKeys(a_check);
        Detail::CheckLocale(a_check);
        Detail::CheckCopyPaths(a_check);
        CheckProgress(a_check);
    }
}
