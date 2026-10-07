# Heart of Magic NPC 주문 배분 애드온 — 설계

날짜: 2026-10-07. 상태: **설계 검토 대기**.
근거 조사: `lab/addon-research/README.md` §2 (NPC 주문 배분), 이 문서 §8 의 실측.

## 1. 목적

모드 주문 3,289개(TAKEALOOK, 소환 제외) 가운데 NPC 가 쓰는 것은 팩 작성자가 SPID 패치를 손으로 만든 일부뿐이다
(설치된 `_DISTR.ini` 의 `Spell =` 줄 423개, 47개 모드. Mysticism NPC 패치 124, Natura 60, Abyss 40).
기준도 팩마다 다르다(같은 견습 파괴 주문이 Mysticism 20%, Natura 10%).

이 애드온은 하트 오브 매직의 사서 카탈로그(`spell_catalog.json`: 학파·티어·원소·기법 태그)로
**로드 오더의 모드 주문을 NPC 에게 같은 기준으로 나눠 주는 SPID 설정 파일을 자동으로 만든다.**

범위 밖: NPC 가 이미 가진 주문을 빼는 것(SPID 는 더하기만 한다. 2026-10-07 사용자 결정), NPC AI 키워드(NSV, 별도 애드온 후보),
패널의 강도 설정(고정값으로 시작).

## 2. 배포 형태

넥서스에 따로 올리는 별도 모드(가칭 **Heart of Magic - NPC Spells**). 같은 저장소에서 빌드한다.

| 파일 | 역할 |
|---|---|
| `SKSE/Plugins/HeartOfMagic_NPCSpells.dll` | 생성기 |
| `SKSE/Plugins/HeartOfMagic_NPCSpells.json` | 규칙 표 (사용자가 고칠 수 있음) |
| `HeartOfMagic_NPCSpells_DISTR.ini` (Data 루트, 생성물) | SPID 가 읽는 결과. 배포 zip 에는 없음 |
| `dist/HeartOfMagic-NPCSpells_<버전>.zip` | 배포 zip (본체 zip 과 별도) |

요구 사항: 하트 오브 매직(§6 의 알림 메시지가 들어간 버전 이상), SPID. ESP 는 필요 없다.

## 3. 데이터 흐름

```
HoM 전체 스캔 → SKSE/Plugins/SpellLearning/spell_catalog.json
      ↓ (HoM 이 kMessageType_CatalogUpdated 로 알림, 또는 시작 시 파일 시각 비교)
애드온: 카탈로그 읽기 + 규칙 표 + 기존 _DISTR.ini 검사
      ↓
선택기 → HeartOfMagic_NPCSpells_DISTR.ini (임시 파일에 쓴 뒤 바꿔치기)
      ↓ 다음 게임 실행
SPID 가 배분
```

## 4. 구성 요소

| 부분 | 하는 일 | 입력 → 출력 |
|---|---|---|
| 카탈로그 읽기 | `spell_catalog.json` 을 읽는다 | 파일 → 주문 목록(태그, 학파, 티어, FormID) |
| 규칙 표 | §5 의 JSON 을 읽고 이름을 FormID 로 푼다 | 파일 + 게임 데이터 → 규칙 |
| 기존 배분 검사 | Data 의 다른 `*_DISTR.ini` 에서 이미 나눠 주는 주문을 찾는다 | Data 폴더 → 건너뛸 주문 |
| 선택기 | 주문마다 대상, 스킬 문턱, 확률을 정한다 (§5.2) | 주문 + 규칙 → 배분 줄 |
| ini 쓰기 | SPID 문법으로 쓴다. 줄마다 이유 주석(예: `; 탈모어 특화: shock`), 머리말에 "자동 생성, JSON 을 고칠 것" | 줄 목록 → `_DISTR.ini` |

공통 생성기 라이브러리는 만들지 않는다. 두 번째 생성기 애드온(I4 아이콘, NSV 키워드)을 만들 때 겹치는 부분을 뽑는다.

## 5. 규칙

### 5.1 정한 규칙 (사용자 결정, 2026-10-07)

