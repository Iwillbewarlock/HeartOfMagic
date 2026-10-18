// =============================================================================
// SlotTxst - a decal texture set slot's runtime write (SlotTxst.h)
// =============================================================================

#include "SlotTxst.h"

#include "DataPathRule.h"
#include "SehGuard.h"
#include "SingleLaunch.h"
#include "SlotKeys.h"
#include "SlotTxstRule.h"
#include "TrackedMutex.h"

#include <array>
#include <cstring>
#include <format>
#include <mutex>

namespace BlackFlame::SlotTxst
{
    namespace
    {
        namespace CC = CopyContract;
        using RE::BSResource::ID;

        using Rule = SlotTxstRule::Rule;

        TrackedMutex g_mutex;
        bool g_made = false;
        Rule g_rule = Rule::kNone;
        std::string g_ruleText = "not made";

        [[nodiscard]] bool Same(const ID& a_lhs, const ID& a_rhs) noexcept
        {
            return std::memcmp(&a_lhs, &a_rhs, sizeof(ID)) == 0;
        }

        [[nodiscard]] bool IsZero(const ID& a_id) noexcept
        {
            return Same(a_id, ID{});
        }

        [[nodiscard]] std::string IdText(const ID& a_id)
        {
            return std::format("file={:08X} ext='{}' dir={:08X}", a_id.file, std::string(a_id.ext, strnlen(a_id.ext, sizeof(a_id.ext))), a_id.dir);
        }

        [[nodiscard]] std::optional<ID> Generate(const std::string& a_path)
        {
            ID id{};
            if (!GuardedKeep("slot TXST GenerateFromPath", [&]() { id.GenerateFromPath(a_path.c_str()); })) {
                return std::nullopt;
            }
            return id;
        }

        /// "prefixed": the name as Data-relative under textures\ (DataPathRule::Normalize); nullopt when Normalize refuses it
        [[nodiscard]] std::optional<std::string> PrefixedPath(const std::string& a_name)
        {
            return DataPathRule::Normalize(a_name, DataPathRule::kTexturesRoot);
        }

        [[nodiscard]] std::optional<ID> ByRule(Rule a_rule, const std::string& a_name)
        {
            switch (a_rule) {
            case Rule::kPrefixed:
                {
                    const auto path = PrefixedPath(a_name);
                    return path ? Generate(*path) : std::nullopt;
                }
            case Rule::kBare: return Generate(a_name);
            default: return std::nullopt;
            }
        }

        [[nodiscard]] std::string NameOf(const RE::BGSTextureSet& a_set, std::size_t a_index)
        {
            const char* text = a_set.textures[a_index].textureName.c_str();
            return text ? std::string(text) : std::string();
        }

        [[nodiscard]] SlotTxstPaths::Strings RawOf(const RE::BGSTextureSet& a_set)
        {
            SlotTxstPaths::Strings raw;
            for (std::size_t i = 0; i < raw.size(); ++i) {
                raw[i] = NameOf(a_set, i);
            }
            return raw;
        }

        [[nodiscard]] const char* RuleTag(Rule a_rule) noexcept
        {
            switch (a_rule) {
            case Rule::kPrefixed: return "prefixed";
            case Rule::kBare: return "bare";
            default: return "none";
            }
        }

