cmake_minimum_required(VERSION 3.24)

project(
    MultiLanguageCDManager
    VERSION 0.1.0
    DESCRIPTION "Cross-platform CD manager focused on Japanese CD-TEXT."
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Multimedia Concurrent)

if(APPLE)
    find_library(DISC_RECORDING_FRAMEWORK DiscRecording)
    find_library(DISK_ARBITRATION_FRAMEWORK DiskArbitration)
    find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation)
    find_library(FOUNDATION_FRAMEWORK Foundation)
endif()

# libcdio detection without relying on pkg-config (which may be absent).
find_library(LIBCDIO_LIBRARY NAMES cdio libcdio
    PATHS /opt/homebrew/lib /usr/local/lib
    PATH_SUFFIXES cdio)
find_path(LIBCDIO_INCLUDE_DIR NAMES cdio/cdio.h cdio.h
    PATHS /opt/homebrew/include /usr/local/include)

set(CDMANAGER_HAS_LIBCDIO OFF)
set(CDMANAGER_HAS_LIBCDIO_PARANOIA OFF)

if(LIBCDIO_LIBRARY AND LIBCDIO_INCLUDE_DIR)
    set(CDMANAGER_HAS_LIBCDIO ON)
    set(LIBCDIO_INCLUDE_DIRS ${LIBCDIO_INCLUDE_DIR})
    set(LIBCDIO_LIBRARIES ${LIBCDIO_LIBRARY})
    message(STATUS "Found libcdio: ${LIBCDIO_LIBRARY}, include: ${LIBCDIO_INCLUDE_DIR}")
else()
    message(STATUS "libcdio not found — audio CD playback will be unavailable.")
endif()

set(CDMANAGER_HAS_LIBCDIO_CPP false)
set(CDMANAGER_HAS_LIBCDIO_PARANOIA_CPP false)

if(CDMANAGER_HAS_LIBCDIO)
    set(CDMANAGER_HAS_LIBCDIO_CPP true)
endif()

if(CDMANAGER_HAS_LIBCDIO_PARANOIA)
    set(CDMANAGER_HAS_LIBCDIO_PARANOIA_CPP true)
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/app/include/cdmanager/infrastructure/build/BuildFeatures.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/cdmanager/infrastructure/build/BuildFeatures.h
    @ONLY
)

qt_standard_project_setup()
enable_testing()

set(CDMANAGER_CDTEXT_CORE_HEADERS
    app/include/cdmanager/application/CdTextPreparationService.h
    app/include/cdmanager/application/CdTextWritePlanBuilder.h
    app/include/cdmanager/application/CdTextWritePayloadBuilder.h
    app/include/cdmanager/application/CdTextTypes.h
    app/include/cdmanager/application/ValidationTypes.h
    app/include/cdmanager/application/burn/CdTextPackAssembler.h
    app/include/cdmanager/application/burn/CdTextPackTypes.h
    app/include/cdmanager/domain/cdtext/CdTextField.h
    app/include/cdmanager/domain/project/CdProject.h
    app/include/cdmanager/domain/project/Track.h
    app/include/cdmanager/infrastructure/encoding/JapaneseFullwidthNormalizer.h
    app/include/cdmanager/infrastructure/encoding/MsJisCodec.h
)

set(CDMANAGER_CDTEXT_CORE_SOURCES
    app/src/application/CdTextPreparationService.cpp
    app/src/application/CdTextWritePlanBuilder.cpp
    app/src/application/CdTextWritePayloadBuilder.cpp
    app/src/application/burn/CdTextPackAssembler.cpp
    app/src/domain/project/CdProject.cpp
    app/src/domain/project/Track.cpp
    app/src/infrastructure/encoding/JapaneseFullwidthNormalizer.cpp
    app/src/infrastructure/encoding/MsJisCodec.cpp
)

