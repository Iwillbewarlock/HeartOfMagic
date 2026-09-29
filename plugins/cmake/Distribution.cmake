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
#    three quarters of the main archive's size if they went in it)
# ============================================================================

set(DIST_PDB_DIR "${DIST_VERSION_DIR}_DebugSymbols")

add_custom_command(TARGET assemble_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_PDB_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_PDB_DIR}/SKSE/Plugins"
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
    fmt spdlog nlohmann-json rapidcsv xbyak directxmath directxtk rapidfuzz-cpp highway)

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
    dev-harness.html dev-harness-bridge.js dev-harness-toolbar.js launch-dev-harness.bat
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
