# 마법 메뉴 책갈피 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 마법 메뉴의 주문 설명 칸에 책갈피를 붙여 누르면 Heart of Magic 패널이 열리고 닫으면 마법 메뉴로 돌아오게 하고, 고른 주문의 경험치 고리와 태그 칩을 설명 칸에 그린다.

**Architecture:** `MenuOpenCloseEvent`로 `MagicMenu`가 열리는 것을 받아, 그 메뉴의 Scaleform 무비 안에서 설명 칸 MovieClip을 데이터 파일의 경로로 찾고, 그 자식으로 빈 MovieClip을 만들어 AS2 그리기 API로 책갈피·고리·칩을 그린다. 클릭과 선택 추적은 `GFxFunctionHandler`로 만든 네이티브 함수를 클립의 `onRelease`/`onEnterFrame`에 건다. SWF는 배포하지도 고치지도 않는다.

**Tech Stack:** C++23, CommonLibSSE-NG v10 (`RE::GFxValue`, `RE::GFxFunctionHandler`, `RE::UIMessageQueue`), nlohmann-json, spdlog. 빌드는 `.\BuildRelease.ps1`.

**Spec:** `docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md`

**Branch:** `feature/next-update`에서 새 브랜치 `feature/magic-menu-bookmark`를 만든다. 설정 쪽(5절: 단축키 없음, 두 스위치, 둘 다 꺼짐 보정, 13개 언어, `panelOpenSettingTest.js`)은 `53e9d9c`로 이미 들어가 있으므로 이 계획에 없다.

## Global Constraints

- 파일 하나 600줄 이하. `Main.cpp`는 지금 590줄이므로 한 줄(설치 호출)만 더한다.
- RE/SKSE 타입은 `plugins/external/commonlibsse-ng/include/RE/`의 헤더를 읽고 쓴다. 아래 계획에 적은 시그니처는 2026-10-08에 읽은 것이다.
- 스마트 포인터만 쓴다(`RE::GPtr`, `std::unique_ptr`). `lock_guard`만 쓴다. 매직 넘버는 `k` 상수로 둔다.
- 메뉴 SWF는 배포하지도 고치지도 않는다. PrismaUI 덧씌우기는 쓰지 않는다.
- 설명 칸을 못 찾으면 아무것도 붙이지 않고 `SpellLearning.log`에 남긴다. 메뉴가 깨지면 안 된다.
- 트리에 없는 주문(`ProgressionManager::IsTreeSpell` false)에는 고리와 칩을 붙이지 않는다.
- 칩 순서는 패널 카드와 같다: `SpellScanner::BuildSpellChips` + `Librarian::MergeCatalogChips` (`SpellScannerScan.cpp:553-554`와 같은 두 줄). 새로 짜지 않는다.
- 게임패드, 목록 줄마다 고리(2안), PrismaUI 덧씌우기는 범위 밖.
- C++ 테스트 틀이 없다(CLAUDE.md). C++ 작업의 확인은 "빌드 통과 + 이름 붙은 로그 줄 하나"이고, 게임 안 확인은 사용자가 한다. 게임 확인은 Task 1, Task 4 끝 두 번으로 묶는다.

## 책갈피 그림 (2026-10-08 사용자 변경)

- 사용자: "심플하게 가죽 책갈피에 Heart of Magic이라고 영어로 적은 것", 이어서 참고 사진(책장 위 모서리를 접어 끼우는 가죽 클립 책갈피, 접힌 선을 따라 박음질, 이름 음각)과 함께 "이런 느낌으로 주문 이름·설명 칸의 오른쪽 위에". 스펙 2.1의 붉은 바탕·심장 문양과 "위로 솟은 탭" 대신 이것을 쓴다. 스펙 2.1을 Task 2에서 고친다.
- 모양: 설명 칸 오른쪽 위 모서리에 끼운 클립. 접힌 부분이 위 테두리 위로 조금(약 6) 올라오고, 앞날개가 칸 안쪽으로 내려온다(약 28). 아래 두 모서리는 둥글다. 황갈색 결 있는 가죽, 접힌 선 아래 밝은 박음질, 앞날개에 "Heart of Magic" 음각(바탕보다 어두운 글자).
- **마우스를 올리면 글자가 금빛으로 빛난다** (사용자, 2026-10-08). 빛나는 글자만 있는 두 번째 그림(`Interface/HeartOfMagic/bookmark_glow.dds`, 검은 바탕을 투명으로 바꾼 금빛 글자)을 책갈피 그림 위에 겹쳐 두고, 평소에는 `_alpha` 0, 마우스를 올리면 0.25초에 걸쳐 나타난 뒤 60~100 사이를 2초 주기로 천천히 오르내린다. 마우스를 떼면 0.25초에 걸쳐 사라진다. 그림을 못 쓰면 글자 칸에 금빛 `GlowFilter`(색 `0xFFD27A`)를 씌우는 쪽으로 간다(Task 2 Step 3b에서 지원 여부 확인).
- 그림 파일을 Scaleform에서 불러올 수 있으면(Task 1에서 확인) 그림 한 장(`Interface/HeartOfMagic/bookmark.dds`)을 쓴다. 안 되면 갈색 가죽 띠를 선으로 그리고 `createTextField`로 "Heart of Magic"을 쓴다.
- 모양 좌표는 상수 배열 하나(`kBookmarkOutline`)에 둔다. 그림이 바뀌어도 그 배열만 고친다.

## Review Focus

1. **세이브 불러오기·메인 메뉴로 갈 때 패널이 열려 있던 경우.** `EnsureFocusReleased`(Main.cpp:453, 474, `NotifyMainMenuLoaded`)가 `HidePanel`을 부른다. 이때 마법 메뉴가 다시 열리면 안 된다. → Task 3이 `EnsureFocusReleased`에서 표시를 먼저 지운다.
2. **메뉴가 열리는 순간 설명 칸이 아직 없는 경우.** `MenuOpenCloseEvent(opening)`는 AS2 자식이 만들어지기 전에 올 수 있다. → Task 1이 한 번 미루고(`AddUITask`) 그래도 없으면 다음 몇 프레임 다시 본다(`kInjectRetries`).
3. **마법 메뉴를 열 때마다 무비가 새로 만들어진다.** 지난번 클립·함수는 쓸 수 없다. → 매번 다시 붙이고, 함수 처리기는 `RE::GPtr`로 정적 보관한다(Task 2).
4. **책갈피를 누른 뒤 메뉴가 닫히기 전에 패널을 여는 경우.** 마법 메뉴와 패널 포커스가 겹친다. → 누르면 표시만 세우고 메뉴를 닫고, 메뉴 닫힘 이벤트에서 패널을 연다(Task 2).
6. **긴 주문 이름이 클립 밑으로 들어가는 경우.** 클립 앞날개가 칸 안쪽 오른쪽 위를 덮는다. → Task 4 게임 확인 1번에서 본다. 가려지면 클립을 오른쪽 여백(`kRightInset`)을 줄이거나 앞날개(`kFlapH`)를 짧게 해서 제목 줄 위로 올린다.
5. **권능·능력·스크롤·트리 없음.** 고리·칩 없이 제목 그대로. 주문을 바꾸면 지난 고리·칩이 남지 않아야 한다. → Task 4가 매번 지우고 다시 그린다.

