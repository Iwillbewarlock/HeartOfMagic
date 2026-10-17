// =============================================================================
// FORM COPIES NAMES - a form pointer named for the copies' diff texts (FormCopies.h, CopyFill)
// =============================================================================
// W12: split from FormCopiesFill.cpp. D3: a pointer is compared with the form map's values, never dereferenced.

#include "FormCopies.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BlackFlame::CopyFill
{
    namespace
    {
        // Set once at kDataLoaded before any Compare reads it (FormCopies.cpp LoadAndFillCopies), read-only afterwards
        std::unordered_map<const RE::TESForm*, RE::FormID>& KnownForms()
        {
            static auto* known = std::make_unique<std::unordered_map<const RE::TESForm*, RE::FormID>>().release();
            return *known;
        }

        thread_local PointerNameScope* t_scope = nullptr;

        // W12: one reverse map of the game's form map per load, from the second unknown pointer on
        constexpr int kMissesBeforeMap = 2;  // the first unknown pointer is found by a walk, the second builds the map

        struct PointerNames
        {
            std::mutex mutex;
            int scopes = 0;   // PointerNameScopes alive on any thread
            int misses = 0;
            bool built = false;
            std::unordered_map<const RE::TESForm*, RE::FormID> forms;
        };

        PointerNames& Names()
        {
            static auto* names = std::make_unique<PointerNames>().release();
            return *names;
        }

        // D3: the game's form map walked under its read lock, comparing pointer values only
        template <class Visit>
        void WalkForms(Visit&& a_visit)
        {
            auto [map, lock] = RE::TESForm::GetAllForms();
            if (!map) {
                return;
            }
            const RE::BSReadLockGuard guard(lock.get());
            for (const auto& [id, form] : *map) {
                if (!a_visit(id, form)) {
                    return;
                }
            }
        }
    }

    void SetKnownForms(std::unordered_map<const RE::TESForm*, RE::FormID> a_known)
    {
        KnownForms() = std::move(a_known);
    }

    PointerNameScope::PointerNameScope() :
        m_outer(t_scope)
    {
        t_scope = this;
        auto& names = Names();
        std::lock_guard lock(names.mutex);
        ++names.scopes;
    }

    PointerNameScope::~PointerNameScope()
    {
        t_scope = m_outer;
        // X16: the reverse map (every form of the game: tens of MB) lives only while a scope does; the last one frees it
        auto& names = Names();
        std::lock_guard lock(names.mutex);
        if (--names.scopes == 0) {
            names.misses = 0;
            names.built = false;
            std::unordered_map<const RE::TESForm*, RE::FormID>().swap(names.forms);
        }
    }

    // D3: a form pointer named without dereferencing it. First our own sources and copies (most links point there), then the
    // game's form map by value (LookupByID would need the id, which only the dereference gives): one reverse map per
    // PointerNameScope, or a full walk per pointer without one (measured: 7-34 ms a field at a load with 157 differing copies)
    std::string NamePointer(const RE::TESForm* a_form)
    {
        if (!a_form) {
            return "null";
        }
        if (const auto it = KnownForms().find(a_form); it != KnownForms().end()) {
            return std::format("{:08X}", it->second);
        }
        const auto start = std::chrono::steady_clock::now();
        std::optional<RE::FormID> id;
        bool walk = false;
        {
            auto& names = Names();
            std::lock_guard lock(names.mutex);
            if (!names.built && t_scope && ++names.misses >= kMissesBeforeMap) {  // Y12: only inside a scope (else the walk)
                // Copied under the form map's read lock (no allocation per pair there), the map built after unlocking
                std::vector<std::pair<const RE::TESForm*, RE::FormID>> pairs;
                {
                    auto [map, mapLock] = RE::TESForm::GetAllForms();
                    if (map) {
                        const RE::BSReadLockGuard guard(mapLock.get());
                        pairs.reserve(map->size());
                        for (const auto& [formId, form] : *map) {
                            pairs.emplace_back(form, formId);
                        }
                    }
                }
                names.forms.reserve(pairs.size());
                names.forms.insert(pairs.begin(), pairs.end());
                names.built = true;
                t_scope->m_built = true;  // Z16: inside a scope only (Y12)
            }
            if (names.built) {
                if (const auto it = names.forms.find(a_form); it != names.forms.end()) {
                    id = it->second;
                }
            }
            walk = !names.built;  // X16: read under the lock
        }
        if (!id && walk) {
            WalkForms([&](RE::FormID a_id, const RE::TESForm* a_entry) {  // the first miss: a walk that stops at it
                if (a_entry == a_form) {
                    id = a_id;
                    return false;
                }
                return true;
            });
        }
        if (auto* scope = t_scope) {
            scope->m_micros +=
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        }
        return id ? std::format("{:08X}", *id) :
                    std::format("pointer {:X} (not a form in the map)", reinterpret_cast<std::uintptr_t>(a_form));
    }
}
