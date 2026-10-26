# ==========================================================================
# SL_MagicMenu -- magic menu prototype (ACF M1-0)
# ==========================================================================
# A separate DLL drawn into SkyUI's magic menu: progress card, black preview
# box, a small spell-tree window. PROTOTYPE: reads HoM through API v2 and the
# saved spell_tree.json directly; both are replaced by the I1 contract.
# Design: C:\Users\qtuna\Desktop\Claude outputs\ACF-Phase0\M0\M0_MAGIC_MENU_DESIGN.md
# CommonLibSSE-NG, compiler flags, and vcpkg packages come from the
# top-level CMakeLists.txt super-build.
# ==========================================================================

project(
    SL_MagicMenu
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "Heart of Magic - magic menu prototype"
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
        src/Hom.cpp
        src/Spells.cpp
        src/TreeData.cpp
        src/HomStyle.cpp
        src/Layout.cpp
        src/Widget.cpp
        src/Preview.cpp
        src/Input.cpp
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
    OUTPUT_NAME "SL_MagicMenu"
)
