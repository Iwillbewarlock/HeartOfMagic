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
- **MO2 우선순위 읽는 법 — `modlist.txt` 는 앞줄이 높은 우선순위다.**
  파일 끝쪽에 `Creation Club Files` 와 `##Base Mods_separator` 가 있는 것이 근거다. 베이스가
  가장 낮은 우선순위이므로 끝줄이 가장 약하고 첫줄이 가장 세다.
  현재: `HeartOfMagic-Librarian-Dev`(6줄) > `HeartOfMagic-UI-Patch`(19줄) > `HeartOfMagic-KR`(2043줄).
  **즉 Dev 가 이긴다.** 2026-09-10 에 이걸 거꾸로 적었다가 사용자가 잡아줬다
- **그래서 Dev 에 파일을 넣을 때는 UI-Patch 를 가리지 않는지 반드시 확인할 것.**
  UI-Patch 가 가진 파일은 `index.html` · `modules/settingsPanel.js` · `modules/state.js` 셋이다.
  Dev 에 리포판 `settingsPanel.js` 를 그대로 넣었더니 UI-Patch 고유 89줄
  (사이드 상세 패널 토글, 카메라 설정 저장, `uiPatchDefaults` 마커, 그리고 **약 30개 UI 핸들러가
  호출하는 `scheduleAutoSave()` 정의**)이 통째로 가려졌다. 그 함수가 없으면 핸들러가 던진다.
  → Dev 에 넣을 이 세 파일은 **UI-Patch 판을 바탕으로 수정**해야 한다.
  2026-09-10 복구 완료: Dev 의 두 파일 모두 UI-Patch 판 대비 차이가 의도한 한 곳뿐이다.
  `settingsPanel.js` 는 `state.fields = data.fields;` → 병합 루프,
  `state.js` 는 `keywords: true` 뒤에 `effectDetails: true` 추가.
  게임이 켜져 있으면 MO2 VFS 가 파일을 잡고 있어 덮어쓰기도 삭제도 안 되므로 반드시 끄고 할 것
- **UI-Patch 의 `index.html` 은 `tagVocabulary.js` 를 로드하지 않는다.** Dev 에 index.html 이 없어
  UI-Patch 판이 유일본이기 때문이다. 지금은 `TAG_ELEMENTS` 를 쓰는 코드가 없어 무해하지만,
  M4/M5 에서 UI 가 태그를 읽기 시작하면 UI-Patch 의 index.html 에도 스크립트 태그를 넣어야 한다
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

### [x] M4. 사서 게임 내 연결 + 카탈로그 (2026-09-10 완료, 게임 검증까지)
- 역할: 스캔 결과에 태그를 붙여 카탈로그로 저장. 1·2단계(MGEF 룰, 프레임워크 룰)만. 텍스트·LLM은 M7
- **분류 엔진(`LibrarianRules.cpp`, `LibrarianClassify.cpp`)은 M2 에서 이미 만들어졌다.**
  여기서는 그것을 게임 안에 붙이고 결과를 파일로 남기는 일만 했다
- 입력: 스캔 JSON, `librarian/*.json` 룰 / 출력: `spell_catalog.json`(2-2)
- [x] `LibrarianCatalog.cpp` — 네 축 도출 + 카탈로그 생성·읽기·쓰기, `persistentId` 키
- [x] 훅: **스캔 진입점 두 곳 모두**에서 같은 `BuildAndWriteCatalog` 를 부른다 —
      `RunScanToFile`(Papyrus·도구)와 `UIManagerScanner::OnScanSpells`(패널 버튼).
      한쪽에만 걸면 Papyrus 로 돌렸을 때 카탈로그가 안 생긴다.
      사서가 실패해도 스캔은 살아남도록 예외를 삼키고 로그만 남긴다
- [x] `librarian-test --catalog <파일> -a <정답셋>` — 게임이 쓴 카탈로그를 채점하는 통로.
      **네 축도 함께 채점한다** (정답셋에 school·tier·casting·targets 가 있다)
- [x] 하네스가 자체 카탈로그를 만들던 것을 없애고 플러그인과 **같은 `BuildCatalog`** 를 쓰게 했다.
      형식이 두 벌이 될 뻔했다. 커버리지 보고도 카탈로그에서 읽으므로 분류는 한 번만 돈다.
      그 결과 `librarian-test.cpp` 가 629줄이 되어 채점부를 `librarian-score.cpp` 로 분리
