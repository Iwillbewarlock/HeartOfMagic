// =============================================================================
// CopyRequestV5 - request.json for the builder in key mode, and the dump beside it (CopyRequestV5.h)
// =============================================================================

#include "CopyRequestV5.h"

#include "Config.h"
#include "CopyRequest.h"
#include "CopyRequestData.h"
#include "CopyRequestDump.h"
#include "SlotAssign.h"

#include "EncodingUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace BlackFlame::CopyRequestV5
{
    namespace
    {
        namespace CC = CopyContract;
        using Json = nlohmann::ordered_json;
        constexpr int kJsonIndent = 1;
        constexpr const char* kRequestName = "request.json";

        std::string Utf8(std::string_view a_text)
        {
            return EncodingUtils::SanitizeToUTF8(std::string(a_text));
        }

        // The dump's identity: every NIF read (path, size, fnv64, ok); the textures have their own (TextureFingerprint)
        std::string DumpFingerprint(const SlotKeys::Inputs& a_inputs)
        {
            std::vector<std::string> lines;
            for (const auto& nif : a_inputs.nifs) {
                lines.push_back(std::format("N|{}|{}|{:016x}|{}", Utf8(nif.path), nif.blob.size, nif.blob.fnv64, nif.blob.ok ? "ok" : "failed"));
            }
            // review Codex #3: every texture's content hash too (a same-size replacement must re-dump: the keys hashed it)
            for (const auto& [path, hash] : a_inputs.textureHash) {
                lines.push_back(std::format("T|{}|{}", Utf8(path), CC::FileHashText(hash).value_or("unset")));
            }
            std::ranges::sort(lines);
            std::uint64_t hash = CC::kFnvOffset;
            for (const auto& line : lines) {
                hash = CC::Fnv64(line + "\n", hash);
            }
            return std::format("v5:{:016x}", hash);
        }

        std::string WriteDump(const std::filesystem::path& a_folder, SlotKeys::Inputs& a_inputs, const std::string& a_fingerprint,
            bool& a_complete)
        {
            using namespace CopyRequestDump;
            const auto textureFingerprint = CopyRequestData::TextureFingerprint(a_inputs.textures);
            const auto manifest = CopyRequestData::ReadJsonObject(a_folder / kManifestName);
            const auto readFailed = CopyRequestData::JsonStringSet(manifest, kTextureReadFailuresKey);
            const bool unchanged = CopyRequestData::JsonString(manifest, kFingerprintKey) == a_fingerprint &&
                                   CopyRequestData::JsonString(manifest, kTextureFingerprintKey) == textureFingerprint &&
                                   DumpIntact(a_folder, a_inputs.nifs, a_inputs.textures, readFailed);
            int failures = 0;
            auto state = unchanged ? RetryReadFailures(a_folder, a_inputs.textures, readFailed, a_fingerprint, textureFingerprint, failures) :
                                     Dump(a_folder, a_inputs.nifs, a_inputs.textures, a_fingerprint, textureFingerprint, failures);
            a_complete = failures == 0;
            return state;
        }

        // ok: in the dump and hashed; unread: planned but its read failed; notdumped: opened, over a cap; missing: no file
        CC::Request::TextureState TextureStateOf(const SlotKeys::Inputs& a_inputs, const CopyRequestData::Texture& a_texture)
        {
            using State = CC::Request::TextureState;
            if (a_texture.planned) {
                const auto it = a_inputs.textureHash.find(a_texture.path);
                const bool hashed = it != a_inputs.textureHash.end() && it->second.state == CC::FileState::kHashed;
                return hashed && !a_texture.readFailed ? State::kOk : State::kUnread;
            }
            return a_texture.probe.opened ? State::kNotDumped : State::kMissing;
        }

        Json Options(const SlotKeys::Inputs& a_inputs)
        {
            const auto& prefix = *a_inputs.prefix;
            return { { CC::Request::kTint, prefix.tint }, { CC::Request::kTintAdd, prefix.tintAdd },
                { CC::Request::kPaletteMode, std::string(CC::PaletteModeText(prefix.paletteMode)) },
                { CC::Request::kKeyPrefix, CC::KeyPrefixString(prefix).value_or("") } };
        }

        Json Forms(const FireClosure& a_closure, Result& a_out)
        {
            std::unordered_map<RE::FormID, std::uint16_t> slotOf;
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                if (slot.usable && slot.source) {  // the slots that can be published (SlotObjects' rule too)
                    slotOf.emplace(slot.source->GetFormID(), id);
                }
            }
            Json forms = Json::array();
            for (const auto& form : a_closure.forms) {
                Json item = CopyRequestData::FormJson(form.form, form.kind);
                item["viaSpell"] = SourceText(RE::TESForm::LookupByID(form.viaSpell));
                item["costliest"] = form.reachedByCostliest;
                if (const auto it = slotOf.find(form.form->GetFormID()); it != slotOf.end()) {
                    const auto& slot = SlotAssign::At(it->second);
                    item[CC::Request::kSlot] = it->second;
                    if (slot.modelKey) {
                        item[CC::Request::kKey] = CC::Hex16(slot.modelKey->key);
                        ++a_out.forms;
                    }
                }
                forms.push_back(std::move(item));
            }
            return forms;
        }

        // muzzles, addnMps and txst: one entry per object the slots need built
        void SlotObjects(Json& a_root, Result& a_out)
        {
            a_root[CC::Request::kMuzzles] = Json::array();
            a_root[CC::Request::kAddnMps] = Json::array();
            a_root[CC::Request::kTxst] = Json::array();
            std::set<std::uint64_t> muzzles;
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                if (!slot.usable || !slot.source) {
                    continue;
                }
                if (slot.muzzleKey && muzzles.insert(slot.muzzleKey->key).second) {
                    a_root[CC::Request::kMuzzles].push_back({ { CC::Request::kPath, Utf8(slot.sourceMuzzle) },
                        { CC::Request::kKey, CC::Hex16(slot.muzzleKey->key) } });
                    ++a_out.muzzles;
                }
                if (slot.type == CC::SlotType::kADDN && slot.modelKey) {
                    const auto* addon = slot.source->As<RE::BGSAddonNode>();
                    if (!addon) {  // review Gemini / agy dc 6: never index 0 for a source that is no ADDN record
                        logger::warn("BlackFlame: slots: ADDN slot {:X}: its source {} is not an ADDN record - no MPS requested", id,
                            slot.sourceText);
                        continue;
                    }
                    a_root[CC::Request::kAddnMps].push_back({ { CC::Request::kSlot, id }, { CC::Request::kPath, Utf8(slot.sourceModel) },
                        { CC::Request::kKey, CC::Hex16(slot.modelKey->key) }, { CC::Request::kIndex, addon->index },
                        { "source", Utf8(slot.sourceText) } });  // the plugin name's bytes are the code page's
                    ++a_out.addnMps;
                }
                if (slot.type == CC::SlotType::kTXST && slot.txstKey) {
                    a_root[CC::Request::kTxst].push_back({ { CC::Request::kSlot, id }, { CC::Request::kKey, CC::Hex16(slot.txstKey->key) },
                        { CC::Request::kDodt, slot.txstDodt } });
                    ++a_out.txst;
                }
            }
        }

        Json RequestJson(const FireClosure& a_closure, const SlotKeys::Inputs& a_inputs, const std::string& a_fingerprint,
            bool a_dumpComplete, Result& a_out)
        {
            Json root;
            root["version"] = CC::Request::kVersion;
            root[CC::Request::kContractVersion] = CC::kContractVersion;
            root["game"] = REL::Module::get().version().string();
            root["fingerprint"] = a_fingerprint;
            root["dumpComplete"] = a_dumpComplete;
            root["slotsPlugin"] = SlotAssign::kSlotsPluginName;
            root["decalTextureSets"] = true;
            root[CC::Request::kOptions] = Options(a_inputs);
            root["forms"] = Forms(a_closure, a_out);
            SlotObjects(root, a_out);
            root["excluded"] = Json::array();
            for (const auto& excluded : a_closure.excluded) {
                root["excluded"].push_back({ { "source", SourceText(excluded.form) },
                    { "type", std::string(RE::FormTypeToString(excluded.form->GetFormType())) }, { "reason", excluded.reason } });
            }
            root["addons"] = CopyRequestData::AddonsJson();
            root["nifs"] = Json::array();
            for (const auto& nif : a_inputs.nifs) {
                Json item{ { "path", Utf8(nif.path) }, { "size", nif.blob.size }, { "fnv64", CC::Hex16(nif.blob.fnv64) }, { "ok", nif.blob.ok } };
                if (!nif.blob.ok) {
                    item["error"] = nif.blob.error;
                }
                root["nifs"].push_back(std::move(item));
            }
            root["textures"] = CopyRequestData::TexturesJson(a_inputs.textures);
            if (root["textures"].size() != a_inputs.textures.size()) {  // review agy 5 #3: one entry a texture, in its order
                throw std::runtime_error("the texture list and its JSON differ in length");
            }
            for (std::size_t i = 0; i < a_inputs.textures.size(); ++i) {  // contract v4: each one's state
                root["textures"][i][CC::Request::kTextureState] = std::string(CC::Request::TextureStateText(TextureStateOf(a_inputs, a_inputs.textures[i])));
            }
            root["textureCaps"] = { { "perTexture", CopyRequestData::kMaxTextureBytes }, { "total", CopyRequestData::kMaxTextureDumpBytes } };
            return root;
        }
    }

    Result Write(const FireClosure& a_closure, SlotKeys::Inputs& a_inputs)
    {
        const auto start = std::chrono::steady_clock::now();
        Result out;
        const auto folder = RequestFolder();
        if (!folder || !a_inputs.prefix) {
            logger::warn("BlackFlame: slots: request v5 not written: {}", !folder ? "no SKSE log folder" : a_inputs.prefixWhy);
            return out;
        }
        try {
            std::filesystem::create_directories(*folder);
            const auto fingerprint = DumpFingerprint(a_inputs);
            out.dumpState = WriteDump(*folder, a_inputs, fingerprint, out.dumpComplete);
            CopyRequestDump::WriteWhole(*folder / kRequestName,
                RequestJson(a_closure, a_inputs, fingerprint, out.dumpComplete, out).dump(kJsonIndent, ' ', false,
                    Json::error_handler_t::replace) + "\n");  // review Gemini / agy 5: a name, reason or error in the code page's
                                                              // bytes is replaced, never the end of the request
            out.written = true;
        } catch (const std::exception& e) {
            logger::error("BlackFlame: slots: request v5 or its dump failed: {}", e.what());
        }
        out.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        logger::info("BlackFlame: slots: request v5 {}: {} form keys, {} muzzles, {} ADDN MPS, {} TXST; dump {}{}; {} ms",
            out.written ? "written" : "NOT written", out.forms, out.muzzles, out.addnMps, out.txst, out.dumpState,
            out.dumpComplete ? "" : " (incomplete)", out.ms);
        return out;
    }
}
