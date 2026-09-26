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

**효과가 하나도 없는 스캔은 카탈로그를 덮어쓰지 않는다(2026-09-27).** 스캔 필드 설정에서 효과가 빠진
스캔을 분류하면 모든 주문이 태그 없이 나온다. 실제로 1,440개 전부가 원소·시전·대상 없이 덮어써진
카탈로그가 발견됐다. 이제 어떤 주문에도 효과 배열이 없으면 경고만 남기고 기존 카탈로그를 둔다.

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
| `spell` | 주문 자체 | `persistentId` 하나 또는 목록(`"Natura.esp|0x000B2C"`). 수동 룰용 |
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
| `manual` | 사람이 주문을 보고 붙인 태그(`spell` 룰). 그 주문이 있는 곳엔 항상 있다 | 1.0 |

`confidence` 는 **증거가 남의 환경에도 존재할 가능성**이지 태그가 맞을 확률이 아니다.
정확도 숫자는 `MEASURED.md` 에 있다. 한 항목의 confidence 는 두 축 중 **약한 쪽**을 따른다.

### 수동 룰 (`80_manual.json`, 2026-09-27)

기록만으로는 알 수 없는 주문이 있다. 스크립트로 평범한 체력 피해를 주는 혈·물·바람 마법, 원소
키워드도 저항도 없이 스크립트로만 원소를 쓰는 주문이다. 이런 주문은 `spell` 조건으로 주문을 직접
집어 태그를 **더하거나**(`add`) 기록이 잘못 붙인 태그를 **뺀다**(`remove`). 빼기는 모든 룰이 더한
뒤에 적용되므로 파일 순서와 상관없이 이긴다. `persistentId` 는 플러그인 안의 ID라 로드오더와 무관하고,
설치되지 않은 주문은 그냥 매칭되지 않는다.

`80_manual.json` 은 1,440개 주문 로드오더의 스캔(2026-09-21)을 보고 만든 것이다(51개 주문이 바뀐다):

- 혈: AncientBlood 의 흡혈의 오로라, 피의 불꽃 소용돌이, 박쥐의 연회, Occult Cradle 의 내면의 힘,
  Mysticism 의 Blood Lance(피의 창)와 Vampiric 다섯 개(Touch·Bolt·Grasp·Blast·Rune - 한국어 번역에서는
  "엔트로피"라 헷갈리지만 원문은 Vampiric, 원작자 설명도 산 자의 체력을 흡수하는 흡혈 마법이다)
- 물: Natura 의 물 튀김(같은 세트에서 혼자 물 키워드가 없다)
- 흙: Natura 의 돌 던지기(같은 이유)
- 바람: Lost Grimoire 의 울부짖는 돌풍, 속박 풀린 빙결(차가운 바람)
- 원소 없던 파괴 마법: 혼돈의 용광로(화염·냉기·전격), Lost Grimoire 의 극지 요새·서리 장막(냉기),
  폭풍 균열·전기장(전격), 살라맨더 비늘(화염), 죽음의 손길(영혼)
- 괴저: Necromancer's Magic 의 해골 무덤과 부정한 저주 둘(양들의 침묵, 어둠의 무게)
- 흡수(구조 룰, 모든 모드): 체력·지구력 흡수(`archetype` Absorb + `primaryAV` Health/Stamina)는 혈,
  매지카 흡수는 비전. 이 로드오더에서는 10개(AncientBlood 연쇄하는 고통·정수 흡수, Lost Grimoire
  감전사, Mysticism 의 흡수 주문들)가 바뀐다. 이미 혈인 주문(Vampiric 등)은 그대로다
- 빼기(기록이 잘못 붙인 것): 바닐라 화염 폭풍의 냉기, Natura 룬 여섯 개의 화염(룬이 공유하는 폭발
  효과 탓)과 바람의 룬의 물, Natura 바람 손 주문 세 개의 흙, 돌풍의 전격, Arclight 뇌광의 벽의 화염,
  Mysticism 폭풍격류의 냉기, 소환·무기 생성 주문 네 개(불꽃심장, 파이어-브랜드, 아이스-브레이커,
  과급 도깨비불)의 영혼

이 파일을 적용하면 원소 없는 파괴 마법이 8 → 0 이 되고, 위 51개 말고는 바뀌지 않는다(하네스로
전후 카탈로그 비교). 사용자가 직접 추가할 때는 이 파일이 업데이트로 덮이지 않도록 `90_user.json`
에 두면 된다. 판단은 번역 이름이 아니라 원문 이름과 원작자 설명으로 한다. 일부러 뺀 것: Natura 의
바위 주문들은 "출혈 피해"를 주지만 흙 마법이다. Creation Club 주문 팩(`ccBGSSSE014-SpellPack01.esl`)의
Oblivion 체력 흡수 네 개(Choking Grasp, Strangulation, Hangman's Noose, Touch of Death)는 질식시켜
흡수하는 컨셉이라 혈도 바람도 아닌 무속성으로 둔다(흡수 룰이 붙인 혈을 `remove` 로 뗀다). 이름만 폭풍·소용돌이인 주문(번개 폭풍, 얼음 폭풍, 냉기 화염
폭풍우, 피의 소용돌이 등)에는 바람을 붙이지 않는다 - 바람을 부리는 마법이 아니라 제 원소가 휘몰아치는
모양일 뿐이다.

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

## 6. 퍽 모드 호환 보정 — 제거됨 (2026-09-21)

MGEF 에 빠진 바닐라 `Magic*` 키워드를 채워 넣던 기능(`LibrarianKeywordPatch.cpp`,
`adapter_vanilla_keywords.json`)은 이 포크에서 **뺐다.** 이유는 둘이다.

- **범위 밖이다.** 키워드 주입은 나중에 만들 별도 모드의 일이고, 이 포크는 스캔까지만 한다
- **처음 게임에서 돌려 보니 엉뚱한 곳에 붙었다.** `MagicNightEye` 가 생명·시체 감지 이펙트 82개에,
  `MagicRune` 이 눈보라·보호의 원 같은 SpawnHazard 이펙트 31개에, `MagicArmorSpell` 이 퍽 보조용
  내부 이펙트에 붙었다. 기본값이 켜짐이었으므로 배포하면 남의 게임에서 퍽이 엉뚱한 주문에 걸린다.
  게다가 보정이 돈 뒤의 스캔은 플러그인 파일에 없는 키워드를 적게 되어 "있는 그대로 받아 적는다"는
  스캔 원칙과 부딪힌다

코드는 `feature/librarian` 브랜치의 커밋 `c7e7670` 에 남아 있다. 별도 모드를 만들 때 참고하되,
위 오탐 사례를 먼저 풀어야 한다. 옛 덤프에 찍힌 `keywordPatchApplied` 는 `librarian-test` 가 여전히
읽고 경고한다.

## 7. 아직 없는 것

- **텍스트 룰 · LLM 폴백 (M7)** — 태그 0개가 (a) 18.6% / (b) 6.9%. Stoneflesh 와 Ironflesh 는
  MGEF 구조가 완전히 같아 `earth` 와 `metal` 을 구조로는 가릴 수 없다
- **손님 어댑터 (M5 트리, M6 퍽 키워드 보정)** — 카탈로그를 읽는 쪽이 아직 없다.
  지금은 파일만 남긴다
- 룰은 스캔마다 다시 읽는다. 스캔이 드물어 문제가 없지만 캐시할 여지는 있다