- **테스트 결과 (2026-09-10)**
  - (1) 빌드 통과, 경고 0
  - (2) 게임 스캔 → `spell_catalog.json` 생성 확인. 로그: 룰 273개 로드,
        **1440 주문 중 1341 태그 부착**, persistentId 없는 항목 0
  - (3) **게임이 쓴 카탈로그와 하네스가 만든 카탈로그가 1440건 전부 항목 단위로 동일**
        (`generated` 만 다름). 채점 결과도 완전히 같다 — P66.3 / R85.9 / F1 74.9.
        **게임 안 경로와 게임 밖 경로가 같은 결과를 낸다는 것이 증명됐다**
- **네 축 실측 — 문서 주장("정답으로 검증됨")보다 낮다.**
  school 97.1% · casting 95.2% · **tier 78.8%** · **targeting 66.3%**.
  tier 불일치 22건은 양방향으로 흩어져 도출 버그가 아니다. HoM 의 티어 판정(하프코스트 퍽·
  minimumSkill)과 SR 티어 라벨은 정의가 다르다.
  targeting 개선 시도(클록→actor, 클록·해저드→aoe)는 **66.3 → 61.5 로 떨어져 되돌렸다.**
  기계적으로는 말이 되지만 SR 이 `self` 와 `aoe` 를 그렇게 겹쳐 쓰지 않는다. 코드 주석에 기록
- **배포에 룰 파일이 빠져 있었다.** 게임은 `Data/SKSE/Plugins/SpellLearning/librarian/` 을 보는데
  어느 모드 폴더에도 없었다. DLL 만 넣으면 사서가 조용히 아무것도 안 한다. 5개 파일을 Dev 에 배포
- 의존: M2(엔진), M3(어휘)
- 문서: `docs/librarian/LIBRARIAN.md` 신규

### [ ] M5. 손님 1 — HoM 트리
- 역할: 태그를 트리 빌더 특징량으로 주입. 어댑터 = "태그 → 토큰"
- 파일: `treebuilder/TreeNLP.cpp` — TF-IDF 코퍼스에 `tag:fire` 형태 토큰 추가(가중치 상수), `edgeScoring.js:207` 동일
- 입력: 카탈로그 / 출력: 없음(트리 품질)
- 의존: M4
- 테스트: 태그 있음/없음으로 트리 2회 생성, 같은 학파 내 간선이 원소별로 뭉치는지 정성 비교. 스크린샷 2장
- 문서: `docs/TREE_BUILDING_SYSTEM.md`

### [제거] M6. 손님 2 — 퍽 모드 호환 보정 (2026-09-10 구현 → **2026-09-21 포크에서 제거**)
> 키워드 주입은 나중에 만들 별도 모드의 일이라 범위 밖이고, 처음 게임에서 돌려 보니 오탐이 나왔다
> (`MagicNightEye` → 생명 감지 82건, `MagicRune` → SpawnHazard 31건 등). 경위는 `LIBRARIAN.md` 6절.
> 코드는 `feature/librarian` 의 `c7e7670` 에 남아 있다. 아래는 당시 기록이다.
- 역할: MGEF에 **빠진 바닐라 `Magic*` 키워드를 추가**. 삭제 없음
- **카탈로그를 읽지 않는다.** 이 절의 원래 계획은 "카탈로그 태그 기준"이었으나 그렇게 만들 수 없다.
  카탈로그는 **주문** 단위인데 퍽은 `HasMagicEffectKeyword` 로 **이펙트**에게 묻는다.
  주문에 이펙트가 여럿이면 어느 이펙트에 붙일지 카탈로그가 모른다.
  → `ApplyVanillaKeywordPatch` 는 로드오더의 모든 `EffectSetting` 을 **하나씩 분류**한다.
  카탈로그도, 사전 스캔도 필요 없다. 새로 설치한 사용자에게도 바로 동작한다
- [x] `src/librarian/LibrarianKeywordPatch.cpp` — `kDataLoaded` 후 `OnDataLoaded` 에서 1회 실행.
      어댑터 `librarian/adapter_vanilla_keywords.json` 21개, 플러그인 제외 목록, 설정 토글(기본 ON)
