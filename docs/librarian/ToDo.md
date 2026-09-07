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

### [x] M1. 스캐너 리팩터 + MGEF 필드 (2026-09-07 구현·빌드 완료, 게임 검증 대기)
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
- 테스트: **빌드 통과 (VS 2026, 경고 0).** 게임 검증은 아직 — 다음 단계에서
  `Dvb papyrus @{ action = "call"; script = "SpellLearning"; function = "RunScan"; args = @("tomes", "full"); timeoutMs = 60000 }`
  로 스캔 후 덤프의 `effects[0].archetype` 이 문자열이고 `MagicDamageFire` 가 1건 이상인지 확인.
  `join_coverage.py` 가 기존 필드로 여전히 동작하면 회귀 없음
- 미검증: PrismaUI JS 테스트(`node run-tests.js`)를 **못 돌렸다 — 이 PC에 Node.js 가 없다.** JS 변경은 3곳뿐이고 모두 기계적
- 문서: `docs/ARCHITECTURE.md`(스캐너 절), `docs/PRESETS.md`

### [ ] M2. 측정 — *Python, 게임 밖*
- 역할: 프레임워크 없는 환경의 진짜 커버리지를 안다. **여기서 나온 숫자가 M4 룰의 우선순위를 정한다**
- 입력: M1 덤프, `pairs_119.json`(재조인) / 출력: `STEP_MGEF_측정.md`, `librarian/00_mgef.json` 초안
- [ ] `join_coverage.py` — 새 덤프로 재조인 (톰 모드라 분모 바뀜)
- [ ] `mgefmap.py` — MGEF 키워드·archetype·resistance·AV → 태그 사전 (kwmap.py와 같은 인터페이스)
- [ ] 정답 119쌍으로 P/R: (a) MGEF만 (b) MGEF + 프레임워크 (c) + 이펙트명
- [ ] 전체 장서 중 "라벨 0개" 비율 — 이게 LLM 폴백이 감당할 크기
- 의존: M1
- 테스트: 스크립트 재현 가능. 숫자를 Concept.md 2-2절에 반영

### [ ] M3. 태그 어휘 확정
- 역할: 태그의 정의를 못 박는다. 이후 모든 모듈이 이 목록만 쓴다
- 출력: `docs/librarian/TAGS.md` (태그·정의·예시 주문 3개씩), `include/librarian/TagVocabulary.h`(`static constexpr` 배열), `modules/tagVocabulary.js` — **두 파일은 내용 동일, 수동 동기화** (빌드 시 검증 스크립트 하나 두면 좋음)
- 의존: M2 (측정에서 실제로 구분되는 태그만 남긴다)
- 테스트: `00_mgef.json`의 모든 `add` 값이 어휘에 있는지 검사 스크립트

### [ ] M4. 사서 (C++) + 카탈로그
- 역할: 스캔 결과에 태그를 붙여 카탈로그로 저장. 1·2단계(MGEF 룰, 프레임워크 룰)만. 텍스트·LLM은 M7
- 입력: 스캔 JSON, `librarian/*.json` 룰 / 출력: `spell_catalog.json`(2-2)
- 파일 (`src/librarian/`, 각 600줄 이하):
  - [ ] `LibrarianRules.cpp` — 룰 파일 로드·검증(어휘 밖 태그 → 경고 후 무시), 파일명 순 병합
  - [ ] `LibrarianClassify.cpp` — `TagSet Classify(const json& spell)` 순수 함수. 룰 매칭 → `source`/`confidence` 기록
  - [ ] `LibrarianCatalog.cpp` — 카탈로그 읽기/쓰기/버전, `persistentId` 키
  - [ ] `include/librarian/Librarian.h`
  - [ ] 훅: 스캔 완료 직후 자동 실행 (`UIManagerScanner.cpp`의 스캔 태스크 끝에서). 별도 버튼 불필요
- 의존: M1, M3
- 테스트: (1) 빌드 (2) 게임 스캔 후 카탈로그 생성 확인 (3) 카탈로그를 `pairs_119.json`과 대조하는 Python 스크립트 — M2의 (b) 숫자와 일치해야 함(C++ 구현이 Python 프로토타입과 같은가)
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
- 테스트: 폴백 결과를 카탈로그에 `source: "llm"`으로 기록, 별도 P/R

### [ ] M8. 학습 층 — 태그 XP
- Concept 5절. **세부 규칙 미확정** → 별도 Concept 절 확정 후 ToDo 추가. `ProgressionManagerXP.cpp`(23KB) 확장 예상
- 의존: M4, M5

---

## 4. 지금 당장 (다음 세션 시작점)

1. **M0** — 사용자: 포크 clone + 빌드 1회. 이게 되면 알려주기
2. 그 사이 Claude: M2용 `mgefmap.py` 골격 + `00_mgef.json` 초안 (헤더의 archetype 47종 · 바닐라 `Magic*` 키워드 목록 기반, 게임 불필요)
3. M0 완료 → M1 착수
