# 퍽 어댑터 (Perk Adapters)

> 사서가 태그를 붙인 주문에, 각 퍽 모드가 찾는 키워드를 게임 시작 때 메모리에서 붙여 주는 층.
> 퍽 모드마다 어댑터 파일 하나. 마법 모드 쪽 호환 패치는 필요 없다.
> 코드: `include/librarian/PerkAdapters.h`(순수 판정), `src/librarian/LibrarianAdapters.cpp`,
> `include/librarian/PerkAdapterPatch.h` · `src/librarian/LibrarianPerkAdapterPatch.cpp`(게임 쪽).
> 파일: `SKSE/Plugins/SpellLearning/librarian/adapters/*.json`. 측정: 2026-09-30.

---

## 1. 왜 필요한가

퍽 모드는 주문을 **이펙트(MGEF)에 붙은 키워드**로 알아본다. Adamant 의 화염 증강은
`EPMagic_SpellHasKeyword MagicDamageFire` 를 묻는다. 모드 주문의 이펙트에 그 키워드가 없으면
퍽은 그 주문이 화염인지 모른다. 지금까지는 마법 모드 × 퍽 모드 조합마다 사람이 ESP 패치를
만들었다.

사서는 이미 모든 주문을 분류해 `spell_catalog.json` 에 태그를 남긴다. 어댑터는 그 태그와 스캔
구조를 보고 퍽 모드가 찾는 키워드를 **이펙트에** 붙인다. 퍽 레코드는 건드리지 않는다.

## 2. 퍽 모드가 실제로 읽는 것 (측정)

받은 퍽 모드 7개의 퍽 조건을 전부 덤프해 모았다(SPERG 1.8, Mysticism 2.5.0, Adamant 6.0.4,
Path of Sorcery 3.2, Vokrii 3.8.2, Ordinator 9.35.0, Vokriinator Black 6.15.3).

- **공통 — 바닐라 키워드**: `MagicDamageFire/Frost/Shock`(7개 전부), `MagicSummon*`,
  `MagicInfluence*`, `MagicRestoreHealth`, `MagicWard`, `MagicRune`, `MagicCloak`,
  `MagicArmorSpell`, `MagicTurnUndead`, `MagicParalysis`, `MagicInvisibility`,
  `MagicSchoolConjuration`(Vokrii·Ordinator 소환 퍽이 "그 밖의 소환"으로 읽는다)
- **Adamant(+Mysticism, VB)**: `MAG_MagicDamagePoison/Sun`, `MAG_MagicInfluence*`,
  `MAG_MagicSummonReanimate/Weapon`, `MAG_MagicShield/Circle/WallSpell` 등. 키워드는 Mysticism 이
  정의한다(대부분 Update.esm FormID 대역)
- **Path of Sorcery**: `IMP_K_MagicDamagePoison/Holy/Disease`, `IMP_K_MagicSummonDaedra`
- **Ordinator**: 바닐라만. `ORD_` 키워드는 전부 Ordinator 자신의 이펙트 표식이다
- **Vokrii**: 바닐라 + `RitualSpellEffect/Illusion`(양손 주문)
- **Ascension**(Darenii, 1.0.1 · 2 1.0.4 · Adamant 6 Patch 2): 무속성 파괴 피해 키워드 하나
  (`Update.esm|0x1EA6002`) + `MagicCloak`. 2026-09-30 추가, 아래 6-1절
- **Paragon 1.0.0**(+ Magic Addon, TAKEALOOK 에 켜져 있음): 바닐라 원소·`MagicVampireDrain`·`MagicCloak`·
  `MagicRune`·`MagicSummonFamiliar`, Mysticism 계열 `MAG_MagicDamagePoison/Sun`·`MAG_MagicSummonReanimate`, 그리고
  무속성 `APO_MagicDamageUnspecified`(= Ascension 의 그 FormID). 새 키워드는 없다 — 그래서 새 파일 대신
  `adamant.json`·`ascension.json` 이 `ParagonPerks.esp` 로도 켜진다. Paragon 자신의 KID ini 가 일반 마법 피해
  키워드(`APO_VALOR_MagicDamageGeneric`)를 규칙으로 직접 나눠 주므로 그것은 필요 없다
- **Bloodmoon · Desecration**(Darenii 주문 팩의 퍽): 원소·흡혈·태양·독·무속성 키워드만 읽는다(이미 붙인다).
  `BloodmoonDestruction`·`DesecrationDestruction` 은 그 모드 자신의 이펙트에만 있고 어느 퍽도 읽지 않는다 — 새 파일 없음
- **Custom Skills 마법 트리**(2026-10-08): Hydromancy·Atromancy·Hemomancy·Venomancy 는 자기 키워드를 읽어 파일을 새로 만들었고, Subclasses 는 Adamant·Ascension 파일을 켜고, 나머지는
  바닐라 키워드만 읽는다. 아래 6-2절
- **Paths of Empyrean 1.0**(2026-10-19): 바닐라 키워드 + Lost Grimoire 의 `GRIM_K_MagicDamagePoison/Sun/Disease/Kinetic` +
  자기 `SF_K_MagicSummonDaedra`. `MAG_MagicDamagePoison/Sun` 도 묻지만 언제나 GRIM_K 쌍둥이와 같은 OR 묶음이다.
  `pathsofempyrean.json`, 아래 6-4절

키워드가 아닌 방법도 있다 — 주문 목록(FormList: Adamant 의 집중 화염·전기 목록, PoS 의 Advanced
Study 목록)과 보조 이펙트(Adamant 의 Impact·Firebrand). 둘 다 아직 다루지 않는다(7절).

## 3. 파일 형식

```json
{
  "perkMod": "Adamant - A Perk Overhaul 6.0.4",
  "measured": "2026-09-30",
  "requires": { "any": ["Adamant.esp", "MysticismMagic.esp", "Vokriinator Black.esp"] },
  "adapters": [
    { "keyword": "MAG_MagicDamagePoison", "why": "poison damage",
      "tags": { "all": ["poison"] },
      "effect": { "detrimental": true, "resistance": "PoisonResist", "primaryAV": "Health" } }
  ]
}
```

