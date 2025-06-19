#ifndef STREAMPUSHTHREAD_H
#define STREAMPUSHTHREAD_H

#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QObject>
extern "C" {
#include <libavformat/avformat.h>
}

class StreamPushThread : public QThread
{
    Q_OBJECT
public:
    StreamPushThread(QObject* parent = nullptr);
    ~StreamPushThread();

    void addPacket(AVPacket* pkt, bool isVideo);
    void stopPushing();

    AVFormatContext *fmtCtx() const;
    void setFmtCtx(AVFormatContext *newFmtCtx);

signals:
    void errorOccurred(const QString& error);

protected:
    void run() override;

private:
    AVFormatContext* m_fmtCtx;          // RTSP 输出上下文
    QQueue<AVPacket*> m_videoQueue;     // 视频包队列
    QQueue<AVPacket*> m_audioQueue;     // 音频包队列
    QMutex m_mutex;                     // 队列访问保护
    volatile bool m_running;            // 运行状态标志
};

#endif // STREAMPUSHTHREAD_H
