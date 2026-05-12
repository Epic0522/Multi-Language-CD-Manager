#include "cdmanager/infrastructure/disc/LegacySampleDiscGateway.h"

#include <QByteArray>

#include "cdmanager/domain/cdtext/CdTextField.h"

namespace cdmanager::infrastructure::disc {

namespace {

cdmanager::domain::project::Track makeTrack(
    int number,
    const QString& filePath,
    const QString& title,
    const QString& artist,
    int durationSeconds
) {
    cdmanager::domain::project::Track track;
    track.number = number;
    track.filePath = filePath;
    track.title = title;
    track.artist = artist;
    track.durationSeconds = durationSeconds;
    return track;
}

}  // namespace

GatewayMode LegacySampleDiscGateway::mode() const {
    return GatewayMode::Sample;
}

QVector<cdmanager::domain::disc::DriveInfo> LegacySampleDiscGateway::listDrives() const {
    return {
        {
            QStringLiteral("sample://legacy-drive"),
            QStringLiteral("Legacy Sample Drive"),
            true,
            true,
            true,
        },
    };
}

cdmanager::domain::disc::DiscSnapshot LegacySampleDiscGateway::readDisc(const QString& deviceId) const {
    Q_UNUSED(deviceId);
    return readSampleDisc();
}

cdmanager::domain::disc::DiscSnapshot LegacySampleDiscGateway::readSampleDisc() const {
    cdmanager::domain::disc::DiscSnapshot snapshot;
    snapshot.sourceName = QStringLiteral("Legacy sample disc");
    snapshot.albumTitle = QStringLiteral("幻想メモリアル");
    snapshot.albumArtist = QStringLiteral("妄想天使");
    snapshot.containsCdText = true;
    snapshot.containsJapaneseCdText = true;
    snapshot.tracks = {
        makeTrack(
            1,
            QStringLiteral("/music/01-intro.wav"),
            QStringLiteral("星屑スタートライン"),
            QStringLiteral("妄想天使"),
            258
        ),
        makeTrack(
            2,
            QStringLiteral("/music/02-ballad.wav"),
            QStringLiteral("夜更けのシグナル"),
            QStringLiteral("妄想天使"),
            301
        ),
        makeTrack(
            3,
            QStringLiteral("/music/03-live.wav"),
            QStringLiteral("未来へつづくうた"),
            QStringLiteral("妄想天使"),
            274
        ),
    };
    return snapshot;
}

}  // namespace cdmanager::infrastructure::disc
