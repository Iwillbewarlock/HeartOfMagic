# The NPC Spells addon ships as its own archive, apart from the main mod, with its own version
# (plugins/NPCSpells/VERSION, read into NPCSPELLS_VERSION by the top-level CMakeLists.txt).
# It has no FOMOD, so the zip's root is the game's Data folder (a mod manager installs it as it is):
#   HeartOfMagic_NPCSpells.esp
#   SKSE/Plugins/HeartOfMagic_NPCSpells.dll, HeartOfMagic_NPCSpells.json
#   SKSE/Plugins/HeartOfMagic_NPCSpells/LICENSE, THIRD-PARTY-NOTICES.md, licenses/
# The licence files sit in that folder so they do not land in Data's top level; nothing reads it (the addon
# reads only SKSE/Plugins/HeartOfMagic_NPCSpells.json, and the tome reach config search takes only folders
# named after a distribution framework). The DLL statically links CommonLibSSE-NG: its own notice
# (plugins/NPCSpells/THIRD-PARTY-NOTICES.md.in, source tag npcspells-v<version>) and the licence texts of only
# the libraries this DLL links. Its PDB goes to HeartOfMagic-NPCSpells_<version>_DebugSymbols.zip, rooted at
# Data the same way (SKSE/Plugins/HeartOfMagic_NPCSpells.pdb).
if(NOT DEFINED DIST_DIR)
    return()
endif()
set(NPCSPELLS_DIST_NAME "HeartOfMagic-NPCSpells")
set(NPCSPELLS_DIST_DIR "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}")
set(NPCSPELLS_PDB_DIR "${NPCSPELLS_DIST_DIR}_DebugSymbols")
set(NPCSPELLS_ZIP "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}.zip")
set(NPCSPELLS_PDB_ZIP "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}_DebugSymbols.zip")
set(NPCSPELLS_DOC_DIR "${NPCSPELLS_DIST_DIR}/SKSE/Plugins/HeartOfMagic_NPCSpells")

set(NPCSPELLS_NOTICES "${CMAKE_BINARY_DIR}/npcspells/THIRD-PARTY-NOTICES.md")
configure_file("${CMAKE_SOURCE_DIR}/plugins/NPCSpells/THIRD-PARTY-NOTICES.md.in" "${NPCSPELLS_NOTICES}" @ONLY)

# The vcpkg ports whose code is in HeartOfMagic_NPCSpells.dll: its target_link_libraries (spdlog with fmt,
# nlohmann-json, zlib) and what CommonLibSSE-NG links or compiles in here (DirectXTK with DirectXMath, Xbyak:
# SKSE_SUPPORT_XBYAK is on; rapidcsv only with ENABLE_SKYRIM_VR, which is off). A port added to the DLL needs
# its name here and a row in the notice.
set(_npcspells_license_ports fmt spdlog nlohmann-json zlib xbyak directxmath directxtk)
set(_npcspells_license_commands)
foreach(_port IN LISTS _npcspells_license_ports)
    if(NOT EXISTS "${_vcpkg_share}/${_port}/copyright")
        message(FATAL_ERROR "DistributionNpcSpells: no license file for vcpkg port ${_port} at ${_vcpkg_share}/${_port}/copyright")
    endif()
    list(APPEND _npcspells_license_commands
        COMMAND "${CMAKE_COMMAND}" -E copy "${_vcpkg_share}/${_port}/copyright" "${NPCSPELLS_DOC_DIR}/licenses/${_port}.txt")
endforeach()

add_custom_target(assemble_npcspells ALL COMMENT "Assembling the NPC Spells addon...")
add_dependencies(assemble_npcspells HeartOfMagic_NPCSpells npcspells_esl)
add_custom_command(TARGET assemble_npcspells POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${NPCSPELLS_DIST_DIR}" "${NPCSPELLS_PDB_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${NPCSPELLS_ZIP}" "${NPCSPELLS_PDB_ZIP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${NPCSPELLS_DOC_DIR}/licenses/CommonLibSSE-NG" "${NPCSPELLS_PDB_DIR}/SKSE/Plugins"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:HeartOfMagic_NPCSpells>" "${NPCSPELLS_DIST_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>,copy,true> "$<$<BOOL:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>" "${NPCSPELLS_PDB_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${NPCSPELLS_ESL_FILE}" "${NPCSPELLS_DIST_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/plugins/NPCSpells/data/HeartOfMagic_NPCSpells.json" "${NPCSPELLS_DIST_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/LICENSE" "${NPCSPELLS_NOTICES}" "${NPCSPELLS_DOC_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${_commonlib_dir}/COPYING.txt"
    "${_commonlib_dir}/EXCEPTIONS.md"
    "${_commonlib_dir}/licenses/LICENSE-MIT.txt"
    "${NPCSPELLS_DOC_DIR}/licenses/CommonLibSSE-NG/"
    ${_npcspells_license_commands}
    VERBATIM)
# The zips are made from inside the staging folders, so their root is Data (no wrapping folder)
add_custom_command(TARGET assemble_npcspells POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E echo "Creating ${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}.zip..."
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${NPCSPELLS_ZIP}" --format=zip -- HeartOfMagic_NPCSpells.esp SKSE
    WORKING_DIRECTORY "${NPCSPELLS_DIST_DIR}"
    VERBATIM)
add_custom_command(TARGET assemble_npcspells POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${NPCSPELLS_PDB_ZIP}" --format=zip -- SKSE
    WORKING_DIRECTORY "${NPCSPELLS_PDB_DIR}"
    VERBATIM)