---

## 파일 구조

| 파일 | 할 일 |
|---|---|
| `plugins/spelllearning/include/magicmenu/MagicMenuBookmark.h` | 공개 함수 `Install()`, `ClearReturnFlag()` 등 |
| `plugins/spelllearning/src/magicmenu/MagicMenuBookmark.cpp` | 메뉴 이벤트, 경로 찾기, 진단 로그, 책갈피 그리기, 클릭 |
| `plugins/spelllearning/src/magicmenu/MagicMenuSpellInfo.cpp` | 고른 주문 따라가기, 고리와 칩 |
| `plugins/spelllearning/src/magicmenu/MagicMenuDraw.h` | AS2 그리기 도우미(`MoveTo`, `LineTo`, `Fill` 등), 두 .cpp가 함께 씀 |
| `SKSE/Plugins/SpellLearning/magicmenu_paths.json` | 설명 칸 경로 목록 (`Distribution.cmake:153-155`가 폴더째 복사하므로 패키징 줄은 더하지 않는다) |
| `plugins/spelllearning/CMakeLists.txt` | 새 .cpp 두 개 (소스를 하나씩 적는 방식, 36-40줄 근처) |
| `plugins/spelllearning/src/Main.cpp` | `OnDataLoaded`에 `MagicMenuBookmark::Install();` 한 줄 |
| `plugins/spelllearning/src/uimanager/UIManagerCore.cpp` | `HidePanel`에서 마법 메뉴 다시 열기, `EnsureFocusReleased`에서 표시 지우기 |
| `plugins/spelllearning/src/HudNotify.cpp` | `ReadTable`이 `chips.*`도 읽게 (칩 이름) |

---

### Task 1: 진단 — 설명 칸 경로, 선택 항목, 그림 불러오기

이 작업의 결과물은 기능이 아니라 답 세 개다. 사용자가 게임을 한 번 열고 마법 메뉴를 열었다 닫으면 로그로 답이 나온다.

**Files:**
- Create: `plugins/spelllearning/include/magicmenu/MagicMenuBookmark.h`
- Create: `plugins/spelllearning/src/magicmenu/MagicMenuBookmark.cpp`
- Create: `SKSE/Plugins/SpellLearning/magicmenu_paths.json`
- Modify: `plugins/spelllearning/CMakeLists.txt` (uimanager 소스 줄 아래)
- Modify: `plugins/spelllearning/src/Main.cpp:399-400` 근처 (`InputHandler::GetSingleton()->Register();` 다음 줄)

**Interfaces:**
- Produces: `namespace MagicMenuBookmark { void Install(); }`, 내부 `std::optional<RE::GFxValue> FindCard(RE::GFxMovieView*)`, `void LogTree(const RE::GFxValue&, std::string_view, int depth)`, `std::vector<std::string> g_paths`

- [ ] **Step 1: 경로 파일 만들기**

`SKSE/Plugins/SpellLearning/magicmenu_paths.json`:

```json
{
  "_comment": "Paths to the magic menu's item card (spell description box), tried in order. One line per menu mod. See docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md 4.2",
  "cardPaths": [
    "_root.Menu_mc.itemCardFadeHolder.ItemCard_mc",
    "_root.Menu_mc.ItemCardFadeHolder_mc.ItemCard_mc",
    "_root.Menu_mc.itemCard"
  ]
}
```

첫 줄은 SkyUI의 `ItemMenu`(`itemCardFadeHolder.ItemCard_mc`), 둘째는 바닐라 이름으로 짐작한 것이다. 사실이 아니라 진단으로 확인할 후보다.

- [ ] **Step 2: 헤더**

`plugins/spelllearning/include/magicmenu/MagicMenuBookmark.h`:

```cpp
#pragma once

// =============================================================================
// MAGIC MENU BOOKMARK - a bookmark on the magic menu's spell description box
// that opens the panel, and the selected spell's XP ring and tag chips.
// docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md
// =============================================================================

namespace MagicMenuBookmark
{
    // Registers the MagicMenu open/close sink and reads magicmenu_paths.json. kDataLoaded.
    void Install();
}
```

- [ ] **Step 3: 이벤트, 경로 찾기, 진단 로그**

`plugins/spelllearning/src/magicmenu/MagicMenuBookmark.cpp`:

```cpp
#include "magicmenu/MagicMenuBookmark.h"
#include "PathText.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace MagicMenuBookmark
{
    namespace
    {
        constexpr const char* kPathsFile = "Data/SKSE/Plugins/SpellLearning/magicmenu_paths.json";
        constexpr int kLogDepth = 2;          // spec 4.2: the menu root's children, two levels
        constexpr int kInjectRetries = 5;     // UI tasks to wait for the card clip after the open event

        std::vector<std::string> g_paths;

        void ReadPaths()
        {
            std::ifstream file(kPathsFile, std::ios::binary);
            auto json = nlohmann::json::parse(file, nullptr, false);
            if (json.is_discarded() || !json.contains("cardPaths") || !json["cardPaths"].is_array()) {
                logger::warn("MagicMenuBookmark: {} missing or unreadable - no bookmark", kPathsFile);
                return;
            }
            for (const auto& path : json["cardPaths"]) {
                if (path.is_string()) g_paths.push_back(path.get<std::string>());
            }
            logger::info("MagicMenuBookmark: {} card paths", g_paths.size());
        }

        // "_root.Menu_mc.a.b" -> walk member by member from the movie's _root
        std::optional<RE::GFxValue> Resolve(RE::GFxMovieView* a_movie, std::string_view a_path)
        {
            RE::GFxValue node;
            if (!a_movie->GetVariable(&node, "_root")) return std::nullopt;
            std::size_t pos = a_path.find('.');
            while (pos != std::string_view::npos) {
                const auto next = a_path.find('.', pos + 1);
                const std::string name(a_path.substr(pos + 1, next == std::string_view::npos ? next : next - pos - 1));
                RE::GFxValue child;
                if (!node.GetMember(name.c_str(), &child) || !child.IsDisplayObject()) return std::nullopt;
                node = child;
                pos = next;
            }
            return node;
        }

        void LogTree(const RE::GFxValue& a_node, const std::string& a_name, int a_depth)
        {
            logger::info("MagicMenuBookmark:   {}{}", std::string(static_cast<std::size_t>((kLogDepth - a_depth) * 2), ' '), a_name);
            if (a_depth == 0) return;
            a_node.VisitMembers([&](const char* a_member, const RE::GFxValue& a_value) {
                if (a_value.IsDisplayObject()) LogTree(a_value, a_name + "." + a_member, a_depth - 1);
            });
        }
    }

    std::optional<RE::GFxValue> FindCard(RE::GFxMovieView* a_movie)
    {
        for (const auto& path : g_paths) {
            if (auto card = Resolve(a_movie, path)) {
                logger::info("MagicMenuBookmark: card at {}", path);
                return card;
            }
        }
        return std::nullopt;
    }
}
```

