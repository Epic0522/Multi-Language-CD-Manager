#include "cdmanager/application/PlaybackService.h"

#include <QDebug>
#include <QRandomGenerator>
#include <QThread>

#include <QTimer>

using cdmanager::infrastructure::audio::lsn_t;

namespace cdmanager::application {

static constexpr int kSectorsPerSecond = 75;
static constexpr int kPositionTimerMs = 250;

PlaybackService::PlaybackService(QObject* parent)
    : QObject(parent) {

    m_positionTimer = new QTimer(this);
    connect(m_positionTimer, &QTimer::timeout,
            this, &PlaybackService::onPositionTick);

    m_output = new cdmanager::infrastructure::audio::AudioOutput(this);
    m_output->setRingBuffer(&m_ringBuffer);
    connect(m_output, &cdmanager::infrastructure::audio::AudioOutput::playbackFinished,
            this, &PlaybackService::onOutputFinished);
    connect(m_output, &cdmanager::infrastructure::audio::AudioOutput::errorOccurred,
            this, &PlaybackService::onOutputError);
}

PlaybackService::~PlaybackService() {
    stop();
}

void PlaybackService::playTrack(int trackNumber) {
    stop();

    const auto location = cdmanager::infrastructure::audio::AudioCdReader::locateTrack(
        trackNumber
    );
    if (!location.valid) {
        Q_EMIT playbackError(QStringLiteral("无法定位音轨，或当前没有可用的音频输出/光驱设备。"));
        Q_EMIT playbackFinished();
        return;
    }

    m_currentTrack = trackNumber;
    m_totalSectors = location.sectorCount;
    m_startLsn = location.startLsn;
    m_elapsedSectors = 0;
    m_devicePath = location.devicePath;

    m_ringBuffer.clear();

    m_reader = new cdmanager::infrastructure::audio::AudioCdReader();
    m_reader->setRingBuffer(&m_ringBuffer);
    m_reader->setDevicePath(location.devicePath);
    m_reader->start(location.startLsn, location.sectorCount);

    m_readerThread = new QThread(this);
    m_reader->moveToThread(m_readerThread);
    QObject::connect(m_readerThread, &QThread::started,
                     m_reader, &cdmanager::infrastructure::audio::AudioCdReader::readLoop);
    QObject::connect(m_reader, &cdmanager::infrastructure::audio::AudioCdReader::finished,
                     this, &PlaybackService::onReaderFinished);
    QObject::connect(m_reader, &cdmanager::infrastructure::audio::AudioCdReader::error,
                     this, &PlaybackService::onReaderError);

    m_readerThread->start();
    QThread::msleep(100);
    m_output->start();

    m_elapsedTimer.start();
    m_positionTimer->start(kPositionTimerMs);
    setState(PlaybackState::Playing);
}

void PlaybackService::pause() {
    if (m_state != PlaybackState::Playing) {
        return;
    }

    m_elapsedSectors += static_cast<uint32_t>(
        static_cast<int64_t>(m_elapsedTimer.elapsed()) * kSectorsPerSecond / 1000
    );
    m_positionTimer->stop();

    if (m_reader != nullptr) {
        m_reader->setInterrupted();
    }
    m_output->pause();
    m_ringBuffer.clear();

    qDebug() << "pause: elapsedSectors =" << m_elapsedSectors << "totalSectors =" << m_totalSectors;

    setState(PlaybackState::Paused);
}

void PlaybackService::resume() {
    if (m_state != PlaybackState::Paused) {
        return;
    }

    const uint32_t remainingSectors = m_totalSectors > m_elapsedSectors
        ? (m_totalSectors - m_elapsedSectors) : 0;
    if (remainingSectors == 0) {
        stop();
        Q_EMIT playbackFinished();
        return;
    }

    const lsn_t resumeLsn = m_startLsn
        + static_cast<lsn_t>(m_elapsedSectors);
    qDebug() << "resume: startLsn =" << m_startLsn << "elapsedSectors =" << m_elapsedSectors << "resumeLsn =" << resumeLsn << "remaining =" << remainingSectors;

    // Clean up old reader/thread synchronously.
    // We don't use deleteLater here because it can race with the new reader.
    if (m_readerThread != nullptr) {
        m_readerThread->quit();
        m_readerThread->wait();
        delete m_reader;  // reader is owned by this thread now
        m_reader = nullptr;
        delete m_readerThread;
        m_readerThread = nullptr;
    }

    // Fully restart audio output rather than resume from suspend.
    m_output->stop();

    qDebug() << "resume: devicePath =" << m_devicePath;
    m_reader = new cdmanager::infrastructure::audio::AudioCdReader();
    m_reader->setRingBuffer(&m_ringBuffer);
    m_reader->setDevicePath(m_devicePath);
    m_reader->start(resumeLsn, remainingSectors);

    m_readerThread = new QThread(this);
    m_reader->moveToThread(m_readerThread);
    QObject::connect(m_readerThread, &QThread::started,
                     m_reader, &cdmanager::infrastructure::audio::AudioCdReader::readLoop);
    QObject::connect(m_reader, &cdmanager::infrastructure::audio::AudioCdReader::finished,
                     this, &PlaybackService::onReaderFinished);
    QObject::connect(m_reader, &cdmanager::infrastructure::audio::AudioCdReader::error,
                     this, &PlaybackService::onReaderError);

    m_readerThread->start();
    QThread::msleep(100);
    m_output->start();
    m_elapsedTimer.restart();
    m_positionTimer->start(kPositionTimerMs);
    setState(PlaybackState::Playing);
}

void PlaybackService::stop() {
    m_positionTimer->stop();

    if (m_reader != nullptr) {
        m_reader->setInterrupted();
    }

    if (m_readerThread != nullptr) {
        m_readerThread->quit();
        m_readerThread->wait();
        delete m_reader;
        m_reader = nullptr;
        delete m_readerThread;
        m_readerThread = nullptr;
    }

    m_output->stop();
    m_ringBuffer.clear();

    m_currentTrack = 0;
    m_totalSectors = 0;
    m_startLsn = 0;
    m_elapsedSectors = 0;
    m_devicePath.clear();
    setState(PlaybackState::Stopped);
}

PlaybackState PlaybackService::state() const {
    return m_state;
}

int PlaybackService::currentTrackNumber() const {
    return m_currentTrack;
}

void PlaybackService::seek(int secondsFromStart) {
    const uint32_t targetSectors = static_cast<uint32_t>(secondsFromStart) * kSectorsPerSecond;
    if (targetSectors >= m_totalSectors) {
        return;
    }

    const lsn_t seekLsn = m_startLsn + static_cast<lsn_t>(targetSectors);
    const uint32_t remainingSectors = m_totalSectors - targetSectors;
    const bool wasPlaying = (m_state == PlaybackState::Playing);

    // Tear down current reader.
    if (m_reader != nullptr) {
        m_reader->setInterrupted();
    }
    if (m_readerThread != nullptr) {
        m_readerThread->quit();
        m_readerThread->wait();
        delete m_reader;
        m_reader = nullptr;
        delete m_readerThread;
        m_readerThread = nullptr;
    }
    m_ringBuffer.clear();
    m_output->stop();
    m_elapsedSectors = targetSectors;

    if (wasPlaying) {
        // Restart from the new position.
        m_reader = new cdmanager::infrastructure::audio::AudioCdReader();
        m_reader->setRingBuffer(&m_ringBuffer);
        m_reader->setDevicePath(m_devicePath);
        m_reader->start(seekLsn, remainingSectors);

        m_readerThread = new QThread(this);
        m_reader->moveToThread(m_readerThread);
        QObject::connect(m_readerThread, &QThread::started,
                         m_reader, &cdmanager::infrastructure::audio::AudioCdReader::readLoop);
        QObject::connect(m_reader, &cdmanager::infrastructure::audio::AudioCdReader::finished,
                         this, &PlaybackService::onReaderFinished);
        QObject::connect(m_reader, &cdmanager::infrastructure::audio::AudioCdReader::error,
                         this, &PlaybackService::onReaderError);

        m_readerThread->start();
        QThread::msleep(100);
        m_output->start();
        m_elapsedTimer.restart();
    } else {
        // Paused — just update the position, don't restart playback.
        m_positionTimer->stop();
        m_elapsedTimer.restart();
        Q_EMIT positionChanged(
            static_cast<int>(targetSectors / kSectorsPerSecond),
            static_cast<int>(m_totalSectors / kSectorsPerSecond)
        );
    }
}

void PlaybackService::onReaderFinished() {
    // Reader has pushed all data to the ring buffer.
    // AudioOutput will finish when the buffer drains.
}

void PlaybackService::onReaderError(const QString& message) {
    stop();
    Q_EMIT playbackError(message);
    Q_EMIT playbackFinished();
}

void PlaybackService::onOutputError(const QString& message) {
    stop();
    Q_EMIT playbackError(message);
    Q_EMIT playbackFinished();
}

void PlaybackService::onOutputFinished() {
    stop();

    if (m_playbackMode == PlaybackMode::LoopOne) {
        playTrack(m_currentTrack);
        return;
    }

    if (m_playbackMode == PlaybackMode::Random) {
        if (!m_trackList.isEmpty()) {
            const int idx = QRandomGenerator::global()->bounded(m_trackList.size());
            playTrack(m_trackList[idx]);
        }
        return;
    }

    // Normal or LoopAll: try next track.
    const int idx = m_trackList.indexOf(m_currentTrack);
    if (idx >= 0 && idx + 1 < m_trackList.size()) {
        playTrack(m_trackList[idx + 1]);
    } else if (m_playbackMode == PlaybackMode::LoopAll && !m_trackList.isEmpty()) {
        playTrack(m_trackList.first());
    } else {
        Q_EMIT playbackFinished();
    }
}

void PlaybackService::nextTrack() {
    if (m_trackList.isEmpty()) return;
    const int idx = m_trackList.indexOf(m_currentTrack);
    const int nextIdx = (idx + 1) % m_trackList.size();
    playTrack(m_trackList[nextIdx]);
}

void PlaybackService::previousTrack() {
    if (m_trackList.isEmpty()) return;
    const int idx = m_trackList.indexOf(m_currentTrack);
    const int prevIdx = (idx <= 0) ? 0 : (idx - 1);
    playTrack(m_trackList[prevIdx]);
}

void PlaybackService::setPlaybackMode(PlaybackMode mode) {
    m_playbackMode = mode;
    Q_EMIT playbackModeChanged(mode);
}

void PlaybackService::cyclePlaybackMode() {
    switch (m_playbackMode) {
        case PlaybackMode::Normal:   setPlaybackMode(PlaybackMode::LoopAll);  break;
        case PlaybackMode::LoopAll:  setPlaybackMode(PlaybackMode::LoopOne);  break;
        case PlaybackMode::LoopOne:  setPlaybackMode(PlaybackMode::Random);   break;
        case PlaybackMode::Random:   setPlaybackMode(PlaybackMode::Normal);   break;
    }
}

PlaybackMode PlaybackService::playbackMode() const {
    return m_playbackMode;
}

QString PlaybackService::playbackModeLabel() const {
    switch (m_playbackMode) {
        case PlaybackMode::Normal:  return QStringLiteral("Normal");
        case PlaybackMode::LoopOne: return QStringLiteral("Loop 1");
        case PlaybackMode::LoopAll: return QStringLiteral("Loop All");
        case PlaybackMode::Random:  return QStringLiteral("Random");
    }
    return {};
}

void PlaybackService::setTrackList(const QVector<int>& trackNumbers) {
    m_trackList = trackNumbers;
}

void PlaybackService::onPositionTick() {
    if (m_state == PlaybackState::Playing) {
        const uint32_t currentSectors = m_elapsedSectors
            + static_cast<uint32_t>(
                static_cast<int64_t>(m_elapsedTimer.elapsed()) * kSectorsPerSecond / 1000
            );
        const int elapsedSec = static_cast<int>(currentSectors / kSectorsPerSecond);
        const int totalSec = static_cast<int>(m_totalSectors / kSectorsPerSecond);
        Q_EMIT positionChanged(elapsedSec, totalSec);
    }
}

void PlaybackService::setState(PlaybackState newState) {
    if (m_state != newState) {
        m_state = newState;
        Q_EMIT stateChanged(m_state);
    }
}

}  // namespace cdmanager::application
