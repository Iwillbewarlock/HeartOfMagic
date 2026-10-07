# Heart of Magic NPC 주문 배분 애드온 — 설계

날짜: 2026-10-07. 상태: **구현 계획 단계**(2026-10-08 — 검증 끝, 배분은 학파별 레벨드 목록(§5.4), 엔진 무작위 수용, 빈 LVSP 를 담은 ESL 배포).
근거 조사: `lab/addon-research/README.md` §2 (NPC 주문 배분), 이 문서 §10 의 실측.

## 1. 목적

모드 주문 3,289개(TAKEALOOK, 소환 제외) 가운데 NPC 가 쓰는 것은 팩 작성자가 SPID 패치를 손으로 만든 일부뿐이다
(설치된 `_DISTR.ini` 의 `Spell =` 줄 423개, 47개 모드. Mysticism NPC 패치 124, Natura 60, Abyss 40).
기준도 팩마다 다르다(같은 견습 파괴 주문이 Mysticism 20%, Natura 10%).

이 애드온은 게임 로딩 때 **하트 오브 매직 트리에 들어간 주문**을 사서 카탈로그의 태그(학파·티어·원소·기법)로
골라 **NPC 레코드에 직접 나눠 준다.** 누구에게 무엇을 왜 줬는지는 보고서 파일로 남긴다.

범위 밖: NPC 가 이미 가진 주문을 빼는 것(사용자 결정 2026-10-07), NPC AI 키워드(NSV, 별도 애드온 후보),
패널의 강도 설정(고정값으로 시작).

### 배분 방식을 SPID 에서 DLL 직접 배분으로 바꾼 이유 (2026-10-07)

처음에는 SPID ini 생성으로 정했다. 규칙이 스킬 구간·개수 늘리기·매지카로 늘면서:
- SPID 는 줄마다 확률을 따로 굴려 줄이 (주문 × 스킬 구간 × 그룹), 어림 3만 줄이 된다. SPID 는 시작할 때 줄 × NPC(TAKEALOOK 26,090명)를 모두 검사한다.
- SPID 에는 매지카 조건이 없어 레벨로 어림해야 한다.
- SkyPatcher 는 `spellsToAdd`·직업·종족·세력·스킬 조건은 있지만 **확률 조건과 매지카 조건이 없다**(SkyPatcher 문서 v6.4.1). 무작위 배분을 하려면 NPC 마다 줄을 써야 하고, 그 줄을 만드는 것도 결국 게임 데이터를 읽는 DLL 이다.

DLL 은 NPC 마다 한 번 계산하므로(어림 0.1초 안팎, §8 에서 측정) 가장 빠르고, 매지카를 레코드 값으로 쓴다.

## 2. 배포 형태

넥서스에 따로 올리는 별도 모드(가칭 **Heart of Magic - NPC Spells**). 같은 저장소에서 빌드한다.

| 파일 | 역할 |
|---|---|
| `HeartOfMagic_NPCSpells.esp` (ESL 플래그) | 빈 LVSP 레코드들 (고정 FormID, 항목은 DLL 이 채움) |
| `SKSE/Plugins/HeartOfMagic_NPCSpells.dll` | 배분기 |
| `SKSE/Plugins/HeartOfMagic_NPCSpells.json` | 규칙 표 (사용자가 고칠 수 있음) |
| `SKSE/Plugins/HeartOfMagic_NPCSpells_report.txt` (생성물) | 이번 실행에서 준 주문과 이유, 요약 수치, 걸린 시간. 배포 zip 에는 없음 |
| `dist/HeartOfMagic-NPCSpells_<버전>.zip` | 배포 zip (본체 zip 과 별도) |

요구 사항: 하트 오브 매직. SPID 는 필요 없다. 빈 LVSP 레코드만 담은 ESL 하나를 함께 배포한다(§5.4). 하트 오브 매직 본체는 바꾸지 않는다(애드온은 본체가 저장한 파일만 읽는다).

## 3. 데이터 흐름

