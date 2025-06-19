#include "streampushthread.h"

StreamPushThread::StreamPushThread( QObject* parent)
    : QThread(parent), m_fmtCtx(nullptr), m_running(false)
{
}

StreamPushThread::~StreamPushThread()
{
    stopPushing();
}

void StreamPushThread::addPacket(AVPacket* pkt, bool isVideo)
{
    QMutexLocker locker(&m_mutex);
    if (isVideo) {
        m_videoQueue.enqueue(pkt);
    } else {
        m_audioQueue.enqueue(pkt);
    }
}

void StreamPushThread::stopPushing()
{
    m_running = false;
    wait();
    QMutexLocker locker(&m_mutex);
    while (!m_videoQueue.isEmpty()) {
        AVPacket* pkt = m_videoQueue.dequeue();
        av_packet_free(&pkt);
    }
    while (!m_audioQueue.isEmpty()) {
        AVPacket* pkt = m_audioQueue.dequeue();
        av_packet_free(&pkt);
    }
}

void StreamPushThread::run()
{
    m_running = true;
    while (m_running) {
        AVPacket* pkt = nullptr;
        {
            QMutexLocker locker(&m_mutex);
            if (!m_videoQueue.isEmpty() && !m_audioQueue.isEmpty()) {
                AVPacket* videoPkt = m_videoQueue.head();
                AVPacket* audioPkt = m_audioQueue.head();
                // 比较 PTS，选择较早的包
                if (av_compare_ts(videoPkt->pts, m_fmtCtx->streams[videoPkt->stream_index]->time_base,
                                  audioPkt->pts, m_fmtCtx->streams[audioPkt->stream_index]->time_base) <= 0) {
                    pkt = m_videoQueue.dequeue();
                } else {
                    pkt = m_audioQueue.dequeue();
                }
            } else if (!m_videoQueue.isEmpty()) {
                pkt = m_videoQueue.dequeue();
            } else if (!m_audioQueue.isEmpty()) {
                pkt = m_audioQueue.dequeue();
            }
        }

        if (pkt) {
            int ret = av_interleaved_write_frame(m_fmtCtx, pkt);
            if (ret < 0) {
                emit errorOccurred("推流失败: " + QString::number(ret));
            }
            av_packet_free(&pkt);
        } else {
            msleep(1); // 避免忙等待
        }
    }
}

AVFormatContext *StreamPushThread::fmtCtx() const
{
    return m_fmtCtx;
}

void StreamPushThread::setFmtCtx(AVFormatContext *newFmtCtx)
{
    m_fmtCtx = newFmtCtx;
}
