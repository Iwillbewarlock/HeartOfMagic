# =============================================================================
# GreySelfTestGate.cmake - the builder's --self-test as a POST_BUILD gate (cmake -P)
# =============================================================================
# Runs <EXE> --self-test --golden-dir <GOLDEN> (the golden fixtures required: a missing folder fails), writes its whole
# output to <LOG> (stderr to <LOG>.err), prints the summary line, the time it took and every FAIL line, and fails the
# build on a non-zero exit or a missing summary (the exe moved to <name>.gate-failed.exe; stderr printed). A pass removes a
# <name>.gate-failed.exe an earlier failed gate left, so no stale failed exe sits beside a passing one.
# Arguments: -DEXE=<builder exe> -DGOLDEN=<golden folder> -DLOG=<log file>

cmake_minimum_required(VERSION 3.21)

foreach(arg EXE GOLDEN LOG)
    if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
        message(FATAL_ERROR "GreySelfTestGate.cmake: -D${arg}=... is required")
    endif()
endforeach()

get_filename_component(exe_dir "${EXE}" DIRECTORY)
get_filename_component(exe_name "${EXE}" NAME_WE)
set(failed_exe "${exe_dir}/${exe_name}.gate-failed.exe")

# an earlier run's log must not pass for this one's
file(REMOVE "${LOG}" "${LOG}.err")
string(TIMESTAMP started "%s")
execute_process(COMMAND "${EXE}" --self-test --golden-dir "${GOLDEN}"
    RESULT_VARIABLE result OUTPUT_FILE "${LOG}" ERROR_FILE "${LOG}.err" TIMEOUT 600)
string(TIMESTAMP ended "%s")
math(EXPR seconds "${ended} - ${started}")

set(summary "")
set(failures "")
if(EXISTS "${LOG}")
    file(STRINGS "${LOG}" summary REGEX "^SELF-TEST ")
    file(STRINGS "${LOG}" failures REGEX "^FAIL [^ ]")
endif()
set(errors "")
if(EXISTS "${LOG}.err")
    file(READ "${LOG}.err" errors)
    string(STRIP "${errors}" errors)
endif()

if(summary STREQUAL "")
    # no summary line: the exe did not run (a missing DLL such as vcomp140.dll, a crash) or stopped before the end
    message(STATUS "Black Flame builder --self-test: no summary line (the exe did not run, or stopped before the end; exit "
                   "'${result}', about ${seconds} s; log: ${LOG})")
else()
    message(STATUS "Black Flame builder --self-test: ${summary} in about ${seconds} s (log: ${LOG})")
endif()
foreach(line IN LISTS failures)
    message(STATUS "  ${line}")
endforeach()

if(NOT result EQUAL 0 OR summary STREQUAL "")
    # stderr only on a failure: a passing run writes the refusal tests' own messages there
    if(NOT errors STREQUAL "")
        message(STATUS "  stderr (${LOG}.err):\n${errors}")
    endif()
    # The exe is moved aside, so the next build links and gates it again (a post-build step that failed once is not run
    # again for an exe that is already there) and nothing picks up an exe that failed; the moved one still runs, for
    # --self-test --print-golden
    file(REMOVE "${failed_exe}")
    if(NOT EXISTS "${EXE}")
        message(FATAL_ERROR "Black Flame builder --self-test failed (exit '${result}'): ${EXE} is not there, so nothing ran")
    endif()
    file(RENAME "${EXE}" "${failed_exe}")
    message(FATAL_ERROR "Black Flame builder --self-test failed (exit '${result}'): see ${LOG}. The exe was moved to "
                        "${failed_exe} (run it with --self-test --golden-dir ${GOLDEN} --print-golden to print this build's "
                        "golden table)")
endif()
file(REMOVE "${failed_exe}")
