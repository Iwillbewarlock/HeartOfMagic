#include "treebuilder/TreeBuilderInternal.h"
#include "SimdKernels.h"

#include <hwy/aligned_allocator.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

// TreeBuilderSimilarity - the similarity matrix the builders score parents with
// (text, name and effect similarity per spell pair) and the keyword bag it and
// the cross school bridges read. Split out of TreeBuilderCore.cpp (600-line limit).

// =============================================================================
// SIMILARITY MATRIX — Dense flat-array storage
// =============================================================================

float TreeBuilder::SimilarityMatrix::GetTextSim(const std::string& a, const std::string& b) const
{
    auto ia = formIdToIndex.find(a);
    auto ib = formIdToIndex.find(b);
    if (ia == formIdToIndex.end() || ib == formIdToIndex.end()) return 0.0f;
    return textSims[ia->second * n + ib->second];
}

float TreeBuilder::SimilarityMatrix::GetNameSim(const std::string& a, const std::string& b) const
{
    auto ia = formIdToIndex.find(a);
    auto ib = formIdToIndex.find(b);
    if (ia == formIdToIndex.end() || ib == formIdToIndex.end()) return 0.0f;
    return nameSims[ia->second * n + ib->second];
}

float TreeBuilder::SimilarityMatrix::GetEffectSim(const std::string& a, const std::string& b) const
{
    auto ia = formIdToIndex.find(a);
    auto ib = formIdToIndex.find(b);
    if (ia == formIdToIndex.end() || ib == formIdToIndex.end()) return 0.0f;
    return effectSims[ia->second * n + ib->second];
}

// Everything a spell can be called, as one sorted bag: its traits and the words
// of its editor ids. The school is left out - the callers compare within a
// school or across two, and either way it tells nothing apart.
std::vector<std::string> TreeBuilder::SpellKeywords(const json& spell,
                                                    const std::unordered_set<std::string>& modTags)
{
    std::vector<std::string> keywords;
    for (const auto& idWord : TreeNLP::Tokenize(TreeNLP::BuildIdText(spell))) {
        if (modTags.contains(idWord)) continue;
        const bool hasDigit = std::any_of(idWord.begin(), idWord.end(),
            [](unsigned char ch) { return std::isdigit(ch) != 0; });
        if (!hasDigit) keywords.push_back("word." + idWord);
    }
    if (const auto traits = spell.find("traits"); traits != spell.end() && traits->is_array()) {
        for (const auto& trait : *traits) {
            if (!trait.is_string()) continue;
            auto name = trait.get<std::string>();
            if (!name.starts_with("school.")) keywords.push_back(std::move(name));
        }
    }
    std::sort(keywords.begin(), keywords.end());
    keywords.erase(std::unique(keywords.begin(), keywords.end()), keywords.end());
    return keywords;
}

