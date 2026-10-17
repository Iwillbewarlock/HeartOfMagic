# Heart of Magic Librarian - Toxicology — 설계

날짜: 2026-10-17. 상태: 사용자 검토 대기.
브랜치: `feature/toxicology` (master d4f325c 에서 분기, 워크트리 `.claude/worktrees/toxicology`).

## 1. 목적

하트 오브 매직 Librarian 이 `poison` 태그를 붙인 주문(독 마법)이 대상에 맞으면, 플레이어가 즐겨찾기한 독약의
효과를 그 대상에게 건다. 독 마법이 독약의 구조를 복제하는 것이다. 독약은 피격마다 줄지 않고, 독 마법에 쓴 매지카가 100 쌓일 때마다 한 병 준다(사용자 결정, 5.6).

넥서스에 별도 페이지로 배포하는 애드온. 원작 Heart of Magic 과는 호환되지 않는다 — Librarian 이 쓰는
`spell_catalog.json` 의 태그를 읽기 때문이다(넥서스 페이지와 README 에 명시).

같은 기능의 넥서스 모드는 없다(2026-10-17 houseCARL 이름 검색 기준. 이름에 poison/venom 이 없는 모드는 놓쳤을 수 있다).

## 2. 사용자가 정한 것

| 항목 | 결정 |
|---|---|
| 독약 소모 | 독 마법에 쓴 매지카 100 마다 복제 중인 독약 한 병(고정값, 설정에서 변경 가능) |
| 어떤 독약 | 즐겨찾기한 독약 중 즐겨찾기 메뉴 맨 위 하나 |
| 겹침 | 처음 맞을 때 효과를 걸고, 효과가 남아 있는 동안 다시 맞으면 지속 시간만 처음으로 되돌린다(쌓이지 않음). 대상·독약마다 최소 1초 간격(설정) |
| 표시 | I4 로 복제 중인 독약의 아이콘을 "마법진 앞의 독약병" 그림으로 바꾼다 |
| 배포 | 넥서스, 이름 "Heart of Magic Librarian - Toxicology" |

## 3. 내가 정한 것 (사용자에게 말했고, 반대 없음)

- 시전자는 플레이어만. NPC 가 쓰는 독 마법에는 걸지 않는다.
- 독약 효과의 세기·지속 시간은 독약 값 그대로. 엔진이 `CastSpellImmediate` 에서 퍽을 적용하는지는 자가 테스트로
  기록만 하고 손대지 않는다.
- 모든 독 마법(투사체, 룬, 접촉, 집중)에 같은 규칙.
- 복제 중에 즐겨찾기 1순위가 바뀌면 이미 걸린 효과는 끝까지 가고, 다음 피격부터 새 독약.
- 즐겨찾기한 독약이 없으면 아무 일도 없다.
- 메뉴·알림 글자 없음. 게임 안 글자는 ESL 의 능력·주문서 이름과 설명뿐(4a). 설정은 json 하나.
- I4 가 없으면 아이콘만 빠지고 기능은 동작.
- 아이콘 색은 `#62A636`(사서가 독 원소 주문에 쓰는 초록). I4 기본 독약 색(`#AD00B3`, 보라)과 달라서 눈에 띈다.

## 4. 배포 형태

| 파일 | 역할 |
|---|---|
| `HeartOfMagic_Toxicology.esp` (ESL 플래그) | 키워드·마법 효과·능력·주문서(4a). I4 는 `<활성 플러그인>.json` 만 읽으므로 이름을 걸 자리이기도 하다 |
| `SKSE/Plugins/HeartOfMagic_Toxicology.dll` | 본체 |
| `SKSE/Plugins/HeartOfMagic_Toxicology.json` | 설정: `enabled`, `showIcon`, `minIntervalSeconds`(기본 1.0), `magickaPerDose`(기본 100) |
| `SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json` | I4 규칙: `{"match":{"formType":"Potion","keywords":"HoMTox_MimicPoison"},"assign":{"iconSource":"HeartOfMagicToxicology/icons.swf","iconLabel":"potion_mimic","iconColor":"#62A636"}}` |
| `Interface/HeartOfMagicToxicology/icons.swf` | 아이콘 1개(라벨 `potion_mimic`) |
| `SKSE/Plugins/HeartOfMagic_Toxicology/` | `LICENSE`, `THIRD-PARTY-NOTICES.md`, `licenses/` (NPC Spells 와 같은 방식) |
| `dist/HeartOfMagic-Librarian-Toxicology_<버전>.zip` | Data 루트 zip, FOMOD 없음. PDB 는 `_DebugSymbols.zip` |

