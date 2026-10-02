# ==========================================================================
# SL_Combat -- Heart of Magic combat addon (spell counter, chains, super)
# ==========================================================================
# CommonLibSSE-NG, compiler flags, and vcpkg packages are provided
# by the top-level CMakeLists.txt super-build.
# Milestone M1: the spell counter (docs/COMBAT.md).
# ==========================================================================

project(
    SL_Combat
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "Heart of Magic - spell counter and combat addon"
    LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME ${PROJECT_NAME}
    AUTHOR "HeartOfMagic"
    USE_ADDRESS_LIBRARY
    MINIMUM_SKSE_VERSION "2.0.12"
    VERSION ${PROJECT_VERSION}
    SOURCES
        src/Main.cpp
        src/CombatConfig.cpp
        src/CounterInput.cpp
        src/CounterKey.cpp
        src/ProjectileCounter.cpp
        src/CounterResponse.cpp
        src/WardSupport.cpp
        src/IncomingWarning.cpp
        src/CounterPrompt.cpp
        src/CombatMenu.cpp
        src/TestSpawn.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../PCH.h
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "SL_Combat"
)