TreeBuilder::SimilarityMatrix TreeBuilder::ComputeSimilarityMatrix(const std::vector<json>& spells)
{
    SimilarityMatrix matrix;

    // Collect form IDs, names, and effect names (indexed by position)
    std::vector<std::string> formIds;
    std::vector<std::string> names;
    std::vector<std::vector<std::string>> effectNames;
    std::vector<std::vector<std::string>> tokenizedDocs;

    // Names and descriptions are translated; editor ids are not. On a Korean
    // load order the text below is Korean, which the tokenizer cannot split, so
    // without the ids two spells only ever looked alike by accident. The
    // author's prefix is taken off first or every spell of a mod would look
    // like every other.
    const auto modTags = FindModTags(spells);
    constexpr int kIdWordWeight = 2;  // same weight the name gets

    // Everything a spell can be called, as one bag of keywords: its traits and
    // the words of its editor ids. No choosing between them - "LUN_MoonTouch" is
    // a moon spell and a touch spell, and is close to both families for it.
    std::vector<std::vector<std::string>> keywordSets;

    for (const auto& s : spells) {
        auto fid = s.value("formId", std::string(""));
        if (fid.empty()) continue;

        size_t idx = formIds.size();
        formIds.push_back(fid);
        matrix.formIdToIndex[fid] = idx;
        // Name similarity compares spellings letter by letter, so it has to run
        // on the editor id where there is one: "Firebolt" and "Fireball" share
        // most of their trigrams, the translated names share none of it. On a
        // translated load order the names were giving it noise. The author's prefix goes, or every spell of a mod
        // would look like a near duplicate of every other.
        {
            auto spelling = s.value("editorId", std::string(""));
            if (spelling.empty()) {
                spelling = s.value("name", std::string(""));
            } else {
                const auto tag = LeadingIdWordOf(spelling);
                if (!tag.empty() && modTags.contains(tag) && spelling.size() > tag.size()) {
                    spelling.erase(0, tag.size());
                    while (!spelling.empty() && !std::isalnum(static_cast<unsigned char>(spelling.front()))) {
                        spelling.erase(0, 1);
                    }
                }
            }
            names.push_back(std::move(spelling));
        }

        // What the effects are called, for the trigram comparison below.
        //
        // Their names, not their editor ids, even though the names are
        // translated. The comparison is letter by letter, and effect ids are
        // built to a convention - "FireDamageFFAimed", "FrostDamageFFAimed" -
        // so most of the string is the delivery and the two elements agree on
        // nearly all of it. Tried it: the (since removed) Graph builder fell from 37% to 29%.
        // What the ids are good for is their words, and the keyword affinity
        // further down already reads those. On a translated load order this
        // score simply comes out near zero and the keywords carry it.
        //
        // Effects flagged Hide in UI are left out. One mod's script controller
        // sits on hundreds of unrelated spells, and this score is the best
        // matching pair of effects - one shared helper made any two of those
        // spells score a perfect match, on the signal the builder weighs
        // highest.
        std::vector<std::string> effs;
        if (s.contains("effects") && s["effects"].is_array()) {
            for (const auto& e : s["effects"]) {
                std::string ename;
                if (e.is_object()) {
                    const auto flags = e.find("flags");
                    if (flags != e.end() && flags->is_object() && flags->value("hideInUI", false)) continue;
                    if (e.contains("name") && e["name"].is_string())
                        ename = e["name"].get<std::string>();
                } else if (e.is_string()) {
                    ename = e.get<std::string>();
                }
                if (!ename.empty()) effs.push_back(std::move(ename));
            }
        }
        if (effs.empty() && s.contains("effectNames") && s["effectNames"].is_array()) {
            for (const auto& e : s["effectNames"]) {
                if (e.is_string()) {
                    auto en = e.get<std::string>();
                    if (!en.empty()) effs.push_back(std::move(en));
                }
            }
        }
        effectNames.push_back(std::move(effs));

        // Build text for TF-IDF
        json spellForText;
        spellForText["name"] = s.value("name", std::string(""));
        spellForText["desc"] = s.contains("description") ? s.value("description", std::string(""))
                                                          : s.value("desc", std::string(""));
        json effectsFlat = json::array();
        if (s.contains("effects") && s["effects"].is_array()) {
            for (const auto& e : s["effects"]) {
                if (e.is_string()) effectsFlat.push_back(e);
                else if (e.is_object() && e.contains("name")) effectsFlat.push_back(e["name"]);
            }
        }
        spellForText["effects"] = effectsFlat;

        auto text = TreeNLP::BuildSpellText(spellForText);
        auto tokens = TreeNLP::Tokenize(text);
        for (const auto& idWord : TreeNLP::Tokenize(TreeNLP::BuildIdText(s))) {
            if (modTags.contains(idWord)) continue;
            for (int i = 0; i < kIdWordWeight; ++i) tokens.push_back(idWord);
        }
        tokenizedDocs.push_back(std::move(tokens));
        keywordSets.push_back(SpellKeywords(s, modTags));
    }

    auto n = formIds.size();
    matrix.n = n;

    // Allocate flat similarity arrays (zero-initialized)
    matrix.textSims.assign(n * n, 0.0f);
    matrix.nameSims.assign(n * n, 0.0f);
    matrix.effectSims.assign(n * n, 0.0f);

    // =========================================================================
    // Text similarity: Dense TF-IDF + Highway SIMD dot product
    // =========================================================================
    {
        // Build vocabulary index and document frequencies
        std::unordered_map<std::string, uint32_t> vocab;
        std::unordered_map<std::string, int> df;

        for (const auto& doc : tokenizedDocs) {
            std::unordered_set<std::string> unique(doc.begin(), doc.end());
            for (const auto& token : unique) {
                if (!vocab.contains(token))
                    vocab[token] = static_cast<uint32_t>(vocab.size());
                df[token]++;
            }
        }

        const size_t vocabSize = vocab.size();
        const size_t paddedVocabSize = SimdKernels::PadToSimd(vocabSize);
        const auto nDocsF = static_cast<float>(n);

        // Compute IDF weights
        std::vector<float> idf(vocabSize);
        for (const auto& [token, idx] : vocab) {
            idf[idx] = std::log((nDocsF + 1.0f) / (static_cast<float>(df[token]) + 1.0f)) + 1.0f;
        }

        // Build dense TF-IDF matrix (aligned, zero-padded, L2-normalized rows)
        auto denseMatrix = hwy::AllocateAligned<float>(n * paddedVocabSize);
        HWY_ASSERT(denseMatrix);
        std::memset(denseMatrix.get(), 0, n * paddedVocabSize * sizeof(float));

        for (size_t d = 0; d < n; ++d) {
            if (tokenizedDocs[d].empty()) continue;

            float* row = denseMatrix.get() + d * paddedVocabSize;

            // Term frequency
            std::unordered_map<std::string, int> tf;
            for (const auto& token : tokenizedDocs[d])
                tf[token]++;

            float total = static_cast<float>(tokenizedDocs[d].size());
            float normSq = 0.0f;

            for (const auto& [token, count] : tf) {
                auto it = vocab.find(token);
                if (it == vocab.end()) continue;
                float w = (static_cast<float>(count) / total) * idf[it->second];
                row[it->second] = w;
                normSq += w * w;
            }

            // L2 normalize
            if (normSq > 0.0f) {
                float invNorm = 1.0f / std::sqrt(normSq);
                for (size_t vi = 0; vi < vocabSize; ++vi)
                    row[vi] *= invNorm;
            }
        }

        // Pairwise cosine similarity via Highway-dispatched dot product
        const auto nSigned = static_cast<int>(n);
        #pragma omp parallel for schedule(dynamic, 16)
        for (int i = 0; i < nSigned; ++i) {
            const float* row_i = denseMatrix.get() + i * paddedVocabSize;
            for (int j = i + 1; j < nSigned; ++j) {
                const float* row_j = denseMatrix.get() + j * paddedVocabSize;
                float sim = SimdKernels::DenseDotProduct(row_i, row_j, paddedVocabSize);
                matrix.textSims[i * nSigned + j] = sim;
                matrix.textSims[j * nSigned + i] = sim;
            }
        }
    }

    // =========================================================================
    // Name similarity: cached char trigram Jaccard (sorted vectors)
    // =========================================================================
    {
        // Pre-compute sorted trigram sets per spell name
        std::vector<std::vector<uint32_t>> cachedNameGrams(n);
        for (size_t i = 0; i < n; ++i) {
            if (names[i].empty()) continue;
            auto lower = TreeNLP::ToLower(names[i]);
            lower.erase(std::remove_if(lower.begin(), lower.end(),
                [](unsigned char c) { return std::isspace(c) != 0; }), lower.end());

            if (lower.size() >= 3) {
                std::unordered_set<uint32_t> seen;
                for (size_t k = 0; k + 3 <= lower.size(); ++k) {
                    uint32_t h = 0;
                    for (int b = 0; b < 3; ++b)
                        h = (h << 8) | static_cast<uint8_t>(lower[k + b]);
                    seen.insert(h);
                }
                cachedNameGrams[i].assign(seen.begin(), seen.end());
                std::sort(cachedNameGrams[i].begin(), cachedNameGrams[i].end());
            }
        }

        // Pairwise Jaccard using sorted set intersection
        const auto nSigned = static_cast<int>(n);
        #pragma omp parallel for schedule(dynamic, 16)
        for (int i = 0; i < nSigned; ++i) {
            if (cachedNameGrams[i].empty()) continue;
            for (int j = i + 1; j < nSigned; ++j) {
                if (cachedNameGrams[j].empty()) continue;

                std::vector<uint32_t> isect;
                std::set_intersection(
                    cachedNameGrams[i].begin(), cachedNameGrams[i].end(),
                    cachedNameGrams[j].begin(), cachedNameGrams[j].end(),
                    std::back_inserter(isect));

                auto unionSize = cachedNameGrams[i].size() + cachedNameGrams[j].size() - isect.size();
                float sim = (unionSize > 0)
                    ? static_cast<float>(isect.size()) / static_cast<float>(unionSize)
                    : 0.0f;
                matrix.nameSims[i * nSigned + j] = sim;
                matrix.nameSims[j * nSigned + i] = sim;
            }
        }
    }

    // =========================================================================
    // Effect similarity: cached n-gram sets (sorted vectors) for Jaccard
    // =========================================================================
    {
        // Pack n-gram bytes into uint32_t
        auto packNgram = [](const char* s, int len) -> uint32_t {
            uint32_t h = 0;
            for (int i = 0; i < len; ++i)
                h = (h << 8) | static_cast<uint8_t>(s[i]);
            return h;
        };

        // Pre-compute sorted trigram vectors per effect name per spell
        std::vector<std::vector<std::vector<uint32_t>>> cachedEffectGrams(n);
        for (size_t i = 0; i < n; ++i) {
            for (const auto& ename : effectNames[i]) {
                auto lower = TreeNLP::ToLower(ename);
                lower.erase(std::remove_if(lower.begin(), lower.end(),
                    [](unsigned char c) { return std::isspace(c) != 0; }), lower.end());

                std::vector<uint32_t> grams;
                if (static_cast<int>(lower.size()) >= 3) {
                    std::unordered_set<uint32_t> seen;
                    for (int k = 0; k <= static_cast<int>(lower.size()) - 3; ++k)
                        seen.insert(packNgram(lower.data() + k, 3));
                    grams.assign(seen.begin(), seen.end());
                    std::sort(grams.begin(), grams.end());
                }
                cachedEffectGrams[i].push_back(std::move(grams));
            }
        }

        // Pairwise effect-name affinity using sorted set intersection
        const auto nSigned = static_cast<int>(n);
        #pragma omp parallel for schedule(dynamic, 16)
        for (int i = 0; i < nSigned; ++i) {
            if (cachedEffectGrams[i].empty()) continue;
            for (int j = i + 1; j < nSigned; ++j) {
                if (cachedEffectGrams[j].empty()) continue;

                float bestSim = 0.0f;
                for (const auto& gramsA : cachedEffectGrams[i]) {
                    if (gramsA.empty()) continue;
                    for (const auto& gramsB : cachedEffectGrams[j]) {
                        if (gramsB.empty()) continue;

                        std::vector<uint32_t> isect;
                        std::set_intersection(
                            gramsA.begin(), gramsA.end(),
                            gramsB.begin(), gramsB.end(),
                            std::back_inserter(isect));

                        auto unionSize = gramsA.size() + gramsB.size() - isect.size();
                        float sim = (unionSize > 0)
                            ? static_cast<float>(isect.size()) / static_cast<float>(unionSize)
                            : 0.0f;
                        bestSim = std::max(bestSim, sim);
                    }
                }
                matrix.effectSims[i * nSigned + j] = bestSim;
                matrix.effectSims[j * nSigned + i] = bestSim;
            }
        }
    }

    // =========================================================================
    // Keyword affinity: shared keywords, the rare ones counting for more
    // =========================================================================
    //
    // Effect names above are translated text, compared byte by byte. The
    // keywords are not: traits come from engine values, id words are English
    // everywhere. Two spells are alike by the keywords they share, weighed by
    // how few spells carry each one - sharing "form.projectile" with eight
    // hundred others says little, sharing "word.moon" with thirteen says a lot.
    // The weights come from the data (inverse document frequency), not a list.
    //
    // It goes into the effect affinity, the signal the builder already weighs
    // highest, as the better of the two: without a full scan there are no
    // keywords and the effect names decide alone, as before.
    {
        std::unordered_map<std::string, int> keywordIds;
        std::vector<int> spellsWithKeyword;
        for (const auto& set : keywordSets) {
            for (const auto& keyword : set) {
                const auto [it, isNew] = keywordIds.try_emplace(keyword, static_cast<int>(keywordIds.size()));
                if (isNew) spellsWithKeyword.push_back(0);
                spellsWithKeyword[it->second]++;
            }
        }

        std::vector<float> weights(spellsWithKeyword.size(), 0.0f);
        for (size_t k = 0; k < weights.size(); ++k) {
            weights[k] = std::log(static_cast<float>(n + 1) / static_cast<float>(spellsWithKeyword[k] + 1));
        }

        // Sorted id lists; a keyword only one spell has cannot be shared, and
        // left in it would only water down that spell's every comparison.
        std::vector<std::vector<int>> ids(n);
        std::vector<float> totalWeight(n, 0.0f);
        for (size_t i = 0; i < n && i < keywordSets.size(); ++i) {
            for (const auto& keyword : keywordSets[i]) {
                const int id = keywordIds[keyword];
                if (spellsWithKeyword[id] < 2) continue;
                ids[i].push_back(id);
                totalWeight[i] += weights[id];
            }
            std::sort(ids[i].begin(), ids[i].end());
        }

        const auto nSigned = static_cast<int>(n);
        #pragma omp parallel for schedule(dynamic, 16)
        for (int i = 0; i < nSigned; ++i) {
            if (ids[i].empty()) continue;
            for (int j = i + 1; j < nSigned; ++j) {
                if (ids[j].empty()) continue;

                std::vector<int> shared;
                std::set_intersection(ids[i].begin(), ids[i].end(), ids[j].begin(), ids[j].end(),
                    std::back_inserter(shared));
                if (shared.empty()) continue;

                float sharedWeight = 0.0f;
                for (const int id : shared) sharedWeight += weights[id];
                const float unionWeight = totalWeight[i] + totalWeight[j] - sharedWeight;
                const float affinity = (unionWeight > 0.0f) ? sharedWeight / unionWeight : 0.0f;

                float& cell = matrix.effectSims[i * nSigned + j];
                if (affinity > cell) {
                    cell = affinity;
                    matrix.effectSims[j * nSigned + i] = affinity;
                }
            }
        }
    }

    return matrix;
}