- 버전은 `plugins/Toxicology/VERSION`(1.0.0), 소스 태그 `toxicology-v1.0.0`.
- 요구: SKSE, Address Library, Heart of Magic Librarian 1.5.0+(전체 스캔 1회). 선택: I4.
- ESL 은 `I4IconAddon.esp` 뒤에 로드되어야 우리 규칙이 이긴다. 새 플러그인은 기본으로 맨 뒤라 보통 맞다(README 에 적음).
- NPC Spells(`feature/npc-spells-1.5`, 미병합)가 같은 애드온 배포 틀(별도 VERSION, 별도 zip, 노티스)을 먼저 만들었다.
  그 브랜치가 먼저 병합되면 그 틀을 재사용하고, 아니면 이 브랜치가 같은 모양으로 만들고 나중 병합 때 맞춘다(빌더 판단).

## 4a. 능력과 주문서 (사용자 결정 2026-10-17)

기능 전체가 능력(Ability) "Toxicology" 에 묶인다. 플레이어가 이 능력을 갖고 있을 때만 5.2~5.6 이 동작한다
(복제, 아이콘 키워드, 매지카 집계, 독약 소모 모두). 능력은 주문서로 배우고, 주문서는 리프튼 연금술 상점
Elgrim's Elixirs 에서 판다.

- ESL 레코드: 키워드 `HoMTox_MimicPoison`, 마법 효과(MGEF, 이로운 상시 효과, 스크립트 없음, 설명문에 기능 설명),
  능력(SPEL, 타입 Ability, 위 MGEF 하나), 주문서(BOOK, "Teaches Spell" = 능력).
- HoM 과의 관계: HoM 의 주문서 훅은 능력·권능을 가르치는 책을 바닐라 방식으로 처리한다 — 바로 배우고 책이
  사라진다(`plugins/spelllearning/src/SpellTomeHook.cpp:81-88, 165-190`). 그래서 공부 XP 로 바뀌지 않고, 트리에도
  들어가지 않는다(스캐너는 kSpell 만 남김).
- 판매: 상인 상자에 주문서 1권을 넣는다. 레코드를 덮어쓰지 않고 DLL 이 `kDataLoaded` 에 메모리에서 상자 기본
  폼(또는 그 상자가 쓰는 레벨 목록)에 넣는다. 어느 상자·목록이 Elgrim's Elixirs 전용인지는 houseCARL 로
  확인한다(다른 상인과 공유하는 기본 폼이면 쓰지 말 것). 상인 재고가 바뀌는 주기(약 48시간)마다 다시 생긴다.
- 내 결정(확인 필요로 사용자에게 알림): 주문서 값 350 골드, 책 모델·아이콘은 바닐라 변화계 주문서를 복사,
  게임 안 글자(능력 이름, 효과 설명, 주문서 이름)는 영어만. 한국어는 이후 별도 번역 파일로.
- ESL 은 레코드가 4개라 생성기를 쓰지 않고 houseCARL 로 작성한 바이너리를 저장소에 넣는다(바닐라 주문서를 새
  레코드로 복사해 필드를 바꾸는 방식). 작성 단계는 README 의 개발 절에 적는다.

## 5. 동작

### 5.1 독 마법 목록