- **직업 3단계 (직업 레코드의 스킬 가중치로 자동 분류).** 이름 목록을 두지 않아 모드 직업도 같은 기준으로 나뉜다.
  | 단계 | 기준 (마법 학파 가중치 비율, 문턱은 §9 에서 바닐라 직업으로 맞춤) | 바닐라 예 |
  |---|---|---|
  | 마법사 | 큼 | `CombatMageDestruction` `CombatMageConjurer` `CombatMageNecro` `CombatMageElemental` `CombatSorcerer` `CombatMystic` |
  | 혼합 | 중간 | `CombatSpellsword` `CombatNightblade` `CombatWitchblade` `CombatMonk` (이름으로 나눈 예, 가중치 미확인) |
  | 비마법사 | 없음 또는 작음 | `CombatWarrior1H/2H` `CombatBarbarian` `CombatRanger` `CombatRogue` `CombatAssassin` `CombatThief` `CombatScout` `CombatNightingale` |
- **강도: 약하게 (고정).** 마법사는 가중치 있는 학파의 맞는 티어 주문을 평균 1.5개, 혼합은 가중치 있는 학파에서만 평균 0.5~1개, 비마법사는 5% 확률로 쉬운 주문 1개.
- **생물 (사용자 결정 2026-10-07):** 짐승 종족은 전부 제외한다 — 늑대인간 변신 종족(`WerewolfBeastRace`), 뱀파이어 군주(`DLC1VampireBeastRace`), `ActorTypeAnimal`. 그 밖의 `ActorTypeCreature`(하그레이븐, 드래곤 프리스트, 팔머, 드라우그르, 스프리건 등)는 **마법사 단계 직업일 때만** 마법사 줄을 받는다. 생물에게는 혼합 줄과 비마법사 줄을 주지 않는다. 사람형(`ActorTypeNPC`)은 직업 단계대로.
- **동료:** 따로 빼지 않는다. 바닐라·모드 동료도 직업 단계대로 받는다 (사용자 결정 2026-10-07).
- **티어 → 학파 스킬 구간 (사용자 결정 2026-10-07):** 견습 0~24, 수습 25~49, 숙련 50~74, 전문가 75~99, 대가 100. NPC 는 **자기 스킬 구간의 티어만** 받는다 — 고수가 견습 주문으로 칸을 채우지 않게(Mysticism 패치의 `13(25/49)` 방식).
- **스킬이 높을수록 더 많이:** 목표 개수가 구간마다 다르다. 마법사 기준 견습 1.0 · 수습 1.25 · 숙련 1.5 · 전문가 2.0 · 대가 2.5 (혼합은 그 절반). 구간마다 줄이 따로라 줄 확률 = 그 구간의 목표 ÷ 칸의 주문 수.
- **매지카로 못 쓰는 주문은 주지 않는다 (사용자 결정 2026-10-07):** 받아도 시전할 수 없기 때문. SPID 는 액터의 매지카로 거를 수 없으므로, 생성기가 주문의 매지카 비용(카탈로그)과 그 스킬 구간에서의 비용 감소로 **실제 비용**을 어림하고, 그 비용 이상의 매지카를 갖는 **최소 액터 레벨**을 계산해 SPID 레벨 조건으로 붙인다. 매지카 = 종족 기본 매지카 + 레벨에 따른 증가(자동 계산 스탯)로 어림한다. 공식과 값은 §9 에서 확인한다.
- **비마법사(전사·궁수):** 견습 이하 `curing`·`armor` 주문만.
- **세력 특화 (가중치 방식):** 특화 세력 NPC 가 받는 주문의 약 3/4 를 "주로 받는 태그"에서 고른다. "절대 안 받는 태그"는 항상 지킨다.

  | 세력 | 주로 받는 태그 | 절대 안 받는 태그 |
  |---|---|---|
  | 포스웨어·하그레이븐 | `nature` `earth` `flesh` `fire` `curse` | `sun` |
  | 뱀파이어 | `siphon` `health` `shadow` `frost` `undead` | `sun` `light` `curing` |
  | 사령술사 | `undead` `soul` `frost` `siphon` | `sun` |
  | 탈모어 | `shock` `fire` `arcane` `light` `shield` | `undead` |
  | 스텐다르 감시관 | `sun` `light` `curing` `shield` | `undead` `daedra` `shadow` |
  | 마법사 길드·궁정 마법사·그 밖의 마법사 직업 | 특화 없음 | — |