`GFxMovieView::GetVariable` 시그니처는 `RE/G/GFxMovie.h`에서 확인하고 쓴다(`bool GetVariable(GFxValue* a_val, const char* a_pathToVar) const`로 기억하지만 확인 전에는 쓰지 않는다). `GetVariable`이 경로 전체를 받으면 `Resolve`는 그 한 줄로 줄인다.

- [ ] **Step 4: 메뉴 이벤트와 진단 (같은 파일 아래)**

```cpp
namespace MagicMenuBookmark
{
    namespace
    {
        RE::GFxMovieView* OpenMagicMenuMovie()
        {
            auto* ui = RE::UI::GetSingleton();
            auto menu = ui ? ui->GetMenu<RE::MagicMenu>() : nullptr;
            return menu ? menu->uiMovie.get() : nullptr;
        }

        void Diagnose(int a_triesLeft)
        {
            auto* movie = OpenMagicMenuMovie();
            if (!movie) return;  // closed meanwhile
            if (auto card = FindCard(movie)) {
                LogTree(*card, "card", kLogDepth);
                // selected spell, through the engine's list (vanilla and SkyUI alike)
                auto menu = RE::UI::GetSingleton()->GetMenu<RE::MagicMenu>();
                auto* item = menu->GetRuntimeData().itemList ? menu->GetRuntimeData().itemList->GetSelectedItem() : nullptr;
                logger::info("MagicMenuBookmark: selected {}",
                    item && item->data.baseForm ? item->data.baseForm->GetName() : "(none)");
                // image check: a scratch clip loads the bookmark picture; the result shows a frame later
                RE::GFxValue probe;
                if (card->CreateEmptyMovieClip(&probe, "hom_probe", -1)) {
                    std::array<RE::GFxValue, 1> args{ RE::GFxValue("HeartOfMagic/bookmark.dds") };
                    probe.Invoke("loadMovie", args);
                    SKSE::GetTaskInterface()->AddUITask([] {
                        if (auto* m = OpenMagicMenuMovie(); auto c = m ? FindCard(m) : std::nullopt) {
                            RE::GFxValue p, w;
                            if (c->GetMember("hom_probe", &p) && p.GetMember("_width", &w))
                                logger::info("MagicMenuBookmark: image probe width {}", w.GetNumber());
                        }
                    });
                }
                return;
            }
            if (a_triesLeft > 0) {
                SKSE::GetTaskInterface()->AddUITask([a_triesLeft] { Diagnose(a_triesLeft - 1); });
                return;
            }
            logger::warn("MagicMenuBookmark: card not found - the menu root's children:");
            RE::GFxValue root;
            if (movie->GetVariable(&root, "_root")) LogTree(root, "_root", kLogDepth);
        }

        class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static MenuSink* GetSingleton() { static MenuSink singleton; return &singleton; }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (a_event && a_event->menuName == RE::MagicMenu::MENU_NAME) {
                    logger::info("MagicMenuBookmark: magic menu {}", a_event->opening ? "opened" : "closed");
                    if (a_event->opening) SKSE::GetTaskInterface()->AddUITask([] { Diagnose(kInjectRetries); });
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            MenuSink(const MenuSink&) = delete;
            MenuSink& operator=(const MenuSink&) = delete;

        private:
            MenuSink() = default;
        };
    }

    void Install()
    {
        ReadPaths();
        RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        logger::info("MagicMenuBookmark: installed");
    }
}
```

`AddUITask`가 `SKSE::TaskInterface`에 있는지 `include/SKSE/Interfaces.h`에서 확인한다(없으면 `AddTask`). `"HeartOfMagic/bookmark.dds"`는 `Data/Interface/` 기준 상대 경로다. 진단용 그림 한 장(아무 DDS)을 dev 설치의 `Interface/HeartOfMagic/bookmark.dds`에 손으로 넣어 둔다.

- [ ] **Step 5: 등록과 빌드**

`CMakeLists.txt`의 uimanager 줄들 아래에:

```cmake
    src/magicmenu/MagicMenuBookmark.cpp
```

`Main.cpp` 맨 위 include 묶음에 `#include "magicmenu/MagicMenuBookmark.h"`, `InputHandler::GetSingleton()->Register();` 다음 줄에:

```cpp
    MagicMenuBookmark::Install();
```

Skyrim이 꺼져 있는지 확인하고 `.\BuildRelease.ps1`. 기대: 경고·오류 없이 빌드. `.\DeployDev.ps1`.

- [ ] **Step 6: 게임 확인 (사용자, 약 3분)**

1. 게임 불러오기 → 마법 메뉴 열기 → 주문 두 개 골라 보기 → 닫기 → 다시 열기 (학파 탭 기억 확인용으로 다른 학파 탭에서 닫는다).
2. `SpellLearning.log`에서 `MagicMenuBookmark:` 줄을 읽는다.

로그에서 얻을 답:
- `card at <path>` 또는 `card not found`와 트리 → 실제 경로를 `magicmenu_paths.json` 맨 앞에 둔다.
- `selected <name>` → 엔진 목록으로 고른 주문을 얻을 수 있는지.
- `image probe width <n>` → 0보다 크면 그림 불러오기 됨.
- 다시 연 메뉴가 같은 학파 탭이었는지(사용자 눈).

- [ ] **Step 7: 답 기록과 커밋**

스펙 7절 "1단계" 아래에 세 답과 날짜를 적는다. 그림이 안 되면 "가죽 띠를 선으로 그림"으로 적는다.

