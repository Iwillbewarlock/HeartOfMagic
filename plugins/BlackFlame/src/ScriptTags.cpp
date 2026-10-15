#include "ScriptTags.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GameThread.h"
#include "LogLimit.h"
#include "Recolor.h"
#include "SehGuard.h"

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr auto kPlacedWindow = std::chrono::seconds(5);  // a placed reference's 3D must show by then
        constexpr auto kSourceWindow = std::chrono::seconds(5);  // a caster's shots count this long
        constexpr std::size_t kMaxTags = 256;                    // full: the entry expiring first makes room

        struct Tag
        {
            RE::ObjectRefHandle handle;
            Clock::time_point expires{};
            bool placed = false;  // waiting for its 3D to be recoloured
            std::uint32_t generation = 0;
        };

        std::mutex g_mutex;
        std::atomic<std::size_t> g_count{ 0 };

        // Guarded by g_mutex; keyed by the handle's value. Heap-held like the other layers' lists.
        std::unordered_map<std::uint32_t, Tag>& Tags()
        {
            static auto* tags = std::make_unique<std::unordered_map<std::uint32_t, Tag>>().release();
            return *tags;
        }

        void Add(RE::TESObjectREFR* a_ref, bool a_placed)
        {
            // Never an actor: a script's Cast on an NPC would turn that NPC's own fire grey, a placed summon
            // would be recoloured whole (the player is the owner rule already)
            if (!a_ref || a_ref->As<RE::Actor>()) {
                return;
            }
            const auto handle = a_ref->GetHandle();
            const auto now = Clock::now();
            std::lock_guard lock(g_mutex);
            auto& tags = Tags();
            if (tags.size() >= kMaxTags) {
                std::erase_if(tags, [&](const auto& a_entry) { return a_entry.second.expires <= now; });
            }
            if (tags.size() >= kMaxTags && !tags.contains(handle.native_handle())) {
                const auto earliest = std::ranges::min_element(tags,
                    [](const auto& a_lhs, const auto& a_rhs) { return a_lhs.second.expires < a_rhs.second.expires; });
                tags.erase(earliest);
            }
            auto& tag = tags[handle.native_handle()];
            tag.handle = handle;
            tag.expires = now + (a_placed ? kPlacedWindow : kSourceWindow);
            tag.placed = tag.placed || a_placed;
            tag.generation = WorldGeneration();
            g_count.store(tags.size());
        }

        RE::NiLight* LightOf(RE::TESObjectREFR* a_ref)
        {
            if (auto* explosion = a_ref->AsExplosion()) {
                return CheckedLight(explosion->GetExplosionRuntimeData().light.get(), "placed explosion");
            }
            if (auto* hazard = a_ref->As<RE::Hazard>()) {
                return CheckedLight(hazard->GetHazardRuntimeData().light.get(), "placed hazard");
            }
            return nullptr;
        }

        // What a script of the player's fire effect placed is recoloured only when its base is fire by the
        // FireSet rules (a fire explosion or hazard), or a visual-only kind (activator, moveable static,
        // static, art object) whose effect shaders are the fire the effect placed. Anything else (an actor,
        // an item, a container, a door, a light...) is left and logged once per base type.
        bool IsRecolourable(RE::TESObjectREFR* a_ref)
        {
            const auto* base = a_ref->GetBaseObject();
            if (!base || a_ref->As<RE::Actor>()) {
                return false;
            }
            switch (base->GetFormType()) {
            case RE::FormType::Explosion:
                return IsFireExplosion(base->As<RE::BGSExplosion>());
            case RE::FormType::Hazard:
                return IsFireHazard(base->As<RE::BGSHazard>());
            case RE::FormType::Activator:
            case RE::FormType::MovableStatic:
            case RE::FormType::Static:
            case RE::FormType::ArtObject:
                return true;
            default:
                break;
            }
            static std::mutex mutex;
            static auto* logged = std::make_unique<std::unordered_set<RE::FormType>>().release();
            bool first = false;
            {
                std::lock_guard lock(mutex);
                first = logged->insert(base->GetFormType()).second;
            }
            if (first) {
                logger::info("BlackFlame: placed by a script of the player's fire effect: {:08X} (base {:08X}, form type "
                             "{}) is not a fire explosion, hazard or visual-only kind - left",
                    a_ref->GetFormID(), base->GetFormID(), static_cast<int>(base->GetFormType()));
            }
            return false;
        }

        void RecolorPlaced(RE::TESObjectREFR* a_ref)
        {
            if (!IsRecolourable(a_ref)) {
                return;
            }
            RecolorStats stats;
            bool tinted = false;
            (void)Guarded("script-placed recolour", [&]() {
                stats = RecolorTree(a_ref->Get3D());
                tinted = GetConfig().light && TintLight(LightOf(a_ref));
            });
            const auto* base = a_ref->GetBaseObject();
            const auto baseId = base ? base->GetFormID() : 0;
            int suppressed = 0;
            if (LogLimit::Allow("script-placed", baseId, suppressed)) {
                logger::info("BlackFlame: placed by a script of the player's fire effect: {:08X} (base {:08X}): {}; light "
                             "{}{}", a_ref->GetFormID(), baseId, stats.Describe(), tinted ? "tinted" : "none or left",
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }
    }

    void TagPlaced(RE::TESObjectREFR* a_ref)
    {
        Add(a_ref, true);
    }

    void TagSource(RE::TESObjectREFR* a_ref)
    {
        Add(a_ref, false);
    }

    bool IsTaggedSource(const RE::TESObjectREFR* a_ref)
    {
        if (!a_ref || g_count.load() == 0) {
            return false;
        }
        const auto handle = const_cast<RE::TESObjectREFR*>(a_ref)->GetHandle();
        std::lock_guard lock(g_mutex);
        const auto it = Tags().find(handle.native_handle());
        return it != Tags().end() && it->second.expires > Clock::now() && it->second.generation == WorldGeneration();
    }

    void TickScriptTags()
    {
        if (g_count.load() == 0) {
            return;
        }
        const auto now = Clock::now();
        std::vector<RE::ObjectRefHandle> due;
        {
            std::lock_guard lock(g_mutex);
            auto& tags = Tags();
            std::erase_if(tags, [&](const auto& a_entry) {
                return a_entry.second.expires <= now || a_entry.second.generation != WorldGeneration();
            });
            for (auto& [value, tag] : tags) {
                if (tag.placed) {
                    due.push_back(tag.handle);
                }
            }
            g_count.store(tags.size());
        }
        for (const auto& handle : due) {
            const auto ref = handle.get();
            if (!ref || !ref->Get3D() || !IsStoked()) {
                continue;
            }
            RecolorPlaced(ref.get());
            std::lock_guard lock(g_mutex);
            if (const auto it = Tags().find(handle.native_handle()); it != Tags().end()) {
                it->second.placed = false;  // done; it stays a source until it expires
            }
        }
    }

    bool HasScriptTags() noexcept
    {
        return g_count.load() > 0;
    }

    void ResetScriptTags()
    {
        std::lock_guard lock(g_mutex);
        Tags().clear();
        g_count.store(0);
    }
}
