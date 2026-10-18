# =============================================================================
# GreyIdentity.cmake - the builder's output identity, written at build time (cmake -P)
# =============================================================================
# The identity is the text the DLL copies into every key prefix (out=<identity>, CopyContractKeys.h):
#   <n>                 a release build: kBuilderOutputVersion from CopyContractKeys.h, nothing else
#   <n>+dev<16 hex>     a build of any configuration but Release (Debug, RelWithDebInfo, MinSizeRel), or one whose
#                       kBuilderOutputVersion has no row in GreyGoldenTable.h's kReleasedPins (an unpinned table, or a
#                       forgotten bump: never a released number; review round 1), or one whose output-affecting sources
#                       differ from git HEAD (modified, staged or untracked), or one where git cannot tell: <16 hex> is a
#                       digest of the configuration name and those sources
# Output-affecting sources: tools/blackflame-greybuild/** (this script, the golden fixtures included), the contract
# headers the builder compiles (plugins/BlackFlame/src/CopyContract*.h, DataPathRule.h), and what picks its compiler
# flags and library versions: tools/CMakeLists.txt (its target), the root CMakeLists.txt and plugins/cmake/*.cmake
# (CompilerFlags.cmake appends /GL, /EHa, /Gw ... to every target's flags), CMakePresets.json (the base flags, the
# standard, the runtime and the triplet), vcpkg.json and vcpkg-configuration.json (the library versions and features;
# /openmp is linked explicitly: tools/CMakeLists.txt's find_package(OpenMP) and OpenMP::OpenMP_CXX on the builder target,
# for its own #pragma omp and omp_set_num_threads). Over-inclusion only gives a dev build its own keys
# (a rebuild), never a release key for other output.
# Digest: SHA-256 of "config\t<configuration>\n" then "<path>\t<SHA-256 of its bytes>\n" over those files in sorted path
# order, its first 16 hex digits (so a Debug and a RelWithDebInfo build of the same sources key apart). git runs with
# --no-optional-locks: a status from a build never takes the index lock an editor's or the user's git may want.
# Not FNV-1a 64: a CMake script has no unsigned 64-bit arithmetic and a byte loop over the sources takes minutes; the
# digest only has to tell two source states apart, the DLL copies it verbatim.
# Writes OUT_DIR/GreyIdentityText.h (the builder's compiled copy) and OUT_DIR/GreyIdentity.rc (the RCDATA resource the
# DLL reads), each only when its text changes, so an unchanged state rebuilds nothing.
# Arguments: -DROOT=<repository root> -DOUT_DIR=<folder> -DCONFIG=<build configuration> [-DGIT=<git executable>]

cmake_minimum_required(VERSION 3.21)

foreach(arg ROOT OUT_DIR CONFIG)
    if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
        message(FATAL_ERROR "GreyIdentity.cmake: -D${arg}=... is required")
    endif()
endforeach()
# One spelling for the paths below (forward slashes, no "." or ".." parts, no doubled separators): a ROOT given with
# backslashes or a trailing "/." globs and writes the same files
cmake_path(SET ROOT NORMALIZE "${ROOT}")
cmake_path(SET OUT_DIR NORMALIZE "${OUT_DIR}")
foreach(arg ROOT OUT_DIR)
    string(REGEX REPLACE "([^:/])/+$" "\\1" ${arg} "${${arg}}")  # "x/." normalises to "x/": no trailing separator
endforeach()

set(keys_header "${ROOT}/plugins/BlackFlame/src/CopyContractKeys.h")
file(READ "${keys_header}" keys_text)
string(REGEX MATCH "kBuilderOutputVersion = ([0-9]+);" version_match "${keys_text}")
if(NOT version_match)
    message(FATAL_ERROR "GreyIdentity.cmake: no 'kBuilderOutputVersion = <n>;' in ${keys_header}")
endif()
set(version "${CMAKE_MATCH_1}")

set(source_globs
    "tools/blackflame-greybuild/*"
    "plugins/BlackFlame/src/CopyContract*.h"
    "plugins/BlackFlame/src/DataPathRule.h"
    "tools/CMakeLists.txt"
    "CMakeLists.txt"
    "plugins/cmake/*.cmake"
    "CMakePresets.json"
    "vcpkg.json"
    "vcpkg-configuration.json")

set(dev_why "")
if(NOT CONFIG STREQUAL "Release")
    set(dev_why "a ${CONFIG} build (only Release builds are releases)")
endif()

# A released number needs its pin: the rows of the kReleasedPins block only (kTables' rows look the same),
# "{ <n>, "<16 lower-case hex>" }", with // comments stripped first (a commented-out row is no pin; review round 2)
set(pins_header "${ROOT}/tools/blackflame-greybuild/GreyGoldenTable.h")
file(READ "${pins_header}" pins_text)
string(FIND "${pins_text}" "kReleasedPins = {" pins_at)
if(pins_at EQUAL -1)
    message(FATAL_ERROR "GreyIdentity.cmake: no 'kReleasedPins = {' in ${pins_header}")
