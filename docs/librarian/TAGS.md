# 태그 어휘 — tags-v1

> 2026-09-09 확정. 원소 38종 / 기법 18종.
> 정본은 이 문서. 코드 쪽 거울은 `include/librarian/TagVocabulary.h` 와
> `PrismaUI/.../modules/tagVocabulary.js` — **셋 다 같아야 하고 수동 동기화다.**
> `librarian-test --check-vocab` 이 룰 파일의 모든 태그가 어휘 안에 있는지 검사한다.

---

## 0. 이 목록을 정한 원칙

**가장 까다로운 손님보다 세밀하게.** 뭉치는 건 어댑터에서 언제나 되지만, 한번 뭉쳐 저장한 걸
다시 쪼갤 수는 없다. 그래서 지금 아무것도 붙지 않는 태그도 남긴다.

출발점은 Spell Research 의 원소 34종 · 기법 15종이다. SR 어휘를 그대로 쓰지 않는 이유는
Concept 4절에 있다 — 그건 손님 한 명의 규칙일 뿐이다. 여기에 **이 로드오더의 키워드 체계가
실제로 구분하는데 SR 이 표현하지 못하는 개념 7종**을 더했다. 근거는 각 항목에 적었다.

M2 측정에서 MGEF 구조만으로 실제 붙은 것은 원소 24종 / 기법 15종이다. 나머지는 M7
(텍스트 룰 · LLM 폴백)이 붙이거나, 프레임워크 룰(M2 (b))이 붙인다. **안 붙는다고 지우지 않는다.**

예시 주문은 바닐라(Skyrim/Dawnguard/Dragonborn)에서 골랐다. 이름이 영어인 쪽이 읽기 쉽고,
정답셋이 실제로 그 라벨을 붙인 주문이라 정의와 어긋나지 않는다.

---

## 1. 원소 (38) — 마법이 무엇으로 되어 있는가

