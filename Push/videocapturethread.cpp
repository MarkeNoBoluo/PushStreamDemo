#include "videocapturethread.h"
#include "Logger.h"

VideoCaptureThread::VideoCaptureThread(QObject *parent)
    : QThread{parent}
{

}

VideoCaptureThread::~VideoCaptureThread() {
    stopCapture();
}

bool VideoCaptureThread::initialize(const QString& sourceUrl, int width, int height, int fps) {
    // ...初始化FFmpeg相关资源...
    return true;
}

void VideoCaptureThread::run() {
    AVFrame* frame = av_frame_alloc();
    AVPacket packet;

    while (m_running) {
        if (av_read_frame(m_formatCtx, &packet) >= 0) {
            if (packet.stream_index == m_videoStreamIndex) {
                // 解码和处理帧...
                emit videoFrameAvailable(frame);
            }
            av_packet_unref(&packet);
        }
    }

    av_frame_free(&frame);
}

void VideoCaptureThread::stopCapture() {
    QMutexLocker locker(&m_mutex);
    m_running = false;
    wait();
    // 清理FFmpeg资源...
}
