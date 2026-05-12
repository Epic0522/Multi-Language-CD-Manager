#include "cdmanager/domain/project/CdProject.h"

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

CdProject CdProject::sampleProject() {
    CdProject project;
    project.albumTitle = QStringLiteral("幻想メモリアル");
    project.albumArtist = QStringLiteral("妄想天使");
    project.albumTitlePresent = true;
    project.albumArtistPresent = true;
    project.tracks = {
        {
            1,
            QStringLiteral("/music/01-intro.wav"),
            QStringLiteral("星屑スタートライン"),
            QStringLiteral("妄想天使"),
            true,
            true,
            258,
        },
        {
            2,
            QStringLiteral("/music/02-ballad.wav"),
            QStringLiteral("夜更けのシグナル"),
            QStringLiteral("妄想天使"),
            true,
            true,
            301,
        },
        {
            3,
            QStringLiteral("/music/03-live.wav"),
            QStringLiteral("未来へつづくうた"),
            QStringLiteral("妄想天使"),
            true,
            true,
            274,
        },
    };
    return project;
}

QVector<cdmanager::domain::cdtext::CdTextField> CdProject::cdTextFields() const {
    QVector<cdmanager::domain::cdtext::CdTextField> fields {
        {
            cdmanager::domain::cdtext::CdTextFieldId::AlbumTitle,
            QStringLiteral("Album Title"),
            albumTitle,
            cdTextLanguage,
            80,
            std::nullopt,
            valueStateForField(albumTitlePresent, albumTitle),
            std::nullopt,
        },
        {
            cdmanager::domain::cdtext::CdTextFieldId::AlbumArtist,
            QStringLiteral("Album Artist"),
            albumArtist,
            cdTextLanguage,
            80,
            std::nullopt,
            valueStateForField(albumArtistPresent, albumArtist),
            std::nullopt,
        },
    };

    for (const Track& track : tracks) {
        fields += track.cdTextFields(cdTextLanguage);
    }

    return fields;
}

}  // namespace cdmanager::domain::project
