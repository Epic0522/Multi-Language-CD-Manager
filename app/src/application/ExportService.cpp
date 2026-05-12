#include "cdmanager/application/ExportService.h"

#include <QDir>
#include <QFile>
#include <QThread>

#include "cdmanager/infrastructure/audio/AudioCdReader.h"

#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cdmanager::application {

using cdmanager::infrastructure::audio::CdTrackLocation;
static constexpr int kBytesPerSector = 2352;

ExportService::ExportService(QObject* parent)
    : QObject(parent) {
}

void ExportService::startExport(const cdmanager::domain::project::CdProject& project,
                                const QString& devicePath, const QString& outputDir,
                                const QHash<int, CdTrackLocation>& locations) {
    m_cancelled = false;

    const QString dir = outputDir.isEmpty() ? QDir::currentPath() : outputDir;
    const int totalTracks = project.tracks.size();

#ifdef __APPLE__
    const int fd = open(devicePath.toUtf8().constData(), O_RDONLY);
    if (fd < 0) {
        ExportProgress prog;
        prog.finished = true;
        prog.errorMessage = QStringLiteral("Cannot open device: %1").arg(devicePath);
        Q_EMIT progressChanged(prog);
        Q_EMIT finished();
        return;
    }
#endif

    for (int i = 0; i < project.tracks.size(); ++i) {
        if (m_cancelled) break;

        const auto& track = project.tracks[i];
        ExportProgress prog;
        prog.currentTrack = i + 1;
        prog.totalTracks = totalTracks;
        Q_EMIT progressChanged(prog);

        auto it = locations.find(track.number);
        if (it == locations.end() || !it->valid) {
            continue;
        }
        const auto& location = *it;

        // Read all audio sectors for this track.
        QByteArray pcmData;
#ifdef __APPLE__
        const size_t totalBytes = static_cast<size_t>(location.sectorCount) * kBytesPerSector;
        pcmData.resize(static_cast<int>(totalBytes));

        const off_t offset = static_cast<off_t>(location.startLsn) * kBytesPerSector;
        size_t bytesRead = 0;
        while (bytesRead < totalBytes && !m_cancelled) {
            const ssize_t n = pread(fd, pcmData.data() + bytesRead,
                                    totalBytes - bytesRead,
                                    offset + static_cast<off_t>(bytesRead));
            if (n <= 0) break;
            bytesRead += static_cast<size_t>(n);
        }
        pcmData.resize(static_cast<int>(bytesRead));
#endif

        if (pcmData.isEmpty()) continue;

        const QString wavPath = QStringLiteral("%1/track%2.wav")
            .arg(dir).arg(track.number, 2, 10, QLatin1Char('0'));
        writeWav(wavPath, pcmData);
    }

#ifdef __APPLE__
    close(fd);
#endif

    // Write CUE sheet.
    if (!m_cancelled) {
        writeCueSheet(QStringLiteral("%1/disc.cue").arg(dir), project);
    }

    ExportProgress prog;
    prog.currentTrack = totalTracks;
    prog.totalTracks = totalTracks;
    prog.finished = true;
    Q_EMIT progressChanged(prog);
    Q_EMIT finished();
}

void ExportService::cancel() {
    m_cancelled = true;
}

void ExportService::writeWav(const QString& path, const QByteArray& pcmData) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;

    const uint32_t dataSize = static_cast<uint32_t>(pcmData.size());
    const uint32_t sampleRate = 44100;
    const uint16_t numChannels = 2;
    const uint16_t bitsPerSample = 16;
    const uint16_t blockAlign = numChannels * bitsPerSample / 8;
    const uint32_t byteRate = sampleRate * blockAlign;

    // RIFF header
    file.write("RIFF", 4);
    const uint32_t chunkSize = 36 + dataSize;
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);

    // fmt subchunk
    file.write("fmt ", 4);
    const uint32_t fmtSize = 16;
    file.write(reinterpret_cast<const char*>(&fmtSize), 4);
    const uint16_t audioFormat = 1;  // PCM
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    // data subchunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);
    file.write(pcmData);
}

static QString escapeCueString(const QString& s) {
    QString escaped = s;
    escaped.replace(u'"', QStringLiteral("''"));
    return escaped;
}

void ExportService::writeCueSheet(const QString& path,
                                   const cdmanager::domain::project::CdProject& project) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);

    // CD-TEXT Performance (album-level)
    if (!project.albumTitle.isEmpty()) {
        out << "PERFORMER \"" << escapeCueString(project.albumArtist) << "\"\n";
        out << "TITLE \"" << escapeCueString(project.albumTitle) << "\"\n";
    }
    out << "FILE \"disc.wav\" WAVE\n";

    for (const auto& track : project.tracks) {
        // Calculate INDEX 01 from the track's duration.
        // For sequential tracks, INDEX is cumulative.
        out << "  TRACK " << QString::number(track.number).rightJustified(2, u'0') << " AUDIO\n";
        if (!track.title.isEmpty()) {
            out << "    TITLE \"" << escapeCueString(track.title) << "\"\n";
        }
        if (!track.artist.isEmpty()) {
            out << "    PERFORMER \"" << escapeCueString(track.artist) << "\"\n";
        }
        // Each track starts at the beginning of its own WAV file.
        out << "    FILE \"track" << QString::number(track.number).rightJustified(2, u'0')
            << ".wav\" WAVE\n";
        out << "    INDEX 01 00:00:00\n";
    }
}

}  // namespace cdmanager::application
