if(NOT DEFINED CDTEXT_DIFF_BIN)
    message(FATAL_ERROR "CDTEXT_DIFF_BIN is required")
endif()

if(NOT DEFINED REFERENCE_SAMPLE_DIR)
    message(FATAL_ERROR "REFERENCE_SAMPLE_DIR is required")
endif()

if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(EXPORT_SONY_BIN "${BINARY_DIR}/cdtext-export-reference-sample-sony.bin")

execute_process(
    COMMAND "${CDTEXT_DIFF_BIN}" export-current
            --sample-dir "${REFERENCE_SAMPLE_DIR}"
            --sony-bin-out "${EXPORT_SONY_BIN}"
    RESULT_VARIABLE export_result
    OUTPUT_VARIABLE export_stdout
    ERROR_VARIABLE export_stderr
)

if(NOT export_result EQUAL 0)
    message(FATAL_ERROR
        "cdtext-diff export-current failed.\nstdout:\n${export_stdout}\nstderr:\n${export_stderr}")
endif()

if(NOT EXISTS "${EXPORT_SONY_BIN}")
    message(FATAL_ERROR "Expected Sony BIN artifact was not created: ${EXPORT_SONY_BIN}")
endif()

file(SIZE "${EXPORT_SONY_BIN}" export_sony_bin_size)
if(NOT export_sony_bin_size EQUAL 433)
    message(FATAL_ERROR "Expected Sony BIN artifact size 433, got ${export_sony_bin_size}")
endif()
