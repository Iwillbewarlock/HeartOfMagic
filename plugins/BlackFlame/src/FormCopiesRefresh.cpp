// =============================================================================
// FORM COPIES REFRESH - a copy filled again from its live source, and the copies' check lines (FormCopies.h)
// =============================================================================
// W18: split from FormCopies.cpp (the fill), which reads the map and fills the copies once at kDataLoaded.

#include "FormCopiesState.h"

#include "TrackedMutex.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace BlackFlame
{
    using CopiesState::TheCopies;

    namespace
    {
        TrackedMutex g_refreshMutex;
        std::vector<std::pair<std::string, std::string>> g_refreshNotes;  // (reason, note), under g_refreshMutex
        std::set<std::string> g_pointersOnlyReasons;                      // reasons whose refresh wrote links only
        std::unordered_set<RE::FormID> g_deferred;  // W26: copies a links-only refresh left differing, until a full one
    }

    void RefreshCopies(const char* a_reason, bool a_pointersOnly)
    {
        using Clock = std::chrono::steady_clock;
        const auto micros = [](Clock::duration a_d) { return std::chrono::duration_cast<std::chrono::microseconds>(a_d).count(); };
        const auto start = Clock::now();
        Clock::duration compare{};
        Clock::duration refill{};
        const CopyFill::PointerNameScope names;  // perf: one form map walk for this refresh's diff texts, at most
        std::vector<std::string> notes;
        std::unordered_set<RE::FormID> deferred;
        std::size_t compared = 0;
        for (const auto& entry : TheCopies().entries) {
            if (!entry.ok) {
                continue;
            }
            ++compared;
            auto t = Clock::now();
            const auto before = CopyFill::Compare(entry.copy, entry.source);
            compare += Clock::now() - t;
            if (before.empty()) {
                continue;
            }
            t = Clock::now();
            // D3: no window where a copy points at an original. W11: in play the link stores only (one pointer each); the
            // structs, names and muzzle models wait for the next load's full refresh
            (void)CopyFill::Refill(entry.copy, entry.source, a_pointersOnly);
            refill += Clock::now() - t;
            t = Clock::now();
            const auto after = CopyFill::Compare(entry.copy, entry.source);
            compare += Clock::now() - t;
            if (a_pointersOnly && !after.empty()) {
                deferred.insert(entry.copy->GetFormID());
            }
            notes.push_back(std::format("{} {:08X} -> copy {:08X} differed at the {} ({}); filled again{}: {}", entry.sourceText,
                entry.source->GetFormID(), entry.copy->GetFormID(), a_reason, before,
                a_pointersOnly ? " (links only, in play)" : "",
                after.empty() ? "now equal" : (a_pointersOnly ? "the rest is deferred to the next load: " + after : after)));
            logger::warn("BlackFlame: copies: {}", notes.back());
        }
        // Perf: the diff texts' pointer naming is part of compare (diff text); before the reverse map it walked the form
        // map once per differing link field (1249 ms for 157 differing copies)
        logger::info("BlackFlame: copies: refresh at the {}: {} compared, {} cop{} differed from the live source; compare {} ms "
                     "(diff text pointer names {} ms{}), refill {} ms, {} ms in all", a_reason, compared, notes.size(),
            notes.size() == 1 ? "y" : "ies", micros(compare) / 1000, names.LookupMicros() / 1000,
            names.Walked() ? ", form map walked once" : "", micros(refill) / 1000, micros(Clock::now() - start) / 1000);
        if (a_pointersOnly) {
            logger::info("BlackFlame: copies: refresh at the {} wrote links only (in play); {} cop{} keep(s) differing fields "
                         "until the next load's refresh", a_reason, deferred.size(), deferred.size() == 1 ? "y" : "ies");
        }
        std::lock_guard lock(g_refreshMutex);
        // W27: a full refresh supersedes the notes of earlier links-only ones (the probe's in play); W26: and their deferrals
        if (!a_pointersOnly) {
            std::erase_if(g_refreshNotes, [](const auto& a_note) { return g_pointersOnlyReasons.contains(a_note.first); });
            g_pointersOnlyReasons.clear();
            g_deferred.clear();
        } else {
            g_pointersOnlyReasons.insert(a_reason);
            g_deferred.insert(deferred.begin(), deferred.end());
        }
        std::erase_if(g_refreshNotes, [&](const auto& a_note) { return a_note.first == a_reason; });
        for (auto& note : notes) {
            g_refreshNotes.emplace_back(a_reason, std::move(note));
        }
    }

    std::vector<std::string> CopyRefreshNotes()
    {
        std::lock_guard lock(g_refreshMutex);
        std::vector<std::string> out;
        for (const auto& [reason, note] : g_refreshNotes) {
            out.push_back(note);
        }
        return out;
    }

    std::vector<CopyCheckLine> CheckCopies()
    {
        std::vector<CopyCheckLine> lines;
        const auto& copies = TheCopies();
        const auto& c = copies.counts;
        const CopyFill::PointerNameScope names;  // perf: one form map walk for these diff texts, at most (Tester::Write, T0-v2)
        std::unordered_set<RE::FormID> deferred;
        {
            std::lock_guard lock(g_refreshMutex);
            deferred = g_deferred;
        }
        lines.push_back({ c.mapRead && c.failed == c.sourceAbsent && c.mapEntries > 0,
            std::format("copies map {}: {} entries, filled PROJ {} EXPL {} IPCT {} HAZD {} ARTO {} TXST {}, {} left out ({} of "
                "them: source mesh absent in this load order; decal texture sets: {} out of step, {} grey texture not opening), "
                "{} links to copies", c.mapRead ? "read" : "NOT read", c.mapEntries, c.projectiles, c.explosions, c.impacts,
                c.hazards, c.arts, c.textureSets, c.failed, c.sourceAbsent, c.textureSetsRejected, c.textureSetsUnopened,
                c.linksToCopies) });
        for (const auto& entry : copies.entries) {
            if (!entry.ok) {
                lines.push_back({ false, std::format("{}: left out: {}", entry.sourceText, entry.detail), entry.sourceAbsent });
                continue;
            }
            const auto diff = CopyFill::Compare(entry.copy, entry.source);
            const auto head = std::format("{} {:08X} -> copy {:08X}: ", entry.sourceText, entry.source->GetFormID(),
                entry.copy->GetFormID());
            if (!diff.empty() && deferred.contains(entry.copy->GetFormID())) {
                // W26: in play only its links were written (drop mode); the rest follows at the next load: not judged
                lines.push_back({ false, head + "deferred to the next load (in play only its links were written): " + diff, true });
                continue;
            }
            lines.push_back({ diff.empty(), head + (diff.empty() ? "fields equal the live source, links to copies where one exists" : diff) });
        }
        // v2i: a copy whose NIF the game could not load is out of the swaps for the session (it stays original)
        for (const auto& item : CopiesFailedToLoad()) {
            lines.push_back({ false, std::format("copy {:08X}: copy NIF failed to load in game, the form stays original this "
                                                 "session: {}", item.copy, item.text) });
        }
        return lines;
    }
}
