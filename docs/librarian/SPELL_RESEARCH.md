# Spell Research 호환

> 사서가 태그를 붙인 주문을 Spell Research(SR) 2.2.3 의 아키타입 목록에 등록해서, SR 패치가 없는 모드
> 주문도 SR 의 연구·조합·마법책 베껴 쓰기에 나오게 한다. 퍽 어댑터(PERK_ADAPTERS.md)와 같은 판정기를
> 쓰고, SR 쪽 레코드는 하나도 바꾸지 않는다.
> 상태: 2026-10-03 구현·빌드, 오프라인 측정 완료, 게임 확인 대기(7절). 대응표는 2026-10-02 세션에서 줄마다 확정.
> 코드: `include/librarian/SpellResearch.h` · `src/librarian/LibrarianSpellResearch.cpp`(순수 등록 계획),
> `include/librarian/SpellResearchPatch.h` · `src/librarian/LibrarianSpellResearchPatch.cpp`(게임 쪽),
> `include/librarian/LoadOrderSource.h`(퍽 어댑터와 함께 쓰는 판정기 입력). 도구: `tools/librarian-spellresearch.cpp`.

---

## 1. SR 이 주문을 아는 방법 (SR 2.2.3 + Upgrade and Fixes 1.4.3 소스로 확인)

- **ESP 의 주문 목록은 전부 비어 있다.** 바닐라 주문까지 모든 등록은 플레이어가 MCM 에서 **Import Spells** 를
  누를 때 Papyrus 가 넣는다. 그래서 등록은 세이브에 묶인 쪽(`BGSListForm::scriptAddedTempForms`)에 쌓인다.
- Import 순서(`_SR_ImportMain.psc`): `_SR_ListImportQuests` 의 퀘스트를 하나씩 부른다. 각 퀘스트는 끝날 때
  `_SR_QuestImport.RegisterForSingleUpdate(0.5)` 로 다음을 부르므로 **차례로 끝까지 돈다**. 첫 퀘스트
  (`_SR_ImportSkyrim`)가 시작할 때 모든 목록을 `Revert()` 로 비운다. 다 돌면 `_SR_GlobalImporting = 0`,
  `_SR_GlobalIsSetup = 1`, 그리고 메시지 상자 "Spell Research: All Spells Imported".
- SR 이 기본으로 가진 임포트 퀘스트는 Skyrim·Dawnguard·Dragonborn 셋뿐이다. 그래서 모드 주문은 패치 없이는
  하나도 들어가지 않는다(2026-10-03 사용자 확인: "기본 DLC 만 임포트된다").
- 주문 하나의 등록(`_SR_ImportSkyrim.psc`): `_SR_ListAllSpells{N}{등급}`, 학파 `_SR_ListSpellsSkill*`,
  시전 `_SR_ListSpellsCasting*`, 대상 `_SR_ListSpellsTarget*`(여러 개), 원소 `_SR_ListSpellsElement*`, 기법
  `_SR_ListSpellsTechnique*` 에 `AddForm`. 마법책은 `_SR_ListTomes{등급}` 과 `_SR_ListTomeSpells{등급}` 에
  **같은 순서로** 넣는다(둘 다 아직 없을 때만). 스크롤도 같은 방식.
- SR 은 목록을 쓸 때마다 그 자리에서 읽는다(`HasForm`/`GetAt`/`Find`, 캐시 없음).
  - 주문 조합: 플레이어가 고른 아키타입을 **전부** 가진 주문만 후보다(`_SR_EffectComposeSpellScript.psc`).
    라벨이 많을수록 더 많은 조합에서 그 주문이 나온다 → 정밀도가 재현율보다 먼저다.
  - 마법책 베껴 쓰기: `_SR_ListTomeSpells{등급}.Find(주문)` 의 인덱스로 `_SR_ListTomes{등급}` 의 책을 꺼낸다
    (`_SR_QuestScript.psc:1459-1479`). 두 목록의 길이가 어긋나면 엉뚱한 책이 나온다.

## 2. 대응표 (확정)

