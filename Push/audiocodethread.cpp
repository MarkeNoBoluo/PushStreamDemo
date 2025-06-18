#include "audiocodethread.h"

AudioCodeThread::AudioCodeThread(QObject* parent)
    : QThread(parent)
{

}

AudioCodeThread::~AudioCodeThread()
{
    stopEncoding();
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
}

bool AudioCodeThread::initialize(AVFormatContext* fmtCtx, int sampleRate, int channels) {
    // ...初始化音频编码器...
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) return false;
    m_codecCtx = avcodec_alloc_context3(codec);
    m_codecCtx->sample_rate = sampleRate;
    m_codecCtx->channels = channels;
    m_codecCtx->channel_layout = av_get_default_channel_layout(channels);
    m_codecCtx->sample_fmt = codec->sample_fmts[0];
    m_codecCtx->bit_rate = 64000;
    m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    m_codecCtx->time_base = {1, sampleRate};

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_stream = avformat_new_stream(fmtCtx, codec);
    if (!m_stream) return false;
    m_stream->time_base = {1, sampleRate};
    avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx);

    m_swrCtx = swr_alloc_set_opts(nullptr,
                                  av_get_default_channel_layout(channels), m_codecCtx->sample_fmt, sampleRate,
                                  av_get_default_channel_layout(channels), AV_SAMPLE_FMT_S16, sampleRate,
                                  0, nullptr);
    if (!m_swrCtx || swr_init(m_swrCtx) < 0) {
        return false;
    }
    m_running = true;
    m_pts = 0;
    return true;
}

void AudioCodeThread::addAudioData(const QByteArray& data) {
    QMutexLocker locker(&m_mutex);
    m_audioBuffer.append(data);
    m_cond.wakeAll();
}

void AudioCodeThread::run() {
    while (m_running) {
        QMutexLocker locker(&m_mutex);
        if (m_audioBuffer.isEmpty()) {
            m_cond.wait(&m_mutex);
            continue;
        }

        // 处理音频数据并编码...
        // 假设每帧为 m_codecCtx->frame_size * channels * 2 字节
        int frameBytes = m_codecCtx->frame_size * m_codecCtx->channels * 2;
        if (m_audioBuffer.size() < frameBytes) {
            m_cond.wait(&m_mutex, 10);
            continue;
        }
        QByteArray frameData = m_audioBuffer.left(frameBytes);
        m_audioBuffer.remove(0, frameBytes);

        // 构造 AVFrame
        AVFrame* frame = av_frame_alloc();
        frame->nb_samples = m_codecCtx->frame_size;
        frame->channel_layout = m_codecCtx->channel_layout;
        frame->format = m_codecCtx->sample_fmt;
        frame->sample_rate = m_codecCtx->sample_rate;

        av_frame_get_buffer(frame, 0);

        const uint8_t* src = reinterpret_cast<const uint8_t*>(frameData.constData());
        //重采样
        swr_convert(m_swrCtx, frame->data, frame->nb_samples, &src, frame->nb_samples);

        frame->pts = m_pts;
        m_pts += frame->nb_samples;

        // 编码
        if (avcodec_send_frame(m_codecCtx, frame) == 0) {
            AVPacket* pkt = av_packet_alloc();
            av_init_packet(pkt);
            while (avcodec_receive_packet(m_codecCtx, pkt) == 0) {
                emit packetEncoded(pkt); // 发送给主线程或推流线程
                pkt = av_packet_alloc();
                av_init_packet(pkt);
            }
            av_packet_free(&pkt);
        }
        emit audioPtsUpdated(frame->pts);
        av_frame_free(&frame);
    }
}

void AudioCodeThread::stopEncoding() {
    m_running = false;
    m_cond.wakeAll();
    wait();
}
