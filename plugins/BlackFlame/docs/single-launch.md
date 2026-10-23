# Black Flame single launch

### Single launch: the copy contract (P0): `CopyContract.h`, `SlotCoSave.h`

Work in progress (branch feature/black-flame-single): grey copies built while the game runs, published at kDataLoaded
and at the main menu before the first load, into a fixed slot pool. P0 is the contract both sides build against
(contract version 5); nothing in game uses it yet. Header-only, C++17, no game types, included by the DLL and the
builder like `DataPathRule.h`:

| File | What |
|---|---|
| `src/CopyContract.h` | contract version; the slot pool (ESL local ids: PROJ 480 at 0x800, IPCT 480 at 0x9E0, EXPL 320 at 0xBC0, ARTO 224 at 0xD00, HAZD 192 at 0xDE0, TXST 96 at 0xEA0 - the first 72 with a DODT stub -, ADDN 208 at 0xF00 with AddOnNode index 40000 + place; 0xFD0-0xFFF reserved; every entry pinned by a static_assert), FNV-1a 64, the tint text (`TintText`, `QuantizedTint`: to_chars fixed 6 decimals, 0..2 for tint and tintAdd as the builder takes them, -0 as 0), file states (an unset one makes every key builder refuse), request.json v5 field names (with `textures[].state` - ok, notdumped, missing, unread - so no side reads the error text, and `addnMps[].index`, the source ADDN's index, both required) |
| `src/CopyContractKeys.h` | the key string of every model object (form NIF, PROJ muzzle, ADDN MPS, TXST, the builder's texture and palette copies) and the ADDN table part; key16 = the string's FNV-1a 64 as 16 lower-case hex digits. A not-dumped file keys as `notdumped:<size>` (content-free: the builder never reads it); a non-ASCII path as hex. The prefix's `out=` field is the builder's output identity (`kBuilderOutputVersion`: `<n>` for a release build, `<n>+dev<16 hex>` for a build of any configuration but Release or one whose builder sources differ from git HEAD; key format BFK4, where BFK3 had `exe=<the exe's fnv64>`). `VerifyKeyPrefix`: the builder rebuilds the DLL's `keyPrefix` from its compiled identity and refuses the run on a difference. The exact format is in the header and frozen by golden strings in the test table; the texture child key16 anchor (`kGoldenTextureKey16`) is asserted by that table (the builder's --self-test and cosave-check) and by cosave-check's txst paths test through the DLL's own `SlotTxstPaths::CopyKey16`, so a builder and DLL that key a texture copy differently fail offline |
| `src/CopyContractIdentity.h` | the builder's output identity in its exe (RCDATA 4101, the text's bytes, no NUL) and its read: `ReadIdentityResource` loads the exe as a data file (never run; kernel32 only) and takes the resource's bytes, `BuilderIdentityFault` refuses a malformed identity or another output version than the DLL's own. rc.exe includes it too (only the resource id's `#define` outside `RC_INVOKED`). The builder's `--self-test` reads its own exe through it; cosave-check runs the failure paths (no file, no PE image, an exe without the resource) and reads the builder built beside it |
| `src/CopyContractProgress.h` | the builder's `progress.jsonl` line: outcomes ok, fallback, failed (retryable), too_large and not_dumped (terminal); the writer and a parser that ignores a last line without `\n`, refuses any v but 1, a key given twice and any path that is not a copy path, and reports a bad line by number; a too_large line carries its memory cap as a `cap` field; the last line for a key wins; the DLL copies staging to Data and never moves a file; `ProgressValidPrefix` for the builder before it appends |
| `src/CopyContractTest.h`, `CopyContractTestPaths.h`, `CopyContractTestProgress.h` | the shared test table: `blackflame-cosave-check --self-test` and `blackflame-greybuild --self-test` both run it (the double-byte path cases with a fixed code page 932 rule, so they run on any system; a case the system cannot run is SKIP, not PASS) |
| `src/SlotCoSave.h` | co-save record BFSL v1 (unique id `HBFS`): epoch, ledger version, plugin table, 9 bytes per slot; one byte sequence per snapshot (names ASCII-lower-cased outside double-byte characters, whose trail bytes are kept as they are; table and slots sorted); a read cut short, out of limits or not in that form invalidates the whole record; Write validates before it opens the record |
| `src/DataPathRule.h` | `CopyPath` / `ParseCopyPath`: a copy lives at `meshes\|textures\heartofmagic\blackflame\copies\<key16>\copy.<ext>`, a derived palette at `textures\heartofmagic\blackflame\copies\<key16>\palette.dds` (`PaletteCopyPath`; `IsOurPalettePath` takes it, not a texture copy) (ASCII only; the source's own name appears only in its key string); the staging folder mirrors Data |

The files P1 (builder) and P2 (DLL) fill in exist as stubs with their CMake entries already in place, among them
`tools/blackflame-cosave-check/SkseFile.cpp` for P2's .skse file reader.

```
blackflame-cosave-check --self-test     (BFSL on byte buffers and the contract table; exit 0 = pass)
```

### Single launch: the builder (P1): `GreySlotPool.h`, `GreyKeyed.h`, `GreyProgress.h`, `GreyBudget.h`

Work in progress like P0 (built against contract version 4): the builder side of the single launch. Nothing in game
runs it until P2's DLL launches it.

```
blackflame-greybuild --write-slot-pool <file.esp>
blackflame-greybuild --collisions <request.json>
blackflame-greybuild --from-game [<request folder>] --staging <dir> [--job-memory <MiB>] [--commit-reserve <MiB>] [--parent-pid <pid>]
```

**Slot pool ESL** (`--write-slot-pool`, committed as `data/HeartOfMagic_BlackFlame_Slots.esp`): ESL, no masters, HEDR
1.70, one placeholder per slot of `CopyContract.h`'s pool table, FormIDs 0x800-0xFCF (next id 0xFD0), written through
the copies ESL's record writer (`GreyFormsEsl.cpp`) and the ADDN record writer (`GreyEsl.cpp`), groups in Skyrim.esm's
order TXST, PROJ, HAZD, EXPL, ADDN, IPCT, ARTO. Written beside the target, read back with both byte readers and compared
slot by slot, then moved into place. EditorIDs `HoMBF_S_<TYPE>_<local id>`. The builder self-test fails when the
committed file is not what the builder writes.

| Slots | Record |
|---|---|
| PROJ, HAZD, EXPL, IPCT | EDID, OBND 0 (not IPCT), DATA in Skyrim.esm's size with every value and form link 0; PROJ VNAM 0 |
| ARTO | EDID, OBND 0, DNAM art type 0 |
| TXST 0xEA0-0xEE7 / 0xEE8-0xEFF | EDID, OBND 0, no texture slot, the 36-byte DODT stub on the first 72 only, DNAM 0 |
| ADDN 0xF00-0xFCF | EDID, OBND as Skyrim.esm's MPS records, MODL `HeartOfMagic\BlackFlame\Slots\Unset.nif` (no file has it), DATA index 40000 + place, DNAM cap 0, flags 1 (on demand) |

No record but the ADDNs has a MODL: the DLL sets every slot's model (the source's, or the grey copy) and fills the
scalars, links, the DODT data and the ADDN cap and flags from the slot's source.

**`--collisions <request>`**: the request's ADDN records (`addons`, a `--request` file or the game's request.json) whose
index is in 40000-40207, our own (`"own": true`) left out, one `COLLIDE` line each with the slot it takes. Exit 0 none,
1 some, 2 the request does not read. It runs alone (no other mode's option).

**Key mode** (`--from-game ... --staging`): reads request.json **version 5** and the dump beside it, and builds every
object at the path its key gives - `DataPathRule::CopyPath(<root>, key16, <source>)`, `...\copies\<key16>\copy.<ext>` -
inside `--staging`, which mirrors Data. The builder never makes a key for an object the DLL keyed; it makes only its child
objects' keys (texture and palette copies): the contract's `TextureKeyString` / `PaletteKeyString` over the verified prefix
and the source file's own bytes. At the start it rebuilds the prefix from the options, its compiled output identity and the dumped
palettes (`GradBlackFlame.dds` / `GradBlackFlameAdd.dds`: hashed, else `notdumped:<size>` when the prefix says so, else
`missing`) and refuses the run (exit 1, nothing written) when `VerifyKeyPrefix` says it is not `options.keyPrefix`.
`--tint`, `--tint-add`, `--palette-mode`, `--no-textures`, `--out`, `--in` and the other run options are refused (they
would change an output without changing its key); `--job-memory`, `--commit-reserve` and `--parent-pid` exist only here.
Only files the request lists are read from the dump: a NIF with its fnv64 in `nifs[]` (built only when the dump's bytes
have that fnv64), a texture `textures[]` lists as dumped (`ok`). Any other file in the dump folder (a stale one the DLL
could not delete) counts as not there.

**Output identity and the golden test** (proposal (b), 2026-10-18). A key's prefix carries `out=<identity>` instead of
the exe's hash, so a builder rebuilt from the same committed sources keys every object as before, and a mod update that
does not change the builder's output rebuilds nothing. `tools/blackflame-greybuild/GreyIdentity.cmake` writes the
identity at build time (a custom target the builder depends on; it rewrites its two files, the builder's
`GreyIdentityText.h` and the RCDATA `GreyIdentity.rc`, only when the text changes, in a folder per configuration -
`build/tools/blackflame-greybuild-identity/<config>/` - so a Debug and a Release build at once each embed their own):
`<n>` = `kBuilderOutputVersion` when the build is Release and the output-affecting sources
(`tools/blackflame-greybuild/**` with the golden fixtures, `plugins/BlackFlame/src/CopyContract*.h`, `DataPathRule.h`,
and what sets the builder's compile options and libraries: `tools/CMakeLists.txt`, the root `CMakeLists.txt`,
`plugins/cmake/*.cmake`, `CMakePresets.json`, `vcpkg.json`, `vcpkg-configuration.json`; only the folder pattern recurses)
are git HEAD's (`git --no-optional-locks status`, so a build never takes the index lock); else `<n>+dev<16 hex>` (any
configuration but Release, a modified, staged or untracked source, or git not there), the hex the first 16 digits of a
SHA-256 over the configuration name and those files (not FNV-1a: a CMake script has no unsigned 64-bit arithmetic; no
file matched is an error, not an empty digest). A dev build so gets keys of its own and never writes into a release's. The release identity is a promise
that the output did not change, kept by hand (bump `kBuilderOutputVersion` whenever any output byte or progress line
changes) and checked by the golden test in `--self-test` (`GreySelfTestGolden.cpp`). It runs as a POST_BUILD step of
`blackflame-greybuild` on every build of it (`GreySelfTestGate.cmake`: the whole `--self-test`, about 3 s, its output in
`build/tools/Release/blackflame-greybuild-selftest.log`, the summary and FAIL lines in the build output, its stderr too
when it fails, a clear line when the exe did not run at all); a failure fails the build and moves the exe to
`blackflame-greybuild.gate-failed.exe`, which still runs `--print-golden`; a passing gate removes a `.gate-failed.exe` an
earlier one left. The test runs the
key mode with the release identity and the CPU encoder forced (`GpuUse::kOff`, checked: no crash marker in its staging;
GPU bytes are not fixed, guard 5 checks the GPU by tolerance) on committed inputs (`tools/blackflame-greybuild/golden/input`,
about 180 KB, written once by `--make-golden-fixtures`): BC7, sRGB BC7, BC3, BC1, B8G8R8A8, a 16 x 4 BC7 down to 1 x 1,
an already grey BC1, a B8G8R8A8 texture one step to either side of `kGreyscaleChroma`, `kColouredShare`,
`kStatColouredFrac` and `kStatMeanSat`, texels exactly on `kStatColouredSat` (saturation 0.25: not coloured) and an
alpha-weighted mean channel exactly on `kStatMinChannel` (0.02: counted), texels at alpha x max 6501 and 6507 on either
side of the lit test (each checked to land on its side), a BC5, a palette shader NIF, glow shapes and a
glow map shader, an MPS with AddOnNode 9, a TXST, and an additive palette shader on a B8G8R8A8_UNORM_SRGB source palette
(the derived palette's sRGB branch; checked to come out sRGB). The sRGB palette and the texel-test fixtures were added
to version 1 before its release (2026-10-18 and -19), every row they do not reach unchanged. It compares every output file's size and fnv64, every progress
line's kind, key, outcome and deps (not its reason) and a digest of the output-affecting constants the headers expose
(hex-float text, `DIRECTX_TEX_VERSION`, the DirectXTex and nifly vcpkg port versions) with the table of that version in
`GreyGoldenTable.h`; a difference fails with "bump kBuilderOutputVersion in CopyContractKeys.h and regenerate the table
(--self-test --print-golden)". A released table is never edited: a new version adds one. A released version is pinned:
its row in `kReleasedPins` (`GreyGoldenTable.h`, apart from the tables, so a table pasted from `--print-golden` never
overwrites a pin; that option prints no pin) holds the fnv64 of its table's text, and a pinned version whose table is
missing or no longer hashes to its pin fails the self-test. `Make-TesterPackage.ps1` refuses a builder whose
`kBuilderOutputVersion` has no pin and prints the row to add. Procedure: run `--self-test`, add the row its `NOTE golden
table v<n> fnv64 <hex>` line gives (`{ <n>, "<hex>" },`), commit, build Release again, package. Version 1 is not pinned
yet: it is pinned before the first tester package with BFK4 is made. The builder turns FMA3 off in the CRT's maths functions
(`_set_FMA3_enable(0)`) at the start of every mode, so a shipped run gives the bytes the table holds. Version 1's table was
made on one machine and has not been confirmed on a second one yet; it is the build's gate all the same (the POST_BUILD
step above). Without `--golden-dir` the golden part is skipped (no developer's folder is compiled into the exe; a tester
package has no fixtures); the POST_BUILD step and `Make-TesterPackage.ps1` pass the repository's
`tools/blackflame-greybuild/golden`, so there a missing folder fails. `--identity` prints the exe's output identity; the
package script refuses a `+dev` one, one whose number is not `kBuilderOutputVersion`, and a builder whose `--self-test` has
a FAIL. Not in the digest: constants inside `.cpp`
files' unnamed namespaces (their changes show only as changed bytes where the fixture reaches them). Stated limit: a
forgotten bump whose output change the fixture does not reach gives stale copies, which the exe hash never did. The
golden gate checks CPU outputs only (the test forces the CPU encoder): a change to the GPU encode settings changes no
byte it sees, so it must bump `kBuilderOutputVersion` by hand (review round 1; the builder's code says the same). Stated
limit: the BFK3 generation is never pruned. After the switch to BFK4 every BFK3 key's staging files and progress lines stay
(`GreyProgress.cpp`'s compaction keeps the last line of every key, whatever its format, so those lines are parsed again,
and their staged files hashed again, at every compaction: part of the `fnv64 checks N ms` time), and so do the Data
copies published under BFK3 keys. Those Data copies stay in use until the slot's BFK4 generation is published (recorded in a session's drains and shown from the start after, when the slot's group opened on the BFK3 one): a slot's
last published generation is applied at kDataLoaded while its files and deps are intact (`ApplyLastPublished`, W1),
whatever its key; once the BFK4 generation of that slot is published they are unused disk space (about 250 MiB on the
measured load order, proposal section 4). Exception: a txst object whose ledger entry has no stored strings (a pre-B2
ledger) is made again from this session's keys (TXST slots below), which name BFK4 copies, so it is lost at the
BFK3 -> BFK4 switch and the slot keeps its source until its BFK4 generation is published.

Request v5 as this builder reads it (P2 writes it; every point below is a builder assumption):
- every v4 field, read as `--from-game --out` reads it (so the dev mode builds a v5 request too), but no `models`: v5
  has none (the contract names no such key, `CopyRequestV5.cpp` writes none); in key mode only, an ADDN in `addons[]`
  whose model is no mesh path keeps its index with a warning, and a null `forms[].data` value (a NaN or infinite float
  as nlohmann writes it) is warned and left out - the key mode reads neither, `--out` still refuses both;
- `contractVersion` (top level) = `CopyContract::kContractVersion`, else refused;
- `options {tint, tintAdd, paletteMode, keyPrefix}`: tints in 0..2 (QuantizedTint values);
- `forms[].key`: a form with a model is a `nif` object at that key; a form with a model and no key (the DLL keys no NIF
  it could not read and no form outside a usable slot) is warned, worded by the reason, and not built: its slot stays
  closed. `forms[].slot`: a TXST form's slot (a TXST slot, one form each, else refused); a TXST form carries no key;
- `muzzles[] {path, key}`: `nif` objects;
- `addnMps[] {slot, path, key, index}`: an `mps` object, and its slot (an ADDN slot's local id) bound to the original
  ADDN index `index` (required since contract 4: an entry without it refuses the request);
- `textures[].state` (required): `ok` (in the dump), `notdumped` (over a dump cap: the object is `not_dumped`), `missing`
  (does not open: a slot left, fallback), `unread` (the game opened it and its read failed: the object is `failed`, so the
  next start, which reads it again under a new key, builds it); the error text is never read;
- `txst[] {slot, key, dodt}`: the TXST form whose `forms[].slot` is `slot` (a key on that form, if any, must be `key`);
  `slot` must be a TXST slot and `dodt` its DODT split.
- A NIF object `nifs[]` does not list (with its fnv64, or ok false; paths compared after `MeshDataPath`) is warned and that
  object alone gets a `failed` line, so one slip of the DLL does not block every build.
- Refused as the DLL's mistakes (exit 2): a txst slot no TXST form has, a TXST form key that is not its entry's, two
  TXST forms on one slot, one key for two objects, an ADDN index or slot bound twice or in no `addons[]` record, an `ok`
  `nifs[]` entry whose fnv64 does not parse.

Checked offline (2026-10-18, after in-game run 2): run 2's builder refused the request the DLL really wrote three times
in a row (`'models' must be an array`; then a form with a model and no key, 4 IPCT whose NIF is missing from the load
order; then `no TXST form has the key`, the DLL writing only `slot` on TXST forms) - the self-test's hand-written v5
fixture had all three. The fixture now has the DLL's shape (slots on every form, a mixed-case model path, a TXST with
`model ""`, `texturesRaw` and `data.flags`, a keyless IPCT whose NIF is ok false, an IPCT with no model, an `excluded`
entry with no source) and every rule above has a test (builder self-test 381 pass, cosave-check 222). The real run 2 request, built by a
throwaway builder carrying the old exe's fnv64 (so the keys match), ran end to end: 495 objects, 485 ok, 10 fallback,
0 failed, 259 s; a checker compiled against `CopyContractProgress.h` read all 775 progress lines and matched every
staged file and dep (size and fnv64), and every key the request asked for has a line.

Objects and their lines (`progress.jsonl`, the contract's writer only):

| Kind | Object | Line |
|---|---|---|
| `nif` | a form's model or a PROJ muzzle: the full-grey copy (GreyEffects, the glow rules, texture and palette copies), AddOnNode values on the slot indices | path, bytes, fnv64; deps: each texture / palette copy it names (path, fnv64) and `{"addn": n}` for every slot index the remap gave it (from the source's AddOnNode values the map binds) |
| `mps` | an ADDN slot's grey master particle system, the same build | the same (`{"addn"}` deps too, since contract 4) |
| `txst` | a TXST's TX00 (diffuse, `tint`) and TX03 (glow, tint 1) copies | no path; deps: those copies. A TXST with no colour slot copied (both left or empty) is `fallback` with no dep, never ok (contract 3) |
| `dds` | a texture copy (role diffuse / glow / effect, `copies\<key16>\copy.dds`) or a derived palette (add / normal, `copies\<key16>\palette.dds` from `PaletteCopyPath`), made once per run and named by every object that uses it | path, bytes, fnv64 |

An AddOnNode value goes to `40000 + place` when an `addnMps` entry binds its original index; a value bound to no slot
stays (fallback), and an index of the request inside the pool's own range is never bound (warned). A source value
that already lies in 40000-40207 is left as it is and warned; it gives no `{"addn"}` dep (a source with 9 and 40012 and 9
bound to 40012 still lists 40012, through 9).

| Outcome | When |
|---|---|
| `ok` | everything applied (an already grey texture at tint 1 left as it is counts as applied) |
| `fallback` | a deterministic slot left: a texture not in the dump (the game: does not open), a format that cannot be recoloured (BC5, HDR, float), a DDS that does not decode, a source palette or our palette that does not read (today's palette kept), a TXST slot that is not ASCII, an AddOnNode value bound to no slot |
| `failed` | retryable: an I/O error, out of memory (`bad_alloc`, `E_OUTOFMEMORY`), a locked, vanished or unreadable file (`IsTransientResult`: too many open files, a pending delete, a lost network share, a device or CRC error and a quota too)
on any read - the glow rules' and palettes' too -, a texture copy's write that fails for any reason but `E_INVALIDARG` or
not supported (a short write, a full disk, a locked target), over the cap with what the process holds, a 30-minute wait for commit, a source NIF not listed, not in the dump or that nifly cannot read, the dump's NIF bytes not the request's, a texture the request lists as dumped that is not in the dump, a texture whose `state` is `unread` |
| `too_large` | a texture whose estimate alone is over the job memory cap: a `too_large` dds line for it (the cap in its `cap` field), and every object naming it `too_large` (with `cap` too); also an object whose glow rules or palettes would read a texture larger than the cap |
| `not_dumped` | a texture the rule recolours that the request gives `state` `notdumped`: the object `not_dumped` |

A failure wins over a terminal state, a terminal state over a fallback. Failed and terminal objects leave no file. The
key's last line is the one that counts (compaction keeps it).

**progress.jsonl and staging** (`GreyProgress.h`): at the start (the DLL has read the file to its end before it launched
the builder) leftover `*.tmp` files are removed - in staging's two copies folders and `progress.jsonl.tmp` only - and the
file is compacted: per key its last line - dropped when it is `failed` (no older line of the key comes back), an ok or
fallback line only while its file is in staging with its size and fnv64; bad lines and a cut last line dropped; written to `progress.jsonl.tmp` and renamed over it (a POSIX-semantics rename, so a reader that
holds the file open with FILE_SHARE_DELETE does not block it; MoveFileExW when the file system has none). A progress
file that is there but does not read stops the run and is left as it is. Then it is opened for appending (shared read,
write and delete) and each finished object adds one line in one write, after its file was written as `<path>.tmp` and
renamed into place. Appended lines are not flushed one by one (a line lost to a crash only rebuilds its object: a kept line
is trusted only after the compaction's fnv64 check of its file); the compacted file is flushed before its rename. The
staging line of the log gives the time the compaction's fnv64 checks took (`fnv64 checks N ms`). Once an object has
failed, its remaining texture slots, palettes and reads are not tried. A write that fails (a full disk) latches the log and
stops the run (exit 1); a line the contract's writer refuses is written as a failed line instead and the run goes on. A reason's bytes outside ASCII
are written as `?`. An object is skipped (reused, nothing rewritten) when its key's last line is ok or fallback, its file
is in staging with the line's bytes and fnv64 and every path dep with its fnv64; or the line is terminal - `too_large`
only while its `cap` field is not below this run's (the cap is no part of the key, so a larger cap tries again). Staging
is the builder's cache: the DLL's copy worker must copy into Data, never move, or every start rebuilds.

**Memory** (`GreyBudget.h`): before a texture is recoloured its peak is estimated from the DDS header (the decoded RGBA
chain x 5: RecolourDds holds the source, the decoded chain, the risk check's copy of mip 0, the grey chain and the
encoded chain at once, plus, for a GPU result, one decoded subresource for its check); before the glow rules or a palette
read one, the read's own peak (the file and mip 0 decoded x 2).
Too large alone: `too_large` (the estimate alone: the GPU device never makes one). The system's available commit (commit
limit - commit total, not the job's own remainder) under `--commit-reserve` (default 4096 MiB) plus the estimate: the
texture cache is cleared once, then a GPU device the run holds is released, each followed by a new measure; still short,
the builder waits, a line a minute, at most 30 minutes. The process's committed bytes plus the estimate over `--job-memory`
(default 1536 MiB, the job object's cap the DLL sets): a GPU device the run holds is released with the texture cache, else
before a recolour the texture cache the glow rules keep is cleared once (never during the glow rules' reads, while a
texture of that cache is in use), and it is measured again; still over: `failed` (retryable). The cache is also cleared
between objects past 256 MiB. One worker builds one object at a time (the texture, palette and progress state is not
thread-safe). Inside an object OpenMP runs on every logical CPU: the builder links `OpenMP::OpenMP_CXX` itself (its own
`#pragma omp` loops; vcpkg's DirectXTex links it too), so it is built with `/openmp` and imports `vcomp140.dll`; `TEX_COMPRESS_PARALLEL` (the CPU encoder) and `ComputeStats`'s row loops use
it. The process runs at `BELOW_NORMAL_PRIORITY_CLASS`.

**GPU BC7 encode** (`GreyGpuEncoder.h`, proposal Step 1, 2026-10-18; DirectXTex's `dx11` feature, `d3d11` and `dxgi`
from the Windows SDK, the builder only). In key mode a BC7 target is encoded by DirectXTex's DirectCompute encoder
(`DEFAULT | BC7_QUICK`, alpha weight 1.0; the CPU overload's float in the same place is BC1's threshold 0.5) when:
- an adapter: `IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE)` (else `EnumAdapters1`; each list ends at its
  first failed call), the first that is not a software adapter, logged once at the start (`GPU   adapter <description> (vendor, device, driver <version>), LUID,
  dedicated memory`); none: the CPU;
- a device: `D3D11CreateDevice` on that adapter (`D3D_DRIVER_TYPE_UNKNOWN`, feature level 11_0), made at the first BC7
  texture only while the job memory cap minus the process's committed bytes is at least 448 MiB (`kGpuDeviceShareBytes`:
  the GPU path's peak addition on the real request, 979 MiB against 563 MiB forced to the CPU, one exe) plus that
  texture's encoded chain; `SetGPUThreadPriority(-7)` (whether WDDM honours it is not known). After each GPU encode the
  context is cleared and flushed (DirectXTex's buffers of that call are freed before the output check). It is released,
  with its context, at the first failure (the rest of the run is CPU), before an object would fail over the cap or wait
  for commit (made again once a run when there is room, never a third time), and for the rest of the run on the throttle
  event;
- the crash guard allows it: `staging\gpu.inflight` (`<strikes>`, the adapter, the texture being encoded and `<deaths>`,
  one a line) exists only while device creation or a GPU encode runs. A start that finds it keeps the GPU; the strike
  is carried only into that texture's GPU calls (a game exit kills the builder in a kill-on-close job, so a death on
  another texture is no evidence against the GPU), the death count whatever the texture. Strike 2 (two starts in a row
  ended inside a GPU call on one texture) or 4 deaths in a row on any textures writes `staging\gpu.disabled` (adapter
  with its driver version, output identity) and from then BC7 is encoded on the CPU; a GPU encode that returns sets the
  death count back to 0, and one on the carried texture the strike too. Stated limit: a fault that lets one encode
  return in a run before it kills the builder on another texture is caught only by the same-texture strike (the cost is
  a builder death per session in the background; without the reset, plain game exits during a main-menu build would add
  up to a permanent CPU fallback). A marker with no texture line (an older builder's) counts as strike 1 and one death,
  its strike never carried, and never disables by itself; on an adapter whose driver version does not read, either limit
  puts only that run on the CPU and no `gpu.disabled` is written (it could never be cleared by a driver update). The
  marker stays until the disable write succeeds (a failed write leaves the counts for the next start). A marker naming
  another adapter or driver is dropped with its counts; one that does not read counts as strike 1 and one death and
  stays. `gpu.disabled` is cleared, with any marker, only at a start whose adapter, driver or output identity differs, and
  only when it is whole (both lines there and non-empty, the output a valid identity); one that does not read, or is
  malformed, keeps the GPU off and is never deleted, and a stale one that cannot be removed keeps the GPU off for that run.
  Both files are written to `<file>.tmp`, flushed and renamed over the file (`MOVEFILE_REPLACE_EXISTING |
  MOVEFILE_WRITE_THROUGH`), so a failed write never leaves a partial marker. The output check also refuses a result in
  another format than the target. A
  CPU run (`BLACKFLAME_GREYBUILD_GPU=off`, or the golden test) neither reads nor writes either file. The DLL never walks
  staging (it copies what progress lines name), so the two files reach nothing else.
- Output check (`CheckGpuOutput`): the result is decoded one subresource at a time and compared with the grey image:
  every subresource at 24 dB PSNR or more (all four channels) and no texel's alpha off by more than 96 steps. The real run-2
  request's 52 BC7 textures gave 30.1 dB at worst (mostly on small mips; mip 0 at 37.6 or more) and 50 steps of alpha on
  this machine (RTX 3070 Laptop, driver 32.0.16.1664). Over a limit: that texture is encoded on the CPU and a `GPU   CPU`
  line says why; a passed one gets a `GPU   BC7` line with its worst subresource. A small local defect under the limits is
  not caught. The run ends with a `gpu    ...` line (textures on the GPU, refused by the check, devices made, memory
  releases). GPU bytes are not the CPU's and can change with the driver. Stated limit (proposal 3.1), same key, other
  bytes: any rebuild of a key whose Data file already exists can hit it, whenever the rebuild's GPU bytes differ from the
  file in Data. A key is rebuilt when its staging file is gone, its last line is failed or transient (not reusable), a
  damaged progress.jsonl lost its line, or its staged copy (or a dep's) changed (`ProgressLog::Reusable`). The DLL keeps
  the Data file when a published object of the ledger stands on it (its own file or a dep, with the Data file's fnv64:
  `SlotLedgerModel::PublishedHolds`; a copy path names its key, so that file is a build of the same key): the new file is
  not deferred, the line counts as in Data for the objects that need it, `BuildProgress::Find` names the file by the Data
  file's size and fnv64 (so what is verified and recorded in the ledger is what is in Data), and the log says
  "'<path>': kept the published copy (another build of the same key; ...)" once a session per path (review round 1 D1:
  `BuildProgress.cpp` `KeepPublished`). Only a file that is there and that no ledger object stands on (a damaged or cut
  one, or a build copied in but never published) still goes the old way: launch N defers the new file as `.next`
  (a slot that already had a published generation keeps it, one that had none stays on its source); launch N+1:
  `ApplyDeferred` renames it in over the file there and marks it replaced this session, so `PlanObject` refuses it and
  `ApplyLastPublished` finds other bytes for any generation on the old file: those slots show their source colour for
  that session; launch N+2: published. `ApplyDeferred` applies the same rule first (reviewer round 2): a `.next` whose
  Data file a published ledger object stands on by then (also one an earlier build of the DLL deferred before this fix)
  is deleted, not renamed in, and logged "'<path>': deferred copy dropped, the published copy kept (...)". The other way
  out (let the builder take the Data file's bytes for a key that has one) is not built. Recovering from a bad recorded
  copy: a recorded file is never replaced by a rebuild of the same key, so delete that Data file and the next start
  copies the staging build in.

**Throttle** (`plugins/BlackFlame/src/BuilderSignal.h`): with `--parent-pid <pid>` the builder opens the named,
manual-reset event `Local\HoMBlackFlame_Throttle_<pid>` (SYNCHRONIZE only; not there: never throttled, one `THROTTLE`
line) and reads it between objects. Once the DLL sets it (when the publish window closes: `BuilderLauncher.h`
`ThrottleAfterWindow`, which also puts the job at idle priority and a CPU hard cap of one logical CPU), the GPU device is
released for the rest of the run and OpenMP drops to one thread (`omp_set_num_threads(1)`), logged once.

**Order**: objects whose NIF names no texture the rule recolours (that the dump has) first, MPS before the rest, then by
the texture estimate. The visual-risk check does not run in key mode (it is the dev mode's report).

**`--parent-pid <pid>`**: a thread waits on that process; when it ends the run stops after the object in hand (exit 3)
and is terminated 30 s later if it is still going. A pid that names no process stops the run at once (exit 3); one that
does not open for another reason is exit 1. Known limit: a pid Windows gave to another process before the builder opened
it is watched instead (the job object's kill-on-close is the first guard). The same pid names the throttle event
("Throttle" above).

Exit codes: 0 every object got a line (whatever its outcome), 1 the run could not go on (staging, the key prefix, a line
not written), 2 bad arguments or a request that is not version 5, 3 the parent is gone.

Checked offline: the builder self-test - the pool ESL (counts per type, the DODT split, ADDN 0xF00 = 40000 .. 0xFCF =
40207 with flags 1, the group order, a damaged and a cut copy refused, the committed file the same bytes), `--collisions`,
the progress file (compaction, append, reuse, a changed file not reused, temp files only in our folders), the memory rule
(waits, cache clears, too large, the estimate of a 64 x 64 BC3), the parent watch, the object guard and outcome order,
`IsTransientResult`, `VerifyKeyPrefix` and the palettes' notdumped state, and key runs on made-up NIFs and DDS files
(outcomes, CopyPath files with their fnv64, deps, order, the AddOnNode remap, a second run reusing everything, a changed
copy rebuilt to the same bytes, a tiny cap, a missing palette and a BC5 decal, a texture outside the dump caps, refused
requests and prefixes, the CLI refusals, and `--from-game --out` on the same v5 request); the GPU encoder's guards (the
crash marker: fresh, one marker, two starts in a row, another driver or output identity, a CPU run leaving both files; the
output check: a good result, one texel's alpha off, a zeroed mip, alpha lost, another mip count; the device released
before an over-cap failure, none to release, too large untouched; the throttle event absent, unset, set; the marker writes cut part way, a failed rename, a leftover temp file, a malformed
or an undeletable stale `gpu.disabled`; on WARP the encoder's state and the wiring, `RecolourDds` reaching the encoder)
and guard 5 (with a hardware device: the BC7, sRGB BC7 and 16 x 4 BC7 fixtures on the GPU against the CPU's result -
format, mips, size, the check's limits; a hardware adapter that gives no device, or none at all, an INFO line); the
throttle's wiring in a key mode run (a real event for the self-test's pid, set before a run whose BC7 texture otherwise
gets a WARP device: the THROTTLE line, no device, OpenMP on one thread); the golden pins (a pinned version without its
table, or with another text, fails). Not checked: a request written by the game (P2), the job object's
own limit, a real pause for commit, a transient read failure, a driver that kills the process or resets (TDR), another
GPU, an iGPU-only machine, the throttle event set by the real DLL.


### Single launch: the DLL side (P2): `SingleLaunch.h`

Config `singleLaunch` (off by default: the slot ESL and the builder's key mode come from P1, and nothing of this route
has run in game yet). With it on, the copies map route (Copies.esp, `LoadAndFillCopies`, the freshness check, the copy
model probe, the v4 request) does not run; with it off nothing below runs except the menu sink.

**kDataLoaded, in order** (Main.cpp, `SingleLaunch.cpp`):

| Step | File | What |
|---|---|---|
| menu watch | `MenuWatch.h` | the `MenuOpenCloseEvent` sink, always registered (DevAutomation reads the main menu from it). The main-thread proof (`MainThreadProof.h`): the drains may publish only when the main-thread queue is installed and its first drain ran on `RE::Main`'s thread, decided only by a caller on that drain's thread before any publish step (a caller on another thread, or before the drain stored its thread, leaves it pending; a pending proof re-queues the stepper); otherwise only kDataLoaded itself publishes (logged with both thread ids). kDataLoaded's own thread is not part of it: on 1.6.1170 under MO2 (2026-10-18) kDataLoaded came on another thread than `Main::threadID`, while every drain and menu event ran on Main's |
| file index | `FileIndex.h` (`FileIndexInit.cpp`, `FileIndexGuardCheck.cpp`) | lifted from the spike's FileProbeEngine: PathToID, IndexGuard, FindRecord, RegisterLoose, GlobalLocations. Used only when every structural check passed and a FindRecord of a shipped path found it; logs whether `usvfs_x64.dll` is loaded and the Data folder (`<exe folder>\Data`). Register: FindRecord first - a loose record is kept, an archive record is a name clash (logged, never registered). The checks also want a path nothing ships not found; the first sane GlobalLocations in `.data` is used (more than one is logged). The stream count offset must be 0x10 on 1.6.1170 (probe run 3); on another AE build 0x0C or 0x10 is taken and the first registration proves it. Any fault in a lookup or a registration, a new stream whose count reads 0, or a RegisterLoose that does not raise the count by exactly one turns the index off for the rest of the session (the stream is then leaked, never released under the index); without it the copies wait for the next start's startup index |
| ledger | `SlotLedger.h` (`SlotLedgerModel.h`, `SlotLedgerJson.h`, `SlotLedgerFiles.h`) | `HoMBF_slots.json` in the folder of `PrepareFileSavePath("HoMBF_slots.json")`'s path (the folder the game saves to: `__MO_Saves` for MO2's profile-local saves). The engine appends `.ess` (1.6.1170: `...\__MO_Saves\HoMBF_slots.json.ess`), so only the folder is taken (`SaveFolderFromPrepared`: the path must be absolute and its file name exactly the name or the name + `.ess`, normalised, no `..` left) and our own name joined to it; the folder is logged, 5 backups beside it (rolled once a session, and only from a main that reads; backup 1 copied beside it and renamed in), a mirror per save folder in `<SKSE log folder>\HeartOfMagic_BlackFlame\HoMBF_slots.<8 hex>.json` (the low 32 bits of the FNV-1a 64 of the save folder's path, lower-cased), merged only when its epoch is the save folder's own files' epoch (the single `HoMBF_slots.json` mirror of earlier builds is no longer read); every write flushed to disk and renamed write-through. All of them are read and merged into the newest, the others newest first (the union of reservations; an older copy of a binding of the same epoch loses to the newer one, logged as stale - a save's rebind survives its backups; one slot bound to two sources across epochs is a conflict, never published again). JSON fields that are there but damaged refuse the file (a backup is read instead), except a damaged `previous` list, which is dropped alone (the entry kept, logged on the file's ledger line); a published path must be a copy path. Main not readable: the BFSL of every `.skse` in the save folder is merged in too (a backup may lack the last session's reservations); no file at all: that union alone. The co-saves of the ledger's epoch are merged newest first, and a newer one's binding replaces an older ledger's (what that published goes with it). When a co-save cannot be read whole (or listed), no new slot is assigned that session; one over the 256 MiB cap is logged as such. Each object a slot published keeps its key, its key string (an FNV-1a 64 collision guard), path, size, fnv64 and deps; recording another generation of a role keeps the one it replaces as that role's `previous` (one per role, the fallback `ApplyLastPublished` takes; its files count as published, so a deferred copy never replaces them; an older build ignores the field); parse failures are counted per key (a second session's makes it terminal `parse`) |
| key inputs | `SlotKeys.h` | the closure's NIFs, PROJ muzzles and every ADDN model read through the engine's file system; the dump's texture plan, each planned texture read whole for its fnv64 (outside the caps: `notdumped:<size>`); the builder exe (`<DLL folder>\HeartOfMagic_BlackFlame\blackflame-greybuild.exe`): it must open, and its output identity (RCDATA, `CopyContractIdentity.h`, logged with the key inputs) must be of the DLL's `kBuilderOutputVersion`; it goes into the key prefix verbatim (missing, unreadable or another version: no key this session); the AddOnNode indices the closure's NIFs name, read from each NIF header's block table (`NifValueNodes.h`: a BSValueNode's value is the int32 five bytes before its block's end; a NIF without the Bethesda header, user version 3+, is not parsed). A NIF that is there but could not be read gives no key; an ADDN MPS's own ADDN deps are followed transitively. TXST keys only with `slotTxst` |
| assign and fill | `SlotAssign.h`, `FormCopiesFillModel.h` | wants: every closure form of a copy type (a TXST by its DODT: the stubbed or unstubbed part of its pool) and every ADDN record `addonNodes` gives for a named index. Kept, re-activated or a never-used id; pool full and refused ones stay orange. Every ledger entry's pool form in `HeartOfMagic_BlackFlame_Slots.esp` is filled from its source (the per-type data fill; an ADDN its cap and sound, its DNAM flags left at 1) or, the source gone, from a vanilla template, and its model set to the source's. One gate per slot, closed; `CopyOf` hands a slot out only while its gate is open, so a closed slot's links go to the source. The copies table (`FormCopiesState.h`) holds the active usable slots, so every hook finds them as it found the copies-map copies. A slot ADDN whose index another record claims, or whose `addonNodes` entry is not the slot, is collided. The slot ESL must be loaded (its compile index); a form of the slot ESL itself never gets a slot |
| last published | `FormCopiesFillModel.h` `ApplyLastPublished` | W1: a slot's last published generation whose files and deps are still what the ledger recorded (copies in Data by size and fnv64, each Data file hashed once a session - `DataFiles.h` caches by path, size and write time, and since 2026-10-23 keeps what the session confirmed in `<SKSE log folder>\HeartOfMagic_BlackFlame\datafiles-hashes.tsv` for the next session, which takes a hash from it while the file's size and write time are unchanged (written at the end of kDataLoaded and when the progress worker ends; a file that does not read is ignored whole; the 2026-10-23 run hashed 2541 files / 915 MiB in 3.6 s at kDataLoaded); source files by the key inputs' hashes) is registered and set as its model; an ADDN slot's MPS first (and its source's DNAM flags, kAlwaysLoaded included). The ADDN slots an object names (its muzzle's too) must be open before any of its files is hashed or registered; an object without a file, off a copy path, or whose form takes no model is not used. A role whose last published object is not usable (an upgrade recorded last session that fails here) takes the ledger's previous one of that role when that is intact, and the ledger swaps them (`RestorePrevious`, saved at once); a TXST set whose strings cannot be restored or written falls back the same way (the log names which set). A slot without a source model (a projectile with only a muzzle) restores its muzzle by itself |
| request v5 | `CopyRequestV5.h` | the dump (the very bytes the keys hashed; incremental since 2026-10-23: `dump-manifest.json` keeps each dumped file's `files` stamp, size and fnv64, and a changed dump reads and writes only the NIFs and textures whose stamp changed or whose dumped file lost its size - a kept texture is not read a second time, the keys hashed it already; a manifest without stamps writes everything, as before; the 2026-10-23 run wrote 765 MiB in 8.4 s at kDataLoaded for one changed NIF) and `request.json` v5: contractVersion, options (quantised tints, palette mode, key prefix), per form its key and slot, `muzzles`, `addnMps` (with the source ADDN's `index`), `txst` |
| progress | `BuildProgress.h` | `staging\progress.jsonl` read to its end before the builder starts (the builder compacts it only at its own start); what earlier sessions copied into Data counts now, but no file is copied on this thread (the worker does, after W1) |
| groups | `CopyGroups.h`, `CopyModelProbeVerify.h` | the connected components of the sources' copy links. **Baseline at W1** (publication fix, 2026-10-19): a group one of whose members shows a verified generation (what `ApplyLastPublished` set: its model, a TXST's set) opens now on what its forms show - the shown members' gates and links, the muzzle `ApplyLastPublished` took; nothing else is set. A member with no generation to show whose object is still coming or not verified yet (*fresh*: a TXST slot with nothing published after the TXST rule fix, a brand-new effect joining a group) stays gated for this session, links to it go to its source, and it never keeps its group closed. A member that shows only its source (a slot without a source model whose muzzle is not grey) opens with its group but never opens one at W1. **Upgrades after W1**: such a group stays *upgrading* while a member's newer object (a key change's, or a fresh member's) is in Data or still coming; in the drains each is registered and its NIF Demanded (a TXST set registered only) as below, and a verified one is only recorded in the ledger (a TXST set with its strings and IDs rule, only while the rule is usable; rule `none`: not recorded, the slot stays left): and while it upgrades no SetModel, TXST write, gate or link change is made in the open group. **Same-launch upgrades** (2026-10-23, `showUpgradesNow`, on by default; `CopyGroupsUpgrade.cpp` `ShowUpgrades`, decisions `CopyGroupsDecide.h` `ShowsNow` / `ShowOf`): once the group's list is done with nothing more to wait for and at least one newer object recorded, the next drain shows them while the publish window is open - before the first load (the startup drains or the main menu), when no reference uses the slots' forms - the way a group with nothing shown opens, member by member: each member's recorded model (SetModel), muzzle and TXST set (`SlotTxst::Write` from the strings recorded with it), then the fresh members whose main object (model, TXST set, or a muzzle-only slot's muzzle) was recorded and set get their gates opened, then every open member's links (`Refill`, pointers only). An object that cannot be set (no form, no model, a TXST write that fails) keeps what its slot shows and stays recorded; a fresh member whose main object was refused stays gated. One line per group: `group of N: K newer object(s) shown now, F fresh member(s) opened[, X kept for the next start]`. The window closed first (the player loaded a save before the builder's lines were all in), or `showUpgradesNow` off: the next start's `ApplyLastPublished` shows what was recorded, as before. In practice the builder makes only the changed keys and ended 14 s after kDataLoaded in the 2026-10-23 run (the main menu came 84 s later), so a mod added or updated is grey at the first launch. A failure (registration, a parse failure counted as below) refuses that object only: the group stays open on the older generation, and the object is not tried again this session. A fresh member opens with its group's same-launch upgrades (above), not on its own: its neighbours' links are rewired only at the group's turn, with the main menu up. Stated limit: a slot with no grey generation yet (a brand-new effect) that joins a group with something shown shows its source until its group's turn, or until the next start when the window closed first or `showUpgradesNow` is off. **Groups with nothing shown** (a first start), while the publish window is open and the proof holds: every new object registered, its NIF Demanded one at a time in 8 ms main-thread slices, and only when all parse SetModel (model, muzzle), the gates, the links (`Refill` pointers only) and the ledger's lastPublished. A member whose object failed, is terminal, has no key, or got no line (once the builder ended and every line is read and copied: `CaughtUp`), or whose files did not all reach Data by then, shows its older generation (or, without one, leaves: gated, links to the source) and never keeps its group waiting (in a group with nothing shown, a member still pending does); TXST members (with `slotTxst`) are registered, not Demanded, and written before the gates open (off: they leave). Without the file index nothing that entered Data this session is a candidate (the next start's index has it). A NIF naming an ADDN slot index waits for that slot's MPS; an ADDN slot that will not open counts as ready with the model it has, a collided one makes the member leave; ADDN slots whose MPS only wait for each other are dropped after a full pass. An open ADDN slot takes a newer complete MPS of its key in W1/W2: it stays in the ADDN queue while its current key may still come (its NIFs use the older MPS meanwhile), and a pass over the queue goes on where the last slice stopped. An ADDN slot a NIF names that no source holds this session, or that cannot be published, makes the member leave (W1 and W2 alike); one not open yet only keeps the NIF on the model it has. A parse failure (an ADDN MPS's too) counts the key and leaves a group with nothing shown closed this session (an upgrading group stays open); a registration failure leaves it closed without counting; a Demand that faults stops publication for the session; there is no re-close. Registration and the re-plan run inside the 8 ms slice; ledger writes at most once a second while publishing, and once more when publication settles or the window closes; a step that throws stops publication with a log line. If the file index turns off during the session, an object that entered Data this session is not Demanded (the group stays closed, or an upgrading one stays on what it shows; no parse failure counted). The plan's decisions (each object's state, what a member shows at W1, when a group opens there, an open group's work) are `CopyGroupsDecide.h`, without game types: `blackflame-cosave-check --self-test` runs them. The kDataLoaded log line gives the Data files hashed, their size and the time. RefreshCopies and CheckCopies skip closed slots; `ExpectedMuzzleFor` gives a slot's grey muzzle only while it is open |
| builder | `BuilderLauncher.h` | `"<exe>" --from-game "<request folder>" --staging "<staging>" --job-memory <MiB> --parent-pid <pid>` (quoted the way `CommandLineToArgvW` reads it back): created suspended, below normal priority, only `builder.log` inherited, assigned to a job with kill-on-close and `JOB_OBJECT_LIMIT_JOB_MEMORY` (`builderJobMemoryMiB`), resumed; the job handle is held for the session and never closed by us. Before it: the request folder's named mutex `Local\HoMBlackFlame_Builder_<FNV-1a 64 of the normalised, lower-case folder path>` (`BuilderFolderMutex.h`) is made and held for the game's lifetime, at the slot route's start before the request, the dump or a deferred copy is touched (`BuilderLauncher::ClaimFolder`, also with `launchBuilder` off; review round 1 D2); one that is there already (another game process on the same folder) leaves `builder.log` alone and starts no builder, and this game writes no request or dump, renames no deferred copy and runs no copy worker (the start's read of progress.jsonl counts as caught up: `BuildProgress::SkipWorker`), one warning `the builder was not started: another game process holds this request folder ...; no request is written, no deferred copy is renamed in and no copy worker runs this session` (not made for another reason: a warning, the builder starts unexcluded). Then the last launch's `builder.log` is renamed to `builder.prev.log` (an older one replaced; not renamed: a warning, and it is overwritten) and the throttle event `Local\HoMBlackFlame_Throttle_<game pid>` (`BuilderSignal.h`, manual reset, unset) is made (not made: a warning, the builder starts without it). The process handle is kept for the session: when the builder ends (a thread-pool wait on it, so also in play), one line `builder (pid N) ended: exit code <code> (<meaning>)`: 0 ok, 1 failed (the run could not go on), 2 refused (bad arguments or not request v5), 3 stopped (the parent process is gone), anything else `0x........ (terminated or crashed)` (`BuilderControl.h`); the progress worker's "caught up" line carries it too |
| throttle | `BuilderLauncher.h` `ThrottleAfterWindow` | when the publish window closes (`MenuWatch::Close`, any of its reasons; a few job and event calls, no wait): the job's extended limits read and `JOB_OBJECT_LIMIT_PRIORITY_CLASS` with `IDLE_PRIORITY_CLASS` added (kill-on-close and the memory cap kept, read back), a CPU hard cap (`JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | HARD_CAP`, `CpuRate` 10000 / logical CPUs, at least 1: one logical CPU of the whole machine), and the throttle event set; one line `builder (pid N) throttled after the publish window closed: ...` with what was read back (a warning when a step failed, with the Windows error). What the builder finishes after that is used only at the next launch. A builder that already ended, or none this session: nothing (debug level, not in the log at the default info level) |
| copy worker | `BuildProgress.h`, `ProgressTail.h` | a thread tails progress.jsonl (shared read, write and delete; read again from its start when its file id changes - the builder's compaction by a POSIX rename - or it is shorter than the offset; a cut last line is read again whole, never fused with a later one; the last line of a key wins, a line that differs in any field starts its object over) and copies (never moves) each ok or fallback file staging -> Data through a temporary file and a replacing rename, fnv64 checked on both sides, only while the publish window is open (checked again right before the rename). A failed copy leaves no temporary file and fails every object that is that file or needs it (an older generation stands in). Another file already at a copy's path that no published ledger object stands on (a damaged or cut one, or a build of the key copied in but never published; one a published object stands on is kept: the "same key, other bytes" part of "GPU BC7 encode") is never replaced while the game runs: the copy waits beside it as `<path>.next` (listed in `<SKSE log folder>\HeartOfMagic_BlackFlame\deferred-copies.txt`), the next start renames it in at kDataLoaded before anything uses the path, and it is used from the start after (the index may hold the old file's size). An object counts as in Data once its file and its copy deps are. `CaughtUp`: a pass that began with the builder ended has read the file to its end (or found none - an I/O failure is tried again at the next poll and never counts) and copied everything |

**The publish window** closes for the session on the first of kPreLoadGame, kNewGame, and (once the main menu opened)
the loading menu opening or the main menu closing; each publish step re-checks it on the main thread right before
SetModel and RegisterLoose. Unfinished groups stay as they were. The drains publish only once the main-thread proof is
made in the first of them; without the main-thread queue nothing steps after kDataLoaded and the progress counts as
settled at once, and a proof refused in the first drain stops the stepper there (settled); a pending one (`Main::threadID` still 0)
re-queues it. W1's steps (and its `RegisterLoose` calls) run on the kDataLoaded thread, which on 1.6.1170 under MO2 is
the loader thread, not Main's: they rely on the engine's IndexGuard lock, and T0-W2 judges only the drain steps (W1's are
counted, INFO). "Settled" means nothing more can open **or be recorded for the next start** this session (or the window
closed, or publication stopped): an upgrading group, and one whose recorded upgrades wait to be shown, keeps the stepper
going. The settled line says both: `publication settled: N of M slots open, G groups open, F failed, A ADDN MPS open;
newer objects: R recorded (S of them shown this session, the rest at the next start), X not used, every open group's
upgrades done` (or `K open group(s) with upgrades NOT finished, H with recorded upgrades NOT shown (the publish window
closed | publication stopped)`: what was not recorded is verified again at the next start, what was recorded is shown
then). The dev auto-load waits for "settled" or every slot open with no group still upgrading or waiting to show its
recorded upgrades, and logs it when its 5-minute wait ran out first. T0-W2
accepts a slot that shows the ledger's previous object of a role (an upgrade recorded, not shown) and one that shows the
recorded object (shown in the same launch).

**The player is never asked to wait** (bf-no-wait-proposal.md section 5): the progress is in the log only, at the main
menu and when the count moves (10 s at most) `slots: progress: N of M grey copies ready (log only)`, and
`slots: progress: all M grey copies that can open this session are ready; L member(s) left for now, F of them
fresh (shown with their group at the main menu once verified, else at the next start) (log only)` once (M counts neither); nothing on screen, and after a
load with copies missing nothing either (what was not open when the window closed opens at the next launch).

**HUD** (English), faults only, one of three lines (`BuilderControl.h` `BuilderNotice`): "Black Flame: the grey copy
builder was not found; only copies built before are shown" when the exe does not open; "Black Flame: the grey copy
builder could not be read; only copies built before are shown" when it does not load as a data file or its output
identity is malformed or does not read; "Black Flame: the grey copy builder is from another version; only copies built
before are shown" when its identity is of another output version or it has none (a builder from before BFK4: review
round 1 D3) (the details logged). When the slot
route does not start, one line at the main menu: "Black Flame: grey copies are off this session (<why>)", the why in
plain words (no ledger beside the saves, the slot ESL not enabled; the builder's absence added), logged too. Whether a
HUD line shows at the main menu is not checked in game.

**Saves.** Every save holds the ledger's pairs (BFSL, unique id `HBFS`). A loaded save whose pairs differ from what the
slots were filled from closes those slots' gates ("Black Flame: grey copies are off for some spells in this save until
you restart the game."), and its pairs win in the ledger (the current sources get new slots at the next start). Refs
that already resolved cannot be repaired: the slot route is for throwaway saves. With `slotPreflight` the save's co-save
is read from disk at kPreLoadGame and the differing slots' data are refilled from the save's sources before the engine
reads the save, their models left as they are (no SetModel at kPreLoadGame; unverified: the message's name format and
refilling at that point); a refill of another type is refused. Off, the preflight only logs what it found. Either way
absent and corrupt differ: no co-save, no HBFS block or a block without a BFSL record (a save made before the mod or the
slot route) is normal; a co-save that is there but cannot be read, a BFSL record that does not read (at kPreLoadGame or
in the load callback), or a preflight that throws closes every slot gate for the session ("this save's slot list could
not be read; grey is off for N spells"). A slot a save binds otherwise stays unusable for the session.

**TXST slots** (`slotTxst`, `SlotTxst.h`, `SlotTxstPaths.h`): new code - the copies-map route never wrote a texture set
at runtime (the builder baked the grey paths into its records), so this is the design's open point v2 §1, answered by
the log. A txst line's deps (no slot per dep) are matched to TX00 (role diffuse) and TX03 (role glow) by the key the
builder gives each copy (the contract's `TextureKeyString` over the prefix, the slot's normalised texture and its
fnv64); a colour slot no dep matches keeps the source's bytes, and a dep that matches neither, an ADDN dep, or no dep
at all (a fallback with both slots kept) publishes nothing for that slot. The write sets `textures[i].textureName`
(TESTexture, record strings without "textures\", `TESTexture::GetDefaultPath`) and `textureFileIDs[i]`
(`RE/B/BGSTextureSet.h` 0x040 / 0x0CC); `SetTexturePath` (vfunc 27) is not called. A copy slot's ID is made with
`BSResource::ID::GenerateFromPath` by the rule the engine's own IDs follow, found once a session on the TXST sources slot
by slot (`SlotTxstRule.h`, logged as "TXST textureFileIDs rule"): a slot whose stored ID is non-zero and equals the hash of
the name normalised under textures\ votes "prefixed", of the bare name "bare"; any other slot does not vote. Run 3
(2026-10-18) showed why: of 92 slots, 78 matched "textures\" + name; the old all-slots rule gave NONE and wrote no TXST.
The other 14 are, as run 3's counts read (its log has only counts; the rule line's `Tally::Text` names them by name at
the next run), 10 TX03 'black.dds' that no record has (3 of them seen holding an
earlier override's TX03 ID, 2 zero) and 4 PSI.esp TX05 'cubemap\shinydull_e.dds', a missing file left at zero. A slot both
forms match (a rooted name) does not vote. The rule is the form with votes when the other has none; with both or
neither, no TXST slot is written. The rule line groups the unexplained slots by name. An unchanged slot keeps the
source's ID when it is zero, the rule's ID of its string, or Normalize changes the string's shape (data\, a root, a
doubled or '/' separator); otherwise it takes the rule's (the stale ID names another file; the builder recoloured that
slot by its string); an empty slot takes zero; `Write` logs each ID not taken from the source. The DDS deps are
registered like every copy file (a DDS loads without it, probe run 3). A publish stores the eight strings it wrote and
the ID rule's name ("prefixed", "bare"; "zero" from an older build)
with the txst object in the ledger (`textures`, `idsRule`). W1 in a later session restores the set from them
(`SlotTxstPaths::RestoredStrings`), never from that session's key prefix, so a changed builder or tint, or no builder,
keeps an intact generation while this session's textureFileIDs rule is usable (`SlotTxst::Write` writes nothing without
it; the stored key string's prefix, BFK3 or BFK4, is never compared: cosave-check's txst restore test): the source's key fields now (its eight strings, TX00 / TX03 hashes, DODT;
`CopyContract::TxstSourceFields`) must end the stored key string, each changed colour slot must name one of the deps
(whose files `ObjectIntact` checks), every other slot must be the source's, and every dep must be named. The IDs are
made by this session's rule (a different stored rule is logged). A txst object whose ledger entry has no stored strings
(a ledger written before they were kept, pre-B2) makes them again from this session's keys, as before
(`SlotTxst::RestoredFor` -> `StringsFor`): its deps must be the copies this session's prefix names, so such a generation
is lost at the BFK3 -> BFK4 switch (and with a changed builder or tint, or no builder) and the slot keeps its source until
its BFK4 generation is published.
Whether the decal on screen takes the grey copies is the eye check (T0-decal and E3-slots judge the forms).
Staging cleanup is the builder's; no GC.

**Offline checks:** `blackflame-cosave-check --self-test` also runs the co-save reader on synthetic files, the ledger
(assignment, merge, JSON, the save compare, the files with backups and the mirror in a temporary folder, and one
regression test for each ledger finding of the P2 reviews: a rebind surviving its backups, the mirror's epoch, an
unreadable main not rolled, newest-first merging, non-ASCII conflict text, damaged fields refused), the progress tail
(`ProgressTail.h`: cut lines, a new file id, a shorter file), the BSValueNode reader, and the route start (the save folder from `PrepareFileSavePath`'s return with `.ess` appended or not, relative and empty refused; the main-thread proof's verdicts), and the builder launcher's pure parts (`BuilderControl.h`: the throttle's `CpuRate` for 1, 3, 4, 16, 64 logical CPUs, 0 and 20000 never 0; the exit code text for 0-3 and crash codes, the codes pinned to the builder's by `static_assert`; the throttle's hand-off; `BuilderFolderMutex.h`: the name's normalisation pinned, the decision, real named mutexes made twice), and the publication fix (`CopyGroupsDecide.h`: an old generation with the new one in Data, still coming, failed or refused; an old neighbour with an unpublished TXST; progress after the group opened; a group with nothing shown waiting; the rest of the plan's order - and the ledger's `previous`: kept on an upgrade, per role, swapped back, its JSON round trip, an older ledger without it, a damaged one dropped alone (the entry and its published objects kept), its files counted as published, dropped with a conflict or a save's rebind); `--cosave <file.skse>...` walks real co-saves (the spike's two 1.6.1170 saves walk to their end, no
Black Flame block); `--nif-values <file.nif>...` prints the AddOnNode values (the sample request's 364 closure NIFs all
parse and name every one of the 97 indices the builder's run made grey ADDN records for, plus 17 others).

**Self-test** (`selfTest` and `selfTestSlots`, both off in the shipped config): T0-slots and T0-W2 (section "Self-test").

