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

> **대체됨 (C6, 2026-10-17)**: 아래 "층 2"부터 "검토 반영"까지(층 2-4의 런타임 재색칠, forcePalette, particleColors, tintScale,
> 설정 표의 그 키들)는 v1 설계 기록이다. 지금은 사본(빌더가 회색으로 구운 폼)이 투사체·폭발·해저드·아트를 맡고, 런타임 재색칠은
> hitArt·handArt·boundWeapon 키 뒤(기본 끔)에만 남는다. 현재 동작은 README와 아래 "C6" 절을 본다.

### 층 2. 투사체
- 투사체 종류별 vtable의 `Handle3DLoaded`(또는 확인된 다른 시점)에 훅. 원본 호출 후, `shooter`가 플레이어 · `spell`이
  불마법 · 버프 중이면 3D를 다시 칠한다.
- 다시 칠하기: `BSEffectShaderProperty`마다 재질을 **인스턴스 전용으로 복제한 뒤** `greyscaleTexture`를 회색 팔레트로,
  `baseColor` RGB를 휘도 기준 회색으로(알파 유지). 투사체 조명은 어두운 회색으로. (emissive와 입자 색 수정자는 아래 "구현 시 제외")
- 재질은 풀로 공유되므로 반드시 복제해서 바꾼다. 속성(`BSEffectShaderProperty`)은 인스턴스마다 따로라서 팔레트 플래그
  (`forcePalette`)는 속성에서 제자리로 바꾼다. 둘 다 NPC 투사체로 번지면 안 된다(자가 시험 T4, 로그 material check).
- (구현 시 제외) `BSLightingShaderProperty` emissive는 건드리지 않는다. 입자 색 수정자는 설정 `particleColors`(기본 끔)일 때만
  플레이어 3D 안에서 탈색한다(구현됨, AE 레이아웃 미확인).

### 층 3. 폭발
- 폭발 레퍼런스의 3D가 로드될 때 같은 방식. 폭발을 일으킨 주체가 플레이어의 불마법인지 확인하는 방법은 API 조사로 정한다.

### 층 4. 손 이펙트와 조명
- 버프 중, 플레이어 손에 붙은 `ModelReferenceEffect` 중 시전 중인 불마법의 castingArt인 것을 찾아 같은 방식으로 칠한다.
  플레이어 시전 조명(MGEF의 casting light)도 어두운 회색으로. 시점은 API 조사로 정한다.

## 설정 (HeartOfMagic_BlackFlame.json)

```json
{ "burnShader": true, "hitArt": true, "projectile": true, "explosion": true, "hazard": true, "handArt": true,
  "light": true, "forcePalette": true, "particleColors": false, "selfTest": false, "tintScale": 0.6,
  "lightColor": [0.25, 0.22, 0.28] }
```

- `burnMode`: 없어짐(7차). 엔진이 hit shader를 만들 때 컨트롤러에게 묻는 자리에서 우리 것을 돌려주므로 맞은 자리마다
  셰이더가 하나뿐이고 덧씌울 것이 없다. 옛 설정 파일의 키는 무시하고 로그에 한 줄 남긴다.
- `hitArt`: 플레이어 불 효과의 hit art(예: 화염 망토의 불꽃) 3D를 칠한다(7차).
- `hazard`: 플레이어 불 해저드(바닥 불)의 공유되지 않는 지오메트리와 조명을 칠한다(7차).
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

## 4차 검증 반영 (2026-10-15)

- 재칠 실패(엔진 사본에 팔레트 텍스처가 없음)면 그 속성의 팔레트 색·알파 플래그를 끈다. 경로만 우리 것이고 텍스처가 없으면
  "회색"도 "이미 칠함"도 아니다.
- 가장 비싼 효과로 들어온 MGEF는 (주문, MGEF) 쌍으로만, 그 폭발도 그 주문이 폭발의 시전 주문일 때만 인정.
- 폭발 시전 주문은 화이트리스트: 없음, 불 주문, 마법부여(폭발 자신의 것; 무기 마법부여와 구별 불가).
- 세이브 로드·새 게임 때 세대 번호를 올려, 그 전에 큐에 들어간 태스크는 아무것도 하지 않는다.
- 즉시형 타격이 늘린 회색 셰이더는 그 몫의 나이(age floor)가 지나기 전에는 지속형 효과가 끝나도 끄지 않는다. 집중형은
  경과 시간으로 만료 판정하지 않는다. 끝난(finished) 셰이더는 어디서도 살아 있는 것으로 세지 않는다.
- 자가 시험: Finish는 상태부터 되돌리고 정리·요약을 따로 감싼다. 시험용 투사체는 Projectile::Launch(LaunchData, 가슴 높이에서
  몸통 방향, 엔진 각도 계산을 방향이 맞을 때만)로 쏜다. CastSpellImmediate는 발치를 겨눠 300 유닛 밖에서는 빗나간다.
  T5-npc는 T4 전부터 있던 셰이더와 플레이어 효과의 hit shader(화상 층이 기록)를 빼고 판정하며, 도착 시간을 기다린다.
  T4는 NPC를 먼저 쏘고 다음 틱에 판정한다. 엔진 셰이더 나이 기준은 0.5초.

## 5차 검증 반영 (2026-10-15)

- 자가 시험은 CombatUtilities::GetAngleToProjectedTarget을 부르지 않는다. 사용자는 AE 1.6.1170이고, 그 함수의 AE id(47286)는
  확인되지 않았으며 CommonLib 자신의 Projectile.cpp:290은 같은 함수의 AE id를 0으로 둔다. 틀린 id는 첫 발사에서 게임을 멈춘다.
  대신 직선 각도에 중력 낙하 보정: 이 로드 오더에서 MAG_ProjectileFirebolt01 중력 0.25, MAG_FireBallProjectile01 0.15, 속도 둘 다
  2500(MysticismMagic 덮어쓰기). 비행 시간 = 거리 / 속도, 낙하 = 0.5 x 686(유닛/초^2, 9.8 m/s^2 x 약 70 유닛/m의 근사) x 중력 x
  시간^2, 약 7-20 유닛(플레이어 Firebolt 약 12, 시전자 Firebolt 약 18.5). 발사마다 로그. `Projectile::LaunchSpell`로는 바꾸지
  않는다: CommonLib 구현이 RELOCATION_ID(46022, 0)에 닿아 AE에서 멈춘다(Address Library 감사, 2026-10-15).
- 갓 소환해 경계가 없는 배우의 GetHeight가 20 미만이면 고정 높이(플레이어 100, 사슴 60, 겨냥 +50)를 쓰고 로그.
- 팔레트 경로만 남고 텍스처가 없는 속성은 종착 상태: 플래그를 끈 채 다시 복사·탈색하지 않는다(매 틱 겹쳐 검게 되던 문제).
- 즉시형 타격의 바닥(age floor)에 걸린 회색 셰이더는 수명을 그 바닥으로 줄인다. 바닥 수도 살아 있는 화상 수에 넣어 티커를 유지.
- 로그 제한 맵은 512를 넘으면 초당 한 번 오래된 것을 지우고, 그래도 넘으면 비운다.
- 자가 시험 T5-npc는 플레이어 hit shader 기록을 기다린 뒤 판정하고, 기록된 포인터는 나이와 시각으로 같은 객체인지 본다.
  T7은 순서가 보장되지 않으면 판정하지 않고, T3의 탈색 전 키와 비교해 T4 이전의 오염도 잡는다.

## 6차 검증 반영 (2026-10-15, 첫 실행 전 마지막)

- 셰이더 동일성 판정: 자가 시험의 일시정지 누적 시간을 기록마다 남겨 경과 시간에서 뺀다(정지 중에는 셰이더 나이가 안 늘어난다).
  여유값은 기록된 나이를 넘지 않게 해서 기록 뒤에 같은 주소에 생긴 새 셰이더가 맞아떨어지지 않게 하고, 끝난 상태로 기록된
  셰이더는 끝나지 않은 셰이더와 같은 것으로 보지 않는다(화상 층이 끄고 난 뒤의 상태를 기록).
- 바닥에 걸린 회색 셰이더는 바닥에서 끝난다(무기한 수명은 바닥으로). 팔레트를 잃은 속성은 다음 로드까지 기억해 다시 칠하지 않는다.
- Main의 레이어 초기화와 자가 시험 중단, Finish의 해제·삭제는 각각 따로 감싼다.

## 7차 반영 (2026-10-15, 첫 게임 실행 AE 1.6.1170 이후)

- 투사체 층의 접근 위반: 재질 확인(주문 id를 맞게 읽음)과 손 이펙트가 동작했으므로 순회와 runtime data 오프셋은 정상으로
  본다. 남은 차이는 조명이다. `PROJECTILE_RUNTIME_DATA::light`는 `NiPointer<BSLight>`로 선언됐지만 폭발은 같은 자리에
  `NiPointLight`를 둔다. 투사체도 NiLight를 쥐고 있으면 `light->light`가 NiAVObject+0x48을 포인터로 읽는다(가장 유력한 원인,
  게임에서 미확인). 조명은 MSVC RTTI로 실제 형을 물어 읽고(`AsNiLight`, 투사체·폭발·해저드 공통), 각 단계를 SEH로 감싸
  처음 한 번 예외 코드와 모듈+오프셋을 남긴다(`SehGuard`).
- 스레드: `Handle3DLoaded`는 플러그인 로드 스레드(메인으로 추정)에서, kDataLoaded와 SKSE 태스크는 다른 스레드에서 돌았다.
  로그에 나란히 남기고 논리는 바꾸지 않았다.
- 화상 셰이더와 hit art는 `ActiveEffectReferenceEffectController`의 `GetHitEffectShader`(0x0D)·`GetHitEffectArt`(0x0C)에서
  처리한다(엔진이 hit shader·art를 만들 때 묻는 자리). 셰이더는 우리 EFSH를 돌려주고, art는 그대로 두고 3D가 생기면 칠한다.
  이벤트 경로(적용 태스크, 재시도, 살아 있는 화상, 나이 바닥, 새 hit shader로 찾는 대체 경로)는 지웠다. `burnMode` overlay는
  맞은 자리마다 셰이더가 하나뿐이라 뜻이 없어 없앴다. 엔진이 언제·몇 번 묻는지는 진단 줄(슬롯별 처음 20번)로 확인한다.
- hit art 대기 목록은 컨트롤러 주소, art, 대상 핸들을 함께 비교한다(해제된 컨트롤러 주소를 NPC 효과가 다시 쓰는 경우 대비).
- 해저드: Hazard vtable의 `Set3D`·`Initialize`. `ownerActor`가 플레이어이고 BGSHazard의 주문이 불이면 공유되지 않는
  지오메트리와 조명만 칠한다. `BSMasterParticleSystem` 아래는 모든 재칠에서 건너뛴다(같은 종류의 모든 이미터가 공유, NPC
  불까지 회색이 됨). 바닐라 바닥 불은 대부분 그런 공유 시스템으로 내보내는 AddOn 노드라 거의 바뀌지 않는다(로그가 셈).
  공유 입자를 어떻게 할지는 사용자 결정 대기.
- 룬: 이 로드 순서의 Fire Rune MGEF(`MAG_RuneFireFFLocation`, MysticismMagic이 덮어씀)는 `MagicDamageFire`·ResistFire라 불 효과다.
  투사체 `MAG_RuneFireProjectile02`는 Lobber(GrenadeProjectile, 훅 있음)이고 그 폭발 `MAG_ExplosionRuneFlame02`는 투사체의
  explosionType으로 불 폭발 집합에 들어간다(houseCARL 읽기로 확인). 게임에서는 미확인.
- 손에 든(준비된) 불마법도 버프가 켜지는 순간 회색, 꺼지면·로드하면 칠한 속성을 원래대로 되돌린다.

## 8차 반영 (2026-10-15, 리뷰 3곳 + 사용자 결정, 두 번째 게임 실행 전)

- SEH: `RunSehGuarded`는 noexcept가 아니다(C++ 예외가 호출자의 catch로 가야 한다). 접근 위반 등 결함 코드만 잡고, 한 번 결함이
  난 단계는 세션 동안 꺼진다(이후 호출은 세어 로그). 재질은 SetMaterial 전에 unique_ptr에서 소유권을 빼서, 결함이 나면 해제
  대신 누수가 된다. 복원 관찰자는 SetMaterial 바로 뒤에 부른다.
- 스레드: SKSE 태스크는 Main::Update의 SKSE 태스크 단계(메인 스레드, SKSE 2.2.8 Hooks_Threads.cpp)에서 돈다. 게임 스레드는
  SKSEPlugin_Load에서 넣은 첫 태스크가 찍는다(kDataLoaded는 초기화 스레드라 예전 도장이 아무것과도 맞지 않았다). 저장된
  스레드를 찍는 로그는 이름표를 단다(hookThread= 등). 공유 목록은 뮤텍스로 감싼다.
- 로드 때 버프 플래그와 플레이어 핸들 캐시를 지운다. 손 이펙트 목록은 우리만 쥔 항목을 매 틱 버리고, 시전 조명도 되돌린다.
- 귀속(공통 규칙): 소유자·발사자가 플레이어, 또는 ActorCause가 플레이어, 또는 NonActorMagicCaster의 blameActor가 플레이어
  (RemoteCast), 또는 스크립트 태그. 폭발은 소유자·원인이 정해진 뒤에 한 번만 판정한다.
- 불 판정(사용자 결정): 실제 불 효과(MagicDamageFire 또는 ResistFire)만 바꾼다. 카탈로그의 fire 태그만으로는 아무것도 바뀌지
  않는다(빨간 이펙트의 소환 등 오탐 제거). 불 해저드는 불 효과의 충돌 데이터·폭발·해저드 생성 원형에서 모은다.
- 결속 불 무기(활)와 그 화살(ArrowProjectile 훅), 빛 원형 불 효과의 조명. 스크립트가 놓은 불: PlaceAtMe·Cast·RemoteCast
  네이티브를 원본을 그대로 부르는 대리 함수로 바꿔 끼우고(BindNativeMethod), 부른 스크립트가 플레이어의 불 효과면 결과를
  태그한다. 특정 스크립트·모드·FormID 이름은 어디에도 없다. Vigilant 조사는 시험 사례로만 썼다.
- 소환수의 불은 포함하지 않는다(사용자 결정, 알려진 한계).
- 자가 시험: T5-second는 새 컨트롤러의 교체 기록을 기다린 뒤 800 ms 후 판정, T5-npc는 훅의 "유지" 기록을 직접 증거로,
  T3-remote는 XMarker의 RemoteCast(blame = 플레이어)를 본다.
- ESP는 이 브랜치에서 바꾸지 않았다(회색 복제 브랜치의 ESP를 병합 때 쓴다).

## 9차 반영 (2026-10-15, 3곳 리뷰 29672c9..b03cca7)

- 오염 방지: 스크립트 태그는 액터를 받지 않는다(NPC에게 건 Cast로 그 NPC의 불이 회색이 되거나, 놓인 소환수가 통째로 칠해지던
  문제). 놓인 레퍼런스는 바탕이 불 폭발·불 해저드이거나 보이기만 하는 종류(활성물·이동 정적·정적·아트 오브젝트)일 때만 칠한다.
