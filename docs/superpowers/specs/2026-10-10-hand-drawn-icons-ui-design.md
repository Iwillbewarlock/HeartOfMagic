# Heart of Magic 패널 — 손그림 아이콘과 틀 정리 — 설계

날짜: 2026-10-10. 상태: **설계 검토 중**.
근거: 개발 하네스(`dev-harness.html`) 헤드리스 스크린샷, Arcane·Modern Dark 의 나무·스캔·설정 화면(2026-10-10).

## 1. 목적

넥서스 공개 배포를 앞두고, 패널이 "AI 로 만든 화면"처럼 보이는 흔적을 걷어낸다. 나무 그림(Arcane 의 양피지·잉크)은
이미 손맛이 있으므로 거의 두고, 나무를 둘러싼 버튼·메뉴·설정 화면을 고친다.

지금 AI 티가 나는 곳(눈에 띄는 순서):

1. 아이콘 자리를 글자로 채운 대괄호 표시: `[K] HOTKEY`, `[S] SAVE SETTINGS`, `[*] [*] SCAN SPELLS`, `[ ]`, `X` 등.
2. 모든 이름표가 대문자 + 넓은 자간이라 강약이 없음.
3. Modern Dark 설정 화면: 남색 둥근 카드, 노란 그라데이션 저장 버튼, 초록 둥근 스위치, 빨간 "ADVANCED OPTIONS" 알약.
4. 버튼·필터·칩이 전부 같은 테두리 상자.

## 2. 결정 (2026-10-10 사용자 답)

| 질문 | 답 |
|---|---|
| 범위 | 모든 디자인의 공통 틀 전부 |
| 방향 | 대괄호 자리를 디자인별 손그림 아이콘으로 (B) |
| 아이콘 출처 | game-icons.net 에서 모양을 고르고, Gemini 가 디자인 질감으로 다시 그림 |
| 틀 정리(대문자·상자·스위치) | 아이콘과 함께 함 |

**규칙 예외**: 평소 디자인 작업은 디자인 계층(토큰, `themes/design-*.css`) 안에서만 한다. 이번에는 사용자가
"공통 틀 전부"를 골랐으므로 공통 스타일시트와 마크업도 고친다. 게임 동작, 나무 만들기, 설정 값의 의미는 바꾸지 않는다.

## 3. 아이콘 목록 (26)

| # | 뜻 | id | 지금 | 자리 |
|---|---|---|---|---|
| 1 | 주문 스캔 | `scan` | `[*]` | 스캔 버튼 (index.html, 스캐너 모듈이 다시 씀) |
| 2 | 스캔 내보내기 | `export` | `[S]` | 스캔 화면 |
| 3 | 나무 만들기 | `build` | `[B]` | 스캔 화면 2곳 |
| 4 | 나무 적용 | `apply` | `[A]` | 스캔 화면 2곳 |
| 5 | 나무 지우기 | `clear` | `[X]` | 스캔 화면 3곳 |
| 6 | 다시 보기 | `replay` | `[R]` | 스캔 화면 |
| 7 | 잠금 적용 | `locks` | `[L]` | 선행 마스터 |
| 8 | 설정 저장 | `save` | `[S]` | 설정 아래 |
| 9 | 기본값으로 | `reset` | `[R]` | 설정 아래 2곳 |
| 10 | 불러오기 | `import` | `[v]` | 아래 막대 |
| 11 | 차단 목록 | `blacklist` | `[-]` | 스캔 필터 |
| 12 | 허용 목록 | `whitelist` | `[+]` | 스캔 필터 |
| 13 | 프리셋 저장 | `preset-save` | `[+]` (번역 문구 안) | 프리셋 줄 2곳 |
| 14 | 단축키 | `hotkey` | `[K]` | 설정 묶음 머리 |
| 15 | 나무 보기 | `tree-view` | `[*]` | 설정 묶음 머리 |
| 16 | 화면 표시 | `display` | `[~]` | 설정 묶음 머리 |
| 17 | 개발자 | `debug` | `[D]` | 설정 묶음 머리 |
| 18 | 진행 | `progression` | `[^]` | 설정 묶음 머리 |
| 19 | 일찍 배우기 | `early` | `*` | 설정 묶음 머리 |
| 20 | 주문서 배우기 | `tome` | `[i]` | 설정 묶음 머리 |
| 21 | 나무 생성 | `tree-gen` | `[T]` | 설정 묶음 머리 |
| 22 | 선행 마스터 | `prereq` | `[+]` | 설정 묶음 머리 |
| 23 | 현재 설정 (도움말) | `howto-settings` | `[i]` (번역 문구 안) | 배우는 법 창 |
| 24 | 경험치 얻기 (도움말) | `howto-xp` | `[^]` (번역 문구 안) | 배우는 법 창 |
| 25 | 전체 화면 | `fullscreen` | `[ ]` | 머리 막대 |
| 26 | 닫기 | `close` | `X`, `×` | 머리 막대, 상세·도움말 창 |

