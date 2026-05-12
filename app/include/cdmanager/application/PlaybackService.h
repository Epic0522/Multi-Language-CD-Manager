#pragma once

#include <QObject>
#include <QThread>
#include <QElapsedTimer>

#include "cdmanager/domain/project/Track.h"
#include "cdmanager/infrastructure/audio/AudioRingBuffer.h"
#include "cdmanager/infrastructure/audio/AudioCdReader.h"
#include "cdmanager/infrastructure/audio/AudioOutput.h"

namespace cdmanager::application {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

enum class PlaybackMode {
    Normal,
    LoopOne,
    LoopAll,
    Random
};

// Orchestrates CD audio playback: opens the device, queries track
// position via libcdio, starts a worker thread for sector reading,
// and feeds PCM data to AudioOutput.
//
// Owns the reader thread, ring buffer, and audio output.
// Call playTrack() to begin; pause()/resume()/stop() to control.
class PlaybackService : public QObject {
    Q_OBJECT

public:
    explicit PlaybackService(QObject* parent = nullptr);
    ~PlaybackService() override;

    void playTrack(int trackNumber);
    void pause();
    void resume();
    void stop();
    void seek(int secondsFromStart);
    void nextTrack();
    void previousTrack();
    void setPlaybackMode(PlaybackMode mode);
    void cyclePlaybackMode();
    void setTrackList(const QVector<int>& trackNumbers);

    PlaybackMode playbackMode() const;
    QString playbackModeLabel() const;

    PlaybackState state() const;
    int currentTrackNumber() const;

signals:
    void stateChanged(cdmanager::application::PlaybackState state);
    void positionChanged(int elapsedSeconds, int totalSeconds);
    void playbackFinished();
    void playbackError(const QString& message);
    void playbackModeChanged(cdmanager::application::PlaybackMode mode);

private slots:
    void onReaderFinished();
    void onReaderError(const QString& message);
    void onOutputFinished();
    void onPositionTick();

private:
    void setState(PlaybackState newState);

    cdmanager::infrastructure::audio::AudioRingBuffer m_ringBuffer;
    cdmanager::infrastructure::audio::AudioCdReader* m_reader = nullptr;
    QThread* m_readerThread = nullptr;
    cdmanager::infrastructure::audio::AudioOutput* m_output = nullptr;

    PlaybackState m_state = PlaybackState::Stopped;
    PlaybackMode m_playbackMode = PlaybackMode::Normal;
    int m_currentTrack = 0;
    QVector<int> m_trackList;
    uint32_t m_totalSectors = 0;
    uint32_t m_startLsn = 0;
    uint32_t m_elapsedSectors = 0;
    QString m_devicePath;
    QElapsedTimer m_elapsedTimer;
    QTimer* m_positionTimer = nullptr;

    Q_DISABLE_COPY(PlaybackService)
};

}  // namespace cdmanager::application
