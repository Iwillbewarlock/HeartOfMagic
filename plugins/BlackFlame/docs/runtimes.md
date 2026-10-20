## Runtimes and Address Library ids

One DLL for **SE 1.5.97 and AE 1.6.x / 1.7.x**: the build sets CommonLibSSE-NG's `ENABLE_SKYRIM_SE` and
`ENABLE_SKYRIM_AE` on (plugins/cmake/commonlibsse.cmake), so every engine address comes from the running game's
Address Library and every member whose offset differs between SE and AE goes through CommonLib's runtime accessors (a
mixed build does not compile direct access to such a member). **VR is not supported** (`ENABLE_SKYRIM_VR` is off: no
VR build, no VR ids). Every id below is also checked in the running game's database before use (`Patch::HasIds`): a
missing id is a log line and that device stays off, never a failed lookup.

Table: every Address Library id Black Flame uses - its own and the CommonLib functions it reaches - looked up in every
database CommonLib ships in `tests/REL` (made with [tools/idaudit/table.js](../tools/idaudit/table.js), which reads formats
1, 2 and 5; 1.7.99 is format 5). The SE column looks up the SE id, every AE column the AE id. **"yes" means the id exists
in that database, not that the code there is what we expect**: the hook sites are checked at runtime (bytes, call
targets, vtables) on every runtime. No 1.6.640 or 1.7.104 database is in `tests/REL`, so those two are not in the
table. The SE ids of the C4 items (SetCastingArt, its caller, hazard creation, the spawn-hazard effect) are the SE
counterparts the research notes name; no SE executable was read, so their SE function identity is unverified.

