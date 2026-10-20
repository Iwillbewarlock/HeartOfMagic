# Toxicology 1.1 — 하급 권능으로 켜고 끄기 (설계)

2026-10-20. 1.0 설계(`2026-10-17-toxicology-addon-design.md`)에 덧붙는다. 여기 없는 동작은 1.0 그대로다.

## 1. 사용자가 정한 것 (2026-10-20)

- 기능을 켜고 끄는 토글을 둔다. 수단은 하급 권능(Lesser Power).
- 독 선택(즐겨찾기 맨 위), 병 소모(매지카 100당 1병), 아이콘은 1.0 그대로.
- "주문 적중을 무기 독 적중처럼 판정"은 조사 결과 이 로드 오더에서 무기 독에만 걸리는 퍽이 독 횟수 진입점
  (Mod Poison Dose Count) 하나뿐이라, 사용자가 넣지 않기로 했다(호환성 우선). 조사 내용은 7절.

## 2. 상태 = 능력을 가졌는가

켜짐/꺼짐은 따로 저장하지 않는다. 켜짐은 플레이어가 능력 `0x802`(HoMTox_ToxicologyAbility)를 가진 상태,
꺼짐은 없는 상태다. 그래서

- 지금의 모든 관문(`enabled && PlayerHasAbility()`: 적중 적용, 독 선택, 아이콘 키워드, 매지카 집계 훅)이 그대로
  토글을 따른다. 코드의 관문은 바꾸지 않는다.
- 상태는 게임 세이브에 들어간다(능력은 엔진이 저장). co-save 레코드를 늘리지 않는다.
- 켜진 동안 마법 메뉴의 활성 효과에 "Toxicology"가 보인다(1.0과 같은 효과 `0x801`).

## 3. ESL 레코드 (다음 오브젝트 ID `0x808`)

| FormID | 종류 | EditorID | 내용 |
|---|---|---|---|
| `0x804` | MGEF | `HoMTox_TogglePowerEffect` | 이름 "Toxicology". 아키타입 Script(스크립트 없음), Fire and Forget, Self, 마법 계열 None, 적대·해로움 플래그 없음, No Duration·No Magnitude·No Area, 설명문 = 권능 설명 |
| `0x805` | SPEL | `HoMTox_ToxicologyPower` | 이름 "Toxicology". 타입 Lesser Power, Fire and Forget, Self, 비용 0(수동 비용), 시전 시간 0, 효과 `0x804` 하나(크기·범위·지속 0) |
| `0x806` | MESG | `HoMTox_MsgOn` | 알림 문구 "Toxicology on" 류(문구는 Argon 초안). 문구는 FULL(이름)에 둔다, DESC 는 비움 |
| `0x807` | MESG | `HoMTox_MsgOff` | 알림 문구 "Toxicology off" 류. 문구는 FULL |

`0x805`의 장착 슬롯(ETYP)은 VoiceEquip(`025BEE:Skyrim.esm`).

- BOOK `0x803`의 가르치는 주문을 `0x802`에서 `0x805`로 바꾼다. HoM 주문서 훅은 권능을 가르치는 책을 바닐라
  방식으로 처리한다(`plugins/spelllearning/src/SpellTomeHook.cpp:81-88`, 1.0 설계 4a).
- 문구 변경: `0x801` DNAM(활성 효과 설명), `0x803` DESC(책 본문, 권능으로 켜고 끈다는 내용 추가), 새 `0x804` DNAM,
  `0x806`/`0x807` FULL(알림 문구; DESC 는 비움). 영어 초안은 Gemini Argon이 사실표에서 쓰고 Claude가 줄마다 대조한다. 한국어판은 같은
  레코드들을 한국어로, 번역 최종 검수(Argon·GPT).
- 만드는 법: houseCARL로 다시 쓰면 `Update.esm` 제거·재색인을 또 해야 하므로, 1.0의 문구 변경처럼 node 바이트
  편집으로 만든다(기존 `0x801`/`0x802` 레코드를 본으로 복사해 필드만 바꾸고, MESG 그룹은 새로 만든다). 헤더의
  레코드 수(HEDR)와 다음 오브젝트 ID, 그룹 크기를 다시 계산한다. 결과는 `housecarl_records`로 읽어 확인한다.
