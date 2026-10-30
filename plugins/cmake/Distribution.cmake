# Distribution.cmake
# Assembles the release distribution folder after all targets are built.
# Requires: DIST_VERSION_DIR (from top-level CMakeLists.txt)
#           PAPYRUS_ISL_SOURCES, PAPYRUS_SPELLLEARNING_SOURCES, PAPYRUS_OUTPUT_DIR
#           (from Papyrus.cmake, if Papyrus compiler is available)

if(NOT DEFINED DIST_VERSION_DIR)
    message(STATUS "DIST_VERSION_DIR not defined - distribution assembly disabled")
    return()
endif()

# ============================================================================
# Distribution target - assembles release folder after all DLLs are built
# ============================================================================

add_custom_target(assemble_dist ALL
    COMMENT "Assembling release distribution...")

add_dependencies(assemble_dist SpellLearning DontEatSpellTomes SL_BookXP)

if(TARGET papyrus_build)
    add_dependencies(assemble_dist papyrus_build)
endif()

# ============================================================================
# 1. Clean and create directory structure
# ============================================================================

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_VERSION_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_VERSION_DIR}/fomod"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_VERSION_DIR}/SKSE/Plugins"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_VERSION_DIR}/Scripts/Source"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_VERSION_DIR}/optional/ISLPatch/SKSE/Plugins"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_VERSION_DIR}/optional/ISLPatch/Scripts/Source"
    VERBATIM
)

# ============================================================================
# 2. Copy DLLs; their debug symbols go to a separate archive
#    (<name>_DebugSymbols: SKSE/Plugins/*.pdb, for crash logs to name functions -
#    three quarters of the main archive's size if they went in it - and
#    SKSE/Plugins/SpellLearning/debug.json, which makes the panel's full scan
#    write the analysis dump too)
# ============================================================================

set(DIST_PDB_DIR "${DIST_VERSION_DIR}_DebugSymbols")

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_PDB_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_PDB_DIR}/SKSE/Plugins/SpellLearning"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_SOURCE_DIR}/debug/debug.json"
    "${DIST_PDB_DIR}/SKSE/Plugins/SpellLearning/"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:SpellLearning>"
    "${DIST_VERSION_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:SpellLearning>>,copy,true>
    "$<$<BOOL:$<TARGET_PDB_FILE:SpellLearning>>:$<TARGET_PDB_FILE:SpellLearning>>"
    "${DIST_PDB_DIR}/SKSE/Plugins/"

    COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:SL_BookXP>"
    "${DIST_VERSION_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:SL_BookXP>>,copy,true>
    "$<$<BOOL:$<TARGET_PDB_FILE:SL_BookXP>>:$<TARGET_PDB_FILE:SL_BookXP>>"
    "${DIST_PDB_DIR}/SKSE/Plugins/"

    COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:DontEatSpellTomes>"
    "${DIST_VERSION_DIR}/optional/ISLPatch/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:DontEatSpellTomes>>,copy,true>
    "$<$<BOOL:$<TARGET_PDB_FILE:DontEatSpellTomes>>:$<TARGET_PDB_FILE:DontEatSpellTomes>>"
    "${DIST_PDB_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying DLLs..."
    VERBATIM
)

# ============================================================================
# 3. Copy fomod files (info.xml is version-substituted by configure_file
#    in the top-level CMakeLists.txt)
# ============================================================================

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_SOURCE_DIR}/fomod/info.xml"
    "${DIST_VERSION_DIR}/fomod/"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_SOURCE_DIR}/fomod/ModuleConfig.xml"
    "${DIST_VERSION_DIR}/fomod/"
    # The MIT licence goes with every copy (at the archive's root: the installer
    # does not put it into Data)
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${DIST_VERSION_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying fomod files..."
    VERBATIM
)

# ============================================================================
# 3b. Third-party licenses. The DLLs statically link CommonLibSSE-NG
#     (GPL-3.0-or-later with the Modding and Linking Exceptions), so they go
#     out as a GPL combined work: THIRD-PARTY-NOTICES.md at the archive's root
#     says so and where the Corresponding Source is; licenses/ holds the texts
#     (CommonLibSSE-NG's under licenses/CommonLibSSE-NG/, each vcpkg library's
#     copyright file as licenses/<port>.txt). The project's own code stays MIT.
# ============================================================================

