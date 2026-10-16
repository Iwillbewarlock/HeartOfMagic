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
  경로"로 판정한다(JudgeGrey, SelfTestJudge). 이미 회색인 것이 기대 상태다. T3-pointer·T3-tint는 사본이면 INFO. 재색칠 판정은 사본이
  아닐 때만. 관찰 없는 통과 금지: T3·T3-remote는 훅에서 3D를 본 기록이 있어야 하고, E1c는 시전자 투사체·폭발 3D에 효과 셰이프가
  있어야 "회색 아님"으로 통과한다(11:11 실행의 E1c B·C는 0개로 통과했었다).
- C3 조준: 목표를 550 앞에(폭발 반경 320 + 100 밖), T2가 목표가 멈출 때까지(높이 변화 2 이내 0.5초) 기다린다 - 플레이어 높이에
  생성돼 떨어지던 중에 T3가 쏴서 빗나갔다(T3 조준 z -4119, T4 때 -4419). LaunchAt은 목표 3D의 world bound 중심을 겨눈다.
  T5·E1a는 TESHitEvent(플레이어, 주문, 목표)로 맞았는지 확인하고 비행시간 + 0.8초 안에 없으면 다시 쏜다(최대 3번). 그래도 없으면
  FAIL "aim: no hit after 3 casts"와 위치 정보. E4는 T5가 맞힘을 확인했을 때만 판정. 시전자는 AI가 켜져 있어 3500~8400 떠돌았으니
  T2에서 멈춘 자리를 집으로 두고 매 시전 전에 200 넘게 벗어났으면 Actor::SetPosition으로 되돌린다(T4·T6-npc·E1c A/B/C).
  시야(line of sight)는 확인하지 않는다: Actor::HasLineOfSight의 1.6.1170 id를 확인하지 않았다.