- 네이티브 대리 함수는 원본이 NativeFunctionBase일 때만 감싸고, 같은 클래스에서 파생해 이름·매개변수·플래그를 복사한다.
  엔진의 NativeFunctionBase::Call이 우리 MarshallAndDispatch를 부르고, 거기서 원본 앞뒤를 본다(프레임 = 네이티브 자신의 것,
  결과 = a_resultValue). 활성 효과는 게임 스레드에서만 읽는다(VM 스레드는 VM 핸들만 남기고 태스크가 판정). 바뀐 것이 확인될
  때만 "replaced"를 남긴다. 새 게임에서도 다시 시도한다.
- 귀속에서 액터 원인(ActorCause)을 뺐다: 플레이어가 밟은 불 함정(바닐라 함정 주문은 Spell 형식에 불 효과)이 회색이 될 수
  있어서다. 남은 규칙은 소유자·발사자, 비액터 캐스터의 blameActor(폭발 자신의 캐스터, 비액터 발사자의 ExtraMagicCaster),
  스크립트 태그. T3-remote는 태그 경로를, T3-blame(INFO)은 blame 경로를 본다.
- 스레드: 결속 무기 목록은 틱 태스크가 만들고 투사체 훅은 FormID만 비교한다. hit art는 엔진 잠금 안에서 비교와 NiPointer
  복사만 하고 컨트롤러 확인은 잠금 밖에서 한다. 복원은 항목마다 감싸고 결함이 나도 꺼지지 않는다. 복원 세대 번호로 오래된
  틱 상태가 되써지지 않게 한다.
- 결속 무기는 자기 마법부여에 불 효과가 있어도 센다. 폭발·해저드의 준비 조건에 스크립트 태그를 넣고, 2초 넘게 판정되지 않은
  폭발은 처리됨으로 둔다.
- 빌드 메모: Skyrim이 13.6 GB를 쓰는 동안에는 PCH 가상 메모리가 모자라 -threads 2로 빌드했다.

## 10차 반영 (2026-10-16, 2차 리뷰 + 회색 교체 DLL 쪽)

- 폭발 대기 조건에서 원인을 뺐다. VM 스레드의 네이티브 호출은 호출자 핸들이 ActiveEffect 형일 때만 태그를 "보류"로 바로
  남기고, 게임 스레드가 판정해 확정하거나 버린다(태스크 없음, 시전보다 태그가 늦는 경합 없음). PlaceAtMe가 지연형이면 결과를
  읽지 않는다. 우리 대리 함수는 포인터로 알아보고, 다른 플러그인이 다시 묶으면 한 번 경고한다. 유지형 SEH 로그도 1, 10, 100에서만.
- 복원 세대는 교환 잠금 안에서 올리고, 복원이 시작된 뒤 들어온 재칠은 바로 되돌린다. 결속 무기 목록은 로드·버프 꺼짐에 비우고
  켜짐에 바로 채운다. 기록에 귀속 규칙을 남겨 T3-remote는 스크립트 태그일 때만 통과, T5-second는 (컨트롤러, 효과 id)로 본다.
  T0-wrap은 대리 함수를 원본과 비교한다.
- 회색 교체: 맵(같은 파일 이름 하나, --request 출력이 VFS로 대신함)을 kDataLoaded에 한 번 읽어 세션 동안 둔다.
  SpawnCollisionEffects의 호출 세 곳을 호출자 몸체에서 E8로 찾아 트램펄린으로 돌리고(이미 다른 플러그인이 돌렸으면 이어 붙임),
  플레이어의 불 투사체일 때만 스레드 창을 연다. 폭발은 Update 둘레에서 같은 창을 연다. 창이 열려 있는 동안 BGSImpactData의
  GetModel이 회색 사본을 돌려준다. 해저드는 Load3D 동안만 BGSHazard의 GetModel이 회색 사본을 돌려준다. 충격 창 안에서 만들어진
  해저드는 태그한다(소유자가 비어 있고 원인은 규칙이 아니므로). 폼은 바꾸지도 만들지도 않는다.
- 여덟 함수 id는 1.6.1170과 1.5.97 주소 라이브러리에 모두 있다(id 감사 도구, 2026-10-16: AE 44213 0x7ecd80, 44123 0x7e5cd0, 44205 0x7ec130, 43806 0x7ce260; SE 43022 0x7531c0, 42943 0x74c710, 43014 0x752620, 42638 0x7369b0; vtable 190905 0x17b7e18, 189083 0x1793ca8).
- 보류 태그는 훅 안에서 판정하지 않는다(VM 핸들 정책이 필요하고 훅은 엔진 잠금을 쥘 수 있음). 훅에서는 확정 태그만 세고, 보류 태그만 맞으면 투사체는 태스크로, 폭발은 기존 대기로 결정을 미루며, 충격 창과 해저드 Load3D 교체는 보류를 "플레이어 것 아님"으로 본다. 해저드 처리는 태스크라 거기서 판정한다.
- 주소 라이브러리 id는 Offset2ID로 먼저 있는지 확인하고 없으면 교체를 끈다(치명적 조회 실패를 피함). AE id는 1.7.104 코드와
  1.6.1170 id 정렬, SE id는 알고리즘 대응만이다. vtable 슬롯(GetModel 4, Load3D 0x6A, Update 0xA3)은 1.6.1170 .rdata에서 확인.
- kDataLoaded 회색 점검(PASS/FAIL)과 자가 시험 T0-grey, T6-swap, T6-npc-swap.

## 11차 반영 (2026-10-16, 3곳 리뷰 fde334c..5a19721)

- 대리 함수는 우리 스마트 포인터로 쥐고 바인딩이 된 것만 기록한다. 해저드 교체의 thread_local과 손 틱 세대는 RAII로 이전 값을
  되돌린다. 대체 오프셋은 커밋된 실행 메모리의 트램펄린 스텁(FF 25 00000000 + 주소)이나 SpawnCollisionEffects 자신일 때만
  잇는다. 충격 창은 저장·복원하는 bool이라 가장 안쪽 결정이 이긴다. 같은 호출 위치는 한 번만 걸고 FAIL로 남긴다. GetModel
  썽크는 try/catch로 감싼다.
- 자가 시험: T6-npc-swap은 기대가 없으면 INFO, 회색 점검은 맵 개수를 늘 남기고 교체가 켜졌는데 모델이 0이면 FAIL, 폭발 기대는
  explosion 설정도 요구, T3-swap(파이어볼트 충격), T6-hazard(INFO), T0-wrap은 "복사한 값"으로 표기.
- 보류 태그: 판정 중 호출자가 바뀌면 false, 살아 있는 태그는 보류 호출이 연장·덮어쓰지 않는다, 보류 태그를 남기자마자 판정
  태스크를 넣는다(그 전에 효과가 끝나면 태그 없음), 보류 태그가 붙은 레퍼런스의 스크립트가 다시 부르면 같은 호출자를 넘긴다,
  미뤄진 투사체가 플레이어 것이 아니라도 기록과 로그 경로는 탄다.

## 알려진 한계 (추가)

- 바닥 불(hazard)의 공유 입자(`BSMasterParticleSystem`)는 바꾸지 않는다. 소환수 자신의 불은 바꾸지 않는다. 플레이어가 밟은 불 함정은 바꾸지 않는다. 버프 중 만들어진 hit shader·art는 버프가 끝나도 그
  효과가 끝날 때까지 회색이다.

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

## 회색 복제본 결정 (2026-10-15)

- 문제: 불 충돌(impact)·바닥 불(hazard) NIF의 AddOnNode(BSValueNode, value = ADDN 인덱스) 입자는 인덱스마다 하나뿐인
  공유 BSMasterParticleSystem으로 나간다. 한 인스턴스만 다시 칠할 수 없어서(모두의 것이 바뀜), 회색 MPS 복사본을 새 ADDN
  인덱스(9100 + 원래)로 싣고, AddOnNode 값만 바꾼 충돌·바닥 불 복사본을 만든다. 플레이어 것으로 바꿔 끼우는 일은 DLL 몫(별도 작업).
- 기본 제공은 바닐라 코어만: Skyrim - Meshes0/1.bsa에서 만든 메시 14개, ESL의 ADDN 9개(0x804-0x80C), 맵 파일. Embers XD 같은
  교체 모드는 MPS 파일과 인덱스(Embers XD.esp 224-242)가 달라서 고정 목록으로는 못 덮는다.
- 설정마다: `tools/blackflame-greybuild --request`가 요청 파일(로드 오더의 ADDN 목록 + 불 충돌·바닥 불 모델)과 사용자의
  설치 폴더에서 회색 메시, 전체 맵, 필요한 추가 ADDN만 담은 `HeartOfMagic_BlackFlame_Local.esp`(마스터 없는 ESL)를 만든다.
  교체 모드 메시는 배포하지 않고 사용자 PC에서 만든다. 요청 파일은 나중에 DLL이 kDataLoaded에 쓰는 것으로 계획(지금은 손이나
  houseCARL 목록으로 만든다).
- 런타임 ADDN 생성(DLL이 IFormFactory로 BGSAddonNode를 만들어 TESDataHandler::addonNodes에 넣기)은 보류: CommonLib에 폼 구조
  (index 0x68, DNAM 0x78, addonNodes NiTPrimitiveArray 0xD20)와 BSParticleSystemManager의 인덱스 레지스트리는 있지만, 엔진이
  BSValueNode 값을 어떤 표로 ADDN에 잇는지, 그 표를 언제 만드는지, 상시 로드 MPS를 언제 짓는지, NiTPrimitiveArray에 안전하게
  붙이는 방법(CommonLib에 Add 없음)은 확인되지 않았다. 확인 전에는 생성 ESL이 확실한 길이다.
- 도구: nifly(GPL-3.0)는 빌더에만 정적 링크, DLL에는 안 들어간다. MSVC가 C++20에서 nifly의 비 const operator==를 모호하다고
  거부해 빌더만 C++17 + fmt.
- 게임 안에서 확인 안 된 것: 회색 MPS가 실제로 회색으로 그려지는지, 상시 로드 9109가 9처럼 동작하는지, 생성 ESL의 ADDN이 동작하는지.

## v2 스파이크 (2026-10-16, 사본 프레임워크, feature/black-flame-v2)

계획: 스크래치 spike-plan.md 개정 2, 설계 design-copyframework.md. 사용자 결정(2026-10-16): 선택 = 불 효과 OR HoM 카탈로그
"fire", 런타임 재질 재색칠 은퇴(사본이 없는 폼은 주황 그대로 + 한 번 로그, 인스턴스별 조명 색만 유지), 스파이크 먼저.

### 커밋 0 (스파이크와 무관한 수정)

- HitArt: (컨트롤러, 아트)로만 맞춘다. 엔진의 첫 GetHitEffectArt 호출은 SetTarget 전이라 대상이 null이고, 그 null을 저장해
  두면 살아 있는 ModelReferenceEffect(대상 = 플레이어)와 영영 맞지 않았다(Vigilant 불꽃 고리, 2차 실행). 주소 재사용은
  걷기 뒤 살아 있는 컨트롤러가 여전히 플레이어의 불 효과인지로 막고, 대상은 살아 있는 효과에서 읽어 로그에 쓴다.
- 선택 규칙: 불 MGEF가 있는 주문 OR 카탈로그 "fire" 주문(kSpell만). 카탈로그로만 불인 주문은 불 MGEF가 없으므로 그 주문의 모든
  효과를 불 효과로 친다(Vigilant 눈부신 화염 같은 Script 원형). 8b차 결정(카탈로그만으로는 아무것도 안 함)을 뒤집은 것.
- 런타임 재색칠 층(hitArt, projectile, explosion, hazard, handArt, boundWeapon)은 기본 꺼짐. 코드는 정식 빌드까지 남긴다.
  투사체·폭발·해저드 훅은 늘 건다(조명 색, 자가 시험 기록, 폭발 쪽 충격 창). 3D 재색칠만 각 키가 정하고 조명은 `light`가 정한다.
  자가 시험의 재색칠 판정(T3, T3-pointer, T3-tint, T4의 플레이어 쪽, T6, T3-remote, T8-cast의 T3 조건)은 키가 꺼지면 INFO
  "not judged"이거나 그 조건을 빼고 판정한다.
- ResolvePending: 찍어 둔 게임 스레드가 아니면 판정을 거절하던 줄만 지웠다. 태스크·티커 본문이면 어느 스레드든 판정하고,
  VM 핸들 정책 검사(HandleIsType, IsHandleObjectAvailable)는 그대로이며 처음 8번 스레드를 로그에 남긴다. 다른 게임 스레드
  관문(Handle3DLoaded, Set3D)은 S4 측정 결과를 본 뒤에 정한다.

### S4 스레드 측정 (게이트는 그대로)

- `ThreadTrace`: QueueTask 본문 전부와 v2 훅 본문이 (종류, 스레드 id, 시작, 끝)을 4096칸 고리 버퍼에 남긴다. 칸마다 seq를
  0으로 두고 채운 뒤 번호를 쓰는 락 없는 방식이라 읽는 쪽은 쓰는 중인 칸을 건너뛴다. 자가 시험 T-threads가 종류별 스레드와
  다른 스레드끼리 겹친 구간 수를 보고한다(기록 0개면 FAIL). IsOnGameThread 관문은 이 데이터를 읽은 뒤에 정한다.

### S1 사본 (빌더 --forms, DLL 채움)

- 요청(version 2)은 원본 폼(정의 플러그인|로컬 id, 종류, 모델, OBND, DATA 스칼라)을 나열한다. 사본 종류는 PROJ, EXPL, IPCT,
  HAZD만(IPDS 사본 없음: BSTHashMap 삽입이 필요해서, LIGH 사본 없음: 조명 색은 인스턴스별로 유지). 스파이크 요청은 houseCARL
  읽기로 손으로 만들었다(plugins/BlackFlame/requests/, 33개 폼).
- 빌더는 마스터 없는 ESL(HeartOfMagic_BlackFlame_Copies.esp)에 자리표시 레코드를 쓴다: 종류별 DATA 크기는 Skyrim.esm과 같고
  (PROJ 92, HAZD 40, EXPL 52, IPCT 24) 폼 링크는 모두 0, OBND는 IPCT 빼고, FULL·MODT는 쓰지 않는다. MODL은 원본 NIF의 완전 회색
  사본(효과 셰이더 전부 팔레트 + AddOnNode 회색 인덱스)으로, 경로는 meshes\heartofmagic\blackflame\copies\ 아래(--request의 값만
  바꾼 충돌 사본과 겹치지 않게). 같은 실행에서 --request 계획을 만들어 사본 모델의 AddOnNode 값도 회색 ADDN을 받는다.
- 안정 id: 사본 맵(HeartOfMagic_BlackFlame_copies.json)을 다음 실행이 다시 읽는다. 원본은 id를 유지하고, 새 원본은 쓰인 id 전부
  다음 번호, 빠진 원본은 retired로 남겨 그 id를 다른 원본에 주지 않는다(저장된 투사체·해저드가 다른 폼이 되지 않게).
- DLL은 kDataLoaded에만 살아 있는 승자에서 데이터 멤버를 복사한다(데이터 구조체는 바이트 복사, BSFixedString은 대입, 머즐
  플래시 모델은 SetModel). 두 번째 단계에서 사본이 있는 링크만 사본으로 돌린다(PROJ.explosionType, EXPL.impactPlacedObject와
  spawnProjectile, IPCT.hazard). 실패한 사본은 맵에서 빠지고 로그. 나중의 다시 채우기는 "바뀐 값 비교 후 고치기"로만(재구성 없음),
  스파이크에는 없다. 자가 시험 T0-v2가 살아 있는 원본과 다시 비교한다.