        // Every non-empty slot of every assigned TXST slot's source votes or is unexplained (SlotTxstRule.h)
        void MakeRule()
        {
            SlotTxstRule::Tally tally;
            std::size_t sources = 0;
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                const auto* source = slot.type == CC::SlotType::kTXST && slot.source ? slot.source->As<RE::BGSTextureSet>() : nullptr;
                if (!source) {
                    continue;
                }
                bool any = false;
                for (std::size_t i = 0; i < SlotTxstPaths::kSlots; ++i) {
                    const auto name = NameOf(*source, i);
                    if (name.empty()) {
                        continue;
                    }
                    any = true;
                    const auto& stored = source->textureFileIDs[i];
                    const auto path = PrefixedPath(name);
                    const auto normalized = path ? Generate(*path) : std::nullopt;
                    const auto raw = Generate(name);
                    SlotTxstRule::SlotEvidence evidence;
                    evidence.storedZero = IsZero(stored);
                    evidence.shapeSkipped = !path;
                    evidence.fault = (path && !normalized) || !raw;
                    evidence.matchesNormalized = normalized && Same(*normalized, stored);
                    evidence.matchesRaw = raw && Same(*raw, stored);
                    tally.Add(name, SlotTxstRule::KindOf(evidence), std::format("{} TX0{} stored {}", slot.sourceText, i, IdText(stored)));
                }
                sources += any ? 1 : 0;
            }
            g_rule = tally.Decide();
            const char* name = g_rule == Rule::kPrefixed ? "GenerateFromPath(Normalize(name, \"textures\\\"))" :
                               g_rule == Rule::kBare     ? "GenerateFromPath(name)" :
                                                           "NONE: no TXST slot is written this session";
            g_ruleText = std::format("textureFileIDs rule: {} ({} TXST sources, {}; an unexplained slot does not vote, an unchanged "
                                     "one keeps its stored ID unless that names another file and the name hashes as it is)", name, sources, tally.Text());
            logger::info("BlackFlame: slots: TXST {}", g_ruleText);
        }