| SR 아키타입 | 사서 쪽 집합 |
|---|---|
| 원소 `health` | (`health` ∪ `blood`) − `undead` − `necrotic` |
| 원소 `light` | `light` ∪ (이펙트 archetype `Invisibility` ∩ 학파 Alteration) |
| 원소 `sun` | `sun` (`holy` 는 접지 않는다: SR 정답셋의 Holy 주문 19개 중 `sun` 은 2개) |
| 원소 `arcane` | `arcane` ∪ ((`holy` ∪ `necrotic` ∪ `eldritch`) ∩ 체력 피해 − SR 원소가 하나라도 있는 주문) |
| 기법 `curing` | `curing` ∪ `dispel` |
| 기법 `summoning` | `summoning` ∪ `teleport` (SR 의 summoning 은 "공간을 건너 옮기기": 정답셋의 순간이동 17건이 예외 없이 summoning) |
| 나머지 원소 30 · 기법 13 | 같은 이름의 태그 |
| 학파 · 등급 · 시전 · 대상 | 카탈로그 필드(`school`, `tier`, `casting`, `targeting`) — 이미 SR 어휘다 |
| 버림 | `sacrifice`, 위에 쓰이지 않은 `holy` · `necrotic` · `eldritch` |

- 체력 피해 = 해로운 이펙트, `primaryAV` Health, archetype `ValueModifier`/`DualValueModifier`/`Absorb`
  (ascension.json 의 무속성 피해 줄과 같은 조건).
- 원소나 기법이 비어도 등록한다(SR 정답셋에도 원소 없는 주문 54, 기법 없는 주문 259). **학파나 등급이 없는
  주문은 등록하지 않는다** — SR 은 등급 목록에 없는 주문을 다루지 못한다.
- `acid`, `life` 는 지금 사서 룰이 한 주문도 주지 않는다(TAGS.md 4절). 이 둘이 든 조합은 SR 패치가 등록한
  주문에서만 나온다.
- 이 표는 SR 로 넘길 때만 쓴다. 사서 어휘에서는 `summoning`/`teleport`, `health`/`blood` 를 계속 따로 둔다
  (트리 묶기와 퍽 어댑터는 둘을 구분해야 한다).
- 알려진 사서 쪽 누락: "반사 광선"(햇빛 광선)이 `sun` 대신 `holy` 만 가져 `arcane` 으로 간다. 사서가 `sun` 을
  붙이면 저절로 빠진다(ToDo).

## 3. 등록 시점 (A2: ESP 없이 C++ 가 Import 완료를 감지)

- **SR 이 없으면 아무것도 안 한다**(`SpellResearch.esp` 가 로드되지 않음).
- 세 시점에 "맞춤(reconcile)"을 한 번 돈다. 모두 게임 스레드다. 앞의 둘은 플레이어가 연구 중일 수 없는 때이고, 셋째
  (스캔 뒤)도 새 위험은 아니다: `BGSListForm::AddForm` 은 SR 자신의 Import 스크립트가 게임 중에 VM 스레드에서 부르는 바로 그
  엔진 함수다.
  1. `MessageBoxMenu` 가 열리거나 닫힐 때, `_SR_GlobalImporting == 0` 이고 `_SR_GlobalIsSetup == 1` 이며 등급 목록 크기가
     지난번 맞춤 뒤로 바뀌었으면(= 방금 Import 가 끝남). 다른 메시지 상자에는 목록 크기가 그대로라 돌지 않는다.
     `_SR_ImportMain` 은 두 전역을 바꾼 **다음** "All Spells Imported" 상자를 부르므로, 열릴 때 이미 조건이 맞는다
     (2026-10-04: 처음에는 닫힐 때만 돌아서, 상자를 읽는 동안에는 아직 들어가지 않았다).
     같은 맞춤을 SKSE 모드 이벤트 `SR_ImportFinished` 로도 부른다(2026-10-31): 상자 없이 조용히 Import 하는 SR 빌드
     (Spell Research - Prisma UI 의 자동 Import)는 두 전역을 바꾼 **다음** Papyrus `SendModEvent("SR_ImportFinished")` 를
     보낸다. 이벤트 이름은 대소문자를 가리지 않고 비교하고, 일은 상자 때와 같이 게임 스레드 작업으로 돈다(마법책 짝
     다시 읽기 → 맞춤). 상자와 이벤트가 둘 다 와도 두 번째는 등급 목록 크기가 지난 맞춤 그대로라 아무것도 안 한다
     (중복 등록·중복 알림 없음).
  2. 세이브를 불러온 뒤(`kPostLoadGame`) `IsSetup == 1` 이면. HoM 을 나중에 설치한 세이브, 메시지 상자를 다른
     모드가 숨긴 경우를 여기서 잡는다. 로드 때 도는 SR 스크립트는 목록을 건드리지 않는다: Fixes 1.4.3 의
     `_SR_AliasPlayerScript.OnPlayerLoadGame` 은 `Maintenance()`(애니메이션 3개 로드)만 부른다(소스 확인).
