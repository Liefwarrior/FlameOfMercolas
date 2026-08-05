# EVERY CASE IN THE BINARY IS RUN BY EXACTLY ONE ctest ENTRY.
#
# #81 stopped registering a ctest entry per TEST_CASE and started registering
# one per test FILE — see the registration block in native/CMakeLists.txt for
# the measurement that forced it. The saving is real and so is the new way to
# lose coverage silently:
#
#   * a source file added to the executable but not to GRANADAD_TEST_SOURCES
#     would compile its cases INTO the binary and have no entry running them;
#   * a filter that matches nothing — a renamed file, a typo, a `--source-file`
#     pattern that stopped matching after a path change — makes doctest run zero
#     cases and exit 0. Green, and proving nothing.
#
# Neither is visible in a pass/fail column. Both are visible in arithmetic: add
# up what each per-file entry actually matches and it must equal what the binary
# says it holds. Less means cases nobody runs. More means a case counted twice,
# which is a pattern matching two files.
#
# Run by ctest as `granadad-test-partition-is-complete`. Costs one --count
# launch per file plus one, and every one of them is a few milliseconds because
# --count is a query flag: doctest prints the number and quits without running
# anything.

if(NOT DEFINED GRANADAD_TEST_EXE)
    message(FATAL_ERROR "GRANADAD_TEST_EXE is not set")
endif()
if(NOT DEFINED GRANADAD_TEST_SOURCES)
    message(FATAL_ERROR "GRANADAD_TEST_SOURCES is not set")
endif()

# Asks the binary how many cases pass `filter_args`, or fails loudly.
function(granadad_count_cases out_var)
    execute_process(
        COMMAND "${GRANADAD_TEST_EXE}" --count ${ARGN}
        OUTPUT_VARIABLE reply
        ERROR_VARIABLE  errors
        RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "'${GRANADAD_TEST_EXE} --count ${ARGN}' exited ${status}.\n${reply}${errors}")
    endif()
    # doctest prints: "[doctest] unskipped test cases passing the current filters: N"
    if(NOT reply MATCHES "passing the current filters: *([0-9]+)")
        message(FATAL_ERROR
            "could not read a case count out of doctest's reply. It printed:\n${reply}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

granadad_count_cases(total)

string(REPLACE "|" ";" sources "${GRANADAD_TEST_SOURCES}")

set(summed 0)
set(empty_files "")
foreach(src IN LISTS sources)
    get_filename_component(name "${src}" NAME)
    granadad_count_cases(here "--source-file=*${name}")
    if(here EQUAL 0)
        list(APPEND empty_files "${name}")
    endif()
    math(EXPR summed "${summed} + ${here}")
endforeach()

if(empty_files)
    string(REPLACE ";" "\n          " listed "${empty_files}")
    message(FATAL_ERROR
        "these registered test files hold no cases at all:\n"
        "          ${listed}\n"
        "       Their ctest entries run nothing and pass. Either the file lost\n"
        "       its TEST_CASEs, or --source-file=*<name> no longer matches the\n"
        "       __FILE__ the compiler baked in.")
endif()

if(NOT summed EQUAL total)
    message(FATAL_ERROR
        "the per-file ctest entries do not partition the suite.\n"
        "       the binary holds     ${total} cases\n"
        "       the entries cover    ${summed}\n"
        "       Fewer means cases nobody runs -- a .cpp compiled into\n"
        "       granadad-tests without being added to GRANADAD_TEST_SOURCES in\n"
        "       native/CMakeLists.txt. More means one case matched by two\n"
        "       entries. Both are silent in a pass/fail column, which is why\n"
        "       this check exists.")
endif()

message(STATUS "test partition: ${total} cases across ${GRANADAD_TEST_SOURCES}")
