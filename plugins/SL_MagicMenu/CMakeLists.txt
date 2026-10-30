# ==========================================================================
# SL_MagicMenu -- the magic menu (ACF M1-0)
# ==========================================================================
# A separate DLL drawn into SkyUI's magic menu: the 3D preview box, our spell
# card, the board, the layout mode and the add-on blocks (BlockApi.h). No Heart
# of Magic since HoM add-on plan Task 5: its blocks are SL_MagicMenu_HoM's.
# Design: C:\Users\qtuna\Desktop\Claude outputs\ACF-Phase0\M0\M0_MAGIC_MENU_DESIGN.md
# CommonLibSSE-NG, compiler flags, and vcpkg packages come from the
# top-level CMakeLists.txt super-build.
# ==========================================================================

project(
    SL_MagicMenu
    VERSION ${MAGIC_MENU_VERSION}
    DESCRIPTION "Magic menu - 3D spell preview, spell card and movable blocks"
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
    VERSION ${MAGIC_MENU_VERSION}
    SOURCES
        src/Main.cpp
        src/GameThread.cpp
        src/Settings.cpp
        src/Layout.cpp
        src/BlockRegistry.cpp
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
