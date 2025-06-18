#include "audiocapturethread.h"
#include "Logger.h"

AudioCaptureThread::AudioCaptureThread(QObject *parent)
    : QThread{parent}
{

}

AudioCaptureThread::~AudioCaptureThread() {
    stopCapture();
}

bool AudioCaptureThread::initialize(int sampleRate, int channels) {
    m_audioFormat.setSampleRate(sampleRate);
    m_audioFormat.setChannelCount(channels);
    m_audioFormat.setSampleSize(16);
    m_audioFormat.setCodec("audio/pcm");
    m_audioFormat.setByteOrder(QAudioFormat::LittleEndian);
    m_audioFormat.setSampleType(QAudioFormat::SignedInt);

    QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();
    if (!info.isFormatSupported(m_audioFormat)) {
        m_audioFormat = info.nearestFormat(m_audioFormat);
        LogInfo << "使用最接近的音频格式:" << m_audioFormat.sampleRate() << "Hz,"
                << m_audioFormat.channelCount() << "channels";
    }
    return true;
}

void AudioCaptureThread::run() {
    m_audioInput = new QAudioInput(m_audioFormat);
    AudioInputDevice* device = new AudioInputDevice(this);
    device->open(QIODevice::WriteOnly);
    m_audioInput->start(device);
    m_running = true;

    exec(); // 进入事件循环

    m_audioInput->stop();
    delete m_audioInput;
    delete device;
    m_running = false;
}

void AudioCaptureThread::stopCapture() {
    if (m_running) {
        quit();
        wait();
    }
}

qint64 AudioCaptureThread::AudioInputDevice::writeData(const char* data, qint64 len) {
    if (m_owner && len > 0) {
        emit m_owner->audioDataAvailable(QByteArray(data, len));
    }
    return len;
}
