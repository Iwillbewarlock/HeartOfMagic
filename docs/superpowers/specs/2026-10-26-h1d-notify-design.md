# H1-d 알림 계약 — 설계

날짜: 2026-10-26. 기준: `2026-10-24-h1-next-plan.md` 1절 표와 1.5절 "H1-d 정정".

## 목표

진행 쪽 코드(ProgressionManager XP/Targets/Core/API, SpellEffectivenessHookGrant, SpellTomeHook, ISLIntegration, LibrarianSpellResearchPatch)가 UIManager 를 직접 부르지 않고 `ProgressionNotify` 한 파일만 부른다. 알림은 상태 변경이 끝난 뒤 나가고, 알림끼리의 순서는 지금과 같다.

## 지금 (조사 결과)

21곳의 직접 호출. 알림이 상태보다 먼저 나가는 곳:

| 위치 | 알림 | 아직 안 바뀐 상태 |
|---|---|---|
| Targets `SetLearningTarget` (교체) | Cleared(old) | `m_learningTargets[school]`, prereqs, EnsureProgress, 재부여 |
| Core `DropTargetFromOtherSchools` | Dropped | 해당 키 erase |
| Core `CanonicalizeLearningTargets` | Dropped/Set | `m_learningTargets = placed` (Dropped 만) |
| Targets `ClearLearningTarget` | RemoveEarly 의 available, Cleared | `m_learningTargets.erase(school)` |
| XP `AddXP` 마스터 | MarkMastered 의 mastered | `progress.unlocked = true`, 대상 해제 |
| Targets `UnlockSpell` / `RelockSpell` | mastered / available | 대상 해제, progress 초기화 |

## 설계

`include/ProgressionNotify.h` + `src/ProgressionNotify.cpp` (한 파일 쌍).

- 함수: `Progress(formId, cur, req)`, `ProgressRefresh(formIdText)`(값은 보낼 때 읽음), `SpellReady`, `SpellUnlocked`, `TargetSet(school, formId, name)`, `TargetCleared(formId)`, `TargetDropped(school, formId)`, `SpellState(formId, state)`, `ModdedSourceRegistered(...)`, `SpellResearchStatus()`.
- 각 함수는 `Event`(종류 + 인자)를 만든다. 열린 `Batch` 가 없으면 바로 보내고, 있으면 줄에 넣는다.
- `Batch`: RAII. 깊이 카운터(thread_local). 0→1 에서 열리고 가장 바깥 소멸자가 줄을 비운다. 안쪽 Batch 는 깊이만 올린다(Set → Drop, Clear → RemoveEarly, AddXP → MarkMastered → ClearLearningTargetForSpell). 비울 때 줄을 지역 변수로 옮긴 뒤 보낸다(보내는 중 새 알림은 그 시점 규칙대로).
- 순서: 줄 순서 = 호출 순서. 그래서 drop/clear → set/grant → 최종 unlocked → progress 순서가 지금과 같다. 바뀌는 것은 "언제"뿐(같은 함수가 끝날 때).
- Batch 를 여는 곳(상태를 여러 단계로 바꾸는 진행 쪽 진입점, 함수 첫 줄): `SetLearningTarget`, `ClearLearningTarget`, `SetLearningTargetFromTome`, `CanonicalizeLearningTargets`, `AddXP(FormID)`, `UnlockSpell`, `RelockSpell`. 일찍 return 해도 소멸자가 비운다. ProgressionManager 에는 mutex 가 없어 잠금 안에서 보내는 문제는 없다.
- 보내기(Dispatch)는 지금과 같은 UIManager 함수를 부른다. 패널 표시 필터(숨김 시 progress·ready 생략, view 유효성)는 UIManager 에 그대로 둔다. 예외는 이벤트마다 잡아 로그로 남기고 다음 이벤트를 보낸다(소멸자에서 던지지 않음).
- 관측 지점: `SetObserver(std::function<bool(const Event&)>)`. 보내는 순간(줄을 비우는 순간)에 불리고, false 를 돌려주면 그 알림은 패널로 가지 않는다. 자가 테스트가 그 안에서 ProgressionManager 조회를 해 "그 순간의 상태"를 기록한다. 평소에는 비어 있음(비용: 알림마다 mutex 잠금과 빈 function 복사 하나). 관측자는 따로 try 로 감싸, 관측자가 실패해도 패널 알림은 나간다.
- UIManager 가 던지는 예외는 이제 호출자에게 올라가지 않고 여기서 로그로 남는다.
- ModEvent(`SendModEvent`)는 옮기지 않고 시점도 예전 그대로다. Papyrus 리스너는 VM 이 나중에 실행하고, SKSE 플러그인의 C++ ModCallbackEvent 싱크는 `SendModEvent` 안에서 바로 불린다(예전과 같음). HUD 알림도 그대로.
- 범위 밖: Main/PapyrusAPI/RisaMenu 의 패널 제어, UIManager 내부, 큰 이벤트 버스.