set(_commonlib_dir "${CMAKE_SOURCE_DIR}/plugins/external/commonlibsse-ng")
set(_licenses_dist "${DIST_VERSION_DIR}/licenses")
set(_vcpkg_share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
# The vcpkg ports whose code ends up in the DLLs (vcpkg.json, and CommonLib's DirectXTK)
set(_vcpkg_license_ports
    fmt spdlog nlohmann-json rapidcsv xbyak directxmath directxtk rapidfuzz-cpp highway zlib)

configure_file(
    "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md.in"
    "${CMAKE_BINARY_DIR}/THIRD-PARTY-NOTICES.md"
    @ONLY
)

set(_vcpkg_license_commands)
foreach(_port IN LISTS _vcpkg_license_ports)
    if(NOT EXISTS "${_vcpkg_share}/${_port}/copyright")
        message(FATAL_ERROR "Distribution: no license file for vcpkg port ${_port} at ${_vcpkg_share}/${_port}/copyright")
    endif()
    list(APPEND _vcpkg_license_commands
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${_vcpkg_share}/${_port}/copyright"
        "${_licenses_dist}/${_port}.txt")
endforeach()

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_licenses_dist}/CommonLibSSE-NG"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_BINARY_DIR}/THIRD-PARTY-NOTICES.md"
    "${DIST_VERSION_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${_commonlib_dir}/COPYING.txt"
    "${_commonlib_dir}/EXCEPTIONS.md"
    "${_commonlib_dir}/licenses/LICENSE-MIT.txt"
    "${_licenses_dist}/CommonLibSSE-NG/"
    ${_vcpkg_license_commands}
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying third-party licenses..."
    VERBATIM
)

# ============================================================================
# 4. Copy SKSE runtime data (presets, librarian, card icons)
# ============================================================================

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_SOURCE_DIR}/SKSE/Plugins/SpellLearning"
    "${DIST_VERSION_DIR}/SKSE/Plugins/SpellLearning"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying SKSE runtime data..."
    VERBATIM
)

# ============================================================================
# 4b. The plugins: HeartOfMagic.esp, ESL-flagged, at the Data root (committed at
#     the repo root; docs/ENVIRONMENT.md). The FOMOD installs it in its core step.
#     HeartOfMagic_Mysticism.esp below.
# ============================================================================

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_SOURCE_DIR}/HeartOfMagic.esp"
    "${DIST_VERSION_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying HeartOfMagic.esp..."
    VERBATIM
)

# The optional Mysticism patch, HeartOfMagic_Mysticism.esp (masters MysticismMagic.esp
# and HeartOfMagic.esp), under optional/ like the ISL patch: a copy of the zip
# extracted by hand into Data without Mysticism would otherwise get a plugin with a
# missing master. The FOMOD installs it to the Data root when MysticismMagic.esp is active.
add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_VERSION_DIR}/optional/MysticismPatch"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_SOURCE_DIR}/HeartOfMagic_Mysticism.esp"
    "${DIST_VERSION_DIR}/optional/MysticismPatch/"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying HeartOfMagic_Mysticism.esp..."
    VERBATIM
)

# ============================================================================
# 5. Copy PrismaUI views
# ============================================================================

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_SOURCE_DIR}/PrismaUI/views"
    "${DIST_VERSION_DIR}/PrismaUI/views"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying PrismaUI views..."
    VERBATIM
)

# The panel's development files, which the game never loads, stay out of the
# release: the desktop harness, the node test runner and its data, the test
# modules (modules/*Test.js, globbed at configure time: reconfigure for a new one)
set(_panel_src "${CMAKE_SOURCE_DIR}/PrismaUI/views/SpellLearning/SpellLearningPanel")
set(_panel_dist "${DIST_VERSION_DIR}/PrismaUI/views/SpellLearning/SpellLearningPanel")
file(GLOB _panel_tests RELATIVE "${_panel_src}" "${_panel_src}/modules/*Test.js")
set(_panel_dev_files
    dev-harness.html dev-harness-parts.html dev-harness-bridge.js dev-harness-toolbar.js launch-dev-harness.bat
    run-tests.js test-runner.html ${_panel_tests})
list(TRANSFORM _panel_dev_files PREPEND "${_panel_dist}/")
add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -f ${_panel_dev_files}
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_panel_dist}/test-data"
    COMMAND "${CMAKE_COMMAND}" -E echo "Leaving out the panel's development files..."
    VERBATIM
)

# ============================================================================
# 6. Copy Papyrus scripts to distribution
#    SpellLearning scripts -> Scripts/ and Scripts/Source/
#    ISL scripts -> optional/ISLPatch/Scripts/ and optional/ISLPatch/Scripts/Source/
# ============================================================================

