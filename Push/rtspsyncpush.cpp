#include "rtspsyncpush.h"

#include "audiocapturethread.h"
#include "videocapturethread.h"
#include "audiocodethread.h"
#include "videocodethread.h"

#include "Logger.h"

RTSPSyncPush::RTSPSyncPush(QObject* parent) : QObject(parent) {
    m_audioCapture = new AudioCaptureThread(this);
    m_videoCapture = new VideoCaptureThread(this);
    m_audioCodec = new AudioCodeThread(this);
    m_videoCodec = new VideoCodeThread(this);
}

bool RTSPSyncPush::start(const QString& rtspUrl, int width, int height, int fps, int bitrate) {
    if (!initializeRTSP(rtspUrl)) return false;

    // 初始化各组件
    m_audioCapture->initialize(44100, 2);
    m_videoCapture->initialize("desktop", width, height, fps);

    m_audioCodec->initialize(m_formatCtx, 44100, 2);
    m_videoCodec->initialize(m_formatCtx, width, height, fps, bitrate);

    // 连接信号槽
    connect(m_audioCapture, &AudioCaptureThread::audioDataAvailable,
            m_audioCodec, &AudioCodeThread::addAudioData);

    connect(m_videoCapture, &VideoCaptureThread::videoFrameAvailable,
            m_videoCodec, &VideoCodeThread::addVideoFrame);

    // 启动所有线程
    m_audioCapture->start();
    m_videoCapture->start();
    m_audioCodec->start();
    m_videoCodec->start();

    return true;
}

void RTSPSyncPush::stop() {
    m_audioCapture->stopCapture();
    m_videoCapture->stopCapture();
    m_audioCodec->stopEncoding();
    m_videoCodec->stopEncoding();
    cleanup();
}

bool RTSPSyncPush::initializeRTSP(const QString &url)
{

}

void RTSPSyncPush::cleanup()
{

}
