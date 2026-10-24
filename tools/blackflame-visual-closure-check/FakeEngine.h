#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
namespace logger { template<class...T> void info(T&&...){} }
namespace RE {
using FormID=std::uint32_t;
enum class FormType { None, Projectile,Explosion,Impact,Hazard,ArtObject,TextureSet,AddonNode,MagicEffect,Spell,Other };
struct TESFile { std::string name="Skyrim.esm"; bool light=false; const char* GetFilename() const{return name.c_str();} bool IsLight() const{return light;} };
inline TESFile vanilla;
struct TESForm { FormID id=0; FormType type=FormType::Other; TESFile* file=&vanilla; virtual ~TESForm()=default; FormID GetFormID() const{return id;} FormType GetFormType() const{return type;} TESFile* GetFile(int) const{return file;} template<class T>T* As(){return dynamic_cast<T*>(this);} template<class T>const T* As()const{return dynamic_cast<const T*>(this);} };
struct TESModel { std::string model; };
struct BGSProjectile;struct BGSExplosion;struct BGSImpactData;struct BGSHazard;struct BGSArtObject;struct BGSImpactDataSet;struct BGSDualCastData;struct BGSReferenceEffect;struct SpellItem;struct MagicItem;
struct TESObjectREFR:TESForm{};
struct BGSArtObject:TESForm,TESModel { int numAlternateTextures=0;BGSArtObject(){type=FormType::ArtObject;} };
struct BGSTextureSet:TESForm{BGSTextureSet(){type=FormType::TextureSet;}};
struct BGSAddonNode:TESForm,TESModel{};
struct EffectSetting:TESForm {
 enum class Archetype{kNone,kSpawnHazard};
 struct Data { BGSProjectile* projectileBase=nullptr;BGSExplosion* explosion=nullptr;BGSImpactDataSet* impactDataSet=nullptr;BGSArtObject* castingArt=nullptr;BGSDualCastData* dualCastData=nullptr;TESForm* associatedForm=nullptr;BGSArtObject* hitEffectArt=nullptr;SpellItem* equipAbility=nullptr;BGSReferenceEffect* hitVisuals=nullptr; }data;
 Archetype archetype=Archetype::kNone;bool HasArchetype(Archetype a)const{return archetype==a;}
};
struct Effect {EffectSetting* baseEffect=nullptr;};
struct MagicItem:TESForm {std::vector<Effect*> effects;EffectSetting* costliest=nullptr; EffectSetting* GetAVEffect()const{return costliest;}};
struct SpellItem:MagicItem{};
struct BGSDualCastData:TESForm { struct Data{BGSProjectile* pProjectile=nullptr;BGSExplosion* pExplosion=nullptr;BGSImpactDataSet* pImpactDataSet=nullptr;BGSArtObject* pHitEffectArt=nullptr;}data;};
struct BGSReferenceEffect:TESForm {struct Data{BGSArtObject* artObject=nullptr;}data;};
struct BGSProjectile:TESForm,TESModel {struct Data{BGSExplosion* explosionType=nullptr;BGSTextureSet* decalData=nullptr;}data;BGSProjectile(){type=FormType::Projectile;}};
struct BGSExplosion:TESForm,TESModel {struct Data{BGSImpactDataSet* impactDataSet=nullptr;TESObjectREFR* impactPlacedObject=nullptr;BGSProjectile* spawnProjectile=nullptr;}data;BGSExplosion(){type=FormType::Explosion;}};
struct BGSImpactDataSet:TESForm {std::map<int,BGSImpactData*> impactMap;};
struct BGSImpactData:TESForm,TESModel {BGSHazard* hazard=nullptr;BGSTextureSet* decalTextureSet=nullptr;BGSTextureSet* decalTextureSet2=nullptr;BGSImpactData(){type=FormType::Impact;}};
struct BGSHazard:TESForm,TESModel {struct Data{BGSImpactDataSet* impactDataSet=nullptr;MagicItem* spell=nullptr;}data;BGSHazard(){type=FormType::Hazard;}};
struct TESObjectWEAP:TESForm {bool bow=false;bool IsBow()const{return bow;}bool IsCrossbow()const{return false;}};
struct TESAmmo:TESForm {struct Runtime {struct Data {BGSProjectile* projectile=nullptr;}data;}runtime;Runtime& GetRuntimeData(){return runtime;}};
}
namespace BlackFlame {
inline std::vector<RE::SpellItem*> testFireSpells,testVisualSpells;
inline std::unordered_set<RE::FormID> testFireEffects;
inline std::vector<RE::TESAmmo*> testAmmo;
struct BoundSeed{RE::EffectSetting* effect;RE::TESObjectWEAP* weapon;};inline std::vector<BoundSeed> testBoundSeeds;
inline const auto& FireSpellList(){return testFireSpells;}
inline const auto& VisualSpellList(){return testVisualSpells;}
inline const auto& BoundArrowAmmo(){return testAmmo;}
inline const auto& BoundFireSeeds(){return testBoundSeeds;}
inline bool IsFireEffect(const RE::EffectSetting* p){return p&&testFireEffects.contains(p->id);}
inline bool IsFireMgefAny(const RE::EffectSetting* p){return IsFireEffect(p);}
inline bool IsBurnable(const RE::EffectSetting* p,const RE::MagicItem*){return IsFireEffect(p);}
inline bool IsVisualEffectForm(const RE::EffectSetting* p){return p!=nullptr;}
}
namespace EncodingUtils {inline std::string SanitizeToUTF8(std::string s){return s;}}
