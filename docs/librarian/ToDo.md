# ToDo — HoM 태그 도서관

> 상태: 초안 v0.1 (2026-09-07). Concept.md v0.2 기준.
> 원칙: **한 번에 한 모듈**, 굴러가는 걸 먼저 만들고 고친다. 완벽주의 금물.
> 이 문서는 포크 리포가 생기면 `docs/` 로 옮긴다.

---

## 0. 전제 확인 결과

- `C:\Users\qtuna\Desktop\HeartOfMagic-master` = HoM **v2.4.0 소스** 맞음
- 단, **git 리포가 아니라 zip 풀어놓은 것**. `.git` 없음, `plugins/external/commonlibsse-ng/` 비어 있음(서브모듈 미초기화) → 이 상태로는 빌드 불가
- `SpellScannerScan.cpp` = **711줄**. CLAUDE.md 600줄 제한 이미 위반. 스캔 JSON 생성 블록이
  `ScanSpellsToJson`(L200~310)과 `ScanSpellTomes`(L435~530)에 **복붙 중복** — CLAUDE.md "코드 중복 금지" 위반
  → M1에서 공통 함수로 추출하면 두 규칙 위반을 동시에 해소하면서 우리 필드를 한 곳에만 추가할 수 있다
- MGEF 필드는 CommonLibSSE-NG 헤더로 확인 완료 (`RE/E/EffectSetting.h`, `RE/E/EffectArchetypes.h`, `RE/B/BGSKeywordForm.h`):
  `data.archetype`(47종), `data.primaryAV`, `data.secondaryAV`, `data.resistVariable`, `data.associatedForm`,
  `data.flags`(kHostile/kDetrimental), `data.castingType`, `data.delivery`, `GetKeywords()`, **`AddKeywords()`**(M6에 필요)

---

## 1. 지켜야 할 것 (HoM CLAUDE.md 요약 + 우리 규칙)

- 파일 600줄 제한. RE/SKSE 타입은 헤더 확인 후 사용. `new/delete` 금지(스마트 포인터). 매직넘버 금지
- 코드 복붙 변형 금지 — `*Impl` 추출 후 양쪽에서 호출
- PrismaUI JS는 `var`만, ES6 모듈 금지, 테마 변수 사용
- AI 서명 금지. 코드 변경 시 `docs/` 같은 세션에 갱신
- 빌드는 `.\BuildRelease.ps1`만. 빌드 전 SkyrimSE 프로세스 확인
- **우리 규칙**: 승인 전 임의 수정 금지 / 확정 스택 외 라이브러리 추가 금지 / 분류 룰은 코드가 아니라 **JSON 데이터**로 (사용자·타 모더가 패치 없이 룰 추가 가능 = 호환성)
- **확정 스택 = C++23(CMake · nlohmann_json · spdlog) + PowerShell + PrismaUI JS + Papyrus.**
  이 규칙은 라이브러리뿐 아니라 **언어·도구에도 적용된다 — Python 을 쓰지 않는다.**
  게임 없이 도는 분석·측정 도구는 `tools/treebuilder-test.cpp` 패턴(스텁 헤더로 RE/SKSE 를
  가린 독립 콘솔 exe)을 따른다. 일회성 확인은 PowerShell `ConvertFrom-Json` 으로 충분하다
- 작업은 항상 작업 브랜치, PR로 main 병합

---

## 2. 데이터 구조

### 2-1. 스캔 덤프 확장 (`spell_scan_output.json`, M1)
기존 스키마 유지 + `effects[]` 항목에 아래 추가 (필드 토글 `effectDetails`):
```json
{
  "name": "소각", "magnitude": 60, "duration": 0, "area": 0, "description": "…",
  "keywords":     ["MagicDamageFire"],        // baseEffect->GetKeywords() EditorID
  "archetype":    "ValueModifier",            // EffectArchetypes 이름표 (숫자 아님)
  "primaryAV":    "Health",                   // ActorValue 이름표
  "secondaryAV":  "None",
  "resistance":   "ResistFire",               // resistVariable
  "hostile": true, "detrimental": true,
  "castingType":  "FireAndForget", "delivery": "Aimed",   // 이펙트 레벨
  "magicSkill":   "Destruction",
  "associatedForm": "Skyrim.esm|0x0001CB01"   // 있을 때만 (소환체·바운드 무기 등)
}
```