| 키 | 뜻 |
|---|---|
| `requires` | `{"always": true}` 또는 `{"any": [플러그인...]}` — 하나라도 로드되면 파일이 켜진다(대소문자 무시, ESL 포함) |
| `keyword` | 붙일 키워드의 EditorID. 로드오더에 없으면 그 줄은 쉰다 |
| `keywordForm` | `keyword` 와 함께: 그 키워드의 영구 id(예 `Update.esm\|0x1EA6002`). 여러 모드가 한 FormID 를 서로 다른 EditorID 로 주입할 때 쓴다 — 게임에서는 로드된 폼이 가진 이름이 `keyword` 를 대신한다(마지막에 로드된 플러그인의 이름). 오프라인 평가는 `keyword` 를 그대로 쓴다 |
| `formList` | `keyword` 대신: 조건에 맞는 주문을 넣을 퍽 모드의 FormList(영구 id, 예 `Adamant.esp\|0x9DEF54`). 목록에 이미 든 주문은 건너뛰고, 누출 검사는 없다(목록은 주문 자체를 가리킨다) |
| `castingPerk` | `keyword` 대신: `"vanilla"` 하나. 고른 주문 중 반값 시전 퍽이 **없는** 주문에 카탈로그 학파·등급의 바닐라 퍽(`DestructionNovice00` …)을 준다. 누출 검사는 없다(퍽은 주문 레코드에 붙는다). 이미 퍽이 있는 주문은 agree. 7-2절 |
| `enabled` | `false` 면 끈다. 이유는 `why` 에 |
| `sharedEffects` | `keyword` 줄에서 `"allow"` 하나. 누출 검사(5절 4)를 건너뛴다 — 고른 주문의 이펙트에 붙이면 그 이펙트를 같이 쓰는 주문서·지팡이·다른 주문(그 이펙트를 퍽 보너스로 쓰는 주문 포함)도 키워드를 받는다. `keyword` 가 아닌 줄에 쓰거나 값이 다르면 그 줄은 꺼진다. 죽인 이펙트를 읽는 FEC 처럼 그쪽이 오히려 맞을 때만 쓴다. 퍽 파일에는 쓰지 않는다(2026-10-13) |
| `tags` | 카탈로그 태그. `all` / `any` / `none` |
| `spell` | 스캔 주문 필드: `school`, `notSchool`, `tier`, `casting`, `twoHanded`, `taughtByTome`(마법책이 가르치는 주문) |
| `effect` | 스캔 이펙트 필드. 맞는 **첫 번째 보이는 이펙트**가 키워드를 받는다(없으면 첫 보이는 이펙트) |
| `spellHas` / `spellLacks` | 주문의 어떤 이펙트가(숨김 포함) 맞아야 / 하나도 맞으면 안 된다 |
| `keywordsNone` | 주문에 이 키워드가 하나도 없어야 한다 — 앞선 줄이 붙인 것까지 센다. 제외 조건 |
| `formListsNone` | 같은 판정에서 앞선(켜진) `formList` 줄이 이 FormList(영구 id)들로 고른 주문이 아니어야 한다. 레코드나 다른 모드가 목록에 이미 넣은 주문은 보지 않는다(그 주문이 앞선 줄에도 맞았으면 센다). Spell Research 의 "SR 원소가 없는 주문" 차집합에 쓴다(2026-10-03) |

이펙트 조건 키: `archetype`, `archetypeAny`, `archetypeNone`, `primaryAV`, `primaryAVAny`,
`resistance`, `detrimental`, `delivery`, `minDuration`, `noDuration`,
`hazardSource`(impact | effect | explosion), `explodes`, `visible`,
`summonedAny` / `summonedNone`(소환되는 생물의 종족 키워드 — 스캔의 `summonedKeywords`).
모르는 키가 있는 줄은 **꺼진다**(로그 경고). 오타가 조용히 모든 주문에 맞는 것보다 낫다.

## 4. 규칙을 정하는 방법

1. **근거는 퍽 쪽에서 잰다.** 줄마다 퍽 조건이 "있어야 한다"(cmp=1)로 요구하는 키워드만 넣는다
2. **태그만으로 붙이지 않는다.** 화염 아트로나크 소환도 `fire` 태그를 받는다. 태그(사서의 판단)와
   이펙트 구조(레코드에 적힌 값)가 둘 다 맞아야 한다
3. **스캔 데이터만 본다.** 주문·이펙트·플러그인 이름을 적지 않는다(`requires` 만 예외).
   원작자가 붙인 키워드는 규칙을 **채점**하는 데만 쓴다
4. **정밀도가 재현율보다 먼저다.** 엉뚱한 주문에 퍽이 걸리는 건 새 버그, 못 붙이는 건 지금 상태다
5. **제외 키워드도 규칙으로 쓸 수 있다** — 원작자의 뜻을 재현할 때만(`MAG_PerkScourgeExclude` 는
   원작에서 "Scourge 효과가 이미 든 주문의 중복 방지"라서 끔)

## 5. 게임에서 하는 일

`kDataLoaded` 와 카탈로그를 다시 만드는 스캔 뒤(패널의 트리용 스캔, 이펙트까지 읽는 Papyrus
`RunScan` - 기본인 마법책 모드도 포함: 그때 카탈로그는 마법책 주문만 남고 어댑터도 그에 맞춘다)에
`PerkAdapters::Apply` 가 돈다. 패널이 열릴 때 도는 마법책 스캔 뒤에는 돌지 않는다(카탈로그가 그대로이고, 패널이 열리는
프레임을 막는다). 모두 게임 스레드.

1. `config.json` 의 `perkAdapters.enabled` 가 `false` 거나(없으면 켜짐) 카탈로그가 없거나 맞는
   파일이 없으면, 지난번에 붙인 것을 전부 거두고 끝
2. 카탈로그를 읽고, `requires` 가 맞는 파일만 읽는다
3. 판정용 주문 JSON 은 스캐너로 만든다 — 스캐너가 붙인 키워드를 빼므로, 지난 실행의 결과가 판정에
   섞이지 않는다
4. 판정(`BuildPlan`, 헤더에 적힌 규칙 그대로):
   - 카탈로그 주문만. 이미 그 키워드를 가진 주문은 건너뛴다
   - 대상 이펙트의 이펙트 항목에 **조건**이 있으면(퍽 보너스) 건너뛴다. 같은 이펙트를 다른 주문이
     조건 걸고(퍽 보너스로) 쓰고 있어도 막는다
   - **누출 검사**: 이펙트를 쓰는 모든 아이템이 통과해야 쓴다 — 같은 줄이 고른 카탈로그 주문,
     이미 키워드를 가진 주문, 카탈로그 밖 주문(태그를 뺀 조건 전부를 만족할 때). 마법부여·
     스크롤·물약·재료가 같은 이펙트를 쓰면 막는다(스크롤은 엔진에서 주문의 한 종류지만 폼 타입으로
     갈라 막는 쪽에 넣는다)
5. 결과에 맞춘다: 지난번에 붙였는데 이번 판정에 없는 쌍은 떼고, 새 쌍만 붙인다(개수를 전후로
   세서 확인). 플러그인이 직접 적은 쌍은 붙이지도 떼지도 않는다. 카탈로그나 파일이 바뀌면 붙인 것도
   거둔다
6. `perk_adapters_report.json`(카탈로그 옆)에 줄마다 agree / writes / gain / leaks /
   conditioned / blockedByItem 과 예시를 남기고, 로그에 한 줄 요약(걸린 시간 포함)