- 빌더 자가 시험 27개 추가(62 -> 89): 종류별 DATA 크기, 링크 칸 0, ESL 플래그·마스터 0·HEDR 1.70, 그룹 순서, 스칼라 위치,
  안정 id와 retired, 잘못된 요청 거절, 만든 NIF로 --forms 전체 실행. 생성 ESL은 별도 node 읽기 도구로도 확인(33개, 크기 일치).

### S2 교체 지점

- 발사 판정: 주문이 있으면 주문만으로(불 규칙, kSpell만: 사본 PROJ를 같이 쓰는 비화염 주문은 교체 안 됨), 주문이 없으면 플레이어의
  묶인 불 무기 목록에 있는 무기 출처만. 마법 부여 단독은 세지 않는다(kSpell 규칙이 지팡이·두루마리·마법 부여를 뺀다).
- Projectile::Launch(42928/44108) 첫머리 우회: 실행 중에 첫 16바이트를 읽는다. E9(다른 플러그인 우회, IDRC가 Launch를 건다)나
  FF 25면 그 목적지로 잇고, 아니면 위치 무관 명령(push, REX push, mov [rsp+disp8], sub rsp, mov rax/r11 rsp)만으로 5바이트 이상이
  될 때만 게이트웨이에 옮긴다. 그 밖은 바이트를 로그에 남기고 건드리지 않는다. 호출 동안 projectileBase를 사본으로, 끝나면
  되돌린다. 호출 뒤 새 투사체의 explosion(Launch가 범위>0이면 MGEF에서 가져옴: 파이어볼)을 사본으로.
- 폭발 생성(42657/43829) 첫머리 우회: 생성 구조체 +0x00 기본 폭발, +0x20 원본 투사체(44216), +0x28 주문, +0x38 MagicCaster
  (34413 자기 범위). 1.7.104 코드에서 읽은 배치이고 1.6.1170은 pdata 크기·unwind 같음으로만 확인, SE는 미확인. 그래서 포인터마다
  폼 종류나 vtable을 확인한 뒤에만 쓴다(배치가 다르면 아무것도 안 바뀜). 이미 사본이면 따로 센다(어느 장치가 했는지 구분).
- 충돌: GetImpactData(20408/20860) 호출을 SpawnCollisionEffects(43022/44213)와 폭발 충돌 함수(42679/43851)에서 E8 검색으로
  찾아 바꾼다. 사본 창 안에서만 충돌 사본을 돌려준다(IPDS 사본·해시맵 삽입 없음). 창은 기존 충돌 호출 위치(투사체 기본이
  사본일 때)와 Explosion::Update(폭발 기본이 사본일 때)에서 연다. 사본이 없는 사슬은 10차의 모델 창이 그대로 맡는다.
  43851은 1.6.1170과 1.7.104의 pdata 크기가 달라(0x135D / 0x139D) 고정 오프셋 대체를 두지 않았다.
- 장치마다 설정 키(copyLaunch, copyLaunchExplosion, copyExplosionCreate, copyImpacts, 기본 켜짐)와 카운터. 트램펄린 512바이트.

### S3 hit shader (E4)

- 조사 결과(scratchpad research-hitshader/notes.md): 엔진은 MGEF data.effectShader(+0x98, hitVisuals +0x120→+0x28, dual
  +0x108→+0x40)를 ActiveEffectReferenceEffectController vtable 0x0D(AE 34846 / SE 34049)로만 읽고, effect->flags(+0x7C)
  bit 1이면 0을 돌려준다(그 비트는 망토 콜백만 쓴다). Start(34761)와 ShaderReferenceEffect 생성자(34910)가 부른다. 그래서
  기존 0x0D 훅을 그대로 두고 새 교체 지점은 만들지 않았다. 2차 실행의 "20줄"은 우리 진단 상한이 저장 로드 때 "any" 줄로
  다 쓰인 것이었다(훅 실패의 증거가 아님). 마법 부여 셰이더(ApplyEffectShader 19872)는 범위 밖.
- 계측: 두 슬롯을 설정과 무관하게 늘 건다(교체·재색칠만 설정이 정함). 플레이어가 시전자인 호출은 결과와 상관없이 60줄,
  그 밖은 보여줄 것(엔진 답 또는 MGEF 필드)이 있을 때 20줄, 상한이 서로 따로다. 줄마다 flags 원값과 bit 1, MGEF와 그 필드,
  주문, 시전자, 버프, burnShader 폼, 엔진 답, 결과와 이유, 스레드. 결과별 카운터는 E4가 쓴다. 이 한 번의 실행으로 세 경우
  (엔진이 null을 줌 / 썽크에 아예 안 옴 / 우리 검사가 거절)가 갈린다.

### v2 자가 시험 (관찰이 목적인 단계는 0건이면 FAIL)

- T0-hooks: Launch·폭발 생성 우회가 걸렸는지(잇기 포함)와 찾은 바이트, GetImpactData 호출 위치 2곳. 원하는데(설정 켜짐, 그
  종류 사본 있음) 안 걸리면 FAIL. T0-pred: IsFireLaunch를 바닐라 소품으로(Firebolt 참, 아무것도 없음 거짓, Sparks 02DD2A 거짓).
  T0-v2: 사본 맵·채움 개수·사본마다 살아 있는 원본과 비교, 그리고 GetImpactData 썽크를 직접 불러(엔진 조회를 거쳐) 창이 닫히면
  원본, 열리면 사본.
- E1a 플레이어 파이어볼트(발사 기록이 사본, 훅 시점 3D에 팔레트 경로, 살아 있는 3D의 AddOnNode가 모두 회색 인덱스, 1.5초 안에
  충돌 사본 1개 이상), E1b 플레이어 파이어볼 폭발이 사본(어느 장치가 했는지 카운터로), E1c 시전자 사슴의 파이어볼을 플레이어
  것보다 먼저·2.5초 뒤·같은 프레임에(단계마다 발사·투사체·폭발을 원본·회색 없음으로 각각 1회 이상, C에서는 플레이어 폭발이 사본).
- E2 화염 흐름: 이 로드 오더에서 MAG_Flames는 Embershard 볼트로 바뀌어 있어서, 투사체가 flamethrower인 첫 주문(MAG_Flames, 아니면
  불꽃 정령의 Flames 07E881)을 400 앞 사슴에 매 틱 발사. 버프 중 1초, 흐름 도중 해제, 0.6초 더, 버프 다시. PASS: 버프 중 발사는 모두
  사본, 꺼진 뒤 발사는 원본, 버프 중 플레이어의 그 주문 TESHitEvent 1회 이상. 흐름이 이어졌는지(고유 투사체 수)는 INFO.
- E3 룬: T9 정리 뒤 마지막 단계. 버프, 300 앞 표지에 Fire Rune 05DB90 발사, 저장 전 기본·폭발이 사본인지, 버리는 슬롯
  HoMBF_SelfTest_E3에 저장(BGSSaveLoadManager::Save), 실행이 DONE까지 끝난 뒤 다음 틱에 그 저장을 불러온다(Load). kPreLoadGame의
  Abort는 우리 로드면 E3 상태를 지키고, kPostLoadGame에서 재개가 무장되어 플레이어 준비 3초 뒤 룬을 FormID로(안 되면 셀에서
  사본 기본으로) 다시 찾아 판정하고 "DONE E3"를 같은 파일에 덧붙인다. ESL을 뺀 경우는 수동 메모(INFO). 정리 단계가 더 이상 마지막
  단계가 아니라서 예외 시 정리로 가는 곳은 "T9"의 위치로 찾는다.
- E4: T5가 우리 EFSH를 대상에서 봤을 때만 PASS. 맞았는지(TESHitEvent)와 0x0D 결과 카운터를 INFO로, FAIL이면 어느 경우였는지를
  적는다.

### v2 id 확인

README "Ids and how each was checked (v2)" 표가 기준. 요약: 모든 id가 1.6.1170·1.5.97 Address Library 두 파일에 있다(iddb.js).
1.6.1170은 실행 파일 코드가 암호화돼 있어 1.7.104 코드를 읽고, pdata 크기와 UNWIND_INFO가 같으면 "1170 같음"으로만 적었다
(44108 0x7DA, 43829 0x2FA, 20860 0x80, 44213 0x7DA, 44216 0x632, 34413 0x7AE, 44123 0x4B1, 44205 0x2AE, 43806 0x49F). 43851은 다르다
(0x135D / 0x139D): 고정 오프셋 대체를 두지 않았다. SE 함수 안쪽은 확인하지 않았다(1.5.97 실행 파일 없음).

### v2 수정 A (3곳 리뷰 fixlist-v2a, 2026-10-16)

- V1 E3를 반자동으로 바꿨다(위 "E3 룬"의 자동 저장·불러오기를 대체). DLL은 저장도 불러오기도 하지 않는다. MO2 로컬 저장은
  __MO_Saves로 가서 경로가 맞지 않고, Save_Impl/Load_Impl 코드는 읽지 않았고, 이전 상태에는 기한이 없어 다른 저장을 판정할 수
  있었다. 지금 흐름: 룬 발사, 저장 전 확인, 표지 파일 HeartOfMagic_BlackFlame_e3.json(룬, 사본 기본, 셀, 게임 일수, 세션 nonce),
  "E3: quicksave now, then quickload that save" 한 줄. kPostLoadGame의 성공 플래그(SKSE가 data 포인터 값으로 넘김, dataLen 1)가
  참이고, 그 전에 kPreLoadGame을 봤고, 10분 안이고, 표지 nonce·셀이 같고 게임 시간이 요청 이후일 때만 판정한다. 그 밖은
  INFO "E3 not run (manual step skipped)". 성공·실패·예외·기한 초과·새 게임 모든 경로에서 정리(버프 해제, 룬·표지 제거)하고
  표지 파일을 지운다(지우지 못하면 로그, nonce가 달라 재사용되지 않음).
- V2 사본 사슬의 충돌 해저드가 플레이어 태그를 잃던 문제: HazardHook Queue와 HazardSwap가 충돌 창 외에 사본 창이 열렸거나
  해저드 기본이 사본이면 태그한다.
- V3 E1a의 AddOnNode 값은 투사체 훅 태스크가 3D를 검사할 때 기록에 넣는다. 3D가 없으면 검사 안 됨 = FAIL, 값이 없으면 INFO.
- V4 폭발 생성 썽크의 기록도 try와 Guarded 안. V5 플레이어 핸들이 0이면 누구의 효과도 플레이어 것이 아니다(시전자 없는 효과가
  0과 맞던 경우). V6 폭발 생성 우회는 SE에서 끈다(시그니처·배치를 1.7.104/1.6.1170에서만 봤음). V14 폭발 사본이 없으면 걸지 않는다
  (T0-hooks 기대와 일치). V7 사본 채움 전에 사본 폼이 이미 다른 항목 것인지 확인(둘째는 로그 후 제외, 두 맵은 일대일).
  V8 copyLaunch가 꺼지면 "사본 없음"을 세지도 로그하지도 않는다. V11 자기 범위 칸의 MagicItem이 주문이 아니면(두루마리 등)
  배치 거절이 아니라 따로 센다. V17 HasIds는 정렬된 id 목록을 한 번만 만든다.
- V9 재색칠이 꺼진 기본값에서 T4는 시전자 투사체가 원본 기본인지, 사본이 기대될 때(copyLaunch 켜짐, Launch 걸림, Firebolt 투사체
  사본 있음) 플레이어 것이 사본 기본인지로, T8-cast는 버프 없는 발사가 원본 기본인지로, T3-remote는 사본이 기대될 때 사본 기본인지로
  판정한다. 리뷰가 말한 "T6-remote"는 없는 단계다(원격 단계는 T3-remote 하나).
- V10 T-threads는 v2 훅 종류(Launch, 폭발 생성, 사본 창 안 GetImpactData)마다 기록이 1개 이상일 때만 PASS, 아니면 빠진 종류를 적은
  INFO. V12 T0-hooks가 재생 해독기를 고정 바이트 사례로 확인한다. V13 v2 단계의 자체 시계도 멈춘 시간만큼 민다, E2는 버프가 다시
  켜지지 않으면 적는다. V11 E1b가 자기 범위 교체, 거절, 주문 아님, 사본 없는 발사 카운터도 적는다.
- V15 빌더: id 배정과 레코드 값 검증을 먼저 하고, 출력 전부를 --out 옆 스테이징 폴더(<out>.staging)에 쓰고 다시 읽어 확인한 뒤에야
  --out으로 옮긴다. 실패하면 이전 출력은 그대로이고 스테이징은 지운다. V16 사본 맵이 같은 원본을 두 번(사본끼리, 또는 사본과
  retired) 담으면 거절하고, 새 원본의 id는 맵의 모든 id(retired 포함) 중 가장 큰 것 다음. 빌더 자가 시험 89 → 93(실패한 실행이
  이전 ESL·맵을 바이트 그대로 남김, 중복 거절 2개, retired 최고값 다음 id). TAKEALOOK 출력을 고친 빌더로 다시 만들어 사본 맵이
  바이트 그대로임을 확인했다.
- V18 README: E3 반자동, 스크립트가 놓은 불은 Launch 때 플레이어 연결(사수, 비난 액터, 확정된 태그)이 보일 때만 사본(발사 때 태그가
  아직 보류면 그 투사체는 원본으로 남는다), 사본은 순수 시각이 아님(OnHit akProjectile, 해저드 개수 제한)은 "Spike limits"에.
- 반박: Gemini "HitEffectHook CallNumber 맵에 잠금 없음"은 HitEffectHook.cpp가 뮤텍스를 잡는다(조정자 확인). "T6-remote"는 없는 단계.

### v2 수정 B (2차 리뷰 fixlist-v2b, 2026-10-16; 세 곳 모두 막을 일 없음, 버리는 저장으로 실행 가능)

- W1 E3 단계 본문을 try로 감싸 예외면 정리 후 FAIL. 단계 시간 초과나 예외로 끝나 무장 전에 멈춘 E3는 실행 종료(Finish) 때 이 세계에서
  정리한다(OnRunFinished, 로드로 인한 종료는 손대지 않음). 판정이 던지면 룬을 보호 아래 다시 찾아 정리에 넘긴다.
- W2 같은 저장 판정을 좁혔다: 게임 시간이 요청부터 하루 안. 재개 대기 중 kPreLoadGame이 오면 그 로드는 판정하지 않고 "not run".
  버프 해제는 E3가 이 세계에서 걸었거나, 자기 저장에서 룬을 FormID로 찾았을 때만(셀 검색으로 찾았거나 못 찾으면 해제 안 함).
- W3 모든 대기에 기한: 시작된 로드 5분, 로드 뒤 플레이어 준비 5분. 10분 초과 정리는 플레이어가 E3 셀에 있고 룬 핸들이 살아 있을 때만,
  아니면 남았을 수 있는 것을 적고 손대지 않는다.
- W4 원본이 사본 플러그인 안의 폼이면 거절(원본 == 사본 거절은 이미 있었음).
- W5 E1a: 값이 있을 때만 회색 판정, 값이 없으면 INFO이고 통과·실패 근거가 아님(코드가 그대로 말하도록). T8-cast는 재색칠이 꺼졌을 때
  T3 발사가 사본이었을 때만 판정, 아니면 INFO. T4·T3-remote는 재색칠이 꺼지고 사본이 기대되지 않으면(copyLaunch 꺼짐, 우회 실패,
  사본 없음) INFO.
