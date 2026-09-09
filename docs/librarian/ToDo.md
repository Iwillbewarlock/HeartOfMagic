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
  "resistance":   "FireResist",               // resistVariable
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
- **추가 (2026-09-09): 게임 창이 포커스를 잃으면 프레임이 멈춘다.** `bAlwaysActive=1` 이 프로필
  ini 에 있고 `profile_local_inis=true` 인데도 그렇다. 창이 최소화된 것도, 일시정지 메뉴가 열린
  것도 아니었다(`menu list` 에 HUD 위젯뿐). DevBench 는 메인 스레드가 돌아야 도구를 실행하므로
  **모든 호출 전에 게임 창을 앞으로 가져와야 한다.** `ShowWindow(hWnd, SW_RESTORE)` 로 프레임이
  다시 돌았다(`SetForegroundWindow` 는 실패해도 무관). 원인 미규명 — 모드 충돌 가능성

### [~] M1. 스캐너 리팩터 + MGEF 필드 (2026-09-07 구현, 2026-09-09 게임 검증 2회 — 필드 확인, 버그 1 해결, 버그 2 미해결)
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
- **버그 1 — ActorValue 필드가 로컬라이즈된다. 수정·검증 완료 (2회차).**
  `primaryAV`/`secondaryAV`/`resistance` 가 한글 로드오더에서 "체력", "화염 저항" 으로 나왔다.
  원인은 `RE::ActorValueToString` → `ActorValueList::GetActorValueName` 이 AVIF 의 **표시 이름**을
  반환하는 것. 영어 환경도 안전하지 않다 — 표시 이름은 "Resist Fire"(공백)이고 룰이 매칭할
  enum 이름은 `FireResist` 다. 룰 JSON 은 전 사용자에게 같은 파일로 배포되므로 키가 언어마다
  달라지면 성립하지 않는다(Concept 8절 다국어 항목).
  → `SpellScannerJson.cpp` 의 `GetActorValueName` 이 `ActorValueList::GetActorValueInfo(av)->enumName`
  을 쓰도록 교체. `archetype`·`school`·`castingType`·`delivery` 는 손으로 쓴 switch 라 원래 안전했다.
  **2회차 결과: 한글 0종.** primaryAV 35종·secondaryAV 5종·resistance 7종 전부 영어 enum 이름
  (`Health`, `Magicka`, `Stamina`, `DamageResist`, `FireResist`, `FrostResist`, `ElectricResist`,
  `PoisonResist`, `MagicResist`, `DiseaseResist` …). 저항 계열의 실제 enum 이름은 `ResistFire` 가
  아니라 **`FireResist`** 순서다 — 룰을 쓸 때 주의
- **버그 2 — `RunScan` 호출이 게임을 120초 멈춘다. `IsOnGameThread()` 가드로 해소되는 것을
  3회차에서 확인했으나, 세션에 따라 갈린다.**
  1회차(가드 없는 빌드)는 120초 타임아웃 후 빈 문자열을 반환했다.
  2회차(가드 있는 빌드)도 같았고 로그가 이유를 보여줬다 — `RunScan` 은 스레드 5200, 스캔 태스크는
  20696 이라 호출 스레드가 게임 스레드가 아니었고 인라인 경로를 타지 않았다
  (호출 15:31:28 → 타임아웃 15:33:28.410 → 태스크 실행 15:33:28.421, **11ms 차이**.
  DevBench 가 자기 메인 스레드 태스크에서 결과를 기다려 SKSE 태스크 큐를 막고 있었다는 뜻).
  **3회차(2026-09-10 00:10)는 정반대다.** `RunScan` 이 스레드 26820 에서 돌았고 이는 플러그인
  로드 배너를 찍은 그 스레드, 즉 게임 스레드다. 가드가 발동해
  `PapyrusAPI.cpp:279 "RunScan finished on the game thread"` 를 남기고 **1초 만에 경로를 반환했다.**
  → 가드는 옳고 실제로 동작한다. 다만 **DevBench 가 어느 스레드에서 네이티브를 부르는지가
  세션마다 달라지는 이유는 규명하지 못했다.** 5200 으로 오는 세션에서는 여전히 120초가 걸린다.
  근본 해법은 여전히 논블로킹 `RunScan`(태스크만 걸고 즉시 반환, 완료는 파일로 확인)이며,
  이는 M4 착수 전 판단할 것
- 재검증 결과(2026-09-09 2회차, 1440 주문 / 4246 이펙트): 덤프 선두 오염 없음 · `archetype` 31종
  전부 문자열(숫자 0건) · MGEF 키워드 31104건 / 고유 729종 · `Magic*` 34종 · `MagicDamageFire` 73건 ·
  MGEF 증거 없는 주문 **0건** · AV 한글 0종. 덤프는 `scan_2026-09-09_tomes_full_v2.json`
