#include "PapyrusWrap.h"
#include "TrackedMutex.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "GameThread.h"
#include "HitEffectHook.h"
#include "SehGuard.h"
#include "ScriptTags.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

// Windows.h (pulled in through other headers) maps GetObject to GetObjectW; Variable::GetObject is meant
#ifdef GetObject
#    undef GetObject
#endif

namespace BlackFlame
{
    namespace
    {
        using RE::BSScript::IFunction;
        using RE::BSScript::NF_util::NativeFunctionBase;

        enum class Wrapped
        {
            kPlaceAtMe,  // ObjectReference.PlaceAtMe: tag what it returns
            kCast        // Spell.Cast / Spell.RemoteCast: tag akSource (argument 0)
        };

        struct Target
        {
            const char* className;
            const char* functionName;
            Wrapped kind;
        };

        constexpr std::array kTargets{
            Target{ "ObjectReference", "PlaceAtMe", Wrapped::kPlaceAtMe },
            Target{ "Spell", "Cast", Wrapped::kCast },
            Target{ "Spell", "RemoteCast", Wrapped::kCast },
        };

        std::array<std::atomic<bool>, kTargets.size()> g_wrapped{};  // per target: bound and confirmed

        constexpr int kMaxFrameLogs = 3;
        std::atomic<int> g_frameLogs{ 0 };
        std::atomic<bool> g_failureReported{ false };
        std::atomic<int> g_deferLogs{ 0 };

        void ReportFailure()
        {
            if (!g_failureReported.exchange(true)) {
                logger::error("BlackFlame: native wrapper threw; the original native still ran; further failures are "
                              "not logged");
            }
        }

        RE::BSScript::Internal::VirtualMachine* VM()
        {
            return RE::BSScript::Internal::VirtualMachine::GetSingleton();
        }

        // The decision for one wrapped call. On the game thread it is made at once; on a VM thread the
        // tag is stored at once as pending with the caller's VM handle (ScriptTags::TagPending) and judged
        // on the game thread, so no ActiveEffect is read on a VM thread and no cast runs ahead of its tag.
        struct Decision
        {
            RE::VMHandle caller = 0;
            RE::ObjectRefHandle callerRef;  // when the caller's self is a reference (a placed activator)
            RE::ObjectRefHandle subject;    // PlaceAtMe's result, or Cast's akSource
            Wrapped kind = Wrapped::kPlaceAtMe;
        };

        // The caller's handle names an ActiveEffect (type and availability checked first, as
        // Object::Resolve does); safe on any thread (the handle policy only)
        bool IsActiveEffectHandle(RE::VMHandle a_handle)
        {
            auto* vm = VM();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            return a_handle != 0 && policy && policy->HandleIsType(RE::ActiveEffect::VMTYPEID, a_handle) &&
                   policy->IsHandleObjectAvailable(a_handle);
        }

        // Game thread only: reads the ActiveEffect
        bool CallerIsPlayersFireEffect(RE::VMHandle a_handle)
        {
            if (!IsActiveEffectHandle(a_handle)) {
                return false;
            }
            auto* policy = VM()->GetObjectHandlePolicy();
            return IsPlayerFireEffect(static_cast<RE::ActiveEffect*>(policy->GetObjectForHandle(RE::ActiveEffect::VMTYPEID,
                a_handle)));
        }

        void Tag(const Decision& a_decision)
        {
            const auto subject = a_decision.subject.get();
            if (a_decision.kind == Wrapped::kPlaceAtMe) {
                TagPlaced(subject.get());
            } else {
                TagSource(subject.get());
            }
        }

        // A pending tag is judged by a task right away (short-lived effects may be gone by the next tick);
        // what is left: an effect that ended before even that judgement leaves no tag
        void StorePending(const Decision& a_decision, RE::VMHandle a_caller)
        {
            const auto subject = a_decision.subject.get();
            TagPending(subject.get(), a_decision.kind == Wrapped::kPlaceAtMe, a_caller);
            const auto handle = a_decision.subject;
            const auto generation = WorldGeneration();
            QueueTask("BlackFlameTagJudge", [handle, generation]() {
                if (generation == WorldGeneration()) {
                    const auto ref = handle.get();
                    (void)ResolvePending(ref.get());
                }
            });
        }

