#include "cdmanager/application/ProjectOverviewBuilder.h"

namespace cdmanager::application {

ProjectOverview ProjectOverviewBuilder::build(
    const cdmanager::domain::project::CdProject& project
) const {
    ProjectOverview overview;
    overview.albumTitle = project.albumTitle;
    overview.albumArtist = project.albumArtist;
    overview.trackCountText = QStringLiteral("Tracks: %1").arg(project.tracks.size());

    for (const auto& track : project.tracks) {
        overview.trackRows.append(
            {
                track.number,
                track.title,
                track.artist,
                formatDuration(track.durationSeconds),
            }
        );
    }

    return overview;
}

QString ProjectOverviewBuilder::formatDuration(int seconds) const {
    const int minutes = seconds / 60;
    const int remainder = seconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(remainder, 2, 10, QLatin1Char('0'));
}

}  // namespace cdmanager::application
