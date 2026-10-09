# 애드온 후보 조사 (2026-10-06)

하트 오브 매직의 스캔 정보(사서 카탈로그: 학파·티어·원소·기법 태그, XP·마스터)를 쓰는 애드온 후보 6개를
인기 참고 모드와 비교해 조사했다. 근거는 TAKEALOOK 에 설치된 모드 파일, houseCARL 레코드 읽기, Nexus API,
GitHub 소스. 인게임 테스트는 하나도 하지 않았다. 세부 보고서는 같은 폴더의 `1-*.md` ~ `6-*.md`
(2번은 하위 에이전트가 파일을 남기지 않아 이 문서의 요약이 전부다).

## 한눈에

| # | 후보 | 판정 | 형태 | 한 줄 이유 |
|---|---|---|---|---|
| 1 | I4 아이콘 | **할 만함** | 설정 파일 생성 | 팩별 아이콘 애드온 약 24개가 설치돼 있음. 카탈로그로 I4 JSON 을 만들면 모든 주문·주문서에 원소 아이콘 |
| 2 | NPC 주문 배분 | **할 만함 (가장 큼)** | SPID ini 생성 또는 메모리 적용 | 팩마다 주문 FormID 를 손으로 나열(Mysticism 124줄, Abyss 96줄). 범용 자동 배분 모드는 못 찾음 |
| 3 | NPC 반응 | **할 일 없음** | — | 바닐라 키워드만 검사하고, vanilla.json 어댑터가 그 키워드를 이미 다 붙임 |
| 4 | VFX 교체 | **가능, 에셋이 관건** | DLL (kDataLoaded 포인터 교체) | 22개 플러그인의 이펙트 89개가 냉기 손 아트를 빌려 씀(대지·바람·독 주문 포함). 물·대지·바람·피·그림자는 바닐라 아트가 없음 |
| 5 | 두루마리·지팡이 | **연동 정도** | 설정 생성 또는 포크 | Staff Enchanting Plus 가 이미 아무 주문으로 지팡이를 만듦. 우리 몫은 "마스터한 주문만" 조건뿐 |
| 6 | NPC Spell Variance | **할 만함, 단 시점 문제** | KID ini 생성 | 키워드가 AI 의 전부. 메모리 어댑터는 NSV 가 읽은 뒤에 붙어서 안 먹힘(로그로 확인) |

## 1. I4 아이콘

- I4 는 `SKSE/Plugins/InventoryInjector/<플러그인>.json` 을 그 이름의 플러그인이 로드돼 있을 때 읽는다.
  규칙은 `{match, assign}`. match 에 `formType`(Spell, Book…), `formId`, `keywords`, `effectKeywords`,
  `school`, `resistance`, 주문서용 `teachesSpell` 이 있다. 마법 메뉴의 주문과 주문서 둘 다 된다.
- 팩별 애드온은 대부분 FormID 를 손으로 나열한다(Wish Magic). Abyss 는 자기 이펙트 키워드 + 자기 swf 아이콘.
- 기본 아이콘(BOOBIES): 불·얼음·번개(저항으로 매칭), 태양, 바람, 물, 대지, 흡혈, 학파별 주문서. 신성·그림자·
  사령·소환·결계·클록 아이콘은 없다.
- 길 두 개:
  - (a) 키워드: `MAG_MagicDamageSun/Poison` 은 이미 우리 어댑터가 붙이고 I4 가 읽는다. 바람·물·대지·피는
    `IconMagicWind/Water/Earth/Blood` 키워드가 필요한데 정의된 곳을 못 찾았다. 주문서는 이 길로 안 된다.
  - (b) **카탈로그로 I4 JSON 생성**: 주문은 formId, 주문서는 teachesSpell 로 전부 덮는다. 다음 실행부터 적용,
    같은 이름 플러그인이 필요하다(우리 ESP 가 있으면 됨).
- 미확인: 설정 로드 시점, 키워드 목록이 AND 인지 OR 인지, 인게임 동작.

### 실측: 아이콘 지정 없이 모드 주문에 원소 아이콘이 붙나 (2026-10-06)

- 불·얼음·번개는 I4 기본 `Skyrim.json` 규칙이 `resistance` 로 붙인다. 그 값은 메뉴 항목의 `magicType` 을
  그대로 옮긴 것이다(I4 소스 `src/Hooks/IconSetter.cpp` 186행). 바닐라가 `magicType` 을 가장 비싼 이펙트의
  저항값으로 채운다는 건 엔진 지식이고 확인하지 않았다.
- TAKEALOOK 의 `spell_scan_output.json` + `spell_catalog.json` 으로 셌다(모드 주문 3,289개, 소환 제외):
  불·얼음·번개 태그 980건 중 **728건(74%)은 자동으로 붙고 252건은 안 붙는다.** 이유: 가장 비싼 이펙트가
  스크립트형(저항 None) 132건, 다원소 무속성(MagicResist, 예: CC 원소 폭발) 71건, 다른 원소 저항 49건.
  플러그인별로는 Vigilant 51, Desecration 25, Flames of Coldharbour 25, Glenmoril 17.
