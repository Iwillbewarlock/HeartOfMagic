// =============================================================================
// CorpsePalettes - the grey effect shader palettes, loaded at the gate and found by source path (CorpsePalettes.h)
// =============================================================================
// Inputs: this session's request entries (SlotKeys::Inputs efsh: each palette's normalised path and key, contract 6) and the
// builder's line for each key (BuildProgress::Find: a dds line at textures\...\copies\<key16>\palette.dds). A palette is
// taken when its line is ok and its file is in Data (kInData, fnv64 checked) and did not replace another file this session
// (BuildProgress::ReplacedNow: the index may hold the old one; it waits for the next start). Gates: W1 and W2 only (the
// callers, SingleLaunch.cpp): what the worker copies after W2 is shown from the next start, the publication rule.
// The texture manager gives a non-null fallback for a file it cannot load (design v2, C17), so a result is refused when it
// is no NiSourceTexture, is the very texture a path that cannot exist gave (asked first, each gate) or shares its
// rendererTexture (when both are set), or names another file than the one asked for. Asked with the Data-relative path ("textures\..."), then without "textures\" (H6: which
// form GetTexture wants is not verified; the summary line counts each).

#include "CorpsePalettes.h"

#include "BuildProgress.h"
#include "CopyContractEfsh.h"
#include "DataPathRule.h"
#include "SehGuard.h"
#include "SingleLaunch.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace BlackFlame::CorpsePalettes
{
    namespace
    {
        namespace CC = CopyContract;
        namespace Rule = DataPathRule;

        constexpr const char* kAbsentPath = "textures\\heartofmagic\\blackflame\\copies\\no-such-palette.dds";
        constexpr std::size_t kRefusalLogLines = 5;  // refused palettes named one by one, then counted

        struct Registry
        {
            std::shared_mutex mutex;
            std::unordered_map<std::string, RE::NiPointer<RE::NiSourceTexture>> ready;  // index path -> grey texture
            std::size_t built = 0;  // keys with an ok line at the last gate
        };

        Registry& TheRegistry()
        {
            static auto* registry = new Registry();  // never destroyed: the textures outlive the plugin's statics at exit
            return *registry;
        }

        /// The index form: Normalize under textures\ (lower case, '\'), then "textures\" dropped; "" when refused
        std::string IndexPath(std::string_view a_path)
        {
            const auto path = Rule::Normalize(a_path, Rule::kTexturesRoot);
            return path ? path->substr(Rule::kTexturesRoot.size()) : std::string();
        }

        /// What a texture's name says (lower case, '/' as '\', a leading "textures\" dropped)
        std::string NameIndex(const RE::NiTexture& a_texture)
        {
            const char* name = a_texture.name.c_str();
            std::string out = name ? name : "";
            std::ranges::transform(out, out.begin(), [](char a_c) {
                return a_c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(a_c)));
            });
            if (out.starts_with(Rule::kTexturesRoot)) {
                out.erase(0, Rule::kTexturesRoot.size());
            }
            return out;
        }

        RE::NiPointer<RE::NiTexture> Ask(const std::string& a_path)
        {
            RE::NiPointer<RE::NiTexture> out;
            if (!GuardedKeep("corpse palettes: GetTexture", [&]() { RE::BSShaderManager::GetTexture(a_path.c_str(), true, out, false); })) {
                out.reset();
            }
            return out;
        }

        struct Gate
        {
            std::size_t requested = 0;  // request entries this session
            std::size_t keyed = 0;
            std::size_t built = 0;      // an ok line
            std::size_t waiting = 0;    // built, not in Data yet (the next start)
            std::size_t replaced = 0;   // in Data, but replaced another file this session (the next start)
            std::size_t loaded = 0;     // newly taken at this gate
            std::size_t withPrefix = 0; // of them, by the Data-relative path
            std::size_t refused = 0;
            std::size_t rendererSet = 0;  // of the loaded, rendererTexture already set (H6: whether demand loads at once)
        };

        /// a_path's grey texture (a copy path in Data), or null (refused: logged for the first few)
        RE::NiPointer<RE::NiSourceTexture> Take(const std::string& a_path, const RE::NiTexture* a_absent,
            const RE::BSGraphics::Texture* a_absentRenderer, Gate& a_gate)
        {
            const auto wanted = IndexPath(a_path);
            const auto forms = { a_path, a_path.substr(Rule::kTexturesRoot.size()) };
            bool first = true;
            for (const auto& form : forms) {
                const auto texture = Ask(form);
                auto* source = texture ? netimmerse_cast<RE::NiSourceTexture*>(texture.get()) : nullptr;
                // the fallback can come back as another NiSourceTexture over the same renderer texture: refused too (only
                // when both are set; rendererTexture may still be null before the demand load, H6)
                const bool sameRenderer = source && a_absentRenderer && source->rendererTexture == a_absentRenderer;
                if (source && texture.get() != a_absent && !sameRenderer && NameIndex(*texture) == wanted) {
                    a_gate.withPrefix += first ? 1 : 0;
                    a_gate.rendererSet += source->rendererTexture ? 1 : 0;
                    return RE::NiPointer<RE::NiSourceTexture>(source);
                }
                first = false;
            }
            if (++a_gate.refused <= kRefusalLogLines) {
                logger::warn("BlackFlame: corpse palettes: {} is in Data, but the texture manager gave a fallback for it (both path "
                             "forms): that palette stays the source's", a_path);
            }
            return {};
        }
    }

    std::size_t LoadAtGate()
    {
        auto& registry = TheRegistry();
        const auto& palettes = SingleLaunch::Inputs().efsh.palettes;
        Gate gate;
        gate.requested = palettes.size();
        const auto absent = Ask(kAbsentPath);  // the fallback a missing file gets (null when the manager gives none)
        const auto* absentSource = absent ? netimmerse_cast<RE::NiSourceTexture*>(absent.get()) : nullptr;
        const RE::BSGraphics::Texture* absentRenderer = absentSource ? absentSource->rendererTexture : nullptr;
        for (const auto& palette : palettes) {
            if (!palette.key) {
                continue;
            }
            ++gate.keyed;
            const auto object = BuildProgress::Find(*palette.key);
            if (!object || object->line.kind != CC::ProgressKind::kDds || object->line.outcome != CC::Outcome::kOk) {
                continue;  // not built yet, not fire (no line), or failed
            }
            const auto& line = object->line;
            const auto parts = Rule::ParseCopyPath(line.path);
            if (!parts || !parts->palette || parts->key16 != CC::Hex16(*palette.key)) {
                continue;  // the builder's line for this key names no palette copy of it (never written so)
            }
            ++gate.built;
            const auto index = IndexPath(palette.path);
            {
                std::shared_lock lock(registry.mutex);
                if (index.empty() || registry.ready.contains(index)) {
                    continue;
                }
            }
            if (object->state != BuildProgress::State::kInData) {
                ++gate.waiting;
                continue;
            }
            if (BuildProgress::ReplacedNow(line.path, line.fnv64)) {
                ++gate.replaced;
                continue;
            }
            if (auto texture = Take(line.path, absent.get(), absentRenderer, gate)) {
                std::unique_lock lock(registry.mutex);
                registry.ready.emplace(index, std::move(texture));
                ++gate.loaded;
            }
        }
        const auto thread = REX::W32::GetCurrentThreadId();
        const auto* main = RE::Main::GetSingleton();
        std::size_t ready = 0;
        {
            std::unique_lock lock(registry.mutex);
            registry.built = gate.built;
            ready = registry.ready.size();
        }
        logger::info("BlackFlame: corpse palettes: {} grey palettes ready of {} built ({} new at this gate: {} by the textures\\ path, "
                     "{} without it, rendererTexture set on {}); {} requested, {} keyed; waiting for the next start: {} not in Data yet, "
                     "{} replaced this session; {} refused (a fallback texture); thread {} ({})",
            ready, gate.built, gate.loaded, gate.withPrefix, gate.loaded - gate.withPrefix, gate.rendererSet, gate.requested, gate.keyed,
            gate.waiting, gate.replaced, gate.refused, thread, main && main->threadID == thread ? "Main's" : "not Main's");
        return ready;
    }

    RE::NiSourceTexture* Find(std::string_view a_sourcePath) noexcept
    {
        try {
            const auto index = IndexPath(a_sourcePath);
            if (index.empty()) {
                return nullptr;
            }
            auto& registry = TheRegistry();
            std::shared_lock lock(registry.mutex);
            const auto it = registry.ready.find(index);
            return it == registry.ready.end() ? nullptr : it->second.get();
        } catch (const std::exception&) {
            return nullptr;  // bad_alloc building the index text: no grey palette for this call
        }
    }

    std::string Stats()
    {
        auto& registry = TheRegistry();
        std::shared_lock lock(registry.mutex);
        return std::format("{} grey palettes ready of {} built", registry.ready.size(), registry.built);
    }
}
