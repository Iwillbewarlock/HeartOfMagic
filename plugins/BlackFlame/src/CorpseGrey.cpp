#include "CorpseGrey.h"

#include "Config.h"
#include "CorpseMark.h"
#include "CorpsePalettes.h"
#include "GameClock.h"
#include "HitPathAddress.h"
#include "HitPathAttribution.h"
#include "Patch.h"
#include "SehGuard.h"

#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <string>

namespace BlackFlame::HitPath::Detail
{
    namespace
    {
        using SRE = RE::ShaderReferenceEffect;

        constexpr std::uint64_t kAttachCallerId = 34933;  // ShaderReferenceEffect: waits for the textures, then attaches
        constexpr std::uint64_t kAttachId = 34934;        // the attach (membrane data, particle properties)
        constexpr std::uint64_t kFactoryId = 17325;       // the particle property factory (findings C7, C8)
        constexpr std::uint32_t kAttachSiteOffset = 0x5D1B8D - 0x5D1A60;   // +0x12D, 34934's only caller (findings C6)
        constexpr std::uint32_t kFactorySiteOffset = 0x5D1F1C - 0x5D1C30;  // +0x2EC, 17325's only caller (Follow-up 2)
        // The bytes before each E8 (1.6.1170 image): A `mov rcx, rbx` (the SRE); B `movzx r9d, r15b` (share),
        // `mov r8, [rsi+0xD8]`, `mov rdx, [rsi+0xD0]`, `mov rcx, [rsi+0x108]` (rsi = the SRE): r8 is the member hook A sets
        constexpr std::array<std::uint8_t, 3> kAttachLead{ 0x48, 0x8B, 0xCB };
        constexpr std::array<std::uint8_t, 25> kFactoryLead{ 0x45, 0x0F, 0xB6, 0xCF, 0x4C, 0x8B, 0x86, 0xD8, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0x96, 0xD0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x8E, 0x08, 0x01, 0x00, 0x00 };
        constexpr std::size_t kBytesPerCall = 14;  // an absolute jump in the trampoline per write_call<5>
        constexpr int kMaxAttachLines = 48;
        constexpr int kMaxKeptLines = 48;
        constexpr int kMaxPrivateLines = 20;

        struct Counts
        {
            std::atomic<std::uint64_t> attachCalls{ 0 }, attachMarked{ 0 }, swapped{ 0 }, particleSwaps{ 0 }, membraneSwaps{ 0 },
                keptNoGrey{ 0 }, keptNoPalette{ 0 }, keptOff{ 0 }, factoryCalls{ 0 }, privateCreated{ 0 };
        };

        Counts g_counts;  // the session's; never reset
        std::atomic<int> g_attachLines{ 0 }, g_keptLines{ 0 }, g_privateLines{ 0 };
        std::atomic<bool> g_failureReported{ false };
        bool g_hooked = false;  // written once at kDataLoaded before the write_calls; read-only afterwards

        // Up for 34934's original call while this thread's SRE has its own grey particle palette (hook B reads it)
        thread_local bool t_privateParticles = false;

        class PrivateParticlesScope
        {
        public:
            explicit PrivateParticlesScope(bool a_on) noexcept : _previous(t_privateParticles) { t_privateParticles = a_on; }
            ~PrivateParticlesScope() { t_privateParticles = _previous; }
            PrivateParticlesScope(const PrivateParticlesScope&) = delete;
            PrivateParticlesScope& operator=(const PrivateParticlesScope&) = delete;

        private:
            bool _previous;
        };

        bool TakeLine(std::atomic<int>& a_counter, int a_cap, int& a_line)
        {
            if (a_counter.load(std::memory_order_relaxed) >= a_cap) {
                return false;
            }
            a_line = a_counter.fetch_add(1) + 1;
            return a_line <= a_cap;
        }

        void ReportFailure(const char* a_what)
        {
            if (!g_failureReported.exchange(true)) {
                logger::error("BlackFlame: corpse grey: {} failed (logged above or a C++ exception); the original was called; "
                              "further failures are not logged", a_what);
            }
        }

