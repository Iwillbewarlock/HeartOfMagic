# The Toxicology addon ships as its own archive, apart from the main mod, with its own version
# (plugins/Toxicology/VERSION, read into TOXICOLOGY_VERSION by the top-level CMakeLists.txt).
# It has no FOMOD, so the zip's root is the game's Data folder (a mod manager installs it as it is):
#   HeartOfMagic_Toxicology.esp                                     (ESL-flagged, committed in plugins/Toxicology/data)
#   SKSE/Plugins/HeartOfMagic_Toxicology.dll, HeartOfMagic_Toxicology.json (settings)
#   SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json     (I4 rule for the mimic poison icon)
#   Interface/HeartOfMagicToxicology/icons.swf                      (the icon the I4 rule names)
#   SKSE/Plugins/HeartOfMagic_Toxicology/LICENSE, THIRD-PARTY-NOTICES.md, licenses/
# The licence files sit in that folder so they do not land in Data's top level; nothing reads it (the DLL reads
# only SKSE/Plugins/HeartOfMagic_Toxicology.json and, in a dev install, the self test file next to it). The dev
# self test file (plugins/Toxicology/data/dev/) and the icon's source art (plugins/Toxicology/art/) never go in.
# The DLL statically links CommonLibSSE-NG: its own notice (plugins/Toxicology/THIRD-PARTY-NOTICES.md.in, source
# tag toxicology-v<version>) and the licence texts of only the libraries this DLL links. Its PDB goes to
# HeartOfMagic-Librarian-Toxicology_<version>_DebugSymbols.zip, rooted at Data the same way
# (SKSE/Plugins/HeartOfMagic_Toxicology.pdb).
# The Korean archive HeartOfMagic-Librarian-Toxicology_<version>_KO.zip is the same files with the Korean ESL
# (plugins/Toxicology/data/ko/HeartOfMagic_Toxicology.esp, same file name; only its text differs, ESL.md).
# toxicology-esl-check holds it to that before the zips are made.
if(NOT DEFINED DIST_DIR)
    return()
endif()
set(TOXICOLOGY_DIST_NAME "HeartOfMagic-Librarian-Toxicology")
set(TOXICOLOGY_DIST_DIR "${DIST_DIR}/${TOXICOLOGY_DIST_NAME}_${TOXICOLOGY_VERSION}")
set(TOXICOLOGY_PDB_DIR "${TOXICOLOGY_DIST_DIR}_DebugSymbols")
set(TOXICOLOGY_ZIP "${DIST_DIR}/${TOXICOLOGY_DIST_NAME}_${TOXICOLOGY_VERSION}.zip")
set(TOXICOLOGY_PDB_ZIP "${DIST_DIR}/${TOXICOLOGY_DIST_NAME}_${TOXICOLOGY_VERSION}_DebugSymbols.zip")
set(TOXICOLOGY_KO_DIR "${TOXICOLOGY_DIST_DIR}_KO")
set(TOXICOLOGY_KO_ZIP "${DIST_DIR}/${TOXICOLOGY_DIST_NAME}_${TOXICOLOGY_VERSION}_KO.zip")
set(TOXICOLOGY_DOC_DIR "${TOXICOLOGY_DIST_DIR}/SKSE/Plugins/HeartOfMagic_Toxicology")
set(TOXICOLOGY_DATA "${CMAKE_SOURCE_DIR}/plugins/Toxicology/data")

set(TOXICOLOGY_NOTICES "${CMAKE_BINARY_DIR}/toxicology/THIRD-PARTY-NOTICES.md")
configure_file("${CMAKE_SOURCE_DIR}/plugins/Toxicology/THIRD-PARTY-NOTICES.md.in" "${TOXICOLOGY_NOTICES}" @ONLY)

# The vcpkg ports whose code is in HeartOfMagic_Toxicology.dll: its target_link_libraries (spdlog with fmt,
# nlohmann-json) and what CommonLibSSE-NG links or compiles in here (DirectXTK with DirectXMath, Xbyak:
# SKSE_SUPPORT_XBYAK is on; rapidcsv only with ENABLE_SKYRIM_VR, which is off). No zlib: neither the DLL nor
# CommonLib links it. A port added to the DLL needs its name here and a row in the notice.
set(_toxicology_license_ports fmt spdlog nlohmann-json xbyak directxmath directxtk)
set(_toxicology_license_commands)
foreach(_port IN LISTS _toxicology_license_ports)
    if(NOT EXISTS "${_vcpkg_share}/${_port}/copyright")
        message(FATAL_ERROR "DistributionToxicology: no license file for vcpkg port ${_port} at ${_vcpkg_share}/${_port}/copyright")
    endif()
    list(APPEND _toxicology_license_commands
        COMMAND "${CMAKE_COMMAND}" -E copy "${_vcpkg_share}/${_port}/copyright" "${TOXICOLOGY_DOC_DIR}/licenses/${_port}.txt")
