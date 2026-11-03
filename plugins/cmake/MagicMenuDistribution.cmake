# MagicMenuDistribution.cmake
# The magic menu's releases, apart from Heart of Magic's (Distribution.cmake; split from it
# 2026-11-04): section 9, Magic Menu Rewrite's own release, and section 10, the Spell
# Research block's package. The Heart of Magic block ships in HoM's release
# (Distribution.cmake 2b). Included by the top-level CMakeLists.txt after Distribution.cmake.
# Each section has its own guard: one disabled does not skip the other.
# Requires: DIST_DIR, MAGIC_MENU_DIST_VERSION_DIR, SPELL_RESEARCH_BLOCK_DIST_DIR (top-level
#           CMakeLists.txt), VCPKG_INSTALLED_DIR / VCPKG_TARGET_TRIPLET (vcpkg)

set(_mmd_commonlib_dir "${CMAKE_SOURCE_DIR}/plugins/external/commonlibsse-ng")
set(_mmd_vcpkg_share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
# CommonLibSSE-NG's ports these DLLs carry (fmt and spdlog for the log, DirectXTK and
# DirectXMath, Xbyak with SKSE_SUPPORT_XBYAK) and the plugins' nlohmann-json. Not
# rapidcsv (CommonLib uses it for VR only, off here) nor SpellLearning's rapidfuzz-cpp,
# highway and zlib. MagicMenuRewrite.dll and MagicMenuRewrite_SpellResearch.dll link the same set.
set(_mm_vcpkg_license_ports fmt spdlog nlohmann-json xbyak directxmath directxtk)

# a_out: the POST_BUILD COMMANDs that fill a_dest (a package's licenses/) as Distribution.cmake
# 3b does: CommonLibSSE-NG's COPYING.txt, EXCEPTIONS.md and LICENSE-MIT.txt under
# CommonLibSSE-NG/, each port's copyright file as <port>.txt. A port without one stops the configure.
function(MagicMenuLicenseCommands a_out a_dest)
    set(_commands
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${a_dest}/CommonLibSSE-NG"
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${_mmd_commonlib_dir}/COPYING.txt"
        "${_mmd_commonlib_dir}/EXCEPTIONS.md"
        "${_mmd_commonlib_dir}/licenses/LICENSE-MIT.txt"
        "${a_dest}/CommonLibSSE-NG/")
    foreach(_port IN LISTS _mm_vcpkg_license_ports)
        if(NOT EXISTS "${_mmd_vcpkg_share}/${_port}/copyright")
            message(FATAL_ERROR "MagicMenuDistribution: no license file for vcpkg port ${_port} at ${_mmd_vcpkg_share}/${_port}/copyright")
        endif()
        list(APPEND _commands
            COMMAND "${CMAKE_COMMAND}" -E copy
            "${_mmd_vcpkg_share}/${_port}/copyright"
            "${a_dest}/${_port}.txt")
    endforeach()
    set(${a_out} ${_commands} PARENT_SCOPE)
endfunction()

# ============================================================================
# 9. The magic menu's own release (Magic Menu Rewrite, MAGIC_MENU_VERSION from the
#    top-level CMakeLists.txt), apart from Heart of Magic's: assemble_magicmenu_dist
#    makes dist/MagicMenuRewrite_<version>/ and its zip, the FOMOD of fomod_magicmenu/:
#      fomod/ModuleConfig.xml, fomod/info.xml
#      00 Core/SKSE/Plugins/MagicMenuRewrite.dll, MagicMenuRewrite.ini
#      00 Core/SKSE/Plugins/MagicMenuRewrite/lang/en.json, ko.json
#      00 Core/SKSE/Plugins/InventoryReforged/MagicMenuRewrite.ini (Inventory Reforged
#        loads every ini in that folder; it turns IR's magic-menu panel off)
#      LICENSE, THIRD-PARTY-NOTICES.md, licenses/ (as Distribution.cmake 3b, MagicMenuLicenseCommands)
#    and the DLL's .pdb in MagicMenuRewrite_<version>_DebugSymbols.zip. Since 1.2.0 the
#    core and the Block API only: the blocks ship with their mods (docs/magicmenu/
#    BLOCKS_MOVE.md) - the Heart of Magic block in HoM's release (Distribution.cmake 2b),
#    the Spell Research block in "Spell Research - Prisma UI 2" (section 10). The target keeps
#    the repo's working name (SL_MagicMenu); $<TARGET_FILE> gives the shipped
#    MagicMenuRewrite.dll (its OUTPUT_NAME).
# ============================================================================

if(DEFINED MAGIC_MENU_DIST_VERSION_DIR)
    set(_mm_dist "${MAGIC_MENU_DIST_VERSION_DIR}")
    set(_mm_pdb_dir "${MAGIC_MENU_DIST_VERSION_DIR}_DebugSymbols")
    set(_mm_core "${_mm_dist}/00 Core")
    set(_mm_core_src "${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu/dist")
    set(_mm_fomod_src "${CMAKE_SOURCE_DIR}/fomod_magicmenu")
    set(_mm_licenses_dist "${_mm_dist}/licenses")

    add_custom_target(assemble_magicmenu_dist ALL
        COMMENT "Assembling the magic menu's release distribution...")

    add_dependencies(assemble_magicmenu_dist SL_MagicMenu)

    configure_file(
        "${_mm_fomod_src}/THIRD-PARTY-NOTICES.md.in"
        "${CMAKE_BINARY_DIR}/magicmenu/THIRD-PARTY-NOTICES.md"
        @ONLY
    )

    MagicMenuLicenseCommands(_mm_license_commands "${_mm_licenses_dist}")

    # Clean, then the FOMOD and the core folder; the .pdb apart
    add_custom_command(TARGET assemble_magicmenu_dist POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_mm_dist}" "${_mm_pdb_dir}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_dist}/fomod"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_core}/SKSE/Plugins/MagicMenuRewrite/lang"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_core}/SKSE/Plugins/InventoryReforged"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mm_pdb_dir}/SKSE/Plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${_mm_fomod_src}/fomod/ModuleConfig.xml"
        "${_mm_fomod_src}/fomod/info.xml"
        "${_mm_dist}/fomod/"
        COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_FILE:SL_MagicMenu>"
        "${_mm_core_src}/MagicMenuRewrite.ini"
        "${_mm_core}/SKSE/Plugins/"
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${_mm_core_src}/InventoryReforged/MagicMenuRewrite.ini"
        "${_mm_core}/SKSE/Plugins/InventoryReforged/"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${_mm_core_src}/lang"
        "${_mm_core}/SKSE/Plugins/MagicMenuRewrite/lang"
        COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:SL_MagicMenu>>,copy,true>
        "$<$<BOOL:$<TARGET_PDB_FILE:SL_MagicMenu>>:$<TARGET_PDB_FILE:SL_MagicMenu>>"
        "${_mm_pdb_dir}/SKSE/Plugins/"
        COMMAND "${CMAKE_COMMAND}" -E echo "Copying the magic menu's FOMOD, DLL, ini and language files..."
        VERBATIM
    )

    # The licences, as 3b
    add_custom_command(TARGET assemble_magicmenu_dist POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${_mm_fomod_src}/LICENSE"
        "${CMAKE_BINARY_DIR}/magicmenu/THIRD-PARTY-NOTICES.md"
        "${_mm_dist}/"
        ${_mm_license_commands}
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
else()
    message(STATUS "MAGIC_MENU_DIST_VERSION_DIR not defined - magic menu release assembly disabled")
endif()

# ============================================================================
# 10. The Spell Research block's package (plugins/SL_MagicMenu_SpellResearch,
#     MagicMenuRewrite_SpellResearch.dll) for "Spell Research - Prisma UI 2" to ship
#     (SPELL_RESEARCH_BLOCK_VERSION, the top-level CMakeLists.txt): no FOMOD of its own,
#     that release puts these files in its archive. assemble_srblock_dist makes
#     dist/SpellResearchBlock_<version>/ and its zip:
#       SKSE/Plugins/MagicMenuRewrite_SpellResearch.dll, MagicMenuRewrite_SpellResearch.ini
#       SKSE/Plugins/MagicMenuRewrite_SpellResearch/lang/en.json, ko.json
#       MagicMenuRewrite_SpellResearch-THIRD-PARTY-NOTICES.md, MagicMenuRewrite_SpellResearch-LICENSE.txt,
#       licenses/ (MagicMenuLicenseCommands) - for the archive's root, not Data
#       DebugSymbols/SKSE/Plugins/MagicMenuRewrite_SpellResearch.pdb (for that release's
#       debug symbols archive)
#     The file, plugin, log, ini and lang names are the ones the magic menu's earlier
#     builds used, so a saved layout and a player's ini keep working.
# ============================================================================

if(DEFINED SPELL_RESEARCH_BLOCK_DIST_DIR)
    set(_sr_dist "${SPELL_RESEARCH_BLOCK_DIST_DIR}")
    set(_sr_name "MagicMenuRewrite_SpellResearch")
    set(_sr_src "${CMAKE_SOURCE_DIR}/plugins/SL_MagicMenu_SpellResearch")
    set(_sr_plugins "${_sr_dist}/SKSE/Plugins")
    set(_sr_licenses_dist "${_sr_dist}/licenses")

    add_custom_target(assemble_srblock_dist ALL
        COMMENT "Assembling the Spell Research block's package...")

    add_dependencies(assemble_srblock_dist SL_MagicMenu_SpellResearch)

    configure_file(
        "${_sr_src}/package/THIRD-PARTY-NOTICES.md.in"
        "${CMAKE_BINARY_DIR}/srblock/${_sr_name}-THIRD-PARTY-NOTICES.md"
        @ONLY
    )

    MagicMenuLicenseCommands(_sr_license_commands "${_sr_licenses_dist}")

    add_custom_command(TARGET assemble_srblock_dist POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_sr_dist}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${_sr_dist}.zip"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_sr_plugins}/${_sr_name}/lang"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_sr_dist}/DebugSymbols/SKSE/Plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_FILE:SL_MagicMenu_SpellResearch>"
        "${_sr_src}/dist/${_sr_name}.ini"
        "${_sr_plugins}/"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${_sr_src}/dist/lang"
        "${_sr_plugins}/${_sr_name}/lang"
        COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:SL_MagicMenu_SpellResearch>>,copy,true>
        "$<$<BOOL:$<TARGET_PDB_FILE:SL_MagicMenu_SpellResearch>>:$<TARGET_PDB_FILE:SL_MagicMenu_SpellResearch>>"
        "${_sr_dist}/DebugSymbols/SKSE/Plugins/"
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${CMAKE_BINARY_DIR}/srblock/${_sr_name}-THIRD-PARTY-NOTICES.md"
        "${_sr_dist}/"
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${_sr_dist}/${_sr_name}-LICENSE.txt"
        ${_sr_license_commands}
        COMMAND "${CMAKE_COMMAND}" -E chdir "${_sr_dist}"
            "${CMAKE_COMMAND}" -E tar cf "${_sr_dist}.zip" --format=zip --
            SKSE DebugSymbols licenses ${_sr_name}-THIRD-PARTY-NOTICES.md ${_sr_name}-LICENSE.txt
        COMMAND "${CMAKE_COMMAND}" -E echo "Creating SpellResearchBlock_${SPELL_RESEARCH_BLOCK_VERSION}.zip..."
        VERBATIM
    )

    message(STATUS "Spell Research block package configured:")
    message(STATUS "  Output:   ${_sr_dist} (+ .zip)")
else()
    message(STATUS "SPELL_RESEARCH_BLOCK_DIST_DIR not defined - Spell Research block package disabled")
endif()