### 2-2. 카탈로그 (`SKSE/Plugins/SpellLearning/spell_catalog.json`, M4)
```json
{
  "version": 1, "vocab": "tags-v1", "generated": "…",
  "spells": {
    "Skyrim.esm|0x10FD5F": {
      "school": "destruction", "tier": "expert", "casting": "fireforget", "targeting": ["actor"],
      "elements": ["fire"], "techniques": [],
      "source": { "elements": "mgef", "techniques": "mgef" },   // mgef | framework | text | llm | manual
      "confidence": 0.9
    }
  }
}
```

### 2-3. 분류 룰 (`SKSE/Plugins/SpellLearning/librarian/*.json`, M4)
```json
{ "match": { "mgefKeyword": "MagicDamageFire" },              "add": { "elements": ["fire"] } }
{ "match": { "archetype": "SummonCreature" },                 "add": { "techniques": ["summoning"] } }
{ "match": { "archetype": "Cloak" },                          "add": { "techniques": ["cloak"] } }
{ "match": { "spellKeyword": "KIT_SpellDamageType_Fire_D" },  "add": { "elements": ["fire"] }, "tier": "framework" }
```
파일 단위로 `00_mgef.json`(기본 배포) / `10_kit.json` / `10_ocf.json` / `90_user.json` 처럼 쌓는다.

---

## 3. 모듈 (의존성 순)

### [x] M0. 리포 셋업 (2026-09-07 완료, 포크만 보류)
- [ ] GitHub 포크 — **보류.** 사용자가 "아직 계획 단계"로 판단. 지금은 업스트림
      `DinkelTwinkel/HeartOfMagic` 를 그대로 clone 했고 `origin` 도 업스트림을 가리킨다.
      포크를 만들면 `git remote set-url origin <포크 URL>` 한 줄로 전환된다
- [x] `git clone --recurse-submodules` → `C:\Dev\HeartOfMagic` (master `b1eb972`, VERSION 2.4.0,
      commonlibsse-ng `v4.2.0-2-g2527ccd47` 체크아웃됨)
- [x] `DownloadExternalDeps.ps1` — papyrus 컴파일러 내려받음
- [x] `Build_Config_Local.ps1` 작성. **이 PC에는 VS 2022가 없고 VS 2026(v18) Community 만 있다**
      → dev shell = `C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1`,
      빌드는 항상 `.\BuildRelease.ps1 -preset Release-2026`
- [x] 손대기 전 `.\BuildRelease.ps1 -preset Release-2026` 1회 성공 (기준선).
      산출: `dist/HeartOfMagic_2.4.0/` + `.zip`, DLL 3종(SpellLearning / SL_BookXP / DontEatSpellTomes), `.pex` 컴파일됨
- [x] 작업 브랜치 `feature/librarian` 생성
- [x] Concept.md / ToDo.md를 `docs/librarian/` 로 복사, 초기 커밋. **이제 이 사본이 정본이다**
- 산출: 빌드되는 로컬 리포. 배포는 `dist/` 수동 복사 (`$defaultOutputPath` 는 CLAUDE.md 에 문서만 있고
  v2.4.0 스크립트 어디서도 쓰이지 않는다)

