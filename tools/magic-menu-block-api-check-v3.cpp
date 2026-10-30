// magic-menu-block-api-check, its v3 part (HoM add-on design 2026-10-30, "Block API v3"; plan Task 1 and Task 4): v3
// registrations (whole, cut at v2's size, SR's own v2 bytes beside them, flags, pad, legacy ids), the 'SLMS' / 'SLMV'
// messages, and the pure rules of the v3 host side in BlockRulesV3.h (the content box, the visible part, the overlay's
// clears and generation, the ShowSpell, Visible and key queues, the empty look with kFlagKeepWhenEmpty). See
// magic-menu-block-api-check.cpp for the tool's usage.

#include "magic-menu-block-api-check.h"

#include "BlockRules.h"
#include "BlockRulesV3.h"

#include <limits>
#include <set>

namespace BlockApiCheck
{
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

    namespace
    {
        // The reserved built-in ids of an SL_MagicMenu that still drew the HoM blocks itself (Widget.cpp's kBlocks before HoM add-on
        // plan Task 5): the registry's rule for a legacyId whose retired id is still reserved
        const std::set<std::string> kBuiltinsWithHom{ "preview", "tree", "progress", "nodecard", "spellcard" };
        // The core's reserved ids since Task 5 (Widget.cpp's kBlocks): the retired ids are free for a legacyId
        const std::set<std::string> kCoreBuiltins{ "preview", "spellcard" };

        void V3RegistrationRules()
        {
            std::printf("v3 registrations: pad bound, legacy id of a built-in still here (Task 4), the core's free retired ids (Task 5)\n");
            BlockRegistry::Registry registry;
            auto tallPad = V3("tallpad");
            tallPad.pad = BlockApi::kMaxHeight + 1.0f;
            ExpectRefused(Send(registry, Bytes(tallPad)), "pad", "v3 pad past kMaxHeight");
            auto maxPad = V3("maxpad");
            maxPad.pad = BlockApi::kMaxHeight;
            Expect(Send(registry, Bytes(maxPad)).accepted, "v3 pad = kMaxHeight: accepted (the host's clamps keep the box >= 0)");
            auto zeroPad = V3("zeropad");
            zeroPad.pad = 0.0f;
            const auto zeroOutcome = Send(registry, Bytes(zeroPad));
            const auto* zeroBlock = registry.Find("AddonA.zeropad");
            Expect(zeroOutcome.accepted && zeroBlock && zeroBlock->pad == 0.0, "v3 pad 0: accepted, kept as 0");

            BlockRegistry::Registry withBuiltins{ kBuiltinsWithHom };
            for (const auto retired : BlockRules::kRetiredIds) {
                const std::string legacy(retired);
                const auto outcome = Send(withBuiltins, Bytes(V3(("claims" + legacy).c_str(), legacy.c_str())));
                Expect(!outcome.accepted && outcome.reason == "the built-in block '" + legacy + "' is still in this SL_MagicMenu",
                    Text("legacyId %s while the built-in %s is reserved: refused, reason \"%s\"", legacy.c_str(), legacy.c_str(), outcome.reason.c_str()));
            }
            Expect(withBuiltins.LegacyAliases().empty() && withBuiltins.Blocks().empty(), "... no alias and no block from those refusals");
            Expect(Send(withBuiltins, Bytes(V3("nolegacy"))).accepted, "v3 with no legacyId beside the built-ins: accepted");
            auto both = V3("both");
            both.flags = BlockApi::kFlagOwnMouse;
            const auto bothOutcome = Send(withBuiltins, Bytes(both));
            const auto* bothBlock = withBuiltins.Find("AddonA.both");
            Expect(bothOutcome.accepted && bothBlock && bothBlock->click && (bothBlock->flags & BlockApi::kFlagOwnMouse) != 0,
                "kFlagOwnMouse with a click: accepted, both stored (the host ignores the click and logs it once)");

            // HoM add-on plan Task 5: the core reserves preview and spellcard only - each retired id is taken over by its legacyId,
            // a built-in's id is no legacyId, and a retired id is still taken once
            BlockRegistry::Registry core{ kCoreBuiltins };
            for (const auto retired : BlockRules::kRetiredIds) {
                const std::string legacy(retired);
                const auto outcome = Send(core, Bytes(V3(legacy.c_str(), legacy.c_str())));
                Expect(outcome.accepted, Text("core: legacyId %s accepted (%s)", legacy.c_str(), outcome.reason.c_str()));
            }
            const auto aliases = core.LegacyAliases();
            const auto aliasIs = [&](const char* a_legacy, const char* a_final) { return aliases.contains(a_legacy) && aliases.at(a_legacy) == a_final; };
            Expect(aliases.size() == 3 && aliasIs("tree", "AddonA.tree") && aliasIs("progress", "AddonA.progress") && aliasIs("nodecard", "AddonA.nodecard"),
                Text("core: three aliases, tree -> AddonA.tree, progress -> AddonA.progress, nodecard -> AddonA.nodecard (got %zu)", aliases.size()));
            Expect(!Send(core, Bytes(V3("tree2", "tree"))).accepted, "core: a second legacyId tree refused (one block per retired id)");
            Expect(!Send(core, Bytes(V3("preview2", "preview"))).accepted && !Send(core, Bytes(V3("card2", "spellcard"))).accepted,
                "core: legacyId preview / spellcard refused (built-in blocks, not retired ids)");
        }

