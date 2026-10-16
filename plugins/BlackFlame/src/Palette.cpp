#include "Palette.h"

#include <array>
#include <atomic>
#include <cctype>
#include <string>
#include <string_view>

namespace BlackFlame
{
    namespace
    {
        // Data-relative with the textures\ prefix first: the form other plugins pass to
        // BSShaderManager::GetTexture (e.g. Community Shaders' Flowmap.cpp)
        constexpr std::array kPaletteCandidates{
            "textures\\HeartOfMagic\\BlackFlame\\GradBlackFlame.dds",
            "HeartOfMagic\\BlackFlame\\GradBlackFlame.dds",
        };

        // A raw pointer with one reference taken and never given back: an NiPointer in a static
        // would DecRef at DLL unload, after the renderer and texture manager are gone. The
        // palette lives for the whole session anyway (an intentional one-texture leak).
        RE::NiSourceTexture* g_palette = nullptr;
        // v2i: the additive palette, loaded the same way (same one-reference leak); written before g_ready
        RE::NiSourceTexture* g_paletteAdd = nullptr;
        constexpr std::array kAddPaletteCandidates{
            "textures\\HeartOfMagic\\BlackFlame\\GradBlackFlameAdd.dds",
            "HeartOfMagic\\BlackFlame\\GradBlackFlameAdd.dds",
        };
        const char* g_palettePath = nullptr;
        // Set after g_palette and g_palettePath are written; hooks on other threads read it first
        std::atomic<bool> g_ready{ false };

        constexpr std::array<std::string_view, 2> kIgnoredPathPrefixes{ "data\\", "textures\\" };

        std::string NormalizePath(std::string_view a_path)
        {
            std::string out;
            out.reserve(a_path.size());
            for (const char c : a_path) {
                out.push_back(c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            for (const std::string_view prefix : kIgnoredPathPrefixes) {
                if (out.starts_with(prefix)) {
                    out.erase(0, prefix.size());
                }
            }
            return out;
        }
    }

    namespace
    {
        RE::NiSourceTexture* LoadTexture(const char* a_path, const char* a_what)
        {
            RE::NiPointer<RE::NiTexture> texture;
            RE::BSShaderManager::GetTexture(a_path, true, texture, false);
            auto* source = netimmerse_cast<RE::NiSourceTexture*>(texture.get());
            logger::info("BlackFlame: {} '{}': {}", a_what, a_path,
                source ? "loaded" : (texture ? "loaded but not an NiSourceTexture" : "not loaded"));
            if (source) {
                source->IncRefCount();
            }
            return source;
        }

        void LoadAddPalette()
        {
            for (const char* path : kAddPaletteCandidates) {
                if ((g_paletteAdd = LoadTexture(path, "additive palette")) != nullptr) {
                    return;
                }
            }
            logger::warn("BlackFlame: the additive palette (GradBlackFlameAdd.dds) did not load from any path - copies built "
                         "with it show their additive palette shapes without colour; the grey palette is not affected");
        }
    }

    bool LoadPalette()
    {
        LoadAddPalette();
        for (const char* path : kPaletteCandidates) {
            if (auto* source = LoadTexture(path, "palette")) {
                g_palette = source;
                g_palettePath = path;
                g_ready.store(true, std::memory_order_release);
                return true;
            }
        }
        logger::error("BlackFlame: the grey palette did not load from any path - the kept runtime recolours (hitArt, handArt, "
                      "boundWeapon) are off (the burn shader layer and the copies still work)");
        return false;
    }

    bool PaletteReady() noexcept
    {
        return g_ready.load(std::memory_order_acquire);
    }

    RE::NiSourceTexture* GetPalette() noexcept
    {
        return PaletteReady() ? g_palette : nullptr;
    }

    const char* GetPalettePath() noexcept
    {
        return PaletteReady() ? g_palettePath : nullptr;
    }

    bool IsPalettePath(const RE::BSFixedString& a_path)
    {
        const char* ours = GetPalettePath();
        return ours && !a_path.empty() && NormalizePath(a_path.c_str()) == NormalizePath(ours);
    }

    RE::NiSourceTexture* GetAddPalette() noexcept
    {
        return PaletteReady() ? g_paletteAdd : nullptr;
    }

    bool IsAddPalettePath(const RE::BSFixedString& a_path)
    {
        return !a_path.empty() && NormalizePath(a_path.c_str()) == NormalizePath(kAddPaletteCandidates[0]);
    }

    bool IsAnyPalettePath(const RE::BSFixedString& a_path)
    {
        return IsPalettePath(a_path) || IsAddPalettePath(a_path);
    }
}