```
(이전 실행) HoM 전체 스캔 → spell_catalog.json, 트리 생성 → spell_tree.json
      ↓ 게임 시작, kDataLoaded
애드온: 트리 주문 목록 ∩ 카탈로그 태그 + 규칙 표 + 다른 _DISTR.ini 가 주는 주문
      ↓
NPC 레코드(26,090개)마다: 대상 판정 → 받을 칸 → 무작위 선택(시드 = NPC FormID)
      ↓
TESNPC 주문 목록에 추가 + 보고서 쓰기
```

- 파일 위치: `Data/SKSE/Plugins/SpellLearning/spell_catalog.json`, `Data/SKSE/Plugins/SpellLearning/spell_tree.json`.
- 트리를 다시 만들거나 다시 스캔하면 **다음 실행부터** 반영된다.
- 시드 = 해시(NPC 를 정의한 플러그인 파일 이름, 로컬 FormID). 런타임 FormID 를 쓰지 않는 것은 ESL 의 FE 인덱스가 로드 오더에 따라 바뀌기 때문이다. 같은 트리·규칙이면 실행마다 같은 NPC 가 같은 주문을 받는다.
- 레코드에만 넣고 세이브에는 쓰지 않는다. 애드온을 빼면 다음 실행부터 사라진다(§9 에서 확인).

## 4. 구성 요소

| 부분 | 하는 일 | 입력 → 출력 |
|---|---|---|
| 주문 목록 | 트리에 든 주문만 고르고 카탈로그 태그·학파·티어·매지카 비용을 붙인다 | `spell_tree.json` + `spell_catalog.json` → 배분 후보 |
| 규칙 표 | §5 의 JSON 을 읽고 FormID 로 푼다 | 파일 + 게임 데이터 → 규칙 |
| 기존 배분 검사 | Data 의 `*_DISTR.ini` 에서 다른 모드가 이미 나눠 주는 주문을 찾는다 | Data 폴더 → 건너뛸 주문 |
| 칸 나누기 | 후보를 (학파 × 티어 × 그룹) 칸에 넣는다 | 후보 + 규칙 → 칸 |
| 직업 분류 | 로드 오더의 모든 직업을 스킬 가중치로 마법사·혼합·비마법사로 나눈다 | CLAS → 단계 |
| NPC 판정·선택 | NPC 마다 대상인지, 어느 칸에서 몇 개를 뽑을지 정하고 뽑는다 (§5.2) | NPC + 칸 → 줄 주문 |
| 적용 | NPC 의 주문 목록에 넣는다(템플릿 규칙 §9) | 줄 주문 → 레코드 |
| 보고서 | 목록을 받은 NPC **모두** 한 줄씩: persistentId(`Plugin\|0xLocal`), 이름, 그룹, 이유(직업 단계·언데드 정책), 붙인 LVSP EditorID. 주문은 엔진이 하이 프로세스에서 고르므로(§5.4) NPC 별 주문 대신 붙인 목록을 적는다. 그 밖에 칸별 수, 시간. TAKEALOOK 에서 1~2 MB | 결과 → `_report.txt` |

공통 생성기 라이브러리는 만들지 않는다. 두 번째 애드온을 만들 때 겹치는 부분을 뽑는다.

## 5. 규칙

### 5.1 정한 규칙 (사용자 결정, 2026-10-07)

