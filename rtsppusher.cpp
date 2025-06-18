// RTSPPusher.cpp
#include "rtsppusher.h"
#include "codethread.h"
#include <QDateTime>
#include "Logger.h"
#include "audioprocessor.h"

RTSPPusher::RTSPPusher(QObject* parent)
    : QObject(parent)
    , mState(PushState::none)
{
    qRegisterMetaType<PushState>("PushState");
}

RTSPPusher::~RTSPPusher()
{
    stop();
}

void RTSPPusher::initNewThread() {
    // 清理旧的线程（如果存在）
    cleanupThread();

    // 创建新的线程
    mPusherThread = new CodeThread(this);
    mPusherThread->setSourceUrl(mSourceUrl);
    mPusherThread->setDestinationUrl(mDestinationUrl);
    mPusherThread->setVideoSize(mWidth, mHeight);
    mPusherThread->setFramerate(mFrameRate);
    mPusherThread->setBitrate(mBitRate * 1000);  // 转换为bps

    // 连接信号槽
    connect(mPusherThread, &CodeThread::stateChanged,
            this, &RTSPPusher::handleThreadState);
    connect(mPusherThread, &CodeThread::error,
            this, &RTSPPusher::handleThreadError);
    connect(mPusherThread, &CodeThread::frameProcessed,
            this, &RTSPPusher::handleFrameProcessed);
}


void RTSPPusher::cleanupThread()
{
    if (mPusherThread) {
       mPusherThread->stop();  // 请求停止
       mPusherThread->wait();  // 等待线程结束
       delete mPusherThread;
       mPusherThread = nullptr;
   }
}

void RTSPPusher::setSource(const QString& url)
{
    if (mState == PushState::play) {
        LogErr<< "【RTSP推流器】无法在推流时设置源流地址";
        return;
    }
    mSourceUrl = url;
}

void RTSPPusher::setDestination(const QString& url)
{
    if (mState == PushState::play) {
        LogErr<< "【RTSP推流器】无法在推流时设置目标流地址";
        return;
    }
    mDestinationUrl = url;
}

void RTSPPusher::setVideoSize(int width, int height)
{
    if (mState == PushState::play) {
        LogErr<< "【RTSP推流器】无法在推流时设置推流分辨率";
        return;
    }
    mWidth = width;
    mHeight = height;
}

void RTSPPusher::setFrameRate(int fps)
{
    if (mState == PushState::play) {
        LogErr<< "【RTSP推流器】无法在推流时设置帧率";
        return;
    }
    mFrameRate = fps;
}

void RTSPPusher::setBitRate(int kbps)
{
    if (mState == PushState::play) {
        LogErr<< "【RTSP推流器】无法在推流时设置比特率";
        return;
    }
    mBitRate = kbps;
}

bool RTSPPusher::start()
{
    if (mState == PushState::play) {
        LogErr << "【RTSP推流器】推流器已启动";
        return false;
    }

    if (mSourceUrl.isEmpty() || mDestinationUrl.isEmpty()) {
        mLastError = "Source or destination URL not set";
        setState(PushState::error);
        return false;
    }
    // 初始化新的线程
    initNewThread();
    // 重置统计信息
    mFrameCount = 0;
    mStartTime = QDateTime::currentMSecsSinceEpoch();

    /// 初始化音频处理器
    if (!m_audioProcessor) {
        m_audioProcessor = new AudioProcessor(this);
        qRegisterMetaType<AVFrame*>("AVFrame*");

        // 连接音频上下文就绪信号
        connect(mPusherThread, &CodeThread::audioContextReady,
                this, [=](AVFormatContext* fmtCtx, AVCodecContext* codecCtx, AVStream* stream) {
            if (m_audioProcessor) {
                m_audioProcessor->setOutputContext(fmtCtx, codecCtx, stream);
                LogInfo << "【音频】成功设置音频输出上下文";
            }
        });

        // 连接音频时间戳更新信号到编码线程
        connect(m_audioProcessor, &AudioProcessor::audioTimestampUpdated,
                mPusherThread, &CodeThread::onAudioTimestampUpdated);

        // 连接同步状态变化信号
        connect(mPusherThread, &CodeThread::syncStatusChanged,
                this, [](bool inSync) {
            LogInfo << "【同步】音视频同步状态:" << (inSync ? "已同步" : "未同步");
        });

        if (!m_audioProcessor->initialize(m_audioSampleRate, m_audioChannels, m_audioSampleFmt)) {
            mLastError = "Failed to initialize audio processor";
            setState(PushState::error);
            return false;
        }

        connect(m_audioProcessor, &AudioProcessor::audioFrameAvailable,
                this, [this](AVFrame* frame) {
            if (mPusherThread) {
                QMetaObject::invokeMethod(mPusherThread, "encodeAudioFrame",
                                          Qt::QueuedConnection,
                                          Q_ARG(AVFrame*, frame));
            } else {
                av_frame_free(&frame);
            }
        });
    }

    // 启动线程
    mPusherThread->start();
    LogInfo << "【RTSP推流器】开始推流,目标地址：" << mDestinationUrl;

    // 启动音频采集
    if (m_audioProcessor) {
        m_audioProcessor->resetTimestamp();  // 重置时间戳
        m_audioProcessor->startCapture();
    }

    return true;
}

void RTSPPusher::stop()
{
    if (mState == PushState::play) {// 停止音频采集
        LogDebug << "停止音频采集";
        if (m_audioProcessor) {
            m_audioProcessor->stopCapture();
        }
        cleanupThread();
        setState(PushState::end);
        mDestinationUrl.clear();
    }
}

void RTSPPusher::handleThreadState(PushState state)
{
    switch (state) {
    case PushState::decode:
    {
        setState(PushState::decode);
        break;
    }
    case PushState::play:
    {
        setState(PushState::play);
        break;
    }
    case PushState::end:
    {
        setState(PushState::end);
        break;
    }
    case PushState::error:
    {
        setState(PushState::error);
        break;
    }
    case PushState::pause:
    {
        setState(PushState::pause);
        break;
    }
    case PushState::none:
    {
        setState(PushState::pause);
        break;
    }
    default:
        break;
    }
}

void RTSPPusher::handleThreadError(const QString& error)
{
    mLastError = error;
    LogErr<< "【RTSP推流器】推流器运行报错："<<mLastError;
    emit this->error(error);
    setState(PushState::error);
}

void RTSPPusher::handleFrameProcessed(qint64 frameCount)
{
    mFrameCount = frameCount;
    updateStatistics();
}

QString RTSPPusher::destinationUrl() const
{
    return mDestinationUrl;
}

void RTSPPusher::setState(PushState newState)
{
    if (mState != newState) {
        mState = newState;
        emit stateChanged(this->objectName(),mState);//更新状态
    }
}

void RTSPPusher::updateStatistics()
{
    // 计算实际比特率
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 duration = currentTime - mStartTime;
    if (duration > 0) {
        qint64 actualBitrate = (mFrameCount * mBitRate * 1000) / duration;
        emit statistics(mFrameCount, actualBitrate);
    }
}