`kDataLoaded` 와 세이브 로드(`kPostLoadGame`, `kNewGame`) 때 `Data/SKSE/Plugins/SpellLearning/spell_catalog.json` 을
읽는다. `version` 이 1 이 아니면 아무것도 하지 않고 로그에 남긴다. `spells` 의 각 항목 중 `elements` 에 `poison`
이 있는 것의 키(`Plugin.esp|0x00XXXX`)를 FormID 로 풀어 `SpellItem*` 집합을 만든다. 풀리지 않는 키는 세어서 로그.
그중 **적대 효과가 하나라도 있는 주문만** 남긴다(`SpellItem` 의 이펙트 중 `EffectSetting::IsHostile()`; 내 결정).
사서는 트리 묶음용으로 Cure Poison, 독 저항 버프, Dispel Poison Cloak 에도 `poison` 을 붙이므로(`librarian/00_mgef.json:215,223`,
`80_manual.json:115,125`, `10_kit.json:121,192`), 거르지 않으면 동료에게 쓴 치료 주문이 동료를 중독시킨다.
독 저항을 깎는 저주(`10_kit.json:202`)는 적대라 남는다.
(세이브 로드 때 다시 읽는 이유: 플레이어가 게임 안에서 전체 스캔을 다시 하면 카탈로그가 바뀐다.)

### 5.2 복제할 독약 고르기

플레이어 인벤토리에서 `AlchemyItem::IsPoison()` 이고 `InventoryEntryData::IsFavorited()` 인 것 중 이름이 가장 앞인 것.
- 이름: 엔트리의 표시 이름(플레이어가 만든 독약은 붙인 이름). 대소문자 무시, 코드 포인트 순.
  근거: SkyUI 즐겨찾기 메뉴가 `_sortFilter.setSortBy(["text"], ...)` 로 이름순 정렬(schlangster/skyui
  `src/FavoritesMenu/FavoritesMenu.as:272`). 바닐라 메뉴의 순서는 확인 안 함 — 같은 규칙을 쓴다.
- 계산 시점: 피격 때(최대 0.5초에 한 번 다시 계산, 그 사이는 캐시)와 인벤토리·즐겨찾기 메뉴가 열릴 때.

### 5.3 피격 감지

`TESHitEvent` 싱크. 조건: `cause` 가 플레이어, `source` 를 `LookupByID` 한 폼의 타입이 `Spell` 이고 5.1 집합에 있음,
`target` 이 살아 있는 Actor 이고 플레이어가 아님.
- 근거: BladeAndBlunt·PAPER 가 `source` 로 주문을 찾는다(조사 보고). 한 피격에 이벤트가 여러 번 오므로
  (대상, 주문) 짝으로 같은 프레임 중복을 버린다.
- 우리 독 시전이 일으키는 이벤트는 `source` 가 AlchemyItem 이라 타입 검사에서 걸러진다(재진입 방지 겸).
- 룬·폭발·망토형, 그리고 `kNoHitEvent` 이펙트에서 이 이벤트가 오는지는 모른다 → 자가 테스트로 확인.
  `TESMagicEffectApplyEvent` 는 쓰지 않는다: 이펙트 ID 만 있어서, 같은 이펙트를 쓰는 독 아닌 주문과 구별할 수
  없다(Codex 검토 2026-10-17). 오지 않는 종류가 있으면 시전 주문(MagicItem)이 함께 오는 경로
  (`MagicTarget::AddTarget` 의 `AddTargetData`, `MagicTarget.h:90-110`)를 훅하는 방안을 테스트 결과를 보고 결정하고,
  그 전까지는 README 의 알려진 한계로 적는다.

### 5.4 독약 효과 걸기 / 갱신

이벤트 싱크 안에서 바로 하지 않고 `SKSE::GetTaskInterface()->AddTask` 로 미룬다.
(대상, 독약) 짝마다 마지막으로 처리한 시각을 기억하고, 그 뒤 `minIntervalSeconds`(설정, 기본 1.0초, 사용자 결정)가
지나지 않은 피격은 버린다 — 갱신이든 새로 걸기든. 즉발 피해만 있는 독약(남는 효과가 없음)이 집중 주문으로
초당 여러 번 걸리는 것을 막는다. 기록은 세이브 로드 때 비운다.

