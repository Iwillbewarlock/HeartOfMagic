// =============================================================================
// GreySlotPool - the slot pool ESL (GreyFormsEsl / GreyEsl writer): TXST with and without DODT, ADDN 40000+, the collision list
// =============================================================================

#include "GreySlotPool.h"

#include "GreyEslIo.h"
#include "GreyForms.h"
#include "GreyJson.h"
#include "GreyProgress.h"

#include "../../plugins/BlackFlame/src/CopyContract.h"

#include <algorithm>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <optional>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>


namespace GreyBuild
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;

        constexpr std::uint16_t kSlotAddnCap = 0;       // the DLL fills the cap from the slot's source
        constexpr std::size_t kProblemsShown = 20;  // the first ones say it

        [[nodiscard]] std::optional<CopyType> CopyTypeOfSlot(CC::SlotType a_type) noexcept
        {
            switch (a_type) {
            case CC::SlotType::kPROJ: return CopyType::kProjectile;
            case CC::SlotType::kIPCT: return CopyType::kImpact;
            case CC::SlotType::kEXPL: return CopyType::kExplosion;
            case CC::SlotType::kARTO: return CopyType::kArtObject;
            case CC::SlotType::kHAZD: return CopyType::kHazard;
            case CC::SlotType::kTXST: return CopyType::kTextureSet;
            default: return std::nullopt;  // ADDN (its own writer) and none
            }
        }

        [[nodiscard]] std::string SlotEditorId(CC::SlotType a_type, std::uint32_t a_localId)
        {
            return fmt::format("{}{}_{:03X}", kSlotEditorIdPrefix, CC::SlotTypeTag(a_type), a_localId);
        }

        [[nodiscard]] const CC::SlotPool& AddnPool() noexcept
        {
            return CC::kSlotPools.back();  // static_assert in CopyContract.h: the ADDN pool is the last
        }

        // Every slot but the ADDNs as a copies map with empty sources (no model, no data: every value 0)
        void PoolForms(CopiesMap& a_map, std::vector<FormSource>& a_sources)
        {
            for (const auto& pool : CC::kSlotPools) {
                const auto type = CopyTypeOfSlot(pool.type);
                if (!type) {
                    continue;
                }
                for (std::uint32_t id = pool.first; id < static_cast<std::uint32_t>(pool.first) + pool.count; ++id) {
                    CopyEntry entry;
                    entry.source = fmt::format("{}|{:06X}", kSlotsPluginName, id);
                    entry.type = *type;
                    entry.localId = id;
                    entry.editorId = SlotEditorId(pool.type, id);
                    entry.hasDecal = CC::TxstHasDodt(id);
                    FormSource source;
                    source.source = entry.source;
                    source.type = *type;
                    a_map.entries.push_back(std::move(entry));
                    a_sources.push_back(std::move(source));
                }
            }
        }

        [[nodiscard]] AddonEntry SlotAddon(std::uint32_t a_localId)
        {
            AddonEntry entry;
            entry.grey = static_cast<int>(*CC::AddnIndexOfSlot(a_localId));
            entry.greyModel = std::string(kMeshesRoot) + kSlotAddnModel;
            entry.greyEditorId = SlotEditorId(CC::SlotType::kADDN, a_localId);
            entry.masterParticleCap = kSlotAddnCap;
            entry.dnamFlags = kDnamMasterParticleSystem;  // v3 #5: on demand; the DLL copies the source's flags
            return entry;
        }

        [[nodiscard]] std::string AddnGroup(std::int32_t& a_count)
        {
            std::string records;
            const auto& pool = AddnPool();
            for (std::uint32_t id = pool.first; id < static_cast<std::uint32_t>(pool.first) + pool.count; ++id) {
                records += AddonRecord(SlotAddon(id), id);
                ++a_count;
            }
            EslIo::Writer group;
            EslIo::TopGroup(group, "ADDN", records);
            ++a_count;
            return group.Data();
        }

        [[nodiscard]] std::string Zeros(std::size_t a_size)
        {
            return std::string(a_size, '\0');
        }

        // One read-back record against its slot; the problems
        void CheckRecord(const EslForm& a_form, CC::SlotType a_type, std::uint32_t a_id, std::vector<std::string>& a_problems)
        {
            const auto add = [&](const std::string& a_what) {
                a_problems.push_back(fmt::format("{:03X} {}: {}", a_id, CC::SlotTypeTag(a_type), a_what));
            };
            if (a_form.type != CC::SlotTypeTag(a_type) || a_form.editorId != SlotEditorId(a_type, a_id)) {
                add(fmt::format("read back {} '{}'", a_form.type, a_form.editorId));
            }
            if (a_type == CC::SlotType::kADDN) {
                const std::array<std::int16_t, 6> bounds{ kAddnBoundMin, kAddnBoundMin, kAddnBoundMin, kAddnBoundMax, kAddnBoundMax,
                    kAddnBoundMax };
                const std::string data = EslIo::Pack(static_cast<std::int32_t>(*CC::AddnIndexOfSlot(a_id)));
                const std::string dnam = EslIo::Pack(kSlotAddnCap, kDnamMasterParticleSystem);
                if (!a_form.hasModel || a_form.model != kSlotAddnModel || !a_form.hasBounds || a_form.bounds != bounds ||
                    a_form.data != data || a_form.dnam != dnam || !a_form.otherSubrecords.empty()) {
                    add(fmt::format("MODL '{}', DATA {} bytes, DNAM {} bytes, other {}", a_form.model, a_form.data.size(),
                        a_form.dnam.size(), a_form.otherSubrecords.size()));
                }
                return;
            }
            const auto copyType = CopyTypeOfSlot(a_type);
            if (!copyType) {
                add("no record type for this slot type");
                return;
            }
            const auto type = *copyType;
            const bool dnamType = std::string_view(CopyDataTag(type)) == "DNAM";
            const bool scalarsOk = dnamType ? (a_form.hasDnam && a_form.dnam == Zeros(CopyDataSize(type)) && !a_form.hasData) :
                                              (a_form.hasData && a_form.data == Zeros(CopyDataSize(type)) && !a_form.hasDnam);
            const bool boundsOk = CopyHasBounds(type) ? (a_form.hasBounds && a_form.bounds == std::array<std::int16_t, 6>{}) :
                                                        !a_form.hasBounds;
            const bool soundOk = a_form.hasSoundLevel == (type == CopyType::kProjectile) && a_form.soundLevel == 0;
            const bool decal = CC::TxstHasDodt(a_id);
            const bool dodtOk = a_form.hasDodt == decal && (!decal || a_form.dodt == Zeros(kDodtSize));
            if (!scalarsOk || !boundsOk || !soundOk || !dodtOk || a_form.hasModel || a_form.textureMask != 0 ||
                !a_form.otherSubrecords.empty()) {
                add(fmt::format("scalars {}, bounds {}, VNAM {}, DODT {} (present {}, want {}), MODL {}, texture slots 0x{:X}, other {}",
                    scalarsOk ? "ok" : "DIFFER", boundsOk ? "ok" : "DIFFER", soundOk ? "ok" : "DIFFER", dodtOk ? "ok" : "DIFFER", a_form.hasDodt, decal,
                    a_form.hasModel, a_form.textureMask, a_form.otherSubrecords.size()));
            }
        }
    }

    void WriteSlotPool(const std::filesystem::path& a_file)
    {
        CopiesMap map;
        std::vector<FormSource> sources;
        PoolForms(map, sources);
        std::string groups;
        std::int32_t count = 0;
        std::uint32_t highest = 0;
        for (const auto type : kCopyTypes) {
            if (type == CopyType::kImpact) {
                groups += AddnGroup(count);  // Skyrim.esm: ADDN after EXPL, before IPCT
            }
            groups += CopiesEslGroup(type, map, sources, count, highest);
        }
        if (highest >= CC::kFirstReservedLocalId) {  // HEDR's next id must stay past every record (the pools end below it)
            throw std::runtime_error(fmt::format("slot pool: a record at 0x{:X}, in the reserved ids from 0x{:X}", highest,
                CC::kFirstReservedLocalId));
        }
        WriteLightPlugin(a_file, groups, count, CC::kFirstReservedLocalId);
    }

    std::vector<std::string> VerifySlotPool(const std::filesystem::path& a_file)
    {
        std::vector<std::string> problems;
        EslHeader header;
        std::vector<EslForm> forms;
        std::vector<EslAddon> addons;
        try {
            forms = ReadCopiesEsl(a_file, header);
            std::uint32_t addonFlags = 0;
            addons = ReadAddonEsl(a_file, addonFlags);  // the ADDN reader too: index and flags as the --request ESL reads them
            if (addonFlags != header.flags) {
                problems.push_back(fmt::format("the two readers disagree on the header flags ({:X}, {:X})", addonFlags, header.flags));
            }
        } catch (const std::exception& e) {
            return { fmt::format("cannot read it back: {}", e.what()) };
        }
        const auto groups = static_cast<std::int32_t>(CC::kSlotPools.size());
        if ((header.flags & kEslFlag) == 0 || header.masters != 0 || header.version != kHedrVersion ||
            header.records != static_cast<std::int32_t>(CC::kSlotCount) + groups || header.nextObjectId != CC::kFirstReservedLocalId) {
            problems.push_back(fmt::format("header flags 0x{:X}, {} masters, HEDR {} records {} next id {:X} (ESL, none, {}, {}, {:X})",
                header.flags, header.masters, header.version, header.records, header.nextObjectId, kHedrVersion,
                CC::kSlotCount + groups, CC::kFirstReservedLocalId));
        }
        if (forms.size() != CC::kSlotCount) {
            problems.push_back(fmt::format("{} records read back, {} slots", forms.size(), CC::kSlotCount));
        }
        std::map<std::uint32_t, const EslForm*> byId;
        for (const auto& form : forms) {
            if (!byId.emplace(form.formId, &form).second) {
                problems.push_back(fmt::format("local id {:03X} twice", form.formId));
            }
        }
        for (const auto& pool : CC::kSlotPools) {
            for (std::uint32_t id = pool.first; id < static_cast<std::uint32_t>(pool.first) + pool.count; ++id) {
                const auto it = byId.find(id);
                if (it == byId.end()) {
                    problems.push_back(fmt::format("{:03X} {}: not read back", id, CC::SlotTypeTag(pool.type)));
                    continue;
                }
                CheckRecord(*it->second, pool.type, id, problems);
                byId.erase(it);
            }
        }
        for (const auto& [id, form] : byId) {
            problems.push_back(fmt::format("{:03X} {}: a record outside the pool table", id, form->type));
        }
        const auto& addnPool = AddnPool();
        bool addnOk = addons.size() == addnPool.count;
        for (std::size_t i = 0; addnOk && i < addons.size(); ++i) {
            const auto id = static_cast<std::uint32_t>(addnPool.first + i);
            addnOk = addons[i].formId == id && addons[i].index == static_cast<int>(*CC::AddnIndexOfSlot(id)) &&
                     addons[i].dnamFlags == kDnamMasterParticleSystem;
        }
        if (!addnOk) {
            problems.push_back(fmt::format("the ADDN reader: {} records, not {} at 0x{:X} up with index 40000 + place and DNAM flags 1",
                addons.size(), addnPool.count, addnPool.first));
        }
        if (problems.size() > kProblemsShown) {
            problems.resize(kProblemsShown);
        }
        return problems;
    }

    std::vector<AddnCollision> FindAddnCollisions(const std::filesystem::path& a_request)
    {
        const auto root = Json::ReadFile(a_request);
        const auto& addons = Json::RequireArray(a_request, root, "addons");
        std::vector<AddnCollision> out;
        for (std::size_t i = 0; i < addons.size(); ++i) {
            const std::string where = fmt::format("addons[{}]", i);
            if (!addons[i].is_object()) {
                Json::Fail(a_request, where + " must be an object");
            }
            const int index = Json::ReadIndex(a_request, addons[i], "index", where);
            if (addons[i].contains("own") && !addons[i]["own"].is_boolean()) {
                Json::Fail(a_request, where + ": 'own' must be true or false");
            }
            const bool own = addons[i].contains("own") && addons[i]["own"].get<bool>();
            const auto slot = CC::SlotOfAddnIndex(static_cast<std::uint32_t>(index));
            if (own || !slot) {
                continue;
            }
            out.push_back({ index, *slot, Json::ReadOptionalString(a_request, addons[i], "source", where),
                Json::ReadOptionalString(a_request, addons[i], "model", where) });
        }
        std::sort(out.begin(), out.end(), [](const AddnCollision& a, const AddnCollision& b) { return a.index < b.index; });
        return out;
    }

    int RunWriteSlotPool(const std::filesystem::path& a_target)
    {
        std::error_code absoluteError;
        const auto file = std::filesystem::absolute(a_target, absoluteError);  // a bare file name: the current folder
        if (absoluteError) {
            std::cout << fmt::format("FAIL  {} is no path this builder can write: {}\n", PathText(a_target), absoluteError.message());
            return kExitFail;
        }
        const auto temp = TempPathOf(file);
        try {
            WriteSlotPool(temp);
            const auto problems = VerifySlotPool(temp);
            for (const auto& problem : problems) {
                std::cout << "FAIL  " << problem << "\n";
            }
            if (!problems.empty()) {
                std::error_code ec;
                std::filesystem::remove(temp, ec);
                std::cout << "nothing written\n";
                return kExitFail;
            }
            if (!::MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::cout << fmt::format("FAIL  moving {} into place failed (Windows error {})\n", PathText(file), ::GetLastError());
                RemoveQuietly(temp);
                return kExitFail;
            }
        } catch (const std::exception& e) {
            std::cout << fmt::format("FAIL  {}\n", e.what());
            RemoveQuietly(temp);
            return kExitFail;
        }
        std::cout << fmt::format("wrote {}: {} slots (PROJ, IPCT, EXPL, ARTO, HAZD, TXST, ADDN at 0x{:X}-0x{:X}), read back\n",
            PathText(file), CC::kSlotCount, CC::kFirstEslLocalId, CC::kFirstReservedLocalId - 1);
        return kExitOk;
    }

    int RunCollisions(const std::filesystem::path& a_request)
    {
        std::vector<AddnCollision> collisions;
        try {
            collisions = FindAddnCollisions(a_request);
        } catch (const std::exception& e) {
            std::cout << fmt::format("FAIL  {}\n", e.what());
            return kExitUsage;  // the request does not read: 2, apart from 1 for collisions found
        }
        for (const auto& c : collisions) {
            std::cout << fmt::format("COLLIDE  ADDN index {} ({}, model '{}') is slot {:03X}'s index: that slot cannot be used "
                                     "on this load order\n", c.index, c.source.empty() ? std::string("no source") : c.source, c.model, c.slot);
        }
        std::cout << fmt::format("{} ADDN index(es) of {} in the slot pool's range {}-{}\n", collisions.size(), PathText(a_request),
            CC::kAddnIndexBase, CC::kAddnIndexEnd - 1);
        return collisions.empty() ? kExitOk : kExitFail;
    }
}
