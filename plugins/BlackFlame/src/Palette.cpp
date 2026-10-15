#include "Palette.h"

#include <array>
#include <atomic>

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
        const char* g_palettePath = nullptr;
        // Set after g_palette and g_palettePath are written; hooks on other threads read it first
        std::atomic<bool> g_ready{ false };
    }

    bool LoadPalette()
    {
        for (const char* path : kPaletteCandidates) {
            RE::NiPointer<RE::NiTexture> texture;
            RE::BSShaderManager::GetTexture(path, true, texture, false);
            auto* source = netimmerse_cast<RE::NiSourceTexture*>(texture.get());
            logger::info("BlackFlame: palette '{}': {}", path,
                source ? "loaded" : (texture ? "loaded but not an NiSourceTexture" : "not loaded"));
            if (source) {
                source->IncRefCount();
                g_palette = source;
                g_palettePath = path;
                g_ready.store(true, std::memory_order_release);
                return true;
            }
        }
        logger::error("BlackFlame: the grey palette did not load from any path - projectile, explosion and hand art "
                      "recolours are off (the burn shader layer still works)");
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
}
