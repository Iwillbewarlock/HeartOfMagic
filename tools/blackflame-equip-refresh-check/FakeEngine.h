#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>
#include <utility>

namespace logger {
template<class... A> void info(const char*, A&&...) {}
template<class... A> void warn(const char*, A&&...) {}
}
namespace RE {
using FormID = std::uint32_t;
struct Actor { struct SlotTypes { static constexpr unsigned kTotal = 4; }; };
struct SpellItem {
    std::uint32_t id = 0x1234;
    bool copied = false;
    template<class T> T* As() { return static_cast<T*>(this); }
    std::uint32_t GetFormID() const { return id; }
};
struct Handle {
    unsigned value = 1;
    unsigned native_handle() const { return value; }
    bool operator==(const Handle&) const = default;
};
struct ActiveEffect;
struct MagicTarget;
struct Controller {
    ActiveEffect* effect = nullptr;
    bool hasArt = true;
    bool hasShader = false;
    int stops = 0;
    void* GetHitEffectArt() { return hasArt ? this : nullptr; }
    void* GetHitEffectShader() { return hasShader ? this : nullptr; }
    void Stop();
};
struct BSTempEffect { virtual ~BSTempEffect() = default; };
struct ReferenceEffect : BSTempEffect {
    Controller* controller = nullptr;
    bool ownController = false;
    bool finished = false;
    bool detachWorks = true;
    bool clearControllerOnDetach = true;
    bool removeHitListOnDetach = true;
    bool detachedAfterStop = false;
    int detaches = 0;
    template<class T> T* As() { return dynamic_cast<T*>(this); }
    void Detach();
};
struct ModelReferenceEffect : ReferenceEffect {
    SpellItem* artObject = nullptr;
    void* artObject3D = nullptr;
};
struct ShaderReferenceEffect : ReferenceEffect { SpellItem* effectData = nullptr; };
struct ActiveEffect {
    enum class Flag { kDispelled = 1, kInactive = 2 };
    struct Flags {
        unsigned bits = 0;
        bool any(Flag a, Flag b) const { return bits & (unsigned(a) | unsigned(b)); }
    } flags;
    SpellItem* spell = nullptr;
    MagicTarget* target = nullptr;
    Handle caster;
    Controller hitEffectController;
    std::vector<ReferenceEffect*>* hitEffects = nullptr;
    unsigned usUniqueID = 42;
    bool oldVisualPresent = true;
    int starts = 0;
};
struct MagicTarget {
    std::vector<ActiveEffect*> effects;
    auto* GetActiveEffectList() { return &effects; }
};
struct PlayerCharacter : MagicTarget {
    inline static PlayerCharacter* singleton = nullptr;
    bool knownSpell = false;
    struct RuntimeData { std::array<SpellItem*, Actor::SlotTypes::kTotal> selectedSpells{}; } runtimeData;
    RuntimeData& GetActorRuntimeData() { return runtimeData; }
    bool removeSucceeds = true;
    bool has3D = true;
    int removeCalls = 0;
    int addCalls = 0;
    static PlayerCharacter* GetSingleton() { return singleton; }
    MagicTarget* AsMagicTarget() { return this; }
    void* Get3D() { return has3D ? this : nullptr; }
    Handle GetHandle() { return {}; }
    bool HasSpell(SpellItem*) { return knownSpell; }
    bool RemoveSpell(SpellItem*) { ++removeCalls; return removeSucceeds; }
    bool AddSpell(SpellItem* spell) {
        ++addCalls;
        for (auto* e : effects) if (e && e->spell == spell) e->oldVisualPresent = false;
        return true;
    }
};
namespace BSContainer { enum class ForEachResult { kContinue }; }
struct ProcessLists {
    inline static ProcessLists* singleton = nullptr;
    std::vector<BSTempEffect*> temps;
    bool locked = false;
    bool mutationUnderLock = false;
    static ProcessLists* GetSingleton() { return singleton; }
    template<class F> void ForEachMagicTempEffect(F f) {
        locked = true;
        for (auto* t : temps) f(t);
        locked = false;
    }
};
inline void Controller::Stop() {
    ++stops;
    if (auto* lists = ProcessLists::singleton) {
        lists->mutationUnderLock |= lists->locked;
        std::erase_if(lists->temps, [&](BSTempEffect* t) {
            auto* r = dynamic_cast<ReferenceEffect*>(t);
            return r && r->controller == this;
        });
    }
}
inline void ReferenceEffect::Detach() {
    ++detaches;
    if (auto* lists = ProcessLists::singleton) lists->mutationUnderLock |= lists->locked;
    detachedAfterStop = controller && controller->stops > 0;
    if (!detachWorks) return;
    if (controller && controller->effect && controller->effect->hitEffects && removeHitListOnDetach)
        std::erase(*controller->effect->hitEffects, this);
    if (clearControllerOnDetach) controller = nullptr;
}
template<class T> struct NiPointer {
    T* ptr;
    explicit NiPointer(T* p) : ptr(p) {}
    T* operator->() const { return ptr; }
};
}
template<class T> T netimmerse_cast(RE::BSTempEffect* p) { return dynamic_cast<T>(p); }
namespace REL {
struct Version {
    int major, minor, patch, build;
    Version(int a, int b, int c, int d) : major(a), minor(b), patch(c), build(d) {}
    bool operator==(const Version&) const = default;
};
inline Version runtime(1, 6, 1170, 0);
struct Module {
    static Module& get() { static Module m; return m; }
    Version version() const { return runtime; }
};
struct ID { std::uint64_t value; explicit ID(std::uint64_t v) : value(v) {} };
inline std::uint64_t lastRelocation = 0;
template<class T> struct Relocation {
    explicit Relocation(ID id) { lastRelocation = id.value; }
    void operator()(RE::ActiveEffect* effect) const {
        ++effect->starts;
        effect->oldVisualPresent = false;
    }
};
}
namespace BlackFlame {
struct Config { bool copyHitArt = true; bool hitArt = false; bool burnShader = false; };
inline Config testConfig;
inline bool stoked = true;
inline bool onGameThread = true;
inline unsigned testGeneration = 0;
inline const Config& GetConfig() { return testConfig; }
inline std::vector<RE::FormID> visualSpells;
inline std::vector<std::pair<RE::FormID, std::vector<RE::FormID>>> abilityOwners;
inline const std::vector<RE::FormID>& VisualEquipOwners(const RE::SpellItem* spell) {
    static const std::vector<RE::FormID> empty;
    if (spell) for (const auto& entry : abilityOwners) if (entry.first == spell->id) return entry.second;
    return empty;
}
inline bool IsVisualEquipAbility(const RE::SpellItem* spell) { return !VisualEquipOwners(spell).empty(); }
inline bool IsVisualSpell(const RE::SpellItem* spell) {
    return spell && std::find(visualSpells.begin(), visualSpells.end(), spell->id) != visualSpells.end();
}
inline bool IsCopy(const RE::SpellItem* form) { return form && form->copied; }
struct Forms { RE::SpellItem* burnShader = nullptr; };
inline Forms forms;
inline const Forms& GetForms() { return forms; }
bool IsPlayerVisualEquipEffect(RE::ActiveEffect*);
void TickVisualEquipContext();
void ResetVisualEquipContext();
inline bool IsStoked() { return stoked; }
inline bool IsOnGameThread() { return onGameThread; }
inline unsigned WorldGeneration() { return testGeneration; }
namespace Patch {
inline bool hasIds = true;
inline bool HasIds(std::initializer_list<std::uint64_t>, const char*) { return hasIds; }
}
namespace MainQueue {
inline bool accepts = true;
inline bool installed = true;
inline std::vector<std::function<void()>> pending;
inline bool Installed() { return installed; }
inline bool Post(const char*, std::function<void()> f) {
    if (!accepts) return false;
    pending.push_back(std::move(f));
    return true;
}
inline void Drain() {
    auto tasks = std::move(pending);
    pending.clear();
    for (auto& task : tasks) task();
}
}
void RequestEquipAbilityRefresh(bool);
}