- **레드가드:** 마법사 직업이 아닌 레드가드는 아무 주문도 받지 않는다(뱀파이어·사령술사 세력이면 언데드 주문은 받는다). 마법사 직업 레드가드는 받되 언데드 주문은 받지 않는다(탈리온, 뱀파이어, 사령술사 세력은 예외. 아래 언데드 규칙).
- **언데드 주문** (`undead` 태그가 붙은 소환·되살리기·조종 주문)을 받는 NPC:
  1. 소환 계열 마법사 직업이면서 레드가드가 아닌 NPC
  2. 뱀파이어
  3. 사령술사 세력
  4. 예외: 탈리온(`0135E9:Skyrim.esm`, 레드가드, `TrainerConjurationMaster`)
  
  레드가드는 1 에서만 빠진다. 레드가드 뱀파이어·사령술사는 2·3 으로 받는다("이미 글렀다", 사용자 결정 2026-10-07).
- **이미 가진 마법은 빼지 않는다.**
- **제외:** `cloak`·`trap`·`sense` 기법, 크래시·스크립트 거부 목록, 다른 `_DISTR.ini` 가 이미 주는 주문.

### 5.2 선택 로직

SPID 는 줄마다 확률을 따로 굴린다. 그래서 확률을 주문마다 고정하지 않고 **칸(학파 × 티어 × 그룹) 안의 주문 수로 나눈다**:
`줄 확률 = 목표 평균 개수 ÷ 칸의 주문 수` (예: 견습 파괴 300개, 목표 1.5 → 0.5%).

주문 하나마다:
1. 제외 조건에 걸리면 건너뛴다.
2. 학파 → SPID 스킬 조건, 티어 → `tierSkill` 구간(그 구간만), 매지카 → 최소 액터 레벨 조건.
3. 태그가 어느 세력의 `prefer` 에 들면 그 세력 줄을 쓴다(확률은 `preferShare` 몫). `never` 에 걸리는 세력은 뺀다.
4. 특화 없는 마법사 줄과 혼합 줄을 쓴다(나머지 몫. 혼합 줄은 그 직업이 가중치를 둔 학파의 주문에만).
5. 견습 이하 `curing`·`armor` 면 비마법사 줄도 쓴다.
6. 모든 줄에 공통 제외를 붙인다: 레드가드 비마법사 제외(언데드 주문의 뱀파이어·사령술사 줄은 예외), 언데드 주문이면 §5.1 의 언데드 대상 외 제외.

### 5.3 규칙 표 모양

```json
{
  "strength": { "mageSpells": 1.5, "hybridSpells": 0.75, "nonMageChance": 0.05 },
  "classTiers": { "mageMinMagicShare": 0.5, "hybridMinMagicShare": 0.2 },
  "tierSkill": { "novice": [0, 24], "apprentice": [25, 49], "adept": [50, 74], "expert": [75, 99], "master": [100, 100] },
  "tierTarget": { "novice": 1.0, "apprentice": 1.25, "adept": 1.5, "expert": 2.0, "master": 2.5 },
  "nonMageTags": ["curing", "armor"],
  "excludeTags": ["cloak", "trap", "sense"],
  "groups": [
    { "name": "Thalmor", "factions": ["ThalmorFaction"],
      "prefer": ["shock", "fire", "arcane", "light", "shield"], "preferShare": 0.75, "never": ["undead"] }
  ],
  "undead": { "conjurerClasses": ["TrainerConjuration*"], "groups": ["Vampire", "Necromancer"],
              "exceptNpcs": ["0135E9:Skyrim.esm"] },
  "redguard": { "race": "RedguardRace", "mageOnly": true, "undeadOnlyVia": ["Vampire", "Necromancer"] }
}
```

바닐라·DLC 레코드는 `XXXXXX:Plugin.esm` FormID 로 적는다. 다른 모드가 EditorID 를 바꾸기 때문이다(TAKEALOOK 에서 하그레이븐 종족이 `OREO_HagravenRace`). 모드 레코드만 EditorID 를 허용한다. 못 찾은 항목은 로그에 남기고 그 그룹만 건너뛴다.

## 6. 하트 오브 매직 본체 변경

- SKSE 메시지 `kMessageType_CatalogUpdated` 하나를 추가한다. 카탈로그를 다 쓴 뒤 보낸다. data 는 카탈로그 경로.
- `plugins/SpellLearningAPI.h`, `docs/PLAN-PUBLIC-MODDER-API.md` 에 문서화한다.

## 7. 오류 처리

