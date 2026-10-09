# Tag-based VFX swap: research

## 1. VFX Edit mod contents (D:/TAKEALOOK/mods)
All are pure mesh replacers of vanilla paths (no ESP, no new paths), confirmed by file listing:
- Frost (19 files): Meshes/Magic/{FrostHandFX01,FrostBoltHandFX01,IceProjectile01/02,IceSprayProjectile01,FXIceCloak01,FXIceCloakHandEffects,ExplosionFrost01,BlizzardHazard,MAGINVIceSpellArt,...}, Meshes/MPS/MPSFrost*.
  Nexus text: "just models replacer, no esl"; says "adds new meshes and particles" = inside the NIFs, not new paths.
- Flame (12): Fireball01HandEffects, FireBallExp01, FireboltProjectile, FireSprayFX01, FireStormHandEffects, FXFireCloak*...
- Lightning (18): LightningBolt01-04, ShockHandEffects, ImpactShock01-03, ExplosionShock01, FXShockCloak*...
- Healing (8), Drain (12: Absorb*HandFX01 red/green/blue, AbsorbBeam), Light (5), Summon portals (10), Dragon breath (9), Candle (1: MPS/MPSCandleFlame01.nif).
- KittyVFX-ENBHands: 15 hand NIFs, same vanilla names. Kittytail Mysticism patch: only an ESP, 14 overrides (Mysticism MGEFs + 4 projectiles).
- Caveat: I did not diff against the vanilla BSA, so "all paths exist in vanilla" is inferred from names (they match vanilla ArtObject model names, e.g. FrostHandFX01).
=> NO new non-replacing art assets are shipped. Replacing them all-at-once is exactly the NIF replacer limitation. Nothing to point other spells at.

## 2. Visual fields (CommonLibSSE-NG, E/EffectSetting.h EffectSettingData)
light (TESObjectLIGH*), effectShader (TESEffectShader*, = hit shader), enchantShader, projectileBase (BGSProjectile*), explosion (BGSExplosion*), castingArt (BGSArtObject*), hitEffectArt (BGSArtObject*), impactDataSet (BGSImpactDataSet*), enchantEffectArt (BGSArtObject*), hitVisuals (BGSReferenceEffect*), enchantVisuals, dualCastData (BGSDualCastData*), imageSpaceMod; sounds are the separate list `BSTArray<SoundPair> effectSounds`.
BGSArtObject : TESBoundObject, TESModelTextureSwap; Data.artType enum ArtType {kMagicCastingArt=0, kMagicHitEffect=1, kMagicEnchantEffect=2}. BGSProjectile : TESModel (so SetModel) with muzzleFlashLight, explosionType, muzzleFlashModel.
The housecarl MGEF dump (record fields CastingArt, CastingLight, HitShader, ImpactData, Explosion, Projectile, HitEffectArt, EnchantArt...) maps 1:1.