- [x] 어댑터 항목은 태그만으로 판단하지 않는다. `match` 가 분류 룰과 **같은 조건 구조**라
      기존 매처를 그대로 쓴다. 이게 없으면 화염 아트로나크 소환에도 `MagicDamageFire` 가 붙는다
      (그것도 `fire` 태그를 받는다) — 화염 강화 퍽이 소환 주문에 걸리게 된다
- [x] 어댑터 태그도 **어휘 게이트를 통과한다.** 룰 파일과 같은 대우. 오타는 경고 후 버려진다
- [x] `BGSKeywordForm::AddKeywords` 사용. **반환값은 항상 true 라 믿을 수 없다**
      (`commonlibsse-ng/src/RE/B/BGSKeywordForm.cpp:39`) → 키워드 개수를 전후로 재서 판정한다
- [x] **스캔 덤프에 `keywordPatchApplied` 를 찍는다.** 패치가 돈 뒤의 스캔은 플러그인 파일에 없는
      키워드를 보므로 커버리지가 실제보다 높게 나온다. `librarian-test` 가 그 덤프를 읽으면 경고한다.
      **`MEASURED.md` 의 숫자는 패치 이전 덤프 기준이다**
- 한계: 랩이 이펙트 하나뿐이라 `spellKeyword` 로 매칭하는 룰(10_nsv.json 18개)은 이 경로에서
  발동할 수 없다. 의도한 것이다 — NSV 태그는 주문을 설명하고, 붙이는 키워드는 이펙트의 것이다
- 의존: M4
- [ ] 테스트: 바닐라 키워드 없는 모드 주문 하나 골라 → 보정 후 화염 강화 퍽 효과 수치 변화 확인
- 문서: `docs/librarian/LIBRARIAN.md` 보정 절 + 호환성 안내(FOMOD/README)

### [ ] M7. 사서 3·4단계 — 텍스트 룰 + LLM 폴백
- 역할: M2에서 라벨 0개로 남은 주문만. 이펙트명·설명문 룰 → 그래도 없으면 `llmTreeFeatures.js` 어휘를 우리 태그로 교체해 호출
- 의존: M4, M2 숫자 (라벨 0개 비율이 작으면 LLM만, 크면 텍스트 룰 먼저)
- 테스트: 폴백 결과를 카탈로그에 `source: "llm"`으로 기록, `librarian-test` 로 단계별 P/R 분리 측정

### [ ] M8. 학습 층 — 태그 XP
- Concept 5절. **세부 규칙 미확정** → 별도 Concept 절 확정 후 ToDo 추가. `ProgressionManagerXP.cpp`(23KB) 확장 예상
- 의존: M4, M5

---

## 3-S. 스캔 강화 트랙 (2026-09-21 시작)

> 방향 전환: 이 리포는 **HoM 포크**(`Iwillbewarlock/HeartOfMagic`, `origin`. 업스트림은 `upstream`)로
> 배포한다. 포크의 범위는 **스캔 강화까지**다. 나중에 만들 두 모드 — Spell Research 류 모드가 쓸
> 아키타입 분류, 그 아키타입을 보고 각 퍽 모드의 키워드를 주입하는 패치 — 는 **별개의 모드**이고
> 여기서 만들지 않는다. 그 둘은 "스캔이 무엇을 빠짐없이 적어야 하는가"의 기준으로만 쓴다.
> 배포물이므로 스캔은 **AI 개입 없이 기계적으로** 돈다: 레코드에 적힌 값을 그대로 옮겨 적고,
> 해석은 하지 않는다. 스캔 기준은 원본과 같이 **마법책**이다. 스크롤은 다루지 않는다.

### [~] S-1. 구조 증거 필드 (2026-09-21 구현·빌드 통과, 게임 검증 대기)
- [x] 신규 `src/spellscanner/SpellScannerEvidence.cpp` — `effectDetails` 가 켜지면 붙는다.
      필드 목록과 출처는 `docs/ARCHITECTURE.md` 스캐너 절의 표가 정본
  - 주문: 반값 퍽(`castingPerk`), 장착 슬롯·양손 여부, castDuration, range, 주문 플래그,
    마법책 persistentId 와 가격
  - 효과(MGEF): 플래그 전부, baseCost, minimumSkill, 투사체(종류·속도·사거리·중력·폭발 여부),
    폭발(반경, 효과 것인지 투사체 것인지), **장판 유무와 경로**, 붙은 퍽·능력
  - 주문 안의 효과: 순서 번호, cost
