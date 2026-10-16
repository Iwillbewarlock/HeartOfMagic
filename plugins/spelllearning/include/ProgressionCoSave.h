#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// PROGRESSION CO-SAVE - byte layout of the progress (SLPR), cap tracker (SLPX)
// and tome XP (SLTX) records
// =============================================================================
//
// Templated on the stream so the plugin passes SKSE::SerializationInterface as it
// is (bool WriteRecordData(const void*, uint32), uint32 ReadRecordData(void*, uint32)
// returning the bytes read) and tools/progression-check.cpp a byte buffer. No game
// types; form ids are the raw ids as saved (the caller resolves them).
//
// SLPR, one entry per spell, written as v2 - the layout 1.5.0 and earlier read:
//   v1  formId u32, progressPercent f32, unlocked u8
//   v2  + moddedCount u32, then per modded source: nameLen u32, name bytes, xp f32
//   v3  + xpFromAny f32, xpFromSchool f32, xpFromDirect f32, xpFromSelf f32
//       (only a development build of 2026-10-17 wrote v3; it is still read)
// Older versions read with the missing fields at 0. A failed or cut-short read
// anywhere in an entry ends the record: every byte after it would be read at the
// wrong offset.
//
// SLPX v1: count u32, then per spell formId u32 and xpFromAny, xpFromSchool,
// xpFromDirect, xpFromSelf (f32) - what the built-in sources' caps count against.
// A record of its own, not an SLPR v3: a build that does not know SLPX skips it
// and still reads SLPR, so going back to 1.5.0 loses these counters, not the progress.
//
// SLTX v1: count u32, then count form ids (u32), the spells a tome already gave XP.
namespace ProgressionCoSave
{
    inline constexpr std::uint32_t kProgressVersion = 2;         // SLPR as written
    inline constexpr std::uint32_t kMaxProgressReadVersion = 3;  // SLPR as read
    inline constexpr std::uint32_t kCapTrackersVersion = 1;
    inline constexpr std::uint32_t kTomeXPVersion = 1;
    inline constexpr std::uint32_t kMaxNameLength = 4096;
    inline constexpr std::uint32_t kMaxModdedSources = 4096;

    struct Entry
    {
        std::uint32_t formId = 0;
        float progressPercent = 0.0f;
        bool unlocked = false;
        std::vector<std::pair<std::string, float>> modded;
        // Read from an SLPR v3 only; written to SLPX
        float xpFromAny = 0.0f;
        float xpFromSchool = 0.0f;
        float xpFromDirect = 0.0f;
        float xpFromSelf = 0.0f;
    };