        void ContentBoxes()
        {
            std::printf("the content box (BlockRules::ContentBoxOf, Registration::pad)\n");
            constexpr double kTitleH = 20.0, kHostPad = 8.0;
            const auto host = BlockRules::ContentBoxOf(300.0, 200.0, true, kTitleH, BlockApi::kPadDefault, kHostPad);
            Expect(host.left == 8.0 && host.top == 28.0 && host.width == 284.0 && host.height == 164.0,
                Text("pad -1 (the host's 8), titled: 8,28 284 x 164 (got %.1f,%.1f %.1f x %.1f)", host.left, host.top, host.width, host.height));
            const auto card = BlockRules::ContentBoxOf(300.0, 200.0, false, kTitleH, 4.0, kHostPad);
            Expect(card.left == 4.0 && card.top == 4.0 && card.width == 292.0 && card.height == 192.0,
                Text("pad 4, no title: 4,4 292 x 192 (got %.1f,%.1f %.1f x %.1f)", card.left, card.top, card.width, card.height));
            const auto none = BlockRules::ContentBoxOf(300.0, 200.0, true, kTitleH, 0.0, kHostPad);
            Expect(none.left == 0.0 && none.top == kTitleH && none.width == 300.0 && none.height == 180.0, "pad 0, titled: 0,20 300 x 180");
            const auto huge = BlockRules::ContentBoxOf(300.0, 200.0, true, kTitleH, BlockApi::kMaxHeight, kHostPad);
            Expect(huge.width == 0.0 && huge.height == 0.0 && huge.left == BlockApi::kMaxHeight, "a pad past the box: width and height 0, never negative");
        }

        void Views()
        {
            std::printf("the visible part (BlockRules::ViewInContent, Context::view*)\n");
            const Settings::Rect viewport{ 900.0, 100.0, 1200.0, 600.0 };
            const auto whole = BlockRules::ViewInContent({ 908.0, 200.0, 1192.0, 400.0 }, viewport);
            Expect(whole.left == 0.0 && whole.top == 0.0 && whole.right == 284.0 && whole.bottom == 200.0, "inside the viewport: the whole clip 0,0..284,200");
            // the column scrolled: the clip's top 50 units are above the viewport's top
            const auto top = BlockRules::ViewInContent({ 908.0, 50.0, 1192.0, 250.0 }, viewport);
            Expect(top.left == 0.0 && top.top == 50.0 && top.right == 284.0 && top.bottom == 200.0,
                Text("partly scrolled out at the top: 0,50..284,200 (got %.1f,%.1f..%.1f,%.1f)", top.left, top.top, top.right, top.bottom));
            const auto bottom = BlockRules::ViewInContent({ 908.0, 500.0, 1192.0, 700.0 }, viewport);
            Expect(bottom.top == 0.0 && bottom.bottom == 100.0, Text("partly below the viewport: top 0, bottom 100 (got %.1f, %.1f)", bottom.top, bottom.bottom));
            const auto out = BlockRules::ViewInContent({ 908.0, 650.0, 1192.0, 850.0 }, viewport);
            Expect(out.left == 0.0 && out.top == 0.0 && out.right == 0.0 && out.bottom == 0.0, "scrolled out entirely: all 0");
            const auto edge = BlockRules::ViewInContent({ 908.0, 600.0, 1192.0, 800.0 }, viewport);
            Expect(edge.Height() == 0.0 && edge.Width() == 0.0, "touching the viewport's bottom edge only: all 0 (nothing shows)");
        }

        struct OverlayCase
        {
            const char* name;
            void (*change)(BlockRules::OverlaySnapshot&);
            bool remove, make;
            BlockRules::OverlayEvent why;
        };

