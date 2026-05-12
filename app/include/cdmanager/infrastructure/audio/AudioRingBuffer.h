#pragma once

#include <QByteArray>
#include <QMutex>

namespace cdmanager::infrastructure::audio {

// Single-producer, single-consumer ring buffer for PCM audio data.
// The worker thread writes CD audio sectors; the main thread reads
// and feeds QAudioSink.  All public methods are thread-safe.
class AudioRingBuffer {
public:
    // Pre-allocate enough for ~3.4 seconds of CD audio at 75 sectors/s,
    // 2352 bytes per sector.
    static constexpr int kDefaultCapacity = 256 * 1024;

    explicit AudioRingBuffer(int capacity = kDefaultCapacity);

    // Producer: append raw PCM bytes.  Blocks if insufficient space
    // (but the caller should check available capacity via writeSpace()).
    void write(const QByteArray& data);

    // Consumer: remove up to maxSize bytes.
    QByteArray read(int maxSize);

    // Bytes available for reading.
    int available() const;

    // Bytes free for writing.
    int writeSpace() const;

    // Discard all buffered data.
    void clear();

    // Producer signals end-of-stream.
    void setFinished();

    // Consumer checks whether producer has finished.
    bool isFinished() const;

    friend class AudioOutput;

private:
    mutable QMutex m_mutex;
    QByteArray m_buffer;
    int m_readPos = 0;
    int m_writePos = 0;
    int m_available = 0;
    bool m_finished = false;

    // Signal that the write side completed (all data pushed + setFinished()).
    // Used by AudioOutput to detect end-of-track without polling.
    Q_DISABLE_COPY(AudioRingBuffer)
};

}  // namespace cdmanager::infrastructure::audio
