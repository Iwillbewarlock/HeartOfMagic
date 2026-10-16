#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// PROGRESSION CO-SAVE - byte layout of the progress (SLPR) and tome XP (SLTX) records
// =============================================================================
//
// Templated on the stream so the plugin passes SKSE::SerializationInterface as it
// is (bool WriteRecordData(const void*, uint32), uint32 ReadRecordData(void*, uint32)
// returning the bytes read) and tools/progression-check.cpp a byte buffer. No game
// types; form ids are the raw ids as saved (the caller resolves them).
//
// SLPR, one entry per spell:
//   v1  formId u32, progressPercent f32, unlocked u8
//   v2  + moddedCount u32, then per modded source: nameLen u32, name bytes, xp f32
//   v3  + xpFromAny f32, xpFromSchool f32, xpFromDirect f32, xpFromSelf f32
// Older versions read with the missing fields at 0. A failed or cut-short read
// anywhere in an entry ends the record: every byte after it would be read at the
// wrong offset.
//
// SLTX v1: count u32, then count form ids (u32), the spells a tome already gave XP.
namespace ProgressionCoSave
{
    inline constexpr std::uint32_t kProgressVersion = 3;
    inline constexpr std::uint32_t kTomeXPVersion = 1;
    inline constexpr std::uint32_t kMaxNameLength = 4096;
    inline constexpr std::uint32_t kMaxModdedSources = 4096;

    struct Entry
    {
        std::uint32_t formId = 0;
        float progressPercent = 0.0f;
        bool unlocked = false;
        std::vector<std::pair<std::string, float>> modded;
        float xpFromAny = 0.0f;
        float xpFromSchool = 0.0f;
        float xpFromDirect = 0.0f;
        float xpFromSelf = 0.0f;
    };

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

    [[nodiscard]] constexpr bool IsReadableProgressVersion(std::uint32_t version) noexcept
    {
        return version >= 1 && version <= kProgressVersion;
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
    }

    // Writes one entry in the kProgressVersion layout. False when a write failed.
    template <class Stream>
    [[nodiscard]] bool WriteEntry(Stream& out, const Entry& entry)
    {
        const std::uint8_t unlocked = entry.unlocked ? 1 : 0;
        bool ok = Detail::Write(out, entry.formId) && Detail::Write(out, entry.progressPercent) &&
                  Detail::Write(out, unlocked);
        const auto count = static_cast<std::uint32_t>(entry.modded.size());
        ok = ok && Detail::Write(out, count);
        for (const auto& [name, xp] : entry.modded) {
            const auto length = static_cast<std::uint32_t>(name.size());
            ok = ok && Detail::Write(out, length) && (length == 0 || out.WriteRecordData(name.data(), length)) &&
                 Detail::Write(out, xp);
        }
        return ok && Detail::Write(out, entry.xpFromAny) && Detail::Write(out, entry.xpFromSchool) &&
               Detail::Write(out, entry.xpFromDirect) && Detail::Write(out, entry.xpFromSelf);
    }

    // Reads one entry saved with `version` (1..kProgressVersion). Anything but Ok
    // means the stream is no longer aligned: stop reading the record.
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
        const std::uint64_t needed = sizeof(count) + static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
        if (needed > recordLength) return ReadResult::CountTooLarge;
        formIds.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t formId = 0;
            if (!Detail::Read(in, formId)) return ReadResult::ShortRead;
            formIds.push_back(formId);
        }
        return ReadResult::Ok;
    }
}
