#include "SreProbe.h"

#include "HitPathAddress.h"
#include "Patch.h"
#include "SehGuard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

namespace BlackFlame::HitPath
{
    namespace
    {
        struct VtableEntry
        {
            std::size_t index;
            const char* name;
            std::uint64_t engineId;  // the engine's own function at this slot, 0: not known here
        };
        // ShaderReferenceEffect.h slot numbers; the ids from the findings (C4, C6, C14; 34912 for Init is presumed there)
        constexpr std::array kEntries{
            VtableEntry{ 0x27, "Detach", 0 },
            VtableEntry{ 0x28, "Update", 34913 },
            VtableEntry{ 0x2D, "SaveGame", 34920 },
            VtableEntry{ 0x2E, "LoadGame", 34921 },
            VtableEntry{ 0x36, "Init", 34912 },
            VtableEntry{ 0x37, "Suspend", 0 },
            VtableEntry{ 0x38, "Resume", 0 },
            VtableEntry{ 0x3B, "UpdatePosition", 0 },
            VtableEntry{ 0x3E, "DetachImpl", 0 },
        };

        struct TextureProbe
        {
            const char* label;
            const char* path;
        };
        constexpr std::array kTextureProbes{
            TextureProbe{ "existing palette, textures\\ prefix", "textures\\effects\\gradients\\GradFireExplosion.dds" },
            TextureProbe{ "existing palette, no prefix (as the EFSH stores it)", "effects\\gradients\\GradFireExplosion.dds" },
            TextureProbe{ "missing file", "textures\\heartofmagic\\blackflame\\probe_missing.dds" },
            TextureProbe{ "second missing file", "textures\\heartofmagic\\blackflame\\probe_missing2.dds" },
        };
        constexpr std::size_t kFirstMissing = 2;

        std::atomic<bool> g_armed{ false };

        std::string Where(std::uintptr_t a_address)
        {
            const auto where = Detail::ModuleOffset(a_address);
            return where.empty() ? std::string("memory no module owns") : where;
        }

        void ReportEntry(const char* a_point, std::uintptr_t a_vtable, const VtableEntry& a_entry)
        {
            const auto slot = a_vtable + a_entry.index * sizeof(std::uintptr_t);
            if (!Detail::IsReadable(slot, sizeof(std::uintptr_t))) {
                logger::warn("BlackFlame: SRE vtable [{}] 0x{:X} {}: slot 0x{:X} not readable", a_point, a_entry.index, a_entry.name,
                    slot);
                return;
            }
            std::uintptr_t to = 0;
            std::memcpy(&to, reinterpret_cast<const void*>(slot), sizeof(to));
            const auto base = REL::Module::get().base();
            const auto text = REL::Module::get().segment(REL::Segment::textx);
            const bool inGame = to >= text.address() && to < text.address() + text.size();
            std::string verdict;
            if (a_entry.engineId != 0 && to == Patch::AddressOf(a_entry.engineId)) {
                verdict = std::format("the engine's own ({})", a_entry.engineId);
            } else if (inGame) {
                verdict = a_entry.engineId != 0 ?
                              std::format("in SkyrimSE.exe at +0x{:X}, not {} (+0x{:X})", to - base, a_entry.engineId,
                                  Patch::AddressOf(a_entry.engineId) - base) :
                              std::format("in SkyrimSE.exe at +0x{:X} (the engine's id not known here)", to - base);
            } else {
                verdict = std::format("outside SkyrimSE.exe's code: {}", Where(to));
            }
            std::string jump;
            if (const auto next = Detail::JumpTarget(to); next != 0) {
                jump = std::format("; its first instruction jumps to {:X} ({})", next, Where(next));
            }
            logger::info("BlackFlame: SRE vtable [{}] 0x{:X} {}: slot 0x{:X} -> {:X} ({}): {}{}", a_point, a_entry.index,
                a_entry.name, slot, to, Where(to), verdict, jump);
        }

        void ReportVtable(const char* a_point)
        {
            const auto vtable = REL::Relocation<std::uintptr_t>{ RE::ShaderReferenceEffect::VTABLE[0] }.address();
            for (const auto& entry : kEntries) {
                if (!GuardedKeep("SRE vtable probe", [&]() { ReportEntry(a_point, vtable, entry); })) {
                    logger::warn("BlackFlame: SRE vtable [{}] 0x{:X} {}: the read faulted - skipped", a_point, entry.index,
                        entry.name);
                }
            }
        }

        // Case-blind, '/' as '\', a leading "textures\" dropped from either side
        std::string NormalPath(std::string_view a_path)
        {
            std::string out(a_path);
            std::ranges::transform(out, out.begin(), [](char a_c) {
                return a_c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(a_c)));
            });
            constexpr std::string_view kPrefix = "textures\\";
            if (out.starts_with(kPrefix)) {
                out.erase(0, kPrefix.size());
            }
            return out;
        }

        void ProbeTextures()
        {
            const auto thread = REX::W32::GetCurrentThreadId();
            const auto* main = RE::Main::GetSingleton();
            std::array<RE::NiPointer<RE::NiTexture>, kTextureProbes.size()> held{};  // released when the probe ends
            for (std::size_t i = 0; i < kTextureProbes.size(); ++i) {
                const auto& probe = kTextureProbes[i];
                RE::BSShaderManager::GetTexture(probe.path, true, held[i], false);
                const auto* texture = held[i].get();
                if (!texture) {
                    logger::info("BlackFlame: GetTexture probe [{}]: '{}': null, thread {}", probe.label, probe.path, thread);
                    continue;
                }
                const char* name = texture->name.c_str();
                const std::string nameText = name && *name ? name : "";
                const auto* source = netimmerse_cast<const RE::NiSourceTexture*>(texture);
                std::string fallback;
                if (i > kFirstMissing) {
                    fallback = std::format(", the same texture as for the first missing file: {}",
                        texture == held[kFirstMissing].get() ? "yes" : "no");
                }
                logger::info("BlackFlame: GetTexture probe [{}]: '{}': 0x{:X}, name '{}' (the path asked for: {}), {}, "
                             "rendererTexture {}{}, thread {} ({})", probe.label, probe.path,
                    reinterpret_cast<std::uintptr_t>(texture), nameText, NormalPath(nameText) == NormalPath(probe.path) ? "yes" : "no",
                    source ? "NiSourceTexture" : "not an NiSourceTexture", !source ? "not read" : source->rendererTexture ? "set" : "null",
                    fallback, thread, main && main->threadID == thread ? "Main's" : "not Main's");
            }
        }
    }

    namespace Detail
    {
        void ArmSreProbes()
        {
            g_armed.store(true);
            ReportVtable("kDataLoaded, inside BlackFlame's handler");
        }
    }

    void RunMainMenuProbes()
    {
        if (!g_armed.load()) {
            return;  // another runtime or missing ids: HitPath::Install logged it
        }
        try {
            ReportVtable("main menu");
            if (!GuardedKeep("GetTexture probe", [&]() { ProbeTextures(); })) {
                logger::warn("BlackFlame: GetTexture probe: a call or read faulted - skipped");
            }
        } catch (const std::exception& e) {
            logger::warn("BlackFlame: SRE probes at the main menu threw ({}) - skipped", e.what());
        }
    }
}