**스캔 격리**: 스캐너는 붙인 키워드를 스캔 JSON, 칩·트리 특성, 카드 아이콘에서 뺀다
(`PerkAdapters::IsInjected`). 스캔은 플러그인에 적힌 그대로 남고, 사서가 자기가 붙인 키워드로
다시 분류하는 일(예전 M6 의 문제)이 없다. 채워 준 시전 퍽도 같다: 스캔 JSON 의 `castingPerk`, 주문 등급
(`DetermineSpellTier` — 트리 깊이·등급 XP·수동 학습이 읽는다), 사본 판정은 `PerkAdapters::OriginalCastingPerk`
로 플러그인이 적은 퍽만 본다.

메모리에서만 바뀐다. 플러그인 파일도 세이브도 건드리지 않으므로 모드를 빼면 원상태다.
설정(`perkAdapters.enabled`)은 게임 시작과 스캔 때 읽는다 — 끄면 다음 게임 시작이나 스캔부터 거둔다.
`config.json` 을 읽지 못하면 켜진 것으로 본다. 붙여 둔 키워드를 다른 모드가 키워드 목록을 새로 만들며
지웠으면 다음 실행이 다시 붙인다.

**다른 배분기보다 나중 (2026-10-11).** KID·SkyPatcher·FEC 도 `kDataLoaded` 에 키워드를 붙이고, SKSE 는 DLL 을
이름순으로 읽어(`po3_…`, `SkyPatcher` 가 `SpellLearning` 앞) 그 순서로 메시지를 돌린다. TAKEALOOK 로그로 확인한 순서:
KID 배분 16:05:30 → SkyPatcher 적용 16:06:31 → 어댑터 16:06:34. 그래서 어댑터는 그들이 붙인 키워드를 보고
(이미 있으면 agree) 마지막에 붙인다. 그 뒤에 무엇이 떼어 가면 새 게임·세이브 로드 때 `PerkAdapters::Reassert`
가 이 파일이 붙였던 쌍 중 빠진 것만 다시 붙인다(새로 판정하지 않고, 플러그인이 적은 키워드는 건드리지 않는다).
다른 패치가 붙인 키워드를 떼지는 않는다 — 퍽 조건이 같은 키워드를 읽기 때문이다.

## 6. 측정 (2026-09-30, TAKEALOOK 스캔 3546 주문)

오프라인 평가: `librarian-test -i <dump> --catalog <catalog> --adapters <dir> [-o report.json]` —
게임과 같은 판정기(`BuildPlan`)를 쓴다. 덤프에는 주문이 아닌 아이템이 없으므로 누출 수는 하한이다.
정답은 받은 퍽 모드 ESP 가 이펙트에 직접 붙인 키워드로 채점했다(PowerShell 평가기, 같은 숫자).

| 파일 | 켜진 줄 | 새로 붙는 주문 (gain) |
|---|---:|---|
| vanilla | 23 | 모든 줄 누출 0. 원소 피해·룬·결계·투명화는 0(원작이 이미 다 붙임). 소환 3/1/1/5, 그 밖의 소환 90, 흡혈 55, 망토 10 |
| adamant | 11 | 독 42, 태양 17, 되살리기 20, 구속 무기 30(누출 1 걸러짐), 벽 20, 원 7 |
| pathofsorcery | 4 | 독 140, 신성 86, 질병 23, 데이드라 35 |
| vokrii · ordinator | 0 | 아래 |

규칙마다 찾은 근거:
- `MagicRune`: `trap`+`construct`, 폭발 투사체, 지점 시전 — 원작과 54건 일치, 새로 0
- `MagicArmorSpell`: 결계도 방어도를 올리므로 결계 키워드·결계력 이펙트가 있으면 제외
- `MAG_MagicWallSpell`: 벽의 피해 이펙트는 archetype 이 Script(32 중 28), 착탄 장판을 남기는
  일반 분사는 114 중 1
- 소환 줄: **종족 먼저, 태그는 그다음.** 종족 줄(소환되는 생물의 종족이 `ActorTypeUndead` 면
  `MagicSummonUndead`, 언데드·데이드라가 아닌 `ActorTypeAnimal` 이면 `MagicSummonFamiliar`, PoS 는
  `ActorTypeDaedra` 면 데이드라)이 앞에 있고, 기존 태그 줄은 뒤에서 종족으로 못 가르는 것(원소, 종족
  키워드가 없는 생물)을 맡는다. 어느 줄이든 원작자나 앞 줄이 이미 준 다른 소환 분류가 있으면 더하지
  않는다. 종족 줄은 2026-09-30 추가, 새 스캔으로 측정할 것
- `IMP_K_MagicSummonDaedra`: PoS 자신이 드레모라·시커·화산재 수호자에만 붙인다 — 원소 아트로나크는
  `MagicSummonFire/Frost/Shock` 로 이미 퍽을 받는다

## 6-1. Ascension (2026-09-30)

넥서스 89223(1.0.1, Adamant 5 애드온 — TAKEALOOK 설치본은 퍽 이름만 한국어로 옮긴 같은 파일),
92000(Ascension 2 1.0.4, Custom Skills 트리), 188452(Adamant 6 Patch v2, 원소술사 퍽)를 받아 퍽을 덤프했다.
셋 다 파괴 주문에 **한 키워드**를 묻는다: 원시의 힘(무속성 피해), 처형(체력 50% 미만 적), 굴절과 원소술사
(화염·냉기·전격 옆). 마법 망토·파괴적 장벽은 `MagicCloak` 이라 바닐라 파일이 맡는다.

그 키워드는 Update.esm 대역에 주입된 `0x1EA6002` 이고, Ascension 은 `DAR_UnspecificMagicDamage`,
Apostasy Framework 는 같은 FormID 를 `APO_MagicDamageUnspecified` 로 부른다. TAKEALOOK 에서는 뒤의 이름이
이겨 스캔에는 APO 만 보인다. 그래서 줄이 키워드를 FormID 로 지정한다(`keywordForm`).

무속성의 뜻은 제작자 것을 따랐다. 굴절 퍽이 파괴 피해를 화염·냉기·전격·독·흡혈·무속성 여섯으로 나누고,
Darenii 의 주문 팩은 비전·그림자·피·괴저·자연 피해(Arcane, Abyss, Bloodmoon, Desecration, Natura)에 이
키워드를 달고 전격·태양·독(Arclight, Lunaris, Necrom)에는 달지 않는다. 그래서 줄은 "다섯 키워드 중 어느 것도
없는 파괴 체력 피해, 사서 원소가 그 밖의 것". TAKEALOOK 의 KID ini 는 흡혈도 무속성으로 넣지만 제작자는
흡혈을 따로 두므로 따르지 않았다.

