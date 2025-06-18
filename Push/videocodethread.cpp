#include "videocodethread.h"
#include "Logger.h"


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
    // 释放历史资源，避免重复初始化
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    m_stream = nullptr;
    // 初始化编码器
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        LogErr << ("找不到H264编码器");
        return false;
    }
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        LogErr << ("无法分配编码器上下文");
        return false;
    }
    m_codecCtx->width = width;
    m_codecCtx->height = height;
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_codecCtx->time_base = {1, fps};
    m_codecCtx->framerate = {fps, 1};
    m_codecCtx->bit_rate = bitrate;
    m_codecCtx->gop_size = 30;
    m_codecCtx->max_b_frames = 0;
    m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 可选：参考CodeThread，设置码率控制参数
    m_codecCtx->rc_buffer_size = bitrate * 1.5;
    m_codecCtx->rc_max_rate = bitrate * 1.5;
    m_codecCtx->rc_min_rate = bitrate * 0.5;

    AVDictionary* codec_options = nullptr;

    // 基础预设
    av_dict_set(&codec_options, "preset", "superfast", 0);
    av_dict_set(&codec_options, "tune", "zerolatency", 0);
    // 固定比特率模式 (CBR)
    av_dict_set(&codec_options, "nal-hrd", "cbr", 0);    // CBR模式
    av_dict_set(&codec_options, "x264-params",
                QString("nal-hrd=cbr:force-cfr=1:vbv-maxrate=%1:vbv-bufsize=%2")
                    .arg(m_maxBitrate/1000)  // 转换为 kbps
                    .arg(m_minBitrate/1000)
                    .toStdString().c_str(), 0);

    if (avcodec_open2(m_codecCtx, codec, &codec_options) < 0) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
        LogErr << ("打开编码器失败");
        return false;
    }

    m_stream = avformat_new_stream(fmtCtx, codec);
    if (!m_stream) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
        LogErr << ("新建输出流失败");
        return false;
    }
    m_stream->time_base = m_codecCtx->time_base;

    if (avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx) < 0) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
        m_stream = nullptr;
        LogErr << ("参数拷贝失败");
        return false;
    }

    m_swsCtx = sws_getContext(width, height, AV_PIX_FMT_BGRA,
                              width, height, AV_PIX_FMT_YUV420P,
                              SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
        m_stream = nullptr;
        LogErr << ("SWS上下文创建失败");
        return false;
    }

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
    static int64_t video_frame_pts = 0;
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

        yuvFrame->pts = video_frame_pts++;
        LogDebug << "编码视频帧PTS:"<<yuvFrame->pts;
        // 编码
        if (avcodec_send_frame(m_codecCtx, yuvFrame) == 0) {
            AVPacket* pkt = av_packet_alloc();
            av_init_packet(pkt);
            while (avcodec_receive_packet(m_codecCtx, pkt) == 0) {
                pkt->stream_index = m_stream->index;
                emit packetEncoded(pkt);
                pkt = av_packet_alloc(); // 下一个
            }
        }
        av_frame_free(&yuvFrame);
        av_frame_free(&srcFrame);
    }
}

AVStream *VideoCodeThread::stream() const
{
    return m_stream;
}

AVCodecContext *VideoCodeThread::codecCtx() const
{
    return m_codecCtx;
}
