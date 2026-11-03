cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# ============================================================================
# HeartOfMagic - Top-Level Super-Build
# ============================================================================
# All sub-projects share:
#   - CommonLibSSE-NG (built once from plugins/external/commonlibsse-ng/)
#   - vcpkg dependencies (single install at root)
#   - Compiler flags (plugins/cmake/CompilerFlags.cmake)
#   - Static MSVC runtime
# ============================================================================
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION)
project(HeartOfMagic VERSION ${PROJECT_VERSION} LANGUAGES CXX)

# Prevent in-source builds
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()

# ============================================================================
# Shared Build Configuration
# ============================================================================

add_definitions(-D_WIN32_WINNT=0x0A00)

# Static runtime for all targets
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Shared cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/plugins/cmake")


# Apply compiler flags globally (MSVC optimizations, /EHa, /GL, linker flags)
include(CompilerFlags)

# ============================================================================
# CommonLibSSE-NG (built once, shared by all targets)
# ============================================================================

include(commonlibsse)

# ============================================================================
# Shared vcpkg packages
# ============================================================================

find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(xbyak CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# ============================================================================
# Sub-projects
# ============================================================================

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist")
# The release's folder and zip: HeartOfMagic-Librarian_<version> (the DLLs keep their names)
set(DIST_NAME "HeartOfMagic-Librarian")
set(DIST_VERSION_DIR "${DIST_DIR}/${DIST_NAME}_${PROJECT_VERSION}")
# The magic menu (plugins/SL_MagicMenu, shipped as Magic Menu Rewrite: MagicMenuRewrite.dll) ships
# apart, with its own version: its DLL version, fomod_magicmenu's info.xml and
# dist/MagicMenuRewrite_<version>. Since 1.2.0 (2026-11-04) it ships the core and the Block API
# only; each block ships with its mod (plugins/cmake/Distribution.cmake, MagicMenuDistribution.cmake):
#   - the Heart of Magic block (plugins/SL_MagicMenu_HoM, MagicMenuRewrite_HoM.dll) in Heart of
#     Magic Librarian's release, an option of its FOMOD, with HoM's version (PROJECT_VERSION)
#   - the Spell Research block (plugins/SL_MagicMenu_SpellResearch, MagicMenuRewrite_SpellResearch.dll)
#     in "Spell Research - Prisma UI 2", which takes dist/SpellResearchBlock_<version>/ from here;
#     its DLL carries that release's version, SPELL_RESEARCH_BLOCK_VERSION (2.0.0 is a placeholder
#     until that release is numbered). Its notices name the source tag spell-research-block-v<version>,
#     made at release on the commit the package was built from (docs/magicmenu/BLOCKS_MOVE.md,
#     "Releasing the Spell Research block")
set(MAGIC_MENU_VERSION "1.2.0")
set(MAGIC_MENU_DIST_VERSION_DIR "${DIST_DIR}/MagicMenuRewrite_${MAGIC_MENU_VERSION}")
set(HOM_VERSION "${PROJECT_VERSION}")
set(SPELL_RESEARCH_BLOCK_VERSION "2.0.0")
set(SPELL_RESEARCH_BLOCK_DIST_DIR "${DIST_DIR}/SpellResearchBlock_${SPELL_RESEARCH_BLOCK_VERSION}")

add_subdirectory(plugins/spelllearning)
add_subdirectory(plugins/DummyDEST)
add_subdirectory(plugins/BookXP)
add_subdirectory(plugins/SL_MagicMenu)
add_subdirectory(plugins/SL_MagicMenu_SpellResearch)
add_subdirectory(plugins/SL_MagicMenu_HoM)
add_subdirectory(examples/MagicMenuBlockExample)  # Block API example for mod authors (docs/magicmenu/BLOCK_API_GUIDE.md); not shipped
add_subdirectory(tools)

# Generate fomod/info.xml from template with current version
configure_file(
    "${CMAKE_SOURCE_DIR}/fomod/info.xml.in"
    "${CMAKE_SOURCE_DIR}/fomod/info.xml"
    @ONLY
)
# and the magic menu's fomod_magicmenu/fomod/info.xml with its own version
configure_file(
    "${CMAKE_SOURCE_DIR}/fomod_magicmenu/fomod/info.xml.in"
    "${CMAKE_SOURCE_DIR}/fomod_magicmenu/fomod/info.xml"
    @ONLY
)

# Papyrus script compilation
include(Papyrus)

# Distribution folder assembly
include(Distribution)
# and the magic menu's: Magic Menu Rewrite's release and the Spell Research block's package
include(MagicMenuDistribution)
