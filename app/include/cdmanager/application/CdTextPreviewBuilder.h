#pragma once

#include <QString>
#include <QVector>

#include "cdmanager/application/CdTextTypes.h"

namespace cdmanager::application {

struct CdTextPreviewRow {
    QString fieldLabel;
    QString languageLabel;
    QString byteCountLabel;
    QString sourceLabel;
    QString stateLabel;
    QString hexPreview;
};

// 把准备结果转成更适合界面展示的调试信息。
// 后面接真实光驱后，这里会是排查“为什么这张盘车机能识别/不能识别”的第一现场。
class CdTextPreviewBuilder {
public:
    QVector<CdTextPreviewRow> build(const CdTextPreparationResult& result) const;

private:
    QString formatHexPreview(const QByteArray& bytes) const;
    QString languageLabel(cdmanager::domain::cdtext::CdTextLanguage language) const;
};

}  // namespace cdmanager::application
