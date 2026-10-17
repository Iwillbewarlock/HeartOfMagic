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
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
        double alphaMean = 0.0;    // Z9: mean alpha (a plain shape's light in EFFECT_DIM)
    };

    struct TextureLook
    {
        TextureRead status = TextureRead::kUnread;
        const RgbaImage* image = nullptr;  // kOk
        const TexStats* stats = nullptr;   // kOk
        std::string path;                  // the Data-relative path ("" when the slot text is none)
        std::string why;                   // kMissing / kUnread
        std::filesystem::path file;        // the file it was read from ("" when none was found)
        bool transient = false;            // not read for the machine's sake (a locked file, no memory, the budget, a listed
                                           // file gone): not cached, read again when looked at
        bool gated = false;                // of those, refused by the read gate (which told the object itself)
    };

    inline constexpr double kStatLitWeight = 0.1;
    inline constexpr double kStatColouredSat = 0.25;
    // The mean colour's saturation has no verdict below this brightest channel (the design's S(c) rule, = kRiskMinChannel):
    // a near-black texture's mean (0, 0.004, 0.016) is not "saturated"
    inline constexpr double kStatMinChannel = 0.02;
    inline constexpr double kStatColouredFrac = 0.20;  // = kRiskTexColoredFrac
    inline constexpr double kStatMeanSat = 0.30;       // = kRiskTexMeanSat

    struct TexStats;

    /// The risk check's coloured texture (TEX_COLOUR): kStatColouredFrac of the lit texels coloured, or a saturated mean
    [[nodiscard]] bool IsColouredTexture(const TexStats& a_stats) noexcept;

    class TextureSource
    {
    public:
        /// a_folders searched in order; a_notInGame: the request's textures the game left out, path -> its reason;
        /// a_generated (Y14, Z11): the output being written - the only folder read for a path under our copies folder
        TextureSource(std::vector<std::filesystem::path> a_folders, std::map<std::string, std::string> a_notInGame,
            std::filesystem::path a_generated = {});

        /// The texture a slot names (read once, then cached)
        [[nodiscard]] TextureLook Look(const std::string& a_slot);

        /// Self-test: a texture as if read from the folders (a_image nullopt: there but unreadable)
        void Put(const std::string& a_path, std::optional<RgbaImage> a_image);

        /// The decoded images the cache holds, in bytes (GreyBudget.h counts them against the job memory cap)
        [[nodiscard]] std::uint64_t CachedBytes() const noexcept;

        /// Every cached texture dropped (read again when looked at). No TextureLook of this source may be in use.
        void Clear() noexcept;

        /// Key mode (GreyBudget.h): asked before a file is decoded; false (with its why) leaves the texture unread and
        /// uncached, so a later object asks again
        using ReadGate = std::function<bool(const std::filesystem::path&, std::string&)>;
        void SetReadGate(ReadGate a_gate) { m_gate = std::move(a_gate); }

        /// Key mode: told why when a read fails for the machine's sake (IsTransientResult), so the object fails instead of
        /// being judged on a texture it could not see
        using TransientSink = std::function<void(const std::string&)>;
        void SetTransientSink(TransientSink a_sink) { m_transientSink = std::move(a_sink); }

        /// Key mode: only these paths (TextureDataPath) are read from the folders - the request's dumped textures; any other
        /// file a folder holds (a stale dump left behind) counts as not there. Our own copies (a_generated) are not limited
        void SetListed(std::set<std::string> a_listed) { m_listed = std::move(a_listed); m_limited = true; }

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
            std::filesystem::path file;
        };
        std::vector<std::filesystem::path> m_folders;
        std::filesystem::path m_generated;
        std::map<std::string, std::string> m_notInGame;
        std::map<std::string, Entry> m_cache;
        std::uint64_t m_cachedBytes = 0;
        ReadGate m_gate;
        TransientSink m_transientSink;
        std::set<std::string> m_listed;
        bool m_limited = false;
    };

    /// Reads a DDS as 8-bit RGBA mip 0; nullopt with a_why on a failure
    /// a_transient (when given): the failure is the machine's (IsTransientResult), not the file's
    [[nodiscard]] std::optional<RgbaImage> ReadRgba(const std::filesystem::path& a_file, std::string& a_why, bool* a_transient = nullptr);

    [[nodiscard]] TexStats ComputeStats(const RgbaImage& a_image);

    /// One palette shader through one palette over its texture, as Community Shaders' Effect.hlsl samples it (Y2, Y17):
    /// rgb = palette(u = texture green, v = a_colourRow: base red x vertex red), alpha = palette alpha (u = texture alpha,
    /// v = a_alphaRow: base alpha x vertex alpha) when palette alpha is on, else the texture's alpha; sampled on a grid of
    /// at most ~128 x 128. With a_testThreshold >= 0 (alpha test, Y4) the coverage counts the samples whose alpha passes it
    struct PaletteSim
    {
        double energy = 0.0;     // mean luminance x alpha
        double coverage = 0.0;   // mean alpha (alpha test: the share that passes)
        double uRange = 0.0;     // spread of the green values of texels with alpha > kSimLitAlpha
        double darkFrac = 0.0;   // of those texels, the share whose palette colour is darker than kRiskDarkLum
    };

    inline constexpr double kSimLitAlpha = 0.05;
    inline constexpr std::size_t kSimGrid = 128;

    /// Z8: a_testThreshold >= 0 (alpha test on): a sample whose alpha is below it is discarded (GEQUAL passes); a_cutout
    /// (alpha test without blending): the coverage counts the passing samples as opaque
    [[nodiscard]] PaletteSim SimulatePalette(const RgbaImage& a_texture, const RgbaImage& a_palette, bool a_paletteAlpha,
        double a_colourRow, double a_alphaRow, double a_darkLum, double a_testThreshold = -1.0, bool a_cutout = false);

    // Z7: the sRGB transfer (IEC 61966-2-1), shared by the palette maker and the simulation
    inline constexpr float kSrgbLinearLimit = 0.04045f;     // encoded value below which the curve is linear
    inline constexpr float kSrgbLinearScale = 12.92f;
    inline constexpr float kSrgbOffset = 0.055f;
    inline constexpr float kSrgbGamma = 2.4f;
    inline constexpr float kLinearLimit = 0.0031308f;       // linear value below which the curve is linear

    [[nodiscard]] float SrgbToLinear(float a_value) noexcept;
    [[nodiscard]] float LinearToSrgb(float a_value) noexcept;

    /// The texture a shader samples when its source texture slot is empty (Y1): one white texel (assumed; not checked
    /// against the game's default texture)
    [[nodiscard]] const RgbaImage& WhiteTexel();
}