### [x] M0-T. 헤드리스 테스트 환경 (2026-09-07 검증 완료)
- [x] DevBench 1.18.0 설치 (`D:\TAKEALOOK\mods\devbench`), SE 포트 `127.0.0.1:8920`
- [x] `Skyrim.ini [General] bAlwaysActive=1` — 창 비활성 시에도 프레임 진행 확인
- [x] 연결 경로: Chrome(사용자 PC) → REST `POST /api/tool/<name>`. MCP 클라이언트 설정 불필요
- [x] 검증된 루프: `health` → `game loadLast` → `postLoadGame` 대기(약 20초) → `inspect scene/player` → `papyrus call SpellLearning.*` 반환값 수신
- 확인된 HoM Papyrus 전역 함수: GetVersion, GetLearningMode, IsSpellUnlocked, GetSpellProgress, GetAllLearningTargets, AddSourcedXP, SetLearningTarget … (**스캔 트리거 없음** → M1에서 추가)
- 미해결: `console capture=true` 읽기가 빈 결과(markersFound=false). 콘솔 대체 모드와 충돌 추정. Papyrus 호출로 대체 가능하므로 보류
- 남은 구멍: **빌드·게임 실행용 셸 없음.** 코드 수정 후 빌드는 아직 사용자가 직접. 셸 MCP 등록 시 완전 자동화

### [~] M1. 스캐너 리팩터 + MGEF 필드 (2026-09-07 구현, 2026-09-09 게임 검증 → 버그 2건 수정, 재검증 대기)
- 역할: 스캔 JSON 생성을 한 곳으로 모으고, 이펙트 구조 정보를 내보낸다
- 입력: `RE::SpellItem*`, `FieldConfig` / 출력: 2-1 스키마의 `json`
- 파일:
  - [x] 신규 `src/spellscanner/SpellScannerJson.cpp` — `json BuildSpellJson(RE::SpellItem*, RE::FormID, const FieldConfig&)`
        + `json BuildEffectJson(const RE::Effect*, const FieldConfig&)` + 이름표 헬퍼(`ArchetypeName`, `ActorValueName`)
  - [x] `SpellScannerScan.cpp` — 중복 블록 2개를 `BuildSpellJson` 호출로 교체. **711 → 523줄.**
        `GetSpellInfoByFormId` 의 세 번째 사본도 `BuildEffectJson` 으로 통합
  - [x] `include/SpellScanner.h` — `FieldConfig::effectDetails` 추가, 새 함수 선언
  - [x] `SpellScannerHelpers.cpp` — `ParseFieldConfig` + `ParseScanConfig` 양쪽에 `effectDetails` 파싱
  - [x] `PrismaUI/.../modules/llmApiSettings.js` — 프리셋 3종에 `effectDetails` 추가(`full` 만 true).
        `modules/state.js` 기본값과 `script.js` 의 `fieldIds` 목록도 갱신. C++ 쪽 거울은
        `SpellScannerJson.cpp` 의 `FieldsForPreset()` — 한쪽만 고치지 말 것
  - [x] **스캔 트리거** — Papyrus 전역 함수 `SpellLearning.RunScan(string mode, string preset) -> string`(출력 파일 경로 반환) 추가. `PapyrusAPI.cpp`에 등록. DevBench `papyrus call`로 외부에서 호출 가능해짐. C-ABI 등록 불필요
  - [x] (별건) 덤프 선두 `[CANVAS]` 로그 오염 — **원인은 `UIManagerIO.cpp` 가 아니었다.**
        `modules/cppCallbacks.js` 의 `window.debugOutput` 이 디버그 줄을 `outputArea` textarea **맨 앞에 붙여** 넣고,
        저장 버튼(`onSaveClick`)이 그 textarea 내용을 그대로 C++ 로 넘겨 파일에 쓴다.
        `debugOutput` 을 `console.log` 전용으로 바꿔 해결. `RunScan` 은 애초에 textarea 를 거치지 않는다
- 의존: M0
- 테스트: **빌드 통과 (VS 2026, 경고 0).**
  **2026-09-09 게임 검증 1회차 — MGEF 필드는 나온다.** 톰 모드 + full 로 1440 주문 / 4246 이펙트.
  덤프 선두 로그 오염 없음, `archetype` 문자열 31종, MGEF 키워드 이펙트 4233건 · 고유 731종
  (그중 바닐라 `Magic*` 34종, `MagicDamageFire` 73건). 전량 스캔에서 0건이던 정보가 100% 나온다
