#ifndef AUDIOCODETHREAD_H
#define AUDIOCODETHREAD_H


#include <QThread>
#include <QMutex>
#include <QWaitCondition>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libswresample/swresample.h>
}

class AudioCodeThread : public QThread {
    Q_OBJECT
public:
    explicit AudioCodeThread(QObject* parent = nullptr);
    ~AudioCodeThread();

    bool initialize(AVFormatContext* fmtCtx, int sampleRate, int channels);
    void addAudioData(const QByteArray& data);
    void stopEncoding();

    AVCodecContext *codecCtx() const;
    AVStream *stream() const;

signals:
    void packetEncoded(AVPacket* packet);
    void audioPtsUpdated(int64_t pts);

protected:
    void run() override;

private:
    AVCodecContext* m_codecCtx = nullptr;
    SwrContext* m_swrCtx = nullptr;
    AVStream* m_stream = nullptr;
    QByteArray m_audioBuffer;
    QMutex m_mutex;
    QWaitCondition m_cond;
    volatile bool m_running = false;
    int64_t m_pts = 0;
};

#endif // AUDIOCODETHREAD_H