- `tools/toxicology-esl-compare`의 "한국어로 달라도 되는 서브레코드" 목록에 새 문구 서브레코드
  (`0x804` FULL·DNAM, `0x805` FULL, `0x806`/`0x807` FULL)를 더하고, 망가뜨린 사본 시험도 맞춘다.

## 4. DLL

- **권능 감지(설계 검토 반영, 2026-10-20)**: 길 두 개를 둔다. 어느 하나만 와도 된다(`game/ToxToggle.*`).
  - `TESSpellCastEvent`: 주문이 권능 `0x805`이고 시전자가 플레이어.
  - `TESMagicEffectApplyEvent`: 효과가 `0x804`이고 시전자와 대상이 모두 플레이어.
  둘 다 SKSE 태스크(메인 스레드, 세션 세대 번호)를 걸고, 로드·새 게임 뒤의 태스크는 버린다. 두 싱크는 매지카
  집계의 `CastSink`와 따로 등록하고 `enabled`·능력을 보지 않는다(꺼진 상태에서도 권능이 켤 수 있어야 하므로).
  싱크는 어느 스레드에서나 올 수 있어 FormID 비교는 원자 변수로 하고, 자가 테스트용으로 길별 이벤트 수를 센다.
  실제 Z 키 시전이 어느 이벤트를 내는지는 게임 안 확인 필요다(로그의 `power used (spell cast event)` /
  `(effect apply event)` 줄로 판정).
- **디바운스**: 한 번 쓴 권능이 두 이벤트를 다 낼 수 있으므로, 마지막으로 받아들인 토글 뒤 0.3초(steady clock,
  실시간) 안의 토글은 반복으로 보고 버린다(로그). 정확히 0.3초 뒤는 새 토글이다. 시각은 `kPreLoadGame`과
  `kNewGame`에서 지운다(로드 뒤 첫 사용은 반복이 아님).
- **토글**: 능력이 있으면 `RemoveSpell(0x802)`, 없으면 `AddSpell(0x802)`. 그 뒤 `HasSpell`을 다시 읽어 실제로
  바뀐 상태만 로그와 알림으로 낸다. 바뀌지 않았으면 오류 로그만, 알림 없음. 알림은 MESG `0x806`/`0x807`의
  FULL(이름) 문구를 `RE::SendHUDMessage::ShowHUDMessage`로 띄운다(설치된 ESL의 언어 그대로). CommonLibSSE-NG
  v10.0.0에는 `RE::DebugNotification`이 없고, 이 함수가 Debug.Notification이 쓰는 엔진 함수다(BookXP·HoM도 이것을
  쓴다). 메시지가 없거나 문구가 비면 경고 로그, 알림만 빠지고 토글은 한다. 바꾼 뒤 `OnSelectionMaybeChanged`를
  불러 새 상태를 바로 적용한다: `CurrentPoison(true)`가 매지카 집계 훅의 플래그를 다시 내고(`PublishMagickaTracking`),
  꺼짐이면 모방 키워드를 독약에서 떼고 켜짐이면 다시 붙인다.
- **`enabled` 꺼짐**: 토글은 그대로 능력을 넣고 뺀다(기능은 1.0처럼 꺼져 있음). 로그에 그 사실을 남긴다.
- **1.0 세이브 이전**: `kPostLoadGame` 성공 뒤(주문서 보상의 로드 확인과 같은 자리, data 가 null 이 아닐 때만)
  태스크(세션 세대 번호)가 능력·권능을 읽는다. 능력은 있고 권능이 없으면 권능을 주고 결과를 다시 읽어 로그(켜진
  상태 유지). 둘 다 없으면 아무것도 안 한다. 권능만 있으면 그대로(꺼진 상태). 새 게임에서는 아무것도 안 한다.
  co-save 레코드는 늘리지 않는다.
- **책을 새로 읽은 경우**: 권능만 배우고 꺼진 상태로 시작한다. 책 본문이 권능을 쓰라고 알려 준다.
- 판단은 순수 함수로 둔다(코어, 오프라인 하네스): `DecideToggle(hasAbility) -> {add, remove}`,
  `DecideMigration(hasAbility, hasPower) -> {grantPower, nothing}`, 디바운스 `IsRepeatToggle(last, now, window)`.

## 5. 테스트