if(TARGET papyrus_build)
    # SpellLearning .psc sources and compiled .pex
    foreach(_psc_path IN LISTS PAPYRUS_SPELLLEARNING_SOURCES)
        get_filename_component(_name "${_psc_path}" NAME)
        get_filename_component(_name_we "${_psc_path}" NAME_WE)
        add_custom_command(TARGET assemble_dist POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy
            "${_psc_path}"
            "${DIST_VERSION_DIR}/Scripts/Source/${_name}"
            COMMAND "${CMAKE_COMMAND}" -E copy
            "${PAPYRUS_OUTPUT_DIR}/${_name_we}.pex"
            "${DIST_VERSION_DIR}/Scripts/${_name_we}.pex"
            VERBATIM
        )
    endforeach()

    # ISL .psc sources and compiled .pex
    foreach(_psc_path IN LISTS PAPYRUS_ISL_SOURCES)
        get_filename_component(_name "${_psc_path}" NAME)
        get_filename_component(_name_we "${_psc_path}" NAME_WE)
        add_custom_command(TARGET assemble_dist POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy
            "${_psc_path}"
            "${DIST_VERSION_DIR}/optional/ISLPatch/Scripts/Source/${_name}"
            COMMAND "${CMAKE_COMMAND}" -E copy
            "${PAPYRUS_OUTPUT_DIR}/${_name_we}.pex"
            "${DIST_VERSION_DIR}/optional/ISLPatch/Scripts/${_name_we}.pex"
            VERBATIM
        )
    endforeach()
endif()

# ============================================================================
# 7. Create zip archive of the distribution folder
# ============================================================================

get_filename_component(_dist_folder_name "${DIST_VERSION_DIR}" NAME)

get_filename_component(_pdb_folder_name "${DIST_PDB_DIR}" NAME)

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E tar cf
    "${DIST_DIR}/${_dist_folder_name}.zip"
    --format=zip
    -- "${_dist_folder_name}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf
    "${DIST_DIR}/${_pdb_folder_name}.zip"
    --format=zip
    -- "${_pdb_folder_name}"
    WORKING_DIRECTORY "${DIST_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo "Creating ${_dist_folder_name}.zip and ${_pdb_folder_name}.zip..."
    VERBATIM
)

message(STATUS "Distribution assembly configured:")
message(STATUS "  Output:   ${DIST_VERSION_DIR}")
message(STATUS "  Archive:  ${DIST_DIR}/${_dist_folder_name}.zip (+ ${_pdb_folder_name}.zip)")

# ============================================================================
# 8. The magic menu's own release (SL_MagicMenu, MAGIC_MENU_VERSION from the
#    top-level CMakeLists.txt), apart from Heart of Magic's: assemble_magicmenu_dist
#    makes dist/SL_MagicMenu_<version>/ and its zip, the FOMOD of fomod_magicmenu/:
#      fomod/ModuleConfig.xml, fomod/info.xml
#      00 Core/SKSE/Plugins/SL_MagicMenu.dll, SL_MagicMenu.ini
#      00 Core/SKSE/Plugins/SL_MagicMenu/lang/en.json, ko.json
#      00 Core/MCM/Settings/InventoryReforged.ini
#      10 Heart of Magic Block/SKSE/Plugins/SL_MagicMenu_HoM.dll
#      10 Heart of Magic Block/SKSE/Plugins/SL_MagicMenu_HoM/lang/en.json, ko.json
#      LICENSE, THIRD-PARTY-NOTICES.md, licenses/ (as 3b, with the ports these
#      two DLLs link)
#    and the two DLLs' .pdb in SL_MagicMenu_<version>_DebugSymbols.zip. The Spell
#    Research block is not in this release (a later FOMOD option). Nothing above
#    changes: HoM's assemble_dist stays as it was.
# ============================================================================

if(NOT DEFINED MAGIC_MENU_DIST_VERSION_DIR)
    message(STATUS "MAGIC_MENU_DIST_VERSION_DIR not defined - magic menu release assembly disabled")
    return()
endif()

set(_mm_dist "${MAGIC_MENU_DIST_VERSION_DIR}")
set(_mm_pdb_dir "${MAGIC_MENU_DIST_VERSION_DIR}_DebugSymbols")
set(_mm_core "${_mm_dist}/00 Core")
set(_mm_hom "${_mm_dist}/10 Heart of Magic Block")
set(_mm_core_src "${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/dist")
set(_mm_hom_src "${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_HoM/dist")
set(_mm_fomod_src "${CMAKE_SOURCE_DIR}/fomod_magicmenu")
set(_mm_licenses_dist "${_mm_dist}/licenses")
# CommonLibSSE-NG's ports these DLLs carry (fmt and spdlog for the log, DirectXTK and
# DirectXMath, Xbyak with SKSE_SUPPORT_XBYAK) and the plugins' nlohmann-json. Not
# rapidcsv (CommonLib uses it for VR only, off here) nor SpellLearning's rapidfuzz-cpp,
# highway and zlib.
set(_mm_vcpkg_license_ports fmt spdlog nlohmann-json xbyak directxmath directxtk)

