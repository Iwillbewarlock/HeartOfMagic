#include "SelfTestProbe.h"

#include "Palette.h"

#include <atomic>
#include <cstring>
#include <mutex>

namespace BlackFlame::SelfTestProbe
{
    namespace
    {
        // A BSPSysSimpleColorModifier's three RGBA colour keys at 0x30, by the layout
        // ParticleColors.cpp assumes; read raw here so the self-test can judge that layout
        constexpr std::size_t kModifierKeysOffset = 0x30;
        constexpr std::size_t kRgbaFloats = 4;

        std::atomic<bool> g_recording{ false };
        std::mutex g_mutex;
        std::uint64_t g_nextSeq = 1;  // guarded by g_mutex
        std::vector<ProjectileRecord> g_projectiles;
        std::vector<ExplosionRecord> g_explosions;
        std::vector<BurnRecord> g_burns;

        RE::BSEffectShaderProperty* EffectPropertyOf(RE::NiAVObject* a_object)
        {
            if (auto* geometry = a_object->AsGeometry()) {
                return netimmerse_cast<RE::BSEffectShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
            }
            if (auto* legacy = netimmerse_cast<RE::NiGeometry*>(a_object)) {
                auto& data = legacy->GetRuntimeData();
                if (auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(data.spEffectState.get())) {
                    return prop;
                }
                return netimmerse_cast<RE::BSEffectShaderProperty*>(data.spPropertyState.get());
            }
            return nullptr;
        }

        template <class Visit>
        void ForEachObject(RE::NiAVObject* a_root, Visit&& a_visit)
        {
            if (!a_root) {
                return;
            }
            RE::BSVisit::TraverseScenegraphObjects(a_root, [&](RE::NiAVObject* a_object) {
                a_visit(a_object);
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }

        void AddProperty(RE::BSEffectShaderProperty* a_prop, Snapshot& a_out)
        {
            a_out.properties.push_back(reinterpret_cast<std::uintptr_t>(a_prop));
            a_out.materials.push_back(reinterpret_cast<std::uintptr_t>(a_prop->GetBaseMaterial()));
            const auto* material = a_prop->GetMaterial();
            a_out.baseColors.push_back(material ? material->baseColor : RE::NiColorA{});
            a_out.grey.push_back(HasGreyMaterial(a_prop));
        }

        void InspectModifiers(RE::NiParticleSystem* a_particles, Inspection& a_out)
        {
            static REL::Relocation<const RE::NiRTTI*> simpleColorRTTI{ RE::NiRTTI_BSPSysSimpleColorModifier };
            for (const auto& modifier : a_particles->GetParticleSystemRuntimeData().modifierList) {
                if (!modifier || modifier->GetRTTI() != simpleColorRTTI.get()) {
                    continue;
                }
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(modifier.get()) + kModifierKeysOffset;
                float keys[kColorKeyFloats];
                std::memcpy(keys, bytes, sizeof(keys));
                a_out.colorModifiers.push_back(reinterpret_cast<std::uintptr_t>(modifier.get()));
                bool grey = true;
                for (std::size_t i = 0; i < kColorKeyFloats; i += kRgbaFloats) {
                    grey = grey && keys[i] == keys[i + 1] && keys[i] == keys[i + 2];
                }
                if (grey) {
                    ++a_out.colorModifiersAllGrey;
                }
                if (a_out.firstModifierKeys.empty()) {
                    a_out.firstModifierKeys.assign(keys, keys + kColorKeyFloats);
                }
            }
        }

        template <class T>
        std::vector<T> Since(const std::vector<T>& a_records, std::uint64_t a_mark)
        {
            std::lock_guard lock(g_mutex);
            std::vector<T> out;
            for (const auto& r : a_records) {
                if (r.seq >= a_mark) {
                    out.push_back(r);
                }
            }
            return out;
        }

        template <class T>
        void Add(std::vector<T>& a_records, T&& a_record)
        {
            std::lock_guard lock(g_mutex);
            a_record.seq = g_nextSeq++;
            a_records.push_back(std::move(a_record));
        }
    }

    bool Recording() noexcept
    {
        return g_recording.load(std::memory_order_acquire);
    }

    void SetRecording(bool a_on) noexcept
    {
        g_recording.store(a_on, std::memory_order_release);
    }

    std::uint64_t Mark() noexcept
    {
        std::lock_guard lock(g_mutex);
        return g_nextSeq;
    }

    void Clear()
    {
        std::lock_guard lock(g_mutex);
        g_projectiles.clear();
        g_explosions.clear();
        g_burns.clear();
    }

    Snapshot Capture(RE::NiAVObject* a_root)
    {
        Snapshot out;
        ForEachObject(a_root, [&](RE::NiAVObject* a_object) {
            if (auto* prop = EffectPropertyOf(a_object)) {
                AddProperty(prop, out);
            }
        });
        return out;
    }

    Inspection Inspect(RE::NiAVObject* a_root)
    {
        Inspection out;
        const auto* palette = GetPalette();
        ForEachObject(a_root, [&](RE::NiAVObject* a_object) {
            auto* prop = EffectPropertyOf(a_object);
            if (!prop) {
                return;
            }
            ++out.effectProperties;
            AddProperty(prop, out.pointers);
            if (const auto* material = prop->GetMaterial()) {
                const bool path = IsPalettePath(material->greyscaleTexturePath);
                const bool pointer = palette && material->greyscaleTexture.get() == palette;
                out.withPalettePath += path ? 1 : 0;
                out.withPalettePointer += pointer ? 1 : 0;
                out.withBoth += (path && pointer) ? 1 : 0;
                out.withGrey += out.pointers.grey.back() ? 1 : 0;
            }
            if (auto* particles = netimmerse_cast<RE::NiParticleSystem*>(a_object)) {
                InspectModifiers(particles, out);
            }
        });
        return out;
    }

    void RecordProjectile(ProjectileRecord a_record)
    {
        Add(g_projectiles, std::move(a_record));
    }

    void RecordExplosion(ExplosionRecord a_record)
    {
        Add(g_explosions, std::move(a_record));
    }

    void RecordBurn(BurnRecord a_record)
    {
        Add(g_burns, std::move(a_record));
    }

    std::vector<ProjectileRecord> ProjectilesSince(std::uint64_t a_mark)
    {
        return Since(g_projectiles, a_mark);
    }

    std::vector<ExplosionRecord> ExplosionsSince(std::uint64_t a_mark)
    {
        return Since(g_explosions, a_mark);
    }

    std::vector<BurnRecord> BurnsSince(std::uint64_t a_mark)
    {
        return Since(g_burns, a_mark);
    }
}
