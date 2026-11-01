#include "ISLIntegration.h"
#include "ThreadUtils.h"

#include <cstring>
#include <string_view>

// =============================================================================
// DESTPapyrusApi - the Papyrus natives of Don't Eat Spell Tomes (DEST 1.2.2)
//
// With the ISL patch, Heart of Magic's inert DontEatSpellTomes.dll replaces the
// real one, so these natives stand in for it: every script written against the
// DEST API keeps working (ISL through DEST_AliasExt; Rite of Arcana through
// DEST_ReferenceAliasExt, a plain script of DEST's that calls DEST_AliasExt).
// One set holds every listener whatever its kind, saved in the co-save's DEST
// record (ISLIntegration.cpp).
//
// With the real DLL loaded, nothing here is registered: both binding the same
// names, the later one (ours, by load order) took the calls, so scripts registered
// in our set while the real DLL sent the events from its own, and no one heard
// them (LoreRim 5.0.4.8: real DEST and Rite of Arcana, 2026-11-02).
//
// The signatures are DEST 1.2.2's own .psc files:
//   DEST                     int GetAPIVersion(), SendSpellTomeReadEvent(Book, Spell, ObjectReference = None)
//   DEST_AliasExt            Register/UnregisterForSpellTomeReadEvent(Alias)
//   DEST_FormExt             Register/UnregisterForSpellTomeReadEvent(Form)
//   DEST_ActiveMagicEffectExt Register/UnregisterForSpellTomeReadEvent(ActiveMagicEffect)
//   DEST_UIExt               Notification(string, string = "", bool = true)
// =============================================================================

namespace DESTIntegration
{
    namespace
    {
        constexpr std::int32_t kDESTApiVersion = 2;   // DEST.psc: "Current version is 2"

        template <class T>
        void Register(const char* script, T* a_object)
        {
            if (!a_object) {
                logger::warn("{}: RegisterForSpellTomeReadEvent called with None", script);
                return;
            }
            if (SpellTomeListeners().Register(a_object)) {
                logger::info("{}: registered for OnSpellTomeRead", script);
            } else {
                logger::warn("{}: not registered for OnSpellTomeRead (already registered, or no VM handle)", script);
            }
        }

        template <class T>
        void Unregister(const char* script, T* a_object)
        {
            if (!a_object) {
                logger::warn("{}: UnregisterForSpellTomeReadEvent called with None", script);
                return;
            }
            if (SpellTomeListeners().Unregister(a_object)) {
                logger::info("{}: unregistered from OnSpellTomeRead", script);
            }
        }

        void RegisterAlias(RE::StaticFunctionTag*, RE::BGSBaseAlias* a_alias) { Register("DEST_AliasExt", a_alias); }
        void UnregisterAlias(RE::StaticFunctionTag*, RE::BGSBaseAlias* a_alias) { Unregister("DEST_AliasExt", a_alias); }
        void RegisterForm(RE::StaticFunctionTag*, RE::TESForm* a_form) { Register("DEST_FormExt", a_form); }
        void UnregisterForm(RE::StaticFunctionTag*, RE::TESForm* a_form) { Unregister("DEST_FormExt", a_form); }
        void RegisterEffect(RE::StaticFunctionTag*, RE::ActiveEffect* a_effect)
        {
            Register("DEST_ActiveMagicEffectExt", a_effect);
        }
        void UnregisterEffect(RE::StaticFunctionTag*, RE::ActiveEffect* a_effect)
        {
            Unregister("DEST_ActiveMagicEffectExt", a_effect);
        }

        std::int32_t GetAPIVersion(RE::StaticFunctionTag*) { return kDESTApiVersion; }

        void SendSpellTomeReadEvent(RE::StaticFunctionTag*, RE::TESObjectBOOK* a_book, RE::SpellItem* a_spell,
            RE::TESObjectREFR* a_container)
        {
            DispatchSpellTomeRead(a_book, a_spell, a_container);
        }

        void Notification(RE::StaticFunctionTag*, RE::BSFixedString a_text,
            [[maybe_unused]] RE::BSFixedString a_soundID, [[maybe_unused]] bool a_cancelIfQueued)
        {
            if (!a_text.empty()) {
                RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
            }
        }
    }

    // The shim says so by an export since this version; an older shim only by the author
    // its SKSE version data names (unchanged since it was added), so an update that kept the
    // old shim is not taken for the real DLL
    bool RealDESTLoaded()
    {
        constexpr std::string_view kShimAuthor = "Heart of Magic";
        constexpr std::size_t kAuthorOffset = 0x108;   // PluginVersionData::author (Interfaces.h)
        constexpr std::size_t kAuthorSize = 256;
        const HMODULE module = GetModuleHandleW(L"DontEatSpellTomes.dll");
        if (!module) return false;
        if (GetProcAddress(module, "HeartOfMagic_IsDESTShim")) return false;
        if (const auto* data = reinterpret_cast<const char*>(GetProcAddress(module, "SKSEPlugin_Version"))) {
            const char* author = data + kAuthorOffset;
            if (std::string_view(author, strnlen(author, kAuthorSize)).starts_with(kShimAuthor)) return false;
        }
        return true;
    }

    bool RegisterDESTAliasExtFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        if (!vm) return false;
        if (RealDESTLoaded()) {
            logger::info("DESTIntegration: the real DontEatSpellTomes.dll is loaded - it provides the DEST API, "
                         "none of its natives registered here");
            return true;
        }

        constexpr std::string_view kRegister = "RegisterForSpellTomeReadEvent";
        constexpr std::string_view kUnregister = "UnregisterForSpellTomeReadEvent";
        RegisterGameThreadNative<RegisterAlias>(vm, kRegister, "DEST_AliasExt");
        RegisterGameThreadNative<UnregisterAlias>(vm, kUnregister, "DEST_AliasExt");
        RegisterGameThreadNative<RegisterForm>(vm, kRegister, "DEST_FormExt");
        RegisterGameThreadNative<UnregisterForm>(vm, kUnregister, "DEST_FormExt");
        RegisterGameThreadNative<RegisterEffect>(vm, kRegister, "DEST_ActiveMagicEffectExt");
        RegisterGameThreadNative<UnregisterEffect>(vm, kUnregister, "DEST_ActiveMagicEffectExt");
        RegisterGameThreadNative<GetAPIVersion>(vm, "GetAPIVersion", "DEST");
        RegisterGameThreadNative<SendSpellTomeReadEvent>(vm, "SendSpellTomeReadEvent", "DEST");
        RegisterGameThreadNative<Notification>(vm, "Notification", "DEST_UIExt");

        logger::info("DESTIntegration: registered the DEST API natives (DEST, DEST_AliasExt, DEST_FormExt, "
                     "DEST_ActiveMagicEffectExt, DEST_UIExt)");
        return true;
    }
}