세션 경계(Codex 검토 반영): 태스크는 대상과 플레이어를 포인터가 아니라 `ActorHandle` 로, 그리고 세대 번호를 들고
간다. 세대 번호는 `kPreLoadGame`·`kNewGame`·co-save Revert 때 올린다. 태스크는 실행 시 세대가 다르면 버리고,
능력 보유·설정·대상 생존·독약 보유를 다시 확인한다.

**우리가 건 효과만 갱신한다**(Codex 검토 반영): 무기에 바른 같은 독약의 효과(시전자 플레이어)를 늘리지 않도록,
우리가 `CastSpellImmediate` 로 건 직후 대상에 생긴 그 독약의 `ActiveEffect` 들을 (대상 핸들, 독약) 아래에 기억한다
(포인터는 대상의 활성 효과 목록에 아직 있는지 확인한 뒤에만 쓴다). 기록은 세이브에 남기지 않는다 — 로드 전부터
있던 효과는 남의 것으로 본다.
1. 우리 기록의 효과 중 아직 대상에 붙어 있는 것이 있으면 → 그 효과들의 경과 시간
   (`ActiveEffect::elapsedSeconds`)을 0 으로. 새로 걸지 않는다. (다시 거는 방식은 지속 피해가 처음부터
   다시 걸리거나 겹칠 수 있어서 쓰지 않는다.)
2. 우리 기록은 없는데 같은 독약 효과가 대상에 있으면(무기 독, 로드 전 효과) → 아무것도 하지 않는다. 그 효과가
   끝난 뒤의 피격부터 우리가 건다.
3. 둘 다 없으면 → `player->GetMagicCaster(kInstant)->CastSpellImmediate(poison, false, target, 1.0f, false, 0.0f, player)`.
   근거: QuickPocket 이 같은 호출로 플레이어 책임의 독을 대상에 건다(조사 보고). 독약의 전달 방식은 Self 지만
   대상을 명시하면 대상에 걸린다(ANDR_SKSEFunctions, Throwing-Stuff-VR, OIF 가 그렇게 씀).
- 효과가 여럿인 독약에서 일부만 끝났으면, 남은 것만 갱신하고 끝난 것은 다시 걸지 않는다. 전부 끝났을 때만
  2번으로 새로 건다(내 결정).
- 활성 효과 순회와 `elapsedSeconds` 의 정확한 멤버·접근 방법은 CommonLib 헤더(`ActiveEffect.h`, `MagicTarget.h`)에서
  확인할 것. 엔진이 `elapsedSeconds` 를 0 으로 되돌린 효과를 실제로 연장하는지는 자가 테스트로 확인.

### 5.5 아이콘

ESL 의 키워드 `HoMTox_MimicPoison` 을 5.2 에서 고른 독약의 기본 폼에 `BGSKeywordForm::AddKeyword` 로 붙이고,
전에 붙인 독약에서는 뗀다. 메모리에서만이고 세이브에 남지 않는다(세이브 로드 때 다시 붙인다).
- I4 는 메뉴가 목록을 그릴 때 항목마다 `keywords` 객체(SKSE 가 기본 폼의 키워드 editorID 로 채움)를 문자열로
  비교한다(Exit-9B/InventoryInjector `Property.cpp:71-78`, skse64 `ScaleformExtendedData.cpp:61-87`).
  그래서 실행 중에 붙인 키워드가 다음 메뉴 그리기에 반영된다.
- **I5(InventoryInjectorImproved, TAKEALOOK 에 설치됨)**: I4 결과를 FormID 로 캐시해서 키워드를 옮겨도
  인벤토리·상점 메뉴에 옛 아이콘이 남는다(`codepuncher/InventoryInjectorImproved` `src/CacheKey.h:27-39`).
  즐겨찾기 메뉴는 캐시하지 않는다. 대응: 캐시를 비우지 않는다(Task 4 조사, 2026-10-17). I5 의 `i5` 콘솔 명령은
  콘솔 명령 함수 안의 CompileAndRun 호출 한 곳을 패치해서 받으므로(`src/ConsoleHook.cpp`, `write_call` at
  `RelocationID(52065, 52952)` + 0xE2/0x52) 플러그인이 `RE::Script::CompileAndRun` 으로 돌린 명령은 I5 에 닿지 않는다.
  설치된 0.4.0 의 명령은 `i5 purge` 다(`i5 cache purge` 는 미배포 main 문법). I5 가 로드되어 있으면 로그에 한 번
  남기고, "I5 를 쓰면 인벤토리 아이콘이 늦게 바뀔 수 있다(콘솔 `i5 purge`), 즐겨찾기 메뉴는 정확"을 README 의
  알려진 한계로 적는다.
