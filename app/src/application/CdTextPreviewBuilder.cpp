#include "cdmanager/application/CdTextPreviewBuilder.h"

namespace cdmanager::application {

QVector<CdTextPreviewRow> CdTextPreviewBuilder::build(const CdTextPreparationResult& result) const {
    QVector<CdTextPreviewRow> rows;
    rows.reserve(result.preparedFields.size());

    for (const auto& preparedField : result.preparedFields) {
        rows.append(
            {
                preparedField.field.label,
                languageLabel(preparedField.field.language),
                QStringLiteral("%1 byte(s)").arg(preparedField.encodedBytes.size()),
                preparedField.sourceNote,
                preparedField.field.valueState == cdmanager::domain::cdtext::CdTextValueState::MissingOnDisc
                    ? QStringLiteral("缺失")
                    : preparedField.field.valueState == cdmanager::domain::cdtext::CdTextValueState::EmptyByEdit
                        ? QStringLiteral("留空")
                        : QStringLiteral("正常"),
                formatHexPreview(preparedField.encodedBytes),
            }
        );
    }

    return rows;
}

QString CdTextPreviewBuilder::formatHexPreview(const QByteArray& bytes) const {
    // 调试界面不需要整段全铺开，先看前 24 字节就够定位绝大多数编码问题。
    const QByteArray clipped = bytes.left(24);
    const QString hex = QString::fromLatin1(clipped.toHex(' ')).toUpper();
    if (bytes.size() > clipped.size()) {
        return QStringLiteral("%1 ...").arg(hex);
    }
    return hex;
}

QString CdTextPreviewBuilder::languageLabel(cdmanager::domain::cdtext::CdTextLanguage language) const {
    switch (language) {
        case cdmanager::domain::cdtext::CdTextLanguage::Latin:
            return QStringLiteral("Latin");
        case cdmanager::domain::cdtext::CdTextLanguage::Japanese:
            return QStringLiteral("Japanese");
    }
    return QStringLiteral("Unknown");
}

}  // namespace cdmanager::application
