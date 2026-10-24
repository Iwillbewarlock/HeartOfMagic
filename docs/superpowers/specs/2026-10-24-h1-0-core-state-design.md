# H1-0 — 패널 없이 진행 상태 채우기 — 설계

날짜: 2026-10-24. 상태: 설계, 사용자 "진행해" (ACF 로드맵 H1의 첫 단계).
근거: ACF Phase 0 검토(08_CLAUDE_REVIEW.md H0-0), 이번 세션의 코드 조사 두 건.

## 1. 목적

지금 HoM의 진행 시스템(ProgressionManager와 학습 관련 훅)은 Prisma 패널이 떠야만 채워진다.

- 트리 소속·선행조건: 패널 JS가 `SetTreePrerequisites`를 보내야만 들어간다 (`treeLoad.js:14-49` → `UIManagerProgression.cpp:464-578`). co-save에 저장되지 않는다.
- 게임플레이 설정(경험치, 조기 학습, 마법책 학습, 패시브 학습, 알림): 패널 JS의 `LoadUnifiedConfig` 요청으로만 적용된다 (`UIManagerConfig.cpp:347-512`, 353행의 `m_prismaUI` guard). 패널이 없으면 각 클래스의 기본값으로 돈다.
- 두 요청 모두 `onPrismaReady`(DOM 준비 뒤)에서만 나간다 (`cppCallbacks.js:1121-1168`).

목표: 플러그인이 게임 시작(kDataLoaded) 때 같은 파일을 스스로 읽어 같은 상태를 만든다. 패널은 지금처럼 나중에 같은 내용을 다시 보내도 결과가 같다. 패널이 없거나 화면 생성에 실패해도 학습 규칙이 그대로 돈다.

범위 밖: 패널 쪽 코드 변경, 트리 생성·스캔·학습 명령·알림 경계(H1-a~d), 외부 API.

## 2. 무엇을 만드나

### 2.1 설정: 시작 때 한 번 읽기 전용으로 적용

- 새 함수 `ApplyGameplayConfig(const json& config)`: 지금 두 곳(`OnLoadUnifiedConfig` 443-450행, `ApplySavedConfig` UIManagerConfigSave.cpp 66-72행)에 복사돼 있는 묶음을 하나로 모은다.
  - `ReadXPSettingsFromConfig` + `ReadModdedSourcesFromConfig` → `ProgressionManager::SetXPSettings`
  - `ApplySettingsFromConfig` (조기 학습·powerSteps, 마법책, 패시브, HudNotify, 약화 알림, developerMode)
  - 두 기존 호출부는 이 함수를 부르도록 바꾼다. `Environment::OnConfigApplied`는 저장 경로에만 남긴다(지금과 같음).
- 새 함수 `LoadGameplayConfigAtStartup()`:
  - `config.json`을 **읽기만** 한다. 새 `ConfigFile::Peek(path)`가 패널의 `ConfigFile::Load`와 같은 규칙으로 분류만 하고(Loaded / Missing / Unreadable / Broken, Broken이면 `.bak`도 읽어 둠) 아무것도 옮기거나 쓰지 않는다. `Load`는 `Peek`을 부르고 Broken일 때 MoveAside 단계만 더한다(패널 경로 동작은 그대로).
  - `GenerateDefaultConfig()` 위에 `MergeJsonNonNull`로 겹친 뒤 `ApplyGameplayConfig`만 한다. 단축키(`UpdateInputHandlerHotkey`)와 `pauseGameOnFocus`는 패널에만 의미가 있으므로 시작 로드에서 적용하지 않는다. 패널 로드가 지금처럼 적용한다(그래서 kDataLoaded에서 `RisaMenu::OnHotkeyChanged` → 재등록 호출도 생기지 않는다).
  - 파일 쓰기, `SetConfigSaveMode`, 패널 언어 파일 쓰기는 하지 않는다. config.json이 없을 때의 legacy settings.json 병합은 메모리에서만 한다(파일로 쓰지 않음). 쓰는 일은 패널 로드가 계속 맡는다.
  - 결과(`StartupConfigResult`)는 두 가지를 따로 알린다: `configBroken`(config.json이 JSON이 아니거나 객체가 아님), `applyThrew`(읽기·적용 중 예외). 둘 다 트리 시작 로드를 건너뛰게 하고 각자 다른 SKIP 이유를 남긴다.
  - 호출 위치: `OnDataLoaded`에서 XP 소스 등록(`registry.InitializeAll()`) 뒤. `ReadModdedSourcesFromConfig`가 이미 등록된 소스 위에 겹치기 때문이다.

### 2.2 트리: 시작 때 저장 파일에서 선행조건 채우기