```bash
git add plugins/spelllearning/include/magicmenu plugins/spelllearning/src/magicmenu plugins/spelllearning/CMakeLists.txt plugins/spelllearning/src/Main.cpp SKSE/Plugins/SpellLearning/magicmenu_paths.json docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md
git commit -m "Magic menu bookmark, step 1: MagicMenu open sink, card path lookup from magicmenu_paths.json, diagnostic log (card tree, selected spell, image load)"
```

---

### Task 2: 책갈피 그리기와 클릭 → 패널

**Files:**
- Create: `plugins/spelllearning/src/magicmenu/MagicMenuDraw.h`
- Modify: `plugins/spelllearning/src/magicmenu/MagicMenuBookmark.cpp` (진단은 `card not found` 로그만 남기고 지운다)
- Modify: `plugins/spelllearning/include/magicmenu/MagicMenuBookmark.h`
- Modify: `docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md` 2.1 (가죽 책갈피로)
- (그림이 되면) Create: `Interface/HeartOfMagic/bookmark.dds`, Modify: `plugins/cmake/Distribution.cmake` (153-155줄과 같은 `copy_directory`로 `Interface/HeartOfMagic`)

**Interfaces:**
- Consumes: Task 1의 `FindCard`, `OpenMagicMenuMovie`
- Produces: `namespace MagicMenuDraw { void MoveTo(RE::GFxValue&, double, double); void LineTo(...); void CurveTo(RE::GFxValue&, double cx, double cy, double x, double y); void BeginFill(RE::GFxValue&, std::uint32_t rgb, double alpha); void LineStyle(RE::GFxValue&, double thickness, std::uint32_t rgb); void EndFill(RE::GFxValue&); void Clear(RE::GFxValue&); RE::GFxValue Text(RE::GFxValue& parent, const char* name, double x, double y, double w, double h, std::string_view text, std::uint32_t rgb, double size); }`
- Produces: `MagicMenuBookmark::ClearReturnFlag()`, `bool MagicMenuBookmark::TakeReturnFlag()` (Task 3이 씀)

- [ ] **Step 1: 그리기 도우미**

`MagicMenuDraw.h`:

```cpp
#pragma once

// AS2 drawing API calls on a MovieClip (beginFill, lineTo, curveTo ...) and a text field
namespace MagicMenuDraw
{
    inline void Call(RE::GFxValue& a_clip, const char* a_name, std::initializer_list<RE::GFxValue> a_args)
    {
        std::vector<RE::GFxValue> args(a_args);
        a_clip.Invoke(a_name, nullptr, args.data(), args.size());
    }
    inline void Clear(RE::GFxValue& c) { Call(c, "clear", {}); }
    inline void LineStyle(RE::GFxValue& c, double t, std::uint32_t rgb) { Call(c, "lineStyle", { t, static_cast<double>(rgb), 100.0 }); }
    inline void BeginFill(RE::GFxValue& c, std::uint32_t rgb, double alpha) { Call(c, "beginFill", { static_cast<double>(rgb), alpha }); }
    inline void EndFill(RE::GFxValue& c) { Call(c, "endFill", {}); }
    inline void MoveTo(RE::GFxValue& c, double x, double y) { Call(c, "moveTo", { x, y }); }
    inline void LineTo(RE::GFxValue& c, double x, double y) { Call(c, "lineTo", { x, y }); }
    inline void CurveTo(RE::GFxValue& c, double cx, double cy, double x, double y) { Call(c, "curveTo", { cx, cy, x, y }); }

    // createTextField + text + a TextFormat colour/size; the menu's own font, so a Korean font mod applies
    inline RE::GFxValue Text(RE::GFxValue& a_parent, const char* a_name, double x, double y, double w, double h,
        std::string_view a_text, std::uint32_t a_rgb, double a_size)
    {
        RE::GFxValue depth, field, format;
        a_parent.Invoke("getNextHighestDepth", &depth);   // above every child the menu already has
        Call(a_parent, "createTextField", { a_name, depth.GetNumber(), x, y, w, h });
        a_parent.GetMember(a_name, &field);
        field.Invoke("getNewTextFormat", &format);
        format.SetMember("color", static_cast<double>(a_rgb));
        format.SetMember("size", a_size);
        field.Invoke("setNewTextFormat", nullptr, &format, 1);
        field.SetMember("selectable", false);
        field.SetMember("text", RE::GFxValue(std::string(a_text)));
        return field;
    }
}
```

`std::initializer_list<RE::GFxValue>`에 `const char*`와 `double`이 섞여 들어가므로, `GFxValue`의 생성자(`GFxValue(const char*)`, `GFxValue(double)`)가 모두 implicit인지 헤더에서 확인한다. 아니면 `RE::GFxValue(...)`로 감싼다.

- [ ] **Step 2: 책갈피 모양 상수와 그리기**

`MagicMenuBookmark.cpp`의 이름 없는 namespace에:

```cpp
        // Spec 2.1 (fold-over leather clip, 2026-10-08): 100 wide at a 600-wide card, scaled to the card's width.
        // The outline is one table so a new design only changes it.
        constexpr double kRefCardWidth = 600.0;
        constexpr double kTabW = 100.0, kFoldH = 6.0, kFlapH = 28.0, kCorner = 6.0, kHoverRise = 4.0, kRightInset = 16.0;
        constexpr std::uint32_t kLeather = 0xA0643A, kLeatherEdge = 0x6E4024, kStitch = 0xE8DCC8, kInk = 0x5E331B;
        struct Pt { double x, y; };
        // y=0 is the card's top border: the fold rises kFoldH above it, the front flap hangs kFlapH into the card
        constexpr std::array<Pt, 7> kBookmarkOutline{ { { 0, -kFoldH }, { kTabW, -kFoldH }, { kTabW, kFlapH - kCorner },
                                                        { kTabW - kCorner, kFlapH }, { kCorner, kFlapH }, { 0, kFlapH - kCorner },
                                                        { 0, -kFoldH } } };
        constexpr double kStitchY = 2.0, kStitchInset = 5.0, kDash = 4.0;

        void DrawTab(RE::GFxValue& a_tab, double a_scale)
        {
            MagicMenuDraw::Clear(a_tab);
            MagicMenuDraw::LineStyle(a_tab, 1.0, kLeatherEdge);
            MagicMenuDraw::BeginFill(a_tab, kLeather, 100.0);
            MagicMenuDraw::MoveTo(a_tab, kBookmarkOutline[0].x * a_scale, kBookmarkOutline[0].y * a_scale);
            for (std::size_t i = 1; i < kBookmarkOutline.size(); ++i)
                MagicMenuDraw::LineTo(a_tab, kBookmarkOutline[i].x * a_scale, kBookmarkOutline[i].y * a_scale);
            MagicMenuDraw::EndFill(a_tab);
            // the stitch line under the fold: dashes across the flap
            MagicMenuDraw::LineStyle(a_tab, 1.0, kStitch);
            for (double x = kStitchInset; x + kDash <= kTabW - kStitchInset; x += 2 * kDash) {
                MagicMenuDraw::MoveTo(a_tab, x * a_scale, kStitchY * a_scale);
                MagicMenuDraw::LineTo(a_tab, (x + kDash) * a_scale, kStitchY * a_scale);
            }
            MagicMenuDraw::Text(a_tab, "hom_label", 4 * a_scale, 8 * a_scale, (kTabW - 8) * a_scale, 16 * a_scale,
                "Heart of Magic", kInk, 11 * a_scale);
        }
```

