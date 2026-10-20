#include "CopyRequestData.h"

#include "CopyContractEfsh.h"
#include "DataPathRule.h"
#include "EncodingUtils.h"

#include <format>
#include <fstream>
#include <sstream>
#include <set>
#include <utility>

namespace BlackFlame::CopyRequestData
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        std::string Utf8(const char* a_text)
        {
            return a_text ? EncodingUtils::SanitizeToUTF8(std::string(a_text)) : std::string();
        }

        Json Bounds(const RE::TESBoundObject& a_object)
        {
            const auto& b = a_object.boundData;
            return Json::array({ b.boundMin.x, b.boundMin.y, b.boundMin.z, b.boundMax.x, b.boundMax.y, b.boundMax.z });
        }

        Json ProjectileData(const RE::BGSProjectile& a_p)
        {
            const auto& d = a_p.data;
            return { { "flags", d.flags.underlying() }, { "projectileType", d.types.underlying() }, { "gravity", d.gravity },
                { "speed", d.speed }, { "range", d.range }, { "tracerChance", d.tracerChance },
                { "explosionProximity", d.explosionProximity }, { "explosionTimer", d.explosionTimer },
                { "muzzleFlashDuration", d.muzzleFlashDuration }, { "fadeDuration", d.fadeOutTime }, { "impactForce", d.force },
                { "coneSpread", d.coneSpread }, { "collisionRadius", d.collisionRadius }, { "lifetime", d.lifetime },
                { "relaunchInterval", d.relaunchInterval }, { "soundLevel", std::to_underlying(a_p.soundLevel) } };
        }

        Json ExplosionData(const RE::BGSExplosion& a_e)
        {
            const auto& d = a_e.data;
            return { { "force", d.force }, { "damage", d.damage }, { "radius", d.radius }, { "imageSpaceRadius", d.imageSpaceRadius },
                { "verticalOffsetMult", d.verticalOffsetMult }, { "flags", d.flags.underlying() },
                { "soundLevel", d.eSoundLevel.underlying() } };
        }

        Json HazardData(const RE::BGSHazard& a_h)
        {
            const auto& d = a_h.data;
            return { { "limit", d.limit }, { "radius", d.radius }, { "lifetime", d.lifetime },
                { "imageSpaceRadius", d.imageSpaceRadius }, { "targetInterval", d.targetInterval }, { "flags", d.flags.underlying() } };
        }

        Json ImpactData(const RE::BGSImpactData& a_i)
        {
            const auto& d = a_i.data;
            return { { "effectDuration", d.effectDuration }, { "orientation", d.orient.underlying() },
                { "angleThreshold", d.angleThreshold }, { "placementRadius", d.placementRadius },
                { "soundLevel", std::to_underlying(d.soundLevel) }, { "flags", d.flags.underlying() },
                { "resultOverride", d.resultOverride.underlying() } };
        }
    }

    std::string MuzzleModelOf(const RE::BGSProjectile& a_projectile)
    {
        const char* text = a_projectile.muzzleFlashModel.model.c_str();
        return text ? std::string(text) : std::string();
    }

    Json FormJson(const RE::TESForm* a_form, CopyKind a_kind)
    {
        Json item{ { "source", SourceText(a_form) }, { "type", CopyKindTag(a_kind) }, { "editorId", Utf8(a_form->GetFormEditorID()) },
            { "model", EncodingUtils::SanitizeToUTF8(ModelPathOf(a_form)) } };
        if (const auto* p = a_form->As<RE::BGSProjectile>()) {
            item["muzzleModel"] = EncodingUtils::SanitizeToUTF8(MuzzleModelOf(*p));
            item["bounds"] = Bounds(*p);
            item["data"] = ProjectileData(*p);
        } else if (const auto* e = a_form->As<RE::BGSExplosion>()) {
            item["bounds"] = Bounds(*e);
            item["data"] = ExplosionData(*e);
        } else if (const auto* h = a_form->As<RE::BGSHazard>()) {
            item["bounds"] = Bounds(*h);
            item["data"] = HazardData(*h);
        } else if (const auto* i = a_form->As<RE::BGSImpactData>()) {
            item["data"] = ImpactData(*i);  // IPCT has no OBND
        } else if (const auto* a = a_form->As<RE::BGSArtObject>()) {
            item["bounds"] = Bounds(*a);
            item["data"] = Json{ { "artType", a->data.artType.underlying() } };
        } else if (const auto* t = a_form->As<RE::BGSTextureSet>()) {
            item["model"] = "";
            item["bounds"] = Bounds(*t);
            item["data"] = Json{ { "flags", t->flags.underlying() } };
            item["hasDecal"] = t->decalData != nullptr;
            Json slots = Json::array();
            Json raw = Json::array();
            for (const auto& texture : t->textures) {  // TX00..TX07 (RE/B/BGSTextureSet.h)
                const char* text = texture.textureName.c_str();
                slots.push_back(Utf8(text));  // for people (a code page's bytes that are no text show as replacements)
                raw.push_back(DataPathRule::ToHex(text ? std::string_view(text) : std::string_view()));  // AB1: the bytes
            }
            item["textures"] = std::move(slots);
            item["texturesRaw"] = std::move(raw);
        }
        return item;
    }

    std::string FingerprintModelOf(const RE::TESForm* a_form)
    {
        const auto normalised = [](const std::string& a_text, bool a_texture) {
            const auto path = a_text.empty() ? std::nullopt :
                              (a_texture ? NifDump::NormalizeTexturePath(a_text) : NifDump::NormalizeModelPath(a_text));
            return EncodingUtils::SanitizeToUTF8(path ? *path : a_text);
        };
        const auto* set = a_form ? a_form->As<RE::BGSTextureSet>() : nullptr;
        if (!set) {
            return normalised(ModelPathOf(a_form), false);
        }
        std::string out = set->decalData ? "dodt" : "nododt";
        for (const auto& texture : set->textures) {
            const char* text = texture.textureName.c_str();
            // AB1: the record string's bytes (case and separators folded), as the copy compares them
            out += ";" + DataPathRule::ToHex(DataPathRule::LowerSlashes(text ? std::string_view(text) : std::string_view()));
        }
        return out;
    }

    std::vector<std::string> FormTexturePaths(const FireClosure& a_closure)
    {
        constexpr std::size_t kColourSlots[] = { RE::BSTextureSet::Textures::kDiffuse, RE::BSTextureSet::Textures::kGlowMap };
        std::vector<std::string> paths;
        for (const auto& form : a_closure.forms) {
            if (const auto* set = form.form->As<RE::BGSTextureSet>()) {
                for (const auto slot : kColourSlots) {
                    if (const char* text = set->textures[slot].textureName.c_str(); text && *text) {
                        paths.emplace_back(text);
                    }
                }
            }
        }
        return paths;
    }

    Json AddonsJson()
    {
        Json list = Json::array();
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            return list;
        }
        for (const auto* addon : dh->GetFormArray<RE::BGSAddonNode>()) {
            if (!addon) {
                continue;
            }
            list.push_back({ { "index", addon->index }, { "model", EncodingUtils::SanitizeToUTF8(ModelPathOf(addon)) },
                { "editorId", Utf8(addon->GetFormEditorID()) }, { "source", SourceText(addon) },
                { "masterParticleCap", addon->data.masterParticleCap }, { "dnamFlags", addon->data.flags.underlying() },
                { "own", IsOwnPluginForm(addon) } });
        }
        return list;
    }

    std::vector<Texture> PlanTextures(const std::vector<const std::vector<std::byte>*>& a_nifBytes,
        const std::vector<std::string>& a_formTextures, const std::vector<std::string>& a_lateTextures,
        const std::vector<std::string>& a_baseKept, const std::vector<std::string>& a_baseNew)
    {
        std::vector<std::string> first(a_formTextures);
        for (const auto* bytes : a_nifBytes) {
            const auto raws = NifDump::TexturePathsIn(*bytes);
            first.insert(first.end(), raws.begin(), raws.end());
        }
        // contract 6: the effect shader palettes after every other one, contract 8: their base textures after those (the
        // ones the last request keyed first), never a path twice (CopyContractEfsh.h, tested there)
        const auto order = CopyContract::TexturePlanTiers({ first, a_lateTextures, a_baseKept, a_baseNew });
        const auto basesFrom = CopyContract::TexturePlanTiers({ first, a_lateTextures }).size();  // the base tier's first index
        std::vector<Texture> textures;
        std::uint64_t total = 0;
        std::uint64_t baseTotal = 0;
        // Our two fixed palettes first (small), read whole for their hash: the builder's risk check judges with them
        for (const auto palette : { DataPathRule::kOurPalette, DataPathRule::kOurPaletteAdd }) {
            Texture texture{ std::string(palette), NifDump::ReadModel(std::string(palette), nullptr), false, {} };
            texture.own = true;
            texture.fnv64 = texture.probe.fnv64;
            if (!texture.probe.ok) {
                texture.error = texture.probe.error;
            } else {
                texture.planned = true;
                total += texture.probe.totalSize;
            }
            textures.push_back(std::move(texture));
        }
        const auto plan = [&](const std::string& a_path, bool a_base) {
            Texture texture{ a_path, NifDump::ProbeModel(a_path), false, {} };
            const std::uint64_t size = texture.probe.totalSize;
            if (!texture.probe.opened) {
                texture.error = texture.probe.error;
            } else if (size > kMaxTextureBytes) {
                texture.error = std::format("over the per-texture cap ({} bytes > {})", size, kMaxTextureBytes);
            } else if (total + size > kMaxTextureDumpBytes) {
                texture.error = std::format("over the dump's total cap ({} bytes)", kMaxTextureDumpBytes);
            } else if (a_base && baseTotal + size > kMaxBaseDumpBytes) {
                texture.error = std::format("over the base textures' cap ({} bytes)", kMaxBaseDumpBytes);
            } else {
                texture.planned = true;
                total += size;
                baseTotal += a_base ? size : 0;
            }
            textures.push_back(std::move(texture));
        };
        for (std::size_t i = 0; i < order.size(); ++i) {
            plan(order[i], i >= basesFrom);
        }
        return textures;
    }

    std::string TextureFingerprint(const std::vector<Texture>& a_textures)
    {
        std::uint64_t hash = NifDump::kFnvOffset;
        for (const auto& texture : a_textures) {  // in path order (PlanTextures)
            const auto line = std::format("T|{}|{}|{}|{:016x}\n", EncodingUtils::SanitizeToUTF8(texture.path),
                texture.probe.totalSize, texture.planned ? "dumped" : "left", texture.fnv64);  // fnv64: own palettes only
            hash = NifDump::Fnv64(line.data(), line.size(), hash);
        }
        return std::format("fnv64:{:016x}", hash);
    }

    Json TexturesJson(const std::vector<Texture>& a_textures)
    {
        Json list = Json::array();
        for (const auto& texture : a_textures) {
            Json item{ { "path", EncodingUtils::SanitizeToUTF8(texture.path) }, { "size", texture.probe.totalSize },
                { "ok", texture.planned } };
            if (texture.own) {
                item["own"] = true;
            }
            if (!texture.planned) {
                item["error"] = EncodingUtils::SanitizeToUTF8(texture.error);
            }
            list.push_back(std::move(item));
        }
        return list;
    }

    nlohmann::json ReadJsonObject(const std::filesystem::path& a_file)
    {
        std::ifstream in(a_file, std::ios::binary);
        if (!in) {
            return nlohmann::json::object();
        }
        std::stringstream text;
        text << in.rdbuf();
        auto root = nlohmann::json::parse(text.str(), nullptr, false);
        return root.is_object() ? root : nlohmann::json::object();
    }

    std::string JsonString(const nlohmann::json& a_object, const char* a_key)
    {
        return a_object.contains(a_key) && a_object[a_key].is_string() ? a_object[a_key].get<std::string>() : std::string();
    }

    std::set<std::string> JsonStringSet(const nlohmann::json& a_object, const char* a_key)
    {
        std::set<std::string> values;
        if (a_object.contains(a_key) && a_object[a_key].is_array()) {
            for (const auto& item : a_object[a_key]) {
                if (item.is_string()) {
                    values.insert(item.get<std::string>());
                }
            }
        }
        return values;
    }
}
