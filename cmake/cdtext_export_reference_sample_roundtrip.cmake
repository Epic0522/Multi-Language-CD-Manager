if(NOT DEFINED CDTEXT_DIFF_BIN)
    message(FATAL_ERROR "CDTEXT_DIFF_BIN is required")
endif()

if(NOT DEFINED CDTEXT_DISC_RECORDING_CHECK_BIN)
    message(FATAL_ERROR "CDTEXT_DISC_RECORDING_CHECK_BIN is required")
endif()

if(NOT DEFINED REFERENCE_SAMPLE_DIR)
    message(FATAL_ERROR "REFERENCE_SAMPLE_DIR is required")
endif()

if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(EXPORT_JSON "${BINARY_DIR}/cdtext-export-roundtrip.json")
set(EXPORT_CDT "${BINARY_DIR}/cdtext-export-roundtrip.cdt")
set(CDT_PARSE_JSON "${BINARY_DIR}/cdtext-export-roundtrip-cdt-parse.json")
set(CDT_COMPARE_JSON "${BINARY_DIR}/cdtext-export-roundtrip-compare.json")
set(DISC_JSON "${BINARY_DIR}/cdtext-export-roundtrip-discrecording.json")

execute_process(
    COMMAND "${CDTEXT_DIFF_BIN}" export-current
            --sample-dir "${REFERENCE_SAMPLE_DIR}"
            --json "${EXPORT_JSON}"
            --cdt-out "${EXPORT_CDT}"
    RESULT_VARIABLE export_result
    OUTPUT_VARIABLE export_stdout
    ERROR_VARIABLE export_stderr
)

if(NOT export_result EQUAL 0)
    message(FATAL_ERROR
        "cdtext-diff export-current failed.\nstdout:\n${export_stdout}\nstderr:\n${export_stderr}")
endif()

execute_process(
    COMMAND "${CDTEXT_DIFF_BIN}" parse
            "${EXPORT_CDT}"
            --format cdt
            --json "${CDT_PARSE_JSON}"
    RESULT_VARIABLE parse_result
    OUTPUT_VARIABLE parse_stdout
    ERROR_VARIABLE parse_stderr
)

if(NOT parse_result EQUAL 0)
    message(FATAL_ERROR
        "cdtext-diff parse failed.\nstdout:\n${parse_stdout}\nstderr:\n${parse_stderr}")
endif()

execute_process(
    COMMAND "${CDTEXT_DIFF_BIN}" compare
            "${EXPORT_CDT}"
            "${EXPORT_JSON}"
            --left-format cdt
            --right-format packs-json
            --mode exact
            --json "${CDT_COMPARE_JSON}"
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_stdout
    ERROR_VARIABLE compare_stderr
)

if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR
        "cdtext-diff compare failed.\nstdout:\n${compare_stdout}\nstderr:\n${compare_stderr}")
endif()

set(compare_output "${compare_stdout}\n${compare_stderr}")
string(FIND "${compare_output}" "CD-TEXT compare result (exact): identical" identical_pos)
if(identical_pos EQUAL -1)
    message(FATAL_ERROR
        "CDT roundtrip compare did not report identical.\nstdout:\n${compare_stdout}\nstderr:\n${compare_stderr}")
endif()

execute_process(
    COMMAND "${CDTEXT_DISC_RECORDING_CHECK_BIN}"
            --cdt-in "${EXPORT_CDT}"
            --expect-block-count 1
            --json "${DISC_JSON}"
    RESULT_VARIABLE disc_result
    OUTPUT_VARIABLE disc_stdout
    ERROR_VARIABLE disc_stderr
)

if(NOT disc_result EQUAL 0)
    message(FATAL_ERROR
        "cdtext-disc-recording-check failed.\nstdout:\n${disc_stdout}\nstderr:\n${disc_stderr}")
endif()
