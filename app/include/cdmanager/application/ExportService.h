#pragma once

#include <QObject>
#include <QString>

#include "cdmanager/domain/project/CdProject.h"
#include "cdmanager/infrastructure/audio/AudioCdReader.h"
#include <QHash>

namespace cdmanager::application {

struct ExportProgress {
    int currentTrack = 0;
    int totalTracks = 0;
    bool finished = false;
    QString errorMessage;
};

// Exports CD audio tracks to WAV files and generates a CUE sheet
// with CD-TEXT metadata.
//
// The caller must provide a directory path; WAV files are named
// "trackNN.wav" and the cue sheet is "disc.cue".
//
// Raw audio is read directly from the CD device (same path used
// for playback), so this must run on a background thread.
class ExportService : public QObject {
    Q_OBJECT

public:
    explicit ExportService(QObject* parent = nullptr);

    void startExport(const cdmanager::domain::project::CdProject& project,
                     const QString& devicePath, const QString& outputDir,
                     const QHash<int, cdmanager::infrastructure::audio::CdTrackLocation>& locations);
    void cancel();

signals:
    void progressChanged(const ExportProgress& progress);
    void finished();

private:
    void writeWav(const QString& path, const QByteArray& pcmData);
    void writeCueSheet(const QString& path,
                       const cdmanager::domain::project::CdProject& project);
    bool m_cancelled = false;
};

}  // namespace cdmanager::application
