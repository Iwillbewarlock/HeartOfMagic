#pragma once

// =============================================================================
// SLOT TXST RULE - the textureFileIDs rule from the engine's own IDs, slot by slot (no game types)
// =============================================================================
// SlotTxst::MakeRule compares each non-empty slot of every assigned TXST source with the ID the engine stored for it.
// A slot votes for a form only when its stored ID is non-zero and equals that form's ID: "prefixed" is GenerateFromPath of
// DataPathRule::Normalize(name, "textures\") (leading data\ / textures\ folded, so a rooted record string votes the way
// a copy string is hashed), "bare" is GenerateFromPath(name) where the normalised form does not match. A slot both match (a
// rooted 'textures\x.dds' hashes alike either way) agrees with either and does not vote. Every other slot is
// unexplained and does not vote: zero (run 3: e.g. PSI.esp's 'cubemap\shinydull_e.dds', a missing file, by the counts) or
// another file (run 3: a 'black.dds' TX03 no record has, holding the ID an earlier override's TX03 had - seen for 3).
// The rule is the one form with votes when the other has none; NONE (no TXST slot written this session) when both or
// neither have votes. No floor above one vote and no zero rule: a 12-byte ID equal to a hash
// is the engine's hashing, and "every ID is zero" says nothing about how an existing copy file is hashed.
// C++17: blackflame-cosave-check builds it.

#include <cstddef>
#include <string>
#include <vector>

namespace BlackFlame::SlotTxstRule
{
    enum class Rule
    {
        kPrefixed,  // GenerateFromPath(Normalize(name, "textures\"))
        kBare,      // GenerateFromPath(name)
        kNone,      // no form fits: no TXST slot is written this session
    };

    /// One non-empty source slot against the ID the engine stored for it
    struct SlotEvidence
    {
        bool storedZero = false;
        bool shapeSkipped = false;       // Normalize refused the name (no prefixed form)
        bool fault = false;              // a GenerateFromPath call faulted
        bool matchesNormalized = false;  // stored == GenerateFromPath(Normalize(name, "textures\"))
        bool matchesRaw = false;         // stored == GenerateFromPath(name)
    };

    enum class Kind
    {
        kPrefixedVote,
        kBareVote,
        kBoth,          // both forms match (a rooted name): agrees with either, no vote
        kZero,          // unexplained: stored zero
        kOtherFile,     // unexplained: stored non-zero, neither form
        kShapeSkipped,  // unexplained: Normalize refused the name
        kFault,
    };

    [[nodiscard]] inline Kind KindOf(const SlotEvidence& a_slot) noexcept
    {
        if (a_slot.fault) {
            return Kind::kFault;
        }
        if (a_slot.shapeSkipped) {
            return Kind::kShapeSkipped;
        }
        if (a_slot.storedZero) {
            return Kind::kZero;  // zero never votes, even if a form hashed to zero
        }
        if (a_slot.matchesNormalized && a_slot.matchesRaw) {
            return Kind::kBoth;
        }
        return a_slot.matchesNormalized ? Kind::kPrefixedVote : a_slot.matchesRaw ? Kind::kBareVote : Kind::kOtherFile;
    }

    [[nodiscard]] inline Rule Decide(std::size_t a_prefixedVotes, std::size_t a_bareVotes) noexcept
    {
        if (a_prefixedVotes > 0 && a_bareVotes == 0) {
            return Rule::kPrefixed;
        }
        return a_bareVotes > 0 && a_prefixedVotes == 0 ? Rule::kBare : Rule::kNone;
    }

    /// a_normalized (Normalize(a_name, a_root)) is a_root + a_name with only ASCII letters lowered: no root, data\, leading,
    /// doubled or '/' separator folded. Only then is the rule's ID of the name certainly the hash the engine would make, so
    /// only then may it replace a stored one (whether GenerateFromPath folds '/' or doubled separators is not known)
    [[nodiscard]] inline bool NormalizeKeepsShape(const std::string& a_name, const std::string& a_normalized, const std::string& a_root)
    {
        if (a_normalized.size() != a_root.size() + a_name.size() || a_normalized.compare(0, a_root.size(), a_root) != 0) {
            return false;
        }
        for (std::size_t i = 0; i < a_name.size(); ++i) {
            const char c = a_name[i];
            const char lowered = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
            if (a_normalized[a_root.size() + i] != lowered) {
                return false;
            }
        }
        return true;
    }