| What | SE id | AE id | 1.5.97 | 1.6.353 | 1.6.629 | 1.6.1130 | 1.6.1170 | 1.6.1179 | 1.7.99 |
|---|---|---|---|---|---|---|---|---|---|
| Projectile::Launch (copy swap detour) | 42928 | 44108 | yes | yes | yes | yes | yes | yes | yes |
| explosion creation (copy swap detour; SE id an unverified match, not used on SE) | 42657 | 43829 | yes | yes | yes | yes | yes | yes | yes |
| hazard creation (C4 detour; SE id an unverified match, not used on SE) | 42786 | 43954 | yes | yes | yes | yes | yes | yes | yes |
| BGSImpactDataSet::GetImpactData (callee) | 20408 | 20860 | yes | yes | yes | yes | yes | yes | yes |
| Projectile::SpawnCollisionEffects (GetImpactData site, impact window) | 43022 | 44213 | yes | yes | yes | yes | yes | yes | yes |
| explosion impact function (GetImpactData site) | 42679 | 43851 | yes | yes | yes | yes | yes | yes | yes |
| projectile decal helper (rune decal GetImpactData site; no SE id, not used on SE) | - | 44186 | - | yes | yes | yes | yes | yes | yes |
| MagicCaster launch helper (rune decal window; 1.6.1170 only; no SE id) | - | 34452 | - | yes | yes | yes | yes | yes | yes |
| Projectile::ProcessImpacts (impact window site) | 42943 | 44123 | yes | yes | yes | yes | yes | yes | yes |
| Projectile::HandleHits caller (impact window site) | 43014 | 44205 | yes | yes | yes | yes | yes | yes | yes |
| ConeProjectile::UpdateImpl (impact window site) | 42638 | 43806 | yes | yes | yes | yes | yes | yes | yes |
| Main::Update (main-thread queue call site) | 35565 | 36564 | yes | yes | yes | yes | yes | yes | yes |
| ActorMagicCaster::SetCastingArt (C4 callee; SE id unverified, not used on SE) | 33369 | 34150 | yes | yes | yes | yes | yes | yes | yes |
| SetCastingArt caller (C4 site; SE id unverified, not used on SE) | 37830 | 38784 | yes | yes | yes | yes | yes | yes | yes |
| ActorMagicCaster::DetachCastingArt (hand art refresh; no SE id, not used on SE) | - | 34152 | - | yes | yes | yes | yes | yes | yes |
| SpawnHazardEffect start, the spawn window (fix F2; no SE id) | - | 36885 | - | yes | yes | yes | yes | yes | yes |
| VTABLE_ActorMagicCaster[0] | 257613 | 205828 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_Explosion[0] | 263870 | 209940 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_Hazard[0] | 263922 | 209971 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSHazard[2] (TESModel) | 233489 | 189083 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSImpactData[1] (TESModel) | 236357 | 190905 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_ActiveEffectReferenceEffectController[0] | 257577 | 205809 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSExplosion[0] (explosion creation base check) | 233467 | 189059 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSArtObject[0] (casting art check) | 233406 | 189017 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSHazard[0] (hazard creation base check) | 233487 | 189079 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_TESObjectCELL[0] (hazard creation cell check) | 235197 | 190143 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_SpawnHazardEffect[0] (spawn-hazard window check) | 259802 | 207209 | yes | yes | yes | yes | yes | yes | yes |
| PlayerCharacter::GetSingleton (singleton ptr) (CommonLib) | 517014 | 403521 | yes | yes | yes | yes | yes | yes | yes |
| ProcessLists::GetSingleton (CommonLib) | 514167 | 400315 | yes | yes | yes | yes | yes | yes | yes |
| TESDataHandler::GetSingleton (CommonLib) | 514141 | 400269 | yes | yes | yes | yes | yes | yes | yes |
| ScriptEventSourceHolder::GetSingleton (fn) (CommonLib) | 14108 | 14298 | yes | yes | yes | yes | yes | yes | yes |
| UI::GetSingleton (CommonLib) | 514178 | 400327 | yes | yes | yes | yes | yes | yes | yes |
| TESForm::GetAllForms map (LookupForm->LookupByID) (CommonLib) | 514351 | 400507 | yes | yes | yes | yes | yes | yes | yes |
| TESForm::GetAllForms lock (CommonLib) | 514360 | 400517 | yes | yes | yes | yes | yes | yes | yes |
| BSReadWriteLock::LockForRead (CommonLib) | 66976 | 68233 | yes | yes | yes | yes | yes | yes | yes |
| BSReadWriteLock::UnlockForRead (CommonLib) | 66982 | 68239 | yes | yes | yes | yes | yes | yes | yes |
| MemoryManager::GetSingleton (RE::free, BSTArray growth) (CommonLib) | 11045 | 11141 | yes | yes | yes | yes | yes | yes | yes |
| MemoryManager::Allocate (CommonLib) | 66859 | 68115 | yes | yes | yes | yes | yes | yes | yes |
| MemoryManager::Deallocate (CommonLib) | 66861 | 68117 | yes | yes | yes | yes | yes | yes | yes |
| BSFixedString::ctor8 (CommonLib) | 67819 | 69161 | yes | yes | yes | yes | yes | yes | yes |
| BSStringPool::Entry::release8 (~BSFixedString) (CommonLib) | 67847 | 69192 | yes | yes | yes | yes | yes | yes | yes |
| BSPointerHandleManagerInterface::GetHandle (TESObjectREFR::GetHandle) (CommonLib) | 15967 | 16212 | yes | yes | yes | yes | yes | yes | yes |
| GetSmartPointer non-const (handle.get()) (CommonLib) | 12785 | 12922 | yes | yes | yes | yes | yes | yes | yes |
| GetSmartPointer const (const handle.get()) (CommonLib) | 12204 | 12332 | yes | yes | yes | yes | yes | yes | yes |
| TESObjectREFR::ApplyEffectShader (CommonLib) | 19446 | 19872 | yes | yes | yes | yes | yes | yes | yes |
| TESObjectREFR::SetPosition(NiPoint3) (CommonLib) | 19363 | 19790 | yes | yes | yes | yes | yes | yes | yes |
| TESDataHandler::CreateReferenceAtLocation (PlaceObjectAtMe) (CommonLib) | 13625 | 13723 | yes | yes | yes | yes | yes | yes | yes |
| Actor::SetHeading (CommonLib) | 36248 | 37230 | yes | yes | yes | yes | yes | yes | yes |
| ActiveEffect::Dispel (CommonLib) | 33286 | 34061 | yes | yes | yes | yes | yes | yes | yes |
| MagicItem::GetAVEffect (CommonLib) | 11194 | 11302 | yes | yes | yes | yes | yes | yes | yes |
| MagicItem::GetCostliestEffectItem (CommonLib) | 11216 | 11335 | yes | yes | yes | yes | yes | yes | yes |
| BSShaderProperty::SetMaterial (CommonLib) | 98897 | 105544 | yes | yes | yes | yes | yes | yes | yes |
| BSShaderProperty::SetFlags (CommonLib) | 98893 | 105540 | yes | yes | yes | yes | yes | yes | yes |
| BSShaderManager::GetTexture (CommonLib) | 98986 | 105640 | yes | yes | yes | yes | yes | yes | yes |
| Projectile::Launch (CommonLib) | 42928 | 44108 | yes | yes | yes | yes | yes | yes | yes |
| RTDynamicCast (skyrim_cast) (CommonLib) | 102238 | 109689 | yes | yes | yes | yes | yes | yes | yes |
| RTTI_TESObjectREFR (CommonLib) | 513899 | 392437 | yes | yes | yes | yes | yes | yes | yes |
| RTTI_Projectile (CommonLib) | 514046 | 394480 | yes | yes | yes | yes | yes | yes | yes |
| RTTI_MagicCaster (CommonLib) | 513999 | 393096 | yes | yes | yes | yes | yes | yes | yes |
| RTTI_ActorMagicCaster (CommonLib) | 686479 | 394312 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_BSEffectShaderProperty (CommonLib) | 527834 | 414781 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_BSEffectShaderPropertyColorController (CommonLib) | 528413 | 415359 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_BSPSysSimpleColorModifier (CommonLib) | 528415 | 415361 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_NiGeometry (CommonLib) | 523978 | 410558 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_NiParticleSystem (CommonLib) | 524465 | 411046 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_NiSourceTexture (CommonLib) | 523939 | 410519 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_MissileProjectile[0] (CommonLib) | 263942 | 209980 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_FlameProjectile[0] (CommonLib) | 263884 | 209953 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_ConeProjectile[0] (CommonLib) | 263822 | 209925 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BeamProjectile[0] (CommonLib) | 263808 | 209914 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_GrenadeProjectile[0] (CommonLib) | 263895 | 209962 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BarrierProjectile[0] (CommonLib) | 263790 | 209901 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_Explosion[0] (CommonLib) | 263870 | 209940 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_Projectile__LaunchData[0] (LaunchData ctor emplace_vtable) (CommonLib) | 230860 | 187478 | yes | yes | yes | yes | yes | yes | yes |
| MagicTarget::DispelEffect (CommonLib) | 33721 | 34505 | yes | yes | yes | yes | yes | yes | yes |
| BSResourceNiBinaryStream ctor (CommonLib) | 69636 | 71014 | yes | yes | yes | yes | yes | yes | yes |
| BSResourceNiBinaryStream dtor (CommonLib) | 69638 | 71016 | yes | yes | yes | yes | yes | yes | yes |


