## Black Flame v2 (copy framework spike)

Branch `feature/black-flame-v2`. The runtime material recolour leaked into NPC fire through shared materials and
relied on a main-thread assumption that run 2 disproved, so v2 makes "the player's only" structural: the player's
fire launches with **grey copies** of its projectile, explosion, impact and hazard forms, whose NIFs have the grey
palette baked in. NPC fire keeps the original forms. Runtime recolour is off by default (config keys above); a form
without a copy stays orange and is logged once. The per-instance light tint stays. Design and decisions:
[docs/superpowers/specs/2026-10-15-black-flame-proto-design.md](../../../docs/superpowers/specs/2026-10-15-black-flame-proto-design.md), "v2 스파이크".

### Copies (S1): `blackflame-greybuild --forms`

```
blackflame-greybuild --forms <forms.json> --request <addons.json> --in <dir> [--in <dir> ...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>] [--no-textures]
```

The forms request (version 2) lists source forms, each with `source` (`Plugin|localId`, the defining plugin), `type`
(`PROJ`, `EXPL`, `IPCT`, `HAZD`, `ARTO`; no IPDS or LIGH copies), `editorId`, `model` (as the record stores it), `bounds`
(6 integers; not on IPCT) and `data` (the scalar DATA values; unknown keys are refused, missing ones are 0; PROJ
`projectileType` takes a name or the raw bit, PROJ `soundLevel` goes to VNAM; ARTO takes only `artType` 0 casting,
1 hit effect, 2 enchant effect, written as its DNAM: an ARTO record has no DATA). `--request` is the ADDN request
of "Grey duplicates": the copies' models count for the AddOnNode values that need a grey ADDN index (no remapped
copy of them is made), so one run writes everything a setup needs into `--out`:

