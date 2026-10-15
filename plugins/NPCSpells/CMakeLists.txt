# ==========================================================================
# HeartOfMagic_NPCSpells -- NPC Spells addon
# ==========================================================================
# Fills the addon ESL's leveled spell lists from the Heart of Magic tree at
# kDataLoaded. CommonLibSSE-NG, compiler flags and vcpkg packages come from
# the top-level CMakeLists.txt super-build (same shape as plugins/BookXP).
# The core (src/core) is pure C++ shared with tools/npcspells-test.
# ==========================================================================

project(HeartOfMagic_NPCSpells VERSION ${NPCSPELLS_VERSION} DESCRIPTION "Heart of Magic - NPC Spells" LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()

# TomeReachConfig.cpp calls TomeReach::LowerAscii (TomeReachRecords.cpp), which
# includes <zlib.h>: the pair builds as in the main plugin and tools/tome-reach-test.
# EncodingUtils.cpp: PluginNames (plugin names with non-ASCII bytes in persistentIds), as SpellScannerFormId.cpp.
set(NPCSPELLS_SOURCES
    src/Main.cpp
    src/core/NpcSpellsRules.cpp src/core/NpcSpellsClassTier.cpp src/core/NpcSpellsPool.cpp src/core/NpcSpellsSlots.cpp
    src/core/NpcSpellsBuild.cpp src/core/NpcSpellsEsl.cpp src/core/NpcSpellsTemplate.cpp src/core/NpcSpellsNpc.cpp
    src/core/NpcSpellsReport.cpp src/core/NpcSpellsDistr.cpp
    src/game/GameFiles.cpp src/game/GameLists.cpp src/game/GamePool.cpp src/game/GameNpcs.cpp src/game/GameRun.cpp
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src/EncodingUtils.cpp
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src/tomereach/TomeReachConfig.cpp
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src/tomereach/TomeReachRecords.cpp
)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME ${PROJECT_NAME}
    AUTHOR "HeartOfMagic"
    USE_ADDRESS_LIBRARY
    MINIMUM_SKSE_VERSION "2.0.12"
    VERSION ${PROJECT_VERSION}
    SOURCES ${NPCSPELLS_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/include
)

target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../PCH.h)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
)

target_compile_definitions(${PROJECT_NAME} PRIVATE NPCSPELLS_VERSION="${PROJECT_VERSION}")

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "HeartOfMagic_NPCSpells")
