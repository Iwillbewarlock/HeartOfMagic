#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"
#include "librarian/TagVocabulary.h"
#include "PathText.h"

#include <vector>

// =============================================================================
// LibrarianRules - loading and merging the classification rule files
// =============================================================================

namespace Librarian
{
    namespace
    {
        constexpr const char* RULE_FILE_EXTENSION = ".json";

        // Reads an optional string field, leaving the target alone when absent.
        void ReadString(const json& object, const char* key, std::string& target)
        {
            const auto found = object.find(key);
            if (found != object.end() && found->is_string()) {
                target = found->get<std::string>();
            }
        }

        // Reads an optional boolean field. Absent leaves the condition unset,
        // which is different from setting it to false.
        void ReadBool(const json& object, const char* key, std::optional<bool>& target)
        {
            const auto found = object.find(key);
            if (found != object.end() && found->is_boolean()) {
                target = found->get<bool>();
            }
        }

        // Reads one string or a list of them, appending to the target.
        void ReadStringList(const json& object, const char* key, std::vector<std::string>& target)
        {
            const auto found = object.find(key);
            if (found == object.end()) return;
            if (found->is_string()) {
                target.push_back(found->get<std::string>());
            } else if (found->is_array()) {
                for (const auto& entry : *found) {
                    if (entry.is_string()) target.push_back(entry.get<std::string>());
                }
            }
        }

        using TagPredicate = bool (*)(std::string_view);

        // Reads a "tags" list, accepting a bare string as a list of one so a
        // rule that adds a single tag does not need brackets. Tags outside the
        // vocabulary are dropped: a typo must not become a tag that no adapter
        // knows how to translate.
        void ReadTagList(const json& object, const char* key, const char* noun,
            TagPredicate inVocabulary, const std::string& originFile,
            std::vector<std::string>& target, std::size_t& rejected)
        {
            const auto found = object.find(key);
            if (found == object.end()) {
                return;
            }

            const auto accept = [&](const json& entry) {
                if (!entry.is_string()) {
                    return;
                }
                std::string tag = entry.get<std::string>();
                if (!inVocabulary(tag)) {
                    ++rejected;
                    logger::warn("Librarian: '{}' uses '{}', which is not a known {} - dropped",
                        originFile, tag, noun);
                    return;
                }
                target.push_back(std::move(tag));
            };

            if (found->is_string()) {
                accept(*found);
                return;
            }

            if (!found->is_array()) {
                return;
            }

            for (const auto& entry : *found) {
                accept(entry);
            }
        }

        // Every key a "match" object may carry. A rule with any other key is
        // skipped: a misspelt or unsupported condition read as no condition
        // would match every spell the others allow.
        constexpr std::string_view kMatchKeys[] = {
            "spell", "pluginContains", "castByVampires", "noElement",
            "spellKeyword", "spellKeywordPrefix", "spellKeywordSuffix",
            "mgefKeyword", "mgefKeywordPrefix", "mgefKeywordSuffix",
            "archetype", "primaryAV", "secondaryAV", "resistance", "magicSkill",
            "hostile", "detrimental", "summonedAny", "summonedNone", "targetRequires",
        };

        // The first key of a match object the grammar does not know, or empty
        std::string UnknownMatchKey(const json& matchObject)
        {
            if (!matchObject.is_object()) return {};
            for (const auto& [key, value] : matchObject.items()) {
                if (key.starts_with('_')) continue;
                if (std::find(std::begin(kMatchKeys), std::end(kMatchKeys), key) == std::end(kMatchKeys)) return key;
            }
            return {};
        }

