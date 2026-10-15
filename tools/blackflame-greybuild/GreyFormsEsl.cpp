// =============================================================================
// GreyFormsEsl - the copies ESL: placeholder PROJ, HAZD, EXPL and IPCT records
// =============================================================================
// DATA layouts as Skyrim.esm stores them (FormIDs 4 bytes; the CommonLibSSE-NG memory layouts
// RE/B/BGSProjectile.h, BGSHazard.h, BGSExplosion.h, BGSImpactData.h with pointers as FormIDs):
//   PROJ 92: flags u16 0, type u16 2, gravity 4, speed 8, range C, light 10, muzzle light 14, tracer chance 18,
//            proximity 1C, timer 20, explosion 24, sound 28, muzzle flash duration 2C, fade 30, impact force 34,
//            countdown sound 38, disable sound 3C, default weapon 40, cone spread 44, collision radius 48,
//            lifetime 4C, relaunch interval 50, decal TXST 54, collision layer 58. Sound level: VNAM u32.
//   HAZD 40: limit u32 0, radius 4, lifetime 8, IS radius C, target interval 10, flags u32 14, spell 18,
//            light 1C, impact data set 20, sound 24.
//   EXPL 52: light 0, sound1 4, sound2 8, impact data set C, placed object 10, spawn projectile 14, force 18,
//            damage 1C, radius 20, IS radius 24, vertical offset mult 28, flags u32 2C, sound level u32 30.
//   IPCT 24: effect duration 0, orientation u32 4, angle threshold 8, placement radius C, sound level u32 10,
//            flags u8 14, result override u8 15, unused u16 16.
// Every form link is written 0 (no masters): the DLL copies the live source's values and links.

