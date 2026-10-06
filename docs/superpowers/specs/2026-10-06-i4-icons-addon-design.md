# Heart of Magic I4 아이콘 애드온 — 설계

날짜: 2026-10-06. 상태: 설계 승인 대기(사용자 검토).
근거 조사: `lab/addon-research/README.md` §1 (숫자는 TAKEALOOK 로드 오더, 2026-10-06 스캔 기준).

## 1. 목적

Inventory Interface Information Injector(I4, Nexus 85702)는 메뉴 항목에 아이콘을 붙이는 SKSE 프레임워크다.
모드 주문은 원소 아이콘이 자주 빠진다.

- 불·얼음·번개 태그가 붙은 모드 주문 980개(소환 제외) 중 252개는 I4 기본 규칙에 안 걸린다(가장 비싼
  이펙트가 스크립트형이거나, 무속성 다원소이거나, 다른 원소 저항).
- 바람·물·대지·피 모드 주문 약 300개는 I4 에 아이콘이 있는데도 거는 규칙이 없다.
- 주문 팩마다 사람이 손으로 만든 I4 애드온이 따로 있다(TAKEALOOK 에 약 24개).

이 애드온은 하트 오브 매직의 사서 카탈로그(`spell_catalog.json`)로 **로드 오더의 모든 주문에 원소 아이콘
규칙을 자동으로 만든다.** 팩 전용 아이콘이 있으면 그쪽이 이긴다.

범위 밖(다음 버전): 신성·그림자·사령·비전 등 I4 에 그림이 없는 원소의 새 아이콘, 원소 안의 기능별 아이콘.

## 2. 배포 형태

넥서스에 따로 올리는 별도 모드. 같은 저장소에서 빌드한다.

| 파일 | 역할 |
|---|---|
| `HeartOfMagic_I4Icons.esl` | 레코드 없는 헤더만의 플러그인. I4 가 `<활성 플러그인 이름>.json` 만 읽기 때문에 이름을 걸 자리 |
| `SKSE/Plugins/HeartOfMagic_I4Icons.dll` | 생성기 |
| `dist/HeartOfMagic-I4Icons_<버전>.zip` | 배포 zip (본체 zip 과 별도) |

요구: Heart of Magic(카탈로그 생산자), I4. 선택: B.O.O.B.I.E.S(있으면 주문서 아이콘).
ESL 은 로드 오더 **앞쪽**에 두라고 README 에 적는다(아래 4절).

## 3. 동작

게임을 켤 때 한 번, DLL 의 `SKSEPlugin_Load` 에서 한다. I4 는 `kDataLoaded` 에 설정을 읽으므로
(I4 소스 `SKSEPlugin.cpp` → `CustomDataManager::LoadConfigs`) 그보다 항상 앞서고, 같은 실행에서 적용된다.
게임 데이터는 필요 없다 — 카탈로그 키(`AncientBloodII.esl|0x000812`)가 I4 의 FormID 형식
(`^plugin\.es[lmp]\|(0[Xx])?[0-9A-Fa-f]{3,8}$`, 스키마 확인)과 그대로 맞는다.

1. `SKSE/Plugins/SpellLearning/spell_catalog.json` 을 읽는다. 최상위 `version` 이 1, `vocab` 이 `tags-v1` 이
   아니면 중단한다.
2. 건너뛸 목록을 만든다. `SKSE/Plugins/InventoryInjector/*.json` 중 우리 파일을 뺀 전부를 읽어서:
   - 파일 이름이 플러그인 이름인 것(`Abyss.json` → `Abyss.esp`/`.esm`/`.esl`)은 그 플러그인의 주문을 통째로 건너뛴다.
   - 규칙의 `formId` / `teachesSpell` 에 적힌 FormID 는 그 주문·주문서를 건너뛴다(예: BOOBIES 의 모드 주문서 774권).
3. 남은 주문마다 원소를 고른다(5절 표). 원소가 하나도 없거나, 기법에 `summoning` 이 있으면(예: 화염 아트로나크
   소환 — 원소 아이콘보다 소환 학파 아이콘이 맞다) 규칙을 만들지 않는다.
4. 주문 규칙: `{"match":{"formType":"Spell","formId":{"anyOf":[...]}},"assign":{iconSource,iconLabel,iconColor}}`.
   아이콘이 같은 주문끼리 한 규칙으로 묶는다.