- W6 폭발 생성 우회는 확인한 판에서만: 1.6.1170.0과 1.7.104.0(실행 파일 FileVersion으로 확인). 다른 AE 판과 SE는 끔.
- W7 빌더 Commit: 스테이징 파일을 먼저 모두 나열한 뒤 옮긴다. 이전 스테이징을 지우지 못하면 아무것도 쓰지 않고 중단. --out 끝의
  구분자를 정규화한 뒤 옆에 스테이징을 만든다(자가 시험 추가, 93 → 94).
- W8 문서와 주석: E3 정리 범위, 같은 셀, 같은 저장이 아닐 때 남은 것, 퀵세이브로 계속 플레이하지 말 것, "saves and reloads" 주석,
  SE 문구, T-threads는 설치된 훅 종류만 기대.

### v2 수정 C (2026-10-16 11:11 인게임 실행 뒤 fixlist-v2c)

- C1 시작 지연: 설정 `selfTestDelaySeconds`(기본 30, 0..600). 로드 뒤 플레이어가 로드된 셀에 있고 메뉴·로딩·일시정지가 아닌 시간만
  더해(틱 사이 0.5초 넘는 공백은 일시정지로 보고 세지 않음) 그만큼 지나야 첫 단계를 시작한다. E3가 퀵로드 뒤 판정하기 전에도 같은
  지연을 기다린다. 무장 줄에 지연을 적는다. 시작 못 한 채 끝나는 기한(5분)과 재개 대기 기한(5분)에 지연을 더했다.
- C6 멈춤 계측(동작 변경 없음): 감시 스레드가 1초마다 로그를 내려쓰고 자가 시험 기록 중에는 info 줄마다 내려쓴다. SKSE 메시지마다
  종류·스레드를 적는다. ThreadTrace는 본문 시작(START)과 끝(END)을 모두 남기고 열린 본문 표(128칸)를 둔다. 우리 뮤텍스는 모두
  TrackedMutex(소유 스레드·선언 위치). 로딩·일시정지가 아닌데 10초 동안 틱 본문이 끝나지 않으면 STALL 블록(마지막 64개 기록, 열린 본문,
  잡힌 뮤텍스)을 한 번 쓴다. 멈춘 게임의 스택은 `cdbX64 -pv -p <pid> -c "~*kb 30;q"`(README Known limits).
- C4 버프 해제 지연: 원인은 BuffState가 아니다(목록 걷기는 이미 kDispelled·kInactive·만료를 빼고, 제거 이벤트에서 재계산한다).
  11:11 실행에서 E2·T8·T9가 매번 살아 있는 Stoke 효과를 다시 찾았고 "buff flag off" 줄도 제거 이벤트도 없었다: Dispel이 남지 않았다.
  1.7.104 SkyrimSE.exe에서 읽은 것: ActiveEffect::Dispel(id 34061)은 +0x7C에 bit 18을 잠금 없이 읽고-쓰고(bts) 효과 갱신을 부른다.
  효과 갱신도 같은 필드를 같은 식으로 쓴다(0x5bcf8d `or [rbx+0x7C], 0x20`). MagicTarget::DispelEffect(id 34505)는 대상의 +0x10 bit 0
  (갱신 중)이면 바로 Dispel하지 않고 postUpdateDispelList에 미룬다(id 34551). 우리 작업은 여러 잡 스레드에서 돈다(T-threads).
  그래서 엔진 자신의 보호 경로인 DispelEffect로 바꾸고(SelfTestDispel.cpp), 효과마다 해제 전후 flags·갱신 중 여부·스레드를 적는다.
  0.5초에 아직 켜져 있으면 한 번 더 해제한다(T8 1초 한도 그대로). 경쟁이 원인이라는 것은 다음 실행의 로그로 확인할 가설이다.
- C5 47DBBD 사본 DIFFERS: 비교를 필드 단위로 바꿨다(구조체마다 멤버 표: 이름, 기대값(원본, 사본이 있는 링크는 그 사본), 사본 값;
  어느 멤버도 아닌 바이트는 따로). 채운 직후에도 비교해 다르면 채우기 결함으로 경고한다. 저장 불러오기·새 게임마다 RefreshCopies가
  다시 비교하고 다른 사본은 필드별로 적은 뒤 다시 채우고 링크를 다시 잇고 재비교한다. T0-v2가 그 결과를 INFO로 먼저 적는다.
  원본(MAG_ExplosionRuneFlame02)은 houseCARL로 확인: 다시 이을 링크가 없고 SkyPatcher가 바꿀 수 없는 형식이다. 그러니 차이는
  kDataLoaded 뒤 런타임에 원본이나 사본이 바뀐 것이고, 어느 필드인지는 다음 실행의 필드 줄이 말한다(아직 모름).
- C2 사본 기대값: 사본은 만들 때부터 회색이므로 T3·T6·T3-remote는 사본이면 "사본 + 살아 있는 3D의 모든 효과 셰이프에 회색 팔레트
  경로"로 판정한다(JudgeGrey, SelfTestJudge; v2i부터는 "사본 규칙": 팔레트 셰이더는 우리 팔레트 둘 중 하나, 나머지는 회색 기본색). 이미 회색인 것이 기대 상태다. T3-pointer·T3-tint는 사본이면 INFO. 재색칠 판정은 사본이
  아닐 때만. 관찰 없는 통과 금지: T3·T3-remote는 훅에서 3D를 본 기록이 있어야 하고, E1c는 시전자 투사체·폭발 3D에 효과 셰이프가
  있어야 "회색 아님"으로 통과한다. 사본 판정의 "모든 셰이프"는 11:11 실행 값과 맞다(T3 9/9, E1a 9, E1b 8/8).
- C3 조준: 목표를 550 앞에(폭발 반경 320 + 100 밖), T2가 목표가 멈출 때까지(높이 변화 2 이내 0.5초) 기다린다 - 플레이어 높이에
  생성돼 떨어지던 중에 T3가 쏴서 빗나갔다(T3 조준 z -4119, T4 때 -4419). LaunchAt은 목표 3D의 world bound 중심을 겨눈다.
  T5·E1a는 TESHitEvent(플레이어, 주문, 목표)로 맞았는지 확인하고 비행시간 + 0.8초 안에 없으면 다시 쏜다(최대 3번). 그래도 없으면
  FAIL "aim: no hit after 3 casts"와 위치 정보. E4는 T5가 맞힘을 확인했을 때만 판정. 시전자는 AI가 켜져 있어 3500~8400 떠돌았으니
  Spawn이 놓은 자리(높이는 T2에서 멈춘 높이)를 집으로 두고 매 시전 전에 200 넘게 벗어났으면 Actor::SetPosition으로 되돌린다
  (T4·T6-npc·E1c A/B/C). E1a 제한 시간은 8초(3번 시전 + 충돌 1.5초). T8은 두 번째 해제가 필요했으면 PASS 줄에 그렇게 적는다.
  시야(line of sight)는 확인하지 않는다: Actor::HasLineOfSight의 1.6.1170 id를 확인하지 않았다.

### v2 수정 D (리뷰어·Gemini·OCR 리뷰와 메인 스레드 조사, fixlist-v2d)

- D1 메인 스레드 큐: SKSE AddTask는 평소 플레이에서 BSJobs "Post process" 잡(39092 -> 36588)으로 작업 스레드에서 돌고, 메인 스레드
  쪽(36564 +0xB9C)은 일시정지 프레임에서만 돈다(research-mainthread). 그래서 우리 작업은 모두 MainThreadQueue로 보낸다:
  Main::Update(SE 35565 +0x748, AE 36564 +0xC26 / 1.7.99부터 +0xC38)의 E8에 write_call<5>. 프레임의 잡 대기(35409) 뒤라 그 프레임의
  잡이 모두 끝난 자리이고, 일시정지 여부와 상관없이 매 프레임 메인 스레드에서 돈다. 쓰기 전 확인: E8, 대상이 게임 코드나 트램펄린
  스텁, AE면 함수 끝까지 0x37(1.6.1170·1.7.104 pdata). 아니면 쓰지 않고 AddTask로 대체(로그). 썽크는 원래 호출 다음에 큐를 비운다
  (TrackedMutex 아래 교환, 잠금 밖에서 작업마다 try/catch·추적). 게임 스레드 표시는 큐의 첫 프레임이 하고 Main::threadID와 비교한다.
  투사체·폭발 훅의 "게임 스레드면 바로 처리" 경로를 없앴다: 훅은 기록하고 큐에 넣기만 한다. 사본 교체 우회(Launch, 폭발 생성,
  GetImpactData)의 포인터 바꾸기는 호출마다의 자료라 그대로. 큐 전체에 세대 확인을 두지 않은 이유: 틱 작업을 버리면 g_tickPending이
  남아 틱이 다시 안 들어온다. 세대 확인은 지금처럼 작업마다(세상에 묶인 작업만) 한다. T-threads는 큐 몸체가 모두 Main::threadID에서
  겹침 없이 돌았을 때만 PASS.
- D2 감시: GamePaused와 모든 RE 읽기를 뺐다. 멈춤 신호는 큐 프레임이 10초(로딩 중 120초) 없을 때. 하트비트는 큐에서 5초마다.
  열린 몸체 칸은 start 먼저 kind 나중(release), 읽기는 kind -> start -> thread -> kind 재확인. "recorded in all"은 몸체 수(END).
  틱 끝 표시는 스코프 가드. 감시 스레드는 atexit 플래그로 플러시 전에 멈추고, TrackedMutex는 소멸자에서 등록 칸을 비운다.
  남은 위험: 종료 순간 이미 시작된 플러시 한 번은 막지 못한다. README에 cdb 명령과 _NT_SYMBOL_PATH.
- D3 자가 시험: muzzleFlashModel GetModel()의 null을 strcmp·SetModel 전에 막았다(Gemini 차단 항목, GetModel은 가상 함수). 필드 비교의
  폼 포인터는 역참조하지 않고 폼 맵에서 값으로 찾는다(차이가 있는 필드에만, 전체 탐색). 해제는 DispelEffect만(직접 Dispel 대체 없음),
  주석을 "재진입 방지, 스레드 간 보호 아님"으로 고쳤다. T8·E2는 첫 해제가 남아야 PASS, 두 번째가 필요하면 FAIL. 조준: "AI는 멈춘
  뒤에 끈다"는 반박: 11:11 실행에서 목표는 AI가 꺼진 채로 300 떨어졌고 AI가 켜진 시전자는 3548~8443 떠돌았다. 그래서 AI는 3D가 생기면
  바로 끄고, 멈춤 판정은 높이만이 아니라 위치 전체(2 이내 0.5초), 그리고 계획보다 200 넘게 멀면 T2 FAIL. 다시 쏠 때 검색 기준을 그 발사로 옮긴다(E1a는 발사·투사체·충돌 카운터를 새로). T5는 TESHitEvent 확인(kHit)을
  기다리고, 우리 셰이더가 보이면 더 쏘지 않는다. T5의 셰이더·교체 기록은 t3Mark부터 그대로 찾는다(목표 쪽 기록이라 어느 발사가
  맞혔든 같다). E4 = 맞힘 확인 또는 셰이더 확인. 실패 줄은 첫 발사 이후 목표에 온 모든 TESHitEvent(cause, source, projectile)를
  적고, projectile이 원본이나 사본 투사체여도 맞힘으로 친다. 시전자 집 = 계획한 자리 xy + T2 때의 높이(O-L1 반박: T2 때 자리는 이미
  떠돈 자리일 수 있다; T2 줄에 그때까지 떠돈 거리를 적는다). RefreshCopies는 기대값
  구조체를 만들어 다를 때만 한 번에 쓴다(Refill). T8-cast는 "T3 grey (copy/recolour)"이고 버프 없는 발사는 늘 사본이 아니어야 한다.
  기한 메시지에 시작 지연을 넣고, 틱 공백 0.5초는 SelfTestCtx.h의 kMaxTickGap 하나로. SelfTestStepsLate.cpp는 574줄이라 나누지 않았다.
- D4 다음 실행 설정(복사본만, 재색칠 끔)은 메인 세션이 사용자와 정한다: 코드는 바꾸지 않았다.

### v2 수정 E (배포 전 마지막, fixlist-v2e)

- E1 T5: 셰이더를 본 뒤에는 다시 쏘지 않고 T5-second 발사 이전 맞힘만 센다. 맞힘 기록이 없어도 셰이더가 증거로 판정하고,
  판정 줄과 시간 초과 줄에 목표에 온 모든 TESHitEvent를 적는다. E2 T8·E2: 두 번째 해제가 살아 있는 효과를 찾았을 때만 "첫 해제
  실패"(0이면 INFO: 첫 해제는 남았고 플래그 재계산이 늦음). E2는 마지막 Check 하나에 합쳤다(같은 단계에 FAIL과 PASS가 함께 안 나옴).
  E3 시전자 집 높이: 200 안이면 T2 때 높이, 벗어났으면 생성 높이. E4 T8-cast: T3가 판정되지 않았으면 그 부분은 "not judged"이고
  버프 없는 발사는 그대로 판정. E5 큐: 설치됐는데 60초 동안 프레임이 없으면 STALL, 대기 10,000 넘으면 한 번 경고, 하트비트는 그
  프레임에 돈 작업 수, Main::threadID는 0인 동안 다시 읽는다. E6 큐가 없으면 재색칠·빛 쓰기 끔. E7 드레인을 추적하고 작업 스레드의
  훅 몸체(Launch, 폭발 생성, GetImpactData, Explosion::Update 사본 창)가 드레인 중에 시작하면 T-threads FAIL. E8 열린 몸체 칸에
  seqlock. E9 C++ 예외가 아닌 것을 던진 작업 종류는 그 세션에서 다시 돌리지 않는다. E10 E1a 카운터를 다시 쏘기 전에 찍고,
  TickHazards는 큐에 하나만. E11 README T-threads 줄, cdbX64(WinDbg 스토어 별칭, SDK는 cdb.exe), 주석 정리.

### v2 수정 F (Gemini 2차 리뷰)

- F1 T5는 T5-second 발사가 있으면 언제나 그 발사 이전 맞힘만 센다(T5-second는 셰이더를 처음 본 순간에 쏘므로 그 전에는 경계가 없다).
- F2 큐가 없으면 자가 시험을 무장하지 않는다(로그). 틱의 손·빛·위험물 작업은 E6에서 끈 설정이 이미 막는다(CasterArt·PlayerArt·
  HitArt·TickExplosionLights·TickHazards·ScriptTags가 handArt/boundWeapon/hitArt/light/hazard/explosion을 확인). hitArt 주석: 아트는
  바꾸지 않고 나중에 3D만 재색칠.
- F3 멈춘 종류는 이름 내용으로 비교, Post에서도 거른다(g_mutex 아래). 대기 플래그 해제 요구는 반박: g_tickPending·g_recomputePending·
  g_tickQueued는 각 작업 몸체의 첫 문장에서 풀리므로 예외로 남지 않고, 멈춘 종류는 다시 돌지 않으니 플래그를 풀어도 버려질 작업만 쌓인다.
  대체 경로(AddTask)에서 TaskInterface가 없으면 위험물 훅·버프 재계산은 플래그를 세우기 전에 그만둔다.
- F4 T-threads 겹침은 구간 교차(시작이 드레인 끝 이전이고 끝이 드레인 시작 이후).
- F5 열린 몸체 칸: 점유 플래그를 따로 두고 thread·start·kind는 홀수 구간 안에서만 쓴다, 해제 때 start도 0, 읽기의 seq 두 번 모두 acquire.

### v2 수정 G (리뷰어 2차 리뷰)

