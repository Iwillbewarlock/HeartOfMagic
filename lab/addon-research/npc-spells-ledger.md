# NPC 주문 배분 애드온 - 검증 장부

규칙: 가설은 확인된 뒤에만 "확인됨"으로 옮긴다. 틀린 가설은 지우지 않고 ~~취소선~~과 이유를 남긴다.
설계서: docs/superpowers/specs/2026-10-07-npc-spells-addon-design.md (§9 확인할 것, §10·§11 실측)

## 확인됨

- 바닐라 NPC 주문 목록에도 LVSP 가 들어 있다. Skyrim.esm 에 LVSP 24개, 이를 쓰는 NPC_ 92개. `LSpellWEBattlemageFireLeftHand` = 레벨 1 불씨조각 / 5 화염 화살 / 20 화염구 (houseCARL, 2026-10-07)
- TAKEALOOK NPC_ 26,087개 중 PcLevelMult 4,296개, 그중 SpellList 를 템플릿에서 받지 않는 것 3,303개, 그중 `CalcMaxLevel = 0` 461개 (houseCARL counts_only)
- CommonLib `TESSpellList::SpellData::AddSpell/AddLevSpell` 은 게임 할당자(`RE::calloc`/`free`)로 배열을 새로 만들고 change flag 를 건드리지 않는다 (`src/RE/T/TESSpellList.cpp`). `TESNPC::ChangeFlags::kSpellList` (1<<4) 가 따로 있지만 Add 함수는 이를 켜지 않는다 -> 우리가 넣은 것은 세이브의 베이스 변경으로 기록되지 않는다 (엔진이 다른 이유로 켜는 경우는 미확인)
- `TESLeveledList`: `entries` 는 `SimpleArray<LEVELED_OBJECT>` (게임 할당자로 resize), `numEntries` 는 `uint8` (목록당 최대 255개), `LEVELED_OBJECT` = form, count(u16), level(u16), itemExtra (`include/RE/T/TESLeveledList.h:15-68`)
- ~~`CalculateCurrentFormList(level, count, out, arg5, usePlayerLevel)` 의 마지막 인자로 호출하는 쪽이 플레이어 레벨을 쓸지 고른다 (`TESLeveledList.h:62`)~~ -> 디스어셈블 2차: NPC 주문 목록의 LVSP 는 이 함수를 거치지 않는다(17577/17582 경로, 학파 스킬 기준). 인자 자체는 헤더에 있으나 이 애드온과 무관
- `IFormFactory::GetConcreteFormFactoryByType<T>()` 는 FORMTYPE 이 있는 모든 폼에 쓰는 일반 템플릿이고 `TESLevSpell::FORMTYPE = LeveledSpell` 이 있다 -> 런타임에 LVSP 폼을 만드는 호출은 컴파일된다 (실제 동작은 미확인)
- CommonLib 에 액터 없이 자동 계산 스킬/매지카를 구하는 함수는 없다 (헤더 검색)
- 트리 형식: `spell_tree.json` 노드마다 `formId`(런타임, 로드 오더에 따라 바뀜)와 `persistentId`(`Plugin.esp|0xLocalID`)가 있다. TAKEALOOK 트리 노드 1,452개 전부 `persistentId` 가 있고 전부 `spell_catalog.json` 의 키와 일치 -> 애드온은 `persistentId` 로 읽는다 (PowerShell ConvertFrom-Json, 2026-10-08)
- 비용: 카탈로그·스캔의 비용은 `CalculateMagickaCost(nullptr)` = 시전자 없는 기본 비용 (`SpellScannerScan.cpp:486`, `SpellScannerJson.cpp:267`). 내부는 `MagicItem::CalculateCost` RELOCATION_ID(11213, 11321) 하나 -> 레코드에서 비용을 읽을 때 같은 호출을 쓰면 되고, NPC 스킬 감소는 따로 계산해야 한다
- SPID: 레벨 조건 항목은 `ProcessLists::HandlePlayerLevelUpdate` 후크로 플레이어 레벨이 오를 때 다시 평가하고, 세이브마다 캐시로 복원 (`DistributePCLevelMult.cpp`, 에이전트가 소스 읽음)

