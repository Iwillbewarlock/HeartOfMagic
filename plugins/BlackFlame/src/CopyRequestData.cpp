#include "CopyRequestData.h"

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
        }
        return item;
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

    std::vector<Texture> PlanTextures(const std::vector<const std::vector<std::byte>*>& a_nifBytes)
    {
        constexpr std::string_view kOwnTextureRoot = "textures\\heartofmagic\\blackflame\\";  // the palettes, texture copies
        std::set<std::string> paths;
        for (const auto* bytes : a_nifBytes) {
            for (const auto& raw : NifDump::TexturePathsIn(*bytes)) {
                if (const auto path = NifDump::NormalizeTexturePath(raw); path && !path->starts_with(kOwnTextureRoot)) {
                    paths.insert(*path);
                }
            }
        }
        std::vector<Texture> textures;
        std::uint64_t total = 0;
        for (const auto& path : paths) {
            Texture texture{ path, NifDump::ProbeModel(path), false, {} };
            const std::uint64_t size = texture.probe.totalSize;
            if (!texture.probe.opened) {
                texture.error = texture.probe.error;
            } else if (size > kMaxTextureBytes) {
                texture.error = std::format("over the per-texture cap ({} bytes > {})", size, kMaxTextureBytes);
            } else if (total + size > kMaxTextureDumpBytes) {
                texture.error = std::format("over the dump's total cap ({} bytes)", kMaxTextureDumpBytes);
            } else {
                texture.planned = true;
                total += size;
            }
            textures.push_back(std::move(texture));
        }
        return textures;
    }

    std::string TextureFingerprint(const std::vector<Texture>& a_textures)
    {
        std::uint64_t hash = NifDump::kFnvOffset;
        for (const auto& texture : a_textures) {  // in path order (PlanTextures)
            const auto line = std::format("T|{}|{}|{}\n", EncodingUtils::SanitizeToUTF8(texture.path), texture.probe.totalSize,
                texture.planned ? "dumped" : "left");
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
