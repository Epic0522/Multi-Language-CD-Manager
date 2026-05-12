if(NOT DEFINED CDTEXT_DIFF_BIN)
    message(FATAL_ERROR "CDTEXT_DIFF_BIN is required")
endif()

if(NOT DEFINED REFERENCE_SAMPLE_DIR)
    message(FATAL_ERROR "REFERENCE_SAMPLE_DIR is required")
endif()

if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(EXPORT_JSON "${BINARY_DIR}/cdtext-export-reference-sample.json")
set(EXPORT_CDT "${BINARY_DIR}/cdtext-export-reference-sample.cdt")

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

if(NOT EXISTS "${EXPORT_CDT}")
    message(FATAL_ERROR "Expected CDT artifact was not created: ${EXPORT_CDT}")
endif()

file(SIZE "${EXPORT_CDT}" export_cdt_size)
if(NOT export_cdt_size EQUAL 468)
    message(FATAL_ERROR "Expected CDT artifact size 468, got ${export_cdt_size}")
endif()

if(NOT EXISTS "${EXPORT_JSON}")
    message(FATAL_ERROR "Expected JSON artifact was not created: ${EXPORT_JSON}")
endif()

file(READ "${EXPORT_JSON}" export_json_text)
string(FIND "${export_json_text}" "\"blockCount\": 1" block_count_pos)
if(block_count_pos EQUAL -1)
    message(FATAL_ERROR "Expected JSON artifact to include blockCount=1.\n${export_json_text}")
endif()
