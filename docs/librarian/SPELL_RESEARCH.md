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
  1. `MessageBoxMenu` 가 닫힐 때, `_SR_GlobalImporting == 0` 이고 `_SR_GlobalIsSetup == 1` 이며 등급 목록 크기가
     지난번 맞춤 뒤로 바뀌었으면(= 방금 Import 가 끝남). 다른 메시지 상자에는 목록 크기가 그대로라 돌지 않는다.
  2. 세이브를 불러온 뒤(`kPostLoadGame`) `IsSetup == 1` 이면. HoM 을 나중에 설치한 세이브, 메시지 상자를 다른
     모드가 숨긴 경우를 여기서 잡는다. 로드 때 도는 SR 스크립트는 목록을 건드리지 않는다: Fixes 1.4.3 의
     `_SR_AliasPlayerScript.OnPlayerLoadGame` 은 `Maintenance()`(애니메이션 3개 로드)만 부른다(소스 확인).
- Fixes 의 JSON 캐시(`_SRU_Cache`, MCM 에서 플레이어가 누를 때만 만든다)는 목록에서 만든 주문별 아키타입 사본이다.
  연구는 캐시에 있는 주문만 캐시로 읽고, 없는 주문은 목록을 읽는다. 그래서 캐시를 만든 **뒤에** 등록한 주문도
  목록 쪽으로 정상 동작한다. 캐시를 다시 만들면 우리 등록도 들어간다.
  3. 전체 스캔이 카탈로그를 새로 쓴 뒤(패널의 트리 스캔, Papyrus `RunScan` — 퍽 어댑터가 다시 도는 자리). HoM 을 처음
     깔고 처음 스캔한 세이브, 새 모드를 넣고 다시 스캔한 경우를 다음 로드까지 기다리지 않는다.
- 맞춤:
  - **마법책이 가르치는 주문만** 대상이다(로드된 책의 `TeachesSpell`, 스캐너의 `TomesBySpell`). 전체 스캔의 카탈로그에는
    NPC·퀘스트·생물 주문도 들어 있다(TAKEALOOK 3546 중 2118). 이것까지 넣으면 플레이어용 원본과 같은 이름의 NPC 판
    (수치·퍽 연동이 다름)이 조합 후보에 함께 뜬다(2026-10-03 리뷰에서 발견).
  - 마법책은 있는데 그 책을 나눠 주는 곳이 없는 주문(`tomeUnreachable`, 트리에서는 빠짐)도 **등록한다** — SR 의 연구가
    그 주문에 닿는 길이 된다. 의도한 결정이다.
  - 카탈로그 주문 중 **어느 `_SR_ListAllSpells{등급}` 에도 없는 주문만** 등록한다. SR 패치가 등록한 주문은
    원작 라벨 그대로 둔다(우리 라벨을 섞지 않는다). 우리가 지난번에 넣은 주문도 여기서 건너뛴다 — 몇 번을 돌아도
    결과가 같다.
  - 엔진의 `BGSListForm::AddForm`(Papyrus `AddForm` 과 같은 함수)으로 넣는다. 그래서 SR 패치의 등록과 똑같이
    세이브에 남고, 다음 재Import 때 SR 의 `Revert()` 로 함께 비워진 뒤 맞춤이 다시 넣는다.
  - 등록 순서는 SR 과 같다: 등급, 학파, 시전, 대상, 원소, 기법, 그다음 마법책 짝.
- 퍽 어댑터의 FormList 줄과 다른 점: 그쪽은 게임 시작 때 목록 레코드 배열에 메모리로만 넣는다(세이브에 안 남음).
  SR 목록은 SR 이 Import 때마다 `Revert()` 하고 세이브 쪽 등록만 쓰므로 `AddForm` 경로가 맞다.

## 4. 마법책 짝

- 게임은 로드된 책에서 주문마다 가르치는 첫 책을 찾는다(스캐너의 `SpellScanner::TomesBySpell`, 트리의 마법책 판정과 같은 규칙).
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
- 카탈로그 옆 `spell_research_report.json`(등록이 실제로 돈 때만): `reason`(import | load | scan), `alreadyRegistered`(SR
  등급 목록에 이미 있던 주문 수), `registered`, `entries`(목록 항목 수), `perList`, `tomes`, `failed`(등급 목록이 받지
  않은 주문 — 그 세션 동안 다시 시도하지 않는다), `tomeTiersSkipped`(짝 목록 길이가 달라 마법책을 건너뛴 등급),
  `noSchoolOrTier`, `lines`(줄별 gain), `elapsedMs`.
- 로그: 설치 때 한 줄(`SpellResearch: SpellResearch.esp found ...`), 등록 때 한 줄 요약, 첫 항목의
  `scriptAddedFormCount` 전후 값.
- 게임 안 알림(2026-10-05): 한 주문이라도 등록하면 HUD 에 한 줄(영어로는 `Heart of Magic: N mod spells added
  to Spell Research`). SR 의 "All Spells Imported" 상자를 닫은 직후(import), HoM 을 나중에 깐 세이브를 처음 불러올
  때(load), 새 모드를 넣고 스캔했을 때(scan) 뜬다. 새로 넣은 주문이 없으면 아무것도 띄우지 않는다. SR MCM 의
  "Total spells imported" 는 SR 자신의 Import(기본 게임과 DLC)에 이 수를 더한 값이다.
  - 문구는 패널 언어 파일의 `notifications.spellResearchAdded`(`{{count}}` 자리에 수). DLL 이 설정을 적용할 때
    (`ApplySettingsFromConfig`) 패널 언어(설정의 `language`, 비었으면 `lang/locale.js`)의 `lang/<code>.json` 에서 읽고,
    없으면 `en.json`, 그것도 없으면 코드의 영어(`kAddedNoticeEnglish`). `PanelText`(UIManagerLocale.cpp).
  - 끄기: 설정 > 조기 습득의 알림 줄 아래 "Spell Research 임포트 알림"(`notifications.spellResearchNotice`, 기본 켜짐).
    알림만 끈다 - 등록은 그대로 한다(등록 자체는 `config.json` 의 `spellResearch.enabled`).
  - 같은 날 고침: 패널이 저장하는 설정에 `notifications` 가 빠져 있어서, "약화된 주문 알림" 스위치와 간격도 DLL 에는
    닿지 않고 기본값으로 돌았다. 이제 함께 저장된다.
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
  (3절 시점 1). 이 상자를 나중에 `SRPrismaUI.ShowNotice` 로 바꾸면 시점 1 이 사라지고 2·3 만 남는다.
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
