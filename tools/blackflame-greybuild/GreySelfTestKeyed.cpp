// =============================================================================
// GreySelfTestKeyed - --self-test of the key mode end to end (GreySelfTest.h CheckKeyed)
// =============================================================================
// A made-up game folder: request.json v5 and a dump with the self-test's source NIF (AddOnNodes 9 and 24, an effect
// shader off the palette on an orange texture, a normal-blend and an additive palette shader on an orange palette) as a
// PROJ model and as ADDN 9's MPS, a NIF with only AddOnNode 9 as another PROJ's model and as a muzzle, and a TXST whose
// TX00 is orange. Runs: a first build (outcomes, paths, deps, order, the AddOnNode remap), a second one (every object
// reused, nothing written), one after a staged copy was changed (that object rebuilt, the same bytes), a tiny job memory
// cap (too_large), a missing source palette and a BC5 decal (fallback), a texture outside the dump caps (not_dumped),
// refused requests, the CLI refusals, and the dev mode (--out) on the same v5 request.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyKeyed.h"
#include "GreySelfTest.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <DirectXTex.h>
#include <NifFile.hpp>
#include <Shaders.hpp>

#include <algorithm>
#include <memory>
#include <fmt/format.h>
#include <fstream>

namespace GreyBuild::SelfTest
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;
        namespace Rule = BlackFlame::DataPathRule;

        constexpr std::uint64_t kModel = 0x1111111111111111ull;   // forms[0]: the source NIF as a PROJ model
        constexpr std::uint64_t kPlain = 0x2222222222222222ull;   // forms[1]: the AddOnNode-only NIF (values 9 and 40012)
        constexpr std::uint64_t kTxst = 0x3333333333333333ull;    // forms[2] and txst[0]
        constexpr std::uint64_t kMuzzle = 0x4444444444444444ull;  // muzzles[0]: the AddOnNode-only NIF
        constexpr std::uint64_t kMps = 0x5555555555555555ull;     // addnMps[0]: the source NIF as ADDN 9's MPS
        constexpr std::uint32_t kSlot = 0xF0C;
        constexpr int kSlotIndex = 40012;
        constexpr std::uint64_t kTinyCap = 64 * 1024;             // under the 64 x 64 test texture's estimate (109220)
        constexpr DWORD kChildTimeoutMs = 120000;                 // a CLI child run: 2 minutes at most
        constexpr const char* kFxFire = "textures\\effects\\fxfire01.dds";
        constexpr const char* kSourcePalette = "textures\\effects\\gradients\\gradflame01.dds";
        constexpr const char* kDecal = "textures\\effects\\decal.dds";

        enum class Variant
        {
            kNormal,
            kMissingPalette,  // gradflame01.dds not in the dump, decal.dds a BC5: fallbacks
            kNotDumped,       // fxfire01.dds outside the dump caps (state notdumped)
            kUnread           // fxfire01.dds opened by the game, its read failed (state unread)
        };

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

        [[nodiscard]] std::string Prefix(const std::filesystem::path& a_dump, double a_tint, std::uint64_t a_exe)
        {
            CC::KeyPrefix prefix;
            prefix.builderExeFnv64 = a_exe;
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

        struct Fixture
        {
            std::filesystem::path folder;
            std::filesystem::path dump;
            std::filesystem::path staging;
        };

        // The game folder of a_variant, request.json written with a_tint in options (the prefix keeps 0.6) and a_version
        Fixture MakeFixture(const std::filesystem::path& a_dir, const std::string& a_name, const std::filesystem::path& a_sourceNif,
            Variant a_variant, double a_tint = 0.6, int a_version = kGameRequestVersionKeyed, std::uint64_t a_exe = 0)
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
                MustWrite(dump / kFxFire, DXGI_FORMAT_BC3_UNORM, true);
            }
            if (a_variant != Variant::kMissingPalette) {
                MustWrite(dump / kSourcePalette, DXGI_FORMAT_B8G8R8A8_UNORM, true);
            }
            MustWrite(dump / kDecal, a_variant == Variant::kMissingPalette ? DXGI_FORMAT_BC5_UNORM : DXGI_FORMAT_BC3_UNORM, true);
            const std::uint64_t exe = a_exe != 0 ? a_exe : Fnv(ThisExecutable());
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
"txst":[{{"slot":3744,"key":"{}","dodt":true}}],
"addons":[{{"index":9,"model":"mps\\a.nif","source":"Skyrim.esm|000009","masterParticleCap":0,"dnamFlags":1,"own":false}},
 {{"index":40001,"model":"x\\collide.nif","source":"Other.esp|000801","own":false}}],
