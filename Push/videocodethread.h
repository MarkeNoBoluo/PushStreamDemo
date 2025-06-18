#ifndef VIDEOCODETHREAD_H
#define VIDEOCODETHREAD_H


#include <QThread>
#include <QMutex>
#include <QQueue>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
}

class VideoCodeThread : public QThread {
    Q_OBJECT
public:
    explicit VideoCodeThread(QObject* parent = nullptr);
    ~VideoCodeThread();

    bool initialize(AVFormatContext* fmtCtx, int width, int height, int fps, int bitrate);
    void addVideoFrame(AVFrame* frame);
    void stopEncoding();

signals:
    void packetEncoded(AVPacket* packet);

protected:
    void run() override;

private:
    AVCodecContext* m_codecCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVStream* m_stream = nullptr;
    QQueue<AVFrame*> m_frameQueue;
    QMutex m_mutex;
    volatile bool m_running = false;
};


#endif // VIDEOCODETHREAD_H
