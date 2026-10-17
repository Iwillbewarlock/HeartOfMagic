#include "DrawTimeGrey.h"
#include "DrawTimeGreyShared.h"

#include "BuffState.h"
#include "Config.h"
#include "DataPathRule.h"
#include "FireSet.h"
#include "GameThread.h"
#include "MainThreadQueue.h"
#include "Palette.h"
#include "Recolor.h"
#include "SehGuard.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

// =============================================================================
// DRAW-TIME GREY - the player geometry set (DrawTimeGrey.h)
// =============================================================================
// Main thread only (tasks of our main-thread queue): the roots are walked, every effect geometry decided once per walk
// (palette flags, blend, which palette), the geometries held by NiPointer in the owner map, and an immutable snapshot of
// raw pointers published for the render hook. A geometry dropped from the set is released only when the render side
// has no hook body in flight after the publish (Counters::inside == 0); until then it waits in the retired list, so no
// address of a listed geometry can be reused while a pass may still compare against it.

namespace BlackFlame
{
    namespace
    {
        using namespace DrawTime;
        using Flag = RE::BSShaderProperty::EShaderPropertyFlag;

        constexpr auto kTickInterval = std::chrono::milliseconds(100);
        constexpr auto kSummaryInterval = std::chrono::seconds(5);
        constexpr std::size_t kMaxRoots = 256;  // noted projectiles / explosions kept at once (oldest dropped)
        constexpr int kDerivedLogs = 12;
        constexpr int kRootLogs = 8;
        constexpr const char* kLeftMagicNodeName = "NPC L MagicNode [LMag]";   // as CasterArt.cpp
        constexpr const char* kRightMagicNodeName = "NPC R MagicNode [RMag]";

        constexpr std::size_t kRootKinds = static_cast<std::size_t>(RootKind::kCount);

        struct Root
        {
            RE::ObjectRefHandle handle;
            RootKind kind = RootKind::kProjectile;
        };

        struct WalkStats
        {
            int derived = 0;
            int fixed = 0;
            int fixedAdd = 0;
            int leftAlone = 0;
            int mps = 0;
            int legacy = 0;
            int noPaletteFlag = 0;
            int noSource = 0;
            int alreadyOurs = 0;
            std::array<int, kRootKinds> roots{};       // roots walked with 3D, per kind
            std::array<int, kRootKinds> geometries{};  // targets per kind (an enchantment explosion shows here too)

            [[nodiscard]] std::string Describe() const
            {
                std::string perKind;
                for (std::size_t i = 0; i < kRootKinds; ++i) {
                    perKind += std::format("{}{} {} ({} roots)", i ? ", " : "", RootKindName(static_cast<RootKind>(i)),
                        geometries[i], roots[i]);
                }
                return std::format("{}; derived {}, fixed {}, fixed add {}, left alone {}; skipped: MPS {}, legacy {}, no "
                                   "palette flag {}, no palette texture {}, already ours {}",
                    perKind, derived, fixed, fixedAdd, leftAlone, mps, legacy, noPaletteFlag, noSource, alreadyOurs);
            }
        };

        using Owners = std::unordered_map<const RE::BSGeometry*, RE::NiPointer<RE::BSGeometry>>;

        // Main thread (and the message handler at a load), under g_stateMutex
        struct MainState
        {
            std::vector<Root> roots;
            Owners owners;
            std::vector<RE::NiPointer<RE::BSGeometry>> retired;
            // DU5: the published snapshot and the ones it replaced, held here so the last reference to a snapshot (and
            // its hash maps) always drops on the main thread, never in a render-side shared_ptr copy
            std::shared_ptr<const Snapshot> current;
            std::vector<std::shared_ptr<const Snapshot>> retiredSnapshots;
            std::unordered_map<std::string, RE::NiSourceTexture*> derived;  // derived path -> texture (null: none)
            std::uint64_t generation = 0;
            bool wasStoked = false;
            std::chrono::steady_clock::time_point lastSummary{};
            WalkStats lastStats;
            std::uint64_t deferredReleases = 0;
        };

