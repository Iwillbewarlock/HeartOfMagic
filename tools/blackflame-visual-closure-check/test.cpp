#include "FireClosure.cpp"
#include <iostream>
using namespace BlackFlame;
int failures=0,checks=0;
void check(bool yes,const char* name){++checks;std::cout<<(yes?"PASS ":"FAIL ")<<name<<'\n';if(!yes)++failures;}
bool has(const FireClosure& c,RE::FormID id){return std::ranges::any_of(c.forms,[&](const auto& f){return f.form->id==id;});}
void reset(){testFireSpells.clear();testVisualSpells.clear();testFireEffects.clear();testAmmo.clear();testBoundSeeds.clear();}
int main(){
 reset();
 RE::SpellItem fire,ability,unrelated;fire.id=1;ability.id=2;unrelated.id=3;
 RE::EffectSetting damage,fear,holder,other;damage.id=10;fear.id=11;holder.id=12;other.id=13;
 RE::Effect de{&damage},fe{&fear},he{&holder},oe{&other};fire.effects={&de,&fe};fire.costliest=&damage;ability.effects={&he};unrelated.effects={&oe};
 RE::BGSArtObject flame,red,foreign;flame.id=20;red.id=21;foreign.id=22;
 damage.data.hitEffectArt=&flame;fear.data.equipAbility=&ability;holder.data.hitEffectArt=&red;other.data.hitEffectArt=&foreign;
 testFireSpells={&fire};testVisualSpells={&fire};testFireEffects={damage.id};
 auto c=BuildFireClosure();
 check(has(c,20),"existing fire hit art");
 check(has(c,21),"supplemental nonfire effect equip ability art");
 check(!has(c,22),"unrelated nonfire root absent");
 check(fire.effects.size()==2&&fear.data.equipAbility==&ability&&holder.data.hitEffectArt==&red&&red.id==21,"original forms unchanged");
 check(testFireEffects.size()==1&&!IsFireEffect(&fear)&&testFireSpells.size()==1,"gameplay fire sets unchanged");
 // Cycle and associated spells: all child effects, not only a costliest one.
 holder.data.associatedForm=&fire;
 RE::SpellItem child;child.id=30;RE::EffectSetting childA,childB;childA.id=31;childB.id=32;
 RE::Effect ca{&childA},cb{&childB};child.effects={&ca,&cb};child.costliest=&childA;
 RE::BGSArtObject childArtA,childArtB;childArtA.id=33;childArtB.id=34;childA.data.hitEffectArt=&childArtA;childB.data.hitEffectArt=&childArtB;
 fear.data.associatedForm=&child;childB.data.associatedForm=&child;
 c=BuildFireClosure();check(has(c,33)&&has(c,34),"all associated spell effects included");
 check(c.forms.size()==4,"equip and associated cycles terminate without duplicate forms");
 // Exclusions remain even when a parent reaches them twice.
 red.numAlternateTextures=1;c=BuildFireClosure();check(!has(c,21)&&c.excluded.size()==1,"alternate texture exclusion remains");red.numAlternateTextures=0;
 RE::TESFile own;own.name="HeartOfMagic_BlackFlame_Test.esp";red.file=&own;c=BuildFireClosure();check(!has(c,21),"own plugin copy excluded");red.file=&RE::vanilla;
 // Hazard child edges reach nonfire supplemental art; cycles use the same bounded queue.
 RE::BGSHazard hazard;hazard.id=40;hazard.data.spell=&child;damage.archetype=RE::EffectSetting::Archetype::kSpawnHazard;damage.data.associatedForm=&hazard;
 c=BuildFireClosure();check(has(c,40)&&has(c,34),"hazard spells include supplemental effects");
 // The costliest seed still marks its casting art.
 damage.data.castingArt=&flame;c=BuildFireClosure();
 auto i=std::ranges::find_if(c.forms,[&](const auto& f){return f.form->id==20;});check(i!=c.forms.end()&&i->reachedByCostliest&&(i->edges&kEdgeCastingArtCostliest),"costliest provenance retained");
 // Existing bound-weapon seeds remain independent of visual roots.
 reset();RE::EffectSetting boundEffect;boundEffect.id=50;RE::TESObjectWEAP weapon;weapon.id=51;weapon.bow=true;
 RE::BGSProjectile ownArrow,scriptArrow;ownArrow.id=52;scriptArrow.id=53;boundEffect.data.projectileBase=&ownArrow;
 RE::TESAmmo ammo;ammo.id=54;ammo.runtime.data.projectile=&scriptArrow;testAmmo={&ammo};testBoundSeeds={{&boundEffect,&weapon}};
 c=BuildFireClosure();check(has(c,52)&&has(c,53)&&c.boundSeeds==1&&c.boundArrowAmmo==1,"bound arrow seeds preserved");
 check(std::ranges::all_of(c.forms,[](const auto& f){return (f.edges&kEdgeBoundArrow)!=0;}),"bound arrow provenance retained");
 std::cout<<"RESULT checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
