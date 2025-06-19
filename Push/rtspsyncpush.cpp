#include "rtspsyncpush.h"

#include "audiocapturethread.h"
#include "videocapturethread.h"
#include "audiocodethread.h"
#include "videocodethread.h"
#include "streampushthread.h"

#include "Logger.h"

RTSPSyncPush::RTSPSyncPush(QObject* parent)
    : QObject(parent)
{
    avformat_network_init();//初始化网络
    avdevice_register_all();//初始化ffmpeg
    // 实例化线程对象
    m_audioCapThread = new AudioCaptureThread(this);
    m_audioCodeThread = new AudioCodeThread(this);
    m_videoCapThread = new VideoCaptureThread(this);
    m_videoCodeThread = new VideoCodeThread(this);
    m_streamPushThread = new StreamPushThread( this);
}

RTSPSyncPush::~RTSPSyncPush()
{
    stop();
}

bool RTSPSyncPush::initialize(const QString &videoSrc, int videoW, int videoH, int videoFps, int videoBitrate, int audioSampleRate, int audioChannels, const QString &rtspUrl)
{
    stop(); // 先停止当前推流并释放资源

    m_videoSrc = videoSrc;
    m_videoW = videoW;
    m_videoH = videoH;
    m_videoFps = videoFps;
    m_videoBitrate = videoBitrate;
    m_audioSampleRate = audioSampleRate;
    m_audioChannels = audioChannels;
    m_rtspUrl = rtspUrl;

    // 创建推流输出上下文
    avformat_network_init();
    if (avformat_alloc_output_context2(&m_fmtCtx, nullptr, "rtsp", m_rtspUrl.toUtf8().data()) < 0) {
        emit error("创建RTSP输出上下文失败");
        return false;
    }

    //设置输出上下文
    m_streamPushThread->setFmtCtx(m_fmtCtx);

    // 初始化采集和编码线程
    if (!m_audioCapThread->initialize(m_audioSampleRate, m_audioChannels) ||
        !m_audioCodeThread->initialize(m_fmtCtx, m_audioSampleRate, m_audioChannels) ||
        !m_videoCapThread->initialize(m_videoSrc, m_videoW, m_videoH, m_videoFps) ||
        !m_videoCodeThread->initialize(m_fmtCtx, m_videoW, m_videoH, m_videoFps, m_videoBitrate)) {
        emit error("线程初始化失败");
        return false;
    }

    // 断开可能存在的连接信号
    disconnect(m_audioCapThread,nullptr,this,nullptr);
    disconnect(m_videoCapThread,nullptr,this,nullptr);
    disconnect(m_audioCodeThread,nullptr,this,nullptr);
    disconnect(m_videoCodeThread,nullptr,this,nullptr);

    // 信号槽连接
    connect(m_audioCapThread, &AudioCaptureThread::audioDataAvailable,
            this, &RTSPSyncPush::onAudioDataAvailable, Qt::QueuedConnection);

    connect(m_videoCapThread, &VideoCaptureThread::videoFrameAvailable,
            this, &RTSPSyncPush::onVideoFrameAvailable, Qt::QueuedConnection);

    connect(m_audioCodeThread, &AudioCodeThread::packetEncoded,
        m_streamPushThread, [this](AVPacket* pkt) {

            if (m_audioCodeThread && m_audioCodeThread->codecCtx() && m_audioCodeThread->stream()) {
                av_packet_rescale_ts(pkt,
                                     m_audioCodeThread->codecCtx()->time_base,
                                     m_audioCodeThread->stream()->time_base);
            }
            m_streamPushThread->addPacket(pkt, false);
        }, Qt::QueuedConnection);

    connect(m_videoCodeThread, &VideoCodeThread::packetEncoded,
        m_streamPushThread, [this](AVPacket* pkt) {
            if (m_videoCodeThread && m_videoCodeThread->codecCtx() && m_videoCodeThread->stream()) {
                av_packet_rescale_ts(pkt,
                                     m_videoCodeThread->codecCtx()->time_base,
                                     m_videoCodeThread->stream()->time_base);
            }
            m_streamPushThread->addPacket(pkt, true);
        }, Qt::QueuedConnection);

    connect(m_streamPushThread, &StreamPushThread::errorOccurred,
            this, &RTSPSyncPush::error, Qt::QueuedConnection);

    return true;
}

