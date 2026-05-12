#pragma once

#include <QString>
#include <QVector>

#include "cdmanager/domain/cdtext/CdTextField.h"

namespace cdmanager::domain::project {

class Track {
public:
    int number {0};
    QString filePath;
    QString title;
    QString artist;
    bool titlePresent {true};
    bool artistPresent {true};
    int durationSeconds {0};

    QVector<cdmanager::domain::cdtext::CdTextField> cdTextFields(
        cdmanager::domain::cdtext::CdTextLanguage language
            = cdmanager::domain::cdtext::CdTextLanguage::Japanese
    ) const;
};

}  // namespace cdmanager::domain::project
