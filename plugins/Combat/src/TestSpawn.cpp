#include "Common.h"
#include "TestSpawn.h"

#include <atomic>
#include <cmath>
#include <vector>

namespace SLCombat
{
    namespace
    {
        constexpr const char* kVanilla = "Skyrim.esm";
        // Skyrim.esm leveled warlocks (read from the plugin: LvlWarlockFire/Frost/Shock)
        constexpr RE::FormID kWarlockFire = 0x01E7CD;
        constexpr RE::FormID kWarlockFrost = 0x01E7CE;
        constexpr RE::FormID kWarlockShock = 0x01E7CF;

        constexpr float kSpawnDistance = 700.0f;  ///< game units in front of the player (~10 m)

        std::vector<RE::ObjectRefHandle> s_spawned;  ///< game thread
        std::atomic<std::size_t> s_count{ 0 };       ///< for the settings page

        RE::FormID LocalID(TestMage a_kind)
        {
            switch (a_kind) {
            case TestMage::kFrost:
                return kWarlockFrost;
            case TestMage::kShock:
                return kWarlockShock;
            default:
                return kWarlockFire;
            }
        }

        const char* KindName(TestMage a_kind)
        {
            switch (a_kind) {
            case TestMage::kFrost:
                return "frost";
            case TestMage::kShock:
                return "shock";
            default:
                return "fire";
            }
        }
    }

    void TestSpawn::Spawn(TestMage a_kind)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!player || !data || !player->GetParentCell()) {
            return;
        }
        auto* base = data->LookupForm<RE::TESNPC>(LocalID(a_kind), kVanilla);
        if (!base) {
            logger::warn("test.spawn {} warlock not found in {}", KindName(a_kind), kVanilla);
            return;
        }
        auto placed = player->PlaceObjectAtMe(base, false);
        if (!placed) {
            logger::warn("test.spawn {} warlock could not be placed", KindName(a_kind));
            return;
        }
        float heading = player->data.angle.z;
        auto pos = player->GetPosition();
        pos.x += std::sin(heading) * kSpawnDistance;
        pos.y += std::cos(heading) * kSpawnDistance;
        placed->SetPosition(pos);
        s_spawned.push_back(placed->CreateRefHandle());
        s_count.store(s_spawned.size());
        logger::info("test.spawn {} warlock {:08X} placed {:.0f} units ahead", KindName(a_kind), placed->GetFormID(), kSpawnDistance);
    }

    void TestSpawn::RemoveAll()
    {
        std::size_t removed = 0;
        for (auto& handle : s_spawned) {
            if (auto ref = handle.get()) {
                ref->Disable();
                ref->SetDelete(true);
                ++removed;
            }
        }
        s_spawned.clear();
        s_count.store(0);
        logger::info("test.spawn removed {}", removed);
    }

    std::size_t TestSpawn::Count()
    {
        return s_count.load();
    }
}
