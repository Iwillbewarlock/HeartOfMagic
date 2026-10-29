# ==========================================================================
# SL_MagicMenu_SpellResearch -- the Spell Research archetypes block for SL_MagicMenu
# ==========================================================================
# The first add-on block of SL_MagicMenu (Block API v2): the archetypes Spell
# Research files for the card's spell, and links to Spell Research Prisma's
# windows. Design: docs/superpowers/specs/2026-10-27-spell-research-addon-design.md
# CommonLibSSE-NG, compiler flags, and vcpkg packages come from the
# top-level CMakeLists.txt super-build.
# ==========================================================================

project(
    SL_MagicMenu_SpellResearch
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "Heart of Magic - Spell Research block for the magic menu"
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
        src/Settings.cpp
        src/Lang.cpp
        src/SrData.cpp
        src/BlockDraw.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../PCH.h
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "SL_MagicMenu_SpellResearch"
)

# --------------------------------------------------------------------------
# dist/SL_MagicMenu_SpellResearch/ - the install folder, made on every build
# (a hand folder like dist/HoM-MagicMenu-Proto until the SL_MagicMenu FOMOD task):
#   SKSE/Plugins/SL_MagicMenu_SpellResearch.dll (+ .pdb for crash logs)
#   SKSE/Plugins/SL_MagicMenu_SpellResearch.ini
#   SKSE/Plugins/SL_MagicMenu_SpellResearch/lang/*.json (en, ko)
# A custom target without OUTPUT, so it runs on every build: an ini or language
# change is copied even when the DLL did not relink.
# --------------------------------------------------------------------------
set(SR_BLOCK_DIST "${CMAKE_SOURCE_DIR}/dist/SL_MagicMenu_SpellResearch")
add_custom_target(SL_MagicMenu_SpellResearch_dist ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SR_BLOCK_DIST}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SR_BLOCK_DIST}/SKSE/Plugins/SL_MagicMenu_SpellResearch/lang"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${SR_BLOCK_DIST}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:${PROJECT_NAME}>>,copy,true>
        "$<$<BOOL:$<TARGET_PDB_FILE:${PROJECT_NAME}>>:$<TARGET_PDB_FILE:${PROJECT_NAME}>>"
        "${SR_BLOCK_DIST}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/dist/SL_MagicMenu_SpellResearch.ini" "${SR_BLOCK_DIST}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist/lang"
        "${SR_BLOCK_DIST}/SKSE/Plugins/SL_MagicMenu_SpellResearch/lang"
    COMMENT "Assembling dist/SL_MagicMenu_SpellResearch"
    VERBATIM
)
add_dependencies(SL_MagicMenu_SpellResearch_dist ${PROJECT_NAME})