- 지금 `OnSetTreePrerequisites`의 항목별 적용 부분(파싱, 자기 참조 제거, softNeeded 상한, `SetPrereqRequirements`, 끝의 `InvalidateTomeInventoryCache`)을 UI 밖 함수 `ApplyTreePrerequisiteEntries(const json& entries)`로 옮기고, 핸들러는 그것을 부른다. 패널 경로의 C++ 규칙은 한 줄도 바뀌지 않는다.
- `OnLoadSpellTree`의 requiredXP 동기화 반복(90-118행)도 `SyncRequiredXPFromTree(const json& tree)`로 옮겨 두 곳이 같이 쓴다. (생성된 트리에는 requiredXP가 없어 지금은 아무 일도 안 하지만, 기능을 빠뜨리지 않기 위해 옮긴다.)
- 새 순수 함수 `BuildPrerequisiteEntries(const json& tree) -> json`: 패널 JS 빠른 경로(`_loadTrustedTree`)와 `sendTreePrerequisites`가 만드는 배열을 그대로 재현한다. 게임 타입(RE::)을 쓰지 않아 오프라인 테스트로 확인할 수 있다. 규칙(조사 보고서 기준, 구현 때 JS 원문으로 다시 확인):
  1. 모든 학파의 `nodes`를 JSON 순서대로. id = `formId`, 없으면 `id`. 둘 다 없으면 건너뜀.
  2. `prerequisites`/`hardPrereqs`/`softPrereqs`에서 문자열이 아닌 것과 자기 id를 뺀다 (`treeParser.js:48-51` withoutId).
  3. `softNeeded`를 soft 개수 이하로, 기본 0 (`treeParser.js:54-58`).
  4. hard·soft가 둘 다 비면 `prerequisites`로 대체: 1개면 hard, 여러 개면 첫째 hard·나머지 soft·softNeeded 1 (`treeLoad.js:158-166`).
  5. 루트(`isRoot` 또는 학파 `root`와 같은 id)는 hard·soft·softNeeded를 비운다 (`treeLoad.js:171-175`).
  6. `PrereqEdit.repair` (`prereqEdit.js:75-91`): 중복 제거, hard와 soft에 다 있으면 hard만, 그때 옮긴 id가 `locks[].nodeId`면 softNeeded를 하나씩 줄이고 0..soft 개수로 자른다.
  7. 내보내기 (`treeLoad.js:14-49`): `_dup_` 앞에서 자른 id로 바꾸고, 사본 `X_dup_n`은 원본 `X`가 없고 첫 사본일 때만 보낸다. hard·soft 목록도 같은 방식으로 바꾸고 문자열 기준 중복 제거.
- 새 함수 `LoadTreeStateAtStartup()`:
  - `UIManager::GetTreeFilePath()`와 같은 경로의 `spell_tree.json`을 읽는다. 없거나 비었거나 깨졌으면 로그 한 줄 남기고 아무것도 안 한다(패널 경로와 같은 처리). 파일 읽기(`ReadTreeFileText`), 빈 파일 판정(`IsBlankTreeText`), 검증과 "원문/검증본" 선택(`ValidateTreeForPanel`: `resolvedFromPersistent > 0 || invalidNodes > 0`, 패널 경로의 로그 두 줄 포함)은 `OnLoadSpellTree`와 같은 함수를 쓴다.
  - 64 MB(`kMaxStartupTreeMiB`)를 넘는 `spell_tree.json`은 시작 로드에서 읽지 않고 로그를 남긴다(패널 경로에는 상한 없음, 지금과 같음).
  - `SpellScanner::ValidateAndFixTree`를 그대로 돌린다(메모리에서만, 파일에 다시 쓰지 않음). 바로 앞에 `Startup tree load: validating spell_tree.json (the panel validates it again when it loads)` 한 줄을 남겨, 검증기 로그가 두 번 나오는 이유를 알 수 있게 한다.
  - 함수 본문 전체가 하나의 `try/catch(const std::exception&)` 안에 있다(SKSE 메시지 핸들러로 예외가 나가지 않게). 잡으면 단계 이름(`spell_tree.json could not be read` / `applying the prerequisites threw` / `the requiredXP sync threw`)으로 로그와 `TreeParity::Skip`을 남긴다.
  - 빠른 경로 조건(`trustPrereqs` 또는 첫 노드에 `x`)이 아니면 건너뛰고 로그를 남긴다. 느린 경로(TreeParser)는 옮기지 않는다 — 패널이 있으면 패널이 계속 처리한다.
  - `ClearAllTreePrerequisites()` → `ApplyTreePrerequisiteEntries(BuildPrerequisiteEntries(tree))` → `SyncRequiredXPFromTree(tree)`.
  - 호출 위치: `OnDataLoaded`에서 설정 적용 바로 뒤, `PrepareTreeSpellForTome` 스레드 시작 전.