        RuleMatch ParseMatch(const json& matchObject)
        {
            RuleMatch match;
            if (!matchObject.is_object()) {
                return match;
            }

            // "spell": one persistentId or a list of them
            ReadStringList(matchObject, "spell", match.spells);
            // "pluginContains": one text or a list of them, any of which will do
            const auto pluginField = matchObject.find("pluginContains");
            if (pluginField != matchObject.end()) {
                if (pluginField->is_string()) {
                    match.pluginContains.push_back(Detail::Lowered(pluginField->get<std::string>()));
                } else if (pluginField->is_array()) {
                    for (const auto& entry : *pluginField) {
                        if (entry.is_string()) match.pluginContains.push_back(Detail::Lowered(entry.get<std::string>()));
                    }
                }
            }
            ReadBool(matchObject, "castByVampires", match.castByVampires);
            std::optional<bool> noElement;
            ReadBool(matchObject, "noElement", noElement);
            match.noElement = noElement.value_or(false);
            ReadString(matchObject, "spellKeyword", match.spellKeyword);
            ReadString(matchObject, "spellKeywordPrefix", match.spellKeywordPrefix);
            ReadString(matchObject, "spellKeywordSuffix", match.spellKeywordSuffix);
            ReadString(matchObject, "mgefKeyword", match.mgefKeyword);
            ReadString(matchObject, "mgefKeywordPrefix", match.mgefKeywordPrefix);
            ReadString(matchObject, "mgefKeywordSuffix", match.mgefKeywordSuffix);
            ReadString(matchObject, "archetype", match.archetype);
            ReadString(matchObject, "primaryAV", match.primaryAV);
            ReadString(matchObject, "secondaryAV", match.secondaryAV);
            ReadString(matchObject, "resistance", match.resistance);
            ReadString(matchObject, "magicSkill", match.magicSkill);
            ReadBool(matchObject, "hostile", match.hostile);
            ReadStringList(matchObject, "summonedAny", match.summonedAny);
            ReadStringList(matchObject, "summonedNone", match.summonedNone);
            ReadStringList(matchObject, "targetRequires", match.targetRequires);
            ReadBool(matchObject, "detrimental", match.detrimental);

            return match;
        }

        // One rule object. Returns false when it would never do anything, so
        // the caller can count it as skipped instead of carrying dead weight.
        bool ParseRule(const json& ruleObject, const std::string& originFile,
            std::size_t index, const std::string& defaultSource, Rule& target,
            std::size_t& rejectedTags)
        {
            if (!ruleObject.is_object()) {
                return false;
            }

            const auto matchField = ruleObject.find("match");
            if (matchField == ruleObject.end()) {
                return false;
            }

            const std::string unknown = UnknownMatchKey(*matchField);
            if (!unknown.empty()) {
                logger::warn("Librarian: '{}' rule {} matches on '{}', which rules cannot read - skipped",
                    originFile, index, unknown);
                return false;
            }
            target.match = ParseMatch(*matchField);
            if (target.match.Empty()) {
                return false;
            }

            const auto addField = ruleObject.find("add");
            if (addField != ruleObject.end() && addField->is_object()) {
                ReadTagList(*addField, "elements", "element", &IsElement, originFile,
                    target.addElements, rejectedTags);
                ReadTagList(*addField, "techniques", "technique", &IsTechnique, originFile,
                    target.addTechniques, rejectedTags);
            }

            const auto removeField = ruleObject.find("remove");
            if (removeField != ruleObject.end() && removeField->is_object()) {
                ReadTagList(*removeField, "elements", "element", &IsElement, originFile,
                    target.removeElements, rejectedTags);
                ReadTagList(*removeField, "techniques", "technique", &IsTechnique, originFile,
                    target.removeTechniques, rejectedTags);
            }

            if (target.addElements.empty() && target.addTechniques.empty() &&
                target.removeElements.empty() && target.removeTechniques.empty()) {
                return false;
            }
            // A noElement rule runs after every removal, so a "remove" there
            // would never take effect; better refused than silently ignored
            if (target.match.noElement &&
                (!target.removeElements.empty() || !target.removeTechniques.empty())) {
                logger::warn("Librarian: '{}' rule {} has noElement with remove - skipped", originFile, index);
                return false;
            }

            target.source = defaultSource;
            ReadString(ruleObject, "tier", target.source);

            target.originFile = originFile;
            target.originIndex = index;
            return true;
        }
    }

