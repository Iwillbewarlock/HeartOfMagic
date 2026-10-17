#pragma once

// =============================================================================
// COPY CONTRACT TEST - the shared test table (keys and tints here; copy paths and the progress line in
// CopyContractTestPaths.h and CopyContractTestProgress.h)
// =============================================================================
// Run by tools/blackflame-cosave-check --self-test and the builder's --self-test, so both sides check the same frozen
// contract against the same code (included, not copied). Not compiled into the DLL. a_check is any callable taking
// (bool ok, const std::string& what); a_skip one taking (const std::string& what) for a case this system cannot run.
// Run returns how many checks it made; each caller fails its run below kMinChecks. The golden strings are the
// contract: a change to them is a contract change (bump kContractVersion). Their FNVs were computed apart from this
// code (Node BigInt FNV-1a, checked against the published vectors).

#include "CopyContractKeys.h"
#include "CopyContractTestPaths.h"
#include "CopyContractTestProgress.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BlackFlame::CopyContractTest
{
    inline constexpr int kMinChecks = 53;  // every check the table makes today but the one that may SKIP (the locale)

    namespace Detail
    {
        namespace CC = CopyContract;

        inline constexpr const char* kGoldenPrefix =
            "format=BFK3|exe=0123456789abcdef|tint=0.600000|tintadd=0.900000|palette=derived|grad=1111111111111111|"
            "gradadd=missing";
        inline constexpr const char* kGoldenNif =
            "format=BFK3|exe=0123456789abcdef|tint=0.600000|tintadd=0.900000|palette=derived|grad=1111111111111111|"
            "gradadd=missing|kind=muzzle|nif=meshes\\magic\\fx\\fireboltmuzzle.nif|nifhash=aaaaaaaaaaaaaaaa|"
            "tex=textures\\effects\\fxfire01.dds=2222222222222222|tex=textures\\effects\\gradientfire.dds=notdumped:4096|"
            "addn=none";
        inline constexpr const char* kGoldenNifKey16 = "9534b61f5b30e53d";
        inline constexpr const char* kGoldenTxst =
            "format=BFK3|exe=0123456789abcdef|tint=0.600000|tintadd=0.900000|palette=derived|grad=1111111111111111|"
            "gradadd=missing|kind=txst|slot0=415c622e646473|slot1=|slot2=|slot3=672e646473|slot4=|slot5=|slot6=|slot7=|"
            "tx00=3333333333333333|tx03=missing|dodt=1";
        inline constexpr const char* kGoldenTxstKey16 = "8a1ce45bf83eee1e";
        inline constexpr const char* kGoldenAddn =
            "format=BFA2|addn=3|src=dawnguard.esm:1abb|model=meshes\\mps\\b.nif|modelhash=5555555555555555|addn=3|"
            "src=dawnguard.esm:1abc|modelraw=613c622e6e6966|modelhash=missing|addn=9|src=skyrim.esm:2f|"
            "model=meshes\\mps\\fxfiremps.nif|modelhash=4444444444444444|slot=f00|src=embers xd.esp:812|slot=f01|"
            "src=skyrim.esm:2f";
        inline constexpr const char* kGoldenAddnHash = "5af0ea9557c3eda3";

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
                { "effects\\gradientfire.dds", CC::FileHash::NotDumped(4096) },
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
                { 3, "Dawnguard.esm", 0x1ABC, "a<b.nif", CC::FileHash::Missing() },
                { 3, "Dawnguard.esm", 0x1ABB, "mps\\b.nif", CC::FileHash::Hashed(0x5555555555555555ull) } };
        }

        [[nodiscard]] inline std::vector<CC::LedgerPair> Pairs()
        {
            return { { 0xF01, "Skyrim.esm", 0x2F }, { 0xF00, "Embers XD.esp", 0x812 } };
        }

        /// Both made a key and the keys differ / are the same (a refusal never counts as either)
        [[nodiscard]] inline bool Differs(const std::optional<std::string>& a_lhs, const std::optional<std::string>& a_rhs)
        {
            return a_lhs && a_rhs && CC::Key16Of(*a_lhs) != CC::Key16Of(*a_rhs);
        }

        [[nodiscard]] inline bool Same(const std::optional<std::string>& a_lhs, const std::optional<std::string>& a_rhs)
        {
            return a_lhs && a_rhs && *a_lhs == *a_rhs;
        }

        [[nodiscard]] inline bool Contains(const std::optional<std::string>& a_text, std::string_view a_needle)
        {
            return a_text && a_text->find(a_needle) != std::string::npos;
        }

        // Restores the C locale's name however the block ends
        class LocaleGuard
        {
        public:
            LocaleGuard()
            {
                const char* current = std::setlocale(LC_ALL, nullptr);
                m_saved = current ? current : "C";
            }
            ~LocaleGuard() { std::setlocale(LC_ALL, m_saved.c_str()); }
            LocaleGuard(const LocaleGuard&) = delete;
            LocaleGuard& operator=(const LocaleGuard&) = delete;

        private:
            std::string m_saved;
        };

        template <class Check, class Skip>
        void CheckTints(Check& a_check, Skip& a_skip)
        {
            a_check(CC::TintText(0.6) == "0.600000" && CC::TintText(0.6000006) == "0.600001" && CC::TintText(0.6000004) == "0.600000" &&
                        CC::TintText(-0.0) == "0.000000" && CC::TintText(-0.0000001) == "0.000000" && CC::TintText(16.0) == "16.000000" &&
                        CC::TintText(-16.0) == "-16.000000",
                "tint: to_chars fixed 6 decimals rounds (0.6000006 -> 0.600001), -0 and -0.0000001 as 0.000000, +-16 taken");
            a_check(!CC::TintText(16.000001) && !CC::TintText(std::numeric_limits<double>::quiet_NaN()) &&
                        !CC::TintText(std::numeric_limits<double>::infinity()) && !CC::TintText(-std::numeric_limits<double>::infinity()) &&
                        !CC::TintText(1e300),
                "tint: |tint| > 16, NaN and +-inf refused");
            bool roundTrips = true;
            for (const double value : { 0.6, 0.9, 0.1234567, 1.9999995, 0.0000005, -0.4, 2.0 }) {
                const auto quantized = CC::QuantizedTint(value);
                roundTrips = roundTrips && quantized && CC::TintText(*quantized) == CC::TintText(value) &&
                             CC::QuantizedTint(*quantized) == quantized;
            }
            const auto zero = CC::QuantizedTint(-0.0);
            a_check(roundTrips && zero && *zero == 0.0 && !std::signbit(*zero) && CC::QuantizedTint(0.1234567) == 0.123457,
                "tint: QuantizedTint gives the text's own value (same text again, idempotent), -0 as +0");

            const LocaleGuard guard;
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
            if (!exercised) {
                a_skip("tint: no comma locale on this system (the locale case was not run)");
                return;
            }
            a_check(Contains(CC::KeyPrefixString(Prefix()), "|tint=0.600000|tintadd=0.900000|"),
                "tint: locale-free (0.600000 under a comma locale where printf gave 0,600000)");
        }

        template <class Check>
        void CheckPrefix(Check& a_check)
        {
            const auto prefix = Prefix();
            a_check(CC::KeyPrefixString(prefix) == kGoldenPrefix, "prefix: the golden prefix string");
            const auto verify = [&](double a_tint, std::string_view a_mode, std::uint64_t a_exe, const CC::FileHash& a_grad) {
                return CC::VerifyKeyPrefix(kGoldenPrefix, a_tint, 0.9, a_mode, a_exe, a_grad, CC::FileHash::Missing());
            };
            const auto grad = CC::FileHash::Hashed(0x1111111111111111ull);
            a_check(verify(0.6, "derived", 0x0123456789ABCDEFull, grad) && verify(0.6000004, "derived", 0x0123456789ABCDEFull, grad),
                "prefix: VerifyKeyPrefix takes the builder's own rebuild of the DLL's keyPrefix (the same 6-decimal text)");
            a_check(!verify(0.61, "derived", 0x0123456789ABCDEFull, grad) && !verify(0.6, "godskin", 0x0123456789ABCDEFull, grad) &&
                        !verify(0.6, "Derived", 0x0123456789ABCDEFull, grad) && !verify(0.6, "derived", 0x0123456789ABCDEEull, grad) &&
                        !verify(0.6, "derived", 0x0123456789ABCDEFull, CC::FileHash::Hashed(1)) &&
                        !verify(0.6, "derived", 0x0123456789ABCDEFull, CC::FileHash{}),
                "prefix: VerifyKeyPrefix refuses another tint, mode, unknown mode text, exe, palette hash or an unset hash");
            auto unknownMode = prefix;
            unknownMode.paletteMode = static_cast<CC::PaletteMode>(9);
            auto unsetGrad = prefix;
            unsetGrad.gradBlackFlame = CC::FileHash{};
            auto badState = prefix;
            badState.gradBlackFlameAdd.state = static_cast<CC::FileState>(9);
            auto nanTint = prefix;
            nanTint.tint = std::numeric_limits<double>::quiet_NaN();
            auto infAdd = prefix;
            infAdd.tintAdd = std::numeric_limits<double>::infinity();
            a_check(!CC::KeyPrefixString(unknownMode) && !CC::KeyPrefixString(unsetGrad) && !CC::KeyPrefixString(badState) &&
                        !CC::KeyPrefixString(nanTint) && !CC::KeyPrefixString(infAdd) && !CC::NifKeyString(nanTint, Muzzle()),
                "prefix: an unknown palette mode, an unset or unknown file state, a NaN tint, an infinite tintAdd: no key");
        }

        template <class Check>
        void CheckNifKeys(Check& a_check)
        {
            const auto prefix = Prefix();
            const auto golden = CC::NifKeyString(prefix, Muzzle());
            a_check(golden && *golden == kGoldenNif && CC::Key16Of(*golden) == kGoldenNifKey16,
                "key: the golden muzzle string and key16 (" + golden.value_or("refused") + ")");
            auto rotated = Muzzle();
            std::rotate(rotated.textures.begin(), rotated.textures.begin() + 1, rotated.textures.end());
            auto twice = Muzzle();
            const auto extra = twice.textures[1];
            twice.textures.push_back(extra);
            a_check(Same(CC::NifKeyString(prefix, rotated), golden) && Same(CC::NifKeyString(prefix, twice), golden),
                "key: texture order and a texture named twice with one state do not change it");
            auto conflict = Muzzle();
            conflict.textures.push_back({ "effects\\GradientFire.dds", CC::FileHash::Missing() });
            auto conflictOther = conflict;
            std::reverse(conflictOther.textures.begin(), conflictOther.textures.end());
            a_check(!CC::NifKeyString(prefix, conflict) && !CC::NifKeyString(prefix, conflictOther),
                "key: one texture given two states is refused in either order");
            for (const auto* path : { "data\\meshes\\magic\\fx\\fireboltmuzzle.nif", "magic\\fx\\fireboltmuzzle.nif",
                     "\\Meshes//Magic\\FX\\FireBoltMuzzle.nif" }) {
                auto spelled = Muzzle();
                spelled.path = path;
                a_check(Same(CC::NifKeyString(prefix, spelled), golden), std::string("key: path normalisation, ") + path);
            }
            auto refused = Muzzle();
            refused.path = "..\\x.nif";
            auto rawTexture = Muzzle();
            rawTexture.textures.push_back({ "c:\\x.dds", CC::FileHash::Missing() });
            a_check(!CC::NifKeyString(prefix, refused) && Contains(CC::NifKeyString(prefix, rawTexture), "|texraw=633a5c782e646473=missing|"),
                "key: a refused NIF path gives no key; a refused texture path goes in as texraw=<hex>");
            auto highBytes = Muzzle();
            highBytes.textures.push_back({ std::string("effects\\\x81|x.dds"), CC::FileHash::Missing() });  // '|' as a trail byte
            const auto high = CC::NifKeyString(prefix, highBytes);
            const bool ascii = high && std::all_of(high->begin(), high->end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
            a_check(ascii && (Contains(high, "|texhex=") || Contains(high, "|texraw=")) && !Contains(high, "\x81|"),
                "key: a path with a code page's bytes goes in as hex, so a trail byte '|' never reaches the string");

            CC::NifKeyInput model;
            model.kind = CC::NifKind::kModel;
            model.path = "magic\\firebolt.nif";
            model.nif = CC::FileHash::Hashed(0x5555555555555555ull);
            auto muzzle = Muzzle();
            const auto muzzleBefore = CC::NifKeyString(prefix, muzzle);
            muzzle.nif = CC::FileHash::Hashed(0x6666666666666666ull);
            const auto modelText = CC::NifKeyString(prefix, model);  // the PROJ model key holds nothing of its muzzle
            a_check(modelText && !Contains(modelText, "fireboltmuzzle") && !Contains(modelText, "aaaaaaaaaaaaaaaa") &&
                        Contains(muzzleBefore, "fireboltmuzzle") && Differs(CC::NifKeyString(prefix, muzzle), muzzleBefore),
                "key: a PROJ's model and muzzle are two objects: a muzzle-only replacement re-keys only the muzzle");
            auto asModel = Muzzle();
            asModel.kind = CC::NifKind::kModel;
            auto asMps = Muzzle();
            asMps.kind = CC::NifKind::kMps;
            auto unknownKind = Muzzle();
            unknownKind.kind = static_cast<CC::NifKind>(9);
            a_check(Differs(CC::NifKeyString(prefix, asModel), golden) && Differs(CC::NifKeyString(prefix, asMps), golden) &&
                        Differs(CC::NifKeyString(prefix, asMps), CC::NifKeyString(prefix, asModel)) &&
                        !CC::NifKeyString(prefix, unknownKind),
                "key: one NIF as model, muzzle and MPS gives three keys; an unknown kind gives none");
            auto unsetNif = Muzzle();
            unsetNif.nif = CC::FileHash{};
            auto unsetTexture = Muzzle();
            unsetTexture.textures.front().hash = CC::FileHash{};
            a_check(!CC::NifKeyString(prefix, unsetNif) && !CC::NifKeyString(prefix, unsetTexture),
                "key: an unset NIF or texture hash gives no key");

            auto notDumped = Muzzle();
            notDumped.textures = { { "effects\\x.dds", CC::FileHash::NotDumped(100) } };
            auto otherSize = notDumped;
            otherSize.textures.front().hash = CC::FileHash::NotDumped(101);
            auto missing = notDumped;
            missing.textures.front().hash = CC::FileHash::Missing();
            const auto notDumpedText = CC::NifKeyString(prefix, notDumped);
            a_check(Differs(notDumpedText, CC::NifKeyString(prefix, missing)) && Contains(notDumpedText, "=notdumped:100|") &&
                        Contains(CC::NifKeyString(prefix, missing), "=missing|") && Differs(notDumpedText, CC::NifKeyString(prefix, otherSize)),
                "key: not dumped (with its size) and missing differ; a not-dumped file of another size re-keys");
        }

        template <class Check>
        void CheckOtherKeys(Check& a_check)
        {
            const auto prefix = Prefix();
            const auto txst = CC::TxstKeyString(prefix, Txst());
            a_check(txst && *txst == kGoldenTxst && CC::Key16Of(*txst) == kGoldenTxstKey16, "key: the golden TXST string and key16");
            auto txstChanged = Txst();
            txstChanged.tx00 = CC::FileHash::Hashed(0x3333333333333334ull);
            auto txstNoDodt = Txst();
            txstNoDodt.dodt = false;
            auto txstNotDumped = Txst();
            txstNotDumped.tx03 = CC::FileHash::NotDumped(7);
            a_check(Differs(CC::TxstKeyString(prefix, txstChanged), txst) && Differs(CC::TxstKeyString(prefix, txstNoDodt), txst) &&
                        Differs(CC::TxstKeyString(prefix, txstNotDumped), txst),
                "key: a TXST texture replacement, a DODT change and not dumped vs missing each re-key the TXST");
            auto empty = Txst();
            empty.slotsRaw[0].clear();
            auto emptyOther = empty;
            emptyOther.tx00 = CC::FileHash{};
            auto unset = Txst();
            unset.tx00 = CC::FileHash{};
            auto seven = Txst();
            seven.slotsRaw.pop_back();
            auto nine = Txst();
            nine.slotsRaw.emplace_back();
            a_check(Same(CC::TxstKeyString(prefix, empty), CC::TxstKeyString(prefix, emptyOther)) &&
                        Contains(CC::TxstKeyString(prefix, empty), "|tx00=none|") && !CC::TxstKeyString(prefix, unset) &&
                        !CC::TxstKeyString(prefix, seven) && !CC::TxstKeyString(prefix, nine),
                "key: an empty TXST slot keys as none whatever its hash; a filled one needs a set hash; 7 or 9 slots refused");

            auto records = Addns();
            const auto table = CC::AddnTableString(records, Pairs());
            std::reverse(records.begin(), records.end());
            auto pairs = Pairs();
            std::reverse(pairs.begin(), pairs.end());
            a_check(table == kGoldenAddn && CC::AddnTableString(records, pairs) == kGoldenAddn &&
                        CC::AddnTableHash(Addns(), Pairs()) == CC::ParseHex16(kGoldenAddnHash),
                "key: the golden ADDN table (two records of one index, a refused model as modelraw) in both input orders");
            auto changed = Addns();
            changed.front().modelHash = CC::FileHash::Hashed(0x4444444444444445ull);
            auto pairChanged = Pairs();
            pairChanged.front().localId = 0x30;
            auto valueNode = Muzzle();
            valueNode.addnTable = CC::AddnTableHash(Addns(), Pairs());
            auto valueNodeChanged = Muzzle();
            valueNodeChanged.addnTable = CC::AddnTableHash(changed, Pairs());
            a_check(Differs(CC::AddnTableString(changed, Pairs()), table) && Differs(CC::AddnTableString(Addns(), pairChanged), table) &&
                        Differs(CC::NifKeyString(prefix, valueNode), CC::NifKeyString(prefix, valueNodeChanged)) &&
                        Differs(CC::NifKeyString(prefix, valueNode), CC::NifKeyString(prefix, Muzzle())),
                "key: an ADDN record or ledger pair change re-keys a NIF that takes the ADDN part (addn=none takes none)");
            auto badId = Addns();
            badId.front().localId = 0x1000000;
            auto slashes = Addns();
            slashes.front().plugin = "A/B.esp";
            auto backslashes = Addns();
            backslashes.front().plugin = "a\\b.esp";
            auto bar = Addns();
            bar.front().plugin = "x|slot=1.esp";
            auto unsetModel = Addns();
            unsetModel.front().modelHash = CC::FileHash{};
            a_check(!CC::AddnTableString(badId, Pairs()) && !CC::AddnTableString(unsetModel, Pairs()) &&
                        Differs(CC::AddnTableString(slashes, Pairs()), CC::AddnTableString(backslashes, Pairs())) &&
                        Contains(CC::AddnTableString(bar, Pairs()), "|srchex=787c736c6f743d312e657370:2f|"),
                "key: a local id over 0xFFFFFF or an unset model hash refused; plugin names ASCII-lowered only, odd ones as srchex");
            a_check(CC::HasValueNodeBlock(std::string_view("\x0b\0\0\0BSValueNode", 15)) &&
                        !CC::HasValueNodeBlock(std::string_view("\x0b\0\0\0BSValueNod", 14)) && !CC::HasValueNodeBlock("NiNode"),
                "key: the BSValueNode byte scan (a cut name is no match)");

            const CC::ChildKeyInput source{ "Effects/FXFire01.dds", 0x7777777777777777ull };
            auto otherSource = source;
            otherSource.sourceFnv64 = 0x7777777777777778ull;
            const auto diffuse = CC::TextureKeyString(prefix, CC::TextureRole::kDiffuse, source);
            a_check(Differs(diffuse, CC::TextureKeyString(prefix, CC::TextureRole::kGlow, source)) &&
                        Differs(diffuse, CC::TextureKeyString(prefix, CC::TextureRole::kDiffuse, otherSource)) &&
                        Differs(diffuse, CC::PaletteKeyString(prefix, CC::PaletteBlend::kAdd, source)) &&
                        Differs(CC::PaletteKeyString(prefix, CC::PaletteBlend::kAdd, source),
                            CC::PaletteKeyString(prefix, CC::PaletteBlend::kNormal, source)) &&
                        Contains(diffuse, "|kind=texture|role=diffuse|src=textures\\effects\\fxfire01.dds|srchash=7777777777777777"),
                "key: texture and palette child keys (role, blend and source bytes each re-key)");
            const CC::ChildKeyInput refusedSource{ "..\\x.dds", 1 };
            a_check(!CC::TextureKeyString(prefix, CC::TextureRole::kDiffuse, refusedSource) &&
                        !CC::PaletteKeyString(prefix, CC::PaletteBlend::kAdd, refusedSource) &&
                        !CC::TextureKeyString(prefix, static_cast<CC::TextureRole>(9), source),
                "key: a child of a refused source path, or of an unknown role, gives no key");
        }
    }

    /// Every contract case; returns how many checks it made (an exception counts as one failed check)
    template <class Check, class Skip>
    int Run(Check&& a_check, Skip&& a_skip)
    {
        int count = 0;
        const auto counted = [&](bool a_ok, const std::string& a_what) {
            ++count;
            a_check(a_ok, a_what);
        };
        try {
            auto check = counted;
            Detail::CheckTints(check, a_skip);
            Detail::CheckPrefix(check);
            Detail::CheckNifKeys(check);
            Detail::CheckOtherKeys(check);
            CheckCopyPaths(check);
            CheckProgress(check);
        } catch (const std::exception& e) {
            counted(false, std::string("threw: ") + e.what());
        }
        return count;
    }
}
