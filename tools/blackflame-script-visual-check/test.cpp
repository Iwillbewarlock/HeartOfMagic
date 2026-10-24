#include "Seam.cpp"
int main(){
 using namespace BlackFlame;using namespace Wrapper;
 int checks=0,failures=0;
 auto check=[&](bool ok,const char* name){++checks;failures+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';};
 RE::BSScript::Internal::VirtualMachine vm;RE::BSScript::Internal::VirtualMachine::singleton=&vm;
 RE::ActiveEffect effect;vm.policy.effects[42]=&effect;
 RE::TESObjectREFR parent(1),child(2),grandchild(3);RE::Actor actor(4);
 FakeNative native;Proxy proxy{&native};RE::BSScript::Variable result{&child};
 auto reset=[&](){ResetScriptTags();tasks.clear();testMainThread=true;testGeneration=1;effect.visual=true;native.latent=false;};
 auto after=[&](RE::VMHandle caller=0){Decision d;d.callerRef=parent.GetHandle();d.caller=caller;proxy.After(result,d);};
 for(bool main:{true,false}){
  reset();testMainThread=main;TagSource(&parent);after();
  check(TagInfoOf(&child).state==TagState::kNone,main?"confirmed source-only parent cannot tag placed child on game thread":"confirmed source-only parent cannot tag placed child on VM thread");
  reset();testMainThread=main;TagPending(&parent,false,42);after();
  check(TagInfoOf(&child).state==TagState::kNone&&tasks.empty(),main?"pending source-only parent cannot create pending placed child on game thread":"pending source-only parent cannot create pending placed child on VM thread");
  reset();testMainThread=main;TagPlaced(&parent);after();
  check(IsTaggedVisualInstance(&child),main?"confirmed placed visual parent tags child on game thread":"confirmed placed visual parent tags child on VM thread");
  reset();testMainThread=main;TagPending(&parent,true,42);after();
  auto info=TagInfoOf(&child);
  check(info.state==TagState::kPending&&info.pendingCaller==42&&info.visualInstance&&tasks.size()==1,main?"pending placed visual parent preserves child provenance on game thread":"pending placed visual parent preserves child provenance on VM thread");
  for(auto& job:tasks)job();check(IsTaggedVisualInstance(&child),"queued pending judgement confirms genuine placed child");
 }
 reset();after(42);check(IsTaggedVisualInstance(&child),"direct eligible ActiveEffect caller still tags placed result");
 reset();effect.visual=false;after(42);check(TagInfoOf(&child).state==TagState::kNone,"nonvisual ActiveEffect caller cannot tag placed result");
 reset();testMainThread=false;after(42);check(TagInfoOf(&child).state==TagState::kPending&&TagInfoOf(&child).visualInstance,"direct off-thread ActiveEffect result remains pending");
 effect.visual=false;for(auto& job:tasks)job();check(TagInfoOf(&child).state==TagState::kNone,"rejected pending effect removes child tag");
 reset();TagPlaced(&parent);Decision cast;cast.callerRef=parent.GetHandle();cast.subject=child.GetHandle();cast.kind=Wrapped::kCast;Decide(cast);
 check(IsTaggedSource(&child)&&!IsTaggedVisualInstance(&child),"Cast source from visual parent gains attribution only");
 Decision nested;nested.callerRef=child.GetHandle();RE::BSScript::Variable nestedResult{&grandchild};proxy.After(nestedResult,nested);
 check(TagInfoOf(&grandchild).state==TagState::kNone,"source-only Cast attribution cannot promote grandchildren to visual instances");
 reset();TagPlaced(&parent);Tags().at(parent.id).placed=false;TagSource(&parent);
 check(IsTaggedVisualInstance(&parent),"placed provenance survives tint-complete flag and source retag");
 reset();TagPlaced(&parent);++testGeneration;TagSource(&parent);
 check(IsTaggedSource(&parent)&&!IsTaggedVisualInstance(&parent),"old-world placed provenance cannot survive source retag");
 reset();TagPlaced(&parent);Tags().at(parent.id).expires=Clock::now()-std::chrono::seconds(1);TagSource(&parent);
 check(IsTaggedSource(&parent)&&!IsTaggedVisualInstance(&parent),"expired placed provenance cannot survive source retag");
 reset();TagPlaced(&actor);check(TagInfoOf(&actor).state==TagState::kNone,"actors are never tagged as visual instances");
 reset();TagPlaced(&parent);native.latent=true;after();check(TagInfoOf(&child).state==TagState::kNone,"latent native result is not tagged synchronously");
 std::cout<<"RESULT checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
