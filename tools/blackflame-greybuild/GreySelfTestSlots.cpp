// =============================================================================
// GreySelfTestSlots - --self-test of the single launch's parts on their own (GreySelfTest.h CheckSlotsAndProgress)
// =============================================================================
// The slot pool ESL (written, read back with both readers, a damaged copy refused, the shipped file the same bytes),
// --collisions, progress.jsonl (compaction, append, reuse), the memory budget (the rule, waits, cache clears, the estimate),
// the parent watch, the object guard and outcome, and the child key strings against the contract's own functions.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyKeyed.h"
#include "GreySelfTest.h"
#include "GreySlotPool.h"

#include "../../plugins/BlackFlame/src/CopyContractTest.h"

#include <DirectXTex.h>

#include <chrono>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <thread>

namespace GreyBuild::SelfTest
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;

        constexpr std::uint64_t kTestTextureEstimate = 109220;  // 64 x 64, 7 mips: 5461 texels x 4 bytes x 5 images
        constexpr std::uint64_t kMiB = kBytesPerMiB;
        constexpr std::size_t kTxstDodt = 72;
        constexpr std::size_t kTxstPlain = 24;

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

        // The top-level group labels of a plugin, in file order (a reader of its own, not the builder's)
        std::vector<std::string> GroupLabels(const std::string& a_bytes)
        {
            std::vector<std::string> labels;
            if (a_bytes.size() < 24) {
                return { "?" };
            }
            std::uint32_t tes4 = 0;
            std::memcpy(&tes4, a_bytes.data() + 4, sizeof(tes4));
            std::size_t at = 24 + static_cast<std::size_t>(tes4);
            for (; at + 24 <= a_bytes.size();) {
                std::uint32_t size = 0;
                std::memcpy(&size, a_bytes.data() + at + 4, sizeof(size));
                if (a_bytes.compare(at, 4, "GRUP") != 0 || size < 24 || size > a_bytes.size() - at) {
                    labels.push_back("?");
                    break;
                }
                labels.push_back(a_bytes.substr(at + 8, 4));
                at += size;
            }
            if (at != a_bytes.size()) {
                labels.push_back("?");  // bytes after the last group, or a group cut short
            }
            return labels;
        }

        void CheckPool(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto file = a_dir / "slots" / kSlotsPluginName;
            WriteSlotPool(file);
            const auto problems = VerifySlotPool(file);
            a_check.Check(problems.empty(), fmt::format("slot pool: written and read back by both readers ({} problem(s){})", problems.size(),
                problems.empty() ? std::string() : ": " + problems.front()));
            EslHeader header;
            const auto forms = ReadCopiesEsl(file, header);
            std::map<std::string, int> perType;
            int withDodt = 0;
            int withoutDodt = 0;
            for (const auto& form : forms) {
                ++perType[form.type];
                if (form.type == "TXST") {
                    (form.hasDodt ? withDodt : withoutDodt) += 1;
                }
            }
            a_check.Check(perType["PROJ"] == 480 && perType["IPCT"] == 480 && perType["EXPL"] == 320 && perType["ARTO"] == 224 &&
                              perType["HAZD"] == 192 && perType["TXST"] == 96 && perType["ADDN"] == 208 && withDodt == static_cast<int>(kTxstDodt) &&
                              withoutDodt == static_cast<int>(kTxstPlain) && (header.flags & kEslFlag) != 0 && header.masters == 0 &&
                              header.nextObjectId == 0xFD0,
                fmt::format("slot pool: PROJ {} IPCT {} EXPL {} ARTO {} HAZD {} TXST {} ({} with DODT) ADDN {}; ESL, no masters, next id {:X}",
                    perType["PROJ"], perType["IPCT"], perType["EXPL"], perType["ARTO"], perType["HAZD"], perType["TXST"], withDodt,
                    perType["ADDN"], header.nextObjectId));
            std::uint32_t flags = 0;
            const auto addons = ReadAddonEsl(file, flags);
            a_check.Check(addons.size() == 208 && addons.front().formId == 0xF00 && addons.front().index == 40000 &&
                              addons.back().formId == 0xFCF && addons.back().index == 40207 && addons.front().dnamFlags == 1 &&
                              addons.front().model == kSlotAddnModel,
                "slot pool: ADDN 0xF00 = index 40000 .. 0xFCF = 40207, DNAM flags 1, the placeholder model");
            const auto bytes = ReadAll(file);
            a_check.Check(GroupLabels(bytes) == std::vector<std::string>{ "TXST", "PROJ", "HAZD", "EXPL", "ADDN", "IPCT", "ARTO" },
                "slot pool: groups in Skyrim.esm's order TXST PROJ HAZD EXPL ADDN IPCT ARTO");
            // a damaged copy: one ADDN index changed, then a file cut short
            std::string damaged = bytes;
            const std::string index40001("\x41\x9C\x00\x00", 4);  // 40001 little-endian
            const auto at = damaged.find(index40001);
            if (at != std::string::npos) {
                damaged[at] = '\x42';
            }
            Write(a_dir / "slots" / "damaged.esp", damaged);
            Write(a_dir / "slots" / "cut.esp", bytes.substr(0, bytes.size() / 2));
            a_check.Check(at != std::string::npos && !VerifySlotPool(a_dir / "slots" / "damaged.esp").empty() &&
                              !VerifySlotPool(a_dir / "slots" / "cut.esp").empty(),
                "slot pool: a changed ADDN index and a file cut short are refused");
            // build\tools\Release\ in the repository: plugins\BlackFlame\data three folders up must hold the committed file
            const auto repoData = ThisExecutable().parent_path() / ".." / ".." / ".." / "plugins" / "BlackFlame" / "data";
            std::error_code ec;
            if (std::filesystem::is_directory(repoData, ec)) {
                const auto committed = repoData / kSlotsPluginName;
                a_check.Check(std::filesystem::exists(committed, ec) && ReadAll(committed) == bytes,
                    fmt::format("slot pool: the committed {} is there and is what this builder writes", PathText(committed)));
            } else {
                a_check.Check(true, "slot pool: not run from the repository's build folder (the committed file was not compared)");
            }
        }

        void CheckCollisions(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto request = Write(a_dir / "collide.json", R"({"version":4,"addons":[
 {"index":39999,"model":"a.nif","source":"A.esp|000801","own":false},
 {"index":40000,"model":"b.nif","source":"B.esp|000802","own":false},
 {"index":40003,"model":"c.nif","source":"HeartOfMagic_BlackFlame_Slots.esp|000F03","own":true},
 {"index":40207,"model":"d.nif","source":"D.esp|000803"},
 {"index":40208,"model":"e.nif","source":"E.esp|000804","own":false}],"models":[]})");
            const auto found = FindAddnCollisions(request);
            a_check.Check(found.size() == 2 && found[0].index == 40000 && found[0].slot == 0xF00 && found[0].source == "B.esp|000802" &&
                              found[1].index == 40207 && found[1].slot == 0xFCF,
                fmt::format("collisions: 40000 and 40207 listed with their slots; 39999, 40208 and our own record not ({} found)", found.size()));
            bool threw = false;
            try {
                (void)FindAddnCollisions(Write(a_dir / "collide_bad.json", R"({"addons":[{"index":"x"}]})"));
            } catch (const std::runtime_error&) {
                threw = true;
            }
            a_check.Check(threw, "collisions: a request whose index is no integer is refused");
        }

        [[nodiscard]] std::string LineText(const CC::ProgressLine& a_line)
        {
            return CC::WriteProgressLine(a_line).value_or("");
        }

        [[nodiscard]] CC::ProgressLine FileLine(std::uint64_t a_key, const std::string& a_path, std::uint64_t a_bytes, std::uint64_t a_fnv,
            CC::Outcome a_outcome = CC::Outcome::kOk)
        {
            CC::ProgressLine line;
            line.kind = CC::ProgressKind::kDds;
            line.key = a_key;
            line.path = a_path;
            line.bytes = a_bytes;
            line.fnv64 = a_fnv;
            line.outcome = a_outcome;
            return line;
        }

        void CheckProgress(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto staging = a_dir / "progress";
            const std::uint64_t kA = 0xAAAAAAAAAAAAAAA1ull;
            const std::uint64_t kB = 0xBBBBBBBBBBBBBBB2ull;
            const std::uint64_t kC = 0xCCCCCCCCCCCCCCC3ull;
            const std::string pathA = BlackFlame::DataPathRule::CopyPath(BlackFlame::DataPathRule::kTexturesRoot, CC::Hex16(kA), "a.dds").value();
            const std::string pathB = BlackFlame::DataPathRule::CopyPath(BlackFlame::DataPathRule::kTexturesRoot, CC::Hex16(kB), "b.dds").value();
            const std::string pathC = BlackFlame::DataPathRule::CopyPath(BlackFlame::DataPathRule::kTexturesRoot, CC::Hex16(kC), "c.dds").value();
            Write(StagedFile(staging, pathA), "AAAA");
            Write(StagedFile(staging, pathA + kTempFileSuffix), "left by a killed run");
            Write(staging / "someone else's.tmp", "not ours");
            const auto fnvA = CC::Fnv64("AAAA");
            CC::ProgressLine failed;
            failed.kind = CC::ProgressKind::kNif;
            failed.key = kC;
            failed.outcome = CC::Outcome::kFailed;
            failed.reason = "io";
            const std::string text = LineText(FileLine(kA, pathA, 3, fnvA)) + LineText(FileLine(kA, pathA, 4, fnvA)) + "{\"v\":7}\n" +
                                     LineText(FileLine(kB, pathB, 4, 1)) + LineText(FileLine(kC, pathC, 4, CC::Fnv64("CCCC"))) + LineText(failed) + LineText(FileLine(kC, pathC, 4, fnvA)).substr(0, 20);
            Write(StagedFile(staging, pathC), "CCCC");  // its ok line comes before its failed one: it must not come back
            Write(staging / CC::kProgressFile, text);
            a_check.Check(RemoveTempFiles(staging) == 1 && !std::filesystem::exists(StagedFile(staging, pathA + kTempFileSuffix)) &&
                              std::filesystem::exists(StagedFile(staging, pathA)) &&
                              std::filesystem::exists(staging / "someone else's.tmp"),
                "progress: a leftover .tmp in our copies folder is removed; the copy beside it and a .tmp outside it kept");
            {
                ProgressLog log;
                std::string why;
                const bool opened = log.Open(staging, why);
                const auto& c = log.LastCompaction();
                const auto back = CC::ParseProgress(ReadAll(staging / CC::kProgressFile));
                a_check.Check(opened && c.read == 5 && c.kept == 1 && c.badLines == 1 && c.cutLastLine && back.lines.size() == 1 &&
                                  back.errors.empty() && !back.cutLastLine && back.lines[0].key == kA && back.lines[0].bytes == 4 &&
                                  log.Last(kB) == nullptr && log.Last(kC) == nullptr,
                    fmt::format("progress: compaction keeps the last line per key, drops a key whose last line failed (its older ok line too), a line "
                                "whose file is gone, a bad "
                                "and a cut line (read {}, kept {}, bad {}, cut {}{})", c.read, c.kept, c.badLines, c.cutLastLine,
                        why.empty() ? std::string() : "; " + why));
                a_check.Check(log.Reusable(kA, kMiB) && !log.Reusable(kB, kMiB), "progress: a line whose file matches its fnv64 is reusable");
                CC::ProgressLine large;
                large.kind = CC::ProgressKind::kDds;
                large.key = kB;
                large.outcome = CC::Outcome::kTooLarge;
                large.reason = "over the job memory cap";
                large.cap = kMiB;
                a_check.Check(log.Append(large, why) && log.Reusable(kB, kMiB) && !log.Reusable(kB, 2 * kMiB),
                    "progress: a too_large line is reused while the cap is not larger, retried under a larger cap");
                CC::ProgressLine refused = FileLine(kC, "meshes\\not\\a\\copy.nif", 1, 1);
                a_check.Check(!log.Append(refused, why) && !why.empty() && !log.Broken(),
                    "progress: a line the contract's writer refuses is not appended, and the log is not latched broken by it");
            }
            const auto again = CC::ParseProgress(ReadAll(staging / CC::kProgressFile));
            a_check.Check(again.lines.size() == 2 && again.errors.empty() && again.lines[1].outcome == CC::Outcome::kTooLarge,
                "progress: an appended line reads back whole after the log is closed");
            Write(StagedFile(staging, pathA), "AAAB");
            ProgressLog changed;
            std::string why;
            a_check.Check(changed.Open(staging, why) && !changed.Reusable(kA, kMiB) && changed.Reusable(kB, kMiB) && !changed.Reusable(kB, 2 * kMiB),
                "progress: a staged file whose bytes changed is not reused; a too_large line survives the next compaction with its cap");

            // the DLL reading progress.jsonl (shared read, write and delete) while the builder starts: the compaction's
            // rename replaces the file under that handle, and the reader's handle still reads the old bytes
            const auto held = (staging / CC::kProgressFile).wstring();
            const HANDLE reader = ::CreateFileW(held.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            // `changed` still holds its append handle too; the compaction writes and renames whatever it keeps (b's line)
            ProgressLog whileHeld;
            std::string heldWhy;
            const bool heldOpened = reader != INVALID_HANDLE_VALUE && whileHeld.Open(staging, heldWhy);
            char first = 0;
            DWORD got = 0;
            const bool readerReads = reader != INVALID_HANDLE_VALUE && ::ReadFile(reader, &first, 1, &got, nullptr) != FALSE && got == 1;
            if (reader != INVALID_HANDLE_VALUE) {
                ::CloseHandle(reader);
            }
            a_check.Check(heldOpened && readerReads && first == '{' && whileHeld.LastCompaction().kept == 1,
                fmt::format("progress: compaction renames over a progress.jsonl another handle holds with FILE_SHARE_DELETE (POSIX "
                            "rename), the holder still reads{}", heldWhy.empty() ? std::string() : " (" + heldWhy + ")"));

            // (disk trim) staging\published.tsv as the DLL writes it (the contract's writer): a listed file counts as staged
            {
                const auto trim = a_dir / "progress-published";
                std::filesystem::create_directories(trim);
                const auto fnvC = CC::Fnv64("CCCC");
                CC::ProgressLine dep = FileLine(kC, pathC, 4, fnvC);
                dep.deps.push_back({ false, 0, pathB, CC::Fnv64("BBBB") });
                Write(trim / CC::kProgressFile, LineText(FileLine(kA, pathA, 4, fnvA)) + LineText(FileLine(kB, pathB, 4, CC::Fnv64("BBBB"))) +
                                                    LineText(dep));
                const auto list = CC::WritePublished({ { pathA, { 4, fnvA } }, { pathB, { 4, CC::Fnv64("BBBB") } }, { pathC, { 4, fnvA } } });
                Write(trim / CC::kPublishedFile, list.value_or(""));
                ProgressLog listed;
                std::string listedWhy;
                const bool opened = listed.Open(trim, listedWhy);
                a_check.Check(opened && list && listed.LastCompaction().kept == 2 && listed.Reusable(kA, kMiB) && listed.Reusable(kB, kMiB) &&
                                  !listed.Reusable(kC, kMiB),
                    fmt::format("progress: with no staging file, a line whose file published.tsv lists with its bytes is kept and reused; one "
                                "listed with other bytes is dropped (kept {}{})", listed.LastCompaction().kept,
                        listedWhy.empty() ? std::string() : "; " + listedWhy));
                Write(StagedFile(trim, pathC), "CCCC");
                ProgressLog staged;
                a_check.Check(staged.Open(trim, listedWhy) && !staged.Reusable(kC, kMiB),
                    "progress: a line dropped once does not come back when its staging file shows up again (the next run builds it)");
                Write(trim / CC::kProgressFile, LineText(dep));
                ProgressLog depListed;
                a_check.Check(depListed.Open(trim, listedWhy) && depListed.Reusable(kC, kMiB),
                    "progress: a staged file whose copy dep is only listed (its staging file trimmed) is reused");
                Write(trim / CC::kPublishedFile, "# blackflame published v0\n" + pathB + "\t4\t" + CC::Hex16(CC::Fnv64("BBBB")) + "\n");
                ProgressLog oldList;
                a_check.Check(oldList.Open(trim, listedWhy) && !oldList.Reusable(kC, kMiB),
                    "progress: a list with another header vouches for nothing (staging alone counts)");
                std::filesystem::remove(trim / CC::kPublishedFile);
                ProgressLog noList;
                a_check.Check(noList.Open(trim, listedWhy) && !noList.Reusable(kC, kMiB),
                    "progress: no published.tsv (a DLL before the disk trim): a dep missing from staging is rebuilt, as before");
            }
        }

        void CheckBudget(Checker& a_check, const std::filesystem::path& a_dir)
        {
            constexpr std::uint64_t kCap = 100;
            a_check.Check(Admit(101, 0, kCap, 1000, 10) == Admission::kTooLarge && Admit(100, 0, kCap, 1000, 10) == Admission::kAdmit &&
                              Admit(50, 0, kCap, 59, 10) == Admission::kPause && Admit(50, 0, kCap, 60, 10) == Admission::kAdmit &&
                              Admit(50, 51, kCap, 1000, 10) == Admission::kOverCap && Admit(50, 50, kCap, 1000, 10) == Admission::kAdmit,
                "budget: the rule - over the cap alone too large, commit short of reserve + estimate pause, in use + estimate over the cap");
            int probes = 0;
            int sleeps = 0;
            Budget waits(kCap, 10, nullptr, nullptr, [&]() -> std::optional<MemoryState> {
                return MemoryState{ 0, ++probes < 3 ? 5u : 1000u };
            }, [&](unsigned) { ++sleeps; });
            std::string why;
            a_check.Check(waits.Acquire(20, why) == Admission::kAdmit && sleeps == 2 && waits.Pauses() == 1,
                fmt::format("budget: short commit waits ({} sleeps) and goes on when it is back", sleeps));
            TextureSource cache({}, {});
            RgbaImage image;
            image.width = 2;
            image.height = 2;
            image.pixels.assign(16, 0);
            cache.Put("textures\\x.dds", image);
            bool cleared = false;
            Budget evicts(kCap, 0, &cache, nullptr, [&]() -> std::optional<MemoryState> {
                cleared = cache.CachedBytes() == 0;
                return MemoryState{ cleared ? 0u : 90u, 1000 };
            }, [](unsigned) {});
            const auto before = cache.CachedBytes();
            a_check.Check(before == 16 && evicts.Acquire(20, why) == Admission::kAdmit && cleared && evicts.Evictions() == 1,
                "budget: over the cap, the texture cache is cleared once and the texture admitted");
            Budget over(kCap, 0, nullptr, nullptr, []() { return std::optional<MemoryState>(MemoryState{ 90, 1000 }); }, [](unsigned) {});
            std::string overWhy;
            std::string largeWhy;
            a_check.Check(over.Acquire(20, overWhy) == Admission::kOverCap && !overWhy.empty() &&
                              over.Acquire(200, largeWhy) == Admission::kTooLarge && largeWhy.find(std::to_string(kCap) + " bytes") != std::string::npos,
                "budget: still over the cap is over (the object fails); a too_large reason names its cap");
            std::atomic<bool> stop{ true };
            Budget stopped(kCap, 10, nullptr, &stop, []() { return std::optional<MemoryState>(MemoryState{ 0, 0 }); }, [](unsigned) {});
            a_check.Check(stopped.Acquire(20, why) == Admission::kOverCap, "budget: a stop (the parent gone) ends a wait");
            const auto dds = a_dir / "budget" / "t.dds";
            std::string estimateWhy;
            a_check.Check(WriteTestTexture(dds, DXGI_FORMAT_BC3_UNORM, true) && EstimateTextureBytes(dds, estimateWhy) == kTestTextureEstimate &&
                              !EstimateTextureBytes(Write(a_dir / "budget" / "no.dds", "not a dds"), estimateWhy),
                fmt::format("budget: a 64 x 64 BC3 with 7 mips estimates {} bytes (decoded chain x 5); a file that is no DDS has none",
                    kTestTextureEstimate));
            cache.Put("textures\\y.dds", image);
            const bool held = cache.CachedBytes() == 16;
            cache.Clear();
            a_check.Check(held && cache.CachedBytes() == 0, "budget: Clear empties the texture cache");
        }

        // A process that ends at once: this exe with no arguments (it prints its usage and exits 2)
        [[nodiscard]] std::optional<std::uint32_t> EndedProcess()
        {
            std::wstring command = L"\"" + ThisExecutable().wstring() + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
                return std::nullopt;
            }
            ::CloseHandle(process.hThread);
            ::WaitForSingleObject(process.hProcess, INFINITE);
            ::CloseHandle(process.hProcess);  // the last handle: the pid now names no process (unless Windows reuses it)
            return static_cast<std::uint32_t>(process.dwProcessId);
        }

        void CheckParentWatch(Checker& a_check)
        {
            std::string why;
            ParentWatch self;
            const bool started = self.Start(::GetCurrentProcessId(), kParentGraceMs, why);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            a_check.Check(started && !self.Gone(), "parent watch: a parent that lives is not gone");
            // a child that has ended but whose handle this process still holds: the watch opens it and sees it gone
            std::wstring command = L"\"" + ThisExecutable().wstring() + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            bool gone = false;
            if (::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
                ::CloseHandle(process.hThread);
                ParentWatch child;
                why.clear();
                if (child.Start(process.dwProcessId, kParentGraceMs, why)) {
                    for (int i = 0; i < 100 && !child.Gone(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    gone = child.Gone();
                } else {
                    gone = child.NoProcess() && !why.empty();  // it had ended already: Start says so at once
                }
                ::CloseHandle(process.hProcess);
            }
            a_check.Check(gone, "parent watch: a parent that ends is gone within 5 s");
            why.clear();
            const auto ended = EndedProcess();
            // Windows may keep an ended process's object a moment (another handle to it): then the watch opens it and finds it
            // gone at once; else the pid opens nothing and NoProcess says so
            ParentWatch none;
            const bool watching = ended && none.Start(*ended, kParentGraceMs, why);
            for (int i = 0; i < 100 && watching && !none.Gone(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            a_check.Check(ended && (watching ? none.Gone() : none.NoProcess() && !why.empty()),
                "parent watch: an ended parent's pid stops the run at once (no process, or one already gone)");
        }

        void CheckGuardAndOutcome(Checker& a_check)
        {
            KeyedSpec spec;
            spec.key = 0x1234567890ABCDEFull;
            const auto oom = GuardObject(spec, []() -> CC::ProgressLine { throw std::bad_alloc(); });
            const auto other = GuardObject(spec, []() -> CC::ProgressLine { throw std::runtime_error("disk gone"); });
            const auto fine = GuardObject(spec, [&]() {
                CC::ProgressLine line;
                line.key = spec.key;
                line.outcome = CC::Outcome::kFallback;
                return line;
            });
            a_check.Check(oom.outcome == CC::Outcome::kFailed && oom.key == spec.key && oom.reason.find("out of memory") != std::string::npos &&
                              other.outcome == CC::Outcome::kFailed && other.reason == "disk gone" && fine.outcome == CC::Outcome::kFallback,
                "object guard: bad_alloc and other exceptions make that key's line failed; a line built is passed on");
            KeyedObject object;
            a_check.Check(ClassifyObject(object).first == CC::Outcome::kOk, "outcome: nothing said is ok");
            object.fallbacks = { "a", "b", "c", "d" };
            const auto fallback = ClassifyObject(object);
            object.terminal = CC::Outcome::kNotDumped;
            object.terminalWhy = "outside the caps";
            const auto terminal = ClassifyObject(object);
            object.failed = "io";
            const auto failed = ClassifyObject(object);
            a_check.Check(fallback.first == CC::Outcome::kFallback && fallback.second == "a; b; c (and 1 more)" &&
                              terminal.first == CC::Outcome::kNotDumped && terminal.second == "outside the caps" &&
                              failed.first == CC::Outcome::kFailed && failed.second == "io",
                "outcome: failed before terminal before fallback (reasons joined, three named) before ok");
            object.AddDep("textures\\x.dds", 1);
            object.AddDep("textures\\x.dds", 1);
            a_check.Check(object.deps.size() == 1, "outcome: a dep named twice is listed once");
            a_check.Check(IsTransientResult(E_OUTOFMEMORY) && IsTransientResult(HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)) &&
                              IsTransientResult(HRESULT_FROM_WIN32(ERROR_DISK_FULL)) && !IsTransientResult(E_FAIL) &&
                              !IsTransientResult(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)) && !IsTransientResult(E_INVALIDARG),
                "outcome: out of memory, a locked file and a full disk are retryable; a bad or unsupported DDS is not");
            a_check.Check(IsTransientResult(HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES)) && IsTransientResult(HRESULT_FROM_WIN32(ERROR_DELETE_PENDING)) &&
                              IsTransientResult(HRESULT_FROM_WIN32(ERROR_NETNAME_DELETED)) && IsTransientWriteResult(E_FAIL) &&
                              IsTransientWriteResult(HRESULT_FROM_WIN32(ERROR_DISK_FULL)) && IsTransientWriteResult(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) &&
                              !IsTransientWriteResult(E_INVALIDARG) && !IsTransientWriteResult(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)),
                "outcome: too many open files, a pending delete or a lost share retry a read; a write fails retryable (a short write "
                "E_FAIL, a full disk, a locked target) unless it is E_INVALIDARG or not supported");
        }

        void CheckChildKeys(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto prefix = BlackFlame::CopyContractTest::Detail::Prefix();
            const auto text = CC::KeyPrefixString(prefix).value();
            const auto mode = std::string(CC::PaletteModeText(prefix.paletteMode));
            const auto verify = [&](const std::string& a_identity) {
                return CC::VerifyKeyPrefix(text, prefix.tint, prefix.tintAdd, mode, a_identity, prefix.gradBlackFlame,
                    prefix.gradBlackFlameAdd);
            };
            const std::string other = std::to_string(CC::kBuilderOutputVersion + 1);
            a_check.Check(verify(prefix.outputIdentity) && !verify(other) && !verify(other + "+dev0123456789abcdef") &&
                              !verify(std::to_string(CC::kBuilderOutputVersion)) && !verify("1+dev0123456789abcdee"),
                "key prefix: VerifyKeyPrefix takes the prefix this builder rebuilds, refuses another output identity's (another "
                "version, release or dev, the same version's release form, another digest)");
            auto own = prefix;
            own.outputIdentity = OutputIdentity();
            const auto ownText = CC::KeyPrefixString(own);
            a_check.Check(CC::OutputIdentityVersion(OutputIdentity()) == CC::kBuilderOutputVersion && ownText &&
                              CC::VerifyKeyPrefix(*ownText, own.tint, own.tintAdd, mode, OutputIdentity(), own.gradBlackFlame,
                                  own.gradBlackFlameAdd),
                fmt::format("key prefix: this build's output identity '{}' is valid, of this contract's version {}, and keys",
                    OutputIdentity(), CC::kBuilderOutputVersion));
            // our palettes are there, but the request lists neither: they are never read (a stale dump file)
            const auto dir = a_dir / "palette-state";
            Write(dir / std::string(BlackFlame::DataPathRule::kOurPalette), "stale");
            Write(dir / std::string(BlackFlame::DataPathRule::kOurPaletteAdd), "stale");
            const std::string received = "format=BFK4|out=1|tint=0|tintadd=0|palette=derived|grad=notdumped:4096|gradadd=missing";
            a_check.Check(PaletteState(dir, BlackFlame::DataPathRule::kOurPalette, received, "grad", {}).state == CC::FileState::kNotDumped &&
                              PaletteState(dir, BlackFlame::DataPathRule::kOurPalette, received, "grad", {}).size == 4096 &&
                              PaletteState(dir, BlackFlame::DataPathRule::kOurPaletteAdd, received, "gradadd", {}).state == CC::FileState::kMissing,
                "key prefix: a palette the request does not list as dumped (its stale file there unread) is notdumped:<size> when the "
                "DLL says so, else missing");

            // an object that has failed tries none of its remaining slots; the next object does
            const auto staging = a_dir / "failed-object";
            std::filesystem::create_directories(staging);
            ProgressLog log;
            std::string why;
            const bool opened = log.Open(staging, why);
            Budget budget(kMiB * 1024, 0, nullptr, nullptr);
            TextureJob textures;
            textures.inputs = { staging / "dump" };
            const std::string listed = "textures\\gone.dds";  // listed as dumped, not in the dump: a failure when tried
            KeyedOutputs outputs(log, budget, prefix, textures, { listed }, {});
            KeyedObject failed;
            failed.failed = "an earlier slot";
            outputs.Begin(&failed);
            const auto skipped = outputs.Texture(listed, TextureRole::kDiffuse, 0.6f, "test");
            KeyedObject next;
            outputs.Begin(&next);
            const auto tried = outputs.Texture(listed, TextureRole::kDiffuse, 0.6f, "test");
            a_check.Check(opened && skipped.empty() && failed.failed == "an earlier slot" && outputs.ObjectFailed() && tried.empty() &&
                              next.failed.find("listed as dumped") != std::string::npos,
                fmt::format("key outputs: a failed object's next slot is not tried (its reason kept), the next object's is ('{}')",
                    next.failed));
            outputs.Begin(nullptr);
        }
    }

    void CheckSlotsAndProgress(Checker& a_check, const std::filesystem::path& a_dir)
    {
        try {
            CheckPool(a_check, a_dir);
            CheckCollisions(a_check, a_dir);
            CheckProgress(a_check, a_dir);
            CheckBudget(a_check, a_dir);
            CheckParentWatch(a_check);
            CheckGuardAndOutcome(a_check);
            CheckChildKeys(a_check, a_dir);
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("single launch parts: exception: {}", e.what()));
        }
    }
}
