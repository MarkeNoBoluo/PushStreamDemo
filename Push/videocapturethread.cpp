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
    m_sourceUrl = sourceUrl;
    m_width = width;
    m_height = height;
    m_fps = fps;
    return true;
}

void VideoCaptureThread::run() {
    m_running = true;
    m_formatCtx = avformat_alloc_context();
#ifdef  Q_OS_WIN
    AVInputFormat* inputFmt = av_find_input_format("gdigrab");//获取GDI屏幕录制设备
#else
    AVInputFormat* inputFmt = av_find_input_format("x11grab");//获取X11屏幕录制设备
#endif
    if (!inputFmt) {
        emit errorOccurred("无法找到输入格式");
        return;
    }
    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", QString::number(m_fps).toUtf8().data(), 0);
    av_dict_set(&options, "video_size", QString("%1x%2").arg(m_width).arg(m_height).toUtf8().data(), 0);

    if (avformat_open_input(&m_formatCtx, m_sourceUrl.toUtf8().data(), inputFmt, &options) < 0) {
        emit errorOccurred("打开视频源失败");
        return;
    }
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        emit errorOccurred("查找流信息失败");
        return;
    }

    // 查找视频流
    m_videoStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIndex < 0) {
        emit errorOccurred("未找到视频流");
        return;
    }

    AVCodecParameters* codecPar = m_formatCtx->streams[m_videoStreamIndex]->codecpar;
    AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, codecPar);
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        emit errorOccurred("打开解码器失败");
        return;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (m_running) {
        if (av_read_frame(m_formatCtx, pkt) >= 0 && pkt->stream_index == m_videoStreamIndex) {
            if (avcodec_send_packet(m_codecCtx, pkt) == 0) {
                while (avcodec_receive_frame(m_codecCtx, frame) == 0) {
                    AVFrame* cloned = av_frame_clone(frame);
                    emit videoFrameAvailable(cloned);
                }
            }
        }
        av_packet_unref(pkt);
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_close(m_codecCtx);
    avcodec_free_context(&m_codecCtx);
    avformat_close_input(&m_formatCtx);
    m_running = false;
}

void VideoCaptureThread::stopCapture() {
    m_running = false;
    wait();
}
