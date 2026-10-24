#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using TrackedMutex=std::mutex;
namespace logger {template<class...T>void info(T&&...){} template<class...T>void error(T&&...){} }
namespace REX::W32 {inline unsigned GetCurrentThreadId(){return 1;}}
namespace RE {
using VMHandle=std::uint64_t;
struct TESObjectREFR;
template<class T>struct Ptr {T* p=nullptr;T* get()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p!=nullptr;}};
struct ObjectRefHandle {TESObjectREFR* p=nullptr;Ptr<TESObjectREFR> get()const{return {p};}std::uint32_t native_handle()const;};
struct TESObjectREFR {std::uint32_t id;explicit TESObjectREFR(std::uint32_t v):id(v){}virtual ~TESObjectREFR()=default;ObjectRefHandle GetHandle(){return {this};}template<class T>T* As(){return dynamic_cast<T*>(this);}};
inline std::uint32_t ObjectRefHandle::native_handle()const{return p?p->id:0;}
struct Actor:TESObjectREFR {using TESObjectREFR::TESObjectREFR;};
struct ActiveEffect {static constexpr int VMTYPEID=99;bool visual=true;};
namespace BSScript {
struct Variable {TESObjectREFR* ref=nullptr;template<class T>T Unpack()const{return static_cast<T>(ref);}};
struct Policy {std::unordered_map<VMHandle,ActiveEffect*> effects;bool HandleIsType(int,VMHandle h){return effects.contains(h);}bool IsHandleObjectAvailable(VMHandle h){return effects.contains(h);}void* GetObjectForHandle(int,VMHandle h){return effects[h];}};
namespace Internal {struct VirtualMachine {Policy policy;inline static VirtualMachine* singleton=nullptr;static VirtualMachine* GetSingleton(){return singleton;}Policy* GetObjectHandlePolicy(){return &policy;}};}
}
}
namespace BlackFlame {
inline std::uint32_t testGeneration=1;inline bool testMainThread=true;inline std::vector<std::function<void()>> tasks;
inline std::uint32_t WorldGeneration(){return testGeneration;}inline bool IsOnGameThread(){return testMainThread;}
inline bool IsPlayerVisualEffect(RE::ActiveEffect* e){return e&&e->visual;}
inline constexpr int kMaxThreadLogs=3;
template<class F>bool Guarded(const char*,F f){f();return true;}
template<class F>bool QueueTask(const char*,F f){tasks.emplace_back(f);return true;}
inline void LogThread(const char*,const char*,int){}
}