| 상황 | 동작 |
|---|---|
| 카탈로그 없음·형식 버전 다름 | ini 를 쓰지 않는다. 이전 ini 유지, 로그에 이유 |
| 규칙 표의 이름을 못 찾음 | 그 그룹만 건너뛴다, 로그에 이름 |
| 규칙 표 JSON 깨짐 | 내장 기본 규칙으로 쓰고 로그와 ini 머리말에 적는다 |
| SPID 없음 | ini 는 쓰고 로그에 "배분 안 됨" 경고 |
| 쓰다가 꺼짐 | 임시 파일 + 바꿔치기라 반쯤 쓴 파일이 남지 않는다 |

## 8. 테스트

- **오프라인 검사기 `npcspells-test`** (`tools/`, `BuildRelease.ps1` 이 빌드): 카탈로그, 규칙 표, `_DISTR.ini` 폴더 → ini. 실패 시 exit 1:
  - 구간마다 기대 평균이 `tierTarget` 에 맞음 (혼합은 절반, 비마법사 5%)
  - 어느 줄도 자기 스킬 구간 밖의 티어를 주지 않음
  - 줄의 최소 레벨에서 어림한 매지카가 그 주문의 어림 비용 이상
  - 레드가드 비마법사에게 가는 줄은 뱀파이어·사령술사의 언데드 줄뿐
  - 언데드 줄이 §5.1 의 대상 밖으로 가지 않음
  - 제외 태그·이미 배분된 주문이 빠짐
- **TAKEALOOK 카탈로그 실측:** 줄 수와 칸별 분포를 이 문서에 남긴다.
- **인게임 확인:** 산적 마법사, 탈모어, 사령술사, 뱀파이어, 레드가드 전사, 레드가드 마법사, 탈리온의 주문 목록(More Informative Console), SPID 로그에 우리 ini 적용 여부.

## 9. 확인할 것 (구현 전)

- SPID 가 `_DISTR.ini` 를 읽는 시점. 애드온이 그보다 먼저 쓰면 같은 실행에 반영될 수 있다.
- SPID 확률에 소수(0.5%)가 되는지. 안 되면 칸마다 레벨드 주문 목록(LVSP)을 만드는 방식으로 바꾼다.
- 레벨드 NPC(템플릿에서 직업을 물려받음, 예: `LvlBanditWizard…` 의 자기 직업 칸은 `EncClassDremoraMelee`)에 SPID 직업 조건이 먹히는지.
- 바닐라 세력·직업 EditorID 와 FormID (포스웨어, 하그레이븐, 뱀파이어, 사령술사, 탈모어, 감시관, 소환 계열 직업 목록).
- 바닐라 사령술사가 소환 직업인지.
- 매지카 어림: NPC 자동 계산 스탯의 매지카 공식(종족 기본값, 레벨당 증가, 직업 영향), 스킬에 따른 주문 비용 감소 공식, SPID 레벨 조건과 스킬 조건을 한 줄에 함께 거는 문법. 어림이 틀리면 생기는 일: 레벨 조건이 너무 높으면 주문이 덜 가고(안전), 너무 낮으면 못 쓰는 주문이 간다. 그래서 어림은 보수적으로(높게) 잡는다.
- 레벨이 고정이 아닌 NPC(PC 레벨 배수)에 SPID 레벨 조건이 어떻게 먹히는지.
- 직업 단계 문턱(`classTiers`): 바닐라 전투 직업 19개의 스킬 가중치를 읽어 마법사·혼합·비마법사가 위 표대로 갈리는 값으로 정한다. CommonLibSSE-NG `TESClass` 의 가중치 필드 이름 확인.
- SPID 에 직업 단계를 거는 방법: 직업 FormID 목록을 줄마다 적는다(애드온이 로드 오더의 모든 CLAS 를 분류해 목록을 만든다).

## 10. 실측 근거 (2026-10-07, TAKEALOOK)

- 산적 마법사 NPC(`EncBandit01~06Magic…`) 직업 = `EncClassBanditWizard` (`039D30:Skyrim.esm`).
- 탈리온: 레드가드, `TrainerConjurationMaster`. 바닐라 레코드에 주문 없음, 현재 승자(TSOS & MOS 패치)는 폭풍 아트로나크·번개 주문 4개.
- SkyPatcher 로 주문을 주는 설정: 0줄.
- 종족 키워드: 늑대인간 변신·뱀파이어 군주·늑대·스프리건·하그레이븐·팔머·드라우그르·드래곤 프리스트 모두 `ActorTypeCreature` (드라우그르·드래곤 프리스트는 `ActorTypeUndead` 도). 하그레이븐 종족 EditorID 는 모드가 `OREO_HagravenRace` 로 바꿈.
