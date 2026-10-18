// =============================================================================
// GreyKeyedTextures - key mode's child objects: texture and derived palette copies at copies\<key16>\ (GreyKeyed.h)
// =============================================================================
// A child's key: the contract's TextureKeyString / PaletteKeyString over the verified prefix and the source file's own
// bytes (hashed here, from the dump). Its file: DataPathRule::CopyPath(textures\, key16, source). Each child is judged once per
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

        constexpr std::uint64_t kPaletteImages = 2;  // MakePalette's copy of the source and the image Write makes

        [[nodiscard]] CC::ProgressLine DdsLine(std::uint64_t a_key, CC::Outcome a_outcome, const std::string& a_reason)
        {
            CC::ProgressLine line;
            line.kind = CC::ProgressKind::kDds;
            line.key = a_key;
            line.outcome = a_outcome;
            line.reason = a_reason;
            return line;
        }

    }

    KeyedOutputs::KeyedOutputs(ProgressLog& a_log, Budget& a_budget, CC::KeyPrefix a_prefix, TextureJob& a_textures,
        std::set<std::string> a_listed, std::map<std::string, CC::Request::TextureState> a_states) :
        m_log(a_log), m_budget(a_budget), m_prefix(std::move(a_prefix)), m_textures(a_textures), m_listed(std::move(a_listed)),
        m_states(std::move(a_states))
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
            m_object->failed = a_why.empty() ? std::string("a failure with no reason given") : a_why;
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
            RemoveQuietly(a_temp);
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
        // the dump's listed files only (CopyRequestDump may leave a stale file it could not delete)
        const bool listed = m_listed.count(a_path) != 0;
        const auto source = listed ? FindSource(m_textures.inputs, a_path) : std::nullopt;
        if (listed && !source) {
            child.why = fmt::format("texture {} is listed as dumped but not in the dump", a_path);
            return child;  // failed: the dump is broken, not the texture (Codex review 2)
        }
        if (!source) {
            namespace R = CC::Request;
            const auto game = m_textures.notDumped.find(a_path);
            const std::string said = game == m_textures.notDumped.end() ? std::string("not listed") : game->second;
            const auto state = m_states.find(a_path);
            ++m_textures.totals.missing;
            std::cout << fmt::format("TEX   MISS {} ({}, first in {}): not in the dump - the game: {}{}\n", a_path, TextureRoleName(a_role),
                a_where, state == m_states.end() ? std::string("no state") : std::string(R::TextureStateText(state->second)),
                said.empty() ? std::string() : " (" + said + ")");
            if (state != m_states.end() && state->second == R::TextureState::kNotDumped) {
                child.state = State::kTerminal;  // the request's state, never its error text (contract 4)
                child.terminal = CC::Outcome::kNotDumped;
                child.why = fmt::format("texture {} is outside the dump caps", a_path);
            } else if (state != m_states.end() && state->second == R::TextureState::kUnread) {
                // the game opened it and its read failed: nothing about the texture is known; the next start reads it again
                child.why = fmt::format("texture {}: the game could not read it this start (unread)", a_path);
            } else {
                child.state = State::kFallback;  // missing (does not open) or not listed: the key says so
                child.why = fmt::format("texture {} is not in the dump ({}): slot left", a_path,
                    state == m_states.end() ? std::string("not listed") : std::string(R::TextureStateText(state->second)));
            }
            return child;
        }
        std::string why;
        const auto digest = DigestFile(*source, why);
        const auto keyText = digest ? CC::TextureKeyString(m_prefix, ContractRole(a_role), { a_path, digest->fnv64 }) : std::nullopt;
        if (!keyText) {
            child.why = digest ? fmt::format("texture {}: the contract gives it no key", a_path) : why;
            return child;  // failed
        }
        const auto key = CC::KeyOf(*keyText);
        const auto slot = Rule::CopyPath(Rule::kTexturesRoot, CC::Hex16(key), a_path);
        if (!slot) {  // CopyPath refuses a source with no plain extension (its paths are always ASCII)
            child.state = State::kFallback;
            child.why = fmt::format("texture {}: no copy path for it: slot left", a_path);
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
            if (std::string lineWhy; [&] {
                    auto line = DdsLine(key, CC::Outcome::kTooLarge, why);
                    line.cap = m_budget.Cap();  // contract 4: the cap is a field
                    return !m_log.Append(line, lineWhy);
                }()) {
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
        const auto result = RecolourDds(*source, temp, a_tint, m_textures.gpu);
        if (!result.gpuNote.empty()) {  // once per texture: each is recoloured once per run
            std::cout << fmt::format("GPU   {} {}: {}\n", result.gpu ? "BC7" : "CPU", a_path, result.gpuNote);
        }
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
            RemoveQuietly(temp);
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
        if (ObjectFailed()) {
            return {};  // failed already: no recolour, budget wait or TEX FAIL line for a copy nothing will publish
        }
        const std::string id = fmt::format("{}|{}", TextureRoleName(a_role), a_path);
        auto it = m_children.find(id);
        if (it == m_children.end()) {
            auto child = MakeTexture(a_path, a_role, a_tint, a_where);
            if (child.state == State::kFailed) {
                return Apply(child);  // a transient failure is not kept: the next object that names it tries again (OCR review 5)
            }
            it = m_children.emplace(id, std::move(child)).first;
        }
        return Apply(it->second);
    }

    KeyedOutputs::Child KeyedOutputs::MakePalette(const TextureLook& a_source, bool a_additive, const MakeImage& a_make,
        const WriteImage& a_write)
    {
        Child child;
        std::string why;
        const auto digest = a_source.file.empty() ? std::nullopt : DigestFile(a_source.file, why);
        const auto keyText = digest ? CC::PaletteKeyString(m_prefix, a_additive ? CC::PaletteBlend::kAdd : CC::PaletteBlend::kNormal,
                                          { a_source.path, digest->fnv64 }) :
                                      std::nullopt;
        if (!keyText) {
            child.why = fmt::format("source palette {}: {}", a_source.path, digest ? "the contract gives it no key" :
                                    a_source.file.empty() ? "read from no file" : why);
            return child;  // failed
        }
        const auto key = CC::KeyOf(*keyText);
        const auto slot = Rule::PaletteCopyPath(CC::Hex16(key));  // contract 4: ...\copies\<key16>\palette.dds
        if (!slot) {
            child.state = State::kFallback;
            child.why = fmt::format("source palette {}: no copy path for it: today's palette kept", a_source.path);
            return child;
        }
        m_paletteBlends[*slot] = a_additive;
        if (auto reused = Reused(key, *slot)) {
            return *reused;
        }
        // the copy and the written image (two RGBA images of the source's size) are admitted like a read (Codex review 2)
        const std::uint64_t estimate = static_cast<std::uint64_t>(a_source.image->pixels.size()) * kPaletteImages;
        switch (m_budget.Acquire(estimate, why, false)) {
        case Admission::kAdmit:
            break;
        case Admission::kTooLarge: {
            auto line = DdsLine(key, CC::Outcome::kTooLarge, why);
            line.cap = m_budget.Cap();
            if (std::string lineWhy; !m_log.Append(line, lineWhy)) {
                std::cout << fmt::format("WARN  the too_large line of {} was not written: {}\n", *slot, lineWhy);
            }
            child.state = State::kTerminal;
            child.terminal = CC::Outcome::kTooLarge;
            child.why = fmt::format("source palette {}: {}", a_source.path, why);
            return child;
        }
        default:
            child.why = fmt::format("source palette {}: {}", a_source.path, why);
            return child;  // failed
        }
        RgbaImage image = *a_source.image;
        switch (a_make(image, why)) {
        case Made::kMade:
            break;
        case Made::kGated:
            child.state = State::kLeft;
            child.keep = false;
            return child;
        case Made::kFailed:
            child.why = why;
            return child;  // failed
        case Made::kFallback:
            child.state = State::kFallback;
            child.why = fmt::format("{}: today's palette kept", why);
            return child;
        }
        const auto temp = TempPathOf(StagedFile(m_log.Staging(), *slot));
        if (!a_write(image, temp, why)) {
            RemoveQuietly(temp);
            child.why = why;
            return child;  // failed
        }
        return Commit(temp, *slot, key);
    }

    std::string KeyedOutputs::Palette(const TextureLook& a_source, bool a_additive, const MakeImage& a_make, const WriteImage& a_write,
        std::string& a_why)
    {
        if (ObjectFailed()) {
            a_why = m_object->failed;
            return {};
        }
        const std::string id = fmt::format("{}|{}", a_additive ? "add" : "normal", a_source.path);
        auto it = m_children.find(id);
        if (it == m_children.end()) {
            auto child = MakePalette(a_source, a_additive, a_make, a_write);
            if (child.state == State::kFailed) {
                a_why = child.why;
                return Apply(child);  // not kept, like a texture's (OCR review 5)
            }
            if (!child.keep) {
                a_why = "a texture it needs was refused by the memory budget";
                return Apply(child);
            }
            it = m_children.emplace(id, std::move(child)).first;
        }
        if (it->second.state != State::kCopy) {
            a_why = it->second.why;
        }
        return Apply(it->second);
    }
}