        void Decide(const Decision& a_decision)
        {
            // A reference already tagged calling in turn (a placed activator's own script): confirmed - tag at
            // once; pending - the new tag waits on the same caller's judgement
            if (const auto ref = a_decision.callerRef.get(); ref) {
                const auto info = TagInfoOf(ref.get());  // one lock: state and caller agree
                if (info.state == TagState::kConfirmed) {
                    Tag(a_decision);
                    return;
                }
                if (info.state == TagState::kPending) {
                    StorePending(a_decision, info.pendingCaller);
                    return;
                }
            }
            if (IsOnGameThread()) {
                if (CallerIsPlayersFireEffect(a_decision.caller)) {
                    Tag(a_decision);
                }
                return;
            }
            // Only ActiveMagicEffect callers go further (trap scripts, quests and the like stop here)
            if (!IsActiveEffectHandle(a_decision.caller)) {
                return;
            }
            if (g_deferLogs.load() < kMaxThreadLogs && g_deferLogs.fetch_add(1) < kMaxThreadLogs) {
                logger::info("BlackFlame: native wrapper called off the game thread (callerThread={}): tag stored "
                             "pending, judged on the game thread", REX::W32::GetCurrentThreadId());
            }
            StorePending(a_decision, a_decision.caller);
        }

        RE::ObjectRefHandle HandleOf(RE::TESObjectREFR* a_ref)
        {
            return a_ref ? a_ref->GetHandle() : RE::ObjectRefHandle{};
        }

        // The calling script: the frame before the native's own. Its self as a VM handle, and as a
        // reference when it is one (resolved through the reference form type's VM type)
        void ReadCaller(const RE::BSScript::StackFrame& a_frame, Decision& a_out)
        {
            const auto* caller = a_frame.previousFrame;
            if (g_frameLogs.load() < kMaxFrameLogs && g_frameLogs.fetch_add(1) < kMaxFrameLogs) {
                logger::info("BlackFlame: native wrapper frames: own '{}', caller '{}' (self is an object: {})",
                    a_frame.owningFunction ? a_frame.owningFunction->GetName().c_str() : "-",
                    caller && caller->owningFunction ? caller->owningFunction->GetName().c_str() : "-",
                    caller && caller->self.IsObject() ? "yes" : "no");
            }
            if (!caller || !caller->self.IsObject()) {
                return;
            }
            const auto object = caller->self.GetObject();
            if (!object) {
                return;
            }
            a_out.caller = object->GetHandle();
            const auto refType = static_cast<RE::VMTypeID>(RE::TESObjectREFR::FORMTYPE);
            a_out.callerRef = HandleOf(static_cast<RE::TESObjectREFR*>(object->Resolve(refType)));
        }

        std::atomic<std::uint64_t> g_placeSwaps{ 0 };
        std::atomic<bool> g_placePendingLogged{ false };

        // C4 (swap point 8): the caller is settled as the player's fire right now - a reference whose tag is confirmed, or on
        // the game thread an ActiveEffect the player cast whose MGEF is fire. A pending tag (a VM thread) is not settled.
        bool SettledNow(const Decision& a_decision)
        {
            if (const auto ref = a_decision.callerRef.get(); ref && TagInfoOf(ref.get()).state == TagState::kConfirmed) {
                return true;
            }
            return IsOnGameThread() && CallerIsPlayersFireEffect(a_decision.caller);
        }

