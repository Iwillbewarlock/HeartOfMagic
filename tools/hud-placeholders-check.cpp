// hud-placeholders-check - offline check of HudNotify::FillPlaceholders
// (plugins/spelllearning/include/HudNotify.h, header-only part).
//
// A placeholder name from Papyrus arrives as a BSFixedString, which the game pools
// without case, so "spell" can come back as "Spell". 1.9.1 compared names exactly and
// left "{{spell}}" in the Immersive Spell Learning study messages. Checked here: the
// plain fill, a name in another case (the 1.9.1 bug), an unknown name kept as it is,
// a value holding "{{" not read again, an unclosed "{{", and names of other lengths.
// Prints one PASS/FAIL line per case; exit code 1 on any FAIL.

#include "HudNotify.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int g_fail = 0;

    void Check(const char* name, const std::string& got, const std::string& want)
    {
        bool ok = got == want;
        std::printf("%s %s [%s]%s%s\n", ok ? "PASS" : "FAIL", name, got.c_str(),
                    ok ? "" : " expected ", ok ? "" : want.c_str());
        if (!ok) ++g_fail;
    }

    std::string Fill(const char* text, std::vector<HudNotify::Param> params)
    {
        return HudNotify::FillPlaceholders(text, params);
    }
}

int main()
{
    Check("plain fill",
          Fill("{{who}} studied {{spell}} for {{hours}} hours.",
               { { "who", "You" }, { "spell", "Healing" }, { "hours", "3" } }),
          "You studied Healing for 3 hours.");

    // The 1.9.1 bug: the pool handed back "Spell" for the name "spell"
    Check("name in another case",
          Fill("{{who}} studied {{spell}} for {{hours}} hours.",
               { { "who", "You" }, { "Spell", "Healing" }, { "hours", "3" } }),
          "You studied Healing for 3 hours.");

    Check("placeholder in another case", Fill("{{SPELL}} learned", { { "spell", "Flames" } }), "Flames learned");

    Check("unknown name kept", Fill("{{who}} have learned {{spell}}.", { { "who", "You" } }),
          "You have learned {{spell}}.");

    Check("value with braces not read again", Fill("{{spell}} and {{who}}", { { "spell", "{{who}}" }, { "who", "You" } }),
          "{{who}} and You");

    Check("unclosed placeholder", Fill("{{spell}} at {{percent", { { "spell", "Flames" } }), "Flames at {{percent");

    Check("names of other lengths", Fill("{{spells}}", { { "spell", "Flames" } }), "{{spells}}");

    return g_fail == 0 ? 0 : 1;
}