### 2.3 자가 확인 (로그 PASS/FAIL)

- 시작 로드가 끝나면 트리 상태의 지문(트리 주문 집합, 각 주문의 hard·soft·softNeeded를 정렬해 만든 텍스트의 해시와 개수)을 기억한다.
- 그 뒤 패널의 `SetTreePrerequisites` 데이터 요청이 처음 적용되면 같은 지문을 다시 만들어 비교하고 `SpellLearning.log`에 한 줄 남긴다: `TreeParity PASS (n spells, m with prerequisites)` 또는 `TreeParity FAIL` + 다른 주문 최대 5개. 한 세션에 한 번만.
- 판정 줄이 세션마다 정확히 한 줄 나오도록, 비교하지 못하는 경우는 모두 `TreeParity SKIP (이유)`로 끝난다:
  - 시작 로드가 트리를 적용하지 않음(파일 없음·빈 파일·64 MB 초과·읽기 실패·빌더 거부·config.json 깨짐·설정 적용 예외 등, 이유별 문구).
  - 패널 없음(UIManager 초기화 실패, Prisma 없음): 기준을 남긴 직후 `TreeParity SKIP (no panel this session - startup state applied)`.
  - 무장(arm)은 `SendTreeData`가 실제로 보냈을 때(`true` 반환)만, 보낸 **뒤에** 한다. 무장 시각을 기억하고, 다음 데이터 적용이 30초(`kPanelLoadWindow`) 넘어서 오면 비교하지 않고 `TreeParity SKIP (the panel sent no prerequisites for its tree load)`.
  - 비교 전에 새 게임 시작(`OnNewGame`)이나 세이브 로드(`OnPostLoadGame`)가 오면 `TreeParity::Expire`가 SKIP을 남긴다(기준을 남겼거나 무장한 상태일 때만).
- 이 확인은 늘 켜져 있어도 비용이 작다(트리 1회 정렬). 별도 설정을 만들지 않는다.

## 3. 바뀌는 것과 안 바뀌는 것

- 패널이 있을 때: 상태가 DOM 준비 뒤가 아니라 kDataLoaded부터 채워진다. 몇 초 뒤 패널이 같은 내용을 다시 보내 덮어쓴다(지문 비교로 확인). 설정도 같은 값이 두 번 적용된다(같은 함수라 결과 동일).
- 패널이 없을 때: 트리 선행조건, 트리 소속, 게임플레이 설정이 이제 적용된다. 패널 화면이 하던 일(트리 보기·생성·편집·저장, 설정 저장)은 여전히 없다.
- 안 바뀌는 것: 패널 JS, 저장 형식(co-save, config.json, spell_tree.json), 학습·경험치 계산, 패널 경로의 C++ 규칙.
- 알려진 한계:
  - 편집 모드에서 저장하지 않고 나간 선행조건은 파일에 없으므로 다음 실행의 시작 로드에 없다. 패널은 지금처럼 다시 보내므로 패널이 있으면 차이 없음.
  - 느린 경로 트리(좌표 없는 옛 트리)는 시작 로드가 건너뛴다.
  - 고아 수리(`orphanRepair.js`)는 파일만 고치고 C++에 다시 보내지 않는 기존 동작 그대로다. 시작 로드는 파일을 읽으므로 오히려 다음 실행부터 반영된다.
  - 깨진 config.json을 패널이 옮기지 못하는 경우(`BrokenKept`: 이름 바꾸기 실패, `.broken` 사본 20개 초과)만은 결과가 다르다. 패널은 그 세션에 기본값을 쓰고, 시작 로드(`Peek`)는 옮겨질지 미리 알 수 없어 `.bak`을 쓴다. 이때도 config.json이 깨졌으므로 트리 시작 로드는 건너뛴다.
  - 64 MB를 넘는 `spell_tree.json`은 시작 로드가 건너뛴다(패널은 지금처럼 읽는다).
  - 패널이 트리를 받기 전에 새 게임을 시작하거나 세이브를 불러오면 PASS 대신 SKIP이 나온다. 패널이 있는데 트리 로드 없이 메인 메뉴에서 게임을 끄면 판정 줄이 없다.
  - 빌더는 패널 JS가 예외를 던지는 트리도 "아무것도 안 보냄"으로 맞춘다: 이름이 `hasOwnProperty`인 학파(`data.schools.hasOwnProperty is not a function`, `treeLoad.js` 101·343행), 그리고 로그 문자열·`isNaN`·속성 키로 바뀌어야 하는데 바뀔 수 없는 값(자기 `toString` 키를 가진 객체 또는 그런 객체를 담은 배열): `trustPrereqs`, 학파의 `root`·`color`·`spokeAngle`·`startAngle`·`endAngle`, 학파마다 첫 3개 노드의 `x`·`y`·`name`·`tier`.