    // =========================================================================
    // MATCH SHAPE
    // =========================================================================

    // The keyword patch's adapter entries carry the same conditions a rule
    // does, so they read them through the same parser rather than a second one.
    RuleMatch ParseRuleMatch(const json& matchObject)
    {
        return ParseMatch(matchObject);
    }

    bool RuleMatch::HasEffectCondition() const
    {
        return !mgefKeyword.empty()
            || !mgefKeywordPrefix.empty()
            || !mgefKeywordSuffix.empty()
            || !archetype.empty()
            || !primaryAV.empty()
            || !secondaryAV.empty()
            || !resistance.empty()
            || !magicSkill.empty()
            || hostile.has_value()
            || detrimental.has_value()
            || !summonedAny.empty()
            || !summonedNone.empty()
            || !targetRequires.empty();
    }

    bool RuleMatch::Empty() const
    {
        return !HasEffectCondition()
            && spells.empty()
            && pluginContains.empty()
            && !castByVampires.has_value()
            && spellKeyword.empty()
            && spellKeywordPrefix.empty()
            && spellKeywordSuffix.empty();
    }

    // =========================================================================
    // LOADING
    // =========================================================================

    void AppendRules(const json& document, const std::string& originFile, RuleSet& target)
    {
        // A rule file is either a bare array of rules or an object with a
        // "rules" array, so it can carry a version or a comment alongside them.
        const json* ruleArray = nullptr;
        std::string defaultSource = SOURCE_MGEF;
        if (document.is_array()) {
            ruleArray = &document;
        } else if (document.is_object()) {
            const auto found = document.find("rules");
            if (found != document.end() && found->is_array()) {
                ruleArray = &(*found);
            }
            // A file of framework rules should not repeat its tier on every line.
            ReadString(document, "tier", defaultSource);
        }

        if (!ruleArray) {
            // The keyword patch keeps its adapter table in this same directory
            // and it is not a rule file. Anything else without rules is a
            // mistake worth mentioning.
            if (!document.is_object() || document.find("adapters") == document.end()) {
                logger::warn("Librarian: '{}' has no rules array - ignored", originFile);
            }
            return;
        }

        std::size_t index = 0;
        for (const auto& ruleObject : *ruleArray) {
            Rule rule;
            if (ParseRule(ruleObject, originFile, index, defaultSource, rule, target.rejectedTags)) {
                target.rules.push_back(std::move(rule));
            } else {
                ++target.skipped;
                logger::warn("Librarian: '{}' rule #{} has no conditions or no tags - skipped",
                    originFile, index);
            }
            ++index;
        }
    }

    RuleSet LoadRules(const std::filesystem::path& directory)
    {
        RuleSet ruleSet;

        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) {
            logger::info("Librarian: no rule directory at '{}' - no rules loaded", PathText::Utf8(directory));
            return ruleSet;
        }

        // File name order is the merge order: 00_mgef before 10_kit before
        // 90_user, so stronger evidence is applied first and later files can
        // only add to what earlier ones found.
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (entry.is_regular_file() && entry.path().extension() == RULE_FILE_EXTENSION) {
                paths.push_back(entry.path());
            }
        }
        std::sort(paths.begin(), paths.end());

        for (const auto& path : paths) {
            const std::string name = PathText::Utf8(path.filename());

            json document;
            if (!Detail::ReadJsonFile(path, document)) {
                logger::error("Librarian: could not read rule file '{}'", name);
                continue;
            }

            const std::size_t before = ruleSet.rules.size();
            AppendRules(document, name, ruleSet);
            ruleSet.files.push_back(name);

            logger::info("Librarian: loaded {} rules from '{}'",
                ruleSet.rules.size() - before, name);
        }

        logger::info("Librarian: {} rules from {} files ({} skipped, {} tags outside the vocabulary)",
            ruleSet.rules.size(), ruleSet.files.size(), ruleSet.skipped, ruleSet.rejectedTags);

        return ruleSet;
    }
}
