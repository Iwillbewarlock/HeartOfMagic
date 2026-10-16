// progression-check - offline check of the progression co-save records and XP arithmetic
// (plugins/spelllearning/include/ProgressionCoSave.h, ProgressionMath.h).
//
//   progression-check --self-test     made-up byte buffers and numbers, no game needed
//
// The record code is the plugin's own (templated on the stream; here a byte buffer
// with SKSE's ReadRecordData/WriteRecordData shapes). Exit 0 when every case passes,
// 1 on a failure, 2 on a bad argument.

#include "ProgressionCoSave.h"
#include "ProgressionMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitUsage = 2;
    constexpr float kEpsilon = 1e-4f;

    namespace CS = ProgressionCoSave;
    namespace PM = ProgressionMath;

    // SKSE::SerializationInterface's shapes: a write is all or nothing, a read
    // returns the bytes it could read (fewer at the end of the record)
    struct ByteWriter
    {
        std::vector<std::uint8_t> bytes;

        bool WriteRecordData(const void* data, std::uint32_t length)
        {
            const auto* p = static_cast<const std::uint8_t*>(data);
            bytes.insert(bytes.end(), p, p + length);
            return true;
        }

        template <class T>
        void Put(const T& value) { WriteRecordData(&value, sizeof(T)); }

        void PutName(std::string_view name)
        {
            Put(static_cast<std::uint32_t>(name.size()));
            WriteRecordData(name.data(), static_cast<std::uint32_t>(name.size()));
        }
    };

    struct ByteReader
    {
        const std::vector<std::uint8_t>& bytes;
        std::size_t at = 0;

        std::uint32_t ReadRecordData(void* out, std::uint32_t length)
        {
            const auto n = static_cast<std::uint32_t>((std::min)(static_cast<std::size_t>(length), bytes.size() - at));
            std::memcpy(out, bytes.data() + at, n);
            at += n;
            return n;
        }
    };

    int g_failures = 0;
    int g_cases = 0;

    void Check(bool ok, const char* name, const std::string& detail = {})
    {
        ++g_cases;
        if (ok) {
            std::printf("PASS %s\n", name);
        } else {
            ++g_failures;
            std::printf("FAIL %s%s%s\n", name, detail.empty() ? "" : ": ", detail.c_str());
        }
    }

    bool Near(float a, float b) { return std::fabs(a - b) <= kEpsilon; }

    bool SameEntry(const CS::Entry& a, const CS::Entry& b)
    {
        return a.formId == b.formId && Near(a.progressPercent, b.progressPercent) && a.unlocked == b.unlocked &&
               a.modded == b.modded && Near(a.xpFromAny, b.xpFromAny) && Near(a.xpFromSchool, b.xpFromSchool) &&
               Near(a.xpFromDirect, b.xpFromDirect) && Near(a.xpFromSelf, b.xpFromSelf);
    }

    // The plugin's record loop: count, then entries until one fails
    struct RecordRead
    {
        std::vector<CS::Entry> entries;
        CS::ReadResult result = CS::ReadResult::Ok;
    };

    RecordRead ReadRecord(const std::vector<std::uint8_t>& bytes, std::uint32_t version)
    {
        RecordRead out;
        ByteReader in{ bytes };
        std::uint32_t count = 0;
        if (in.ReadRecordData(&count, sizeof(count)) != sizeof(count)) {
            out.result = CS::ReadResult::ShortRead;
            return out;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            CS::Entry entry;
            out.result = CS::ReadEntry(in, version, entry);
            if (out.result != CS::ReadResult::Ok) break;
            out.entries.push_back(entry);
        }
        return out;
    }

    // A v1 or v2 entry as those versions wrote it
    void PutOldEntry(ByteWriter& w, std::uint32_t version, std::uint32_t formId, float percent, bool unlocked,
                     const std::vector<std::pair<std::string, float>>& modded)
    {
        w.Put(formId);
        w.Put(percent);
        w.Put(static_cast<std::uint8_t>(unlocked ? 1 : 0));
        if (version >= 2) {
            w.Put(static_cast<std::uint32_t>(modded.size()));
            for (const auto& [name, xp] : modded) {
                w.PutName(name);
                w.Put(xp);
            }
        }
    }

    std::vector<CS::Entry> SampleEntries()
    {
        CS::Entry a;
        a.formId = 0x0001'2FCD;
        a.progressPercent = 0.42f;
        a.modded = { { "passive", 12.5f }, { "isl_study", 50.0f } };
        a.xpFromAny = 5.0f;
        a.xpFromSchool = 15.0f;
        a.xpFromDirect = 30.0f;
        a.xpFromSelf = 7.5f;
        CS::Entry b;  // mastered, nothing tracked
        b.formId = 0xFE00'0801;
        b.progressPercent = 1.0f;
        b.unlocked = true;
        CS::Entry c;  // an empty and a non-ASCII source name
        c.formId = 0x0300'0D62;
        c.progressPercent = 0.0f;
        c.modded = { { "", 1.0f }, { "\xEC\x88\x98\xEB\xA0\xA8", 2.0f } };
        return { a, b, c };
    }

    std::vector<std::uint8_t> WriteRecord(const std::vector<CS::Entry>& entries)
    {
        ByteWriter w;
        w.Put(static_cast<std::uint32_t>(entries.size()));
        for (const auto& e : entries) {
            if (!CS::WriteEntry(w, e)) std::printf("FAIL writer refused an entry\n");
        }
        return w.bytes;
    }

    void CoSaveCases()
    {
        // Normal: v3 round trip
        const auto sample = SampleEntries();
        const auto bytes = WriteRecord(sample);
        const auto read = ReadRecord(bytes, CS::kProgressVersion);
        bool same = read.result == CS::ReadResult::Ok && read.entries.size() == sample.size();
        for (std::size_t i = 0; same && i < sample.size(); ++i) same = SameEntry(read.entries[i], sample[i]);
        Check(same, "v3 round trip keeps percent, unlocked, modded and the four cap trackers");

        // Older saves: v2 and v1 bytes read with the trackers at 0, every entry aligned
        ByteWriter v2;
        v2.Put(std::uint32_t{ 2 });
        PutOldEntry(v2, 2, 0x10, 0.3f, false, { { "book_reading", 4.0f } });
        PutOldEntry(v2, 2, 0x11, 1.0f, true, {});
        const auto r2 = ReadRecord(v2.bytes, 2);
        Check(r2.result == CS::ReadResult::Ok && r2.entries.size() == 2 && r2.entries[0].modded.size() == 1 &&
                  r2.entries[0].modded[0].first == "book_reading" && Near(r2.entries[0].xpFromDirect, 0.0f) &&
                  r2.entries[1].formId == 0x11 && r2.entries[1].unlocked,
              "v2 save reads: modded kept, trackers 0, second entry aligned");

        ByteWriter v1;
        v1.Put(std::uint32_t{ 2 });
        PutOldEntry(v1, 1, 0x20, 0.5f, false, {});
        PutOldEntry(v1, 1, 0x21, 0.75f, false, {});
        const auto r1 = ReadRecord(v1.bytes, 1);
        Check(r1.result == CS::ReadResult::Ok && r1.entries.size() == 2 && r1.entries[1].formId == 0x21 &&
                  Near(r1.entries[1].progressPercent, 0.75f) && r1.entries[1].modded.empty(),
              "v1 save reads: no modded block, second entry aligned");

        // Versions: every written one readable, nothing newer
        Check(!CS::IsReadableProgressVersion(0) && CS::IsReadableProgressVersion(1) &&
                  CS::IsReadableProgressVersion(2) && CS::IsReadableProgressVersion(3) &&
                  !CS::IsReadableProgressVersion(4),
              "versions 1-3 readable, 0 and 4 refused");

        // Failure: cut inside the last entry's trackers - the entries before it stay
        auto cut = bytes;
        cut.resize(cut.size() - 2);
        const auto rc = ReadRecord(cut, CS::kProgressVersion);
        Check(rc.result == CS::ReadResult::ShortRead && rc.entries.size() == 2 && SameEntry(rc.entries[1], sample[1]),
              "record cut short: read stops at the broken entry, earlier entries kept");

        // Failure: a v3 record read as v2 is misaligned - shows why the version must be honoured
        const auto rWrong = ReadRecord(bytes, 2);
        Check(rWrong.result != CS::ReadResult::Ok || rWrong.entries.size() != sample.size() ||
                  !SameEntry(rWrong.entries[1], sample[1]),
              "v3 bytes read as v2 do not come out right (the version decides the layout)");

        // Failure: cut inside a modded name - the record ends there, no misaligned entry after it
        ByteWriter broken;
        broken.Put(std::uint32_t{ 2 });
        broken.Put(std::uint32_t{ 0x30 });
        broken.Put(0.5f);
        broken.Put(std::uint8_t{ 0 });
        broken.Put(std::uint32_t{ 1 });   // one modded source
        broken.Put(std::uint32_t{ 10 });  // a 10-byte name, only 3 bytes follow
        broken.WriteRecordData("abc", 3);
        const auto rb = ReadRecord(broken.bytes, 2);
        Check(rb.result == CS::ReadResult::ShortRead && rb.entries.empty(),
              "short modded name: record aborted, nothing read past it");

        // Edge / failure: limits
        ByteWriter longName;
        longName.Put(std::uint32_t{ 1 });
        PutOldEntry(longName, 1, 0x40, 0.1f, false, {});
        longName.Put(std::uint32_t{ 1 });
        longName.Put(CS::kMaxNameLength + 1);
        const auto rl = ReadRecord(longName.bytes, 2);
        Check(rl.result == CS::ReadResult::NameTooLong, "name over the limit refused");

        ByteWriter many;
        many.Put(std::uint32_t{ 1 });
        PutOldEntry(many, 1, 0x41, 0.1f, false, {});
        many.Put(CS::kMaxModdedSources + 1);
        const auto rm = ReadRecord(many.bytes, 2);
        Check(rm.result == CS::ReadResult::TooManySources, "modded source count over the limit refused");

        const std::vector<std::uint8_t> empty;
        Check(ReadRecord(empty, 3).result == CS::ReadResult::ShortRead, "empty record: short read, nothing kept");

        // SLTX: round trip, empty list, a count the record cannot hold, a short list
        ByteWriter ids;
        const std::vector<std::uint32_t> tomeIds = { 0x0001'2FCD, 0xFE00'0801, 0x0300'0D62 };
        Check(CS::WriteFormIds(ids, tomeIds), "tome XP list written");
        std::vector<std::uint32_t> back;
        ByteReader idsIn{ ids.bytes };
        Check(CS::ReadFormIds(idsIn, static_cast<std::uint32_t>(ids.bytes.size()), back) == CS::ReadResult::Ok &&
                  back == tomeIds,
              "tome XP list round trip");

        ByteWriter none;
        Check(CS::WriteFormIds(none, {}), "empty tome XP list written");
        ByteReader noneIn{ none.bytes };
        Check(CS::ReadFormIds(noneIn, static_cast<std::uint32_t>(none.bytes.size()), back) == CS::ReadResult::Ok &&
                  back.empty(),
              "empty tome XP list reads empty");

        ByteWriter huge;
        huge.Put(std::uint32_t{ 0x4000'0000 });
        ByteReader hugeIn{ huge.bytes };
        Check(CS::ReadFormIds(hugeIn, static_cast<std::uint32_t>(huge.bytes.size()), back) ==
                  CS::ReadResult::CountTooLarge && back.empty(),
              "tome XP count larger than the record refused before allocating");

        auto shortIds = ids.bytes;
        shortIds.resize(shortIds.size() - 4);
        ByteReader shortIn{ shortIds };
        // The record length as saved (whole), the stream as found (cut): two ids kept
        Check(CS::ReadFormIds(shortIn, static_cast<std::uint32_t>(ids.bytes.size()), back) ==
                  CS::ReadResult::ShortRead && back.size() == 2,
              "tome XP list cut short: ids before the cut kept");
    }

    void MathCases()
    {
        // Caps
        Check(Near(PM::GrantWithinCap(10.0f, 50.0f, 45.0f), 5.0f), "cap: grant cut to what is left");
        Check(Near(PM::GrantWithinCap(10.0f, 50.0f, 50.0f), 0.0f), "cap: nothing at the cap");
        Check(Near(PM::GrantWithinCap(10.0f, 50.0f, 60.0f), 0.0f) && Near(PM::GrantWithinCap(-3.0f, 50.0f, 0.0f), 0.0f),
              "cap: over the cap or a negative amount gives 0");

        // Passive: the tier cap holds after the overall multiplier
        const float ask = PM::PreMultiplierGrant(10.0f, 6.0f, 2.0f);
        Check(Near(ask, 3.0f) && Near(ask * 2.0f, 6.0f), "passive: asks 3 so x2 lands on the 6 left");
        Check(Near(PM::PreMultiplierGrant(10.0f, 60.0f, 1.0f), 10.0f), "passive: under the cap asks the full amount");
        Check(Near(PM::PreMultiplierGrant(10.0f, 6.0f, 0.0f), 0.0f) && Near(PM::PreMultiplierGrant(10.0f, 0.0f, 1.0f), 0.0f),
              "passive: no multiplier or no room gives 0");
        // Passive's Master cap of 50% is no longer cut at the modded default of 25%
        const float required = 1500.0f;
        const float masterCapXP = required * 0.50f;
        float tracked = 0.0f;
        for (int hour = 0; hour < 1000; ++hour) {
            tracked += PM::PreMultiplierGrant(5.0f, PM::CapRemaining(masterCapXP, tracked), 1.0f);
        }
        Check(Near(tracked, masterCapXP), "passive: a 50% tier cap fills to 50%, not 25%",
              std::to_string(tracked) + " of " + std::to_string(masterCapXP));

        // SetSpellXP
        Check(Near(PM::ProgressForXP(50.0f, 100.0f), 0.5f), "SetSpellXP: 50 of 100 is 0.5");
        Check(Near(PM::ProgressForXP(200.0f, 100.0f), 1.0f) && Near(PM::ProgressForXP(100.0f, 100.0f), 1.0f),
              "SetSpellXP: 200 of 100 clamps to 1.0");
        Check(Near(PM::ProgressForXP(-5.0f, 100.0f), 0.0f) && Near(PM::ProgressForXP(50.0f, 0.0f), 0.0f),
              "SetSpellXP: negative XP or no required XP gives 0");

        // ISL study: swapping books restarts ISL's count, not the cap
        const float req = 400.0f;
        const float threshold = 0.25f;
        float studied = 0.0f;
        for (int book = 0; book < 4; ++book) {
            for (int hour = 0; hour < 8; ++hour) {
                studied += PM::StudyGrant((1.0f / 8.0f) * req * threshold, req, threshold, studied);
            }
        }
        Check(Near(studied, req * threshold), "study: four books read in full give one threshold share (100 of 400)",
              std::to_string(studied));
        Check(Near(PM::StudyGrant(30.0f, req, threshold, 90.0f), 10.0f), "study: a session is cut at the share");
        Check(Near(PM::StudyGrant(30.0f, req, 0.0f, 0.0f), 0.0f) && Near(PM::StudyGrant(-1.0f, req, threshold, 0.0f), 0.0f),
              "study: no threshold or a negative session gives 0");

        // AddXPNoGrant stops below 1.0
        Check(Near(PM::NoGrantProgress(0.1f, 10.0f, 100.0f), 0.2f), "no-grant: 0.1 + 10 of 100 is 0.2");
        Check(Near(PM::NoGrantProgress(0.9f, 50.0f, 100.0f), PM::kNoGrantMaxProgress),
              "no-grant: stops at kNoGrantMaxProgress, not 1.0");
        Check(Near(PM::NoGrantProgress(1.0f, 10.0f, 100.0f), 1.0f) && Near(PM::NoGrantProgress(0.5f, 10.0f, 0.0f), 0.0f),
              "no-grant: a save already at 1.0 is not lowered; no required XP gives 0");

        // Mastery
        Check(PM::ShouldMaster(0.95f, 1.0f, false, false), "mastery: crossing 1.0 masters");
        Check(PM::ShouldMaster(1.0f, 1.0f, false, true), "mastery: stuck at 1.0, early learned (ISL overflow) masters");
        Check(!PM::ShouldMaster(1.0f, 1.0f, false, false),
              "mastery: stuck at 1.0, never granted (cheat 100%) stays ready for Unlock");
        Check(!PM::ShouldMaster(0.95f, 1.0f, true, true) && !PM::ShouldMaster(0.5f, 0.99f, false, true),
              "mastery: unlocked or below 1.0 does not master");
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: progression-check --self-test\n");
        return kExitUsage;
    }
    CoSaveCases();
    MathCases();
    std::printf("%d of %d cases passed\n", g_cases - g_failures, g_cases);
    return g_failures == 0 ? kExitOk : kExitFail;
}