- **직업 문턱 (houseCARL CLAS 287개, 에이전트 덤프 `scratchpad/clas.jsonl`):** 설계서의 0.5 / 0.2 는 틀림(Witchblade .545 가 마법사, Scout .273 이 혼합이 됨). **0.6 / 0.3 (이상)** 이 바닐라 19개 전투 직업을 표대로 나눈다: 마법사 최저 Sorcerer .636, 혼합 최고 Witchblade .545(바닐라), 혼합 최저 Monk .364, 비마법사 최고 Scout .273. 예외: USSEP 가 Witchblade 를 .636 으로 바꿔 이 로드 오더에서는 마법사(내가 tree 로 USSEP 변경 확인). 경계에 딱 걸리는 직업(.600, .300)이 있어 정수 비교(마법 합 × 10 ≥ 전체 × 6). 분포: 마법사 48 · 혼합 69 · 비마법사 170
- **소환 직업:** 소환이 단독 1위인 것은 TrainerConjurationMaster 하나. CombatMageNecro·CombatMageConjurer 는 소환 3 = 회복 3 동점 -> 규칙은 "소환 ≥ 3 이고 다른 학파보다 작지 않음(동점 허용)". EncClassThalmorWizard 도 걸리지만 탈모어 그룹 never `undead` 로 빠짐
- **세력 FormID:** Forsworn 043599 (534명), Hagraven 04359E (149), Vampire 027242 (234) + DLC1Vampire 003376:Dawnguard (54), Necromancer 034B74 (447), Thalmor 039F26 (185) + WEThalmor 0B80F7, MGThalmor 0135A0, Vigilant 0B3292 (76), College 01F259 (55), JobCourtWizard 05091E (24). 수는 NPC 가 직접 가진 세력만(템플릿 상속 제외)
- **템플릿:** LvlBanditWizard 계열·LvlWarlockNecromancer·LvlForswornShaman 6개 모두 직업·스탯·세력·주문 목록을 레벨드 목록이 고른 끝 레코드에서 받는다. 끝 레코드도 다시 템플릿을 쓸 수 있다(034F7F 는 직업은 자기 것, 주문 목록·스탯은 034F58). 주문은 자기 SpellList 를 실제로 쓰는 레코드(034F58)에 넣는다
- **마법사 단계(0.6) + 자기 주문 목록 NPC 1,401명:** 자동 계산 + PcLevelMult 529 (상한 없음 53), 자동 계산 + 고정 레벨 843, 고정 스탯 29
- **SPID:** 켜진 모드의 `Spell =` 431줄(설계서 423 과 차이, 원인 미확인). 주소 형태 `0xFormID~Plugin` 394, EditorID 37, 그 밖 0. LVSP 를 주는 줄 0. 레벨·스킬 필터가 있는 줄 321 (스킬 `14(20)` 299, 레벨 `10/60` 22)

- **정적 분석은 막혔다 (2026-10-08):** TAKEALOOK `SkyrimSE.exe` 1.6.1170 은 SteamStub 로 `.text` 가 암호화돼 있다(진입점이 `.bind` 섹션, 함수 시작 바이트가 의미 없음, 호출 0건). 복호화된 사본·Steamless 는 PC 에 없고, 크래시 덤프에는 `.text` 가 없다. 주소만 확인: `CalculateCurrentFormList` ID 14751 = RVA 1E3A30, `GetCalcLevel` 20205 = 2FA590, `CombatInventoryItemMagic::GetResource`(vtable 슬롯 0x10) ID 45077 = 8199B0 (0x1D 바이트). 설정 객체 `fCombatInventoryResource{Current,Desired}RequiredMult` = ID 372484 / 372481, 값 1.0 / 2.0. CommonLib 은 `versionlib-1-6-1170-0.bin` 을 읽는다. 도구·노트: `scratchpad/disasm/`
- ~~H1·H2·H3·H4 를 게임 밖 디스어셈블로 확인한다~~ -> SteamStub 때문에 불가. 남은 길: 게임 실행 중 전체 메모리 덤프(.text 복호화된 상태)로 다시 분석, 또는 인게임 시험

