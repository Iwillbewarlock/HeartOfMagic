#include "LanternPlace.h"

#include "Config.h"
#include "Forms.h"
#include "GameThread.h"

#include <algorithm>
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
        constexpr RE::FormID kTableLocalId = 0x03DFA0;  // the UpperTable01 ref in it (user 2026-10-21: on this table, not the alchemy table)
        constexpr auto kMaxWait = std::chrono::seconds(10);  // for the refs' 3D before a keyframe, and for the table's after a load
        constexpr float kPlaced = 1.0f;
        constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
        constexpr std::size_t kYaw = 3;
        // A later load keyframes only a lantern or journal this close to its configured place next to the table (review 1
        // R4): ours, not one the player dropped elsewhere in the cell. A placement that settled before its keyframe moves
        // far less
        constexpr float kNearTarget = 64.0f;

        struct PostLoadCheck
        {
            Clock::time_point started;
            std::uint32_t generation = 0;
        };

        // The lantern and the journal to keyframe (right after creation, and at each later load of the cell): each waits
        // for its 3D on the tick, 10 s at most. Best effort: the placement is done without it (GLOB set at creation)
        struct PendingRekey
        {
            std::vector<RE::ObjectRefHandle> refs;
            Clock::time_point started;
            std::uint32_t generation = 0;
        };

        // Game thread only (tasks and the tick); g_waiting is what other threads may read
        std::optional<PostLoadCheck> g_postLoad;
        std::optional<PendingRekey> g_rekey;
        std::atomic<bool> g_waiting{ false };
        std::atomic<bool> g_enabled{ false };
        std::atomic<bool> g_threadWarned{ false };
        std::atomic<RE::FormID> g_cellId{ 0 };  // Ansilvund02's FormID, set at InitLanternPlace (the sinks compare it)

        void UpdateWaiting()
        {
            g_waiting.store(g_postLoad.has_value() || g_rekey.has_value());
        }

        RE::TESObjectCELL* LanternCell()
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            return dh ? dh->LookupForm<RE::TESObjectCELL>(kCellLocalId, kSkyrimMaster) : nullptr;
        }

        // A temporary ref: only in the form map while its cell is loaded, so looked up each time
        RE::TESObjectREFR* TableRef()
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            return dh ? dh->LookupForm<RE::TESObjectREFR>(kTableLocalId, kSkyrimMaster) : nullptr;
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

        // Table-local offset to world. Assumed convention (not verified in game): the z angle turns clockwise seen from
        // above, a ref's forward (+y) is (sin a, cos a) and its right (+x) (cos a, -sin a); the ok line prints the table's
        // and the lantern's coordinates, which settles it on the first run
        RE::NiPoint3 TableToWorld(const RE::TESObjectREFR& a_table, float a_x, float a_y, float a_z)
        {
            const float angle = a_table.data.angle.z;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const auto table = a_table.GetPosition();
            return { table.x + a_x * c + a_y * s, table.y - a_x * s + a_y * c, table.z + a_z };
        }

        // Where the lantern and the journal belong next to the table (the config offsets)
        struct Targets
        {
            RE::NiPoint3 lantern;
            RE::NiPoint3 journal;
        };

        Targets TargetsOf(const RE::TESObjectREFR& a_table)
        {
            const auto& lo = GetConfig().lanternOffset;
            const auto& jo = GetConfig().journalOffset;
            return { TableToWorld(a_table, lo[0], lo[1], lo[2]), TableToWorld(a_table, jo[0], jo[1], jo[2]) };
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

        // One lantern or journal looked at for a keyframe: logged (first-run diagnostics), true when it is ours to keyframe -
        // its base, not disabled, parent cell a_cell, within kNearTarget of its place
        bool IsOursToKeyframe(const RE::TESObjectREFR& a_ref, const RE::TESObjectCELL* a_cell, const Targets& a_targets)
        {
            const auto& forms = GetForms();
            const auto* base = a_ref.GetBaseObject();
            const bool lantern = base == forms.lantern;
            const bool journal = base == forms.luahJournal;
            const auto* cell = a_ref.GetParentCell();
            const bool disabled = a_ref.IsDisabled();
            const float dist = a_ref.GetPosition().GetDistance(lantern ? a_targets.lantern : a_targets.journal);
            logger::info("BlackFlame: lantern place: candidate {:08X} base={:08X} disabled={} cell={:08X} has3D={} dist={:.1f}",
                a_ref.GetFormID(), base ? base->GetFormID() : 0, disabled, cell ? cell->GetFormID() : 0, a_ref.Get3D() != nullptr,
                dist);
            return (lantern || journal) && !disabled && cell == a_cell && dist <= kNearTarget;
        }

        // Review 2 R4: the post-load check and the cell event can both queue the same refs; each handle is queued once,
        // and the 10 s window restarts only when something new came in
        void QueueRekey(const std::vector<RE::ObjectRefHandle>& a_refs)
        {
            if (!g_rekey || g_rekey->generation != WorldGeneration()) {
                g_rekey = PendingRekey{ {}, Clock::now(), WorldGeneration() };
            }
            auto& queued = g_rekey->refs;
            bool added = false;
            for (const auto& handle : a_refs) {
                if (handle && std::find(queued.begin(), queued.end(), handle) == queued.end()) {
                    queued.push_back(handle);
                    added = true;
                }
            }
            if (added) {
                g_rekey->started = Clock::now();
            }
            if (queued.empty()) {
                g_rekey.reset();
            }
            UpdateWaiting();
        }

        // GLOB 1, a later load of the cell: the lantern and the journal this addon placed, keyframed again once their 3D is
        // there (the motion type is not known to survive a save). Told from a lantern or journal the player dropped in the
        // cell by their distance to the configured place
        void Rekeyframe(const RE::TESObjectCELL* a_cell)
        {
            const auto& forms = GetForms();
            const auto* table = TableRef();
            if (!table || table->GetParentCell() != a_cell) {
                logger::warn("BlackFlame: lantern place: keyframe again skipped (table ref 03DFA0 {})", !table ? "not found" : "not in cell 01F358");
                return;
            }
            const auto targets = TargetsOf(*table);
            std::vector<RE::ObjectRefHandle> found;
            for (const auto& ref : RefsOf(a_cell, { forms.lantern, forms.luahJournal })) {
                if (IsOursToKeyframe(*ref, a_cell, targets)) {
                    found.push_back(ref->GetHandle());
                }
            }
            if (found.empty()) {
                logger::info("BlackFlame: lantern place: nothing of ours to keyframe again (none within {} units of its place)", kNearTarget);
                return;
            }
            QueueRekey(found);
        }

        // Tick: a waiting ref is keyframed through TESObjectREFR::SetMotionType (it also sets the kHavokMoved change flag, so
        // the state goes into the save; review 2 I1) once its 3D is there. A false result is tried again on the next ticks
        // (the Havok body can come a tick after the 3D root) and logged as is when the wait ends; a ref without 3D by then
        // is logged failed with the reason. Nothing else happens on a failure
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
                if (!ref->Get3D()) {
                    if (timedOut) {
                        logger::info("BlackFlame: lantern place: keyframed {:08X} failed (no 3D within {} s)", ref->GetFormID(),
                            kMaxWait.count());
                        it = refs.erase(it);
                    } else {
                        ++it;
                    }
                    continue;
                }
                const bool ok = ref->SetMotionType(RE::hkpMotion::MotionType::kKeyframed);
                if (ok || timedOut) {
                    logger::info("BlackFlame: lantern place: keyframed {:08X} {}", ref->GetFormID(), ok ? "ok" : "failed");
                    it = refs.erase(it);
                } else {
                    ++it;
                }
            }
            if (refs.empty()) {
                g_rekey.reset();
            }
        }

        // Empty when the table may take the objects, else why not
        std::string TableProblem(const RE::TESObjectREFR* a_table, const RE::TESObjectCELL* a_cell)
        {
            if (!a_cell) {
                return "cell 01F358 not found";
            }
            if (!a_table) {
                return "table ref 03DFA0 not found";
            }
            if (a_table->IsDeleted()) {
                return "table ref deleted";
            }
            if (a_table->IsDisabled()) {
                return "table ref disabled";
            }
            if (a_table->GetParentCell() != a_cell) {
                return "table ref not in cell 01F358";
            }
            if (!a_table->Is3DLoaded()) {
                return "table 3D not loaded";
            }
            return {};
        }

        RE::ObjectRefHandle Create(RE::TESBoundObject* a_base, RE::TESObjectREFR& a_table, const RE::NiPoint3& a_at, float a_yawDeg)
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh || !a_base) {
                return {};
            }
            const RE::NiPoint3 angle{ 0.0f, 0.0f, a_table.data.angle.z + a_yawDeg * kDegToRad };
            // As TESObjectREFR::PlaceObjectAtMe calls it, at the final position (moving a placed object later lets
            // Havok push it out of the table's mesh, review R2)
            return dh->CreateReferenceAtLocation(a_base, a_at, angle, a_table.GetParentCell(), a_table.GetWorldspace(), nullptr,
                nullptr, RE::ObjectRefHandle(), true, true);
        }

        RE::FormID IdOf(const RE::ObjectRefHandle& a_handle)
        {
            const auto ref = a_handle.get();
            return ref ? ref->GetFormID() : 0;
        }

        // The names of the refs that could not be created, comma separated
        std::string MissingNames(RE::FormID a_lantern, RE::FormID a_journal, RE::FormID a_smoke)
        {
            std::string names;
            for (const auto& [id, name] : { std::pair{ a_lantern, "lantern" }, std::pair{ a_journal, "journal" }, std::pair{ a_smoke, "smoke" } }) {
                if (id == 0) {
                    names += names.empty() ? name : std::string(", ") + name;
                }
            }
            return names;
        }

        // Design 2026-10-21 section 4 (coordinator decision of the same day): the placement is done when the creation calls
        // returned. The global becomes 1 here, in the same task, whatever the keyframe does later, so no save and no later
        // load can make a second lantern. A creation that failed part-way keeps what was made and still sets the global
        // (a retry could double the made ones); only when nothing was made does the global stay 0
        void Place(RE::TESObjectREFR& a_table, const RE::TESObjectCELL* a_cell)
        {
            const auto& forms = GetForms();
            const auto& config = GetConfig();
            const auto targets = TargetsOf(a_table);
            const auto& lo = config.lanternOffset;
            const auto& jo = config.journalOffset;
            const auto& so = config.smokeOffset;
            const auto lantern = Create(forms.lantern, a_table, targets.lantern, lo[kYaw]);
            const auto journal = Create(forms.luahJournal, a_table, targets.journal, jo[kYaw]);
            const auto smoke = Create(forms.lanternSmoke, a_table, TableToWorld(a_table, so[0], so[1], so[2]), 0.0f);
            const RE::FormID lanternId = IdOf(lantern);
            const RE::FormID journalId = IdOf(journal);
            const RE::FormID smokeId = IdOf(smoke);
            if (lanternId == 0 && journalId == 0 && smokeId == 0) {
                logger::warn("BlackFlame: lantern place: skipped (could not create the lantern, journal and smoke)");
                return;
            }
            forms.lanternPlaced->value = kPlaced;
            const auto table = a_table.GetPosition();
            if (lanternId != 0 && journalId != 0 && smokeId != 0) {
                logger::info("BlackFlame: lantern place: ok lantern={:08X} journal={:08X} smoke={:08X} at ({:.1f},{:.1f},{:.1f}) "
                             "table=({:.1f},{:.1f},{:.1f})",
                    lanternId, journalId, smokeId, targets.lantern.x, targets.lantern.y, targets.lantern.z, table.x, table.y, table.z);
            } else {
                logger::warn("BlackFlame: lantern place: partial lantern={:08X} journal={:08X} smoke={:08X} at ({:.1f},{:.1f},{:.1f}) "
                             "table=({:.1f},{:.1f},{:.1f}) (could not create the {}; what was made is kept, global set to 1)",
                    lanternId, journalId, smokeId, targets.lantern.x, targets.lantern.y, targets.lantern.z, table.x, table.y, table.z,
                    MissingNames(lanternId, journalId, smokeId));
            }
            // The made handles directly, not a scan of the cell: whether CreateReferenceAtLocation has put a new ref in the
            // cell's list by the time it returns is not known here. They are ours by construction, so queued whatever the
            // candidate test says (its line is the diagnostic)
            std::vector<RE::ObjectRefHandle> toKey;
            for (const auto& handle : { lantern, journal }) {
                if (const auto ref = handle.get()) {
                    (void)IsOursToKeyframe(*ref, a_cell, targets);
                    toKey.push_back(handle);
                }
            }
            QueueRekey(toKey);
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
            auto* table = TableRef();
            if (const auto problem = TableProblem(table, cell); !problem.empty()) {
                logger::warn("BlackFlame: lantern place: skipped ({})", problem);
                return;
            }
            Place(*table, cell);
        }

        // After a game load: the player's cell, once the table's 3D is there (or 10 s passed: the skip line names it)
        void TickPostLoad()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto* cell = player ? player->GetParentCell() : nullptr;
            if (!cell || cell->GetFormID() != g_cellId.load()) {
                g_postLoad.reset();
                return;
            }
            const auto* table = TableRef();
            if (!(table && table->Is3DLoaded()) && Clock::now() - g_postLoad->started < kMaxWait) {
                return;
            }
            g_postLoad.reset();
            OnCellReady();
        }

        // Game thread: the player took the lantern. The global is already 1 for a placed lantern (set at creation); setting
        // it again only matters for a lantern that came from a console AddItem before any visit, which then never gets the
        // placed one. Then the smoke in the player's cell goes out
        void OnLanternPickedUp()
        {
            if (!g_enabled.load() || !OnGameThreadOrWarn()) {
                return;
            }
            GetForms().lanternPlaced->value = kPlaced;
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
        logger::info("BlackFlame: lantern place on (cell {:06X}, table ref {:06X} of {})", kCellLocalId, kTableLocalId, kSkyrimMaster);
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
        // g_postLoad and g_rekey are the game thread's alone (review 1 7a): off it, nothing is touched
        if (!g_waiting.load() || !IsOnGameThread()) {
            return;
        }
        const auto generation = WorldGeneration();
        if (g_postLoad && g_postLoad->generation != generation) {
            g_postLoad.reset();
        }
        if (g_rekey && g_rekey->generation != generation) {
            g_rekey.reset();  // the world its refs were in is gone; the next load of the cell queues them again
        }
        if (g_postLoad) {
            TickPostLoad();
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