        void Overlays()
        {
            std::printf("the overlay's clears and generation (BlockRules::StepOverlay)\n");
            constexpr double kTolerance = 0.01;
            using S = BlockRules::OverlaySnapshot;
            using E = BlockRules::OverlayEvent;
            S now{ true, 1, { 900.0, 100.0, 1200.0, 300.0 }, 1, 0, 5 };
            BlockRules::OverlayTrack track;
            const OverlayCase events[] = {
                { "the first frame shown", [](S&) {}, false, true, E::kNone },
                { "a frame with nothing changed", [](S&) {}, false, false, E::kNone },
                { "a change below the tolerance", [](S& a_s) { a_s.onScreen.top += 0.005; }, false, false, E::kNone },
                { "a scroll (the rect on screen moves up)", [](S& a_s) { a_s.onScreen.top -= 40; a_s.onScreen.bottom -= 40; }, true, true, E::kMoved },
                { "a resize ('SLMH')", [](S& a_s) { a_s.onScreen.bottom -= 30; }, true, true, E::kMoved },
                { "a move to the other column", [](S& a_s) { a_s.column = 0; }, true, true, E::kMoved },
                { "a redraw (a new content clip)", [](S& a_s) { ++a_s.contentDraws; }, true, true, E::kDrawn },
                // the host makes no overlay in the layout mode (its covers stay on top): shown is false there
                { "the layout mode on", [](S& a_s) { ++a_s.modeChanges; a_s.shown = false; }, true, false, E::kLayoutMode },
                { "in the layout mode, scrolled", [](S& a_s) { a_s.onScreen.top -= 10; a_s.onScreen.bottom -= 10; }, false, false, E::kNone },
                { "the layout mode off", [](S& a_s) { ++a_s.modeChanges; a_s.shown = true; }, false, true, E::kNone },
                { "a panel made in its column after it", [](S& a_s) { ++a_s.panelsMade; }, true, true, E::kPanelAbove },
                { "hidden ('SLMV' 0, a tab, the columns down)", [](S& a_s) { a_s.shown = false; }, true, false, E::kHidden },
                { "still hidden", [](S&) {}, false, false, E::kNone },
                { "hidden and drawn again meanwhile", [](S& a_s) { ++a_s.contentDraws; }, false, false, E::kNone },
                { "shown again", [](S& a_s) { a_s.shown = true; }, false, true, E::kNone },
            };
            std::uint32_t generation = 0;
            for (const auto& event : events) {
                event.change(now);
                const auto step = BlockRules::StepOverlay(track, now, kTolerance);
                if (event.make) ++generation;
                Expect(step.remove == event.remove && step.make == event.make && step.why == event.why && track.generation == generation &&
                           track.made.has_value() == now.shown,
                    Text("%s: remove %d make %d (%s), generation %u (got remove %d make %d, %s, generation %u)", event.name, event.remove, event.make,
                        BlockRules::OverlayEventText(event.why), generation, step.remove, step.make, BlockRules::OverlayEventText(step.why), track.generation));
            }
            Expect(track.generation == 8, Text("8 overlay clips made over the 15 frames, a generation each (got %u)", track.generation));
            BlockRules::OverlayTrack both;
            S hiddenMode = now;
            BlockRules::StepOverlay(both, now, kTolerance);
            ++hiddenMode.modeChanges;
            hiddenMode.shown = false;
            Expect(BlockRules::StepOverlay(both, hiddenMode, kTolerance).why == E::kLayoutMode, "the layout mode on: the reason is the layout mode, not a hide");
            BlockRules::OverlayTrack failed{ std::nullopt, 4 };
            BlockRules::StepOverlay(failed, now, kTolerance);
            failed.made.reset();  // the host's make failed
            const auto retry = BlockRules::StepOverlay(failed, now, kTolerance);
            Expect(retry.make && !retry.remove && failed.generation == 6, "a failed make is tried again the next frame, with a new generation");
        }

        BlockRules::QueuedShowSpell Request(const char* a_id, std::uint32_t a_form, std::uint32_t a_request, std::uint64_t a_seq)
        {
            return { { a_id, a_form, a_request, true }, a_seq };
        }

        std::vector<std::uint32_t> RequestIds(const std::vector<BlockRules::QueuedShowSpell>& a_requests)
        {
            std::vector<std::uint32_t> ids;
            for (const auto& queued : a_requests) ids.push_back(queued.request.requestId);
            return ids;
        }