#include "GreyEslIo.h"
#include "GreyForms.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace GreyBuild
{
    namespace
    {
        using namespace EslIo;

        enum class Kind
        {
            kU8,
            kU16,
            kU32,
            kFloat
        };

        struct Field
        {
            const char* key;
            std::size_t offset;
            Kind kind;
        };

        constexpr std::size_t kProjectileData = 92;
        constexpr std::size_t kHazardData = 40;
        constexpr std::size_t kExplosionData = 52;
        constexpr std::size_t kImpactData = 24;
        constexpr const char* kSoundLevelKey = "soundLevel";  // PROJ: VNAM, the others: inside DATA

        const std::vector<Field>& Fields(CopyType a_type)
        {
            static const std::vector<Field> kProjectile{ { "flags", 0x00, Kind::kU16 },
                { "projectileType", 0x02, Kind::kU16 }, { "gravity", 0x04, Kind::kFloat }, { "speed", 0x08, Kind::kFloat },
                { "range", 0x0C, Kind::kFloat }, { "tracerChance", 0x18, Kind::kFloat },
                { "explosionProximity", 0x1C, Kind::kFloat }, { "explosionTimer", 0x20, Kind::kFloat },
                { "muzzleFlashDuration", 0x2C, Kind::kFloat }, { "fadeDuration", 0x30, Kind::kFloat },
                { "impactForce", 0x34, Kind::kFloat }, { "coneSpread", 0x44, Kind::kFloat },
                { "collisionRadius", 0x48, Kind::kFloat }, { "lifetime", 0x4C, Kind::kFloat },
                { "relaunchInterval", 0x50, Kind::kFloat } };
            static const std::vector<Field> kHazard{ { "limit", 0x00, Kind::kU32 }, { "radius", 0x04, Kind::kFloat },
                { "lifetime", 0x08, Kind::kFloat }, { "imageSpaceRadius", 0x0C, Kind::kFloat },
                { "targetInterval", 0x10, Kind::kFloat }, { "flags", 0x14, Kind::kU32 } };
            static const std::vector<Field> kExplosion{ { "force", 0x18, Kind::kFloat }, { "damage", 0x1C, Kind::kFloat },
                { "radius", 0x20, Kind::kFloat }, { "imageSpaceRadius", 0x24, Kind::kFloat },
                { "verticalOffsetMult", 0x28, Kind::kFloat }, { "flags", 0x2C, Kind::kU32 },
                { "soundLevel", 0x30, Kind::kU32 } };
            static const std::vector<Field> kImpact{ { "effectDuration", 0x00, Kind::kFloat },
                { "orientation", 0x04, Kind::kU32 }, { "angleThreshold", 0x08, Kind::kFloat },
                { "placementRadius", 0x0C, Kind::kFloat }, { "soundLevel", 0x10, Kind::kU32 }, { "flags", 0x14, Kind::kU8 },
                { "resultOverride", 0x15, Kind::kU8 } };
            switch (a_type) {
            case CopyType::kProjectile:
                return kProjectile;
            case CopyType::kHazard:
                return kHazard;
            case CopyType::kExplosion:
                return kExplosion;
            default:
                return kImpact;
            }
        }

        template <class T>
        void Put(std::string& a_bytes, std::size_t a_offset, T a_value)
        {
            std::memcpy(a_bytes.data() + a_offset, &a_value, sizeof(T));
        }

        [[nodiscard]] double ValueOf(const FormSource& a_source, const char* a_key)
        {
            const auto it = a_source.data.find(a_key);
            return it == a_source.data.end() ? 0.0 : it->second;
        }

        // An integer field: refused when not a whole number in range (a request typo must not wrap)
        template <class T>
        [[nodiscard]] T Integer(const FormSource& a_source, const Field& a_field)
        {
            const double value = ValueOf(a_source, a_field.key);
            if (value != std::floor(value) || value < 0 || value > static_cast<double>(std::numeric_limits<T>::max())) {
                throw std::runtime_error(fmt::format("{} {}: '{}' = {} is not an integer in 0..{}",
                    CopyTypeTag(a_source.type), a_source.source, a_field.key, value, std::numeric_limits<T>::max()));
            }
            return static_cast<T>(value);
        }

        [[nodiscard]] std::string DataBytes(const FormSource& a_source)
        {
            std::string bytes(CopyDataSize(a_source.type), '\0');  // every form link stays 0
            for (const auto& field : Fields(a_source.type)) {
                switch (field.kind) {
                case Kind::kU8:
                    Put(bytes, field.offset, Integer<std::uint8_t>(a_source, field));
                    break;
                case Kind::kU16:
                    Put(bytes, field.offset, Integer<std::uint16_t>(a_source, field));
                    break;
                case Kind::kU32:
                    Put(bytes, field.offset, Integer<std::uint32_t>(a_source, field));
                    break;
                case Kind::kFloat:
                    Put(bytes, field.offset, static_cast<float>(ValueOf(a_source, field.key)));
                    break;
                }
            }
            return bytes;
        }

        [[nodiscard]] std::uint32_t ProjectileSoundLevel(const FormSource& a_source)
        {
            return Integer<std::uint32_t>(a_source, Field{ kSoundLevelKey, 0, Kind::kU32 });
        }

        [[nodiscard]] std::string CopyRecord(const CopyEntry& a_entry, const FormSource& a_source)
        {
            Writer body;
            body.ZString("EDID", a_entry.editorId);
            if (CopyHasBounds(a_entry.type)) {
                const auto& b = a_source.bounds;
                body.Sub("OBND", Pack(b[0], b[1], b[2], b[3], b[4], b[5]));
            }
            if (!a_entry.greyModel.empty()) {
                body.ZString("MODL", a_entry.greyModel.substr(std::char_traits<char>::length(kMeshesRoot)));
            }
            body.Sub("DATA", DataBytes(a_source));
            if (a_entry.type == CopyType::kProjectile) {
                body.Sub("VNAM", Pack(ProjectileSoundLevel(a_source)));
            }
            Writer record;
            RecordHeader(record, CopyTypeTag(a_entry.type), body.Data().size(), 0, a_entry.localId);
            record.Bytes(body.Data().data(), body.Data().size());
            return record.Data();
        }

        [[nodiscard]] EslForm ReadForm(const Reader& a_in, std::size_t a_at, std::size_t a_limit)
        {
            EslForm form;
            form.type = a_in.Tag(a_at);
            const auto size = a_in.Value<std::uint32_t>(a_at + 4);
            form.formId = a_in.Value<std::uint32_t>(a_at + 12);
            std::size_t pos = a_at + kHeaderSize;
            const std::size_t end = pos + size;
            if (end > a_limit) {
                throw std::runtime_error(fmt::format("{} record at byte {} runs past its group's end", form.type, a_at));
            }
            while (pos < end) {
                if (pos + kSubHeaderSize > end) {
                    throw std::runtime_error(fmt::format("subrecord header at byte {} runs past its record", pos));
                }
                const std::string tag = a_in.Tag(pos);
                const auto length = a_in.Value<std::uint16_t>(pos + 4);
                const std::size_t data = pos + kSubHeaderSize;
                if (data + length > end) {
                    throw std::runtime_error(fmt::format("subrecord {} at byte {} runs past its record", tag, pos));
                }
                const auto text = [&] { return a_in.Slice(data, length > 0 ? length - 1u : 0u); };
                if (tag == "EDID") {
                    form.editorId = text();
                } else if (tag == "MODL") {
                    form.model = text();
                } else if (tag == "OBND" && length == sizeof(form.bounds)) {
                    form.hasBounds = true;
                    for (std::size_t i = 0; i < form.bounds.size(); ++i) {
                        form.bounds[i] = a_in.Value<std::int16_t>(data + i * sizeof(std::int16_t));
                    }
                } else if (tag == "DATA") {
                    form.data = a_in.Slice(data, length);
                } else if (tag == "VNAM" && length == sizeof(std::uint32_t)) {
                    form.hasSoundLevel = true;
                    form.soundLevel = a_in.Value<std::uint32_t>(data);
                } else {
                    form.otherSubrecords.push_back(fmt::format("{}({})", tag, length));
                }
                pos = data + length;
            }
            return form;
        }
    }

    const char* CopyTypeTag(CopyType a_type) noexcept
    {
        switch (a_type) {
        case CopyType::kProjectile:
            return "PROJ";
        case CopyType::kHazard:
            return "HAZD";
        case CopyType::kExplosion:
            return "EXPL";
        default:
            return "IPCT";
        }
    }

    std::size_t CopyDataSize(CopyType a_type) noexcept
    {
        switch (a_type) {
        case CopyType::kProjectile:
            return kProjectileData;
        case CopyType::kHazard:
            return kHazardData;
        case CopyType::kExplosion:
            return kExplosionData;
        default:
            return kImpactData;
        }
    }

    bool CopyHasBounds(CopyType a_type) noexcept
    {
        return a_type != CopyType::kImpact;
    }

    std::vector<std::size_t> CopyLinkOffsets(CopyType a_type)
    {
        switch (a_type) {
        case CopyType::kProjectile:
            return { 0x10, 0x14, 0x24, 0x28, 0x38, 0x3C, 0x40, 0x54, 0x58 };
        case CopyType::kHazard:
            return { 0x18, 0x1C, 0x20, 0x24 };
        case CopyType::kExplosion:
            return { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14 };
        default:
            return {};
        }
    }

    std::vector<std::string> CopyFieldKeys(CopyType a_type)
    {
        std::vector<std::string> keys;
        for (const auto& field : Fields(a_type)) {
            keys.emplace_back(field.key);
        }
        if (a_type == CopyType::kProjectile) {
            keys.emplace_back(kSoundLevelKey);
        }
        return keys;
    }

    void WriteCopiesEsl(const CopiesMap& a_map, const std::vector<FormSource>& a_sources,
        const std::filesystem::path& a_file)
    {
        if (a_map.entries.empty() || a_map.entries.size() != a_sources.size() ||
            a_map.entries.size() > kLastLightFormId - kFirstLightFormId + 1) {
            throw std::runtime_error(fmt::format("the copies ESL needs 1 to 2048 entries matching the sources ({} entries, "
                "{} sources)", a_map.entries.size(), a_sources.size()));
        }
        Writer file;
        std::string groups;
        std::int32_t count = 0;
        std::uint32_t highest = 0;
        for (const auto type : { CopyType::kProjectile, CopyType::kHazard, CopyType::kExplosion, CopyType::kImpact }) {
            std::string records;
            for (std::size_t i = 0; i < a_map.entries.size(); ++i) {
                if (a_map.entries[i].type != type) {
                    continue;
                }
                records += CopyRecord(a_map.entries[i], a_sources[i]);
                highest = std::max(highest, a_map.entries[i].localId);
                ++count;
            }
            if (!records.empty()) {
                Writer group;
                TopGroup(group, CopyTypeTag(type), records);
                groups += group.Data();
                ++count;
            }
        }
        Writer header;
        header.Sub("HEDR", Pack(kHedrVersion, count, highest + 1));
        header.ZString("CNAM", kAuthor);
        RecordHeader(file, "TES4", header.Data().size(), kEslFlag, 0);
        file.Bytes(header.Data().data(), header.Data().size());
        file.Bytes(groups.data(), groups.size());

        std::filesystem::create_directories(a_file.parent_path());
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        out.write(file.Data().data(), static_cast<std::streamsize>(file.Data().size()));
        out.close();
        if (!out) {
            throw std::runtime_error(fmt::format("writing '{}' failed", a_file.string()));
        }
    }

    std::vector<EslForm> ReadCopiesEsl(const std::filesystem::path& a_file, EslHeader& a_header)
    {
        std::ifstream in(a_file, std::ios::binary);
        if (!in) {
            throw std::runtime_error(fmt::format("cannot open '{}'", a_file.string()));
        }
        const Reader reader{ std::string{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() } };
        if (reader.Tag(0) != "TES4") {
            throw std::runtime_error(fmt::format("'{}' is not a plugin (no TES4 record)", a_file.string()));
        }
        a_header = {};
        a_header.flags = reader.Value<std::uint32_t>(8);
        const std::size_t tes4End = kHeaderSize + reader.Value<std::uint32_t>(4);
        for (std::size_t pos = kHeaderSize; pos + kSubHeaderSize <= tes4End;) {
            const auto tag = reader.Tag(pos);
            const auto length = reader.Value<std::uint16_t>(pos + 4);
            if (tag == "HEDR") {
                a_header.version = reader.Value<float>(pos + kSubHeaderSize);
                a_header.records = reader.Value<std::int32_t>(pos + kSubHeaderSize + 4);
                a_header.nextObjectId = reader.Value<std::uint32_t>(pos + kSubHeaderSize + 8);
            } else if (tag == "MAST") {
                ++a_header.masters;
            }
            pos += kSubHeaderSize + length;
        }
        std::vector<EslForm> forms;
        for (std::size_t pos = tes4End; pos < reader.Size();) {
            if (reader.Tag(pos) != "GRUP") {
                throw std::runtime_error(fmt::format("'{}': expected a GRUP at byte {}", a_file.string(), pos));
            }
            const auto groupSize = reader.Value<std::uint32_t>(pos + 4);
            const std::size_t groupEnd = pos + groupSize;
            if (groupSize < kHeaderSize || groupEnd > reader.Size()) {
                throw std::runtime_error(fmt::format("'{}': GRUP at byte {} has size {}", a_file.string(), pos, groupSize));
            }
            const std::string label = reader.Tag(pos + 8);
            for (std::size_t at = pos + kHeaderSize; at < groupEnd;) {
                EslForm form = ReadForm(reader, at, groupEnd);
                if (form.type != label) {
                    throw std::runtime_error(fmt::format("'{}': a {} record inside the {} group", a_file.string(),
                        form.type, label));
                }
                at += kHeaderSize + reader.Value<std::uint32_t>(at + 4);
                forms.push_back(std::move(form));
            }
            pos = groupEnd;
        }
        return forms;
    }

    std::vector<std::string> VerifyCopiesEsl(const CopiesMap& a_map, const std::vector<FormSource>& a_sources,
        const std::filesystem::path& a_file)
    {
        std::vector<std::string> problems;
        EslHeader header;
        std::vector<EslForm> back;
        try {
            back = ReadCopiesEsl(a_file, header);
        } catch (const std::exception& e) {
            return { fmt::format("cannot read it back: {}", e.what()) };
        }
        if ((header.flags & kEslFlag) == 0 || header.masters != 0 || header.version != kHedrVersion) {
            problems.push_back(fmt::format("header flags 0x{:X}, {} masters, HEDR {} (ESL flag, no masters, {} expected)",
                header.flags, header.masters, header.version, kHedrVersion));
        }
        if (back.size() != a_map.entries.size()) {
            problems.push_back(fmt::format("{} records read back, {} written", back.size(), a_map.entries.size()));
            return problems;
        }
        for (std::size_t i = 0; i < a_map.entries.size(); ++i) {
            const auto& entry = a_map.entries[i];
            const auto it = std::find_if(back.begin(), back.end(), [&](const EslForm& f) { return f.formId == entry.localId; });
            if (it == back.end()) {
                problems.push_back(fmt::format("{:06X} {} not read back", entry.localId, entry.editorId));
                continue;
            }
            const std::string model = entry.greyModel.empty() ?
                                          std::string() :
                                          entry.greyModel.substr(std::char_traits<char>::length(kMeshesRoot));
            const bool boundsOk = CopyHasBounds(entry.type) ? (it->hasBounds && it->bounds == a_sources[i].bounds) :
                                                              !it->hasBounds;
            const bool soundOk = entry.type == CopyType::kProjectile ?
                                     (it->hasSoundLevel && it->soundLevel == ProjectileSoundLevel(a_sources[i])) :
                                     !it->hasSoundLevel;
            if (it->type != CopyTypeTag(entry.type) || it->editorId != entry.editorId || it->model != model ||
                it->data != DataBytes(a_sources[i]) || !boundsOk || !soundOk || !it->otherSubrecords.empty()) {
                problems.push_back(fmt::format("{:06X} {}: read back {} '{}' model '{}' DATA {} bytes, bounds {}, sound level "
                    "{}, other subrecords {}", entry.localId, entry.editorId, it->type, it->editorId, it->model,
                    it->data.size(), boundsOk ? "ok" : "DIFFER", soundOk ? "ok" : "DIFFER", it->otherSubrecords.size()));
            }
        }
        return problems;
    }
}
