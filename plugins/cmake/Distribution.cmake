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

# The game-icons.net source list, one copy for the base notices and the design add-ons' (section 8)
set(_design_package_dir "${CMAKE_SOURCE_DIR}/addons/designs/_package")
file(READ "${_design_package_dir}/game-icons-list.md" GAME_ICONS_LIST)
string(STRIP "${GAME_ICONS_LIST}" GAME_ICONS_LIST)

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

# ============================================================================
# 8. The design add-ons. The base mod ships one design, Skald (built into the
#    panel); each other design is its own archive,
#    HeartOfMagic-Design-<folder>_<version>.zip, made from addons/designs/<folder>/
#    (the game's Data layout inside): its SKSE/ and PrismaUI/ at the archive's
#    root, a README, the MIT licence and, when the add-on has
#    THIRD-PARTY-NOTICES.md.in, its third-party notices. A shared tree
#    (addons/designs/_shared/<name>/) goes into every archive that lists it:
#    the book fonts into Arcane's and Night Grimoire's (installing both writes
#    the same files twice).
# ============================================================================

set(_designs_src "${CMAKE_SOURCE_DIR}/addons/designs")
set(_design_addons Classic ModernDark Arcane Chalkboard NightGrimoire)
set(_design_title_Classic "Classic")
set(_design_title_ModernDark "Modern Dark")
set(_design_title_Arcane "Arcane")
set(_design_title_Chalkboard "Chalkboard")
set(_design_title_NightGrimoire "Night Grimoire")
set(_design_shared_Arcane book-fonts)
set(_design_shared_NightGrimoire book-fonts)
file(READ "${_design_package_dir}/book-fonts.md" BOOK_FONTS_NOTICE)
string(STRIP "${BOOK_FONTS_NOTICE}" BOOK_FONTS_NOTICE)

set(_design_zips)
foreach(_name IN LISTS _design_addons)
    set(_folder "HeartOfMagic-Design-${_name}_${PROJECT_VERSION}")
    set(_stage "${DIST_DIR}/${_folder}")
    set(_meta "${CMAKE_BINARY_DIR}/designs/${_name}")
    set(_prefix "HeartOfMagic-Design-${_name}")
    set(DESIGN_TITLE "${_design_title_${_name}}")
    set(DESIGN_LICENSE_FILE "${_prefix}-LICENSE.txt")
    if(NOT EXISTS "${_designs_src}/${_name}/SKSE/Plugins/SpellLearning/presets/design/${_name}.json")
        message(FATAL_ERROR "Distribution: no design file at addons/designs/${_name}/SKSE/Plugins/SpellLearning/presets/design/${_name}.json")
    endif()

    # The add-on's tree and the shared trees it lists: their SKSE/ and PrismaUI/ into the stage
    set(_trees "${_designs_src}/${_name}")
    foreach(_shared IN LISTS _design_shared_${_name})
        list(APPEND _trees "${_designs_src}/_shared/${_shared}")
    endforeach()
    set(_entries)
    set(_copy)
    foreach(_tree IN LISTS _trees)
        foreach(_top SKSE PrismaUI)
            if(EXISTS "${_tree}/${_top}")
                list(APPEND _copy COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_tree}/${_top}" "${_stage}/${_top}")
                if(NOT _top IN_LIST _entries)
                    list(APPEND _entries ${_top})
                endif()
            endif()
        endforeach()
    endforeach()
    list(JOIN _entries "/ and " DESIGN_FOLDERS)
    set(DESIGN_FOLDERS "${DESIGN_FOLDERS}/")

    # The archive's own files are made afresh at each configure: the whole folder goes into the stage,
    # so a file left from an earlier configure (a notices file since removed) must not
    file(REMOVE_RECURSE "${_meta}")
    set(DESIGN_NOTICE_LINE "")
    set(_files "${_prefix}-README.txt" "${DESIGN_LICENSE_FILE}")
    if(EXISTS "${_designs_src}/${_name}/THIRD-PARTY-NOTICES.md.in")
        configure_file("${_designs_src}/${_name}/THIRD-PARTY-NOTICES.md.in" "${_meta}/${_prefix}-THIRD-PARTY-NOTICES.md" @ONLY)
        set(DESIGN_NOTICE_LINE "\nThe fonts and icon shapes it ships: ${_prefix}-THIRD-PARTY-NOTICES.md.")
        list(APPEND _files "${_prefix}-THIRD-PARTY-NOTICES.md")
    endif()
    configure_file("${_design_package_dir}/README.txt.in" "${_meta}/${_prefix}-README.txt" @ONLY)
    configure_file("${CMAKE_SOURCE_DIR}/LICENSE" "${_meta}/${DESIGN_LICENSE_FILE}" COPYONLY)

    add_custom_command(TARGET assemble_dist POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_stage}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${DIST_DIR}/${_folder}.zip"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stage}"
        ${_copy}
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_meta}" "${_stage}"
        # chdir, not WORKING_DIRECTORY: the stage does not exist yet when the step starts
        COMMAND "${CMAKE_COMMAND}" -E chdir "${_stage}"
            "${CMAKE_COMMAND}" -E tar cf "${DIST_DIR}/${_folder}.zip" --format=zip -- ${_entries} ${_files}
        COMMAND "${CMAKE_COMMAND}" -E echo "Creating ${_folder}.zip..."
        VERBATIM
    )
    list(APPEND _design_zips "${_folder}.zip")
endforeach()

message(STATUS "Distribution assembly configured:")
message(STATUS "  Output:   ${DIST_VERSION_DIR}")
message(STATUS "  Archive:  ${DIST_DIR}/${_dist_folder_name}.zip (+ ${_pdb_folder_name}.zip)")
list(JOIN _design_zips ", " _design_zip_list)
message(STATUS "  Designs:  ${_design_zip_list}")