구현 전에 `grep` 으로 다시 세어 빠진 자리가 없는지 확인한다(JS 가 만드는 마크업 포함).

## 4. 디자인별 처리

| 디자인 | 아이콘 세트 | 만드는 법 |
|---|---|---|
| Arcane (기본) | 잉크 | Gemini 가 펜 잉크화로 26장 |
| Night Grimoire | 금박 | 잉크 세트를 금색(`--book-gold` 계열)으로 구움. 따로 그리지 않음 |
| Chalkboard | 분필 | Gemini 가 칠판 분필로 26장 |
| Modern Dark | 선 | game-icons 원본 SVG, 탁한 금색 한 색 |
| Classic | 선 | 같은 SVG, 무채색 |
| 애드온 디자인 | 선 (기본값) | 아이콘을 지정하지 않으면 Modern 의 선 세트를 받음 |

Gemini 가 그리는 양은 52장이다.

## 5. 게임 안에 넣는 방식

- **그림**: 디자인마다 PNG 한 장(아틀라스). 칸 48×48, 투명 바탕, 26칸(가로 8 × 세로 4). 화면에는 24px 로 보인다.
  위치: `themes/arcane/icons.png`, `themes/nightgrimoire/icons.png`, `themes/chalkboard/icons.png`, 선 세트는 `themes/icons-line.png`.
- **마크업**: 지금의 `.btn-icon` / `.settings-block-icon` 칸을 그대로 쓰고 `data-icon="<id>"` 를 붙인다.
  머리 막대의 `[ ]` 와 `X` 는 같은 칸으로 감싼다. JS 가 만드는 버튼(`'[*] ' + t(...)`)도 같은 칸을 쓰게 고친다.
- **CSS**: 칸 안 글자는 `font-size:0` 으로 숨기고, 아틀라스를 배경으로 깔고 `[data-icon=…]` 마다 배경 위치를 정한다.
  아틀라스 경로는 각 디자인의 스타일시트(Arcane: `design-arcane.css`, 프리셋 JSON 디자인: 그 `css` 필드)가 정한다.
  지정이 없으면 공통 스타일시트의 선 세트가 깔린다.
- **번역 문구**: `presets.save`, `settings.presets.save`, `howto.currentSettings`, `howto.gainingXp` 의 `[+] ` 같은
  머리글을 13개 언어 파일(en, de, es, fr, it, ja, ko, pl, pt-br, ru, tr, zh-cn, zh-tw 중 해당 키가 있는 것)에서 지우고,
  `.js` 미리 불러오기 파일을 `build-preloads.js` 로 다시 만든다. 아이콘은 마크업 쪽에 붙인다.
- **성능**: 아틀라스는 처음 한 번 불러오고 DOM 배경으로만 쓴다. 캔버스에 매 프레임 그리지 않는다.
  필터·그림자·그라데이션을 새로 넣지 않는다.
- **Ultralight**: `var` 만, 새 DOM API 없음. CSS 변수 안에 `url()` 을 넣지 않는다(디자인 스타일시트가 직접 경로를 씀).

## 6. 틀 정리 규칙

1. **대문자**: 앱 이름, 맨 위 메뉴(돌아가기·스캔·설정), 탭(쉬움·복잡)에만. 설정 묶음 제목은 작은 대문자(small-caps),
   나머지 이름표·버튼은 보통 글자. 대소문자 없는 언어는 자간만 정리.
2. **버튼 세 단계**: 주요(스캔, 나무 적용, 저장) = 아이콘 + 아래 줄 + 강조색. 보통 = 아이콘 + 글자.
   위험(지우기, 기본값으로) = 붉은 기운의 글자. 그라데이션·알약·그림자 없음.
