project(
    treebuilder-test
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "Standalone test harness for TreeBuilder algorithms"
    LANGUAGES CXX
)

set(SL_SRC_DIR "${CMAKE_SOURCE_DIR}/plugins/spelllearning/src")
set(SL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/plugins/spelllearning/include")

include_directories(${SL_INCLUDE_DIR})

add_executable(${PROJECT_NAME}
    treebuilder-test.cpp
    ${SL_SRC_DIR}/treebuilder/TreeNLP.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderCore.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderLinks.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderPlugins.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderReachability.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderSimilarity.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderClassic.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderTypeGrouping.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderThemes.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderBridges.cpp
    ${SL_SRC_DIR}/treebuilder/SimdKernels.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

# Stub headers (PCH.h, Common.h) MUST come first so they shadow the real
# plugin headers that pull in RE/Skyrim.h and SKSE/SKSE.h.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
    ${SL_SRC_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    hwy::hwy
    hwy::hwy_contrib
)

# Highway foreach_target dispatch must not be merged into unity build groups
set_source_files_properties(${SL_SRC_DIR}/treebuilder/SimdKernels.cpp PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON
)

# =============================================================================
# librarian-test  -  offline harness for the tag librarian
# =============================================================================
# Compiles the same classifier the plugin uses, so the coverage and precision
# it reports are the plugin's own numbers, not a second implementation's.

add_executable(librarian-test
    librarian-test.cpp
    librarian-selftest.cpp
    librarian-adapters-selftest.cpp
    environment-selftest.cpp
    librarian-score.cpp
    librarian-vocab-check.cpp
    librarian-adapters.cpp
    librarian-spellresearch.cpp
    ${SL_SRC_DIR}/librarian/LibrarianRules.cpp
    ${SL_SRC_DIR}/librarian/LibrarianAdapters.cpp
    ${SL_SRC_DIR}/librarian/LibrarianAdapterMatch.cpp
    ${SL_SRC_DIR}/librarian/LibrarianAdapterDamage.cpp
    ${SL_SRC_DIR}/librarian/LibrarianSpellResearch.cpp
    ${SL_SRC_DIR}/librarian/LibrarianClassify.cpp
    ${SL_SRC_DIR}/librarian/LibrarianCatalog.cpp
    ${SL_SRC_DIR}/librarian/LibrarianTraits.cpp
)

target_compile_features(librarian-test PRIVATE cxx_std_23)

# Stub headers first, same reason as above.
target_include_directories(librarian-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
    ${SL_SRC_DIR}
)

target_link_libraries(librarian-test PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# =============================================================================
# tome-reach-test  -  offline harness for the tome reach pass
# =============================================================================
# The plugin's own TomeReach sources: which tree spells have tomes nothing in
# the load order hands out (inputs from lab/tome-reach/make_inputs.py).

add_executable(tome-reach-test
    tome-reach-test.cpp
    tome-reach-selftest.cpp
    ${SL_SRC_DIR}/tomereach/TomeReachRecords.cpp
    ${SL_SRC_DIR}/tomereach/TomeReachConfig.cpp
    ${SL_SRC_DIR}/tomereach/TomeReachPass.cpp
)

target_compile_features(tome-reach-test PRIVATE cxx_std_23)

target_include_directories(tome-reach-test PRIVATE
    ${SL_SRC_DIR}
    ${SL_INCLUDE_DIR}
)

target_link_libraries(tome-reach-test PRIVATE
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
)

# =============================================================================
# declutter-test  -  offline harness for the native tree declutter pass
# =============================================================================
# The plugin's own LayoutDeclutter sources, so a tree run here comes out as it
# does for the panel; compare with modules/layoutDeclutter.js on the same tree.

add_executable(declutter-test
    declutter-test.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutDeclutter.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineClear.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineClearCost.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineGrid.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutMath.cpp
)

target_compile_features(declutter-test PRIVATE cxx_std_23)

# Stub headers first, same reason as above.
target_include_directories(declutter-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
    ${SL_SRC_DIR}
)

target_link_libraries(declutter-test PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# =============================================================================
# tree-save-check  -  offline check of SaveSpellTree's no-spells guard
# =============================================================================
# The plugin's own TreeSaveCheck.h (header only, no game types): --self-test on
# made-up trees, or tree files checked against a full parse.

add_executable(tree-save-check
    tree-save-check.cpp
)

target_compile_features(tree-save-check PRIVATE cxx_std_23)

target_include_directories(tree-save-check PRIVATE
    ${SL_INCLUDE_DIR}
)

target_link_libraries(tree-save-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# tree-remap-check  -  offline check of a saved tree's load order fix
# =============================================================================
# The plugin's own TreeRemap.h (header only, no game types), the part of
# SpellScanner::ValidateAndFixTree that moves formIds and links: --self-test on
# made-up trees and load orders (shift, swap, legacy nodes, removed spells).

add_executable(tree-remap-check
    tree-remap-check.cpp
)

target_compile_features(tree-remap-check PRIVATE cxx_std_23)

target_include_directories(tree-remap-check PRIVATE
    ${SL_INCLUDE_DIR}
)

target_link_libraries(tree-remap-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# progression-check  -  offline check of the progression co-save and XP maths
# =============================================================================
# The plugin's own ProgressionCoSave.h and ProgressionMath.h (header only, no game
# types): --self-test writes and reads SLPR v1-v3, SLPX and SLTX records in byte
# buffers (cut short, over the limits) and runs the cap, study and mastery numbers.

add_executable(progression-check
    progression-check.cpp
)

target_compile_features(progression-check PRIVATE cxx_std_23)

target_include_directories(progression-check PRIVATE
    ${SL_INCLUDE_DIR}
)

# =============================================================================
# tree-prereq-check  -  offline check of the tree prerequisites game start applies
# =============================================================================
# The plugin's own TreePrereqEntries.h (header only, no game types): the array the
# panel's fast path sends to SetTreePrerequisites. --self-test on made-up trees, or
# a tree file's array printed for a comparison with the panel's JS.

add_executable(tree-prereq-check
    tree-prereq-check.cpp
)

target_compile_features(tree-prereq-check PRIVATE cxx_std_23)

target_include_directories(tree-prereq-check PRIVATE
    ${SL_INCLUDE_DIR}
)

target_link_libraries(tree-prereq-check PRIVATE
    nlohmann_json::nlohmann_json
)

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
# and BlockRegistry.cpp (the add-on block registration checks, Task 6) on made-up cases. One PASS/FAIL line per check and a total; exit 1 when any fails.

add_executable(magic-menu-layout-check
    magic-menu-layout-check.cpp
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
# cancel rules, the 'SLMH' / 'SLMS' / 'SLMV' messages and their queues, the key queue,
# the v3 content box, visible part and overlay clears. One PASS/FAIL line per check and
# a total; --selftest the same; exit 1 when any check fails.

add_executable(magic-menu-block-api-check
    magic-menu-block-api-check.cpp
    magic-menu-block-api-check-v3.cpp
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
# SL_MagicMenu.ini (the shipped dist ini and made-up ones), the registered heights,
# the pick rules (bug G1), the strings' fill and key labels, the cards' height
# requests, and the tree window / progress card / node card arithmetic moved here
# from magic-menu-preview-check (HoM add-on plan Task 6). The core's src is on the
# include path for Settings.h, LayoutMath.h, BlockRules.h and BlockApi.h. One
# PASS/FAIL line per check and a total; --selftest the same; exit 1 when any fails.

add_executable(magic-menu-hom-block-check
    magic-menu-hom-block-check.cpp
)

target_compile_features(magic-menu-hom-block-check PRIVATE cxx_std_23)

target_include_directories(magic-menu-hom-block-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/src
    ${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/src
)

target_compile_definitions(magic-menu-hom-block-check PRIVATE
    HOM_BLOCK_DIST_INI="${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/dist/SL_MagicMenu.ini"
)

target_link_libraries(magic-menu-hom-block-check PRIVATE
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