"nifs":[{{"path":"meshes\\magic\\missing.nif","size":0,"fnv64":"0000000000000000","ok":false,"error":"does not open"}},
 {{"path":"meshes\\magic\\keyed.nif","size":1,"fnv64":"{}","ok":true}},{{"path":"meshes\\magic\\plain.nif","size":1,"fnv64":"{}","ok":true}},
 {{"path":"meshes\\mps\\a.nif","size":1,"fnv64":"{}","ok":true}}],
"textures":[{{"path":"{}","size":1,"ok":true,"state":"ok","own":true}},{{"path":"{}","size":1,"ok":true,"state":"ok","own":true}},{}{}],
"excluded":[{{"source":"","type":"IPCT","reason":"no defining file"}}]}})", a_version, CC::kContractVersion, a_tint, Escape(Prefix(dump, 0.6, exe)), CC::Hex16(kModel), CC::Hex16(kPlain),
                CC::Hex16(kMuzzle), kSlot, CC::Hex16(kMps), CC::Hex16(kTxst), CC::Hex16(Fnv(dump / "meshes/magic/keyed.nif")),
                CC::Hex16(Fnv(dump / "meshes/magic/plain.nif")), CC::Hex16(Fnv(dump / "meshes/mps/a.nif")),
                Escape(std::string(Rule::kOurPalette)), Escape(std::string(Rule::kOurPaletteAdd)), fxEntry, listed);
            Write(fixture.folder / "request.json", request);
            return fixture;
        }

        [[nodiscard]] int Run(const Fixture& a_fixture, std::uint64_t a_cap = kDefaultJobMemoryMiB * kBytesPerMiB)
        {
            KeyedOptions options;
            options.staging = a_fixture.staging;
            options.jobMemory = a_cap;
            options.commitReserve = 0;  // the test never waits for the system's commit
            // nothing in use: the tiny cap test sees the estimates alone (this process holds more than 64 KiB)
            options.memoryProbe = []() { return std::optional<MemoryState>(MemoryState{ 0, UINT64_MAX }); };
            return RunKeyed(a_fixture.folder, options);
        }

        [[nodiscard]] CC::ProgressFile Progress(const Fixture& a_fixture)
        {
            return CC::ParseProgress(ReadAll(a_fixture.staging / CC::kProgressFile));
        }

        [[nodiscard]] const CC::ProgressLine* Find(const CC::ProgressFile& a_file, std::uint64_t a_key)
        {
            const CC::ProgressLine* found = nullptr;
            for (const auto& line : a_file.lines) {
                found = line.key == a_key ? &line : found;  // the last one
            }
            return found;
        }

        [[nodiscard]] std::size_t Position(const CC::ProgressFile& a_file, std::uint64_t a_key)
        {
            for (std::size_t i = 0; i < a_file.lines.size(); ++i) {
                if (a_file.lines[i].key == a_key) {
                    return i;
                }
            }
            return a_file.lines.size();
        }

        [[nodiscard]] bool HasAddnDep(const CC::ProgressLine* a_line, std::uint32_t a_index)
        {
            return a_line && std::any_of(a_line->deps.begin(), a_line->deps.end(),
                                 [&](const CC::ProgressDep& a_dep) { return a_dep.isAddn && a_dep.addn == a_index; });
        }

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
            prefix.builderExeFnv64 = Fnv(ThisExecutable());
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

        void CheckOutcomes(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
        {
            const auto tiny = MakeFixture(a_dir, "tiny", a_sourceNif, Variant::kNormal);
            const int exit = Run(tiny, kTinyCap);
            const auto progress = Progress(tiny);
            const auto* model = Find(progress, kModel);
            const auto* plain = Find(progress, kPlain);
            const auto* txst = Find(progress, kTxst);
            a_check.Check(exit == 0 && model && model->outcome == CC::Outcome::kTooLarge && model->cap == kTinyCap &&
                              txst && txst->outcome == CC::Outcome::kTooLarge && plain && plain->outcome == CC::Outcome::kOk,
                fmt::format("job memory cap {}: textures over it make their objects too_large (cap in the reason), the plain NIF ok",
                    kTinyCap));
            ProgressLog log;
            std::string why;
            a_check.Check(log.Open(tiny.staging, why) && log.Reusable(kModel, kTinyCap) && !log.Reusable(kModel, kTinyCap * 2),
                "too_large: terminal while the cap stays, built again under a larger cap");
            const int again = Run(tiny);  // the default cap: built this time
            const auto larger = Progress(tiny);
            a_check.Check(again == 0 && Find(larger, kModel) && Find(larger, kModel)->outcome == CC::Outcome::kFallback &&
                              Find(larger, kTxst) && Find(larger, kTxst)->outcome == CC::Outcome::kOk,
                "too_large: the next run under a larger cap builds the model and the TXST (the last line for a key wins)");

            const auto missing = MakeFixture(a_dir, "palette", a_sourceNif, Variant::kMissingPalette);
            const int missingExit = Run(missing);
            const auto fallback = Progress(missing);
            const auto* fallbackModel = Find(fallback, kModel);
            const auto* bc5 = Find(fallback, kTxst);
            a_check.Check(missingExit == 0 && fallbackModel && fallbackModel->outcome == CC::Outcome::kFallback &&
                              fallbackModel->reason.find("source palette") != std::string::npos && !fallbackModel->path.empty() && bc5 &&
                              bc5->outcome == CC::Outcome::kFallback && bc5->deps.empty() && bc5->reason.find("blue") != std::string::npos &&
                              bc5->reason.find("no colour slot") != std::string::npos,
                fmt::format("fallback: a missing source palette publishes, slot left ('{}'); a TXST whose BC5 decal got no "
                            "copy is a fallback with no dep ('{}')", fallbackModel ? fallbackModel->reason : "",
                    bc5 ? bc5->reason : ""));

            const auto capped = MakeFixture(a_dir, "capped", a_sourceNif, Variant::kNotDumped);
            const int cappedExit = Run(capped);
            const auto notDumped = Progress(capped);
            const auto* cappedModel = Find(notDumped, kModel);
            a_check.Check(cappedExit == 0 && cappedModel && cappedModel->outcome == CC::Outcome::kNotDumped && cappedModel->path.empty() &&
                              Find(notDumped, kTxst) && Find(notDumped, kTxst)->outcome == CC::Outcome::kOk,
                "not_dumped: a texture the rule picks outside the dump caps makes its object not_dumped (no file); the TXST ok");

            const auto unread = MakeFixture(a_dir, "unread", a_sourceNif, Variant::kUnread);
            const int unreadExit = Run(unread);
            const auto unreadProgress = Progress(unread);
            const auto* unreadModel = Find(unreadProgress, kModel);
            a_check.Check(unreadExit == 0 && unreadModel && unreadModel->outcome == CC::Outcome::kFailed &&
                              unreadModel->reason.find("unread") != std::string::npos,
                fmt::format("failed: a texture the game could not read (state unread) fails its object, retried at the next start ('{}')",
                    unreadModel ? unreadModel->reason : ""));

            const auto broken = MakeFixture(a_dir, "broken", a_sourceNif, Variant::kNormal);
            std::filesystem::remove(broken.dump / kFxFire);  // listed as dumped, gone from the dump
            const int brokenExit = Run(broken);
            const auto brokenProgress = Progress(broken);
            const auto* brokenModel = Find(brokenProgress, kModel);
            a_check.Check(brokenExit == 0 && brokenModel && brokenModel->outcome == CC::Outcome::kFailed &&
                              brokenModel->reason.find("listed as dumped") != std::string::npos,
                fmt::format("failed: a texture the request lists as dumped that is not in the dump fails its object, never a "
                            "fallback that would be reused ('{}')", brokenModel ? brokenModel->reason : ""));

            const auto slip = MakeFixture(a_dir, "slip", a_sourceNif, Variant::kNormal);
            auto slipText = ReadAll(slip.folder / "request.json");
            const auto replaceEntry = [&](const std::string& a_start, const std::string& a_entry) {
                const auto at = slipText.find(a_start);
                slipText.replace(at, slipText.find('}', at) - at + 1, a_entry);
            };
            replaceEntry(R"({"path":"meshes\\magic\\keyed.nif")", R"({"path":"meshes\\magic\\other.nif","size":1,"ok":false})");
            replaceEntry(R"({"path":"meshes\\mps\\a.nif")", R"({"path":"MPS\\A.nif","size":1,"ok":false})");  // MeshDataPath folds it
            Write(slip.folder / "request.json", slipText);
            const int slipExit = Run(slip);
            const auto slipProgress = Progress(slip);
            const auto* slipModel = Find(slipProgress, kModel);
            const auto* slipMps = Find(slipProgress, kMps);
            a_check.Check(slipExit == 0 && slipModel && slipModel->outcome == CC::Outcome::kFailed &&
                              slipModel->reason.find("does not list") != std::string::npos && slipMps &&
                              slipMps->outcome == CC::Outcome::kFailed && slipMps->reason.find("could not read") != std::string::npos &&
                              Find(slipProgress, kPlain) && Find(slipProgress, kPlain)->outcome == CC::Outcome::kOk,
                fmt::format("failed: a NIF nifs[] does not list fails its object alone ('{}'); an ok false path matched after "
                            "MeshDataPath ('{}'); the rest builds", slipModel ? slipModel->reason : "", slipMps ? slipMps->reason : ""));

            const auto otherExe = MakeFixture(a_dir, "exe", a_sourceNif, Variant::kNormal, 0.6, kGameRequestVersionKeyed, 0x0123456789ABCDEFull);
            const auto wrongTint = MakeFixture(a_dir, "tint", a_sourceNif, Variant::kNormal, 0.7);
            const auto v4 = MakeFixture(a_dir, "v4", a_sourceNif, Variant::kNormal, 0.6, kGameRequestVersion);
            std::error_code ec;
            a_check.Check(Run(otherExe) == 1 && Run(wrongTint) == 1 && Run(v4) == 2 && !std::filesystem::exists(otherExe.staging / CC::kProgressFile, ec) &&
                              !std::filesystem::exists(wrongTint.staging / CC::kProgressFile, ec) &&
                              !std::filesystem::exists(v4.staging / CC::kProgressFile, ec),
                "refused runs: VerifyKeyPrefix refuses keys for another builder exe and a prefix not of its options (exit 1, nothing "
                "written); a v4 request (exit 2)");
        }

        // This exe with a_args (output to nothing); its exit code
        [[nodiscard]] DWORD RunSelf(const std::wstring& a_args)
        {
            std::wstring command = L"\"" + ThisExecutable().wstring() + L"\" " + a_args;
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
                return MAXDWORD;
            }
            ::CloseHandle(process.hThread);
            if (::WaitForSingleObject(process.hProcess, kChildTimeoutMs) == WAIT_TIMEOUT) {
                ::TerminateProcess(process.hProcess, MAXDWORD);  // a hang is a failure, never a self-test that never ends
                ::WaitForSingleObject(process.hProcess, INFINITE);
            }
            DWORD code = MAXDWORD;
            ::GetExitCodeProcess(process.hProcess, &code);
            ::CloseHandle(process.hProcess);
            return code;
        }

        void CheckCommandLine(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
        {
            const auto fixture = MakeFixture(a_dir, "cli", a_sourceNif, Variant::kNormal);
            const std::wstring folder = L"\"" + fixture.folder.wstring() + L"\"";
            const std::wstring staging = L"\"" + fixture.staging.wstring() + L"\"";
            const auto key = [&](const std::wstring& a_more) { return RunSelf(L"--from-game " + folder + L" --staging " + staging + L" " + a_more); };
            a_check.Check(key(L"--tint 0.5") == 2 && key(L"--palette-mode godskin") == 2 && key(L"--out " + staging) == 2 &&
                              key(L"--job-memory 10") == 2 && RunSelf(L"--staging " + staging) == 2,
                "CLI: the key mode refuses --tint, --palette-mode, --out, a job memory under 64 MiB, and --staging without --from-game");
            const DWORD gone = RunSelf(L"--from-game " + folder + L" --staging " + staging + L" --parent-pid 4294967290");
            a_check.Check(gone == static_cast<DWORD>(kExitParentGone), fmt::format("CLI: a parent pid that names no process stops the run at "
                                                                                    "once (exit {})", gone));
            const DWORD full = key(L"");
            a_check.Check(full == 0 && CC::ParseProgress(ReadAll(fixture.staging / CC::kProgressFile)).lines.size() >= 5,
                fmt::format("CLI: --from-game <folder> --staging <dir> builds (exit {})", full));
            const auto collide = fixture.folder / "request.json";
            a_check.Check(RunSelf(L"--collisions \"" + collide.wstring() + L"\"") == 1, "CLI: --collisions exits 1 for a request with ADDN 40001");
        }

        void CheckDevMode(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
        {
            const auto fixture = MakeFixture(a_dir, "dev", a_sourceNif, Variant::kNormal);
            const auto out = a_dir / "dev_out";
            std::filesystem::create_directories(out);
            const int exit = RunFromGame(fixture.folder, out, kDefaultTint, false);
            std::error_code ec;
            a_check.Check(exit == 0 && std::filesystem::exists(out / kCopiesPluginName, ec),
                fmt::format("dev mode: --from-game --out still builds the copies ESL from a v5 request (exit {})", exit));
        }
    }

    void CheckKeyed(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        const auto dir = a_dir / "keyed";
        try {
            CheckRequest(a_check, dir, a_sourceNif);
            CheckFirstRuns(a_check, dir, a_sourceNif);
            CheckOutcomes(a_check, dir, a_sourceNif);
            CheckCommandLine(a_check, dir, a_sourceNif);
            CheckDevMode(a_check, dir, a_sourceNif);
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("key mode: exception: {}", e.what()));
        }
    }
}
