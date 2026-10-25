// =============================================================================
// GreySelfTestKeyed - --self-test of the key mode end to end (GreySelfTest.h CheckKeyed)
// =============================================================================
// A made-up game folder: request.json v5 and a dump with the self-test's source NIF (AddOnNodes 9 and 24, an effect
// shader off the palette on an orange texture, a normal-blend and an additive palette shader on an orange palette) as a
// PROJ model and as ADDN 9's MPS, a NIF with only AddOnNode 9 as another PROJ's model and as a muzzle, and a TXST whose
// TX00 is orange. Here: the fixture (GreySelfTestKeyed.h), the refused and lenient requests, a first build (outcomes,
// paths, deps, order, the AddOnNode remap), a second one (every object reused, nothing written), one after a staged copy
// was changed (that object rebuilt, the same bytes). GreySelfTestKeyedRuns.cpp: the other outcomes, the CLI, the dev mode
// and the throttle.

#include "GreySelfTestKeyed.h"
#include "GreyFormsTxst.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <DirectXTex.h>
#include <NifFile.hpp>
#include <Shaders.hpp>

#include <algorithm>
#include <fmt/format.h>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

namespace GreyBuild::SelfTest::Keyed
{
    namespace CC = BlackFlame::CopyContract;
    namespace Rule = BlackFlame::DataPathRule;

    namespace
    {
        // A fixture texture or nothing: a write that fails throws, so no check passes on a file that is not there
        void MustWrite(const std::filesystem::path& a_file, unsigned a_format, bool a_orange)
        {
            if (!WriteTestTexture(a_file, a_format, a_orange)) {
                throw std::runtime_error(fmt::format("fixture: {} was not written", PathText(a_file)));
            }
        }

        void MakePlainNif(const std::filesystem::path& a_file)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            auto node = std::make_unique<nifly::BSValueNode>();
            node->name.get() = "AddOnNode9";
            node->value = 9;
            const auto id = nif.GetHeader().AddBlock(std::move(node));
            nif.GetRootNode()->childRefs.AddBlockRef(id);
            // a value already at the slot index 9 is bound to: its dep still comes from 9 (Codex review), with a warning
            auto already = std::make_unique<nifly::BSValueNode>();
            already->name.get() = "AddOnNode40012";
            already->value = kSlotIndex;
            const auto alreadyId = nif.GetHeader().AddBlock(std::move(already));
            nif.GetRootNode()->childRefs.AddBlockRef(alreadyId);
            SaveNif(nif, a_file);
        }

        [[nodiscard]] std::string Prefix(const std::filesystem::path& a_dump, double a_tint, const std::string& a_identity)
        {
            CC::KeyPrefix prefix;
            prefix.outputIdentity = a_identity;
            prefix.tint = a_tint;
            prefix.tintAdd = 0.9;
            prefix.gradBlackFlame = CC::FileHash::Hashed(Fnv(a_dump / std::string(Rule::kOurPalette)));
            prefix.gradBlackFlameAdd = CC::FileHash::Hashed(Fnv(a_dump / std::string(Rule::kOurPaletteAdd)));
            return CC::KeyPrefixString(prefix).value();  // bad_optional_access: the check fails, the test goes on
        }

