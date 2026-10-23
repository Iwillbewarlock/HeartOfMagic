# Corpse shader instance colours: engine evidence

Read-only verification on 2026-10-23 for Skyrim AE 1.6.1170. This records the engine seam used by the corpse colour fix; it does not establish which mod supplied a record or texture.

## Inputs and reproducibility

- Flat decrypted image: `D:\qtuna\Temp\Temp\claude\D--DEV-HeartOfMagic\20a2b2a6-2fad-4f91-839e-61c223b10984\scratchpad\disasm\SkyrimSE_1170_image.bin`.
- Image size: **59,179,008 bytes**. SHA-256: `EB4BCB3C02D1DE5C6448B39FBF92E78216D2E8FA4D031E79EF1B7D1B05FD79E9`.
- Address Library mapping: `ids.txt` alongside the image.
- Disassembler and caller scanner: `D:\qtuna\Temp\Temp\claude\C--Users-qtuna-Desktop\10f7c06f-a25d-4289-ac9d-564b9a31711e\scratchpad\sre-inst\{skydis.py,callers.py}`. The earlier `sre-instance-findings.md` supplied leads; the decisive instructions and caller scan below were checked again against the image.
- Headers read from `D:\DEV\HeartOfMagic\plugins\external\commonlibsse-ng\include`, because this worktree's submodule is empty. No file outside the worktree was written.

All instruction addresses below are RVAs. The disassembler presents code against `0x140000000`; pointers stored in the captured image use its actual image base `0x7FF7C1ED0000`.

These commands were run with `C:\Users\qtuna\.agent-reach-venv\Scripts\python.exe -B` (the WindowsApps `python.exe` alias was unavailable). `-B` prevents writing imported-module caches outside the worktree:

```text
skydis.py 5D1250 300 100
skydis.py vt:1880C20:28 5
skydis.py vt:1880C20:35 5
callers.py 34913 34925
callers.py 17329 17332
```

The caller script was invoked with `runpy.run_path`, its directory added to `sys.path`, while keeping the working directory in the worktree. It scans every byte of the image's `.text` range for E8/E9 destinations and scans the data range for absolute function pointers. This establishes direct-call coverage in this image, not coverage of another DLL's generated code.

## Particle colour keys are safe only on a private property

`17325` (`0x2660E0`) is called by attach `34934` at `0x5D1F1C`. Its arguments are the original `TESEffectShader*`, base texture, palette texture, and `bool share`. It returns `BSParticleShaderProperty*`.

| RVA | Instruction/effect | Consequence |
| --- | --- | --- |
| `266136` | `test r9b,r9b`; `je 266150` | `share=false` skips reading the form's particle cache. |
| `26613B` / `266142` | Reads form `+0x210` / `+0x218` | The shared property and geometry are otherwise reused across SREs. |
| `266392` | Calls `17326` | Copies record values into the new property. |
| `266397` | `test r12b,r12b`; `je 2663FC` | `share=false` also skips writing both shared cache pointers. |
| `2663B1` / `2663DF` | Stores property/geometry at form `+0x210` / `+0x218` | Only the shared path writes the cache. |
| `266406` | `mov rax,rbx` | Returns the created/reused property. |

`17326` copies RGB and alpha into three `NiColorA` members:

```text
2667B3: 44 0F 11 8B 00 01 00 00   movups [rbx+0x100],xmm9
2667C6: 44 0F 11 93 10 01 00 00   movups [rbx+0x110],xmm10
2667E4: 44 0F 11 9B 20 01 00 00   movups [rbx+0x120],xmm11
```

These match `RE/B/BSParticleShaderProperty.h:65-67`: `colorKey1`, `colorKey2`, `colorKey3`. `colorScale`, alpha, key times, texture pointers, and particle geometry can remain unchanged. `34913` updates particle emitter alpha and birth ratio (`5D0138` / `5D013D`), not these property RGB keys.

For every marked SRE with corpse greying enabled, call the existing factory with `share=false`, then grey the returned property's RGB before returning it to attach. This applies even when the caller already passed `share=false`; an early return on `!a_share` would miss that case. Texture-copy availability must not decide whether record-derived colours are greyed. No original form, shared cache, or unmarked property needs modification.

## Membrane RGB must survive initial attach, Update, and Pop

