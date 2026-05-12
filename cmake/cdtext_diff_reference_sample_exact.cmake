if(NOT DEFINED CDTEXT_DIFF_BIN)
    message(FATAL_ERROR "CDTEXT_DIFF_BIN is required")
endif()

if(NOT DEFINED REFERENCE_SAMPLE_DIR)
    message(FATAL_ERROR "REFERENCE_SAMPLE_DIR is required")
endif()

if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(EXPORT_JSON "${BINARY_DIR}/cdtext-diff-reference-sample-exact.json")

execute_process(
    COMMAND "${CDTEXT_DIFF_BIN}" export-current
            --sample-dir "${REFERENCE_SAMPLE_DIR}"
            --json "${EXPORT_JSON}"
    RESULT_VARIABLE export_result
    OUTPUT_VARIABLE export_stdout
    ERROR_VARIABLE export_stderr
)

if(NOT export_result EQUAL 0)
    message(FATAL_ERROR
        "cdtext-diff export-current failed.\nstdout:\n${export_stdout}\nstderr:\n${export_stderr}")
endif()

execute_process(
    COMMAND "${CDTEXT_DIFF_BIN}" compare
            "${REFERENCE_SAMPLE_DIR}"
            "${EXPORT_JSON}"
            --left-format reference-sample
            --right-format packs-json
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_stdout
    ERROR_VARIABLE compare_stderr
)

if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR
        "reference sample compare failed.\nstdout:\n${compare_stdout}\nstderr:\n${compare_stderr}")
endif()

set(compare_output "${compare_stdout}\n${compare_stderr}")
string(FIND "${compare_output}" "CD-TEXT compare result (exact): identical" identical_pos)
if(identical_pos EQUAL -1)
    message(FATAL_ERROR
        "reference sample compare did not report identical.\nstdout:\n${compare_stdout}\nstderr:\n${compare_stderr}")
endif()
