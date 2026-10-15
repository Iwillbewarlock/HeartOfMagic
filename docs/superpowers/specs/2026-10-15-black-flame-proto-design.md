# 흑염 달구기 시제품 설계 (2026-10-15, 초안)

## 목표

플레이어가 "흑염 달구기"(자기 대상 버프, 60초)를 쓰면, 그동안 **플레이어가 쓰는 모든 불마법**의 시각 효과가
회흑색으로 바뀐다. 기준 색감은 Black Flame(넥서스 192777)의 화상 셰이더 정도: 가산 혼합은 그대로 두고
팔레트만 무채색으로 바꾼다. NPC가 쓰는 불마법과 탄도·사거리·피해는 바뀌지 않는다.

시제품이다. 게임 안에서 한 층씩 켜고 끄며 확인할 수 있게 층마다 설정으로 끈다.

## 사용자 결정 (2026-10-15)

- 별도 애드온: 독립 DLL + ESL. HoM 본체는 건드리지 않고 `Data/SKSE/Plugins/SpellLearning/spell_catalog.json`만 읽는다.
- 불마법 판정: 카탈로그의 `elements`에 `fire`가 있는 주문. 카탈로그가 없거나 거기 없는 주문은 효과 중 하나의
  MGEF에 바닐라 `MagicDamageFire`(Skyrim.esm 0x01CEAD) 키워드가 있으면 불마법.
- 바꿀 범위: 몸에 붙는 불, 투사체, 폭발, 손 이펙트와 조명.
- 에셋은 직접 만든다 (회색 그라디언트 텍스처, 화상 셰이더 EFSH). Black Flame에 의존하지 않는다.

## 파일

| 파일 | 내용 |
|---|---|
| `HeartOfMagic_BlackFlame.esp` (ESL) | KYWD 표식, MGEF 버프, SPEL "흑염 달구기", EFSH 회흑색 화상 셰이더 |
| `SKSE/Plugins/HeartOfMagic_BlackFlame.dll` | 색 교체 |
| `SKSE/Plugins/HeartOfMagic_BlackFlame.json` | 층별 on/off, 색 값 |
| `textures/HeartOfMagic/BlackFlame/gradblackflame.dds` | 회색 팔레트 (가로 = 회색 입력값, 세로 행은 같음) |

## ESP 레코드

- KYWD `HoMBF_StokedKeyword`: 버프 표식.
- MGEF `HoMBF_StokeEffect`: Script 원형(스크립트 없음), Self, Fire and Forget, 파괴 학파, 키워드 위 표식,
  Hit Shader = 회흑색 EFSH (시전 순간 플레이어 몸에 잠깐 검은 불).
- SPEL `HoMBF_StokeSpell` 이름 "흑염 달구기": 일반 주문, 한손, 비용 고정 50, 효과 지속 60초.
  시제품은 콘솔 `player.addspell`로 준다.
- EFSH `HoMBF_BurnShader`: 바닐라 `FireFXShader`(Skyrim.esm 0x01B212) 값을 그대로 두고 막(membrane)·입자 팔레트만
  회색 그라디언트로 바꾼 것.

## DLL 동작

### 시작 (kDataLoaded)
- ESP 폼 찾기, 카탈로그 읽기 ("Plugin|0xLocalID" 키 → 런타임 FormID 집합), 회색 팔레트 텍스처 로드.
- 판정 결과는 주문별로 캐시.

### 버프 상태
- 플레이어에게 `HoMBF_StokedKeyword` 효과가 걸려 있는가. 확인 방법과 스레드는 API 조사 결과로 정한다.

### 층 1. 몸에 붙는 불
- 플레이어가 쏜 불마법 효과가 대상에게 걸리면, 다음 틱(SKSE 태스크)에 `ProcessLists::ForEachShaderEffect`로
  그 대상의 `ShaderReferenceEffect` 중 `effectData`가 그 MGEF의 hit shader인 것을 끝내고,
  `target->ApplyEffectShader(HoMBF_BurnShader, 남은 지속시간)`을 건다. 연속 시전(Flames)은 이미 붙은 우리 셰이더를
  다시 걸지 않는다.