- **배분 대상 주문: 하트 오브 매직 트리에 들어간 주문만.** `spell_tree.json` 이 없으면 아무것도 주지 않는다.
- **합쳐진 사본:** 트리는 같은 주문의 사본(이름·학파·비용·효과가 같은 것)을 한 노드로 합친다(`SpellCopies`). 배분은 **트리 노드의 대표 주문 하나만** 준다. 사본을 따로 주면 같은 주문이 칸에서 여러 번 뽑힐 수 있다.
- **직업 3단계 (직업 레코드의 스킬 가중치로 자동 분류).** 이름 목록을 두지 않아 모드 직업도 같은 기준으로 나뉜다.
  | 단계 | 기준: 마법 학파 가중치 합 / 18개 스킬 가중치 합 (정수 비교) | 바닐라 예 (Skyrim.esm 값) |
  |---|---|---|
  | 마법사 | 0.6 이상 | `CombatMageConjurer`·`CombatMageElemental` .909, `CombatMageDestruction`·`CombatMystic` .818, `CombatMageNecro` .727, `CombatSorcerer` .636 |
  | 혼합 | 0.3 이상 0.6 미만 | `CombatWitchblade` .545, `CombatSpellsword`·`CombatNightblade` .455, `CombatMonk` .364 |
  | 비마법사 | 0.3 미만 | `CombatScout` .273, `CombatNightingale` .182, `CombatAssassin` .091, 전사·바바리안·레인저·로그·도적 0 |

  문턱은 2026-10-08 에 바닐라 전투 직업 19개의 가중치로 정했다(처음 적은 0.5 / 0.2 는 Witchblade 를 마법사로, Scout 를 혼합으로 보내서 틀림). 0.600·0.300 에 딱 걸리는 모드 직업이 있어 `마법 합 × 10 ≥ 전체 × 6` 처럼 정수로 비교한다. 로드 오더가 가중치를 바꾸면 그 값을 따른다(USSEP 는 Witchblade 를 .636 으로 바꿔 마법사가 된다). TAKEALOOK 직업 287개: 마법사 48, 혼합 69, 비마법사 170.
- **생물:** 짐승 종족은 전부 제외한다 — 늑대인간 변신 종족(`WerewolfBeastRace`), 뱀파이어 군주(`DLC1VampireBeastRace`), `ActorTypeAnimal`. 그 밖의 `ActorTypeCreature`(하그레이븐, 드래곤 프리스트, 팔머, 드라우그르, 스프리건 등)는 **마법사 단계 직업일 때만** 마법사 몫을 받는다. 생물에게는 혼합·비마법사 몫을 주지 않는다. 사람형(`ActorTypeNPC`)은 직업 단계대로.
- **동료:** 따로 빼지 않는다. 바닐라·모드 동료도 직업 단계대로 받는다.
- **티어는 자기 스킬 구간 이하:** 학파 스킬 구간은 견습 0~24, 수습 25~49, 숙련 50~74, 전문가 75~99, 대가 100. NPC 는 **자기 구간과 그 아래 티어**를 받는다(파괴 80 → 견습~전문가 파괴 주문).
- **스킬이 높을수록 더 많이 (강도 "약하게", 고정):** 학파마다 받는 평균 개수가 구간에 따라 다르다. 마법사 견습 1.0 · 수습 1.25 · 숙련 1.5 · 전문가 2.0 · 대가 2.5. 혼합은 그 절반이고 가중치를 둔 학파에서만. 그 개수의 **절반쯤은 자기 티어, 나머지는 아래 티어들에 고르게**(견습 구간은 전부 견습). "절반" 은 어림이다: LVSP 하나가 주문 하나를 주므로(§5.4) 사다리로 맞춘 자기 티어 몫은 견습 1.0 · 수습 .8 · 숙련 .67 · 전문가 .5 · 대가 .6 이다(2026-10-08 사용자 결정).
- **비마법사(전사·궁수):** 5% 확률로 견습 이하 `curing`·`armor` 주문 1개.
- **매지카로 못 쓰는 주문은 주지 않는다:** 받아도 시전할 수 없기 때문. NPC 의 매지카와 그 학파 스킬에서의 실제 시전 비용을 비교한다(§9 에서 공식 확인). 자동 계산·레벨 변동 NPC 는 §9 의 대표 레벨로 스킬과 매지카를 함께 본다.
- **세력 특화 (가중치 방식):** 특화 세력 NPC 가 받는 주문의 약 3/4 를 "주로 받는 태그"에서 고른다. "절대 안 받는 태그"는 항상 지킨다. 알려진 한계: 그룹이 둘 이상인 NPC 는 아래 표 순서(포스웨어, 뱀파이어, 사령술사, 탈모어, 감시관)에서 첫 그룹의 목록만 받고, 다른 그룹의 "절대 안 받는 태그"는 적용되지 않는다. 목록을 그룹마다 미리 만들기 때문이다(보고서에 수, 2026-10-08 사용자 결정).

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

  레드가드는 1 에서만 빠진다. 레드가드 뱀파이어·사령술사는 2·3 으로 받는다("이미 글렀다").

  "소환 계열 직업"은 소환 가중치가 3 이상이고 다른 마법 학파 어느 것보다 작지 않은 직업이다(동점 허용: `CombatMageNecro`·`CombatMageConjurer` 는 소환 3 = 회복 3).
