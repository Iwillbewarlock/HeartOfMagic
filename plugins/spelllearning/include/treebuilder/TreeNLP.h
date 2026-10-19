#pragma once

#include "Common.h"

#include <nlohmann/json.hpp>
#include <unordered_map>

using json = nlohmann::json;

// =============================================================================
// TreeNLP — Core NLP algorithms for spell tree generation
//
// Implements TF-IDF vectorization, cosine similarity, character n-gram
// similarity, and fuzzy string matching. These are the building blocks
// used by the tree builder (Classic) and PreReqMaster scoring.
//
// =============================================================================

namespace TreeNLP
{
    // =========================================================================
    // TYPES
    // =========================================================================

    // Sparse TF-IDF vector: token → weight, with pre-computed L2 norm
    struct SparseVector {
        std::unordered_map<std::string, float> weights;
        float norm = 0.0f;
    };

    // Scored candidate result (for PRM and parent selection)
    struct ScoredCandidate {
        std::string nodeId;
        float       score = 0.0f;
    };

    // =========================================================================
    // TOKENIZATION
    // =========================================================================

    // Tokenize text: lowercase, strip non-alphanumeric, filter words <= 2 chars
    std::vector<std::string> Tokenize(const std::string& text);

    // Build combined text from spell JSON data (name 2x, desc, effects)
    std::string BuildSpellText(const json& spellData);

    // Build combined text with heavier name/effect weighting for theme discovery
    std::string BuildThemeText(const json& spellData);

    // Editor ids of the spell and of the effects it shows the player, split into
    // words ("WindBladeSpell" -> "Wind Blade Spell"). English on every load order.
    std::string BuildIdText(const json& spellData);

    // =========================================================================
    // TF-IDF
    // =========================================================================

    // Compute TF-IDF vectors for a set of documents (each doc = token list).
    // Returns sparse vectors with pre-computed L2 norms.
    std::vector<SparseVector> ComputeTfIdf(
        const std::vector<std::vector<std::string>>& documents);

    // =========================================================================
    // SIMILARITY METRICS
    // =========================================================================

    // Cosine similarity between two sparse TF-IDF vectors (0.0 to 1.0)
    float CosineSimilarity(const SparseVector& a, const SparseVector& b);

    // =========================================================================
    // THEME SCORING (replaced former spell_grouper.py::calculate_theme_score)
    // =========================================================================

    // Score how well a spell matches a theme using multiple fuzzy strategies.
    // Returns 0-100.
    int CalculateThemeScore(const json& spellData, const std::string& theme);

    // =========================================================================
    // STOP WORDS
    // =========================================================================

    // Check if a word is in the spell stop word list
    bool IsStopWord(const std::string& word);

    // =========================================================================
    // PRE-REQ MASTER SCORING (replaced former prereq_master_scorer.py)
    // =========================================================================

    // PRM scoring settings
    struct PRMSettings {
        float proximityBias = 0.5f;
        std::string poolSource = "nearby";
        float maxDistance = 5.0f;
    };

    // Score candidates against a spell for PRM lock assignment.
    // Returns top N candidates sorted by score (descending).
    std::vector<ScoredCandidate> ScorePRMCandidates(
        const json& spellData,
        const std::vector<json>& candidates,
        const PRMSettings& settings,
        int topN = 5);

    // Process a full PRM scoring request
    json ProcessPRMRequest(const json& request);
}
