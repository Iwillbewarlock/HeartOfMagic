#include "SelfTestProbe.h"

#include "Palette.h"

#include <atomic>
#include <cstring>
#include <mutex>

namespace BlackFlame::SelfTestProbe
{
    namespace
    {
        // Bytes 0x30-0x7F of a BSPSysSimpleColorModifier: three NiColorA keys at 0x30 by the
        // layout ParticleColors.cpp assumes; read raw here so the dump checks that layout
        constexpr std::size_t kModifierDumpOffset = 0x30;
        constexpr std::size_t kModifierDumpFloats = (0x80 - 0x30) / sizeof(float);
        constexpr std::size_t kColorKeyFloats = 12;  // 3 keys x RGBA
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

        void InspectModifiers(RE::NiParticleSystem* a_particles, Inspection& a_out)
        {
            static REL::Relocation<const RE::NiRTTI*> simpleColorRTTI{ RE::NiRTTI_BSPSysSimpleColorModifier };
            for (const auto& modifier : a_particles->GetParticleSystemRuntimeData().modifierList) {
                if (!modifier || modifier->GetRTTI() != simpleColorRTTI.get()) {
                    continue;
                }
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(modifier.get()) + kModifierDumpOffset;
                float raw[kModifierDumpFloats];
                std::memcpy(raw, bytes, sizeof(raw));
                a_out.colorModifiers.push_back(reinterpret_cast<std::uintptr_t>(modifier.get()));
                bool grey = true;
                for (std::size_t i = 0; i < kColorKeyFloats; i += kRgbaFloats) {
                    grey = grey && raw[i] == raw[i + 1] && raw[i] == raw[i + 2];
                }
                if (grey) {
                    ++a_out.colorModifiersAllGrey;
                }
                if (a_out.firstModifierRaw.empty()) {
                    a_out.firstModifierRaw.assign(raw, raw + kModifierDumpFloats);
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

    Snapshot Capture(RE::NiAVObject* a_root)
    {
        Snapshot out;
        ForEachObject(a_root, [&](RE::NiAVObject* a_object) {
            if (auto* prop = EffectPropertyOf(a_object)) {
                out.properties.push_back(reinterpret_cast<std::uintptr_t>(prop));
                out.materials.push_back(reinterpret_cast<std::uintptr_t>(prop->GetBaseMaterial()));
            }
        });
        return out;
    }

    Inspection Inspect(RE::NiAVObject* a_root)
    {
        Inspection out;
        const auto* palette = GetPalette();
        const char* palettePath = GetPalettePath();
        ForEachObject(a_root, [&](RE::NiAVObject* a_object) {
            auto* prop = EffectPropertyOf(a_object);
            if (!prop) {
                return;
            }
            ++out.effectProperties;
            out.pointers.properties.push_back(reinterpret_cast<std::uintptr_t>(prop));
            out.pointers.materials.push_back(reinterpret_cast<std::uintptr_t>(prop->GetBaseMaterial()));
            if (const auto* material = prop->GetMaterial()) {
                const bool path = palettePath && material->greyscaleTexturePath == palettePath;
                const bool pointer = palette && material->greyscaleTexture.get() == palette;
                out.withPalettePath += path ? 1 : 0;
                out.withPalettePointer += pointer ? 1 : 0;
                out.withBoth += (path && pointer) ? 1 : 0;
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
