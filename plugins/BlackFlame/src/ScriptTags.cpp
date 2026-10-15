#include "ScriptTags.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GameThread.h"
#include "HitEffectHook.h"
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
            // Set by a call on a VM thread: the calling script's VM handle, not judged yet. Judged on the
            // game thread (the ActiveEffect is read only there) when the tag is first asked about or at
            // the next tick: confirmed (0) or the tag dropped.
            RE::VMHandle pendingCaller = 0;
        };

        std::mutex g_mutex;
        std::atomic<std::size_t> g_count{ 0 };

        // Guarded by g_mutex; keyed by the handle's value. Heap-held like the other layers' lists.
        std::unordered_map<std::uint32_t, Tag>& Tags()
        {
            static auto* tags = std::make_unique<std::unordered_map<std::uint32_t, Tag>>().release();
            return *tags;
        }

        void Add(RE::TESObjectREFR* a_ref, bool a_placed, RE::VMHandle a_pendingCaller = 0)
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
            // A live tag in this world is not touched by a pending call: a confirmed one keeps its expiry (an
            // unjudged call must not extend it), a pending one keeps its first caller until that is judged
            const auto existing = tags.find(handle.native_handle());
            const bool live = existing != tags.end() && existing->second.generation == WorldGeneration() &&
                              existing->second.expires > now;
            if (live && a_pendingCaller != 0) {
                return;
            }
            const bool wasConfirmed = live && existing->second.pendingCaller == 0;
            auto& tag = tags[handle.native_handle()];
            if (!live) {
                tag = Tag{};  // an expired or old-world entry passes nothing on (a stale placed flag included)
            }
            tag.handle = handle;
            tag.expires = now + (a_placed ? kPlacedWindow : kSourceWindow);
            tag.placed = tag.placed || a_placed;
            tag.generation = WorldGeneration();
            tag.pendingCaller = wasConfirmed ? 0 : a_pendingCaller;
            g_count.store(tags.size());
        }

        // Game thread only: the caller's VM handle resolved through the handle policy (type and
        // availability checked first, as Object::Resolve does) to an ActiveEffect that is a fire effect the
        // player cast. Confirms the tag (true) or drops it.
        bool JudgePending(std::uint32_t a_value, RE::VMHandle a_caller)
        {
            bool confirmed = false;
            (void)Guarded("script tag judge", [&]() {
                auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
                if (policy && policy->HandleIsType(RE::ActiveEffect::VMTYPEID, a_caller) &&
                    policy->IsHandleObjectAvailable(a_caller)) {
                    confirmed = IsPlayerFireEffect(
                        static_cast<RE::ActiveEffect*>(policy->GetObjectForHandle(RE::ActiveEffect::VMTYPEID, a_caller)));
                }
            });
            std::lock_guard lock(g_mutex);
            const auto it = Tags().find(a_value);
            if (it == Tags().end() || it->second.pendingCaller != a_caller) {
                return false;  // changed meanwhile: this judgement is not about the tag as it is now
            }
            if (confirmed) {
                it->second.pendingCaller = 0;
            } else {
                Tags().erase(it);
                g_count.store(Tags().size());
            }
            return confirmed;
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

    void TagPending(RE::TESObjectREFR* a_ref, bool a_placed, RE::VMHandle a_caller)
    {
        Add(a_ref, a_placed, a_caller);
    }

    TagInfo TagInfoOf(const RE::TESObjectREFR* a_ref)
    {
        if (!a_ref || g_count.load() == 0) {
            return {};
        }
        const auto value = const_cast<RE::TESObjectREFR*>(a_ref)->GetHandle().native_handle();
        std::lock_guard lock(g_mutex);
        const auto it = Tags().find(value);
        if (it == Tags().end() || it->second.expires <= Clock::now() || it->second.generation != WorldGeneration()) {
            return {};
        }
        const auto caller = it->second.pendingCaller;
        return { caller == 0 ? TagState::kConfirmed : TagState::kPending, caller };
    }

    TagState TagStateOf(const RE::TESObjectREFR* a_ref)
    {
        return TagInfoOf(a_ref).state;
    }

    bool IsTaggedSource(const RE::TESObjectREFR* a_ref)
    {
        return TagStateOf(a_ref) == TagState::kConfirmed;  // never judges: hooks call this
    }

    bool ResolvePending(const RE::TESObjectREFR* a_ref)
    {
        if (!a_ref || !IsOnGameThread()) {
            return IsTaggedSource(a_ref);
        }
        const auto value = const_cast<RE::TESObjectREFR*>(a_ref)->GetHandle().native_handle();
        RE::VMHandle pending = 0;
        {
            std::lock_guard lock(g_mutex);
            const auto it = Tags().find(value);
            if (it == Tags().end() || it->second.expires <= Clock::now() || it->second.generation != WorldGeneration()) {
                return false;
            }
            pending = it->second.pendingCaller;
        }
        return pending == 0 || JudgePending(value, pending);
    }

    RE::VMHandle PendingCallerOf(const RE::TESObjectREFR* a_ref)
    {
        if (!a_ref || g_count.load() == 0) {
            return 0;
        }
        const auto value = const_cast<RE::TESObjectREFR*>(a_ref)->GetHandle().native_handle();
        std::lock_guard lock(g_mutex);
        const auto it = Tags().find(value);
        if (it == Tags().end() || it->second.expires <= Clock::now() || it->second.generation != WorldGeneration()) {
            return 0;
        }
        return it->second.pendingCaller;
    }

    void LogDeferredOnce(const char* a_layer)
    {
        static std::mutex mutex;
        static auto* logged = std::make_unique<std::unordered_set<const char*>>().release();
        {
            std::lock_guard lock(mutex);
            if (!logged->insert(a_layer).second) {
                return;
            }
        }
        logger::info("BlackFlame: {}: decision deferred - only a pending script tag matched (judged by a task or the "
                     "ticker, never inside a hook)", a_layer);
    }

    void TickScriptTags()
    {
        if (g_count.load() == 0) {
            return;
        }
        const auto now = Clock::now();
        std::vector<RE::ObjectRefHandle> due;
        std::vector<std::pair<std::uint32_t, RE::VMHandle>> pending;
        {
            std::lock_guard lock(g_mutex);
            auto& tags = Tags();
            std::erase_if(tags, [&](const auto& a_entry) {
                return a_entry.second.expires <= now || a_entry.second.generation != WorldGeneration();
            });
            for (auto& [value, tag] : tags) {
                if (tag.pendingCaller != 0) {
                    pending.emplace_back(value, tag.pendingCaller);
                } else if (tag.placed) {
                    due.push_back(tag.handle);
                }
            }
            g_count.store(tags.size());
        }
        for (const auto& [value, caller] : pending) {
            (void)JudgePending(value, caller);  // a confirmed placed tag is recoloured on the next tick
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
