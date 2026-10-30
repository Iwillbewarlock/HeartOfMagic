# ==========================================================================
# SL_MagicMenu_HoM -- the Heart of Magic blocks for SL_MagicMenu
# ==========================================================================
# The spell tree window, the learning progress card and the node card as three
# add-on blocks of SL_MagicMenu (Block API v3), reading Heart of Magic's API v2
# and spell_tree.json. The magic menu itself has no Heart of Magic since HoM
# add-on plan Task 5. Design: docs/superpowers/specs/2026-10-30-magic-menu-hom-addon-design.md
# The host's shared headers (BlockApi.h, Draw.h, TextFit.h, LayoutMath.h,
# SpellSchool.h, Settings.h for its Rect) and its GameThread.cpp come from
# plugins/SL_MagicMenu/src. CommonLibSSE-NG, compiler flags, and vcpkg packages
# come from the top-level CMakeLists.txt super-build.
# ==========================================================================

project(
    SL_MagicMenu_HoM
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "Heart of Magic - spell tree, progress and node card blocks for the magic menu"
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
        src/HomSettings.cpp
        src/AddonState.cpp
        src/Frame.cpp
        src/ProgressCard.cpp
        src/NodeCard.cpp
        src/TreeMarks.cpp
        src/TreeDraw.cpp
        src/TreeLabels.cpp
        src/TreeInput.cpp
        src/Tooltip.cpp
        src/Hom.cpp
        src/Spells.cpp
        src/TreeData.cpp
        src/HomStyle.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../SL_MagicMenu/src/GameThread.cpp
)

# the add-on's own src first: its headers are found before the host's
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../SL_MagicMenu/src
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
    OUTPUT_NAME "SL_MagicMenu_HoM"
)

# --------------------------------------------------------------------------
# dist/SL_MagicMenu_HoM/ - the install folder, made on every build (as
# SL_MagicMenu_SpellResearch_dist, until the SL_MagicMenu FOMOD task):
#   SKSE/Plugins/SL_MagicMenu_HoM.dll (+ .pdb for crash logs)
# No ini of its own: the block reads its keys from SL_MagicMenu.ini.
# --------------------------------------------------------------------------
set(HOM_BLOCK_DIST "${CMAKE_SOURCE_DIR}/dist/SL_MagicMenu_HoM")
add_custom_target(SL_MagicMenu_HoM_dist ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${HOM_BLOCK_DIST}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${HOM_BLOCK_DIST}/SKSE/Plugins"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${HOM_BLOCK_DIST}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:${PROJECT_NAME}>>,copy,true>
        "$<$<BOOL:$<TARGET_PDB_FILE:${PROJECT_NAME}>>:$<TARGET_PDB_FILE:${PROJECT_NAME}>>"
        "${HOM_BLOCK_DIST}/SKSE/Plugins/"
    COMMENT "Assembling dist/SL_MagicMenu_HoM"
    VERBATIM
)
add_dependencies(SL_MagicMenu_HoM_dist ${PROJECT_NAME})