- [x] 장판은 **있다/없다만.** 반경·지속시간은 적지 않는다(사용자 결정). 길은 셋 —
      MGEF 의 연결 폼이 해저드 / 폭발이 남기는 물건이 해저드 / 착탄 설정에 해저드
- [x] 빌드 통과 (VS 2026). `BSSimpleList` 의 const `begin()` 이 이 CommonLib 에서 컴파일되지 않아
      상쇄 효과 목록은 비-const 참조로 돈다(쓰기 없음)
- [x] 게임 검증 2회 (2026-09-21, 1440 주문 / 4246 이펙트, 덤프 `scan_2026-09-21_tomes_full_v4_evidence.json`)
  - 나온 것: 반값 퍽(화염구 = `Skyrim.esm|0x0C44C0`), 장착 슬롯, 마법책 가격(화염구 350),
    투사체(Missile 799 · Beam 100 · Flame 91 · Cone 64 · Lobber 53 · Arrow 11), 폭발 반경(화염구 320),
    조건 유무, 효과 순서·cost
  - **장판은 예상이 틀렸다.** 1회차에 화염구·화염 화살까지 "있음"(783건)으로 나왔다. 읽기 오류가
    아니라 경로가 셋이고 성격이 달랐다 → 2회차에 `hazardSource` 를 추가해 갈랐다:
    `effect` 33 · `explosion` 15 · `impact` 735. 앞의 둘은 주문이 그것을 중심으로 만들어진 장판
    (눈보라, 보호의 원), `impact` 는 착탄 흔적이고 엔진이 시각 효과에도 해저드를 쓰기 때문에
    아트로나크 소환·추방·대규모 환영 주문에서도 켜진다. 스캔은 어느 쪽인지만 적고 고르지 않는다
  - 예상 목록의 "화염 룬 = 장판" 도 틀렸다. 룬은 해저드가 아니라 Lobber 투사체다
  - 벽 주문 3종은 이 로드오더의 톰 스캔 덤프에 없어 확인하지 못했다
- [x] 기존 필드 회귀 확인 — v3 덤프(09-10)와 1440 주문 / 4246 이펙트를 항목별로 대조, 기존 24개
      필드 **차이 0건.** 키워드 차이는 전부 스캔 밖 원인(세이브마다 번호가 바뀌는 `NSV*Base<N>`,
      그리고 아래 M6 보정이 붙인 `Magic*`)
- [x] 새 필드 전수 점검 — 누락 0, 투사체 종류 None 0, 속도 0 이하 0. 반값 퍽 1423/1440, 양손 15,
      폭발 517(반경 0 이하 8), 책값 0 이하 3
- **확실한 것만 들고 간다 (사용자 결정, 2026-09-21).** 검증하지 못한 두 필드를 뺐다:
  `counterEffects`(4246건 전부 빈 목록 — 정말 없는지 읽기가 틀린 건지 가릴 수 없었다)와
  `conditions{base,item}`(MGEF 조건 있음이 70% 로 나왔는데 맞는지 확인 못 함).
  다시 넣으려면 SSEEdit 로 레코드 몇 개를 대조해 읽기가 맞는지부터 확인할 것
- **M6 키워드 보정을 포크에서 제거했다.** 이번에 게임에서 처음 돌았고(969 이펙트에 971개 추가)
  오탐이 확인됐다. 위 M6 절 참조. 덤프의 `keywordPatchApplied` 스탬프도 함께 없앴다
- [x] 3회차(제거 후 빌드, 덤프 `scan_2026-09-21_tomes_full_v5_evidence.json`): 보정이 붙이던 `Magic*` 키워드 0건,
      뺀 두 필드 0건, 기존 필드 차이 0건, 남은 새 필드 수치는 2회차와 동일
- 부수 관찰: `RunScan` 이 세 번 다 121~123초 걸렸다(M1 버그 2). 파일은 정상으로 써지지만
  반환값이 빈 문자열이다 → S-3 을 앞당길 이유