## 3.5 GPT 설계 검토 반영 (2026-10-24, gpt-6-astra, 7건 모두 코드로 확인)

1. **패널이 받는 것과 같은 트리를 변환한다.** 패널은 `resolvedFromPersistent > 0 || invalidNodes > 0`일 때만 검증된 트리를 받고(그때는 `JsonText::Dump`한 정렬된 JSON), 아니면 파일 원문을 받는다 (`UIManagerTree.cpp:83-85`). 검증기는 카운터가 0이어도 id 표기를 `0x%08X`로 바꾼다. 그래서 시작 로드도 같은 조건으로 고른다: 카운터가 0이면 **원문을 키 순서 그대로**(`nlohmann::ordered_json`) 변환하고, 아니면 검증된 트리를 그 키 순서(정렬)대로 변환한다. 학파 순서는 같은 formId가 여러 학파에 있을 때 마지막 것이 이기므로 결과에 영향을 준다. requiredXP 동기화는 지금처럼 검증된 트리로 한다.
2. **문자열 lock도 받는다.** `_lockIds`(`prereqEdit.js:50-58`)는 `{nodeId}` 객체와 맨 id 문자열을 모두 받는다.
3. **설정 파일 고르는 규칙을 패널과 같게, 단 쓰기는 안 함.** `config.json`이 없으면 defaults 위에 legacy `settings.json`을 겹친다(`UIManagerConfig.cpp:404-413`). 읽을 수 없음(열기 실패)이면 defaults만. JSON이 아니거나 객체가 아니면 패널은 파일을 옮기고 `.bak`을 쓰므로, 시작 로드는 옮기지 않고 `.bak`만 읽는다. 다 실패하면 defaults.
4. **config.json이 깨져 있으면 시작 트리 로드는 건너뛴다.** `ValidateAndFixTree`의 gate 판정(`ReadGateKept`, `SpellScannerGate.cpp:262`)은 디스크의 config.json을 직접 읽는다. 패널은 먼저 설정 로드에서 깨진 파일을 `.bak`으로 바꿔 둔 뒤 트리를 읽으므로, 깨진 파일 상태로 시작 로드를 하면 gate 결과가 패널과 달라질 수 있다. 드문 경우라 건너뛰고 로그를 남긴다(패널이 있으면 패널이 처리).
5. **requiredXP 동기화의 수명은 패널과 같다(한계로 기록).** `SetRequiredXP`는 `m_spellProgress`에만 남고 세이브 로드 때 지워진다. 패널 경로도 메인 메뉴 한 번만 하므로 동작은 같다. 생성된 트리는 requiredXP를 만들지 않아 지금은 영향이 없다. 보존 방법은 이번 범위가 아니다.
6. **TreeParity는 같은 입력일 때만 비교한다.** 시작 로드가 성공했을 때만 기준을 남긴다(아니면 `TreeParity SKIP` + 이유). 비교는 `OnLoadSpellTree`가 패널에 저장 트리를 보낸 **바로 다음** `SetTreePrerequisites` 데이터 적용 뒤에 한 번만 한다. 편집 커밋·자동 생성 적용은 비교하지 않는다.
7. **빠른 경로 규칙을 JS와 똑같이.** id는 `nd.formId`만(대체 없음). 빠른 경로 판정은 `trustPrereqs`가 참이거나, 학파 키 순서상 **첫 학파의 첫 노드**에 `x` 키가 있으면(`null`이어도 있음으로 본다) 참. 첫 학파의 노드가 비면 다음 학파를 보지 않는다 (`treeLoad.js:338-352`).

## 4. 검증

- 빌드: `BuildRelease.ps1 -preset Release-2026`.
- 오프라인 테스트(tools/에 작은 exe): `BuildPrerequisiteEntries`에 정상(일반 트리), 경계(루트·잠금 repair·`_dup_` 사본·중복 학파), 실패(노드 id 없음·문자열 아닌 항목·빈 트리) 입력을 넣어 기대 배열과 비교.
- 실제 데이터: 사용자 `spell_tree.json`(overwrite)을 입력으로 오프라인 exe가 만든 항목 수와 hard·soft 합계를 기록. 같은 파일을 패널 JS 로직(node로 `sendTreePrerequisites` 경로를 재현할 수 있으면)과 비교.
- 게임 안: 로그의 `TreeParity` 한 줄을 Claude가 읽어 판정. 사용자가 할 일은 게임을 켜고 저장을 불러오는 것뿐.
- 리뷰: 독립 리뷰어 에이전트 + GPT(Codex) 코드 리뷰.