## 자가 테스트 (dev 전용, selftest.json)

`Environment::SelfTest` 의 실행 목록에 `notify` 검사를 더한다(같은 스위치·대기·PASS/FAIL 줄).

GPT 설계 검토(2026-10-26, gpt-6-astra) 지적 3건 반영: 기존 진행이 있는 주문은 쓰지 않는다, Set 알림은 마법책 경로에서만 나온다, 교체 때는 "키 없음"이 아니라 "키가 새 주문"으로 판정한다.

1. 관측자는 단계의 알림을 보내는 순간 기록하고 패널로는 보내지 않는다(패널의 상태와 "새 주문" 표시는 정리가 되돌릴 수 없어서, 코드 리뷰 지적). 조기 학습이 켜져 있고 `unlockThreshold` 가 (0, 100] 밖이면 시작 전에 SKIP. 대상 없는 학파에서, 플레이어가 모르고 진행 기록·조기 추적·마법책 XP 기록이 모두 없는 트리 주문 A, B 를 고른다(조기 학습이 켜져 있으면 `CanLearnEarly` 도). 없으면 SKIP.
2. 단계마다 관측자를 달고 실행한 뒤, 각 알림 순간의 상태가 단계가 끝난 뒤의 상태와 같은지, 알림 종류·순서가 기대와 정확히 같은지 본다.
   - set: 마법책 경로 `SetLearningTargetFromTome(A)` → `[target-set(A)]` (single 모드에서 다른 학파 대상이 있으면 직접 `SetLearningTarget`, 알림 없음).
   - replace: `SetLearningTarget(B)` → `[target-cleared(A)]`, 그 순간 학파 키는 B.
   - master: 조기 학습 켜짐이면 `AddXP(B, required)` → `[weakened, mastered, target-cleared(B), unlocked, progress]`. 꺼짐이면 `[ready, progress]` 뒤 `UnlockSpell(B)` → `[mastered, target-cleared(B)]`.
3. 되돌리기(소멸자, 예외에도): 준 주문 제거, `RelockSpell`, `ForgetProgress`. 값이 0인 키를 뺀 대상 맵, 두 주문의 진행 기록·보유·조기 추적·마법책 XP 가 처음과 같은지 `notify-cleanup` 줄로 판정. HUD 와 ModEvent 는 실제로 나간다(dev 세이브 전용). 직접 지정 prerequisites(`m_targetPrerequisites`)는 진행 기록 없는 주문에는 생기지 않는다고 보고 따로 되돌리지 않는다.

오프라인: `ProgressionNotify` 의 줄·깊이·순서 로직은 RE 없이 컴파일되는 부분으로 나눠 tools 아래 작은 하네스로 확인(정상·중첩·일찍 return·보내는 중 재진입).

## 완료 기준

- 진행 코드에 `UIManager::` 0, `uimanager/UIManager.h` include 0 (uimanager/, Main, PapyrusAPI, RisaMenu 제외).
- 빌드 경고 0, tree-prereq-check 등 기존 오프라인 검사 PASS, 새 하네스 PASS.
- 게임 자가 테스트 `notify` PASS (사용자 승인 뒤 배포해 확인).
