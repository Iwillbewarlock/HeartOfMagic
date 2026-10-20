#include "GreyParticleGraph.h"
#include "GreyShapes.h"
#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace GreyBuild::HandGraph
{
    void ChildRefs(nifly::NiObject& block, std::set<nifly::NiRef*>& refs)
    {
        block.GetChildRefs(refs);
        // nifly's NiBlendInterpolator does not enumerate its persisted child items.
        if (auto* blend = dynamic_cast<nifly::NiBlendInterpolator*>(&block))
            for (auto& item : blend->interpItems) refs.insert(&item.interpolatorRef);
    }

    // Only owned children are copied. Scene nodes, other shapes and back pointers stay external.
    // All indices are in the same header; nifly rebuilds its string table when saving.
    std::uint32_t CloneGraph(nifly::NiHeader& dst, nifly::NiHeader& src,
        std::uint32_t id, std::uint32_t root, Remap& map)
    {
        if (id == nifly::NIF_NPOS) return id;
        if (const auto it = map.find(id); it != map.end()) return it->second;
        auto* block = src.GetBlock<nifly::NiObject>(id);
        if (!block || src.GetBlock<nifly::NiUnknown>(id))
            throw std::runtime_error("marble: missing or unknown owned block");
        if (id != root && (dynamic_cast<nifly::NiAVObject*>(block) ||
            dynamic_cast<nifly::NiControllerManager*>(block))) return id;
        const auto added = dst.AddBlock(block->Clone());
        map.emplace(id, added);
        std::set<nifly::NiRef*> refs;
        ChildRefs(*dst.GetBlock<nifly::NiObject>(added), refs);
        // GetChildRefs orders pointers, which depends on heap layout. Allocate by source block
        // index instead so two fresh builds of the same key produce identical graph bytes.
        std::vector<nifly::NiRef*> ordered(refs.begin(), refs.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->index < b->index; });
        for (auto* ref : ordered) ref->index = CloneGraph(dst, src, ref->index, root, map);
        return added;
    }

    bool DropShaderController(nifly::NiHeader& hdr, std::uint32_t id)
    {
        if (hdr.GetBlock<nifly::BSEffectShaderPropertyColorController>(id)) return true;
        auto* f = hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(id);
        return f && f->typeOfControlledVariable == kFloatVarEmissiveMultiple;
    }

    void RemapPointers(nifly::NiHeader& hdr, const Remap& map)
    {
        for (const auto& [oldId, newId] : map) {
            auto* block = hdr.GetBlock<nifly::NiObject>(newId);
            std::set<nifly::NiPtr*> ptrs;
            block->GetPtrs(ptrs);
            for (auto* ptr : ptrs) {
                if (const auto it = map.find(ptr->index); it != map.end()) ptr->index = it->second;
            }
        }
    }

    void ScaleFloat(nifly::NiHeader& hdr, std::uint32_t id, float scale, std::set<std::uint32_t>& done)
    {
        if (id == nifly::NIF_NPOS || !done.insert(id).second) return;
        const auto value = [scale](float& f) { if (std::abs(f) < kHugeFloatValue) f *= scale; };
        if (auto* interp = hdr.GetBlock<nifly::NiFloatInterpolator>(id)) {
            value(interp->floatValue);
            ScaleFloat(hdr, interp->dataRef.index, scale, done);
        } else if (auto* blend = hdr.GetBlock<nifly::NiBlendFloatInterpolator>(id)) {
            value(blend->value);
            for (const auto& item : blend->interpItems)
                ScaleFloat(hdr, item.interpolatorRef.index, scale, done);
        } else if (auto* data = hdr.GetBlock<nifly::NiFloatData>(id)) {
            for (unsigned i = 0; i < data->data.GetNumKeys(); ++i) {
                auto key = data->data.GetKey(i);
                value(key.value); value(key.forward); value(key.backward);
                data->data.SetKey(i, key);
            }
        } else {
            throw std::runtime_error("marble: unsupported animated radius/alpha interpolator");
        }
    }

    // A sequence owns interpolators not reachable from the particle's controller chain.
    // Duplicate its links, resolving reference-based and name-only bindings, for the new name.
    std::uint32_t NamedController(nifly::NiHeader& src, std::uint32_t root, const nifly::ControllerLink& link)
    {
        auto* system = src.GetBlock<nifly::NiParticleSystem>(root);
        std::uint32_t target = root;
        if (!link.propType.get().empty()) {
            if (link.propType.get() == "BSEffectShaderProperty") target = system->shaderPropertyRef.index;
            else if (link.propType.get() == "NiAlphaProperty") target = system->alphaPropertyRef.index;
            else throw std::runtime_error("marble: unsupported named controller property " + link.propType.get());
        }
        std::uint32_t found = nifly::NIF_NPOS;
        // Include sequence-owned controllers outside the object's chain, but require an exact target.
        for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
            auto* ctrl = src.GetBlock<nifly::NiTimeController>(id);
            if (!ctrl || ctrl->targetRef.index != target || link.ctrlType.get() != ctrl->GetBlockName()) continue;
            if (auto* modifier = dynamic_cast<nifly::NiPSysModifierCtlr*>(ctrl)) {
                if (!link.ctrlID.get().empty() && link.ctrlID.get() != modifier->modifierName.get()) continue;
            }
            if (auto* shader = dynamic_cast<nifly::BSEffectShaderPropertyFloatController*>(ctrl)) {
                if (!link.ctrlID.get().empty() && link.ctrlID.get() != std::to_string(shader->typeOfControlledVariable)) continue;
            }
            if (found != nifly::NIF_NPOS)
                throw std::runtime_error("marble: ambiguous named controller " + link.ctrlType.get());
            found = id;
        }
        if (found == nifly::NIF_NPOS)
            throw std::runtime_error("marble: unresolved named controller " + link.ctrlType.get());
        return found;
    }

    void CloneSequenceLinks(nifly::NiHeader& dst, nifly::NiHeader& src, std::uint32_t root,
        Remap& map, const std::string& oldName, const std::string& newName, bool shaderOnly)
    {
        for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
            auto* sequence = src.GetBlock<nifly::NiControllerSequence>(id);
            if (!sequence) continue;
            auto* target = dst.GetBlock<nifly::NiControllerSequence>(id);
            for (std::uint32_t i = 0; i < sequence->controlledBlocks.size(); ++i) {
                auto link = sequence->controlledBlocks[i];
                auto controller = link.controllerRef.index;
                const bool direct = map.count(controller) != 0;
                const bool named = !oldName.empty() && link.nodeName.get() == oldName;
                if (!direct && !named) continue;
                if (shaderOnly && !link.nodeName.get().empty() && !named) continue;
                if (!shaderOnly && link.ctrlType.get() == "BSEffectShaderPropertyColorController") continue;
                if (controller == nifly::NIF_NPOS) controller = NamedController(src, root, link);
                // A matching name is not ownership: explicit links may target a different same-named node.
                const auto* ctrl = src.GetBlock<nifly::NiTimeController>(controller);
                const auto* system = src.GetBlock<nifly::NiParticleSystem>(root);
                if (!ctrl || (ctrl->targetRef.index != root &&
                    ctrl->targetRef.index != system->shaderPropertyRef.index &&
                    ctrl->targetRef.index != system->alphaPropertyRef.index)) continue;
                if (shaderOnly && ctrl->targetRef.index != system->shaderPropertyRef.index) continue;
                if (!shaderOnly && DropShaderController(src, controller)) continue;
                link.controllerRef.index = CloneGraph(dst, src, controller, root, map);
                link.interpolatorRef.index = CloneGraph(dst, src, link.interpolatorRef.index, root, map);
                link.nodeName.get() = newName;
                // Brightening replaces this object's binding, not a second writer with the
                // same name. A different consumer of a shared shader keeps its original link.
                if (shaderOnly && named) target->controlledBlocks[i] = link;
                else target->controlledBlocks.push_back(link);
            }
        }
    }

    void Before(nifly::NiRefArray& refs, std::uint32_t source, std::uint32_t added)
    {
        std::vector<std::uint32_t> ids, out;
        refs.GetIndices(ids);
        for (const auto id : ids) {
            if (id == source) out.push_back(added);
            out.push_back(id);
        }
        refs.SetIndices(out);
    }

    void Attach(nifly::NiHeader& dst, nifly::NiHeader& src, std::uint32_t root,
        std::uint32_t added, const std::string& name)
    {
        const auto* flame = src.GetBlock<nifly::NiParticleSystem>(root);
        bool masterManaged = false;
        for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
            if (auto* master = src.GetBlock<nifly::BSMasterParticleSystem>(id)) {  // nifly's ref arrays iterate non-const only
                for (const auto& ref : master->childRefs) masterManaged |= ref.index == root;
                for (const auto& ref : master->particleSysRefs) masterManaged |= ref.index == root;
            }
        }
        auto orderedId = nifly::NIF_NPOS;
        if (masterManaged) {
            // Masters reference systems directly; an intervening ordered node's engine
            // update/render behavior is unverified. Retain direct siblings for this pair.
            std::cout << "NOTE  marble " << name <<
                ": ordered wrapper skipped; BSMasterParticleSystem direct-reference wrapping is unverified\n";
        } else {
            auto ordered = std::make_unique<nifly::BSOrderedNode>();
            ordered->name.get() = name + " Order";
            ordered->transform.Clear();
            // The identity wrapper occupies the flame's old parent slot. Keep both particle
            // transforms and all emitter pointers untouched; move only the bound into parent CS.
            auto bound = flame->bounds;
            if (!src.GetVersion().IsSSE()) {
                if (const auto* data = src.GetBlock<nifly::NiGeometryData>(flame->dataRef))
                    bound = data->GetBounds();
            }
            const auto center = flame->transform.ApplyTransform(bound.center);
            ordered->alphaSortBound = nifly::Vector4(center.x, center.y, center.z,
                std::abs(flame->transform.scale) * bound.radius);
            // An authored radius does not prove invariance as the hand/emitter moves.
            // Always use live child bounds (including the scaled marble), especially in
            // world space. No static path is retained without an invariance proof.
            ordered->isStaticBound = false;
            ordered->flags = 0x8000E; // AVObject fixed-bound bit 13 stays clear.
            ordered->childRefs.AddBlockRef(added);
            ordered->childRefs.AddBlockRef(root);
            orderedId = dst.AddBlock(std::move(ordered));
        }
        bool attached = false;
        for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
            if (auto* node = dst.GetBlock<nifly::NiNode>(id)) {
                for (auto& ref : node->childRefs) {
                    if (ref.index != root) continue;
                    if (!masterManaged) ref.index = orderedId;
                    attached = true;
                }
                if (masterManaged) Before(node->childRefs, root, added);
            }
            if (auto* master = dst.GetBlock<nifly::BSMasterParticleSystem>(id))
                Before(master->particleSysRefs, root, added);
            if (auto* palette = dst.GetBlock<nifly::NiDefaultAVObjectPalette>(id)) {
                bool found = false;
                for (auto& obj : palette->objects) found |= obj.objectRef.index == root;
                if (found) {
                    nifly::AVObject obj;
                    obj.name.get() = name;
                    obj.objectRef.index = added;
                    palette->objects.push_back(obj);
                }
            }
        }
        if (!attached) throw std::runtime_error("marble: source system has no scene parent");
    }

}
