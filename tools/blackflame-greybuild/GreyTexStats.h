#pragma once

// =============================================================================
// GreyTexStats - textures read for judging: the source the glow rules and the visual-risk check look at
// =============================================================================
// A texture is read as 8-bit RGBA, mip 0 (DirectXTex: BCn decoded, other formats converted, sRGB kept as stored), found
// by its slot text (TextureDataPath) in the run's folders: the --in folders (--from-game: the game's dump), the output
// being written (derived palettes), and our own shipped textures (next to the builder in the tester package, or the
// repository's plugins\BlackFlame\data). A texture in none of them is MISSING when the game said it does not open (the
// request's texture list), UNREAD otherwise; one that is there but does not decode is UNREAD.
// The statistics and the palette simulation are the prototype's (scratchpad glow-rule/texstats.js, palsim.js).

#include "GreyShapes.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GreyBuild
{
    struct RgbaImage
    {
        std::size_t width = 0;
        std::size_t height = 0;
        std::vector<std::uint8_t> pixels;  // RGBA, row after row
        bool srgb = false;                 // the source stored sRGB (the bytes are as stored)

        /// r, g, b, a in 0..1 (a_x, a_y inside the image)
        [[nodiscard]] std::array<double, 4> At(std::size_t a_x, std::size_t a_y) const noexcept;
    };

    enum class TextureRead
    {
        kOk,
        kMissing,  // not in the load order (the game could not open it): the source has the same hole
        kUnread    // not in the folders for another reason, or it does not decode
    };

    /// The prototype's texture statistics (3 decimals where it rounds)
    struct TexStats
    {
        Rgb meanRgb;               // alpha-weighted mean colour
        double meanSat = 0.0;      // its saturation
        double colouredFrac = 0.0; // of the texels with alpha x max channel > kStatLitWeight, the share with S > kStatColouredSat
        double energyMean = 0.0;   // mean of max channel x alpha
        double detail = 0.0;       // mean |1 px gradient| of max channel x alpha over its mean
    };

    struct TextureLook
    {
        TextureRead status = TextureRead::kUnread;
        const RgbaImage* image = nullptr;  // kOk
        const TexStats* stats = nullptr;   // kOk
        std::string path;                  // the Data-relative path ("" when the slot text is none)
        std::string why;                   // kMissing / kUnread
    };

    inline constexpr double kStatLitWeight = 0.1;
    inline constexpr double kStatColouredSat = 0.25;

    class TextureSource
    {
    public:
        /// a_folders searched in order; a_notInGame: the request's textures the game left out, path -> its reason
        TextureSource(std::vector<std::filesystem::path> a_folders, std::map<std::string, std::string> a_notInGame);

        /// The texture a slot names (read once, then cached)
        [[nodiscard]] TextureLook Look(const std::string& a_slot);

        /// Self-test: a texture as if read from the folders (a_image nullopt: there but unreadable)
        void Put(const std::string& a_path, std::optional<RgbaImage> a_image);

        /// Our own shipped textures' folders for this builder: the tester package's mod root (two folders above
        /// tools\BlackFlame\blackflame-greybuild.exe) and the repository's plugins\BlackFlame\data (three above
        /// build\tools\Release\), whichever have a textures folder
        [[nodiscard]] static std::vector<std::filesystem::path> OwnFolders();

    private:
        struct Entry
        {
            TextureRead status = TextureRead::kUnread;
            std::unique_ptr<RgbaImage> image;
            std::unique_ptr<TexStats> stats;
            std::string why;
        };
        std::vector<std::filesystem::path> m_folders;
        std::map<std::string, std::string> m_notInGame;
        std::map<std::string, Entry> m_cache;
    };

    /// Reads a DDS as 8-bit RGBA mip 0; nullopt with a_why on a failure
    [[nodiscard]] std::optional<RgbaImage> ReadRgba(const std::filesystem::path& a_file, std::string& a_why);

    [[nodiscard]] TexStats ComputeStats(const RgbaImage& a_image);

    /// One palette shader through one palette over its texture (the prototype's model, not checked against the game):
    /// rgb = palette(u = texture green, v), alpha = palette alpha (u = texture alpha, v) when palette alpha is on, else the
    /// texture's alpha; sampled on a grid of at most ~128 x 128
    struct PaletteSim
    {
        double energy = 0.0;     // mean luminance x alpha
        double coverage = 0.0;   // mean alpha
        double uRange = 0.0;     // spread of the green values of texels with alpha > kSimLitAlpha
        double darkFrac = 0.0;   // of those texels, the share whose palette colour is darker than kRiskDarkLum
    };

    inline constexpr double kSimLitAlpha = 0.05;
    inline constexpr std::size_t kSimGrid = 128;

    [[nodiscard]] PaletteSim SimulatePalette(const RgbaImage& a_texture, const RgbaImage& a_palette, bool a_paletteAlpha,
        double a_v, double a_darkLum);
}