- G1 T-threads: 드레인 중 작업 스레드에서 들어온 작업 쪽 훅 몸체를 훅 입구에서 바로 센다(MainQueue::NoteJobSideHook, 세션 전체).
  링에는 작업을 돌린 드레인만 남긴다(매 프레임 기록하면 4096칸이 약 30초뿐).
- G2 시전자 집: 떠났으면 높이는 목표의 멈춘 높이, 집 거리 판정은 T2와 같은 바닥면 거리, 옮긴 다음 틱에 쏜다(PrepareCaster kWait).
- G3 Post는 bool: 멈춘 종류면 false, 부른 쪽이 대기 플래그를 푼다(틱·버프 재계산·위험물). 멈춤 줄은 틱이면 그 결과를 적는다.
  F3의 "플래그는 풀 필요 없다"는 틀렸다: 멈춘 종류의 다음 Post가 거절되면 플래그가 세워진 채 남아 틱이 세션 내내 죽는다.
- G4 /EHa 확인(CompilerFlags.cmake): catch(...)가 SEH도 잡는다. 작업을 Guarded로 감싸 코드·모듈+오프셋을 남기고 그 종류를 멈춘다.
  썽크의 바깥 catch는 한 번 로그. README 문구 수정.
- G5 큐가 없으면 재질 로그(3D 걷기) 끔, 위험물 큐 넣기가 던져도 플래그 해제, 첫 프레임 없음 STALL은 따로 문장, 하트비트는
  실제로 돈 작업 수, DisableEngineWrites는 LogConfig를 다시 부르지 않고 바꾼 키를 한 줄로.

### v2 수정 H (14:25 실행)

- H1 T2 멈춤: XY 5 단위, Z 5 단위를 따로 기준점에 대어 둘 다 0.5초. 8초가 지나도 안 멈추면 높이가 0.5초 버텼고 거리 제한(플레이어에서 750) 안이면 INFO로 진행, 아니면 FAIL.
  14:25 실행의 로그에는 틱마다 위치가 없다: T2 FAIL 때 (36804, 14, -4521), 0.36초 뒤 E4 때 (36800, -3, -4520) - 높이는 그대로, 바닥면으로 약 17 단위 미끄러짐.
  그 실행의 목표는 플레이어에서 약 826 떨어져 있어 750 제한을 넘으므로 새 규칙으로도 T2는 FAIL이다(거리 제한 그대로). T2 줄에 대기 중 움직인 거리와 높이 범위를 남긴다.
- H2 T1: 디스펠 2초 뒤에도 버프가 켜져 있으면 INFO로 그 버프를 안고 진행(buffWasTurnedOn 참, T8이 그 버프를 디스펠).
- H3 디스펠은 우리 버그: usUniqueID로 다시 찾았는데 대부분 0이라 플레이어의 첫 id-0 효과(무관한 것)를 디스펠했다(064e331부터). 이제 디스펠마다 키워드로 목록을 다시 훑고,
  디스펠 너머로 포인터를 역참조하지 않는다. T5-second의 한 번 맞음 = (컨트롤러, ActiveEffect 주소), HitEffectHook 로그도 주소. 이전 실행의 세이브는 버린다.

### v2 수정 I

- I1 T2 거리: 지형에 기대지 않는다. 멈춘 자리가 플레이어에서 바닥면 300 - 1200이면 통과(거리 기록), 안 멈추거나 그 밖이면 FAIL. 폭발 여유는 T2 조건이 아니고
  T6·T6-npc·E1b·E1c가 쏘기 직전 실제 거리로 본다(목표-플레이어, NPC 시전이면 목표-시전자도 반경+100). 모자라면 INFO로 그 단계만 건너뛰고 T6-swap·T6-npc-swap은 판정 안 함. T4(화염탄)는 여유 검사 없음.
  조준·비행 시간·기하는 원래 실시간 위치를 읽는다(LaunchAt, ConfirmHit, AimGeometry).
- I2 T1 제한 6초(옛 버프 기다림 3초), PASS 줄에 디스펠→꺼짐, 시전 시각.

### v2 확장: ARTO 미니 스파이크

- 빌더: 사본 종류 ARTO 추가(그룹 순서 PROJ, HAZD, EXPL, IPCT, ARTO). ARTO 레코드는 EDID, OBND, MODL, DNAM(artType 4바이트)이고 DATA 없음. 요청은 v2 그대로 type ARTO, data는 artType만.
  지도 v1 그대로(이 스파이크는 v3 요청·지도 v2 전에 따로 배포 가능). 자가 시험: ARTO 다시 읽기, 그룹 순서, 기존 맵에 ARTO 추가 시 기존 id 유지, artType 3 거절.
- DLL: 채움은 data(DNAM)와 boundData만, 사본의 모델 성분은 건드리지 않음. 원본에 MODS가 있으면 맵에서 뺌("alternate textures not copied").
  GetHitEffectArt(0x0C) 썽크가 플레이어 불 효과·버프 중·copyHitArt이면 사본을 돌려줌(사본이 있으면 런타임 재색칠 기억 안 함). 카운터 copied.
- 스파이크 요청: forms-takealook.json에 FXFireCloak01 02ACD7(맞은 자리)과 FireCloakHandEffects 036342(손) 추가. 맞은 자리 아트는 바닐라 Flame Cloak 계열을 골랐다:
  불 Cloak MGEF 30개 중 가장 많이 쓰는 아트이고 NIF가 느슨한 승자(Flame VFX Edit, SE 스트림)라 LE NIF 위험(Vigilant BSA)이 없다. 손 아트는 채움·비교(T0-v2)만 확인, 교체는 C4.
- 자가 시험 E6/E6-npc(SelfTestStepsV2Art.cpp): 데이터로 고른 불 Cloak 주문(발사형, 맞은 자리 아트에 사본)을 플레이어가 자기에게 → 2초 안 플레이어 대상 ModelReferenceEffect의
  artObject가 사본, 3D 로드, 효과 모양 전부 팔레트 경로, 훅 카운터 증가면 PASS. 사슴은 원본. 판정 뒤 바로 망토 디스펠(DispelSpell, Stoke 디스펠과 같은 방식).

### v2 확장 C1: 요청과 덤프

- FireSet: 불 Cloak MGEF의 associatedForm 주문을 SpellType과 관계없이 불 주문으로, 그 효과 전부를 불 효과로(고정점까지). 로그에 수.
- FireClosure: 시작점 (a) 불 주문의 불 MGEF, (b) 가장 비싼 효과(GetAVEffect) MGEF. 간선은 계획 §1 그대로, 맞은 자리 아트(hitEffectArt, RFCT 아트)는 불 MGEF에서만.
  같은 폼을 가장 비싼 효과 경로로 다시 만나면 한 번 더 걷는다(costliest 표시가 아래로 전파). 제외: 우리 플러그인, 사본 종류 아닌 placed object, MODS 있는 ARTO.
- CopyRequest: 지문은 매 시작 계산(바이트 해시), 요청·덤프는 writeRequest일 때. 덤프는 manifest 지문과 다를 때만, 바이트를 다시 읽어 해시가 같을 때만 쓴다.
  읽기 실패 NIF는 덤프에서 지운다(빌더가 낡은 파일을 쓰지 않게). 경로는 UTF-8 → wide 경로, 정규화된 경로는 드라이브·:·..가 없어 덤프 폴더 밖으로 못 나간다.
- 자가 시험 T0-request: 요청이 돌았는지만(수·지문·시간). 집합의 정확성은 request.json을 센서스와 대조해 판정(게임 1회 필요, 미검증).
- 지문 비용(매 시작 NIF 읽기)은 아직 측정 전: 1초를 넘으면 (path, totalSize) 공식으로 바꾼다(코디네이터 결정).

### v2 확장 C2: 빌더 --from-game

- --from-game: 문서 폴더(SHGetKnownFolderPath)의 Skyrim Special Edition / ... GOG 중 request.json이 새로운 쪽, VR 미지원. --in 없이 덤프만 읽는다. --out은 있는 폴더, 매번 같은 폴더.
- 경고하고 계속(--from-game만): 게임이 못 읽은 NIF·덤프 없음·파싱 실패 → 사본 MODL = 원본 경로, greyModel "", note. 회색 MPS 실패 → 그 회색 인덱스 빼고 원래 입자. ADDN 없는 AddOnNode 값 → 그대로.
  --forms(손으로 만든 요청)는 예전처럼 실패.
- 맵 v2(requestFingerprint, copiesPlugin, sourceMuzzleModel/greyMuzzleModel/note, excluded), v1 읽음. EDID HoMBF_C_<TYPE>_<id>. 머즐 모델도 회색 사본.
- id: 바닥나면 아무것도 안 쓰고 수(요청·이전·retired, 종류별)와 --reclaim-retired 안내. --reclaim-retired는 retired id를 낮은 것부터(새 세이브 전용).
- 커밋: 스테이징 후 파일마다 MoveFileExW(교체, write-through), --out 폴더는 지우지도 rename하지도 않음(meta.ini 보존). Local.esp 늘 씀(빈 것 허용). 빈 Copies/Local ESL 배포(--write-empty-esls).
- 오프라인 확인: 자가 시험 119/0, 손 요청을 v3로 바꾼 모의 덤프로 --forms 결과와 NIF 30개 바이트 동일·Local ESL·greymap 동일, 재실행 id 유지, LE NIF 3개 roundtrip 문자열 표 순서만.

### v2 수정 J (968f5b1..fbff1cc 3곳 리뷰)

- J1 폐포: 제외된 폼은 어떤 시작점에서 다시 와도 걷지 않는다(제외 집합). 정의 파일 없는 사본 종류 폼(런타임 생성)은 이유와 함께 제외.
- J2 T2 하한 = 폭발 반경(런타임) + 100, 상한 1200. SKIP 줄을 따로 세어 DONE에 skipped=N. T1 디스펠 우회와 폭발 여유 건너뛰기는 SKIP. T0-request는 NIF 실패·덤프 실패면 FAIL.
- J3 E6 제한 6초, 디스펠 뒤 2초 안 사본 아트 사라짐(E6-drop), 시전 전부터 있던 원본 아트·효과 셰이더 없는 사본 NIF는 INFO. T9가 E6 망토와 연결 주문을 high process 모든 배우에게서 디스펠.
- J4 망토 연결 주문의 효과는 g_fireEffects에만(폭발·해저드 집합에 넣지 않음).
- J5 사본의 모델이 게임 파일 시스템으로 열리지 않으면 그 사본은 맵에서 뺀다(모든 종류, 엔진은 원본).
- J6 DoSeek kUnsupported는 한 번 경고하고 계속, kBusy는 한 번 재시도. 경로 정규화: 금지 문자 거절, data\·meshes\ 반복 제거, DBCS 선행 바이트 뒤는 그대로. 덤프는 \\?\ 긴 경로, unchanged는 파일 존재·크기 확인.
- J7 writeRequest 기본 false(배포 json도), 꺼져 있으면 NIF를 읽지 않는다. 켜져 있으면 한 번만 읽고 바이트를 덤프에 쓴다. 요청에 fingerprintSizes(바이트 해시 없는 같은 공식), 빌더가 맵에 requestFingerprintSizes로 복사(C3 싼 검사용).
- J8 HAZD.data.spell: 그 주문의 불 효과 + 가장 비싼 효과만 시작점으로.
- J9 한계: 버프 중 만든 맞은 자리 아트는 사본 FormID로 저장된다(README). J10 FormCopiesBytes.h로 바이트 도우미 공유.

### 다중 런타임과 테스터 (2026-10-16)

- DLL 하나로 SE 1.5.97·AE 1.6.x·1.7.x(ENABLE_SKYRIM_SE/AE 켬, VR 끔). id 표는 README(tests/REL 7개 DB, 1.7.99 형식 5 읽기 추가), 1.6.640·1.7.104 DB는 없어 미확인.
- Main::Update 자리: SE 0x748, AE 0xC26, 1.7.99+ 0xC38(OAR·Precision·TDM), E8·대상 확인 어디서나, 꼬리 0x37은 1170·1.7.104에서만 강제.
- 폭발 생성 우회: 버전 관문 없앰, 디코더가 받는 런타임 전부. 썽크는 레지스터 인자 4개 전달·rax 반환, 기반은 BGSExplosion vtable 확인. 디코더 거절이면 로그·자가 시험 INFO.
- 고정 오프셋 대체(다른 플러그인이 바꾼 호출)는 확인된 빌드에서만: GetImpactData 44213+0xBC는 1170·1.7.104, 충격 창 자리는 SE 1.5.97과 1170·1.7.104. 그 밖은 스캔만.
- 모든 훅 기록(Patch::WriteCall/WriteVfunc/PrologueDetour → HookRecord), tester.txt에 설치·건너뜀·주소·바이트·이유, 사본·요청·자가 시험 요약. testerMode 키.
- 테스터 패키지 스크립트 Make-TesterPackage.ps1, README_TESTER.md. 생성 사본·비어 있지 않은 ESL·사본 맵은 넣지 않는다(스크립트가 거절).

### v2 확장 C3: 맵 v2, 머즐, 낡음

- DLL이 맵 v2를 읽는다(v1은 stale). PROJ 사본 머즐 = 맵 greyMuzzleModel(meshes\ 뗀 레코드 형태), 없으면 원본. Fill·Compare·Refill이 같은 기대값.
- 낡음: none(맵 없음·항목 0) / pluginMissing(Copies.esp 미로드 또는 항목 절반 넘게 사본 폼 없음) / stale(v1 맵, 지문 다름) / fresh. 지문은 writeRequest가 계산한 바이트 형태, 아니면 크기 형태(스트림 열기만, 바이트 안 읽음).
- 세션당 한 번 첫 불러오기·새 게임에 메인 큐로 SendHUDMessage(영어), 큐가 없으면 로그만. 사본 없는 폼은 폼마다 한 번 로그에 상태와 이유.
- tester.txt에 낡음 상태와 맵 버전.

### v2 확장 C4: 손 아트, 해저드 생성, PlaceAtMe

- SetCastingArt: 38784(SE 37830) 앞 0x200바이트에서 SetCastingArt로 가는 E8을 찾는다(pdata가 나뉘어 함수표 범위 스캔은 놓침), 정확히 1개. +0x91 대체는 1170·1.7.104만.
  썽크는 caster·art vtable 확인 뒤 PickCastingArtImpl(플레이어, 손, 선택 주문 불, art == 가장 비싼 효과 castingArt, 버프 중) → 사본. rax 전달. 플레이어 호출 40줄 계측.
- R-H(1.7.104): 34360은 create data를 스택에 만들고 43954 호출, 효과는 r13. 34360 첫머리 우회로 thread_local 창(SpawnHazardEffect vtable일 때만). 43954 우회: 기반 BGSHazard vtable·셀 vtable 확인,
  복사 창 또는 플레이어 불 spawn-hazard 효과면 사본, 호출 뒤 복원. 레지스터 인자 4개 전달.
- PlaceAtMe: 호출자가 지금 확정(확정 태그 참조, 또는 게임 스레드에서 플레이어 불 ActiveEffect)일 때 akFormToPlace(HAZD·EXPL·PROJ)를 사본으로(그 호출의 인자만). 대기 중이면 원본, 로그 1회.
- ImpactSwap/HazardSwap은 사본을 건너뛰라고 따로 고치지 않았다: 사본 모델은 copies\ 아래라 회색 맵에 없고, 원본 경로를 지닌 경고-계속 사본은 회색 맵 모델로 바뀌는 게 의도된 대체.
- 설정 copyCastingArt, copyHazardCreate, copyPlaced(기본 true). 트램펄린 사용 최대 274바이트(512).

### v2 확장 C5: 자가 시험 추가