그림 파일이 되면(Task 1 답) `DrawTab` 대신 `tab.Invoke("loadMovie", { "HeartOfMagic/bookmark.dds" })`를 쓰고 `_x`, `_y`, `_width`, `_height`로 같은 자리·크기에 맞춘다. 사용자가 고른 Gemini 그림에서 클립만 잘라 DDS(BC3, 256×88, 투명 바탕)로 바꿔 `Interface/HeartOfMagic/bookmark.dds`에 둔다.

- [ ] **Step 3: 클릭 처리기와 붙이기**

```cpp
        std::atomic<bool> g_openPanelOnClose{ false };   // clicked: open the panel once the menu has closed
        std::atomic<bool> g_returnToMenu{ false };       // the panel was opened from the bookmark

        class Handler final : public RE::GFxFunctionHandler
        {
        public:
            enum class Kind { kClick, kRollOver, kRollOut };
            explicit Handler(Kind a_kind) : m_kind(a_kind) {}
            void Call(Params& a_params) override
            {
                auto* tab = a_params.thisPtr;
                switch (m_kind) {
                case Kind::kClick:
                    logger::info("MagicMenuBookmark: bookmark clicked");
                    g_openPanelOnClose = true;
                    RE::UIMessageQueue::GetSingleton()->AddMessage(RE::MagicMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    break;
                case Kind::kRollOver: Move(tab, -kHoverRise); break;
                case Kind::kRollOut: Move(tab, kHoverRise); break;
                }
            }
        private:
            static void Move(RE::GFxValue* a_tab, double a_dy)
            {
                RE::GFxValue y;
                if (a_tab && a_tab->GetMember("_y", &y)) a_tab->SetMember("_y", y.GetNumber() + a_dy);
            }
            Kind m_kind;
        };

        // Refcounted Scaleform objects, alive for the plugin's life; each new menu movie wraps them again
        RE::GPtr<Handler> g_click, g_rollOver, g_rollOut;

        void SetHandler(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const char* a_event, Handler* a_handler)
        {
            RE::GFxValue fn;
            a_movie->CreateFunction(&fn, a_handler);
            a_clip.SetMember(a_event, fn);
        }

        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_card)
        {
            RE::GFxValue width, tab;
            a_card.GetMember("_width", &width);
            const double scale = width.GetNumber() / kRefCardWidth;
            if (!a_card.CreateEmptyMovieClip(&tab, "hom_bookmark", -1)) return;
            DrawTab(tab, scale);
            tab.SetMember("_x", width.GetNumber() - (kTabW + kRightInset) * scale);
            SetHandler(a_movie, tab, "onRelease", g_click.get());
            SetHandler(a_movie, tab, "onRollOver", g_rollOver.get());
            SetHandler(a_movie, tab, "onRollOut", g_rollOut.get());
            logger::info("MagicMenuBookmark: bookmark attached (scale {:.2f})", scale);
        }
```

`CreateEmptyMovieClip`의 깊이 `-1`이 무엇을 뜻하는지 `GFxValue.cpp`에서 확인하고, 다른 자식과 겹치면 `getNextHighestDepth` 값을 쓴다. `RE::GPtr`과 `GFxFunctionHandler`의 생성 방식(`RE::make_gptr<Handler>(...)` 유무)은 `RE/G/GPtr.h`에서 확인한다.

`Install()`에서 `g_click = RE::make_gptr<Handler>(Handler::Kind::kClick);` 식으로 셋을 만든다. Task 1의 `Diagnose`는 `Inject(int)`로 이름을 바꾸고, 카드를 찾으면 `if (UIManager::MagicMenuBookmark()) Attach(movie, *card);`, 못 찾으면 지금처럼 트리 로그.

- [ ] **Step 3b: 마우스를 올리면 금빛 글자**

책갈피 클립 안에 `hom_glow` 자식을 하나 더 만들고(그림이면 `loadMovie("HeartOfMagic/bookmark_glow.dds")`, 아니면 `hom_label` 글자 칸 복제), `_alpha` 0으로 둔다. `kRollOver`/`kRollOut` 처리기는 위치 이동과 함께 `hom_glow`에 목표 상태를 적는다(`hom_glowTarget` 1 또는 0). `hom_glow`의 `onEnterFrame`에 네이티브 함수 하나를 걸어 매 프레임 `_alpha`를 목표 쪽으로 움직인다:

```cpp
        // Gold glow on hover: fade in over kGlowFadeMs, then breathe between kGlowLow and 100 every kGlowPeriodMs
        constexpr double kGlowFadeMs = 250.0, kGlowPeriodMs = 2000.0, kGlowLow = 60.0;

        class GlowTick final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                auto* glow = a_params.thisPtr;
                RE::GFxValue target, alpha;
                if (!glow || !glow->GetMember("hom_glowTarget", &target) || !glow->GetMember("_alpha", &alpha)) return;
                const auto now = std::chrono::steady_clock::now();
                const double dt = std::chrono::duration<double, std::milli>(now - m_last).count();
                m_last = now;
                const double step = 100.0 * std::min(dt, kGlowFadeMs) / kGlowFadeMs;
                double a = alpha.GetNumber();
                if (target.GetNumber() > 0) {
                    m_phase = std::fmod(m_phase + dt / kGlowPeriodMs, 1.0);
                    const double breath = kGlowLow + (100.0 - kGlowLow) * (0.5 + 0.5 * std::cos(2 * std::numbers::pi * m_phase));
                    a = std::min(a + step, breath);
                } else {
                    m_phase = 0.0;
                    a = std::max(a - step, 0.0);
                }
                glow->SetMember("_alpha", a);
            }
        private:
            std::chrono::steady_clock::time_point m_last = std::chrono::steady_clock::now();
            double m_phase = 0.0;
        };
```