- 메뉴가 열린 채 즐겨찾기를 바꾸면 아이콘은 다음에 메뉴를 열 때 바뀐다(알려진 한계).

### 5.6 독약 소모

복제할 독약이 있는 동안 플레이어가 시전한 독 마법(5.1 집합)의 매지카 소모량을 쌓는다. 쌓인 양이
`magickaPerDose`(설정, 기본 100, 사용자 결정) 이상이 되면 그때 복제 중인 독약을 한 병 없애고 그만큼 뺀다.
나머지는 다음으로 넘어간다. 이하 내 결정(사용자에게 말함):
- 맞혔는지와 상관없이 시전으로 쓴 매지카를 센다.
- 양은 엔진이 매기는 시전 비용(퍽·장비 감소 뒤)으로 센다. 측정 계약(Codex 검토 반영, 순수 매지카 증감은
  재생·회복 물약·다른 손 주문이 섞이므로 쓰지 않는다):
  - 발사형(fire-and-forget): 플레이어의 `TESSpellCastEvent`(주문이 실제로 나간 때만 옴 → 충전하다 끊은 시전은
    엔진도 매지카를 안 받으므로 세지 않음)에서 `CalculateMagickaCost(player)`. 이중 시전이면 엔진의 이중 시전
    비용 배율을 곱한다(배율을 읽는 방법은 헤더·GMST 로 확인).
  - 집중: 프레임 갱신 훅에서, 왼손·오른손 캐스터가 독 마법을 집중 시전 중인 프레임마다
    `CalculateMagickaCost(player)` × 경과 시간(초)을 더한다. 매 프레임 다시 계산하므로 도중의 비용 변화도 반영.
    양손이면 각 손을 따로 센다.
  - 검증: 개발 로그에 "센 양 / 같은 구간의 실제 매지카 감소량"을 남긴다. 재생·물약이 없는 조건에서 5% 안이면 통과.
  - 알려진 한계(README): 엔진이 다른 경로로 비용을 바꾸는 모드(시전 비용을 스크립트로 깎는 퍽 등)는 반영 안 될 수 있다.
- 복제할 독약이 없을 때(즐겨찾기 독약 없음, `enabled` false) 쓴 매지카는 세지 않는다.
- 독약을 바꿔도 쌓인 양은 이어진다. 한 번에 여러 병 기준을 넘으면 그만큼 여러 병.
- 병을 없앤 뒤 그 독약이 0 병이 되면 5.2 를 다시 계산해 다음 독약으로 넘어간다(키워드도 옮김). 남은 게 없으면 멈춤.
- 쌓인 양은 SKSE co-save 에 저장한다(고유 ID 는 빌더가 정함). Revert 콜백에서 먼저 0 으로 되돌리고 그다음 레코드를
  읽는다 — 이 애드온의 레코드가 없는 세이브(설치 전 세이브, 다른 캐릭터)는 0 에서 시작한다. 새 게임은 0.
- 없애는 방법은 `RemoveItem`(조용히, 알림 없음). 플레이어가 만든 독약(0xFF 폼)도 같은 방식.
- `magickaPerDose` 가 0 이하이거나 숫자가 아니면 기본값 100.

## 6. 아이콘 그림

- 내용: 마법진(원과 룬 문자) 앞에 독약병 실루엣. 단색(흰색 채움 + 투명). I4 가 `iconColor` 로 색을 덮어쓰므로
  색은 의미 없다.
- 초안은 Gemini(agy `image-generator`)가 그리고, Claude 가 결과를 열어 보고 고칠 점을 요청한다. 작은 크기
  (목록 아이콘 약 20~30px)에서 병과 원이 구분되어야 한다.