**Hook sites on every runtime** (2026-10-16): no in-function offset measured only on 1.6.1170 is trusted elsewhere.

| Site | How it is found and checked |
|---|---|
| main-thread queue | `Main::Update` (35565 / 36564) + 0x748 on SE, + 0xC26 on AE before 1.7.99, + 0xC38 from 1.7.99 (the offsets OAR, Precision and TDM use for every runtime); the site must be an E8 into the game image or a trampoline stub. The AE tail anchor (pdata end - site = 0x37) is enforced only on 1.6.1170 and 1.7.104, where it was measured; other AE builds log their distance. SE's SKSE task drain sits in the same function (+0x6B8), so the queue's reasoning holds there too |
| Launch, explosion creation, hazard creation, SpawnHazardEffect start | prologue detours on every runtime whose first bytes the replay decoder takes (else left alone, logged, and the self-test says INFO). Explosion creation, hazard creation and the spawn window run on AE only (fix F1: their SE ids are unverified matches); their thunks forward all four argument registers and return rax (verified on 1.7.104: the functions take one or two pointer arguments and no float or stack argument); every pointer is checked by vtable or form type; a fault trips the SEH guard once and stops that swap decision for the session (the detour stays and calls the original). The SetCastingArt site is AE only too |
| GetImpactData sites, impact window sites | scan of the caller's function-table range for its one E8 to the callee; a fixed offset only for a call another plugin already rewrote (an E8 into a trampoline stub), and only where the offset was confirmed: SE 1.5.97 (MuIF, KillFeed) and AE 1.6.1170 / 1.7.104. The rune decal lookup (44186) is scan only, AE only |
| rune decal window | AE 34452 + 0x52F on **1.6.1170 only** (the only build whose 34452 was read): the 28 bytes before must be the null-IPCT argument setup and the call an E8 to 44186; other builds, SE and any mismatch: not installed, logged |
| vtable slots | CommonLib's vtable ids, checked in the database |

