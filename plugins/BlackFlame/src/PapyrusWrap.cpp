#include "PapyrusWrap.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GameThread.h"
#include "HitEffectHook.h"
#include "SehGuard.h"
#include "ScriptTags.h"

#include <array>
#include <atomic>
#include <cstring>

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

        // On the stamped game thread an ActiveEffect may be read; the call is decided at once
        bool IsPlayerFireEffectObject(void* a_effect)
        {
            return IsPlayerFireEffect(static_cast<RE::ActiveEffect*>(a_effect));
        }

        // The decision for one wrapped call, made on the game thread. Off it (a VM thread) the caller is
        // only resolved to its VM handle there and then; a task resolves the handle again on the game
        // thread and reads the ActiveEffect only there (no ActiveEffect is read on a VM thread).
        struct Decision
        {
            RE::VMHandle caller = 0;
            RE::ObjectRefHandle callerRef;  // when the caller's self is a reference (a placed activator)
            RE::ObjectRefHandle subject;    // PlaceAtMe's result, or Cast's akSource
            Wrapped kind = Wrapped::kPlaceAtMe;
        };

        bool CallerIsPlayersFire(const Decision& a_decision)
        {
            if (a_decision.caller != 0) {
                auto* vm = VM();
                auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
                if (policy && IsPlayerFireEffectObject(policy->GetObjectForHandle(RE::ActiveEffect::VMTYPEID,
                                  a_decision.caller))) {
                    return true;
                }
            }
            const auto ref = a_decision.callerRef.get();
            return ref && IsTaggedSource(ref.get());
        }

        void Apply(const Decision& a_decision)
        {
            if (!CallerIsPlayersFire(a_decision)) {
                return;
            }
            const auto subject = a_decision.subject.get();
            if (a_decision.kind == Wrapped::kPlaceAtMe) {
                TagPlaced(subject.get());
            } else {
                TagSource(subject.get());
            }
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

        void Decide(Decision a_decision)
        {
            if (IsOnGameThread()) {
                Apply(a_decision);
                return;
            }
            if (g_deferLogs.load() < kMaxThreadLogs && g_deferLogs.fetch_add(1) < kMaxThreadLogs) {
                logger::info("BlackFlame: native wrapper called off the game thread (callerThread={}): decided by a task",
                    REX::W32::GetCurrentThreadId());
            }
            QueueTask("BlackFlameNativeTag", [a_decision]() { Apply(a_decision); });
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
                return true;
            }

            // PlaceAtMe completed: its result is the result value passed to MarshallAndDispatch
            void After(RE::BSScript::Variable& a_resultValue, Decision& a_decision) const
            {
                a_decision.subject = HandleOf(a_resultValue.Unpack<RE::TESObjectREFR*>());
                Decide(a_decision);
            }

            RE::BSTSmartPointer<IFunction> m_holder;  // keeps the original alive
            NativeFunctionBase* m_native;
            Wrapped m_kind;
        };

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
                Wrap(a_vm, i);  // a second lookup after a success would find our proxy: never wrapped twice
            }
        }
        return true;
    }
}
