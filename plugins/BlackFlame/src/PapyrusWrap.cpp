#include "PapyrusWrap.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "HitEffectHook.h"
#include "SehGuard.h"
#include "ScriptTags.h"

#include <array>
#include <atomic>
#include <string_view>

// Windows.h (pulled in through other headers) maps GetObject to GetObjectW; Variable::GetObject is meant
#ifdef GetObject
#    undef GetObject
#endif

namespace BlackFlame
{
    namespace
    {
        using RE::BSScript::IFunction;

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

        std::array<std::atomic<bool>, kTargets.size()> g_wrapped{};  // per target: bound already

        constexpr int kMaxFrameLogs = 3;
        std::atomic<int> g_frameLogs{ 0 };

        // The script that called the native: the native's own frame is the stack's top, its caller
        // the frame before it. True when its self is a fire effect the player cast (an ActiveMagicEffect
        // script resolves to an ActiveEffect), or a reference already tagged (a placed activator's own
        // script placing or casting in turn).
        bool IsPlayerFireCaller(const RE::BSScript::Stack& a_stack)
        {
            const auto* own = a_stack.top;
            const auto* caller = own ? own->previousFrame : nullptr;
            if (g_frameLogs.fetch_add(1) < kMaxFrameLogs) {
                logger::info("BlackFlame: native wrapper frames: own '{}', caller '{}' (self is an object: {})",
                    own && own->owningFunction ? own->owningFunction->GetName().c_str() : "-",
                    caller && caller->owningFunction ? caller->owningFunction->GetName().c_str() : "-",
                    caller && caller->self.IsObject() ? "yes" : "no");
            }
            if (!caller || !caller->self.IsObject()) {
                return false;
            }
            const auto object = caller->self.GetObject();
            if (!object) {
                return false;
            }
            if (IsPlayerFireEffect(static_cast<RE::ActiveEffect*>(object->Resolve(RE::ActiveEffect::VMTYPEID)))) {
                return true;
            }
            const auto refType = static_cast<RE::VMTypeID>(RE::TESObjectREFR::FORMTYPE);
            return IsTaggedSource(static_cast<RE::TESObjectREFR*>(object->Resolve(refType)));
        }

        // A spell with any fire MGEF (script-cast spells need not be counted kSpell spells)
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

        class NativeProxy final : public IFunction
        {
        public:
            NativeProxy(RE::BSTSmartPointer<IFunction> a_original, Wrapped a_kind) :
                m_original(std::move(a_original)), m_kind(a_kind)
            {}

            const RE::BSFixedString& GetName() const override { return m_original->GetName(); }
            const RE::BSFixedString& GetObjectTypeName() const override { return m_original->GetObjectTypeName(); }
            const RE::BSFixedString& GetStateName() const override { return m_original->GetStateName(); }
            RE::BSScript::TypeInfo GetReturnType() const override { return m_original->GetReturnType(); }
            std::uint32_t GetParamCount() const override { return m_original->GetParamCount(); }
            void GetParam(std::uint32_t a_idx, RE::BSFixedString& a_nameOut, RE::BSScript::TypeInfo& a_typeOut) const override
            {
                m_original->GetParam(a_idx, a_nameOut, a_typeOut);
            }
            std::uint32_t GetStackFrameSize() const override { return m_original->GetStackFrameSize(); }
            bool GetIsNative() const override { return m_original->GetIsNative(); }
            bool GetIsStatic() const override { return m_original->GetIsStatic(); }
            bool GetIsEmpty() const override { return m_original->GetIsEmpty(); }
            FunctionType GetFunctionType() const override { return m_original->GetFunctionType(); }
            std::uint32_t GetUserFlags() const override { return m_original->GetUserFlags(); }
            const RE::BSFixedString& GetDocString() const override { return m_original->GetDocString(); }
            void InsertLocals(RE::BSScript::StackFrame* a_frame) override { m_original->InsertLocals(a_frame); }
            const RE::BSFixedString& GetSourceFilename() const override { return m_original->GetSourceFilename(); }
            bool TranslateIPToLineNumber(std::uint32_t a_indexPtr, std::uint32_t& a_lineNumberOut) const override
            {
                return m_original->TranslateIPToLineNumber(a_indexPtr, a_lineNumberOut);
            }
            bool GetVarNameForStackIndex(std::uint32_t a_idx, RE::BSFixedString& a_nameOut) const override
            {
                return m_original->GetVarNameForStackIndex(a_idx, a_nameOut);
            }
            bool CanBeCalledFromTasklets() const override { return m_original->CanBeCalledFromTasklets(); }
            void SetCallableFromTasklets(bool a_callable) override { m_original->SetCallableFromTasklets(a_callable); }

