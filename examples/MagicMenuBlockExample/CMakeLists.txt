# ==========================================================================
# MagicMenuBlockExample - an example add-on block for Magic Menu Rewrite
#   (Block API v3; shipped as MagicMenuBlockExample.dll; not part of any release)
# ==========================================================================
# Builds two ways:
#   - inside the HeartOfMagic super-build (root CMakeLists.txt adds this folder):
#     CommonLibSSE-NG, the compiler flags and the vcpkg packages come from there;
#   - on its own (this folder as the source dir): CommonLibSSE-NG as a source tree,
#     by default a clone of v10.0.0 at external/CommonLibSSE-NG, or any checkout given
#     with -DCOMMONLIBSSE_PATH=...; its packages come from vcpkg (the vcpkg.json and
#     vcpkg-configuration.json beside this file, the vcpkg toolchain, triplet
#     x64-windows-static - the default here). See docs/magicmenu/BLOCK_API_GUIDE.md, "Building".
# ==========================================================================

cmake_minimum_required(VERSION 3.25)

# On its own: vcpkg's triplet defaults to x64-windows-static (the static MSVC runtime, as CMAKE_MSVC_RUNTIME_LIBRARY
# below). It has to be set before project(), where the vcpkg toolchain reads it; PROJECT_IS_TOP_LEVEL is not known yet
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "vcpkg triplet (the static MSVC runtime)")
endif()

project(
    MagicMenuBlockExample
    VERSION 1.0.0
    DESCRIPTION "Example add-on block for Magic Menu Rewrite"
    LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()

if(PROJECT_IS_TOP_LEVEL)
    # What the super-build would give: C++23, the static MSVC runtime and the flags
    # CommonLibSSE-NG's headers need (/Zc:preprocessor: its STATIC_ASSERT_SIZE macro)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(MSVC)
        add_compile_options(/permissive- /Zc:preprocessor /utf-8)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()

    # CommonLibSSE-NG from source, the way HeartOfMagic's plugins/cmake/commonlibsse.cmake adds it
    set(COMMONLIBSSE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/CommonLibSSE-NG" CACHE PATH
        "A CommonLibSSE-NG source tree (default: external/CommonLibSSE-NG)")
    if(NOT EXISTS "${COMMONLIBSSE_PATH}/CMakeLists.txt" OR NOT EXISTS "${COMMONLIBSSE_PATH}/cmake/CommonLibSSE.cmake")
        message(FATAL_ERROR
            "No CommonLibSSE-NG source tree at \"${COMMONLIBSSE_PATH}\". Get the version this example was built with:\n"
            "  git clone --branch v10.0.0 --recursive https://github.com/alandtse/CommonLibVR.git external/CommonLibSSE-NG\n"
            "or pass -DCOMMONLIBSSE_PATH=<your checkout>.")
    endif()
    set(BUILD_TESTS OFF CACHE BOOL "No CommonLibSSE-NG unit tests" FORCE)
    set(ENABLE_SKYRIM_SE ON CACHE BOOL "Skyrim SE support" FORCE)
    set(ENABLE_SKYRIM_AE ON CACHE BOOL "Skyrim AE support (SKSE::log::init writes a file only with it)" FORCE)
    set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Skyrim VR support (MagicMenuRewrite is built for SE and AE)" FORCE)
    set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "Trampoline support through Xbyak" FORCE)
    # the Trampoline patch-site check is unused here; on, every fresh configure fetches MinHook's hde64 from GitHub
    set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "Validate Trampoline patch sites (unused here)" FORCE)
    add_subdirectory("${COMMONLIBSSE_PATH}" "${CMAKE_BINARY_DIR}/CommonLibSSE" EXCLUDE_FROM_ALL)
    include("${COMMONLIBSSE_PATH}/cmake/CommonLibSSE.cmake")  # add_commonlibsse_plugin
    find_package(spdlog CONFIG REQUIRED)
endif()

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME "MagicMenuBlockExample"  # the SKSE plugin name: the Result's receiver and the log file's name
    AUTHOR "HeartOfMagic"
    USE_ADDRESS_LIBRARY
    VERSION ${PROJECT_VERSION}
    SOURCES
        src/main.cpp
        src/BlockApi.h
        src/PCH.h
)

if(NOT PROJECT_IS_TOP_LEVEL)
    # In the super-build: built only when asked for (--target MagicMenuBlockExample), not by every ALL build. Set as a
    # property: add_commonlibsse_plugin's EXCLUDE_FROM_ALL option passes a generator expression to add_library, which
    # add_library takes as a source, not as its keyword
    set_target_properties(${PROJECT_NAME} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
endif()
# PCH.h also gives the generated plugin declaration the std::literals it needs
target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h")
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