        // PlaceAtMe's akFormToPlace (argument 0) becomes its copy when it is a HAZD, EXPL or PROJ with one and the caller is
        // settled now; only the argument of this call changes (the native's own frame), no engine state
        void SwapPlacedForm(const RE::BSScript::StackFrame& a_frame, const Decision& a_decision)
        {
            if (!GetConfig().copyPlaced) {
                return;
            }
            auto& argument = a_frame.GetStackFrameVariable(0, a_frame.GetPageForFrame());
            auto* form = argument.Unpack<RE::TESForm*>();
            const auto type = form ? form->GetFormType() : RE::FormType::None;
            if (type != RE::FormType::Hazard && type != RE::FormType::Explosion && type != RE::FormType::Projectile) {
                return;
            }
            auto* copy = IsCopy(form) ? nullptr : CopyOf(form);
            if (!copy) {
                return;
            }
            if (!SettledNow(a_decision)) {
                if (!g_placePendingLogged.exchange(true)) {
                    logger::info("BlackFlame: PlaceAtMe of {:08X} (has a copy) from a caller not settled yet (a VM thread): "
                                 "placed as it is, it stays orange; not logged again", form->GetFormID());
                }
                return;
            }
            argument.Pack(copy);
            if (g_placeSwaps.fetch_add(1) == 0) {
                logger::info("BlackFlame: PlaceAtMe: {:08X} placed as its copy {:08X} for the player's fire (first of its kind)",
                    form->GetFormID(), copy->GetFormID());
            }
        }

        // A spell with any fire MGEF (script-cast spells need not be counted kSpell spells). Spell
        // records are immutable after load: safe on any thread
        bool CarriesFire(const RE::SpellItem* a_spell)
        {
            if (!a_spell) {
                return false;
            }
            for (const auto* effect : a_spell->effects) {
                if (effect && IsFireMgefAny(effect->baseEffect)) {
                    return true;
                }
            }
            return false;
        }

        // The engine's own NativeFunctionBase::Call (RELOCATION_ID 97923/104651) runs for this object and
        // dispatches to MarshallAndDispatch (vfunc 0x16), where the original's runs between our two looks.
        class NativeProxy final : public NativeFunctionBase
        {
        public:
            NativeProxy(RE::BSTSmartPointer<IFunction> a_holder, NativeFunctionBase* a_native, Wrapped a_kind) :
                NativeFunctionBase(a_native->GetName().c_str(), a_native->GetObjectTypeName().c_str(),
                    a_native->GetIsStatic(), static_cast<std::uint16_t>(a_native->GetParamCount())),
                m_holder(std::move(a_holder)),
                m_native(a_native),
                m_kind(a_kind)
            {
                _stateName = a_native->GetStateName();
                _retType = a_native->GetReturnType();
                for (std::uint32_t i = 0; i < a_native->GetParamCount(); ++i) {
                    auto& entry = _descTable.entries[i];
                    a_native->GetParam(i, entry.first, entry.second);
                }
                _isCallableFromTasklet = a_native->CanBeCalledFromTasklets();
                _isLatent = a_native->GetIsLatent();
                _userFlags = a_native->GetUserFlags();
                _docString = a_native->GetDocString();
            }

            bool HasStub() const override { return m_native->HasStub(); }

            bool MarshallAndDispatch(RE::BSScript::Variable& a_baseValue, RE::BSScript::Internal::VirtualMachine& a_vm,
                RE::VMStackID a_stackID, RE::BSScript::Variable& a_resultValue,
                const RE::BSScript::StackFrame& a_frame) const override
            {
                Decision decision;
                bool look = false;
                try {
                    if (IsStoked()) {
                        (void)Guarded("native wrapper (before)", [&]() { look = Before(a_baseValue, a_frame, decision); });
                    }
                } catch (...) {
                    look = false;
                    ReportFailure();
                }
                const bool done = m_native->MarshallAndDispatch(a_baseValue, a_vm, a_stackID, a_resultValue, a_frame);
                try {
                    if (look && done) {
                        (void)Guarded("native wrapper (after)", [&]() { After(a_resultValue, decision); });
                    }
                } catch (...) {
                    ReportFailure();
                }
                return done;
            }