- 나머지 원소(모드 주문 수): 독 229·태양 105 는 `MAG_` 키워드로 붙는다(우리 어댑터가 붙임, 메뉴에서 읽으니
  시점 문제 없음 — 미확인). 바람 56·물 41·대지 66·피 137 은 `IconMagic*` 키워드를 아무도 정의하지 않아 안 붙는다.
  신성 89·그림자 96·사령 116·비전 179 는 I4 에 아이콘 자체가 없다.
- 주문서: BOOBIES 가 바닐라 주문서를 EditorID 로(KID), **모드 주문서 774개(17개 플러그인: Arcanum 205,
  Mysticism 188, Natura 121, Darkstorm 38 …)를 FormID 로** 손 지정한다(`BOOBIES_ImmersiveIcons.json`).
  처음에 "모드 주문서 1,412개 전부 없음"이라 쓴 것은 틀렸다. 1,412 는 주문서로 배우는 모드 주문 수이고,
  주문서 FormID 는 스캔에 없어 정확히 몇 권이 비는지는 아직 못 셌다.

### 팩별 애드온과 겹칠 때 (Abyss - Custom I4 Icons, I4 소스)

- Abyss 애드온 = KID ini(주문서 32·두루마리 27·주문 EditorID 를 손으로 나열해 `ADAR_*` 키워드 부착) +
  `Abyss.json` 20규칙(그 키워드 → 자체 swf 의 Cloak·Conjuration·DamageShadow 등 기능별 아이콘, 색 `#4c4cff`)
  + Wheeler 용 SVG. 원소 하나(그림자) 안에서 기능별로 아이콘을 나눈다 — 우리 원소 아이콘보다 세밀하다.
- I4 는 kDataLoaded 에 **로드 오더 순서대로** 플러그인마다 `<플러그인>.json` 을 읽어 규칙을 한 줄로 쌓는다
  (`CustomDataManager::LoadConfigs`). 메뉴 항목마다 **맞는 규칙을 전부 차례로 적용**하고, 각 규칙은
  `icon*` 값을 덮어쓴다(`Rule::SetIcon`). 즉 **나중에 로드된 플러그인의 규칙이 이긴다.**
- 하트 오브 매직은 자기 ESP 가 없다(배포 ESP 는 DEST 호환용 `DontEatSpellTomes.esp`·`DEST_ISL.esp` 뿐).
  I4 설정을 내려면 이름을 걸 플러그인이 필요하다.

## 2. NPC 주문 배분

- SPID 한 줄 = `Spell = form~plugin | 문자열 | 폼필터 | 스킬·레벨 | 특성 | 개수 | 확률`. 참고 모드들은 직업
  (CombatMageConjurer, Thalmor 마법사), 세력(Necromancer, Vampire, Bandit, Hagraven), 학파 스킬 문턱
  (견습 50~90, 마스터 100), 확률(20%, 마스터 5%)로 나눈다.
- 손 작업: 주문마다 FormID 를 직접 쓴다. NSV AIO 의 KID 파일만 1,254줄.
- 설계: 학파 → 스킬 인덱스, 티어 → 스킬 문턱·확률, 원소·기법 → 세력·직업 목록. 클록·집중·룬·유틸리티는
  빼고, 스크립트 주문·크래시 주문은 거부 목록. 이미 설치된 `_DISTR.ini` 를 읽어 겹치는 주문은 건너뛴다.
  ini 로 내면 새 모드가 한 번 늦게 반영되고, 메모리 적용이면 그 지연이 없다.
- 범용 자동 배분 모드는 못 찾았다(NSV Spell Variety AI 는 주문을 나눠 주지 않고, NPCs Learn Skills and Spells 는
  바닐라 주문만).
- 미확인: EEOS·ODIN·Apocalypse 의 ini 원문, SPID 에서 같은 주문 중복 시 동작, 인게임.

## 3. NPC 반응

- 사령술: 대화 212개가 `HasMagicEffectKeyword`(MagicSummonUndead/Fire/Frost/Shock/Familiar)로 판정.
  투명화: MagicInvisibility. 광란: MagicInfluenceFrenzy. 셋 다 vanilla.json 이 붙이는 키워드다.
- 치유 반응은 바닐라 치유 이펙트에 붙은 스크립트라 키워드로는 못 고친다.
- 결론: 이미 된다. 선택 사항으로 `WISpellDangerous` 한 줄 정도.

## 4. VFX 교체