### 층 2. 투사체
- 투사체 종류별 vtable의 `Handle3DLoaded`(또는 확인된 다른 시점)에 훅. 원본 호출 후, `shooter`가 플레이어 · `spell`이
  불마법 · 버프 중이면 3D를 다시 칠한다.
- 다시 칠하기: `BSEffectShaderProperty`마다 재질을 **인스턴스 전용으로 복제한 뒤** `greyscaleTexture`를 회색 팔레트로,
  `baseColor` RGB를 휘도 기준 회색으로(알파 유지). `BSLightingShaderProperty`는 emissive 색을 회색으로.
  입자 색 수정자(`BSPSysSimpleColorModifier`)의 RGB도 회색으로(알파 유지). 투사체 조명은 어두운 회색으로.
- 재질·속성이 다른 인스턴스와 공유되면 NPC 투사체까지 바뀐다. 반드시 복제해서 바꾼다.
- (구현 시 제외) `BSLightingShaderProperty` emissive와 입자 색 수정자는 "검토 반영"대로 건드리지 않는다. 입자 색은
  설정 `particleColors`(기본 끔)로만.

### 층 3. 폭발
- 폭발 레퍼런스의 3D가 로드될 때 같은 방식. 폭발을 일으킨 주체가 플레이어의 불마법인지 확인하는 방법은 API 조사로 정한다.

### 층 4. 손 이펙트와 조명
- 버프 중, 플레이어 손에 붙은 `ModelReferenceEffect` 중 시전 중인 불마법의 castingArt인 것을 찾아 같은 방식으로 칠한다.
  플레이어 시전 조명(MGEF의 casting light)도 어두운 회색으로. 시점은 API 조사로 정한다.

## 설정 (HeartOfMagic_BlackFlame.json)

```json
{ "burnShader": true, "burnMode": "replace", "projectile": true, "explosion": true, "handArt": true,
  "light": true, "forcePalette": true, "particleColors": false, "selfTest": false, "tintScale": 0.6,
  "lightColor": [0.25, 0.22, 0.28] }
```

- `burnMode`: `"replace"`(원래 hit shader를 끝내고 우리 것) / `"overlay"`(원래 것을 두고 위에 우리 것). 비교 시험용.
- `forcePalette`: 팔레트 플래그가 없는 이펙트 셰이프에 플래그를 켜서 회색 램프를 거치게 한다(Embers XD처럼
  플래그를 뺀 교체 모드 대응). 끄면 baseColor 탈색만.
- `selfTest`: 켜면 세이브 로드 후 한 번 자동 자가 시험(T0-T9)을 돌려 결과를 HeartOfMagic_BlackFlame_selftest.txt에 쓴다. README "Self-test".
- `particleColors`: 구현됨(기본 끔). CommonLib에 없는 `BSPSysSimpleColorModifier` 레이아웃(Jonahex 포크, AE 미확인)을
  써서 플레이어 3D 안의 색 키만 탈색. 공유 여부를 게임에서 확인한 뒤에만 켠다.

## 알려진 한계

- 불에 타 죽을 때의 주황 불꽃은 엔진 하드코딩이라 바꾸지 않는다 (Black Flame 제작자 주석).
- 바닥 그을음(데칼)은 바꾸지 않는다.
- 가산 혼합이라 새까맣지 않고 회흑색이다 (사용자 승인: "Black Flame 화상 셰이더 정도면 됨").

## 검토 반영 (Gemini 4 Argon 설계 검토, 2026-10-15)

- 버프 상태는 `std::atomic<bool>` 하나. 메인 스레드에서만 쓴다(효과 적용/제거 이벤트, 없으면 메인 스레드의 가벼운
  확인). `kPostLoadGame`/`kNewGame`에서 플레이어 활성 효과를 한 번 훑어 다시 세운다. 3D 로드 훅에서는 효과 목록을
  읽지 않고 이 플래그만 읽는다.
- 미뤄 실행하는 태스크는 `ObjectRefHandle`/`ActorHandle`만 넘긴다. 생 포인터 금지.
- 재질은 지오메트리마다 복제한 뒤 바꾼다. `BSPSysSimpleColorModifier`와 색 컨트롤러는 공유된다고 보고 건드리지
  않는다(설정 `particleColors` 기본 false, 게임에서 공유 여부를 확인한 뒤에만 켠다). 컨트롤러가 붙은 셰이프는 팔레트만 바꾼다.