endforeach()

add_custom_target(assemble_toxicology ALL COMMENT "Assembling the Toxicology addon...")
add_dependencies(assemble_toxicology HeartOfMagic_Toxicology)
# The Korean ESL must still be the English one with only its five text subrecords changed (tools/toxicology-esl-check,
# ESL.md "Korean ESL"). A mismatch stops the build here: after the old staging folders and zips are removed, so none
# is left to pass for this build, and before anything is staged or zipped.
add_dependencies(assemble_toxicology toxicology-esl-check)
add_custom_command(TARGET assemble_toxicology POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TOXICOLOGY_DIST_DIR}" "${TOXICOLOGY_PDB_DIR}" "${TOXICOLOGY_KO_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${TOXICOLOGY_ZIP}" "${TOXICOLOGY_PDB_ZIP}" "${TOXICOLOGY_KO_ZIP}"
    COMMAND "$<TARGET_FILE:toxicology-esl-check>" "${TOXICOLOGY_DATA}/HeartOfMagic_Toxicology.esp" "${TOXICOLOGY_DATA}/ko/HeartOfMagic_Toxicology.esp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${TOXICOLOGY_DOC_DIR}/licenses/CommonLibSSE-NG"
    "${TOXICOLOGY_DIST_DIR}/SKSE/Plugins/InventoryInjector"
    "${TOXICOLOGY_DIST_DIR}/Interface/HeartOfMagicToxicology"
    "${TOXICOLOGY_PDB_DIR}/SKSE/Plugins"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:HeartOfMagic_Toxicology>" "${TOXICOLOGY_DIST_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:HeartOfMagic_Toxicology>>,copy,true> "$<$<BOOL:$<TARGET_PDB_FILE:HeartOfMagic_Toxicology>>:$<TARGET_PDB_FILE:HeartOfMagic_Toxicology>>" "${TOXICOLOGY_PDB_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${TOXICOLOGY_DATA}/HeartOfMagic_Toxicology.esp" "${TOXICOLOGY_DIST_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${TOXICOLOGY_DATA}/HeartOfMagic_Toxicology.json" "${TOXICOLOGY_DIST_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${TOXICOLOGY_DATA}/SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json" "${TOXICOLOGY_DIST_DIR}/SKSE/Plugins/InventoryInjector/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${TOXICOLOGY_DATA}/Interface/HeartOfMagicToxicology/icons.swf" "${TOXICOLOGY_DIST_DIR}/Interface/HeartOfMagicToxicology/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/LICENSE" "${TOXICOLOGY_NOTICES}" "${TOXICOLOGY_DOC_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy
    "${_commonlib_dir}/COPYING.txt"
    "${_commonlib_dir}/EXCEPTIONS.md"
    "${_commonlib_dir}/licenses/LICENSE-MIT.txt"
    "${TOXICOLOGY_DOC_DIR}/licenses/CommonLibSSE-NG/"
    ${_toxicology_license_commands}
    VERBATIM)
# The zips are made from inside the staging folders, so their root is Data (no wrapping folder)
add_custom_command(TARGET assemble_toxicology POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E echo "Creating ${TOXICOLOGY_DIST_NAME}_${TOXICOLOGY_VERSION}.zip..."
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${TOXICOLOGY_ZIP}" --format=zip -- HeartOfMagic_Toxicology.esp SKSE Interface
    WORKING_DIRECTORY "${TOXICOLOGY_DIST_DIR}"
    VERBATIM)
# The Korean staging folder: a copy of the English one with the Korean ESL over it
add_custom_command(TARGET assemble_toxicology POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${TOXICOLOGY_DIST_DIR}" "${TOXICOLOGY_KO_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${TOXICOLOGY_DATA}/ko/HeartOfMagic_Toxicology.esp" "${TOXICOLOGY_KO_DIR}/"
    VERBATIM)
add_custom_command(TARGET assemble_toxicology POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E echo "Creating ${TOXICOLOGY_DIST_NAME}_${TOXICOLOGY_VERSION}_KO.zip..."
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${TOXICOLOGY_KO_ZIP}" --format=zip -- HeartOfMagic_Toxicology.esp SKSE Interface
    WORKING_DIRECTORY "${TOXICOLOGY_KO_DIR}"
    VERBATIM)
add_custom_command(TARGET assemble_toxicology POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${TOXICOLOGY_PDB_ZIP}" --format=zip -- SKSE
    WORKING_DIRECTORY "${TOXICOLOGY_PDB_DIR}"
    VERBATIM)