- 오프라인(`toxicology-test --self-test`): 위 순수 함수 3개에 정상·경계·실패 사례(`tools/toxicology-test-reward.cpp`:
  `toxicology-test.cpp`가 600줄 한도에 가까워서), ESL 쌍 검사(새 서브레코드 포함, 망가뜨린 사본 FAIL).
  `IsRepeatToggle`: 처음(없음)은 반복 아님, 창 안(같은 순간 포함)은 반복, 정확히 창 끝과 그 뒤는 반복 아님,
  NaN·무한대·0 이하 창·시계가 거꾸로 간 경우는 반복 아님(권능이 잠기지 않게).
- 게임 안 자가 테스트(dev 파일이 있을 때):
  - 단계 `4b power`(`game/ToxSelfTestToggle.cpp`): 4a 보상 단계 뒤, 1단계(능력을 줌) 앞에서 세이브 자신의 능력
    상태로 돈다. `enabled`와 상관없이 돈다.
  - 레코드: `0x804`(Script 아키타입, Fire and Forget, Self), `0x805`(Lesser Power, Fire and Forget, Self, 효과 `0x804`
    하나, 장착 슬롯 VoiceEquip `025BEE:Skyrim.esm`), `0x806`/`0x807`의 FULL 이 비지 않음. 책이 `0x805`를 가르치고
    값 350 인지는 기존 4a 판정을 `0x805`로 바꿔 본다.
  - 토글(비동기): 능력 유무를 기록하고 플레이어 instant caster로 `CastSpellImmediate(power, false, player, 1.0, false,
    0.0, player)` → 자가 테스트 자신의 틱(100 ms)마다 `HasSpell(0x802)`가 뒤집혔는지 본다(메인 스레드를 막지 않음).
    5초 안에 안 뒤집히면 FAIL "no toggle (neither event arrived)"(또는 이벤트는 왔는데 안 바뀜), 길별 이벤트 수를 함께
    적는다. 1초(디바운스보다 김) 기다려 한 번 시전에 토글이 정확히 한 번이었는지 보고, 다시 시전해 되돌아오는지 본다.
    꺼진 직후에는 `CurrentPoison`이 없음인지도 본다.
  - 이전: 권능 유무를 기록하고 능력을 둔 채 권능을 빼고 이전 함수를 부름 → `GrantPower`, 권능이 생김. 둘 다 있을 때
    → `Nothing`. 능력을 빼고 권능만 있을 때 → `Nothing`, 능력이 생기지 않음.
  - 끝에(그리고 도중에 끝날 때도) 세이브의 능력과 권능을 원래대로 되돌린다(`UndoToggle`). 로드·새 게임이 오면 기존
    중단 경로가 다음 틱에 끝내고, 걸려 있던 토글 태스크는 세션 세대 번호로 버려진다.
  - 1~7단계는 시작할 때 능력을 주므로 그대로.
- 사용자 게임 안 확인(자동화 불가 이유: 실제 Z 키 입력과 화면 알림은 플러그인이 대신 할 수 없음):
  Z로 권능을 써서 알림 문구가 보이는지, 로그의 `toggle` 줄이 나오는지(판정은 Claude가 로그로).

## 6. 문서

README(토글, 이전, 레코드 표, 확인/미확인), ESL.md(새 레코드와 만든 법, 한국어 표), CHANGELOG, VERSION 1.1.0,
넥서스 문구(영어는 Argon 초안, 한국어는 최종 검수).

## 7. 조사 기록 (무기 독 판정, 넣지 않음)

- CommonLibSSE-NG v10.0.0: `HitData`·`TESHitEvent`에 독 정보가 없고, 무기 독 적중을 적용하는 함수도 없다.
  `ExtraPoison`은 독과 남은 횟수만 가진다.
- Poisoner's Aid(Apache-2.0) 소스: 엔진이 근접·화살 적중 때 독을 꺼내는 호출 지점(37799/38748 +0x148/0x153,
  41778/42859 +0x11A/0x11F)과 독 횟수 진입점 호출 형태(`HandleEntryPoint(kModPoisonDoseCount, player, weapon, poison, &out)`,
  시작값 1).
- houseCARL 퍽 조사(TKL - MUNG ADDON, Vokriinator Black 트리): 무기 전달에만 걸리는 퍽은 모두 독 횟수 퍽(바텀리스 컵,
  우로보로스, 뱀 수호석, Artificer 장비, 컨트롤러 퍽). 독살자 등은 양조 때, 알카헤스트 등은 대상 상태로 이미 작동.
