#include "SelfTestProbe.h"
#include "Watchdog.h"
#include "TrackedMutex.h"

#include "Palette.h"
#include "ParticleColors.h"
#include "SehGuard.h"

#include <atomic>
#include <mutex>

namespace BlackFlame::SelfTestProbe
{
    namespace
    {
        constexpr std::size_t kRgbaFloats = 4;

        std::atomic<bool> g_recording{ false };
        TrackedMutex g_mutex;
        std::uint64_t g_nextSeq = 1;  // guarded by g_mutex
        std::vector<ProjectileRecord> g_projectiles;
        std::vector<ExplosionRecord> g_explosions;
        std::vector<BurnRecord> g_burns;
        std::vector<LaunchRecord> g_launches;
        std::vector<ExplosionCreateRecord> g_explosionCreates;
        std::vector<HitRecord> g_hits;
        float g_pausedSeconds = 0.0f;  // guarded by g_mutex

        void AddProperty(RE::BSEffectShaderProperty* a_prop, Snapshot& a_out)
        {
            a_out.properties.push_back(reinterpret_cast<std::uintptr_t>(a_prop));
            a_out.materials.push_back(reinterpret_cast<std::uintptr_t>(a_prop->GetBaseMaterial()));
            const auto* material = a_prop->GetMaterial();
            a_out.baseColors.push_back(material ? material->baseColor : RE::NiColorA{});
            a_out.grey.push_back(HasGreyMaterial(a_prop));
            a_out.colorControlled.push_back(HasColorController(a_prop));
        }

        void InspectModifiers(RE::NiParticleSystem* a_particles, Inspection& a_out)
        {
            ForEachSimpleColorModifier(a_particles, [&](RE::NiPSysModifier* a_modifier, const ColorKeys& keys) {
                a_out.colorModifiers.push_back(reinterpret_cast<std::uintptr_t>(a_modifier));
                a_out.modifierKeys.push_back(keys);
                bool grey = true;
                for (std::size_t i = 0; i < kColorKeyFloats; i += kRgbaFloats) {
                    grey = grey && keys[i] == keys[i + 1] && keys[i] == keys[i + 2];
                }
                if (grey) {
                    ++a_out.colorModifiersAllGrey;
                }
            });
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
        Watchdog::SetFlushEveryLine(a_on);  // C6a: during the self-test every info line reaches the file at once
        g_recording.store(a_on, std::memory_order_release);
    }

    std::uint64_t Mark()
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
        g_launches.clear();
        g_explosionCreates.clear();
        g_hits.clear();
        g_pausedSeconds = 0.0f;
    }

    void AddPausedSeconds(float a_seconds)
    {
        std::lock_guard lock(g_mutex);
        g_pausedSeconds += a_seconds;
    }

    float PausedSeconds()
    {
        std::lock_guard lock(g_mutex);
        return g_pausedSeconds;
    }

    Snapshot Capture(RE::NiAVObject* a_root)
    {
        Snapshot out;
        // Guarded like the layers: a fault leaves what was read so far
        (void)Guarded("self-test capture", [&]() {
        ForEachEffectProperty(a_root, [&](RE::BSEffectShaderProperty* a_prop, RE::BSGeometry* a_geometry, bool) {
            AddProperty(a_prop, out);
            // The colour keys as they were before any recolour (the self-test's T7 compares with them)
            if (auto* particles = a_geometry ? netimmerse_cast<RE::NiParticleSystem*>(a_geometry) : nullptr) {
                ForEachSimpleColorModifier(particles, [&](RE::NiPSysModifier*, const ColorKeys& a_keys) {
                    out.modifierKeys.push_back(a_keys);
                });
            }
        });
        });
        return out;
    }

    Inspection Inspect(RE::NiAVObject* a_root)
    {
        Inspection out;
        const auto* palette = GetPalette();
        (void)Guarded("self-test inspect", [&]() {
        ForEachEffectProperty(a_root, [&](RE::BSEffectShaderProperty* a_prop, RE::BSGeometry* a_geometry, bool) {
            ++out.effectProperties;
            AddProperty(a_prop, out.pointers);
            if (const auto* material = a_prop->GetMaterial()) {
                const bool path = IsPalettePath(material->greyscaleTexturePath);
                const bool pointer = palette && material->greyscaleTexture.get() == palette;
                out.withPalettePath += path ? 1 : 0;
                out.withPalettePointer += pointer ? 1 : 0;
                out.withBoth += (path && pointer) ? 1 : 0;
                out.withGrey += out.pointers.grey.back() ? 1 : 0;
            }
            if (auto* particles = a_geometry ? netimmerse_cast<RE::NiParticleSystem*>(a_geometry) : nullptr) {
                InspectModifiers(particles, out);
            }
        });
        });
        return out;
    }

    namespace
    {
        constexpr int kMaxNodeDepth = 64;

        void CollectValues(RE::NiAVObject* a_object, std::vector<std::uint32_t>& a_out, int a_depth)
        {
            if (!a_object || a_depth > kMaxNodeDepth) {
                return;
            }
            if (auto* value = netimmerse_cast<RE::BSValueNode*>(a_object)) {
                a_out.push_back(value->value);  // flat build (SE/AE): the runtime data members are BSValueNode's own (BSValueNode.h)
            }
            if (auto* node = a_object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    CollectValues(child.get(), a_out, a_depth + 1);
                }
            }
        }
    }

    std::vector<std::uint32_t> ValueNodeValues(RE::NiAVObject* a_root)
    {
        std::vector<std::uint32_t> out;
        CollectValues(a_root, out, 0);
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

    void RecordLaunch(LaunchRecord a_record)
    {
        Add(g_launches, std::move(a_record));
    }

    void RecordExplosionCreate(ExplosionCreateRecord a_record)
    {
        Add(g_explosionCreates, std::move(a_record));
    }

    void RecordHit(HitRecord a_record)
    {
        Add(g_hits, std::move(a_record));
    }

    std::vector<LaunchRecord> LaunchesSince(std::uint64_t a_mark)
    {
        return Since(g_launches, a_mark);
    }

    std::vector<ExplosionCreateRecord> ExplosionCreatesSince(std::uint64_t a_mark)
    {
        return Since(g_explosionCreates, a_mark);
    }

    std::vector<HitRecord> HitsSince(std::uint64_t a_mark)
    {
        return Since(g_hits, a_mark);
    }
}