        TrackedMutex g_stateMutex;
        TrackedMutex g_notesMutex;
        std::atomic<bool> g_tickQueued{ false };
        std::atomic<bool> g_tickerStarted{ false };
        std::atomic<int> g_derivedLogs{ 0 };
        std::atomic<int> g_rootLogs{ 0 };

        // Heap-held and never freed: nothing runs at DLL unload (the textures and strings too, as Palette.cpp)
        MainState& State()
        {
            static auto* state = std::make_unique<MainState>().release();
            return *state;
        }

        std::vector<Root>& Notes()
        {
            static auto* notes = std::make_unique<std::vector<Root>>().release();
            return *notes;
        }

        Srv* ViewOf(const RE::NiSourceTexture* a_texture)
        {
            return a_texture && a_texture->rendererTexture ? a_texture->rendererTexture->resourceView : nullptr;
        }

        // Below a BSMasterParticleSystem: shared by every emitter of that kind in the cell (an NPC's too), never ours
        // (the same rule as Recolor.cpp's, applied always)
        bool UnderMasterParticleSystem(RE::NiAVObject* a_object, const RE::NiAVObject* a_root)
        {
            for (RE::NiAVObject* node = a_object; node; node = node->parent) {
                if (netimmerse_cast<RE::BSMasterParticleSystem*>(node)) {
                    return true;
                }
                if (node == a_root) {
                    break;
                }
            }
            return false;
        }

        // The builder's palette for this source palette and blend: DataPathRule::kDerivedPaletteAdd / Normal followed by
        // the source path after "textures" (tools/blackflame-greybuild/GreyPalette.h). Loaded once through the engine's
        // texture path when the loose file is there; cached either way. Loose files only: std::filesystem sees the Data
        // folder (through MO2's VFS), not a BSA, so a derived palette packed into an archive reads as missing (the
        // builder writes them loose; CopyFreshness.cpp counts them the same way)
        RE::NiSourceTexture* DerivedPalette(MainState& a_state, const RE::BSFixedString& a_source, bool a_additive)
        {
            if (a_source.empty()) {
                return nullptr;
            }
            const auto normal = DataPathRule::Normalize(a_source.c_str(), DataPathRule::kTexturesRoot);
            if (!normal) {
                return nullptr;
            }
            std::string path(a_additive ? DataPathRule::kDerivedPaletteAdd : DataPathRule::kDerivedPaletteNormal);
            path += std::string_view(*normal).substr(DataPathRule::kTexturesRoot.size());
            if (const auto it = a_state.derived.find(path); it != a_state.derived.end()) {
                return it->second;
            }
            std::error_code error;
            const bool onDisk = std::filesystem::exists(std::filesystem::path("Data") / path, error);
            RE::NiSourceTexture* texture = nullptr;
            if (onDisk) {
                RE::NiPointer<RE::NiTexture> loaded;
                RE::BSShaderManager::GetTexture(path.c_str(), true, loaded, false);
                texture = netimmerse_cast<RE::NiSourceTexture*>(loaded.get());
                if (texture) {
                    texture->IncRefCount();  // session-long: never released (as Palette.cpp)
                }
            }
            if (g_derivedLogs.fetch_add(1, std::memory_order_relaxed) < kDerivedLogs) {
                logger::info("BlackFlame: draw-time grey: derived palette '{}' for '{}': {}", path, a_source.c_str(),
                    !onDisk ? "not on disk" : (texture ? (ViewOf(texture) ? "loaded" : "loaded without a view") : "did not load"));
            }
            a_state.derived.emplace(std::move(path), texture);
            return texture;
        }

        bool IsAdditive(RE::BSGeometry* a_geometry)
        {
            const auto& alpha = a_geometry->GetGeometryRuntimeData().alphaProperty;
            return alpha && alpha->GetAlphaBlending() &&
                   alpha->GetDestBlendMode() == RE::NiAlphaProperty::AlphaFunction::kOne;
        }