Our jumps never use a hand-written rel32 into the DLL: call redirects and the prologue jump go through the SKSE trampoline
(within 2 GB of the game), and the gateway returns with an absolute `FF 25` jump.

**Tester diagnostics.** `HeartOfMagic_BlackFlame_tester.txt` next to the log, written at kDataLoaded and again when the
self-test ends: the runtime, every hook INSTALLED or SKIPPED with its address (image offset), the bytes it found and why,
when and why it was written (kDataLoaded, every save load and save, the self-test's end; fix G5), the copies map and fill counts with any copy problem, the copy request, a counters line (Launch, explosion creation,
hazard creation and the spawn window, the SetCastingArt site, PlaceAtMe copies, the native wrappers bound or not; also
an INFO line of T9, fix F6), and the self-test's DONE lines. `testerMode` (config, default false) turns on `selfTest`
and `writeRequest` and writes every log line through at once (a crash keeps it), during and after the self-test (fix F9);
the self-test starts after `selfTestDelaySeconds` (30) of unpaused play.

**Tester package.** `plugins\BlackFlame\tools\Make-TesterPackage.ps1` (after `BuildRelease.ps1`) writes
`dist\HeartOfMagic-BlackFlame-Tester_<VERSION>-<commit>.zip`: the DLL and PDB, the ESP and the two empty ESLs, the shipped
vanilla-derived grey meshes and palette, the grey map, the config with `testerMode` true, the builder with
`Build Black Flame copies.bat` (no argument: `Documents\HeartOfMagic BlackFlame Copies`) under `tools\BlackFlame`, and
[tools/README_TESTER.md](../tools/README_TESTER.md) (MO2 and Vortex steps, the files to send back, a throwaway save). The
script refuses to package generated copies (a `copies` mesh folder, a copies map, a non-empty copies or local ESL).
The package's `THIRD-PARTY-NOTICES.md` is its own (`tools/THIRD-PARTY-NOTICES-TESTER.md.in`, not the Librarian
release's): the DLL (CommonLibSSE-NG) and the builder (nifly) as GPL combined works, each one's libraries with the
version vcpkg installed, and the exact commit as the Corresponding Source. The licence texts are copied from the
CommonLib submodule and vcpkg's installed copyright files when packaging (and checked byte for byte). The commit counts as
published only when a branch or tag origin has now reaches it (`git fetch origin`, then `git ls-remote` and
`merge-base --is-ancestor`; local tags and other remotes do not count, offline counts as unpublished). An unpublished
commit is refused unless `-AllowUnpublished`, which puts `source\HeartOfMagic-<version>-source.zip` in the package:
`git archive` of the commit (with `-AllowDirty`, which needs it, the tracked files' uncommitted changes too) and of the
CommonLibSSE-NG submodule's commit at `plugins/external/commonlibsse-ng/`, in one zip; the notice then points into the
zip. The zip is written with .NET's ZipArchive, which marks entries as made on Windows: the submodule's few symbolic
links (documentation files) come out as small files holding their target (Y20). An untracked file under the DLL's or
the builder's sources is refused always. The DLL's library table has no xbyak
and no zlib: neither one's strings are in `HeartOfMagic_BlackFlame.dll` (both are in `SpellLearning.dll`, whose
release notice lists them).

