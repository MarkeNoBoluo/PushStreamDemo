// RTSPPusher.h
#ifndef RTSPPUSHER_H
#define RTSPPUSHER_H

#include <QObject>
#include <QString>
#include "DataStruct.h"
class CodeThread;
class AudioProcessor;
extern "C"{
#include <libswresample/swresample.h>
}

class RTSPPusher : public QObject {
    Q_OBJECT
public:
    explicit RTSPPusher(QObject* parent = nullptr);
    ~RTSPPusher();

    // 配置方法
    void setSource(const QString& url);
    void setDestination(const QString& url);
    void setVideoSize(int width, int height);
    void setFrameRate(int fps);
    void setBitRate(int kbps);

    // 操作方法
    bool start();  // 每次调用 start() 都会创建新的 CodeThread
    void stop();

    // 状态查询
    PushState state() const { return mState; }
    QString lastError() const { return mLastError; }

    // 目的Url
    QString destinationUrl() const;

signals:
    void stateChanged(const QString &objName, PushState newState);
    void error(const QString& errorMessage);
    void statistics(qint64 frameCount, qint64 bitrate);

private:
    void setState(PushState newState);
    void updateStatistics();
    void cleanupThread();  // 清理旧的线程
    void initNewThread();  // 初始化新的线程

private slots:
    void handleThreadState(PushState state);
    void handleThreadError(const QString& error);
    void handleFrameProcessed(qint64 frameCount);

private:
    CodeThread* mPusherThread = nullptr;  // 每次推流时重新创建
    // 添加音频处理器
    AudioProcessor* m_audioProcessor = nullptr;
    // 音频参数
    int m_audioSampleRate = 44100;
    int m_audioChannels = 2;
    AVSampleFormat m_audioSampleFmt = AV_SAMPLE_FMT_FLTP;

    PushState mState;
    QString mLastError;

    // 配置参数
    QString mSourceUrl;
    QString mDestinationUrl;
    int mWidth = 1920;
    int mHeight = 1080;
    int mFrameRate = 30;
    int mBitRate = 2000;  // kbps

    // 统计信息
    qint64 mFrameCount = 0;
    qint64 mStartTime = 0;
};

#endif // RTSPPUSHER_H
