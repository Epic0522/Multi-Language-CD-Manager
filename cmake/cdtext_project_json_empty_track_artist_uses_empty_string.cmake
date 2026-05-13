if(NOT DEFINED CDTEXT_DIFF_BIN)
    message(FATAL_ERROR "CDTEXT_DIFF_BIN is required")
endif()

if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(PROJECT_JSON "${BINARY_DIR}/cdtext-project-json-empty-track-artist.json")
set(EXPORT_JSON "${BINARY_DIR}/cdtext-project-json-empty-track-artist-export.json")
set(CHECK_PY "${BINARY_DIR}/cdtext-project-json-empty-track-artist-check.py")

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
      "artist": "",
      "durationSeconds": 207
    },
    {
      "number": 2,
      "filePath": "/music/02.wav",
      "title": "車窓",
      "artist": "",
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

file(WRITE "${CHECK_PY}" [=[
import json
import sys

path = sys.argv[1]
obj = json.load(open(path, "r", encoding="utf-8"))

size_info = obj["generatedMetadata"]["sizeInfoValues"]
performer_pack_count = size_info[5]
if performer_pack_count != 2:
    raise SystemExit(f"Expected performer pack count 2 for empty track artists, got {performer_pack_count}")

for pack in obj["packAssembly"]["packs"]:
    if pack["packTypeLabel"] != "PERFORMER":
        continue
    if "8140" in pack["rawHex"]:
        raise SystemExit(f"Unexpected fullwidth-space placeholder remained in performer pack: {pack['rawHex']}")
]=])

execute_process(
    COMMAND /usr/bin/python3 "${CHECK_PY}" "${EXPORT_JSON}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_stdout
    ERROR_VARIABLE check_stderr
)

if(NOT check_result EQUAL 0)
    message(FATAL_ERROR
        "empty track artist regression check failed.\nstdout:\n${check_stdout}\nstderr:\n${check_stderr}")
endif()
