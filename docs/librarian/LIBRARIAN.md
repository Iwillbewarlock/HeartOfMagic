# 사서 (Librarian)

> 설치된 모든 마법 모드의 주문을 호환 패치 없이 자동으로 분류해 `spell_catalog.json` 으로 남기는 층.
> 무엇을 왜 만드는지는 `Concept.md`, 진행 순서는 `ToDo.md`, 실측 숫자는 `MEASURED.md`,
> 태그 정의는 `TAGS.md`.

---

## 1. 한눈에

```
스캔 (SpellScanner)                사서 (Librarian)                    손님
spell_scan_output.json  ──▶  룰 매칭 + 축 도출  ──▶  spell_catalog.json  ──▶  트리 / 퍽 모드
                              librarian/*.json
```

스캔이 끝나면 **자동으로** 돈다. 버튼이 따로 없다. 스캔 진입점 두 곳
(`RunScanToFile`, UI 의 `OnScanSpells`)이 같은 `Librarian::BuildAndWriteCatalog` 를 부르므로
Papyrus 로 돌리든 패널의 Scan 버튼을 누르든 같은 카탈로그가 남는다.

사서가 실패해도 스캔은 살아남는다. 예외를 잡아 로그만 남기고 빈 경로를 반환한다.

## 2. 파일

| 경로 | 내용 |
|---|---|
| `SKSE/Plugins/SpellLearning/librarian/*.json` | 분류 룰. 파일명 순으로 병합 |
| `SKSE/Plugins/SpellLearning/spell_catalog.json` | 산출물. `persistentId` 키 |
| `src/librarian/LibrarianRules.cpp` | 룰 로드·검증·병합 |
| `src/librarian/LibrarianClassify.cpp` | `Classify(spell, rules) -> TagSet`. 순수 함수 |
| `src/librarian/LibrarianCatalog.cpp` | 네 축 도출, 카탈로그 생성·읽기·쓰기 |
| `include/librarian/TagVocabulary.h` | 어휘. `modules/tagVocabulary.js` 와 수동 동기화 |
| `tools/librarian-test` | 게임 밖 하네스. 커버리지·정밀도·재현율·축 채점 |

`LibrarianRules.cpp` 와 `LibrarianClassify.cpp` 는 **RE/SKSE 타입을 쓰지 않는다.** 그래서 같은
소스가 플러그인과 하네스 양쪽에 컴파일되고, 하네스가 내는 숫자가 곧 플러그인의 숫자다.

## 3. 룰

```json
{ "match": { "mgefKeyword": "MagicDamageFire" }, "add": { "elements": ["fire"] } }
```

**룰은 코드가 아니라 데이터다.** 사용자나 다른 모더가 패치 없이 `90_user.json` 을 얹어 분류를
추가할 수 있다. 이게 호환성 전략의 핵심이다.

### 매칭 조건

| 키 | 대상 | 비고 |
|---|---|---|
| `spellKeyword` `spellKeywordPrefix` `spellKeywordSuffix` | SPEL 키워드 | |
| `mgefKeyword` `mgefKeywordPrefix` `mgefKeywordSuffix` | MGEF 키워드 | |
| `archetype` | 이펙트 archetype | `SummonCreature` 등 |
| `primaryAV` `secondaryAV` `resistance` | 이펙트 액터값 | **`FireResist` 꼴** (`ResistFire` 아님) |
| `magicSkill` | 이펙트 유파 | |
| `hostile` `detrimental` | 이펙트 플래그 | 없으면 조건 없음, `false` 와 다르다 |

한 룰의 조건은 모두 성립해야 한다(AND). **이펙트 레벨 조건은 하나의 같은 이펙트에서** 성립해야
하고, 접두사와 접미사도 **같은 키워드 하나**에서 성립해야 한다. 서로 다른 이펙트에서 긁어모은
archetype 과 resistance 는 그 룰이 말하는 것의 증거가 아니기 때문이다.

### 증거 등급 (`tier`)