- SWF: 기존 I4 아이콘 SWF 와 같은 구조 — CWS v10, 프레임 1 은 stop + placeholder, 아이콘 프레임은
  `FrameLabel` + `DefineShape`(흰 벡터) + `DefineSprite` 래퍼 + `PlaceObject2`. 벡터로 넣는다(비트맵 채움은 I4 선례가 불확실).
  제작 도구: FFDec 18.5.0 이 있다(`D:\TAKEALOOK\mods\Dynamic Interface Patcher - DIP\DIP\ffdec\ffdec.jar`, JDK 21) —
  `-importShapes` 로 SVG 를 넣을 수 있음을 확인했다. 래스터 → 벡터 변환 방법과 라벨 이름 쓰기는 빌더 판단.
  Python 은 쓰지 않는다(프로젝트 규칙). 만든 SWF 는 저장소에 바이너리로 넣는다.

## 7. 실패 처리

모두 게임을 멈추지 않고 `HeartOfMagic_Toxicology.log` 에 이유를 남긴다.

| 상황 | 동작 |
|---|---|
| 카탈로그 없음 / `version` 불일치 | 독 마법 집합 비어 있음 → 아무 일 없음 |
| ESL 꺼짐(능력·키워드 못 찾음) | 아무 일 없음 — 기능 전체가 ESL 의 능력에 묶여 있다(4a). 로그에 이유 |
| 설정 json 깨짐 / 없음, 값이 범위 밖 | 기본값(`enabled` true, `showIcon` true, `minIntervalSeconds` 1.0; 음수·NaN 은 기본값) |
| 즐겨찾기 독약 없음 | 아무 일 없음 |
| 대상이 죽었거나 사라짐(태스크 실행 시점) | 건너뜀 |

## 8. 코드 구조

워크트리는 서브모듈이 비어 있다 — 먼저 `git submodule update --init --recursive`.

`plugins/Toxicology/` (BookXP·NPC Spells 와 같은 CMake 구조, CommonLibSSE-NG, 파일당 600줄 한도).
- 순수 부분(게임 없이 도는 것): 카탈로그 JSON → 독 마법 키 목록, 독약 후보(이름, 즐겨찾기) 목록 → 고른 하나.
  오프라인 도구 `tools/toxicology-test --self-test` 로 확인(정상·경계·실패 각각).
- 순수 부분에 소모 계산도 넣는다: (쌓인 양, 이번 소모량, 기준) → (없앨 병 수, 남은 양).
- 게임 부분: 이벤트 싱크, 태스크, 활성 효과 갱신, 키워드 이동, I5 purge, 매지카 측정, 병 제거, co-save.

## 9. 테스트

- 오프라인 `toxicology-test --self-test`: 카탈로그 파싱(poison 있음/없음/버전 불일치/깨진 키), 이름순 선택
  (대소문자, 한글, 같은 이름, 빈 목록).
- 게임 안 자가 테스트(개발용 `SKSE/Plugins/HeartOfMagic_Toxicology_selftest.json` 이 있을 때만, 로드가 가라앉은 뒤
  약 30초에 시작, 결과는 로그에 PASS/FAIL):
  1. 독약 2개를 주고 즐겨찾기 → 이름이 앞인 것이 골라지고 그 폼에만 키워드.
  2. 대상 NPC 를 소환, 독 마법을 전달 방식별로(투사체, 접촉, 집중, 룬) 플레이어 시전으로 맞힘 → 피격 이벤트 수신
     여부, 독약 효과가 대상에 생겼는지.
  3. 다시 맞힘 → 같은 효과가 하나뿐이고 경과 시간이 0 근처로 돌아갔는지, 지속 시간이 실제로 연장되는지.
  4. 독약 효과의 크기를 독약 레코드 값과 비교해 로그(퍽 반영 여부 기록, 판정 없음).
  5. I5 가 있으면 `i5 cache purge` 실행 결과 로그.
  6. 효과의 시전자가 플레이어인지.
  7. 독 마법을 여러 번 시전 → 센 매지카 양이 플레이어 매지카 실제 감소량과 맞는지(집중 포함), 100 을 넘을 때
     독약이 정확히 한 병 줄었는지, 마지막 병을 쓰면 다음 독약으로 넘어가는지, 저장·불러오기 뒤 쌓인 양이 그대로인지.
  게임 실행·세이브 로드는 NPC Spells 와 같은 경로(mo2_guard -Mode Test, DevBench).