    // One SLPX entry
    struct CapEntry
    {
        std::uint32_t formId = 0;
        float xpFromAny = 0.0f;
        float xpFromSchool = 0.0f;
        float xpFromDirect = 0.0f;
        float xpFromSelf = 0.0f;

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return xpFromAny == 0.0f && xpFromSchool == 0.0f && xpFromDirect == 0.0f && xpFromSelf == 0.0f;
        }
    };
    inline constexpr std::uint32_t kCapEntrySize = sizeof(std::uint32_t) + 4 * sizeof(float);

    enum class ReadResult
    {
        Ok,
        ShortRead,       // the stream ended inside a field
        NameTooLong,     // a modded source name over kMaxNameLength
        TooManySources,  // more than kMaxModdedSources modded sources
        CountTooLarge,   // a list count the record's length cannot hold
    };

    [[nodiscard]] constexpr const char* Describe(ReadResult result) noexcept
    {
        switch (result) {
            case ReadResult::Ok: return "ok";
            case ReadResult::ShortRead: return "the record ended inside a field";
            case ReadResult::NameTooLong: return "a source name is longer than the limit";
            case ReadResult::TooManySources: return "more modded sources than the limit";
            case ReadResult::CountTooLarge: return "a count larger than the record";
        }
        return "unknown";
    }

    // A modded source name the reader takes back (ReadEntry refuses longer ones,
    // and every entry after it in the record with them)
    [[nodiscard]] constexpr bool IsWritableSourceName(std::string_view name) noexcept
    {
        return name.size() <= kMaxNameLength;
    }

    [[nodiscard]] constexpr bool IsReadableProgressVersion(std::uint32_t version) noexcept
    {
        return version >= 1 && version <= kMaxProgressReadVersion;
    }

    namespace Detail
    {
        template <class Stream, class T>
        [[nodiscard]] bool Write(Stream& out, const T& value)
        {
            return out.WriteRecordData(&value, static_cast<std::uint32_t>(sizeof(T)));
        }

        template <class Stream, class T>
        [[nodiscard]] bool Read(Stream& in, T& value)
        {
            return in.ReadRecordData(&value, static_cast<std::uint32_t>(sizeof(T))) == sizeof(T);
        }

        // A count the record (length as GetNextRecordInfo gave it) can hold, with
        // `headerSize` bytes before the entries
        [[nodiscard]] constexpr bool CountFits(std::uint32_t count, std::uint32_t entrySize, std::uint32_t headerSize,
                                               std::uint32_t recordLength) noexcept
        {
            return headerSize + static_cast<std::uint64_t>(count) * entrySize <= recordLength;
        }
    }

    // Writes one entry in the kProgressVersion (v2) layout. A modded source the
    // reader would refuse - a name over kMaxNameLength, or past kMaxModdedSources -
    // is left out (counted in `skipped`): written, it would cost every entry after
    // it on load. False when a write failed.
    template <class Stream>
    [[nodiscard]] bool WriteEntry(Stream& out, const Entry& entry, std::uint32_t* skipped = nullptr)
    {
        std::vector<const std::pair<std::string, float>*> sources;
        sources.reserve(entry.modded.size());
        for (const auto& source : entry.modded) {
            if (IsWritableSourceName(source.first) && sources.size() < kMaxModdedSources) sources.push_back(&source);
        }
        if (skipped) *skipped = static_cast<std::uint32_t>(entry.modded.size() - sources.size());

        const std::uint8_t unlocked = entry.unlocked ? 1 : 0;
        bool ok = Detail::Write(out, entry.formId) && Detail::Write(out, entry.progressPercent) &&
                  Detail::Write(out, unlocked);
        const auto count = static_cast<std::uint32_t>(sources.size());
        ok = ok && Detail::Write(out, count);
        for (const auto* source : sources) {
            const auto length = static_cast<std::uint32_t>(source->first.size());
            ok = ok && Detail::Write(out, length) &&
                 (length == 0 || out.WriteRecordData(source->first.data(), length)) && Detail::Write(out, source->second);
        }
        return ok;
    }

    // Reads one entry saved with `version` (1..kMaxProgressReadVersion). Anything
    // but Ok means the stream is no longer aligned: stop reading the record.
    template <class Stream>
    [[nodiscard]] ReadResult ReadEntry(Stream& in, std::uint32_t version, Entry& entry)
    {
        entry = Entry{};
        std::uint8_t unlocked = 0;
        if (!Detail::Read(in, entry.formId) || !Detail::Read(in, entry.progressPercent) ||
            !Detail::Read(in, unlocked)) {
            return ReadResult::ShortRead;
        }
        entry.unlocked = unlocked != 0;

        if (version >= 2) {
            std::uint32_t count = 0;
            if (!Detail::Read(in, count)) return ReadResult::ShortRead;
            if (count > kMaxModdedSources) return ReadResult::TooManySources;
            entry.modded.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t length = 0;
                if (!Detail::Read(in, length)) return ReadResult::ShortRead;
                if (length > kMaxNameLength) return ReadResult::NameTooLong;
                std::string name(length, '\0');
                float xp = 0.0f;
                if ((length > 0 && in.ReadRecordData(name.data(), length) != length) || !Detail::Read(in, xp)) {
                    return ReadResult::ShortRead;
                }
                entry.modded.emplace_back(std::move(name), xp);
            }
        }

        if (version >= 3) {
            if (!Detail::Read(in, entry.xpFromAny) || !Detail::Read(in, entry.xpFromSchool) ||
                !Detail::Read(in, entry.xpFromDirect) || !Detail::Read(in, entry.xpFromSelf)) {
                return ReadResult::ShortRead;
            }
        }
        return ReadResult::Ok;
    }

    // SLPX: count, then the entries. False when a write failed.
    template <class Stream>
    [[nodiscard]] bool WriteCapEntries(Stream& out, const std::vector<CapEntry>& entries)
    {
        const auto count = static_cast<std::uint32_t>(entries.size());
        bool ok = Detail::Write(out, count);
        for (const auto& e : entries) {
            ok = ok && Detail::Write(out, e.formId) && Detail::Write(out, e.xpFromAny) &&
                 Detail::Write(out, e.xpFromSchool) && Detail::Write(out, e.xpFromDirect) &&
                 Detail::Write(out, e.xpFromSelf);
        }
        return ok;
    }

    // SLPX: a count the record cannot hold is refused before anything is
    // allocated. On a short read the whole entries read so far are kept.
    template <class Stream>
    [[nodiscard]] ReadResult ReadCapEntries(Stream& in, std::uint32_t recordLength, std::vector<CapEntry>& entries)
    {
        entries.clear();
        std::uint32_t count = 0;
        if (!Detail::Read(in, count)) return ReadResult::ShortRead;
        if (!Detail::CountFits(count, kCapEntrySize, sizeof(count), recordLength)) return ReadResult::CountTooLarge;
        entries.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            CapEntry e;
            if (!Detail::Read(in, e.formId) || !Detail::Read(in, e.xpFromAny) || !Detail::Read(in, e.xpFromSchool) ||
                !Detail::Read(in, e.xpFromDirect) || !Detail::Read(in, e.xpFromSelf)) {
                return ReadResult::ShortRead;
            }
            entries.push_back(e);
        }
        return ReadResult::Ok;
    }

    // SLPX onto the progress entries: each counter set goes to the spell's entry
    // (any map from form id to a struct with the four xpFrom* fields). A spell
    // with no progress entry gets none made - its SLPR entry did not load (plugin
    // gone, record cut short), and counters alone are no progress. Returns how
    // many were applied.
    template <class ProgressMap>
    std::size_t ApplyCapEntries(const std::vector<CapEntry>& entries, ProgressMap& progress)
    {
        std::size_t applied = 0;
        for (const auto& e : entries) {
            auto it = progress.find(e.formId);
            if (it == progress.end()) continue;
            it->second.xpFromAny = e.xpFromAny;
            it->second.xpFromSchool = e.xpFromSchool;
            it->second.xpFromDirect = e.xpFromDirect;
            it->second.xpFromSelf = e.xpFromSelf;
            ++applied;
        }
        return applied;
    }

    // SLTX: the form ids, count first. False when a write failed.
    template <class Stream>
    [[nodiscard]] bool WriteFormIds(Stream& out, const std::vector<std::uint32_t>& formIds)
    {
        const auto count = static_cast<std::uint32_t>(formIds.size());
        bool ok = Detail::Write(out, count);
        for (const auto formId : formIds) {
            ok = ok && Detail::Write(out, formId);
        }
        return ok;
    }

    // SLTX: `recordLength` is the record's length as GetNextRecordInfo gave it; a
    // count it cannot hold is refused before anything is allocated. On a short
    // read the ids read so far are kept in `formIds`.
    template <class Stream>
    [[nodiscard]] ReadResult ReadFormIds(Stream& in, std::uint32_t recordLength, std::vector<std::uint32_t>& formIds)
    {
        formIds.clear();
        std::uint32_t count = 0;
        if (!Detail::Read(in, count)) return ReadResult::ShortRead;
        if (!Detail::CountFits(count, sizeof(std::uint32_t), sizeof(count), recordLength)) {
            return ReadResult::CountTooLarge;
        }
        formIds.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t formId = 0;
            if (!Detail::Read(in, formId)) return ReadResult::ShortRead;
            formIds.push_back(formId);
        }
        return ReadResult::Ok;
    }
}