| 태그 | 정의 | 예시 |
|---|---|---|
| `acid` | 산성 부식 피해 | (바닐라 없음. 모드 전용) |
| `air` | 바람·기류. KIT 의 `Wind` 를 여기로 합친다 | Whirlwind Cloak / Muffle / Waterbreathing |
| `apparition` | 유령·혼령 형태의 존재 | Summon Arniel's Shade |
| `arcane` | 속성 없는 순수 마력 피해 | Freeze / Ignite / Arniel's Convection |
| `armor` | 방어도 자체를 올리고 내리는 것 | Oakflesh / Stoneflesh / Ironflesh |
| **`blood`** | 피를 대가로 쓰거나 피로 해치는 마법. **SR 에 없음** — KIT `SpellDamageType_Blood`, `SpellSacrificeType_Blood`, OCF `MgefClassBlood` 가 구분한다 | (모드 전용) |
| `construct` | 세상에 남는 물체를 만든다. 룬·수호진·촛불 | Ash Rune / Conjure Ash Guardian / Candlelight |
| `creature` | 살아있는 생물. 소환수, 그리고 생물을 대상으로 하는 것 | Conjure Familiar / Summon Arvak / Frenzy Rune |
| `daedra` | 데이드라 존재와 그 권능. 아트로나크·바운드 무기 | Bound Sword / Conjure Seeker / Bound Dagger |
| `disease` | 질병과 그 치료 | Vampiric Drain |
| `earth` | 흙·돌·재 | Stoneflesh / Ash Rune / Ash Shell |
| **`eldritch`** | 데이드라도 아에드라도 아닌 바깥의 것. **SR 에 없음** — KIT `SpellDamageType_Eldritch`, `SpellSummonType_Eldritch`, OCF `MgefClassEldritch` | (모드 전용) |
| `fire` | 불꽃과 열 | Flames / Firebolt / Fire Rune |
| `flesh` | 육신 자체를 다루는 것. 마법 갑옷, 시체, 마비 | Oakflesh / Raise Zombie / Paralyze |
| `force` | 물리적 밀어냄·충격 | Lesser Ward / Steadfast Ward |
| `frost` | 냉기와 서리 | Frostbite / Ice Spike / Frost Rune |
| `health` | 체력 자원 자체를 주고받는 것. **피해 주문 전반이 아니다** (2절 주의) | Healing / Equilibrium / Courage |
| **`holy`** | 신성·축성된 힘. `sun` 보다 넓다. **SR 에 없음** — OCF `MgefClassHoly`, `SpellDamage_Holy` | (모드 전용) |
| `human` | 인간형 대상에게만 걸리는 것 | Courage / Fury / Calm |
| `life` | 생명력 그 자체 | Conjure Ash Guardian / Conjure Ash Spawn |
| `light` | 빛과 조명 | Candlelight / Magelight |
| `magicka` | 마력 자원 | Equilibrium |
| `metal` | 금속 재질 | Ironflesh / Ebonyflesh / Transmute |
| `nature` | 식물·자연 | Oakflesh |
| **`necrotic`** | 산 것을 시들게 하는 죽음의 힘. 언데드라는 존재(`undead`)와 다르다. **SR 에 없음** — OCF `MgefClassNecromancy`, `SpellDamage_Necrotic` | (모드 전용) |
| `poison` | 독 | Poison Rune |
| `resistance` | 저항 수치 자체를 다루는 것 | (바닐라 없음) |
| `shadow` | 그림자·은신 | (바닐라 없음. KIT `SpellDamageType_Shadow`) |
| `shield` | 막아내는 장벽 | Lesser Ward / Steadfast Ward / Greater Ward |
| `shock` | 번개와 전기 | Sparks / Lightning Bolt / Lightning Rune |
| `soul` | 영혼. 소환의 결속, 소울 젬 | Conjure Boneman / Soul Trap / Bound Sword |
| `stamina` | 기력 자원 | Courage / Rally / Call to Arms |
| `sun` | 햇빛. 언데드에게 특히 아프다 | Stendarr's Aura / Sun Fire / Vampire's Bane |
| `time` | 시간의 흐름 | (바닐라 없음. Slow Time 계열) |
| `trap` | 설치해두고 나중에 터지는 것 | Fire Rune / Frenzy Rune / Poison Rune |
| `undead` | 언데드 존재. 되살리기와 쫓아내기 양쪽 | Conjure Boneman / Raise Zombie / Turn Undead |
| `water` | 물 | Waterbreathing |
| `weapon` | 무기 그 자체 | Bound Sword / Bound Bow / Bound Battleaxe |

## 2. 기법 (18) — 마법이 무엇을 하는가

| 태그 | 정의 | 예시 |
|---|---|---|
| `cloak` | 시전자 주위에 머무르며 계속 작용 | Whirlwind Cloak / Stendarr's Aura / Muffle |
| `control` | 대상의 행동을 지배한다. 소환수 명령, 마비, 되살린 시체 | Conjure Boneman / Raise Zombie |
| `courage` | 사기를 올린다 | Courage / Rally |
| `curing` | 해로운 것을 없애거나 되돌린다 | Healing / Fast Healing / Healing Hands |
| `curse` | 대상을 약화시키는 지속 효과 | Freeze / Ignite / Spectral Arrow |
| **`dispel`** | 걸린 마법을 걷어낸다. **SR 에 없음** — KIT `SpellAADispel`, OCF `MgefSpellDispel` | (모드 전용) |
| `fear` | 겁을 주어 달아나게 한다 | Fear / Turn Lesser Undead |
| `frenzy` | 광란시켜 아무나 공격하게 한다 | Fury / Frenzy / Frenzy Rune |
| `infuse` | 대상에 지속되는 성질을 불어넣는다. 마법 갑옷, 시체 강화 | Oakflesh / Raise Zombie / Conjure Ash Guardian |
| `pacify` | 진정시켜 싸움을 멈추게 한다 | Calm / Pacify / Harmony |
| **`sacrifice`** | 무언가를 대가로 지불하고 효과를 얻는다. **SR 에 없음** — KIT `SpellSacrificeType_Blood`, OCF `MgefSpellSacrifice_Blood/Corpse/Summon` | (모드 전용) |
| `sense` | 보이지 않는 것을 감지한다 | Clairvoyance / Detect Life / Detect Dead |
| `siphon` | 대상에게서 빨아내어 시전자에게 옮긴다 | Soul Trap / Vampiric Drain |
| `strengthen` | 능력치를 끌어올린다 | Courage / Rally / Call to Arms |
| `summoning` | 존재를 불러낸다 | Conjure Boneman / Conjure Familiar |
| `telekinesis` | 물리적으로 붙든다. 마비도 여기다 (정답셋이 그렇게 라벨링한다) | Telekinesis / Paralyze / Ash Shell |
| **`teleport`** | 공간을 건너뛴다. **SR 에 없음** — KIT `SpellTeleportType`, NSV `Magic_Teleport`, OCF `MgefSpellSpace_Teleport` | (모드 전용) |
| `transform` | 형태를 바꾼다 | Bound Sword / Equilibrium |