            CallResult Call(const RE::BSTSmartPointer<RE::BSScript::Stack>& a_stack, RE::BSScript::ErrorLogger* a_logger,
                RE::BSScript::Internal::VirtualMachine* a_vm, bool a_arg4) override
            {
                bool fromPlayerFire = false;
                try {
                    if (a_stack && IsStoked()) {
                        (void)Guarded("native wrapper (before)", [&]() { fromPlayerFire = Before(*a_stack); });
                    }
                } catch (...) {
                    fromPlayerFire = false;  // the original call below runs whatever happened here
                }
                const auto result = m_original->Call(a_stack, a_logger, a_vm, a_arg4);
                try {
                    if (fromPlayerFire && result == CallResult::kCompleted) {
                        (void)Guarded("native wrapper (after)", [&]() { After(*a_stack); });
                    }
                } catch (...) {
                }
                return result;
            }

        private:
            // True when the caller is the player's fire effect; Cast/RemoteCast tag their source here
            bool Before(RE::BSScript::Stack& a_stack)
            {
                if (!IsPlayerFireCaller(a_stack)) {
                    return false;
                }
                if (m_kind == Wrapped::kCast) {
                    auto* frame = a_stack.top;
                    const auto* spell = frame ? frame->self.Unpack<RE::SpellItem*>() : nullptr;
                    if (frame && CarriesFire(spell)) {
                        const auto page = a_stack.GetPageForFrame(frame);
                        TagSource(a_stack.GetStackFrameVariable(frame, 0, page).Unpack<RE::TESObjectREFR*>());
                    }
                    return false;  // nothing to do after the call
                }
                return true;
            }

            // PlaceAtMe completed: its result is in the stack's return value
            void After(RE::BSScript::Stack& a_stack)
            {
                TagPlaced(a_stack.returnValue.Unpack<RE::TESObjectREFR*>());
            }

            RE::BSTSmartPointer<IFunction> m_original;
            Wrapped m_kind;
        };

        RE::BSTSmartPointer<IFunction> FindMember(RE::BSScript::IVirtualMachine* a_vm, const Target& a_target)
        {
            RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> info;
            if (!a_vm->GetScriptObjectType1(a_target.className, info) || !info) {
                return {};
            }
            auto* functions = info->GetMemberFuncIter();
            for (std::uint32_t i = 0; functions && i < info->GetNumMemberFuncs(); ++i) {
                auto& fn = functions[i].func;
                if (fn && std::string_view(fn->GetName().c_str()) == a_target.functionName) {
                    return fn;
                }
            }
            return {};
        }
    }

    void RetryNativeWrappers()
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
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
            const auto& target = kTargets[i];
            if (g_wrapped[i].load()) {
                continue;  // a second FindMember would find our proxy (it answers native too)
            }
            auto original = FindMember(a_vm, target);
            if (!original || !original->GetIsNative()) {
                logger::warn("BlackFlame: {}.{}: no native function found yet - not wrapped (retried after a load)",
                    target.className, target.functionName);
                continue;
            }
            const auto proxy = RE::make_smart<NativeProxy>(original, target.kind);
            const bool bound = a_vm->BindNativeMethod(proxy.get());
            g_wrapped[i].store(bound);
            logger::info("BlackFlame: {}.{} wrapped: {}", target.className, target.functionName,
                bound ? "yes (the original runs inside the wrapper)" : "NO (BindNativeMethod refused)");
        }
        return true;
    }
}