### [~] S-TREE. 트리 테마를 성격(traits)으로 (2026-09-21 구현, 오프라인 비교 완료, 게임 안 트리 재생성 미확인)
- 스캔에 `traits` 칸 추가(키워드 칸은 원본 이름 하나, 정리된 것은 traits 하나 — 사용자 지적으로 `vanillaKeywords` 는 뺐다)
- 빌더 5종 공통 `GetSpellPrimaryTheme` 이 traits 를 먼저 본다. 단어 방식은 나머지에만, 모드 키워드·숨김 효과 이름은 재료에서 제외
- 09-02 저장 트리의 테마는 `spel`·`adar`·`kit` 등 키워드 조각이었다 → 지금은 fire/frost/shock/summon_undead … , 테마 없음 22%
- 비교 도구: `treebuilder-test -i <v6 덤프> -t classic`. 숫자는 `docs/TREE_BUILDING_SYSTEM.md`
- "Export Scan" 버튼은 개발자 모드 전용으로 옮김

### [ ] S-2. 스캔 코어 분리 — 트리용 거름망을 루프에서 빼 옵션으로, 덤프에 `scanVersion`·로드오더 해시·언어 메타
### [ ] S-3. 논블로킹 실행 — M1 버그 2(세션에 따라 120초 정지) 해소
### [ ] S-4. MGEF 테이블 정규화 (스키마 v2) — 효과 설명은 한 번만, 주문은 참조. v1 출력 병행 유지

### [~] S-UI. 주문 카드 (2026-09-21 구현, 브라우저 미리보기 확인, 게임 확인 대기) — 브랜치 `feature/spell-card`
- [x] 사용자의 UI-Patch(하단 정보 바 등)를 포크에 3-way 병합 — 충돌 0. 이제 포크 UI = 실제 쓰는 UI
- [x] 키워드 칩: C++ `SpellScannerChips.cpp` 가 닫힌 집합(저항·투사체 종류·전달 방식·archetype·학파)에서만
      안정 id 를 뽑고 `modules/spellCard.js` 가 `chips.*` 키로 번역(en·ko 추가, 나머지 언어는 영어 폴백)
- [x] 개방 순서 이름 → 키워드 → 설명·상세. 브라우저에서 0/10/30/60%/잠김 다섯 경우 확인
- [x] 게임 안 1차 확인(사용자 스크린샷) → 효과 목록은 시스템 내용이라 편집 모드에서만 보이게, 설명문의 `<dur>` `<25>` 기호 정리
- [x] 아이콘: I4 의 SWF 는 웹 화면에서 못 그린다. 대신 같은 아이콘 팩이 Wheeler 용으로 넣어 둔
      `KWD_<키워드>.svg` 를 키워드 이름으로 찾는다. 이 로드오더 기준 843개 파일 / 21개 모드, 톰 주문
      1440개 중 974개(68%)에 맞는 아이콘이 있다. 브라우저에서 실제 SVG 로 표시 확인
- [x] **아이콘 팩(KIT)을 요구사항으로 잡고 분배 기준을 만들었다 (사용자 결정, 2026-09-21).** 직접 그린 학파 그림은
      뺐다. 순서: 팩이 그 주문에 붙인 아이콘 → `card_icons.json` 규칙(화염+망토, 냉기+룬, 소환 … → KIT 파일)
      → 원본 학파 문양. 규칙 40개 전부 설치된 파일과 대조 확인. 이 로드오더 분배: 977 / 266 / 197 / 없음 0
- [x] 화염 폭풍에 냉기 칩이 붙던 것: 숨김(Hide in UI) 표시된 보조 효과를 칩에서 제외
- [ ] 아이콘·설명 정리·칩 게임 안 확인 (사용자가 나중에 하기로 함)
- 네 줄: (1) 아이콘 + 이름 — I4 등 아이콘 모드가 있으면 표시 (2) 스캔 정보에서 뽑은 키워드
  ("화염 · 투사체 · 파괴마법" 식) (3) 설명 (4) 매지카 소모량 · 피해량 등 상세
- **개방 순서(사용자 결정): 이름 → 키워드 → 설명·상세.** 원본의 이름 → 효과 → 설명 점진 공개를 대체한다
- 미확인: PrismaUI 화면에서 I4 아이콘 파일을 불러올 수 있는지

