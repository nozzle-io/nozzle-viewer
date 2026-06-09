if(NOT DEFINED NOZZLE_VIEWER_EXE)
    message(FATAL_ERROR "NOZZLE_VIEWER_EXE is required")
endif()
if(NOT DEFINED EVIDENCE_PATH)
    message(FATAL_ERROR "EVIDENCE_PATH is required")
endif()

execute_process(
    COMMAND "${NOZZLE_VIEWER_EXE}" --smoke-receiver --source nozzle_viewer_bounds_regression --width 24 --height 240 --min-frames 2 --timeout-ms 10 --evidence "${EVIDENCE_PATH}" --expect-moving-marker
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(result EQUAL 0)
    message(FATAL_ERROR "expected smoke receiver to fail for too-small moving marker width\nstdout=${stdout}\nstderr=${stderr}")
endif()
if(NOT EXISTS "${EVIDENCE_PATH}")
    message(FATAL_ERROR "expected evidence JSON at ${EVIDENCE_PATH}")
endif()
file(READ "${EVIDENCE_PATH}" evidence)
if(NOT evidence MATCHES "\"verdict\": \"FAIL\"")
    message(FATAL_ERROR "missing deterministic FAIL verdict in evidence: ${evidence}")
endif()
if(NOT evidence MATCHES "invalid_smoke_dimensions:moving_marker_width_too_small")
    message(FATAL_ERROR "missing moving marker width failure reason in evidence: ${evidence}")
endif()