- **버그 1 — ActorValue 필드가 로컬라이즈된다 (수정 완료, 재검증 대기).**
  `primaryAV`/`secondaryAV`/`resistance` 가 한글 로드오더에서 "체력", "화염 저항" 으로 나왔다.
  원인은 `RE::ActorValueToString` → `ActorValueList::GetActorValueName` 이 AVIF 의 **표시 이름**을
  반환하는 것. 영어 환경도 안전하지 않다 — 표시 이름은 "Resist Fire"(공백)이고 룰이 매칭할
  enum 이름은 `ResistFire` 다. 룰 JSON 은 전 사용자에게 같은 파일로 배포되므로 키가 언어마다
  달라지면 성립하지 않는다(Concept 8절 다국어 항목).
  → `SpellScannerJson.cpp` 의 `GetActorValueName` 이 `ActorValueList::GetActorValueInfo(av)->enumName`
  을 쓰도록 교체. `archetype`·`school`·`castingType`·`delivery` 는 손으로 쓴 switch 라 원래 안전했다
- **버그 2 — `RunScan` 이 게임 스레드에서 불리면 데드락 (수정 완료, 재검증 대기).**
  DevBench 는 네이티브를 게임 스레드에서 호출하는데, `RunScan` 이 다시 게임 스레드 태스크를
  큐에 넣고 기다렸다. 그 태스크는 호출이 끝나야 실행되므로 120초 타임아웃까지 교착하고,
  그 뒤에야 스캔이 150ms 만에 끝났으며 반환값은 빈 문자열. 게임은 그대로 행에 빠졌다
  (실제 Papyrus 스크립트는 VM 스레드라 이 경로를 타지 않는다).
  → `ThreadUtils.h` 에 `MarkGameThread()`/`IsOnGameThread()` 추가, `Main.cpp` 의 `MessageHandler`
  가 스레드 ID 를 찍고, `PapyrusAPI.cpp` 의 `RunScan` 은 같은 스레드면 인라인 실행
- 재검증 항목: `RunScan` 이 **즉시 경로를 반환**하는가 / `primaryAV`·`resistance` 가 영어 enum 이름인가
- 미검증: PrismaUI JS 테스트(`node run-tests.js`)를 **못 돌렸다 — 이 PC에 Node.js 가 없다.** JS 변경은 3곳뿐이고 모두 기계적
- 문서: `docs/ARCHITECTURE.md`(스캐너 절), `docs/PRESETS.md`

### [ ] M2. 룰 엔진 + 측정 하네스 — *C++, 게임 밖*
- 역할: 프레임워크 없는 환경의 진짜 커버리지를 안다. **여기서 나온 숫자가 룰의 우선순위를 정한다**
- **Python 을 쓰지 않는다.** 리포의 확정 스택은 C++23(CMake · nlohmann_json · spdlog) +
  PowerShell + PrismaUI JS + Papyrus 뿐이고 Python 은 리포에 한 줄도 없다. 1절의
  "확정 스택 외 추가 금지"가 언어에도 적용된다. `magi scan` 폴더의 `join_coverage.py`,
  `kwmap.py` 는 2026-09 이전 세션의 일회성 측정물이며 여기서 이식하고 더 쓰지 않는다
- 대신 `tools/treebuilder-test.cpp` 패턴을 따른다: 같은 CMake 빌드에서 나오는 독립 콘솔 exe 로,
  플러그인 소스를 직접 컴파일하고 `tools/include/` 의 스텁 헤더(PCH.h, Common.h)로 RE/SKSE 를
  가려 게임 없이 돈다. nlohmann_json 이 이미 링크돼 있다