endif()
string(SUBSTRING "${pins_text}" ${pins_at} -1 pins_text)
string(REGEX REPLACE "//[^\n]*" "" pins_text "${pins_text}")  # before the end is looked for: a comment may hold "};"
string(FIND "${pins_text}" "};" pins_end)
if(pins_end EQUAL -1)
    message(FATAL_ERROR "GreyIdentity.cmake: the kReleasedPins block in ${pins_header} has no '};'")
endif()
string(SUBSTRING "${pins_text}" 0 ${pins_end} pins_text)
string(REPEAT "[0-9a-f]" 16 pin_hex)  # CMake's regex has no {16}
string(REGEX MATCH "\\{[ \t\r\n]*${version}[ \t\r\n]*,[ \t\r\n]*\"${pin_hex}\"[ \t\r\n]*\\}" pin_match "${pins_text}")
if(dev_why STREQUAL "" AND NOT pin_match)
    set(dev_why "kBuilderOutputVersion ${version} has no row in kReleasedPins (GreyGoldenTable.h), so it is no released number")
endif()

if(dev_why STREQUAL "")
    if(NOT DEFINED GIT OR GIT STREQUAL "")
        find_program(GIT git)
    endif()
    if(NOT GIT)
        set(dev_why "git was not found, so the sources cannot be compared with HEAD")
    else()
        execute_process(COMMAND "${GIT}" --no-optional-locks -C "${ROOT}" status --porcelain --untracked-files=all -- ${source_globs}
            RESULT_VARIABLE git_result OUTPUT_VARIABLE git_status ERROR_VARIABLE git_error TIMEOUT 60)
        if(NOT git_result EQUAL 0)
            string(STRIP "${git_error}" git_error)
            set(dev_why "git status failed (${git_result}: ${git_error}), so the sources cannot be compared with HEAD")
        elseif(NOT git_status STREQUAL "")
            string(REGEX MATCHALL "\n" changed_lines "\n${git_status}")
            list(LENGTH changed_lines changed)
            math(EXPR changed "${changed} - 1")
            set(dev_why "${changed} output-affecting source file(s) differ from git HEAD")
        endif()
    endif()
endif()

if(dev_why STREQUAL "")
    set(identity "${version}")
else()
    set(files "")
    foreach(pattern IN LISTS source_globs)
        # Only a folder pattern ("<folder>/*") recurses: GLOB_RECURSE on "CMakeLists.txt" or "vcpkg.json" would also take
        # every file of that name below ROOT (plugins/external, build/ scratch), as git status's pathspecs do not
        if(pattern MATCHES "/\\*$")
            file(GLOB_RECURSE matched LIST_DIRECTORIES false RELATIVE "${ROOT}" "${ROOT}/${pattern}")
        else()
            file(GLOB matched LIST_DIRECTORIES false RELATIVE "${ROOT}" "${ROOT}/${pattern}")
        endif()
        list(APPEND files ${matched})
    endforeach()
    list(REMOVE_DUPLICATES files)
    list(SORT files)
    list(LENGTH files file_count)
    if(file_count EQUAL 0)
        message(FATAL_ERROR "GreyIdentity.cmake: no output-affecting source matched under ROOT '${ROOT}' (${source_globs}): "
                            "ROOT is not the repository root, so no digest can tell this build's sources apart")
    endif()
    set(listing "config\t${CONFIG}\n")
    foreach(file IN LISTS files)
        file(SHA256 "${ROOT}/${file}" file_hash)
        string(APPEND listing "${file}\t${file_hash}\n")
    endforeach()
    string(SHA256 digest "${listing}")
    string(SUBSTRING "${digest}" 0 16 digest)
    set(identity "${version}+dev${digest}")
endif()

set(header_text "#pragma once\n// Written by tools/blackflame-greybuild/GreyIdentity.cmake at build time: do not edit, do not commit\n#define BLACKFLAME_BUILDER_OUTPUT_IDENTITY \"${identity}\"\n")
set(rc_text "// Written by tools/blackflame-greybuild/GreyIdentity.cmake at build time: do not edit, do not commit\n#include \"${ROOT}/plugins/BlackFlame/src/CopyContractIdentity.h\"\nBLACKFLAME_BUILDER_IDENTITY_RESOURCE RCDATA { \"${identity}\" }\n")

function(write_if_changed a_file a_text)
    if(EXISTS "${a_file}")
        file(READ "${a_file}" old_text)
        if(old_text STREQUAL a_text)
            return()
        endif()
    endif()
    file(WRITE "${a_file}" "${a_text}")
endfunction()

file(MAKE_DIRECTORY "${OUT_DIR}")
write_if_changed("${OUT_DIR}/GreyIdentityText.h" "${header_text}")
write_if_changed("${OUT_DIR}/GreyIdentity.rc" "${rc_text}")

if(dev_why STREQUAL "")
    message(STATUS "Black Flame builder output identity: ${identity} (release: the sources are git HEAD's)")
else()
    message(STATUS "Black Flame builder output identity: ${identity} (dev: ${dev_why}; digest over ${file_count} file(s))")
endif()
