#ifndef RTSPSYNCPUSH_H
#define RTSPSYNCPUSH_H


#include <QObject>


class AudioCaptureThread;
class VideoCaptureThread;
class AudioCodeThread;
class VideoCodeThread;
struct AVFormatContext;

class RTSPSyncPush : public QObject {
    Q_OBJECT
public:
    explicit RTSPSyncPush(QObject* parent = nullptr);
    ~RTSPSyncPush();

    bool start(const QString& rtspUrl, int width, int height, int fps, int bitrate);
    void stop();

signals:
    void stateChanged(int state);
    void errorOccurred(const QString& message);

private:
    AudioCaptureThread* m_audioCapture;
    VideoCaptureThread* m_videoCapture;
    AudioCodeThread* m_audioCodec;
    VideoCodeThread* m_videoCodec;
    AVFormatContext* m_formatCtx;

private:
    bool initializeRTSP(const QString& url);
    void cleanup();

};

#endif // RTSPSYNCPUSH_H