- **M4 의 룰 엔진 두 파일을 여기서 먼저 만든다.** 룰 매칭은 태그 어휘에 의존하지 않으므로
  M3 앞에 와도 된다. 측정 하네스가 실제 분류 엔진을 그대로 컴파일하므로 프로토타입과 구현이
  갈라지지 않는다 (원래 M4 테스트 (3)의 "C++ 구현이 프로토타입과 같은가"가 통째로 사라진다)
- 입력: M1 덤프, `spellresearch_archetypes_1160.json`(정답셋) / 출력: `docs/librarian/MEASURED.md`,
  `librarian/00_mgef.json` 초안
- 파일:
  - [ ] `src/librarian/LibrarianRules.cpp` — 룰 파일 로드·검증, 파일명 순 병합 (M4 것을 앞당김)
  - [ ] `src/librarian/LibrarianClassify.cpp` — `TagSet Classify(const json& spell)` 순수 함수 (M4 것을 앞당김)
  - [ ] `include/librarian/Librarian.h`
  - [ ] `tools/librarian-test.cpp` + `tools/CMakeLists.txt` 에 타깃 추가 —
        덤프와 룰을 읽어 분류하고, 정답셋과 조인해 P/R 을 낸다.
        조인 키는 `(plugin.lower(), formId & 0xFFFFFF)` 정확 비교만. **ESL 12비트 폴백 금지**(오탐 100%)
- [ ] 새 덤프로 재조인 (톰 모드라 분모가 바뀐다 — 2026-09-09 실측 1440건, 정확 매칭 104쌍)
- [ ] `00_mgef.json` 초안 — MGEF 키워드·archetype·resistance·AV → 태그
- [ ] P/R: (a) MGEF만 (b) MGEF + 프레임워크 (c) + 이펙트명
- [ ] 전체 장서 중 "라벨 0개" 비율 — 이게 LLM 폴백이 감당할 크기
- 의존: M1
- 테스트: `librarian-test` 가 같은 입력에 같은 숫자를 낸다. 숫자를 Concept.md 2-2절에 반영

### [ ] M3. 태그 어휘 확정
- 역할: 태그의 정의를 못 박는다. 이후 모든 모듈이 이 목록만 쓴다
- 출력: `docs/librarian/TAGS.md` (태그·정의·예시 주문 3개씩), `include/librarian/TagVocabulary.h`(`static constexpr` 배열), `modules/tagVocabulary.js` — **두 파일은 내용 동일, 수동 동기화** (빌드 시 검증 스크립트 하나 두면 좋음)
- 의존: M2 (측정에서 실제로 구분되는 태그만 남긴다)
- 테스트: `librarian-test` 에 어휘 검사 모드를 추가 — `00_mgef.json` 의 모든 `add` 값이
  `TagVocabulary.h` 에 있는지. 어휘가 생기면 `LibrarianRules.cpp` 의 검증(어휘 밖 태그 →
  경고 후 무시)도 이때 켠다

### [ ] M4. 사서 게임 내 연결 + 카탈로그
- 역할: 스캔 결과에 태그를 붙여 카탈로그로 저장. 1·2단계(MGEF 룰, 프레임워크 룰)만. 텍스트·LLM은 M7
- **분류 엔진(`LibrarianRules.cpp`, `LibrarianClassify.cpp`)은 M2 에서 이미 만들어졌다.**
  여기서는 그것을 게임 안에 붙이고 결과를 파일로 남기는 일만 한다
- 입력: 스캔 JSON, `librarian/*.json` 룰 / 출력: `spell_catalog.json`(2-2)
- 파일 (`src/librarian/`, 각 600줄 이하):
  - [ ] `LibrarianCatalog.cpp` — 카탈로그 읽기/쓰기/버전, `persistentId` 키
  - [ ] 훅: 스캔 완료 직후 자동 실행 (`UIManagerScanner.cpp`의 스캔 태스크 끝에서). 별도 버튼 불필요
- 의존: M2(엔진), M3(어휘)
- 테스트: (1) 빌드 (2) 게임 스캔 후 카탈로그 생성 확인 (3) 그 카탈로그를 `librarian-test` 에
  먹여 M2 의 (b) 숫자가 그대로 나오는지 — 게임 안 경로와 오프라인 경로가 같은 결과를 내는가