- 폭발: `kDataLoaded`에 불마법 MGEF와 불 투사체가 가리키는 `BGSExplosion` 집합을 캐시하고, 그 폭발이면서 원인이
  플레이어일 때만 칠한다. 원인 필드가 없으면 층 3은 시제품에서 뺀다.
- 대상은 `SpellType::kSpell`만(HoM 시전 처리와 같은 기준). 지팡이·주문서·마법부여는 제외.
- HoM이 이미 훅하는 `ActiveEffect` 계열 vtable 0번은 다시 훅하지 않는다.
- 몸에 붙는 불: hit shader를 거는 시점에 효과(시전자·주문)를 알 수 있으면 거기서 셰이더를 바꿔 끼운다(Flames 깜빡임
  없음). 알 수 없으면 사후 교체를 쓰고 Flames 깜빡임을 한계로 적는다.
- 층 순서(확신 높은 것부터, 층마다 커밋): 0 플래그·설정·로그 → 1 몸의 불 → 4 손 이펙트·조명 → 2 투사체 → 3 폭발.
  칠할 때마다 주문 FormID를 담은 info 로그 한 줄.

## 알려진 한계 (추가)

- 바닥에 남는 화염(hazard)은 시제품에서 바꾸지 않는다.

## 확인 필요 (API 조사) - 정한 것 (2026-10-15 구현)

- 투사체: `Handle3DLoaded`(SE/AE vfunc 0xC0)를 Missile·Flame·Cone·Beam·Grenade·Barrier vtable마다 따로 훅
  (클래스별 원본 보관). 호출 스레드는 여전히 모름: 게임 스레드면 바로, 아니면 핸들로 게임 스레드 태스크에 미룬다.
  처음 3번 스레드 id를 로그에 남긴다.
- 폭발: 3D 로드 가상함수가 없어 `TESObjectREFR::Set3D`(0x6C)를 Explosion vtable에 훅. 주문 필드가 없어 기본 폼이
  불 폭발 집합(불마법 MGEF의 explosion, 그 투사체의 explosionType)에 있고 `actorOwner` 또는 `actorCause->actor`가
  플레이어일 때만 칠한다. 플레이어가 아닌 불 폭발 처음 5개는 소유자·원인을 로그로 남긴다.
- 재질: `Create()` → `CopyMembers` → 복제본 수정 → `SetMaterial(copy, true)`(속성 전용 비공유 사본을 엔진이 만든다,
  다른 플러그인 코드 근거) → 임시본은 소멸자 + `RE::free`. 속성이 실제로 무엇을 쥐었는지 한 번 로그. `SetupGeometry`는
  부르지 않는다(값은 그릴 때 읽힘). 텍스처는 `BSShaderManager::GetTexture`, `textures\` 접두사 경로 먼저.
- 조명: 투사체 `GetProjectileRuntimeData().light->light`, 폭발 `GetExplosionRuntimeData().light`, 손 `ActorMagicCaster::light->light`의
  diffuse를 `lightColor`로.
- 버프 상태: atomic 하나를 게임 스레드에서만 쓴다. 플레이어 대상 효과 적용/제거 이벤트, `kPostLoadGame`/`kNewGame`,
  티커(켜짐 100 ms, 꺼짐 1 s)에서 플레이어 활성 효과를 직접 훑는다(해제·비활성·만료 효과 제외).
- 손 이펙트: `ActorMagicCaster::Update` 훅 대신(DMMF가 통째로 교체) 티커가 손마다 `magicNode`, 1·3인칭 골격의 이름으로 찾은
  마법 노드, 시전 아트 `ModelReferenceEffect`를 칠한다. 어디에 셰이프가 있었는지 시전마다 로그.
- 몸에 붙는 불: 적용 시점에 hit shader를 바꿔 끼울 방법은 찾지 못해 사후 교체(+ Flames 깜빡임 한계). `hitEffects`가 비어 있으면
  다음 틱에 한 번 더 찾는다. 지속시간 없는 효과는 -1(영구) 대신 1.5초.
- 게임 안에서 확인할 것: plugins/BlackFlame/README.md "In-game test checklist".
