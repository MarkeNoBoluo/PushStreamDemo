#include "audiocodethread.h"

AudioCodeThread::AudioCodeThread(QObject* parent)
    : QThread(parent)
{

}

bool AudioCodeThread::initialize(AVFormatContext* fmtCtx, int sampleRate, int channels) {
    // ...初始化音频编码器...
    return true;
}

void AudioCodeThread::addAudioData(const QByteArray& data) {
    QMutexLocker locker(&m_mutex);
    m_audioBuffer.append(data);
    m_cond.wakeAll();
}

void AudioCodeThread::run() {
    while (m_running) {
        QMutexLocker locker(&m_mutex);
        if (m_audioBuffer.isEmpty()) {
            m_cond.wait(&m_mutex);
            continue;
        }

        // 处理音频数据并编码...
        emit audioPtsUpdated(m_pts);
    }
}

void AudioCodeThread::stopEncoding() {
    m_running = false;
    m_cond.wakeAll();
    wait();
}
