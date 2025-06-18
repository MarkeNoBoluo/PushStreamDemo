#include "audioprocessor.h"

AudioProcessor::AudioProcessor(QObject* parent)
    : QObject(parent)
{

}

AudioProcessor::~AudioProcessor() {
    stopCapture();
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }
}

bool AudioProcessor::initialize(int sampleRate, int channels, AVSampleFormat format) {
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_sampleFormat = format;

    // 初始化时间戳相关
    m_audioStartTime = 0;
    m_isFirstFrame = true;

    // 设置Qt音频格式
    m_audioFormat.setSampleRate(sampleRate);
    m_audioFormat.setChannelCount(channels);
    m_audioFormat.setSampleSize(16); // 16-bit PCM
    m_audioFormat.setCodec("audio/pcm");
    m_audioFormat.setByteOrder(QAudioFormat::LittleEndian);
    m_audioFormat.setSampleType(QAudioFormat::SignedInt);

    // 检查设备支持
    QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();
    if (!info.isFormatSupported(m_audioFormat)) {
        m_audioFormat = info.nearestFormat(m_audioFormat);
        LogInfo << "【音频】使用最接近的音频格式:"
                << "采样率:" << m_audioFormat.sampleRate()
                << "通道数:" << m_audioFormat.channelCount()
                << "采样大小:" << m_audioFormat.sampleSize();
    }

    m_swrCtx = swr_alloc_set_opts(nullptr,
                                  av_get_default_channel_layout(channels), AV_SAMPLE_FMT_FLTP, sampleRate, // 输出格式
                                  av_get_default_channel_layout(channels), AV_SAMPLE_FMT_S16, sampleRate, // 输入格式（QT采集默认S16）
                                  0, nullptr);
    if (!m_swrCtx || swr_init(m_swrCtx) < 0) {
        LogErr << "【音频】swr上下文初始化失败";
        return false;
    }


    return true;
}

void AudioProcessor::startCapture()
{
    if (!m_audioInput) {
        m_audioInput = new QAudioInput(m_audioFormat, this);
        m_audioDevice = new AudioInputDevice(this);
        m_audioDevice->open(QIODevice::WriteOnly);
        m_audioInput->start(m_audioDevice);
        LogInfo << "【音频】音频输入已启动";
    }
}

void AudioProcessor::stopCapture()
{
    if (m_audioInput) {
        m_audioInput->stop();
        delete m_audioInput;
        m_audioInput = nullptr;
        m_audioDevice = nullptr;
    }
}

void AudioProcessor::setOutputContext(AVFormatContext* fmtCtx, AVCodecContext* codecCtx, AVStream* stream)
{
    m_outputContext = fmtCtx;
    m_codecCtx = codecCtx;
    m_stream = stream;
}


void AudioProcessor::processAudioData(const char *data, qint64 len)
{
    if (!data || len <= 0 || !m_codecCtx) return;

    m_audioBuffer.append(data, len);
    int frameBytes = m_codecCtx->frame_size * m_channels * 2; // 16-bit

    while (m_audioBuffer.size() >= frameBytes) {
        QByteArray frameData = m_audioBuffer.left(frameBytes);
        m_audioBuffer.remove(0, frameBytes);
        sendFrame(frameData); // 封装成AVFrame，编码
    }
}

void AudioProcessor::resetTimestamp()
{
    QMutexLocker locker(&m_timestampMutex);
    m_isFirstFrame = true;
    m_pts = 0;
}

int64_t AudioProcessor::getCurrentAudioPts()
{
    QMutexLocker locker(&m_timestampMutex);
    return m_pts;
}


qint64 AudioProcessor::AudioInputDevice::writeData(const char *data, qint64 len)
{
    if (m_owner) {
        m_owner->processAudioData(data, len);
    }
    return len;
}

void AudioProcessor::sendFrame(const QByteArray& frameData)
{
    int inSamples = frameData.size() / (2 * m_channels);
    const uint8_t* inData = reinterpret_cast<const uint8_t*>(frameData.constData());

    AVFrame* frame = av_frame_alloc();
    frame->format = m_sampleFormat;
    frame->channel_layout = av_get_default_channel_layout(m_channels);
    frame->sample_rate = m_sampleRate;
    frame->nb_samples = inSamples;

    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return;
    }

    swr_convert(m_swrCtx, frame->data, frame->nb_samples, &inData, inSamples);

    {
        QMutexLocker locker(&m_timestampMutex);
        frame->pts = m_pts;
        m_pts += frame->nb_samples;
    }

    emit audioTimestampUpdated(frame->pts);
    emit audioFrameAvailable(frame);
}

