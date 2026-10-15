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
- 플레이어가 쏜 불 효과가 대상에게 걸리면, 그 효과 자신의 `hitEffects`에서 `effectData`가 그 MGEF의 hit shader인
  `ShaderReferenceEffect`를 끝내고(대상 전체를 `ForEachShaderEffect`로 훑어 끝내지 않는다: 같은 셰이더를 쓴 아군 것까지
  꺼진다) `target->ApplyEffectShader(HoMBF_BurnShader, 남은 지속시간)`을 건다. 이미 붙은 우리 셰이더는 다시 걸지 않고
  수명을 늘린다. 자세한 규칙은 아래 "1차 검증 반영".

### 층 2. 투사체
- 투사체 종류별 vtable의 `Handle3DLoaded`(또는 확인된 다른 시점)에 훅. 원본 호출 후, `shooter`가 플레이어 · `spell`이
  불마법 · 버프 중이면 3D를 다시 칠한다.
- 다시 칠하기: `BSEffectShaderProperty`마다 재질을 **인스턴스 전용으로 복제한 뒤** `greyscaleTexture`를 회색 팔레트로,
  `baseColor` RGB를 휘도 기준 회색으로(알파 유지). (초안: `BSLightingShaderProperty` emissive와 입자 색 수정자는 구현에서 뺐다, 아래 "구현 시 제외")
  입자 색 수정자(`BSPSysSimpleColorModifier`)의 RGB도 회색으로(알파 유지). 투사체 조명은 어두운 회색으로.
- 재질은 풀로 공유되므로 반드시 복제해서 바꾼다. 속성(`BSEffectShaderProperty`)은 인스턴스마다 따로라서 팔레트 플래그
  (`forcePalette`)는 속성에서 제자리로 바꾼다. 둘 다 NPC 투사체로 번지면 안 된다(자가 시험 T4, 로그 material check).
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
  않는다(설정 `particleColors` 기본 false, 게임에서 공유 여부를 확인한 뒤에만 켠다). baseColor 탈색은 모든 셰이프에 하지만,
  색 컨트롤러가 붙은 셰이프에서는 컨트롤러가 매 프레임 덮어쓸 수 있다(로그가 그런 셰이프 수를 센다).
- 폭발: `kDataLoaded`에 불마법 MGEF와 불 투사체가 가리키는 `BGSExplosion` 집합을 캐시하고, 그 폭발이면서 원인이
  플레이어일 때만 칠한다. 원인 필드가 없으면 층 3은 시제품에서 뺀다.
- 대상은 `SpellType::kSpell`만(HoM 시전 처리와 같은 기준). 지팡이·주문서·마법부여는 제외. 화상은 MGEF와 그 효과를 건
  주문을 함께 본다(3차 검증 반영).
- HoM이 이미 훅하는 `ActiveEffect` 계열 vtable 0번은 다시 훅하지 않는다.
- 몸에 붙는 불: hit shader를 거는 시점에 효과(시전자·주문)를 알 수 있으면 거기서 셰이더를 바꿔 끼운다(Flames 깜빡임
  없음). 알 수 없으면 사후 교체를 쓰고 Flames 깜빡임을 한계로 적는다.
- 층 순서(확신 높은 것부터, 층마다 커밋): 0 플래그·설정·로그 → 1 몸의 불 → 4 손 이펙트·조명 → 2 투사체 → 3 폭발.
  칠할 때마다 주문 FormID를 담은 info 로그 한 줄.

## 1차 검증 반영 (2026-10-15, 검증 에이전트가 코드·헤더로 확인한 항목만)

- 층 1: 회색 셰이더는 첫 시도에 바로 건다. 효과의 MGEF는 이벤트 안에서(효과가 아직 목록에 있을 때) 읽어 태스크에
  넘기므로, 즉시형 효과가 태스크 전에 사라져도 회색은 걸린다(그때 엔진 셰이더는 찾을 수 없어 그대로 둔다). replace 모드의
  엔진 셰이더 끄기는 그 효과의 `hitEffects`에서만, 비어 있으면 큐에 넣은 틱보다 나중 틱에 한 번 더. 다시 볼 때 효과가
  사라졌으면 엔진 셰이더는 남긴다(한계). 이미 붙은 회색 셰이더는 ProcessLists 잠금 안에서 `lifetime = max(lifetime,
  age + 초)`로 늘린다(`lifetime` < 0 은 무기한이라 건드리지 않음). ApplyEffectShader가 준 셰이더의 lifetime/age를 한 번 로그.
- 지속시간이 있는 효과(집중형 포함)는 살아 있는 화상 목록에 넣고, 틱마다 그 효과가 대상에 남아 있는 동안 회색 셰이더 수명을
  늘린다. 제거 이벤트가 오고 플레이어의 다른 불 효과가 대상에 없으면 그 회색 셰이더를 끝낸다. 즉시형은 자기 1.5초를 간다.
- 불 판정은 효과(MGEF) 단위: 불 주문의 MGEF 중 MagicDamageFire 키워드 또는 저항값이 화염 저항인 것. 카탈로그만 불이라 하고
  그런 효과가 없는 주문은 가장 비싼 효과 하나만. 화상과 폭발 집합 모두 이 기준. 카탈로그 항목은 "elements"가 배열일 때만
  판정에 쓴다.
- 폭발: `magicCaster->currentSpell`이 있고 불 주문이 아니면(지팡이·주문서 등) 칠하지 않는다. 없으면 기존 규칙(한계 README).
  Set3D 때 조명이 없으면 다음 틱에 핸들로 한 번 더 칠하고 그런 횟수를 로그.