---

## 3. 정의가 헷갈리는 자리

M2 에서 실제로 틀렸던 지점들이다. 룰을 쓸 때 여기를 먼저 볼 것.

**`health` 는 피해 주문 전반이 아니다.** 1차 룰 초안이 `primaryAV: Health` 인 모든 이펙트에
`health` 를 붙였다가 9적중 28오탐이 났다. 모든 공격 주문이 체력을 건드리므로 이 태그로는
아무것도 구분되지 않는다. **자원으로서의 체력을 주고받을 때만** 쓴다.

**마법 갑옷은 `strengthen` 이 아니라 `infuse` 다.** Oakflesh 는 `{armor, flesh, nature}` +
`{infuse}` 다. `strengthen` 은 Courage 처럼 능력치를 직접 올릴 때다.

**마비는 `control` 이 아니라 `telekinesis` 다.** Paralyze 가 `{flesh}` + `{telekinesis}` 다.
붙들어두는 것이지 조종하는 게 아니라는 해석이다.

**소환은 `soul` 을 동반한다.** 영혼을 묶어 형체를 준다는 관점. Bound Sword 조차
`{daedra, soul, weapon}` 이다.

**`creature` 와 `human` 은 대상을 말한다.** 살아있는 것에게 거는 주문이면 붙는다.
소환수뿐 아니라 Calm, Fury, Detect Life, Heal Other 전부.

**`undead` 와 `necrotic` 은 다르다.** 전자는 존재, 후자는 죽음의 힘 그 자체다.

**`holy` 와 `sun` 은 다르다.** `sun` 은 햇빛 그 자체, `holy` 는 신성한 힘 전반이다.
바닐라 태양 주문은 `sun` 이다.

---

## 4. MGEF 구조로는 절대 볼 수 없는 것

Stoneflesh 와 Ironflesh 는 **MGEF 구조가 완전히 동일하다.** archetype, primaryAV, 저항,
키워드가 전부 같고 이름과 설명문에만 차이가 있다. 그래서 `earth` 와 `metal` 은 구조로
가려낼 수 없다. `air`, `sun`, `nature`, `water`, `life`, `acid` 도 같은 처지다.

이건 룰을 더 쓴다고 해결되지 않는다. **M7 텍스트 룰과 LLM 폴백의 몫이고, 그 크기가
M2 가 잰 태그 0개 268건(18.6%) 과 이런 미세 구분이다.**

---

## 5. 어휘를 고칠 때

1. 이 문서에 태그·정의·예시를 추가한다
2. `include/librarian/TagVocabulary.h` 와 `modules/tagVocabulary.js` **양쪽**에 같은 값을 넣는다
3. `librarian-test --check-vocab -r <룰 디렉터리>` 로 룰 파일과 어긋나지 않는지 확인한다
4. 태그를 **지우는** 것은 카탈로그 호환을 깬다. 지우기 전에 `vocab` 버전을 올릴지 판단할 것

손님에게 나갈 때 뭉치는 것은 어댑터의 일이다. 예를 들어 Spell Research 어댑터는
`blood` → `health`, `eldritch` → `apparition`, `holy` → `sun`, `necrotic` → `undead`,
`teleport` → (버림) 으로 접어서 내보내면 된다. **어휘 자체를 SR 에 맞춰 줄이지 않는다.**