파일 단위로 선언한다. 룰마다 반복할 필요가 없다.

| tier | 뜻 | confidence |
|---|---|---:|
| `mgef` (기본) | MGEF 구조. **모든 로드오더에 있다** | 1.0 |
| `framework` | KIT · OCF · ADAR · NSV 키워드. 그 모드가 깔려야 있다 | 0.8 |

`confidence` 는 **증거가 남의 환경에도 존재할 가능성**이지 태그가 맞을 확률이 아니다.
정확도 숫자는 `MEASURED.md` 에 있다. 한 항목의 confidence 는 두 축 중 **약한 쪽**을 따른다.

### 어휘 게이트

어휘(`TagVocabulary.h`) 밖의 태그는 로드 시 경고와 함께 버려진다. 사용자 룰 파일의 오타가
아무도 번역할 수 없는 태그를 만들어내지 못한다.

```
librarian-test --check-vocab -r <룰 디렉터리> -j <tagVocabulary.js>
```

룰의 태그가 어휘 안에 있는지, 그리고 C++ 목록과 JS 거울이 어긋나지 않았는지 검사하고
어긋나면 종료 코드 1 을 낸다.

## 4. 카탈로그

```json
{
  "version": 1, "vocab": "tags-v1", "generated": "2026-09-10T02:17:36Z",
  "spells": {
    "Skyrim.esm|0x10FD5F": {
      "school": "destruction", "tier": "expert",
      "casting": "fireforget", "targeting": ["actor", "aoe"],
      "elements": ["fire"], "techniques": [],
      "source": { "elements": "mgef", "techniques": "" },
      "confidence": 1.0
    }
  }
}
```

키는 `persistentId` 라 로드오더가 바뀌어도 안전하다.

### 네 축은 룰이 아니라 도출이다

`school` `tier` 는 스캐너 값을 소문자화한 것, `casting` 은 `"Fire and Forget"` → `fireforget`,
`targeting` 은 delivery 와 이펙트 area 에서 나온다. **`Aimed` 는 `location` 이 아니라 `actor` 다** —
Firebolt · Flames · Fear · Ice Spike 로 실측했고 초기 추측이 틀렸다.

정답셋 104쌍 대비 일치율 (2026-09-10):

| 축 | 일치율 | |
|---|---:|---|
| school | 97.1% | |
| casting | 95.2% | |
| tier | 78.8% | HoM 의 티어 판정(하프코스트 퍽·minimumSkill)과 SR 라벨은 **정의가 다르다.** 불일치 22건이 양방향으로 흩어져 있어 도출 버그가 아니다 |
| targeting | 66.3% | 다중 라벨이라 완전일치 기준이 엄격하다 |

**targeting 을 올리려다 실패한 기록**: 클록에 `actor`(주변을 태우니까), 클록·해저드에 `aoe`
(해저드 반경은 해저드 레코드에 있어 이펙트 area 가 0이니까)를 더해봤다. 기계적으로는 말이 되는데
**66.3% → 61.5% 로 떨어졌다.** 열두 건쯤 고치고 그보다 많이 깨뜨렸다. SR 이 `self` 와 `aoe` 를
그렇게 겹쳐 쓰지 않기 때문이다. 되돌렸고 `LibrarianCatalog.cpp` 주석에 남겼다.

## 5. 검증

게임 없이:

```
librarian-test -i <덤프> -r <룰> -a <정답셋>            # 전체
librarian-test -i <덤프> -r <룰> -a <정답셋> -t mgef    # 프레임워크 없는 환경
librarian-test --catalog <카탈로그> -a <정답셋>          # 게임이 쓴 카탈로그를 채점
```

**2026-09-10 확인**: 게임이 쓴 카탈로그와 하네스가 만든 카탈로그가 1440건 전부 항목 단위로
동일했다(`generated` 만 다름). 게임 안 경로와 게임 밖 경로가 같은 결과를 낸다.

## 6. 퍽 모드 호환 보정