    /// An unchanged slot (its string is the source's): true when the copy takes the source's stored ID - it is zero, it
    /// is the rule's ID of the string, no rule ID could be made, or the rule's hash input is not the string's own shape
    /// (a_shapeKept false: NormalizeKeepsShape); false: the rule's ID replaces it (the stored one names another file, e.g.
    /// the stale _g behind run 3's 'black.dds', so the copy's name and ID agree)
    [[nodiscard]] inline bool KeepsStored(bool a_storedZero, bool a_ruleIdMade, bool a_storedIsRuleId, bool a_shapeKept) noexcept
    {
        return a_storedZero || !a_ruleIdMade || a_storedIsRuleId || !a_shapeKept;
    }

    /// The counts and the unexplained slots grouped by name, for the rule line
    class Tally
    {
    public:
        static constexpr std::size_t kNamesShown = 5;

        /// a_example: the first slot of this name, as the log shows it (source, slot, stored ID)
        void Add(const std::string& a_name, Kind a_kind, const std::string& a_example)
        {
            ++checked_;
            switch (a_kind) {
            case Kind::kPrefixedVote: ++prefixed_; return;
            case Kind::kBareVote: ++bare_; return;
            case Kind::kBoth: ++both_; return;
            case Kind::kShapeSkipped: ++skipped_; break;
            case Kind::kFault: ++faults_; break;
            default: break;
            }
            auto* group = Find(a_name);
            if (!group) {
                names_.push_back({ a_name, 0, 0, 0, a_example });
                group = &names_.back();
            }
            ++(a_kind == Kind::kZero ? group->zero : a_kind == Kind::kOtherFile ? group->other : group->rest);
        }

        [[nodiscard]] Rule Decide() const noexcept { return SlotTxstRule::Decide(prefixed_, bare_); }
        [[nodiscard]] std::size_t Checked() const noexcept { return checked_; }
        [[nodiscard]] std::size_t Prefixed() const noexcept { return prefixed_; }
        [[nodiscard]] std::size_t Bare() const noexcept { return bare_; }

        [[nodiscard]] std::size_t Unexplained() const noexcept { return checked_ - prefixed_ - bare_ - both_; }

        /// "<checked> slots: <p> vote ..., <b> vote name, <u> unexplained[: 'name' xN (a another file, z zero, s ...), ...]"
        [[nodiscard]] std::string Text() const
        {
            std::string out = std::to_string(checked_) + " slots checked: " + std::to_string(prefixed_) + " vote the name under textures\\, " +
                              std::to_string(bare_) + " vote name, " + std::to_string(both_) + " match both, " +
                              std::to_string(Unexplained()) + " unexplained";
            if (skipped_ || faults_) {
                out += " (" + std::to_string(skipped_) + " shape skipped, " + std::to_string(faults_) + " faults)";
            }
            for (std::size_t i = 0; i < names_.size() && i < kNamesShown; ++i) {
                const auto& group = names_[i];
                std::string kinds;
                const auto part = [&](std::size_t a_count, const char* a_what) {
                    if (a_count) {
                        kinds += (kinds.empty() ? "" : ", ") + std::to_string(a_count) + " " + a_what;
                    }
                };
                part(group.other, "another file");
                part(group.zero, "zero");
                part(group.rest, "skipped or faulted");
                out += (i == 0 ? ": '" : "; '") + group.name + "' x" + std::to_string(group.zero + group.other + group.rest) + " (" + kinds +
                       "; first " + group.example + ")";
            }
            if (names_.size() > kNamesShown) {
                out += "; " + std::to_string(names_.size() - kNamesShown) + " more names";
            }
            return out;
        }

    private:
        struct Group
        {
            std::string name;
            std::size_t zero;
            std::size_t other;
            std::size_t rest;
            std::string example;
        };

        [[nodiscard]] Group* Find(const std::string& a_name)
        {
            for (auto& group : names_) {
                if (group.name == a_name) {
                    return &group;
                }
            }
            return nullptr;
        }

        std::size_t checked_ = 0;
        std::size_t prefixed_ = 0;
        std::size_t bare_ = 0;
        std::size_t both_ = 0;
        std::size_t skipped_ = 0;
        std::size_t faults_ = 0;
        std::vector<Group> names_;  // O(names) lookup: a session has a few dozen TXST slots
    };
}