- 사용자 눈 확인(자동화 불가: 화면 모양): 즐겨찾기 메뉴와 인벤토리에서 아이콘이 알아볼 만한지, 색.
- 리뷰: 독립 리뷰어 에이전트 + OCR(프로젝트 규칙). Gemini 는 이미지 전담(2026-10-16 이후 메모리 규칙).

## 10. 문서

- `plugins/Toxicology/README.md`: 설치, 요구, 원작과 비호환, 동작, 설정, 알려진 한계(I5, 메뉴 열린 채 변경,
  NPC 시전 없음, 스캔 뒤 새 주문 모드는 다음 스캔부터).
- CLAUDE.md 문서 트리·Key Locations 한 줄, `docs/ARCHITECTURE.md` 애드온 한 줄.
- 넥서스 페이지 초안(영어), 추천 독 마법 모드: Venomancy(146585), Plague(62807), Poison Spells(25497),
  Mordant(145884). Venomancy 의 "독 지속 피해는 자기와 겹치지 않음" 과의 상호작용은 게임에서 확인.

## 11. 확인하지 못한 전제

- `TESHitEvent` 가 룬·폭발·망토·집중(얼마나 자주) 주문에서 오는지.
- `elapsedSeconds` 를 0 으로 하면 지속이 연장되는지.
- `CastSpellImmediate` 로 건 독약에 퍽이 적용되는지, 비적대 대상에 범죄·적대가 생기는지(독 마법 자체의 피격이 이미 만든다).
- ~~I5 캐시를 DLL 에서 비울 수 있는지.~~ 확인됨: 콘솔 경로로는 안 된다(5.5).
- 바닐라(SkyUI 없음) 즐겨찾기 메뉴의 정렬 순서.
- 설치된 I5 버전(meta 0.4.0, 로그 0.3.1).
- 매지카 소모량을 시전 시점에 정확히 얻는 방법(특히 집중 주문).

## 12. 외부 검토 반영 (Codex·Gemini, 2026-10-17)

받아들인 것(위 절에도 반영):
- 태스크는 `ActorHandle`·FormID 와 세대 번호만 들고 간다(5.4). Revert 에서 누적값 0·세대 증가·기록 비우기(5.6).
- 키워드를 붙인 독약은 포인터가 아니라 FormID 로 기억한다. `kPreLoadGame` 에 그 폼을 다시 찾아 키워드를 뗀다
  (정적 폼에 붙인 키워드는 로드를 넘어 메모리에 남는다). 0xFF(플레이어 제작) 폼은 찾지 못하면 그냥 잊는다.
- 갱신 대상은 우리가 건 효과만(5.4). `kInactive`·`kDispelled` 플래그가 켜진 효과는 없는 것으로 친다.
- `TESMagicEffectApplyEvent` 보조는 쓰지 않는다(5.3).
- 망토형 독 마법: 집합을 만들 때 그 주문 이펙트의 연관 주문(MGEF 의 associated item 이 SpellItem 인 것)도 넣는다.
  실제로 적에게 닿는 것은 그 하위 주문이다.
- 집중·폭발(룬)에서 `TESHitEvent` 가 안 오거나 `source` 가 주문이 아니면(자가 테스트로 판정), `MagicTarget::AddTarget`
  훅(`AddTargetData` 에 시전 MagicItem 이 있음)으로 바꾼다 — 계획 Task 5b(조건부).