- T0-fresh(fresh PASS, 그 밖 FAIL), T0-cover(폐포 사본 종류 폼마다 사본 또는 제외, 없는 것 처음 20개 FAIL; 주문별 가장 비싼 효과 PROJ·DUAL PROJ·EXPL·손 아트 INFO; 회색 없는 사본·쓰이지 않는 사본 INFO).
  폐포는 간선 비트를 기록한다(손 아트가 가장 비싼 효과에서 왔는지, 해저드가 불 아닌 spawn-hazard에서만 왔는지).
- E1m: 데이터로 고른 모드 불 주문(가장 비싼 효과 PROJ 사본, 파일 순서 맨 뒤), 공식 마스터뿐이면 INFO. 발사 → PROJ 사본, 폭발 사본이 있으면 1.5초 안 폭발 사본.
- E5: PickCastingArtImpl 직접 호출 5가지 경우, SetCastingArt 자리·세션 카운터 INFO. T0-hooks에 SetCastingArt·해저드 생성·spawn 창.
- 하지 않음: E6-reload(E3 저장·불러오기에 망토를 얹는 반자동 확인). 한계는 README에 적혀 있다.

### v2 수정 F (6bedbd7..3081385 리뷰)

- 반박(변경 없음): G1-1 XMM·스택 인자 손실 - 1.7.104에서 43829(인자 1), 43954(1), 34360(2), 44108(2) 모두 xmm·스택 인자 없음(verify-signatures.md). G2-2(1) SwapPlacedForm은 MarshallAndDispatch가 IsStoked를 먼저 본다.
  G1-2(2) Guarded는 결함 뒤 그 단계(교체 결정)를 끈다(SkipDisabled), 우회는 남는다 - 주석을 그렇게 고침.
- F1 SE: 폭발 생성·해저드 생성·spawn 창·SetCastingArt 자리를 SE에서 설치하지 않는다(SE id는 맞춤 시도뿐). 훅 목록에 SKIPPED, testerMode면 후보 주소 첫 32바이트를 tester.txt에. Launch 사후 쓰기는 SE에서도.
- F2 spawn 창: 34360은 GuideEffect(경로 표시)였다. AE 36885(1.7.104 0x666ed0, 48 89 5C 24 18, 0x66700b에서 43954 호출) 첫머리 우회로 옮김, SpawnHazardEffect vtable 확인 유지, 기반이 효과 MGEF의 associatedForm일 때만.
- F3 생성 데이터·창 복원은 범위 가드(SlotSwap, SpawnScope). F4 SetCastingArt: E8 앞 7바이트가 48 8B 90 E8 00 00 00이어야 설치, 썽크 레지스터 4개 전달, 주석(세 pdata 조각, 0xDA).
- F5 PickCastingArtImpl은 FireSet이 kDataLoaded에 만든 주문→가장 비싼 효과 castingArt 캐시만 읽는다. F7 id 없음도 훅 목록에. F8 남겨 둔 장치는 T0-hooks INFO.
- F6 카운터(해저드·spawn·SetCastingArt·PlaceAtMe)와 네이티브 래퍼 상태를 tester.txt와 T9 INFO에. F9 testerMode면 자가 시험 뒤에도 줄마다 기록.
- F10 알림: none 문구 따로, pluginMissing 문구에 모드 순서, 큐에 들어간 뒤에만 표시 처리, kDataLoaded가 확인을 못 했으면 알림 없이 로그. pluginMissing은 사본 없는 항목 절반 이상(1개 이상).
- F11 E1m: E1a 조준·재시전·일시정지 보정, aimed 주문만(룬은 표적 맞음으로 확인 안 됨), 엔진 규칙의 폭발(면적>0이면 MGEF, 아니면 투사체)과 그 사본 id로 판정, 로드 순서에 없는 파일 건너뜀, 제한 12초.
- F12 E5: 사슴 없음·다른 castingArt 없음은 not run(PASS 조건 밖), 다른 효과의 실제 castingArt로 확인. F13 패키지 스크립트: PS7, 커밋 안 된 트리 거절(-AllowDirty), 소스보다 오래된 DLL 거절.
- F14 README_TESTER: request.json·copies.json 회수, 사본 모드가 충돌에서 이기게, MO2 F5, 단계 번호, SE·다른 AE는 진단 실행. F15 README 장치 표·런타임 표·id 표(36885, SE 열 없음).

### v2 수정 G (3081385..ceb5c66 2차 리뷰)

- G1 T0-hooks: 의도적으로 남겨 둔 경우(디코더 거절, SE 미검증, SetCastingArt 스캔·바이트 확인)만 INFO, 1170·1.7.104에서는 원하는데 안 걸리면 FAIL, 트램펄린 부족·실행 불가 점프·id 없음은 어디서나 FAIL. spawn 창도 판정.
- G2 E1m: fire-and-forget만, 발사·사본은 첫 시전 표시부터(재시전이 첫 발사를 지우지 않음), 엔진 폭발 규칙(GetAVEffect의 효과 항목 면적>0이면 그 MGEF 폭발만, 없으면 기대 없음; 0이면 투사체 폭발), T0-cover 수도 같은 규칙. 제한 16초.
  Aim의 시계는 hitAt 하나뿐이고 재시전·대기는 Ctx::lastCast(SelfTest.cpp가 보정)를 본다 - 추가 시계 없음.
- G3 E5: 서로 다른 castingArt가 둘 이상인 불 주문을 먼저 고른다. G4 SE+testerMode: SE 후보에서 SetCastingArt 스캔·7바이트 확인을 읽기만 해서 tester.txt에(훅 없음).
- G5 tester.txt를 불러오기·저장 때도 다시 쓰고, 언제(UTC)·왜 썼는지, kDataLoaded 때는 래퍼 상태가 첫 불러오기 전이라고 적는다.
- G6 패키지 스크립트: DLL은 컴파일 입력 전부(src, CMakeLists, plugins 공용 헤더, spelllearning EncodingUtils·PluginNames·AsciiText), 빌더는 tools 소스보다 새것이어야 함. Fail은 stderr+exit 1.
- G7 README_TESTER: SE에서도 회색 맵의 해저드 모델은 적용. G8 재생 디코더 사례에 36885 첫머리(5). G9 항목 0개 맵은 따로 문구.
- G10 주석(장치 6: AE 36885, SE 미사용), 빈 바이트는 '-', SlotSwap은 a_slot && a_to, 폭발 썽크 catch에서 before도 비움.
  반박: CopySwapArt의 HexBytes는 Patch::HexBytes 안에서 IsExecutable을 먼저 본다(Patch.cpp HexBytes 첫 줄). SpawnThunk는 이미 a_effect 널 확인 뒤 ReadAt.

### v2i 팔레트 규칙 (원인 A1·A2, 2026-10-16)

- A2: 빌더가 모든 BSEffectShaderProperty에 팔레트 플래그와 GradBlackFlame.dds를 강제했다(356개 사본 NIF에서 367개 강제, 그중 132개는
  소스 텍스처 없음). 텍스처 없는 글로(Fire Rune·Fire Storm Glow:0), FXWhite 틴트 구(RedGlowAdd), 렌즈 플레어(PCoreCircularFlare)가
  평평한 회색 도형이 됐다. 이제 팔레트는 원본이 이미 팔레트로 텍스처를 매핑하던 셰이더(플래그 켜짐 + 소스 텍스처 있음, `UsesPalette`)에만
  들어가고, 나머지는 플래그·텍스처를 그대로 두고 기본색만 회색으로.
- A1(사용자 결정): 가산 셰이프는 어두운 색을 그릴 수 없다(1676개 중 1375개가 SRC_ALPHA->ONE). 가산 팔레트 셰이더(셰이프의 NiAlphaProperty:
  블렌딩 켜짐, 대상 블렌드 ONE)는 밝은 가장자리 팔레트 `GradBlackFlameAdd.dds`와 `--tint-add`(기본 0.9), 일반 블렌드는 GradBlackFlame.dds와
  `--tint` 0.6. 알파 속성 없는 셰이프는 일반으로 보고 NOTE 줄. 색 컨트롤러 키도 대상 셰이더의 배율을 따른다. 셰이프는 모두 그대로.
- 색 선택은 파일로: 경로는 고정, 그 자리에 놓인 DDS가 색이다(기본 Godskin; Black Blade·Messmer는 data/optional/palettes, 게임이 읽지 않음).
  나중 FOMOD 선택지가 사본을 다시 만들지 않고 바꿀 수 있다. `make-gradient.js <out> [default|godskin|blackblade|messmer]`, 이름 없으면 기존과 바이트 같음.
- 집계: 각 NIF 줄, 실행 요약(`shaders` 줄), 회색 맵 `tintAdd`, 사본 맵 `shaders`(정보용).
- DLL: LoadPalette가 가산 팔레트도 불러 로그에 남긴다(못 불러도 회색 팔레트와 무관, 경고). HasGreyMaterial·자가 시험 판정은 두 팔레트 경로를
  모두 받고, 사본 판정은 사본 규칙(`greyByRule`)으로. 런타임 재색칠(C6까지 남음)은 GradBlackFlame.dds 그대로이고, 가산 팔레트를 단 사본은
  "이미 우리 것"으로 건너뛴다.
- 첫 회 로그(사본마다 한 줄, 세션당, 최대 256키, `LogLimit::FirstTime`): 사본 투사체 첫 발사(만들어졌는지), 사본 투사체 첫 3D(사본 규칙
  집계와 처음 재질들 - 재질 점검 5회 상한과 무관), 사본 손 아트·사본 hit 아트 첫 교체. 23:0x 로그로는 Dragonfire 발사·3D 여부를 알 수 없던 문제.

### 원인 B 계측: Flame Cloak 사본 아트의 3D (scratchpad causeB-cloak/notes.md)

- 확인된 것: ModelReferenceEffect는 아트를 생성자(34653)에서 한 번만 읽고(훅이 번갈아 답해도 무관, 반증), 3D는
  BGSArtObjectCloneTask(효과 +0xC0)가 끝났을 때만 붙는다(task+0x80 non-null, state +0x0C == 5). Clone3D가 null이면 끝나지 않고 효과는
  3D 없이 산다. 남은 가설: 모델 로드 실패(H-B1), 느린 로드(H-B2).
- 감시(CloneWatch.cpp): GetHitEffectArt 훅이 사본 아트를 건넨 뒤 10초 동안 티커마다, 플레이어의 그 아트 효과의 클론 태스크를
  ProcessLists 잠금 안에서 읽는다(state +0x0C, pending +0x50, queue +0x70, art +0x78, 결과 +0x80; 오프셋은 디스어셈블리, 16880/16883은
  1.6.1170과 1.7.104 같음, CommonLib에 타입 없음). 엔진 호출 없이 평범한 읽기, 콜백 안 GuardedKeep, 태스크 포인터 null 확인. 값이 바뀔
  때만 한 줄(최대 12), artObject3D가 생기면 걸린 시간과 함께 한 줄, 10초에 3D 없으면 마지막 값과 한 줄. 감시 중에는 티커가 100 ms.
  훅이 엔진의 ProcessLists 잠금을 쥔 채 불릴 수 있으므로 감시 상태 뮤텍스를 쥔 채 순회하지 않는다.
- 사본 모델 탐침(CopyModelProbe.cpp, 원인 B 조사 causeB/ledger.md 뒤 넓힘): kDataLoaded 뒤 메인 스레드(우리 큐의 첫 프레임, 메인 메뉴)에서
  한 번, 채운 사본 전부(PROJ, EXPL, IPCT, HAZD, ARTO)의 레코드 모델과 PROJ 사본의 머즐 모델, 서로 다른 경로마다 BSModelDB::Demand(경로,
  out, {}) (CommonLib RELOCATION_ID(74040, 75782); 기본 ArgsType은 LoadGraphics의 RequestModel2 인자 {0, 3, 0x1010001}와 같음). 요약(성공,
  실패, ms), 실패마다 ErrorCode, 20초 예산을 넘으면 못 본 경로 목록. 핸들은 NiPointer reset. 경로는 레코드에 적힌 그대로(meshes\ 없음).
- 안전망: 사본 경로가 실패하고 같은 역할의 원본 경로(대조)는 불러지면 그 사본을 세션 동안 CopyOf에서 뺀다(DisableCopiesForSession: 한 번
  쓰고 atomic release로 공개, CopyOf는 acquire 뒤에만 읽음; byCopy/SourceOf는 그대로라 IsCopy는 참). 그 폼은 보이지 않는 대신 원본으로
  남는다. 대조도 실패하면(경로 해석이 틀렸거나 원본도 없는 경우) 빼지 않고 경고만 - 탐침이 통째로 틀려도 사본을 다 끄지 않게. 테스터 파일
  "copy NIF failed to load in game: N", T0-v2는 그런 사본마다 FAIL(경로, 오류).
- 빌더 헤더 문자열(GreyNif.cpp WriteKeepingStrings): nifly(vcpkg 680a3c0)의 Save는 FinalizeData 끝에서 UpdateHeaderStrings(false)로
  표를 지우고 아는 참조만 블록 순서로 다시 넣는다 - 고아 문자열이 빠지고 나머지 번호가 바뀐다. hasUnknown은 private이라 끌 수 없어서,
  Save 뒤 원본 표를 그대로(중복 포함: 자리표시로 칸을 만든 뒤 SetStringById) 되돌리고, 각 참조를 Save 전 번호로(그 칸이 아직 같은
  문자열일 때) 되돌린 다음, Save와 같은 순서로 직접 쓴다(헤더, 블록, 꼬리 1/0, 블록 크기 덮어쓰기). 결과 표가 원본 표로 시작하는지 검사.
  TAKEALOOK 요청: 무변경 재저장 동일 156 -> 489/513, 나머지 24는 NiNode 빈 자식 참조 제거(파일 전체로 4-48바이트 짧아짐, 이번 범위 밖).
- E6 kWatch 2초 -> 10초(E6-drop도), 단계 한도 6 -> 23초, 시전에서 3D까지 시간을 줄에, E6-npc는 원본 아트 3D를 10초까지 기다려 그 시간을 INFO로.

### C6: 런타임 재색칠 은퇴 (사용자 승인, scratchpad c6-gaps.md "권장 분할")

- 지움: 투사체·폭발·해저드 3D 재색칠 분기(훅 설치, 조명, 기록, 충격·사본 창, 해저드 태그는 그대로), 스크립트가 놓은 물체의 RecolorTree(놓인
  불 폭발·해저드의 조명만 남음, 시각 전용 종류는 더 이상 다루지 않음), particleColors 쓰기 경로(ParticleColors.cpp/.h 전부: 읽기 도우미를
  쓰던 T7과 함께), forcePalette(런타임 재색칠도 팔레트 플래그를 더하지 않음: 빌더 v2i 규칙과 같게), tintScale(남은 재색칠은 0.6 고정),
  자가 시험 T3-pointer, T3-tint, T7, JudgeGrey의 런타임 분기(사본이 아니면 "not judged"), 기록의 recolored/stats/recolorMicros.
- 키 뒤에 남김(기본 꺼짐): hitArt(해제 조건 E6·E6-npc PASS), handArt(실제 충전 시전에서 SetCastingArt 호출 > 0, SE는 SE id 확인),
  boundWeapon(묶인 무기 사본이 대신할 때까지; 사용자 결정 항목이라 이번에 지우지 않음). Recolor.cpp의 재질 코드는 이 셋이 함께 쓰므로
  남고, 셋이 갈 때 같이 간다(Recolor.h 머리 주석, README 설정 표).
