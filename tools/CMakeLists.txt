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
    librarian-score.cpp
    librarian-vocab-check.cpp
    librarian-adapters.cpp
    librarian-spellresearch.cpp
    ${SL_SRC_DIR}/librarian/LibrarianRules.cpp
    ${SL_SRC_DIR}/librarian/LibrarianAdapters.cpp
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
# toxicology-test  -  offline harness for the Toxicology addon core
# =============================================================================
# The addon's own core (src/core, no game types): settings, poison spell keys,
# top poison, magicka per dose, coat interval, catalog key split, hit and cost spell sets,
# the tome reward decision and its co-save flag (toxicology-test-reward.cpp), and the English / Korean
# ESL check on the committed pair and on broken copies (toxicology-test-esl.cpp).
# --self-test, exit 1 on a FAIL.

set(TOXICOLOGY_SRC_DIR "${CMAKE_SOURCE_DIR}/plugins/Toxicology/src")

add_executable(toxicology-test
    toxicology-test.cpp
    toxicology-test-reward.cpp
    toxicology-test-esl.cpp
    toxicology-esl-compare.cpp
    ${TOXICOLOGY_SRC_DIR}/core/ToxCore.cpp
)

target_compile_features(toxicology-test PRIVATE cxx_std_23)

target_include_directories(toxicology-test PRIVATE
    ${TOXICOLOGY_SRC_DIR}
)

target_compile_definitions(toxicology-test PRIVATE
    TOXICOLOGY_DATA_DIR="${CMAKE_SOURCE_DIR}/plugins/Toxicology/data"
)

target_link_libraries(toxicology-test PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# toxicology-esl-check  -  the Korean Toxicology ESL still matches the English one
# =============================================================================
# toxicology-esl-check <english.esp> <korean.esp>: the two must be byte-identical outside the ten text
# subrecords of plugins/Toxicology/ESL.md "Korean ESL". DistributionToxicology.cmake runs it before the zips.

add_executable(toxicology-esl-check
    toxicology-esl-check.cpp
    toxicology-esl-compare.cpp
)

target_compile_features(toxicology-esl-check PRIVATE cxx_std_23)
