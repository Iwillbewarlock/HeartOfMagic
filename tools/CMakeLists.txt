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
    scanrecord-selftest.cpp
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
# file-utils-check  -  offline check of FileUtils::WriteAtomically
# =============================================================================
# The plugin's own FileUtils.h (header only): the file stays there at every step
# (the .bak a copy, the temp file moved onto it, which this build's rename does in
# one step), and a failed write leaves it as it was. --self-test in a temp folder.

add_executable(file-utils-check
    file-utils-check.cpp
)

target_compile_features(file-utils-check PRIVATE cxx_std_23)

# Stub headers first (logger): FileUtils.h logs through the plugin's PCH
target_include_directories(file-utils-check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
)

target_link_libraries(file-utils-check PRIVATE
    spdlog::spdlog
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
# buffers (cut short, over the limits) and runs the cap, study and mastery numbers;
# ConfigRenames.h moves renamed settings (passive XP per game day).

add_executable(progression-check
    progression-check.cpp
)

target_compile_features(progression-check PRIVATE cxx_std_23)

target_include_directories(progression-check PRIVATE
    ${SL_INCLUDE_DIR}
)

target_link_libraries(progression-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# spell-tomes-check  -  offline check of the pairs for tomes that teach nothing
# =============================================================================
# The plugin's own SpellTomePairs.h (header only, no game types): --self-test reads
# made-up SynthesizedPatch.json texts and pairs them with made-up Spell Research
# lists (JSON first, live lists unverified, a re-run patch's shifted lists).

add_executable(spell-tomes-check
    spell-tomes-check.cpp
)

target_compile_features(spell-tomes-check PRIVATE cxx_std_23)

target_include_directories(spell-tomes-check PRIVATE
    ${SL_INCLUDE_DIR}
)

target_link_libraries(spell-tomes-check PRIVATE
    nlohmann_json::nlohmann_json
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
# progression-notify-check  -  offline check of the H1-d notice queue
# =============================================================================
# The plugin's own ProgressionNotifyQueue.h (header only, no game types): held
# while a Batch is open, sent in posting order when the outermost one closes.

add_executable(progression-notify-check
    progression-notify-check.cpp
)

target_compile_features(progression-notify-check PRIVATE cxx_std_23)

target_include_directories(progression-notify-check PRIVATE
    ${SL_INCLUDE_DIR}
)

# =============================================================================
# thread-flag-check  -  offline check of IsOnGameThread()
# =============================================================================
# The plugin's own GameThreadFlag.h (header only): a thread is on the game thread
# inside a task or message scope, and after it has run one; never another thread.

add_executable(thread-flag-check
    thread-flag-check.cpp
)

target_compile_features(thread-flag-check PRIVATE cxx_std_23)

target_include_directories(thread-flag-check PRIVATE
    ${SL_INCLUDE_DIR}
)

# =============================================================================
# graft-check  -  offline check of the tree graft
# =============================================================================
# The plugin's own GraftCandidates.h and TreeGraft.cpp on the treebuilder-test
# sources: --self-test on made-up trees and scans (what is new, the panel's
# candidate rule, every existing node kept, the parent a new spell gets).

add_executable(graft-check
    graft-check.cpp
    ${SL_SRC_DIR}/treebuilder/TreeGraft.cpp
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

target_compile_features(graft-check PRIVATE cxx_std_23)

# Stub headers first, same reason as above.
target_include_directories(graft-check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
    ${SL_SRC_DIR}
)

target_link_libraries(graft-check PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    hwy::hwy
    hwy::hwy_contrib
)

# =============================================================================
# graft-run-check  -  offline check of the graft's runner at the main menu
# =============================================================================
# The plugin's own TreeGraftRun.h (when it runs, the player's rules, the seen
# list, the record, the id refresh, the declutter of the grafted nodes and the
# verification) on graft-check's sources and the declutter's.

add_executable(graft-run-check
    graft-run-check.cpp
    graft-run-check-record.cpp
    ${SL_SRC_DIR}/treebuilder/TreeGraft.cpp
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
    ${SL_SRC_DIR}/treebuilder/LayoutDeclutter.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineClear.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineClearCost.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineGrid.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutMath.cpp
)

target_compile_features(graft-run-check PRIVATE cxx_std_23)

# Stub headers first, same reason as above.
target_include_directories(graft-run-check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
    ${SL_SRC_DIR}
)

target_link_libraries(graft-run-check PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    hwy::hwy
    hwy::hwy_contrib
)
