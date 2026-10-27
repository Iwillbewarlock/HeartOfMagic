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
#include "RequiredXPTable.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <limits>
#include <string_view>
#include <unordered_map>
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
        bool written = true;
        for (const auto& e : entries) {
            written = CS::WriteEntry(w, e) && written;
        }
        Check(written, "progress entries written");
        return w.bytes;
    }

    void CoSaveCases()
    {
        // Normal: SLPR is written as v2, byte for byte what 1.5.0 writes and reads
        const auto sample = SampleEntries();
        const auto bytes = WriteRecord(sample);
        Check(CS::kProgressVersion == 2, "SLPR written as version 2 (the version 1.5.0 reads)");
        ByteWriter as150;
        as150.Put(static_cast<std::uint32_t>(sample.size()));
        for (const auto& e : sample) PutOldEntry(as150, 2, e.formId, e.progressPercent, e.unlocked, e.modded);
        Check(bytes == as150.bytes, "SLPR bytes identical to 1.5.0's v2 layout (no cap trackers in it)");

        const auto read = ReadRecord(bytes, CS::kProgressVersion);
        bool same = read.result == CS::ReadResult::Ok && read.entries.size() == sample.size();
        for (std::size_t i = 0; same && i < sample.size(); ++i) {
            auto expected = sample[i];
            expected.xpFromAny = expected.xpFromSchool = expected.xpFromDirect = expected.xpFromSelf = 0.0f;
            same = SameEntry(read.entries[i], expected);
        }
        Check(same, "SLPR v2 round trip keeps percent, unlocked and modded; trackers come from SLPX");

        // A development build's v3 (trackers appended) is still read
        ByteWriter v3;
        v3.Put(std::uint32_t{ 2 });
        for (const auto& e : { sample[0], sample[1] }) {
            PutOldEntry(v3, 2, e.formId, e.progressPercent, e.unlocked, e.modded);
            v3.Put(e.xpFromAny);
            v3.Put(e.xpFromSchool);
            v3.Put(e.xpFromDirect);
            v3.Put(e.xpFromSelf);
        }
        const auto r3 = ReadRecord(v3.bytes, 3);
        Check(r3.result == CS::ReadResult::Ok && r3.entries.size() == 2 && SameEntry(r3.entries[0], sample[0]) &&
                  SameEntry(r3.entries[1], sample[1]),
              "dev-build SLPR v3 reads with its trackers, second entry aligned");

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

        // Failure: cut inside the last entry - the entries before it stay
        auto cut = bytes;
        cut.resize(cut.size() - 2);
        const auto rc = ReadRecord(cut, CS::kProgressVersion);
        Check(rc.result == CS::ReadResult::ShortRead && rc.entries.size() == 2 &&
                  rc.entries[1].formId == sample[1].formId && rc.entries[1].unlocked,
              "record cut short: read stops at the broken entry, earlier entries kept");

        // Failure: a v3 record read as v2 is misaligned - shows why the version must be honoured
        const auto rWrong = ReadRecord(v3.bytes, 2);
        Check(rWrong.result != CS::ReadResult::Ok || rWrong.entries.size() != 2 ||
                  !SameEntry(rWrong.entries[1], sample[1]),
              "v3 bytes read as v2 do not come out right (the version decides the layout)");

        // SLPX: round trip, applied onto the SLPR entries
        const std::vector<CS::CapEntry> caps = {
            { 0x0001'2FCD, 5.0f, 15.0f, 30.0f, 7.5f },
            { 0x0BAD'0001, 1.0f, 2.0f, 3.0f, 4.0f },  // a spell with no SLPR entry
        };
        ByteWriter px;
        Check(CS::WriteCapEntries(px, caps) && px.bytes.size() == 4 + caps.size() * CS::kCapEntrySize,
              "SLPX written: count + 20 bytes per spell");
        std::vector<CS::CapEntry> capsBack;
        ByteReader pxIn{ px.bytes };
        const bool pxOk = CS::ReadCapEntries(pxIn, static_cast<std::uint32_t>(px.bytes.size()), capsBack) ==
                          CS::ReadResult::Ok;
        Check(pxOk && capsBack.size() == 2 && capsBack[0].formId == caps[0].formId &&
                  Near(capsBack[0].xpFromDirect, 30.0f) && Near(capsBack[1].xpFromSelf, 4.0f),
              "SLPX round trip");

        std::unordered_map<std::uint32_t, CS::Entry> loaded;
        for (const auto& e : read.entries) loaded.emplace(e.formId, e);
        const auto applied = CS::ApplyCapEntries(capsBack, loaded);
        const auto& a = loaded.at(0x0001'2FCD);
        Check(applied == 1 && Near(a.xpFromAny, 5.0f) && Near(a.xpFromSchool, 15.0f) && Near(a.xpFromDirect, 30.0f) &&
                  Near(a.xpFromSelf, 7.5f),
              "SLPX applied onto the spell's SLPR entry");
        Check(loaded.size() == read.entries.size() && !loaded.contains(0x0BAD'0001),
              "SLPX for a spell with no SLPR entry makes no entry");

        // Old save (1.5.0): no SLPX at all - the entries keep their counters at 0
        std::unordered_map<std::uint32_t, CS::Entry> old;
        for (const auto& e : read.entries) old.emplace(e.formId, e);
        Check(CS::ApplyCapEntries({}, old) == 0 && Near(old.at(0x0001'2FCD).xpFromDirect, 0.0f),
              "save without SLPX: progress loads, cap trackers 0");

        // Failure: SLPX cut short keeps whole entries; a count the record cannot hold is refused
        auto pxCut = px.bytes;
        pxCut.resize(pxCut.size() - 3);
        ByteReader pxCutIn{ pxCut };
        Check(CS::ReadCapEntries(pxCutIn, static_cast<std::uint32_t>(px.bytes.size()), capsBack) ==
                      CS::ReadResult::ShortRead &&
                  capsBack.size() == 1 && capsBack[0].formId == caps[0].formId,
              "SLPX cut short: the whole entry before the cut kept");
        ByteReader pxLenIn{ px.bytes };
        Check(CS::ReadCapEntries(pxLenIn, static_cast<std::uint32_t>(px.bytes.size()) - 1, capsBack) ==
                  CS::ReadResult::CountTooLarge,
              "SLPX count larger than the record refused");
        Check(CS::CapEntry{ 1, 0.0f, 0.0f, 0.0f, 0.0f }.IsEmpty() && !caps[0].IsEmpty(),
              "SLPX skips spells with nothing counted");

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

        // Oversized names and too many sources are never written: the rest round-trips
        Check(CS::IsWritableSourceName(std::string(CS::kMaxNameLength, 'x')) &&
                  !CS::IsWritableSourceName(std::string(CS::kMaxNameLength + 1, 'x')),
              "source name limit: kMaxNameLength bytes writable, one more not");
        CS::Entry big;
        big.formId = 0x50;
        big.progressPercent = 0.3f;
        big.modded = { { "ok", 1.0f }, { std::string(CS::kMaxNameLength + 1, 'x'), 2.0f }, { "also", 3.0f } };
        CS::Entry after;
        after.formId = 0x51;
        after.progressPercent = 0.6f;
        after.modded = { { "book_reading", 4.0f } };
        ByteWriter bigW;
        bigW.Put(std::uint32_t{ 2 });
        std::uint32_t skippedBig = 0;
        const bool bigOk = CS::WriteEntry(bigW, big, &skippedBig) && CS::WriteEntry(bigW, after);
        const auto rBig = ReadRecord(bigW.bytes, CS::kProgressVersion);
        Check(bigOk && skippedBig == 1 && rBig.result == CS::ReadResult::Ok && rBig.entries.size() == 2 &&
                  rBig.entries[0].modded.size() == 2 && rBig.entries[0].modded[1].first == "also" &&
                  rBig.entries[1].formId == 0x51 && rBig.entries[1].modded.size() == 1,
              "oversized source name left out on write; that entry and the next load");

        CS::Entry crowded;
        crowded.formId = 0x52;
        for (std::uint32_t i = 0; i <= CS::kMaxModdedSources; ++i) crowded.modded.emplace_back(std::to_string(i), 1.0f);
        ByteWriter crowdW;
        crowdW.Put(std::uint32_t{ 2 });
        std::uint32_t skippedCrowd = 0;
        const bool crowdOk = CS::WriteEntry(crowdW, crowded, &skippedCrowd) && CS::WriteEntry(crowdW, after);
        const auto rCrowd = ReadRecord(crowdW.bytes, CS::kProgressVersion);
        Check(crowdOk && skippedCrowd == 1 && rCrowd.result == CS::ReadResult::Ok && rCrowd.entries.size() == 2 &&
                  rCrowd.entries[0].modded.size() == CS::kMaxModdedSources && rCrowd.entries[1].formId == 0x51,
              "sources past kMaxModdedSources left out on write; the next entry loads");

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

        // SLEL v2: the early list, then the held-for-ISL list, read with the
        // remaining length (SpellEffectivenessHook::ReadRecord)
        ByteWriter slel;
        const std::vector<std::uint32_t> early = { 0x0001'2FCD, 0x0300'0D62 };
        const std::vector<std::uint32_t> held = { 0x0300'0D62 };
        Check(CS::WriteFormIds(slel, early) && CS::WriteFormIds(slel, held), "SLEL v2 written as two lists");
        ByteReader slelIn{ slel.bytes };
        std::vector<std::uint32_t> earlyBack;
        std::vector<std::uint32_t> heldBack;
        const auto slelLength = static_cast<std::uint32_t>(slel.bytes.size());
        const bool earlyRead = CS::ReadFormIds(slelIn, slelLength, earlyBack) == CS::ReadResult::Ok;
        const auto used = static_cast<std::uint32_t>(4 * (earlyBack.size() + 1));
        Check(earlyRead && CS::ReadFormIds(slelIn, slelLength - used, heldBack) == CS::ReadResult::Ok &&
                  earlyBack == early && heldBack == held,
              "SLEL v2 round trip: early and held lists");
        ByteReader slelV1In{ ids.bytes };
        Check(CS::ReadFormIds(slelV1In, static_cast<std::uint32_t>(ids.bytes.size()), earlyBack) ==
                  CS::ReadResult::Ok && earlyBack == tomeIds && slelV1In.at == ids.bytes.size(),
              "SLEL v1 (one list) reads whole, nothing left for a held list");
        ByteWriter cutId;
        cutId.Put(std::uint32_t{ 2 });
        cutId.Put(std::uint32_t{ 0x0001'2FCD });
        cutId.Put(std::uint16_t{ 0x0D62 });  // a form id cut to 2 of its 4 bytes
        ByteReader cutIdIn{ cutId.bytes };
        Check(CS::ReadFormIds(cutIdIn, 12, earlyBack) == CS::ReadResult::ShortRead && earlyBack.size() == 1 &&
                  earlyBack[0] == 0x0001'2FCD,
              "a form id cut short is a failure, not another spell's id");

        // SLTR: round trip, empty name, cut-short id, a count the record cannot hold, a long name
        ByteWriter sltr;
        const std::vector<CS::TargetEntry> targets = { { "Destruction", 0x0001'2FCD }, { "", 0xFE00'0801 } };
        Check(CS::WriteTargets(sltr, targets), "learning targets written");
        std::vector<CS::TargetEntry> targetsBack;
        ByteReader sltrIn{ sltr.bytes };
        Check(CS::ReadTargets(sltrIn, static_cast<std::uint32_t>(sltr.bytes.size()), targetsBack) ==
                      CS::ReadResult::Ok && targetsBack.size() == 2 && targetsBack[0].school == "Destruction" &&
                  targetsBack[0].formId == 0x0001'2FCD && targetsBack[1].school.empty() &&
                  targetsBack[1].formId == 0xFE00'0801,
              "learning targets round trip (an empty school name too)");
        auto sltrCut = sltr.bytes;
        sltrCut.resize(sltrCut.size() - 1);  // the last form id 3 of 4 bytes
        ByteReader sltrCutIn{ sltrCut };
        Check(CS::ReadTargets(sltrCutIn, static_cast<std::uint32_t>(sltr.bytes.size()), targetsBack) ==
                      CS::ReadResult::ShortRead && targetsBack.size() == 1 && targetsBack[0].formId == 0x0001'2FCD,
              "learning target with a cut-short form id refused, the whole one before it kept");
        ByteWriter sltrHuge;
        sltrHuge.Put(std::uint32_t{ 0x1000'0000 });
        ByteReader sltrHugeIn{ sltrHuge.bytes };
        Check(CS::ReadTargets(sltrHugeIn, 4, targetsBack) == CS::ReadResult::CountTooLarge && targetsBack.empty(),
              "learning target count larger than the record refused");
        ByteWriter sltrLong;
        sltrLong.Put(std::uint32_t{ 1 });
        sltrLong.Put(std::uint32_t{ CS::kMaxNameLength + 1 });
        ByteReader sltrLongIn{ sltrLong.bytes };
        Check(CS::ReadTargets(sltrLongIn, 1 << 20, targetsBack) == CS::ReadResult::NameTooLong,
              "learning target school name over the limit refused");
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
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(Near(PM::ProgressForXP(nan, 100.0f), 0.0f) && Near(PM::ProgressForXP(50.0f, nan), 0.0f) &&
                  Near(PM::ProgressForXP(std::numeric_limits<float>::infinity(), 100.0f), 1.0f),
              "SetSpellXP: NaN gives 0 (not a NaN progress), infinity clamps to 1");
        Check(Near(PM::GrantWithinCap(nan, 50.0f, 0.0f), 0.0f) && Near(PM::PreMultiplierGrant(nan, 6.0f, 1.0f), 0.0f) &&
                  Near(PM::StudyGrant(nan, 400.0f, 0.25f, 0.0f), 0.0f),
              "caps: a NaN amount grants 0");

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
        // ShouldMaster(old, new, unlocked, earlyLearned, playerHasSpell)
        Check(PM::ShouldMaster(0.95f, 1.0f, false, true, true), "mastery: weakened spell crossing 1.0 masters");
        Check(PM::ShouldMaster(0.95f, 1.0f, false, false, false),
              "mastery: crossing 1.0 for a spell not early learned (a copy known) masters as before");
        Check(!PM::ShouldMaster(0.95f, 1.0f, false, true, false),
              "mastery: crossing 1.0 while held for ISL and not taught stays ready");
        Check(PM::ShouldMaster(1.0f, 1.0f, false, true, true),
              "mastery: stuck at 1.0, early learned and taught (ISL overflow) masters");
        Check(!PM::ShouldMaster(1.0f, 1.0f, false, true, false),
              "mastery: stuck at 1.0, held for ISL and not taught, waits");
        Check(!PM::ShouldMaster(1.0f, 1.0f, false, false, false),
              "mastery: stuck at 1.0, never granted (cheat 100%), stays ready for Unlock");
        Check(PM::ShouldMaster(0.95f, 1.0f, true, false, true),
              "mastery: an unlocked spell lowered by cheat mode masters again when it crosses 1.0");
        Check(!PM::ShouldMaster(1.0f, 1.0f, true, true, true) && !PM::ShouldMaster(0.5f, 0.99f, false, true, true),
              "mastery: already unlocked at 1.0, or below 1.0, does not master");

        // Passive learning ticks in game hours (Calendar::GetHoursPassed)
        const auto hour = PM::PassiveTickFor(100.0f, 101.0f);
        Check(Near(hour.grantHours, 1.0f) && Near(hour.lastHours, 101.0f),
              "passive: one game hour later grants one hour (5 XP/h gives 5, not 5/24)");
        const auto small = PM::PassiveTickFor(100.0f, 100.05f);
        Check(Near(small.grantHours, 0.0f) && Near(small.lastHours, 100.0f),
              "passive: under 0.1 h grants nothing and keeps the last time (short ticks add up)");
        const auto atFloor = PM::PassiveTickFor(100.0f, 100.0f + PM::kPassiveMinHours);
        Check(Near(atFloor.grantHours, 0.0f), "passive: exactly 0.1 h is not past the floor");
        const auto back = PM::PassiveTickFor(100.0f, 50.0f);
        Check(Near(back.grantHours, 0.0f) && Near(back.lastHours, 50.0f),
              "passive: time going back grants nothing and starts from now");
        const auto nanTick = PM::PassiveTickFor(100.0f, nan);
        Check(Near(nanTick.grantHours, 0.0f), "passive: a NaN time grants nothing");

        // AddXP's progress; a required XP not set leaves the progress alone
        Check(Near(PM::ProgressAfterXP(0.2f, 10.0f, 100.0f), 0.3f), "AddXP: 0.2 + 10 of 100 is 0.3");
        Check(Near(PM::ProgressAfterXP(0.95f, 50.0f, 100.0f), 1.0f), "AddXP: stops at 1");
        Check(Near(PM::ProgressAfterXP(0.4f, 10.0f, 0.0f), 0.4f) && Near(PM::ProgressAfterXP(0.4f, 10.0f, -5.0f), 0.4f) &&
                  Near(PM::ProgressAfterXP(0.4f, 10.0f, nan), 0.4f),
              "AddXP: required XP 0, negative or NaN keeps the progress (was wiped to 0)");
    }

    // Required XP by kind (RequiredXPTable.h): what a revert keeps
    void RequiredXPCases()
    {
        using RequiredXP::Kind;
        RequiredXP::Table table;
        constexpr std::uint32_t kSpell = 0x0001'2FCD;
        constexpr float kTier = 100.0f;
        table.Set(Kind::kTree, kSpell, 150.0f);
        table.SetOverrides({ { kSpell, 400.0f } });
        Check(Near(table.For(kSpell, kTier, false, 1.0f), 400.0f), "required XP: the settings' override wins over the tree");
        table.Set(Kind::kCharacter, kSpell, 120.0f);
        Check(Near(table.For(kSpell, kTier, false, 1.0f), 120.0f), "required XP: this character's number wins");
        table.OnRevert();   // a reload
        Check(Near(table.For(kSpell, kTier, false, 1.0f), 400.0f),
              "required XP: after a reload the override still applies (only the character's number went)");
        Check(Near(table.For(kSpell, kTier, true, 0.3f), 120.0f),
              "required XP: the reverse unlock share on the override, as the panel works it out");
        // The panel's Math.max(1, Math.round(base * share))
        Check(Near(RequiredXP::Table::Shared(2.0f, 0.3f), 1.0f), "required XP: 2 at 30% is 1 (not 0.6)");
        Check(Near(RequiredXP::Table::Shared(1.0f, 0.4f), 1.0f) && Near(RequiredXP::Table::Shared(1.0f, 0.1f), 1.0f),
              "required XP: a share under 1 XP is 1 at least (0.4, 0.1)");
        Check(Near(RequiredXP::Table::Shared(5.0f, 0.3f), 2.0f) && Near(RequiredXP::Table::Shared(5.0f, 0.5f), 3.0f) &&
                  Near(RequiredXP::Table::Shared(333.0f, 0.3f), 100.0f),
              "required XP: rounded to whole XP as Math.round (1.5 -> 2, 2.5 -> 3, 99.9 -> 100)");
        table.SetOverrides({ { 0x0400'0001, 2.0f } });
        Check(Near(table.For(0x0400'0001, kTier, true, 0.3f), 1.0f) && Near(table.For(0x0400'0002, 7.0f, true, 0.3f), 2.0f),
              "required XP: the rounded share on an override and on the tier");
        table.SetOverrides({ { kSpell, 400.0f } });
        table.SetOverrides({});
        Check(Near(table.For(kSpell, kTier, false, 1.0f), 150.0f), "required XP: override removed - the tree's number");
        Check(Near(table.For(0x0300'0D62, kTier, false, 1.0f), kTier), "required XP: nothing known - the tier's");
        table.Set(Kind::kCharacter, 0x0300'0D62, -5.0f);
        Check(Near(table.For(0x0300'0D62, kTier, false, 1.0f), kTier) && !table.HasCharacterValue(0x0300'0D62),
              "required XP: a non-positive number counts as none");
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
    RequiredXPCases();
    std::printf("%d of %d cases passed\n", g_cases - g_failures, g_cases);
    return g_failures == 0 ? kExitOk : kExitFail;
}
