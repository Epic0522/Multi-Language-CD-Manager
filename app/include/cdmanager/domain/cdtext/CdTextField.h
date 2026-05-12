#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace cdmanager::domain::cdtext {

enum class CdTextFieldId {
    AlbumTitle,
    AlbumArtist,
    TrackTitle,
    TrackArtist
};

enum class CdTextLanguage {
    Latin,
    Japanese
};

enum class CdTextValueState {
    Present,
    MissingOnDisc,
    EmptyByEdit
};

struct OriginalEncodedBytes {
    QByteArray bytes;
    QString sourceDescription;
};

struct CdTextField {
    CdTextFieldId id {};
    QString label;
    QString value;
    CdTextLanguage language {CdTextLanguage::Japanese};
    int maxEncodedBytes {80};
    std::optional<int> trackNumber;
    CdTextValueState valueState {CdTextValueState::Present};
    std::optional<OriginalEncodedBytes> preservedBytes;
};

}  // namespace cdmanager::domain::cdtext