void RTSPSyncPush::setVideoParam(const QString &videoSrc, int videoW, int videoH, int videoFps, int videoBitrate)
{
    m_videoSrc = videoSrc;
    m_videoW = videoW;
    m_videoH = videoH;
    m_videoFps = videoFps;
    m_videoBitrate = videoBitrate;
}

void RTSPSyncPush::setAudioParam(int audioSampleRate, int audioChannels, int audioSamepleSize)
{
    m_audioSampleRate = audioSampleRate;
    m_audioChannels = audioChannels;
    m_audioSampleSize = audioSamepleSize;
}

void RTSPSyncPush::start() {
    if (m_running)
        return;

    // 确保格式上下文已经创建
    if (!m_fmtCtx) {
        emit error("格式上下文未初始化，请先调用 initialize()");
        return;
    }

    int ret = -1;
    //打开输出流
    LogDebug << "flag1:"<<m_fmtCtx->oformat->flags <<"AVFMT_NOFILE:"<<AVFMT_NOFILE;
    if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_fmtCtx->pb, m_rtspUrl.toUtf8().data(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            emit error("打开RTSP输出失败");
            // 如果写头失败，需要关闭已打开的输出流
            if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE) && m_fmtCtx->pb) {
                avio_closep(&m_fmtCtx->pb);
            }
            return;
        }
    }else{
        LogDebug << "没有打开输入上下文";
    }
    LogDebug << "flag2:"<<m_fmtCtx->oformat->flags <<"AVFMT_NOFILE:"<<AVFMT_NOFILE;

    // 写文件头
    ret = avformat_write_header(m_fmtCtx, nullptr);
    if (ret < 0) {
        emit error("写入文件头失败:"+QString::number(ret));
        return;
    }

    m_running = true;
    // 启动所有线程
    m_audioCapThread->start();
    m_audioCodeThread->start();
    m_videoCapThread->start();
    m_videoCodeThread->start();
    m_streamPushThread->start();
}

void RTSPSyncPush::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_streamPushThread) {
        m_streamPushThread->stopPushing();
        m_streamPushThread->wait(); // 阻塞直到真正退出
    }

    // 停止线程
    if (m_audioCapThread) { m_audioCapThread->stopCapture(); /*m_audioCapThread->quit(); m_audioCapThread->wait();*/ }
    if (m_audioCodeThread) { m_audioCodeThread->stopEncoding(); /*m_audioCodeThread->quit(); m_audioCodeThread->wait();*/ }
    if (m_videoCapThread) { m_videoCapThread->stopCapture(); /*m_videoCapThread->quit(); m_videoCapThread->wait();*/ }
    if (m_videoCodeThread) { m_videoCodeThread->stopEncoding(); /*m_videoCodeThread->quit(); m_videoCodeThread->wait();*/ }

    // 等待推流线程完全停止后再清理格式上下文
    if (m_streamPushThread && m_streamPushThread->isRunning()) {
        m_streamPushThread->wait(3000); // 最多等待3秒
    }

    if (m_fmtCtx) {
        // 只有当输出流存在时才写trailer
        if (m_fmtCtx->pb) {
            av_write_trailer(m_fmtCtx);
        }

        if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE) && m_fmtCtx->pb) {
            avio_closep(&m_fmtCtx->pb);
        }
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
        m_streamPushThread->setFmtCtx(nullptr);  // 避免访问已释放的指针
    }
}

void RTSPSyncPush::onVideoFrameAvailable(AVFrame *frame)
{
    if (m_videoCodeThread) {
        m_videoCodeThread->addVideoFrame(frame);
    }
}

void RTSPSyncPush::onAudioDataAvailable(const QByteArray &data)
{
    // 采集线程采集到的原始音频数据
    if (m_audioCodeThread) {
        m_audioCodeThread->addAudioData(data);
    }
}

void RTSPSyncPush::setRtspUrl(const QString &newRtspUrl)
{
    m_rtspUrl = newRtspUrl;
}

PushState RTSPSyncPush::state() const
{
    return m_state;
}

void RTSPSyncPush::setState(const PushState &newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(this->objectName(),m_state);//更新状态
    }
}


