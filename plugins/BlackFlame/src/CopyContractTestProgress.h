#pragma once

// =============================================================================
// COPY CONTRACT TEST PROGRESS - the progress line cases of the shared test table (CopyContractTest.h)
// =============================================================================
// Each refusal case changes one field of a line the parser takes, so it fails only for that field.

#include "CopyContractProgress.h"

#include <string>

namespace BlackFlame::CopyContractTest
{
    namespace ProgressDetail
    {
        namespace CC = CopyContract;

        inline constexpr const char* kNifPath = "meshes\\heartofmagic\\blackflame\\copies\\0123456789abcdef\\copy.nif";
        inline constexpr const char* kDdsPath = "textures\\heartofmagic\\blackflame\\copies\\fedcba9876543210\\copy.dds";
        // The JSON text of those paths
        inline constexpr const char* kNifJson = "meshes\\\\heartofmagic\\\\blackflame\\\\copies\\\\0123456789abcdef\\\\copy.nif";
        inline constexpr const char* kDdsJson = "textures\\\\heartofmagic\\\\blackflame\\\\copies\\\\fedcba9876543210\\\\copy.dds";

        [[nodiscard]] inline CC::ProgressLine NifLine()
        {
            CC::ProgressLine line;
            line.kind = CC::ProgressKind::kNif;
            line.key = 0x0123456789ABCDEFull;
            line.path = kNifPath;
            line.bytes = 12345;
            line.fnv64 = 0x8888888888888888ull;
            line.outcome = CC::Outcome::kOk;
            CC::ProgressDep texture;
            texture.path = kDdsPath;
            texture.fnv64 = 0x9999999999999999ull;
            CC::ProgressDep addn;
            addn.isAddn = true;
            addn.addn = 40012;
            line.deps = { texture, addn };
            return line;
        }

        /// An ok nif line the parser takes, with a_kind, a_path, a_bytes, a_fnv, a_outcome and a_extra replacing parts
        [[nodiscard]] inline std::string Line(const std::string& a_head, const std::string& a_path = std::string("\"") + kNifJson + "\"",
            const std::string& a_bytes = "1", const std::string& a_fnv = "\"0000000000000001\"", const std::string& a_outcome = "\"ok\"",
            const std::string& a_extra = "")
        {
            std::string text = "{" + a_head;
            if (!a_path.empty()) {
                text += ",\"path\":" + a_path;
            }
            if (!a_bytes.empty()) {
                text += ",\"bytes\":" + a_bytes;
            }
            if (!a_fnv.empty()) {
                text += ",\"fnv64\":" + a_fnv;
            }
            return text + ",\"outcome\":" + a_outcome + ",\"reason\":\"io\"" + a_extra + "}";
        }

        inline const std::string kHead = "\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\"";

        [[nodiscard]] inline bool Takes(const std::string& a_text)
        {
            return CC::ParseProgressLine(a_text).line.has_value();
        }
    }