5. 주문서 규칙(B.O.O.B.I.E.S 의 `Interface/BOOBIES Immersive Icons/books.swf` 가 있을 때만):
   `{"match":{"formType":"Book","teachesSpell":{"anyOf":[...]}},"assign":{...}}`, 학파별 주문서 그림과 색.
   카탈로그 `school` 로 학파를 정하고, 원소 색은 쓰지 않는다.
6. `SKSE/Plugins/InventoryInjector/HeartOfMagic_I4Icons.json` 에 쓴다(MO2 에서는 overwrite 에 생김).
   내용이 지난번과 같으면 쓰지 않는다.

불·얼음·번개 주문도 규칙을 쓴다. 카탈로그에는 저항값이 없어 I4 기본 규칙이 이미 맞게 붙였는지 알 수 없고,
같은 아이콘을 다시 지정해도 결과는 같다. 기본 규칙이 틀린 경우(예: Natura 바위 룬에 화염 저항 → 불 아이콘)는
우리 규칙이 뒤에 읽혀 바로잡는다.

## 4. 겹칠 때의 승자

I4 는 활성 플러그인을 로드 오더 순서로 돌며 각자의 json 규칙을 한 줄로 쌓고, 메뉴 항목마다 맞는 규칙을
전부 차례로 적용해 `icon*` 값을 덮어쓴다(`CustomDataManager::ProcessEntry`, `Rule::SetIcon`).
**나중에 로드된 플러그인의 규칙이 이긴다.**

팩 전용 아이콘(예: Abyss 의 기능별 그림)이 더 세밀하므로 팩이 이겨야 한다. 두 겹으로 막는다.

1. 생성할 때 건너뛴다(3절 2단계) — 로드 순서와 무관하게 FormID 로 적힌 팩·BOOBIES 지정은 지켜진다.
2. ESL 을 앞쪽에 둔다 — 키워드로 지정하는 팩 규칙(생성 시점에 알 수 없음)이 뒤에 읽혀 이긴다.

I4 기본 `Skyrim.json`(Skyrim.esm)은 언제나 맨 앞이라 우리 규칙이 그 뒤에 온다.

## 5. 원소 → 아이콘

그림과 색은 I4 쪽 기존 값(I4 `Skyrim.json`, `I4IconAddon.json`)을 그대로 쓴다.

| 원소 태그 | iconSource | iconLabel | iconColor |
|---|---|---|---|
| `air` | `I4IconAddon/icons.swf` | `magic_wind` | `#CDCBC4` |
| `earth` | `I4IconAddon/icons.swf` | `magic_earth` | `#855A41` |
| `water` | `I4IconAddon/icons.swf` | `magic_water` | `#3861C5` |
| `blood` | `I4IconAddon/icons.swf` | `magic_vampire` | `#8E2129` |
| `sun` | `I4IconAddon/icons.swf` | `magic_sun` | `#FF8700` |
| `poison` | `skyui/icons_item_psychosteve.swf` | `misc_remains` | `#62A636` |
| `shock` | `skyui/icons_item_psychosteve.swf` | `magic_shock` | `#EAAB00` |
| `frost` | `skyui/icons_item_psychosteve.swf` | `magic_frost` | `#1FFBFF` |
| `fire` | `skyui/icons_item_psychosteve.swf` | `magic_fire` | `#C73636` |

**원소가 둘 이상이면 표의 위쪽(드문 원소)이 이긴다**: 바람 > 대지 > 물 > 피 > 태양 > 독 > 번개 > 얼음 > 불.
예: 대지+불(41개) → 대지, 불+얼음(42개) → 얼음. 위 9개 기준 다원소 주문은 1,594개 중 121개.
카탈로그의 `elements` 는 알파벳순이라 순서에 뜻이 없다 — 그래서 고정 우선순위를 쓴다.

주문서(B.O.O.B.I.E.S `books.swf`):

| school | iconLabel | iconColor |
|---|---|---|
| alteration | `book_spell_alteration` | `#86c542` |
| conjuration | `book_spell_conjuration` | `#b361d3` |
| destruction | `book_spell_destruction` | `#e12f2f` |
| illusion | `book_spell_illusion` | `#2b9be3` |
| restoration | `book_spell_restoration` | `#f5d730` |