        private:
            // Reads what the decision needs; true when there is something to decide after the call
            bool Before(RE::BSScript::Variable& a_baseValue, const RE::BSScript::StackFrame& a_frame, Decision& a_out) const
            {
                a_out.kind = m_kind;
                ReadCaller(a_frame, a_out);
                if (a_out.caller == 0 && !a_out.callerRef.get()) {
                    return false;
                }
                if (m_kind == Wrapped::kCast) {
                    if (CarriesFire(a_baseValue.Unpack<RE::SpellItem*>())) {
                        const auto& source = a_frame.GetStackFrameVariable(0, a_frame.GetPageForFrame());
                        a_out.subject = HandleOf(source.Unpack<RE::TESObjectREFR*>());
                        Decide(a_out);
                    }
                    return false;  // the source is tagged before the cast runs
                }
                SwapPlacedForm(a_frame, a_out);
                return true;
            }

            // PlaceAtMe completed: its result is the result value passed to MarshallAndDispatch - unless the
            // native is latent, when the result comes later and is not read (logged once)
            void After(RE::BSScript::Variable& a_resultValue, Decision& a_decision) const
            {
                if (m_native->GetIsLatent()) {
                    static std::atomic<bool> logged{ false };
                    if (!logged.exchange(true)) {
                        logger::info("BlackFlame: {} is latent: its result is not read, so what it places is not tagged",
                            GetName().c_str());
                    }
                    return;
                }
                a_decision.subject = HandleOf(a_resultValue.Unpack<RE::TESObjectREFR*>());
                Decide(a_decision);
            }

        public:
            [[nodiscard]] const NativeFunctionBase* Original() const noexcept { return m_native; }

        private:
            RE::BSTSmartPointer<IFunction> m_holder;  // keeps the original alive
            NativeFunctionBase* m_native;
            Wrapped m_kind;
        };

        // Our proxies by target, held by our own smart pointer for as long as we report them (stored only when
        // the bind took); recognised by pointer: MSVC RTTI cannot see our class through skyrim_cast. Under
        // g_proxyMutex; heap-held and never freed (no DecRef into the game heap at DLL unload).
        TrackedMutex g_proxyMutex;
        std::array<std::atomic<bool>, kTargets.size()> g_reboundWarned{};

        std::array<RE::BSTSmartPointer<NativeProxy>, kTargets.size()>& Proxies()
        {
            static auto* proxies = std::make_unique<std::array<RE::BSTSmartPointer<NativeProxy>, kTargets.size()>>().release();
            return *proxies;
        }

        RE::BSTSmartPointer<NativeProxy> ProxyOf(std::size_t a_index)
        {
            std::lock_guard lock(g_proxyMutex);
            return Proxies()[a_index];
        }

        bool IsOurProxy(const IFunction* a_fn)
        {
            std::lock_guard lock(g_proxyMutex);
            return std::ranges::any_of(Proxies(), [&](const auto& a_proxy) { return a_proxy && a_proxy.get() == a_fn; });
        }

        bool SameName(const RE::BSFixedString& a_name, const char* a_wanted)
        {
            return a_name.c_str() && _stricmp(a_name.c_str(), a_wanted) == 0;
        }

        RE::BSTSmartPointer<IFunction> FindMember(RE::BSScript::IVirtualMachine* a_vm, const Target& a_target)
        {
            RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> info;
            if (!a_vm->GetScriptObjectType1(a_target.className, info) || !info || !info->IsLinkedValid()) {
                return {};
            }
            auto* functions = info->GetMemberFuncIter();
            for (std::uint32_t i = 0; functions && i < info->GetNumMemberFuncs(); ++i) {
                auto& fn = functions[i].func;
                if (fn && SameName(fn->GetName(), a_target.functionName)) {
                    return fn;
                }
            }
            return {};
        }

