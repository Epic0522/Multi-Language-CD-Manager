#pragma once

#include <QString>
#include <QVector>

#include "cdmanager/domain/cdtext/CdTextField.h"
#include "cdmanager/domain/project/Track.h"

namespace cdmanager::domain::project {

class CdProject {
public:
    QString albumTitle;
    QString albumArtist;
    bool albumTitlePresent {true};
    bool albumArtistPresent {true};
    cdmanager::domain::cdtext::CdTextLanguage cdTextLanguage {
        cdmanager::domain::cdtext::CdTextLanguage::Japanese
    };
    int trackGapSeconds {2};
    bool allowOverburn {false};
    QVector<Track> tracks;

    static CdProject sampleProject();

    QVector<cdmanager::domain::cdtext::CdTextField> cdTextFields() const;
};

}  // namespace cdmanager::domain::project
