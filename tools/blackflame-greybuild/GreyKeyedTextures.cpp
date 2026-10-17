// =============================================================================
// GreyKeyedTextures - key mode's child objects: texture and derived palette copies at copies\<key16>\ (GreyKeyed.h)
// =============================================================================
// A child's key: the request's prefix text + the contract's texture or palette tail over the source file's own bytes
// (hashed here, from the dump). Its file: DataPathRule::CopyPath(textures\, key16, source). Each child is judged once per
// run (by role or blend and source path); every object that names it gets its dep, fallback or failure. A texture child:
//   not in the dump               outside the dump caps ("over the ... cap"): the object not_dumped; else fallback (left)
//   reusable line in staging      the slot names the staged copy (no rewrite)
//   estimate over the job cap     a too_large line for the child (its cap in the reason), the object too_large
//   over the cap with the process / not measurable later    failed (retryable)
//   recoloured                    <path>.tmp, renamed, an ok dds line, the object's dep
//   already grey (tint 1)         the slot stays (nothing to say)
//   recolour failed               transient (I/O, out of memory): failed; else (format, decode): fallback, the slot stays

#include "GreyKeyed.h"

#include "GreyFormsTxst.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <algorithm>
#include <fmt/format.h>
#include <iostream>

namespace GreyBuild
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;
        namespace Rule = BlackFlame::DataPathRule;

        constexpr const char* kOverTheCap = "over the";  // the DLL's reason for a texture outside the dump caps (CopyRequestData.cpp)

        [[nodiscard]] CC::ProgressLine DdsLine(std::uint64_t a_key, CC::Outcome a_outcome, const std::string& a_reason)
        {
            CC::ProgressLine line;
            line.kind = CC::ProgressKind::kDds;
            line.key = a_key;
            line.outcome = a_outcome;
            line.reason = a_reason;
            return line;
        }

        void Remove(const std::filesystem::path& a_file)
        {
            std::error_code ec;
            std::filesystem::remove(a_file, ec);
        }
    }

    KeyedOutputs::KeyedOutputs(ProgressLog& a_log, Budget& a_budget, std::string a_prefix, TextureJob& a_textures) :
        m_log(a_log), m_budget(a_budget), m_prefix(std::move(a_prefix)), m_textures(a_textures)
    {}

    void KeyedOutputs::Fallback(const std::string& a_why)
    {
        if (m_object && std::find(m_object->fallbacks.begin(), m_object->fallbacks.end(), a_why) == m_object->fallbacks.end()) {
            m_object->fallbacks.push_back(a_why);
        }
    }

    void KeyedOutputs::Failed(const std::string& a_why)
    {
        if (m_object && m_object->failed.empty()) {
            m_object->failed = a_why;
        }
    }

    void KeyedOutputs::Terminal(CC::Outcome a_outcome, const std::string& a_why)
    {
        if (m_object && !m_object->terminal) {
            m_object->terminal = a_outcome;
            m_object->terminalWhy = a_why;
        }
    }

    std::string KeyedOutputs::Apply(const Child& a_child)
    {
        switch (a_child.state) {
        case State::kCopy:
            if (m_object) {
                m_object->AddDep(a_child.slot, a_child.fnv64);
            }
            return a_child.slot;
        case State::kFallback:
            Fallback(a_child.why);
            break;
        case State::kFailed:
            Failed(a_child.why);
            break;
        case State::kTerminal:
            Terminal(a_child.terminal, a_child.why);
            break;
        case State::kLeft:
            break;
        }
        return {};
    }

    std::optional<KeyedOutputs::Child> KeyedOutputs::Reused(std::uint64_t a_key, const std::string& a_slot)
    {
        if (!m_log.Reusable(a_key, m_budget.Cap())) {
            return std::nullopt;
        }
        const auto* line = m_log.Last(a_key);
        Child child;
        if (CC::IsPublishable(line->outcome) && line->path == a_slot) {
            child.state = State::kCopy;
            child.slot = a_slot;
            child.fnv64 = line->fnv64;
            return child;
        }
        if (CC::IsTerminal(line->outcome)) {
            child.state = State::kTerminal;
            child.terminal = line->outcome;
            child.why = fmt::format("{}: {}", a_slot, line->reason);
            return child;
        }
        return std::nullopt;
    }

    KeyedOutputs::Child KeyedOutputs::Commit(const std::filesystem::path& a_temp, const std::string& a_slot, std::uint64_t a_key)
    {
        Child child;
        std::string why;
        const auto digest = DigestFile(a_temp, why);
        if (!digest || !CommitTemp(a_temp, StagedFile(m_log.Staging(), a_slot), why)) {
            Remove(a_temp);
            child.why = why;
            return child;  // failed
        }
        m_log.NoteWritten(a_slot, *digest);
        CC::ProgressLine line = DdsLine(a_key, CC::Outcome::kOk, "");
        line.path = a_slot;
        line.bytes = digest->bytes;
        line.fnv64 = digest->fnv64;
        if (!m_log.Append(line, why)) {
            child.why = a_slot + ": " + why;
            return child;  // failed: no line, so the next run builds it again
        }
        child.state = State::kCopy;
        child.slot = a_slot;
        child.fnv64 = digest->fnv64;
        return child;
    }

    KeyedOutputs::Child KeyedOutputs::MakeTexture(const std::string& a_path, TextureRole a_role, float a_tint, const std::string& a_where)
    {
        Child child;
        const auto source = FindSource(m_textures.inputs, a_path);
        if (!source) {
            const auto game = m_textures.notDumped.find(a_path);
            const std::string said = game == m_textures.notDumped.end() ? std::string("not listed") : game->second;
            ++m_textures.totals.missing;
            std::cout << fmt::format("TEX   MISS {} ({}, first in {}): not in the dump - the game: {}\n", a_path, TextureRoleName(a_role),
                a_where, said);
            if (game != m_textures.notDumped.end() && StartsWith(game->second, kOverTheCap)) {
                child.state = State::kTerminal;
                child.terminal = CC::Outcome::kNotDumped;
                child.why = fmt::format("texture {} is outside the dump caps ({})", a_path, game->second);
            } else {
                child.state = State::kFallback;
                child.why = fmt::format("texture {} is not in the dump ({}): slot left", a_path, said);
            }
            return child;
        }
        std::string why;
        const auto digest = DigestFile(*source, why);
        const auto keyText = digest ? ChildTextureKeyString(m_prefix, a_role, a_path, digest->fnv64) : std::nullopt;
        if (!keyText) {
            child.why = digest ? fmt::format("texture {}: the contract gives it no key", a_path) : why;
            return child;  // failed
        }
        const auto key = CC::KeyOf(*keyText);
        const auto slot = Rule::CopyPath(Rule::kTexturesRoot, CC::Hex16(key), a_path);
        if (!slot || !IsAscii(*slot)) {  // a progress line is UTF-8: a code page name cannot go in (P0's ASCII basenames)
            child.state = State::kFallback;
            child.why = fmt::format("texture {}: no ASCII copy path for it: slot left", a_path);
            return child;
        }
        if (auto reused = Reused(key, *slot)) {
            return *reused;
        }
        bool transient = false;
        const auto estimate = EstimateTextureBytes(*source, why, &transient);
        if (!estimate) {
            child.state = transient ? State::kFailed : State::kFallback;  // RecolourDds would refuse the same bytes
            child.why = fmt::format("texture {}: {}{}", a_path, why, transient ? "" : ": slot left");
            return child;
        }
        switch (m_budget.Acquire(*estimate, why)) {
        case Admission::kTooLarge:
            if (std::string lineWhy; !m_log.Append(DdsLine(key, CC::Outcome::kTooLarge, why), lineWhy)) {
                std::cout << fmt::format("WARN  the too_large line of {} was not written: {}\n", *slot, lineWhy);
            }
            std::cout << fmt::format("TEX   LARGE {} ({}): {}\n", a_path, TextureRoleName(a_role), why);
            child.state = State::kTerminal;
            child.terminal = CC::Outcome::kTooLarge;
            child.why = fmt::format("texture {}: {}", a_path, why);
            return child;
        case Admission::kAdmit:
            break;
        default:
            child.why = fmt::format("texture {}: {}", a_path, why);
            return child;  // failed
        }
        const auto temp = TempPathOf(StagedFile(m_log.Staging(), *slot));
        const auto result = RecolourDds(*source, temp, a_tint);
        switch (result.outcome) {
        case RecolourOutcome::kRecoloured:
            child = Commit(temp, *slot, key);
            if (child.state == State::kCopy) {
                ++m_textures.totals.recoloured;
                m_textures.totals.bytes += result.bytes;
                std::cout << fmt::format("TEX   OK   {} -> {} ({}, tint {}, {}, {} mips, {} bytes)\n", a_path, *slot,
                    TextureRoleName(a_role), a_tint, result.format, result.mips, result.bytes);
            }
            return child;
        case RecolourOutcome::kGreyscale:
            ++m_textures.totals.greyscale;
            std::cout << fmt::format("TEX   GREY {} ({}): {}; slot left\n", a_path, result.format, result.reason);
            child.state = State::kLeft;
            return child;
        default:
            Remove(temp);
            ++m_textures.totals.failed;
            std::cout << fmt::format("TEX   FAIL {} ({}, first in {}): {}{}\n", a_path, TextureRoleName(a_role), a_where, result.reason,
                result.transient ? " (tried again at the next run)" : "; slot left");
            child.state = result.transient ? State::kFailed : State::kFallback;
            child.why = fmt::format("texture {}: {}{}", a_path, result.reason, result.transient ? "" : ": slot left");
            return child;
        }
    }

    std::string KeyedOutputs::Texture(const std::string& a_path, TextureRole a_role, float a_tint, const std::string& a_where)
    {
        const std::string id = fmt::format("{}|{}", TextureRoleName(a_role), a_path);
        auto it = m_children.find(id);
        if (it == m_children.end()) {
            it = m_children.emplace(id, MakeTexture(a_path, a_role, a_tint, a_where)).first;
        }
        return Apply(it->second);
    }

    KeyedOutputs::Child KeyedOutputs::MakePalette(const TextureLook& a_source, bool a_additive, const MakeImage& a_make,
        const WriteImage& a_write)
    {
        Child child;
        std::string why;
        const auto digest = a_source.file.empty() ? std::nullopt : DigestFile(a_source.file, why);
        const auto keyText = digest ? ChildPaletteKeyString(m_prefix, a_additive, a_source.path, digest->fnv64) : std::nullopt;
        if (!keyText) {
            child.why = fmt::format("source palette {}: {}", a_source.path, digest ? "the contract gives it no key" :
                                    a_source.file.empty() ? "read from no file" : why);
            return child;  // failed
        }
        const auto key = CC::KeyOf(*keyText);
        const auto slot = Rule::CopyPath(Rule::kTexturesRoot, CC::Hex16(key), a_source.path);
        if (!slot || !IsAscii(*slot)) {
            child.state = State::kFallback;
            child.why = fmt::format("source palette {}: no ASCII copy path for it: today's palette kept", a_source.path);
            return child;
        }
        m_paletteBlends[*slot] = a_additive;
        if (auto reused = Reused(key, *slot)) {
            return *reused;
        }
        RgbaImage image = *a_source.image;
        bool transient = false;
        if (!a_make(image, why, transient)) {
            child.state = transient ? State::kFailed : State::kFallback;
            child.why = transient ? why : fmt::format("{}: today's palette kept", why);
            return child;
        }
        const auto temp = TempPathOf(StagedFile(m_log.Staging(), *slot));
        if (!a_write(image, temp, why)) {
            Remove(temp);
            child.why = why;
            return child;  // failed
        }
        return Commit(temp, *slot, key);
    }

    std::string KeyedOutputs::Palette(const TextureLook& a_source, bool a_additive, const MakeImage& a_make, const WriteImage& a_write,
        std::string& a_why)
    {
        const std::string id = fmt::format("{}|{}", a_additive ? "add" : "normal", a_source.path);
        auto it = m_children.find(id);
        if (it == m_children.end()) {
            it = m_children.emplace(id, MakePalette(a_source, a_additive, a_make, a_write)).first;
        }
        if (it->second.state != State::kCopy) {
            a_why = it->second.why;
        }
        return Apply(it->second);
    }
}