        // Decides one geometry: palette colour only takes the derived palette, else the fixed one of its blend; palette
        // alpha takes only the derived one (one universal palette would carve or fill its outline, README "Palettes")
        void Classify(MainState& a_state, RE::BSEffectShaderProperty* a_prop, RE::BSGeometry* a_geometry, RootKind a_root,
            Snapshot& a_out, Owners& a_owners, WalkStats& a_stats)
        {
            const bool colour = a_prop->flags.any(Flag::kGrayscaleToPaletteColor);
            const bool alpha = a_prop->flags.any(Flag::kGrayscaleToPaletteAlpha);
            if (!colour && !alpha) {
                ++a_stats.noPaletteFlag;
                return;
            }
            auto* material = a_prop->GetMaterial();
            Srv* source = material ? ViewOf(material->greyscaleTexture.get()) : nullptr;
            if (!source) {
                ++a_stats.noSource;
                return;
            }
            if (IsAnyPalettePath(material->greyscaleTexturePath)) {
                ++a_stats.alreadyOurs;  // a copy's own grey palette (copy swaps on): not bound, not an NPC leak either
                a_out.playerSkipped.insert(a_geometry);
                a_owners.try_emplace(a_geometry, RE::NiPointer<RE::BSGeometry>(a_geometry));  // its address held too
                return;
            }
            const bool additive = IsAdditive(a_geometry);
            Target target{ a_prop, material, source, ViewOf(material->sourceTexture.get()), nullptr, GreyKind::kLeftAlone,
                a_root };
            if (Srv* derived = ViewOf(DerivedPalette(a_state, material->greyscaleTexturePath, additive))) {
                target.grey = derived;
                target.kind = GreyKind::kDerived;
                ++a_stats.derived;
            } else if (Srv* fixed = colour && !alpha ? ViewOf(additive ? GetAddPalette() : GetPalette()) : nullptr) {
                target.grey = fixed;
                target.kind = additive ? GreyKind::kFixedAdd : GreyKind::kFixed;
                ++(additive ? a_stats.fixedAdd : a_stats.fixed);
            } else {
                ++a_stats.leftAlone;
            }
            if (target.grey) {
                a_out.ours.insert(target.grey);
            }
            a_out.targets.emplace(a_geometry, target);
            a_owners.try_emplace(a_geometry, RE::NiPointer<RE::BSGeometry>(a_geometry));
            ++a_stats.geometries[static_cast<std::size_t>(a_root)];
        }

        void Collect(MainState& a_state, RE::NiAVObject* a_root, RootKind a_kind, Snapshot& a_out, Owners& a_owners,
            WalkStats& a_stats)
        {
            if (!a_root) {
                return;
            }
            ForEachEffectProperty(a_root, [&](RE::BSEffectShaderProperty* a_prop, RE::BSGeometry* a_geometry, bool a_legacy) {
                if (a_legacy || !a_geometry || !a_prop) {
                    ++a_stats.legacy;  // a BSRenderPass names a BSGeometry: a legacy NiGeometry cannot be matched
                    return;
                }
                if (UnderMasterParticleSystem(a_geometry, a_root)) {
                    ++a_stats.mps;
                    return;
                }
                if (!a_out.targets.contains(a_geometry) && !a_out.playerSkipped.contains(a_geometry)) {
                    Classify(a_state, a_prop, a_geometry, a_kind, a_out, a_owners, a_stats);
                }
            });
        }

        // Vanilla node names as BSFixedStrings, made once on the main thread and never freed (as CasterArt.cpp)
        const RE::BSFixedString& MagicNodeName(bool a_left)
        {
            static const RE::BSFixedString* const left = std::make_unique<RE::BSFixedString>(kLeftMagicNodeName).release();
            static const RE::BSFixedString* const right = std::make_unique<RE::BSFixedString>(kRightMagicNodeName).release();
            return a_left ? *left : *right;
        }

