#ifndef RTSPSYNCPUSH_H
#define RTSPSYNCPUSH_H


#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QThread>
#include <QString>
#include "DataStruct.h"

class AudioCaptureThread;
class VideoCaptureThread;
class AudioCodeThread;
class VideoCodeThread;
class StreamPushThread;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;


class RTSPSyncPush : public QObject {
    Q_OBJECT
public:
    explicit RTSPSyncPush(QObject* parent = nullptr);
    ~RTSPSyncPush();
    bool initialize(const QString& videoSrc, int videoW, int videoH, int videoFps,
                    int videoBitrate,
                    int audioSampleRate, int audioChannels, const QString& rtspUrl);
    void setVideoParam(const QString& videoSrc, int videoW, int videoH, int videoFps,int videoBitrate);
    void setAudioParam(int audioSampleRate, int audioChannels,int audioSamepleSize);

    void start();
    void stop();

    PushState state() const;
    void setState(const PushState &newState);

    void setRtspUrl(const QString &newRtspUrl);

signals:
    void stateChanged(const QString &objName,const PushState &newState);
    void error(const QString& msg);
    void info(const QString& msg);

private slots:
    void onVideoFrameAvailable(AVFrame* frame);
    void onAudioDataAvailable(const QByteArray& data);

private:
    // 推流上下文
    AVFormatContext* m_fmtCtx = nullptr;
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
    bool m_running = false;
    QMutex m_mutex;

    // 线程成员
    AudioCaptureThread* m_audioCapThread = nullptr;
    AudioCodeThread* m_audioCodeThread = nullptr;
    VideoCaptureThread* m_videoCapThread = nullptr;
    VideoCodeThread* m_videoCodeThread = nullptr;
    StreamPushThread *m_streamPushThread  = nullptr;

    // 参数配置
    QString m_videoSrc;
    int m_videoW = 0, m_videoH = 0, m_videoFps = 0, m_videoBitrate = 0;
    int m_audioSampleRate = 0, m_audioChannels = 0, m_audioSampleSize = 0;
    QString m_rtspUrl;

    // 简单音视频同步相关
    int64_t m_lastAudioPts = 0;
    int64_t m_lastVideoPts = 0;

    PushState m_state;

};

#endif // RTSPSYNCPUSH_H