- **복호화된 코드 확보 (2026-10-08):** 실행 중인 게임(PID 7532)에서 주 모듈을 ReadProcessMemory 로 복사 -> `scratchpad/disasm/SkyrimSE_1170_image.bin` (메모리 배치, 오프셋 = RVA, 0x3870000 바이트, 읽기 실패 0, 로드 베이스 0x7FF7C1ED0000). RVA 1E3A30 이 `66 45 85 C0 0F 84 …` 로 정상 코드 -> 디스어셈블 재개

- **[디스어셈블 2차, 2026-10-08] LVSP 는 레벨이 아니라 그 학파 스킬로 풀린다.** 경로 `Actor::VisitSpells`(38781) -> 39443 `6EAF20` -> 17577 `26D570` -> 17576 -> 17582 `26D860`. `CalculateCurrentFormList`(14751)는 쓰지 않는다. 17577 이 학파 0~4 를 돌며 `14E370`(학파 번호 -> AV 0x12~0x16, **내가 바이트로 확인**: 0->12, 1->13, 2->14, 3->15, 4->16) 으로 스킬을 정하고 `ActorValueOwner(+0xB8)->GetActorValue`(현재값, 강화 포함)를 정수로 바꿔 "레벨"로 넘긴다(**내가 `fn/26D570.asm` 으로 확인**). 항목 level ≤ 스킬 중 가장 높은 묶음에서 하나(플래그 1 이면 이하 전부, 4 면 전부). **학파가 다른 항목을 만나면 루프가 끝난다** -> LVSP 하나는 한 학파만 담아야 한다. 항목 정렬 전제는 미확인
- **LVSP 결과는 `HighProcessData+0x398` 캐시**에 들어간다. 없을 때만 만들고, HighProcessData 소멸자(39727)가 지운다. `addedSpells` 가 아니다 -> 세이브에 남지 않고 액터가 하이 프로세스에 다시 들어올 때 새로 뽑힐 것(정황, AIProcess 저장 코드는 안 읽음). Ability 는 빠지고 Disease 는 인자에 따라
- **전투 AI 비용 = 스킬로 줄어든 비용:** 항목 생성 44898 이 45323 -> `MagicItem::CalculateCost(item, NPC 자신)` 로 비용을 구해 `+0x40`(CommonLib 이름 `currentAV`)에 저장. **최대 매지카(영구값) < 비용이면 그 주문은 전투 목록에 아예 안 들어간다.** 고를 때(44868 `80FCD0`): 비용 × 1.0 < 지금 매지카, 또는 비용 < 3.0(RegenTime) × 재생량이면 채택하고 예산에서 뺀다. Desired 2.0 도 같은 식으로 쓰이는 것은 정황
- ~~H1 (액터의 계산된 레벨로 푼다)~~ -> 틀림, 학파 스킬로 푼다. ~~H2 (세이브에 남는다)~~ -> 캐시라 남지 않음(정황). H3 은 수정해서 확인: 지금 매지카 또는 3초 재생량. H4 확인: 스킬 감소 비용

- 바닐라 대조: `LSpellWalls`·`LSpellDestructionArea` 는 항목이 모두 레벨 1(같은 티어 안에서 무작위), `LSpellDragonFrostBreath` 는 1/16/23/31/40. 스킬 기준을 뒷받침하지도 반박하지도 않음
- 엔진 학파 = `MagicItem::GetAssociatedSkill()` vtable 0x66 (= 디스어셈블의 `[rdx+330h]`). HoM 스캔의 학파는 첫 효과의 `GetMagickSkill()` (`SpellScannerJson.cpp:163`) 이라 다를 수 있다 -> 애드온은 엔진 함수로 묶는다. 트리 학파와 스캔 학파는 1,452개 모두 일치

## 가설 (확인 전)

