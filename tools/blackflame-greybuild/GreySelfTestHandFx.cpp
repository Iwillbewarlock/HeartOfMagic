// Hand-only motion and ember graph tests use arbitrary names and no game paths.
#include "GreySelfTest.h"
#include "GreyMarble.h"
#include "GreyShapes.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr const char* kPalette = "textures\\selftest\\handfx-palette.dds";
        constexpr float kPi = 3.14159265358979323846f;
        struct System
        {
            std::uint32_t id, shader, data, emitter, rate, life, rateValue;
        };

        template<class T>
        std::uint32_t Add(nifly::NiHeader& hdr, std::unique_ptr<T> value)
        {
            return hdr.AddBlock(std::move(value));
        }

        bool Near(float a, float b) { return std::abs(a - b) < 1e-5f; }

        std::uint32_t FloatPath(nifly::NiHeader& hdr, float value)
        {
            auto data = std::make_unique<nifly::NiFloatData>();
            data->data.SetInterpolationType(nifly::NiKeyType::LINEAR_KEY);
            nifly::NiAnimationKey<float> key;
            key.value = value;
            data->data.AddKey(key);
            key.time = 1;
            data->data.AddKey(key);
            auto interp = std::make_unique<nifly::NiFloatInterpolator>();
            interp->floatValue = value;
            interp->dataRef.index = Add(hdr, std::move(data));
            return Add(hdr, std::move(interp));
        }

        bool ValueIs(nifly::NiHeader& hdr, std::uint32_t id, float expected)
        {
            auto* interp = hdr.GetBlock<nifly::NiFloatInterpolator>(id);
            auto* data = interp ? hdr.GetBlock<nifly::NiFloatData>(interp->dataRef) : nullptr;
            if (!interp || !data || !Near(interp->floatValue, expected) || data->data.GetNumKeys() != 2) return false;
            for (unsigned i = 0; i < data->data.GetNumKeys(); ++i)
                if (!Near(data->data.GetKey(i).value, expected)) return false;
            return true;
        }

        bool ActivationIs(nifly::NiHeader& hdr, std::uint32_t id)
        {
            if (auto* blend = hdr.GetBlock<nifly::NiBlendBoolInterpolator>(id)) {
                return blend->value && blend->interpItems.size() == 1 &&
                    Near(blend->interpItems[0].weight, 1) &&
                    ActivationIs(hdr, blend->interpItems[0].interpolatorRef.index);
            }
            auto* value = hdr.GetBlock<nifly::NiBoolInterpolator>(id);
            auto* data = value ? hdr.GetBlock<nifly::NiBoolData>(value->dataRef) : nullptr;
            return value && value->boolValue == 1 && data && data->data.GetNumKeys() == 2 &&
                data->data.GetKey(0).value == 0 && Near(data->data.GetKey(0).time, 0) &&
                data->data.GetKey(1).value == 1 && Near(data->data.GetKey(1).time, 1);
        }

        System MakeSystem(nifly::NifFile& nif, const std::string& name, unsigned cells,
            float radius, float variation, unsigned pool, bool strip = false)
        {
            auto& hdr = nif.GetHeader();
            System result{};
            auto shader = std::make_unique<nifly::BSEffectShaderProperty>();
            shader->sourceTexture.get() = "textures\\selftest\\arbitrary-cards.dds";
            shader->greyscaleTexture.get() = kPalette;
            shader->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor | kSlsf1GreyscaleToPaletteAlpha;
            shader->baseColor = { 1, 1, 1, 1 };
            shader->baseColorScale = 1.25f;
            result.shader = Add(hdr, std::move(shader));
            auto alpha = std::make_unique<nifly::NiAlphaProperty>();
            alpha->flags = 4109;
            const auto alphaId = Add(hdr, std::move(alpha));
            std::unique_ptr<nifly::NiPSysData> data = strip ?
                std::unique_ptr<nifly::NiPSysData>(std::make_unique<nifly::BSStripPSysData>()) :
                std::make_unique<nifly::NiPSysData>();
            std::vector<nifly::Vector3> vertices(pool);
            auto version = nifly::NiVersion::getSSE();
            data->Create(version, &vertices, nullptr, nullptr, nullptr);
            data->hasTextureIndices = cells > 0;
            data->subtexOffsets.resize(cells);
            for (unsigned i = 0; i < cells; ++i)
                data->subtexOffsets[i] = { float(i) / cells, 1.0f / cells, 0, 1 };
            result.data = Add(hdr, std::move(data));
            std::unique_ptr<nifly::NiParticleSystem> system = strip ?
                std::unique_ptr<nifly::NiParticleSystem>(std::make_unique<nifly::BSStripParticleSystem>()) :
                std::make_unique<nifly::NiParticleSystem>();
            system->name.get() = name;
            system->psysDataRef.index = result.data;
            system->shaderPropertyRef.index = result.shader;
            system->alphaPropertyRef.index = alphaId;
            result.id = Add(hdr, std::move(system));
            nif.GetRootNode()->childRefs.AddBlockRef(result.id);
            auto emitter = std::make_unique<nifly::NiPSysBoxEmitter>();
            emitter->name.get() = "Fixture emitter";
            emitter->targetRef.index = result.id;
            emitter->emitterNodeRef.index = 0;
            emitter->isActive = true;
            emitter->order = 1000;
            emitter->radius = radius;
            emitter->radiusVariation = variation;
            emitter->lifeSpan = 0.5f;
            emitter->color = { 1, 1, 1, 1 };
            result.emitter = Add(hdr, std::move(emitter));
            auto colors = std::make_unique<nifly::BSPSysSimpleColorModifier>();
            colors->name.get() = "Fixture life colours";
            colors->targetRef.index = result.id;
            colors->isActive = true;
            colors->order = 3000;
            colors->color1 = { 1, 1, 1, 0 };
            colors->color2 = { 1, 1, 1, 1 };
            colors->color3 = { 1, 1, 1, 0 };
            colors->fadeInPercent = 0.1f;
            colors->fadeOutPercent = 0.8f;
            const auto colorId = Add(hdr, std::move(colors));
            auto bounds = std::make_unique<nifly::NiPSysBoundUpdateModifier>();
            bounds->targetRef.index = result.id;
            bounds->isActive = true;
            bounds->order = 7000;
            const auto boundId = Add(hdr, std::move(bounds));
            auto* ps = hdr.GetBlock<nifly::NiParticleSystem>(result.id);
            for (const auto id : { result.emitter, colorId, boundId }) ps->modifierRefs.AddBlockRef(id);
            result.rateValue = FloatPath(hdr, 18);
            auto life = std::make_unique<nifly::NiPSysEmitterLifeSpanCtlr>();
            life->targetRef.index = result.id;
            life->modifierName.get() = "Fixture emitter";
            life->interpolatorRef.index = result.rateValue; // Deliberately shared with rate and shader brightness.
            result.life = Add(hdr, std::move(life));
            auto rate = std::make_unique<nifly::NiPSysEmitterCtlr>();
            rate->targetRef.index = result.id;
            rate->modifierName.get() = "Fixture emitter";
            rate->interpolatorRef.index = result.rateValue;
            rate->nextControllerRef.index = result.life;
            result.rate = Add(hdr, std::move(rate));
            ps->controllerRef.index = result.rate;
            return result;
        }

        std::uint32_t AddLinks(nifly::NifFile& nif, const System& ember)
        {
            auto& hdr = nif.GetHeader();
            auto brightness = std::make_unique<nifly::BSEffectShaderPropertyFloatController>();
            brightness->typeOfControlledVariable = kFloatVarEmissiveMultiple;
            brightness->targetRef.index = ember.shader;
            brightness->interpolatorRef.index = ember.rateValue;
            const auto brightnessId = Add(hdr, std::move(brightness));
            hdr.GetBlock<nifly::BSEffectShaderProperty>(ember.shader)->controllerRef.index = brightnessId;
            auto seq = std::make_unique<nifly::NiControllerSequence>();
            seq->name.get() = "Fixture action";
            for (bool emission : { true, false }) {
                nifly::ControllerLink link;
                link.nodeName.get() = hdr.GetBlock<nifly::NiParticleSystem>(ember.id)->name.get();
                link.controllerRef.index = emission ? ember.rate : brightnessId;
                link.interpolatorRef.index = ember.rateValue;
                link.ctrlType.get() = emission ? "NiPSysEmitterCtlr" : "BSEffectShaderPropertyFloatController";
                link.ctrlID.get() = emission ? "Fixture emitter" : "0";
                if (!emission) link.propType.get() = "BSEffectShaderProperty";
                seq->controlledBlocks.push_back(link);
            }
            // Skyrim sequences bind birth-rate floats and activation bools to the same emitter controller.
            auto boolData = std::make_unique<nifly::NiBoolData>();
            boolData->data.SetInterpolationType(nifly::NiKeyType::LINEAR_KEY);
            nifly::NiAnimationKey<std::uint8_t> key;
            key.time = 0;
            key.value = 0;
            boolData->data.AddKey(key);
            key.time = 1;
            key.value = 1;
            boolData->data.AddKey(key);
            auto enabled = std::make_unique<nifly::NiBoolInterpolator>();
            enabled->boolValue = 1;
            enabled->dataRef.index = Add(hdr, std::move(boolData));
            const auto enabledId = Add(hdr, std::move(enabled));
            auto blend = std::make_unique<nifly::NiBlendBoolInterpolator>();
            blend->flags = nifly::INTERP_BLEND_NONE;
            blend->value = true;
            blend->arraySize = blend->interpCount = 1;
            blend->singleIndex = 0;
            blend->interpItems.resize(1);
            blend->interpItems[0].interpolatorRef.index = enabledId;
            blend->interpItems[0].weight = blend->interpItems[0].normalizedWeight = 1;
            const auto blendId = Add(hdr, std::move(blend));
            for (const auto activationId : { enabledId, blendId }) {
                auto link = seq->controlledBlocks[0];
                link.interpolatorRef.index = activationId;
                seq->controlledBlocks.push_back(link);
            }
            const auto seqId = Add(hdr, std::move(seq));
            auto manager = std::make_unique<nifly::NiControllerManager>();
            manager->targetRef.index = 0;
            manager->controllerSequenceRefs.AddBlockRef(seqId);
            nif.GetRootNode()->controllerRef.index = Add(hdr, std::move(manager));
            return seqId;
        }

        nifly::NiParticleSystem* DarkPartner(nifly::NifFile& nif, std::uint32_t bright, std::uint32_t& darkId)
        {
            auto& hdr = nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                auto* node = hdr.GetBlock<nifly::BSOrderedNode>(id);
                if (!node || node->childRefs.GetSize() != 2 || node->childRefs.GetBlockRef(1) != bright) continue;
                darkId = node->childRefs.GetBlockRef(0);
                return hdr.GetBlock<nifly::NiParticleSystem>(darkId);
            }
            return nullptr;
        }

        bool UpdateChain(nifly::NiHeader& hdr, nifly::NiParticleSystem& system, std::uint32_t target)
        {
            std::set<std::uint32_t> seen;
            unsigned updates = 0;
            auto id = system.controllerRef.index;
            if (!hdr.GetBlock<nifly::NiPSysEmitterCtlr>(id)) return false;
            while (id != nifly::NIF_NPOS) {
                auto* ctrl = hdr.GetBlock<nifly::NiTimeController>(id);
                if (!ctrl || !seen.insert(id).second || ctrl->targetRef.index != target) return false;
                if (hdr.GetBlock<nifly::NiPSysUpdateCtlr>(id)) {
                    ++updates;
                    if (ctrl->nextControllerRef.index != nifly::NIF_NPOS) return false;
                }
                id = ctrl->nextControllerRef.index;
            }
            return updates == 1;
        }

        std::uint32_t Pool(nifly::NiHeader& hdr, nifly::NiParticleSystem& ps)
        {
            auto* data = hdr.GetBlock<nifly::NiPSysData>(ps.dataRef);
            if (!data) data = hdr.GetBlock<nifly::NiPSysData>(ps.psysDataRef);
            return data ? data->GetNumVertices() : 0;
        }
    }

    void CheckHandFx(Checker& check, const std::filesystem::path& dir)
    {
        TextureSource textures({}, {});
        RgbaImage palette{ 4, 4, std::vector<std::uint8_t>(64), false };
        for (std::size_t i = 0; i < palette.pixels.size(); i += 4) {
            palette.pixels[i] = palette.pixels[i + 3] = 255;
            palette.pixels[i + 1] = 90;
        }
        textures.Put(kPalette, palette);
        nifly::NifFile source;
        source.Create(nifly::NiVersion::getSSE());
        const auto flame = MakeSystem(source, "Unrelated A", 4, 6, 0.25f, 24);
        const auto ember = MakeSystem(source, "Unrelated B", 1, 1.5f, 2, 12);
        const auto glow = MakeSystem(source, "Unrelated C", 0, 26, 2, 8);
        const auto strip = MakeSystem(source, "Unrelated D", 1, 0.3f, 0.2f, 10, true);
        const auto sequenceId = AddLinks(source, ember);
        // A large glow shares the ember shader. Boosting its owner must split this state.
        source.GetHeader().GetBlock<nifly::NiParticleSystem>(glow.id)->shaderPropertyRef.index = ember.shader;
        auto* sourceSequence = source.GetHeader().GetBlock<nifly::NiControllerSequence>(sequenceId);
        auto glowLink = sourceSequence->controlledBlocks[1];
        glowLink.nodeName.get() = source.GetHeader().GetBlock<nifly::NiParticleSystem>(glow.id)->name.get();
        sourceSequence->controlledBlocks.push_back(glowLink);
        const auto original = SaveToBytes(source);
        nifly::NifFile copy(source);
        const auto totals = AddHandMarble(copy, source, textures, 0.9, 1.15);
        check.Check(totals.systems == 1 && totals.emberSystems == 1,
            "handfx detection: arbitrary names select one atlas flame and one small additive ember, excluding glow/strip");
        check.Check(totals.addedParticles == 36 && totals.sourceParticles == 36,
            "handfx budget: selected 24 flame + 12 ember pools add exactly 36; selected capacity is bounded at 2x");
        check.Check(SaveToBytes(source) == original, "handfx source: source NIF stays byte-identical");
        const auto path = dir / "handfx-built.nif";
        SaveNif(copy, path);
        nifly::NifFile saved;
        LoadNif(saved, path);
        bool valid = CountUnknownBlocks(saved) == 0;
        try { ValidateMarbleRefs(saved); } catch (...) { valid = false; }
        check.Check(valid, "handfx graph: save/reload resolves every reference, with no unknown blocks");
        auto& hdr = saved.GetHeader();
        std::uint32_t marbleId = nifly::NIF_NPOS, darkId = nifly::NIF_NPOS;
        auto* marble = DarkPartner(saved, flame.id, marbleId);
        auto* dark = DarkPartner(saved, ember.id, darkId);
        check.Check(marble && dark, "handfx ordering: both source pairs have BSOrderedNode [dark, bright]");
        if (!marble || !dark) return;
        auto* bright = hdr.GetBlock<nifly::NiParticleSystem>(ember.id);
        auto* brightShader = hdr.GetBlock<nifly::BSEffectShaderProperty>(bright->shaderPropertyRef);
        auto* glowPs = hdr.GetBlock<nifly::NiParticleSystem>(glow.id);
        auto* glowShader = hdr.GetBlock<nifly::BSEffectShaderProperty>(glowPs->shaderPropertyRef);
        auto* stripPs = hdr.GetBlock<nifly::NiParticleSystem>(strip.id);
        auto* stripShader = hdr.GetBlock<nifly::BSEffectShaderProperty>(stripPs->shaderPropertyRef);
        check.Check(brightShader && glowShader && stripShader &&
            Near(brightShader->baseColorScale, 2.5f) && Near(glowShader->baseColorScale, 1.25f) &&
            Near(stripShader->baseColorScale, 1.25f) && bright->shaderPropertyRef.index != glowPs->shaderPropertyRef.index,
            "handfx brightness: only small ember is 2x; shared large glow and strip shader values are preserved");
        auto* darkShader = hdr.GetBlock<nifly::BSEffectShaderProperty>(dark->shaderPropertyRef);
        auto* darkAlpha = hdr.GetBlock<nifly::NiAlphaProperty>(dark->alphaPropertyRef);
        check.Check(darkShader && darkAlpha && darkAlpha->flags == 4333 &&
            Near(darkShader->baseColor.r, 0.006f) && Near(darkShader->baseColor.g, 0.006f) &&
            Near(darkShader->baseColor.b, 0.006f) && Near(darkShader->baseColor.a, 0.9f) &&
            Near(darkShader->baseColorScale, 1) &&
            !(darkShader->shaderFlags1 & (kSlsf1GreyscaleToPaletteColor | kSlsf1GreyscaleToPaletteAlpha)) &&
            (darkShader->shaderFlags1 & (1u << 31)) && !(darkShader->shaderFlags2 & 1),
            "handfx dark ember: alpha blend 4333, dark .006, opacity .9, palettes off, depth test on/write off");
        check.Check(Pool(hdr, *marble) == 24 && Pool(hdr, *dark) == 12,
            "handfx saved pools: marble and dark ember preserve bounded source capacities");
        auto* darkRate = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(dark->controllerRef);
        auto* darkLife = darkRate ? hdr.GetBlock<nifly::NiPSysEmitterLifeSpanCtlr>(darkRate->nextControllerRef) : nullptr;
        auto* originalRate = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(ember.rate);
        auto* originalLife = hdr.GetBlock<nifly::NiPSysEmitterLifeSpanCtlr>(ember.life);
        check.Check(darkRate && darkLife && originalRate && originalLife &&
            ValueIs(hdr, darkRate->interpolatorRef.index, 6) && ValueIs(hdr, darkLife->interpolatorRef.index, 18) &&
            ValueIs(hdr, originalRate->interpolatorRef.index, 18) && ValueIs(hdr, originalLife->interpolatorRef.index, 18),
            "handfx emission: dark rate is one third, shared lifetime path and original bright rate stay unchanged");
        auto* brightness = brightShader ?
            hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(brightShader->controllerRef) : nullptr;
        auto* originalBrightness = glowShader ?
            hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(glowShader->controllerRef) : nullptr;
        check.Check(brightness && originalBrightness && brightness->targetRef.index == bright->shaderPropertyRef.index &&
            ValueIs(hdr, brightness->interpolatorRef.index, 8) &&
            ValueIs(hdr, originalBrightness->interpolatorRef.index, 18),
            "handfx animated brightness: doubled values clip at 8 on independent paths; shared source glow stays 18");
        auto* sequence = hdr.GetBlock<nifly::NiControllerSequence>(sequenceId);
        bool brightLink = false, darkLink = false;
        bool glowLinkKept = false;
        unsigned brightShaderLinks = 0, glowShaderLinks = 0;
        unsigned brightActivationLinks = 0, darkActivationLinks = 0;
        bool activationKept = true;
        if (sequence) for (const auto& link : sequence->controlledBlocks) {
            if (link.nodeName.get() == bright->name.get() && link.propType.get() == "BSEffectShaderProperty") {
                ++brightShaderLinks;
                brightLink = brightness && link.controllerRef.index == brightShader->controllerRef.index &&
                    ValueIs(hdr, link.interpolatorRef.index, 8);
            }
            if (link.nodeName.get() == dark->name.get() && link.ctrlType.get() == "NiPSysEmitterCtlr" &&
                hdr.GetBlock<nifly::NiFloatInterpolator>(link.interpolatorRef))
                darkLink = darkRate && link.controllerRef.index == dark->controllerRef.index &&
                    ValueIs(hdr, link.interpolatorRef.index, 6);
            if (link.ctrlType.get() == "NiPSysEmitterCtlr" &&
                (hdr.GetBlock<nifly::NiBoolInterpolator>(link.interpolatorRef) ||
                    hdr.GetBlock<nifly::NiBlendBoolInterpolator>(link.interpolatorRef))) {
                if (link.nodeName.get() == bright->name.get()) {
                    ++brightActivationLinks;
                    activationKept &= link.controllerRef.index == bright->controllerRef.index &&
                        ActivationIs(hdr, link.interpolatorRef.index);
                }
                if (link.nodeName.get() == dark->name.get()) {
                    ++darkActivationLinks;
                    activationKept &= link.controllerRef.index == dark->controllerRef.index &&
                        ActivationIs(hdr, link.interpolatorRef.index);
                }
            }
            if (link.nodeName.get() == glowPs->name.get() && link.propType.get() == "BSEffectShaderProperty") {
                ++glowShaderLinks;
                glowLinkKept = link.controllerRef.index == glowShader->controllerRef.index &&
                    ValueIs(hdr, link.interpolatorRef.index, 18);
            }
        }
        check.Check(brightLink && darkLink && brightShaderLinks == 1 && glowLinkKept && glowShaderLinks == 1,
            "handfx sequence: bright emissive and dark birth-rate links use independently scaled controller/data paths");
        check.Check(activationKept && brightActivationLinks == 2 && darkActivationLinks == 2,
            "handfx activation: paired bool and blend-bool sequence links preserve values/keys while float birth rate is reduced");
        check.Check(UpdateChain(hdr, *marble, marbleId) && UpdateChain(hdr, *dark, darkId),
            "handfx update: one targeted NiPSysUpdateCtlr follows existing emitter/lifetime controllers");
        unsigned rotations = 0, subtex = 0;
        bool motion = true, ordered = true;
        std::uint32_t previousOrder = 0;
        for (const auto& ref : marble->modifierRefs) {
            auto* modifier = hdr.GetBlock<nifly::NiPSysModifier>(ref);
            ordered &= modifier && modifier->targetRef.index == marbleId && modifier->order >= previousOrder;
            if (!modifier) continue;
            previousOrder = modifier->order;
            if (auto* rotation = dynamic_cast<nifly::NiPSysRotationModifier*>(modifier)) {
                ++rotations;
                motion &= rotation->isActive && Near(rotation->initialSpeed, 0.6f * kPi) &&
                    Near(rotation->initialSpeedVariation, 0.3f * kPi) && rotation->randomSpeedSign &&
                    Near(rotation->initialAngle, 0.384f) && Near(rotation->initialAngleVariation, kPi);
            }
            if (auto* texture = dynamic_cast<nifly::BSPSysSubTexModifier*>(modifier)) {
                ++subtex;
                // The engine advances integer frame count over particle life, rather than reading FPS.
                motion &= texture->isActive && Near(texture->frameCount, 8.4f * 0.5f) &&
                    Near(texture->frameCountVariation, 1) && Near(texture->startFrame, 0) &&
                    Near(texture->startFrameVariation, 4.0f - 1.0e-3f) && Near(texture->endFrame, 3) &&
                    Near(texture->loopStartFrame, 0) && Near(texture->loopStartFrameVariation, 0);
            }
        }
        auto* marbleData = hdr.GetBlock<nifly::NiPSysData>(marble->dataRef);
        if (!marbleData) marbleData = hdr.GetBlock<nifly::NiPSysData>(marble->psysDataRef);
        check.Check(motion && rotations == 1 && subtex == 1 && ordered && marbleData &&
            marbleData->hasRotationAngles && marbleData->hasRotationSpeeds,
            "handfx marble motion: active 108 +/-54 deg/s random rotation, random angle, subtex and sorted owned modifiers");
        auto* white = hdr.GetBlock<nifly::NiParticleSystem>(flame.id);
        bool whiteUnchanged = white->modifierRefs.GetSize() == 3 && white->controllerRef.index == flame.rate;
        for (const auto& ref : white->modifierRefs)
            whiteUnchanged &= !hdr.GetBlock<nifly::NiPSysRotationModifier>(ref) && !hdr.GetBlock<nifly::BSPSysSubTexModifier>(ref);
        check.Check(whiteUnchanged, "handfx white flame: no rotation/cell motion added and controller head unchanged");
        nifly::NifFile repeat(source);
        (void)AddHandMarble(repeat, source, textures, 0.9, 1.15);
        check.Check(SaveToBytes(repeat) == SaveToBytes(copy), "handfx determinism: fresh identical requests produce identical graph bytes");
        nifly::NifFile varied(source);
        auto* variedEmitter = varied.GetHeader().GetBlock<nifly::NiPSysEmitter>(flame.emitter);
        variedEmitter->lifeSpan = 1;
        variedEmitter->lifeSpanVariation = 0.5f;
        nifly::NifFile variedCopy(varied);
        (void)AddHandMarble(variedCopy, varied, textures, 0.9, 1.15);
        auto* variedMarble = DarkPartner(variedCopy, flame.id, marbleId);
        bool lifetimeRate = false;
        if (variedMarble) for (const auto& ref : variedMarble->modifierRefs) {
            auto* texture = variedCopy.GetHeader().GetBlock<nifly::BSPSysSubTexModifier>(ref);
            if (texture) lifetimeRate = Near(texture->frameCount, 4.2f / std::log(5.0f / 3.0f)) &&
                Near(texture->frameCountVariation, 1);
        }
        check.Check(lifetimeRate,
            "handfx atlas rate: full-width variation .5 gives uniform .75..1.25s lifetime for mean 8.4 cells/s");
        nifly::NifFile onlyEmber;
        onlyEmber.Create(nifly::NiVersion::getSSE());
        const auto solo = MakeSystem(onlyEmber, "Renamed full-texture particles", 0, 2, 2, 6);
        onlyEmber.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(solo.shader)->baseColorScale = 6;
        nifly::NifFile soloCopy(onlyEmber);
        const auto soloTotals = AddHandMarble(soloCopy, onlyEmber, textures, 0, 1.15);
        const auto* soloPs = soloCopy.GetHeader().GetBlock<nifly::NiParticleSystem>(solo.id);
        const auto* soloShader = soloCopy.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(soloPs->shaderPropertyRef);
        check.Check(soloTotals.systems == 0 && soloTotals.emberSystems == 0 && soloTotals.addedParticles == 0 &&
            soloShader && Near(soloShader->baseColorScale, 6) && SaveToBytes(soloCopy) == SaveToBytes(onlyEmber),
            "handfx ember-only: zero opacity keeps ramp bytes and brightness unchanged, without dark embers");
        nifly::NifFile rampCopy(source);
        rampCopy.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(flame.shader)->greyscaleTexture.get() = "built-hand-ramp.dds";
        const auto rampBytes = SaveToBytes(rampCopy);
        const auto zeroTotals = AddHandMarble(rampCopy, source, textures, 0, 1.15);
        check.Check(zeroTotals.systems == 0 && zeroTotals.emberSystems == 0 && zeroTotals.addedParticles == 0 &&
            SaveToBytes(rampCopy) == rampBytes,
            "handfx mixed systems: zero opacity preserves the existing ramp copy byte-for-byte");
        for (const float opacity : { 0.5f, 1.0f }) {
            nifly::NifFile boundary(onlyEmber);
            const auto count = AddHandMarble(boundary, onlyEmber, textures, opacity, 1.15);
            SaveNif(boundary, dir / "handfx-opacity.nif");
            nifly::NifFile read;
            LoadNif(read, dir / "handfx-opacity.nif");
            auto* darkEmber = DarkPartner(read, solo.id, darkId);
            auto* shader = darkEmber ? read.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(darkEmber->shaderPropertyRef) : nullptr;
            auto* brightEmber = read.GetHeader().GetBlock<nifly::NiParticleSystem>(solo.id);
            auto* brightMaterial = read.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(brightEmber->shaderPropertyRef);
            check.Check(count.systems == 0 && count.emberSystems == 1 && shader &&
                Near(shader->baseColor.a, opacity == 0.5f ? 0.5f : 0.9f) && Near(brightMaterial->baseColorScale, 8),
                "handfx ember boundary: opacity " + std::to_string(opacity) + " caps dark alpha at min(.9, opacity); brightness clips at 8");
        }
        nifly::NifFile invalidLife(source);
        auto* badEmitter = invalidLife.GetHeader().GetBlock<nifly::NiPSysEmitter>(flame.emitter);
        badEmitter->lifeSpan = 0.1f;
        badEmitter->lifeSpanVariation = 0.2f; // Lower endpoint is zero: only this marble's new motion must be skipped.
        const auto good = MakeSystem(invalidLife, "Unaffected atlas", 4, 6, 0.25f, 6);
        nifly::NifFile partial(invalidLife);
        std::ostringstream summary;
        auto* output = std::cout.rdbuf(summary.rdbuf());
        bool isolated = false;
        try {
            const auto count = AddHandMarble(partial, invalidLife, textures, 0.9, 1.15);
            SaveNif(partial, dir / "handfx-invalid-life.nif");
            nifly::NifFile read;
            LoadNif(read, dir / "handfx-invalid-life.nif");
            ValidateMarbleRefs(read);
            auto& h = read.GetHeader();
            auto* badMarble = DarkPartner(read, flame.id, marbleId);
            auto* goodMarble = DarkPartner(read, good.id, darkId);
            unsigned badMotion = 0, goodMotion = 0;
            if (badMarble) for (const auto& ref : badMarble->modifierRefs)
                badMotion += h.GetBlock<nifly::NiPSysRotationModifier>(ref) || h.GetBlock<nifly::BSPSysSubTexModifier>(ref);
            if (goodMarble) for (const auto& ref : goodMarble->modifierRefs)
                goodMotion += h.GetBlock<nifly::NiPSysRotationModifier>(ref) || h.GetBlock<nifly::BSPSysSubTexModifier>(ref);
            auto* keptEmber = h.GetBlock<nifly::NiParticleSystem>(ember.id);
            isolated = count.systems == 2 && count.emberSystems == 1 && badMarble && goodMarble &&
                badMotion == 0 && goodMotion == 2 && UpdateChain(h, *badMarble, marbleId) &&
                Near(h.GetBlock<nifly::BSEffectShaderProperty>(keptEmber->shaderPropertyRef)->baseColorScale, 2.5f) &&
                DarkPartner(read, ember.id, darkId);
        } catch (const std::exception&) { isolated = false; }
        std::cout.rdbuf(output);
        check.Check(isolated && summary.str().find("Unrelated A") != std::string::npos &&
            summary.str().find("lifespan") != std::string::npos,
            "handfx invalid lifespan: keep dark clone, skip only its new motion and report reason; other marble/ember survive reload");
        nifly::NifFile oldMotion(source);
        auto& oldHdr = oldMotion.GetHeader();
        auto* oldFlame = oldHdr.GetBlock<nifly::NiParticleSystem>(flame.id);
        auto oldRotation = std::make_unique<nifly::NiPSysRotationModifier>();
        oldRotation->name.get() = "Old inactive spin";
        oldRotation->order = 3000;
        oldRotation->targetRef.index = flame.id;
        oldRotation->initialSpeed = 9;
        const auto oldRotationId = Add(oldHdr, std::move(oldRotation));
        oldFlame->modifierRefs.AddBlockRef(oldRotationId);
        auto oldSubtex = std::make_unique<nifly::BSPSysSubTexModifier>();
        oldSubtex->name.get() = "Old inactive cells";
        oldSubtex->order = 3000;
        oldSubtex->targetRef.index = flame.id;
        oldSubtex->frameCount = 99;
        oldFlame->modifierRefs.AddBlockRef(Add(oldHdr, std::move(oldSubtex)));
        auto active = std::make_unique<nifly::NiPSysModifierActiveCtlr>();
        active->targetRef.index = flame.id;
        active->modifierName.get() = "Old inactive spin";
        auto disable = std::make_unique<nifly::NiBoolInterpolator>();
        disable->boolValue = false;
        active->interpolatorRef.index = Add(oldHdr, std::move(disable));
        oldHdr.GetBlock<nifly::NiPSysEmitterLifeSpanCtlr>(flame.life)->nextControllerRef.index = Add(oldHdr, std::move(active));
        nifly::NifFile replaced(oldMotion);
        (void)AddHandMarble(replaced, oldMotion, textures, 0.9, 1.15);
        auto* newMarble = DarkPartner(replaced, flame.id, marbleId);
        bool replacedMotion = newMarble != nullptr;
        rotations = subtex = 0;
        if (newMarble) {
            for (const auto& ref : newMarble->modifierRefs) {
                if (auto* r = replaced.GetHeader().GetBlock<nifly::NiPSysRotationModifier>(ref)) {
                    ++rotations;
                    replacedMotion &= r->isActive && Near(r->initialSpeed, 0.6f * kPi);
                }
                if (auto* s = replaced.GetHeader().GetBlock<nifly::BSPSysSubTexModifier>(ref)) {
                    ++subtex;
                    replacedMotion &= s->isActive && Near(s->frameCount, 4.2f);
                }
            }
            auto id = newMarble->controllerRef.index;
            std::set<std::uint32_t> seen;
            while (auto* ctrl = replaced.GetHeader().GetBlock<nifly::NiTimeController>(id)) {
                if (!seen.insert(id).second) { replacedMotion = false; break; }
                if (auto* mod = dynamic_cast<nifly::NiPSysModifierCtlr*>(ctrl))
                    replacedMotion &= mod->modifierName.get() != "Old inactive spin";
                id = ctrl->nextControllerRef.index;
            }
        }
        const auto* keptRotation = replaced.GetHeader().GetBlock<nifly::NiPSysRotationModifier>(oldRotationId);
        check.Check(replacedMotion && rotations == 1 && subtex == 1 && keptRotation &&
            !keptRotation->isActive && Near(keptRotation->initialSpeed, 9),
            "handfx old motion: clone replaces inactive modifiers and their disable controller; original white stays inactive");
    }

    void CheckSequenceColourKeys(Checker& a_check)
    {
        for (const bool linked : { true, false }) {
            nifly::NifFile source;
            source.Create(nifly::NiVersion::getSSE());
            auto& hdr = source.GetHeader();

            const auto shaderId = Add(hdr, std::make_unique<nifly::BSEffectShaderProperty>());

            auto blend = std::make_unique<nifly::NiBlendPoint3Interpolator>();
            blend->point.x = 1.0f;
            blend->point.y = 0.2f;
            blend->point.z = 0.1f;
            const auto blendId = Add(hdr, std::move(blend));

            auto ctrl = std::make_unique<nifly::BSEffectShaderPropertyColorController>();
            ctrl->targetRef.index = shaderId;
            ctrl->interpolatorRef.index = blendId;
            const auto ctrlId = Add(hdr, std::move(ctrl));
            hdr.GetBlock<nifly::BSEffectShaderProperty>(shaderId)->controllerRef.index = ctrlId;

            auto posData = std::make_unique<nifly::NiPosData>();
            posData->data.SetInterpolationType(nifly::NiKeyType::LINEAR_KEY);
            nifly::NiAnimationKey<nifly::Vector3> key;
            key.time = 0.0f;
            key.value.x = 1.0f;
            key.value.y = 0.2f;
            key.value.z = 0.1f;
            posData->data.AddKey(key);
            key.time = 1.0f;
            posData->data.AddKey(key);
            const auto dataId = Add(hdr, std::move(posData));

            auto interp = std::make_unique<nifly::NiPoint3Interpolator>();
            interp->point3Value = key.value;
            interp->dataRef.index = dataId;
            const auto interpId = Add(hdr, std::move(interp));

            auto seq = std::make_unique<nifly::NiControllerSequence>();
            seq->name.get() = "Fixture sequence";
            nifly::ControllerLink link;
            if (linked) {
                link.controllerRef.index = ctrlId;
            }
            link.interpolatorRef.index = interpId;
            link.ctrlType.get() = "BSEffectShaderPropertyColorController";
            seq->controlledBlocks.push_back(link);
            Add(hdr, std::move(seq));

            const auto stats = GreyEffects(source, 0.7f, 0.85f, nullptr);
            const auto* greyData = hdr.GetBlock<nifly::NiPosData>(dataId);
            bool keysGrey = greyData && greyData->data.GetNumKeys() == 2;
            for (unsigned i = 0; keysGrey && i < greyData->data.GetNumKeys(); ++i) {
                const auto v = greyData->data.GetKey(i).value;
                keysGrey = v.x > 0.0f && Near(v.x, v.y) && Near(v.y, v.z);
            }
            const auto* greyBlend = hdr.GetBlock<nifly::NiBlendPoint3Interpolator>(blendId);
            const bool blendGrey = greyBlend && Near(greyBlend->point.x, greyBlend->point.y) &&
                Near(greyBlend->point.y, greyBlend->point.z);
            a_check.Check(keysGrey && blendGrey && stats.sequenceKeys == 2 && stats.controllersUnreached == 0,
                linked ? "sequence colour keys: linked ControllerLink greys NiPosData keys and marks controller reached"
                       : "sequence colour keys: null controllerRef greys NiPosData keys and clears unreached count");
        }
    }
}
