# ==========================================================================
# HeartOfMagic_Toxicology -- Heart of Magic Librarian - Toxicology addon
# ==========================================================================
# CommonLibSSE-NG, compiler flags and vcpkg packages come from the top-level
# CMakeLists.txt super-build (same shape as plugins/BookXP). The core
# (src/core) is pure C++ shared with tools/toxicology-test.
# ==========================================================================

project(HeartOfMagic_Toxicology VERSION ${TOXICOLOGY_VERSION} DESCRIPTION "Heart of Magic Librarian - Toxicology" LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()

# EncodingUtils.cpp: PluginNames (plugin names with non-ASCII bytes in catalog keys), as SpellScannerFormId.cpp.
set(TOXICOLOGY_SOURCES
    src/Main.cpp
    src/core/ToxCore.cpp
    src/game/ToxData.cpp
    src/game/ToxSelection.cpp
    src/game/ToxApply.cpp
    src/game/ToxHits.cpp
    src/game/ToxMagicka.cpp
    src/game/ToxMagickaHook.cpp
    src/game/ToxBottles.cpp
    src/game/ToxCheck.cpp
    src/game/ToxSave.cpp
    src/game/ToxSelfTest.cpp
    src/game/ToxSelfTestSetup.cpp
    src/game/ToxSelfTestHits.cpp
    src/game/ToxSelfTestDoses.cpp
    src/game/ToxSelfTestReload.cpp
    src/game/ToxSelfTestShop.cpp
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src/EncodingUtils.cpp
)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME ${PROJECT_NAME}
    AUTHOR "Iwillbewarlock"
    USE_ADDRESS_LIBRARY
    MINIMUM_SKSE_VERSION "2.0.12"
    VERSION ${PROJECT_VERSION}
    SOURCES ${TOXICOLOGY_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/include
)

target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../PCH.h)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

target_compile_definitions(${PROJECT_NAME} PRIVATE TOXICOLOGY_VERSION="${PROJECT_VERSION}")

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "HeartOfMagic_Toxicology")
