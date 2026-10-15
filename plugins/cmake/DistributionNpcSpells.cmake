# The NPC Spells addon ships as its own archive, apart from the main mod, with its own version
# (plugins/NPCSpells/VERSION, read into NPCSPELLS_VERSION by the top-level CMakeLists.txt):
# HeartOfMagic-NPCSpells_<version>.zip = the ESL, the DLL, the rules JSON, LICENSE and the licence files
# (the DLL statically links CommonLibSSE-NG: its own THIRD-PARTY-NOTICES.md, made from the main template with
# this archive's name and source tag, and the main archive's licenses/ folder); its PDB goes to
# HeartOfMagic-NPCSpells_<version>_DebugSymbols.zip.
if(NOT DEFINED DIST_DIR)
    return()
endif()
set(NPCSPELLS_DIST_NAME "HeartOfMagic-NPCSpells")
set(NPCSPELLS_DIST_DIR "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}")
set(NPCSPELLS_PDB_DIR "${NPCSPELLS_DIST_DIR}_DebugSymbols")

# The addon release's source tag: the repository tag that holds this addon version's code
set(NOTICES_PRODUCT "Heart of Magic - NPC Spells")
set(NOTICES_VERSION "${NPCSPELLS_VERSION}")
set(NOTICES_TAG "npcspells-v${NPCSPELLS_VERSION}")
set(NPCSPELLS_NOTICES "${CMAKE_BINARY_DIR}/npcspells/THIRD-PARTY-NOTICES.md")
configure_file("${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md.in" "${NPCSPELLS_NOTICES}" @ONLY)

add_custom_target(assemble_npcspells ALL COMMENT "Assembling the NPC Spells addon...")
# assemble_dist: its licenses/ folder is what this archive copies
add_dependencies(assemble_npcspells HeartOfMagic_NPCSpells npcspells_esl assemble_dist)
add_custom_command(TARGET assemble_npcspells POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${NPCSPELLS_DIST_DIR}" "${NPCSPELLS_PDB_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${NPCSPELLS_DIST_DIR}/SKSE/Plugins" "${NPCSPELLS_PDB_DIR}/SKSE/Plugins" "${NPCSPELLS_DIST_DIR}/licenses/CommonLibSSE-NG"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:HeartOfMagic_NPCSpells>" "${NPCSPELLS_DIST_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>,copy,true> "$<$<BOOL:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>" "${NPCSPELLS_PDB_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${NPCSPELLS_ESL_FILE}" "${NPCSPELLS_DIST_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/plugins/NPCSpells/data/HeartOfMagic_NPCSpells.json" "${NPCSPELLS_DIST_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/LICENSE" "${NPCSPELLS_NOTICES}" "${NPCSPELLS_DIST_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${DIST_VERSION_DIR}/licenses" "${NPCSPELLS_DIST_DIR}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}.zip" --format=zip -- "${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}_DebugSymbols.zip" --format=zip -- "${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}_DebugSymbols"
    WORKING_DIRECTORY "${DIST_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo "Creating ${NPCSPELLS_DIST_NAME}_${NPCSPELLS_VERSION}.zip..."
    VERBATIM)
