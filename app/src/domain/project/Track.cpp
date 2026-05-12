#include "cdmanager/domain/project/Track.h"

namespace cdmanager::domain::project {

namespace {

cdmanager::domain::cdtext::CdTextValueState valueStateForField(bool presentOnDisc, const QString& value) {
    if (!presentOnDisc) {
        return cdmanager::domain::cdtext::CdTextValueState::MissingOnDisc;
    }

    if (value.trimmed().isEmpty()) {
        return cdmanager::domain::cdtext::CdTextValueState::EmptyByEdit;
    }

    return cdmanager::domain::cdtext::CdTextValueState::Present;
}

}  // namespace

QVector<cdmanager::domain::cdtext::CdTextField> Track::cdTextFields(
    cdmanager::domain::cdtext::CdTextLanguage language
) const {
    return {
        {
            cdmanager::domain::cdtext::CdTextFieldId::TrackTitle,
            QStringLiteral("Track %1 Title").arg(number),
            title,
            language,
            64,
            number,
            valueStateForField(titlePresent, title),
            std::nullopt
        },
        {
            cdmanager::domain::cdtext::CdTextFieldId::TrackArtist,
            QStringLiteral("Track %1 Artist").arg(number),
            artist,
            language,
            64,
            number,
            valueStateForField(artistPresent, artist),
            std::nullopt
        },
    };
}

}  // namespace cdmanager::domain::project