        std::string TextureText(const RE::NiSourceTexture* a_texture)
        {
            if (!a_texture) {
                return "none";
            }
            const char* name = a_texture->name.c_str();
            return std::format("0x{:X} '{}'", reinterpret_cast<std::uintptr_t>(a_texture), name && *name ? name : "(no name)");
        }

        enum class MemberResult : std::uint8_t
        {
            kNoPath,    // the shader has no such palette (empty TESTexture)
            kNotLoaded, // the path is set but the member is null (the texture did not load): left alone
            kNoGrey,    // CorpsePalettes has none for the path
            kSwapped,
            kAlready    // the member already holds the grey texture
        };

        struct MemberSwap
        {
            const char* label = "";
            std::string source;           // the TESTexture path the SRE loaded the member from
            std::string before;           // the member before the swap
            RE::NiSourceTexture* grey = nullptr;
            MemberResult result = MemberResult::kNoPath;

            [[nodiscard]] bool Grey() const noexcept { return result == MemberResult::kSwapped || result == MemberResult::kAlready; }
        };

        struct Swap
        {
            bool marked = false;
            bool off = false;        // greyCorpseShaders false
            bool noShader = false;   // effectData null
            bool privateParticles = false;  // set before +0xD8 is written: a fault after it still forces share false
            MemberSwap particle{ "particle palette" };
            MemberSwap membrane{ "membrane palette" };
        };

        // Decides one member without writing it
        void Look(MemberSwap& a_out, const RE::NiPointer<RE::NiSourceTexture>& a_member, const RE::TESTexture& a_source)
        {
            const char* path = a_source.textureName.c_str();
            a_out.source = path ? path : "";
            a_out.before = TextureText(a_member.get());
            if (a_out.source.empty()) {
                a_out.result = MemberResult::kNoPath;
                return;
            }
            if (!a_member) {
                a_out.result = MemberResult::kNotLoaded;
                return;
            }
            a_out.grey = CorpsePalettes::Find(a_out.source);
            a_out.result = !a_out.grey                   ? MemberResult::kNoGrey :
                           a_member.get() == a_out.grey ? MemberResult::kAlready :
                                                           MemberResult::kSwapped;
        }

        // Before the original: the SRE is not attached yet, its members are read by 34934 and 17329 only after this
        void PrepareSwap(SRE* a_sre, Swap& a_swap)
        {
            if (!a_sre || (a_sre->flags.underlying() & kCorpseGreyBit) == 0) {
                return;
            }
            a_swap.marked = true;
            if (!GetConfig().greyCorpseShaders) {
                a_swap.off = true;  // the bit rides in saves: a marked SRE loaded with the swap off keeps its colours
                return;
            }
            const auto* shader = a_sre->effectData;
            if (!shader) {
                a_swap.noShader = true;
                return;
            }
            Look(a_swap.particle, a_sre->particlePaletteTexture, shader->particlePaletteTexture);
            Look(a_swap.membrane, a_sre->paletteTexture, shader->membranePaletteTexture);
            if (a_swap.particle.Grey()) {
                a_swap.privateParticles = true;  // first: hook B must see it whenever +0xD8 holds the grey
                a_sre->particlePaletteTexture.reset(a_swap.particle.grey);
            }
            if (a_swap.membrane.Grey()) {
                a_sre->paletteTexture.reset(a_swap.membrane.grey);
            }
        }

        std::string MemberText(const MemberSwap& a_member)
        {
            switch (a_member.result) {
            case MemberResult::kNoPath:
                return std::format("{}: none on the shader", a_member.label);
            case MemberResult::kNotLoaded:
                return std::format("{} '{}': not loaded (member null), kept", a_member.label, a_member.source);
            case MemberResult::kNoGrey:
                return std::format("{} '{}' {}: kept, no grey palette", a_member.label, a_member.source, a_member.before);
            case MemberResult::kAlready:
                return std::format("{} '{}': already grey {}", a_member.label, a_member.source, a_member.before);
            default:
                return std::format("{} '{}' {} -> grey {}", a_member.label, a_member.source, a_member.before,
                    TextureText(a_member.grey));
            }
        }