- 반드시 남김: light, burnShader, impactSwap, hazardSwap, scriptPlaced, 세 훅의 설치·기록·창·태그, 묶인 불 무기 목록.
- 조명은 EffectLight.cpp/.h로 분리(AsNiLight, CheckedLight, TintLight): Recolor.cpp는 재질 + 읽기 도우미만.
- Config: 은퇴 키 목록(projectile, explosion, hazard, forcePalette, particleColors, tintScale, burnMode) - 예전 json은 키마다
  "retired, ignored"와 이유를 info로 한 줄, "not known" 경고 아님. 배포 json에서 여섯 키 삭제. DisableEngineWrites는 hitArt, handArt,
  boundWeapon, light만.
- README 254행 "needs `explosion` on" 정정: 폭발 쪽 충격 창은 impactSwap만 본다(ExplosionHook.cpp).
- 600줄 확인: 가장 긴 파일 SelfTestReload.cpp 573줄.

### 묶인 불 무기 1단계: 화살 (scratchpad bound-weapon-plan.md)

- 전제 정정: 폐포는 MGEF projectileBase를 이미 따라가서, 카탈로그 fire 주문의 묶인 활 화살(예: Vigilant 1258AD)은 이미 사본이 있었다.
  c6-gaps.md의 "화살 PROJ는 요청되지 않는다"는 일부 틀림(그 파일에 취소선과 정정).
- FireSet: kDataLoaded에 정적 씨앗(BoundFireSeeds). BoundWeapon 원형 MGEF 중 associatedForm이 묶인 WEAP이고, 런타임 규칙의 두
  갈래 중 하나로 불인 것: 불 주문의 불 효과(IsFireEffect), 또는 무기 마법 부여에 불 MGEF(HasFireEnchantment, PlayerArt에서 옮김).
  씨앗마다 그 효과를 가진 주문(불 주문 먼저)을 찾아 둔다(자가 시험이 시전). RefreshBoundFire는 씨앗 무기만 받는다: 런타임 목록 ⊆ 폐포.
- FireClosure: 새 간선 kEdgeBoundArrow 둘: 씨앗 MGEF의 projectileBase, 활·석궁이면 WeapTypeBoundArrow 기본 객체 키워드
  (DEFAULT_OBJECTS::kKeywordWeaponTypeBoundArrow, TESAmmo::AsKeywordForm)를 가진 모든 AMMO의 data.projectile. via는 씨앗 MGEF.
  과대 근사는 무해(Launch 교체는 플레이어 + 묶인 불 무기가 무기 출처일 때만). windows.h의 GetObject 매크로 때문에 FireSet.cpp에서 undef.
- 자가 시험 SelfTestStepsBound.cpp: T0-bound(씨앗마다 화살 PROJ 사본 / 제외 이유 / 없음 FAIL), E-bound-launch(씨앗 주문 시전, 목록
  반영 뒤 AMMO LaunchData 생성자로 발사, copyBase = 사본), E-bound-npc(사슴이 같은 탄·무기로 쏘면 원본), E-bound-off(T8 뒤 버프 꺼짐,
  원본, 그 뒤 주문 해제; T9가 다시 해제). T0-cover에 묶인 화살 간선 INFO.
- 빌더 변경 없음. 새 요청은 게임이 kDataLoaded에 쓴다: 지난 요청(10-16 21:42)으로 한 스크래치 재빌드에는 새 사본(예: 바닐라
  BoundArrowPROJ 10B0A5)이 나올 수 없다. 다음 게임 실행이 요청을 다시 쓴 뒤 빌더를 돌려야 한다.
- 2단계(무기·화살 메시, GetModel 창)는 게임 안 진단 실행을 기다린다. 진단 훅도 이번에는 넣지 않았다(세 vtable GetModel thunk +
  창 후보 둘이라 작지 않음).

### v2 수정 I (5d51a49..46855dd 리뷰, fixlist-v2i.md)

- I1 CloneWatch는 효과의 ActiveEffect::target(0x50)이 플레이어의 MagicTarget일 때만 무장(컨트롤러의 target 0x10은 첫 호출 때 아직 비어
  있음: HitArt.h 주석). 플레이어 주문에 맞은 NPC의 사본 아트가 한 칸뿐인 감시를 빼앗지 않는다.
- I2 효과가 사라진 줄과 Stop은 줄 상한과 무관. I3 줄은 g_mutex 안에서 만들고 풀고 나서 남김.
- I4 태스크 +0x78(아트)이 효과의 artObject(0xB8)와 다르면 "layout mismatch"를 한 번 남기고 세션 동안 태스크를 읽지 않음.
- I5 사본 경로 = 원본 경로(빌더가 원본 모델을 둠)면 대조 없이 유지, 이미 탐침한 경로 결과는 재사용.
- I6 탐침을 50 ms 조각으로 여러 드레인에 나눔(메인 메뉴가 멈추지 않음), 예산 20초는 조각 합. 대조도 예산을 따르고 예산 뒤 대조는
  "확인 못 함, 유지". 설정 copyModelProbe(기본 true).
- I7 DisableCopiesForSession 뒤 RefreshCopies("copy model probe")를 메인 스레드에서: 다른 사본이 떨어진 사본을 가리키던 링크가 첫
  로드가 아니라 바로 원본으로(LinkFor가 CopyOf를 쓰고, CopyOf가 떨어진 사본을 돌려주지 않음). HitEffectHook 낡은 주석 정정.
- I8 머즐 모델만 실패하면 사본은 유지하고 ExpectedMuzzleFor가 원본 머즐을 기대하게 함(RefreshCopies가 씀).
- I9 LaunchThunk·SetCastingArt의 첫 회/기존 로그 블록을 try 안에.
- I10 빌더: 원본에서 NIF_NPOS였던 빈 참조는 NIF_NPOS로(nifly의 AddOrFindStringId("", false)는 표에 빈 문자열이 있으면 그 번호를 줌).
  자가 시험: 표에 두 번 있는 문자열, 빈 표 문자열 옆의 NPOS 빈 이름(133 PASS).
- I11 빌더: 우리 바이트를 다시 파싱해 표가 원본으로 시작하는지 확인한 뒤에만 사용(아니면 nifly 바이트), 자리표시 경로 실패 시
  UpdateHeaderStrings(false)로 표 복구, 헤더 검사 실패한 출력 파일은 지움.
- I12 셰이프가 쓰지 않는 셰이더는 NOTE 없이 일반 팔레트; 가산·일반 컨트롤러가 공유한 키 블록은 큰 배율로, NOTE; 테스터 시작 줄은
  탐침 전이면 "not probed yet"; LoadPalette가 LoadTexture 사용; 탐침의 ModelOf 감싸기 제거(ModelPathOf 직접); LogLimit Once는 set.

### 사본 모델 탐침 비용 (사용자 피드백: 매 시작 메인 메뉴 최대 20초 멈춤은 배포에 치명적)

- 캐시: 완전한 탐침 결과를 `<SKSE 로그 폴더>\HeartOfMagic_BlackFlame\HeartOfMagic_BlackFlame_probe.json`에 씀(요청·덤프와 같은 폴더,
  VFS 밖). 키 = 사본 맵 바이트의 FNV-1a 64 + 크기 + 마지막 기록 시각(빌더를 다시 돌리면 바뀜) + 플러그인 버전. 떨어뜨린 사본과 되돌린
  머즐은 사본 ESL의 로컬 id로 저장(로드 순서와 무관), 시작 때 LookupFormID로 다시 찾음. 같은 키면 kDataLoaded에 바로 적용하고 탐침하지
  않음; 키가 다르거나 캐시가 없거나 selfTest·testerMode이거나 copyModelProbe "always"면 다시 탐침. 예산이 끝나 빠진 경로가 있으면 캐시를
  쓰지 않음.
- 조각: 50 ms -> 8 ms(60 fps 프레임의 절반 정도), 드레인마다 한 조각. 메인 메뉴에서 뒤에서 끝나고, 먼저 세이브를 불러오면 플레이 중에도
  같은 예산으로 계속(아직 안 본 사본은 켜진 채). 로그: 탐침 시간 합, 쓴 프레임 수, 가장 긴 조각, 시작부터 끝까지 시간. Demand 하나가
  8 ms보다 길면 그 조각은 그만큼 길다(한 NIF는 나눌 수 없음) - 그래서 가장 긴 조각을 남김.
- 설정 copyModelProbe: "off" | "log"(기본) | "drop", 다시 탐침은 copyModelProbeAlways. 테스터 파일 줄에 탐침 상태.
- 긴급 변경(01:13-01:23 실행, 46855dd): 탐침이 356개 중 347개를 "error code 1, node NULL, 0 ms"로 실패라 했고 안전망이 거의 모든
  사본을 빼서 대부분 주문이 원본으로 나갔다. 파일은 Copies 모드에 있고 탐침 없던 실행에서는 사본이 불러졌다(회색 Firebolt) -> 틀린 것은
  탐침의 요청 방식. 그래서 기본은 "log"(사본을 절대 빼지 않음), 캐시는 결과만 저장하고 "drop"에서만 적용. 실패 줄마다 사본과 원본(대조)의
  Demand 문자열을 그대로 나란히 남겨 경로 형식 문제를 한 줄에서 읽게 함. 원인은 별도 조사.

### v2 수정 J (46855dd..1f437b5 두 리뷰, fixlist-v2j.md)

- J1 "drop"에서 떨어뜨린 뒤 RefreshCopies는 메인 스레드 큐가 있을 때만(AddTask 대체 경로는 작업 스레드), 플레이 중이면 포인터·값만
  (이름·머즐 BSFixedString은 다음 kPostLoadGame 새로 고침). 캐시 적용은 kDataLoaded라 전부.
- J2 T0는 사본 모델 탐침이 끝나기를 최대 30초 기다림(T0 한도 35초), 아니면 INFO. T0-bound·T0-cover는 탐침이 뺀 사본을 "dropped by
  the copy model probe"로 따로 셈(FormCopies IsDroppedByProbe).
- J3 g_refreshNotes는 이유별로 교체(같은 이유만), 탐침의 새로 고침이 로드의 것을 지우지 않음.
- J4 조각이 결함으로 멈춘 탐침도 "probe stopped: ..." 상태로 끝나고 아무 사본도 빼지 않음.
- J5 E-bound: 시전 전 양손과 화살을 기록, 해제 뒤 다르면 ActorEquipManager로 다시 장착(주문은 EquipSpell, 무기·화살은 EquipObject, 손마다
  기본 객체 LeftHandEquip/RightHandEquip 슬롯), 결과 INFO.
- J6 화살은 T3와 같은 조준(SelfTestWorld의 AimAt를 LaunchAt와 새 LaunchAmmoAt가 같이 씀)으로 시험 표적 사슴에게. 표적 없으면 SKIP,
  아무것도 쏘지 않음(경비병 등 범죄 없음).
- J7 판정 뒤 시험 화살(플레이어 둘, 사슴, 버프 끔)을 LaunchRecord 핸들로 Disable + SetDelete, INFO로 개수. 저장에 사본 화살이 남지 않음.
- J8 SelfTest Finish의 늦은 정리(로드 중단 제외)도 CleanupArtSpells·CleanupBoundSpell을 각자 try로.
- J9 씨앗 주문 대체 후보는 SpellType kSpell만.
- J10 README E-bound 행에 E3와 같은 저장 경고.
- J11 CloneWatch: 태스크 +0x78이 0이면 "아직 안 채워짐"(다음 틱 다시), 진짜 불일치는 따로 Describe.
- J12 C6 전 낡은 문장 정리(README 층 표·스레드·T4·T3-remote·T8-cast·해저드 한계, greymap.json, Tint.h, SelfTestStepsRemote.cpp, SelfTestCtx.h),
  이 문서 층 2-4 절에 "대체됨" 표시.
- J13 README 알려진 한계: 런타임 묶인 목록은 정적 씨앗으로 제한(런타임에 MGEF associatedForm을 바꾸는 모드는 못 덮음).
- J14 배포 json에 copyModelProbe "log".

### 탐침 실패의 원인: 느슨한 파일 색인 캐시 (scratchpad probe-fail/notes.md)

- CRDW AE(Nexus 186434)가 느슨한 파일 탐색(70050)과 존재·크기 질의(70164)를 걸어 2026-10-16 11:13에 만든 캐시로 답한다. MO2 플러그인의
  지문은 모드·플러그인 목록만 봐서, 켜진 모드 안에 나중에 쓴 사본 파일은 모델 DB가 모른다(Demand 오류 1). 성공한 9개는 그 캐시에 있던
  옛 파일이다. 평범한 스트림(BSResourceNiBinaryStream)은 그 훅을 거치지 않아 J5 검사는 통과했다. Dragonfire·Incinerate·Flame Cloak이
  안 보인 것도 같은 원인일 가능성이 높다(엔진의 LoadGraphics가 같은 조회를 씀).
- 탐침: 실패한 Demand마다 BSResourceNiBinaryStream("meshes\\" + 경로)이 열리는지 함께 남기고, "열림 + 오류 1"은 "파일은 있으나 게임의
  파일 색인이 모름"으로 따로 셈. 하나라도 있으면 화면에 한 번(플레이 중이면 바로, 아니면 첫 kPostLoadGame/kNewGame): "Black Flame: N grey
  copies are not visible to the game's file index (...) - rebuild that cache.", CRDW.dll이 올라와 있으면 CRDW를 이름으로. 로그와 테스터
  파일 "file index" 줄에도.
- 캐시: 실패가 있던 결과는 다시 쓰지 않고 다시 탐침(원인이 사본 밖, 예컨대 색인 캐시일 수 있음).
- 문서: README 알려진 한계, README_TESTER 3단계, 빌더 마지막 줄 "느슨한 파일 캐시 모드를 쓰면 빌더 뒤 그 캐시를 다시 만들 것".
- 기본 모드는 "log" 그대로.

### v2 dev automation (580e315 이후)

- 목적: 사람이 게임을 만지지 않고 자가 시험을 끝까지 돌린다(MO2로 실행만 하면 됨). 개발용 설정 키 셋, 기본 꺼짐, 배포 json·테스터 패키지에 없음, `selfTest`(testerMode 포함)가 켜졌을 때만 적용(Config.cpp GateDevKeys). 코드는 DevAutomation.cpp/.h.
- `devAutoLoad` "세이브 이름(확장자 없음)": 세션에서 메인 메뉴가 처음 열리고 5초 뒤 메인 스레드에서 BGSSaveLoadManager::Load(이름, checkForMods false). 세션당 한 번, 그 전에 다른 불러오기가 시작됐으면 하지 않음. checkForMods false는 빠진 콘텐츠 창이 클릭을 기다리지 않게.
- `devAutoE3`: E3가 저장·불러오기를 요청하면 3초 뒤 Save("HoMBF_E3"), SKSE kSaveGame 뒤 3초(신호 없으면 10초) 뒤 Load("HoMBF_E3"). E3 판정(표지 nonce·셀·게임 시간·preload·10분 창)은 그대로 - 플레이어의 퀵로드와 같은 길.
- `devAutoQuit`: 본 실행의 DONE(E3가 걸려 있으면 DONE E3) 10초 뒤 메인 스레드에서 Main::quitGame = true. 마지막 불러오기(또는 자동 불러오기 요청, devAutoLoad가 있으면 kDataLoaded) 뒤 6분 안에 끝나지 않으면 TIMEOUT을 남기고 종료.
- 스레드 하나(250 ms 폴링, 분리)가 대기를 재고, 엔진 호출은 모두 MainQueue::Post. 쓰는 id(516860/403340, 34818/35727, 34819/35728, 516943/403449)는 Patch::HasIds로 먼저 확인, 없으면 꺼짐.
- 게임 안에서 확인할 것: SKSE의 kPreLoadGame이 Load_Impl 경로에서도 오는지(안 오면 E3가 "preload not seen"으로 not run), 메인 메뉴 프레임에서 Load가 도는지.

