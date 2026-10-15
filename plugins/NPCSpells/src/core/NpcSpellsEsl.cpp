#include "core/NpcSpellsEsl.h"
#include <cstring>
namespace NpcSpells {
namespace {
    using Bytes = std::vector<unsigned char>;
    constexpr std::uint32_t kGroupHeaderSize = 24;
    constexpr std::size_t kObndSize = 12;       // OBND: six int16 bounds, all zero for a leveled list
    constexpr std::size_t kTes4DataSize = 8;    // TES4 DATA after each MAST: uint64 master size, 0
    void Put16(Bytes& b, std::uint16_t v) { b.push_back(static_cast<unsigned char>(v)); b.push_back(static_cast<unsigned char>(v >> 8)); }
    void Put32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(v >> (8 * i))); }
    void PutTag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }
    void Join(Bytes& into, const Bytes& more) { into.insert(into.end(), more.begin(), more.end()); }
    Bytes Sub(const char* t, const Bytes& data) { Bytes b; PutTag(b, t); Put16(b, static_cast<std::uint16_t>(data.size())); Join(b, data); return b; }
    Bytes Str(std::string_view s) { Bytes b(s.begin(), s.end()); b.push_back(0); return b; }
    Bytes Record(const char* type, std::uint32_t flags, std::uint32_t formId, const Bytes& body)
    { Bytes b; PutTag(b, type); Put32(b, static_cast<std::uint32_t>(body.size())); Put32(b, flags); Put32(b, formId); Put32(b, 0); Put16(b, kFormVersion); Put16(b, 0); Join(b, body); return b; }
}
std::vector<unsigned char> BuildEslBytes(const std::vector<SlotDef>& slots)
{
    const std::uint32_t records = static_cast<std::uint32_t>(slots.size()) * kMaxChunks;
    // numRecords = records + groups (TES4 convention; xEdit flags a mismatch, the game does not read it)
    Bytes hedr; { float v = kHeaderVersion; unsigned char raw[sizeof v]; std::memcpy(raw, &v, sizeof v); hedr.insert(hedr.end(), raw, raw + sizeof v); Put32(hedr, records + kTopGroupCount); Put32(hedr, kFirstLocalFormId + records); }
    Bytes header; Join(header, Sub("HEDR", hedr)); Join(header, Sub("CNAM", Str("HeartOfMagic"))); Join(header, Sub("MAST", Str(kEslMaster))); { Bytes zero(kTes4DataSize, 0); Join(header, Sub("DATA", zero)); }
    Bytes out = Record("TES4", kTes4LightFlag, 0, header);
    Bytes group;
    for (std::size_t i = 0; i < slots.size(); ++i)
        for (std::uint8_t c = 0; c < kMaxChunks; ++c) {
            Bytes body; Join(body, Sub("EDID", Str(EditorIdFor(slots[i].key, c))));
            { Bytes obnd(kObndSize, 0); Join(body, Sub("OBND", obnd)); }
            { Bytes one(1, 0); Join(body, Sub("LVLD", one)); Join(body, Sub("LVLF", one)); Join(body, Sub("LLCT", one)); }
            Join(group, Record("LVSP", 0, kOwnModIndexInFile | LocalFormId(i, c), body));
        }
    PutTag(out, "GRUP"); Put32(out, kGroupHeaderSize + static_cast<std::uint32_t>(group.size())); PutTag(out, "LVSP"); Put32(out, 0); Put32(out, 0); Put32(out, 0); Join(out, group);
    return out;
}
}