- Fixes 의 JSON 캐시(`_SRU_Cache`, MCM 에서 플레이어가 누를 때만 만든다)는 목록에서 만든 주문별 아키타입 사본이다.
  연구는 캐시에 있는 주문만 캐시로 읽고, 없는 주문은 목록을 읽는다. 그래서 캐시를 만든 **뒤에** 등록한 주문도
  목록 쪽으로 정상 동작한다. 캐시를 다시 만들면 우리 등록도 들어간다.
  3. 전체 스캔이 카탈로그를 새로 쓴 뒤(패널의 트리 스캔, Papyrus `RunScan` — 퍽 어댑터가 다시 도는 자리). HoM 을 처음
     깔고 처음 스캔한 세이브, 새 모드를 넣고 다시 스캔한 경우를 다음 로드까지 기다리지 않는다.
- 맞춤:
  - **마법책이 가르치는 주문만** 대상이다(스캐너의 `TomesBySpell`: 책이 가르치는 주문, 그리고 Spell Research Synthesizer 가
    아무것도 안 가르치게 바꾼 책은 그 짝의 주문 - `SpellTomes::SpellOf`, docs/ARCHITECTURE.md "Spell tomes that teach
    nothing"). SR 의 Import 가 끝난 뒤라면 그런 짝의 주문은 SR 이 이미 알고 있어 다시 넣지 않는다(FLM 만 쓰고 Import 를
    안 했으면 등급 목록에 없으니 HoM 이 넣는다). 전체 스캔의 카탈로그에는
    NPC·퀘스트·생물 주문도 들어 있다(TAKEALOOK 3546 중 2118). 이것까지 넣으면 플레이어용 원본과 같은 이름의 NPC 판
    (수치·퍽 연동이 다름)이 조합 후보에 함께 뜬다(2026-10-03 리뷰에서 발견).
  - 트리처럼 **음성 슬롯 주문**(`voiceSlot`), **주문 감별기가 빼는 주문**(`JudgeSpell` 의 out: 마나 소모 0 - 애완동물·탈것·추종자 부르기,
    모드의 유틸리티 주문 - 과 돌려보내기만 하는 주문. 플레이어가 패널에서 넣기를 누른 주문은 들어간다. 2026-10-06)과 **다른 플러그인의 같은 주문 사본**(`duplicateOf`, 스캐너의
    `DuplicateGroupOf` — 트리와 마법책 판정이 읽는 그 묶음)은 넣지 않는다. 묶음에서는 남기는 레코드 하나만 넣고,
    묶음의 어느 레코드든 SR 등급 목록에 이미 있으면 그 묶음은 넣지 않는다 — 한 주문이 SR 에 두 번 뜨지 않게
    (2026-10-04). 빠진 수는 로그 한 줄과 보고서 `leftOut` 에 남는다.
  - **빌더가 트리에 넣는 주문과 똑같이 넣는다**(2026-10-05, 사용자 결정): 마법책을 나눠 주는 곳이 없는 주문
    (`tomeUnreachable`, `TomeReach::UnreachableSpells`)도, 플레이어의 주문 블랙리스트와 플러그인 화이트리스트에서 끈
    플러그인(`config.json` 의 `spellBlacklist`, `pluginWhitelist`, `ReadPlayerFilter`)도 트리처럼 뺀다. 전에는
    `tomeUnreachable` 주문을 "SR 의 연구가 닿는 길"로 일부러 넣었다. TAKEALOOK(2026-10-05): 빌더 1419 = SR 대상 1419.
    빠진 수는 로그와 보고서 `leftOut`(`tomeUnreachable`, `player`)에 남는다. 그 판정을 하는 마법책 도달 검사가
    아직 돌고 있으면 아무것도 넣지 않고(`g_lastSeen` 도 그대로), 검사가 끝난 뒤 한 번 다시 맞춘다
    (`TomeReach::RunAfterPass`, 2026-10-27). 그사이 Import 로 마법책 짝이 바뀌어 새 검사가 돌고 있으면 그 검사를 다시 기다린다(`kSuperseded`). 검사가 실패했으면 표시 없이 넣는다(스캔과 같음). 이미 넣은 등록은 그 세이브의 목록에
    남으므로, 바뀐 규칙은 SR 에서 Import 를 한 번 더 누르면 적용된다(Import 가 목록을 비우고 다시 채운다).
  - 카탈로그 주문 중 **어느 `_SR_ListAllSpells{등급}` 에도 없는 주문만** 등록한다. SR 패치가 등록한 주문은
    원작 라벨 그대로 둔다(우리 라벨을 섞지 않는다). 우리가 지난번에 넣은 주문도 여기서 건너뛴다 — 몇 번을 돌아도
    결과가 같다.
  - 엔진의 `BGSListForm::AddForm`(Papyrus `AddForm` 과 같은 함수)으로 넣는다. 그래서 SR 패치의 등록과 똑같이
    세이브에 남고, 다음 재Import 때 SR 의 `Revert()` 로 함께 비워진 뒤 맞춤이 다시 넣는다.
  - 등록 순서는 SR 과 같다: 등급, 학파, 시전, 대상, 원소, 기법, 그다음 마법책 짝.
