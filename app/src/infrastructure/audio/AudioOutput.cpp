#include "cdmanager/infrastructure/audio/AudioOutput.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

#include "cdmanager/infrastructure/audio/AudioRingBuffer.h"

namespace cdmanager::infrastructure::audio {

AudioOutput::AudioOutput(QObject* parent)
    : QObject(parent) {
}

AudioOutput::~AudioOutput() {
    stop();
}

void AudioOutput::setRingBuffer(AudioRingBuffer* buffer) {
    m_ringBuffer = buffer;
}

void AudioOutput::start() {
    if (m_sink != nullptr) {
        stop();
    }

    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(kChannelCount);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (defaultDevice.isNull()) {
        Q_EMIT errorOccurred(QStringLiteral("No default audio output device is available."));
        return;
    }
    if (!defaultDevice.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Float);
        if (!defaultDevice.isFormatSupported(format)) {
            qWarning("AudioOutput: neither Int16 nor Float is supported.");
            Q_EMIT errorOccurred(
                QStringLiteral("Default audio output does not support CD audio playback format.")
            );
            return;
        }
    }

    m_sink = new QAudioSink(defaultDevice, format, this);
    m_sink->setBufferSize(kSampleRate * kChannelCount * 2 / 2);  // ~0.5 seconds
    connect(
        m_sink,
        &QAudioSink::stateChanged,
        this,
        [this](QAudio::State state) {
            if (m_sink == nullptr || state != QAudio::StoppedState) {
                return;
            }
            const auto sinkError = m_sink->error();
            if (sinkError == QtAudio::NoError) {
                return;
            }
            QString message = QStringLiteral("Audio output stopped unexpectedly.");
            switch (sinkError) {
                case QtAudio::OpenError:
                    message = QStringLiteral("Failed to open the selected audio output device.");
                    break;
                case QtAudio::IOError:
                    message = QStringLiteral("Audio output device I/O failed.");
                    break;
                case QtAudio::FatalError:
                    message = QStringLiteral("Audio output device reported a fatal error.");
                    break;
                case QtAudio::NoError:
                    break;
            }
            Q_EMIT errorOccurred(message);
        }
    );

    m_feedTimer = new QTimer(this);
    connect(m_feedTimer, &QTimer::timeout, this, &AudioOutput::feedAudio);
    m_feedTimer->start(kTimerIntervalMs);

    m_device = m_sink->start();
    if (m_device == nullptr) {
        Q_EMIT errorOccurred(QStringLiteral("Could not start audio playback on the default output device."));
        stop();
    }
}

void AudioOutput::stop() {
    if (m_feedTimer != nullptr) {
        m_feedTimer->stop();
    }

    if (m_sink != nullptr) {
        m_sink->reset();
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }

    delete m_feedTimer;
    m_feedTimer = nullptr;

    m_device = nullptr;
}

void AudioOutput::pause() {
    if (m_sink != nullptr) {
        m_sink->suspend();
    }
    if (m_feedTimer != nullptr) {
        m_feedTimer->stop();
    }
}

void AudioOutput::resume() {
    if (m_sink != nullptr && m_sink->state() == QAudio::SuspendedState) {
        m_sink->resume();
    }
    if (m_feedTimer != nullptr) {
        m_feedTimer->start(kTimerIntervalMs);
    }
}

QAudio::State AudioOutput::state() const {
    if (m_sink == nullptr) {
        return QAudio::StoppedState;
    }
    return m_sink->state();
}

void AudioOutput::feedAudio() {
    if (m_device == nullptr || m_ringBuffer == nullptr || m_sink == nullptr) {
        return;
    }

    // Write only what the hardware buffer has room for.
    const int bytesFree = m_sink->bytesFree();
    const int available = m_ringBuffer->available();

    if (bytesFree > 0 && available > 0) {
        const int bytesToWrite = qMin(bytesFree, available);
        const QByteArray chunk = m_ringBuffer->read(bytesToWrite);
        if (!chunk.isEmpty()) {
            m_device->write(chunk);
        }
    }

    if (m_ringBuffer->isFinished() && m_ringBuffer->available() == 0
        && m_sink->bytesFree() == m_sink->bufferSize()) {
        // All data has been consumed and played out.
        m_feedTimer->stop();
        m_sink->stop();
        Q_EMIT playbackFinished();
    }
}

}  // namespace cdmanager::infrastructure::audio
