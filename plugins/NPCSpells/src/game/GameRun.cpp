#include "game/GameRun.h"

#include "core/NpcSpellsBuild.h"
#include "game/GameFiles.h"
#include "game/GameLists.h"
#include "game/GameNpcs.h"
#include "game/GamePool.h"

#include <chrono>
#include <format>
#include <map>
#include <exception>
#include <string>

namespace
{
    using Clock = std::chrono::steady_clock;

    long long MsSince(Clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }

    std::string SkippedText(const std::map<std::string, std::size_t>& skipped)
    {
        std::string out;
        for (const auto& [reason, count] : skipped) {
            out += std::format("{}{}={}", out.empty() ? "" : ", ", reason, count);
        }
        return out.empty() ? "none" : out;
    }
}

void NpcSpellsGame::Run()
{
    // A malformed file or an unreadable folder must never take the game down
    try {
        const auto runStart = Clock::now();
        auto stageStart = runStart;
        auto inputs = ReadInputs();
        logger::info("NPCSpells: input files read in {} ms", MsSince(stageStart));
        if (!inputs.ok) {
            logger::error("NPCSpells: stopped: {}", inputs.stopReason);
            return;
        }
        if (inputs.rulesFromDefaults) {
            logger::info("NPCSpells: using the built-in rules");
        }

        const auto esl = LookupEslRecords();
        logger::info("NPCSpells: ESL records loaded={} found={} missing={} wrongType={}",
            esl.loaded, esl.byRecord.size() - esl.missing - esl.wrongType, esl.missing, esl.wrongType);
        if (!esl.loaded) {
            return;
        }

        stageStart = Clock::now();
        NpcSpells::PoolStats ps;
        const auto pool = ResolvePool(inputs.tree, inputs.catalog, inputs.rules, inputs.distr, ps);
        logger::info("NPCSpells: pool in {} ms: treeNodes={} notInCatalog={} noSchoolOrTier={} excludedTag={} excludedList={} "
                     "excludedDistr={} unresolved={} schoolMismatch={} kept={}",
            MsSince(stageStart), ps.treeNodes, ps.notInCatalog, ps.noSchoolOrTier, ps.excludedTag, ps.excludedList,
            ps.excludedDistr, ps.unresolved, ps.schoolMismatch, ps.kept);
        if (pool.facts.empty()) {
            logger::error("NPCSpells: stopped: no tree spell left to give (the catalog version, the tree's schools or the "
                          "counts above say why); nothing changed");
            return;
        }

        stageStart = Clock::now();
        NpcSpells::BuildStats bs;
        const auto lists = NpcSpells::BuildLists(pool.facts, inputs.rules, bs);
        std::size_t skippedNoRecord = 0;
        const auto index = FillLists(lists, pool, esl, skippedNoRecord);
        logger::info("NPCSpells: lists built and filled in {} ms: lists={} emptySlots={} droppedOverCapacity={} "
                     "skippedNoRecord={} perSchool={}/{}/{}/{}/{}",
            MsSince(stageStart), bs.lists, bs.emptySlots, bs.droppedOverCapacity, skippedNoRecord,
            bs.perSchool[0], bs.perSchool[1], bs.perSchool[2], bs.perSchool[3], bs.perSchool[4]);
        if (lists.size() == skippedNoRecord) {
            logger::error("NPCSpells: stopped: no leveled list was filled; no NPC changed");
            return;
        }

        stageStart = Clock::now();
        std::vector<NpcSpells::NpcLine> lines;  // the report's NPC rows (Task 10)
        const auto ns = AttachToNpcs(inputs.rules, index, esl, lines);
        logger::info("NPCSpells: NPCs in {} ms: scanned={} eligible={} attached={} listsAttached={} unresolvableTemplate={} "
                     "multiGroup={} unresolvedRefs={}",
            MsSince(stageStart), ns.scanned, ns.eligible, ns.attached, ns.listsAttached, ns.unresolvableTemplate,
            ns.multiGroup, ns.unresolvedRefs);
        logger::info("NPCSpells: attached by tier nonMage/hybrid/mage={}/{}/{} by group None/Forsworn/Vampire/Necromancer/"
                     "Thalmor/Vigilant={}/{}/{}/{}/{}/{}",
            ns.byTier[0], ns.byTier[1], ns.byTier[2], ns.byGroup[0], ns.byGroup[1], ns.byGroup[2], ns.byGroup[3],
            ns.byGroup[4], ns.byGroup[5]);
        logger::info("NPCSpells: skipped: {}", SkippedText(ns.skipped));
        // Spec section 9: budget 500 ms for about 26,000 NPCs
        logger::info("NPCSpells: done in {} ms total", MsSince(runStart));
    } catch (const std::exception& e) {
        logger::error("NPCSpells: stopped by an error: {}", e.what());
    } catch (...) {
        logger::error("NPCSpells: stopped by an unknown error");
    }
}