- H1: 엔진은 NPC 주문 목록의 LVSP 를 액터가 생길 때 그 액터의 계산된 레벨로 풀고, 레벨 이하 가장 높은 항목 하나를 준다 -> 디스어셈블 진행 중
- H2: 풀린 주문이 액터의 런타임 목록에 들어가 세이브에 남는다 / 또는 로드마다 다시 푼다 -> 디스어셈블 진행 중
- H3: 전투 AI 는 지금 매지카 < 비용 x `fCombatInventoryResourceCurrentRequiredMult`(1.0) 이면 그 주문을 고르지 않는다. 최대 매지카는 보지 않는다 -> 디스어셈블 진행 중 (근거: 설정 이름, `GetResource` 인터페이스, 플레이 관찰)
- H4: AI 가 보는 비용은 스킬로 줄어든 비용(`CalculateMagickaCost(Actor*)`) -> 미확인
- ~~제미나이 답의 "스폰 때 한 번 풀리고 세이브에 남는다"~~ -> 디스어셈블과 반대(하이 프로세스 캐시, 학파 스킬 기준). 원문: 제미나이(gemini-3.1-pro-high) 답 (근거로 쓰지 않고 실마리로만, `scratchpad/gemini-lvsp.txt`): LVSP 는 액터가 처음 스폰될 때 그 액터의 계산된 레벨로 한 번 풀리고 세이브에 남으며, 리스폰 전에는 다시 뽑지 않는다(H1·H2 와 같은 방향). `DesiredRequiredMult` 2.0 은 최대 매지카가 비용의 2배 이상이어야 "쓸 만한 주문"으로 본다는 뜻이라고 함(출처 없음). 런타임에 만든 0xFF 폼을 TESNPC 에 넣으면 세이브 오염 위험이 있다고 경고 -> 대안: 빈 LVSP 레코드를 고정 FormID 로 담은 작은 ESL 을 함께 배포하고, 항목만 런타임에 채운다
- H5: kDataLoaded 에 런타임으로 만든 LVSP 를 TESNPC 에 넣어도 안전하다 (폼 ID 할당, 세이브에 폼이 기록되는지) -> 미확인

## 인게임 1차 (2026-10-08 19:13, TAKEALOOK, dev-a49e971)
- 확인됨: ESL 레코드 921개 모두 LVSP 로 로드(found=921, missing=0, wrongType=0).
- 확인됨: **엔진은 LLCT 0 인 LVSP 에도 entries 포인터를 null 이 아닌 값으로 둔다**(numEntries=0). 그래서 안전장치 `entries.data() != nullptr` 가 338개 목록을 모두 건너뜀 -> 배분 0, NPC 변경 없음, 튕김 없음. 그 포인터가 SimpleArray 머리를 가진 힙 블록인지는 미확인 -> 해제하지 않고 새 블록으로 바꾸고 옛 것은 둔다(누수 338 x 몇 바이트).
- 확인됨: 입력 읽기 2847 ms (SPID 설정 169개 + 트리·카탈로그), pool 99 ms, 목록 5 ms. 목표 500 ms 초과.
- 확인됨: excludedDistr=146 (오프라인 하네스는 0, _DISTR 를 안 읽으므로), kept 1159.

## 인게임 2차 (2026-10-08 21:30, dev-cd75d74)
- 확인됨: 머리 없는 새 항목 배열로 338개 목록 채움, skippedPrefilled=0. NPC 26,093 중 eligible 6,964, attached 2,468, 붙인 목록 19,823. tier 마법사/혼합/비마법사 1367/872/229, 그룹 None/Forsworn/Vampire/Necro/Thalmor/Vigilant 2226/99/46/55/35/7. 건너뜀: template spell list 14211, creature not a mage 2629, beast 1829, face preset 230, redguard non-mage 226, deleted 3.
- 확인됨: 시간 3132 ms 중 SPID config search 2926 ms (Data 의 169 파일, USVFS 루트 열거). 나머지 단계 합 ~200 ms. 본체 TomeReach 도 같은 FindDistributionConfigs 를 씀 -> 같은 비용 추정(미측정).
- 미확인: 실제 NPC 가 그 주문을 받아 쓰는지(콘솔·전투), 세이브 재로드.

## 인게임 4차 (2026-10-09 09:57, dev-116a93f)
- 확인됨: 총 299 ms (Data 최상위 102 ms, SKSE/Plugins 건너뜀). 배분 동일.