- **소환 주문은 사령술사와 소환술사가 나눠 받는다 (사용자 결정 2026-10-08).** 소환 학파 몫을 뽑을 때:
  - **사령술사** = 사령술사 세력, 뱀파이어, 또는 직업 `CombatMageNecro`(`0C969F:Skyrim.esm`, 사용자가 꼭 넣으라고 함). 소환 몫의 약 3/4 를 `undead` 주문에서 고른다.
  - **소환술사** = 그 밖의 소환 계열 직업. 소환 몫의 약 3/4 를 `undead` 가 **아닌** 소환 주문(아트로나크·데이드라·속박 무기 등)에서 고른다. 나머지 1/4 은 소환 주문 전체에서 고르므로 언데드도 가끔 나온다(위 언데드 규칙이 허락할 때만).
  - 비율은 세력 특화와 같은 `preferShare`(0.75)를 쓴다. 사령술사가 세력 특화도 받으면(사령술사 세력의 `prefer` 에 `undead` 가 있음) 같은 쪽으로 겹칠 뿐 충돌하지 않는다.
- **이미 가진 마법은 빼지 않는다.** 이미 가진 주문은 다시 주지 않는다.
- **제외:** `cloak`·`trap`·`sense` 기법, 크래시·스크립트 거부 목록, 다른 `_DISTR.ini` 가 이미 주는 주문.

### 5.2 선택 로직 (NPC 하나마다)

0. **템플릿에서 주문 목록을 물려받는 NPC(`TemplateFlags` 에 `SpellList`)는 건너뛴다.** 게임은 그 NPC 자기 목록을 읽지 않고 템플릿 쪽 목록을 쓴다(CK·엔진 동작, 코드로는 미확인). 주문은 **자기 목록을 실제로 쓰는 레코드**(템플릿 사슬의 끝)에만 더한다. 레벨드 NPC 는 레벨드 목록이 고른 끝 레코드의 주문을 그대로 쓴다. TAKEALOOK 에서 26,090명 중 14,389명(55%)이 건너뛰는 쪽이다. 예: `LvlBanditWizardCommonerMAggro1024` → 템플릿 `OREO_LCharBanditWizardCommonerMMagesOnly`(레벨드 목록) → `EncBandit0xMagic…` 의 목록을 씀.
1. 짐승 종족이면 끝. 생물이면 마법사 단계 직업일 때만 계속.
2. 직업 단계, 종족, 세력(특화 그룹), 학파 스킬 5개, 매지카를 읽는다.
3. 레드가드·언데드 규칙으로 쓸 수 있는 태그를 정한다.
4. 학파마다(마법사: 모든 학파, 혼합: 가중치 둔 학파) 스킬 구간을 찾고, 평균 개수(§5.1)를 자기 티어와 아래 티어로 나눈다.
5. 그 개수만큼 해당 칸에서 뽑는다. 평균이 소수이면 소수 부분은 확률로(1.5 → 1개 + 50% 로 1개 더). 특화 세력이면 약 3/4 를 `prefer` 태그 주문에서.
6. 뽑은 주문 중 매지카로 못 쓰는 것, `never` 태그, 이미 가진 것은 버리고 같은 칸에서 다시 뽑는다(칸이 비면 그만).
7. 비마법사면 5% 확률로 견습 이하 `curing`·`armor` 1개.

### 5.4 배분 방식: 학파별 레벨드 주문 목록 (2026-10-08)

디스어셈블로 확인한 엔진 동작(근거: `lab/addon-research/npc-spells-ledger.md`):
- NPC 주문 목록의 LVSP 는 액터가 하이 프로세스에 들어올 때 풀린다. 학파 5개를 돌며 **그 학파 스킬의 현재값**(강화 장비 포함)을 "레벨"로 쓴다. 항목 레벨 ≤ 스킬인 것 중 가장 높은 레벨 묶음에서 하나를 무작위로 고른다.
- 결과는 하이 프로세스 캐시에만 있고 세이브에 남지 않는다. 다시 들어오면 새로 뽑는다(정황).
- 항목을 레벨 순서로 보다가 **스킬보다 높은 항목을 만나면 멈추고, 다른 학파 주문을 만나도 멈춘다.**

