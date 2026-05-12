#include "cdmanager/application/CdTextPreparationService.h"

#include "cdmanager/infrastructure/encoding/JapaneseFullwidthNormalizer.h"
#include "cdmanager/infrastructure/encoding/MsJisCodec.h"

namespace cdmanager::application {

namespace {

EncodedText encodeLatin1Strict(const QString& text) {
    EncodedText result;
    const QByteArray latin1 = text.toLatin1();
    if (QString::fromLatin1(latin1) != text) {
        result.errorMessage = QStringLiteral("Contains characters that cannot be represented in ISO-8859-1.");
        return result;
    }

    result.ok = true;
    result.bytes = latin1;
    return result;
}

ValidationIssue makeIssue(const cdmanager::domain::cdtext::CdTextField& field, const QString& message) {
    return {field.label, message};
}

QString sourceNoteForField(const cdmanager::domain::cdtext::CdTextField& field) {
    using cdmanager::domain::cdtext::CdTextValueState;
    switch (field.valueState) {
        case CdTextValueState::Present:
            return QStringLiteral("有值");
        case CdTextValueState::MissingOnDisc:
            return QStringLiteral("盘上缺失");
        case CdTextValueState::EmptyByEdit:
            return QStringLiteral("用户留空");
    }
    return QStringLiteral("未知");
}

}  // namespace

CdTextPreparationResult CdTextPreparationService::prepare(
    const cdmanager::domain::project::CdProject& project
) const {
    CdTextPreparationResult result;
    const cdmanager::infrastructure::encoding::MsJisCodec msJisCodec;
    const cdmanager::infrastructure::encoding::JapaneseFullwidthNormalizer fullwidthNormalizer;

    for (const auto& field : project.cdTextFields()) {
        PreparedCdTextField preparedField;
        preparedField.field = field;
        preparedField.effectiveValue = field.value;
        preparedField.sourceNote = sourceNoteForField(field);

        if (field.valueState != cdmanager::domain::cdtext::CdTextValueState::Present) {
            result.preparedFields.append(preparedField);
            continue;
        }

        if (field.preservedBytes.has_value()) {
            preparedField.encodedBytes = field.preservedBytes->bytes;
            preparedField.reusedPreservedBytes = true;
            preparedField.sourceNote = QStringLiteral("导入字节复用");
        } else {
            QString effectiveValue = field.value;
            EncodedText encoded;
            switch (field.language) {
                case cdmanager::domain::cdtext::CdTextLanguage::Latin:
                    encoded = encodeLatin1Strict(effectiveValue);
                    break;
                case cdmanager::domain::cdtext::CdTextLanguage::Japanese:
                {
                    const auto normalized = fullwidthNormalizer.normalize(field.value);
                    effectiveValue = normalized.normalizedText;
                    preparedField.effectiveValue = effectiveValue;
                    preparedField.normalizedToFullwidth = normalized.changed;
                    encoded = msJisCodec.encode(effectiveValue);
                    break;
                }
            }

            if (!encoded.ok) {
                result.validation.ok = false;
                result.validation.issues.append(makeIssue(field, encoded.errorMessage));
                continue;
            }

            preparedField.encodedBytes = encoded.bytes;
            preparedField.sourceNote = preparedField.normalizedToFullwidth
                ? QStringLiteral("本次重新编码（已转全角）")
                : QStringLiteral("本次重新编码");
        }

        if (preparedField.encodedBytes.size() > field.maxEncodedBytes) {
            result.validation.ok = false;
            result.validation.issues.append(
                makeIssue(
                    field,
                    QStringLiteral("Encoded length %1 exceeds current limit of %2 bytes.")
                        .arg(preparedField.encodedBytes.size())
                        .arg(field.maxEncodedBytes)
                )
            );
            continue;
        }

        result.preparedFields.append(preparedField);
    }

    return result;
}

}  // namespace cdmanager::application