측정(키워드 이름을 로드된 이름으로 바꿔 오프라인 평가): 이미 단 주문과 일치 158, 새로 붙는 주문 141
(이펙트 83개), 누출로 막힌 이펙트 2(Necromancer's Magic 과 Midnight Sun 이 함께 쓰는 이펙트). 새로 받는
것은 Vigilant·Glenmoril·Unslaad·Dragonborn 의 비전·지식 흡수 계열 등이고, 상당수는 NPC·보스 주문이다.

## 6-2. Custom Skills 마법 트리 (2026-10-08)

Custom Skills Framework 로 마법 퍽 트리를 따로 두는 모드와 마법에 닿는 커스텀 스킬 모드 20개를 넥서스에서 받아 퍽 조건을 덤프했다. 이 트리들은
주문 모드를 SkyPatcher ini 로 **이름을 적어** 지원한다(Hydromancy: Natura, Elemental Destruction Magic Redux,
Phenderix, KittySpellPack01, Water Atronach). 적히지 않은 모드의 주문은 퍽을 못 받는다. 어댑터는 사서 태그로 그
목록 밖의 주문까지 같은 키워드를 준다.

| 모드 (넥서스) | 퍽이 묻는 주문 키워드 | 결과 |
|---|---|---|
| Hydromancy 1.1 (186022) | `_HYD_MagicDamageWater`(17), `_HYD_MagicSummonWater`(1) | `hydromancy.json` |
| Atromancy 2.1 (149636) | 바닐라 소환·원소 + `_ATRO_MagicSummon<Earth/Water/Wind/Shadow/Sun/Flesh/Sugar>`, `_ATRO_MagicSummonAtronach`, `_ATRO_MagicDamage<Earth/Water/Wind/Shadow/Sun>` | `atromancy.json` |
| Electromancy 1.7 (171694) · Cryomancy 1.3 (179969) · Pyromancy 1.1 (182965) | `MagicDamageShock` / `Frost` / `Fire` (Pyromancy 는 자기 화상 표식 키워드도) | 바닐라 파일이 맡는다 |
| Pyromancy - Custom Skill Tree 1.0.1 (53312) · Cryomancy - Custom Skill Tree 1.2 (121265) | 바닐라 원소 + 자기 강화 이펙트 키워드(`FLM_`/`CLM_Boosted*`) | 바닐라 파일이 맡는다 |
| Hemomancy 2.0.2 (74968) | `Kata_BloodMagicPower`/`…Conc`(혈마법 위력·흡수 퍽), `Kata_Blood*Cost*`(피 비용 퍽) | `hemomancy.json` — 위력 키워드만 |
| Venomancy 1.5 (146585) | `_VENOM_MagicDamagePoison`(18), `_VENOM_MagicSummonVirulent`(9) | `venomancy.json` |
| Subclasses of Skyrim 2 1.0.3 (98784) | `MAG_MagicDamagePoison/Sun`, `MAG_MagicSummonReanimate`, `DAR_UnspecificMagicDamage`(= Ascension 의 `0x1EA6002`) — 직접 Update.esm 대역에 정의 | `adamant.json`·`ascension.json` 의 `requires` 에 추가 |
| Constellations 1.0.3 (117352) · Sets of Skills CSF 1.3 (97855) | 바닐라 원소·소환·방어 + `EPMagic_SpellHasSkill`(학파) | 바닐라 파일이 맡는다 |
| Custom Skills - VIGILANT 2.0a (45026) | 바닐라 + `DLC1VampireDrainEffect`(`MagicVampireDrain` 과 OR) | 바닐라 파일이 맡는다 |
| Prelude to Purgatory 3.0b6 (53143) · Molag's Will 2.4.1 (58621) · Haemophilia 1.0.1 (46632) | 바닐라 키워드, 나머지는 행위자 키워드(`Vampire` 등) | 할 일 없음 |
| Custom Skills - GLENMORIL 2.1.0 (45120) · Pilgrim CSF 1.2.1 (93913) | 자기 무기·축복 표식 | 할 일 없음 |
| Everdamned 1.7.0 (162489) | 자기 피 비용 표식, `ED_Mechanics_Keyword_Hemomancy`(Vitae, Blood Resonance 발동) | `everdamned.json` — 혈마법 키워드만 |
| College of Winterhold Perk Tree 1.1 (112348) | 바닐라(`MagicWard`·`MagicArmorSpell`·`MagicInvisibility`·`MagicRestoreHealth`) + `SpellHasCastingPerk`(파괴·소환 퍽 2개) | 키워드는 바닐라 파일이 맡는다. 시전 퍽은 아래 |

Custom Skills 를 설명에 적은 넥서스 모드 560개(v2 GraphQL 설명 검색)에서 마법과 관련된 것을 골라 받았다.

**Venomancy.** 본체와 선택 패치(Natura·Lost Grimoire·Mysticism)가 `_VENOM_MagicDamagePoison` 을 독 이펙트에 단다. 줄은 Adamant 의
독 줄과 같은 조건. 측정 130 주문, 누출 0. `_VENOM_MagicSummonVirulent`("Blight" 소환 — 패치는 스프리건, 시커, 럴커를 골랐다)는
이 고른 것을 따라 세 줄로 준다: 독·질병 태그 소환(2), 종족이 스프리건인 소환 중 언데드·데이드라가 아닌 것(11 — RKF 종족 키워드는
골격을 따라서 망령·골렘도 스프리건으로 나온다), 데이드라·eldritch 태그에 종족이 시커·럴커인 소환(7). 2026-10-09 스캔 기준.

**Everdamned.** 흡혈귀 개편의 혈마법은 자기 주문에 `ED_Mechanics_Keyword_Hemomancy` 를 달고, Vitae 와 Blood Resonance 퍽이
그것을 묻는다. 줄은 hemomancy.json 과 같은 조건(피 태그 + 해로운 체력 이펙트)이고 비용 표식(`BloodCost*`, `DeductBloodCost`)은
주지 않는다. 측정 121 주문(2026-10-09 스캔), 누출 1(Dawnguard 생기 흡수가 이펙트를 공유).

**Atromancy Flesh·Sugar.** 애드온이 고른 살점·설탕 아트로나크는 스캔에서 갈라낼 태그도 종족 키워드도 없다(ECSS 살점 아트로나크는
언데드 종족 키워드뿐). 주문 이름을 적지 않는 원칙(4절 3)이라 이 둘만 주지 않는다.

**Hydromancy.** 물 피해는 `water` 태그 + 해로운 체력 이펙트. 소환은 `water`+`daedra` — Hydromancy ini 가 표시하는
Natura 물 아트로나크는 Natura 가 `MagicSummonFamiliar` 를 달아 두었으므로 그 키워드로 거르지 않는다.

**Hemomancy.** 자기 주문 15개에만 `Kata_BloodMagicPower`(위력)와, 체력을 대신 깎는 숨은 비용 이펙트에 `Kata_BloodCost*` 를 단다.
Blood Pressure(+10/25/50%)·Blood Cloting(체력이 낮을 때 흡수 증가)·Blood Pact·Blood Spheres·Sanguine Mortis 가 위력 키워드를
묻는다. 어댑터는 `blood` 태그 + 해로운 체력 이펙트에 위력 키워드만 준다. 비용 키워드는 주지 않는다 — 다른 모드의 혈마법은
자기 비용을 따로 치르고, 비용 퍽은 Hemomancy 의 비용 이펙트 크기를 줄일 뿐이다. Hemomancy 가 체력 흡수도 혈마법으로
두므로 흡혈 계열도 받는다. 측정: 94개 주문(이펙트 67), 누출 0. `blood` 태그 주문의 출처는 Bloodmoon 24, Ancient Blood II 22, Vigilant 21(보스·NPC),
Mysticism 흡혈 16, Sacrilege 12, 바닐라 흡혈귀의 흡수 등. 알려진 겹침: Bloodskal Blade 의 휘두르기 주문은 KIT 가 피 원소로
분류해 함께 받는다(무기가 대신 시전하는 주문이라 퍽이 그 피해도 키운다).

**College of Winterhold.** 파괴·소환 퍽 둘은 키워드가 아니라 `SpellHasCastingPerk`(주문의 반값 시전 퍽 = 학파와 등급)를
묻는다. 시전 퍽이 없는 주문은 바닐라 학파 퍽처럼 이 퍽도 못 받는다. TAKEALOOK 카탈로그에서 파괴 1506 중 636, 소환 862 중
58 이 시전 퍽이 없다(대부분 Vigilant·Glenmoril·바닐라의 보스·NPC·무기 주문). 마법책 주문은 시전 퍽 줄이 채운다(7-2절).

**Atromancy.** 원소 키워드는 본체(`Atromancy.esp`)가 정의하고, 선택 애드온이 SkyPatcher 로 정해진 아트로나크·주문
모드의 이펙트에 붙인다. 애드온은 소환마다 원소 키워드 옆에 `_ATRO_MagicSummonAtronach`(Elemental Conservation 의
비용 감소)를 함께 단다. 어댑터도 같다. 퍽은 피해 키워드를 "그 원소 아트로나크가 쓰러지면 그 원소 주문이 약해진다"
(Diminished)에서 읽는다. 불·냉기·전격 태그가 있는 소환과 피해는 뺀다 — 바닐라 키워드로 이미 퍽을 받고, Molten-Iron
애드온도 Vulcano 용암 아트로나크에 원소를 주지 않는다. Flesh·Sugar 는 맞는 태그가 없어 주지 않는다. 애드온이 하는
나머지 절반(아트로나크 종족 키워드, 종족에 주는 주문)은 레코드가 다른 층이라 어댑터 몫이 아니다.
알려진 겹침: Abyss 의 그림자 늑대는 그림자 아트로나크와 태그·종족 키워드가 같아 함께 받는다(Shadow 애드온은 뺐다).

측정(TAKEALOOK 덤프, 두 파일 모두 `requires` 를 풀고 오프라인 평가 — 두 모드는 설치돼 있지 않아 agree 는 0):

| 줄 | gain | 예 |
|---|---:|---|
| `_HYD_MagicDamageWater` · `_ATRO_MagicDamageWater` | 19 | Natura 물 주문, Simonrim 왼손 사본 |
| `_HYD_MagicSummonWater` · `_ATRO_MagicSummonWater` | 2 | Natura 물 아트로나크·군주 |
| `_ATRO_MagicSummonEarth/Wind/Shadow`, `…Atronach` | 2 / 2 / 3, 9 | Natura 바위·바람, Abyss 그림자 |
| `_ATRO_MagicDamageEarth/Wind/Shadow/Sun` | 18 / 28 / 31 / 60 | Natura, Abyss, Vigilant, Dawnguard 태양 불꽃 |

누출로 막힌 것: 바람 1(Dragonborn 회오리 망토가 피해 이펙트를 공유). 애드온 범위와 대조: Sun 애드온도 Dawnguard·
Mysticism·Odin·Triumvirate 의 태양 주문을, Air 애드온도 Skyrim.esm 의 바람 이펙트를 넣으므로 바닐라·DLC 주문이 받는 것은
제작자 뜻과 같다. Natura 물 공격 주문 중 물의 벽은 피해 이펙트가 Script·AV 없음이라 빠진다.
게임에서는 확인하지 않았다.

## 6-3. Frozen Electrocuted Combustion (2026-10-11)

퍽 모드가 아니지만 읽는 방식이 같다. FEC 6.1.3(넥서스 3532)은 마법으로 죽은 액터의 사망 효과를 죽인
이펙트의 키워드로 고른다(github.com/powerof3/FEC, `src/Papyrus.cpp` GetCauseOfDeath — 적대·해로움 둘만 본다,
판정 순서 Sun → Fire → Frost → Shock). 불·냉기·번개는 바닐라 키워드라 `vanilla.json` 이 이미 맡는다. `fec.json` 은
FEC 만 읽는 둘을 맡는다(`requires`: `FEC.esp`):

| 줄 | 태그 | writes / gain (allow 이전, 오프라인) | 예 |
|---|---|---:|---|
| `PO3_MagicDamageSun` (FEC.esp 가 정의) | `sun` | 64 / 76 | Lightpower 태양의 파편, Stellaris 천체의 물기, Lunaris 달의 특이점, Mysticism 태양빛의 손길 |
| `MagicDamageWind` (FEC 는 정의하지 않음) | `air`, 불·냉기·번개 아님 | 21 / 25 (agree 3) | Natura 돌풍·바람의 칼날, Vigilant 카인의 바람의 칼날 |

- FEC 의 DLL 도 게임 시작 때 `PO3_MagicDamageSun` 을 붙이지만, 바닐라 Dawnguard 태양 이펙트(타격 셰이더·아트·시전 아트)를
  쓰는 이펙트에만 붙인다(`src/Patches.cpp` SunKeyword). 다른 모드의 태양 주문은 이 줄로만 태양 사망을 얻는다. Dawnguard
  주문이 gain 에 잡히는 것은 오프라인 평가에 FEC DLL 이 없어서다 — 게임에서는 FEC 가 먼저 붙여 agree 가 된다.
- `MagicDamageWind` 는 FEC 가 EditorID 문자열로 비교하고 정의하지 않는다. Mihail High Fantasy Pack·Elemental Destruction Magic
  Redux 같은 주문 모드가 정의하며, 없는 로드오더에서는 줄이 쉰다. FEC 가 이 키워드를 읽는 곳은 `fec_deatheffectsnpctemp.psc` 의 `OnEnterBleedout` 하나뿐이다:
  대상이 빈사에 들어갈 때(필수·보호 NPC 등) 바람 효과가 걸려 있으면 질식시킨다. 빈사 없이 바로 죽는 대상에게는 아무 일도 없다.
- 처음(allow 이전) 오프라인에서 누출로 막힌 것은 바람 1(Dragonborn 회오리 망토)뿐이었다. **이 숫자는 하한이었다**: 덤프에 주문서·마법부여가
  없다. 게임(TAKEALOOK 2026-10-13)에서는 태양 35·바람 16 이펙트가 막혔고(Lunaris 달빛 섬광은 주문서·지팡이, 월식은 주문서가 같은
  피해 이펙트를 쓴다), Lunaris 로 죽인 해골은 FEC 확률 100 에서도 재가 되지 않았다. 그래서 두 줄 모두 `"sharedEffects": "allow"`
  — 주문서·지팡이로 죽여도 FEC 사망이 나와야 맞다. allow 이후에는 막히는 것이 없다(회오리 망토도 받는다). 위 표 수치는
  allow 이전 값이다. allow 이후 수치는 게임의 `perk_adapters_report.json` 으로 다시 잰다.
- `holy` 는 넣지 않았다. FEC 에 신성 사망이 없고, 태양 사망(불붙어 재가 됨)은 신성 피해의 모습이 아니다.
- 측정: TAKEALOOK 스캔(2026-10-07, 3776 주문), `librarian-test -i <덤프> --catalog <master 룰로 만든 카탈로그> --adapters`.
  게임: 누출 차단은 2026-10-13 에 확인했다. allow 를 넣은 뒤의 사망 효과는 아직 확인하지 않았다.

## 6-4. Paths of Empyrean (2026-10-19)

Paths of Empyrean 1.0(넥서스 58656, 2026-10-17 파일)은 18개 스킬 트리를 모두 바꾸는 퍽 모드다. Ordinator·Adamant·Vokriinator 와
함께 쓰지 않고, Mysticism 은 필요 없다. 마법 레코드는 `PathsOfEmpyrean.esp` 하나에 있고, 그 하드 마스터가 Lost Grimoire
(`LostGrimoire.esp`)라서 `GRIM_K_*` 키워드는 PoE 와 언제나 함께 있다. 퍽 561개(자기 432, 덮어쓰기 129)의 조건을 덤프했다.

**퍽이 묻는 것.** 주문 쪽 조건(엔트리 포인트 탭 1)은 `EPMagic_SpellHasSkill`(학파, 키워드 불필요), `EPMagic_SpellHasKeyword`,
`SpellHasCastingPerk`, `IsInList` 다. "있어야 한다"(cmp=1)로 묻는 이펙트 키워드:

| 키워드 | 정의 | 묻는 퍽 | 결과 |
|---|---|---|---|
| `MagicDamageFire/Frost/Shock`, `MagicSummon*`, `MagicInfluence*`, `MagicRune`, `MagicWard`, `MagicRestoreHealth`, `MagicTurnUndead` | 바닐라 | Elementalist, Conductivity, Rune Mage, Mage Ward, Urgency, Fear The Dawn 등 | `vanilla.json` |
| `GRIM_K_MagicDamagePoison` (`17F246`) | Lost Grimoire | Druid, Spell Aegis, Aetherial Shield, 피해 배율 OR 목록 | 새 줄 |
| `GRIM_K_MagicDamageSun` (`14FA41`) | Lost Grimoire | Fear The Dawn(이것만, `MagicDamageFire` 와 OR), 피해 OR 목록 | 새 줄 |
| `GRIM_K_MagicDamageDisease` (`17F247`) | Lost Grimoire | Druid, 피해 OR 목록 | 새 줄 |
| `GRIM_K_MagicDamageKinetic` (`1364C5`) | Lost Grimoire | Telekinetic(이것만), 피해 OR 목록 | 새 줄 |
| `SF_K_MagicSummonDaedra` (`000809`) | PoE | Legion of Souls, Summoner 30/70, Twin Souls (모두 다른 소환 키워드와 OR) | 새 줄 |
| `MAG_MagicDamagePoison/Sun` (`Update.esm` `ADA001/ADA002`, PoE 가 레코드 사본을 가짐) | Mysticism 대역 | 위 OR 목록 | 붙이지 않음 |
| `PoE_K_MagicTransformation` (`000803`) | PoE | Metamorphosis 하나 | 붙이지 않음 |

**파일.** `adamant.json` 의 `requires` 에 PoE 를 넣지 않고 `pathsofempyrean.json` 을 따로 만들었다(`requires`: `PathsOfEmpyrean.esp`).
PoE 퍽 조건을 OR 묶음으로 풀어 보면 `MAG_MagicDamagePoison/Sun` 을 묻는 조건 58개(엔트리마다 센 수, 독·태양 합계)가 전부 같은 묶음에서 GRIM_K 쌍둥이도 묻고,
Fear The Dawn 은 `GRIM_K_MagicDamageSun` 만 묻는다. 그래서 GRIM_K 넷이면 모든 퍽을 덮고 MAG_ 줄은 더하는 것이 없다.
`adamant.json` 을 켜면 PoE 가 읽지 않는 `MAG_` 보호막·원·벽·영향 키워드까지 붙는다. 키워드는 모두 `keywordForm` 으로 FormID 를 적었다.

| 줄 | 조건 (재사용한 모양) |
|---|---|
| `GRIM_K_MagicDamagePoison` | `adamant.json` 독과 같다: `poison` + 해로움·`PoisonResist`·체력 |
| `GRIM_K_MagicDamageSun` | `adamant.json` 태양과 같다: `sun` 또는 `holy` + 해로움·체력 |
| `GRIM_K_MagicDamageDisease` | `pathofsorcery.json` 질병과 같다: `disease` + 해로움·`DiseaseResist`(Horrid Affliction 은 Script 이펙트라 AV 를 묻지 않는다) |
| `GRIM_K_MagicDamageKinetic` | `force` 이고 화염·냉기·전격·독·태양·신성·질병 태그가 없음 + 해로움·체력·저항 `None`·ValueModifier/DualValueModifier, 이미 원소 피해 키워드가 있으면 제외. Lost Grimoire 의 운반자 넷(Force Wave, Concussive Blast, Earthquake, Blasting Rune)이 모두 이 모양 |
| `SF_K_MagicSummonDaedra` ×2 | `pathofsorcery.json` 의 데이드라 두 줄(종족 `ActorTypeDaedra`, 그다음 `daedra` 태그). 제외 키워드에 `MagicSummonUndead`·`MagicSummonFamiliar` 를 더했다 |

`force` 태그(TAGS.md: 물리적 밀어냄·충격)는 Lost Grimoire 의 키네틱 피해와 뜻이 맞는다. 저항 조건을 빼면 더 붙는 것은 유령 화살(Trevas,
마법 저항·소환계) 하나뿐이라 넣지 않았다. 데이드라 줄의 제외 키워드: PoE 가 `SF_K_MagicSummonDaedra` 를 묻는 조건은 모두 같은 OR 묶음에서
`MagicSummonFamiliar` 와 `Fire/Frost/Shock` 도 묻으므로 그것을 이미 가진 주문은 잃는 것이 없다(CC 골든 세인트·다크 세듀서, 시커는 원작자가
Familiar 를 달았다 — 제외를 셋으로 줄이면 gain 이 23 → 95 로 늘지만 퍽 효과는 같다). Summoner 30/70 은 Undead 를 묻지 않는데, 언데드 소환이
이 퍽을 받지 않는 것은 바닐라와 같아서 데이드라 종족 해골(Rune Peryite Skeletons)·괴저 밴시는 언데드로 둔다. PoE 는 드레모라에서
`MagicSummonShock` 을 떼고 데이드라를 주지만, 어댑터는 키워드를 떼지 않으므로 다른 모드의 드레모라는 Shock 으로 같은 퍽을 받는다.

**측정.** TAKEALOOK 스캔(2026-10-14, 3983 주문, Lost Grimoire 포함, PoE 없음), `librarian-test -i <스캔> --catalog <카탈로그>
--adapters <vanilla.json + pathsofempyrean.json 만 든 폴더>`. 오프라인 평가기는 `PlanSource.keywordDefined` 를 주지 않아 모든 키워드를
정의된 것으로 보므로(헤더: 없으면 always), PoE 가 없는 로드오더에서도 `SF_K_MagicSummonDaedra` 줄이 평가된다.
agree 는 Lost Grimoire(와 TAKEALOOK 패치)가 직접 단 키워드와의 일치다.

| 줄 | agree | writes | gain | 누출 | 예 |
|---|---:|---:|---:|---:|---|
| `GRIM_K_MagicDamagePoison` | 29 | 88 | 130 | 0 | 스프리건 분사(바닐라·Dawnguard·Amalgam), Necrom, BSAssets 독 |
| `GRIM_K_MagicDamageSun` | 6 | 69 | 83 (holy 만으로 9) | 0 | Dawnguard 태양 불꽃·스텐다르의 오라, Lightpower, Stellaris, Lunaris, Ordinator 거룩한 손길 |
| `GRIM_K_MagicDamageDisease` | 6 | 15 | 22 | 0 | Vigilant 촉수의 벽, Necromancer's Magic 역병 가시, Ordinator 역신의 안개 |
| `GRIM_K_MagicDamageKinetic` | 8 | 0 | 0 | 0 | — (Lost Grimoire 운반자 8 모두 일치) |
| `SF_K_MagicSummonDaedra` (종족) | 0 | 23 | 23 | 0 | CC 골든 세인트·다크 세듀서 전사, Mihail 루이나크·화산재 골렘, Vigilant 베르나쿠스, Dragonborn 시커 |
| `SF_K_MagicSummonDaedra` (태그) | 23 | 2 | 2 | 0 | Mihail 콜드하버·데드랜드 베인킨 |

Lost Grimoire 운반자 중 줄이 고르지 않는 것: 독 36 중 7(Poison Seed 감시·Toxic Cloud 장판 생성·독 망토 같은 Script/SpawnHazard/Cloak 보조
이펙트, 숨김 이펙트), 태양 8 중 2(Sun Shard 장판 생성, 숨김 장판 이펙트). 이미 키워드를 가졌으니 손해는 없다.
gain 은 PoE 로드오더에서는 상한이다: PoE 가 플러그인에서 이미 붙이는 Dawnguard 태양 이펙트 4(GRIM_K Sun), Dragonborn 독 룬 2(GRIM_K Poison),
드레모라 군주·시커·화산재 수호자·Lost Grimoire 데이드라 8(SF_K)은 게임에서 agree 가 된다. 덤프에 주문서·마법부여가 없으므로 누출 0 은 하한이다(6-3절).
PoE 의 KID·SPID·SkyPatcher ini 는 주문·이펙트에 키워드를 주지 않는다(SkyPatcher 는 함성 이펙트에만 `PoE_K_MagicShoutScale*`·`MagicShout`).

**시전 퍽.** 다섯 학파의 Student 퍽(비용 -25/-50%)은 그 학파의 바닐라 반값 퍽 다섯을 `SpellHasCastingPerk` 로 묻고, 변화·소환(지속)·환영·회복
(위력) 배율도 같은 조건이다. Polymath 는 Novice00/Apprentice25, Advanced Study 는 스크립트가 `sp.GetPerk()`(반값 퍽)로 학파를 정한다.
PoE 는 바닐라 반값 퍽 25개를 덮어쓰지도 트리에 두지도 않으므로, 그 퍽들은 학파·등급 표시로만 쓰인다. 그래서 `vanilla.json` 의 시전 퍽 줄
(`requires: always`, PoE 에서도 켜짐)이 마법책 주문 중 퍽이 빈 것을 이 퍽들에 닿게 한다 — PoE 자신의 Telepathy 마법책 주문(반값 퍽 없음)이
정확히 그 경우다. Student: Destruction 의 위력 배율만 `EPMagic_SpellHasSkill(Destruction)` 이라 키워드도 퍽도 필요 없다.

**다루지 않는 것.**
- `PoE_K_MagicTransformation`(Metamorphosis, 지속 +50%): Lost Grimoire 의 변신 주문은 카탈로그에서 `creature` 이거나 태그가 없고, `transform`
  기법은 결속 무기·평형 같은 것이라(TAGS.md) 변신을 가를 태그도 스캔 필드도 없다
- `MAG_MagicDamagePoison/Sun`: 위 OR 묶음 이유. Mysticism 이 함께 깔리면 `adamant.json` 이 따로 켜져 붙인다
- `GRIM_K_RES_Cocoon`, `GRIM_K_CON_FamiliarBoosted`(Lost Grimoire 자체 기능), `PoE_K_DruidScaleDur`·`PoE_K_MagicDestructionNoScale`(PoE 표식,
  뒤의 것은 제외 조건), 함성·노래·무기·물약 키워드, `DLC2MagicAshSpawn`
- 주문 탭의 일반 `HasKeyword`: Ahzidal's Arcana(`DLC2dunKolbjornHelmPerk`, PoE 가 덮어씀)가 `MagicRune`·`MagicSummon*` 을, 여러 퍽이 `MagicShout` 를
  `EPMagic_SpellHasKeyword` 대신 `HasKeyword` 로 묻는다. CommonLib 은 `MagicItem` 이 `BGSKeywordForm::HasKeyword` 를 재정의한다고 적고,
  바닐라에서 룬·소환 키워드는 이펙트에만 있으므로 이펙트 키워드를 읽을 가능성이 높다 — 그렇다면 어댑터가 붙인 키워드도 보인다. 확인 필요(게임)
- 게임 중에 채워지는 FormList: Advanced Study 75개(`PoE_LIST_ALT_AdvancedStudy<학파>01..15`, 스크립트가 시전 횟수로 옮긴다), Spell Charging,
  Witch's Familiar·Spirit Guide 저장 주문, Blood Mage 장착 주문(7-1절과 같은 이유)
- PoE 가 Detect Life·Detect Dead 의 반값 퍽을 변화 → 환영(Adept50·Expert75)으로 옮긴다. PoE 로드오더의 스캔에서는 두 주문이 환영으로
  분류된다. 어댑터와 무관하고, 스캔이 플러그인에 적힌 대로 따른다
- 게임에서는 확인하지 않았다. PoE 로드오더로 스캔한 적도 없다

## 7. 붙이지 않는 것과 이유

| 키워드 | 이유 |
|---|---|
| `MAG_PerkScourgeExclude` | 원작은 Scourge 효과가 든 주문의 중복 방지용. 다른 독 주문에 붙이면 퍽에서 빠진다 |
| `MAG_MagicRegenSpell`, `MAG_MagicAttunementSpell` | 스캔 필드로 다른 회복 버프와 가를 수 없다(퀘스트 주문을 골랐다) |
| `RitualSpellEffect/Illusion` (Vokrii) | 뜻은 "양손 주문"이지만, 바닐라 마스터를 한손으로 바꾼 로드오더에서는 남는 양손 주문이 대부분 퀘스트·NPC용이고 이펙트를 한손 주문과 공유한다 |
| Ordinator `ORD_*` | Ordinator 자신의 표식 이펙트에만 있다 |
| `MagicThrallHealthRegen` | Ordinator Necromaster 보조 이펙트용. 영구 노예를 스캔 필드로 가를 수 없다 |
| `MAG_MagicDamageMagicka/Stamina/Bleed/Critical`, `MAG_StaggerSpellKeyword` | Adamant 의 무기·마법부여 분류 |
| `MagicBlessing`, 지팡이 마법부여 키워드, 퍽 자체 이펙트 키워드 | 주문이 아니다 |

아직 없는 것: 보조 이펙트 어댑터(Impact 등).

## 7-1. FormList (2026-09-30)

퍽이 `IsInList` 로 주문을 묻는 목록 중, **원작자가 레코드에 미리 주문을 적어 둔 분류 목록**만 채운다.
규칙은 목록에 이미 든 주문들의 공통점이다.

| 목록 | 쓰는 퍽 | 원래 든 것 | 규칙 | 새로 (TAKEALOOK) |
|---|---|---|---|---|
| Adamant `MAG_ConcentrationFireSpells` | Firebrand 가 **제외** | 불씨조각, 대 화염, 망토·벽 피해 | 파괴, 화염 피해, 집중 시전 또는 망토 | 30 |
| Adamant `MAG_ConcentrationShockSpells` | Unstable Current 가 **제외** | 번개조각, 번개 망토, 번개 폭풍 | 같은 규칙, 전기 | 36 |
| Vokrii `…NonReanimateConjurationSpellsWithMagnitudeList` | Grand Conjurer | 데이드라 추방·지배 | 소환계, Banish 또는 CommandSummoned | 10 |

채우지 않는 것: 게임 중에 **플레이어 상태**로 채워지는 목록(Ordinator Arcane Thesis — 플레이어가 고른
주문, PoS Witch's Familiar — 가장 자주 부른 소환, Spell Charging, Advanced Study 75개), 주문이 아닌 목록
(재료, 성소, 함성, 지팡이 마법부여, 무기), 퍽이 아니라 스크립트가 읽는 목록(Ordinator Natural Magic 제외).

목록에는 엔진의 `AddForm`(Papyrus 와 같은 경로라 세이브에 남을 수 있다) 대신 목록 레코드의 배열(`forms`)에
메모리에서만 넣는다. 목록은 **게임 시작(kDataLoaded) 때만** 고친다 — 배열이 커지면 옮겨질 수 있고, 게임 중에는
퍽 조건이 다른 스레드에서 목록을 읽는다. 게임 중 스캔은 목록 계획만 보고서에 남기고(`listsDeferred`), 목록은
다음 게임 시작에 따른다. 같은 목록을 가리키는 줄이 둘이면 한 주문이 두 줄의 `gain` 에 모두 셀 수 있다(실제 쓰기는
한 번). 위 표의 30·36 은 그렇게 센 값이다.

## 7-2. 시전 퍽 (2026-10-08)

주문의 반값 시전 퍽(CommonLib `castingPerk`, SPEL 의 Half-cost Perk)은 학파와 등급 표시다. 바닐라 비용 퍽은 이것을
읽는다 — `DestructionApprentice25` 는 `ModSpellCost ×0.5`, 조건 `SpellHasCastingPerk DestructionApprentice25`(Skyrim.esm
레코드로 확인). College of Winterhold 의 파괴·소환 퍽도 `SpellHasCastingPerk` 로 등급별 바닐라 퍽을 묻는다. 퍽이 빈 주문은
이 퍽들을 하나도 받지 못한다.

`vanilla.json` 의 `{ "castingPerk": "vanilla", "spell": { "taughtByTome": true } }` 줄이 마법책이 가르치는 주문 중 퍽이 빈
것에 카탈로그 학파·등급의 바닐라 퍽을 준다(25개 표: `include/librarian/VanillaCastingPerks.h`, 스캐너의 등급 판정도
같은 표를 읽는다). 등급은 카탈로그의 것 — 퍽도 없고 첫 이펙트의 최소 숙련도가 0 인 주문은 트리가 Novice 로 두므로 Novice
퍽을 받는다.

게임에서: 주문 레코드의 포인터 하나라 FormList 와 달리 게임 중 스캔 뒤에도 바뀐다. 퍽이 비어 있을 때만 주고, 우리가 준
퍽이 그대로일 때만 거둔다(다른 모드가 그새 퍽을 적었으면 손대지 않는다). 손으로 시전하는 주문(`SpellType::kSpell`)만.
보고서에 `castingPerkAdded/Kept/Removed/Failed`(Failed: 풀리지 않은 폼, 손으로 시전하지 않는 주문).

측정(2026-10-08 TAKEALOOK 스캔, 오프라인): 마법책 주문 중 퍽이 있는 1497, 새로 받는 20 — 바닐라 유령 화살·드래곤 사제
소환, CC 애완동물 부르기, Glenmoril 망령 4, BSAssets 자물쇠 열기 3, 동료 모드 주문 등. 퍽이 빈 주문 대부분(파괴 636 등,
6-2절)은 마법책이 없는 보스·NPC·퀘스트 주문이라 받지 않는다. 비용 0 인 주문(애완동물 부르기)은 받아도 달라지지 않는다.
게임에서는 확인하지 않았다.

## 8. 게임에서 확인한 것 (2026-09-30)

TAKEALOOK(Adamant 5.9.2, Mysticism 2.4.2)에서:

- 게임 시작 때 키워드 189개, 1670 ms(로딩 중). 전체 스캔 뒤 182개 유지·7개 회수, 573 ms
- vanilla·adamant 파일만 켜짐(나머지 퍽 모드 미설치). `MAG_MagicWallSpell` 은 Mysticism 2.4.2 에 키워드가
  없어 쉼(`ran=false`)
- 오프라인에서 못 보던 마법부여·스크롤 공유 이펙트와 조건 이펙트가 걸러짐(구속 무기 11, 망토 2, 독 3 등)
- **전제 확인**: 엔진은 주문 쪽 퍽 조건에 답할 때 이펙트 키워드를 읽는다. 화산재 수호자 소환(원래 소환 분류
  키워드 없음)이 `MagicSummonFire` 를 받자 Adamant 암흑 서약(`MAG_DarkOath20`, 소환 지속 +50%)이 적용됐다