## 3. Feasibility
Technically trivial at kDataLoaded: iterate EffectSetting forms, write data.castingArt/hitEffectArt/effectShader/light/explosion pointers (plain pointer writes, no hooks). Spells to MGEF via spell->effects. Needs a rule table: tag -> {castingArt, hitEffectArt, shader, light, explosion, projectile}.
Reusable vanilla art (ArtObject EditorIDs found in Skyrim.esm): 
- fire: FireStormHandEffects (089F26), Fireball01HandEffects, FireCloakHandEffects
- frost: FrostHandFX01 (020BF5), FrostBoltHandFX01, FXIceCloakHandEffects
- shock: ShockHandEffects (058E9B), LightningStormHandEffects
- holy/light: FXLightSpellHandEffects, Heal01HandEffects, HealRitualHandEffects, DA09LightBeamCastingEffect
- life/heal: HealMystFXHand01 (04E222)
- blood/drain: AbsorbRedHandFX01 (0ABF02), AbsorbGreen/BlueHandFX01 (green = poison-ish, blue = arcane)
- poison: PoisonCloak01 (hit), AbsorbGreen
- shadow/necro: Reanimate/necro ritual art, FXNecroSkeleton* (weak)
- Summoning: SummonHandFX01, SummonTargetFX
Gaps (no fitting vanilla art): water, earth/stone, air/wind (the vanilla projectile/explosion for these do not exist; mods fake wind with frost), blood (only the red absorb), shadow. Light/holy is weak. Those need custom NIFs (or other mods' assets with permission).

## 4. Do packs reuse vanilla art for off-element spells? YES (verified)
Records readable via housecarl_records. MGEFs whose CastingArt = vanilla FrostHandFX01 but ResistValue != ResistFrost (first 60 matches, 2 plugins failed to load so incomplete): 
- KittySpellPack02.esl "Diamond Splash" (_K02_DiamondWall*, earth/crystal, 5 MGEFs)
- Glenmoril.esm (22 total with frost hand): WindCutter/WindPush, ChaosCutter, MoonlightCutter, SunlightCutter, VoidCutter, Kienzan, FireBlueCutter (ResistFire!), ShockCutter
- DAc0da.esm: FlameStrike (ResistFire), ChaosStrike; Vigilant.esm: Ebony/DS cutters; WargodBoss "Wind Strike"; Siege at Icemoth plague breath (PoisonResist); Dragonborn Bloodskal strikes; Lurker Spray (poison).
Total 89 MGEFs in the load order use frost hand art; 22 plugins. Confirms the problem is real (wind, earth, poison, fire, holy, void all wearing frost hands). Many are enchant-style/boss MGEFs, NPC variants (e.g. _NPC suffix).
Mysticism check: MAG_FireDamageFFAimedArea100 uses FireStormHandEffects, Explosion FireBallExp01, ImpactData MAGFirebolt01ImpactSet (so Mysticism is element-correct; also overridden by the Kittytail patch).

## 5. Existing frameworks
Nexus search (housecarl_nexus_search, not GraphQL): "art extender" returns only Enchantment Art Extender and ports (105492 etc.); "spell art" no relevant; "magic effect art" and "VFX framework" 0 hits. EAE: config JSON in SKSE/Plugins/EnchantmentArtExtender with WeaponArt + WeaponKeywords + EnchantmentKeywords, enchantments only (weapon art), not spells. I did not find a spell-art-by-keyword framework; not exhaustive (did not try GraphQL or SkyPatcher/Keyword Item Distributor angle). Note: SkyPatcher can already patch MGEF CastingArt/HitEffectArt by editorid via INI, so a static per-spell INI approach exists but is not tag driven. (I did not verify SkyPatcher's exact MGEF field support.)

## 6. Risks
- Shared MGEFs: one MGEF used by several spells (and by NPC spells, scrolls, staffs, enchantments, perks); the spell tag belongs to the spell, the art to the MGEF. Rule: swap only when ALL carrier spells share the tag (walk Effects[].BaseEffect reverse). NPC/_NPC duplicates are separate MGEFs; leave NPC-only ones.
- Vanilla/DLC MGEFs: do not touch (tag mismatch there is deliberate); restrict to non-Skyrim.esm MGEFs.
- Dual cast: dualCastData has own art; castingArt swap alone looks mixed. Projectile model swap changes every spell sharing that BGSProjectile (same shared-form issue one level down).
- Performance: one-off load pass over ~14k MGEF; negligible. Runtime cost nil.
- Other mods (Kitty VFX Edit, ENB hands, SkyPatcher INIs) also write these fields; run order and who wins.
- Elemental clash with mod-authored lighting/hit shaders (shader color must match art).
- Assets: Kittytail mods are replacers under their own Nexus permissions; shipping or pointing to their NIFs requires permission; custom water/earth art must be made or licensed. Pointing to vanilla assets needs no permission.
- Not verified: in-game behavior; art object model paths for custom MGEF nodes (castingArt attach to hand nodes); tag-to-MGEF mapping from spell_catalog.json (did not read); 2 plugins failed to load in housecarl, so counts are lower bounds.