add_library(cdmanager_cdtext_core STATIC
    ${CDMANAGER_CDTEXT_CORE_HEADERS}
    ${CDMANAGER_CDTEXT_CORE_SOURCES}
)

target_include_directories(cdmanager_cdtext_core
    PUBLIC
        app/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(cdmanager_cdtext_core
    PUBLIC
        Qt6::Core
        $<$<PLATFORM_ID:Darwin>:iconv>
)

target_compile_definitions(cdmanager_cdtext_core
    PRIVATE
        QT_NO_CAST_TO_ASCII
        QT_NO_CAST_FROM_ASCII
)

target_compile_options(cdmanager_cdtext_core
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

qt_add_executable(cdmanager
    app/include/cdmanager/application/CdTextPreviewBuilder.h
    app/include/cdmanager/application/import/DiscImportResult.h
    app/include/cdmanager/application/import/DiscImportService.h
    app/include/cdmanager/application/PlaybackService.h
    app/include/cdmanager/application/ExportService.h
    app/include/cdmanager/infrastructure/audio/AudioCdReader.h
    app/include/cdmanager/infrastructure/audio/AudioBurnSourcePreparer.h
    app/include/cdmanager/infrastructure/audio/AudioOutput.h
    app/include/cdmanager/infrastructure/audio/AudioRingBuffer.h
    app/include/cdmanager/infrastructure/burn/DiscRecordingBurner.h
    app/include/cdmanager/infrastructure/burn/CdrecordBurner.h
    app/include/cdmanager/infrastructure/burn/CdrdaoBurner.h
    app/include/cdmanager/infrastructure/burn/DrutilBurner.h
    app/include/cdmanager/infrastructure/burn/TocFileWriter.h
    app/include/cdmanager/infrastructure/disc/DiscDeviceGateway.h
    app/include/cdmanager/infrastructure/disc/DiscDeviceGatewayFactory.h
    app/include/cdmanager/infrastructure/disc/DiscKeepAlive.h
    app/include/cdmanager/infrastructure/disc/DiscMediaMonitor.h
    app/include/cdmanager/infrastructure/disc/DrutilCommandRunner.h
    app/include/cdmanager/infrastructure/disc/DrutilOutputParser.h
    app/include/cdmanager/infrastructure/disc/LegacySampleDiscGateway.h
    app/include/cdmanager/infrastructure/disc/SystemDiscGateway.h
    app/include/cdmanager/presentation/editor/AlbumDetailsWidget.h
    app/include/cdmanager/presentation/editor/CdTextPreviewWidget.h
    app/include/cdmanager/presentation/editor/BurnWidget.h
    app/include/cdmanager/presentation/editor/ExportWidget.h
    app/include/cdmanager/presentation/editor/TrackTableWidget.h
    app/include/cdmanager/presentation/mainwindow/MainWindow.h
    app/include/cdmanager/presentation/ui/MacVisualEffectHelper.h
    app/include/cdmanager/presentation/ui/UiPreferences.h
    app/include/cdmanager/tools/cdtextdiff/CdTextDiffTypes.h
    app/include/cdmanager/tools/cdtextdiff/CdTextDocumentParser.h
    app/include/cdmanager/tools/cdtextdiff/CdTextDiffEngine.h
    app/include/cdmanager/tools/cdtextdiff/CurrentProjectPackExporter.h
    app/src/main.cpp
    app/src/application/CdTextPreviewBuilder.cpp
    app/src/application/import/DiscImportService.cpp
    app/src/application/PlaybackService.cpp
    app/src/application/ExportService.cpp
    app/src/application/ProjectOverviewBuilder.cpp
    app/src/application/ProjectValidationService.cpp
    app/src/infrastructure/audio/AudioCdReader.cpp
    app/src/infrastructure/audio/AudioBurnSourcePreparer.cpp
    app/src/infrastructure/audio/AudioOutput.cpp
    app/src/infrastructure/audio/AudioRingBuffer.cpp
    app/src/infrastructure/burn/DiscRecordingBurner.mm
    app/src/infrastructure/burn/CdrecordBurner.cpp
    app/src/infrastructure/burn/CdrdaoBurner.cpp
    app/src/infrastructure/burn/DrutilBurner.cpp
    app/src/infrastructure/burn/TocFileWriter.cpp
    app/src/infrastructure/disc/DiscDeviceGatewayFactory.cpp
    app/src/infrastructure/disc/DiscKeepAlive.cpp
    app/src/infrastructure/disc/DiscMediaMonitor.mm
    app/src/infrastructure/disc/DrutilCommandRunner.cpp
    app/src/infrastructure/disc/DrutilOutputParser.cpp
    app/src/infrastructure/disc/LegacySampleDiscGateway.cpp
    app/src/infrastructure/disc/SystemDiscGateway.cpp
    app/src/presentation/editor/AlbumDetailsWidget.cpp
    app/src/presentation/editor/CdTextPreviewWidget.cpp
    app/src/presentation/editor/BurnWidget.cpp
    app/src/presentation/editor/ExportWidget.cpp
    app/src/presentation/editor/TrackTableWidget.cpp
    app/src/presentation/mainwindow/MainWindow.cpp
    app/src/presentation/ui/MacVisualEffectHelper.mm
    app/src/presentation/ui/UiPreferences.cpp
    app/src/tools/cdtextdiff/CdTextDiffTypes.cpp
    app/src/tools/cdtextdiff/CdTextDocumentParser.cpp
    app/src/tools/cdtextdiff/CdTextDiffEngine.cpp
    app/src/tools/cdtextdiff/CurrentProjectPackExporter.cpp
)

set_target_properties(cdmanager PROPERTIES
    OUTPUT_NAME "MultiLanguageCDManager"
)

target_include_directories(cdmanager
    PRIVATE
        app/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        $<$<BOOL:${CDMANAGER_HAS_LIBCDIO}>:${LIBCDIO_INCLUDE_DIRS}>
)

target_link_libraries(cdmanager
    PRIVATE
        cdmanager_cdtext_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::Concurrent
        $<$<BOOL:${CDMANAGER_HAS_LIBCDIO}>:${LIBCDIO_LIBRARIES}>
        $<$<BOOL:${DISC_RECORDING_FRAMEWORK}>:${DISC_RECORDING_FRAMEWORK}>
        $<$<BOOL:${DISK_ARBITRATION_FRAMEWORK}>:${DISK_ARBITRATION_FRAMEWORK}>
)

target_compile_definitions(cdmanager
    PRIVATE
        QT_NO_CAST_TO_ASCII
        QT_NO_CAST_FROM_ASCII
        CDMANAGER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_options(cdmanager
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(cdtext-diff
    app/include/cdmanager/tools/cdtextdiff/CdTextDiffTypes.h
    app/include/cdmanager/tools/cdtextdiff/CdTextDocumentParser.h
    app/include/cdmanager/tools/cdtextdiff/CdTextDiffEngine.h
    app/include/cdmanager/tools/cdtextdiff/CurrentProjectPackExporter.h
    app/src/tools/cdtextdiff/CdTextDiffTypes.cpp
    app/src/tools/cdtextdiff/CdTextDocumentParser.cpp
    app/src/tools/cdtextdiff/CdTextDiffEngine.cpp
    app/src/tools/cdtextdiff/CurrentProjectPackExporter.cpp
    app/src/tools/cdtextdiff/main.cpp
)

target_include_directories(cdtext-diff
    PRIVATE
        app/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(cdtext-diff
    PRIVATE
        cdmanager_cdtext_core
        Qt6::Core
)

target_compile_definitions(cdtext-diff
    PRIVATE
        QT_NO_CAST_TO_ASCII
        QT_NO_CAST_FROM_ASCII
)

target_compile_options(cdtext-diff
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(APPLE)
    add_executable(cdtext-disc-recording-check
        app/include/cdmanager/infrastructure/burn/DiscRecordingBurner.h
        app/include/cdmanager/tools/cdtextdiff/CdTextDocumentParser.h
        app/include/cdmanager/tools/cdtextdiff/CdTextDiffTypes.h
        app/include/cdmanager/tools/cdtextdiff/CurrentProjectPackExporter.h
        app/src/infrastructure/burn/DiscRecordingBurner.mm
        app/src/tools/cdtextdiff/CdTextDocumentParser.cpp
        app/src/tools/cdtextdiff/CdTextDiffTypes.cpp
        app/src/tools/cdtextdiff/CurrentProjectPackExporter.cpp
        app/src/tools/cdtextdiff/discrecording_check_main.mm
    )

    target_include_directories(cdtext-disc-recording-check
        PRIVATE
            app/include
            ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_link_libraries(cdtext-disc-recording-check
        PRIVATE
            cdmanager_cdtext_core
            Qt6::Core
            $<$<BOOL:${CORE_FOUNDATION_FRAMEWORK}>:${CORE_FOUNDATION_FRAMEWORK}>
            $<$<BOOL:${FOUNDATION_FRAMEWORK}>:${FOUNDATION_FRAMEWORK}>
            $<$<BOOL:${DISC_RECORDING_FRAMEWORK}>:${DISC_RECORDING_FRAMEWORK}>
    )

    target_compile_definitions(cdtext-disc-recording-check
        PRIVATE
            QT_NO_CAST_TO_ASCII
            QT_NO_CAST_FROM_ASCII
    )

    target_compile_options(cdtext-disc-recording-check
        PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endif()

add_test(
    NAME cdtext-diff-reference-sample-exact
    COMMAND ${CMAKE_COMMAND}
        -DCDTEXT_DIFF_BIN=$<TARGET_FILE:cdtext-diff>
        -DREFERENCE_SAMPLE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/reference_samples/kureha_success_sana_collection_06
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cdtext_diff_reference_sample_exact.cmake
)

add_test(
    NAME cdtext-export-reference-sample-artifacts
    COMMAND ${CMAKE_COMMAND}
        -DCDTEXT_DIFF_BIN=$<TARGET_FILE:cdtext-diff>
        -DREFERENCE_SAMPLE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/reference_samples/kureha_success_sana_collection_06
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cdtext_export_reference_sample_artifacts.cmake
)

add_test(
    NAME cdtext-export-reference-sample-sony-bin
    COMMAND ${CMAKE_COMMAND}
        -DCDTEXT_DIFF_BIN=$<TARGET_FILE:cdtext-diff>
        -DREFERENCE_SAMPLE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/reference_samples/kureha_success_sana_collection_06
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cdtext_export_reference_sample_sony_bin.cmake
)

add_test(
    NAME cdtext-project-json-preserves-track1-title
    COMMAND ${CMAKE_COMMAND}
        -DCDTEXT_DIFF_BIN=$<TARGET_FILE:cdtext-diff>
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cdtext_project_json_preserves_track1_title.cmake
)

if(APPLE)
    add_test(
        NAME cdtext-export-reference-sample-roundtrip
        COMMAND ${CMAKE_COMMAND}
            -DCDTEXT_DIFF_BIN=$<TARGET_FILE:cdtext-diff>
            -DCDTEXT_DISC_RECORDING_CHECK_BIN=$<TARGET_FILE:cdtext-disc-recording-check>
            -DREFERENCE_SAMPLE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/reference_samples/kureha_success_sana_collection_06
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cdtext_export_reference_sample_roundtrip.cmake
    )

    add_test(
        NAME cdtext-disc-recording-reference-sample-accepted
        COMMAND $<TARGET_FILE:cdtext-disc-recording-check>
            --sample-dir ${CMAKE_CURRENT_SOURCE_DIR}/reference_samples/kureha_success_sana_collection_06
            --expect-block-count 1
    )
endif()
