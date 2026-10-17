#pragma once

// =============================================================================
// COPY CONTRACT TEST PROGRESS - the progress line cases of the shared test table (CopyContractTest.h)
// =============================================================================

#include "CopyContract.h"

#include <string>

namespace BlackFlame::CopyContractTest
{
    namespace ProgressDetail
    {
        namespace CC = CopyContract;

        inline constexpr const char* kKey16 = "0123456789abcdef";
        inline constexpr const char* kNifPath = "meshes\\heartofmagic\\blackflame\\copies\\0123456789abcdef\\fireboltmuzzle.nif";
        inline constexpr const char* kDdsPath = "textures\\heartofmagic\\blackflame\\copies\\fedcba9876543210\\fxfire01.dds";

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

        /// A line of a_fields, the rest of an ok nif line's fields as written
        [[nodiscard]] inline std::string Json(const std::string& a_fields)
        {
            return "{" + a_fields + "}";
        }

        [[nodiscard]] inline bool Refused(const std::string& a_text)
        {
            return !CC::ParseProgressLine(a_text).line;
        }
    }

    template <class Check>
    void CheckProgress(Check& a_check)
    {
        using namespace ProgressDetail;
        const auto written = CC::WriteProgressLine(NifLine());
        const std::string golden =
            "{\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"path\":\"meshes\\\\heartofmagic\\\\blackflame\\\\copies\\\\"
            "0123456789abcdef\\\\fireboltmuzzle.nif\",\"bytes\":12345,\"fnv64\":\"8888888888888888\",\"outcome\":\"ok\","
            "\"reason\":\"\",\"deps\":[{\"path\":\"textures\\\\heartofmagic\\\\blackflame\\\\copies\\\\fedcba9876543210\\\\"
            "fxfire01.dds\",\"fnv64\":\"9999999999999999\"},{\"addn\":40012}]}\n";
        a_check(written && *written == golden, "progress: the writer's line, fields in contract order, '\\n' at the end (" +
                                                   (written ? written->substr(0, written->size() - 1) : std::string("refused")) + ")");
        const auto back = written ? CC::ParseProgress(*written) : CC::ProgressFile{};
        a_check(back.lines.size() == 1 && back.errors.empty() && !back.cutLastLine && back.lines[0].key == 0x0123456789ABCDEFull &&
                    back.lines[0].path == kNifPath && back.lines[0].bytes == 12345 && back.lines[0].fnv64 == 0x8888888888888888ull &&
                    back.lines[0].deps.size() == 2 && back.lines[0].deps[0].path == kDdsPath &&
                    back.lines[0].deps[0].fnv64 == 0x9999999999999999ull && back.lines[0].deps[1].isAddn &&
                    back.lines[0].deps[1].addn == 40012,
            "progress: a written line reads back field for field");

        const std::string line = written ? *written : std::string();
        const auto cut = CC::ParseProgress(line + line + line.substr(0, line.size() / 2));
        const auto crlf = CC::ParseProgress(line.substr(0, line.size() - 1) + "\r\n\n  \n" + line);
        a_check(cut.lines.size() == 2 && cut.errors.empty() && cut.cutLastLine && crlf.lines.size() == 2 && crlf.errors.empty() &&
                    !crlf.cutLastLine,
            "progress: a last line without '\\n' is ignored; CRLF and blank lines are taken");
        const auto middle = CC::ParseProgress(line + "{\"v\":2}\n" + line);
        a_check(middle.lines.size() == 2 && middle.errors.size() == 1 && middle.errors[0].lineNumber == 2,
            "progress: a bad middle line is reported with its number; the lines around it still count");

        const std::string head = "\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"path\":\"" + std::string() +
                                 "meshes\\\\heartofmagic\\\\blackflame\\\\copies\\\\0123456789abcdef\\\\a.nif\",\"bytes\":1,"
                                 "\"fnv64\":\"0000000000000001\",\"outcome\":\"ok\"";
        a_check(!Refused(Json("\"v\":1," + head)) && Refused(Json("\"v\":2," + head)) && Refused(Json(head)) &&
                    Refused(Json("\"v\":\"1\"," + head)) && Refused("not json") && Refused("[1]"),
            "progress: v must be the number 1 (2, none, a string, no object are refused)");
        a_check(Refused(Json("\"v\":1,\"kind\":\"esp\",\"key\":\"0123456789abcdef\",\"outcome\":\"failed\"")) &&
                    Refused(Json("\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"outcome\":\"done\"")) &&
                    Refused(Json("\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789ABCDEF\",\"outcome\":\"failed\"")) &&
                    !Refused(Json("\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"outcome\":\"failed\",\"reason\":\"io\"")),
            "progress: an unknown kind or outcome or an upper-case key is refused; a failed line needs no file");
        a_check(Refused(Json("\"v\":1,\"kind\":\"nif\",\"key\":\"fedcba9876543210\",\"path\":\"meshes\\\\heartofmagic\\\\blackflame\\\\"
                             "copies\\\\0123456789abcdef\\\\a.nif\",\"bytes\":1,\"fnv64\":\"0000000000000001\",\"outcome\":\"ok\"")) &&
                    Refused(Json("\"v\":1,\"kind\":\"dds\",\"key\":\"0123456789abcdef\",\"path\":\"meshes\\\\heartofmagic\\\\blackflame\\\\"
                                 "copies\\\\0123456789abcdef\\\\a.nif\",\"bytes\":1,\"fnv64\":\"0000000000000001\",\"outcome\":\"ok\"")) &&
                    Refused(Json("\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"path\":\"meshes\\\\magic\\\\a.nif\","
                                 "\"bytes\":1,\"fnv64\":\"0000000000000001\",\"outcome\":\"ok\"")) &&
                    Refused(Json("\"v\":1,\"kind\":\"txst\",\"key\":\"0123456789abcdef\",\"path\":\"meshes\\\\heartofmagic\\\\"
                                 "blackflame\\\\copies\\\\0123456789abcdef\\\\a.nif\",\"outcome\":\"ok\"")) &&
                    !Refused(Json("\"v\":1,\"kind\":\"txst\",\"key\":\"0123456789abcdef\",\"outcome\":\"ok\",\"deps\":[]")),
            "progress: the path must be a copy path under the kind's root in the line's key folder; a txst line has none");
        a_check(Refused(Json("\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"path\":\"meshes\\\\heartofmagic\\\\blackflame\\\\"
                             "copies\\\\0123456789abcdef\\\\a.nif\",\"bytes\":1,\"outcome\":\"ok\"")) &&
                    Refused(Json("\"v\":1,\"kind\":\"nif\",\"key\":\"0123456789abcdef\",\"outcome\":\"fallback\"")) &&
                    Refused(Json("\"v\":1," + head + ",\"bytes\":-1")),
            "progress: an ok or fallback file line needs path, bytes and fnv64 (bytes unsigned)");
        const auto withDep = [&](const std::string& a_dep) { return Json("\"v\":1," + head + ",\"deps\":[" + a_dep + "]"); };
        a_check(!Refused(withDep("{\"addn\":40000}")) && !Refused(withDep("{\"addn\":40207}")) && Refused(withDep("{\"addn\":39999}")) &&
                    Refused(withDep("{\"addn\":40208}")) && Refused(withDep("{\"addn\":-1}")) &&
                    Refused(withDep("{\"path\":\"Textures\\\\a.dds\",\"fnv64\":\"0000000000000001\"}")) &&
                    Refused(withDep("{\"path\":\"textures\\\\a.dds\"}")) && Refused(withDep("{\"path\":\"..\\\\a.dds\",\"fnv64\":\"0000000000000001\"}")) &&
                    !Refused(withDep("{\"path\":\"textures\\\\a.dds\",\"fnv64\":\"0000000000000001\"}")) && Refused(withDep("7")),
            "progress: deps are an ADDN slot index (40000-40207) or a normalised path with its fnv64");

        auto badText = NifLine();
        badText.reason = std::string("\xFF\xFE", 2);
        auto noPath = NifLine();
        noPath.path.clear();
        auto failed = NifLine();
        failed.outcome = CC::Outcome::kFailed;
        failed.path.clear();
        failed.deps.clear();
        a_check(!CC::WriteProgressLine(badText) && !CC::WriteProgressLine(noPath) && CC::WriteProgressLine(failed),
            "progress: the writer refuses text that is not UTF-8 and a line that would not read back");
        a_check(CC::IsPublishable(CC::Outcome::kOk) && CC::IsPublishable(CC::Outcome::kFallback) && !CC::IsPublishable(CC::Outcome::kFailed) &&
                    CC::IsTerminal(CC::Outcome::kTooLarge) && CC::IsTerminal(CC::Outcome::kNotDumped) && !CC::IsTerminal(CC::Outcome::kFailed) &&
                    CC::OutcomeFromText("too_large") == CC::Outcome::kTooLarge && CC::OutcomeFromText("not_dumped") == CC::Outcome::kNotDumped &&
                    CC::ProgressKindFromText("mps") == CC::ProgressKind::kMps && !CC::ProgressKindFromText("NIF"),
            "progress: outcome and kind strings; ok and fallback publish, too_large and not_dumped are terminal");
    }
}