- **3회차 (2026-09-10, 코드리뷰 수정 반영 빌드) — 회귀 없음.** 같은 1440 주문 / 4246 이펙트,
  AV 한글 0종, `archetype` 숫자값 0건, `MagicDamageFire` 73건. 새 덤프
  `scan_2026-09-10_tomes_full_v3.json` 로 하네스를 돌린 결과가 오프라인 숫자와 **완전히 일치**한다
  ((a) P70.4/R79.2/F1 74.5, 태그 0개 18.6% · (b) P66.3/R85.9/F1 74.9, 6.9%).
  게임에서 나온 덤프와 저장해둔 덤프가 같은 결과를 낸다는 것까지 확인된 셈이다.
  덤프 해시는 다른데, 차이는 `NSVAimed*Base<N>` 계열 5종뿐이다 — 세이브마다 달라지는 런타임
  배포 키워드이고 우리 룰이 쓰지 않는다
- **게임이 포커스를 잃으면 프레임이 멈춘다** (`bAlwaysActive=1` 이 프로필 ini 에 있고
  `profile_local_inis=true` 인데도). DevBench 는 메인 스레드가 돌아야 도구를 실행하므로,
  스캔 전에 창을 앞으로 가져와야 한다(`ShowWindow(hWnd, SW_RESTORE)` 로 충분했다).
  M1 과 무관한 환경 문제지만 앞으로 모든 게임 테스트에 영향
- **배포 함정: `HeartOfMagic-UI-Patch` 가 `HeartOfMagic-Librarian-Dev` 를 덮어쓴다.**
  MO2 우선순위가 UI-Patch 19 / Dev 6 이라 UI-Patch 가 이긴다. UI-Patch 는
  `index.html` · `modules/settingsPanel.js` · `modules/state.js` 를 갖고 있으므로,
  **Dev 폴더에 넣은 이 세 파일의 수정은 게임에 반영되지 않는다.**
  DLL · `script.js` · `uiHelpers.js` · `tagVocabulary.js` 는 UI-Patch 에 없어 정상 반영된다.
  → 게임에서 UI 경로를 검증하려면 Dev 를 UI-Patch **아래(높은 우선순위)** 로 옮기고 게임을 재시작할 것.
  MO2 의 VFS 는 실행 시점에 고정되므로 게임을 켠 채로는 바꿀 수 없다
- 미검증: PrismaUI JS 테스트(`node run-tests.js`)를 **못 돌렸다 — 이 PC에 Node.js 가 없다.** JS 변경은 3곳뿐이고 모두 기계적
- 문서: `docs/ARCHITECTURE.md`(스캐너 절), `docs/PRESETS.md`

### [~] M2. 룰 엔진 + 측정 하네스 — *C++, 게임 밖* (2026-09-09 (a)(b) 측정 완료, (c) 남음)
- 역할: 프레임워크 없는 환경의 진짜 커버리지를 안다. **여기서 나온 숫자가 룰의 우선순위를 정한다**
- **결론 (a): MGEF 구조만으로 81.4% 에 태그가 붙는다.** 프레임워크 키워드 분류기가 그것들이
  설치된 환경에서 낸 63.3% 보다 넓다. 프레임워크 룰은 필수가 아니라 보너스다
- **결론 (b): 프레임워크 층이 사는 것은 정확도가 아니라 커버리지다.**
  태그 0개 18.6% → 6.9%, F1 은 74.5 → 74.9 로 사실상 제자리. 정답셋 104쌍 중 90건이 바닐라라
  프레임워크가 실제로 돕는 모드 주문 쪽을 잴 수 없기 때문이다. 전체 숫자는 `MEASURED.md`
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
  - [x] `src/librarian/LibrarianRules.cpp` — 룰 파일 로드·검증, 파일명 순 병합 (M4 것을 앞당김)
  - [x] `src/librarian/LibrarianClassify.cpp` — `TagSet Classify(const json&, const RuleSet&)` 순수 함수.
        RE/SKSE 타입을 쓰지 않으므로 플러그인과 하네스 양쪽에 그대로 컴파일된다
  - [x] `include/librarian/Librarian.h`
  - [x] `tools/librarian-test.cpp` + `tools/CMakeLists.txt` 에 타깃 추가 —
        덤프와 룰을 읽어 분류하고, 정답셋과 조인해 P/R 과 **태그별 적중/오탐/미탐**을 낸다.
        조인 키는 `(plugin.lower(), formId & 0xFFFFFF)` 정확 비교만. **ESL 12비트 폴백 금지**(오탐 100%)
  - [x] 룰 매칭 조건: `mgefKeyword`(+`Prefix`/`Suffix`) `spellKeyword`(+`Prefix`/`Suffix`) `archetype`
        `primaryAV` `secondaryAV` `resistance` `magicSkill` `hostile` `detrimental`.
        **이펙트 레벨 조건은 하나의 같은 이펙트에서 모두 성립해야 한다** — 서로 다른 이펙트에서
        긁어모은 archetype 과 resistance 는 그 룰이 말하는 것의 증거가 아니다
  - [x] 룰 파일이 `"tier"` 를 선언하면 그 파일 전체의 기본 증거 등급이 된다.
        프레임워크 룰 파일이 줄마다 `"tier": "framework"` 를 반복하지 않아도 된다
