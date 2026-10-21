#include "LanternIed.h"

#include "Config.h"
#include "Forms.h"
#include "GameThread.h"
#include "LanternIedRules.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace BlackFlame::LanternIed
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using Args = RE::BSScript::IFunctionArguments;

        constexpr const char* kScript = "IED";
        constexpr const wchar_t* kIedModule = L"ImmersiveEquipmentDisplays.dll";
        // Under meshes\, as the ESP names lantern_worn.nif (the builder's kLanternEmptyNif)
        constexpr const char* kEmptyModel = "HeartOfMagic\\BlackFlame\\Lantern\\lantern_empty.nif";
        // A call whose result has not come back by then is given up on (the ticker checks)
        constexpr auto kResultWait = std::chrono::seconds(5);
        // SetItemAttachmentModeActor's aiAttachmentMode (IED.psc 1.7.4: "0 = reference, 1 = parent"; PapyrusCustomItemImpl.cpp
        // SetItemAttachmentModeImpl: 1 clears kReferenceMode). Lights On's profiles (flags 0x900010) have neither
        // kReferenceMode (bit 7) nor kSyncReferenceTransform (bit 6): parent mode, no sync
        constexpr int kParentAttachment = 1;
        constexpr bool kSyncReference = false;

        std::atomic<bool> g_ied{ false };       // IED detected at kDataLoaded
        std::atomic<bool> g_attach{ false };    // and lanternIed with the ARMA swapped: the entry is set up at each load
        std::atomic<bool> g_failed{ false };    // the setup failed: no entry set up for the rest of the session (the delete still runs)
        std::atomic<bool> g_pending{ false };   // a call out or waiting (HasPending)
        std::atomic<bool> g_threadWarned{ false };

        // One step: an IED native (its name and sex for the log, the arguments made when it is sent - the player and the
        // worn state read then; nullptr: nothing to send - what a false result means, what to do with the result on the main
        // thread: a_got false when it was not sent, timed out or was not a bool), or a local step with no call
        struct Step
        {
            std::string label;
            std::function<Args*(std::string&)> make;
            const char* falseMeans = nullptr;
            std::function<void(bool a_got, bool a_value)> onResult;
            std::function<void()> local;
        };

        // What the check after an enable step reads back (per sex: 0 male, 1 female)
        struct Check
        {
            bool existsGot = false;
            bool exists = false;
            std::array<bool, 2> enabledGot{};
            std::array<bool, 2> enabled{};
        };

        // Game thread only: the calls waiting, the one out (its sequence number and result handler), this world's generation
        struct Runner
        {
            std::deque<Step> steps;
            bool waiting = false;
            std::uint64_t lastSeq = 0;
            std::uint64_t waitingSeq = 0;
            std::string waitingFor;
            std::function<void(bool, bool)> waitingOnResult;
            Clock::time_point sentAt;
            std::uint32_t generation = 0;
            bool configured = false;     // the setup's enable step was sent: equip events now queue their own
            bool createGot = false;      // CreateItemActor's result came back this setup
            bool createValue = false;    // and what it was
            std::array<bool, 2> sent{};  // the enabled state the last enable call of each sex sent
            Check check;
        };
        Runner g_run;

        // The models SwapArma replaced and their paths before (kDataLoaded; read again only by the restore, main thread)
        struct Swapped
        {
            RE::TESModelTextureSwap* model;
            std::string what;
            std::string before;
        };
        std::vector<Swapped> g_swapped;
        RE::TESObjectARMA* g_arma = nullptr;  // set by SwapArma (the ARMA checks)
        RE::FormID g_armaId = 0;
        bool g_armaCheckedAtLoad = false;     // main thread: the ARMA check at the first game load done

        void RunNext();
        void Enqueue(std::vector<Step> a_steps);
        Step DeleteStep();

        void UpdatePending()
        {
            g_pending.store(g_run.waiting || !g_run.steps.empty());
        }

        const char* SexName(bool a_female)
        {
            return a_female ? "female" : "male";
        }

        bool OnGameThreadOrWarn()
        {
            if (IsOnGameThread()) {
                return true;
            }
            if (!g_threadWarned.exchange(true)) {
                logger::warn("BlackFlame: lantern ied: a task ran off the main thread - no IED call sent");
            }
            return false;
        }

        // Main thread: the ARMA's models back to their paths before the swap, no entry set up for the rest of the session
        // (OnGameReady then deletes the player's entry at each load, as with lanternIed false)
        void SetupFailed(const std::string& a_why)
        {
            if (!IsOnGameThread()) {
                logger::warn("BlackFlame: lantern ied: setup failed off the main thread ({}) - nothing restored", a_why);
                return;
            }
            if (g_failed.exchange(true)) {
                return;
            }
            g_attach.store(false);
            g_run.steps.clear();
            for (auto& s : g_swapped) {
                s.model->SetModel(s.before.c_str());
                logger::info("BlackFlame: lantern ied: ARMA {:08X} {} restored '{}'", g_armaId, s.what, s.before);
            }
            g_swapped.clear();
            logger::warn("BlackFlame: lantern ied: setup failed - the skinned worn mesh is back (re-equip the lantern to see it); "
                         "cause: {}; the player's IED entry is deleted now and at each load", a_why);
            // Now too, so a mid-session failure cannot leave an enabled entry beside the restored skinned mesh until the next load
            Enqueue({ DeleteStep() });
        }

        // Main thread: the result of call a_seq. Only the call being waited for counts: a result that came after its timeout
        // (another call is out by now) is ignored
        void OnResult(std::uint64_t a_seq, bool a_got, bool a_value)
        {
            if (!g_run.waiting || a_seq != g_run.waitingSeq) {
                logger::info("BlackFlame: lantern ied: the result of call #{} ignored (it came after its timeout; {})", a_seq,
                    g_run.waiting ? std::format("call #{} is out", g_run.waitingSeq) : std::string("no call is out"));
                return;
            }
            g_run.waiting = false;
            if (auto handler = std::move(g_run.waitingOnResult)) {
                handler(a_got, a_value);
            }
            RunNext();
        }

        std::string ResultText(const RE::BSScript::Variable& a_result, const char* a_falseMeans)
        {
            if (a_result.IsBool()) {
                const bool value = a_result.GetBool();
                return value || !a_falseMeans ? (value ? "true" : "false") : std::format("false ({})", a_falseMeans);
            }
            if (a_result.IsInt()) {
                return std::to_string(a_result.GetSInt());
            }
            return "no bool (none or another type)";
        }

        // The VM's result of one call: logged, then handed to the main thread (the functor may run on the VM's thread).
        // Nothing may throw out of here; a result lost here is given up on by the ticker after kResultWait
        class StepResult final : public RE::BSScript::IStackCallbackFunctor
        {
        public:
            StepResult(std::string a_label, const char* a_falseMeans, std::uint32_t a_generation, std::uint64_t a_seq) :
                m_label(std::move(a_label)), m_falseMeans(a_falseMeans), m_generation(a_generation), m_seq(a_seq)
            {}

            void operator()(RE::BSScript::Variable a_result) override
            {
                try {
                    logger::info("BlackFlame: lantern ied: {} -> {}", m_label, ResultText(a_result, m_falseMeans));
                    const bool got = a_result.IsBool();
                    const bool value = got && a_result.GetBool();
                    const auto generation = m_generation;
                    const auto seq = m_seq;
                    if (!QueueTask("BlackFlameLanternIed", [generation, seq, got, value]() {
                            if (generation == WorldGeneration() && generation == g_run.generation) {
                                OnResult(seq, got, value);
                            }
                        })) {
                        logger::warn("BlackFlame: lantern ied: {} result not queued for the main thread - the next call waits for "
                                     "the {} s timeout", m_label, kResultWait.count());
                    }
                } catch (...) {
                    // the ticker gives the call up after kResultWait
                }
            }

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

        private:
            std::string m_label;
            const char* m_falseMeans;
            std::uint32_t m_generation;
            std::uint64_t m_seq;
        };

        // IED.GetVersion(): MAKE_PLUGIN_VERSION, major << 16 | minor << 8 | revision (IED version.h). No int: the natives are
        // not what this file calls, so the setup is given up (main thread)
        class VersionResult final : public RE::BSScript::IStackCallbackFunctor
        {
        public:
            void operator()(RE::BSScript::Variable a_result) override
            {
                try {
                    if (!a_result.IsInt()) {
                        if (!QueueTask("BlackFlameLanternIedVersion", []() { SetupFailed("IED.GetVersion() gave no int"); })) {
                            logger::warn("BlackFlame: lantern ied: IED.GetVersion() gave no int, and the fallback could not be queued");
                        }
                        return;
                    }
                    const auto v = static_cast<std::uint32_t>(a_result.GetSInt());
                    logger::info("BlackFlame: lantern ied: IED found (version {}.{}.{}, IED.GetVersion() = {})", (v >> 16) & 0xFF,
                        (v >> 8) & 0xFF, v & 0xFF, v);
                } catch (...) {
                }
            }

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
        };

        // Sends one static call of IED. The arguments are copied into the call's stack before DispatchStaticCall returns
        // (the VM has no way to own them: SKSE passes stack-held arguments to the same entry points), so they are freed here
        bool Dispatch(const char* a_fn, Args* a_args, RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> a_callback)
        {
            const std::unique_ptr<Args> args(a_args);
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            return vm && args && vm->DispatchStaticCall(kScript, a_fn, args.get(), a_callback);
        }

        // Game thread: sends the next calls until one is out (local steps and calls that cannot be sent run straight on)
        void RunNext()
        {
            if (!OnGameThreadOrWarn()) {
                return;
            }
            while (!g_run.waiting && !g_run.steps.empty()) {
                auto step = std::move(g_run.steps.front());
                g_run.steps.pop_front();
                if (step.local) {
                    step.local();
                    continue;
                }
                const std::string fn = step.label.substr(0, step.label.find(' '));
                Args* args = step.make(step.label);
                const char* notSent = nullptr;
                const auto seq = ++g_run.lastSeq;
                if (!args) {
                    notSent = "no player";
                } else if (!Dispatch(fn.c_str(), args,
                               RE::make_smart<StepResult>(step.label, step.falseMeans, g_run.generation, seq))) {
                    notSent = "the VM refused the call";
                }
                if (notSent) {
                    logger::warn("BlackFlame: lantern ied: {} not sent ({})", step.label, notSent);
                    if (step.onResult) {
                        step.onResult(false, false);
                    }
                    continue;
                }
                g_run.waiting = true;
                g_run.waitingSeq = seq;
                g_run.waitingFor = std::move(step.label);
                g_run.waitingOnResult = std::move(step.onResult);
                g_run.sentAt = Clock::now();
            }
            UpdatePending();
        }

        // Game thread: a_steps after the ones already waiting; a new world starts from nothing
        void Enqueue(std::vector<Step> a_steps)
        {
            if (!OnGameThreadOrWarn()) {
                return;
            }
            if (g_run.generation != WorldGeneration()) {
                g_run = Runner{};
                g_run.generation = WorldGeneration();
            }
            for (auto& step : a_steps) {
                g_run.steps.push_back(std::move(step));
            }
            RunNext();
        }

        RE::Actor* Player()
        {
            return RE::PlayerCharacter::GetSingleton();
        }

        bool LanternWorn()
        {
            auto* player = Player();
            const auto* lantern = GetForms().lantern;
            return player && lantern && player->GetWornArmor(lantern->GetFormID()) != nullptr;
        }

        // IED.<a_fn>Actor(player, plugin, "Lantern", a_extra...): the item calls without a sex
        template <class... Extra>
        Step PlayerCall(const char* a_fn, const char* a_falseMeans, Extra... a_extra)
        {
            return Step{ a_fn, [=](std::string&) -> Args* {
                            auto* player = Player();
                            return player ? RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), RE::BSFixedString(kPluginName),
                                                RE::BSFixedString(LanternIedRules::kItemName), Extra(a_extra)...) :
                                            nullptr;
                        },
                a_falseMeans };
        }

        // DeleteItemActor has no sex: the name goes for both
        Step DeleteStep()
        {
            return PlayerCall("DeleteItemActor", "nothing to delete");
        }

        // IED.<a_fn>Actor(player, plugin, "Lantern", female, a_extra...)
        template <class... Extra>
        Step SexCall(const char* a_fn, bool a_female, Extra... a_extra)
        {
            return Step{ std::format("{} {}", a_fn, SexName(a_female)), [=](std::string&) -> Args* {
                            auto* player = Player();
                            return player ? RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), RE::BSFixedString(kPluginName),
                                                RE::BSFixedString(LanternIedRules::kItemName), bool(a_female), Extra(a_extra)...) :
                                            nullptr;
                        } };
        }

        // SetItemEnabledActor for one sex: a_on, or (a_fromWorn) whether the player wears the lantern when it is sent. The
        // state sent is kept for the check; the setup's step also marks the runner configured
        Step EnableCall(bool a_female, bool a_on, bool a_fromWorn)
        {
            return Step{ std::format("SetItemEnabledActor {}", SexName(a_female)), [=](std::string& a_label) -> Args* {
                            auto* player = Player();
                            if (!player) {
                                return nullptr;
                            }
                            const bool on = a_fromWorn ? LanternWorn() : a_on;
                            if (a_fromWorn) {
                                g_run.configured = true;
                            }
                            g_run.sent[a_female ? 1 : 0] = on;
                            a_label += std::format(" {}{}", on ? "on" : "off", a_fromWorn ? " (worn now)" : "");
                            return RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), RE::BSFixedString(kPluginName),
                                RE::BSFixedString(LanternIedRules::kItemName), bool(a_female), bool(on));
                        } };
        }

        // After the enable calls: IED asked whether the entry exists and each sex is enabled, then one PASS/FAIL line per sex
        // (enabled must equal the state the enable call sent: the worn state at the setup, the event's at an equip). A PASS
        // says IED's config holds what was sent, not that the model is attached (that is the look in game). After the setup
        // (a_setup) ItemExistsActor's received result decides it: false -> SetupFailed; no result -> a warning, IED kept
        void AppendCheck(std::vector<Step>& a_steps, bool a_setup)
        {
            Step exists = PlayerCall("ItemExistsActor", nullptr);
            exists.onResult = [](bool a_got, bool a_value) {
                g_run.check.existsGot = a_got;
                g_run.check.exists = a_value;
            };
            a_steps.push_back(std::move(exists));
            for (const bool female : { false, true }) {
                Step enabled = SexCall("ItemEnabledActor", female);
                enabled.onResult = [female](bool a_got, bool a_value) {
                    g_run.check.enabledGot[female ? 1 : 0] = a_got;
                    g_run.check.enabled[female ? 1 : 0] = a_value;
                };
                a_steps.push_back(std::move(enabled));
            }
            Step report;
            report.label = "check";
            report.local = [a_setup]() {
                const auto c = g_run.check;
                for (const bool female : { false, true }) {
                    const std::size_t i = female ? 1 : 0;
                    const bool pass = c.existsGot && c.exists && c.enabledGot[i] && c.enabled[i] == g_run.sent[i];
                    logger::info("BlackFlame: lantern ied check {}: {} exists={} enabled={} sent={}{}", pass ? "PASS" : "FAIL",
                        SexName(female), c.exists, c.enabled[i], g_run.sent[i],
                        c.existsGot && c.enabledGot[i] ? "" : " (a result did not come back)");
                }
                g_run.check = Check{};
                if (!a_setup) {
                    return;
                }
                if (!c.existsGot) {
                    logger::warn("BlackFlame: lantern ied: the setup's ItemExistsActor gave no result - IED kept (the next load sets "
                                 "up again)");
                } else if (!c.exists) {
                    SetupFailed("ItemExistsActor false after the setup");
                }
            };
            a_steps.push_back(std::move(report));
        }

        std::vector<float> Floats(const std::array<float, 3>& a_values)
        {
            return { a_values[0], a_values[1], a_values[2] };
        }

        // The whole setup, in order: create once (IED sets the form, inventory flag and node only on the sex it creates, and
        // refuses a second create of the name), both sexes off, every setter for both, both on as worn now, the check. When
        // the create and the male form both fail, nothing can attach: the ARMA's models go back
        std::vector<Step> SetupSteps()
        {
            auto* form = static_cast<RE::TESForm*>(GetForms().lantern);
            const auto& t = GetConfig().lanternIedTransform;
            const RE::BSFixedString node(LanternIedRules::kNode);
            std::vector<Step> steps;
            steps.push_back(PlayerCall("CreateItemActor", "exists or refused", false, form, true, node));
            steps.back().label = "CreateItemActor male";
            steps.back().onResult = [](bool a_got, bool a_value) {
                g_run.createGot = a_got;
                g_run.createValue = a_value;
            };
            for (const bool female : { false, true }) {
                steps.push_back(EnableCall(female, false, false));
            }
            for (const bool female : { false, true }) {
                steps.push_back(SexCall("SetItemFormActor", female, form));
                if (!female) {
                    // Only two received falses end it here; a missing result is unknown (a slow VM after a load), the check
                    // at the end decides then
                    steps.back().onResult = [](bool a_got, bool a_value) {
                        if (a_got && !a_value && g_run.createGot && !g_run.createValue) {
                            SetupFailed("CreateItemActor and SetItemFormActor male both returned false");
                        }
                    };
                }
                steps.push_back(SexCall("SetItemInventoryActor", female, true));
                steps.push_back(SexCall("SetItemNodeActor", female, node));
                steps.push_back(SexCall("SetItemAttachmentModeActor", female, kParentAttachment, kSyncReference));
                steps.push_back(SexCall("SetItemUseWorldModelActor", female, true));
                steps.push_back(SexCall("SetItemDisableHavokActor", female, true));
                steps.push_back(SexCall("SetItemPositionActor", female, Floats(t.position)));
                steps.push_back(SexCall("SetItemRotationActor", female, Floats(t.rotationDeg)));
                steps.push_back(SexCall("SetItemScaleActor", female, t.scale));
            }
            for (const bool female : { false, true }) {
                steps.push_back(EnableCall(female, false, true));
            }
            AppendCheck(steps, true);
            return steps;
        }

        void QueueGuarded(const char* a_name, std::function<void()> a_work)
        {
            const auto generation = WorldGeneration();
            if (!QueueTask(a_name, [work = std::move(a_work), generation]() {
                    if (generation == WorldGeneration()) {
                        work();
                    }
                })) {
                logger::warn("BlackFlame: lantern ied: task {} not queued for the main thread", a_name);
            }
        }

        // Game thread: the player equipped or unequipped the lantern. Before this load's setup reached its enable step
        // nothing is queued (that step reads the worn state when it is sent)
        void OnEquip(bool a_equipped)
        {
            if (!g_attach.load()) {
                return;
            }
            const char* what = a_equipped ? "equipped" : "unequipped";
            if (g_run.generation != WorldGeneration()) {
                logger::info("BlackFlame: lantern ied: {} before this load's setup started - its enable step reads the worn state", what);
                return;
            }
            if (!g_run.configured) {
                logger::info("BlackFlame: lantern ied: {} during the setup, before its enable step - that step reads the worn state",
                    what);
                return;
            }
            std::vector<Step> steps;
            steps.push_back(EnableCall(false, a_equipped, false));
            steps.push_back(EnableCall(true, a_equipped, false));
            AppendCheck(steps, false);
            Enqueue(std::move(steps));
        }

        class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
        {
        public:
            static EquipSink* GetSingleton()
            {
                static EquipSink instance;
                return &instance;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
            {
                // Called from engine code: nothing may throw out of here; only the actor and the FormID are read
                try {
                    const auto* lantern = GetForms().lantern;
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (a_event && lantern && player && a_event->actor.get() == player && a_event->baseObject == lantern->GetFormID()) {
                        const bool equipped = a_event->equipped;
                        QueueGuarded("BlackFlameLanternIedEquip", [equipped]() { OnEquip(equipped); });
                    }
                } catch (...) {
                    // the next equip or load sets it
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            EquipSink() = default;
            ~EquipSink() override = default;
        };

        // Empty when IED is there, else why not
        std::string Detect()
        {
            if (!REX::W32::GetModuleHandleW(kIedModule)) {
                return "ImmersiveEquipmentDisplays.dll not loaded";
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                return "no Papyrus VM";
            }
            const RE::BSFixedString script(kScript);
            if (vm->TypeIsValid(script)) {
                return {};
            }
            // TypeIsValid may only know the types loaded so far: GetScriptObjectType loads IED.pex when it is there
            RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> type;
            if (vm->GetScriptObjectType(script, type) && type) {
                logger::info("BlackFlame: lantern ied: script type IED loaded by GetScriptObjectType (TypeIsValid said no)");
                return {};
            }
            return "the VM does not know the script type IED (IED.pex missing?)";
        }

        std::string ModelOf(const RE::TESModel& a_model)
        {
            const char* path = a_model.GetModel();
            return path ? path : "";
        }

        // Before any save loads (no lantern 3D yet): both biped models, and the first-person ones when they are set; each
        // replaced path kept for SetupFailed. Then the check that both biped models are the empty one
        void SwapArma(RE::TESObjectARMA& a_arma)
        {
            g_armaId = a_arma.GetFormID();
            const auto swap = [&](RE::TESModelTextureSwap& a_model, std::string a_what) {
                std::string before = ModelOf(a_model);
                a_model.SetModel(kEmptyModel);
                logger::info("BlackFlame: lantern ied: ARMA {:08X} {} '{}' -> '{}'", g_armaId, a_what, before, ModelOf(a_model));
                g_swapped.push_back(Swapped{ &a_model, std::move(a_what), std::move(before) });
            };
            for (const bool female : { false, true }) {
                const auto sex = female ? RE::SEXES::kFemale : RE::SEXES::kMale;
                swap(a_arma.bipedModels[sex], std::format("{} biped model", SexName(female)));
                auto& first = a_arma.bipedModel1stPersons[sex];
                if (ModelOf(first).empty()) {
                    logger::info("BlackFlame: lantern ied: ARMA {:08X} {} first-person model empty - left", g_armaId, SexName(female));
                    continue;
                }
                swap(first, std::format("{} first-person model", SexName(female)));
            }
            g_arma = &a_arma;
        }

        // Both biped models are the empty one (a_when: kDataLoaded, after the swap; the first game load, after every plugin's
        // kDataLoaded and the load: a later change by another plugin shows here). The engine's string pool does not keep case
        // apart: neither does the check
        void CheckArma(const char* a_when)
        {
            if (!g_arma) {
                return;
            }
            const auto male = ModelOf(g_arma->bipedModels[RE::SEXES::kMale]);
            const auto female = ModelOf(g_arma->bipedModels[RE::SEXES::kFemale]);
            const bool pass = _stricmp(male.c_str(), kEmptyModel) == 0 && _stricmp(female.c_str(), kEmptyModel) == 0;
            logger::info("BlackFlame: lantern ied check {}: ARMA {:08X} biped models are the empty model at {} (male '{}', female '{}')",
                pass ? "PASS" : "FAIL", g_armaId, a_when, male, female);
        }
    }

    void Init()
    {
        const auto& forms = GetForms();
        if (!forms.lanternForms) {
            logger::info("BlackFlame: lantern ied off (lantern records missing) - the skinned worn mesh is used");
            return;
        }
        if (const auto why = Detect(); !why.empty()) {
            logger::info("BlackFlame: lantern ied: IED not found ({}) - the skinned worn mesh is used", why);
            return;
        }
        if (!Dispatch("GetVersion", RE::MakeFunctionArguments(), RE::make_smart<VersionResult>())) {
            logger::warn("BlackFlame: lantern ied: IED.GetVersion() not sent (the VM refused the call) - IED is not used, the skinned "
                         "worn mesh is used");
            return;
        }
        g_ied.store(true);
        logger::info("BlackFlame: lantern ied: IED detected (ImmersiveEquipmentDisplays.dll loaded, script type IED known)");
        if (!GetConfig().lanternIed) {
            logger::info("BlackFlame: lantern ied: lanternIed false - the skinned worn mesh is used; the player's IED entry is "
                         "deleted at each load");
            return;
        }
        SwapArma(*forms.lanternArma);
        CheckArma("kDataLoaded");
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) {
            logger::warn("BlackFlame: lantern ied: ScriptEventSourceHolder unavailable - equipping does not show or hide the lantern "
                         "until the next load");
        } else {
            holder->AddEventSink<RE::TESEquipEvent>(EquipSink::GetSingleton());
        }
        g_attach.store(true);
        const auto& t = GetConfig().lanternIedTransform;
        logger::info("BlackFlame: lantern ied on: {} {} pos ({}, {}, {}) rot ({}, {}, {}) deg scale {} on {}, parent attachment",
            GetConfig().lanternIedPreset, GetConfig().lanternIedOverride ? "overridden by lanternIedTransform" : "preset",
            t.position[0], t.position[1], t.position[2], t.rotationDeg[0], t.rotationDeg[1], t.rotationDeg[2], t.scale,
            LanternIedRules::kNode);
    }

    void OnGameReady(const char* a_why, bool a_loaded)
    {
        if (!g_ied.load()) {
            return;
        }
        if (!a_loaded) {
            logger::warn("BlackFlame: lantern ied: {} failed - no IED call", a_why);
            return;
        }
        std::string why = a_why;
        QueueGuarded("BlackFlameLanternIedLoad", [why]() {
            if (g_attach.load()) {
                if (!g_armaCheckedAtLoad) {
                    g_armaCheckedAtLoad = true;
                    CheckArma("the first game load");
                }
                logger::info("BlackFlame: lantern ied: setting up the player's entry ({})", why);
                Enqueue(SetupSteps());
            } else {
                // lanternIed false, or the setup failed this session: a save with an enabled entry must not show the IED copy
                // beside the skinned mesh
                Enqueue({ DeleteStep() });
            }
        });
    }

    void Tick()
    {
        // g_run is the game thread's alone: off it, nothing is touched
        if (!g_pending.load() || !IsOnGameThread()) {
            return;
        }
        if (g_run.generation != WorldGeneration()) {
            g_run = Runner{};  // the world its calls were for is gone; the next load sets up again
            g_run.generation = WorldGeneration();
            UpdatePending();
            return;
        }
        if (g_run.waiting && Clock::now() - g_run.sentAt >= kResultWait) {
            logger::warn("BlackFlame: lantern ied: {} (call #{}) gave no result in {} s - going on", g_run.waitingFor, g_run.waitingSeq,
                kResultWait.count());
            g_run.waiting = false;
            if (auto handler = std::move(g_run.waitingOnResult)) {
                handler(false, false);
            }
        }
        RunNext();
    }

    bool HasPending() noexcept
    {
        return g_pending.load();
    }
}