        [[nodiscard]] std::string Escape(std::string a_text)
        {
            std::string out;
            for (const char c : a_text) {
                out += c == '\\' ? std::string("\\\\") : std::string(1, c);
            }
            return out;
        }
    }

    std::filesystem::path Write(const std::filesystem::path& a_file, const std::string& a_text)
    {
        std::filesystem::create_directories(a_file.parent_path());
        std::ofstream(a_file, std::ios::binary) << a_text;
        return a_file;
    }

    std::string ReadAll(const std::filesystem::path& a_file)
    {
        std::ifstream in(a_file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::uint64_t Fnv(const std::filesystem::path& a_file)
    {
        std::string why;
        const auto digest = DigestFile(a_file, why);
        return digest ? digest->fnv64 : 0;
    }

    Fixture MakeFixture(const std::filesystem::path& a_dir, const std::string& a_name, const std::filesystem::path& a_sourceNif,
        Variant a_variant, double a_tint, int a_version, const std::string& a_identity)
    {
        Fixture fixture{ a_dir / a_name, a_dir / a_name / "data", a_dir / a_name / "staging" };
        const auto& dump = fixture.dump;
        for (const char* rel : { "meshes/magic/keyed.nif", "meshes/mps/a.nif" }) {
            std::filesystem::create_directories((dump / rel).parent_path());
            std::filesystem::copy_file(a_sourceNif, dump / rel, std::filesystem::copy_options::overwrite_existing);
        }
        MakePlainNif(dump / "meshes/magic/plain.nif");
        MustWrite(dump / std::string(Rule::kOurPalette), DXGI_FORMAT_B8G8R8A8_UNORM, true);
        MustWrite(dump / std::string(Rule::kOurPaletteAdd), DXGI_FORMAT_B8G8R8A8_UNORM, false);
        if (a_variant != Variant::kNotDumped && a_variant != Variant::kUnread) {
            MustWrite(dump / kFxFire, a_variant == Variant::kBc7 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC3_UNORM, true);
        }
        if (a_variant != Variant::kMissingPalette) {
            MustWrite(dump / kSourcePalette, DXGI_FORMAT_B8G8R8A8_UNORM, true);
        }
        MustWrite(dump / kDecal, a_variant == Variant::kMissingPalette ? DXGI_FORMAT_BC5_UNORM : DXGI_FORMAT_BC3_UNORM, true);
        const std::string identity = a_identity.empty() ? std::string(OutputIdentity()) : a_identity;
        // the error text is no longer read: the state decides (contract 4)
        const std::string fxEntry = a_variant == Variant::kNotDumped ?
                                        fmt::format(R"j({{"path":"{}","size":9000000,"ok":false,"state":"notdumped","error":"text"}})j",
                                            Escape(kFxFire)) :
                                    a_variant == Variant::kUnread ?
                                        fmt::format(R"j({{"path":"{}","size":9000,"ok":false,"state":"unread","error":"text"}})j",
                                            Escape(kFxFire)) :
                                        fmt::format(R"({{"path":"{}","size":1,"ok":true,"state":"ok"}})", Escape(kFxFire));
        // the dump's other textures as the request lists them (a file the request does not list is never read)
        std::string listed = fmt::format(R"(,{{"path":"{}","size":1,"ok":true,"state":"ok"}})", Escape(kDecal));
        if (a_variant != Variant::kMissingPalette) {
            listed += fmt::format(R"(,{{"path":"{}","size":1,"ok":true,"state":"ok"}})", Escape(kSourcePalette));
        }
        const std::string request = fmt::format(R"({{"version":{},"contractVersion":{},"game":"1.6.1170.0","fingerprint":"fnv64:00000000000000ab",
"slotsPlugin":"HeartOfMagic_BlackFlame_Slots.esp","dumpComplete":true,"decalTextureSets":true,
"options":{{"tint":{},"tintAdd":0.9,"paletteMode":"derived","keyPrefix":"{}"}},
"forms":[
 {{"source":"Skyrim.esm|012FCF","type":"PROJ","editorId":"","model":"Magic\\Keyed.nif","muzzleModel":"magic\\plain.nif","bounds":[0,0,0,0,0,0],"data":{{}},"viaSpell":"Skyrim.esm|012FCE","costliest":true,"slot":2048,"key":"{}"}},
 {{"source":"Skyrim.esm|012FD0","type":"PROJ","editorId":"","model":"magic\\plain.nif","muzzleModel":"","bounds":[0,0,0,0,0,0],"data":{{}},"viaSpell":"Skyrim.esm|012FCE","costliest":true,"slot":2049,"key":"{}"}},
 {{"source":"Skyrim.esm|0ABCDE","type":"TXST","editorId":"DecalRuneFire","model":"","bounds":[0,0,0,0,0,0],"data":{{"flags":0}},"hasDecal":true,"textures":["Effects\\Decal.dds","","","","","","",""],"texturesRaw":["456666656374735C446563616C2E646473","","","","","","",""],"viaSpell":"Skyrim.esm|012FCE","costliest":true,"slot":3744}},
 {{"source":"Skyrim.esm|0ABCE0","type":"IPCT","editorId":"","model":"magic\\missing.nif","data":{{"flags":1}},"viaSpell":"Skyrim.esm|012FCE","costliest":true,"slot":2600}},
 {{"source":"Skyrim.esm|0ABCE1","type":"IPCT","editorId":"","model":"","data":{{"flags":1}},"viaSpell":"Skyrim.esm|012FCE","costliest":true,"slot":2601}}],
"muzzles":[{{"path":"magic\\plain.nif","key":"{}"}}],
"addnMps":[{{"slot":{},"path":"mps\\a.nif","key":"{}","index":9}}],
"txst":[{{"slot":3744,"key":"{}","dodt":true}}],"efshPalettes":[],"efshBases":[],
"addons":[{{"index":9,"model":"mps\\a.nif","source":"Skyrim.esm|000009","masterParticleCap":0,"dnamFlags":1,"own":false}},
 {{"index":40001,"model":"x\\collide.nif","source":"Other.esp|000801","own":false}}],
"nifs":[{{"path":"meshes\\magic\\missing.nif","size":0,"fnv64":"0000000000000000","ok":false,"error":"does not open"}},
 {{"path":"meshes\\magic\\keyed.nif","size":1,"fnv64":"{}","ok":true}},{{"path":"meshes\\magic\\plain.nif","size":1,"fnv64":"{}","ok":true}},
 {{"path":"meshes\\mps\\a.nif","size":1,"fnv64":"{}","ok":true}}],
"textures":[{{"path":"{}","size":1,"ok":true,"state":"ok","own":true}},{{"path":"{}","size":1,"ok":true,"state":"ok","own":true}},{}{}],
"excluded":[{{"source":"","type":"IPCT","reason":"no defining file"}}]}})", a_version, CC::kContractVersion, a_tint, Escape(Prefix(dump, 0.6, identity)), CC::Hex16(kModel), CC::Hex16(kPlain),
            CC::Hex16(kMuzzle), kSlot, CC::Hex16(kMps), CC::Hex16(kTxst), CC::Hex16(Fnv(dump / "meshes/magic/keyed.nif")),
            CC::Hex16(Fnv(dump / "meshes/magic/plain.nif")), CC::Hex16(Fnv(dump / "meshes/mps/a.nif")),
            Escape(std::string(Rule::kOurPalette)), Escape(std::string(Rule::kOurPaletteAdd)), fxEntry, listed);
        Write(fixture.folder / "request.json", request);
        return fixture;
    }

    int Run(const Fixture& a_fixture, std::uint64_t a_cap)
    {
        KeyedOptions options;
        options.staging = a_fixture.staging;
        options.jobMemory = a_cap;
        options.commitReserve = 0;  // the test never waits for the system's commit
        // nothing in use: the tiny cap test sees the estimates alone (this process holds more than 64 KiB)
        options.memoryProbe = []() { return std::optional<MemoryState>(MemoryState{ 0, UINT64_MAX }); };
        return RunKeyed(a_fixture.folder, options);
    }

    CC::ProgressFile Progress(const Fixture& a_fixture)
    {
        return CC::ParseProgress(ReadAll(a_fixture.staging / CC::kProgressFile));
    }

    const CC::ProgressLine* Find(const CC::ProgressFile& a_file, std::uint64_t a_key)
    {
        const CC::ProgressLine* found = nullptr;
        for (const auto& line : a_file.lines) {
            found = line.key == a_key ? &line : found;  // the last one
        }
        return found;
    }

    std::size_t Position(const CC::ProgressFile& a_file, std::uint64_t a_key)
    {
        for (std::size_t i = 0; i < a_file.lines.size(); ++i) {
            if (a_file.lines[i].key == a_key) {
                return i;
            }
        }
        return a_file.lines.size();
    }

    bool HasAddnDep(const CC::ProgressLine* a_line, std::uint32_t a_index)
    {
        return a_line && std::any_of(a_line->deps.begin(), a_line->deps.end(),
                             [&](const CC::ProgressDep& a_dep) { return a_dep.isAddn && a_dep.addn == a_index; });
    }

    namespace
    {
        void CheckRequest(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
        {
            const auto fixture = MakeFixture(a_dir, "req", a_sourceNif, Variant::kNormal);
            const auto request = LoadKeyedRequest(fixture.folder / "request.json");
            const auto kinds = [&](CC::ProgressKind a_kind) {
                return std::count_if(request.objects.begin(), request.objects.end(), [&](const KeyedSpec& a_s) { return a_s.kind == a_kind; });
            };
            a_check.Check(request.objects.size() == 5 && kinds(CC::ProgressKind::kNif) == 3 && kinds(CC::ProgressKind::kMps) == 1 &&
                              kinds(CC::ProgressKind::kTxst) == 1 && request.objects.back().form == 2 && request.addnSlots.size() == 1 &&
                              request.addnSlots.at(9) == kSlotIndex && request.nifHashes.count("meshes\\magic\\keyed.nif") == 1 &&
                              request.base.forms[0].key == CC::Hex16(kModel) && request.tint == 0.6,
                fmt::format("request v5: 3 NIFs (2 models, 1 muzzle), 1 MPS bound 9 -> 40012, 1 TXST by its slot; forms keyed ({} objects)",
                    request.objects.size()));
            const auto refuses = [&](const std::string& a_name, double a_tint, int a_version) {
                try {
                    (void)LoadKeyedRequest(MakeFixture(a_dir, a_name, a_sourceNif, Variant::kNormal, a_tint, a_version).folder / "request.json");
                    return false;
                } catch (const std::runtime_error&) {
                    return true;
                }
            };
            a_check.Check(refuses("req_v4", 0.6, kGameRequestVersion), "request v5: a version 4 request is refused");
            auto text = ReadAll(fixture.folder / "request.json");
            const std::string version = fmt::format("\"contractVersion\":{}", CC::kContractVersion);
            text.replace(text.find(version), version.size(), fmt::format("\"contractVersion\":{}", CC::kContractVersion + 1));
            Write(a_dir / "req_cv" / "request.json", text);
            bool refused = false;
            try {
                (void)LoadKeyedRequest(a_dir / "req_cv" / "request.json");
            } catch (const std::runtime_error&) {
                refused = true;
            }
            a_check.Check(refused, "request v5: another contract version is refused");
            auto noIndex = ReadAll(fixture.folder / "request.json");
            noIndex.replace(noIndex.find(",\"index\":9}"), std::string(",\"index\":9").size(), "");
            Write(a_dir / "req_index" / "request.json", noIndex);
            refused = false;
            try {
                (void)LoadKeyedRequest(a_dir / "req_index" / "request.json");
            } catch (const std::runtime_error&) {
                refused = true;
            }
            a_check.Check(refused, "request v5: an addnMps entry without its source ADDN index is refused (no guess by model path)");
            // a_why set: refused for that reason, not for another one on the way (agy round 2)
            const auto refusedText = [&](const std::string& a_name, std::string a_text, const char* a_why = nullptr) {
                Write(a_dir / a_name / "request.json", a_text);
                try {
                    (void)LoadKeyedRequest(a_dir / a_name / "request.json");
                    return false;
                } catch (const std::runtime_error& e) {
                    return !a_why || std::string(e.what()).find(a_why) != std::string::npos;
                }
            };
            auto handRequest = nlohmann::json::parse(ReadAll(fixture.folder / "request.json"));
            auto& hand = handRequest["forms"][0];
            hand["type"] = "ARTO";
            hand["data"] = { { "artType", 0 } };
            hand["slot"] = 0xD00;
            hand[CC::Request::kRole] = CC::Request::kCastingArtRole;
            hand[CC::Request::kTintAddHands] = 1.4;
            hand[CC::Request::kHandMarbleOpacity] = 0.9;
            hand[CC::Request::kHandMarbleScale] = 1.15;
            const std::string handKey = handRequest["options"]["keyPrefix"].get<std::string>() +
                "|kind=model|nif=meshes\\magic\\keyed.nif|nifhash=" + CC::Hex16(Fnv(fixture.dump / "meshes/magic/keyed.nif")) +
                "|addn=none|hands=1.400000|handmarbleopacity=0.900000|handmarblescale=1.150000|handfx=2";
            hand[CC::Request::kKeyString] = handKey;
            hand["key"] = CC::Key16Of(handKey);
            a_check.Check(!refusedText("req_hands", handRequest.dump()), "request v5: casting art carries a verified object tint/key");
            // FormJson sanitizes raw ACP paths to UTF-8; SlotKeys hashes the original bytes.
            for (const std::string utf8 : { "magic\\\xED\x9A\xA8\xEA\xB3\xBC.nif", "magic\\\xC3\xA9.nif" }) {
                const auto raw = Utf8ToAnsiExact(utf8);
                for (const auto& bytes : { std::optional<std::string>(utf8), raw }) {
                    if (!bytes) continue;
                    auto encoded = handRequest;
                    auto& form = encoded["forms"][0];
                    form["model"] = utf8;
                    auto key = handKey;
                    const std::string ascii = "nif=meshes\\magic\\keyed.nif";
                    key.replace(key.find(ascii), ascii.size(), *CC::Detail::PathField("nif", *bytes,
                        BlackFlame::DataPathRule::kMeshesRoot, true));
                    form[CC::Request::kKeyString] = key;
                    form["key"] = CC::Key16Of(key);
                    const auto file = Write(a_dir / "req_hand_encoding" / "request.json", encoded.dump());
                    bool accepted = false;
                    try {
                        const auto read = LoadKeyedRequest(file);
                        accepted = read.objects.size() == request.objects.size() && read.objects[0].requestFailure.empty();
                    } catch (const std::runtime_error&) {}
                    a_check.Check(accepted, "request v5: non-ASCII hand model compares the original UTF-8 or ACP key bytes");
                }
            }
            auto badModel = handRequest;
            badModel["forms"][0]["model"] = "magic\\wrong.nif";
            bool isolated = false;
            try {
                const auto read = LoadKeyedRequest(Write(a_dir / "req_hand_badmodel" / "request.json", badModel.dump()));
                bool built = false;
                const auto line = GuardObject(read.objects[0], [&]() { built = true; return CC::ProgressLine{}; });
                isolated = read.objects.size() == request.objects.size() && !built && line.outcome == CC::Outcome::kFailed &&
                    line.reason.find("model") != std::string::npos && read.objects[1].requestFailure.empty();
            } catch (const std::runtime_error&) {}
            a_check.Check(isolated, "request v5: one hand model/key mismatch fails only that object, before building");
            const auto originalRequest = ReadAll(fixture.folder / "request.json");
            Write(fixture.folder / "request.json", badModel.dump());
            const int isolatedExit = Run(fixture);
            const auto isolatedProgress = Progress(fixture);
            Write(fixture.folder / "request.json", originalRequest);
            const auto* badLine = Find(isolatedProgress, CC::KeyOf(handKey));
            const auto* goodLine = Find(isolatedProgress, kPlain);
            a_check.Check(isolatedExit == 0 && badLine && badLine->outcome == CC::Outcome::kFailed &&
                goodLine && goodLine->outcome == CC::Outcome::kOk,
                "key run: a hand model/key mismatch fails alone while another NIF is written successfully");
            auto malformed = handRequest;
            malformed["forms"][0][CC::Request::kTintAddHands] = 1.5;
            a_check.Check(refusedText("req_handkey", malformed.dump(), "disagree"),
                "request v5: hand tint that disagrees with its key is refused");
            malformed = handRequest;
            const auto oldHandKey = handKey.substr(0, handKey.rfind("|handfx=")) + "|marbleorder=1";
            malformed["forms"][0][CC::Request::kKeyString] = oldHandKey;
            malformed["forms"][0]["key"] = CC::Key16Of(oldHandKey);
            a_check.Check(refusedText("req_handorder", malformed.dump(), "disagree"),
                "request v5: old marbleorder hand key is refused even when its hash matches");
            malformed = handRequest;
            malformed["forms"][0]["data"]["artType"] = 1;
            a_check.Check(refusedText("req_handrole", malformed.dump(), "another object role"),
                "request v5: a hit-art object cannot request a casting-art override");
            malformed = handRequest;
            malformed["forms"][0].erase(CC::Request::kTintAddHands);
            a_check.Check(refusedText("req_handmissing", malformed.dump(), "requires role"),
                "request v5: casting art without its required override is refused");
            malformed = handRequest;
            malformed["forms"][0][CC::Request::kTintAddHands] = 1.4000004;
            a_check.Check(refusedText("req_handround", malformed.dump(), "quantised"),
                "request v5: an unquantised hand override is refused");
            malformed = handRequest;
            malformed["forms"][0]["key"] = "0000000000000001";
            a_check.Check(refusedText("req_handhash", malformed.dump(), "disagree"),
                "request v5: a hand key that is not its canonical text hash is refused");
            malformed = handRequest;
            malformed["muzzles"][0][CC::Request::kTintAddHands] = 1.4;
            a_check.Check(refusedText("req_handmuzzle", malformed.dump(), "another object role"),
                "request v5: muzzle objects cannot carry a casting-art override");
            struct MarbleCase { const char* field; double value; const char* reason; };
            const MarbleCase marbleCases[] = {
                { CC::Request::kHandMarbleOpacity, 0.8, "disagree" },
                { CC::Request::kHandMarbleScale, 1.2, "disagree" },
                { CC::Request::kHandMarbleOpacity, -0.01, "in range" },
                { CC::Request::kHandMarbleOpacity, 1.01, "in range" },
                { CC::Request::kHandMarbleScale, CC::kMinHandMarbleScale - 0.01, "in range" },
                { CC::Request::kHandMarbleScale, CC::kMaxHandMarbleScale + 0.01, "in range" },
                { CC::Request::kHandMarbleOpacity, 0.9000004, "quantised" },
                { CC::Request::kHandMarbleScale, 1.1500004, "quantised" }
            };
            int marbleCase = 0;
            for (const auto& test : marbleCases) {
                malformed = handRequest;
                malformed["forms"][0][test.field] = test.value;
                a_check.Check(refusedText("req_marble_" + std::to_string(marbleCase++), malformed.dump(), test.reason),
                    "request v5: " + std::string(test.field) + " rejects key mismatch, unclamped or unquantised value " + std::to_string(test.value));
            }
            for (const auto* field : { CC::Request::kHandMarbleOpacity, CC::Request::kHandMarbleScale }) {
                malformed = handRequest;
                malformed["forms"][0].erase(field);
                a_check.Check(refusedText("req_missing_" + std::string(field), malformed.dump(), "requires numeric"),
                    "request v5: casting art requires " + std::string(field));
            }
            auto unlisted = ReadAll(fixture.folder / "request.json");
            const std::string keyedNif = R"({"path":"meshes\\magic\\keyed.nif")";
            unlisted.replace(unlisted.find(keyedNif), keyedNif.size(), R"({"path":"meshes\\magic\\other.nif")");
            // the TXST form is found by its slot (the DLL writes no key on it): another slot, or a key of its own that is
            // not the txst entry's, is refused
            const std::string txstForm = R"("slot":3744},)";
            auto noForm = ReadAll(fixture.folder / "request.json");
            noForm.replace(noForm.find(txstForm), txstForm.size(), R"("slot":3745},)");
            auto otherKey = ReadAll(fixture.folder / "request.json");
            otherKey.replace(otherKey.find(txstForm), txstForm.size(), R"("slot":3744,"key":"9999999999999999"},)");
            auto badDodt = ReadAll(fixture.folder / "request.json");
            badDodt.replace(badDodt.find("\"dodt\":true"), std::string("\"dodt\":true").size(), "\"dodt\":false");
            a_check.Check(refusedText("req_noform", noForm, "no TXST form has the slot") &&
                              refusedText("req_otherkey", otherKey, "has the key 9999999999999999") &&
                              refusedText("req_dodt", badDodt, "'dodt' must be"),
                "request v5: a txst slot no TXST form has, a TXST form key that is not its entry's and a dodt that is not its "
                "slot's are refused");
            auto sameKey = ReadAll(fixture.folder / "request.json");
            sameKey.replace(sameKey.find(txstForm), txstForm.size(), fmt::format(R"("slot":3744,"key":"{}"}},)", CC::Hex16(kTxst)));
            a_check.Check(!refusedText("req_samekey", sameKey), "request v5: a TXST form that carries its entry's own key is read");
            Write(a_dir / "req_unlisted" / "request.json", unlisted);
            bool warned = false;
            try {
                const auto slipped = LoadKeyedRequest(a_dir / "req_unlisted" / "request.json");
                warned = slipped.nifHashes.count("meshes\\magic\\keyed.nif") == 0 &&
                         std::any_of(slipped.warnings.begin(), slipped.warnings.end(),
                             [](const std::string& a_line) { return a_line.find("keyed.nif") != std::string::npos; });
            } catch (const std::runtime_error&) {
                warned = false;
            }
            a_check.Check(warned, "request v5: a NIF nifs[] does not list is warned, not refused (its object fails alone)");
            // a form with a model and no key is left out with a warning, not a refused request: the fixture's IPCT, whose NIF the
            // game could not read (run 2, 2026-10-18), and a read NIF that lost its key (worded apart: a DLL slip); the IPCT with
            // no model is neither an object nor a warning
            bool unreadSkipped = false;
            try {
                const auto read = LoadKeyedRequest(fixture.folder / "request.json");
                const auto lines = [&](const char* a_text) {
                    return std::count_if(read.warnings.begin(), read.warnings.end(),
                        [&](const std::string& a_line) { return a_line.find(a_text) != std::string::npos; });
                };
                unreadSkipped = std::none_of(read.objects.begin(), read.objects.end(),
                                    [](const KeyedSpec& a_spec) { return a_spec.source.find("missing.nif") != std::string::npos; }) &&
                                lines("has no key") == 1 && lines("0ABCE0") == 1 && lines("could not read the NIF") == 1 &&
                                lines("0ABCE1") == 0;
            } catch (const std::runtime_error&) {
                unreadSkipped = false;
            }
            a_check.Check(unreadSkipped, "request v5: a form whose NIF the game could not read has no key: warned and not built");
            auto keyless = ReadAll(fixture.folder / "request.json");
            const std::string plainKey = fmt::format(R"("slot":2049,"key":"{}"}})", CC::Hex16(kPlain));
            keyless.replace(keyless.find(plainKey), plainKey.size(), R"("slot":2049})");
            Write(a_dir / "req_keyless" / "request.json", keyless);
            bool skipped = false;
            try {
                const auto read = LoadKeyedRequest(a_dir / "req_keyless" / "request.json");
                skipped = std::none_of(read.objects.begin(), read.objects.end(),
                              [&](const KeyedSpec& a_spec) { return a_spec.key == kPlain; }) &&
                          std::any_of(read.warnings.begin(), read.warnings.end(),
                              [](const std::string& a_line) { return a_line.find("yet the DLL gave it no key") != std::string::npos; });
            } catch (const std::runtime_error&) {
                skipped = false;
            }
            a_check.Check(skipped, "request v5: a read NIF's form with no key is warned (as the DLL's slip) and not built, not refused");
            auto slotless = ReadAll(fixture.folder / "request.json");
            const std::string missingSlot = R"("costliest":true,"slot":2600})";
            slotless.replace(slotless.find(missingSlot), missingSlot.size(), R"("costliest":true})");
            Write(a_dir / "req_slotless" / "request.json", slotless);
            bool slotlessWarned = false;
            try {
                const auto read = LoadKeyedRequest(a_dir / "req_slotless" / "request.json");
                slotlessWarned = std::any_of(read.warnings.begin(), read.warnings.end(),
                    [](const std::string& a_line) { return a_line.find("0ABCE0") != std::string::npos && a_line.find("no usable slot") != std::string::npos; });
            } catch (const std::runtime_error&) {
                slotlessWarned = false;
            }
            a_check.Check(slotlessWarned, "request v5: a keyless form with no slot is worded as one the pool could not take");
            // two TXST forms on one slot, or a TXST form on a slot of another type, is the DLL's mistake: refused
            const std::string lastForm = R"("slot":2601})";
            auto twoOnSlot = ReadAll(fixture.folder / "request.json");
            twoOnSlot.replace(twoOnSlot.find(lastForm), lastForm.size(), R"("slot":2601},
 {"source":"Skyrim.esm|0ABCE2","type":"TXST","editorId":"","model":"","bounds":[0,0,0,0,0,0],"data":{"flags":0},"hasDecal":true,"textures":["","","","","","","",""],"texturesRaw":["","","","","","","",""],"slot":3744})");
            auto wrongType = ReadAll(fixture.folder / "request.json");
            wrongType.replace(wrongType.find(txstForm), txstForm.size(), R"("slot":2602},)");
            a_check.Check(refusedText("req_twoslot", twoOnSlot, "another TXST form's as well") &&
                              refusedText("req_wrongtype", wrongType, "which is no TXST slot"),
                "request v5: two TXST forms on one slot and a TXST form on a slot that is no TXST slot are refused");
            // the key mode reads no ADDN model and no data value: another mod's ADDN with no mesh model, and a NaN or infinite
            // float the DLL's JSON writes as null, are warned and the request is read (one odd record never blocks every build)
            auto odd = ReadAll(fixture.folder / "request.json");
            const std::string collide = R"("model":"x\\collide.nif")";
            odd.replace(odd.find(collide), collide.size(), R"("model":"")");
            const std::string flags = R"("data":{"flags":0})";
            odd.replace(odd.find(flags), flags.size(), R"("data":{"flags":null})");
            Write(a_dir / "req_odd" / "request.json", odd);
            bool oddRead = false;
            try {
                const auto read = LoadKeyedRequest(a_dir / "req_odd" / "request.json");
                const auto has = [&](const char* a_text) {
                    return std::any_of(read.warnings.begin(), read.warnings.end(),
                        [&](const std::string& a_line) { return a_line.find(a_text) != std::string::npos; });
                };
                oddRead = has("its index kept, no model") && has("is null (not a finite number)") &&
                          read.base.addons->addons.count(40001) == 1 && read.objects.size() == 5;
            } catch (const std::runtime_error&) {
                oddRead = false;
            }
            a_check.Check(oddRead, "request v5: an ADDN with no mesh model and a null data value are warned, the request read");
            auto nullModel = ReadAll(fixture.folder / "request.json");
            nullModel.replace(nullModel.find(collide), collide.size(), R"("model":null)");
            a_check.Check(refusedText("req_nullmodel", nullModel, "must be a string"),
                "request v5: an ADDN model that is no string is refused in the key mode too");
            // only the key mode's route is lenient: --out writes the data values and plans from the ADDN models (Codex round 2)
            const auto outRefuses = [&](const std::string& a_name, const std::string& a_text) {
                Write(a_dir / a_name / "request.json", a_text);
                try {
                    (void)LoadFormsRequestFile(a_dir / a_name / "request.json");
                    return false;
                } catch (const std::runtime_error&) {
                    return true;
                }
            };
            auto nullOnly = ReadAll(fixture.folder / "request.json");
            nullOnly.replace(nullOnly.find(flags), flags.size(), R"("data":{"flags":null})");
            auto addnOnly = ReadAll(fixture.folder / "request.json");
            addnOnly.replace(addnOnly.find(collide), collide.size(), R"("model":"")");
            a_check.Check(outRefuses("req_out_null", nullOnly) && outRefuses("req_out_addn", addnOnly) &&
                              !outRefuses("req_out_plain", ReadAll(fixture.folder / "request.json")),
                "request v5 outside the key mode (--out): a null data value and an ADDN with no mesh model are refused");
        }

        void CheckFirstRuns(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
        {
            const auto fixture = MakeFixture(a_dir, "game", a_sourceNif, Variant::kNormal);
            const int exit = Run(fixture);
            const auto progress = Progress(fixture);
            const auto* model = Find(progress, kModel);
            const auto* plain = Find(progress, kPlain);
            const auto* muzzle = Find(progress, kMuzzle);
            const auto* mps = Find(progress, kMps);
            const auto* txst = Find(progress, kTxst);
            a_check.Check(exit == 0 && progress.errors.empty() && !progress.cutLastLine && model && plain && muzzle && mps && txst,
                fmt::format("key run: exit {}, every object has a line that parses ({} lines, {} bad)", exit, progress.lines.size(),
                    progress.errors.size()));
            if (!model || !plain || !muzzle || !mps || !txst) {
                return;
            }
            const auto modelPath = Rule::CopyPath(Rule::kMeshesRoot, CC::Hex16(kModel), "meshes\\magic\\keyed.nif");
            a_check.Check(model->outcome == CC::Outcome::kFallback && model->path == modelPath && mps->outcome == CC::Outcome::kFallback &&
                              model->reason.find("AddOnNode") != std::string::npos && plain->outcome == CC::Outcome::kOk &&
                              muzzle->outcome == CC::Outcome::kOk && txst->outcome == CC::Outcome::kOk && txst->path.empty(),
                fmt::format("key run: AddOnNode 24 bound to no slot makes the model and MPS fallback ('{}'); the plain NIFs and the TXST ok",
                    model->reason));
            const auto staged = StagedFile(fixture.staging, model->path);
            std::string why;
            const auto digest = DigestFile(staged, why);
            a_check.Check(digest && digest->fnv64 == model->fnv64 && digest->bytes == model->bytes && Fnv(StagedFile(fixture.staging, mps->path)) == mps->fnv64,
                "key run: the staged copies are at their CopyPath with the line's bytes and fnv64");
            const auto dds = std::count_if(progress.lines.begin(), progress.lines.end(),
                [](const CC::ProgressLine& a_line) { return a_line.kind == CC::ProgressKind::kDds && a_line.outcome == CC::Outcome::kOk; });
            const auto paths = [](const CC::ProgressLine* a_line) {
                return std::count_if(a_line->deps.begin(), a_line->deps.end(), [](const CC::ProgressDep& a_dep) { return !a_dep.isAddn; });
            };
            a_check.Check(dds == 4 && paths(model) == 3 && paths(mps) == 3 && paths(txst) == 1 && HasAddnDep(model, kSlotIndex) &&
                              HasAddnDep(mps, kSlotIndex) && HasAddnDep(plain, kSlotIndex) && HasAddnDep(muzzle, kSlotIndex) && !HasAddnDep(txst, kSlotIndex),
                fmt::format("key run: 4 child copies made once (effect texture, 2 derived palettes, decal); the NIFs and the MPS list their "
                            "3 and {{\"addn\":40012}}, the TXST its decal ({} dds lines)", dds));
            const auto order = [&](std::uint64_t a_first, std::uint64_t a_later) { return Position(progress, a_first) < Position(progress, a_later); };
            a_check.Check(order(kPlain, kMps) && order(kMuzzle, kMps) && order(kMps, kModel) && order(kMps, kTxst),
                "key run: the NIFs with no texture to recolour first, then the MPS, then the rest by texture cost");
            nifly::NifFile nif;
            LoadNif(nif, staged);
            std::vector<int> values;
            std::string effectSource;
            const auto& hdr = nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (const auto* node = hdr.GetBlock<nifly::BSValueNode>(id)) {
                    values.push_back(node->value);
                } else if (const auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(id); effect && effect->name.get() == "FlameOff") {
                    effectSource = effect->sourceTexture.get();
                }
            }
            CC::KeyPrefix prefix;
            prefix.outputIdentity = OutputIdentity();
            prefix.tint = 0.6;
            prefix.tintAdd = 0.9;
            prefix.gradBlackFlame = CC::FileHash::Hashed(Fnv(fixture.dump / std::string(Rule::kOurPalette)));
            prefix.gradBlackFlameAdd = CC::FileHash::Hashed(Fnv(fixture.dump / std::string(Rule::kOurPaletteAdd)));
            const auto childKey = CC::Key16Of(*CC::TextureKeyString(prefix, CC::TextureRole::kEffectSource, { kFxFire, Fnv(fixture.dump / kFxFire) }));
            a_check.Check(std::find(values.begin(), values.end(), kSlotIndex) != values.end() && std::find(values.begin(), values.end(), 24) != values.end() &&
                              std::find(values.begin(), values.end(), 9) == values.end() &&
                              effectSource == Rule::CopyPath(Rule::kTexturesRoot, childKey, kFxFire),
                fmt::format("key run: AddOnNode 9 on slot index 40012, 24 kept; the effect shader names its texture copy at the key the "
                            "contract's own TextureKeyString gives ('{}')", effectSource));

            const auto modelTime = std::filesystem::last_write_time(staged);
            const auto mpsFile = StagedFile(fixture.staging, mps->path);
            const auto lines = progress.lines.size();
            const int second = Run(fixture);
            const auto after = Progress(fixture);
            a_check.Check(second == 0 && after.lines.size() == lines && std::filesystem::last_write_time(staged) == modelTime,
                fmt::format("key run again: every object reused - no line added, no copy rewritten ({} lines, were {})", after.lines.size(), lines));

            std::ofstream(staged, std::ios::binary | std::ios::app) << 'x';
            const auto mpsTime = std::filesystem::last_write_time(mpsFile);
            const int third = Run(fixture);
            const auto rebuilt = Progress(fixture);
            const auto* line = Find(rebuilt, kModel);
            a_check.Check(third == 0 && line && line->fnv64 == model->fnv64 && Fnv(staged) == model->fnv64 && rebuilt.lines.size() == lines &&
                              std::filesystem::last_write_time(mpsFile) == mpsTime,
                "key run after a staged copy changed: only that object is built again, to the same bytes");
        }
    }
}

namespace GreyBuild::SelfTest
{
    void CheckKeyed(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        const auto dir = a_dir / "keyed";
        try {
            Keyed::CheckRequest(a_check, dir, a_sourceNif);
            Keyed::CheckFirstRuns(a_check, dir, a_sourceNif);
            Keyed::CheckOutcomes(a_check, dir, a_sourceNif);
            Keyed::CheckCommandLine(a_check, dir, a_sourceNif);
            Keyed::CheckDevMode(a_check, dir, a_sourceNif);
            Keyed::CheckThrottleRun(a_check, dir, a_sourceNif);
            Keyed::CheckPublishedRun(a_check, dir, a_sourceNif);
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("key mode: exception: {}", e.what()));
        }
    }
}
