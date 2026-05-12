#pragma once

#include <QObject>
#include <QAudio>
#include <QTimer>

class QAudioSink;

namespace cdmanager::infrastructure::audio {

class AudioRingBuffer;

// Feeds PCM data from an AudioRingBuffer to a QAudioSink.
// Uses a QTimer (~50 Hz) to pull data from the ring buffer and push
// it to the system audio device.
//
// Owns the QAudioSink; stop() / pause() / resume() delegate to it.
// Emits playbackFinished() when the ring buffer signals end-of-stream
// and all remaining data has been consumed.
class AudioOutput : public QObject {
    Q_OBJECT

public:
    // CD audio: 44100 Hz, stereo, 16-bit signed integer.
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannelCount = 2;
    static constexpr int kTimerIntervalMs = 20;

    explicit AudioOutput(QObject* parent = nullptr);
    ~AudioOutput() override;

    void setRingBuffer(AudioRingBuffer* buffer);

    void start();
    void stop();
    void pause();
    void resume();

    QAudio::State state() const;

signals:
    void playbackFinished();

private slots:
    void feedAudio();

private:
    AudioRingBuffer* m_ringBuffer = nullptr;
    QAudioSink* m_sink = nullptr;
    QTimer* m_feedTimer = nullptr;
    QIODevice* m_device = nullptr;

    Q_DISABLE_COPY(AudioOutput)
};

}  // namespace cdmanager::infrastructure::audio