        [[nodiscard]] Rule TheRule()
        {
            std::lock_guard lock(g_mutex);
            if (!g_made) {
                MakeRule();
                g_made = true;
            }
            return g_rule;
        }
    }

    std::optional<SlotTxstPaths::Strings> StringsFor(const SlotAssign::Slot& a_slot, const SlotLedgerModel::Object& a_object, std::string& a_why)
    {
        const auto* source = a_slot.source ? a_slot.source->As<RE::BGSTextureSet>() : nullptr;
        const auto& inputs = SingleLaunch::Inputs();
        if (!source || !inputs.prefix) {
            a_why = !source ? "no source texture set" : "no key prefix this session";
            return std::nullopt;
        }
        const auto raw = RawOf(*source);
        return SlotTxstPaths::RecordStrings(*inputs.prefix, raw, SlotKeys::TextureHash(inputs, raw[CC::kTxstDiffuseSlot]),
            SlotKeys::TextureHash(inputs, raw[CC::kTxstGlowSlot]), a_object.deps, a_why);
    }

    std::optional<SlotTxstPaths::Strings> RestoredFor(const SlotAssign::Slot& a_slot, const SlotLedgerModel::Object& a_object, std::string& a_why)
    {
        if (a_object.textures.empty()) {
            return StringsFor(a_slot, a_object, a_why);  // a ledger written before the strings were kept: made again, as before
        }
        const auto* source = a_slot.source ? a_slot.source->As<RE::BGSTextureSet>() : nullptr;
        if (!source) {
            a_why = "no source texture set";
            return std::nullopt;
        }
        const auto fields = CC::TxstSourceFields(SlotKeys::TxstInput(SingleLaunch::Inputs(), *source));
        auto out = SlotTxstPaths::RestoredStrings(a_object.keyText, fields, RawOf(*source), a_object.textures, a_object.deps, a_why);
        std::string unusable;
        if (out && !a_object.idsRule.empty() && a_object.idsRule != RuleName() && RuleUsable(unusable)) {
            logger::info("BlackFlame: slots: TXST slot {:X} ({}): the textureFileIDs rule was '{}' when it was published, '{}' now: "
                "the IDs are made by this session's", a_slot.localId, a_slot.sourceText, a_object.idsRule, RuleName());
        }
        return out;
    }

    std::string RuleName()
    {
        return RuleTag(TheRule());
    }

    bool RuleUsable(std::string& a_why)
    {
        if (TheRule() == Rule::kNone) {
            a_why = "no textureFileIDs rule fits the engine's own (" + RuleText() + ")";
            return false;
        }
        return true;
    }

    std::string RuleText()
    {
        std::lock_guard lock(g_mutex);
        return g_ruleText;
    }

    std::optional<ID> ExpectedId(const SlotAssign::Slot& a_slot, std::size_t a_index, const std::string& a_string)
    {
        const auto* source = a_slot.source ? a_slot.source->As<RE::BGSTextureSet>() : nullptr;
        if (!source || a_index >= SlotTxstPaths::kSlots) {
            return std::nullopt;
        }
        if (a_string.empty()) {
            return ID{};  // an empty slot never takes an ID (not even a stale one the source holds)
        }
        const auto current = TheRule();
        const auto rule = ByRule(current, a_string);
        if (a_string == NameOf(*source, a_index)) {
            // The source's string: its stored ID when that is zero (a missing file stays as the engine left it), the rule's
            // own, or the string's hash input is not its own shape (Normalize folded something); else the stored one names
            // another file (run 3: the stale _g behind a 'black.dds' TX03) and the rule's ID replaces it, so the copy's name
            // and ID agree. This leaves "copy == source" on purpose: the builder recoloured that slot by its string.
            const auto& stored = source->textureFileIDs[a_index];
            const auto path = current == Rule::kPrefixed ? PrefixedPath(a_string) : std::nullopt;
            const bool shapeKept = current != Rule::kPrefixed ||
                                   (path && SlotTxstRule::NormalizeKeepsShape(a_string, *path, std::string(DataPathRule::kTexturesRoot)));
            return SlotTxstRule::KeepsStored(IsZero(stored), rule.has_value(), rule && Same(*rule, stored), shapeKept) ? stored : *rule;
        }
        return rule;
    }

    bool Write(SlotAssign::Slot& a_slot, const SlotTxstPaths::Strings& a_strings, std::string& a_why)
    {
        auto* set = a_slot.form ? a_slot.form->As<RE::BGSTextureSet>() : nullptr;
        if (!set || !RuleUsable(a_why)) {
            a_why = set ? a_why : "the slot's form is no texture set";
            return false;
        }
        std::array<ID, SlotTxstPaths::kSlots> ids{};
        for (std::size_t i = 0; i < ids.size(); ++i) {  // every ID first: nothing is written when one cannot be made
            const auto id = ExpectedId(a_slot, i, a_strings[i]);
            if (!id) {
                a_why = std::format("the textureFileID of TX0{} '{}' could not be made", i, a_strings[i]);
                return false;
            }
            ids[i] = *id;
        }
        const auto* source = a_slot.source ? a_slot.source->As<RE::BGSTextureSet>() : nullptr;
        std::string replaced;  // unchanged slots not given the source's stored ID (ExpectedId: another file, or an empty slot)
        for (std::size_t i = 0; source && i < ids.size(); ++i) {
            if (a_strings[i] == NameOf(*source, i) && !Same(ids[i], source->textureFileIDs[i])) {
                replaced += std::format("; TX0{} '{}' stored {}, written {}", i, a_strings[i], IdText(source->textureFileIDs[i]), IdText(ids[i]));
            }
        }
        for (std::size_t i = 0; i < ids.size(); ++i) {
            set->textures[i].textureName = a_strings[i].empty() ? RE::BSFixedString() : RE::BSFixedString(a_strings[i].c_str());
            set->textureFileIDs[i] = ids[i];
        }
        a_slot.publishedTextures = a_strings;
        logger::info("BlackFlame: slots: TXST slot {:X} ({}) written: TX00 '{}', TX03 '{}'{}", a_slot.localId, a_slot.sourceText,
            a_strings[CC::kTxstDiffuseSlot], a_strings[CC::kTxstGlowSlot],
            replaced.empty() ? std::string() : " (IDs not taken from the source" + replaced + ")");
        return true;
    }
}