3. **켜고 끄기**: 둥근 스위치(`.toggle-switch`)를 네모 체크 모양으로. 테두리·채움 색은 디자인 토큰.
4. **설정 카드**: 둥근 남색 상자 대신 위쪽 가는 줄로 묶음을 나눔. 두 단 배치는 유지.
5. **Modern Dark**: 노란 그라데이션 → 탁한 금색 한 색, 빨간 "ADVANCED OPTIONS" 알약 → 글자.

## 7. 제작 과정

1. **모양 고르기**: 26가지 뜻마다 game-icons.net 후보 2~3개를 골라 견본 페이지(아티팩트) 하나로 보여 주고,
   사용자가 고르면 확정. 아이콘 이름과 작가를 함께 적는다.
2. **Gemini 지시서** (`antigravity-offload` 스킬, agy `image-generator`):
   고른 SVG 를 PNG 로 렌더해 참고 그림으로 넣는다. 잉크 세트 조건: 1:1, 아이콘 하나, 글자 없음, 빛번짐·그라데이션 없음,
   단색 바탕(지우기 쉽게), 같은 선 굵기. 첫 합격 아이콘을 나머지의 화풍 기준으로 함께 넣는다. 분필 세트도 같은 방식.
3. **검수**: 52장을 모두 연다. 참고 모양과 같은가, 선 굵기, 글자·빛번짐 섞임, 24px 로 줄였을 때 알아볼 수 있는가.
   어긋나면 무엇이 틀렸는지(모양, 선, 질감) 적어 다시 그리게 한다. 손보는 단계를 처음부터 일정에 넣는다.
4. **가공**: 바탕 제거 → 48px 로 축소 → 아틀라스로 묶기 → Night Grimoire 금색 굽기. 일회성 작업이라 임시 폴더에서
   하고 저장소에는 결과 PNG 만 넣는다(저장소에 Python 도구를 들이지 않음).
5. **크레딧**: `CREDITS.md`(없으면 새로) 와 넥서스 설명에 game-icons 작가와 CC BY 3.0 표기.

## 8. 확인

- 화면 3개(나무, 스캔, 설정) × 디자인 5개를 바꾸기 전·후로 헤드리스 Chrome 스크린샷. 한국어 화면 한 번.
- 아이콘 26자리 모두 그림이 나오는지(빈 칸·대괄호 잔존 0), 애드온 디자인처럼 아이콘을 지정하지 않은 경우 선 세트가 나오는지.
- 다른 디자인 애드온의 기존 `.btn-icon` 스타일이 깨지지 않는지(칸은 그대로 남김).
- 게임 안 확인은 프레임 측정(`[Perf]` 줄)이 필요할 때만 짧게 요청.

## 9. 함께 고칠 작은 버그

스캔 화면 머리에 "undefined spells scanned across 0 schools" 가 보인다(테스트 데이터를 불러온 직후). 날것의 코드 글자가
플레이어에게 보이면 안 된다는 규칙에 걸리므로 따로 작은 수정으로 고친다. 원인은 구현 때 확인.

## 10. 범위 밖

- 설정 설명을 아래 안내 줄로 옮기는 MCM 식 재배치.
- 나무 안의 학파 칩(ALTER. CONJ. …)과 노드 모양.
- 책장 넘기기 같은 화면 흐름 변경.

## 11. 위험

- Gemini 그림 26장의 화풍이 흔들릴 수 있음 → 첫 합격작을 기준 그림으로 고정, 검수에서 다시 그리기.
- game-icons 모양이 다른 인디 게임과 겹쳐 낯익을 수 있음 → 다시 그리는 과정에서 질감이 바뀌어 덜함.
- 24px 에서 잉크 질감이 뭉개질 수 있음 → 검수에 "24px 로 줄여 보기"를 넣음.

## 12. 조사 후 바뀐 점 (2026-10-11, master 기준 코드 조사)

아래가 위 절들보다 우선한다.

1. **작업 브랜치**: `feature/spell-card` 는 10-09 이후 멈춰 있고 master 와 갈라졌다(95/25). 작업은 master 에서 딴
   `feature/hand-drawn-icons` (worktree `.claude/worktrees/hand-drawn-icons`) 에서 한다. 줄 번호 대신 id·선택자로 찾는다.