그래서 애드온은 NPC 마다 주문을 고르지 않고, **학파 × 티어 칸마다 LVSP 를 만들어 NPC 에 붙인다.** 스킬 구간·티어 판정과 레벨이 변하는 NPC 의 문제를 엔진이 맡는다.

- **LVSP 항목 규칙 (엔진이 요구):** 한 LVSP 에는 한 학파 주문만 넣는다. 학파는 엔진과 같은 `MagicItem::GetAssociatedSkill()`(vtable 0x66, 디스어셈블의 `[rdx+330h]`)로 정한다. 항목은 레벨 오름차순으로 정렬한다. 항목 레벨 = 그 주문 티어의 스킬 하한(견습 0, 수습 25, 숙련 50, 전문가 75, 대가 100).
- **"스킬이 높을수록 더 많이":** LVSP 하나는 주문 하나를 준다. 그래서 개수만큼 LVSP 를 붙이고, 추가 몫의 LVSP 는 첫 항목을 높은 티어 하한부터 시작하게 만든다. 스킬이 모자라면 그 LVSP 는 아무것도 주지 않는다. 예: 파괴 몫 LVSP 1 은 견습부터, 2 는 숙련부터, 3 은 대가부터.
- **"절반은 자기 티어, 절반은 아래":** 자기 티어 몫은 기본 동작(가장 높은 묶음), 아래 티어 몫은 "이하 전부" 플래그(`llFlags & 1`)를 켠 LVSP 로 만든다.
- **세력·사령술사 특화:** 그룹마다 따로 LVSP 를 만든다(예: 사령술사용 소환 LVSP 는 `undead` 주문 비중 3/4). 같은 NPC 에 같은 LVSP 가 겹치면 하나만 붙는다.
- **매지카:** 거르지 않는다. AI 가 최대 매지카보다 비싼 주문을 전투에서 쓰지 않는다(§9).
- **결정론:** §3 의 "같은 NPC 는 실행마다 같은 주문" 은 더 이상 지켜지지 않는다. 엔진이 하이 프로세스에 들어올 때마다 무작위로 고른다.
- **LVSP 폼 만들기:** 칸 수(학파 5 × 티어 몫 × 그룹)만큼 필요하다. 항목은 목록당 최대 255개(`numEntries` 가 `uint8`)이므로 넘치면 같은 칸을 여러 LVSP 로 나눈다.

**사용자 결정 (2026-10-08)**
1. **엔진 무작위를 받아들인다.** 같은 NPC 라도 하이 프로세스에 들어올 때마다 주문이 바뀔 수 있다.
2. **ESL 로 간다.** 빈 LVSP 레코드를 고정 FormID 로 담은 작은 ESL(가칭 `HeartOfMagic_NPCSpells.esp`, ESL 플래그)을 함께 배포하고, DLL 은 kDataLoaded 에 그 레코드의 항목만 채운다. 런타임 폼(0xFF)은 만들지 않는다. 칸이 ESL 의 레코드 수보다 많으면 남는 칸은 건너뛰고 보고서에 적는다.
### 5.3 규칙 표 모양

