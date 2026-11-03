# magicmenu.cmake - the magic menu's offline harnesses (split from tools/CMakeLists.txt
# 2026-11-04), included by it: the core (plugins/SL_MagicMenu), the Heart of Magic block
# (plugins/SL_MagicMenu_HoM) and the Spell Research block (plugins/SL_MagicMenu_SpellResearch).
# Each prints one PASS/FAIL line per check and a total; exit 1 when one fails
# (CLAUDE.md, docs/magicmenu/CHECKS.md).

# =============================================================================
# magic-menu-tree-check  -  offline check of the Heart of Magic block's tree window
# =============================================================================
# The SL_MagicMenu_HoM add-on's own TreeData.cpp and HomStyle.cpp (pure data, no
# game types; the core's until HoM add-on plan Task 5): parses a saved
# spell_tree.json and runs the tree window query with every node as the centre
# (window sizes, rotation sanity). --selftest on made-up trees. The core's src is
# on the include path for Settings.h, SelectionRule.h and LayoutMath.h.

add_executable(magic-menu-tree-check
    magic-menu-tree-check.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src/TreeData.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src/TreeDataLinks.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src/HomStyle.cpp
)

target_compile_features(magic-menu-tree-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-tree-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src
)

target_link_libraries(magic-menu-tree-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# magic-menu-layout-check  -  offline check of the magic menu prototype's block layout
# =============================================================================
# The SL_MagicMenu plugin's own Layout.cpp (no game types): the two snapping columns,
# hidden and fill blocks, the drop slot and move, scroll clamp and the layout JSON,
# BlockRegistry.cpp (the add-on block registration checks, Task 6) and HiddenBlocks.h ([Blocks] sHidden) on made-up cases. One PASS/FAIL line per check and a total; exit 1 when any fails.

add_executable(magic-menu-layout-check
    magic-menu-layout-check.cpp
    magic-menu-layout-check-hidden.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src/Layout.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src/BlockRegistry.cpp
)

target_compile_features(magic-menu-layout-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-layout-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src
)

target_link_libraries(magic-menu-layout-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# magic-menu-block-api-check  -  offline check of the magic menu's Block API v3 host side
# =============================================================================
# The SL_MagicMenu plugin's own BlockRegistry.cpp, BlockRules.h and BlockRulesV3.h (no
# game types): the registration parsing (v1 bytes in the frozen layout, v2 in full and
# cut short, SR's own v2 header bytes, v3 in full and cut at v2's size, version 4
# refused, neither draw refused, the tabs mask, v3's flags, pad and legacy ids), the
# tab bit of a SkyUI category and a block's visibility per tab, the press / release
# cancel rules, the 'SLMH' / 'SLMS' / 'SLMV' messages and their queues, the key queue, the
# 'SLMK' key hints (reading, the per-block list, the strip's buttons, the panel's growth),
# the v3 content box, visible part and overlay clears, the opening height ('SLMH' asked
# again after the first draw: the next menu opens at it) and Context::columnRoom. One
# PASS/FAIL line per check and a total; --selftest the same; exit 1 when any check fails.

add_executable(magic-menu-block-api-check
    magic-menu-block-api-check.cpp
    magic-menu-block-api-check-v3.cpp
    magic-menu-block-api-check-hints.cpp
    magic-menu-block-api-check-heights.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src/BlockRegistry.cpp
)

target_compile_features(magic-menu-block-api-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-block-api-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src
)

# =============================================================================
# magic-menu-sr-block-check  -  offline check of the Spell Research add-on block's pure parts
# =============================================================================
# The SL_MagicMenu_SpellResearch plugin's own SrBlockRules.h and ArchetypeIcons.h (header
# only, no game types): archetype grouping and order, what the block shows per state, the
# list and line layouts and their wrapping, the unknown archetype's frame-only icon, the
# icon data's bounds, the link click rule, reply code -> notification, the open request's
# requestId matching and 5 s drop; SrBlockText.h's sLayout / sLanguage ini reading and
# string fallbacks; and the shipped lang/en.json against the keys the block uses. One PASS/FAIL line per check and
# a total; --selftest the same; exit 1 when any check fails.

add_executable(magic-menu-sr-block-check
    magic-menu-sr-block-check.cpp
)

target_compile_features(magic-menu-sr-block-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-sr-block-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_SpellResearch/src
)

target_compile_definitions(magic-menu-sr-block-check PRIVATE
    SR_BLOCK_EN_JSON="${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_SpellResearch/dist/lang/en.json"
)

target_link_libraries(magic-menu-sr-block-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# magic-menu-hom-block-check  -  offline check of the Heart of Magic add-on block's pure parts
# =============================================================================
# The SL_MagicMenu_HoM add-on's own HomSettings.h, Selection.h, Strings.h and
# HomLayoutMath.h (header only, no game types): the block's keys read from
# MagicMenuRewrite.ini (the shipped dist ini and made-up ones), the registered heights,
# the pick rules (bug G1), the strings' fill and key labels, the cards' height
# requests, and the tree window / progress card / node card arithmetic moved here
# from magic-menu-preview-check (HoM add-on plan Task 6); the progress card's
# next-spell rows (ChoiceRules.h, 2026-11-03: order, rows that fit, rooms at four
# screen shapes, [Progress] iMaxChoices). The core's src is on the
# include path for Settings.h, LayoutMath.h, BlockRules.h and BlockApi.h. One
# PASS/FAIL line per check and a total; --selftest the same; exit 1 when any fails.

add_executable(magic-menu-hom-block-check
    magic-menu-hom-block-check.cpp
    magic-menu-hom-block-check-choices.cpp
)

target_compile_features(magic-menu-hom-block-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-hom-block-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src
)

target_compile_definitions(magic-menu-hom-block-check PRIVATE
    HOM_BLOCK_DIST_INI="${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/dist/MagicMenuRewrite.ini"
)

target_link_libraries(magic-menu-hom-block-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# magic-menu-lang-check  -  offline check of the magic menu's language files
# =============================================================================
# The core's WidgetStrings.h and the HoM block's Strings.h tables against their
# shipped dist/lang/en.json and ko.json (every key, no extra key, parse,
# {placeholders} and " · " separators equal in en, ko and the built-in text, the
# built-in text equal to en.json), and the shared LangFiles.h (plugins/SL_MagicMenu/src):
# [Display] sLanguage + the game's language -> the code, the file chain on the
# real and on made-up folders, the one load, the log lines; the shipped ini's
# [Display] sLanguage=auto. One PASS/FAIL line per check and a total; --selftest
# the same; exit 1 when any fails.

add_executable(magic-menu-lang-check
    magic-menu-lang-check.cpp
)

target_compile_features(magic-menu-lang-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-lang-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src
)

target_compile_definitions(magic-menu-lang-check PRIVATE
    MM_CORE_LANG_DIR="${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/dist/lang"
    MM_HOM_LANG_DIR="${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/dist/lang"
    MM_DIST_INI="${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/dist/MagicMenuRewrite.ini"
)

target_link_libraries(magic-menu-lang-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# magic-menu-homstyle-check  -  offline check of the Heart of Magic block's style reader
# =============================================================================
# The SL_MagicMenu_HoM add-on's own HomStyle.cpp (no game types; the core's until
# HoM add-on plan Task 5): Heart of Magic's
# config.json and design preset read as the plugin reads them, the colour, ink,
# shape and dash helpers, the state mapping and the name reveal rule (spec v11 T,
# R1, R7). --selftest on made-up input; a config path reads the real files.

add_executable(magic-menu-homstyle-check
    magic-menu-homstyle-check.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src/HomStyle.cpp
)

target_compile_features(magic-menu-homstyle-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-homstyle-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src
)

target_link_libraries(magic-menu-homstyle-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# magic-menu-preview-check  -  offline check of the magic menu prototype's preview math
# =============================================================================
# The SL_MagicMenu plugin's own PreviewMath.h (header only, no game types): the
# place, measure, grow, parent-space move, rebuilt-model rule and clip rect that
# Preview.cpp runs on the game's values, on made-up cases (spec v5 Q5). One
# PASS/FAIL line per check and a total; exit 1 when any check fails.

add_executable(magic-menu-preview-check
    magic-menu-preview-check.cpp
)

target_compile_features(magic-menu-preview-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-preview-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src
)