        void Wrap(RE::BSScript::IVirtualMachine* a_vm, std::size_t a_index)
        {
            const auto& target = kTargets[a_index];
            auto original = FindMember(a_vm, target);
            if (original && IsOurProxy(original.get())) {
                g_wrapped[a_index].store(true);  // bound earlier, confirmed now
                return;
            }
            auto* native = original ? skyrim_cast<NativeFunctionBase*>(original.get()) : nullptr;
            if (!native) {
                logger::warn("BlackFlame: {}.{}: {} - not wrapped (retried after a load)", target.className,
                    target.functionName, original ? "not a NativeFunctionBase" : "not found or type not linked yet");
                return;
            }
            const auto proxy = RE::make_smart<NativeProxy>(original, native, target.kind);
            (void)a_vm->BindNativeMethod(proxy.get());
            // Replaced only when the type now holds our proxy
            const auto now = FindMember(a_vm, target);
            const bool replaced = now.get() == proxy.get();
            if (replaced) {
                std::lock_guard lock(g_proxyMutex);
                Proxies()[a_index] = proxy;  // our own reference: valid even after another plugin rebinds
            }
            g_wrapped[a_index].store(replaced);
            logger::info("BlackFlame: {}.{}: {} (callable from tasklets: {}, latent: {})", target.className,
                target.functionName, replaced ? "replaced by the wrapper (the original runs inside it)" : "NOT replaced",
                native->CanBeCalledFromTasklets(), native->GetIsLatent());
        }
    }

    void RetryNativeWrappers()
    {
        auto* vm = VM();
        if (vm && GetConfig().scriptPlaced) {
            (void)InstallNativeWrappers(vm);
        }
    }

    bool InstallNativeWrappers(RE::BSScript::IVirtualMachine* a_vm)
    {
        if (!a_vm || !GetConfig().scriptPlaced) {
            return true;
        }
        for (std::size_t i = 0; i < kTargets.size(); ++i) {
            if (!g_wrapped[i].load()) {
                Wrap(a_vm, i);  // our own proxy is recognised by pointer: never wrapped twice
                continue;
            }
            // Wrapped before: is it still ours?
            const auto now = FindMember(a_vm, kTargets[i]);
            const auto ours = ProxyOf(i);
            if (now && now.get() != ours.get() && !g_reboundWarned[i].exchange(true)) {
                logger::warn("BlackFlame: {}.{} was rebound by another plugin after our wrapper - script-placed fire "
                             "from it is no longer tagged", kTargets[i].className, kTargets[i].functionName);
            }
        }
        return true;
    }

    std::vector<WrapperStatus> CheckNativeWrappers()
    {
        std::vector<WrapperStatus> out;
        auto* vm = VM();
        for (std::size_t i = 0; i < kTargets.size(); ++i) {
            WrapperStatus status{ kTargets[i].className, kTargets[i].functionName };
            const auto held = ProxyOf(i);
            const auto* proxy = held.get();
            const auto now = vm ? FindMember(vm, kTargets[i]) : RE::BSTSmartPointer<IFunction>{};
            status.bound = proxy && now.get() == proxy;
            if (proxy) {
                const auto* original = proxy->Original();
                status.matches = proxy->GetParamCount() == original->GetParamCount() &&
                                 proxy->GetIsStatic() == original->GetIsStatic() &&
                                 proxy->GetIsLatent() == original->GetIsLatent() &&
                                 proxy->CanBeCalledFromTasklets() == original->CanBeCalledFromTasklets() &&
                                 proxy->GetReturnType().GetRawType() == original->GetReturnType().GetRawType();
                for (std::uint32_t p = 0; status.matches && p < original->GetParamCount(); ++p) {
                    RE::BSFixedString proxyName, originalName;
                    RE::BSScript::TypeInfo proxyType, originalType;
                    proxy->GetParam(p, proxyName, proxyType);
                    original->GetParam(p, originalName, originalType);
                    status.matches = proxyType.GetRawType() == originalType.GetRawType();
                }
                status.params = original->GetParamCount();
                status.latent = original->GetIsLatent();
            }
            out.push_back(status);
        }
        return out;
    }
}
