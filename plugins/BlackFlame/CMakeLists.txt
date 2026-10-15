# ==========================================================================
# HeartOfMagic_BlackFlame -- "흑염 달구기" prototype
# ==========================================================================
# While the player's Stoke buff (HeartOfMagic_BlackFlame.esp) is active, the
# visuals of the player's own fire spells turn grey-black. A separate
# prototype: not part of the Heart of Magic release (not in Distribution.cmake
# or the FOMOD). CommonLibSSE-NG, compiler flags and vcpkg packages come from
# the top-level CMakeLists.txt super-build (same shape as plugins/BookXP).
# ==========================================================================

project(HeartOfMagic_BlackFlame VERSION ${PROJECT_VERSION} DESCRIPTION "Heart of Magic - Black Flame prototype" LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()

# EncodingUtils.cpp: PluginNames (plugin names with non-ASCII bytes in the
# catalog's persistentIds), as plugins/spelllearning SpellScannerFormId.cpp.
set(BLACKFLAME_SOURCES
    src/Main.cpp
    src/Config.cpp
    src/Forms.cpp
    src/FireSet.cpp
    src/Palette.cpp
    src/BuffState.cpp
    src/EffectEvents.cpp
    src/HitEffectHook.cpp
    src/HitArt.cpp
    src/HazardHook.cpp
    src/Attribution.cpp
    src/PlayerArt.cpp
    src/ScriptTags.cpp
    src/PapyrusWrap.cpp
    src/Recolor.cpp
    src/SehGuard.cpp
    src/ParticleColors.cpp
    src/CasterArt.cpp
    src/ProjectileHook.cpp
    src/ExplosionHook.cpp
    src/SelfTest.cpp
    src/SelfTestLog.cpp
    src/SelfTestJudge.cpp
    src/SelfTestProbe.cpp
    src/SelfTestSteps.cpp
    src/SelfTestStepsLate.cpp
    src/SelfTestStepsRemote.cpp
    src/SelfTestText.cpp
    src/SelfTestWorld.cpp
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src/EncodingUtils.cpp
)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME ${PROJECT_NAME}
    AUTHOR "HeartOfMagic"
    USE_ADDRESS_LIBRARY
    MINIMUM_SKSE_VERSION "2.0.12"
    VERSION ${PROJECT_VERSION}
    SOURCES ${BLACKFLAME_SOURCES}
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

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "HeartOfMagic_BlackFlame")
