#pragma once

#include <QString>

#include "cdmanager/application/CdTextWritePayloadBuilder.h"
#include "cdmanager/domain/project/CdProject.h"

namespace cdmanager::infrastructure::burn {

enum class TocWriterTarget {
    Drutil,
    Cdrdao,
};

// Generates a cdrdao-compatible .toc file with CD-TEXT support.
// 这里虽然都叫 TOC，但 drutil 和 cdrdao 实际能接受的细节并不完全一样。
// 为了把“日文 CD-TEXT 真写盘”这件事做稳，后端差异要在这里显式分流。
class TocFileWriter {
public:
    // Build the textual TOC document before encoding/writing.
    // 方便在控制台里显示最后一次刻录时真正生成了什么内容。
    static QString buildTocText(const cdmanager::domain::project::CdProject& project,
                                const cdmanager::application::CdTextWritePayload& writePayload,
                                const QStringList& wavFilePaths,
                                const QString& language = QStringLiteral("EN"),
                                TocWriterTarget target = TocWriterTarget::Drutil);

    // Write a .toc file referencing absolute WAV paths.
    // language: "EN" for English/Latin-1, "JP" for Japanese (MS-JIS).
    static bool write(const QString& filePath,
                      const cdmanager::domain::project::CdProject& project,
                      const cdmanager::application::CdTextWritePayload& writePayload,
                      const QStringList& wavFilePaths,
                      const QString& language = QStringLiteral("EN"),
                      TocWriterTarget target = TocWriterTarget::Drutil);
};

}  // namespace cdmanager::infrastructure::burn