add_custom_target(assemble_magicmenu_dist ALL
    COMMENT "Assembling the magic menu's release distribution...")

add_dependencies(assemble_magicmenu_dist SL_MagicMenu SL_MagicMenu_HoM)

configure_file(
    "${_mm_fomod_src}/THIRD-PARTY-NOTICES.md.in"
    "${CMAKE_BINARY_DIR}/magicmenu/THIRD-PARTY-NOTICES.md"
    @ONLY
)

set(_mm_vcpkg_license_commands)
foreach(_port IN LISTS _mm_vcpkg_license_ports)
    if(NOT EXISTS "${_vcpkg_share}/${_port}/copyright")
        message(FATAL_ERROR "Distribution (magic menu): no license file for vcpkg port ${_port} at ${_vcpkg_share}/${_port}/copyright")
    endif()
    list(APPEND _mm_vcpkg_license_commands
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${_vcpkg_share}/${_port}/copyright"
        "${_mm_licenses_dist}/${_port}.txt")
endforeach()

# Clean, then the FOMOD, the core folder and the HoM block's folder; the .pdb apart
add_custom_command(TARGET assemble_magicmenu_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_mm_dist}" "${_mm_pdb_dir}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_dist}/fomod"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_core}/SKSE/Plugins/SL_MagicMenu/lang"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_core}/MCM/Settings"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_hom}/SKSE/Plugins/SL_MagicMenu_HoM/lang"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_pdb_dir}/SKSE/Plugins"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${_mm_fomod_src}/fomod/ModuleConfig.xml"
    "${_mm_fomod_src}/fomod/info.xml"
    "${_mm_dist}/fomod/"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:SL_MagicMenu>"
    "${_mm_core_src}/SL_MagicMenu.ini"
    "${_mm_core}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${_mm_core_src}/MCM/Settings/InventoryReforged.ini"
    "${_mm_core}/MCM/Settings/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${_mm_core_src}/lang"
    "${_mm_core}/SKSE/Plugins/SL_MagicMenu/lang"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:SL_MagicMenu_HoM>"
    "${_mm_hom}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${_mm_hom_src}/lang"
    "${_mm_hom}/SKSE/Plugins/SL_MagicMenu_HoM/lang"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:SL_MagicMenu>>,copy,true>
    "$<$<BOOL:$<TARGET_PDB_FILE:SL_MagicMenu>>:$<TARGET_PDB_FILE:SL_MagicMenu>>"
    "${_mm_pdb_dir}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:SL_MagicMenu_HoM>>,copy,true>
    "$<$<BOOL:$<TARGET_PDB_FILE:SL_MagicMenu_HoM>>:$<TARGET_PDB_FILE:SL_MagicMenu_HoM>>"
    "${_mm_pdb_dir}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying the magic menu's FOMOD, DLLs, ini and language files..."
    VERBATIM
)

# The licences, as 3b
add_custom_command(TARGET assemble_magicmenu_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_licenses_dist}/CommonLibSSE-NG"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${_mm_fomod_src}/LICENSE"
    "${CMAKE_BINARY_DIR}/magicmenu/THIRD-PARTY-NOTICES.md"
    "${_mm_dist}/"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${_commonlib_dir}/COPYING.txt"
    "${_commonlib_dir}/EXCEPTIONS.md"
    "${_commonlib_dir}/licenses/LICENSE-MIT.txt"
    "${_mm_licenses_dist}/CommonLibSSE-NG/"
    ${_mm_vcpkg_license_commands}
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying the magic menu's licenses..."
    VERBATIM
)

# The zips, as 7
get_filename_component(_mm_dist_folder_name "${_mm_dist}" NAME)
get_filename_component(_mm_pdb_folder_name "${_mm_pdb_dir}" NAME)

add_custom_command(TARGET assemble_magicmenu_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E tar cf
    "${DIST_DIR}/${_mm_dist_folder_name}.zip"
    --format=zip
    -- "${_mm_dist_folder_name}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf
    "${DIST_DIR}/${_mm_pdb_folder_name}.zip"
    --format=zip
    -- "${_mm_pdb_folder_name}"
    WORKING_DIRECTORY "${DIST_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo "Creating ${_mm_dist_folder_name}.zip and ${_mm_pdb_folder_name}.zip..."
    VERBATIM
)

message(STATUS "Magic menu release assembly configured:")
message(STATUS "  Output:   ${_mm_dist}")
message(STATUS "  Archive:  ${DIST_DIR}/${_mm_dist_folder_name}.zip (+ ${_mm_pdb_folder_name}.zip)")