- **플레이어에게 보이는 것** (2026-10-04, 사용자 보고 "재임포트해도 던가드랑 드래곤본만 인식"): SR 의 임포트
  퀘스트는 "Importing 46 Forms from Dawnguard" 같은 알림을 띄우지만, 우리 등록은 퀘스트가 아니라 조용했다.
  - 주문을 하나라도 넣은 맞춤은 화면 알림 `hud.spellResearchAdded`("Spell Research: Heart of Magic이 주문 N개를
    추가했습니다", 알림 표시·알림 언어 설정을 따름)를 띄운다.
  - 맞춤마다 상태(`SpellResearchPatch::StatusJson`: `notImported`·`off`·`noCatalog`·`upToDate`·`added`, SR 전체
    주문 수, 넣은 수, 시각)를 남기고, 패널을 열 때와 상태가 바뀔 때 `onSpellResearchStatus` 로 보낸다. 패널은
    설정 > 마법서 학습 맨 위의 "Spell Research" 줄에 보여 준다(`modules/spellResearchStatus.js`, SR 이 없으면 숨김).
  - 고른 방법이 아닌 것: SR 패치 형식(ESP + 임포트 퀘스트 스크립트)의 작은 패치 동봉 — 'ESP 없이' 결정을
    뒤집고 플러그인 칸 하나가 든다. Fixes 의 JSON 캐시에 직접 쓰기 — 다른 모드 파일이고, 캐시에 없는 주문도 목록으로
    읽히므로 동작에 차이가 없다. `Debug.MessageBox` 가로채기 — 모든 모드의 메시지 상자를 건드린다.
- 퍽 어댑터의 FormList 줄과 다른 점: 그쪽은 게임 시작 때 목록 레코드 배열에 메모리로만 넣는다(세이브에 안 남음).
  SR 목록은 SR 이 Import 때마다 `Revert()` 하고 세이브 쪽 등록만 쓰므로 `AddForm` 경로가 맞다.

## 4. 마법책 짝

- 게임은 로드된 책에서 주문마다 가르치는 첫 책을 찾는다(스캐너의 `SpellScanner::TomesBySpell`, 트리의 마법책 판정과 같은 규칙).
  SR 의 Import 가 끝날 때 뜨는 메시지 상자마다 짝 목록을 먼저 다시 읽는다(`SpellTomes::Rebuild("import")`, 항목 수가 같아도).
  HoM 이 이 목록을 마법책 짝의 근거로 읽는 규칙(JSON 우선, 이 목록은 검증 안 된 대체 수단)은 `SpellTomePairs.h`.
- 등급별로 `_SR_ListTomes{등급}` 과 `_SR_ListTomeSpells{등급}` 의 길이가 **같고**, 레코드에 적힌 항목 수(`forms`)도 같을 때만
  넣는다(Papyrus 인덱스는 레코드 항목 다음에 스크립트 추가분을 센다). 다르면(다른 패치가
  짝을 어긋나게 넣음) 그 등급의 마법책은 건너뛰고 보고서와 로그에 남긴다. 책이나 주문이 이미 들어 있으면 건너뛴다
  (SR 과 같은 조건).
- 스크롤 짝은 다음 단계(스캔이 주문↔스크롤 짝을 모른다. 이펙트 목록이 같은 스크롤 하나만 짝으로 보는 방식으로
  따로 측정한 뒤).

## 5. 규칙 파일

`SKSE/Plugins/SpellLearning/librarian/compat/spellresearch.json`. 퍽 어댑터와 **같은 줄 형식**(`formList` 줄)과 같은
판정기(`BuildPlan`)를 쓴다. 따로 `compat/` 에 두는 이유: `adapters/` 의 파일은 게임 시작 때 목록 레코드에
메모리로 쓰이고(3절 마지막 줄), `librarian/` 바로 아래의 JSON 은 사서 태그 룰 파일로 읽힌다.

- `lists`: 등급별 `allSpells`·`tomes`·`tomeSpells`, `school`·`casting`·`target`, 이름표 `element`(34)·`technique`(15),
  전역 `isSetup`(`_SR_GlobalIsSetup`)·`importing`(`_SR_GlobalImporting`). ID 는 SR 2.2.3 ESP 에서 뽑았다. 하나라도
  빠지면 등록하지 않는다(로그 경고). SR 버전이 바뀌어 ID 가 없으면 역시 꺼진다.
- 원소·기법 줄만 `adapters` 에 적는다(51줄: 원소 34 + light·arcane 보조 줄, 기법 15). 대상은 SR 목록의 영구 id(`SpellResearch.esp|0x…`). 런타임에는 FormList 의
  EditorID 가 남지 않으므로 이름 대신 id 를 쓴다(퍽 어댑터와 같음).
- 학파·등급·시전·대상 4축과 마법책 짝은 코드가 카탈로그·스캔 필드를 그대로 옮긴다. 파일에는 그 목록들의 id 표만
  둔다.
- "SR 원소가 하나라도 있는 주문"(arcane 줄의 차집합)을 위해 줄 조건 `formListsNone` 을 더한다: 이 목록들 중
  앞선 `formList` 줄이 고르지 않았어야 한다(같은 판정 안에서만 센다 — 레코드나 다른 패치가 이미 넣은 것은 보지 않는다). arcane 줄들은 다른 원소 줄
  뒤에 둔다.

## 5-1. 측정 (2026-10-03, TAKEALOOK 고정 덤프 3155 주문)

`librarian-test -i <덤프> --catalog <카탈로그> --spellresearch librarian/compat/spellresearch.json -a <정답셋>`:

- 3155 주문 모두 학파·등급이 있어 등록된다. 원소 상위: soul 592, fire 407, undead 356, daedra 303, shock 301,
  frost 241, health 232, arcane 194, poison 194. 기법: summoning 800, control 737, cloak 154, siphon 133
- 정답셋(조인 119): SR 라벨 P 84.8 / R 81.7 / F1 83.2. 사서 태그를 그대로 채점하면 82.5 — 접기가 정답셋에 더 맞다.
  `librarian-check` 기준선(`accuracy.spellResearch`)에 들어 있어 사서 룰이 바뀌어 떨어지면 실패한다
- 위 숫자는 마법책 표시(`taughtByTome`)가 없는 고정 덤프라 NPC·퀘스트 주문까지 센 값이다(기준선용). 게임과 같은
  거르기는 표시가 있는 스캔에서만 된다: 10-02 게임 스캔(3546)에서 마법책 주문 **1428** 이 등록되고 2118 은 빠진다.
  그 1428 의 라벨 점수(조인 104): F1 87.6. 상위: summoning 622, control 576, soul 446, fire 135, health 87, frost 74
- 보이는 이펙트가 하나도 없는 207 주문(고정 덤프)은 원소·기법 없이 4축만 받는다(`formList` 줄도 대상 이펙트를 고르는
  판정기 규칙). 전부 생물·NPC 공격(데이드로스 비틀거리는 공격, 클랜피어 돌진 등)이라 마법책 거르기에서 빠진다.
  그래서 판정기는 바꾸지 않았다
- 스캔 출력에는 마법책 id 가 없어 오프라인에서는 마법책 짝을 세지 못한다

## 6. 설정과 보고서

- `config.json` 의 `spellResearch.enabled`(키가 없으면 켜짐, `perkAdapters` 와 같은 규칙). 꺼도 이미 넣은 등록은
  그 세이브에 남는다 — SR 패치와 같은 방식이라 다음 재Import 때 비워진다.
- 카탈로그 옆 `spell_research_report.json`(등록이 실제로 돈 때만): `reason`(import | load), `alreadyRegistered`(SR
  등급 목록에 이미 있던 주문 수), `registered`, `entries`(목록 항목 수), `perList`, `tomes`, `failed`(등급 목록이 받지
  않은 주문 — 그 세션 동안 다시 시도하지 않는다), `tomeTiersSkipped`(짝 목록 길이가 달라 마법책을 건너뛴 등급),
  `noSchoolOrTier`, `leftOut`(`voiceSlot`·`gate`(주문 감별기의 out: 무료, 1시간 넘는 소환, 추방만, 규칙)·`duplicate`·`tomeUnreachable`·`player`: 트리처럼 뺀 마법책 주문 수), `lines`(줄별 gain), `elapsedMs`.
- 로그: 설치 때 한 줄(`SpellResearch: SpellResearch.esp found ...`), 등록 때 한 줄 요약, 첫 항목의
  `scriptAddedFormCount` 전후 값.
- 빠른 길: 전역 두 개와 등급 목록 항목 수를 먼저 본다. 수가 지난번과 같으면(Import 가 아닌 메시지 상자) 설정 파일도
  읽지 않고 끝. 다르면 카탈로그에서 등록할 주문만 골라, 없으면 끝(판정기는 돌지 않는다). 있으면 **그 주문들만** 판정기에
  넣는다(줄의 선택은 주문마다 따로라 결과가 같다). 규칙 파일을 못 읽어도 수는 기억해서 같은 경고를 되풀이하지 않는다.
  세이브를 불러오거나 새 게임, 전체 스캔이면 기억한 수를 지운다.

## 7. 테스트

1. **오프라인**: `librarian-test --spellresearch <규칙> --catalog <카탈로그> -i <덤프>` — 게임과 같은 판정기로
   주문별 SR 라벨을 만들고, SR 정답셋(조인 119건)과 축별 정밀도·재현율을 낸다. TAKEALOOK 덤프에서 목록별 등록 수도
   센다. 이 점수를 `librarian-check` 기준선에 넣어 사서 룰이 바뀌어 SR 점수가 떨어지면 실패로 잡는다.
2. **빌드**: `BuildRelease.ps1`.
3. **게임**(체크리스트):
   - SR Import 뒤 로그에 등록 요약이 남는다. 보고서의 등록 수가 0 이 아니다
   - Prisma UI 경험치 창(Y)이 정상이다
   - 논문 조합으로 우리가 등록한 모드 주문(예: 괴저 불씨 = 파괴·`arcane`)이 후보로 나온다
   - 모드 주문을 마법책으로 베껴 쓰면 그 주문의 책이 나온다
   - 세이브를 다시 불러와도 중복 등록이 없다. 재Import 뒤에도 등록 수가 같다
   - **`AddForm` 경로 확인**: 첫 등록 때 로그가 목록 하나의 `scriptAddedFormCount` 전후 값을 남긴다(늘어야 한다).
     저장 → 불러오기 뒤에도 등록이 남아 있고, 재Import 직후(맞춤 전) 우리 주문이 목록에서 빠지는지 본다.
     `forms` 배열에 들어갔다면 `Revert()` 가 지우지 못해 마법책 짝이 어긋난다

## 7-1. Spell Research - Prisma UI (Nexus 187900, 0.2.0 확인)

SR 의 메시지 상자와 메뉴를 PrismaUI 창으로 바꾸는 모드. `_SR_QuestScript`, `_SR_EffectComposeSpellScript`,
`_SR_EffectResearchScript`, `_SR_FurnitureCauldronScript`, `_SRU_Utility`, `_SRU_AnalyzeSpell` 을 덮어쓴다
(2026-10-03 설치본 소스로 확인).

- **Import 감지는 그대로 된다**: `_SR_ImportMain` 은 덮어쓰지 않아 "All Spells Imported" 는 원래 메시지 상자다
  (3절 시점 1). 상자 없이 조용히 Import 하는 빌드는 끝에서 `SendModEvent("SR_ImportFinished")` 를 보내면 시점 1 이
  그대로 돈다(2026-10-31). 상자도 이벤트도 없으면 2·3 만 남는다.
- **실제 조합은 우리 등록을 본다**: 덮어쓴 조합 스크립트도 목록을 Papyrus `HasForm`/`GetAt` 으로 그 자리에서 읽는다.
- **확인 안 됨 — 조합 창의 미리보기**: 후보 수와 reach 는 DLL 의 `BuildComposeCache` 가 따로 센다(소스 없음). 세이브 쪽
  등록(`scriptAddedTempForms`)을 읽는지, 언제 다시 만드는지 모른다. 못 읽거나 맞춤 전에 만든 캐시라면 창의 숫자에는
  우리 주문이 빠지고 실제 결과에는 나온다.
- 게임 확인(7절 3 에 더해): 우리 주문만 맞는 논문 조합에서 조합 창의 후보 수가 0 이 아닌지, 그 조합으로 실제
  우리 주문이 나오는지. `SpellResearchPrismaUI.log` 의 `Compose Spell cache: …` 줄이 캐시를 만든 때다.

## 8. 하지 않는 것

- SR 게임플레이(연구 세션·저널·실험 실패) 재구현, SR 어휘를 사서 태그로 채택(Concept.md).
- `_SR_ListUndiscoverableSpells`: 등록하는 주문은 모두 마법책이 가르치는 주문이라 비워 둔다.
- 유물·연금 목록: 주문이 아니다.

## 9. 연습과 기억 (2026-11-02, 빌드만 됨 · 게임 확인 전)

HoM 의 시전 XP 는 "연습", SR 의 아키타입 XP(아키타입 60개 x 등급 5개의 GlobalVariable)는 "기억"이다. 둘이 서로를
채운다. 설계와 이유: `docs/superpowers/specs/2026-11-02-sr-memory-exchange-design.md`.
코드: `include/librarian/SpellResearchMemoryMath.h`(순수 계산, `progression-check --self-test` 가 확인) ·
`include/librarian/SpellResearchMemory.h` · `src/librarian/LibrarianSpellResearchMemory.cpp`(게임 쪽).

- **A. 연습이 기억으로 남는다.** `ProgressionManager::OnSpellCast` 에서 `AddXP` 앞뒤의 진행도를 비교해, 이번
  시전이 넘은 경계(15, 30, ... 90%, 그리고 100%) 하나마다 연구 한 번 값을 낸다:
  `v = _SR_GlobalResearchTimeBase(0x2B82, 15.0) x memoryMinutes / 60`(기본 25분 → 6.25). SR 의 연구 공식
  (`_SR_EffectResearchScript.UpdateArchetypeList`)대로, 주문 등급 R, 슬롯 s = 0..6, 대상 등급 t = R + 2 - s
  (0..4 안만)에 대해 `global[a][t] += v ^ ((s + R + 1) / 3)`, 주문이 든 모든 아키타입 a 에. SR 의
  `ModGlobalVariable` 규칙: 값이 10,000,000 미만일 때만 더하고 넘으면 10,000,000 으로 자른다. 경계마다 고정
  금액인 이유: 공식이 선형이 아니라 넘은 폭을 합쳐 내면 총액이 잘린 방식에 따라 달라진다.
- **B. 기억이 연습을 이끈다.** 같은 함수에서 마법책 소지 보너스 바로 뒤, 출처별 상한 앞:
  `xpGain x (1 + maxPracticeBoost% x known)`. `known` = 주문의 아키타입마다 `min(1, global[a][R] / KnownFull(R))`
  의 평균, `KnownFull(R) = ((R + 1) x d)^2`, d = `_SR_GlobalCraftSpellDifficulty`(0x9EB3, ESP 값 3.0; 호출마다 읽고
  없거나 0 이하·유한하지 않으면 3.0). Novice 9, Master 225. 기준을 SR 의 조합 난이도 척도로 잡은 이유: SR 이
  조합 확률에 쓰는 크기(`_SR_EffectComposeSpellScript.psc:249`, `Math.Pow((SpellRank + 1) x difficulty, 2)`)라
  "이 등급에서 이만큼이면 충분히 안다" 를 SR 이 정한 값으로 잴 수 있다. 처음 설계의 "값 > 0" 은 A 의 슬롯 2
  지급(t = R)이 첫 15% 에서 모든 global[a][R] 를 0 보다 크게 만들어 B 가 곧바로 +25% 고정이 됐다(리뷰 1차,
  2026-11-02). 상한은 그대로 걸린다.
- **시전 XP 만.** 마법책 읽기, 패시브, ISL 공부, DEST, BookXP, 다른 모드 출처, `AddRawXP`/`SetSpellXP` 는 A 에
  들어가지 않는다(SR 이 HoM 에 주는 `spell_research` 몫이 기억으로 되돌아오지 않게).
- **안 하는 때(디버그 로그만):** `SpellResearch.esp` 없음, `_SR_GlobalIsSetup` != 1 이거나 `_SR_GlobalImporting`
  != 0, 해당 스위치 꺼짐, 주문이 SR 등급 목록(`lists.allSpells`)이나 아키타입 목록 어디에도 없음. 등급은 주문이
  든 가장 낮은 등급 목록.
- **목록:** `_SR_ListSpellsArchetypes`(`lists.archetypeSpells`, 0xD9E, FormList 60개)와
  `_SR_ListGlobalsArchetypes`(`lists.archetypeGlobals`, 0x133D, FormList 60개 x TESGlobal 5개, 0 Novice ... 4 Master)를
  같은 순서로 짝짓는다. 바깥 목록은 인덱스로 읽는다(`forms[i]`, 그다음 스크립트 추가분을 저장된 순서대로):
  빈 항목도 자리를 지켜 뒤 아키타입이 이웃의 전역으로 밀리지 않는다(`ForEachForm` 은 빈 항목을 건너뛴다).
  소속은 `HasForm`(플러그인 항목 + `scriptAddedTempForms`, SR 의 Import 와 우리 등록이
  있는 곳). 폼은 kDataLoaded 에 한 번 찾고(두 바깥 목록의 길이가 다르면 기능 전체를 끈다), 등급과 소속은
  호출마다 새로 읽는다(캐시 없음: Import 가 목록 크기를 그대로 두고 내용을 바꿀 수 있다).
- **로그:** 지급마다 info 한 줄(주문 이름·formID, 진행도, 넘은 경계 수, v, 등급, 아키타입 수, 바뀐 전역 수).
  B 의 배율은 debug. kPostLoadGame 에 SR 이 준비돼 있으면 info 한 줄: 기억 전역 300개의 합과 0 보다 큰 개수
  (전역에 직접 쓴 값이 세이브에 남는지 확인용 - 저장 → 불러오기 뒤 이 줄이 같아야 한다).
- **설정:** `config.json` `spellResearch.memoryExchange` = `{ practiceToMemory: true, memoryMinutes: 25,
  memoryToPractice: true, maxPracticeBoost: 25 }`. 빠진 키는 기본값, `memoryMinutes` 0 - 240, `maxPracticeBoost`
  0 - 100(%)로 자른다. 패널: 설정 > 마법서 학습, "Spell Research" 줄 아래 스위치 둘과 숫자 둘(SR 이 없으면 줄과
  함께 숨는다, `modules/spellResearchMemorySetting.js`). 상태 줄의 `memoryReady`(이 부분이 kDataLoaded 에 SR 의
  목록과 전역을 찾았는지)가 true 일 때만 보인다. `spellResearch.enabled`(등록 스위치)와는 따로다.
- **한계(코드에 `// LIMIT:`):** (1) 주문마다 얼마를 냈는지 기록하지 않는다. 다른 출처가 올린 진행은 그 구간의
  경계를 넘는 다음 시전 때 함께 치러지고, 시전이 아닌 출처가 넘긴 경계는 치러지지 않는다. 진행도를 되돌리면
  다시 치러진다: `RelockSpell`(치트 모드)이나 `SetSpellXP(주문, 0)`(Papyrus/C++ API, 패널의 XP 편집) 뒤 다시
  연습하면 같은 주문이 7번을 또 낸다. 고칠 길: 코세이브 진행 기록에 주문마다 "낸 가장 높은 경계" 1바이트(버전
  올림). (2) SR 의 Papyrus `GlobalVariable.Mod` 는 GetValue 후 SetValue 라, 그 사이에 C++ 쓰기가 끼면 한쪽이
  사라진다. SR 연구가 도는 중에 시전이 경계를 넘을 때만, 한 번에 한 지급까지. 막지 않는다.
- **SR 자체 연구 모드 보상과 겹친다**(`_SR_EffectResearchScript.OnSpellCast`, 연구 중 장착 주문 시전마다 v = 1.5):
  둘 다 연습이라 일부러 둔다.
- **테스트:** `progression-check --self-test` 의 `memory:` 사례(경계, 등급별 이득표, 상한, 아는 비율, 배율, 설정).
  게임 안: 개발용 자가 테스트 `sr-memory`(`src/environment/EnvironmentSelfTestMemory.cpp`): 등급 목록에서 아키타입이
  있는 첫 주문의 전역을 저장해 두고, 게임 쪽 `Pay` 로 한 번 지급한 뒤 모든 전역을 계산 헤더의 값과 비교하고,
  RAII 가드로 되돌린 뒤(예외·중도 반환에도) 되돌려졌는지와 B 의 배율을 공식과 비교한다. 등급 대응은 이름으로
  따로 본다(각 전역의 editor id 가 `<t+1><Rank>` 로 끝나야 한다: `_SR_GlobalArchetype{Group}{Name}{1..5}{Rank}`).
  `sr-memory-hook`: 같은 가드 안에서 훅이 부르는 `OnPracticeProgress(주문, 10%, 32%)` 가 두 번 지급하는지(전역마다
  `ApplyGain(ApplyGain(전, g), g)`); practiceToMemory 가 꺼져 있으면 SKIP. SR 이 준비되지 않았으면 둘 다 SKIP.
- **게임 확인 할 것:** 학습 대상 주문을 시전해 15% 를 넘길 때 로그의 지급 줄, SR MCM 의 아키타입 지식이 늘었는지,
  저장 → 불러오기 뒤 kPostLoadGame 줄의 합이 그대로인지(직접 쓴 전역이 세이브에 남는지는 증명되지 않았다).