    template <class Check>
    void CheckProgress(Check& a_check)
    {
        using namespace ProgressDetail;
        const auto written = CC::WriteProgressLine(NifLine());
        const std::string golden = std::string("{\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"path\":\"") + kNifJson +
                                   "\",\"bytes\":12345,\"fnv64\":\"8888888888888888\",\"outcome\":\"ok\",\"reason\":\"\",\"deps\":[{\"path\":\"" +
                                   kDdsJson + "\",\"fnv64\":\"9999999999999999\"},{\"addn\":40012}]}\n";
        a_check(written == golden, "progress: the writer's line, fields in contract order, '\\n' at the end (" +
                                       written.value_or("refused") + ")");
        const auto back = written ? CC::ParseProgress(*written) : CC::ProgressFile{};
        a_check(back.lines.size() == 1 && back.errors.empty() && !back.cutLastLine && back.lines[0] == NifLine(),
            "progress: a written line reads back field for field (kind, key, path, bytes, fnv64, outcome, reason, deps)");
        auto failed = NifLine();
        failed.outcome = CC::Outcome::kFailed;
        failed.path.clear();
        failed.bytes = 0;
        failed.fnv64 = 0;
        failed.reason = "io";
        failed.deps.clear();
        a_check(CC::WriteProgressLine(failed) ==
                    std::string("{\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"outcome\":\"failed\",\"reason\":\"io\",\"deps\":[]}\n"),
            "progress: a line without a file has no path, bytes or fnv64");

        const std::string line = written.value_or("x\n");
        const std::string bare = line.substr(0, line.size() - 1);
        const auto cut = CC::ParseProgress(line + line + line.substr(0, line.size() / 2));
        const auto crlf = CC::ParseProgress(bare + "\r\n\n  \n" + line + " \r");
        a_check(cut.lines.size() == 2 && cut.errors.empty() && cut.cutLastLine && crlf.lines.size() == 2 && crlf.errors.empty() &&
                    !crlf.cutLastLine,
            "progress: a last line without '\\n' is ignored; CRLF, blank lines and blanks after the last '\\n' are no cut line");
        a_check(CC::ProgressValidPrefix(line + "{\"v\":1,\"ki") == line.size() && CC::ProgressValidPrefix("{\"v\"") == 0 &&
                    CC::ProgressValidPrefix(line + line) == 2 * line.size(),
            "progress: ProgressValidPrefix keeps up to the last '\\n' (what the builder keeps before appending)");
        const auto middle = CC::ParseProgress(line + "{\"v\":2}\n" + line);
        a_check(middle.lines.size() == 2 && middle.errors.size() == 1 && middle.errors[0].lineNumber == 2,
            "progress: a bad middle line is reported with its number; the lines around it still count");

        const std::string ok = Line(kHead);
        a_check(Takes(ok) && !Takes(Line("\"v\":2,\"kind\":\"nif\",\"key\":\"0123456789abcdef\"")) &&
                    !Takes(Line("\"kind\":\"nif\",\"key\":\"0123456789abcdef\"")) &&
                    !Takes(Line("\"v\":\"1\",\"kind\":\"nif\",\"key\":\"0123456789abcdef\"")) &&
                    !Takes(Line("\"v\":1.0,\"kind\":\"nif\",\"key\":\"0123456789abcdef\"")) &&
                    !Takes(Line("\"v\":-1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\"")) &&
                    !Takes(Line("\"v\":18446744073709551617,\"kind\":\"nif\",\"key\":\"0123456789abcdef\"")) && !Takes("not json") &&
                    !Takes("[1]") && !Takes(ok + "x") && !Takes(ok.substr(0, ok.size() - 1)),
            "progress: v must be the unsigned number 1; no JSON object, trailing text or a cut object refused");
        a_check(!Takes(Line("\"v\":1,\"kind\":\"esp\",\"key\":\"0123456789abcdef\"")) &&
                    !Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "1", "\"0000000000000001\"", "\"done\"")) &&
                    !Takes(Line("\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789ABCDEF\"")) &&
                    !Takes(Line("\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcde\"")) &&
                    !Takes(Line("\"v\":1,\"kind\":7,\"key\":\"0123456789abcdef\"")),
            "progress: an unknown kind or outcome, an upper-case or short key, a kind that is no string refused");
        a_check(!Takes(Line(kHead, "123")) && !Takes(Line(kHead, "null")) &&
                    !Takes("{" + kHead + ",\"outcome\":\"failed\",\"reason\":false}") &&
                    Takes("{" + kHead + ",\"outcome\":\"failed\",\"reason\":\"io\"}") && !Takes("{" + kHead + ",\"v\":1,\"outcome\":\"failed\"}") &&
                    !Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "1", "\"0000000000000001\"", "\"ok\"", ",\"deps\":[{\"addn\":40000,\"addn\":40001}]")),
            "progress: a path or reason that is no string, a key given twice (top level or in a dep) refused");
        a_check(!Takes(Line(kHead, std::string("\"") + kDdsJson + "\"")) &&
                    !Takes(Line(kHead, "\"meshes\\\\heartofmagic\\\\blackflame\\\\copies\\\\fedcba9876543210\\\\copy.nif\"")) &&
                    !Takes(Line(kHead, "\"meshes\\\\heartofmagic\\\\blackflame\\\\copies\\\\0123456789abcdef\\\\copy.dds\"")) &&
                    !Takes(Line(kHead, "\"meshes\\\\magic\\\\a.nif\"")) &&
                    !Takes(Line(kHead, "\"meshes\\\\heartofmagic\\\\blackflame\\\\copies\\\\0123456789abcdef\\\\copy.n\\u0000f\"")) &&
                    !Takes(Line(kHead, "\"meshes\\\\heartofmagic\\\\blackflame\\\\copies\\\\0123456789abcdef\\\\copy.nif\\u00e9\"")) &&
                    !Takes(Line("\"v\":1,\"kind\":\"txst\",\"key\":\"0123456789abcdef\"")) &&
                    Takes(Line("\"v\":1,\"kind\":\"mps\",\"key\":\"0123456789abcdef\"")),
            "progress: the path must be the line's own copy path with the kind's root and extension (no NUL, ASCII); txst has none");
        a_check(!Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "")) && !Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "0")) &&
                    !Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "-1")) &&
                    !Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "18446744073709551616")) &&
                    !Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "1", "")) &&
                    !Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "1", "\"0000000000000000\"")) &&
                    !Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "1", "\"000000000000001\"")) &&
                    !Takes(Line(kHead, "", "", "", "\"fallback\"")),
            "progress: an ok or fallback file line needs path, bytes > 0 (unsigned, in range) and a real 16-digit fnv64");
        const auto dep = [&](const std::string& a_dep, const std::string& a_head = kHead) {
            return Takes(Line(a_head, std::string("\"") + kNifJson + "\"", "1", "\"0000000000000001\"", "\"ok\"", ",\"deps\":[" + a_dep + "]"));
        };
        const std::string ddsHead = "\"v\":1,\"kind\":\"dds\",\"key\":\"fedcba9876543210\"";
        const auto ddsDep = [&](const std::string& a_dep) {
            return Takes(Line(ddsHead, std::string("\"") + kDdsJson + "\"", "1", "\"0000000000000001\"", "\"ok\"", ",\"deps\":[" + a_dep + "]"));
        };
        a_check(dep("{\"addn\":40000}") && dep("{\"addn\":40207}") && !dep("{\"addn\":39999}") && !dep("{\"addn\":40208}") &&
                    !dep("{\"addn\":-1}") && !dep("{\"addn\":40000,\"path\":\"" + std::string(kDdsJson) + "\"}") &&
                    !dep("{\"addn\":40000,\"fnv64\":\"0000000000000001\"}") && !ddsDep("{\"addn\":40000}") && !dep("7") &&
                    dep("{\"path\":\"" + std::string(kDdsJson) + "\",\"fnv64\":\"0000000000000001\"}") &&
                    !dep("{\"path\":\"textures\\\\a.dds\",\"fnv64\":\"0000000000000001\"}") &&
                    !dep("{\"path\":\"" + std::string(kDdsJson) + "\"}") &&
                    !dep("{\"path\":\"" + std::string(kDdsJson) + "\",\"fnv64\":\"0000000000000000\"}") && !dep("{}"),
            "progress: a dep is exactly one of {addn} (40000-40207, nif lines only) and {path, fnv64} (a copy path, a real fnv64)");
        const std::string txstHead = "\"v\":1,\"kind\":\"txst\",\"key\":\"0123456789abcdef\"";
        const auto txst = [&](const std::string& a_deps, const std::string& a_outcome = "\"ok\"") {
            return Takes(Line(txstHead, "", "", "", a_outcome, ",\"deps\":[" + a_deps + "]"));
        };
        a_check(txst("{\"path\":\"" + std::string(kDdsJson) + "\",\"fnv64\":\"0000000000000001\"}") && !txst("") &&
                    !txst("{\"path\":\"" + std::string(kNifJson) + "\",\"fnv64\":\"0000000000000001\"}") && txst("", "\"failed\"") &&
                    txst("", "\"fallback\"") &&
                    !txst("{\"path\":\"" + std::string(kNifJson) + "\",\"fnv64\":\"0000000000000001\"}", "\"fallback\""),
            "progress: an ok txst line needs at least one dep, all textures; a fallback one (both colour slots kept) or a "
            "failed one may have none");
        CC::ProgressLine keptSlots;
        keptSlots.kind = CC::ProgressKind::kTxst;
        keptSlots.key = 0x0123456789ABCDEFull;
        keptSlots.outcome = CC::Outcome::kFallback;
        keptSlots.reason = "TX00 and TX03 kept";
        const auto keptText = CC::WriteProgressLine(keptSlots);
        auto keptOk = keptSlots;
        keptOk.outcome = CC::Outcome::kOk;
        a_check(keptText && CC::ParseProgress(*keptText).lines.size() == 1 && CC::ParseProgress(*keptText).lines[0] == keptSlots &&
                    !CC::WriteProgressLine(keptOk),
            "progress: a fallback txst line with no texture dep is written and read back; as ok it is refused");
        a_check(!Takes(Line(kHead, std::string("\"") + kNifJson + "\"", "1", "\"0000000000000001\"", "\"ok\"",
                    ",\"pad\":\"" + std::string(CC::kMaxProgressLine, 'a') + "\"")) &&
                    !Takes("{" + kHead + ",\"outcome\":\"failed\",\"reason\":\"\xFF\xFE\"}"),
            "progress: a line over 64 KiB and text that is not UTF-8 refused");

        auto badText = NifLine();
        badText.reason = std::string("\xFF\xFE", 2);
        auto noPath = NifLine();
        noPath.path.clear();
        auto badAddn = NifLine();
        badAddn.deps[1].addn = 40208;
        auto otherKey = NifLine();
        otherKey.key = 0x0123456789ABCDEEull;
        auto txstPath = NifLine();
        txstPath.kind = CC::ProgressKind::kTxst;
        auto zeroFnv = NifLine();
        zeroFnv.fnv64 = 0;
        auto unknownOutcome = NifLine();
        unknownOutcome.outcome = static_cast<CC::Outcome>(9);
        auto unknownKind = NifLine();
        unknownKind.kind = static_cast<CC::ProgressKind>(9);
        a_check(!CC::WriteProgressLine(badText) && !CC::WriteProgressLine(noPath) && !CC::WriteProgressLine(badAddn) &&
                    !CC::WriteProgressLine(otherKey) && !CC::WriteProgressLine(txstPath) && !CC::WriteProgressLine(zeroFnv) &&
                    !CC::WriteProgressLine(unknownOutcome) && !CC::WriteProgressLine(unknownKind),
            "progress: the writer refuses what the parser would (not UTF-8, no path, a bad addn, another key, a txst path, "
            "fnv64 0) and an unknown kind or outcome");
        a_check(CC::IsPublishable(CC::Outcome::kOk) && CC::IsPublishable(CC::Outcome::kFallback) && !CC::IsPublishable(CC::Outcome::kFailed) &&
                    CC::IsTerminal(CC::Outcome::kTooLarge) && CC::IsTerminal(CC::Outcome::kNotDumped) && !CC::IsTerminal(CC::Outcome::kFailed) &&
                    CC::OutcomeFromText("too_large") == CC::Outcome::kTooLarge && CC::OutcomeFromText("not_dumped") == CC::Outcome::kNotDumped &&
                    CC::ProgressKindFromText("mps") == CC::ProgressKind::kMps && !CC::ProgressKindFromText("NIF") &&
                    !CC::ProgressKindFromText("") && !CC::OutcomeFromText(""),
            "progress: outcome and kind strings; ok and fallback publish, too_large and not_dumped are terminal");
    }
}