그림을 못 쓰는 경우의 확인: `movie->CreateObject(&filter, "flash.filters.GlowFilter", args, 6)`(색 `0xFFD27A`, 알파 1, blurX/Y 6, 세기 2, 품질 2)가 true이고 `hom_glow.filters = [filter]` 뒤 다시 읽은 `filters`의 길이가 1이면 지원. `GFxMovieView::CreateObject` 시그니처는 `RE/G/GFxMovie.h`에서 확인한다. 둘 다 안 되면 금빛 글자색(`0xFFD27A`)으로 바꾸는 것만 하고 원장에 Ruling을 남긴다.

빛 그림은 Gemini가 그린 검은 바탕 금빛 글자를 밝기→투명도로 바꿔(밝은 곳일수록 불투명) DDS로 만든다. 책갈피 그림과 같은 크기·위치로 맞춘다.

- [ ] **Step 4: 메뉴 닫힘에서 패널 열기**

`MenuSink::ProcessEvent`의 닫힘 쪽:

```cpp
                    if (!a_event->opening && g_openPanelOnClose.exchange(false)) {
                        SKSE::GetTaskInterface()->AddTask([] {
                            g_returnToMenu = true;
                            UIManager::GetSingleton()->ShowPanel();
                        });
                    }
```

헤더에 Task 3용 두 함수:

```cpp
    // HidePanel: true once when the panel was opened from the bookmark (then reopen the menu)
    bool TakeReturnFlag();
    // A save load or the main menu: the panel closes without going back to the menu
    void ClearReturnFlag();
```

```cpp
    bool TakeReturnFlag() { return g_returnToMenu.exchange(false); }
    void ClearReturnFlag() { g_returnToMenu = false; g_openPanelOnClose = false; }
```

- [ ] **Step 5: 빌드**

`.\BuildRelease.ps1`. 기대: 빌드 통과. 게임 확인은 Task 4 끝에 묶는다.

- [ ] **Step 6: 스펙 2.1 고치고 커밋**

스펙 2.1을 "설명 칸 오른쪽 위 모서리에 끼운 가죽 클립 책갈피, 'Heart of Magic' 음각 영문, 그림 파일 또는 선 그리기(Task 1 답)"로 바꾸고, 참고 사진 설명을 적는다.

```bash
git add -A plugins/spelllearning/src/magicmenu plugins/spelllearning/include/magicmenu docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md
git commit -m "Magic menu bookmark: a leather tab on the item card (drawn, or the bookmark picture), hover rise, click closes the menu and opens the panel once it has closed"
```

---

### Task 3: 패널을 닫으면 마법 메뉴로

**Files:**
- Modify: `plugins/spelllearning/src/uimanager/UIManagerCore.cpp:188-228` (`HidePanel`, `EnsureFocusReleased`)

**Interfaces:**
- Consumes: `MagicMenuBookmark::TakeReturnFlag()`, `MagicMenuBookmark::ClearReturnFlag()`

- [ ] **Step 1: `EnsureFocusReleased` 맨 앞에서 표시 지우기**

세이브 불러오기(Main.cpp:453, 474)와 메인 메뉴(`NotifyMainMenuLoaded` → 299줄)가 모두 이 함수를 지나므로 여기 한 곳이면 된다.

```cpp
void UIManager::EnsureFocusReleased()
{
    // A load or the main menu closes the panel without going back to the magic menu
    MagicMenuBookmark::ClearReturnFlag();

    if (!m_prismaUI || !m_prismaUI->IsValid(m_view)) {
```

- [ ] **Step 2: `HidePanel` 끝에서 다시 열기**

`PapyrusAPI::SendMenuClosedEvent();` 다음에:

```cpp
    // Opened from the magic menu's bookmark: back to the menu, which reads its list
    // again, so a spell learned in the panel is there
    if (MagicMenuBookmark::TakeReturnFlag()) {
        logger::info("UIManager: back to the magic menu");
        RE::UIMessageQueue::GetSingleton()->AddMessage(RE::MagicMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    }
```

`#include "magicmenu/MagicMenuBookmark.h"`를 더한다. Esc, 단축키, 닫기 버튼, JS `HidePanel`이 모두 `HidePanel()`을 지나는지 `HidePanel(` 호출처를 다시 grep해서 확인한다(2026-10-08: `TogglePanel`, `OnHidePanel`, `EnsureFocusReleased`, `PapyrusAPI.cpp:71`). Papyrus로 닫는 경우도 메뉴로 돌아가는 것이 맞다(책갈피로 열었으므로).

- [ ] **Step 3: 빌드와 커밋**

`.\BuildRelease.ps1` 통과 확인.

```bash
git add plugins/spelllearning/src/uimanager/UIManagerCore.cpp
git commit -m "Panel opened from the magic menu bookmark goes back to the menu on close; a save load or the main menu clears that"
```

---

### Task 4: 고른 주문의 경험치 고리와 태그 칩

**Files:**
- Create: `plugins/spelllearning/src/magicmenu/MagicMenuSpellInfo.cpp`
- Modify: `plugins/spelllearning/src/magicmenu/MagicMenuBookmark.cpp` (`Attach` 끝에서 `SpellInfo::Attach` 호출)
- Modify: `plugins/spelllearning/include/magicmenu/MagicMenuBookmark.h`
- Modify: `plugins/spelllearning/src/HudNotify.cpp:43-63` (`chips.*`도 읽기)
- Modify: `plugins/spelllearning/CMakeLists.txt`

**Interfaces:**
- Consumes: `MagicMenuDraw::*`, `ProgressionManager::GetSingleton()->IsTreeSpell(RE::FormID)`, `->IsSpellMastered(RE::FormID)`, `->GetProgressPercent(RE::FormID)` (0..1), `SpellScanner::BuildSpellChips(RE::SpellItem*) -> json`, `SpellScanner::GetPersistentFormId(RE::FormID) -> std::string`, `Librarian::MergeCatalogChips(json&, const std::string&)`, `HudNotify::Text(std::string_view key, std::string_view english, std::span<const Param> = {})`
- Produces: `namespace MagicMenuBookmark::SpellInfo { void Attach(RE::GFxMovieView*, RE::GFxValue& a_card); }`

- [ ] **Step 1: 칩 이름을 HudNotify 표에 넣기**

`HudNotify.cpp`의 `ReadTable`:

```cpp
        constexpr std::string_view kChipPrefix = "chips.";   // the magic menu's tag chips (MagicMenuSpellInfo)
        ...
                if ((key.starts_with(kKeyPrefix) || key.starts_with(kChipPrefix)) && value.is_string()) {
```