## 인게임 5차 (2026-10-09, 콘솔)
- 확인됨(사용자): `player.placeatme 01E79F`(LvlBanditWizard) 로 만든 산적 마법사의 MIC 주문 19개에 HoMNS_ 목록 이름 없음, 원래 템플릿 주문(OREO_EncBandit10TemplateMagic: 피부·방어막·치유 3·Magicka Recovery·LSpellBandit05/07 FireFrostShock) 밖의 이름 없음(번개조각=바닐라 Sparks).
- 가설 H1: 레벨드 NPC 가 고른 끝 레코드에 우리 목록이 없거나, 임시 베이스 복사에 안 들어감 -> 우리 목록 12개가 붙은 AA034F58 을 직접 소환해 확인 중
- 가설 H2: 엔진이 우리 LVSP 를 풀지 못함(레벨·플래그·항목 배열) -> H1 결과로 갈림
- 가설 H3: MIC 가 레벨드 목록에서 나온 주문을 보여 주지 않음 -> 바닐라 LSpellBandit 주문이 보이므로 가능성 낮음
- 확인됨(사용자, 2026-10-09): `player.placeatme AA034F58`(OREO_EncBandit10TemplateMagic, 직접 목록 12개) 의 주문에 모드 주문(산의 손가락, 혈천) 있음 -> 우리 LVSP 가 엔진에서 풀린다. H2·H3 기각 방향
- 확인됨: 낮은 레벨 산적 마법사 템플릿(039D31/039D4A/039D58:Skyrim.esm 등)도 보고서상 목록을 받음
- 가설 H4 (H1 구체화): 템플릿에서 SpellList 를 물려받는 NPC(예: 034F7F)는 로딩 중 템플릿 주문 목록을 미리 복사해 둬서 kDataLoaded 에 템플릿에 붙인 목록이 안 퍼진다 -> AA034F7F 소환으로 확인 중. 맞으면 상속 NPC(14,211)에도 제공자의 목록을 직접 붙인다

## 인게임 6차 - 자가 테스트 (2026-10-15 20:02, TAKEALOOK, dev e432729, 1.6.1170)

DevBench `game loadLast` 로 최신 세이브(Save3, 화이트런 홀드, 레벨 1)를 불러온 뒤 플러그인 자가 테스트가 돌았다. 로그 원문: `Documents/My Games/Skyrim Special Edition/SKSE/HeartOfMagic_NPCSpells.log` (같은 실행의 SELFTEST 줄).

확인됨:
- 목록을 직접 가진 NPC 는 엔진이 우리 목록을 푼다. 파렌가 013BBB PASS (괴저 불씨, 불구 면역, 신성한 방패), 강령마법사 0551AD PASS (냉기 껍질, 감옥장 위글 소환, 새하얀 화염화살, 신성한 정화, 태양빛 방패). 레벨은 학파 스킬 기준 (파렌가 파괴 34 -> 레벨 25 그룹까지).
- **H4 확인 (출시 차단)**: 템플릿에서 주문 목록을 물려받는 NPC 는 우리가 템플릿에 붙인 목록을 받지 못한다. 039D37 (OREO_EncBandit01MagicNordM): actorBase = 039D37 자신, copiedTemplate=yes, holder 039D31 의 HoMNS 목록 9개, actorBase 의 HoMNS 목록 0개, 캐시 1개(바닐라 목록의 불씨조각). 로드 중 kCopiedTemplate 로 템플릿 주문 목록이 자식에 복사되고, 자식은 따로 배열을 가진다(포인터 공유 아님 -> 자식에 직접 붙여도 안전할 것으로 보임).
- LVLN 039D5F 는 `TESObjectREFR::PlaceObjectAtMe` 로 소환되지 않는다 ("PlaceObjectAtMe gave no actor"). 레벨드 소환 경로는 아직 미검사.

가설 (확인 전):
- H7: 일부 학파 R1 목록이 스킬 이하 위트니스가 있는데도 아무것도 안 준다 (파렌가 변이 R1 skill 28, 소환 R1 skill 28, 환영 R1 skill 15 -> picked 0; 강령마법사 환영 R1 skill 20 -> 0). 파괴·회복·변이 Prefer 는 줌. 후보: 엔진이 이미 아는 주문(정적 목록)과 겹치는 뽑기를 버림 / 학파마다 뽑기 횟수 제한 / 비용·매지카 조건. 다음 확인: 자가 테스트에서 같은 액터를 여러 번 소환해 학파별 picked 분포를 본다.
