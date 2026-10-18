// =============================================================================
// SlotTxst - a decal texture set slot's runtime write (SlotTxst.h)
// =============================================================================

#include "SlotTxst.h"

#include "SehGuard.h"
#include "SingleLaunch.h"
#include "SlotKeys.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <vector>
#include <mutex>

namespace BlackFlame::SlotTxst
{
    namespace
    {
        namespace CC = CopyContract;
        using RE::BSResource::ID;
        constexpr std::string_view kTexturesPrefix = "textures\\";

        enum class Rule
        {
            kUnmade,
            kPrefixed,  // GenerateFromPath("textures\" + name)
            kBare,      // GenerateFromPath(name)
            kZero,      // the engine leaves them zero
            kNone,      // nothing fits: no TXST slot is written this session
        };

        TrackedMutex g_mutex;
        Rule g_rule = Rule::kUnmade;
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

        [[nodiscard]] std::optional<ID> ByRule(Rule a_rule, const std::string& a_name)
        {
            switch (a_rule) {
            case Rule::kPrefixed: return Generate(std::string(kTexturesPrefix) + a_name);
            case Rule::kBare: return Generate(a_name);
            case Rule::kZero: return ID{};
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

        constexpr std::size_t kRuleExamples = 5;  // sources quoted by the rule line (all are counted)
        constexpr std::size_t kForms = 3;         // "textures\" + name, name, zero

        [[nodiscard]] const char* RuleTag(Rule a_rule) noexcept
        {
            switch (a_rule) {
            case Rule::kPrefixed: return "prefixed";
            case Rule::kBare: return "bare";
            case Rule::kZero: return "zero";
            case Rule::kNone: return "none";
            default: return "not made";
            }
        }

        // Every non-empty slot of every assigned TXST slot's source against both forms
        void MakeRule()
        {
            std::size_t checked = 0;
            std::size_t prefixed = 0;
            std::size_t bare = 0;
            std::size_t zero = 0;
            std::string first;
            // per source: its first slot that disagrees with each form ("" when every slot agrees)
            std::vector<std::pair<std::string, std::array<std::string, kForms>>> sources;
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                const auto* source = slot.type == CC::SlotType::kTXST && slot.source ? slot.source->As<RE::BGSTextureSet>() : nullptr;
                if (!source) {
                    continue;
                }
                std::array<std::string, kForms> disagree;
                bool any = false;
                for (std::size_t i = 0; i < SlotTxstPaths::kSlots; ++i) {
                    const auto name = NameOf(*source, i);
                    if (name.empty()) {
                        continue;
                    }
                    any = true;
                    const auto& stored = source->textureFileIDs[i];
                    const auto withPrefix = Generate(std::string(kTexturesPrefix) + name);
                    const auto without = Generate(name);
                    ++checked;
                    prefixed += withPrefix && Same(*withPrefix, stored) ? 1 : 0;
                    bare += without && Same(*without, stored) ? 1 : 0;
                    zero += IsZero(stored) ? 1 : 0;
                    const bool agrees[kForms] = { withPrefix && Same(*withPrefix, stored), without && Same(*without, stored), IsZero(stored) };
                    for (std::size_t f = 0; f < kForms; ++f) {
                        if (!agrees[f] && disagree[f].empty()) {
                            disagree[f] = std::format("TX0{} stored {}", i, IdText(stored));
                        }
                    }
                    if (first.empty()) {
                        first = std::format("{} TX0{} '{}': stored {} | \"textures\\\" + name {} | name {}", slot.sourceText, i, name,
                            IdText(stored), withPrefix ? IdText(*withPrefix) : "FAULT", without ? IdText(*without) : "FAULT");
                    }
                }
                if (any) {
                    sources.emplace_back(slot.sourceText, std::move(disagree));
                }
            }
            g_rule = checked == 0 ? Rule::kNone : prefixed == checked ? Rule::kPrefixed : bare == checked ? Rule::kBare :
                     zero == checked ? Rule::kZero : Rule::kNone;
            const char* name = g_rule == Rule::kPrefixed ? "GenerateFromPath(\"textures\\\" + name)" :
                               g_rule == Rule::kBare ? "GenerateFromPath(name)" : g_rule == Rule::kZero ? "zero (the engine leaves them so)" :
                                                                                                         "NONE: no TXST slot is written this session";
            // the chosen form, or (none) the one most slots follow: every source that disagrees with it is counted
            const std::size_t form = g_rule == Rule::kPrefixed ? 0 : g_rule == Rule::kBare ? 1 : g_rule == Rule::kZero ? 2 :
                                     prefixed >= bare && prefixed >= zero ? 0 : bare >= zero ? 1 : 2;
            std::size_t disagreeing = 0;
            std::string examples;
            for (const auto& [text, disagree] : sources) {
                if (disagree[form].empty()) {
                    continue;
                }
                if (++disagreeing <= kRuleExamples) {
                    examples += std::format("{}{} {}", examples.empty() ? "" : "; ", text, disagree[form]);
                }
            }
            constexpr const char* kFormNames[kForms] = { "\"textures\\\" + name", "name", "zero" };
            g_ruleText = std::format("textureFileIDs rule: {} ({} slots of the TXST sources checked: {} match \"textures\\\" + name, {} "
                                     "match name, {} stored zero; first: {}; sources: {}, {} disagree with {}{})", name, checked, prefixed,
                bare, zero, first.empty() ? "none" : first, sources.size(), disagreeing, kFormNames[form],
                examples.empty() ? std::string() : std::format(" (first {}: {})", (std::min)(disagreeing, kRuleExamples), examples));
            logger::info("BlackFlame: slots: TXST {}", g_ruleText);
        }

        [[nodiscard]] Rule TheRule()
        {
            std::lock_guard lock(g_mutex);
            if (g_rule == Rule::kUnmade) {
                MakeRule();
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
        if (out && !a_object.idsRule.empty() && a_object.idsRule != RuleName()) {
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
        if (a_string == NameOf(*source, a_index)) {
            return source->textureFileIDs[a_index];  // the engine's own for the source's string
        }
        return ByRule(TheRule(), a_string);
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
        for (std::size_t i = 0; i < ids.size(); ++i) {
            set->textures[i].textureName = a_strings[i].empty() ? RE::BSFixedString() : RE::BSFixedString(a_strings[i].c_str());
            set->textureFileIDs[i] = ids[i];
        }
        a_slot.publishedTextures = a_strings;
        logger::info("BlackFlame: slots: TXST slot {:X} ({}) written: TX00 '{}', TX03 '{}'", a_slot.localId, a_slot.sourceText,
            a_strings[CC::kTxstDiffuseSlot], a_strings[CC::kTxstGlowSlot]);
        return true;
    }
}