`RE/S/ShaderReferenceEffect.h:90-91` distinguishes the original form `effectData` (`+0x108`) from the SRE's `effectShaderData` (`+0x110`). `17328` allocates a fresh **0x88-byte** `BSEffectShaderData` (`266A2D` size, `266A39` allocator call). `17329` initializes its fill and rim values, and `17330`/`17356` attaches that same instance to the target geometry's existing shader property. `17356` takes a reference and stores the pointer at the geometry property's `+0x68` (`267E54`). There is no form-level membrane-data cache corresponding to the particle cache.

`RE/B/BSEffectShaderData.h:30-31` identifies `fillColor` (`+0x2C`) and `rimColor` (`+0x3C`), both `NiColorA`:

```text
266CB1: 0F 11 43 2C   movups [rbx+0x2C],xmm0  ; 17329 initializes fill
266E9F: 0F 11 43 3C   movups [rbx+0x3C],xmm0  ; 17329 initializes rim
267356: 0F 11 66 2C   movups [rsi+0x2C],xmm4  ; 17332 overwrites interpolated fill
```

- **Attach:** original attach creates and connects the membrane data. Grey its RGB after attach returns.
- **Update:** `34913` calls `17332` at `5D01DF`, with the original form in `rcx` and SRE membrane data in `rdx`. This recalculates fill RGB every frame from the form's fill colour keys. Alpha, scale, UV, and hole cutoff are also updated. A one-time fill write is insufficient. Grey RGB after the original Update returns, preserving the original boolean return value.
- **Pop:** when `pushCount` reaches zero and the effect has valid geometry, `34925` calls `17329` at `5D1301`, resetting fill **and rim** from the original form; it then calls `17330` at `5D1326` to reconnect the data. Grey RGB after the original Pop returns. Otherwise the restored lower effect can regain its record colours.

Only RGB needs changing. Preserve alpha and signed values: subtractive rim colours are produced by `17329` at `266E2A-266E63`; clamping those values to zero would change the effect's meaning. The SRE's `effectData` pointer remains the original record throughout, so save/load, Stop, HasEffectShader, and stack matching retain their existing identity.

## Update and Pop vtable coverage

The actual image resolves `ShaderReferenceEffect` vtable `0x1880C20` as follows:

| Slot | Signature (CommonLib header) | Target |
| --- | --- | --- |
| `0x28` | `bool Update(float)` (`ShaderReferenceEffect.h:55`) | `34913`, RVA `5CFD90` |
| `0x35` | `void Pop()` (`ShaderReferenceEffect.h:66`) | `34925`, RVA `5D1250` |

The full caller scan returned:

```text
ptr  34913 at 1880D60
ptr  34925 at 1880DC8
```

There were **zero direct E8/E9 calls** to either function and exactly one data pointer for each, at the matching vtable slot. These two slots were still original engine functions in this captured image. `34913` does not destroy its SRE before returning; its release near `5CFEFE` applies to the resolved target reference. It returns its lifetime decision in `al` at `5CFF26`.

The helper caller scan returned:

```text
call 17329 from 266AEE in func 2669B0 <id 17328>
call 17332 from 5D01DF in func 5CFD90 <id 34913>
call 17329 from 5D1301 in func 5D12AE
```

`5D12AE` is a split unwind range inside Pop, not a separate logical entry. Disassembling `5D1250` through `5D1340` shows the complete function. Thus the initial attach, Update, and Pop paths cover the observed engine writes to these colour members.

The repository's `Patch::WriteVfunc` can retain and chain the current slot target. This also preserves an earlier plugin's vtable hook; replacing it with a hard-coded engine address would bypass that plugin. Installation should verify the targets are executable and use the existing runtime checks. The relevant hooks must only mutate marked SREs with corpse greying enabled.

## Alternatives and limits

A temporary copied `TESEffectShader` is unnecessary. The factory and membrane updater expose instance-owned colour values already. A stack copy would require a separate proof of C++ object lifetime, embedded texture-string ownership, and every callee's pointer retention; no such lifetime assumption is used by the instance fix.

This verification does not run Skyrim, MO2, the plugin, a build, or self-tests. It establishes engine ownership and write sites in this **captured 1.6.1170 image**. Another plugin that later replaces these vtable slots without chaining, modifies the same RGB after these hooks return, or independently creates a differently attributed SRE needs next-run evidence. Actor geometry sharing and another plugin's generated code were not exhaustively audited. Actual visible colour, save/load, stacked Pop behaviour, and NPC isolation still require the next game run.
