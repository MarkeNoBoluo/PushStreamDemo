#include "videocodethread.h"



VideoCodeThread::VideoCodeThread(QObject *parent)
{

}

VideoCodeThread::~VideoCodeThread()
{
    stopEncoding();
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
}

bool VideoCodeThread::initialize(AVFormatContext *fmtCtx, int width, int height, int fps, int bitrate)
{
    // 初始化编码器
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) return false;
    m_codecCtx = avcodec_alloc_context3(codec);
    m_codecCtx->width = width;
    m_codecCtx->height = height;
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_codecCtx->time_base = {1, fps};
    m_codecCtx->framerate = {fps, 1};
    m_codecCtx->bit_rate = bitrate;
    m_codecCtx->gop_size = 30;
    m_codecCtx->max_b_frames = 0;
    m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_stream = avformat_new_stream(fmtCtx, codec);
    if (!m_stream) return false;
    m_stream->time_base = m_codecCtx->time_base;
    avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx);

    m_swsCtx = sws_getContext(width, height, AV_PIX_FMT_BGR0, // 假设采集到的是BGR0
                              width, height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);

    m_running = true;
    return true;
}

void VideoCodeThread::addVideoFrame(AVFrame *frame)
{
    QMutexLocker locker(&m_mutex);
    m_frameQueue.enqueue(frame);
}

void VideoCodeThread::stopEncoding()
{
    m_running = false;
    wait();
}

void VideoCodeThread::run()
{
    while (m_running) {
        m_mutex.lock();
        if (m_frameQueue.isEmpty()) {
            m_mutex.unlock();
            msleep(5);
            continue;
        }
        AVFrame* srcFrame = m_frameQueue.dequeue();
        m_mutex.unlock();

        // 转换为YUV420P
        AVFrame* yuvFrame = av_frame_alloc();
        yuvFrame->format = m_codecCtx->pix_fmt;
        yuvFrame->width = m_codecCtx->width;
        yuvFrame->height = m_codecCtx->height;
        av_frame_get_buffer(yuvFrame, 0);

        sws_scale(m_swsCtx,
                  srcFrame->data, srcFrame->linesize,
                  0, m_codecCtx->height,
                  yuvFrame->data, yuvFrame->linesize);

        yuvFrame->pts = srcFrame->pts;

        // 编码
        if (avcodec_send_frame(m_codecCtx, yuvFrame) == 0) {
            AVPacket pkt;
            av_init_packet(&pkt);
            pkt.data = nullptr;
            pkt.size = 0;
            while (avcodec_receive_packet(m_codecCtx, &pkt) == 0) {
                pkt.stream_index = m_stream->index;
                emit packetEncoded(&pkt);
                av_packet_unref(&pkt);
            }
        }
        av_frame_free(&yuvFrame);
        av_frame_free(&srcFrame);
    }
}