퍽 모드는 바닐라 키워드로 조건을 건다. 바닐라·Adamant·Ordinator 전부
`HasMagicEffectKeyword MagicDamageFire` 를 묻는다. 모드가 만든 화염 주문의 MGEF 에 그 키워드가
없으면 화염 강화 퍽이 통째로 안 먹는다. 사서는 그 이펙트가 화염이라는 걸 아니까 키워드를 채워준다.
**패치를 쓰는 게 아니라, 모두가 이미 읽는 키워드를 채우는 것이다.**

`kDataLoaded` 직후 `OnDataLoaded` 에서 한 번 돈다. 어댑터는
`librarian/adapter_vanilla_keywords.json`:

```json
{ "tag": "fire", "match": { "detrimental": true }, "keyword": "MagicDamageFire" }
```

`match` 는 분류 룰과 같은 조건 구조다. **태그만 보면 안 된다** — 화염 아트로나크 소환도 `fire`
태그를 받으므로, 조건이 없으면 소환 주문에 `MagicDamageFire` 가 붙어 화염 강화 퍽이 걸린다.

### 이펙트 단위로 분류한다

카탈로그를 읽지 않는다. **카탈로그는 주문 단위인데 퍽은 이펙트에게 묻기 때문이다.** 주문에
이펙트가 여럿이면 어느 이펙트에 붙일지 카탈로그가 알 수 없다. 그래서 로드오더의 모든
`EffectSetting` 을 하나씩 분류한다. 카탈로그도 사전 스캔도 필요 없다.

부작용으로 `spellKeyword` 로 매칭하는 룰(`10_nsv.json` 18개)은 이 경로에서 발동하지 않는다.
의도한 것이다 — NSV 태그는 주문을 설명하고, 붙이는 키워드는 이펙트의 것이다.

### 안전장치

- 추가만 한다. 삭제 없음
- 로드오더에 이미 있는 키워드만 쓴다. 없으면 그 어댑터는 비활성이고 로그에 남는다
- 이미 가진 이펙트는 건드리지 않는다
- 사용자가 지정한 플러그인은 통째로 건너뛴다
- `config.json` 의 `vanillaKeywordPatch.enabled` 로 전부 끈다 (없으면 켜짐)
- 런타임 폼 데이터라 **세이브에 안 남는다.** 모드를 빼면 원상복구된다

```json
{ "vanillaKeywordPatch": { "enabled": true, "excludePlugins": ["SomeMod.esp"] } }
```

`AddKeywords` 의 반환값은 항상 `true` 라 믿을 수 없다
(`commonlibsse-ng/src/RE/B/BGSKeywordForm.cpp:39`). 키워드 개수를 전후로 재서 판정한다.

### 측정에 미치는 영향

**패치가 돈 뒤의 스캔은 플러그인 파일에 없는 키워드를 본다.** 모드 화염 이펙트에
`MagicDamageFire` 가 채워지면 다음 스캔에서 `00_mgef.json` 룰이 매칭되고, 카탈로그의 `source` 가
`framework` 에서 `mgef` 로, `confidence` 가 0.8 에서 1.0 으로 바뀐다. 커버리지도 올라간다.

그래서 스캔 덤프에 `keywordPatchApplied` 를 찍는다. `librarian-test` 가 그런 덤프를 읽으면
경고를 낸다. **`MEASURED.md` 의 숫자는 패치 이전 덤프 기준이다.**

## 7. 아직 없는 것

- **텍스트 룰 · LLM 폴백 (M7)** — 태그 0개가 (a) 18.6% / (b) 6.9%. Stoneflesh 와 Ironflesh 는
  MGEF 구조가 완전히 같아 `earth` 와 `metal` 을 구조로는 가릴 수 없다
- **손님 어댑터 (M5 트리, M6 퍽 키워드 보정)** — 카탈로그를 읽는 쪽이 아직 없다.
  지금은 파일만 남긴다
- 룰은 스캔마다 다시 읽는다. 스캔이 드물어 문제가 없지만 캐시할 여지는 있다