## 4. 지금 당장 (다음 세션 시작점)

> **2026-09-21 갱신: 지금 진행 중인 것은 3-S 절의 스캔 강화 트랙이다.** M6 는 포크에서 제거됐고,
> 아래 M6 관련 항목(게임 검증·정밀도)은 더 이상 이 리포의 일이 아니다. 아래는 09-10 시점 기록.

M0 · M1 · M2(a)(b) · M3 · M4 완료. M6 는 구현·빌드·배포까지 했고 **게임 검증만 남았다.**
커밋 `c7e7670`, 워킹트리 clean, Dev 모드에 DLL 과 룰 6파일 배포됨.

| | 커버리지 | 정밀도 | 재현율 | F1 |
|---|---:|---:|---:|---:|
| (a) MGEF만 — 프레임워크 없는 기본 환경 | 81.4% | 70.4% | 79.2% | 74.5 |
| (b) + KIT · OCF · ADAR · NSV | 93.1% | 66.3% | 85.9% | 74.9 |

**기본값으로 말할 숫자는 (a) 다.** (b) 는 이 설치본에서만 나오는 값이다 — 아래 참조.

### 2026-09-10 세션 마지막에 제기된 것 — 다음 세션에서 먼저 답할 것

**"만든 것이 프로그램이 아니라 프로그램이 뽑아냈어야 할 결과값 아니냐."**
룰 조건 240개 중 **156개가 로드오더 덤프에서 키워드 문자열을 뽑아 손으로 옮겨 적은 것**이다.
게다가 튜닝은 정답셋을 조회해 규칙을 읽어낸 뒤 타이핑한 것이라, F1 74.9 는 부분적으로
"정답셋을 얼마나 잘 옮겨 적었는가"를 재고 있다. 홀드아웃이 없다.

경계는 있다:
- `00_mgef.json` 의 archetype 47종 · 액터값 매핑은 **엔진이 정의한 닫힌 집합** 위의 완성된
  함수다. 본 적 없는 입력이 없으므로 일반화할 것이 없다. 이건 프로그램이 맞다
- KIT · OCF · ADAR · NSV 의 207개는 **제3자 어휘의 스냅샷**이다. 프레임워크가 버전을 올리거나
  안 본 모드가 자기 체계를 들고 오면 그만큼 구멍이 난다
- 접두·접미 84개는 중간. 나열하지 않은 것도 잡는다 (ADAR 가 33개로 가장 많다)

**그리고 "본 적 없는 것을 처리하는 층"인 M7 은 손도 안 댔다.** Concept 3절이 마지막 층으로
배치한 것이 그건데, 그 앞 층을 표로 채우는 데 시간을 썼다. 순서가 뒤집혔을 가능성.

**갈림길 (사용자 결정 필요):**
- (가) 표를 인정하고 기본값을 (a) 81.4% 로 낮춰 쓴다. 프레임워크 룰은 보너스로 둔다
- (나) M7 을 앞당겨 본 적 없는 어휘를 처리하는 층을 먼저 세운다. 원래 설계 의도에 가깝다

### 그와 별개로 남은 일

1. **M6 게임 검증** — 다섯 마일스톤 중 **유일하게 플레이어가 체감하는 기능**이고 아직 한 번도
   안 돌려봤다. 로그에 몇 개 붙었는지 나온다. 바닐라 키워드 없던 모드 화염 주문을 하나 골라
   퍽 수치가 바뀌는지 확인
2. **M6 정밀도** — 퍽 보정은 오탐이 곧 게임플레이 버그다(엉뚱한 주문에 퍽이 걸린다).
   어댑터 21개가 맞는 이펙트만 건드리는지는 아직 안 쟀다. 태그 F1 보다 이쪽이 중요하다
3. **M5 (트리)** — 카탈로그에 소비자를 붙여야 M4 가 완료된다. 지금은 파일만 남고 아무도 안 읽는다
4. 잔가지: `RunScan` 이 세션에 따라 120초(M1 버그 2), 포커스 잃으면 프레임 정지(M0-T)

**지표는 소비자에 따라 다르다.** M6 는 정밀도, M5 는 재현율이 중요하다.
둘을 합친 F1 을 쫓는 것은 어느 쪽에도 맞지 않는다.