- 재칠: SetFlags 직전에 속성의 재질을 다시 읽어 팔레트 텍스처가 없으면 실패로 센다. "이미 회색" 판정은 포인터 또는 경로(멱등성).
- 손 이펙트: `castingArtData.attachedArt`만 칠하고, 비어 있을 때만 이름으로 찾은 마법 노드, 마지막으로 ModelReferenceEffect.
- 입자 색 수정자: 쓰기 전에 키 값을 읽어 0..2 밖이거나 NaN이면 레이아웃 불일치로 보고 쓰지 않는다.
- 스레드: Heart of Magic의 ThreadUtils.h를 그대로 쓰고(복사본 삭제), 게임 스레드의 Windows id만 따로 찍는다. 장면 그래프를
  훑는 일(material check 포함)은 게임 스레드에서만. SKSE 메시지 처리와 티커 루프 본문은 try/catch. RecomputeBuff 태스크는
  한 번에 하나만 큐에.
- 로그: 칠하기·화상 줄은 주문·종류마다 초당 한 줄(생략 수 표시). info는 버퍼(plugins/Common.h 기본), 경고·오류·게임 로드·
  저장·새 게임·자가 시험 끝에 기록. 팔레트 텍스처와 손 노드 이름 BSFixedString은 의도적으로 해제하지 않는다(DLL 내릴 때 엔진이
  이미 정리된 뒤 해제되는 것을 피함).

## 3차 검증 반영 (2026-10-15, 2차 리뷰 3곳 지적 중 코드로 확인한 것)

- 화상 범위: 효과의 MGEF가 불이고 그 효과를 건 것이 불 주문(`kSpell`)일 때만. 같은 MGEF를 쓰는 주문서·지팡이·마법부여는
  제외. 카탈로그 밖 주문도 불 MGEF 규칙(키워드 또는 화염 저항)으로 판정. 가장 비싼 효과로 들어온 MGEF는 따로 두고 늘 주문
  판정과 함께만 쓴다.
- 이벤트 싱크의 스레드는 처음 3번 로그. 효과 목록은 게임 스레드일 때만 싱크 안에서 읽고, 아니면 태스크에서 읽는다(즉시형은
  그때 사라졌을 수 있고, 그 경우를 기록). 재시도는 큐에 넣은 뒤 꼬박 한 틱이 지난 다음.
- 세이브 로드·새 게임(kPreLoadGame, kNewGame)에서 화상 목록·재시도·폭발 조명 대기·손 상태를 비운다. NiPointer를 쥔 목록은
  힙에 두고 해제하지 않는다(DLL 내릴 때 엔진 객체를 건드리지 않도록).
- 집중형 시전도 살아 있는 화상으로 추적. 틱마다 ProcessLists에서 실제로 본 셰이더만 남기고, 효과가 끝났는데 대상에 플레이어의
  다른 불 효과가 없으면 그 회색 셰이더를 잠금 안에서 끝낸다. "다른 불 효과" 판정은 해제·비활성·만료 효과를 뺀다.
- 엔진 hit shader 끄기도 잠금 안에서(다른 쓰기와 같게).
- 재칠: SetMaterial 뒤 재질의 팔레트 텍스처 확인을 팔레트 플래그가 이미 있는 셰이프와 강제로 켜는 셰이프 모두에.
- 폭발: magicCaster는 폭발 자신의 NonActorMagicCaster(Explosion.h:87)라 플레이어가 나중에 쓴 주문을 가리킬 수 없다(Gemini 지적
  반박). 다만 currentSpell이 폭발 자신의 마법부여일 수 있어, 제외는 주문서·지팡이·불 아닌 주문일 때로 좁혔다.
- 손 이펙트: attachedArt에 이펙트 셰이프가 없으면 대체 경로로.
- 입자 색 키: 0..2 안이고 하나 이상 0보다 클 때만 색으로 보며, 처음 몇 개는 쓰기 전 값을 로그.
- 자가 시험: T5는 0.3초 이상 된 엔진 셰이더만 세고(두 번째 시전 기준 유예 삭제), replace 모드에서는 엔진 셰이더 목록이 비어
  있지 않아야 PASS. T5-npc는 검사(NPC hit shader가 남아 있어야). 빛은 T4의 살아 있는 투사체에서 나중 틱에 읽는다(T4-light).
  T7은 NPC 색 키를 훅 때 값과 T4 때 값으로 비교. 시전자는 시전마다 목표를 향하게 하고 거리를 다시 잰다. Finish가 모든 비로드
  종료에서 버프 해제와 배우 삭제를 보장.

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
- 손 이펙트 (1차 검증 반영에서 attachedArt 우선으로 바뀜): `ActorMagicCaster::Update` 훅 대신(DMMF가 통째로 교체) 티커가 손마다 `magicNode`, 1·3인칭 골격의 이름으로 찾은
  마법 노드, 시전 아트 `ModelReferenceEffect`를 칠한다. 어디에 셰이프가 있었는지 시전마다 로그.
- 몸에 붙는 불 (1차 검증 반영에서 첫 시도 즉시 적용으로 바뀜): 적용 시점에 hit shader를 바꿔 끼울 방법은 찾지 못해 사후 교체(+ Flames 깜빡임 한계). `hitEffects`가 비어 있으면
  다음 틱에 한 번 더 찾는다. 지속시간 없는 효과는 -1(영구) 대신 1.5초.
- 게임 안에서 확인할 것: plugins/BlackFlame/README.md "Self-test"와 "In-game checks the self-test cannot do".
