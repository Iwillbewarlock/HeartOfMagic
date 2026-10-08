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
# npcspells-test  -  offline harness for the NPC Spells addon core
# =============================================================================
set(NPCSPELLS_CORE_DIR "${CMAKE_SOURCE_DIR}/plugins/NPCSpells/src")
set(NPCSPELLS_CORE_SOURCES
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsRules.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsClassTier.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsPool.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsSlots.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsBuild.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsEsl.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsTemplate.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsNpc.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsReport.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsDistr.cpp
)
add_executable(npcspells-test
    npcspells-test.cpp
    npcspells-selftest-rules.cpp
    npcspells-selftest-build.cpp
    npcspells-selftest-npc.cpp
    npcspells-selftest-esl.cpp
    npcspells-selftest-distr.cpp
    ${SL_SRC_DIR}/tomereach/TomeReachConfig.cpp
    ${SL_SRC_DIR}/tomereach/TomeReachRecords.cpp
    ${NPCSPELLS_CORE_SOURCES}
)
target_compile_features(npcspells-test PRIVATE cxx_std_23)
target_include_directories(npcspells-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NPCSPELLS_CORE_DIR}
    ${SL_SRC_DIR}
)
target_link_libraries(npcspells-test PRIVATE spdlog::spdlog nlohmann_json::nlohmann_json ZLIB::ZLIB)
# The rules suite reads the shipped JSON by absolute path, so the harness runs from any directory
target_compile_definitions(npcspells-test PRIVATE NPCSPELLS_DATA_DIR="${CMAKE_SOURCE_DIR}/plugins/NPCSpells/data")

# npcspells-esl  -  writes HeartOfMagic_NPCSpells.esp (empty LVSP records at the slot table's FormIDs)
add_executable(npcspells-esl
    npcspells-esl.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsSlots.cpp
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsEsl.cpp
)
target_compile_features(npcspells-esl PRIVATE cxx_std_23)
target_include_directories(npcspells-esl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NPCSPELLS_CORE_DIR}
    ${SL_SRC_DIR}
)
target_link_libraries(npcspells-esl PRIVATE spdlog::spdlog nlohmann_json::nlohmann_json)

set(NPCSPELLS_ESL_DIR "${CMAKE_BINARY_DIR}/npcspells")
set(NPCSPELLS_ESL_FILE "${NPCSPELLS_ESL_DIR}/HeartOfMagic_NPCSpells.esp" CACHE INTERNAL "generated ESL")
add_custom_command(OUTPUT "${NPCSPELLS_ESL_FILE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${NPCSPELLS_ESL_DIR}"
    COMMAND npcspells-esl -o "${NPCSPELLS_ESL_FILE}"
    DEPENDS npcspells-esl
    COMMENT "Writing HeartOfMagic_NPCSpells.esp from the slot table"
    VERBATIM)
add_custom_target(npcspells_esl ALL DEPENDS "${NPCSPELLS_ESL_FILE}")