        std::string SwapText(const Swap& a_swap)
        {
            if (a_swap.off) {
                return "not swapped: greyCorpseShaders false";
            }
            if (a_swap.noShader) {
                return "not swapped: no effectData";
            }
            return std::format("{}; {}; particles private {}", MemberText(a_swap.particle), MemberText(a_swap.membrane),
                a_swap.privateParticles ? "yes" : "no");
        }

        void CountAndKeep(const SRE* a_sre, const Swap& a_swap)
        {
            if (a_swap.off) {
                g_counts.keptOff.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (a_swap.particle.Grey() || a_swap.membrane.Grey()) {
                g_counts.swapped.fetch_add(1, std::memory_order_relaxed);
                g_counts.particleSwaps.fetch_add(a_swap.particle.Grey() ? 1 : 0, std::memory_order_relaxed);
                g_counts.membraneSwaps.fetch_add(a_swap.membrane.Grey() ? 1 : 0, std::memory_order_relaxed);
                return;
            }
            const bool noGrey = a_swap.particle.result == MemberResult::kNoGrey || a_swap.membrane.result == MemberResult::kNoGrey;
            (noGrey ? g_counts.keptNoGrey : g_counts.keptNoPalette).fetch_add(1, std::memory_order_relaxed);
            int line = 0;
            if (!TakeLine(g_keptLines, kMaxKeptLines, line)) {
                return;
            }
            std::string paths;
            for (const auto* member : { &a_swap.particle, &a_swap.membrane }) {
                if (member->result == MemberResult::kNoGrey) {
                    paths += std::format("{}'{}' ({})", paths.empty() ? "" : " and ", member->source, member->label);
                }
            }
            const auto* shader = a_sre->effectData;
            logger::info("BlackFlame: corpse kept [{}]: {}, SRE 0x{:X}, shader {:08X} '{}'", line,
                a_swap.noShader ? std::string("no effectData") :
                noGrey          ? std::format("no grey palette for {}", paths) :
                                  std::format("no palette to grey ({}; {})", MemberText(a_swap.particle), MemberText(a_swap.membrane)),
                reinterpret_cast<std::uintptr_t>(a_sre), shader ? shader->GetFormID() : 0, EditorIdOf(shader));
        }

        // After the original: counted always; an SRE with the bit logged (the run 8 attach line, with the swap)
        void ObserveAttach(SRE* a_sre, const Swap& a_swap)
        {
            if (g_counts.attachCalls.fetch_add(1, std::memory_order_relaxed) == 0) {
                const auto thread = REX::W32::GetCurrentThreadId();
                const auto* main = RE::Main::GetSingleton();
                logger::info("BlackFlame: corpse attach: first 34934 call, thread {} ({}) (H1)", thread,
                    main && main->threadID == thread ? "Main's" : "not Main's");
            }
            if (!a_sre || !a_swap.marked) {
                return;
            }
            g_counts.attachMarked.fetch_add(1, std::memory_order_relaxed);
            CountAndKeep(a_sre, a_swap);
            int line = 0;
            if (!TakeLine(g_attachLines, kMaxAttachLines, line)) {
                return;
            }
            const auto target = a_sre->target.get();
            const auto* shader = a_sre->effectData;
            const auto thread = REX::W32::GetCurrentThreadId();
            const auto* main = RE::Main::GetSingleton();
            logger::info("BlackFlame: corpse attach [{}]: SRE 0x{:X}, shader {:08X} '{}', flags 0x{:08X}, target {:08X}, textures "
                         "particle {}, particle palette {}, fill {}, holes {}, membrane palette {}; {}; after a load {}, game "
                         "{:.1f} s, thread {} ({})", line, reinterpret_cast<std::uintptr_t>(a_sre), shader ? shader->GetFormID() : 0,
                EditorIdOf(shader), a_sre->flags.underlying(), target ? target->GetFormID() : 0,
                TextureText(a_sre->particleShaderTexture.get()), TextureText(a_sre->particlePaletteTexture.get()),
                TextureText(a_sre->fillTexture.get()), TextureText(a_sre->holesTexture.get()),
                TextureText(a_sre->paletteTexture.get()), SwapText(a_swap), IsAfterLoad() ? "yes" : "no", GameClock::Seconds(),
                thread, main && main->threadID == thread ? "Main's" : "not Main's");
        }

        struct AttachHook
        {
            // 34934(this = the SRE), void; rax is handed back as it came (34933 is not known to read it)
            static std::uintptr_t thunk(SRE* a_sre)
            {
                Swap swap;
                try {
                    if (!GuardedKeep("corpse grey swap", [&]() { PrepareSwap(a_sre, swap); })) {
                        ReportFailure("the swap (a fault)");
                    }
                } catch (const std::exception&) {  // not catch (...): /EHa would swallow SEH codes SehGuard passes on
                    ReportFailure("the swap");
                }
                std::uintptr_t result = 0;
                {
                    const PrivateParticlesScope scope(swap.privateParticles);
                    result = func(a_sre);
                }
                try {
                    if (!GuardedKeep("corpse attach line", [&]() { ObserveAttach(a_sre, swap); })) {
                        ReportFailure("the attach line (a fault)");
                    }
                } catch (const std::exception&) {
                    ReportFailure("the attach line");
                }
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        void LogPrivate(const RE::TESEffectShader* a_shader, const RE::NiSourceTexture* a_palette, const void* a_property,
            const void* a_cacheBefore, const void* a_cacheAfter)
        {
            int line = 0;
            if (!TakeLine(g_privateLines, kMaxPrivateLines, line)) {
                return;
            }
            logger::info("BlackFlame: corpse particles private [{}]: shader {:08X} '{}', palette {}, property 0x{:X} (share "
                         "false), the shader's shared cache 0x{:X} -> 0x{:X} ({}), thread {}", line,
                a_shader ? a_shader->GetFormID() : 0, EditorIdOf(a_shader), TextureText(a_palette),
                reinterpret_cast<std::uintptr_t>(a_property), reinterpret_cast<std::uintptr_t>(a_cacheBefore),
                reinterpret_cast<std::uintptr_t>(a_cacheAfter), a_cacheBefore == a_cacheAfter ? "unchanged" : "CHANGED",
                REX::W32::GetCurrentThreadId());
        }

        struct FactoryHook
        {
            // 17325(effectData, base texture, palette, share): the particle property (34934 takes a reference to it)
            static void* thunk(RE::TESEffectShader* a_shader, RE::NiSourceTexture* a_base, RE::NiSourceTexture* a_palette,
                bool a_share)
            {
                g_counts.factoryCalls.fetch_add(1, std::memory_order_relaxed);
                if (!t_privateParticles || !a_share) {
                    return func(a_shader, a_base, a_palette, a_share);
                }
                const void* cacheBefore = a_shader ? a_shader->unk210 : nullptr;
                auto* property = func(a_shader, a_base, a_palette, false);
                g_counts.privateCreated.fetch_add(1, std::memory_order_relaxed);
                try {
                    if (!GuardedKeep("corpse particles private line", [&]() {
                            LogPrivate(a_shader, a_palette, property, cacheBefore, a_shader ? a_shader->unk210 : nullptr);
                        })) {
                        ReportFailure("the private particles line (a fault)");
                    }
                } catch (const std::exception&) {
                    ReportFailure("the private particles line");
                }
                return property;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        template <std::size_t N>
        bool LeadMatches(const char* a_name, std::uintptr_t a_site, const std::array<std::uint8_t, N>& a_lead)
        {
            const auto from = a_site - N;
            if (!Patch::IsExecutable(from, N)) {
                logger::warn("BlackFlame: corpse grey: {}: the {} bytes before the call are not readable code", a_name, N);
                return false;
            }
            if (std::memcmp(reinterpret_cast<const void*>(from), a_lead.data(), N) != 0) {
                logger::warn("BlackFlame: corpse grey: {}: the bytes before the call are {}, expected {} (another build or "
                             "another plugin's patch)", a_name, Patch::HexBytes(from, N), Hex(a_lead.data(), N));
                return false;
            }
            return true;
        }
    }

    void InstallCorpseGrey()
    {
        constexpr const char* kNameA = "corpse grey A (34933 -> 34934)";
        constexpr const char* kNameB = "corpse grey B (34934 -> 17325)";
        if (REL::Module::get().version() != REL::Version(1, 6, 1170, 0)) {
            logger::info("BlackFlame: corpse grey not installed (runtime {}: the sites are 1.6.1170's); the corpse mark has no "
                         "effect", REL::Module::get().version().string("."));
            return;
        }
        const auto siteA = Patch::AddressOf(kAttachCallerId) + kAttachSiteOffset;
        const auto siteB = Patch::AddressOf(kAttachId) + kFactorySiteOffset;
        const auto attach = Patch::AddressOf(kAttachId);
        const auto factory = Patch::AddressOf(kFactoryId);
        // every check runs (each logs its own result) before either site is written
        const bool okA = VerifyCallSite(kNameA, siteA, attach) && LeadMatches(kNameA, siteA, kAttachLead);
        const bool okB = VerifyCallSite(kNameB, siteB, factory) && LeadMatches(kNameB, siteB, kFactoryLead);
        const auto free = SKSE::GetTrampoline().free_size();
        const bool room = free >= 2 * kBytesPerCall;
        if (!okA || !okB || !room) {
            const std::string why = !okA || !okB ? std::string("a site check failed") :
                                                   std::format("the SKSE trampoline has {} free bytes, {} needed", free,
                                                       2 * kBytesPerCall);
            logger::error("BlackFlame: corpse grey not installed ({}; site A {}, site B {}): neither site hooked, the corpse "
                          "mark has no effect", why, okA ? "ok" : "failed", okB ? "ok" : "failed");
            for (const auto& [ok, name, site] : { std::tuple{ okA, kNameA, siteA }, std::tuple{ okB, kNameB, siteB } }) {
                if (ok) {  // a failed check recorded itself (VerifyCallSite) or is in the line above
                    Patch::RecordHook(name, false, site, Patch::HexBytes(site, Patch::kRel32Size), why);
                }
            }
            return;
        }
        // B first: it forwards unchanged until A raises the flag; each original is set before its E8 is written
        FactoryHook::func = decltype(FactoryHook::func){ factory };
        (void)Patch::WriteCall(kNameB, siteB, FactoryHook::thunk, "share false while hook A's private flag is up");
        AttachHook::func = decltype(AttachHook::func){ attach };
        (void)Patch::WriteCall(kNameA, siteA, AttachHook::thunk, "grey palettes into a marked SRE before the original");
        g_hooked = true;
        logger::info("BlackFlame: corpse grey hooked (A 34933 +0x12D, B 34934 +0x2EC); greyCorpseShaders {}: {}",
            GetConfig().greyCorpseShaders ? "true" : "false",
            GetConfig().greyCorpseShaders ? "a marked SRE takes the grey palettes" : "the hooks forward unchanged");
    }

    std::string CorpseGreyStats()
    {
        if (!g_hooked) {
            return "corpse grey not hooked";
        }
        const auto& c = g_counts;
        return std::format("attach 34934 {} calls, {} with the bit: greyed {} (particle palette {}, membrane palette {}), kept "
                           "with no grey palette {}, kept with no palette {}, kept with greyCorpseShaders false {}; particle "
                           "factory {} calls, {} private; {}", c.attachCalls.load(), c.attachMarked.load(), c.swapped.load(),
            c.particleSwaps.load(), c.membraneSwaps.load(), c.keptNoGrey.load(), c.keptNoPalette.load(), c.keptOff.load(),
            c.factoryCalls.load(), c.privateCreated.load(), CorpsePalettes::Stats());
    }
}
