# =============================================================================
# GreySelfTestGate.cmake - the builder's --self-test as a POST_BUILD gate (cmake -P)
# =============================================================================
# Runs <EXE> --self-test --golden-dir <GOLDEN> (the golden fixtures required: a missing folder fails), writes its whole
# output to <LOG>, prints the summary line, the time it took and every FAIL line, and fails the build on a non-zero exit
# (the exe moved to <name>.gate-failed.exe).
# Arguments: -DEXE=<builder exe> -DGOLDEN=<golden folder> -DLOG=<log file>

cmake_minimum_required(VERSION 3.21)

foreach(arg EXE GOLDEN LOG)
    if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
        message(FATAL_ERROR "GreySelfTestGate.cmake: -D${arg}=... is required")
    endif()
endforeach()

string(TIMESTAMP started "%s")
execute_process(COMMAND "${EXE}" --self-test --golden-dir "${GOLDEN}"
    RESULT_VARIABLE result OUTPUT_FILE "${LOG}" ERROR_FILE "${LOG}.err" TIMEOUT 600)
string(TIMESTAMP ended "%s")
math(EXPR seconds "${ended} - ${started}")

file(STRINGS "${LOG}" summary REGEX "^SELF-TEST ")
file(STRINGS "${LOG}" failures REGEX "^FAIL [^ ]")
message(STATUS "Black Flame builder --self-test: ${summary} in about ${seconds} s (log: ${LOG})")
foreach(line IN LISTS failures)
    message(STATUS "  ${line}")
endforeach()
if(NOT result EQUAL 0)
    # The exe is moved aside, so the next build links and gates it again (a post-build step that failed once is not run
    # again for an exe that is already there) and nothing picks up an exe that failed; the moved one still runs, for
    # --self-test --print-golden
    get_filename_component(exe_dir "${EXE}" DIRECTORY)
    get_filename_component(exe_name "${EXE}" NAME_WE)
    set(failed_exe "${exe_dir}/${exe_name}.gate-failed.exe")
    file(REMOVE "${failed_exe}")
    file(RENAME "${EXE}" "${failed_exe}")
    message(FATAL_ERROR "Black Flame builder --self-test failed (exit ${result}): see ${LOG}. The exe was moved to "
                        "${failed_exe} (run it with --self-test --print-golden to print this build's golden table)")
endif()
