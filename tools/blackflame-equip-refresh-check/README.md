# Equip ability visual refresh and ownership regression check

Run from a Visual Studio Developer PowerShell with C++20 tools enabled:

```powershell
& .\tools\blackflame-equip-refresh-check\run.ps1
```

The runner copies the actual production `EquipAbilityRefresh.cpp`,
`VisualEquipContext.cpp`, `VisualEquipRules.h`, and `CodeReadBuilds.h` unchanged into
`build/blackflame-equip-refresh-check` and checks SHA256 equality for each file.
The two CPP files compile as separate translation units against dependency shims.
No production statements are rewritten or reproduced in test code. Optional
`-RepoRoot` and `-BuildRoot` arguments support an isolated workspace run.

## Coverage

The 69 checks exercise the actual public refresh request and selection-tick paths,
current selected-spell ownership policy, restoration after a fire-to-nonfire or
mixed-owner transition, old-copy and old-burn-shader restoration, shader-only
effects, both buff edges, and gameplay preservation for both listed and unlisted
abilities. Listed abilities now use only visual restart; RemoveSpell/AddSpell are
never called. Checks also cover duplicate effects, scene detach ordering, stale
generation/buff requests, engine-version guards (AE 1.6.1170 and 1.7.104 restart the
visuals, AE 1.6.640 and SE 1.5.97 are refused; the shim's Patch::IsCodeReadBuild calls the
copied `CodeReadBuilds.h` rule on the fake runtime version, it does not compile Patch.cpp),
direct cases of that rule (`IsCodeReadVersion`: 1.6.1170 and 1.7.104 accepted, 1.6.640,
1.7.99 and 1.5.97 refused), thread/queue guards, player
caster/target requirements, inactive/dispelled effects, and partial detach failure.

`VisualEquipContext.cpp` and the actual `VisualEquipRules.h` perform the ownership
decision. The shim supplies selected spell objects and static spell-to-owner
fixture data; it does not replace eligibility with a test boolean. The shim does
not run the production VisualSet builder, so classification and graph collection
need separate checks.

The context deliberately preserves ownership attribution while the buff is off:
the hit hook separately gates recolouring, and the refresh path needs attribution
to rebuild the original visual. These tests verify buff-off restart requests, not
the production hit hook's returned art/shader choice.

## Evidence and limits

The preserved 2026-10-23 global-ability policy failed nonfire-only and mixed-owner
expectations once an ability was collected from a fire parent. The old policy API
has no selection input; this is a policy regression demonstration, not a native
engine equipment-change simulation. The existing earlier baseline also reproduced
the unlisted-ability refresh omission.

Fake Stop/Detach and the relocated start record engine requests. `NiPointer` is a
non-owning wrapper over stack fixtures. Passing tests do not establish native
reference counts/lifetime, relocation ABI, real asynchronous attachment, final
getter results, asset correctness, or rendered pixels. In-game confirmation is
still required.