- [x] 새 덤프로 재조인 (톰 모드라 분모가 바뀐다 — 2026-09-09 실측 1440건, 정확 매칭 104쌍)
- [x] `00_mgef.json` — 66룰. 초안이 아니라 정답셋 패턴을 반영해 2회 보정한 것
- [x] (a) MGEF만 — **커버리지 81.4%, P70.4 / R79.2 / F1 74.5**
- [x] (b) + 프레임워크 4파일 (`10_kit` 89 · `10_ocf` 66 · `10_adar` 34 · `10_nsv` 18)
      — **커버리지 93.1%, P66.3 / R85.9 / F1 74.9**.
      `librarian-test -t mgef` 로 (a) 만 골라 잴 수 있다
- [ ] (c) 이펙트명 — 여기부터 **언어 의존**이라 한글 로드오더에서 안 먹는다. 영어 환경 보너스로만
- [x] 태그 0개 비율 = (a) **18.6% (268/1440)** / (b) **6.9% (99/1440)** — LLM 폴백이 감당할 크기
- 산출: `docs/librarian/MEASURED.md` (숫자·근거·주의사항 전문)
- 의존: M1
- 테스트: `librarian-test` 가 같은 입력에 같은 숫자를 낸다. 숫자를 Concept.md 2-2절에 반영

### [x] M3. 태그 어휘 확정 (2026-09-09) — `tags-v1`, **원소 38 / 기법 18**
- 역할: 태그의 정의를 못 박는다. 이후 모든 모듈이 이 목록만 쓴다
- [x] `docs/librarian/TAGS.md` — 정본. 태그·정의·바닐라 예시. 헷갈리는 정의 6가지를 3절에 모았다
- [x] `include/librarian/TagVocabulary.h` — `inline constexpr std::string_view` 배열 + `IsElement`/`IsTechnique`.
      헤더 온리라 .cpp 가 필요 없다
- [x] `modules/tagVocabulary.js` — 거울. `var` 만 사용
- [x] `LibrarianRules.cpp` 어휘 검증 — 어휘 밖 태그는 경고 후 버리고 `RuleSet::rejectedTags` 로 센다.
      사용자 룰 파일의 오타가 아무도 번역할 수 없는 태그를 만들어내지 못한다
- [x] `librarian-test --check-vocab -r <룰> -j <js>` — 룰의 태그가 어휘 안에 있는지 + **C++/JS 거울이
      어긋나지 않았는지** 검사하고 어긋나면 종료 코드 1. 오타 룰 파일로 동작 확인함
- **결정: 안 붙는 태그도 남긴다.** Concept 4절 "뭉치는 건 언제나 되지만 쪼개는 건 안 된다".
  M2 에서 실제로 붙은 건 원소 24 / 기법 15 뿐이지만, 나머지는 M7 이 붙일 것이고 지금 지우면
  카탈로그를 다시 만들어야 한다. 손님에게 나갈 때 뭉치는 것은 **어댑터의 일**이다
- **SR 34/15 에 7종을 더했다.** 이 로드오더의 키워드 체계가 실제로 구분하는데 SR 이 표현
  못 하는 것들 — 원소 `blood` `eldritch` `holy` `necrotic`, 기법 `dispel` `sacrifice` `teleport`.
  근거 키워드는 TAGS.md 각 항목에 적었다 (KIT `SpellDamageType_Blood`, OCF `MgefClassEldritch` 등)
- 의존: M2

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

M0 · M1 · M3 완료, M2 는 (a)(b) 완료. 룰 273개가 어휘 게이트를 통과한 상태다.

| | 커버리지 | 정밀도 | 재현율 | F1 |
|---|---:|---:|---:|---:|
| (a) MGEF만 — 프레임워크 없는 기본 환경 | 81.4% | 70.4% | 79.2% | 74.5 |
| (b) + KIT · OCF · ADAR · NSV | 93.1% | 66.3% | 85.9% | 74.9 |

**다음은 M4 (사서 게임 내 연결 + 카탈로그) 다.** 분류 엔진은 이미 있으므로
`LibrarianCatalog.cpp` 와 스캔 완료 훅만 붙이면 된다. 다만 **게임 테스트가 필요하다.**

1. **M4 전에 게임 테스트 경로부터 고칠 것** — `RunScan` 블로킹(M1 버그 2, 해법 미결정)과
   포커스 잃으면 프레임 정지(M0-T). 지금은 창을 앞으로 띄우고 120초 기다려야 스캔이 나온다
2. **M4** — `LibrarianCatalog.cpp`, `UIManagerScanner.cpp` 훅. 카탈로그를 하네스에 먹여
   (b) 숫자가 그대로 나오는지 확인하면 게임 안팎이 같은 결과를 낸다는 증명이 된다
3. (c) 이펙트명 룰은 **언어 의존**이라 한글 로드오더에서 안 먹는다. 급하지 않다.
   M5(트리) · M6(퍽 호환) 쪽이 사용자에게 보이는 값어치가 크다
