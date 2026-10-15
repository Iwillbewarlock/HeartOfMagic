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
