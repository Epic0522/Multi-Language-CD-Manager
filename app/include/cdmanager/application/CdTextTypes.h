#pragma once

#include <QByteArray>
#include <QVector>

#include "cdmanager/application/ValidationTypes.h"
#include "cdmanager/domain/cdtext/CdTextField.h"

namespace cdmanager::application {

struct PreparedCdTextField {
    cdmanager::domain::cdtext::CdTextField field;
    QByteArray encodedBytes;
    bool reusedPreservedBytes {false};
    bool normalizedToFullwidth {false};
    QString effectiveValue;
    QString sourceNote;
};

struct CdTextPreparationResult {
    ValidationReport validation;
    QVector<PreparedCdTextField> preparedFields;

    bool ok() const {
        return validation.ok;
    }
};

}  // namespace cdmanager::application