주석 "Only the hud.* strings"를 "The hud.* strings and the chips.* names"로 고친다. 언어가 바뀔 때 다시 읽는 것은 이미 `UIManagerConfig.cpp:199`가 한다. `GetPersistentFormId`가 `SpellScanner` namespace의 공개 함수인지 `SpellScanner.h`에서 확인한다.

- [ ] **Step 2: 고리와 칩 그리기, 선택 추적**

`MagicMenuSpellInfo.cpp`:

```cpp
#include "magicmenu/MagicMenuBookmark.h"
#include "magicmenu/MagicMenuDraw.h"
#include "HudNotify.h"
#include "ProgressionManager.h"
#include "SpellScanner.h"
#include "librarian/Librarian.h"
#include "uimanager/UIManager.h"

namespace MagicMenuBookmark::SpellInfo
{
    namespace
    {
        constexpr std::size_t kMaxChips = 4;                       // spec 2.2
        constexpr double kRingScale = 1.6;                         // ring diameter / title text height
        constexpr std::uint32_t kLearning = 0x4A90D9, kMastered = 0xD6B56A, kNotStarted = 0x808080;
        constexpr std::uint32_t kChipText = 0xDDDDDD;
        constexpr int kArcSegments = 24;

        RE::FormID g_shown = 0;   // the spell drawn now; 0 = none

        RE::GFxValue Child(RE::GFxValue& a_parent, const char* a_name)
        {
            RE::GFxValue v;
            a_parent.GetMember(a_name, &v);
            return v;
        }

        void Ring(RE::GFxValue& a_clip, double a_r, float a_progress, bool a_mastered)
        {
            MagicMenuDraw::LineStyle(a_clip, 2.0, kNotStarted);
            if (a_mastered) {
                MagicMenuDraw::BeginFill(a_clip, kMastered, 100.0);
            }
            MagicMenuDraw::MoveTo(a_clip, a_r, 0);
            for (int i = 1; i <= kArcSegments; ++i) {
                const double t = 2 * std::numbers::pi * i / kArcSegments;
                MagicMenuDraw::LineTo(a_clip, a_r * std::cos(t), a_r * std::sin(t));
            }
            if (a_mastered) { MagicMenuDraw::EndFill(a_clip); return; }
            if (a_progress <= 0.0f) return;
            MagicMenuDraw::LineStyle(a_clip, 3.0, kLearning);   // the learned part, clockwise from the top
            const int steps = std::max(1, static_cast<int>(kArcSegments * a_progress));
            MagicMenuDraw::MoveTo(a_clip, 0, -a_r);
            for (int i = 1; i <= steps; ++i) {
                const double t = -std::numbers::pi / 2 + 2 * std::numbers::pi * a_progress * i / steps;
                MagicMenuDraw::LineTo(a_clip, a_r * std::cos(t), a_r * std::sin(t));
            }
        }

        // The panel card's chip ids, the first kMaxChips names and "+N"
        std::string ChipLine(RE::SpellItem* a_spell)
        {
            auto chips = SpellScanner::BuildSpellChips(a_spell);
            Librarian::MergeCatalogChips(chips, SpellScanner::GetPersistentFormId(a_spell->GetFormID()));
            std::string line;
            std::size_t i = 0;
            for (const auto& chip : chips) {
                if (!chip.is_string()) continue;
                if (i == kMaxChips) { line += std::format("  +{}", chips.size() - kMaxChips); break; }
                const auto id = chip.get<std::string>();
                line += (i ? "  " : "") + HudNotify::Text("chips." + id, id);
                ++i;
            }
            return line;
        }
```

같은 이름 없는 namespace 안에 `Draw`를 이어 쓴다:

```cpp
        // a_info is "hom_info", a child of the card made fresh for each spell
        void Draw(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, RE::SpellItem* a_spell)
        {
            RE::GFxValue old = Child(a_card, "hom_info");
            if (old.IsDisplayObject()) old.Invoke("removeMovieClip");
            if (!a_spell) return;
            const auto id = a_spell->GetFormID();
            auto* progression = ProgressionManager::GetSingleton();
            if (!progression->IsTreeSpell(id)) return;   // powers, abilities, scrolls, no tree: title as it is

            RE::GFxValue title = Child(a_card, "ItemName");   // name from Task 1's card tree log
            RE::GFxValue tx, ty, th;
            title.GetMember("_x", &tx); title.GetMember("_y", &ty); title.GetMember("_height", &th);

            RE::GFxValue info;
            if (!a_card.CreateEmptyMovieClip(&info, "hom_info", -1)) return;
            const double r = th.GetNumber() * kRingScale / 2;
            RE::GFxValue ring;
            info.CreateEmptyMovieClip(&ring, "ring", -1);
            ring.SetMember("_x", tx.GetNumber() - r - 4);
            ring.SetMember("_y", ty.GetNumber() + th.GetNumber() / 2);
            const bool mastered = progression->IsSpellMastered(id);
            const float progress = progression->GetProgressPercent(id);
            Ring(ring, r, progress, mastered);
            if (!mastered) {
                MagicMenuDraw::Text(info, "pct", tx.GetNumber() - 2 * r - 4, ty.GetNumber() + th.GetNumber() / 2 - r * 0.6,
                    2 * r, r * 1.2, std::format("{}", static_cast<int>(progress * 100)), kChipText, r * 0.8);
            }
            MagicMenuDraw::Text(info, "chips", tx.GetNumber(), ty.GetNumber() + th.GetNumber(), 400, th.GetNumber(),
                ChipLine(a_spell), kChipText, th.GetNumber() * 0.6);
        }
```

`"ItemName"`은 SkyUI·바닐라 ItemCard의 제목 텍스트 필드 이름으로 짐작한 것이다. Task 1 로그의 카드 트리에서 실제 이름을 보고 `magicmenu_paths.json`에 `"titleField": "ItemName"`으로 두고 읽는다(카드 경로와 같은 이유: 메뉴 모드마다 다를 수 있다).

- [ ] **Step 3: 선택 추적 — 매 프레임 비교, 바뀔 때만 다시 그림**

훅을 새로 걸지 않는다. `hom_info`와 별개로 카드에 보이지 않는 `hom_watch` 클립을 하나 두고, 그 `onEnterFrame`에 네이티브 함수를 건다:

