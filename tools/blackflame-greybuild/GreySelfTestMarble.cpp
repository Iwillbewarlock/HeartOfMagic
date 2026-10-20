// A synthetic flame graph exercises clone ownership, animation and saved NIF references without game assets.
#include "GreySelfTest.h"
#include "GreySelfTestKeyed.h"
#include "GreyMarble.h"
#include "GreyTexStats.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>

#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr const char* kFirePalette = "textures\\selftest\\marble-fire.dds";
        struct Fixture
        {
            std::uint32_t system, shader, alpha, emitter, color, rotation, sequence, rate, rateData, radiusValue;
        };

        template<class T>
        std::uint32_t Add(nifly::NifFile& a_nif, std::unique_ptr<T> a_block)
        {
            return a_nif.GetHeader().AddBlock(std::move(a_block));
        }

        Fixture Make(nifly::NifFile& a_nif)
        {
            a_nif.Create(nifly::NiVersion::getSSE());
            Fixture f{};
            auto node = std::make_unique<nifly::NiNode>();
            node->name.get() = "Test emitter volume";
            const auto emitterNode = Add(a_nif, std::move(node));
            a_nif.GetRootNode()->childRefs.AddBlockRef(emitterNode);
            auto shader = std::make_unique<nifly::BSEffectShaderProperty>();
            shader->sourceTexture.get() = "textures\\selftest\\arbitrary-flame-atlas.dds";
            shader->greyscaleTexture.get() = kFirePalette;
            shader->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor;
            shader->baseColor = { 1, 1, 1, 0.5f }; // Normalise a non-unit source peak to requested opacity.
            shader->baseColorScale = 1;
            f.shader = Add(a_nif, std::move(shader));
            auto alpha = std::make_unique<nifly::NiAlphaProperty>();
            alpha->flags = 13 | (5u << 10); // Non-default test function must survive on the source flame.
            f.alpha = Add(a_nif, std::move(alpha));
            auto data = std::make_unique<nifly::NiPSysData>();
            std::vector<nifly::Vector3> pool(24);
            auto version = nifly::NiVersion::getSSE();
            data->Create(version, &pool, nullptr, nullptr, nullptr);
            data->hasTextureIndices = true;
            data->subtexOffsets.resize(4);
            for (std::uint32_t i = 0; i < 4; ++i)
                data->subtexOffsets[i] = { static_cast<float>(i) / 4, 0, 0.25f, 1 };
            const auto dataId = Add(a_nif, std::move(data));
            auto system = std::make_unique<nifly::NiParticleSystem>();
            system->name.get() = "Arbitrary fixture fire";
            system->psysDataRef.index = dataId;
            system->shaderPropertyRef.index = f.shader;
            system->alphaPropertyRef.index = f.alpha;
            system->isWorldSpace = true;
            f.system = Add(a_nif, std::move(system));
            a_nif.GetRootNode()->childRefs.AddBlockRef(f.system);
            auto emitter = std::make_unique<nifly::NiPSysSphereEmitter>();
            emitter->name.get() = "Independent emitter";
            emitter->targetRef.index = f.system;
            emitter->emitterNodeRef.index = emitterNode;
            emitter->isActive = true;
            emitter->radius = 3;  // The volume's radius must stay unchanged.
            emitter->NiPSysEmitter::radius = 2;
            emitter->radiusVariation = 0.4f;
            emitter->lifeSpan = 0.6f;
            emitter->lifeSpanVariation = 0.2f;
            emitter->color = { 1, 1, 1, 1 };
            f.emitter = Add(a_nif, std::move(emitter));
            auto color = std::make_unique<nifly::BSPSysSimpleColorModifier>();
            color->name.get() = "Life colour";
            color->targetRef.index = f.system;
            color->isActive = true;
            color->color1 = { 1, 0.3f, 0, 0 };
            color->color2 = { 1, 0.8f, 0, 0.5f };
            color->color3 = { 0.8f, 0.1f, 0, 0.125f };
            color->fadeInPercent = 0.1f;
            color->fadeOutPercent = 0.8f;
            color->color2StartPercent = 0.2f;
            color->color2EndPercent = 0.6f;
            f.color = Add(a_nif, std::move(color));
            auto rotation = std::make_unique<nifly::NiPSysRotationModifier>();
            rotation->name.get() = "Rotation";
            rotation->targetRef.index = f.system;
            rotation->initialAngle = 0.2f;
            rotation->initialAngleVariation = 0.5f;
            f.rotation = Add(a_nif, std::move(rotation));
            auto keys = std::make_unique<nifly::NiFloatData>();
            keys->data.SetInterpolationType(nifly::NiKeyType::LINEAR_KEY);
            nifly::NiAnimationKey<float> key;
            key.value = 17;
            keys->data.AddKey(key);
            f.rateData = Add(a_nif, std::move(keys));
            auto rate = std::make_unique<nifly::NiFloatInterpolator>();
            rate->floatValue = 17;
            rate->dataRef.index = f.rateData;
            f.rate = Add(a_nif, std::move(rate));
            auto radiusValue = std::make_unique<nifly::NiFloatInterpolator>();
            radiusValue->floatValue = 2;
            f.radiusValue = Add(a_nif, std::move(radiusValue));
            auto radiusBlend = std::make_unique<nifly::NiBlendFloatInterpolator>();
            radiusBlend->flags = static_cast<nifly::InterpBlendFlags>(0);
            radiusBlend->value = 2;
            radiusBlend->arraySize = radiusBlend->interpCount = 1;
            radiusBlend->singleIndex = 0;
            radiusBlend->interpItems.resize(1);
            radiusBlend->interpItems[0].interpolatorRef.index = f.radiusValue;
            radiusBlend->interpItems[0].weight = radiusBlend->interpItems[0].normalizedWeight = 1;
            auto radiusController = std::make_unique<nifly::NiPSysEmitterInitialRadiusCtlr>();
            radiusController->targetRef.index = f.system;
            radiusController->modifierName.get() = "Independent emitter";
            radiusController->interpolatorRef.index = Add(a_nif, std::move(radiusBlend));
            const auto radiusControllerId = Add(a_nif, std::move(radiusController));
            auto controller = std::make_unique<nifly::NiPSysEmitterCtlr>();
            controller->targetRef.index = f.system;
            controller->modifierName.get() = "Independent emitter";
            controller->interpolatorRef.index = Add(a_nif, std::make_unique<nifly::NiBlendFloatInterpolator>());
            controller->nextControllerRef.index = radiusControllerId;
            const auto controllerId = Add(a_nif, std::move(controller));
            auto sequence = std::make_unique<nifly::NiControllerSequence>();
            sequence->name.get() = "Emission sequence";
            nifly::ControllerLink link;
            link.nodeName.get() = "Arbitrary fixture fire";
            link.ctrlType.get() = "NiPSysEmitterCtlr";
            link.ctrlID.get() = "Independent emitter";
            link.controllerRef.index = controllerId;
            link.interpolatorRef.index = f.rate;
            sequence->controlledBlocks.push_back(link);
            f.sequence = Add(a_nif, std::move(sequence));
            auto manager = std::make_unique<nifly::NiControllerManager>();
            manager->targetRef.index = 0;
            manager->controllerSequenceRefs.AddBlockRef(f.sequence);
            a_nif.GetRootNode()->controllerRef.index = Add(a_nif, std::move(manager));
            auto* ps = a_nif.GetHeader().GetBlock<nifly::NiParticleSystem>(f.system);
            ps->controllerRef.index = controllerId;
            for (const auto id : { f.emitter, f.color, f.rotation }) ps->modifierRefs.AddBlockRef(id);
            return f;
        }

        bool Near(float a, float b) { return std::abs(a - b) < 1e-5f; }

        bool ValidRefs(nifly::NifFile& a_nif)
        {
            auto& hdr = a_nif.GetHeader();
            for (std::uint32_t i = 0; i < hdr.GetNumBlocks(); ++i) {
                auto* block = hdr.GetBlock<nifly::NiObject>(i);
                std::set<nifly::NiRef*> refs;
                std::set<nifly::NiPtr*> ptrs;
                block->GetChildRefs(refs);
                if (auto* blend = dynamic_cast<nifly::NiBlendInterpolator*>(block))
                    for (auto& item : blend->interpItems) refs.insert(&item.interpolatorRef);
                block->GetPtrs(ptrs);
                for (const auto* ref : refs) if (!ref->IsEmpty() && !hdr.GetBlock<nifly::NiObject>(ref->index)) return false;
                for (const auto* ptr : ptrs) if (!ptr->IsEmpty() && !hdr.GetBlock<nifly::NiObject>(ptr->index)) return false;
            }
            return true;
        }

        std::string Bytes(const std::filesystem::path& a_file)
        {
            std::ifstream input(a_file, std::ios::binary);
            return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        }

        void CheckBindings(Checker& check, nifly::NifFile& source, const Fixture& f, TextureSource& textures)
        {
            for (bool sharedInterpolator : { false, true }) {
                nifly::NifFile input(source);
                auto& src = input.GetHeader();
                auto* emitter = src.GetBlock<nifly::NiPSysEmitterCtlr>(
                    src.GetBlock<nifly::NiParticleSystem>(f.system)->controllerRef);
                auto* radius = src.GetBlock<nifly::NiPSysEmitterInitialRadiusCtlr>(emitter->nextControllerRef);
                auto* blend = src.GetBlock<nifly::NiBlendFloatInterpolator>(radius->interpolatorRef);
                if (sharedInterpolator) blend->interpItems[0].interpolatorRef.index = f.rate;
                else src.GetBlock<nifly::NiFloatInterpolator>(f.radiusValue)->dataRef.index = f.rateData;
                auto lifetime = std::make_unique<nifly::NiPSysEmitterLifeSpanCtlr>();
                lifetime->targetRef.index = f.system;
                lifetime->modifierName.get() = "Independent emitter";
                lifetime->interpolatorRef.index = f.rate;
                radius->nextControllerRef.index = Add(input, std::move(lifetime));
                auto* sequence = src.GetBlock<nifly::NiControllerSequence>(f.sequence);
                auto link = sequence->controlledBlocks[0];
                link.controllerRef.index = src.GetBlock<nifly::NiPSysEmitterCtlr>(
                    src.GetBlock<nifly::NiParticleSystem>(f.system)->controllerRef)->nextControllerRef.index;
                link.ctrlType.get() = "NiPSysEmitterInitialRadiusCtlr";
                sequence->controlledBlocks.push_back(link); // Same interpolator as the rate sequence link.
                nifly::NifFile copy(input);
                (void)AddHandMarble(copy, input, textures, 0.9, 1.15);
                auto& hdr = copy.GetHeader();
                const auto* ps = hdr.GetBlock<nifly::NiParticleSystem>(src.GetNumBlocks());
                const auto* e = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(ps->controllerRef);
                const auto* r = hdr.GetBlock<nifly::NiPSysEmitterInitialRadiusCtlr>(e->nextControllerRef);
                const auto* l = hdr.GetBlock<nifly::NiPSysEmitterLifeSpanCtlr>(r->nextControllerRef);
                const auto* b = hdr.GetBlock<nifly::NiBlendFloatInterpolator>(r->interpolatorRef);
                const auto* value = hdr.GetBlock<nifly::NiFloatInterpolator>(b->interpItems[0].interpolatorRef);
                const auto* life = hdr.GetBlock<nifly::NiFloatInterpolator>(l->interpolatorRef);
                auto* seq = hdr.GetBlock<nifly::NiControllerSequence>(f.sequence);
                const auto* rate = hdr.GetBlock<nifly::NiFloatInterpolator>(seq->controlledBlocks[2].interpolatorRef);
                const auto* size = hdr.GetBlock<nifly::NiFloatInterpolator>(seq->controlledBlocks[3].interpolatorRef);
                check.Check(value && life && rate && size && Near(value->floatValue, (sharedInterpolator ? 17 : 2) * 1.15f) &&
                    Near(hdr.GetBlock<nifly::NiFloatData>(value->dataRef)->data.GetKey(0).value, 17 * 1.15f) &&
                    Near(life->floatValue, 17) && Near(rate->floatValue, 17) && Near(size->floatValue, 17 * 1.15f) &&
                    Near(hdr.GetBlock<nifly::NiFloatData>(life->dataRef)->data.GetKey(0).value, 17) && ValidRefs(copy),
                    "marble shared animation: scale changes only radius, keeps rate/lifetime (shared interpolator " +
                    std::to_string(sharedInterpolator) + ")");
            }
            nifly::NifFile input(source);
            auto& src = input.GetHeader();
            auto* sequence = src.GetBlock<nifly::NiControllerSequence>(f.sequence);
            std::uint32_t previous = nifly::NIF_NPOS;
            for (auto variable : { kFloatVarEmissiveMultiple, kFloatVarAlpha }) {
                auto ctrl = std::make_unique<nifly::BSEffectShaderPropertyFloatController>();
                ctrl->typeOfControlledVariable = variable;
                ctrl->targetRef.index = f.shader;
                ctrl->nextControllerRef.index = previous;
                ctrl->interpolatorRef.index = f.rate;
                previous = Add(input, std::move(ctrl));
                nifly::ControllerLink link;
                link.nodeName.get() = "Arbitrary fixture fire";
                link.propType.get() = "BSEffectShaderProperty";
                link.ctrlType.get() = "BSEffectShaderPropertyFloatController";
                link.ctrlID.get() = std::to_string(variable);
                link.interpolatorRef.index = f.rate;
                sequence->controlledBlocks.push_back(link);
            }
            src.GetBlock<nifly::BSEffectShaderProperty>(f.shader)->controllerRef.index = previous;
            auto* emitterCtrl = src.GetBlock<nifly::NiPSysEmitterCtlr>(
                src.GetBlock<nifly::NiParticleSystem>(f.system)->controllerRef);
            auto lifetime = std::make_unique<nifly::NiPSysEmitterLifeSpanCtlr>();
            lifetime->targetRef.index = f.system;
            lifetime->modifierName.get() = "Independent emitter";
            lifetime->interpolatorRef.index = f.rate;
            src.GetBlock<nifly::NiPSysEmitterInitialRadiusCtlr>(emitterCtrl->nextControllerRef)->nextControllerRef.index =
                Add(input, std::move(lifetime));
            nifly::NifFile copy(input);
            (void)AddHandMarble(copy, input, textures, 0.9, 1.15);
            auto* seq = copy.GetHeader().GetBlock<nifly::NiControllerSequence>(f.sequence);
            const auto* ctrl = copy.GetHeader().GetBlock<nifly::BSEffectShaderPropertyFloatController>(
                seq->controlledBlocks[seq->controlledBlocks.size() - 1].controllerRef);
            check.Check(seq->controlledBlocks.size() == 5 && ctrl && ctrl->typeOfControlledVariable == kFloatVarAlpha,
                "marble named shader controllers: ctrlID resolves alpha beside emissive-multiple and drops only emissive link");
            auto& hdr = copy.GetHeader();
            const auto valueIs = [&](const nifly::NiRef& ref, float expected) {
                const auto* value = hdr.GetBlock<nifly::NiFloatInterpolator>(ref);
                const auto* data = value ? hdr.GetBlock<nifly::NiFloatData>(value->dataRef) : nullptr;
                return value && data && Near(value->floatValue, expected) && Near(data->data.GetKey(0).value, expected);
            };
            const auto* rateCtrl = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(seq->controlledBlocks[3].controllerRef);
            const auto* radiusCtrl = hdr.GetBlock<nifly::NiPSysEmitterInitialRadiusCtlr>(rateCtrl->nextControllerRef);
            const auto* lifeCtrl = hdr.GetBlock<nifly::NiPSysEmitterLifeSpanCtlr>(radiusCtrl->nextControllerRef);
            check.Check(ctrl && valueIs(ctrl->interpolatorRef, 0.9f) &&
                valueIs(seq->controlledBlocks[4].interpolatorRef, 0.9f) &&
                valueIs(seq->controlledBlocks[3].interpolatorRef, 17) && valueIs(lifeCtrl->interpolatorRef, 17),
                "marble shared alpha: controller/sequence opacity normalises independently; emission rate and lifetime stay 17");

            nifly::NifFile other(source);
            auto foreign = std::make_unique<nifly::NiPSysEmitterCtlr>();
            foreign->targetRef.index = 1; // An unrelated same-named scene node.
            other.GetHeader().GetBlock<nifly::NiNode>(1)->name.get() = "Arbitrary fixture fire";
            auto link = other.GetHeader().GetBlock<nifly::NiControllerSequence>(f.sequence)->controlledBlocks[0];
            link.controllerRef.index = Add(other, std::move(foreign));
            other.GetHeader().GetBlock<nifly::NiControllerSequence>(f.sequence)->controlledBlocks.push_back(link);
            nifly::NifFile otherCopy(other);
            (void)AddHandMarble(otherCopy, other, textures, 0.9, 1.15);
            check.Check(otherCopy.GetHeader().GetBlock<nifly::NiControllerSequence>(f.sequence)->controlledBlocks.size() == 3,
                "marble ownership: same nodeName cannot copy an explicit controller targeting another block");
            nifly::NifFile mismatched(source);
            Add(mismatched, std::make_unique<nifly::NiFloatData>());
            bool rejected = false;
            try { (void)AddHandMarble(mismatched, source, textures, 0.9, 1.15); }
            catch (const std::exception& e) { rejected = std::string(e.what()).find("block counts differ") != std::string::npos; }
            check.Check(rejected && mismatched.GetHeader().GetNumBlocks() == source.GetHeader().GetNumBlocks() + 1,
                "marble guard: mismatched block counts rejected before mutation");
        }

        void CheckRoleRequest(Checker& a_check, const std::filesystem::path& a_dir, nifly::NifFile& a_source,
            const Fixture& a_ids)
        {
            namespace K = Keyed;
            namespace CC = BlackFlame::CopyContract;
            nifly::NifFile fixture(a_source);
            auto* sourceShader = fixture.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(a_ids.shader);
            sourceShader->sourceTexture.get() = K::kFxFire;
            sourceShader->greyscaleTexture.get() = K::kSourcePalette;
            const auto sourcePath = a_dir / "marble-role-source.nif";
            SaveNif(fixture, sourcePath);
            const auto baseline = K::MakeFixture(a_dir, "marble-role", sourcePath, K::Variant::kNormal);
            a_check.Check(K::Run(baseline) == 0, "marble role control: particle NIF builds as ordinary request object");
            const auto before = K::Progress(baseline);
            auto mixed = baseline;
            mixed.staging = baseline.folder / "mixed";
            auto request = nlohmann::json::parse(K::ReadAll(baseline.folder / "request.json"));
            CC::KeyPrefix prefix;
            prefix.outputIdentity = OutputIdentity();
            prefix.tint = 0.6;
            prefix.tintAdd = 0.9;
            prefix.gradBlackFlame = CC::FileHash::Hashed(K::Fnv(baseline.dump / kPalettePath));
            prefix.gradBlackFlameAdd = CC::FileHash::Hashed(K::Fnv(baseline.dump / kPaletteAddPath));
            CC::NifKeyInput input;
            input.path = "meshes\\magic\\keyed.nif";
            input.nif = CC::FileHash::Hashed(K::Fnv(baseline.dump / input.path));
            input.tintAddHands = 1.4;
            input.handMarbleOpacity = 0.9;
            input.handMarbleScale = 1.15;
            const auto text = CC::NifKeyString(prefix, input).value();
            const auto key = CC::KeyOf(text);
            auto hand = request["forms"][0];
            hand["source"] = "Synthetic.esp|000801";
            hand["type"] = "ARTO";
            hand["data"] = { { "artType", 0 } };
            hand["role"] = "castingArt";
            hand["tintAddHands"] = 1.4;
            hand["handMarbleOpacity"] = 0.9;
            hand["handMarbleScale"] = 1.15;
            hand["keyString"] = text;
            hand["key"] = CC::Hex16(key);
            hand["slot"] = 0xD01;
            request["forms"].insert(request["forms"].begin(), hand);
            K::Write(baseline.folder / "request.json", request.dump());
            a_check.Check(K::Run(mixed) == 0, "marble role request: casting art and ordinary copies build together");
            const auto after = K::Progress(mixed);
            const auto* result = K::Find(after, key);
            a_check.Check(result && CC::IsPublishable(result->outcome) && !result->path.empty(),
                "marble role request: casting art publishes a keyed NIF");
            if (result && !result->path.empty()) {
                nifly::NifFile built;
                LoadNif(built, StagedFile(mixed.staging, result->path));
                std::size_t systems = 0, dark = 0, ramp = 0;
                auto& hdr = built.GetHeader();
                for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                    const auto* ps = hdr.GetBlock<nifly::NiParticleSystem>(id);
                    if (!ps) continue;
                    ++systems;
                    const auto* shader = hdr.GetBlock<nifly::BSEffectShaderProperty>(ps->shaderPropertyRef);
                    const auto* alpha = hdr.GetBlock<nifly::NiAlphaProperty>(ps->alphaPropertyRef);
                    if (!shader || !alpha) continue;
                    if (Near(shader->baseColor.r, 0.006f) && Near(shader->baseColor.a, 0.9f) &&
                        (alpha->flags & 0x1ff) == 237 && !(alpha->flags & 512) &&
                        (shader->shaderFlags1 & (1u << 31)) && !(shader->shaderFlags2 & 1)) ++dark;
                    if ((shader->shaderFlags1 & kSlsf1GreyscaleToPaletteColor) &&
                        shader->greyscaleTexture.get() != K::kSourcePalette &&
                        std::filesystem::exists(StagedFile(mixed.staging, shader->greyscaleTexture.get()))) ++ramp;
                }
                a_check.Check(systems == 2 && dark == 1 && ramp == 1 && ValidRefs(built),
                    "marble role integration: saved casting art has bright ramp plus one correctly blended dark particle graph");
            }
            bool unchanged = true;
            for (const auto& line : before.lines) {
                if (!CC::IsPublishable(line.outcome)) continue;
                const auto* now = K::Find(after, line.key);
                unchanged &= now && now->path == line.path && now->fnv64 == line.fnv64 && now->bytes == line.bytes;
                if (now && !line.path.empty()) unchanged &= Bytes(StagedFile(baseline.staging, line.path)) ==
                    Bytes(StagedFile(mixed.staging, now->path));
            }
            a_check.Check(unchanged, "marble role isolation: every ordinary NIF/MPS/TXST/DDS copy remains byte-identical");
            // A directory at the marble output path forces a real SaveNif I/O failure after AddHandMarble succeeds.
            if (!result) {
                return;  // the checks above already failed: no casting art copy to block
            }
            auto io = mixed;
            io.staging = baseline.folder / "marble-io";
            const auto blocked = StagedFile(io.staging, result->path + ".tmp.marble.tmp");
            K::Write(blocked / "keep.txt", "force marble write failure");
            a_check.Check(K::Run(io) == 0, "marble I/O: request completes with a per-object failure");
            const auto ioProgress = K::Progress(io);
            const auto* failed = K::Find(ioProgress, key);
            a_check.Check(failed && failed->outcome == CC::Outcome::kFailed && failed->path.empty() &&
                failed->reason.find("cannot open") != std::string::npos,
                "marble I/O: SaveNif failure is failed, never a published ramp-only fallback");
            {
                ProgressLog log;
                std::string why;
                a_check.Check(log.Open(io.staging, why) && !log.Reusable(key, kDefaultJobMemoryMiB * kBytesPerMiB),
                    "marble I/O: failed key is not reusable on the next run");
            }
            std::filesystem::remove(blocked / "keep.txt");
            std::filesystem::remove(blocked);
            a_check.Check(K::Run(io) == 0, "marble I/O: request retries after the write obstruction is removed");
            const auto retryProgress = K::Progress(io);
            const auto* retry = K::Find(retryProgress, key);
            a_check.Check(retry && result && retry->outcome == result->outcome && retry->fnv64 == result->fnv64,
                "marble I/O: retry produces the complete marble copy, not a cached ramp-only copy");
            // A valid NIF with an unsupported radius interpolator must still publish its ramp-only result.
            auto& brokenHdr = fixture.GetHeader();
            auto* emitter = brokenHdr.GetBlock<nifly::NiPSysEmitterCtlr>(
                brokenHdr.GetBlock<nifly::NiParticleSystem>(a_ids.system)->controllerRef);
            auto* radius = brokenHdr.GetBlock<nifly::NiPSysEmitterInitialRadiusCtlr>(emitter->nextControllerRef);
            radius->interpolatorRef.index = Add(fixture, std::make_unique<nifly::NiBoolInterpolator>());
            SaveNif(fixture, sourcePath);
            const auto fallback = K::MakeFixture(a_dir, "marble-fallback", sourcePath, K::Variant::kNormal);
            auto fallbackRequest = nlohmann::json::parse(K::ReadAll(fallback.folder / "request.json"));
            input.nif = CC::FileHash::Hashed(K::Fnv(fallback.dump / input.path));
            const auto fallbackText = CC::NifKeyString(prefix, input).value();
            const auto fallbackKey = CC::KeyOf(fallbackText);
            hand["keyString"] = fallbackText;
            hand["key"] = CC::Hex16(fallbackKey);
            fallbackRequest["forms"].insert(fallbackRequest["forms"].begin(), hand);
            K::Write(fallback.folder / "request.json", fallbackRequest.dump());
            a_check.Check(K::Run(fallback) == 0, "marble exception: request completes after unsupported radius animation");
            const auto fallbackProgress = K::Progress(fallback);
            const auto* fallbackLine = K::Find(fallbackProgress, fallbackKey);
            bool retained = fallbackLine && fallbackLine->outcome == CC::Outcome::kFallback &&
                fallbackLine->reason.find("ramp-only copy retained") != std::string::npos &&
                fallbackLine->reason.find("unsupported animated radius") != std::string::npos;
            if (retained) {
                nifly::NifFile rampOnly;
                LoadNif(rampOnly, StagedFile(fallback.staging, fallbackLine->path));
                std::size_t systems = 0;
                auto& rh = rampOnly.GetHeader();
                for (std::uint32_t id = 0; id < rh.GetNumBlocks(); ++id)
                    if (rh.GetBlock<nifly::NiParticleSystem>(id)) ++systems;
                const auto* ramp = rh.GetBlock<nifly::BSEffectShaderProperty>(a_ids.shader);
                retained = systems == 1 && ValidRefs(rampOnly) && ramp &&
                    ramp->greyscaleTexture.get() != K::kSourcePalette;
            }
            a_check.Check(retained, "marble exception: publishable ramp-only NIF survives, with failure reason recorded");
        }
    }

    void CheckMarble(Checker& a_check, const std::filesystem::path& a_dir)
    {
        TextureSource textures({}, {});
        RgbaImage palette{ 4, 4, std::vector<std::uint8_t>(64), false };
        for (std::size_t i = 0; i < palette.pixels.size(); i += 4) {
            palette.pixels[i] = 255;
            palette.pixels[i + 1] = 100;
            palette.pixels[i + 3] = 255;
        }
        textures.Put(kFirePalette, palette);
        nifly::NifFile source;
        const auto f = Make(source);
        const auto sourcePath = a_dir / "marble-source.nif";
        SaveNif(source, sourcePath);
        const auto before = Bytes(sourcePath);
        nifly::NifFile copy(source);
        const auto sourceBlocks = source.GetHeader().GetNumBlocks();
        const auto totals = AddHandMarble(copy, source, textures, 0.9, 1.15);
        a_check.Check(totals.systems == 1 && totals.sourceParticles == 24 && totals.addedParticles == 24,
            "marble budget: one independent pool per flame, exactly +24 particles, source + layer bounded at 48");
        SaveNif(source, sourcePath);
        a_check.Check(before == Bytes(sourcePath), "marble isolation: original source graph stays byte-identical");
        const auto outputPath = a_dir / "marble-built.nif";
        SaveNif(copy, outputPath);
        nifly::NifFile saved;
        LoadNif(saved, outputPath);
        auto& hdr = saved.GetHeader();
        a_check.Check(ValidRefs(saved) && CountUnknownBlocks(saved) == 0,
            "marble graph: saved NIF re-reads, every reference and pointer resolves, no unknown blocks");
        nifly::NiParticleSystem* marble = nullptr;
        std::uint32_t marbleId = nifly::NIF_NPOS;
        for (std::uint32_t id = sourceBlocks; id < hdr.GetNumBlocks(); ++id) {
            if (auto* ps = hdr.GetBlock<nifly::NiParticleSystem>(id)) { marble = ps; marbleId = id; }
        }
        a_check.Check(marble != nullptr, "marble graph: extra NiParticleSystem survives serialization");
        if (!marble) return;
        const auto* alpha = hdr.GetBlock<nifly::NiAlphaProperty>(marble->alphaPropertyRef);
        const auto* shader = hdr.GetBlock<nifly::BSEffectShaderProperty>(marble->shaderPropertyRef);
        a_check.Check(alpha && (alpha->flags & 1) && ((alpha->flags >> 1) & 15) == 6 &&
            ((alpha->flags >> 5) & 15) == 7 && !(alpha->flags & 512) && !(alpha->flags & (1u << 13)) &&
            ((alpha->flags >> 10) & 7) == 4,
            "marble blend: SrcAlpha/InvSrcAlpha, alpha test off, unused no-sorter flag off");
        const auto* bright = hdr.GetBlock<nifly::NiParticleSystem>(f.system);
        const auto* brightAlpha = hdr.GetBlock<nifly::NiAlphaProperty>(bright->alphaPropertyRef);
        a_check.Check(brightAlpha->flags == source.GetHeader().GetBlock<nifly::NiAlphaProperty>(f.alpha)->flags &&
            bright->alphaPropertyRef.index == f.alpha,
            "marble source alpha: original property reference and every flag remain unchanged");
        a_check.Check(shader && (shader->shaderFlags1 & (1u << 31)) && !(shader->shaderFlags2 & 1),
            "marble depth: z-test on and z-write off");
        const auto* sourceShader = source.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(f.shader);
        a_check.Check(shader && shader->sourceTexture.get() == sourceShader->sourceTexture.get() &&
            Near(shader->baseColor.r, 0.006f) && Near(shader->baseColor.g, 0.006f) && Near(shader->baseColor.b, 0.006f),
            "marble shading: original atlas retained and linear dark colour 0.006");
        const auto* originalPs = source.GetHeader().GetBlock<nifly::NiParticleSystem>(f.system);
        const auto* originalData = source.GetHeader().GetBlock<nifly::NiPSysData>(originalPs->psysDataRef);
        const auto* data = hdr.GetBlock<nifly::NiPSysData>(marble->psysDataRef);
        a_check.Check(data && data->GetNumVertices() == originalData->GetNumVertices() &&
            data->subtexOffsets.size() == originalData->subtexOffsets.size() &&
            data->subtexOffsets.data()[0].x != originalData->subtexOffsets.data()[0].x,
            "marble atlas: independent data keeps pool capacity and offsets atlas cells");
        bool emitters = false, curve = false, rotated = false, targets = true;
        for (const auto& ref : marble->modifierRefs) {
            const auto* mod = hdr.GetBlock<nifly::NiPSysModifier>(ref);
            targets &= mod && mod->targetRef.index == marbleId;
            if (const auto* emitter = dynamic_cast<const nifly::NiPSysSphereEmitter*>(mod)) {
                const auto* original = source.GetHeader().GetBlock<nifly::NiPSysSphereEmitter>(f.emitter);
                emitters = ref.index != f.emitter && emitter->emitterNodeRef.index == original->emitterNodeRef.index &&
                    Near(emitter->radius, original->radius) && Near(emitter->NiPSysEmitter::radius, 2.3f) &&
                    Near(emitter->radiusVariation, 0.46f) && emitter->lifeSpan == original->lifeSpan &&
                    emitter->lifeSpanVariation == original->lifeSpanVariation;
            }
            if (const auto* color = dynamic_cast<const nifly::BSPSysSimpleColorModifier*>(mod)) {
                curve = Near(color->color1.a, 0) && Near(color->color3.a / color->color2.a, 0.25f) &&
                    Near(color->fadeInPercent, 0.1f) && Near(color->fadeOutPercent, 0.8f) &&
                    Near(color->color2StartPercent, 0.2f) && Near(color->color2EndPercent, 0.6f) &&
                    shader && Near(shader->baseColor.a * color->color2.a, 0.9f);
            }
            if (const auto* rotation = dynamic_cast<const nifly::NiPSysRotationModifier*>(mod))
                rotated = !Near(rotation->initialAngle, 0.2f);
        }
        a_check.Check(emitters && targets, "marble emitter: independently cloned, same volume/lifetime, particle radii x1.15, all targets remapped");
        a_check.Check(curve, "marble opacity: original life curve shape and timing preserved, peak 0.9");
        a_check.Check(rotated, "marble rotation: independent rotation differs from additive flame");
        const auto* emitterController = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(marble->controllerRef);
        const auto* radiusController = emitterController ?
            hdr.GetBlock<nifly::NiPSysEmitterInitialRadiusCtlr>(emitterController->nextControllerRef) : nullptr;
        const auto* radiusBlend = radiusController ?
            hdr.GetBlock<nifly::NiBlendFloatInterpolator>(radiusController->interpolatorRef) : nullptr;
        bool radiusRemapped = radiusBlend && radiusBlend->interpItems.size() == 1 && Near(radiusBlend->value, 2.3f);
        if (radiusRemapped) {
            const auto& valueRef = radiusBlend->interpItems[0].interpolatorRef;
            const auto* value = hdr.GetBlock<nifly::NiFloatInterpolator>(valueRef);
            radiusRemapped = value && valueRef.index != f.radiusValue && Near(value->floatValue, 2.3f) &&
                Near(hdr.GetBlock<nifly::NiFloatInterpolator>(f.radiusValue)->floatValue, 2);
        }
        a_check.Check(radiusRemapped, "marble radius animation: standalone blend child cloned and scaled x1.15 without changing original");
        auto* sequence = hdr.GetBlock<nifly::NiControllerSequence>(f.sequence);
        bool linked = sequence && sequence->controlledBlocks.size() == 2;
        if (linked) {
            auto& link = sequence->controlledBlocks[1];
            const auto* controller = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(link.controllerRef);
            const auto* rate = hdr.GetBlock<nifly::NiFloatInterpolator>(link.interpolatorRef);
            const auto* rateData = rate ? hdr.GetBlock<nifly::NiFloatData>(rate->dataRef) : nullptr;
            linked = controller && controller->targetRef.index == marbleId && link.nodeName.get() == marble->name.get() &&
                controller->modifierName.get() == "Independent emitter" && rate && Near(rate->floatValue, 17) &&
                link.interpolatorRef.index != f.rate && rateData && rate->dataRef.index != f.rateData &&
                rateData->data.GetNumKeys() == 1 && Near(rateData->data.GetKey(0).value, 17);
        }
        a_check.Check(linked, "marble animation: sequence-only rate interpolator/data cloned, target/name remapped, emission rate kept");
        const auto& children = saved.GetRootNode()->childRefs;
        const auto* ordered = hdr.GetBlock<nifly::BSOrderedNode>(children.GetBlockRef(1));
        a_check.Check(children.GetSize() == 2 && ordered && ordered->childRefs.GetSize() == 2 &&
            ordered->childRefs.GetBlockRef(0) == marbleId && ordered->childRefs.GetBlockRef(1) == f.system,
            "marble order: source parent slot contains BSOrderedNode children [marble, flame]");
        std::ostringstream dump;
        auto* previous = std::cout.rdbuf(dump.rdbuf());
        const auto dumpResult = DumpNif(outputPath);
        std::cout.rdbuf(previous);
        a_check.Check(dumpResult == 0 && dump.str().find(marble->name.get()) != std::string::npos &&
            dump.str().find("NiPSysSphereEmitter") != std::string::npos && dump.str().find("particle pool 24") != std::string::npos,
            "marble dump: re-read output lists the new system, emitter and bounded pool");
        std::ofstream(a_dir / "marble-built.dump.txt") << dump.str();
        auto lowFire = palette;
        for (std::size_t i = 12; i < lowFire.pixels.size(); i += 4) {
            lowFire.pixels[i] = lowFire.pixels[i + 1] = 0;
            lowFire.pixels[i + 2] = 255; // Lit non-fire pixels belong in the denominator.
        }
        textures.Put("textures\\selftest\\low-fire.dds", lowFire); // Three of sixteen pixels: 18.75% fire.
        for (int variant = 0; variant < 6; ++variant) {
            nifly::NifFile rejected(source);
            auto& rejectHdr = rejected.GetHeader();
            if (variant == 0) rejectHdr.GetBlock<nifly::NiAlphaProperty>(f.alpha)->flags = 237;
            if (variant == 1) rejectHdr.GetBlock<nifly::BSEffectShaderProperty>(f.shader)->shaderFlags1 &= ~kSlsf1GreyscaleToPaletteColor;
            if (variant == 2) rejectHdr.GetBlock<nifly::BSEffectShaderProperty>(f.shader)->greyscaleTexture.get() = "textures\\missing.dds";
            if (variant == 3) rejectHdr.GetBlock<nifly::BSEffectShaderProperty>(f.shader)->greyscaleTexture.get() = "textures\\selftest\\low-fire.dds";
            if (variant == 4) rejectHdr.GetBlock<nifly::NiPSysData>(originalPs->psysDataRef)->subtexOffsets.resize(1);
            nifly::NifFile untouched(rejected);
            a_check.Check(AddHandMarble(untouched, rejected, textures, variant == 5 ? 0 : 0.9, 1.15).systems == 0 &&
                untouched.GetHeader().GetNumBlocks() == rejected.GetHeader().GetNumBlocks(),
                "marble detection: normal/non-palette/missing/<20% fire/single-cell/zero-opacity excluded (case " + std::to_string(variant) + ")");
        }
        nifly::NifFile namedSource(source);
        namedSource.GetHeader().GetBlock<nifly::NiControllerSequence>(f.sequence)->controlledBlocks[0].controllerRef.Clear();
        nifly::NifFile namedCopy(namedSource);
        const auto namedTotals = AddHandMarble(namedCopy, namedSource, textures, 0.9, 1.15);
        const auto namedPath = a_dir / "marble-name-link.nif";
        SaveNif(namedCopy, namedPath);
        nifly::NifFile namedSaved;
        LoadNif(namedSaved, namedPath);
        auto& namedHdr = namedSaved.GetHeader();
        auto* namedSequence = namedHdr.GetBlock<nifly::NiControllerSequence>(f.sequence);
        bool namedBound = namedTotals.systems == 1 && namedSequence && namedSequence->controlledBlocks.size() == 2;
        if (namedBound) {
            auto& link = namedSequence->controlledBlocks[1];
            const auto* controller = namedHdr.GetBlock<nifly::NiPSysEmitterCtlr>(link.controllerRef);
            const auto* target = controller ? namedHdr.GetBlock<nifly::NiParticleSystem>(controller->targetRef.index) : nullptr;
            namedBound = target && controller->targetRef.index != f.system && target->name.get() == link.nodeName.get();
        }
        a_check.Check(namedBound && ValidRefs(namedSaved),
            "marble animation: name-only controller binding resolves to the cloned target after saving and re-reading");
        CheckRoleRequest(a_check, a_dir, source, f);
        CheckBindings(a_check, source, f, textures);
        CheckMarbleOrder(a_check, a_dir);
    }
}
