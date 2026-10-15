#include "game/GameRun.h"

#include "core/NpcSpellsBuild.h"
#include "core/NpcSpellsReport.h"
#include "game/GameFiles.h"
#include "game/GameLists.h"
#include "game/GameNpcs.h"
#include "game/GamePool.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>

#ifndef NPCSPELLS_VERSION
#define NPCSPELLS_VERSION "unknown"
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    double MsSince(Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    std::string SkippedText(const std::map<std::string, std::size_t>& skipped)
    {
        std::string out;
        for (const auto& [reason, count] : skipped) {
            out += std::format("{}{}={}", out.empty() ? "" : ", ", reason, count);
        }
        return out.empty() ? "none" : out;
    }

    // Always written, also for a stopped run (spec section 7). UTF-8 bytes as they are; a failure only logs.
    void WriteReport(NpcSpells::ReportData& rd)
    {
        try {
            rd.version = NPCSPELLS_VERSION;
            rd.generatedUtc = std::format("{:%F %T}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
            const std::filesystem::path path = NpcSpellsGame::kReportPath;
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << NpcSpells::RenderReport(rd);
            file.close();  // close() flushes: a failed final write sets failbit here, after the << above looked fine
            if (!file) {
                logger::error("NPCSpells: could not write the report {}", NpcSpellsGame::kReportPath);
            }
        } catch (const std::exception& e) {
            logger::error("NPCSpells: could not write the report: {}", e.what());
        }
    }

    // Fills rd as far as the run got; a return before the end leaves rd.stopReason set
    void RunImpl(NpcSpells::ReportData& rd)
    {
        const auto runStart = Clock::now();
        auto stageStart = runStart;
        auto inputs = NpcSpellsGame::ReadInputs();
        rd.timingsMs.emplace_back("input files", MsSince(stageStart));
        logger::info("NPCSpells: input files read in {:.0f} ms", rd.timingsMs.back().second);
        rd.rulesFromDefaults = inputs.rulesFromDefaults;
        rd.rulesError = inputs.rulesError;
        if (!inputs.ok) {
            rd.stopReason = inputs.stopReason;
            logger::error("NPCSpells: stopped: {}", inputs.stopReason);
            return;
        }
        if (inputs.rulesFromDefaults) {
            logger::info("NPCSpells: using the built-in rules");
        }

        const auto esl = NpcSpellsGame::LookupEslRecords();
        rd.eslRecords = esl.byRecord.size();
        rd.eslMissing = esl.missing;
        rd.eslWrongType = esl.wrongType;
        logger::info("NPCSpells: ESL records loaded={} found={} missing={} wrongType={}",
            esl.loaded, esl.byRecord.size() - esl.missing - esl.wrongType, esl.missing, esl.wrongType);
        if (!esl.loaded) {
            rd.stopReason = "HeartOfMagic_NPCSpells.esp is not loaded";
            return;
        }

        stageStart = Clock::now();
        NpcSpells::PoolStats& ps = rd.pool;
        const auto pool = NpcSpellsGame::ResolvePool(inputs.tree, inputs.catalog, inputs.rules, inputs.distr, ps);
        rd.timingsMs.emplace_back("pool", MsSince(stageStart));
        logger::info("NPCSpells: pool in {:.0f} ms: treeNodes={} duplicates={} notInCatalog={} noSchoolOrTier={} excludedTag={} excludedList={} "
                     "excludedDistr={} unresolved={} schoolMismatch={} kept={}",
            rd.timingsMs.back().second, ps.treeNodes, ps.duplicates, ps.notInCatalog, ps.noSchoolOrTier, ps.excludedTag, ps.excludedList,
            ps.excludedDistr, ps.unresolved, ps.schoolMismatch, ps.kept);
        if (pool.facts.empty()) {
            rd.stopReason = "no tree spell left to give (the catalog version, the tree's schools or the pool counts say why); nothing changed";
            logger::error("NPCSpells: stopped: {}", rd.stopReason);
            return;
        }

        stageStart = Clock::now();
        NpcSpells::BuildStats& bs = rd.build;
        const auto lists = NpcSpells::BuildLists(pool.facts, inputs.rules, bs);
        const auto index = NpcSpellsGame::FillLists(lists, pool, esl, rd.listsSkippedNoRecord, rd.listsSkippedPrefilled, rd.listsSkippedNoMemory);
        for (const auto& l : lists) {
            // Only the lists the game has: a skipped one (no record, prefilled, no memory) is counted in listsSkipped*, not listed
            if (l.slotIndex >= index.present.size() || !index.present[l.slotIndex][l.chunk]) {
                continue;
            }
            rd.listSizes.emplace_back(NpcSpells::EditorIdFor(NpcSpells::SlotTable()[l.slotIndex].key, l.chunk), l.entries.size(), l.flags);
        }
        rd.timingsMs.emplace_back("lists", MsSince(stageStart));
        logger::info("NPCSpells: lists built and filled in {:.0f} ms: lists={} emptySlots={} droppedOverCapacity={} "
                     "variantsToppedUp={} skippedNoRecord={} skippedPrefilled={} skippedNoMemory={} perSchool={}/{}/{}/{}/{}",
            rd.timingsMs.back().second, bs.lists, bs.emptySlots, bs.droppedOverCapacity, bs.variantsToppedUp, rd.listsSkippedNoRecord, rd.listsSkippedPrefilled, rd.listsSkippedNoMemory,
            bs.perSchool[0], bs.perSchool[1], bs.perSchool[2], bs.perSchool[3], bs.perSchool[4]);
        if (lists.size() == rd.listsSkippedNoRecord + rd.listsSkippedPrefilled + rd.listsSkippedNoMemory) {
            rd.stopReason = "no leveled list was filled; no NPC changed";
            logger::error("NPCSpells: stopped: {}", rd.stopReason);
            return;
        }

        stageStart = Clock::now();
        const auto ns = NpcSpellsGame::AttachToNpcs(inputs.rules, index, esl, rd.npcs);
        rd.timingsMs.emplace_back("NPCs", MsSince(stageStart));
        rd.npcsScanned = ns.scanned;
        rd.npcsEligible = ns.eligible;
        rd.npcsAttached = ns.attached;
        rd.listsAttached = ns.listsAttached;
        rd.inheritedAttached = ns.inheritedAttached;
        rd.inheritedLists = ns.inheritedLists;
        rd.inheritedCreated = ns.inheritedCreated;
        rd.inheritedViaShared = ns.inheritedViaShared;
        rd.inheriting = ns.inheriting;
        rd.notCopiedLeveledChain = ns.notCopiedLeveledChain;
        rd.notCopiedPlainChain = ns.notCopiedPlainChain;
        rd.beforeHolder = ns.beforeHolder;
        rd.otherProvider = ns.otherProvider;
        rd.sharedSpellData = ns.sharedSpellData;
        rd.sharedLevSpells = ns.sharedLevSpells;
        rd.blockedWrites = ns.blockedWrites;
        rd.unresolvableTemplate = ns.unresolvableTemplate;
        rd.multiGroup = ns.multiGroup;
        rd.unresolvedRefs = ns.unresolvedRefs;
        rd.byTier = ns.byTier;
        rd.byGroup = ns.byGroup;
        rd.skipped.assign(ns.skipped.begin(), ns.skipped.end());
        logger::info("NPCSpells: NPCs in {:.0f} ms: scanned={} eligible={} attached={} listsAttached={} inheritedAttached={} inheritedLists={} "
                     "inheritedCreated={} inheritedViaShared={} unresolvableTemplate={} multiGroup={} unresolvedRefs={}",
            rd.timingsMs.back().second, ns.scanned, ns.eligible, ns.attached, ns.listsAttached, ns.inheritedAttached, ns.inheritedLists,
            ns.inheritedCreated, ns.inheritedViaShared, ns.unresolvableTemplate, ns.multiGroup, ns.unresolvedRefs);
        // Read at kDataLoaded, before any write: the template children and the memory audit (sharedLevSpells and blockedWrites expected 0)
        logger::info("NPCSpells: template spell list: inheriting={} notCopiedTemplate leveledChain={} plainChain={} beforeHolder={} "
                     "otherProvider={} sharedSpellData={} sharedLevSpells={} blockedWrites={}",
            ns.inheriting, ns.notCopiedLeveledChain, ns.notCopiedPlainChain, ns.beforeHolder, ns.otherProvider, ns.sharedSpellData,
            ns.sharedLevSpells, ns.blockedWrites);
        logger::info("NPCSpells: attached by tier nonMage/hybrid/mage={}/{}/{} by group None/Forsworn/Vampire/Necromancer/"
                     "Thalmor/Vigilant={}/{}/{}/{}/{}/{}",
            ns.byTier[0], ns.byTier[1], ns.byTier[2], ns.byGroup[0], ns.byGroup[1], ns.byGroup[2], ns.byGroup[3],
            ns.byGroup[4], ns.byGroup[5]);
        logger::info("NPCSpells: skipped: {}", SkippedText(ns.skipped));
        // Spec section 9: budget 500 ms for about 26,000 NPCs
        rd.timingsMs.emplace_back("total", MsSince(runStart));
        logger::info("NPCSpells: done in {:.0f} ms total", rd.timingsMs.back().second);
    }
}

void NpcSpellsGame::Run()
{
    NpcSpells::ReportData rd;
    // A malformed file or an unreadable folder must never take the game down
    try {
        RunImpl(rd);
    } catch (const std::exception& e) {
        rd.stopReason = std::format("stopped by an error: {}", e.what());
        logger::error("NPCSpells: stopped by an error: {}", e.what());
    } catch (...) {
        rd.stopReason = "stopped by an unknown error";
        logger::error("NPCSpells: stopped by an unknown error");
    }
    WriteReport(rd);
}
