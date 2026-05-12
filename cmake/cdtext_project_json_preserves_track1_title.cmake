if(NOT DEFINED CDTEXT_DIFF_BIN)
    message(FATAL_ERROR "CDTEXT_DIFF_BIN is required")
endif()

if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(PROJECT_JSON "${BINARY_DIR}/cdtext-project-json-preserves-track1-title.json")
set(EXPORT_JSON "${BINARY_DIR}/cdtext-project-json-preserves-track1-title-export.json")

file(WRITE "${PROJECT_JSON}" [=[
{
  "albumTitle": "Ｅｌｍａ　＆　創作",
  "albumArtist": "ヨルシカ",
  "cdTextLanguage": "japanese",
  "tracks": [
    {
      "number": 1,
      "filePath": "/music/01.wav",
      "title": "歩く",
      "artist": "ヨルシカ",
      "durationSeconds": 207
    },
    {
      "number": 2,
      "filePath": "/music/02.wav",
      "title": "車窓",
      "artist": "ヨルシカ",
      "durationSeconds": 116
    }
  ]
}
]=])

execute_process(
    COMMAND "${CDTEXT_DIFF_BIN}" export-current
            --project-json "${PROJECT_JSON}"
            --json "${EXPORT_JSON}"
    RESULT_VARIABLE export_result
    OUTPUT_VARIABLE export_stdout
    ERROR_VARIABLE export_stderr
)

if(NOT export_result EQUAL 0)
    message(FATAL_ERROR
        "cdtext-diff export-current failed.\nstdout:\n${export_stdout}\nstderr:\n${export_stderr}")
endif()

if(NOT EXISTS "${EXPORT_JSON}")
    message(FATAL_ERROR "Expected JSON artifact was not created: ${EXPORT_JSON}")
endif()

file(READ "${EXPORT_JSON}" export_json_text)

string(FIND "${export_json_text}" "\"effectiveValue\": \"歩く\"" walk_pos)
if(walk_pos EQUAL -1)
    message(FATAL_ERROR
        "Expected prepared/exported JSON to preserve track 1 title 歩く.\n${export_json_text}")
endif()

string(FIND "${export_json_text}" "星遇９タートライン" bad_title_pos)
if(NOT bad_title_pos EQUAL -1)
    message(FATAL_ERROR
        "Unexpected legacy corrupted title appeared in export JSON.\n${export_json_text}")
endif()