- 문서: 신규 `docs/librarian/LIBRARIAN.md`, CLAUDE.md 결정 트리에 추가

### [ ] M5. 손님 1 — HoM 트리
- 역할: 태그를 트리 빌더 특징량으로 주입. 어댑터 = "태그 → 토큰"
- 파일: `treebuilder/TreeNLP.cpp` — TF-IDF 코퍼스에 `tag:fire` 형태 토큰 추가(가중치 상수), `edgeScoring.js:207` 동일
- 입력: 카탈로그 / 출력: 없음(트리 품질)
- 의존: M4
- 테스트: 태그 있음/없음으로 트리 2회 생성, 같은 학파 내 간선이 원소별로 뭉치는지 정성 비교. 스크린샷 2장
- 문서: `docs/TREE_BUILDING_SYSTEM.md`

### [ ] M6. 손님 2 — 퍽 모드 호환 보정
- 역할: 카탈로그 태그 기준으로 MGEF에 **빠진 바닐라 `Magic*` 키워드를 추가**. 삭제 없음
- 파일: `src/librarian/LibrarianKeywordPatch.cpp` — `kDataLoaded` 후 실행, 어댑터 룰 `librarian/adapter_vanilla_keywords.json`(`{"tag":"fire","mgefKeyword":"MagicDamageFire"}`), 플러그인 제외 목록, 설정 토글(기본 ON)
- 주의: `BGSKeywordForm::AddKeywords` 사용(헤더 확인됨). 원본 폼 수정이므로 로그에 변경 건수 기록. 세이브 영향 없음(런타임 폼 데이터)
- 의존: M4
- 테스트: 바닐라 키워드 없는 모드 주문 하나 골라 → 보정 후 화염 강화 퍽 효과 수치 변화 확인
- 문서: `docs/librarian/LIBRARIAN.md` 보정 절 + 호환성 안내(FOMOD/README)

### [ ] M7. 사서 3·4단계 — 텍스트 룰 + LLM 폴백
- 역할: M2에서 라벨 0개로 남은 주문만. 이펙트명·설명문 룰 → 그래도 없으면 `llmTreeFeatures.js` 어휘를 우리 태그로 교체해 호출
- 의존: M4, M2 숫자 (라벨 0개 비율이 작으면 LLM만, 크면 텍스트 룰 먼저)
- 테스트: 폴백 결과를 카탈로그에 `source: "llm"`으로 기록, `librarian-test` 로 단계별 P/R 분리 측정

### [ ] M8. 학습 층 — 태그 XP
- Concept 5절. **세부 규칙 미확정** → 별도 Concept 절 확정 후 ToDo 추가. `ProgressionManagerXP.cpp`(23KB) 확장 예상
- 의존: M4, M5

---

## 4. 지금 당장 (다음 세션 시작점)

M0, M1 완료. **M1 재검증이 먼저다** — 2026-09-09 첫 게임 검증에서 버그 2개가 나와 고쳤고,
빌드·배포까지 끝냈지만 고친 결과를 게임에서 아직 못 봤다.

1. 게임 실행 → `RunScan("tomes","full")` 이 **즉시 경로를 반환**하는지(데드락 수정 확인),
   덤프의 `primaryAV`/`resistance` 가 **영어 enum 이름**인지(`ResistFire`, `Health`) 확인.
   되면 M1 을 닫고 커밋
2. M2 착수 — `tools/librarian-test` + 룰 엔진 2파일 + `00_mgef.json` 초안.
   `00_mgef.json` 은 `RE/E/EffectArchetypes.h` 의 archetype 47종과 실측 덤프의
   바닐라 `Magic*` 키워드 34종(2026-09-09 톰 모드 기준)으로 시작한다. 게임 불필요
3. M2 숫자 → M3 어휘 확정 → M4