### 텍스처 사본 (texture copies, T1, 2026-10-17, feature/black-flame-tex)

- 문제: 빌더는 셰이더 값(팔레트, 기본·발광 색)만 바꾸고 텍스처 그림은 그대로 둔다. 그래서 텍스처에 칠해진 색(조명 셰이더 메시의 주황 diffuse·glow 맵, 팔레트 밖에 남긴 이펙트 셰이더의 source 텍스처)은 주황으로 남는다. 덧붙여 코드상 조명 셰이더는 GreyEffects가 세기만 하고 emissive 색은 바꾸지 않는다(회색 glow 맵 × 주황 emissive = 여전히 주황일 수 있음, 남은 문제).
- 고르는 규칙(NIF 데이터만, 어떤 로드 오더든): 전부 회색 사본 NIF와 회색 MPS의 BSLightingShaderProperty 텍스처 세트 슬롯 0(diffuse)·2(glow), UsesPalette가 아닌 BSEffectShaderProperty의 source 텍스처. 우리 텍스처(`textures\heartofmagic\blackflame\`)는 건너뜀. 0단계 밉의 보이는 픽셀 평균 HSV 채도가 kGreyscaleSaturation(0.05) 미만이면 이미 회색이라 둠(BC1 양자화만으로 약 0.02). 한 채널 형식(BC4·R8·A8)도 둠. 두 채널(BC5·R8G8)은 파랑이 없어 회색을 담을 수 없으니 실패로 둠.
- 다시 칠하기(빌더만 DirectXTex, MIT): BCn은 원본 색공간 그대로 8비트 RGBA로 풀고, 픽셀마다 Rec.709 휘도(빌더의 Luminance) × 역할별 tint, 알파 유지, 원본 형식·크기·밉 수·배열 크기 그대로 다시 압축(BC7은 TEX_COMPRESS_BC7_QUICK). tint: diffuse 0.6(조명 셰이더는 그 밖에 손대지 않으니 어둡게 하는 몫), glow 1.0(밝기 마스크), 이펙트 source 1.0(기본 색이 이미 tint를 가짐). 텍스처마다 한 번, 다른 역할로 다시 나오면 첫 사본을 씀(TEX NOTE).
- 출력: `--out`의 `textures\heartofmagic\blackflame\copies\<textures\ 뒤 경로>`(스테이징 뒤 커밋), 슬롯은 `textures\`를 앞에 붙인 그 경로(kPalettePath와 같은 꼴). 원본은 쓰지 않음. 없거나 못 읽거나 실패한 텍스처는 슬롯을 그대로 두고 NIF를 실패시키지 않음. 끝에 `textures  recoloured N, skipped greyscale G, failed F, not in the dump M; R of S slots ...; copies X MiB`. `--no-textures`로 끔. `--map`/`--request`에는 없음.
- DLL(writeRequest 켬일 때만): 잘 읽힌 NIF 바이트에서 `.dds`로 끝나고 앞 4바이트 길이와 정확히 맞는 문자열을 모두 찾고(NifDump::TexturePathsIn, NIF 파서 없음), 모델 경로와 같은 규칙에 `textures\` 루트로 정규화, ProbeModel로 크기만 열어 경로 순으로 상한(한 장 64 MiB, 합 768 MiB) 안에서 계획. 바이트는 덤프를 다시 쓸 때만 읽음. 요청 지문 공식은 그대로(플레이어 신선도 검사와 공유), 매니페스트에 `textureFingerprint`(경로·크기·계획)를 따로 둠. 예전 매니페스트는 한 번 다시 덤프. request.json에 `textures`(path, size, ok, error)와 `textureCaps`, 요청은 덤프 뒤에 씀. 텍스처 목록이 없는 예전 요청이면 빌더가 알리고 텍스처 없이 빌드.
- 비용: 2026-10-16 덤프(NIF 662개)에서 텍스처 경로 411개(effects 216, 노멀맵 23, 팔레트 그라디언트 포함: DLL은 경로를 다 받고 빌더가 슬롯을 고름). 매 시작 크기 열기 약 80-100 ms(테스터 실행에서 NIF 677개 열기 136 ms), 덤프를 쓸 때 읽기·쓰기는 미측정, 추정 300-600 MB·2-6초(NIF 677개 20 MB 읽기 289 ms 기준). writeRequest 끔(플레이어 기본)이면 0.
- 확인: 빌더 자가 시험 147/147(색 BC3·BC7·B8G8R8A8 → 회색, 알파·형식·밉 7 유지, 원본 불변; 회색 BC1 건너뜀; 깨진 DDS·BC5 이유와 함께 실패; NIF 슬롯 바꿈 규칙). 미확인: 게임 안 텍스처 덤프, 회색 사본이 화면에서 어떻게 보이는지.

### 텍스처 사본 수정 T (3곳 리뷰 fixlist-tex-t, 2026-10-17)

- T1: 열리지만 못 읽은 텍스처는 매니페스트 `textureReadFailures`에 남기고, 두 지문이 그대로인 동안 다시 읽지 않음(전체 덤프를 매 시작마다 다시 하지 않음). 쓰기 실패는 planned 해제·오류 기록, 매니페스트 갱신 안 함(다음 시작에 다시 덤프).
- T2: 이미 회색 판정은 보이고(알파) 밝은(최대 채널 ≥ kLitFloor 0.1) 픽셀의 평균 채도(chroma, 최대-최소) < 0.05. 밝은 픽셀이 없으면 회색. 검은 바탕의 작은 주황 점은 다시 칠하고, 565 잡음의 어두운 회색은 둠.
- T3: 글로우 셰이더(BSLSP_GLOWMAP 또는 SLSF2 bit 6)의 emissive 색을 휘도(×1)로.
- T4: tint는 역할별, diffuse는 --tint. tint≠1 사본은 `copies\t<백분율>\`에, 한 텍스처가 두 tint로 쓰이면 사본 둘.
- T5: DDS_FLAGS_EXPAND_LUMINANCE로 읽어 L8·A8L8·L16은 회색으로 건너뜀. 진짜 BC5·R8G8은 계속 거절.
- T6: 슬롯 2는 글로우 셰이더일 때만.
- T7: 경로 규칙과 텍스처 스캔을 `plugins/BlackFlame/src/DataPathRule.h` 하나로(DLL NifDump와 빌더가 함께 include), 빌더 자가 시험에 공통 표.
- T8: 테스터 패키지에 THIRD-PARTY-NOTICES.md와 licenses\(nifly, directxtex, CommonLibSSE-NG 등)를 넣고 없으면 거부. README_TESTER와 bat에 VC++ 2015-2022 x64 재배포 패키지와 종료 코드 -1073741515(0xC0000135) 안내.
- 같이 한 것: T11(덤프 경로가 덤프 폴더를 벗어나지 않는지 검사), T13(목록이 실패하면 지문 "error", 요청에서 textures 생략), T14(ReadModel이 totalSize만큼 미리 확보, 상한 256 MiB), T15(textureCaps를 요청에 씀), T16(문서), T17(float 형식 거절).
- 안 한 것: T9(같은 크기 교체는 README에 매니페스트 지우기 안내만), T10(덤프 예산 우선순위), T12(고아 사본·덤프 정리).

### 데칼 사본 (decal texture set copies, TXST, 2026-10-17)

- 문제(게임과 데이터로 확인): 버프 중에도 Fire Rune의 룬 문양과 땅에 남는 그을음이 주황. 둘 다 NIF가 아니라 데칼이다. MAG_RuneFireProjectile02의 DecalData = DecalRuneFire(Skyrim.esm 06051D, TX00·TX03 RuneFire01.dds, 주황 DXT5), 화염 IPCT의 TextureSet = DecalFlameBurn01(02B95A). FormCopiesFill이 PROJ.decalData와 IPCT.decalTextureSet/2를 원본 그대로 복사하고 있었다.
- 결정: TXST를 여섯 번째 사본 종류로. 클로저가 사본 PROJ의 decalData와 사본 IPCT의 두 texture set을 따라가고(kEdgeDecal), 빌더가 TXST 사본 레코드를 ESL에 쓰고(TXST 그룹이 맨 앞), DLL이 사본 PROJ/IPCT의 데칼 링크를 TXST 사본으로 다시 잇는다. NPC 폼과 공유되는 원본 TXST는 건드리지 않는다. 요청 버전 3, 맵 버전 2 그대로(TXST가 forms/copies에 실려 옛 빌더는 `type 'TXST'`에서, 옛 DLL은 항목마다 "bad entry"로 크게 실패).
- 슬롯: CommonLib BSTextureSet.h 순서(0 diffuse, 1 normal, 2 env mask, 3 glow/detail ...). TX00은 --tint(조명 받는 표면 위 diffuse), TX03은 1(밝기 마스크), NIF와 같은 텍스처 단계·같은 채도 판정. 나머지 슬롯은 원본 문자열. 브리프의 "TX02"는 짐작이었고 헤더로 TX03.
- DODT: BGSTextureSet::decalData는 포인터(0x0C0). DLL은 할당하지 않는다. 빌더가 원본에 DODT가 있을 때만 36바이트 0 스텁을 쓰고, DLL이 살아 있는 원본 값을 대입(색 RGB는 Tint.h 휘도, 알파 유지). IPCT 사본 자신의 dData 색도 같은 규칙으로 회색. textures[]·textureFileIDs·SetTexturePath는 쓰지 않음(사본은 레코드의 회색 경로 그대로).
- 사본을 빼는 경우(데칼은 원본으로 남음): DODT 있음/없음이 원본과 다름, 원본 슬롯이 빌드 때와 다름, 사본 슬롯이 맵과 다름(ESL·맵이 다른 실행), 회색 텍스처가 게임 파일 시스템으로 안 열림. 이런 TXST 사본이 있으면 지문이 같아도 신선도 stale.
- 문자: 요청은 UTF-8, 레코드와 텍스처 찾기는 시스템 코드 페이지 바이트(Utf8ToAnsiExact, 손실이면 그 TXST 제외 경고), 맵은 다시 UTF-8.
- id: 원본 키와 함께 타입도 유지. retired 항목에 type을 남기고, 같은 원본이 다른 타입으로 다시 오면(활성이든 retired든) 쓰기 전에 거부. 옛 요청(decalTextureSets 없음)이 TXST 사본을 가진 --out에 오면 쓰기 전에 거부.
- 팔레트(같은 날 따로 요청): 13:09 실행의 위험 검사 UNREAD 167은 GradBlackFlame.dds·GradBlackFlameAdd.dds가 덤프에 없어서. DLL이 두 팔레트를 매 요청 시작마다 게임 파일 시스템으로 읽어 해시하고(같은 크기 교체도 다시 덤프) 덤프와 request.json textures(own)에 넣는다. 빌더는 dump-manifest.json을 읽지 않으므로 "목록"은 request.json. 목록에 있으면 빌더 자신의 폴더는 안 읽고(게임 것 아니면 그 이유), 없으면(옛 DLL) 시작에 한 줄 알리고 빌더 폴더로. 못 읽은 우리 팔레트는 팔레트마다 UNREAD 한 줄(셰이프 수 포함), 게이트는 그대로 센다.
- 검증 방식: 실시간 데칼 배열(BGSDecalManager) 스캔은 하지 않음(스레드 보장이 코드에 없음, SEH는 락이 아님, 주소 재사용, T4가 NPC·플레이어를 한 프레임에 쏨). 자가 시험 T0-decal이 데이터로 판정: 사본 링크, TXST 사본의 DODT 유무·슬롯·textureFileIDs. 화면의 룬·그을음은 눈 확인.
- 세이브: 사본 ESL에는 활성 항목만 쓰므로, TXST 사본을 가리키는 저장된 참조는 그 원본이 아직 사본일 때만 풀린다. retired는 id를 예약할 뿐(다른 원본·타입에 주지 않음) 레코드는 없어진다. 데칼이 저장되는지는 미확인(BSTempEffect의 저장 가상 함수, UESP의 temp effects 구역). 저장된 데칼이 있는 채로 사본을 지우거나 그 TXST가 빠지는 재빌드는 알려진 한계.
- 리뷰에서 받지 않은 것: TXST가 ESL id를 고갈시킨다(사본 PROJ/IPCT를 거쳐서만 들어와 수십 개), 런타임 IFormFactory TXST(런타임 id의 세이브·경로 생성 미확인, 나중의 슬롯 풀 변경 때 다시 봄), TX02·TX06 색(마스크·맵 슬롯, 덤프하지 않음: README 알려진 한계).
- 수정 AB(2차 리뷰): TXST 슬롯 문자열은 코드 페이지를 거치지 않는다. DLL이 슬롯마다 원래 바이트를 hex(`texturesRaw`)로 보내고, 빌더는 다시 칠하지 않는 슬롯에 그 바이트를 그대로 쓰며, 맵은 `sourceTexturesRaw`로 같은 바이트를 두고, DLL은 바이트끼리(ASCII 대소문자·슬래시만 접고 DBCS 문자는 그대로) 비교한다. 949·936 같은 DBCS에서 SanitizeToUTF8이 잘못된 바이트를 '?'로 바꾸던 문제(U+FFFD는 65001에서만 나옴)가 이로써 없어져 U+FFFD 제외는 뺐다. TX00·TX03 원본이 ASCII가 아니면 다시 칠하지 않고 NOTE·DECAL_NOT_JUDGED(WARN). 폭발이 배치하는 TXST는 걷지도 제외하지도 않고 사본 EXPL은 원본을 둔다(제외하면 데칼 사본까지 사라지던 순서 의존 문제). 덤프 쓰기 실패가 있으면 요청에 `dumpComplete: false`(실패 수 포함)를 따로 쓰고(수정 AC: 지문은 깨끗하게 둔다. 지문에 표시를 붙이면 이번 시작의 비교가 늘 어긋나고, 연속으로 실패한 두 시작은 같은 값을 만든다), 빌더가 맵에 옮기며, DLL은 그런 맵을 지문과 상관없이 stale("불완전한 덤프로 빌드")로 본다. 필드가 없는 옛 맵은 완전한 것으로 친다. 회색 데칼 텍스처가 안 열리면 새 상태 texturesMissing(출력 모드 텍스처 누락·덮어씀, 또는 CRDW 같은 캐시가 낡음; 파일 색인 상태에도 포함). 빠진 TXST가 --out에 사본을 가지면 거부하고, 의도적으로 지우려면 `--retire-left-out`. DLL이 제외한 TXST(요청의 excluded)도 같은 검사. `--forms`는 버전 3 요청을 거부.
- 수정 AC: 경로 규칙이 "." 토큰도 거부(공유 표에 추가), 덤프는 경로마다 예외를 잡아 쓰기 실패로 세고 바깥 예외도 불완전으로 표시, 재시도의 쓰기 실패도 셈. 어긋난 TXST와 안 열리는 회색 텍스처가 같이 있으면 stale이 이긴다(재빌드가 텍스처도 다시 씀, 두 수는 detail에). 형식이 틀린 excluded TXST 원본은 경고가 아니라 거부. raw 슬롯도 공통 검사(빈 슬롯 일치, subrecord 길이).