## 6. 실패 처리

모두 게임을 멈추지 않고, `HeartOfMagic_I4Icons.log` 에 이유와 개수를 남긴다.

| 상황 | 동작 |
|---|---|
| 카탈로그 없음(전체 스캔 전) | 쓰지 않음 |
| 카탈로그 `version`/`vocab` 불일치 | 쓰지 않음 |
| I4 미설치(`SKSE/Plugins/InventoryInjector.dll` 없음) | 쓰지 않음 |
| B.O.O.B.I.E.S 없음 | 주문 규칙만, 주문서 규칙 없음 |
| 다른 I4 json 이 깨짐 | 그 파일만 건너뛰기 목록에서 빼고 계속 |
| 결과가 지난번과 같음 | 다시 쓰지 않음 |
| 마지막 전체 스캔 뒤에 추가한 주문 모드 | 다음 전체 스캔 전까지 규칙 없음(README 에 적음) |

## 7. 코드 구조

`plugins/I4Icons/` (BookXP 와 같은 CMake 구조, CommonLibSSE-NG, 600줄 한도).

- 순수 생성 함수: 입력 = 카탈로그 JSON, 다른 I4 json 목록(이름 + 내용), BOOBIES 유무 → 출력 = I4 json.
  파일 입출력과 분리해 게임 없이 돈다.
- DLL 진입점: 경로를 모으고 파일을 읽어 순수 함수를 부르고 결과를 쓴다. 로그.
- 오프라인 도구 `tools/i4icons-test`(BuildRelease 가 `build/tools/Release/` 에 빌드):
  `i4icons-test -c <catalog> -i <InventoryInjector 폴더> -o <out.json>`. 같은 순수 함수를 쓴다.
- ESL: 헤더만의 바이너리 파일을 저장소에 넣는다(I4 의 `I4IconAddon.esp` 와 같은 방식, 234바이트).
- `plugins/cmake/Distribution.cmake`: 별도 zip 조립.

## 8. 테스트

- `i4icons-test` 자체 검사: 우선순위(대지+불 → 대지), `Abyss.json` 이 있으면 `Abyss.esp` 주문 없음,
  BOOBIES 에 적힌 주문서 FormID 없음, 원소 없는 주문 없음, BOOBIES 없으면 Book 규칙 없음, 소환 주문 없음, 출력이 I4 스키마의
  FormID 형식에 맞음.
- TAKEALOOK 카탈로그로 실행해 규칙에 든 주문 수가 "카탈로그에서 5절 원소가 있고 소환이 아닌 주문 수 − 건너뛴 수"와
  맞는지 본다(바닐라 주문 포함. 모드 주문만 세면 원소 주문은 1,594개).
- 게임: TAKEALOOK(Case 122 로 SpellHotBar2 일가를 꺼 둠)에서 마법 메뉴에 바람·물·대지·피 아이콘, 빠지던
  불·얼음·번개 주문(예: Kittytail 혼돈의 용광로, CC 원소 폭발), Natura 바위 룬의 대지 아이콘, Abyss 주문은
  팩 아이콘 그대로, 모드 주문서의 학파 아이콘.

## 9. 문서

- `plugins/I4Icons/README.md`: 설치, 요구·선택 모드, 로드 오더(ESL 앞쪽), 동작, 한계(스캔 뒤 새 모드).
- CLAUDE.md 문서 안내 트리와 Key Locations 에 항목 추가, `docs/ARCHITECTURE.md` 에 애드온 한 줄.

## 10. 확인하지 못한 전제

- I4 가 MO2 가상 파일 시스템의 overwrite 에 생긴 json 을 읽는지(`BSResourceNiBinaryStream` 이라 읽을 것으로 봄).
- 바닐라 메뉴의 `magicType` 이 가장 비싼 이펙트의 저항값이라는 점(빠지는 252개 추정의 근거).
- 키워드 목록 매칭이 AND 인지 OR 인지(우리는 FormID 만 쓰므로 영향 없음, 팩 규칙 해석에만 관련).
- `SKSEPlugin_Load` 시점에 MO2 VFS 경로로 파일을 읽고 쓸 수 있는지(보통 가능, 미확인).
