// magic-menu-block-api-check - offline check of the magic menu's Block API host side (v2: Spell Research add-on design
// 2026-10-27, "Changes / 1"; v3: HoM add-on design 2026-10-30): the registration parsing of BlockRegistry.cpp (v1 bytes in
// the frozen v1 layout, v2 in full and cut short, SR's own v2 header bytes, v3 in full and cut at v2's size, version 4
// refused, the tabs mask, v3's flags and legacy ids), BlockRulesV3.h's 'SLMS' / 'SLMV' reading and ShowSpellDue, and the
// pure rules of BlockRules.h that Widget.cpp runs on the game's values (the tab bit of a SkyUI category flag, a block's
// visibility per tab, the press / release cancel rules, the content clip coordinates, the kMessageRedraw id rule).
//
//   magic-menu-block-api-check              one PASS/FAIL line per check, a total line
//   magic-menu-block-api-check --selftest   the same checks (no game or input files exist for this tool), "selftest: ..." line
//
// No game types. Exit 0 when every check passes, 1 when one fails, 2 on an unknown argument.

#include "BlockApi.h"
#include "BlockRegistry.h"
#include "BlockRules.h"
#include "BlockRulesV3.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <limits>
#include <vector>

// The Spell Research add-on's own v2 header (std includes already in): an unmodified v2 add-on's bytes; its RE types are SrV2::RE
namespace SrV2
{
#include "../plugins/SL_MagicMenu_SpellResearch/src/BlockApi.h"
}

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitUsage = 2;
    constexpr unsigned char kJunkByte = 0xAB;  // bytes past a struct's covered part: never read as a field

    int g_pass = 0;
    int g_fail = 0;

    void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        (a_ok ? g_pass : g_fail) += 1;
    }

    std::string Text(const char* a_format, auto... a_values)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), a_format, a_values...);
        return buffer;
    }

    void DrawNothing(RE::GFxMovieView*, RE::GFxValue&, float, float, void*) {}
    void DrawSelectionNothing(RE::GFxMovieView*, RE::GFxValue&, float, float, std::uint32_t, void*) {}
    void ClickNothing(float, float, float, float, void*) {}
    void FrameNothing(RE::GFxMovieView*, RE::GFxValue&, RE::GFxValue*, const BlockApi::Context&, void*) {}
    bool KeyNothing(std::uint32_t, void*) { return false; }
    void SrDrawSelection(SrV2::RE::GFxMovieView*, SrV2::RE::GFxValue&, float, float, std::uint32_t, void*) {}

    /// A function pointer's address, to compare pointers of the two headers' (distinct) callback types
    std::uintptr_t Address(auto a_fn) { return std::bit_cast<std::uintptr_t>(a_fn); }

    int g_user = 0;  // the user pointer handed back

    /// a_bytes of a_value, then a_extra junk bytes: what a sender with a bigger struct (or garbage after it) puts in the message
    template <class T>
    std::vector<unsigned char> Bytes(const T& a_value, std::size_t a_extra = 0)
    {
        std::vector<unsigned char> bytes(sizeof(T) + a_extra, kJunkByte);
        std::memcpy(bytes.data(), &a_value, sizeof(T));
        return bytes;
    }

    BlockRegistry::Outcome Send(BlockRegistry::Registry& a_registry, const std::vector<unsigned char>& a_bytes, std::uint32_t a_dataLen)
    {
        return a_registry.Register("AddonA", a_bytes.data(), a_dataLen);
    }

    BlockRegistry::Outcome Send(BlockRegistry::Registry& a_registry, const std::vector<unsigned char>& a_bytes)
    {
        return Send(a_registry, a_bytes, static_cast<std::uint32_t>(a_bytes.size()));
    }

    void ExpectRefused(const BlockRegistry::Outcome& a_outcome, std::string_view a_reasonPart, const std::string& a_case)
    {
        Expect(!a_outcome.accepted && a_outcome.reason.find(a_reasonPart) != std::string::npos,
            Text("%s: refused, reason has \"%s\" (got %s \"%s\")", a_case.c_str(), std::string(a_reasonPart).c_str(),
                a_outcome.accepted ? "accepted" : "refused", a_outcome.reason.c_str()));
    }

    BlockApi::RegistrationV1 V1(const char* a_id)
    {
        BlockApi::RegistrationV1 reg;
        reg.id = a_id;
        reg.title = "T";
        reg.height = 100.0f;
        reg.draw = DrawNothing;
        reg.user = &g_user;
        return reg;
    }

    BlockApi::RegistrationV2 V2(const char* a_id)
    {
        return { .id = a_id, .title = "T", .height = 100.0f, .user = &g_user, .drawSelection = DrawSelectionNothing, .click = ClickNothing,
            .tabs = BlockApi::kTabSpells };
    }

    BlockApi::Registration V3(const char* a_id, const char* a_legacyId = nullptr)
    {
        return { .id = a_id, .title = "T", .height = 100.0f, .user = &g_user, .drawSelection = DrawSelectionNothing, .click = ClickNothing,
            .tabs = BlockApi::kTabSpells, .frame = FrameNothing, .key = KeyNothing, .pad = 4.0f,
            .flags = BlockApi::kFlagOwnMouse | BlockApi::kFlagKeepWhenEmpty, .legacyId = a_legacyId };
    }

    /// No v3 field read: what a v1 / v2 block (or a v3 one cut at v2's size) is stored with
    bool NoV3Fields(const BlockRegistry::Block* a_block)
    {
        return a_block && !a_block->frame && !a_block->key && a_block->pad == BlockApi::kPadDefault && a_block->flags == 0 && a_block->legacyId.empty();
    }

    void Layouts()
    {
        std::printf("the frozen v1 layout\n");
        Expect(BlockApi::kRegistrationV1Size == 48, Text("RegistrationV1 is 48 bytes, the v1 header's x64 layout (got %u)", BlockApi::kRegistrationV1Size));
        Expect(offsetof(BlockApi::Registration, drawSelection) == BlockApi::kRegistrationV1Size,
            Text("v2's first field right after v1's (drawSelection at %zu)", offsetof(BlockApi::Registration, drawSelection)));
        Expect(BlockApi::kRegistrationV2Size == 72 && offsetof(BlockApi::Registration, frame) == BlockApi::kRegistrationV2Size,
            Text("RegistrationV2 is 72 bytes, v3's first field right after it (frame at %zu)", offsetof(BlockApi::Registration, frame)));
        Expect(sizeof(SrV2::BlockApi::Registration) == BlockApi::kRegistrationV2Size && SrV2::BlockApi::kVersion == BlockApi::kVersionV2,
            "SR's v2 header: its Registration is RegistrationV2's size, its kVersion 2");
        Expect(BlockApi::kMessageRedraw == 0x534C4D44u, Text("kMessageRedraw is 'SLMD' (0x%08X)", BlockApi::kMessageRedraw));
        Expect(BlockApi::kMessageShowSpell == 0x534C4D53u && BlockApi::kMessageVisible == 0x534C4D56u, "kMessageShowSpell 'SLMS', kMessageVisible 'SLMV'");
        Expect(BlockApi::kVersion == 3 && BlockApi::kVersionV2 == 2 && BlockApi::kVersionV1 == 1, "kVersion 3, kVersionV2 2, kVersionV1 1");
        Expect(BlockApi::kFlagsKnown == 3 && BlockApi::Registration{}.pad == BlockApi::kPadDefault && BlockApi::kPadDefault < 0.0f,
            "kFlagsKnown 0x3 (own mouse, keep when empty); pad defaults to -1 (the host's pad)");
        Expect(BlockApi::kTabsAll == 0 && std::popcount(BlockApi::kTabsKnown) == 4 && BlockApi::kTabsKnown == 0xF,
            "the four tab bits are distinct (kTabsKnown has 4 bits, 0xF), kTabsAll 0");
    }

    void V1Registrations()
    {
        std::printf("v1 registrations (the old layout)\n");
        BlockRegistry::Registry registry;
        const auto plain = Send(registry, Bytes(V1("plain")));
        const auto* block = registry.Find("AddonA.plain");
        Expect(plain.accepted && plain.apiVersion == BlockApi::kVersionV1 && block && block->apiVersion == BlockApi::kVersionV1 &&
                   block->draw == DrawNothing && block->user == &g_user && block->title == "T" && block->height == 100.0,
            Text("v1 bytes, 48 sent: accepted as v1, draw / user / title / height kept (result apiVersion %u)", plain.apiVersion));
        Expect(block && !block->drawSelection && !block->click && block->tabs == BlockApi::kTabsAll, "v1: no drawSelection, no click, every tab");

        // a v1 sender whose message holds more bytes than its struct (junk where the v2 fields would be)
        const auto junkAfter = Send(registry, Bytes(V1("junk"), 32));
        const auto* junk = registry.Find("AddonA.junk");
        Expect(junkAfter.accepted && junk && !junk->drawSelection && !junk->click && junk->tabs == BlockApi::kTabsAll,
            "v1 with 32 junk bytes after it (message 80): the junk is not read as v2 fields");
        // a v1 add-on whose own struct grew (structSize past 48): v1 is frozen, only its v1 fields are read
        auto grown = V1("grown");
        grown.structSize = BlockApi::kRegistrationV1Size + 24;
        const auto grownOutcome = Send(registry, Bytes(grown, 24));
        const auto* grownBlock = registry.Find("AddonA.grown");
        Expect(grownOutcome.accepted && grownBlock && !grownBlock->drawSelection && !grownBlock->click && grownBlock->tabs == BlockApi::kTabsAll,
            "v1 with structSize 72 and junk after 48: accepted, only the v1 fields read");
        // v1 needs draw, whatever lies past its fields
        auto noDraw = V1("nodraw");
        noDraw.draw = nullptr;
        auto noDrawBytes = Bytes(noDraw, 24);
        const BlockApi::DrawSelectionFn selection = DrawSelectionNothing;
        std::memcpy(noDrawBytes.data() + BlockApi::kRegistrationV1Size, &selection, sizeof(selection));
        const auto noDrawOutcome = Send(registry, noDrawBytes);
        ExpectRefused(noDrawOutcome, "no draw callback", "v1 without draw (a drawSelection pointer after its fields)");
        Expect(noDrawOutcome.apiVersion == BlockApi::kVersionV1, Text("a v1 refusal is answered as v1 (apiVersion %u)", noDrawOutcome.apiVersion));
        auto small = V1("small");
        small.structSize = BlockApi::kRegistrationV1Size - 8;
        ExpectRefused(Send(registry, Bytes(small)), "smaller than the v1", "v1 structSize 40");
        ExpectRefused(Send(registry, Bytes(V1("cut")), BlockApi::kRegistrationV1Size - 1), "smaller than the v1", "v1 message 47 bytes");

        // review round 1 #5: refusals before the fields are read still answer a v1 sender as v1
        registry.Close();
        const auto lateV1 = Send(registry, Bytes(V1("late")));
        ExpectRefused(lateV1, "closed", "v1 after the window closed");
        Expect(lateV1.apiVersion == BlockApi::kVersionV1, Text("the closed-window refusal answers a v1 sender as v1 (apiVersion %u)", lateV1.apiVersion));
        const auto lateV2 = Send(registry, Bytes(V2("late")));
        Expect(!lateV2.accepted && lateV2.apiVersion == BlockApi::kVersionV2, "... and a v2 sender as v2");
        Expect(Send(registry, Bytes(V3("late"))).apiVersion == BlockApi::kVersion, "... and a v3 sender as v3");
        auto lateV4 = V2("late");
        lateV4.apiVersion = BlockApi::kVersion + 1;
        Expect(Send(registry, Bytes(lateV4)).apiVersion == BlockApi::kVersion, "... and a version 4 sender with the host's version");
        BlockRegistry::Registry open;
        const auto noData = open.Register("AddonA", nullptr, 0);
        Expect(!noData.accepted && noData.apiVersion == BlockApi::kVersion, "no data: refused, answered with the host's version");
    }

    void V2Registrations()
    {
        std::printf("v2 registrations\n");
        BlockRegistry::Registry registry;
        const auto full = Send(registry, Bytes(V2("full")));
        const auto* block = registry.Find("AddonA.full");
        Expect(full.accepted && full.apiVersion == BlockApi::kVersionV2 && block && block->apiVersion == BlockApi::kVersionV2 &&
                   block->drawSelection == DrawSelectionNothing && block->click == ClickNothing && block->tabs == BlockApi::kTabSpells &&
                   !block->draw && block->user == &g_user,
            "v2 in full: accepted as v2, drawSelection, click, tabs spells, user kept");
        Expect(NoV3Fields(block), "v2 in full: no frame, key, flags or legacyId; pad the host's");

        auto both = V2("both");
        both.draw = DrawNothing;
        both.click = nullptr;
        both.tabs = BlockApi::kTabsAll;
        const auto* bothBlock = Send(registry, Bytes(both)).accepted ? registry.Find("AddonA.both") : nullptr;
        Expect(bothBlock && bothBlock->draw == DrawNothing && bothBlock->drawSelection == DrawSelectionNothing && !bothBlock->click,
            "v2 with draw and drawSelection, no click: both kept (the host calls drawSelection)");
        auto drawOnly = V2("drawonly");
        drawOnly.draw = DrawNothing;
        drawOnly.drawSelection = nullptr;
        Expect(Send(registry, Bytes(drawOnly)).accepted, "v2 with draw only: accepted");
        auto neither = V2("neither");
        neither.drawSelection = nullptr;
        ExpectRefused(Send(registry, Bytes(neither)), "neither draw nor drawSelection", "v2 with neither draw");

        // cut short by structSize: only whole fields are read
        auto upToSelection = V2("cut1");
        upToSelection.structSize = static_cast<std::uint32_t>(offsetof(BlockApi::RegistrationV2, click));
        const auto* cut1 = Send(registry, Bytes(upToSelection)).accepted ? registry.Find("AddonA.cut1") : nullptr;
        Expect(cut1 && cut1->drawSelection == DrawSelectionNothing && !cut1->click && cut1->tabs == BlockApi::kTabsAll,
            Text("v2 structSize %u (drawSelection only): accepted, click null, tabs every tab", upToSelection.structSize));
        auto halfPointer = V2("cut2");
        halfPointer.draw = DrawNothing;
        halfPointer.structSize = static_cast<std::uint32_t>(offsetof(BlockApi::RegistrationV2, drawSelection) + sizeof(void*) / 2);
        const auto* cut2 = Send(registry, Bytes(halfPointer)).accepted ? registry.Find("AddonA.cut2") : nullptr;
        Expect(cut2 && !cut2->drawSelection && cut2->draw == DrawNothing,
            Text("v2 structSize %u (half of drawSelection): the half pointer is not read, draw used", halfPointer.structSize));
        auto halfNoDraw = halfPointer;
        halfNoDraw.id = "cut3";
        halfNoDraw.draw = nullptr;
        ExpectRefused(Send(registry, Bytes(halfNoDraw)), "neither draw", "v2 cut inside drawSelection without draw");
        auto v1Only = V2("cut4");
        v1Only.draw = DrawNothing;
        v1Only.structSize = BlockApi::kRegistrationV1Size;
        const auto* cut4 = Send(registry, Bytes(v1Only)).accepted ? registry.Find("AddonA.cut4") : nullptr;
        Expect(cut4 && cut4->apiVersion == BlockApi::kVersionV2 && !cut4->drawSelection && !cut4->click && cut4->tabs == BlockApi::kTabsAll,
            "v2 with structSize 48 (v1's fields only): accepted as v2 with no v2 fields");
        // cut short by the message: structSize says v2, the message ends after click
        const auto messageCut = Send(registry, Bytes(V2("cut5")), static_cast<std::uint32_t>(offsetof(BlockApi::RegistrationV2, tabs)));
        const auto* cut5 = registry.Find("AddonA.cut5");
        Expect(messageCut.accepted && cut5 && cut5->click == ClickNothing && cut5->tabs == BlockApi::kTabsAll,
            "v2 message ending before tabs (structSize full): click read, tabs every tab");
        auto tiny = V2("tiny");
        tiny.structSize = BlockApi::kRegistrationV1Size - 4;
        ExpectRefused(Send(registry, Bytes(tiny)), "smaller than the v1", "v2 structSize under v1's");
        // a v2 struct of a later 2.x header that grew: the known fields read, the rest (junk where v3's fields lie) ignored
        auto grown = V2("grown");
        grown.structSize = BlockApi::kRegistrationV2Size + 40;
        const bool grownAccepted = Send(registry, Bytes(grown, 40)).accepted;
        Expect(grownAccepted && NoV3Fields(registry.Find("AddonA.grown")), "v2 with structSize past v2 (40 junk bytes): accepted, no v3 field read");

        std::printf("versions\n");
        auto v4 = V2("v4");
        v4.apiVersion = BlockApi::kVersion + 1;
        const auto v4Outcome = Send(registry, Bytes(v4));
        ExpectRefused(v4Outcome, "update SL_MagicMenu", "apiVersion 4");
        Expect(v4Outcome.apiVersion == BlockApi::kVersion && v4Outcome.id.empty(), "apiVersion 4: answered with the host's version, no id read");
        auto v0 = V2("v0");
        v0.apiVersion = 0;
        ExpectRefused(Send(registry, Bytes(v0)), "apiVersion 0", "apiVersion 0");

        std::printf("the tabs mask at registration\n");
        for (const std::uint32_t tabs : { BlockApi::kTabPowers, BlockApi::kTabShouts, BlockApi::kTabActiveEffects, BlockApi::kTabsKnown,
                 BlockApi::kTabPowers | BlockApi::kTabShouts }) {
            auto reg = V2("");
            const std::string id = Text("tabs%u", tabs);
            reg.id = id.c_str();
            reg.tabs = tabs;
            const auto* kept = Send(registry, Bytes(reg)).accepted ? registry.Find("AddonA." + id) : nullptr;
            Expect(kept && kept->tabs == tabs, Text("tabs 0x%X accepted and kept", tabs));
        }
        auto unknownBit = V2("unknownbit");
        unknownBit.tabs = BlockApi::kTabSpells | (BlockApi::kTabsKnown + 1);
        ExpectRefused(Send(registry, Bytes(unknownBit)), "does not know", "tabs with a bit outside kTabsKnown");
    }

    void TabMask()
    {
        std::printf("tab bits and visibility (BlockRules)\n");
        using namespace BlockRules;
        constexpr int kFlagAll = -257, kFlagFavorites = 1, kFlagUnknown = 4096, kFlagNone = 0;
        for (const int flag : { kFlagAll, kFlagFavorites, kFlagNone, kFlagUnknown, kFlagAlteration, kFlagIllusion, kFlagDestruction, kFlagConjuration,
                 kFlagRestoration }) {
            Expect(TabBit(flag) == BlockApi::kTabSpells, Text("flag %d is a spell tab", flag));
        }
        Expect(TabBit(kFlagPowers) == BlockApi::kTabPowers && TabBit(kFlagShouts) == BlockApi::kTabShouts &&
                   TabBit(kFlagActiveEffects) == BlockApi::kTabActiveEffects,
            "Powers, Shouts and Active Effects have their own bits");
        const std::vector<int> flags{ kFlagAll, kFlagDestruction, kFlagPowers, kFlagShouts, kFlagActiveEffects };
        const auto shownOn = [&](std::uint32_t a_tabs) {
            std::string tabs;
            for (const int flag : flags) tabs += ShownOn(a_tabs, flag) ? "1" : "0";
            return tabs;
        };
        // the order of flags: All, Destruction, Powers, Shouts, Active Effects
        const std::vector<std::pair<std::uint32_t, std::string>> cases{
            { BlockApi::kTabsAll, "11111" },
            { BlockApi::kTabSpells, "11000" },
            { BlockApi::kTabPowers, "00100" },
            { BlockApi::kTabShouts, "00010" },
            { BlockApi::kTabActiveEffects, "00001" },
            { BlockApi::kTabPowers | BlockApi::kTabShouts, "00110" },
            { BlockApi::kTabSpells | BlockApi::kTabActiveEffects, "11001" },
            { BlockApi::kTabsKnown, "11111" },
        };
        for (const auto& [tabs, want] : cases) {
            const auto got = shownOn(tabs);
            Expect(got == want, Text("tabs 0x%X shows on All/Destruction/Powers/Shouts/Effects = %s (got %s)", tabs, want.c_str(), got.c_str()));
        }
    }

    void ClickRules()
    {
        std::printf("press / release (BlockRules::Judge)\n");
        using BlockRules::Verdict;
        BlockRules::Moment at;
        at.content = { 990, 400, 1230, 520 };
        at.viewport = { 978, 67, 1238, 650 };
        const BlockRules::Press press{ at, { 1000, 410 } };
        const auto judge = [&](BlockRules::Release a_release) { return BlockRules::Judge(press, a_release); };
        const auto expect = [&](const BlockRules::Release& a_release, Verdict a_want, const char* a_case) {
            const auto got = judge(a_release);
            Expect(got == a_want, Text("%s: %s (got %s)", a_case, BlockRules::VerdictText(a_want), BlockRules::VerdictText(got)));
        };
        expect({ at, { 1005, 415 } }, Verdict::kClick, "release in the block, nothing changed");
        expect({ at, { 1230, 520 } }, Verdict::kClick, "release on the content area's bottom-right corner (edges count)");
        expect({ at, { 1005, 415 }, false }, Verdict::kOutsideBlock, "onReleaseOutside");
        expect({ at, { 1231, 415 } }, Verdict::kOutsideBlock, "release right of the content area (over the clip's padding)");
        expect({ at, { 1005, 399 } }, Verdict::kOutsideBlock, "release above the content area (the title strip)");
        const BlockRules::Press outsidePress{ at, { 985, 410 } };  // left of the content area (the panel's padding)
        Expect(BlockRules::Judge(outsidePress, { at, { 1005, 415 } }) == Verdict::kOutsideBlock,
            "press outside the content area, release inside: released outside the block");
        auto low = at;
        low.content = { 990, 600, 1230, 720 };  // the block runs past the column's bottom (scrolled column)
        BlockRules::Press lowPress{ low, { 1000, 610 } };
        Expect(BlockRules::Judge(lowPress, { low, { 1000, 660 } }) == Verdict::kOutsideViewport, "release in the block but below the viewport");
        lowPress.point = { 1000, 655 };
        Expect(BlockRules::Judge(lowPress, { low, { 1000, 640 } }) == Verdict::kOutsideViewport, "press below the viewport (masked part)");
        auto scrolled = at;
        ++scrolled.scrollMoves;
        expect({ scrolled, { 1005, 415 } }, Verdict::kScrolled, "the column scrolled");
        auto redrawn = at;
        ++redrawn.draws;
        expect({ redrawn, { 1005, 415 } }, Verdict::kRedrawn, "the block was drawn again");
        auto mode = at;
        ++mode.modeChanges;
        expect({ mode, { 1005, 415 } }, Verdict::kModeChanged, "the layout mode entered (and left) between");
        auto inMode = at;
        inMode.layoutMode = true;
        ++inMode.modeChanges;
        expect({ inMode, { 1005, 415 } }, Verdict::kLayoutMode, "the layout mode is on at the release");
        BlockRules::Press modePress{ inMode, { 1000, 410 } };
        Expect(BlockRules::Judge(modePress, { inMode, { 1005, 415 } }) == Verdict::kLayoutMode, "pressed and released in the layout mode");
        auto moved = at;
        moved.content.top += 20;
        moved.content.bottom += 20;
        expect({ moved, { 1005, 425 } }, Verdict::kMoved, "the block moved (a tab change closed up the column)");
        auto wider = at;
        wider.content.right += 30;
        expect({ wider, { 1005, 415 } }, Verdict::kMoved, "the block changed width");
        auto many = at;
        ++many.draws;
        ++many.scrollMoves;
        expect({ many, { 2000, 2000 }, false }, Verdict::kScrolled, "several reasons: the first in order (scrolled before redrawn and outside)");

        const auto local = BlockRules::ToContent({ 1000, 410 }, at.content);
        Expect(local.x == 10 && local.y == 10, Text("stage 1000,410 in content 990,400: 10,10 (got %.1f,%.1f)", local.x, local.y));
        const auto origin = BlockRules::ToContent({ 990, 400 }, at.content);
        Expect(origin.x == 0 && origin.y == 0, "the content area's top-left is 0,0");
    }

    void RedrawIds()
    {
        std::printf("kMessageRedraw ids (BlockRules::RedrawId)\n");
        const auto read = [](const std::string& a_text, std::uint32_t a_len) { return BlockRules::RedrawId(a_text.c_str(), a_len); };
        const std::string plain = "MyMod.stats";
        Expect(read(plain, static_cast<std::uint32_t>(plain.size() + 1)) == plain, "an id with its NUL");
        Expect(read(plain, static_cast<std::uint32_t>(plain.size())) == plain, "an id sent without its NUL (dataLen = its length)");
        Expect(read(plain, 5) == std::string("MyMod"), "read no further than dataLen");
        Expect(!BlockRules::RedrawId(nullptr, 8) && !read("", 1) && !read(plain, 0), "null data, an empty id, dataLen 0: none");
        // review round 1 #1: the longest final id the registry makes (a 255-character plugin name + '.' + a 128-character id)
        BlockRegistry::Registry registry;
        const std::string sender(BlockRules::kMaxPluginNameLength, 'P');
        const std::string longId(BlockApi::kMaxIdLength, 'x');
        auto reg = V2(longId.c_str());
        const auto outcome = registry.Register(sender, &reg, sizeof(reg));
        Expect(outcome.accepted && outcome.id.size() == BlockRules::kMaxFinalIdLength,
            Text("a 128-character id from a 255-character plugin: final id %zu characters", outcome.id.size()));
        Expect(read(outcome.id, static_cast<std::uint32_t>(outcome.id.size() + 1)) == outcome.id, "that final id is read whole by RedrawId");
        const std::string tooLong(BlockRules::kMaxFinalIdLength + 1, 'y');
        Expect(!read(tooLong, static_cast<std::uint32_t>(tooLong.size() + 1)), Text("an id of %zu characters: none", tooLong.size()));
    }

    void HeightRequests()
    {
        std::printf("kMessageHeight (BlockRules::ReadHeight, JudgeHeight, LaidOutHeight, Enqueue)\n");
        std::string why;
        const std::string id = "SpellResearch.archetypes";
        BlockApi::HeightRequest request{ .id = id.c_str(), .height = 94.0f };
        const auto read = BlockRules::ReadHeight(&request, sizeof(request), why);
        Expect(read && read->id == id && read->height == 94.0, "a whole request: its id and height");
        Expect(BlockRules::ReadHeight(&request, static_cast<std::uint32_t>(BlockRules::kHeightNeeded), why).has_value(),
            "a message cut right after the height still reads");
        // final review 4: each result first, then its label (the argument order is unspecified: the label read the last reason)
        const bool shortRefused = !BlockRules::ReadHeight(&request, static_cast<std::uint32_t>(BlockRules::kHeightNeeded - 1), why) &&
                                  why.find("bytes") != std::string::npos;
        Expect(shortRefused, Text("a message one byte short: refused (%s)", why.c_str()));
        Expect(!BlockRules::ReadHeight(nullptr, sizeof(request), why), "null data: refused");
        auto bad = request;
        for (const std::uint32_t version : { BlockApi::kVersionV2, BlockApi::kVersion }) {
            bad.apiVersion = version;
            Expect(BlockRules::ReadHeight(&bad, sizeof(bad), why).has_value(), Text("apiVersion %u: read ('SLMH' is v2's, unchanged in v3)", version));
        }
        SrV2::BlockApi::HeightRequest srRequest{ .id = id.c_str(), .height = 94.0f };
        const auto srRead = BlockRules::ReadHeight(&srRequest, sizeof(srRequest), why);
        Expect(srRead && srRead->height == 94.0, Text("SR's own v2 HeightRequest bytes (apiVersion %u): read", srRequest.apiVersion));
        bad.apiVersion = BlockApi::kVersion + 1;
        const bool v4Refused = !BlockRules::ReadHeight(&bad, sizeof(bad), why) && why.find("apiVersion 4") != std::string::npos;
        Expect(v4Refused, Text("apiVersion 4: refused (%s)", why.c_str()));
        bad = request;
        bad.apiVersion = 1;
        Expect(!BlockRules::ReadHeight(&bad, sizeof(bad), why), "apiVersion 1 (v1 has no height message): refused");
        bad = request;
        bad.structSize = 8;
        Expect(!BlockRules::ReadHeight(&bad, sizeof(bad), why), "structSize not covering the height: refused");
        bad = request;
        bad.id = nullptr;
        Expect(!BlockRules::ReadHeight(&bad, sizeof(bad), why), "no id: refused");
        bad.id = "";
        Expect(!BlockRules::ReadHeight(&bad, sizeof(bad), why), "an empty id: refused");
        const std::string tooLong(BlockRules::kMaxFinalIdLength + 1, 'y');
        bad.id = tooLong.c_str();
        Expect(!BlockRules::ReadHeight(&bad, sizeof(bad), why), "an id past kMaxFinalIdLength: refused");
        for (const float height : { 0.0f, -5.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
            bad = request;
            bad.height = height;
            Expect(!BlockRules::ReadHeight(&bad, sizeof(bad), why), Text("height %g: refused", static_cast<double>(height)));
        }

        using BlockRules::HeightVerdict;
        Expect(BlockRules::JudgeHeight(94, 132, std::nullopt) == HeightVerdict::kApply, "94 of 132, none asked yet: applied");
        Expect(BlockRules::JudgeHeight(132, 132, std::nullopt) == HeightVerdict::kUnchanged, "the registered height, none asked: unchanged");
        // JudgeHeight only: that the widget then lays nothing out again is its code's (not reached by this tool)
        Expect(BlockRules::JudgeHeight(94, 132, 94.0) == HeightVerdict::kUnchanged, "94 again: unchanged (JudgeHeight; no redraw loop)");
        Expect(BlockRules::JudgeHeight(132, 132, 94.0) == HeightVerdict::kApply, "back to the registered height: applied");
        Expect(BlockRules::JudgeHeight(132.5, 132, std::nullopt) == HeightVerdict::kTooTall, "more than registered: refused");
        Expect(BlockRules::JudgeHeight(132.005, 132, std::nullopt) == HeightVerdict::kUnchanged, "within the tolerance of registered: unchanged");
        Expect(BlockRules::LaidOutHeight(132, 94.0, false) == 94 && BlockRules::LaidOutHeight(132, 94.0, true) == 132 &&
                   BlockRules::LaidOutHeight(132, std::nullopt, false) == 132,
            "laid out: the asked height, but the registered one in the layout mode or when none was asked");

        std::vector<std::pair<std::string, double>> queue;
        Expect(BlockRules::Enqueue(queue, std::string("a"), 50.0, 2) == BlockRules::Queued::kAdded, "a queued");
        Expect(BlockRules::Enqueue(queue, std::string("a"), 70.0, 2) == BlockRules::Queued::kReplaced && queue.size() == 1 && queue[0].second == 70.0,
            "a again: one entry, the last height (70)");
        Expect(BlockRules::Enqueue(queue, std::string("b"), 30.0, 2) == BlockRules::Queued::kAdded, "b queued");
        Expect(BlockRules::Enqueue(queue, std::string("c"), 30.0, 2) == BlockRules::Queued::kFull && queue.size() == 2, "c past the cap: dropped");
        Expect(BlockRules::Enqueue(queue, std::string("b"), 40.0, 2) == BlockRules::Queued::kReplaced && queue[1].second == 40.0,
            "b again at the cap: replaced, not dropped");
    }

    void V3Registrations()
    {
        std::printf("v3 registrations\n");
        BlockRegistry::Registry registry;
        const auto whole = Send(registry, Bytes(V3("whole", "tree")));
        const auto* block = registry.Find("AddonA.whole");
        Expect(whole.accepted && whole.apiVersion == BlockApi::kVersion && block && block->apiVersion == BlockApi::kVersion &&
                   block->drawSelection == DrawSelectionNothing && block->click == ClickNothing && block->tabs == BlockApi::kTabSpells &&
                   block->frame == FrameNothing && block->key == KeyNothing && block->pad == 4.0 && block->flags == BlockApi::kFlagsKnown &&
                   block->legacyId == "tree",
            "v3 bytes whole: accepted as v3, v2 fields and frame / key / pad 4 / flags / legacyId kept");
        const auto aliases = registry.LegacyAliases();
        Expect(aliases.size() == 1 && aliases.contains("tree") && aliases.at("tree") == "AddonA.whole", "LegacyAliases: tree -> AddonA.whole");

        auto atV2 = V3("atv2", "progress");
        atV2.structSize = BlockApi::kRegistrationV2Size;
        const auto atV2Outcome = Send(registry, Bytes(atV2));
        const auto* atV2Block = registry.Find("AddonA.atv2");
        Expect(atV2Outcome.accepted && atV2Outcome.apiVersion == BlockApi::kVersion && atV2Block && atV2Block->click == ClickNothing &&
                   NoV3Fields(atV2Block),
            "v3 with structSize 72 (v2's): v2 fields read, v3 fields their defaults (pad -1)");
        const auto read = BlockRegistry::ReadRegistration(Bytes(V3("x", "tree")).data(), BlockApi::kRegistrationV2Size);
        Expect(read.apiVersion == BlockApi::kVersion && read.tabs == BlockApi::kTabSpells && !read.frame && !read.key && read.pad == BlockApi::kPadDefault &&
                   read.flags == 0 && !read.legacyId,
            "ReadRegistration of a v3 message cut at 72 bytes: tabs read, frame / key / flags / legacyId null, pad -1");
        auto plain = V3("plain");
        plain.flags = 0;
        Expect(Send(registry, Bytes(plain)).accepted && registry.LegacyAliases().size() == 1, "v3 with no legacyId, no flags: accepted, no alias");

        const SrV2::BlockApi::Registration sr{ .id = "archetypes", .title = "ARCHETYPES", .height = 132.0f, .drawSelection = SrDrawSelection,
            .tabs = SrV2::BlockApi::kTabSpells };
        const auto srOutcome = registry.Register("SL_MagicMenu_SpellResearch", &sr, sizeof(sr));
        const auto* srBlock = registry.Find("SL_MagicMenu_SpellResearch.archetypes");
        Expect(srOutcome.accepted && srOutcome.apiVersion == BlockApi::kVersionV2 && srBlock && srBlock->apiVersion == BlockApi::kVersionV2 &&
                   Address(srBlock->drawSelection) == Address(&SrDrawSelection) && srBlock->tabs == BlockApi::kTabSpells && NoV3Fields(srBlock),
            Text("SR's own v2 header bytes (%zu): accepted, answered as v2 (Result.apiVersion %u), no v3 field", sizeof(sr), srOutcome.apiVersion));

        auto badFlag = V3("badflag");
        badFlag.flags = BlockApi::kFlagsKnown + 1;
        ExpectRefused(Send(registry, Bytes(badFlag)), "does not know", "v3 flags with a bit outside kFlagsKnown");
        auto nanPad = V3("nanpad");
        nanPad.pad = std::numeric_limits<float>::quiet_NaN();
        ExpectRefused(Send(registry, Bytes(nanPad)), "pad", "v3 pad NaN");
        const auto notRetired = Send(registry, Bytes(V3("notretired", "preview")));
        ExpectRefused(notRetired, "retired", "v3 legacyId preview (not a retired id)");
        Expect(notRetired.apiVersion == BlockApi::kVersion, "a v3 refusal is answered as v3");
        ExpectRefused(Send(registry, Bytes(V3("second", "tree"))), "claimed already", "a second claim of tree");
        Expect(registry.LegacyAliases().at("tree") == "AddonA.whole" && !registry.Find("AddonA.second"), "... the first claim stays");
        const std::string longLegacy(BlockApi::kMaxIdLength + 1, 'l');
        ExpectRefused(Send(registry, Bytes(V3("longlegacy", longLegacy.c_str()))), "legacyId", "a legacyId past kMaxIdLength");
        auto neither = V3("neither");
        neither.drawSelection = nullptr;
        ExpectRefused(Send(registry, Bytes(neither)), "neither draw nor drawSelection", "v3 with neither draw");

        std::printf("legacy ids (BlockRules::JudgeLegacy)\n");
        for (const auto retired : BlockRules::kRetiredIds) {
            Expect(!BlockRules::JudgeLegacy(retired, false), Text("%s, unclaimed: taken", std::string(retired).c_str()));
        }
        Expect(BlockRules::kRetiredIds.size() == 3 && BlockRules::kRetiredIds[0] == "tree" && BlockRules::kRetiredIds[1] == "progress" &&
                   BlockRules::kRetiredIds[2] == "nodecard",
            "kRetiredIds: tree, progress, nodecard");
        Expect(!BlockRules::JudgeLegacy("", false) && BlockRules::JudgeLegacy("tree", true) && BlockRules::JudgeLegacy("Tree", false),
            "empty: no claim; tree claimed already: refused; Tree (case): refused");
    }

    void V3Messages()
    {
        std::printf("'SLMS' / 'SLMV' (BlockRules::ReadShowSpell, ReadVisible, ShowSpellDue)\n");
        std::string why;
        const std::string id = "SL_MagicMenu_HoM.tree";
        const BlockApi::ShowSpellRequest show{ .id = id.c_str(), .formId = 0x0001C789, .requestId = 7, .nameShown = 1 };
        const auto spell = BlockRules::ReadShowSpell(&show, sizeof(show), why);
        Expect(spell && spell->id == id && spell->formId == 0x0001C789 && spell->requestId == 7 && spell->nameShown, "a whole ShowSpellRequest read");
        const auto shortBy = static_cast<std::uint32_t>(BlockRules::kShowSpellNeeded - 1);
        const bool showShort = !BlockRules::ReadShowSpell(&show, shortBy, why) && why.find(std::to_string(shortBy) + " bytes") != std::string::npos;
        Expect(showShort, Text("'SLMS' one byte short: refused with the byte count (%s)", why.c_str()));
        for (const std::uint32_t version : { BlockApi::kVersionV2, BlockApi::kVersion + 1 }) {
            auto bad = show;
            bad.apiVersion = version;
            Expect(!BlockRules::ReadShowSpell(&bad, sizeof(bad), why), Text("'SLMS' apiVersion %u: refused", version));
        }
        const BlockApi::ShowSpellRequest noForm{ .id = id.c_str() }, noId{ .id = "", .formId = 1 };
        Expect(!BlockRules::ReadShowSpell(&noForm, sizeof(noForm), why) && !BlockRules::ReadShowSpell(nullptr, sizeof(show), why),
            "'SLMS' with formId 0, or null data: refused");
        Expect(!BlockRules::ReadShowSpell(&noId, sizeof(noId), why), "'SLMS' with an empty id: refused");

        const BlockApi::VisibleRequest hide{ .id = id.c_str(), .shown = 0 }, showAgain{ .id = id.c_str(), .shown = 1 };
        const auto hidden = BlockRules::ReadVisible(&hide, sizeof(hide), why);
        const auto shownAgain = BlockRules::ReadVisible(&showAgain, sizeof(showAgain), why);
        Expect(hidden && hidden->id == id && !hidden->shown && shownAgain && shownAgain->shown, "VisibleRequest shown 0 / 1 read");
        const auto visibleShort = static_cast<std::uint32_t>(BlockRules::kVisibleNeeded - 1);
        const bool hideShort = !BlockRules::ReadVisible(&hide, visibleShort, why) && why.find(std::to_string(visibleShort) + " bytes") != std::string::npos;
        Expect(hideShort, Text("'SLMV' one byte short: refused with the byte count (%s)", why.c_str()));
        const BlockApi::VisibleRequest hideV2{ .apiVersion = BlockApi::kVersionV2, .id = id.c_str() };
        Expect(!BlockRules::ReadVisible(&hideV2, sizeof(hideV2), why), "'SLMV' apiVersion 2: refused");

        Expect(BlockRules::ShowSpellDue(5, 5) && !BlockRules::ShowSpellDue(5, 6), "ShowSpellDue(5,5) true, (5,6) false (a newer list choice wins)");
    }

    int Run()
    {
        Layouts();
        V1Registrations();
        V2Registrations();
        TabMask();
        ClickRules();
        RedrawIds();
        HeightRequests();
        V3Registrations();
        V3Messages();
        std::printf("block-api-check: %d PASS, %d FAIL\n", g_pass, g_fail);
        return g_fail == 0 ? kExitOk : kExitFail;
    }
}

int main(int a_argc, char** a_argv)
{
    const std::string_view arg = a_argc > 1 ? a_argv[1] : "";
    if (!arg.empty() && arg != "--selftest") {
        std::fprintf(stderr, "usage: magic-menu-block-api-check [--selftest]\n");
        return kExitUsage;
    }
    const int code = Run();
    if (arg == "--selftest") std::printf(g_fail == 0 ? "selftest: all PASS\n" : "selftest: %d FAIL\n", g_fail);
    return code;
}