        void Queues()
        {
            std::printf("the 'SLMS' queue (arrival order, newest per block; one served per frame, the newest, if still due), the 'SLMV' set, the key queue\n");
            using Ids = std::vector<std::uint32_t>;
            using BlockRules::Queued;
            BlockRules::ShowSpellQueue queue;
            Expect(BlockRules::EnqueueShowSpell(queue, Request("A.tree", 0x100, 1, 5), 2) == Queued::kAdded &&
                       BlockRules::EnqueueShowSpell(queue, Request("B.list", 0x200, 9, 5), 2) == Queued::kAdded &&
                       BlockRules::EnqueueShowSpell(queue, Request("A.tree", 0x101, 2, 5), 2) == Queued::kReplaced,
                "A.tree, B.list, A.tree again: added, added, replaced");
            Expect(RequestIds(queue) == Ids{ 9, 2 } && queue.back().request.formId == 0x101, "the queue in arrival order, A.tree's newest last: 9, 2");
            Expect(BlockRules::EnqueueShowSpell(queue, Request("C.card", 0x300, 4, 5), 2) == Queued::kFull && queue.size() == 2,
                "a third id past the cap 2: dropped");
            const auto due = BlockRules::PickShowSpell(queue, 5);
            Expect(due.serve && due.serve->request.requestId == 2 && RequestIds(due.dropped) == Ids{ 9 },
                "seq 5: the newest (A.tree's request 2) served, the older one (9) dropped - one per frame");
            const auto stale = BlockRules::PickShowSpell(queue, 6);
            Expect(!stale.serve && RequestIds(stale.dropped) == Ids{ 9, 2 }, "seq 6 (the player chose after they were sent): none served, both dropped");
            BlockRules::ShowSpellQueue mixed{ Request("B.list", 0x200, 7, 5), Request("A.tree", 0x100, 8, 6) };
            const auto newest = BlockRules::PickShowSpell(mixed, 6);
            Expect(newest.serve && newest.serve->request.requestId == 8 && RequestIds(newest.dropped) == Ids{ 7 },
                "an older request sent before a list change, the newest after it: the newest served, the stale one dropped");
            Expect(!BlockRules::PickShowSpell({}, 3).serve && BlockRules::PickShowSpell({}, 3).dropped.empty(), "an empty queue: nothing served or dropped");

            std::set<std::string> hidden;
            Expect(BlockRules::SetShown(hidden, "A", false) && hidden.contains("A"), "'SLMV' A shown 0: hidden, a change");
            Expect(!BlockRules::SetShown(hidden, "A", false), "A shown 0 again: no change");
            Expect(BlockRules::SetShown(hidden, "A", true) && hidden.empty(), "A shown 1: shown again, a change");
            Expect(!BlockRules::SetShown(hidden, "B", true), "B shown 1 (never hidden): no change");

            Ids keys;
            Expect(BlockRules::QueueKey(keys, 22, 2) && BlockRules::QueueKey(keys, 22, 2) && keys == Ids{ 22, 22 }, "keys 22, 22: both queued (every key down)");
            Expect(!BlockRules::QueueKey(keys, 34, 2) && keys.size() == 2, "a third key past the cap 2: dropped");
        }

        void EmptyLooks()
        {
            std::printf("the empty look (kFlagKeepWhenEmpty, BlockRules::ClosesWhenEmpty)\n");
            using BlockRules::EmptyLook;
            Expect(BlockRules::ClosesWhenEmpty(true, 0) && !BlockRules::ClosesWhenEmpty(true, BlockApi::kFlagKeepWhenEmpty) &&
                       !BlockRules::ClosesWhenEmpty(false, 0) && BlockRules::ClosesWhenEmpty(true, BlockApi::kFlagOwnMouse),
                "closes when empty: drawSelection without kFlagKeepWhenEmpty only");
            const bool keeps = BlockRules::ClosesWhenEmpty(true, BlockApi::kFlagKeepWhenEmpty), closes = BlockRules::ClosesWhenEmpty(true, 0);
            Expect(BlockRules::EmptyLookOf("SL_MagicMenu_HoM.tree", true, keeps, false) == EmptyLook::kAsUsual &&
                       BlockRules::EmptyLookOf("SL_MagicMenu_HoM.tree", true, keeps, true) == EmptyLook::kAsUsual,
                "drawSelection with kFlagKeepWhenEmpty and nothing shown: as usual (place kept), in and out of the layout mode");
            Expect(BlockRules::EmptyLookOf("SpellResearch.archetypes", true, closes) == EmptyLook::kClosed &&
                       BlockRules::EmptyLookOf("SpellResearch.archetypes", true, closes, true) == EmptyLook::kAsUsual,
                "without the flag (SR's v2 block): closed, shown in the layout mode - as before");
        }
    }

    void V3Host()
    {
        V3RegistrationRules();
        ContentBoxes();
        Views();
        Overlays();
        Queues();
        EmptyLooks();
    }
}
