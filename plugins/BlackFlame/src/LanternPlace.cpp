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
        // A later load keyframes only a lantern or journal this close to its configured place next to the lab (review 1
        // R4): ours, not one the player dropped elsewhere in the cell. The keyframed refs do not move; a placement that
        // settled before its keyframe moves far less
        constexpr float kNearTarget = 64.0f;

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

        // Placed refs to keyframe again (a later load of the cell, or the journal of a placement the pickup ended): each
        // waits for its 3D on the tick like FinishPending, 10 s at most
        struct PendingRekey
        {
            std::vector<RE::ObjectRefHandle> refs;
            Clock::time_point started;
            std::uint32_t generation = 0;
        };

        // Game thread only (tasks and the tick); g_waiting is what other threads may read
        std::optional<PendingPlace> g_pending;
        std::optional<PostLoadCheck> g_postLoad;
        std::optional<PendingRekey> g_rekey;
        std::atomic<bool> g_waiting{ false };
        std::atomic<bool> g_enabled{ false };
        std::atomic<bool> g_threadWarned{ false };
        std::atomic<RE::FormID> g_cellId{ 0 };  // Ansilvund02's FormID, set at InitLanternPlace (the sinks compare it)

        void UpdateWaiting()
        {
            g_waiting.store(g_pending.has_value() || g_postLoad.has_value() || g_rekey.has_value());
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

        // A ref standing in the cell's world: not disabled, not deleted, parent cell a_cell, and not held by a container.
        // The last test assumes a world ref taken into an inventory keeps an ExtraReferenceHandle to its container
        // (CommonLib names the field containerRef; not checked in game) - skipping such a ref is the safe side
        bool InWorldOf(const RE::TESObjectREFR& a_ref, const RE::TESObjectCELL* a_cell)
        {
            return !a_ref.IsDisabled() && !a_ref.IsDeleted() && !a_ref.IsMarkedForDeletion() && a_ref.GetParentCell() == a_cell &&
                   !a_ref.extraList.HasType(RE::ExtraDataType::kReferenceHandle);
        }

        void DeleteRef(RE::TESObjectREFR* a_ref)
        {
            if (a_ref) {
                a_ref->Disable();
                a_ref->SetDelete(true);
            }
        }

        // Only what still stands in the cell: a made lantern the player already carries is never deleted
        void DeleteMade(const PendingPlace& a_made)
        {
            const auto* cell = LanternCell();
            for (const auto& handle : { a_made.lantern, a_made.journal, a_made.smoke }) {
                const auto ref = handle.get();
                if (ref && InWorldOf(*ref, cell)) {
                    DeleteRef(ref.get());
                }
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

        void QueueRekey(std::vector<RE::ObjectRefHandle> a_refs)
        {
            if (a_refs.empty()) {
                return;
            }
            if (g_rekey && g_rekey->generation == WorldGeneration()) {
                g_rekey->refs.insert(g_rekey->refs.end(), a_refs.begin(), a_refs.end());
                g_rekey->started = Clock::now();
            } else {
                g_rekey = PendingRekey{ std::move(a_refs), Clock::now(), WorldGeneration() };
            }
            UpdateWaiting();
        }

        // GLOB 1: the lantern and the journal this addon placed, keyframed again once their 3D is there (review 1 R4). They
        // are told from a lantern or journal the player dropped in the cell by their distance to the configured place
        void Rekeyframe(const RE::TESObjectCELL* a_cell)
        {
            const auto& forms = GetForms();
            const auto& config = GetConfig();
            const auto* lab = LabRef();
            if (!lab || lab->GetParentCell() != a_cell) {
                logger::warn("BlackFlame: lantern place: keyframe again skipped (lab ref 0C0090 {})", !lab ? "not found" : "not in cell 01F358");
                return;
            }
            const auto& lo = config.lanternOffset;
            const auto& jo = config.journalOffset;
            const RE::NiPoint3 lanternAt = LabToWorld(*lab, lo[0], lo[1], lo[2]);
            const RE::NiPoint3 journalAt = LabToWorld(*lab, jo[0], jo[1], jo[2]);
            std::vector<RE::ObjectRefHandle> found;
            for (const auto& ref : RefsOf(a_cell, { forms.lantern, forms.luahJournal })) {
                const auto& target = ref->GetBaseObject() == forms.lantern ? lanternAt : journalAt;
                if (InWorldOf(*ref, a_cell) && ref->GetPosition().GetDistance(target) <= kNearTarget) {
                    found.push_back(ref->GetHandle());
                }
            }
            if (found.empty()) {
                logger::info("BlackFlame: lantern place: nothing of ours to keyframe again (none within {} units of its place)", kNearTarget);
                return;
            }
            QueueRekey(std::move(found));
        }

        // Tick: each waiting ref keyframed once its 3D (and Havok body) is there, or logged failed when the wait ends
        void TickRekey()
        {
            const bool timedOut = Clock::now() - g_rekey->started >= kMaxWait;
            auto& refs = g_rekey->refs;
            for (auto it = refs.begin(); it != refs.end();) {
                const auto ref = it->get();
                if (!ref) {
                    it = refs.erase(it);  // gone (picked up, deleted)
                    continue;
                }
                const bool ok = TryKeyframe(*ref);
                if (ok || timedOut) {
                    LogKeyframe(*ref, ok);
                    it = refs.erase(it);
                } else {
                    ++it;
                }
            }
            if (refs.empty()) {
                g_rekey.reset();
            }
        }

        // GLOB 0 and refs of ours in the cell: a placement that did not finish (a save inside its 10 s, a keyframe that
        // failed) - removed before the new one, so a retry cannot leave two lanterns. Only refs standing in the cell's
        // world (review 1 R3: never one that is disabled, deleted, carried or elsewhere)
        void RemoveLeftovers(const RE::TESObjectCELL* a_cell)
        {
            const auto& forms = GetForms();
            std::size_t removed = 0;
            for (const auto& ref : RefsOf(a_cell, { forms.lantern, forms.luahJournal, forms.lanternSmoke })) {
                if (InWorldOf(*ref, a_cell)) {
                    DeleteRef(ref.get());
                    ++removed;
                }
            }
            if (removed > 0) {
                logger::info("BlackFlame: lantern place: removed {} ref(s) left by an unfinished placement", removed);
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
            if (!lanternOk || !journalOk) {
                LogKeyframe(*lantern, lanternOk);
                LogKeyframe(*journal, journalOk);
                return;  // the global stays 0: the next load of the cell removes these and places again
            }
            // Design section 7's order: the ok line, then the two keyframed lines
            GetForms().lanternPlaced->value = kPlaced;
            const auto smoke = made.smoke.get();
            logger::info("BlackFlame: lantern place: ok lantern={:08X} journal={:08X} smoke={:08X} at ({:.1f},{:.1f},{:.1f}) "
                         "lab=({:.1f},{:.1f},{:.1f})",
                lantern->GetFormID(), journal->GetFormID(), smoke ? smoke->GetFormID() : 0, made.at.x, made.at.y, made.at.z,
                made.lab.x, made.lab.y, made.lab.z);
            LogKeyframe(*lantern, lanternOk);
            LogKeyframe(*journal, journalOk);
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

        // Game thread: the player took the lantern (review 1 R3). The global becomes 1 at once, so no later load places a
        // second one; a placement still waiting for its 3D ends here (its journal is keyframed on the tick), then the smoke
        // in the player's cell goes out
        void OnLanternPickedUp()
        {
            if (!g_enabled.load() || !OnGameThreadOrWarn()) {
                return;
            }
            GetForms().lanternPlaced->value = kPlaced;
            if (g_pending) {
                const auto journal = g_pending->journal;
                g_pending.reset();
                logger::info("BlackFlame: lantern place: the lantern was picked up before its placement finished - global set "
                             "to 1, the journal keyframed on its own");
                QueueRekey({ journal });
                UpdateWaiting();
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
                        QueueGuarded("BlackFlameLanternPickup", &OnLanternPickedUp);
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
            if (!OnGameThreadOrWarn()) {
                return;
            }
            g_postLoad = PostLoadCheck{ Clock::now(), WorldGeneration() };
            UpdateWaiting();
        });
    }

    void TickLanternPlace()
    {
        // g_pending, g_postLoad and g_rekey are the game thread's alone (review 1 7a): off it, nothing is touched
        if (!g_waiting.load() || !IsOnGameThread()) {
            return;
        }
        const auto generation = WorldGeneration();
        if (g_postLoad && g_postLoad->generation != generation) {
            g_postLoad.reset();
        }
        if (g_pending && g_pending->generation != generation) {
            g_pending.reset();  // the world it was made in is gone (its refs went with it or were saved half done)
        }
        if (g_rekey && g_rekey->generation != generation) {
            g_rekey.reset();
        }
        if (g_postLoad) {
            TickPostLoad();
        }
        if (g_pending) {
            FinishPending();
        }
        if (g_rekey) {
            TickRekey();
        }
        UpdateWaiting();
    }

    bool HasPendingLanternPlace() noexcept
    {
        return g_waiting.load();
    }
}
