#pragma once
#include <atomic>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include "CatalogFile.h"

namespace Probe {
inline bool catalogReady = false;
inline bool publishOpen = true, proof = true, proofPending = false, queueInstalled = true, queueAccepts = true;
inline int fireSets = 0, readyFireSets = 0, closure = 0, singleLaunch = 0, readyLaunch = 0, hooks = 0;
inline std::vector<std::function<void()>> waiting;
inline void Drain() { auto tasks = std::move(waiting); waiting.clear(); for (auto& f : tasks) f(); }
}
namespace REX::W32 { inline unsigned GetCurrentThreadId() { return 1; } }
namespace logger { template<class... T> void error(T&&...) {} template<class... T> void info(T&&...) {} template<class... T> void warn(T&&...) {} }
namespace BlackFlame {
inline std::atomic<unsigned> g_dataLoadedThreadNativeId{};
struct Config { bool singleLaunch = true; };
inline const Config& GetConfig() { static Config c; return c; }
inline bool LoadForms() { return true; }
template<class T> void MarkFreshnessUnchecked(T&&) {}
inline void BuildFireSets() { ++Probe::fireSets; if (ReadCatalogFile()) ++Probe::readyFireSets; }
struct FireClosure {};
inline FireClosure BuildFireClosure() { ++Probe::closure; return {}; }
inline void PrepareCopyRequest(const FireClosure&) {}
inline void CheckFreshness(const FireClosure&) {}
namespace SingleLaunch { inline void OnDataLoaded(const FireClosure&) { ++Probe::singleLaunch; if (ReadCatalogFile()) ++Probe::readyLaunch; } }
namespace FileIndex { inline void Init() {} }
namespace SlotLedger { inline bool Load() { return true; } }
namespace MenuWatch {
inline void Install() {} inline void OnMainMenuOpen(std::function<void()>) {}
inline bool PublishOpen() { return Probe::publishOpen; }
inline bool DrainMayPublish() { return Probe::publishOpen && Probe::proof; }
inline bool ProofPending() { return Probe::proofPending; }
}
namespace MainQueue {
inline bool Installed() { return Probe::queueInstalled; }
inline bool Post(const char*, std::function<void()> task) {
    if (!Probe::queueAccepts) return false;
    Probe::waiting.push_back(std::move(task)); return true;
}
}
namespace LanternDamage { inline void Init() {} }
namespace LanternIed { inline void Init() {} }
namespace Watchdog { inline void Start() {} }
namespace Tester { inline void WriteStartup() {} }
namespace DevAutomation { inline void Install() {} }
namespace HitPath { inline void Install() {} inline void RegisterMagicApplySink() {} inline void LogHitShaderProbe(const char*) {} inline void RunMainMenuProbes() {} }
struct EffectEventSink { static EffectEventSink* GetSingleton() { static EffectEventSink x; return &x; } void Register() {} };
struct HitEventSink { static HitEventSink* GetSingleton() { static HitEventSink x; return &x; } void Register() {} };
inline void LoadAndFillCopies() {}
inline void LoadGreyMap() {}
inline bool LoadPalette() { return true; }
inline void QueueCopyModelProbe() {}
inline void InitLanternPlace() {}
inline void StartTicker() {}
inline void InstallProjectileHooks() { ++Probe::hooks; }
inline void InstallExplosionHook() {}
inline void InstallHazardHook() {}
inline void InstallHitEffectHooks() {}
inline void InstallImpactSwap() {}
inline void InstallCopySwaps() {}
inline void InstallHazardSwap() {}
inline void RunGreyCheck() {}
}