```cpp
        // Simplification: one native call per frame while the magic menu is open (the game is paused
        // there), comparing one FormID. Upgrade path if it ever shows in a profile: a write_vfunc on
        // MagicMenu::PostDisplay (slot 6), the pattern SpellCastImmediateHook.cpp:56 uses.
        class Watch final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                auto menu = RE::UI::GetSingleton()->GetMenu<RE::MagicMenu>();
                if (!menu || !menu->GetRuntimeData().itemList) return;
                auto* item = menu->GetRuntimeData().itemList->GetSelectedItem();
                auto* spell = item && item->data.baseForm ? item->data.baseForm->As<RE::SpellItem>() : nullptr;
                const RE::FormID id = spell ? spell->GetFormID() : 0;
                if (id == g_shown) return;
                g_shown = id;
                RE::GFxValue card;
                if (a_params.thisPtr && a_params.thisPtr->GetMember("_parent", &card))
                    Draw(menu->uiMovie.get(), card, spell);
            }
        };
        RE::GPtr<Watch> g_watch;
    }

    void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_card)
    {
        if (!UIManager::MagicMenuSpellInfo()) return;
        if (!g_watch) g_watch = RE::make_gptr<Watch>();
        g_shown = 0;   // a new movie: draw the first selection
        RE::GFxValue watch, fn;
        if (!a_card.CreateEmptyMovieClip(&watch, "hom_watch", -1)) return;
        a_movie->CreateFunction(&fn, g_watch.get());
        watch.SetMember("onEnterFrame", fn);
    }
}
```

`GetSelectedItem()`이 SkyUI에서도 맞는 주문을 주는지는 Task 1의 `selected <name>` 로그가 답한다. 안 맞으면 `thisPtr`의 `_parent`에서 SkyUI 목록의 `selectedEntry.formId`를 읽는 쪽으로 바꾸고, 그 경로도 `magicmenu_paths.json`에 둔다.

헤더에 `namespace SpellInfo { void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_card); }`를 더하고, `MagicMenuBookmark.cpp`의 `Inject`가 카드를 찾으면 책갈피와 별개로 `SpellInfo::Attach(movie, *card);`를 부른다(두 스위치는 따로 켜고 끈다). `CMakeLists.txt`에 `src/magicmenu/MagicMenuSpellInfo.cpp`.

- [ ] **Step 4: 빌드, 배포**

`.\BuildRelease.ps1` 통과, `.\DeployDev.ps1`. 두 .cpp가 각각 600줄 아래인지 센다.

- [ ] **Step 5: 게임 확인 (사용자, 약 5분 — 이 기능의 두 번째이자 마지막 게임 확인)**

1. 마법 메뉴: 설명 칸 오른쪽 위 모서리에 끼운 가죽 클립, "Heart of Magic" 글자. 마우스를 올리면 조금 올라오고, 글자가 금빛으로 서서히 빛나며 숨 쉬듯 밝아졌다 어두워진다. 떼면 빛이 사라진다. 긴 주문 이름(예: 다른 모드의 긴 이름)이 클립에 가려지지 않는다.
2. 배우는 중인 주문: 이름 왼쪽 파란 호와 %, 이름 아래 칩 최대 4개(+N). 마스터한 주문: 금색 원, 숫자 없음. 시작 안 한 주문: 회색 원과 0.
3. 권능(예: 종족 권능)·스크롤로 옮기면 고리와 칩이 사라진다.
4. 책갈피 클릭 → 메뉴 닫히고 패널 열림 → Esc → 마법 메뉴로 돌아옴. 단축키로 연 패널은 닫으면 게임으로.
5. 패널을 책갈피로 연 채 세이브 불러오기 → 마법 메뉴가 열리지 않는다.
6. 설정에서 "마법 메뉴 책갈피" 끄기 → 책갈피 없음(고리는 그대로). "태그와 고리" 끄기 → 고리·칩 없음.
7. 로그: `bookmark attached`, `bookmark clicked`, `back to the magic menu`.

- [ ] **Step 6: 커밋**

```bash
git add -A plugins/spelllearning/src/magicmenu plugins/spelllearning/include/magicmenu plugins/spelllearning/src/HudNotify.cpp plugins/spelllearning/CMakeLists.txt SKSE/Plugins/SpellLearning/magicmenu_paths.json
git commit -m "Magic menu: XP ring and tag chips by the selected spell's name (tree spells only), redrawn when the selection changes; chip names from the panel's lang file"
```

---

### Task 5: 문서

**Files:**
- Modify: `docs/ARCHITECTURE.md` (새 시스템 절: 이벤트 → 경로 → 책갈피·고리, 데이터 파일, 반환 표시)
- Modify: `docs/DESIGN.md` (마법 메뉴 책갈피와 고리의 모양, 두 스위치와의 관계)
- Modify: `docs/TRANSLATING.md` (칩 이름이 마법 메뉴에도 쓰인다는 한 줄. 새 키는 없음)
- Modify: `CHANGELOG.md` 1.4.0
- Modify: `CLAUDE.md` 문서 결정 트리 ("마법 메뉴 책갈피 → docs/ARCHITECTURE.md의 해당 절, 경로 추가는 magicmenu_paths.json")
- Modify: `docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md` 상태를 "구현됨"으로

- [ ] **Step 1: 위 문서를 고친다.** `magicmenu_paths.json`에 새 메뉴 모드 경로를 더하는 법(한 줄, 다시 빌드 없음)을 ARCHITECTURE.md에 적는다.
- [ ] **Step 2: `node run-tests.js`** (`PrismaUI/views/SpellLearning/SpellLearningPanel`에서) — JS는 바뀌지 않았지만 `panelOpenSettingTest.js`가 여전히 통과하는지. 기대: 0 failed.
- [ ] **Step 3: 커밋**

```bash
git add docs CHANGELOG.md CLAUDE.md
git commit -m "Docs: magic menu bookmark (architecture, design, translating, changelog, decision tree)"
```

---

## 확인하지 않은 것 (구현 전에 알아 둘 것)

- 카드 경로, 제목 필드 이름(`ItemName`), SkyUI에서 `GetSelectedItem()`의 정확성, 그림 불러오기는 모두 짐작이고 Task 1이 답한다.
- `GFxMovieView::GetVariable`, `SKSE::TaskInterface::AddUITask`, `RE::make_gptr`, `CreateEmptyMovieClip`의 깊이 `-1`의 뜻은 헤더에서 아직 읽지 않았다. 각 Step에 확인하라고 적어 두었다.
- 바닐라 UI(SkyUI 없음) 확인은 TAKEALOOK이 SkyUI를 쓰므로 따로 시험 프로필이 필요하다. 지금 계획은 SkyUI 경로만 게임으로 확인한다.
