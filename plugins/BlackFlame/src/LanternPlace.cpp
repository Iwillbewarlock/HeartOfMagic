#include "LanternPlace.h"

#include "Config.h"
#include "Forms.h"
#include "GameThread.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr RE::FormID kCellLocalId = 0x01F358;  // CELL Ansilvund02 (Skyrim.esm)
        constexpr RE::FormID kLabLocalId = 0x0C0090;   // the Alchemy Lab ref in it
        constexpr auto kMaxWait = std::chrono::seconds(10);  // for the placed refs' 3D, and for the lab's after a load
        constexpr float kPlaced = 1.0f;
        constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
        constexpr std::size_t kYaw = 3;

        struct PendingPlace
        {
            RE::ObjectRefHandle lantern;
            RE::ObjectRefHandle journal;
            RE::ObjectRefHandle smoke;
            RE::NiPoint3 at;   // the lantern's position (the ok line)
            RE::NiPoint3 lab;  // the lab's
            Clock::time_point started;
            std::uint32_t generation = 0;
        };

        struct PostLoadCheck
        {
            Clock::time_point started;
            std::uint32_t generation = 0;
        };

        // Game thread only (tasks and the tick); g_waiting is what other threads may read
        std::optional<PendingPlace> g_pending;
        std::optional<PostLoadCheck> g_postLoad;
        std::atomic<bool> g_waiting{ false };
        std::atomic<bool> g_enabled{ false };
        std::atomic<bool> g_threadWarned{ false };
        std::atomic<RE::FormID> g_cellId{ 0 };  // Ansilvund02's FormID, set at InitLanternPlace (the sinks compare it)

        void UpdateWaiting()
        {
            g_waiting.store(g_pending.has_value() || g_postLoad.has_value());
        }

        RE::TESObjectCELL* LanternCell()
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            return dh ? dh->LookupForm<RE::TESObjectCELL>(kCellLocalId, kSkyrimMaster) : nullptr;
        }

        // A temporary ref: only in the form map while its cell is loaded, so looked up each time
        RE::TESObjectREFR* LabRef()
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            return dh ? dh->LookupForm<RE::TESObjectREFR>(kLabLocalId, kSkyrimMaster) : nullptr;
        }

        bool OnGameThreadOrWarn()
        {
            if (IsOnGameThread()) {
                return true;
            }
            if (!g_threadWarned.exchange(true)) {
                logger::warn("BlackFlame: lantern place: a task ran off the main thread - nothing placed or changed");
            }
            return false;
        }

        // Lab-local offset to world. Assumed convention (not verified in game): the z angle turns clockwise seen from
        // above, a ref's forward (+y) is (sin a, cos a) and its right (+x) (cos a, -sin a); the ok line prints the lab's
        // and the lantern's coordinates, which settles it on the first run
        RE::NiPoint3 LabToWorld(const RE::TESObjectREFR& a_lab, float a_x, float a_y, float a_z)
        {
            const float angle = a_lab.data.angle.z;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const auto lab = a_lab.GetPosition();
            return { lab.x + a_x * c + a_y * s, lab.y - a_x * s + a_y * c, lab.z + a_z };
        }

        std::vector<RE::NiPointer<RE::TESObjectREFR>> RefsOf(const RE::TESObjectCELL* a_cell,
            std::initializer_list<const RE::TESBoundObject*> a_bases)
        {
            std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
            if (!a_cell) {
                return refs;
            }
            // Inside the cell's spin lock only comparisons and NiPointer copies
            a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
                const auto* base = a_ref->GetBaseObject();
                for (const auto* wanted : a_bases) {
                    if (base && base == wanted && !a_ref->IsDeleted()) {
                        refs.emplace_back(a_ref);
                        break;
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return refs;
        }

        void DeleteRef(RE::TESObjectREFR* a_ref)
        {
            if (a_ref) {
                a_ref->Disable();
                a_ref->SetDelete(true);
            }
        }

        void DeleteMade(const PendingPlace& a_made)
        {
            for (const auto& handle : { a_made.lantern, a_made.journal, a_made.smoke }) {
                DeleteRef(handle.get().get());
            }
        }

        bool TryKeyframe(RE::TESObjectREFR& a_ref)
        {
            auto* root = a_ref.Get3D();
            return root && root->SetMotionType(RE::hkpMotion::MotionType::kKeyframed);
        }

        void LogKeyframe(const RE::TESObjectREFR& a_ref, bool a_ok)
        {
            logger::info("BlackFlame: lantern place: keyframed {:08X} {}", a_ref.GetFormID(), a_ok ? "ok" : "failed");
        }

        bool Keyframe(RE::TESObjectREFR& a_ref)
        {
            const bool ok = TryKeyframe(a_ref);
            LogKeyframe(a_ref, ok);
            return ok;
        }

        // GLOB 1: the lantern and the journal that are still in the cell keyframed again
        void Rekeyframe(const RE::TESObjectCELL* a_cell)
        {
            const auto& forms = GetForms();
            for (const auto& ref : RefsOf(a_cell, { forms.lantern, forms.luahJournal })) {
                if (!ref->IsDisabled()) {
                    (void)Keyframe(*ref);
                }
            }
        }

        // GLOB 0 and refs of ours in the cell: a placement that did not finish (a save inside its 10 s, a keyframe that
        // failed) - removed before the new one, so a retry cannot leave two lanterns
        void RemoveLeftovers(const RE::TESObjectCELL* a_cell)
        {
            const auto& forms = GetForms();
            const auto refs = RefsOf(a_cell, { forms.lantern, forms.luahJournal, forms.lanternSmoke });
            for (const auto& ref : refs) {
                DeleteRef(ref.get());
            }
            if (!refs.empty()) {
                logger::info("BlackFlame: lantern place: removed {} ref(s) left by an unfinished placement", refs.size());
            }
        }

        // Empty when the lab may take the objects, else why not
        std::string LabProblem(const RE::TESObjectREFR* a_lab, const RE::TESObjectCELL* a_cell)
        {
            if (!a_cell) {
                return "cell 01F358 not found";
            }
            if (!a_lab) {
                return "lab ref 0C0090 not found";
            }
            if (a_lab->IsDeleted()) {
                return "lab ref deleted";
            }
            if (a_lab->IsDisabled()) {
                return "lab ref disabled";
            }
            if (a_lab->GetParentCell() != a_cell) {
                return "lab ref not in cell 01F358";
            }
            if (!a_lab->Is3DLoaded()) {
                return "lab 3D not loaded";
            }
            return {};
        }

        RE::ObjectRefHandle Create(RE::TESBoundObject* a_base, RE::TESObjectREFR& a_lab, const RE::NiPoint3& a_at, float a_yawDeg)
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh || !a_base) {
                return {};
            }
            const RE::NiPoint3 angle{ 0.0f, 0.0f, a_lab.data.angle.z + a_yawDeg * kDegToRad };
            // As TESObjectREFR::PlaceObjectAtMe calls it, at the final position (moving a placed object later lets
            // Havok push it out of the lab's mesh, review R2)
            return dh->CreateReferenceAtLocation(a_base, a_at, angle, a_lab.GetParentCell(), a_lab.GetWorldspace(), nullptr,
                nullptr, RE::ObjectRefHandle(), true, true);
        }

        void Place(RE::TESObjectREFR& a_lab, const RE::TESObjectCELL* a_cell)
        {
            const auto& forms = GetForms();
            const auto& config = GetConfig();
            RemoveLeftovers(a_cell);
            const auto& lo = config.lanternOffset;
            const auto& jo = config.journalOffset;
            const auto& so = config.smokeOffset;
            PendingPlace made;
            made.at = LabToWorld(a_lab, lo[0], lo[1], lo[2]);
            made.lab = a_lab.GetPosition();
            made.lantern = Create(forms.lantern, a_lab, made.at, lo[kYaw]);
            made.journal = Create(forms.luahJournal, a_lab, LabToWorld(a_lab, jo[0], jo[1], jo[2]), jo[kYaw]);
            made.smoke = Create(forms.lanternSmoke, a_lab, LabToWorld(a_lab, so[0], so[1], so[2]), 0.0f);
            if (!made.lantern.get() || !made.journal.get() || !made.smoke.get()) {
                DeleteMade(made);
                logger::warn("BlackFlame: lantern place: skipped (could not create the {})",
                    !made.lantern.get() ? "lantern" : !made.journal.get() ? "journal" : "smoke");
                return;
            }
            made.started = Clock::now();
            made.generation = WorldGeneration();
            g_pending = made;
            UpdateWaiting();
        }

        // Game thread: the cell is loaded (the event, or the player's cell after a game load)
        void OnCellReady()
        {
            if (!g_enabled.load() || !OnGameThreadOrWarn()) {
                return;
            }
            auto* placed = GetForms().lanternPlaced;
            const auto* cell = LanternCell();
            if (placed->value >= kPlaced) {
                Rekeyframe(cell);
                return;
            }
            if (g_pending) {
                return;  // a placement already waits for its 3D
            }
            auto* lab = LabRef();
            if (const auto problem = LabProblem(lab, cell); !problem.empty()) {
                logger::warn("BlackFlame: lantern place: skipped ({})", problem);
                return;
            }
            Place(*lab, cell);
        }

        void FinishPending()
        {
            const auto made = *g_pending;
            g_pending.reset();
            const auto lantern = made.lantern.get();
            const auto journal = made.journal.get();
            if (!lantern || !journal || !lantern->Get3D() || !journal->Get3D()) {
                if (Clock::now() - made.started < kMaxWait && lantern && journal) {
                    g_pending = made;  // not yet
                    return;
                }
                const auto* missing = !lantern || !lantern->Get3D() ? lantern.get() : journal.get();
                logger::warn("BlackFlame: lantern place: skipped (no 3D for {:08X} within {} s; what was made is deleted, the next "
                             "load of the cell tries again)", missing ? missing->GetFormID() : 0, kMaxWait.count());
                DeleteMade(made);
                return;
            }
            // A 3D root can come a tick before its Havok body is in the world: a failed keyframe is tried again on the next
            // ticks until the wait ends, and only then logged as failed
            const bool lanternOk = TryKeyframe(*lantern);
            const bool journalOk = TryKeyframe(*journal);
            if ((!lanternOk || !journalOk) && Clock::now() - made.started < kMaxWait) {
                g_pending = made;
                return;
            }
            LogKeyframe(*lantern, lanternOk);
            LogKeyframe(*journal, journalOk);
            if (!lanternOk || !journalOk) {
                return;  // the global stays 0: the next load of the cell removes these and places again
            }
            GetForms().lanternPlaced->value = kPlaced;
            const auto smoke = made.smoke.get();
            logger::info("BlackFlame: lantern place: ok lantern={:08X} journal={:08X} smoke={:08X} at ({:.1f},{:.1f},{:.1f}) "
                         "lab=({:.1f},{:.1f},{:.1f})",
                lantern->GetFormID(), journal->GetFormID(), smoke ? smoke->GetFormID() : 0, made.at.x, made.at.y, made.at.z,
                made.lab.x, made.lab.y, made.lab.z);
        }

        // After a game load: the player's cell, once the lab's 3D is there (or 10 s passed: the skip line names it)
        void TickPostLoad()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto* cell = player ? player->GetParentCell() : nullptr;
            if (!cell || cell->GetFormID() != g_cellId.load()) {
                g_postLoad.reset();
                return;
            }
            const auto* lab = LabRef();
            if (!(lab && lab->Is3DLoaded()) && Clock::now() - g_postLoad->started < kMaxWait) {
                return;
            }
            g_postLoad.reset();
            OnCellReady();
        }

        void DisableSmoke()
        {
            if (!g_enabled.load() || !OnGameThreadOrWarn()) {
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            int disabled = 0;
            for (const auto& ref : RefsOf(player ? player->GetParentCell() : nullptr, { GetForms().lanternSmoke })) {
                if (!ref->IsDisabled()) {
                    ref->Disable();
                    ++disabled;
                }
            }
            logger::info("BlackFlame: lantern smoke: disabled {}", disabled);
        }

        void QueueGuarded(const char* a_name, void (*a_work)())
        {
            const auto generation = WorldGeneration();
            (void)QueueTask(a_name, [a_work, generation]() {
                if (generation == WorldGeneration()) {
                    a_work();
                }
            });
        }

        class CellSink final : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
        {
        public:
            static CellSink* GetSingleton()
            {
                static CellSink instance;
                return &instance;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event,
                RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
            {
                // Called from engine code: nothing may throw out of here; only the FormID is read, the rest is a task
                try {
                    if (a_event && a_event->cell && a_event->cell->GetFormID() == g_cellId.load()) {
                        QueueGuarded("BlackFlameLanternCell", &OnCellReady);
                    }
                } catch (...) {
                    // this load does not place; the next one tries again
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            CellSink() = default;
            ~CellSink() override = default;
        };

        class ContainerSink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
        {
        public:
            static ContainerSink* GetSingleton()
            {
                static ContainerSink instance;
                return &instance;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event,
                RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
            {
                constexpr RE::FormID kPlayerRef = 0x14;
                try {
                    // From the world (old container 0) to the player; a console AddItem also passes (harmless)
                    const auto* lantern = GetForms().lantern;
                    if (a_event && lantern && a_event->oldContainer == 0 && a_event->newContainer == kPlayerRef &&
                        a_event->baseObj == lantern->GetFormID()) {
                        QueueGuarded("BlackFlameLanternSmoke", &DisableSmoke);
                    }
                } catch (...) {
                    // the smoke stays
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            ContainerSink() = default;
            ~ContainerSink() override = default;
        };
    }

    void InitLanternPlace()
    {
        if (!GetForms().lanternForms || !GetConfig().lanternPlace) {
            logger::info("BlackFlame: lantern place off ({})", !GetForms().lanternForms ? "lantern records missing" : "lanternPlace false");
            return;
        }
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        const auto* cell = LanternCell();
        if (!holder || !cell) {
            logger::warn("BlackFlame: lantern place off ({})", !holder ? "ScriptEventSourceHolder unavailable" : "cell 01F358 not found");
            return;
        }
        g_cellId.store(cell->GetFormID());
        holder->AddEventSink<RE::TESCellFullyLoadedEvent>(CellSink::GetSingleton());
        holder->AddEventSink<RE::TESContainerChangedEvent>(ContainerSink::GetSingleton());
        g_enabled.store(true);
        logger::info("BlackFlame: lantern place on (cell {:06X}, lab ref {:06X} of {})", kCellLocalId, kLabLocalId, kSkyrimMaster);
    }

    void OnLanternGameLoaded()
    {
        if (!g_enabled.load()) {
            return;
        }
        QueueGuarded("BlackFlameLanternLoad", []() {
            g_postLoad = PostLoadCheck{ Clock::now(), WorldGeneration() };
            UpdateWaiting();
        });
    }

    void TickLanternPlace()
    {
        if (!g_waiting.load()) {
            return;
        }
        const auto generation = WorldGeneration();
        if (g_postLoad && g_postLoad->generation != generation) {
            g_postLoad.reset();
        }
        if (g_pending && g_pending->generation != generation) {
            g_pending.reset();  // the world it was made in is gone (its refs went with it or were saved half done)
        }
        if (IsOnGameThread()) {
            if (g_postLoad) {
                TickPostLoad();
            }
            if (g_pending) {
                FinishPending();
            }
        }
        UpdateWaiting();
    }

    bool HasPendingLanternPlace() noexcept
    {
        return g_waiting.load();
    }
}
