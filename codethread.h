// CodeThread.h
#ifndef CodeThread_H
#define CodeThread_H

#include <QThread>
#include <QString>
#include <QMutex>
#include <QUrl>
#include <QQueue>
#include <QFileInfo>
#include "DataStruct.h"
#include <QWaitCondition>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>
}

class CodeThread : public QThread {
    Q_OBJECT
public:
    explicit CodeThread(QObject* parent = nullptr);
    ~CodeThread();

    // 配置方法
    void setSourceUrl(const QString& url) { mSrcUrl = url; }
    void setDestinationUrl(const QString& url);
    void setVideoSize(int width, int height) {
        mDstVideoWidth = width;
        mDstVideoHeight = height;
    }
    void setBitrate(int bitrate) {
        mBitrate = bitrate;
        mMaxBitrate = (int)(mBitrate * 1.5);  // 最大比特率提高50%
        mMinBitrate = (int)(mBitrate * 0.5);  // 最小比特率降低50%
        mBufferSize = mMaxBitrate;            // 缓冲区大小等于最大比特率（1秒缓冲）
    }
    void setFramerate(int fps) { mDstVideoFps = fps; }
    void setRateControl(const QString& mode) { mRateControl = mode; }

    AVFormatContext *dstFmtCtx() const;

    AVCodecContext *audioCodecCtx() const;

    AVStream *audioStream() const;
    void init();

public slots:
    void stop();
    void encodeAudioFrame(AVFrame *frame);
    void onAudioTimestampUpdated(int64_t audioPts);

signals:
    void stateChanged(PushState state);
    void error(const QString& message);
    void frameProcessed(qint64 frameCount);
    void audioContextReady(AVFormatContext* fmtCtx, AVCodecContext* codecCtx, AVStream* stream);
    void syncStatusChanged(bool inSync);

protected:
    void run() override;

private:
    bool initializeSource();
    bool initializeDestination();
    bool setupEncoderContext();
    bool processNextFrame(AVFrame* srcFrame, AVFrame* dstFrame, SwsContext* swsCtx);
    void cleanup();
    bool inferOutputFormat();
    bool handleFFmpegError(int errorCode, const QString& operation);    // 统一的错误处理函数
    void synchronizeFrames();
private:
    // 基本配置
    QString mSrcUrl;
    QString mDstUrl;
    volatile bool mRunning = false;
    QMutex mMutex;

    // FFmpeg上下文
    AVInputFormat* mInputFormat = nullptr;
    AVFormatContext* mSrcFmtCtx = nullptr;
    AVFormatContext* mDstFmtCtx = nullptr;
    AVCodecContext* mSrcVideoCodecCtx = nullptr;
    AVCodecContext* mDstVideoCodecCtx = nullptr;
    AVStream* mSrcVideoStream = nullptr;
    AVStream* mDstVideoStream = nullptr;

    // 添加音频编码相关成员
    AVCodecContext* m_audioCodecCtx = nullptr;
    AVStream* m_audioStream = nullptr;
    int m_audioIndex = -1;

    // 音频参数
    int m_audioSampleRate = 44100;
    int m_audioChannels = 2;
    AVSampleFormat m_audioSampleFmt = AV_SAMPLE_FMT_FLTP;

    // 默认视频参数
    int mSrcVideoIndex = -1;
    int mDstVideoIndex = -1;
    int mSrcVideoWidth = 2560;
    int mSrcVideoHeight = 1600;
    int mDstVideoWidth = 2560;
    int mDstVideoHeight = 1600;
    int mDstVideoFps = 30;

    int mBitrate = 2000000;         // 目标比特率
    int mMaxBitrate = 6000000;      // 最大比特率 (bps)
    int mMinBitrate = 2000000;      // 最小比特率 (bps)
    int mBufferSize = 6000000;      // 缓冲区大小
    QString mRateControl = "cbr";    // 比特率控制模式：cbr/vbr/abr

    // 错误计数
    int mErrorCount = 0;
    static const int MAX_ERROR_COUNT = 5;

    QString mOutputFormat;

    // 添加音视频同步相关
    int64_t m_audioBasePts = 0;          // 音频基准PTS
    int64_t m_videoBasePts = 0;          // 视频基准PTS
    bool m_syncInitialized = false;          // 同步是否已初始化
    QMutex m_syncMutex;              // 同步锁

    // 帧缓冲队列用于同步
    QQueue<AVFrame*> m_videoFrameQueue;
    QQueue<AVFrame*> m_audioFrameQueue;
    QMutex m_queueMutex;

    bool m_waitingForFirstAudioFrame = true;  // 等待第一个音频帧
    int64_t m_firstAudioPts = AV_NOPTS_VALUE; // 第一个音频帧的PTS
    int64_t m_firstVideoPts = AV_NOPTS_VALUE; // 第一个视频帧的PTS
    static const int64_t MAX_WAIT_TIME_US = 1000000; // 最大等待时间5秒
    int64_t m_startWaitTime = 0; // 开始等待的时间
    int64_t m_videoFrameCount = 0;  // 实际编码的视频帧计数
    int64_t m_lastDiffMs = 0; // 记录上次的音视频差异

    static const int64_t SYNC_THRESHOLD_MS = 25;   // 同步阈值25ms
    static const int64_t SYNC_MAX_WAIT_MS = 1000;  // 最大等待时间1000ms

    // 等待音频同步的条件变量机制
    QWaitCondition m_syncWaitCond;
    QMutex m_syncWaitMutex;

};

#endif // CodeThread_H
