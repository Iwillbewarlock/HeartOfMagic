#include "EffectLight.h"
#include "TrackedMutex.h"

#include "Config.h"

#include <memory>
#include <mutex>
#include <unordered_set>

namespace BlackFlame
{
    RE::NiLight* AsNiLight(RE::NiRefObject* a_raw, const char*& a_kind)
    {
        a_kind = "neither BSLight nor NiLight";
        if (!a_raw) {
            a_kind = "null";
            return nullptr;
        }
        if (auto* bsLight = skyrim_cast<RE::BSLight*>(a_raw)) {
            a_kind = "BSLight";
            return bsLight->light.get();
        }
        if (auto* niLight = skyrim_cast<RE::NiLight*>(a_raw)) {
            a_kind = "NiLight";
            return niLight;
        }
        return nullptr;
    }

    RE::NiLight* CheckedLight(RE::NiRefObject* a_raw, const char* a_owner)
    {
        const char* kind = nullptr;
        auto* light = AsNiLight(a_raw, kind);
        if (a_raw) {
            static TrackedMutex mutex;
            static auto* logged = std::make_unique<std::unordered_set<const char*>>().release();
            bool first = false;
            {
                std::lock_guard lock(mutex);
                first = logged->insert(a_owner).second;
            }
            if (first) {
                logger::info("BlackFlame: {} light object 0x{:X} is a {}", a_owner, reinterpret_cast<std::uintptr_t>(a_raw),
                    kind);
            }
        }
        return light;
    }

    bool TintLight(RE::NiLight* a_light)
    {
        if (!a_light) {
            return false;
        }
        a_light->GetLightRuntimeData().diffuse = GetConfig().lightColor;
        return true;
    }
}
