#include "tomereach/TomeReachRecords.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace TomeReach
{
    // =============================================================================
    // NAMES
    // =============================================================================

    std::string LowerAscii(std::string_view text)
    {
        std::string out(text);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(c < 0x80 ? std::tolower(c) : c);
        });
        return out;
    }

    std::uint32_t NameTable::Id(std::string_view fileName)
    {
        auto lower = LowerAscii(fileName);
        if (auto it = m_ids.find(lower); it != m_ids.end()) return it->second;
        const auto id = static_cast<std::uint32_t>(m_names.size());
        const bool eslName = lower.size() > 4 && lower.ends_with(".esl");
        m_ids.emplace(lower, id);
        m_names.push_back(std::move(lower));
        m_light.push_back(eslName);
        return id;
    }

    void NameTable::SetLight(std::uint32_t id, bool light)
    {
        if (id < m_light.size()) m_light[id] = m_light[id] || light;
    }

    bool NameTable::IsLight(std::uint32_t id) const
    {
        return id < m_light.size() && m_light[id];
    }

    // =============================================================================
    // RECORDS
    // =============================================================================

    namespace
    {
        constexpr std::size_t kRecordHeader = 24;
        constexpr std::size_t kSubHeader = 6;
        constexpr std::uint32_t kCompressed = 0x00040000;
        constexpr std::uint32_t kLightFile = 0x00000200;
        constexpr std::uint8_t kTeachesSpell = 0x04;
        // No record inflates past this: a larger stated size means a damaged file
        constexpr uLongf kMaxInflatedRecord = 64u * 1024u * 1024u;
        constexpr std::size_t kInflatedSizeField = 4;      // a compressed record starts with its full size
        constexpr std::size_t kFormIdSize = 4;
        constexpr std::size_t kBigSizeField = 4;           // XXXX: the next subrecord's real size
        constexpr std::size_t kLevelEntryFormOffset = 4;   // LVLO: level, pad, form, count
        constexpr std::size_t kBookSpellOffset = 4;        // BOOK DATA: flags, type, pad, spell

        using Tag = std::array<char, 4>;

        constexpr Tag T(const char (&s)[5]) { return { s[0], s[1], s[2], s[3] }; }

        bool Is(const unsigned char* p, const Tag& tag) { return std::memcmp(p, tag.data(), 4) == 0; }

        std::uint32_t U32(const unsigned char* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        std::uint16_t U16(const unsigned char* p)
        {
            std::uint16_t v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        // The record types whose items or script properties can hand out a tome
        constexpr std::array kScanned = {
            T("BOOK"), T("REFR"), T("ACHR"), T("LVLI"), T("CONT"), T("NPC_"), T("FLST"), T("COBJ"),
            T("QUST"), T("INFO"), T("ACTI"), T("PERK"), T("SCEN"), T("PACK"), T("MGEF"), T("SPEL"),
            T("MISC"), T("ALCH"), T("ARMO"), T("WEAP"), T("FURN"), T("DOOR"), T("FLOR"), T("TACT"),
            T("LIGH"), T("ENCH"), T("KEYM"), T("AMMO"), T("SCRL"), T("INGR"), T("SLGM"),
        };

        bool Scanned(const unsigned char* type)
        {
            return std::any_of(kScanned.begin(), kScanned.end(), [&](const Tag& t) { return Is(type, t); });
        }

        struct Subrecord
        {
            const unsigned char* type;
            const unsigned char* data;
            std::size_t size;
        };

        // Calls fn for each subrecord; false when the last one runs past `size`
        template <class Fn>
        bool ForEachSubrecord(const unsigned char* data, std::size_t size, Fn&& fn)
        {
            std::size_t i = 0;
            std::size_t bigSize = 0;
            while (i + kSubHeader <= size) {
                const unsigned char* type = data + i;
                std::size_t length = U16(data + i + 4);
                i += kSubHeader;
                if (Is(type, T("XXXX")) && length >= kBigSizeField && i + kBigSizeField <= size) {
                    bigSize = U32(data + i);
                    i += length;
                    continue;
                }
                if (bigSize) {
                    length = bigSize;
                    bigSize = 0;
                }
                if (i + length > size) return false;
                fn(Subrecord{ type, data + i, length });
                i += length;
            }
            return i == size;
        }

        // A plugin's view of form ids: the high byte picks a master, or the plugin itself
        struct Resolver
        {
            NameTable& names;
            std::vector<std::uint32_t> masters;
            std::uint32_t self = 0;

            Key operator()(std::uint32_t formId) const
            {
                const std::uint32_t index = formId >> kFileIndexShift;
                const std::uint32_t plugin = index < masters.size() ? masters[index] : self;
                const std::uint32_t mask = names.IsLight(plugin) ? kLightLocalMask : kLocalMask;
                return MakeKey(plugin, formId & mask);
            }
        };

        struct Collector
        {
            const Resolver& resolve;
            const std::unordered_set<Key>& wanted;
            Evidence& evidence;

            void Add(std::uint32_t formId, bool loose = false) const
            {
                if (!formId) return;
                const Key key = resolve(formId);
                if (!wanted.contains(key)) return;
                if (!loose) {
                    evidence.found.insert(key);
                } else if (evidence.foundLoose.insert(key).second) {
                    evidence.looseFrom.emplace(key, resolve.self);
                }
            }

            // A script property or quest alias: any four bytes may be a form id (a
            // false hit can only keep a tome, never drop one)
            void AddLoose(const unsigned char* data, std::size_t size) const
            {
                for (std::size_t i = 0; i + kFormIdSize <= size; ++i) Add(U32(data + i), true);
            }
        };

        // False when a subrecord runs past the record: the rest of it was not read
        bool ReadRecord(const unsigned char* type, const unsigned char* body, std::size_t size,
            std::uint32_t formId, const Collector& collect, std::vector<BookRecord>* books)
        {
            const bool quest = Is(type, T("QUST"));
            BookRecord book;
            const bool isBook = books && Is(type, T("BOOK"));
            const bool whole = ForEachSubrecord(body, size, [&](const Subrecord& sub) {
                if (Is(type, T("REFR")) && Is(sub.type, T("NAME")) && sub.size >= 4) {
                    collect.Add(U32(sub.data));
                } else if (Is(type, T("LVLI")) && Is(sub.type, T("LVLO")) && sub.size >= kLevelEntryFormOffset + 4) {
                    collect.Add(U32(sub.data + kLevelEntryFormOffset));
                } else if (Is(sub.type, T("CNTO")) && sub.size >= 4) {
                    collect.Add(U32(sub.data));
                } else if (Is(type, T("FLST")) && Is(sub.type, T("LNAM")) && sub.size >= 4) {
                    collect.Add(U32(sub.data));
                } else if (Is(type, T("COBJ")) && Is(sub.type, T("CNAM")) && sub.size >= 4) {
                    collect.Add(U32(sub.data));
                } else if (Is(sub.type, T("VMAD")) ||
                           (quest && (Is(sub.type, T("ALCO")) || Is(sub.type, T("ALFR")) || Is(sub.type, T("ALUA"))))) {
                    collect.AddLoose(sub.data, sub.size);
                }
                if (isBook) {
                    if (Is(sub.type, T("EDID"))) {
                        book.editorId.assign(reinterpret_cast<const char*>(sub.data), strnlen(
                            reinterpret_cast<const char*>(sub.data), sub.size));
                    } else if (Is(sub.type, T("DATA")) && sub.size >= kBookSpellOffset + 4 &&
                               (sub.data[0] & kTeachesSpell)) {
                        book.spell = collect.resolve(U32(sub.data + kBookSpellOffset));
                    }
                }
            });
            if (isBook) {
                book.book = collect.resolve(formId);
                books->push_back(std::move(book));
            }
            return whole;
        }

        bool ReadFile(const std::filesystem::path& path, std::vector<unsigned char>& out)
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in) return false;
            const auto size = static_cast<std::size_t>(in.tellg());
            out.resize(size);
            in.seekg(0);
            return size == 0 || static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()),
                                    static_cast<std::streamsize>(size)));
        }
    }

    PluginScan ScanPlugin(const PluginFile& file, NameTable& names, const std::unordered_set<Key>& wanted,
        Evidence& evidence, std::vector<BookRecord>* books)
    {
        PluginScan result;
        std::vector<unsigned char> data;
        if (!ReadFile(file.path, data) && (file.fallback.empty() || !ReadFile(file.fallback, data))) return result;
        if (data.size() < kRecordHeader || !Is(data.data(), T("TES4"))) return result;

        Resolver resolve{ names, {}, names.Id(file.name) };
        const std::uint32_t headerSize = U32(data.data() + 4);
        const std::uint32_t headerFlags = U32(data.data() + 8);
        if (kRecordHeader + headerSize > data.size()) return result;
        names.SetLight(resolve.self, (headerFlags & kLightFile) != 0);
        // A header read in part leaves the master list, and so every form id, in doubt
        const bool headerWhole = ForEachSubrecord(data.data() + kRecordHeader, headerSize, [&](const Subrecord& sub) {
            if (Is(sub.type, T("MAST"))) {
                resolve.masters.push_back(names.Id(std::string_view(
                    reinterpret_cast<const char*>(sub.data), strnlen(reinterpret_cast<const char*>(sub.data), sub.size))));
            }
        });
        if (!headerWhole) return result;

        const auto damaged = [&] {
            result.status = ScanStatus::kDamaged;
            result.scope = resolve.masters;
            result.scope.push_back(resolve.self);
            return result;
        };
        const Collector collect{ resolve, wanted, evidence };
        std::vector<unsigned char> inflated;
        std::size_t pos = kRecordHeader + headerSize;
        while (pos + kRecordHeader <= data.size()) {
            const unsigned char* header = data.data() + pos;
            if (Is(header, T("GRUP"))) {
                pos += kRecordHeader;  // step into the group; its records follow
                continue;
            }
            const std::uint32_t size = U32(header + 4);
            const std::uint32_t flags = U32(header + 8);
            const std::uint32_t formId = U32(header + 12);
            const unsigned char* body = header + kRecordHeader;
            pos += kRecordHeader + size;
            if (pos > data.size()) return damaged();  // a record runs past the end
            if (!Scanned(header)) continue;

            std::size_t bodySize = size;
            if (flags & kCompressed) {
                if (size < kInflatedSizeField) return damaged();
                uLongf length = U32(body);
                if (length > kMaxInflatedRecord) return damaged();
                inflated.resize(length);
                if (uncompress(inflated.data(), &length, body + kInflatedSizeField, size - kInflatedSizeField) != Z_OK) {
                    return damaged();
                }
                body = inflated.data();
                bodySize = length;
            }
            if (!ReadRecord(header, body, bodySize, formId, collect, books)) return damaged();
        }
        if (pos != data.size()) return damaged();  // the file ends inside a record header
        result.status = ScanStatus::kRead;
        return result;
    }
}
