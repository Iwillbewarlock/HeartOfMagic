// Ordered alpha pairs must retain scene transforms and casting animation targets after serialization.
#include "GreySelfTest.h"
#include "GreyMarble.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>

#include <array>
#include <memory>

namespace GreyBuild::SelfTest
{
    void CheckMarbleOrder(Checker& check, const std::filesystem::path& dir)
    {
        constexpr const char* palettePath = "textures\\selftest\\ordered-fire.dds";
        TextureSource textures({}, {});
        RgbaImage paletteImage{ 2, 2, std::vector<std::uint8_t>(16), false };
        for (std::size_t i = 0; i < paletteImage.pixels.size(); i += 4) {
            paletteImage.pixels[i] = paletteImage.pixels[i + 3] = 255;
            paletteImage.pixels[i + 1] = 100;
        }
        textures.Put(palettePath, paletteImage);
        nifly::NifFile source;
        source.Create(nifly::NiVersion::getSSE());
        auto& src = source.GetHeader();
        const auto add = [&](auto object) { return src.AddBlock(std::move(object)); };
        auto parent = std::make_unique<nifly::NiNode>();
        parent->name.get() = "Casting parent";
        parent->transform.translation = { 20, 40, 60 };
        parent->transform.scale = 3;
        const auto parentId = add(std::move(parent));
        source.GetRootNode()->childRefs.AddBlockRef(parentId);
        auto shader = std::make_unique<nifly::BSEffectShaderProperty>();
        shader->sourceTexture.get() = "textures\\selftest\\ordered-atlas.dds";
        shader->greyscaleTexture.get() = palettePath;
        shader->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor;
        const auto shaderId = add(std::move(shader));
        auto alpha = std::make_unique<nifly::NiAlphaProperty>();
        alpha->flags = 13;
        const auto alphaId = add(std::move(alpha));
        auto sequence = std::make_unique<nifly::NiControllerSequence>();
        sequence->name.get() = "CastLoop";
        auto palette = std::make_unique<nifly::NiDefaultAVObjectPalette>();
        palette->sceneRef.index = 0;
        std::array<std::uint32_t, 2> systems{}, emitters{}, controllers{};
        for (std::uint32_t i = 0; i < systems.size(); ++i) {
            auto emitterNode = std::make_unique<nifly::NiNode>();
            emitterNode->name.get() = "Emitter " + std::to_string(i);
            const auto emitterNodeId = add(std::move(emitterNode));
            src.GetBlock<nifly::NiNode>(parentId)->childRefs.AddBlockRef(emitterNodeId);
            auto data = std::make_unique<nifly::NiPSysData>();
            auto version = nifly::NiVersion::getSSE();
            std::vector<nifly::Vector3> pool(8);
            data->Create(version, &pool, nullptr, nullptr, nullptr);
            data->hasTextureIndices = true;
            data->subtexOffsets.resize(2);
            data->subtexOffsets[0] = { 0, 0, 0.5f, 1 };
            data->subtexOffsets[1] = { 0.5f, 0, 0.5f, 1 };
            auto system = std::make_unique<nifly::NiParticleSystem>();
            system->name.get() = "Casting fire " + std::to_string(i);
            system->psysDataRef.index = add(std::move(data));
            system->shaderPropertyRef.index = shaderId;
            system->alphaPropertyRef.index = alphaId;
            system->isWorldSpace = i == 0;
            system->transform.translation = { 7, 11, 13 };
            system->transform.rotation[0] = { 0, -1, 0 };
            system->transform.rotation[1] = { 1, 0, 0 };
            system->transform.scale = static_cast<float>(i + 2);
            system->bounds = { { 1, 2, 3 }, 5 };
            systems[i] = add(std::move(system));
            src.GetBlock<nifly::NiNode>(parentId)->childRefs.AddBlockRef(systems[i]);
            auto emitter = std::make_unique<nifly::NiPSysSphereEmitter>();
            emitter->name.get() = "Sphere emitter";
            emitter->targetRef.index = systems[i];
            emitter->emitterNodeRef.index = emitterNodeId;
            emitters[i] = add(std::move(emitter));
            auto controller = std::make_unique<nifly::NiPSysEmitterCtlr>();
            controller->targetRef.index = systems[i];
            controller->modifierName.get() = "Sphere emitter";
            controller->interpolatorRef.index = add(std::make_unique<nifly::NiFloatInterpolator>());
            controllers[i] = add(std::move(controller));
            auto* ps = src.GetBlock<nifly::NiParticleSystem>(systems[i]);
            ps->modifierRefs.AddBlockRef(emitters[i]);
            // A later volume emitter must not erase the first emitter's successful match.
            auto otherEmitter = std::make_unique<nifly::NiPSysSphereEmitter>();
            otherEmitter->name.get() = "Other emitter";
            otherEmitter->targetRef.index = systems[i];
            otherEmitter->emitterNodeRef.index = parentId;
            ps->modifierRefs.AddBlockRef(add(std::move(otherEmitter)));
            ps->controllerRef.index = controllers[i];
            nifly::ControllerLink link;
            link.nodeName.get() = ps->name.get();
            link.ctrlType.get() = "NiPSysEmitterCtlr";
            link.ctrlID.get() = "Sphere emitter";
            if (i == 0) link.controllerRef.index = controllers[i]; // Other source binds by name.
            sequence->controlledBlocks.push_back(link);
            nifly::AVObject entry;
            entry.name.get() = ps->name.get();
            entry.objectRef.index = systems[i];
            palette->objects.push_back(entry);
        }
        const auto sequenceId = add(std::move(sequence));
        const auto paletteId = add(std::move(palette));
        auto manager = std::make_unique<nifly::NiControllerManager>();
        manager->targetRef.index = 0;
        manager->controllerSequenceRefs.AddBlockRef(sequenceId);
        manager->objectPaletteRef.index = paletteId;
        source.GetRootNode()->controllerRef.index = add(std::move(manager));
        nifly::NifFile copy(source);
        const auto totals = AddHandMarble(copy, source, textures, 0.9, 1.15);
        const auto path = dir / "marble-ordered-pairs.nif";
        SaveNif(copy, path);
        nifly::NifFile saved;
        LoadNif(saved, path);
        auto& hdr = saved.GetHeader();
        bool refs = true;
        try { ValidateMarbleRefs(saved); } catch (const std::exception&) { refs = false; }
        const auto* oldParent = src.GetBlock<nifly::NiNode>(parentId);
        const auto* newParent = hdr.GetBlock<nifly::NiNode>(parentId);
        bool order = newParent && newParent->childRefs.GetSize() == 4;
        bool transforms = newParent && newParent->transform.IsNearlyEqualTo(oldParent->transform);
        bool bindings = true, bounds = true, blend = true;
        for (std::uint32_t i = 0; i < systems.size(); ++i) {
            const auto* ordered = newParent ?
                hdr.GetBlock<nifly::BSOrderedNode>(newParent->childRefs.GetBlockRef(i * 2 + 1)) : nullptr;
            const auto* old = src.GetBlock<nifly::NiParticleSystem>(systems[i]);
            const auto* flame = hdr.GetBlock<nifly::NiParticleSystem>(systems[i]);
            auto* marble = ordered ?
                hdr.GetBlock<nifly::NiParticleSystem>(ordered->childRefs.GetBlockRef(0)) : nullptr;
            order &= ordered && marble && ordered->childRefs.GetSize() == 2 &&
                ordered->childRefs.GetBlockRef(1) == systems[i] &&
                newParent->childRefs.GetBlockRef(i * 2) == oldParent->childRefs.GetBlockRef(i * 2);
            if (!ordered || !flame || !marble) { transforms = bindings = bounds = blend = false; continue; }
            transforms &= ordered->transform.IsNearlyEqualTo(nifly::MatTransform{}) &&
                flame->transform.IsNearlyEqualTo(old->transform) && marble->transform.IsNearlyEqualTo(old->transform) &&
                flame->isWorldSpace == old->isWorldSpace && marble->isWorldSpace == old->isWorldSpace;
            // Positive authored radii cannot prove that moving particles stay inside a fixed
            // sphere, even in local space. The larger marble must contribute its live bound.
            bounds &= !ordered->isStaticBound && (ordered->flags & 0x2000) == 0;
            const auto* darkAlpha = hdr.GetBlock<nifly::NiAlphaProperty>(marble->alphaPropertyRef);
            blend &= darkAlpha && darkAlpha->flags == 4333 && flame->alphaPropertyRef.index == alphaId &&
                hdr.GetBlock<nifly::NiAlphaProperty>(alphaId)->flags == 13;
            bool emitterFound = false;
            for (const auto& ref : marble->modifierRefs) {
                if (const auto* emitter = hdr.GetBlock<nifly::NiPSysVolumeEmitter>(ref)) {
                    emitterFound |= emitter->emitterNodeRef.index ==
                        src.GetBlock<nifly::NiPSysVolumeEmitter>(emitters[i])->emitterNodeRef.index &&
                        emitter->targetRef.index == ordered->childRefs.GetBlockRef(0);
                }
            }
            bindings &= emitterFound && flame->name.get() == old->name.get() &&
                flame->controllerRef.index == controllers[i] && flame->modifierRefs.GetBlockRef(0) == emitters[i];
        }
        check.Check(totals.systems == 2 && order && refs,
            "marble ordered pairs: each flame slot holds [marble, flame], siblings and valid references survive save/reload");
        check.Check(transforms && bindings,
            "marble ordered transforms: identity wrappers preserve local/world-space systems, emitter nodes and source targets");
        check.Check(bounds && blend,
            "marble ordered bounds/blend: positive-radius local/world-space pairs use dynamic bounds; alpha modes unchanged");
        auto* seq = hdr.GetBlock<nifly::NiControllerSequence>(sequenceId);
        auto* objects = hdr.GetBlock<nifly::NiDefaultAVObjectPalette>(paletteId);
        bool resolved = seq && objects && seq->name.get() == "CastLoop" &&
            seq->controlledBlocks.size() == 4 && objects->objects.size() == 4;
        if (resolved) {
            for (const auto& link : seq->controlledBlocks) {
                bool found = false;
                for (const auto& entry : objects->objects) {
                    if (entry.name.get() != link.nodeName.get()) continue;
                    const auto* target = hdr.GetBlock<nifly::NiParticleSystem>(entry.objectRef);
                    const auto ctrlId = link.controllerRef.IsEmpty() && target ?
                        target->controllerRef.index : link.controllerRef.index;
                    const auto* ctrl = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(ctrlId);
                    found = target && target->name.get() == entry.name.get() && ctrl &&
                        ctrl->targetRef.index == entry.objectRef.index && ctrl->modifierName.get() == link.ctrlID.get();
                }
                resolved &= found;
            }
            for (std::uint32_t i = 0; i < systems.size(); ++i)
                resolved &= seq->controlledBlocks[i].nodeName.get() ==
                    src.GetBlock<nifly::NiParticleSystem>(systems[i])->name.get() &&
                    objects->objects[i].objectRef.index == systems[i] &&
                    objects->objects[i].name.get() == seq->controlledBlocks[i].nodeName.get();
        }
        check.Check(resolved, "marble ordered animation: original sequence/palette names survive; all explicit and named targets resolve");
        nifly::NifFile zeroSource(source);
        for (const auto id : systems)
            zeroSource.GetHeader().GetBlock<nifly::NiParticleSystem>(id)->bounds.radius = 0;
        nifly::NifFile zeroCopy(zeroSource);
        const auto zeroTotals = AddHandMarble(zeroCopy, zeroSource, textures, 0.9, 1.15);
        const auto zeroPath = dir / "marble-ordered-zero-bound.nif";
        SaveNif(zeroCopy, zeroPath);
        nifly::NifFile zeroSaved;
        LoadNif(zeroSaved, zeroPath);
        auto& zh = zeroSaved.GetHeader();
        bool dynamic = zeroTotals.systems == 2;
        try { ValidateMarbleRefs(zeroSaved); } catch (const std::exception&) { dynamic = false; }
        const auto* zeroParent = zh.GetBlock<nifly::NiNode>(parentId);
        dynamic &= zeroParent && zeroParent->childRefs.GetSize() == 4;
        for (std::uint32_t i = 0; i < systems.size(); ++i) {
            const auto* ordered = zeroParent ?
                zh.GetBlock<nifly::BSOrderedNode>(zeroParent->childRefs.GetBlockRef(i * 2 + 1)) : nullptr;
            dynamic &= ordered && !ordered->isStaticBound && ordered->flags == 0x8000E &&
                ordered->alphaSortBound.w == 0 && ordered->childRefs.GetSize() == 2 &&
                ordered->childRefs.GetBlockRef(1) == systems[i] &&
                zh.GetBlock<nifly::NiParticleSystem>(ordered->childRefs.GetBlockRef(0));
        }
        check.Check(dynamic,
            "marble ordered zero bounds: saved pairs use dynamic child bounds, with both static-bound controls clear");

        nifly::NifFile masterSource(source);
        auto& mh = masterSource.GetHeader();
        auto master = std::make_unique<nifly::BSMasterParticleSystem>();
        master->name.get() = "Casting master";
        master->childRefs.AddBlockRef(systems[0]);
        master->particleSysRefs.AddBlockRef(systems[0]);
        const auto masterId = mh.AddBlock(std::move(master));
        for (auto& ref : mh.GetBlock<nifly::NiNode>(parentId)->childRefs)
            if (ref.index == systems[0]) ref.index = masterId;
        nifly::NifFile masterCopy(masterSource);
        const auto masterTotals = AddHandMarble(masterCopy, masterSource, textures, 0.9, 1.15);
        const auto masterPath = dir / "marble-master-pairs.nif";
        SaveNif(masterCopy, masterPath);
        nifly::NifFile masterSaved;
        LoadNif(masterSaved, masterPath);
        auto& ms = masterSaved.GetHeader();
        bool direct = masterTotals.systems == 2;
        try { ValidateMarbleRefs(masterSaved); } catch (const std::exception&) { direct = false; }
        const auto* savedMaster = ms.GetBlock<nifly::BSMasterParticleSystem>(masterId);
        const auto* savedParent = ms.GetBlock<nifly::NiNode>(parentId);
        direct &= savedMaster && savedMaster->childRefs.GetSize() == 2 &&
            savedMaster->particleSysRefs.GetSize() == 2 && savedParent &&
            savedParent->childRefs.GetSize() == 4 && savedParent->childRefs.GetBlockRef(1) == masterId;
        if (direct) {
            const auto marbleId = savedMaster->childRefs.GetBlockRef(0);
            const auto* marble = ms.GetBlock<nifly::NiParticleSystem>(marbleId);
            const auto* ordinaryPair = ms.GetBlock<nifly::BSOrderedNode>(savedParent->childRefs.GetBlockRef(3));
            direct &= marble && savedMaster->childRefs.GetBlockRef(1) == systems[0] &&
                savedMaster->particleSysRefs.GetBlockRef(0) == marbleId &&
                savedMaster->particleSysRefs.GetBlockRef(1) == systems[0] &&
                ordinaryPair && ordinaryPair->childRefs.GetBlockRef(1) == systems[1];
        }
        std::size_t wrappers = 0;
        for (std::uint32_t id = 0; id < ms.GetNumBlocks(); ++id)
            if (ms.GetBlock<nifly::BSOrderedNode>(id)) ++wrappers;
        check.Check(direct && wrappers == 1,
            "marble master parent: direct particle children/references survive reload; only the ordinary pair gets a wrapper");
    }
}