```json
{
  "tierSkill": { "novice": [0, 24], "apprentice": [25, 49], "adept": [50, 74], "expert": [75, 99], "master": [100, 100] },
  "tierTarget": { "novice": 1.0, "apprentice": 1.25, "adept": 1.5, "expert": 2.0, "master": 2.5 },
  "ownTierShare": 0.5,
  "hybridScale": 0.5,
  "nonMageChance": 0.05,
  "nonMageTags": ["curing", "armor"],
  "classTiers": { "mageMinMagicShare": 0.6, "hybridMinMagicShare": 0.3 },
  "conjuration": { "necromancerClasses": ["0C969F:Skyrim.esm"], "necromancerGroups": ["Necromancer", "Vampire"],
                   "necromancerPrefer": ["undead"], "conjurerAvoidPrefer": ["undead"], "share": 0.75 },
  "excludeTags": ["cloak", "trap", "sense"],
  "beastRaces": ["0CDD84:Skyrim.esm", "00283A:Dawnguard.esm"],
  "groups": [
    { "name": "Forsworn", "factions": ["043599:Skyrim.esm", "04359E:Skyrim.esm"],
      "prefer": ["nature", "earth", "flesh", "fire", "curse"], "preferShare": 0.75, "never": ["sun"] },
    { "name": "Vampire", "factions": ["027242:Skyrim.esm", "003376:Dawnguard.esm"],
      "prefer": ["siphon", "health", "shadow", "frost", "undead"], "preferShare": 0.75, "never": ["sun", "light", "curing"] },
    { "name": "Necromancer", "factions": ["034B74:Skyrim.esm"],
      "prefer": ["undead", "soul", "frost", "siphon"], "preferShare": 0.75, "never": ["sun"] },
    { "name": "Thalmor", "factions": ["039F26:Skyrim.esm", "0B80F7:Skyrim.esm", "0135A0:Skyrim.esm"],
      "prefer": ["shock", "fire", "arcane", "light", "shield"], "preferShare": 0.75, "never": ["undead"] },
    { "name": "Vigilant", "factions": ["0B3292:Skyrim.esm"],
      "prefer": ["sun", "light", "curing", "shield"], "preferShare": 0.75, "never": ["undead", "daedra", "shadow"] }
  ],
  "undead": { "conjurerClasses": "가중치로 판정", "groups": ["Vampire", "Necromancer"],
              "exceptNpcs": ["0135E9:Skyrim.esm"] },
  "redguard": { "race": "013748:Skyrim.esm", "mageOnly": true, "undeadOnlyVia": ["Vampire", "Necromancer"] }
}
```

모든 레코드는 `XXXXXX:Plugin.ext` FormID 로만 적는다. 모드 레코드도 마찬가지이고 EditorID 는 받지 않는다(2026-10-08 사용자 결정). 다른 모드가 EditorID 를 바꾸고(TAKEALOOK 에서 하그레이븐 종족이 `OREO_HagravenRace`), 게임은 powerofthree's Tweaks 없이는 대부분의 폼에서 EditorID 를 빈 문자열로 돌려주기 때문이다. 못 찾은 항목은 로그에 남기고 그 항목만 버린다(§7). 위 표의 `tierSkill`, `ownTierShare`, `conjuration` 의 `necromancerGroups`·`necromancerPrefer`·`conjurerAvoidPrefer`, `undead` 의 `conjurerClasses`·`groups`, `redguard` 의 `mageOnly`·`undeadOnlyVia` 는 고정값이라 배포 파일에 넣지 않는다(README 에 적음). 배포 파일에는 `ladder`, `excludeSpells`, `keywords`(`ActorTypeNPC`·`ActorTypeCreature`·`ActorTypeAnimal` FormID) 가 더 있다. 세력 FormID 는 2026-10-08 houseCARL 로 확인했다(하그레이븐 세력은 포스웨어 그룹에 넣음, 대학·궁정 마법사는 특화 없음이라 목록에 없음). `beastRaces` 두 FormID 도 같은 날 확인.

## 6. 하트 오브 매직 본체 변경

없음. 애드온은 본체가 이미 저장하는 `spell_catalog.json` 과 `spell_tree.json` 만 읽는다. 두 파일의 형식이 바뀌면 애드온이 형식 버전을 보고 멈춘다(§7).

## 7. 오류 처리

| 상황 | 동작 |
|---|---|
| 트리·카탈로그 없음, 형식 버전 다름 | 아무것도 주지 않는다. 로그와 보고서에 이유 |
| 규칙 표의 항목을 못 찾음 (FormID 가 아니거나 로드 오더에 없음) | 그 항목만 버린다(그룹·목록의 나머지는 그대로), 로그에 항목 |
| 규칙 표 JSON 깨짐 | 내장 기본 규칙으로 쓰고 로그와 보고서 머리말에 적는다 |
| 트리의 주문이 이번 로드 오더에 없음 (모드 제거) | 그 주문만 건너뛴다, 보고서에 수 |
| 칸이 비어 뽑을 주문이 없음 | 그만큼 덜 준다, 보고서에 칸별 부족 수 |

