// DistrIndex against TomeReach::ConfigText::Names on a sample SPID config: the index must give the old scan's answers
#include "NpcSpellsCheck.h"
#include "core/NpcSpellsDistr.h"
#include "tomereach/TomeReachConfig.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
using NpcSpellsCheck::Check;
namespace {
    // Each line is an edge of the old matcher: prefix, leading zeros, both sides and separators, spaces, a longer
    // id, a hex letter after the id, a hex digit before "0x", a plugin inside a longer name, a mention on a filter line
    constexpr const char* kSample =
        "Spell = 0x800~MyMod.esp|ActorTypeNPC\n"
        "Spell = MyMod.esp|0x00801\n"
        "Spell = 12800~Other.esp\n"
        "Spell = 0x803 ~ MyMod.esp\n"
        "Spell = Skyrim.esm:0x1A2B3|NONE\n"
        "Spell = BigMod.esp|0x8045\n"
        "Spell = xMyMod.esp|0x805\n"
        "Spell = MyMod.esp|0x806a\n"
        "Keyword = Foo|NONE|0x807~MyMod.esp\n"
        "Spell = A0x808~MyMod.esp\n"
        "Spell = MyMod.esp | 809\n";
}
int RunDistrSelfTest()
{
    std::printf("distr index\n");
    const auto dir = std::filesystem::temp_directory_path() / "npcspells-selftest-distr";
    std::filesystem::create_directories(dir);
    const auto file = dir / "Sample_DISTR.ini";
    { std::ofstream(file, std::ios::binary | std::ios::trunc) << kSample; }
    TomeReach::ConfigText text;
    text.Add(file);
    NpcSpells::DistrIndex index(text.Text());
    const std::array<std::string, 7> plugins = { "mymod.esp", "other.esp", "skyrim.esm", "bigmod.esp", "xmymod.esp", "nothere.esp", "" };
    const std::array<std::uint32_t, 10> extra = { 0, 0x12800, 0x2800, 0x1a2b3, 0x8045, 0x806a, 0x80, 0x8, 0x1a2b, 0xa };
    std::size_t asked = 0, same = 0, named = 0;
    const auto ask = [&](const std::string& p, std::uint32_t id) {
        const bool old = text.Names(p, id, "");
        const bool now = index.Names(p, id);
        ++asked; same += old == now ? 1 : 0; named += old ? 1 : 0;
    };
    for (const auto& p : plugins) {
        for (std::uint32_t id = 0x7FF; id <= 0x80F; ++id) ask(p, id);
        for (const auto id : extra) ask(p, id);
    }
    std::printf("  %zu questions, %zu named by the old scan\n", asked, named);
    Check(same == asked, "index answers every (plugin, id) as ConfigText::Names does");
    Check(named >= 8, "the sample names some forms (the comparison is not all false)");
    Check(index.Names("mymod.esp", 0x800) && index.Names("mymod.esp", 0x801) && index.Names("mymod.esp", 0x803), "0x prefix, leading zeros, spaces round ~");
    Check(index.Names("other.esp", 0x12800) && !index.Names("other.esp", 0x2800), "a longer id is not its tail");
    Check(index.Names("bigmod.esp", 0x8045) && !index.Names("bigmod.esp", 0x804), "a hex digit after the id ends no match");
    Check(index.Names("mymod.esp", 0x805), "a plugin inside a longer name still counts (accepted over-match)");
    Check(!index.Names("mymod.esp", 0x806) && index.Names("mymod.esp", 0x806a), "a hex letter after the id");
    Check(index.Names("mymod.esp", 0x807), "a mention outside a Spell line counts (whole-file over-match)");
    Check(!index.Names("mymod.esp", 0x808), "a hex digit before 0x ends no match");
    Check(!index.Names("", 0x800) && !index.Names("mymod.esp", 0), "no plugin or id 0 is never named");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return 0;
}
