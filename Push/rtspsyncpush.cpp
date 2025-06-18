#include "rtspsyncpush.h"

#include "audiocapturethread.h"
#include "videocapturethread.h"
#include "audiocodethread.h"
#include "videocodethread.h"

#include "Logger.h"

RTSPSyncPush::RTSPSyncPush(QObject* parent)
    : QObject(parent)
{
    avformat_network_init();//初始化网络
    avdevice_register_all();//初始化ffmpeg
}

RTSPSyncPush::~RTSPSyncPush()
{
    stop();
}

bool RTSPSyncPush::initialize(const QString &videoSrc, int videoW, int videoH, int videoFps, int videoBitrate, int audioSampleRate, int audioChannels, const QString &rtspUrl)
{
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

    // 实例化线程对象
    m_audioCapThread = new AudioCaptureThread();
    m_audioCodeThread = new AudioCodeThread();
    m_videoCapThread = new VideoCaptureThread();
    m_videoCodeThread = new VideoCodeThread();

    // 初始化采集和编码线程
    m_audioCapThread->initialize(m_audioSampleRate, m_audioChannels);
    m_audioCodeThread->initialize(m_fmtCtx, m_audioSampleRate, m_audioChannels);
    m_videoCapThread->initialize(m_videoSrc, m_videoW, m_videoH, m_videoFps);
    m_videoCodeThread->initialize(m_fmtCtx, m_videoW, m_videoH, m_videoFps, m_videoBitrate);

    // 信号槽连接
    connect(m_audioCapThread, &AudioCaptureThread::audioDataAvailable,
            this, &RTSPSyncPush::onAudioDataAvailable,Qt::QueuedConnection);

    connect(m_audioCodeThread, &AudioCodeThread::packetEncoded,
            this, &RTSPSyncPush::onEncodedAudioPacket,Qt::QueuedConnection);

    connect(m_videoCapThread, &VideoCaptureThread::videoFrameAvailable,
            this, &RTSPSyncPush::onVideoFrameAvailable,Qt::QueuedConnection);

    connect(m_videoCodeThread, &VideoCodeThread::packetEncoded,
            this, &RTSPSyncPush::onEncodedVideoPacket,Qt::QueuedConnection);

    // 也可以连接 errorOccurred 以及其它信号

    return true;
}

void RTSPSyncPush::start() {
    if (m_running)
        return;
    m_running = true;
    m_audioCapThread->start();
    m_audioCodeThread->start();
    m_videoCapThread->start();
    m_videoCodeThread->start();

    // 写文件头
    if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_fmtCtx->pb, m_rtspUrl.toUtf8().data(), AVIO_FLAG_WRITE) < 0) {
            emit error("打开RTSP输出失败");
            return;
        }
    }
    if (avformat_write_header(m_fmtCtx, nullptr) < 0) {
        emit error("写入文件头失败");
        return;
    }
}

void RTSPSyncPush::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_audioCapThread) { m_audioCapThread->stopCapture(); m_audioCapThread->deleteLater(); m_audioCapThread = nullptr; }
    if (m_audioCodeThread) { m_audioCodeThread->stopEncoding(); m_audioCodeThread->deleteLater(); m_audioCodeThread = nullptr; }
    if (m_videoCapThread) { m_videoCapThread->stopCapture(); m_videoCapThread->deleteLater(); m_videoCapThread = nullptr; }
    if (m_videoCodeThread) { m_videoCodeThread->stopEncoding(); m_videoCodeThread->deleteLater(); m_videoCodeThread = nullptr; }

    if (m_fmtCtx) {
        av_write_trailer(m_fmtCtx);
        if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE) && m_fmtCtx->pb) {
            avio_closep(&m_fmtCtx->pb);
        }
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
    }
}

void RTSPSyncPush::onVideoFrameAvailable(AVFrame *frame)
{
    if (m_videoCodeThread) {
        m_videoCodeThread->addVideoFrame(frame);
    }
}

void RTSPSyncPush::onEncodedVideoPacket(AVPacket *pkt)
{
    QMutexLocker locker(&m_mutex);
    pushPacket(pkt, true);
}

void RTSPSyncPush::onAudioDataAvailable(const QByteArray &data)
{
    // 采集线程采集到的原始音频数据
    if (m_audioCodeThread) {
        m_audioCodeThread->addAudioData(data);
    }
}

void RTSPSyncPush::onEncodedAudioPacket(AVPacket *pkt)
{
    QMutexLocker locker(&m_mutex);
    pushPacket(pkt, false);
}

void RTSPSyncPush::pushPacket(AVPacket *pkt, bool isVideo)
{
    // 简单同步控制可以在这里实现（根据 PTS 控制写入先后/丢帧等策略）
    if (!pkt || !pkt->data || pkt->size <= 0) {
        av_packet_free(&pkt); // 确保释放
        return;
    }
    if (av_interleaved_write_frame(m_fmtCtx, pkt) < 0) {
        emit error("推流包写入失败");
    }
    QString txt = isVideo?"视频包":"音频包";
    LogDebug << "写入"<<txt;
    av_packet_free(&pkt); // 彻底释放，不用 unref
}