## 8. 테스트

- **오프라인 검사기 `npcspells-test`** (`tools/`, `BuildRelease.ps1` 이 빌드): 선택 로직(§5.2)을 게임 없이 돌린다. 입력은 트리·카탈로그와, 직업·종족·세력·스킬·매지카를 적은 NPC 목록 JSON(houseCARL 이나 덤프로 만든다). 실패 시 exit 1:
  - 구간·학파마다 평균 개수가 `tierTarget` 에 맞음(혼합 절반, 비마법사 5%), 수천 번 반복으로 확인
  - NPC 스킬 구간보다 높은 티어가 없음
  - (§5.4) 만든 LVSP 마다 한 학파 주문만 있고 항목이 레벨 오름차순이며 항목 레벨이 티어 하한과 같음, 목록당 255개 이하
  - 트리에 없는 주문이 없음
  - 레드가드·언데드·생물·짐승 규칙 위반 없음
  - 같은 입력이면 같은 결과(시드)
- **TAKEALOOK 실측:** NPC 수, 받은 NPC 수, 칸별 분포, **걸린 시간**을 이 문서에 남긴다.
- **인게임 확인:** 산적 마법사, 탈모어, 사령술사, 뱀파이어, 레드가드 전사, 레드가드 마법사, 탈리온, 하그레이븐, 늑대의 주문 목록(More Informative Console)과 보고서가 일치하는지.

## 9. 확인할 것 (구현 전)

2026-10-08 게임 밖 검증 결과. 근거와 수치는 `lab/addon-research/npc-spells-ledger.md` (확인됨 / 가설).

- **[확인] 직업·스킬을 물려받는 NPC:** 레벨드 NPC 는 직업·스탯·세력·주문 목록을 모두 레벨드 목록이 고른 끝 레코드에서 받는다(LvlBanditWizard 계열 등 6개). 끝 레코드도 다시 템플릿을 쓸 수 있어(034F7F: 직업은 자기 것, 주문 목록·스탯은 034F58), 주문은 자기 SpellList 를 실제로 쓰는 레코드에 넣고 직업은 그 레코드의 값으로 본다.
- **[확인] 주문 비용:** HoM 스캔과 같은 `MagicItem::CalculateMagickaCost(nullptr)`(시전자 없는 기본 비용, 내부는 `CalculateCost` RELOCATION_ID(11213, 11321))로 읽는다. NPC 학파 스킬에 따른 감소는 애드온이 따로 계산한다(공식은 아래 미확인).
- **[확인] 기존 `_DISTR.ini`:** TAKEALOOK 에서 켜진 `Spell =` 431줄 중 `0xFormID~Plugin` 394, EditorID 37, 그 밖 0. 두 형식만 읽으면 된다. LVSP 를 주는 줄은 없다.
- **[확인] 직업 문턱:** 0.6 / 0.3 (§5.1 표).
- **[확인] 세력 FormID:** §5.3 `groups` 에 채움. 뱀파이어는 세력 두 개, 탈모어는 세 개.
- **[확인] 트리 파일 형식:** 노드마다 `persistentId`(`Plugin.esp|0xLocalID`)가 있고 카탈로그 키와 전부 일치(1,452/1,452). 런타임 `formId` 는 로드 오더에 따라 바뀌므로 쓰지 않는다.
- **[확인] 저장:** CommonLib `TESSpellList::AddSpell/AddLevSpell` 은 change flag 를 켜지 않는다 -> 베이스 레코드 변경으로 세이브에 쓰이지 않는다.
- **[확인, 2026-10-08 디스어셈블] 기준 레벨 문제는 사라졌다: 엔진은 LVSP 를 NPC 의 학파 스킬로 푼다.** 레벨이 아니다. §5.4 로 설계를 바꾼다. 남은 사용자 결정 둘은 §5.4 끝.
- **[확인] 전투 AI 와 매지카:** AI 는 주문마다 그 NPC 의 스킬로 줄어든 비용을 계산하고, **최대 매지카 < 비용이면 그 주문을 전투 목록에 넣지 않는다.** 고를 때는 지금 매지카 ≥ 비용, 또는 비용 < 3초 동안 회복되는 매지카일 때만 쓴다. -> 애드온이 매지카로 주문을 거를 필요가 없다(§5.1 의 매지카 규칙은 엔진에 맡긴다).- **[미확인] 걸린 시간:** 26,090명 처리 시간. 0.5초를 넘으면 칸 색인을 다시 본다.
## 10. 실측 근거 (2026-10-07, TAKEALOOK)