2. **§5 아이콘 칸**: 동작 버튼 10개(`buttons.scanSpells`, `exportScan`, `buildTree`, `applyTree`, `clearTree`, `replay`,
   `filter.blacklist`, `filter.whitelist`, `prm.applyLocks`, `prm.clearLocks`)는 `data-i18n-html` 이 버튼 innerHTML 을 번역 문구로
   통째로 바꾸고, 그 문구 안에 `<span class="btn-icon">[X]</span>` 가 들어 있다. 그래서 13개 언어 문구에서 이 span 을 지워
   **평문으로 만들고**, 번역 속성은 버튼 안 글자 칸(`<span class="hom-label">`)으로 옮긴다. 아이콘 칸은 마크업에만 둔다.
   번역자는 아이콘을 다시 보지 않는다.
3. **§5 클래스**: `.btn-icon` 은 재사용하지 않는다. `styles-skyrim.css` 의 `.btn-icon` 규칙이 테두리 상자를 그려 "상자 반복"의 원인이었다.
   새 칸은 `<span class="hom-icon" data-icon="<id>" aria-hidden="true">` 이다.
4. **§5 그림 형식**: 아틀라스 대신 **아이콘마다 48px PNG 한 장** (`themes/<세트>/<id>.png`). Ultralight 에서 스프라이트의
   `background-position` 은 확인되지 않았고, `url() center/contain no-repeat` 는 이미 쓰고 있어 확인된 방식이다.
   선 세트도 SVG 를 그대로 쓰지 않고 PNG 로 굽는다.
5. **§5 세트 선택**: 디자인 데이터에 `icons` 경로(예 `"themes/arcane/icons/"`)를 두고, 새 모듈 `modules/homIcons.js` 가
   디자인 적용 때 모든 칸의 배경 그림을 정한다. `icons` 가 없는 디자인(애드온 포함)은 기본 선 세트를 받는다.
6. **§3 아이콘 수 28개**: `collapse`·`expand` (선행 마스터 접기, `prereqMaster.js` 가 `[+]`/`[-]` 를 씀) 추가.
   `close` 는 머리 막대 `X`, 상세·도움말 `×`, `.popup-close`·`.modal-close` 의 `×`, `colorPicker.js` 의 `×` 를 모두 덮는다.
   일찍 배우기의 "기본값으로" 도 `reset`. 주문 카드 안의 `×`(spellCardView 삭제 버튼)는 §10 대로 범위 밖.
   글자를 바꾸는 JS: `uiHelpers.js` `restoreScanButton`, `buttonHandlers.js` 스캔 중 글자, `script.js` 전체화면 글자,
   `prereqMaster.js` 접기 표시, `colorPicker.js` 닫기. 모두 글자 칸만 쓰도록 고친다.
7. **§9 버그**: 플레이어 화면이 아니라 개발 하네스의 "Load Test Data" 가 배열만 넘겨서 생긴다(`dev-harness-toolbar.js`).
   한 줄 수정으로 하네스만 고친다.
8. **§7.5 크레딧**: `CREDITS.md` 를 새로 만들지 않고 글꼴이 이미 적힌 세 곳에 적는다: `README.md` Credits,
   `THIRD-PARTY-NOTICES.md.in`, `docs/NEXUS_PAGE.bbcode`.
9. **§6.1 대문자**: 그대로 두는 것 = 앱 이름·메뉴·탭(`.panel-title`, `.header-btn-wide`, `.howto-tab-text`, `.scanner-mode-btn`)과
   나무·주문 카드 규칙(`.school-label`, `.center-hub-text`, `.school-badge`, `.state-badge`, `.btn-unlock`, `.btn-learn`,
   `.learning-status-badge`, `.detail-label`, `.prereq-type-label`, `.tooltip-state`, `.renderer-badge`, `.find-spell-state`).
   나머지 대문자 규칙은 보통 글자로, `.settings-block-title` 은 작은 대문자로. 바꾸는 규칙은 공통 파일 `panel-chrome.css`
   (patch-ui.css 다음에 연결) 한 곳에 모은다.
10. **순서**: 모양 고르기(사용자 선택) → 칸·선 세트 → 틀 정리 → 잉크 → 금박 → 분필. Gemini 결과가 나쁘면 선 세트가 그대로 남는다.
