# 2026-10-23 시체 효과 자산 재검증

대상은 `TKL - MUNG ADDON`, 게임 실행 16:57–17:07의 FEC EFSH 7개입니다.
MO2·게임을 실행하지 않고 ESP/BSA/DDS, 게임이 추출한 DDS와 로그를 읽었습니다.
원본 파일을 추출·수정하지 않았으며, 기존 조사 결과와 별도로 바이트를 재검증했습니다.

## 결론

확인된 주황색 원인은 FEC의 **실행 중 Embers XD 호환 패치가 넣는 particle colour keys**입니다.
FEC.esp의 디스크상 색만 검사하면 이 변경을 놓칩니다. `FE91A8E2`는 흑염 base texture와
palette가 모두 적용된 뒤에도 주황색 particle key를 그대로 사용했습니다.
팔레트·base 사본 누락, MultiEffectShader의 미표식 SRE, 이 실행의 사망 표식 누락은 근거가 없습니다.

- `po3_FEC.log:9`: `[17:02:07.822] [36256] [I] Applied EmbersXD patch`.
- [FEC Patches.cpp:76–98](https://github.com/powerof3/FEC/blob/master/src/Patches.cpp#L76-L98):
  `1B212`의 particle texture가 Embers XD의 atlas일 때 particle greyscale-colour flag를 해제하고
  texture, colour scale, particle key 2·3, animation frame 설정을 복사합니다.
- [FEC Globals.h:81–84](https://github.com/powerof3/FEC/blob/master/include/Globals.h#L81-L84):
  대상은 FEC local `8E2`, `877`, `896`입니다. 이번 로그에는 `8E2`, `896`이 등장했습니다.
- `1B212`의 마지막 ESP override는 `Embers XD - Fire Magick Add-On.esp`입니다.
  particle key RGB는 `D5845C / F29664 / D5845C`입니다. 따라서 FEC가 key 2·3에 복사하는 색은
  `F29664 / D5845C`입니다. `8E2`의 원래 디스크상 key는 `000000 / 282828 / 000000`입니다.
- `HeartOfMagic_BlackFlame.full.log:1181`의 `8E2` flags는 `00020542`이고 디스크 값은
  `00030542`입니다. `896`도 로그 1117행 `0002856A`, 디스크 `0003856A`입니다.
  두 경우 모두 `0x10000`만 해제되어 FEC 패치의 예측과 일치합니다.
- 같은 로그 1180·1185행: `8E2`의 `EmbersHD\mx_fireatlas02.dds`는
  `copies\06c85bb9f7b1c6fb\copy.dds`로 바뀌었습니다. 그 사본은 모든 11 mip의 RGB가 같습니다.
  주황색을 없애려면 marked SRE의 particle colour keys도 개인 사본에서 바꿔야 합니다.

## 실제 파일 승자

읽기 전용 재현 도구는 [corpse-assets-audit.py](corpse-assets-audit.py)입니다.
4,557개 enabled mod, 4,245개 `plugins.txt` 활성 항목, `archives.txt`의 1,771개 BSA를 검사했습니다.
BSA 목록 읽기·압축 해제·DDS 판독 실패는 0개였습니다.
같은 loose 경로와 같은 BSA 파일명은 modlist 위쪽/overwrite가 우선이고,
서로 다른 BSA의 내부 파일 충돌은 `archives.txt`의 로드 순서로 판정했습니다.
아래 실제 사용 자산은 게임이 해당 실행에 추출한 DDS와 **전체 바이트가 동일**했습니다.
유일한 예외 `FXdragonHolesNoA`는 stage가 꺼져 요청·게임 dump에 없으며 디스크 승자만 확인했습니다.

`textures\`는 표에서 생략했습니다. fire 비율은 top mip의 실제 DDS를 디코드하여,
base에는 alpha 가중치가 있는 `EfshBaseFireShare`, palette에는 alpha를 무시하는 `EfshFireShare`와
동일한 HSV/밝기 기준으로 계산했습니다. 0%는 압축된 DDS가 수학적으로 완전 회색이라는 뜻은 아니며,
주황색으로 판정되는 픽셀이 없다는 뜻입니다.

| 실제 경로 | 승리한 모드/파일 | 색 검사 |
|---|---|---|
| `Effects\FXthinSpinBitAnim01.dds` | Project Clarity / `Project Clarity - Effects Half Res.bsa` | base fire **0%**, 512×512, 채널 최대 차이 17/255 |
| `Effects\FXSmokeSoftSub.dds` | Project Clarity / 같은 BSA | base fire **0%**, 2048×2048, 채널 최대 차이 11/255 |
| `effects\po3_FEC\fire\shaders\sootshader.dds` | Frozen Electrocuted Combustion / loose | base fire **0%**, 2048×2048, 모든 RGB 동일 |
| `Effects\FXMagicDisolveAlpha01.dds` | Project Clarity / 같은 BSA | base fire **0%**, 512×512, 채널 최대 차이 13/255 |
| `Effects\FXSkinParticleDissolveAnim01.dds` | Project Clarity / 같은 BSA | base fire **0%**, 1024×1024, 채널 최대 차이 17/255 |
| `Effects\FXdragonHolesNoA.dds` | Project Clarity / 같은 BSA | base fire **0%**; 이번 SRE에서는 membrane off |
| `Effects\FXFireAtlas02.dds` | Inferno / `Inferno - Fire Effects Redux - Textures.bsa` | base fire **51.065099%**, 주황색; FEC 826은 palette colour 경로 |
| `EmbersHD\mx_fireatlas02.dds` | Embers XD / loose | base fire **88.548858%**, 주황색; FEC 패치 이후 8E2가 사용 |
| `Effects\Gradients\GradFireExplosion.dds` | Glorious Gradients / `Glorious Gradients - Textures.bsa` | palette fire **41.184998%**, 주황색 포함 |
| `Effects\Gradients\GradDragonHolesSkinBits.dds` | Glorious Gradients / 같은 BSA | palette fire **62.829974%**, 주황색 포함 |
| `Effects\Gradients\GradDragonHoles.dds` | Glorious Gradients / 같은 BSA | palette fire **99.218750%**, 주황색; 이번 826 SRE의 membrane에는 미적재 |
| `effects\po3_FEC\gradients\gradfiredisintegrate.dds` | Frozen Electrocuted Combustion / loose | palette fire **63.044358%**, 주황색 포함 |
| `EmbersHD\Gradients\mx_fire01.dds` | Embers XD / loose | palette fire **39.920044%**; 1B212의 palette이며 FEC 패치는 이 필드를 복사하지 않음 |
| `Effects\Gradients\GradFrostIceFormOrange.dds` | Glorious Gradients / 같은 BSA | palette fire **63.970274%**; 1B212의 membrane palette |

Project Clarity가 이기는 공통 `Effects` 자산의 다음 후보는
`Cleaned Skyrim SE Textures\Skyrim - Textures5.bsa`입니다.
`FXFireAtlas02`는 Inferno → Project Clarity → Cleaned Skyrim 순입니다.
Glorious Gradients의 해당 팔레트는 Glorious Gradients → Cleaned Skyrim 순입니다.
위 loose FEC/Embers 경로에는 다른 후보가 없었습니다.

지목된 Embers XD PBR, Embers XD - CS, Embers XD(Less Poly), Medieval Torch Edit,
EmbersXD Torch Edit, Enchantment Effect Replacer SE, Glow Be Gone Redone - NG, Crackling Fire도
활성 모드 전체 검색에 포함했습니다. 이 표의 파일 경로를 이기는 자산은 없었습니다.

## 각 EFSH가 실제로 사용하는 경로

7개 모두 마지막 ESP 레코드는 `FEC.esp`이고, 뒤의 플러그인에 override가 없었습니다.
7개 모두 `MODL`은 없고 DATA의 addon/debris form도 `00000000`입니다.
따라서 이 7개 EFSH가 별도 particle NIF 또는 debris NIF를 지정한 증거는 없습니다.

| EFSH | 활성 단계와 자산 | 이번 실행 결과 |
|---|---|---|
| FE91A801 | particle `FXthinSpinBitAnim01`, palette 없음 | 회색 base 유지. 디스크 particle keys `808080 / 808080 / 808080` |
| FE91A819 | particle `FXSmokeSoftSub`, palette 없음 | 회색 smoke 유지. keys `323232 / 191919 / 0A0A0A` |
| FE91A823 | membrane `sootshader`, palette 없음; particles off | RGB가 같은 soot 유지. fill keys 모두 `000000` |
| FE91A896 | membrane fill/holes `FXMagicDisolveAlpha01` + FEC `gradfiredisintegrate` | membrane palette 사본 사용. particle path는 패치로 Embers atlas가 되지만 particles off이고 member null |
| FE91A826 | particles `FXFireAtlas02` + `GradFireExplosion`; membrane off | particle palette 사본 사용. coloured atlas는 greyscale-to-colour가 읽어 RGB 색으로 사용되지 않음 |
| FE91A8E2 | 패치 후 particles `EmbersHD\mx_fireatlas02` + record의 기존 `GradFireExplosion`; membrane fill/holes `FXMagicDisolveAlpha01` + FEC `gradfiredisintegrate` | base/palette 사본 모두 적용, **particle key 2·3의 주황색이 남음**. palette colour flag는 꺼짐 |
| FE91A800 | particles `FXSkinParticleDissolveAnim01` + `GradDragonHolesSkinBits`; membrane off | particle palette 사본 적용 |

"FEC의 모든 색 필드가 무채색"이라는 주장은 정확하지 않습니다. 예를 들어 826의 edge는 `924105`,
801/819/800의 edge는 `8080FF`, 823의 particle keys에는 푸른 기가 있지만 해당 단계가 꺼져 있습니다.
켜진 단계에 쓰이는 **디스크상** particle/fill 키는 무채색입니다. DATA disk offset
`0xBC/0xC0/0xC4`의 particle RGB, `0x10/0x138/0x13C`의 fill RGB, `0x180`의 flags를 직접 읽고
기존 로그의 fill/flags와 대조했습니다. 이후 FEC runtime 패치가 이 사실을 바꿉니다.

## `no copy (not built or not fire)` 세 자산

세 항목 모두 request의 `efshBases`에 key와 `state: ok`가 있고 `textures[]`에도 dump 성공으로 있습니다.
게임 dump와 실제 우선순위 승자 파일이 같았고, fresh DDS 디코드로 재계산한 base fire share는 모두 0입니다.
빌더의 5% 기준에서 제외된 것이 맞으며 요청 누락이나 DDS 읽기 실패가 아닙니다.

| 경로 | 요청 key | `staging/efshbase-notfire.txt` 줄 |
|---|---|---|
| FXSmokeSoftSub | `f65f828bbe07ec28` | 44 |
| FXthinSpinBitAnim01 | `2cd3d4c4f55e35ed` | 49 |
| sootshader | `a5ed85c771aeaf73` | 69 |

모두 `fire1:share=0.05:red=0.55:orange=0.041666666666666664-0.1527777777777778:wrap=0.9583333333333334:sat=0.45:value=0.3:lit=0.1` tag입니다.
`builder.log:1130`은 `118 not fire (118 known from the cache)`라고 기록합니다.
`GreyKeyed.h:151–176`의 판정 기준 및 `GreyKeyedBases.cpp`의 cache 경로와 일치합니다.
따라서 캐시를 무효화하거나 이 세 회색 텍스처를 억지로 복사하는 수정은 원인 해결에 필요하지 않습니다.

## 이미 사용 중인 사본과 SRE 추적

로그의 corpse attach가 실제로 이름을 출력한 사본 4개를 `overwrite`에서 다시 읽었습니다.
RGBA8 header/mask와 전체 mip byte 배열을 검사했습니다.

| key/파일 | mip / 전체 texel | 모든 RGB 동일 |
|---|---:|---|
| `ff137a2497d015df/palette.dds` | 1 / 65,536 | PASS |
| `153325d8cc8826ef/palette.dds` | 1 / 16,384 | PASS |
| `8bef37f124ede76a/palette.dds` | 1 / 65,536 | PASS |
| `06c85bb9f7b1c6fb/copy.dds` | 11 / 699,051 | PASS |

MultiEffectShader.log의 tracker 추가 8개 SRE(5·7·26·28·30·32·34·36행)는 모두 BF log의
`corpse mark`와 주소가 같습니다. layer 생성의 고유 SRE 3개도 이 안에 있습니다.
예를 들어 `0x271f42fb600`은 MultiEffect log 7행의 tracker, 10행의 layer,
BF full log 1118행의 mark, 1123행의 attach에 모두 등장합니다.
MultiEffectShader.ini는 actor geometry를 clone하여 기존 effect를 별도 layer에 보이게 하는 설정입니다.
이 실행에서 별도 미표식 SRE를 추가했다는 가설은 **반박**됩니다.

BF log 자체도 mark 20개와 attach 20개를 SRE/shader/target으로 맞춰 전부 일치했습니다.
대상별 19DFC 7개, 19DFB 6개, 210A25E4 6개, 45827 1개입니다.
45827은 full log 1162행의 등불 burn이 마지막 공격이며 1182행 mark, 1185행 attach에 도달했습니다.
이 실행의 DoT 또는 사망 경로 표식 누락 가설도 **반박**됩니다.
기록되지 않은 다른 실행이나 모든 사망 방식까지 증명하는 결과는 아닙니다.

## 재현 명령과 한계

```powershell
$env:PYTHONDONTWRITEBYTECODE='1'
& 'C:\Users\qtuna\AppData\Local\Python\pythoncore-3.14-64\python.exe' `
  'plugins\BlackFlame\docs\diagnostics\corpse-assets-audit.py' `
  'D:\TAKEALOOK' 'TKL - MUNG ADDON' `
  'C:\Users\qtuna\Documents\My Games\Skyrim Special Edition\SKSE\HeartOfMagic_BlackFlame' `
  'D:\qtuna\Temp\Temp\claude\C--Users-qtuna-Desktop\afe5e4de-4afd-412b-bbe3-034b8f079cb7\scratchpad\bf-dossier\HeartOfMagic_BlackFlame.full.log'
```

실행 결과 exit 0, archive_errors 빈 배열, 사본의 `rgb_equal_all_mips` 모두 true였습니다.
Pillow/numpy/lz4가 설치된 Python을 사용하며 JSON은 stdout에만 씁니다.
플러그인 수정의 빌드·자체 테스트·게임 화면은 이 조사에서 실행하지 않았습니다.
실행 당시 SRE particle key를 직접 덤프한 기존 로그는 없으므로 다음 게임 실행에서
개인 색 사본의 before/after와 원본 유지 여부를 로그로 확인해야 합니다.
FEC 소스는 공식 저장소의 공개 구현을 확인했고, 설치 DLL의 실행 로그와 실제 변경된 path/flags로 대조했습니다.