- NPC 레코드 26,090개. 그중 `SpellList` 를 템플릿에서 물려받는 것 14,389개.
- 설치된 SPID 주문 줄 423개(47개 모드). SkyPatcher 로 주문을 주는 설정 0줄.
- 산적 마법사 NPC(`EncBandit01~06Magic…`) 직업 = `EncClassBanditWizard` (`039D30:Skyrim.esm`).
- 탈리온: 레드가드, `TrainerConjurationMaster`. 바닐라 레코드에 주문 없음, 현재 승자(TSOS & MOS 패치)는 폭풍 아트로나크·번개 주문 4개.
- 바닐라 전투 직업 19개(`Combat*`) 목록은 §5.1 표.
- 종족 키워드: 늑대인간 변신·뱀파이어 군주·늑대·스프리건·하그레이븐·팔머·드라우그르·드래곤 프리스트 모두 `ActorTypeCreature` (드라우그르·드래곤 프리스트는 `ActorTypeUndead` 도). 하그레이븐 종족 EditorID 는 모드가 `OREO_HagravenRace` 로 바꿈.
- 트리 파일 경로: `Data/SKSE/Plugins/SpellLearning/spell_tree.json` (`UIManagerCore.cpp:28`).
- 레벨이 플레이어를 따라가는 NPC(`PcLevelMult`) 4,296명. 그중 자기 주문 목록을 쓰는 NPC(SpellList 템플릿 아님) 3,303명, 레벨 상한 없음(`CalcMaxLevel = 0`) 461명. 레벨 폭 예: Midnight Sun 사령술사 엘리트 20~60, Penitus Oculatus 암살자 20~90.

## 11. 자동 계산 NPC 레벨 조사 (2026-10-07)

- **바닐라도 레벨별 주문 목록(LVSP)을 NPC 주문 목록에 넣는다.** Skyrim.esm 에 LVSP 24개, 이를 쓰는 NPC 92개. 예: `LSpellWEBattlemageFireLeftHand` = 레벨 1 불씨조각, 5 화염 화살, 20 화염구. 게임이 이 목록을 언제, 어느 레벨로 고르는지는 미확인(문서·소스 없음).
- **SPID:** 베이스 `TESNPC` 에 넣는다. 레벨 조건이 있는 항목만 플레이어 레벨이 오를 때(`ProcessLists::HandlePlayerLevelUpdate` 후크) 다시 평가하고, 세이브마다 캐시로 복원한다(`DistributePCLevelMult.cpp`). 필터는 베이스 레코드 값(`npc->GetLevel()`, `playerSkills`)으로 본다.
- **CommonLib 에 액터 없이 자동 계산 스킬·매지카를 구하는 함수는 없다.** 직접 계산한다: 스킬 ≈ 15 + 종족 보너스 + 8 × (레벨 − 1) × 가중치 / 가중치 합, 매지카 ≈ 종족 기본 + 오프셋 + 10 × (레벨 − 1) × 매지카 가중치 / 능력치 가중치 합 (UESP CLAS 형식 문서, 근사식).
- **전투 AI 와 매지카:** 엔진 코드로는 미확인. 정황: 마법 전투 항목이 "자원(능력치, 양)"을 보고하고(`CombatInventoryItem.h:15`, `GetResource`), 게임 설정 `fCombatInventoryResourceCurrentRequiredMult = 1.0`, `fCombatInventoryResourceDesiredRequiredMult = 2.0` 이 있다. 이름대로라면 지금 매지카가 비용 이상일 때만 그 주문을 고른다. 매지카가 떨어진 마법사는 근접 무기로 바꾼다는 플레이 관찰이 있다.