- 매지카는 캐스터 자신의 비용(`MagicCaster::GetCurrentSpellCost`)을 쓴다 — 이중 시전 배율이 들어 있다(5.6, 계획 Task 6).
- (2026-10-17 외부 검토 2차 수정) 셈에는 캐스터의 `currentSpellCost` 멤버를 쓴다(발사형·집중 모두, 계획의 선택). `GetCurrentSpellCost` 는
  시전 구간마다 한 번 로그에 나란히 남기고, 게임 안 5초 비율 점검이 둘 중 무엇이 맞는지 정할 때까지 이대로 둔다.
- (같은 날) 망토는 집합을 둘로 나눈다: 피격 집합은 적대 카탈로그 주문 + 적대 하위 주문, 비용 집합은 적대 카탈로그 주문 + 하위 주문이
  적대인 망토 주문 자체. 하위 주문은 시전 비용을 세지 않는다.
- (같은 날) 발사형은 손 캐스터 읽기가 맞은 시전만 센다. 맞는 손이 없으면(인스턴트 캐스터·스크립트 시전) 세지 않는다(`CalculateMagickaCost`
  로 대신 계산하지 않는다).
- (같은 날, 최종 검토) 세이브 로드·새 게임 때 카탈로그는 파일 수정 시각이 마지막으로 읽은 것과 다를 때만 다시 읽는다(5초 대기 없음).
  로드 순서는 세션 중 바뀌지 않으므로 그 밖에는 다시 풀지 않는다(5.1 의 "세이브 로드 때 읽는다"를 이렇게 좁힘).
- (같은 날) 로드 뒤 인벤토리의 독약을 훑어 지금 복제하는 독약 말고는 키워드를 뗀다. 0xFF(플레이어 제작) 폼은 키워드째 세이브에 남기 때문.
  컨테이너 메뉴는 열릴 때와 닫힐 때 모두 고르기를 다시 한다.
- (같은 날) 자가 테스트에 상점 확인을 더한다(읽기만): Elgrim 상자 참조 `0x0A31BE`(Skyrim.esm)에 주문서가 1권 이상(0권이어도 플레이어가 능력이나 주문서를 가졌으면 이미 산
  세이브로 보고 INFO), 주문서가 능력을
  가르치는지, 값이 350 인지(4a).
- 병을 없앨 때 즐겨찾기(`ExtraHotkey`)가 붙지 않은 묶음부터 없앤다. 즐겨찾기 묶음만 남았을 때만 그것을 줄인다.
- 카탈로그는 피격 처리 때 최대 5초에 한 번 파일 수정 시각을 보고, 바뀌었으면 다시 읽는다(게임 안 재스캔 직후 반영).
- 독약 효과가 대상이 아니라 플레이어에게 걸리면(전달 방식 Self 때문에) 대상의 캐스터로 걸고 책임자만 플레이어로
  넘기는 방식으로 바꾼다 — 자가 테스트 2단계가 판정.
- `elapsedSeconds` 를 0 으로 되돌려도 지속이 늘지 않거나 감쇠·스크립트 효과가 깨지면, 우리 효과를 해제하고 다시
  거는 방식으로 바꾼다 — 자가 테스트 3단계가 판정.

받아들이지 않은 것:
- "SkyUI 와 정렬이 어긋난다": AS2 `CASEINSENSITIVE` 는 A-Z 를 소문자로 바꿔 비교하고, 우리도 소문자로 바꾼다(5.2) — 같다.
  동점 규칙도 이미 있다(FormID 작은 쪽).
- "매지카 0 이 되는 감면 장비로 무한 복제": 사용자가 정한 소모 규칙(쓴 매지카 비례)의 결과라 그대로 둔다.

README 의 알려진 한계로 적는 것:
- 이미 Elgrim's Elixirs 와 거래한 세이브에서는 상인 재고가 바뀔 때(약 48시간)까지 주문서가 안 보일 수 있다.
- 능력은 마법 메뉴의 주문 목록이 아니라 "활성 효과" 에만 보인다(주문서 글에 적는다).
- 즉발 효과 + 긴 지속 효과가 섞인 독약은, 지속 효과가 남아 있는 동안 갱신만 되므로 즉발 피해는 적용 한 번에 한 번이다
  (사용자 규칙 "맞으면 지속 시간만 갱신" 의 결과).