| Output | What |
|---|---|
| `HeartOfMagic_BlackFlame_Copies.esp` | ESL, no masters, HEDR 1.70, form version 44, FormIDs 0x800 up: one placeholder per source - EDID `HoMBF_Copy_<editorId>_<localId>`, OBND (PROJ, HAZD, EXPL), MODL = the grey copy, DATA in Skyrim.esm's size for the type (PROJ 92, HAZD 40, EXPL 52, IPCT 24) with the scalars and every form link 0, VNAM on PROJ. Groups PROJ, HAZD, EXPL, IPCT. Read back and compared after writing |
| `meshes\heartofmagic\blackflame\copies\<model path>` | the full-grey copy of each distinct source NIF: effect shaders as in the grey MPS (palette shaders on the palette of their blend, the rest with a grey base colour) and every AddOnNode on its grey ADDN index |
| `SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json` | the copies map (version 1): source -> copy id, type, models. The next run reads it back from `--out`: a source keeps its id, a new one takes the id after every id used, a source no longer requested is kept as `retired` and its id is never given to another (a saved projectile or hazard of that copy must not turn into another form) |
| grey MPS, `HeartOfMagic_BlackFlame_greymap.json`, `HeartOfMagic_BlackFlame_Local.esp` | as `--request` writes them (the copies' AddOnNode indices included) |

Ids are assigned and every record value is validated before anything is written; all outputs are then written and
read back in a staging folder beside `--out` (`<out>.staging`) and only then moved into `--out`, so a failed run
leaves the previous outputs unchanged. The copies map refuses a source listed twice (in copies, or in copies and
retired), and a new source takes the id after the highest id the map holds, retired ones included.

Install the output folder as a mod after this one and enable both ESLs. The spike's requests are hand-made from
read-only houseCARL reads of the TAKEALOOK profile: [requests/forms-takealook.json](../requests/forms-takealook.json)
(33 forms: the winning PROJ, EXPL, IPCT and HAZD reached from vanilla Firebolt, Fireball, Flames and Fire Rune, plus
the two ARTOs of the art spike: Flame Cloak's hit art `FXFireCloak01` 02ACD7 and casting art `FireCloakHandEffects`
036342) and
[requests/addons-takealook.json](../requests/addons-takealook.json) (the profile's 329 ADDN records). Their NIFs are
other mods' files (Flame VFX Edit, Embers XD - CS, CS Particle Patch), so the output is built on the user's machine
and not committed; the spike run used `--in` Flame VFX Edit, Dragon Breath VFX Edit, Embers XD - CS, Embers XD, then
the CS Particle Patch, Inferno, Particle Patch and vanilla Meshes0/1 BSAs extracted to scratch (33 copies, 9 copy
NIFs, 7 extra grey ADDN records for indices 5, 6, 34, 35, 80, 81 and Embers XD's 225). The art spike re-ran it into a
copy of that output with `KittyVFX - ENBHands` first (the winning `fxfirecloakhandeffects.nif`): the 33 copies kept
their ids 0x800-0x820, the two ARTOs took 0x821 and 0x822 (35 copies, 11 copy NIFs).

### Copy request (v2 expansion C1, DLL): `FireClosure.h`, `CopyRequest.h`

At kDataLoaded, after the fire sets, the DLL walks the **fire closure** from the original forms: for every fire spell its
fire MGEFs and its costliest effect's MGEF (`MagicItem::GetAVEffect`, where the engine takes the projectile and the
casting art; fire or not), then to a fixed point MGEF projectile, explosion, impact data set, casting art, dual cast
data and a spawn-hazard archetype's hazard (and for a fire MGEF its hit art and its RFCT's art), DUAL projectile,
explosion, impact data set and hit art, PROJ explosion, EXPL impact data set, placed object and spawn projectile, every
impact of an IPDS, IPCT hazard, HAZD impact data set and its spell's MGEFs. Copy types: PROJ, EXPL, IPCT, HAZD, ARTO;
IPDS, DUAL, RFCT, SPEL and MGEF are walked through; a copied PROJ's decal texture set and a copied IPCT's two make TXST
copies (section "Decal texture set copies"). Excluded, each with its reason: a form of our own plugins
(`HeartOfMagic_BlackFlame*`), a placed object that is not a copy type, an art with alternate textures. Bound fire
weapons (bound-weapon stage 1): every Bound Weapon-archetype MGEF whose associated form is a bound WEAP that is fire by the
runtime rule's two branches - a fire effect of a fire spell, or the weapon's enchantment carrying a fire MGEF (the MGEF
then need be in no fire spell) - is a seed (`FireSet.h` BoundFireSeeds, logged at kDataLoaded); the closure takes its
MGEF's projectile (bound bow MGEFs carry the arrow's PROJ there) and, for a bow or crossbow, the PROJ of every AMMO with
the `WeapTypeBoundArrow` default-object keyword (the arrows bound-bow scripts hand out: an over-approximation, harmless
because the Launch swap needs the player's bound fire weapon as the weapon source). The request's `viaSpell` is then the
seed's MGEF. No new copy type: the arrows are PROJ copies. The runtime bound fire list (`PlayerArt.cpp`) now also requires
a seed's weapon, so it stays inside the closure. Then, in
`<SKSE log folder>\HeartOfMagic_BlackFlame\` (`My Games\Skyrim Special Edition\SKSE\...`: outside the VFS, so MO2's
overwrite stays clean):

| File | What |
|---|---|
| `request.json` | version 4 (AD1; the builder still reads 3, which had no `dumpComplete`): `game`, `fingerprint`, `copiesPlugin`, `dumpComplete` (false when a dump file could not be written, with `dumpWriteFailures`, or `dumpError` when the dump stopped part way); `forms` (source `Plugin|localId` in UTF-8, type, editorId, model and muzzleModel as the record stores them, bounds, the scalars the builder writes into a placeholder, `viaSpell`, `costliest`); `excluded` (source, type, reason); `addons` (every ADDN record: index, model, cap, DNAM flags, `own` for ours); `models` (the closure's impact and hazard models); `nifs` (each NIF read: Data-relative path, size, fnv64, ok, error); `textures` (T1: each texture those NIFs name - path, size, ok = in the dump, error) and `textureCaps`. Written through a temporary file and a rename, after the dump |
| `data\meshes\...` | the winning bytes of every NIF the request names (form models, PROJ muzzles, every ADDN model not ours), read through the engine's own file system (`BSResourceNiBinaryStream`: archives, loose files and MO2's VFS as the game loads them). Rewritten only when the fingerprint differs from `dump-manifest.json`'s; a NIF that fails to read is removed from the dump; one that cannot be written, or whose path the dump refuses, is removed and listed as not read, and the request then says `"dumpComplete": false` with `dumpWriteFailures` (the fingerprints stay this load order's); the builder copies it into the map and the DLL reads such a map as stale ("built from an incomplete dump") whatever the fingerprint says (AC1; a map without the field counts as complete) |
| `data\textures\...` | T1: the textures the dumped NIFs name, read the same way within the caps (Texture copies below) |
| `dump-manifest.json` | the fingerprint the dump was made for, the textures' own `textureFingerprint`, and `textureReadFailures` (textures that opened but could not be read) |

Model paths are normalised (ASCII lower case, `/` as `\`, repeated separators folded, a leading `data\`/`meshes\` dropped,
one `meshes\` put in front); a path with a drive letter, `:` or a `.` or `..` token is refused and never read or written
(AC2: a `.` token is refused, not folded - `meshes\.\fx.nif` is never read, a known limit). Each
NIF is read in 64 KiB pieces with every return code checked and must give exactly the stream's `totalSize`.

**Fingerprint** (one formula for the request, which the builder copies into the copies map, and for the freshness
check at every start): FNV-1a 64 over the sorted lines of every closure form (type, source, model, muzzle), every ADDN
not ours (index, source, model) and every NIF (path, size, fnv64). Scalars, EditorIDs and bounds are left out (the DLL
fills them from the live forms), and so are our own plugins' forms and ADDNs (a built copies plugin must not make its
own request stale). The NIF bytes are read at every start on the kDataLoaded thread, so a same-size replacer (a
recolour) is caught; the log line gives the time (`copy request: N forms (PROJ a, EXPL b, IPCT c, HAZD d, ARTO e) from S
fire spells, X excluded, M NIFs (K failed) read in T ms, fingerprint F, request written, dump written|unchanged`). The
fingerprint, the request and the dump each have their own try/catch: a failure is logged and the hooks are installed
anyway. `writeRequest` false (the default for players) reads no NIF and makes no fingerprint, request or dump (v2 fix
J7; the freshness check of C3 makes its own cheap size form). The request also carries `fingerprintSizes`, the same
formula without the byte hashes (path and size), which the builder copies into the map for that check. A NIF is read
once (its bytes are kept for the dump); a busy open or read is tried once more and a stream that cannot seek is read from
where it opened (warned once), so a transient error does not change the fingerprint. "Unchanged" also needs every dumped
NIF to be there with its size, and dump paths past MAX_PATH are opened through `\\?\`. Model paths with `<>"|?*` or a
control character are refused; a double-byte character of the system code page is copied as it is (its trail byte is never
lowered or taken for a separator), and every leading `data\` and `meshes\` is dropped. The closure never walks an excluded
form (a costliest-effect seed reaching it again included, v2 fix J1); a copy-type form with no defining file (made at
runtime) is excluded, and a hazard's spell seeds only its fire effects and its costliest effect (J8).

### Builder from the game (v2 expansion C2): `--from-game`

```
blackflame-greybuild --from-game [<request folder>] --out <output mod folder> [--tint <0..2>] [--tint-add <0..2>] [--reclaim-retired] [--no-textures] [--retire-left-out]
```

The request folder defaults to `<Documents>\My Games\Skyrim Special Edition\SKSE\HeartOfMagic_BlackFlame` (SE and AE)
or `...\Skyrim Special Edition GOG\...`, whichever `request.json` is newer (Documents through `SHGetKnownFolderPath`, so a
redirected or OneDrive Documents works). Skyrim VR is not supported. The run reads `request.json` (version 3) and the
NIF dump beside it - no `--in`, `--map`, `--request` or `--forms` - and writes everything into `--out`, which must be an
existing folder and **the same folder every time** (its copies map gives the stable ids). Every refusal names the next
step (no request: start the game to the main menu once; no `--out`: make an empty mod first).

- **Warn and continue**: a model the game could not read (`nifs` ok false), a NIF missing from the dump or one nifly
  cannot parse gives a copy that keeps the **source's** model (`greyModel` "", a `note` in the map, a `WARN` line); a grey
  MPS that cannot be made drops its grey index (those AddOnNodes keep the original particles); an AddOnNode value in no
  ADDN record (e.g. Flamearrowflight.nif's 99) is kept as it is and warned. With `--forms` (hand-made requests) these
  stay failures, as before: the behaviour change is `--from-game` only. A request listing one ADDN index twice keeps the
  later record (warned).
- **Copies map version 2** (`SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json`): `requestFingerprint` (the request's,
  compared by the DLL at every start), `dumpComplete` (the request's; false or not a bool: the DLL reads the map as
  stale), `copiesPlugin`, per copy also `sourceMuzzleModel`, `greyMuzzleModel` (PROJ: the
  grey copy of its muzzle flash model, built like the form models) and `note`; `retired`; `excluded` (the request's);
  `shaders` (v2i, information only, no reader takes it: `tint`, `tintAdd` and the grey NIFs' effect shader counts
  `paletteNormal`, `paletteNormalNoAlpha`, `paletteAdditive`, `left`; the run prints the same as its `shaders` line).
  Version 1 maps are still read. Copy EditorIDs are `HoMBF_C_<TYPE>_<local id>` (the runtime gives no source EditorID).
- **Ids**: kept and new as before; when no id above every used one is left, the run writes nothing and the message gives
  the requested, earlier and retired counts per type. `--reclaim-retired` then gives retired ids to new forms, lowest
  first - only for a new save (a saved projectile or hazard of a retired copy would read as the new form).
- **Commit**: every output is staged and read back beside `--out` first, then each file is moved in on its own
  (`MoveFileExW`, replace, write through). `--out` itself is never removed or renamed (MO2's `meta.ini` stays); a failed
  move names how many files were moved, and running again converges. `HeartOfMagic_BlackFlame_Local.esp` is always
  written, empty when no local grey ADDN record is needed (the old run removed it).
- **Install**: MO2 - add the builder as an executable with the arguments
  `--from-game --out "<MO2 mods>\HeartOfMagic BlackFlame Copies"`, make that empty mod first and place it after Heart of
  Magic - Black Flame; Vortex - point `--out` at a mod folder in Vortex's staging folder, or zip the output folder and
  install it as a mod. The builder needs no VFS (it reads only the dump). Then start the game again.

Checked offline (2026-10-16): the builder self-test (v3 parsing, map v2, the 2049th form, `--reclaim-retired`, warn and
continue on a missing, an unparsable and an unreadable model and an AddOnNode with no ADDN, `meta.ini` kept, the empty ESL);
a simulated game request made from the spike's hand-made requests with the same NIFs as a dump gave the same 30 grey NIFs
byte for byte, the same local ESL and grey map as the `--forms` run, and a second run kept every id; `--roundtrip` of the LE
streams `bluefiresprayfx01.nif` (Skeletal Revenants, Vigilant) and Vigilant's `flamering.nif`: string table reordered only.
Not checked: a request written by the game (C1 needs the in-game run).

### Texture copies (T1): `GreyTexture.h`, `--no-textures`

`GreyEffects` changes shader values only, so colour **painted into a texture** stays: an orange diffuse or glow map on a
lighting-shaded mesh, or the source texture of an effect shader the builder leaves off the palette. `--forms` and
`--from-game` (not `--map` / `--request`) therefore also write grey copies of those textures; `--no-textures` turns it
off (the copy NIFs then keep naming the source textures). An output built before this release has no texture copies:
start the game once to the main menu with this release (the DLL then dumps the textures), quit and run the builder again.

- **Which textures** (NIF data only, any load order): in every full-grey copy NIF and grey MPS, `BSLightingShaderProperty`
  texture set slot 0 (diffuse) and slot 2 only on a **glow shader** (shader type `BSLSP_GLOWMAP` or the SLSF2 glow flag,
  bit 6; on a skin, face or multi-layer shader slot 2 is another map and stays), and the source texture of every
  `BSEffectShaderProperty` that `UsesPalette` rejects (a palette shader's texture is a greyscale map through our palette).
  Our own textures (`textures\heartofmagic\blackflame\...`) are skipped. A texture is **already grey** when the mean chroma
  (brightest minus darkest channel) of the mip 0 pixels that are visible (alpha) and lit (brightest channel at
  `kLitFloor` 0.1 or above) is under `kGreyscaleChroma` (0.05) and no more than `kColouredShare` (0.5%) of them have a
  chroma of `kColouredChroma` (0.15) or more, or when none is lit: small orange specks on black count fully, a few orange
  embers in grey smoke count through the share, the 5:6:5 noise of a dark grey BC1 does not. When no pixel is both visible
  and lit (an additive texture whose alpha is zero), the lit pixels are measured whatever their alpha. An already grey
  texture is left only at a tint of 1: a diffuse at `--tint` 0.6 gets a darkened copy even when grey, so the diffuse
  always carries the darkening. One-channel formats (BC4, R8, A8) and legacy luminance DDS (L8,
  A8L8, L16, read expanded to RGBA) are grey too. A two-channel format (BC5, R8G8) cannot hold grey (no blue) and is
  left as a failure.
- **Glow shaders' emissive colour**: set to its luminance (x 1) in the copy NIF, since a grey glow map under an orange
  emissive colour still glows orange; the same for a lighting shader with the SLSF1 `Own_Emit` flag (bit 22), which adds
  its emissive colour without a glow map. Other lighting shaders keep their emissive colour. A
  `BSLightingShaderPropertyColorController` that animates the emissive colour is not greyed (known limit).
- **Recolour** (DirectXTex, MIT, linked into the builder only): decode (BCn to 8-bit RGBA in the source's colour space, so
  sRGB stays sRGB), each pixel set to its Rec. 709 luminance (the builder's own `Luminance`) x the role's tint, alpha kept,
  encoded again in the **source format** (BC1/BC2/BC3/BC7 or the uncompressed format; BC7 with `TEX_COMPRESS_BC7_QUICK`)
  with the same size, mip count and array size. In key mode a BC7 target (BC7_UNORM, BC7_UNORM_SRGB) goes to the GPU
  first (DirectXTex's DirectCompute encoder, "GPU BC7 encode" below); its result is decoded and checked on every
  subresource, and anything over a limit, a refusal or a failure is encoded on the CPU from the same grey image. The other
  routes (`--from-game --out`, `--forms`) encode on the CPU only. Tints: diffuse = the run's `--tint` (default 0.6; lighting shaders are
  otherwise left alone, so the diffuse carries the darkening), glow `kTextureTintGlow` 1.0 (a brightness mask), effect
  source `kTextureTintEffect` 1.0 (the base colour already carries the tint). HDR (BC6H), float, typeless, planar,
  palettized and video formats are refused with a reason (a float texture is not clamped).
- **Where**: `textures\heartofmagic\blackflame\copies\_tint\<tint x 100>\<path after textures\>` for every tint (`_tint\100\`
  for 1, `_tint\60\` for the default diffuse tint), in `--out` (staged and committed with the rest). The fixed `_tint\`
  segment keeps a source path from landing on another source's copy (a texture under `textures\t60\` once did when tint 1
  had no folder). After a successful run the builder removes from `--out` every file under its own copy folders
  (`textures\heartofmagic\blackflame\copies\`, `meshes\heartofmagic\blackflame\copies\`) that this run did not write - an
  earlier run's copy of a source that left the load order, or of a texture now grey; nothing outside those folders, and
  nothing through a symbolic link or junction (W17, X13). One texture used at two tints gets two copies; each texture and tint is made once per run. The slot is
  rewritten as that path with `textures\` in front, as `kPalettePath` is. The source file is never written. A texture that
  is missing, unreadable or fails leaves its slot as it was; it never fails the NIF.
- **Paths**: the builder reads a slot with the DLL's own rule (`plugins/BlackFlame/src/DataPathRule.h`, included by both,
  not copied): lower case, `/` as `\`, leading and repeated separators folded, every leading `data\` and `textures\`
  dropped and one `textures\` put in front, a double-byte character of the system code page kept as it is; a drive, `:`,
  a `.` or `..` token (a `.` token is refused, not folded: `textures\.\fx.dds` is a known limit), a trailing separator or
  a character no file name has is refused. A double-byte character counts as one
  character of its path token, so a trail byte 0x5C is never taken for a separator (the separator after it is kept, a
  path ending in it is a file, it splits no `..` off its token). One table of cases runs in the self-test against that
  header (the double-byte cases only on a double-byte system code page). The same header's `LowerSlashes` (case and
  separators, a double-byte character kept) is the key rule of the builder's model paths (`NormalizeDataPath`;
  `MeshDataPath` is `Normalize` under `meshes\`) and of the DLL's grey map lookups, so model and texture keys follow
  one rule.
- **Output**: one `TEX OK` line per copy (texture and tint) and one `GREY|FAIL|MISS` line per texture path, the first
  time it is met (a texture that failed or is missing is not tried again at another tint), and at the end
  `textures  recoloured N, skipped greyscale G, failed F, not in the dump M; R of S slots now name a grey copy; glow shader
  emissive colours greyed E; copies X MiB`. `MISS` names the game's reason when the request lists the texture as left out
  of the dump.
- **Where the textures come from** (`--from-game`): the DLL's dump (`data\textures\...`, Copy request above). A request
  without a `textures` list (an older DLL, or a listing that failed) builds without texture copies and says so. `--forms`
  reads them from the `--in` folders.
- **The DLL side**: with `writeRequest` on, every NIF that read ok is scanned for inline texture strings
  (`DataPathRule::TexturePathsIn`: each `.dds`, any case, whose run of printable bytes before it is exactly as long as the 4
  length bytes in front say - no NIF parser, so every block type's slots are found), normalised with `textures\` as the
  root, opened for their size (`ProbeModel`) and planned in path order within `kMaxTextureBytes` (64 MiB each) and
  `kMaxTextureDumpBytes` (768 MiB in all; a texture that no longer fits is left out, smaller ones after it still go in).
  They are read and written only when the dump is (re)written. `request.json` lists them (`textures`: path, size, ok,
  error) with `textureCaps`; it is written after the dump, so a read failure is named there. The request fingerprint is
  unchanged (the players' freshness check shares it), so a texture-only change does not make the copies stale;
  `dump-manifest.json` carries its own `textureFingerprint` (path, size, planned) and `textureReadFailures`: a texture
  that opens but cannot be read is listed there; the next start with an unchanged dump reads only those again (a read
  failure may pass, e.g. a busy archive) and rewrites the manifest when one reads, without redoing the dump; a texture
  that cannot be written leaves the manifest as it was, so the next start dumps again. A manifest
  without a texture fingerprint - a dump made before T1 - is written again once. A listing that throws writes `error` as
  the texture fingerprint (never equal to a real one) and leaves `textures` out of the request.
- **Our palettes**: the two fixed palettes (`textures\heartofmagic\blackflame\gradblackflame.dds`, `...add.dds`) are
  planned first (`own` in the request's `textures`), read whole at every start with `writeRequest` on for their FNV,
  which goes into the texture fingerprint: replacing one, even with a file of the same size, dumps again.
- **Same-size replacements**: the texture fingerprint holds sizes only (no byte is read at a start), so a texture mod
  swapped for one of the same size is not dumped again. After changing texture mods, delete `dump-manifest.json` in the
  request folder, start the game to the main menu once and run the builder again.
- **Cost** (writeRequest on only; players' default `false` opens no texture): on the 662-NIF dump of 2026-10-16 the scan
  finds 411 distinct texture paths (216 under `textures\effects\`, 23 normal maps, the palette gradients too: the DLL takes
  every path, the builder picks the slots; a run over that dump used 90 of them). Opening them for their size at every
  start: about 80-100 ms (677 NIFs took 136 ms in the tester run). Reading and writing them when the dump is written: not
  measured; estimated 300-600 MB and 2-6 s (677 NIFs, 20 MB, read in 289 ms), once per load order change.
- **Not done** (fix list T10, T12): the dump does not put the textures of the NIFs the builder greys first, so under the
  total cap a needed texture can lose to an unneeded one (the cap is far above the 411-path estimate); copies a run no
  longer makes stay in `--out`, a failed NIF's texture copies are committed with the rest, and the dump keeps textures
  that are no longer named (none of these is read by anything).

Checked offline (2026-10-17): the builder self-test (the shared path table against the DLL's header - the DBCS case runs
only on a double-byte system code page; the texture scan cases; a coloured BC3, BC7 and B8G8R8A8 become grey with alpha,
format and 7 mips kept and the source unchanged; a grey BC1, a dark grey BC1 and a legacy A8L8 are skipped; black with
small orange specks is recoloured; a broken DDS, a BC5 and a float texture fail with a reason; the glow shader rule, its
emissive colour, per-tint copies, and the slots that must stay). Not checked: the DLL's texture dump in the game, and how
the grey copies look.

### Decal texture set copies (TXST): `GreyFormsTxst.h`, `FormCopiesFillTexture.cpp`

Fire Rune's glyph and the scorch a fire impact leaves are **decals**: texture sets (TXST) the projectile
(`BGSProjectileData::decalData`) and the impact (`BGSImpactData::decalTextureSet`, `decalTextureSet2`) name, drawn
apart from their NIFs. Measured 2026-10-17: Fire Rune's `MAG_RuneFireProjectile02` names `DecalRuneFire` (Skyrim.esm
06051D: TX00 and TX03 `Effects\RuneFire01.dds`, an orange DXT5), the fire impacts `DecalFlameBurn01` (02B95A); the copy
NIFs were grey, the decals stayed orange. The player's copies now point at copies of those texture sets; NPCs keep the
originals, and a texture set the player's and an NPC's forms share is never written (only the copies point at its copy).

- **Closure and request**: the closure takes a copied PROJ's decal set and a copied IPCT's two sets (edge `kEdgeDecal`).
  A texture set an explosion places (`impactPlacedObject`) is neither walked nor excluded for that (AB2: excluding it
  would take away its decal copy); the copy EXPL keeps the source's placed set (T0-decal checks it).
  A TXST form in `request.json` gives `textures` (TX00..TX07 for people, UTF-8, `""` for an empty slot), `texturesRaw`,
  `hasDecal` (the source has DODT), `data.flags` (DNAM) and `bounds`; the request says `"decalTextureSets": true`. Its
  TX00 and TX03 textures are dumped with the NIFs' (a path both name, once). The fingerprint's line of a TXST is its
  eight slots and whether it has DODT, so a change of either makes the copies stale.
- **Slots** (CommonLibSSE-NG `RE/B/BSTextureSet.h`, TX00..TX07): diffuse TX00 is recoloured at the run's `--tint` (a
  decal is drawn on a lit surface like a mesh diffuse), glow / detail TX03 at 1 (a brightness mask), both through the
  same texture step as the NIFs (the same chroma judgement, the same `copies\_tint\<pct>\` folders; RuneFire01 gets a
  `_tint\60\` and a `_tint\100\` copy). Normal, masks, environment and the rest keep the source's text. Not checked against
  the game: how the decal shader uses each slot.
- **Record** (in the copies ESL, the TXST group first): EDID `HoMBF_C_TXST_<id>`, OBND, each slot the source has (its grey
  copy when one was made, else the source's text, without `textures\`), a **DODT stub** (36 zero bytes) exactly when
  the source has DODT, DNAM flags; no DATA, no MODL. `decalData` is a pointer (`RE/B/BGSTextureSet.h` 0x0C0) the engine
  allocates for a record with DODT, which the DLL then fills; it never allocates one. Read back subrecord by subrecord
  (presence kept apart from content, a subrecord twice is a fault).
- **Text** (AB1): a slot's text never goes through a code page. The DLL sends each slot's own bytes (`texturesRaw`, hex
  of the record string) beside its text for people (`textures`); the builder writes those bytes unchanged into the copy
  record for every slot it does not recolour, the map keeps them (`sourceTexturesRaw`, hex; `sourceTextures` the text),
  and the DLL compares bytes with bytes (ASCII case and `/` folded, a double-byte character of the system code page
  kept). A double-byte path, or bytes that are no text in any code page, pass through as they are. A colour slot
  (TX00 / TX03) whose bytes are not plain ASCII is not recoloured (a `NOTE`; the risk check's `DECAL_NOT_JUDGED` WARN),
  since its dump file's name depends on a conversion of those bytes. Only a hand-made `--forms` request without
  `texturesRaw` converts its UTF-8 text to the code page (exactly: no best-fit character, read back to the same text;
  otherwise that texture set is left out with a warning). A texture set left out while reading, or one the DLL excluded
  (the request's `excluded`, type TXST), whose copy `--out` still holds refuses the whole build, like an older request
  over TXST copies: run again with `--retire-left-out` to retire it on purpose (its record goes: only for a new save),
  or build into an empty `--out`. `--forms` refuses a game (version 3) request: it carries the dump, the decal flag and
  the left-out forms only `--from-game` reads.
- **Fill (DLL)**: `boundData`, `flags` and `*decalData` (its colour's RGB set to its Rec. 709 luminance, alpha kept)
  from the live source; never `textures[]` or `textureFileIDs`, nor `SetTexturePath`: the copy keeps its record's
  paths. An IPCT copy's own `dData` colour is greyed the same way, and its two texture sets, like a PROJ copy's
  `decalData`, point at the TXST copies (the second pass, the compare and the refresh, in play too). A TXST copy is
  left out (the decal stays the source's, orange) when its DODT presence differs from the live source's, a live source
  slot is not the one it was built for, its slots are not the map's (the ESL and the map from different runs), or a
  grey texture does not open through the game's file system (opens only: whether it decodes is the builder's risk
  check, which decodes every copy it judges). The reasons are counted apart: an out-of-step copy makes freshness `stale`
  ("run the builder") even with a matching fingerprint; a grey texture that does not open makes it `texturesMissing`
  (its own corner notice: the output mod's textures missing or overwritten, or a loose-file cache such as CRDW out of
  date - the copy's line says "on disk under Data" when the file is there but the game's file system does not open it;
  both counts in the detail when both occur); the tester file and CheckCopies give the two counts. The grey decal
  textures are part of the copy model probe's file-index state, so a changed one probes again. A
  slot that changes after the start cannot be refilled (the record's string): CheckCopies reports it and the next start
  leaves the copy out.
- **Risk check**: `DECAL_COLOUR` (FAIL) when a TXST copy's TX00 or TX03 still names a coloured texture (the same
  `IsColouredTexture` judgement as `TEX_COLOUR`); UNREAD / MISSING as for the NIFs.
- **Old and new**: an old copies ESL and map with this DLL: no TXST entries, the decals stay the source's, the fingerprint
  differs (stale: "run the builder"), and the log says how many texture sets the closure has without a copy. A new
  output with an older DLL: one "bad entry" warning per TXST entry (T0-v2 FAIL lines), every other copy works and the
  decals stay the source's. An older builder refuses a new request (`type 'TXST' is not ...`) and a new map. This builder
  refuses a request from an older DLL when `--out` already holds TXST copies (building would retire their records),
  and builds without decals, saying so, otherwise. A source keeps its type with its id: one that comes back as another
  type, active or retired, is refused before anything is written (retired entries now keep their `type`).
- **Saves**: the copies ESL holds only active entries, so a saved reference to a TXST copy resolves only while its source
  is still copied; retiring a source only reserves its id (it is never given to another source or type), its record is
  gone. Whether decals are saved at all is not checked (`BSTempEffect` has save and load virtuals; the save format has a
  temp-effects section, UESP); uninstalling the copies, or a rebuild that drops a texture set, with saved decals is a
  known limit.
- **Known limits**: TX02 (environment mask / subsurface tint), TX06 (multilayer) and TX07 are not dumped or judged; the
  decal on screen is an eye check (self-test T0-decal judges the forms by data).