        // The casting art of each hand that casts or holds a fire spell: attachedArt, the caster's magicNode and the
        // hand's magic node in both skeletons (the places CasterArt.cpp reads)
        int CollectHands(MainState& a_state, Snapshot& a_out, Owners& a_owners, WalkStats& a_stats)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return 0;
            }
            int roots = 0;
            using Source = RE::MagicSystem::CastingSource;
            for (const auto source : { Source::kLeftHand, Source::kRightHand }) {
                const bool left = source == Source::kLeftHand;
                auto* caster = skyrim_cast<RE::ActorMagicCaster*>(player->GetMagicCaster(source));
                if (!caster) {
                    continue;
                }
                const RE::MagicItem* spell = caster->currentSpell;
                if (!IsFireSpell(spell)) {
                    auto* equipped = player->GetEquippedObject(left);
                    spell = equipped ? equipped->As<RE::SpellItem>() : nullptr;
                }
                if (!IsFireSpell(spell)) {
                    continue;
                }
                Collect(a_state, caster->castingArtData.attachedArt.get(), RootKind::kHand, a_out, a_owners, a_stats);
                Collect(a_state, caster->magicNode, RootKind::kHand, a_out, a_owners, a_stats);
                for (const bool firstPerson : { true, false }) {
                    auto* root = player->Get3D(firstPerson);
                    Collect(a_state, root ? root->GetObjectByName(MagicNodeName(left)) : nullptr, RootKind::kHand, a_out,
                        a_owners, a_stats);
                }
                ++roots;
                ++a_stats.roots[static_cast<std::size_t>(RootKind::kHand)];
            }
            return roots;
        }

        void ReleaseRetired(MainState& a_state)
        {
            if (a_state.retired.empty() && a_state.retiredSnapshots.empty()) {
                return;
            }
            // seq_cst: read after the publish. 0 = no hook body holds an older snapshot (its shared_ptr copy is gone
            // with it, so ours here is the last one); later bodies load the new one
            if (GetCounters().inside.load() == 0) {
                a_state.retiredSnapshots.clear();
                a_state.retired.clear();
            } else {
                ++a_state.deferredReleases;  // tried again at the next walk
            }
        }

        void Publish(MainState& a_state, std::shared_ptr<Snapshot> a_snapshot, Owners a_owners)
        {
            for (auto& [geometry, owner] : a_state.owners) {
                if (!a_owners.contains(geometry)) {
                    a_state.retired.push_back(std::move(owner));
                }
            }
            a_state.owners = std::move(a_owners);
            if (a_state.current) {
                a_state.retiredSnapshots.push_back(std::move(a_state.current));
            }
            a_state.current = a_snapshot;  // held before the store, so the atomic never drops the last reference
            PublishSnapshot(std::move(a_snapshot));
            ReleaseRetired(a_state);
        }

        void TakeNotes(MainState& a_state)
        {
            std::vector<Root> notes;
            {
                std::lock_guard lock(g_notesMutex);
                notes.swap(Notes());
            }
            for (auto& note : notes) {
                const bool known = std::ranges::any_of(a_state.roots,
                    [&](const Root& a_root) { return a_root.handle.native_handle() == note.handle.native_handle(); });
                if (!known) {
                    a_state.roots.push_back(note);
                }
            }
            if (a_state.roots.size() > kMaxRoots) {
                a_state.roots.erase(a_state.roots.begin(),
                    a_state.roots.begin() + static_cast<std::ptrdiff_t>(a_state.roots.size() - kMaxRoots));
            }
        }

        // Every palette view we may bind: the fixed ones and every derived one loaded so far (an empty snapshot keeps
        // them too, so the NPC leak check still sees our views after a clear)
        void FillOurs(const MainState& a_state, Snapshot& a_snapshot)
        {
            for (auto* fixed : { GetPalette(), GetAddPalette() }) {
                if (Srv* view = ViewOf(fixed)) {
                    a_snapshot.ours.insert(view);
                }
            }
            for (const auto& [path, texture] : a_state.derived) {
                if (Srv* view = ViewOf(texture)) {
                    a_snapshot.ours.insert(view);
                }
            }
        }

        // Every live root walked again (clones and late 3D attach after the first walk); the set replaced whole
        void Rewalk(MainState& a_state)
        {
            const auto start = std::chrono::steady_clock::now();
            TakeNotes(a_state);
            auto snapshot = std::make_shared<Snapshot>();
            snapshot->generation = ++a_state.generation;
            Owners owners;
            WalkStats stats;
            std::erase_if(a_state.roots, [](const Root& a_root) { return !a_root.handle.get(); });
            for (const auto& root : a_state.roots) {
                const auto ref = root.handle.get();
                auto* object = ref ? ref->Get3D() : nullptr;
                if (!object) {
                    continue;
                }
                ++stats.roots[static_cast<std::size_t>(root.kind)];
                if (g_rootLogs.load(std::memory_order_relaxed) < kRootLogs) {
                    g_rootLogs.fetch_add(1, std::memory_order_relaxed);
                    logger::info("BlackFlame: draw-time grey: walking {} {:08X} (base {:08X})", RootKindName(root.kind),
                        ref->GetFormID(), ref->GetBaseObject() ? ref->GetBaseObject()->GetFormID() : 0);
                }
                Collect(a_state, object, root.kind, *snapshot, owners, stats);
            }
            (void)CollectHands(a_state, *snapshot, owners, stats);
            FillOurs(a_state, *snapshot);  // after the walk: derived palettes it loaded count too
            Publish(a_state, std::move(snapshot), std::move(owners));
            a_state.lastStats = stats;
            const auto spent = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
            GetCounters().rewalkNs.fetch_add(static_cast<std::uint64_t>(spent.count()), std::memory_order_relaxed);
        }

        void Clear(MainState& a_state)
        {
            auto empty = std::make_shared<Snapshot>();
            empty->generation = ++a_state.generation;
            FillOurs(a_state, *empty);
            a_state.roots.clear();
            Publish(a_state, std::move(empty), {});
        }

        void Tick()
        {
            std::lock_guard lock(g_stateMutex);
            auto& state = State();
            if (!IsStoked()) {
                if (state.wasStoked || !state.owners.empty()) {
                    Clear(state);
                }
                ReleaseRetired(state);  // what a clear had to defer (no hook body runs while the buff is off)
                state.wasStoked = false;
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (!state.wasStoked) {
                state.wasStoked = true;
                state.lastSummary = now;
                StartSummaryWindow();
                ResetSlotVote();  // DU1: an unmeasured slot gets a fresh examined-materials budget each buff-on
            }
            Rewalk(state);
            if (now - state.lastSummary >= kSummaryInterval) {
                state.lastSummary = now;
                LogSummary(state.owners.size(), state.roots.size(),
                    std::format("{}; releases deferred {}", state.lastStats.Describe(), state.deferredReleases));
            }
        }

        constexpr int kMaxConsecutiveTickFaults = 3;  // then the tick is off for the session (the set emptied)
        constexpr int kMaxFaultWarns = 10;             // tick-fault warning lines per session
        int g_consecutiveTickFaults = 0;               // main thread only (RunTick)
        int g_faultWarns = 0;                          // main thread only (RunTick)
        std::atomic<bool> g_tickOff{ false };

        // A tick that faulted: the set is emptied (no stale snapshot, no geometry held from a walk that stopped half
        // way); the next tick walks again (GuardedKeep: the step stays on) until kMaxConsecutiveTickFaults
        bool ClearAfterFault()
        {
            // The build uses /EHa (plugins/cmake/CompilerFlags.cmake:45, the vcxproj's ExceptionHandling Async), so
            // the faulted Tick's lock_guard was unwound and released; try_lock is belt-and-braces, never a deadlock
            std::unique_lock lock(g_stateMutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                logger::error("BlackFlame: draw-time grey: the set's lock is still held after a faulted tick - not cleared");
                return false;
            }
            auto& state = State();
            Clear(state);
            state.wasStoked = false;
            return true;
        }

        // Runs a_work under GuardedKeep with every C++ exception caught and logged (a queue task must not let one out)
        template <class F>
        bool RunCaught(const char* a_step, F&& a_work)
        {
            try {
                return GuardedKeep(a_step, std::forward<F>(a_work));
            } catch (const std::exception& e) {
                logger::error("BlackFlame: draw-time grey: {} threw: {}", a_step, e.what());
            } catch (...) {
                logger::error("BlackFlame: draw-time grey: {} threw an exception of an unknown type", a_step);
            }
            return false;
        }

        void RunTick()
        {
            g_tickQueued.store(false);
            if (g_tickOff.load()) {
                return;
            }
            if (RunCaught("draw-time grey tick", [] { Tick(); })) {
                g_consecutiveTickFaults = 0;
                return;
            }
            GetCounters().failures.fetch_add(1, std::memory_order_relaxed);
            bool cleared = false;
            if (!RunCaught("draw-time grey clear after a fault", [&cleared] { cleared = ClearAfterFault(); }) || !cleared) {
                cleared = false;
                GetCounters().failures.fetch_add(1, std::memory_order_relaxed);
            }
            if (++g_consecutiveTickFaults >= kMaxConsecutiveTickFaults) {
                g_tickOff.store(true);
                logger::error("BlackFlame: draw-time grey: the tick failed {} times in a row - rewalks off for the "
                              "session ({})",
                    g_consecutiveTickFaults,
                    cleared ? "the set was emptied: nothing is bound any more" : "the clear failed too: the last set stays");
            } else if (g_faultWarns < kMaxFaultWarns) {
                ++g_faultWarns;
                logger::warn("BlackFlame: draw-time grey: a tick failed ({} of {} in a row) - the set {}; the next tick "
                             "walks again",
                    g_consecutiveTickFaults, kMaxConsecutiveTickFaults, cleared ? "was emptied" : "could not be emptied");
            }
        }

        // Any thread. Never throws: a failed post clears the flag so the next one can go out
        void PostTick() noexcept
        {
            if (g_tickQueued.exchange(true)) {
                return;
            }
            try {
                if (QueueTask("BlackFlameDrawTimeTick", [] { RunTick(); })) {
                    return;
                }
            } catch (const std::exception& e) {
                try {
                    logger::error("BlackFlame: draw-time grey: posting the tick threw: {}", e.what());
                } catch (...) {
                }
            } catch (...) {
            }
            g_tickQueued.store(false);
        }

        // Detached, never joined (as BuffState.cpp's ticker): it only sleeps and posts; nothing leaves it (an exception
        // out of a thread function would end the game)
        void TickerLoop() noexcept
        {
            for (;;) {
                try {
                    std::this_thread::sleep_for(kTickInterval);
                } catch (...) {
                }
                PostTick();
            }
        }
    }

    namespace DrawTime
    {
        void StartSetTicker()
        {
            if (g_tickerStarted.exchange(true)) {
                return;
            }
            std::thread(&TickerLoop).detach();
            logger::info("BlackFlame: draw-time grey: rewalk ticker started ({} ms)", kTickInterval.count());
        }
    }

    void InstallDrawTimeGrey()
    {
        if (!GetConfig().devDrawTimeGrey) {
            return;
        }
        logger::info("BlackFlame: draw-time grey (dev spike, devDrawTimeGrey) on: the player's effect geometries get our "
                     "palette at draw time while stoked");
        if (!MainQueue::Installed()) {
            logger::warn("BlackFlame: draw-time grey: the main-thread queue is not installed - the set cannot be walked on "
                         "the main thread; not started");
            return;
        }
        if (!DrawTime::InstallHook()) {
            logger::warn("BlackFlame: draw-time grey: not hooked - not started");
            return;
        }
        DrawTime::StartSetTicker();
    }

    void NotePlayerRef(RE::TESObjectREFR* a_ref, DrawTime::RootKind a_kind) noexcept
    {
        if (!a_ref || !GetConfig().devDrawTimeGrey || !DrawTime::HookInstalled() || !IsStoked()) {
            return;
        }
        try {
            std::lock_guard lock(g_notesMutex);
            auto& notes = Notes();
            if (notes.size() >= kMaxRoots) {
                notes.erase(notes.begin());
            }
            notes.push_back(Root{ a_ref->GetHandle(), a_kind });
        } catch (...) {
            return;  // called from the engine hooks: a failed note is only a root missed
        }
        PostTick();  // walked at the next drain, not only at the next 100 ms tick
    }

    void ResetDrawTimeGrey()
    {
        if (!GetConfig().devDrawTimeGrey) {
            return;
        }
        {
            std::lock_guard lock(g_notesMutex);
            Notes().clear();
        }
        std::lock_guard lock(g_stateMutex);
        auto& state = State();
        Clear(state);
        state.wasStoked = false;
    }
}