- 설치된 VFX Edit 모드는 전부 바닐라 NIF 경로를 덮어쓰는 교체 파일이다. 새로 가리킬 에셋은 없다.
- 바꿀 수 있는 필드(CommonLibSSE-NG `EffectSetting.h` 의 `EffectSettingData`): `castingArt`, `hitEffectArt`,
  `enchantEffectArt`, `effectShader`, `enchantShader`, `projectileBase`, `explosion`, `light`,
  `impactDataSet`, `dualCastData`, 소리는 `effectSounds`.
- **팩들이 실제로 남의 원소 아트를 빌려 쓴다(레코드로 확인)**: 22개 플러그인의 이펙트 89개가 `FrostHandFX01`
  을 시전 아트로 쓴다. 예: KittySpellPack02 Diamond Splash(대지), Glenmoril 바람·혼돈·공허·햇빛·달빛 커터,
  Siege at Icemoth 역병 숨결(독).
- 바닐라 아트가 있는 원소: 불, 얼음, 번개, 빛(신성은 약함), 치유, 흡수(피 대용), 독, 소환.
  없는 원소: 물, 대지, 바람, 피, 그림자 → 커스텀 에셋 필요.
- 주문 아트를 규칙으로 바꾸는 프레임워크는 못 찾았다(Enchantment Art Extender 는 무기 인챈트만).
- 위험: 여러 주문이 공유하는 이펙트·투사체는 모든 주문이 같은 태그일 때만 바꾼다. SkyPatcher 등이 같은
  필드를 덮을 수 있다. Kittytail 에셋 재배포는 허가 필요.

## 5. 두루마리·지팡이

- Sorcerer 는 런타임 생성 없이 미리 만든 레코드(Mysticism·Praedystaves)를 쓰고, 주문 모드 지원은 Synthesis
  패처로 만든다. 팩별 Praedystaves 패치는 지팡이 레코드 묶음(Natura 48개).
- Staff Enchanting Plus(GitHub Exit-9B/StaffEnchanting): SKSE DLL. 아는 주문 아무거나로 지팡이를 만들고
  바닐라 `BGSCreatedObjectManager::AddWeaponEnchantment` 를 써서 엔진이 저장한다. 지팡이만, 두루마리는 없음.
- 우리 몫: "마스터한 주문만". 하트 오브 매직에서는 약화된 사본도 "아는 주문"이라 차이가 난다.
  가장 싼 연동은 마스터하지 않은 주문으로 SEP 의 `exclusions` JSON 을 만드는 것(재시작 전까지 갱신 안 됨).
- 두루마리는 런타임 폼이 저장되지 않아(엔진은 인챈트·포션만 저장) 자체 코세이브가 필요하다. 엔진 지식에
  근거한 판단이고 코드로 확인하지는 않았다.

## 6. NPC Spell Variance

- NSV 는 `NSV_Magic_Damage/Heal/Cloak/Ward/Summon/Trap/...` 키워드가 붙은 주문만 AI 에 쓴다. 키워드가 없으면
  건너뛴다. 팩별 패치는 KID 줄 `Keyword = NSV_Magic_Damage|Spell|0x810~Arcane.esp` 로 **주문 레코드**에 붙인다.
- 카탈로그 매핑(1,206줄 중 1,204줄 조인): 소환(`summoning`, 정탐 144 / 오탐 4), 무기, 트랩, 결계, 클록,
  투명화, 공포, 갑옷은 깔끔. 치유·진정·흡수·피해는 판단 필요. Tag_Undead·Ally·Enemy·Buff·Debuff 는 도출 불가.
- **시점 문제 (로그로 확인)**: 같은 실행(pid 84908)에서 NSV 는 21:50:16.934 에 "KID finished" 를 받고 AI 를
  만들었고, 하트 오브 매직 어댑터는 21:51:19 에야 키워드를 붙였다(`SpellLearning.log` 92행). 그래서 메모리
  어댑터로는 NSV 에 안 먹힌다. 대안: 카탈로그로 `HeartOfMagic_NSV_KID.ini` 를 생성해 KID 가 읽게 한다
  (새 모드는 다음 실행에 반영).
- 미확인: NSV 가 이펙트 키워드도 보는지(`HasKeyword` 동작), GitHub 소스와 설치판의 일치 여부(보고서는 2.7.2 로
  읽었는데 설치된 DLL 로그 머리말은 `NPCSpellVariance v1-0-1-0` — 버전 표기 차이일 수 있으나 "키워드 없으면
  건너뛴다"의 근거가 그 소스라 확인 필요).

## 공통 결론

- 1·2·6 은 같은 모양이다: **카탈로그 → 다른 프레임워크의 설정 파일 생성기**(I4 JSON, SPID ini, KID ini).
  DLL 훅 없이 데이터만 내보내면 된다. 생성 시점(전체 스캔 직후)과 "다음 실행부터 적용"이 공통 제약.
- 3 은 이미 해결, 5 는 연동 수준, 4 는 유일하게 새 DLL 과 에셋이 필요한 진짜 애드온.
