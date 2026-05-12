#pragma once

#include <QString>
#include <QVector>

namespace cdmanager::application {

struct TrackOverviewRow {
    int number {0};
    QString title;
    QString artist;
    QString duration;
};

struct ProjectOverview {
    QString albumTitle;
    QString albumArtist;
    QString trackCountText;
    QVector<TrackOverviewRow> trackRows;
};

}  // namespace cdmanager::application
